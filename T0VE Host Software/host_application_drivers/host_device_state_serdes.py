'''
Host Device State Serdes

This module is responsible for serializing and deserializing to/from a protobuf format. 
And also managing global state for the connected device. It will use PyPubSub to interface with the rest of the application after construction. 

This class will passed a device regex, default state polling rate in seconds (default 0.5), max state polling rate in seconds (default 0.1), receive timeout in seconds (default 5), and optional logger. 
It will instantiate a `host_device_serial` port using the regex and the logger.
NOTE: there's a bit of publishing mentioned in this description. I specifically want a cached copy of each topic maintained. A topic should only be published
      to if the value of that topic changes! This minimizes broker traffic and should hopefully improve the performance of the system. 

It will also instantiate a thread. The function of this thread is (transmit thread):
    - waits on the refresh_state_signal, falls through after the default gentle poll rate
    - if(connected to the serial port)
        - pulls from the command queue, if command queue is none, create an empty command (so we can pull status from the node)
        - serializes this command using `better_proto`
        - resets a `received_frame` flag 
        - writes this serialized command to the serial port
        - waits (with timeout) until `received_frame` is set
        - if we time out
            - check if we're still connected
            - if still connected, call the port's recover function
    - otherwise
        - flush the command queue
        - sleep for the default gentle poll rate

It also instantiates a second thread. This thread is (receive thread):
 - pulls a frame from the receive queue
    - if we got a frame, deserialize it using betterproto
    - if debug_message
        - extract the topic from the debug message enum
        - publish on the 'debug' topic (see below)
    - if state message
        - directly publish the protobuf NodeState message to the 'state' topic (see below)

And the third thread is (trigger/connect thread):
 - checks if we want to connect/disconnect the serial port
 - if refresh_state has been signaled, OR the command queue has something in it
    - assert the refresh_state_signal
 - report the port status and the command queue space

Fourth thread is (file request thread):
 - wait until the file request queue has something in it
 - pops a file request from the file request queue
 - serializes the file request using betterproto
 - writes the serialized file request to the serial port
 - waits (with timeout) until `received_file` is set
 - recover if we time out
 - handle file responses via the receive thread

Let's talk about message organization
 - app
    - devices
        -node##
            -port
                - command
                    - request_connect
                    - refresh_state
                - status
                    - connected
                    - port_name
                    - serial_number
                    - commands_enqueued
                    - command_queue_space
            -command <drop protobuf NodeState messages here to command, serialized and sent to device>
                \--> protobuf commands put into a message queue (will bound this to a fixed size, say 16 messages)
                \--> if queue is full, drop any messages attempting to be queued
            -status <publishes protobuf NodeState messages here received from device>
                \--> raw protobuf messages publishes to topic
            -file_request
                \--> protobuf file request messages put into a message queue (will bound this to a fixed size, say 16 messages)
                \--> if queue is full, drop any messages attempting to be queued
            -file_response
                \--> responses from file requests from the node published to topic
            -debug
                **message channel per `debug_level`, i.e.
                - info
                - warn
                - error

'''

from __future__ import annotations
from typing import Any, Optional
import threading
import logging
from pubsub import pub
import copy

from queue import Queue, Empty  #for command message queue
import betterproto

from host_application_drivers.host_device_serial import HostSerial                                          # lower layer serial driver
from host_application_drivers.state_proto_defs import Communication, NodeState, Debug, NeuralMemFileRequest # protobuf message definitions
from host_application_drivers.state_proto_node_default import NodeStateDefaults                             # protobuf message defaults

class HostDeviceStateSerdes:
    """
    Serialize/deserialize BetterProto `Communication` messages for a specific node and
    expose mirrored state via PyPubSub. Current design uses three threads:

    - Transmit: waits on `refresh_state_signal` or `default_poll_s`, sends local `NodeState`,
      and awaits an RX state message (else invokes `recover()` on timeout if still connected).
    - Receive: parses inbound frames; publishes `node_state` to `...state` and `debug_message` to `...debug`.
      Only `node_state` messages acknowledge TX.
    - Trigger/connect: reflects `...port.command.request_connect`, triggers `refresh_state_signal` when
      `...port.command.refresh_state` is true, and publishes `...port.status.{connected,port_name,serial_number}`.

    Notes:
    - Values publish only when changed.
    - TX intentionally polls at `default_poll_s` in absence of commands.
    - Auto-connect uses a case-sensitive serial-number regex; ensure device serials match formatting.
    """
    def __init__(
        self,
        *,
        node_index: str, 
        default_poll_s: float = 0.5,
        max_poll_s: float = 0.1,
        rx_timeout_s: float = 5.0,
        logger: Optional[logging.Logger] = None,
    ) -> None:

        #sanity check the node index, raise value error if not sane
        if(node_index not in ["0", "1", "2", "3", "4", "15", "Any"]):
            raise ValueError("Invalid Node index!")

        # identity / hierarchy
        self.node = "node_" + node_index.zfill(2)  #ensure two digits for node label
        self.root = f"app.devices.{self.node}"  # PyPubSub uses '.' separator by default

        self.log = logger or logging.getLogger(f"{__name__}.{self.node}.serdes")

        # lower layer port
        # NOTE: serial-number regex is case-sensitive; adjust upstream if device serials may differ in case.
        regex_map = {
            "0": r'^[0-9A-F]{24}_NODE_00$',
            "1": r'^[0-9A-F]{24}_NODE_01$',
            "2": r'^[0-9A-F]{24}_NODE_02$',
            "3": r'^[0-9A-F]{24}_NODE_03$',
            "4": r'^[0-9A-F]{24}_NODE_04$',
            "15": r'^[0-9A-F]{24}_NODE_15$',
            "Any": r'^[0-9A-F]{24}_NODE_(?:[0-9]{2})$', #matches any node 00-99
        }
        self.port = HostSerial(device_serial_regex=regex_map[node_index], logger=self.log)

        # timings
        self.default_poll_s = float(default_poll_s)
        self.max_poll_s = float(max_poll_s)
        self.rx_timeout_s = float(rx_timeout_s)

        # command message queue
        self._command_queue: Queue[NodeState] = Queue(maxsize=16)          #queue for outbound commands

        # file request message queue
        self._file_request_queue: Queue[NeuralMemFileRequest] = Queue(maxsize=16)   #queue for outbound file requests

        #============== THREADING ==============
        # request/response coordination
        self.refresh_state_signal = threading.Event()
        self.refresh_state_signal_external = threading.Event()
        self.rx_frame_received_signal = threading.Event()
        self.rx_file_response_signal = threading.Event()  # separate signal for file responses
        self.stop = threading.Event()

        # configure topic subscriptions before starting threads
        self._configure_sub_port_state()
        self._configure_sub_node_state()
        self._configure_sub_debug()
        self._configure_sub_file_request()

        # threads
        self.t_transmit = threading.Thread(target=self._transmit_thread, name=self.node + "_serdes_transmit", daemon=True)
        self.t_receive = threading.Thread(target=self._receive_thread, name=self.node + "_serdes_receive", daemon=True)
        self.t_trig_conn = threading.Thread(target=self._trigger_connect_thread, name=self.node + "_serdes_trig_conn", daemon=True)
        self.t_file_request = threading.Thread(target=self._file_request_thread, name=self.node + "_serdes_file_request", daemon=True)

        # go
        self.t_transmit.start()
        self.t_receive.start()
        self.t_trig_conn.start()        
        self.t_file_request.start()
        
    # ---------- Public API ----------
    def close(self) -> None:
        self.stop.set()
        try:
            self.port.close()
        except Exception:
            pass
        for t in (self.t_transmit, self.t_receive, self.t_trig_conn, self.t_file_request):
            try:
                t.join(timeout=1.0)
            except Exception:
                pass

    # ---------- Thread 1: transmitter ----------
    def _transmit_thread(self) -> None:
        """
        When signaled, serialize local state and send to device,
        then block for a reply (or timeout + recover).
        """
        while not self.stop.is_set():
            # wait until we either want state or we time out for a gentle poll
            self.refresh_state_signal.wait(self.default_poll_s)
            self.refresh_state_signal.clear()

            # if we're not connected, we can't do anything, skip everything below
            if not self.port.port_connected:
                continue

            # pull a command from the command queue
            # if none, create an empty command so we can pull state information from the device
            # use the NodeStateDefaults class to pull this empty command
            try:
                command = self._command_queue.get_nowait()
            except Empty:
                command = NodeStateDefaults.default_command_empty()
                # Note: Not logging here to avoid spam during idle polling

            # serialize this command by packing it into a communication message
            try:
                comm = Communication(node_state=command) 
                outbound_bytes = bytes(comm)
            except Exception as e:
                self.log.warning(f"encode failed: {e}")
                continue

            # fire and wait for acknowledgement (any inbound frame will set rx_frame_seen)
            self.rx_frame_received_signal.clear()
            self.port.write_frame(outbound_bytes)  # framed by Host_Serial 

            if not self.rx_frame_received_signal.wait(self.rx_timeout_s):
                # No RX -> check connection and try recover
                if self.port.port_connected:
                    self.log.info("RX timeout; attempting recover()")
                    self.port.recover()
                continue

    # ---------- Thread 2: RX consumer / deserializer ----------
    def _receive_thread(self) -> None:
        while not self.stop.is_set():
            #blocking wait for a frame to pop into our queue
            #should fire instantly if we get data
            frame = self.port.read_frame(wait=True, timeout=0.02)

            #if we didn't get a payload, wait a bit and try again
            if frame is None:
                continue

            #deserialize/parse the protobuf
            try:
                comm = Communication().parse(frame)
            except Exception as e:
                self.log.warning(f"decode failed: {e}")
                continue

            # route by payload kind using oneof discriminator to avoid AttributeError
            try:
                which, payload = betterproto.which_one_of(comm, "payload")
            except Exception as e:
                self.log.warning(f"Failed to discriminate payload type: {e}")
                which, payload = (None, None)

            # if payload is a node state message, publish payload as node state message
            # and signal that we've received a response from the node (since node only sends as response to transmitted message)
            if which == "node_state" and payload is not None:
                self._pub_node_state(payload)
                self.rx_frame_received_signal.set() #notify that we received a state message

            #and if the payload is a file request response, publish the payload as a file response
            #and signal that we've received a response from the node (since node only sends as a response to transmitted message)
            elif which == "neural_mem_request" and payload is not None:
                self._pub_file_response(payload)
                self.rx_file_response_signal.set() #notify that we received a file request response
            
            #if payload is a debug message, publish payload as debug message
            # don't notify tx thread that we've received a response to our transmission (since debug messages are asynchronous + unrelated to commands)
            elif which == "debug_message" and payload is not None:
                self._pub_debug(payload)
            
            #if payload is unknown, log a warning
            else:
                self.log.warning(f"unknown payload type: {which}")
    
    # ---------- Thread 3: IO control + command topics ----------
    def _trigger_connect_thread(self) -> None:
        while not self.stop.is_set():
            #port connection handled directly in callback function
            #new state request and acknowledgement handled directly in callback function

            #publish the status of the serial port
            self._pub_port_state(
                stat_connected=self.port.port_connected,
                stat_port_name=self.port.port_name,
                stat_serial_number=self.port.serial_number,
                stat_commands_enqueued=self._command_queue.qsize(),
                stat_command_queue_space=self._command_queue.maxsize - self._command_queue.qsize()
            )

            #push the state request to the transmit thread
            #having a buffered state request lets us rate limit the state updates; doesn't spam the node with comms
            if self.refresh_state_signal_external.is_set() or self._command_queue.qsize() > 0:
                self.refresh_state_signal_external.clear()
                self.refresh_state_signal.set()

            #rate limit by sleeping this thread 
            self.stop.wait(timeout=self.max_poll_s)

    # ---------- Thread 4: file request thread ----------
    def _file_request_thread(self) -> None:
        while not self.stop.is_set():
            # wait until the file request queue has something in it (with timeout to allow clean shutdown)
            try:
                file_request = self._file_request_queue.get(timeout=0.2) 
            except Empty:
                continue

            # if we're not connected, we can't do anything, skip
            if not self.port.port_connected:
                self.log.warning("file request dropped: port not connected")
                continue

            # pack and serialize message with betterproto
            try:
                comm = Communication(neural_mem_request=file_request)
                outbound_bytes = bytes(comm)
            except Exception as e:
                self.log.warning(f"encode failed: {e}")
                continue

            # fire and wait for acknowledgement (uses separate signal from state messages)
            self.rx_file_response_signal.clear()
            self.port.write_frame(outbound_bytes)  # framed by Host_Serial 

            if not self.rx_file_response_signal.wait(self.rx_timeout_s):
                # No RX -> check connection and try recover
                if self.port.port_connected:
                    self.log.info("RX timeout on file request; attempting recover()")
                    self.port.recover()
                continue

    # ---------- Publishing/Subscription Handling -------------
    ###### PORT STATE #######
    def _configure_sub_port_state(self) -> None:
        #subscribe to the command topics
        pub.subscribe(self._on_refresh_state, f"{self.root}.port.command.refresh_state")
        pub.subscribe(self._on_request_connect, f"{self.root}.port.command.request_connect")

    def _pub_port_state(self, 
                        *, 
                        stat_connected: bool, 
                        stat_port_name: Optional[str], 
                        stat_serial_number: Optional[str],
                        stat_commands_enqueued: int,
                        stat_command_queue_space: int) -> None:
        # publish on change-only using topic-based cache
        pub.sendMessage(f"{self.root}.port.status.connected", payload=stat_connected)
        pub.sendMessage(f"{self.root}.port.status.port_name", payload=stat_port_name if stat_port_name is not None else "---")
        pub.sendMessage(f"{self.root}.port.status.serial_number", payload=stat_serial_number if stat_serial_number is not None else "---")
        pub.sendMessage(f"{self.root}.port.status.commands_enqueued", payload=stat_commands_enqueued)
        pub.sendMessage(f"{self.root}.port.status.command_queue_space", payload=stat_command_queue_space)

    ###### NODE STATE ######
    def _configure_sub_node_state(self) -> None:
        #subscribe to the node state topic
        pub.subscribe(self._on_node_command, f"{self.root}.command") 

    def _pub_node_state(self, node_state: NodeState) -> None:
        #publish the node state directly to the status topic
        pub.sendMessage(f"{self.root}.status", payload=node_state)

    ###### DEBUG ######
    def _configure_sub_debug(self) -> None:
        #debug information only is outbound, so nothing here
        pass

    def _pub_debug(self, debug_message: Debug) -> None:        
        #extract the topic from the stringified version of the debug level
        debug_topic = f"{self.root}.debug.{debug_message.level.name}"
        
        #publish the debug message to the correct status topic
        pub.sendMessage(debug_topic, payload=debug_message.msg)

    ###### FILE RESPONSE ######
    def _configure_sub_file_request(self) -> None:
        #subscribe to the file response topic
        pub.subscribe(self._on_file_request, f"{self.root}.file_request")

    def _pub_file_response(self, file_request: NeuralMemFileRequest) -> None:
        #publish the file request directly to the status topic
        pub.sendMessage(f"{self.root}.file_response", payload=file_request)

    #------------------------------- Callback Functions ---------------------------------
    def _on_request_connect(self, payload: Any = None) -> None:
        #sanity check payload
        if payload is None:
            self.log.warning("request_connect callback invoked without payload")
            return
        
        #sanity check the type, then connect/disconnect the port if the payload is True
        #and only do so if the port is not already in the desired state
        if isinstance(payload, bool):
            if payload:
                if not self.port.port_connected:
                    self.port.connect()
            else:
                if self.port.port_connected:
                    self.port.disconnect()
        else:
            self.log.warning(f"invalid request connect type: {type(payload)} (expected bool)")

    #register this callback to handle request state messages
    def _on_refresh_state(self, payload: Any = None) -> None:
        #sanity check payload
        if payload is None:
            self.log.warning("refresh_state callback invoked without payload")
            return

        #sanity check the type, then assert the flag if the payload is True
        if isinstance(payload, bool):
            if payload:
                self.refresh_state_signal_external.set()
                #clear the request flag to acknowledge service (MAY REENTER, should be fine)
                pub.sendMessage(f"{self.root}.port.command.refresh_state", payload=False)
        else:
            self.log.warning(f"invalid refresh state type: {type(payload)} (expected bool)")

    # Handler drops commands into the outgoing command queue
    # as long as type check passes and queue has space.
    def _on_node_command(self, payload: Any = None) -> None:
        #sanity check payload
        if payload is None:
            self.log.warning("node command callback invoked without payload")
            return
        
        #sanity check the type, then put the payload into the command queue
        if isinstance(payload, NodeState):
            try:
                self._command_queue.put_nowait(payload)
            except Exception as e:
                # Catch both queue full and other put_nowait exceptions
                self.log.warning(f"command queue is full or put failed, dropping command: {e}")
        else:
            self.log.warning(f"Received invalid command for node: {type(payload)} (expected NodeState)")

    # Handler drops file requests into the outgoing file request queue
    # as long as type check passes and queue has space
    def _on_file_request(self, payload: Any = None) -> None:
        #sanity check payload
        if payload is None:
            self.log.warning("file request callback invoked without payload")
            return
        
        #sanity check the type, then put the payload into the file request queue
        if isinstance(payload, NeuralMemFileRequest):
            try:
                self._file_request_queue.put_nowait(payload)
            except Exception as e:
                # Catch both queue full and other put_nowait exceptions
                self.log.warning(f"file request queue is full or put failed, dropping file request: {e}")
        else:
            self.log.warning(f"Received invalid file request for node: {type(payload)} (expected NeuralMemFileRequest)")

    