'''
Host Device State Serdes

This module is responsible for serializing and deserializing to/from a protobuf format. 
And also managing global state for the connected device. It will use PyPubSub to interface with the rest of the application after construction. 

This class will passed a device regex, default state polling rate in seconds (default 0.5), max state polling rate in seconds (default 0.1), receive timeout in seconds (default 5), and optional logger. 
It will instantiate a `host_device_serial` port using the regex and the logger.
NOTE: there's a bit of publishing mentioned in this description. I specifically want a cached copy of each topic maintained. A topic should only be published
      to if the value of that topic changes! This minimizes broker traffic and should hopefully improve the performance of the system. 

It will also instantiate a thread. The function of this thread is:
 - if(connected and signaled)
    - reads the locally maintained copy of the system state (initialized with default value)
    - serializes this system state using `better_proto`
    - resets a `received_frame` flag 
    - writes this serialized state to the serial port
    - waits (with timeout) until `received_frame` is set
    - if we time out
        - check if we're still connected
        - if still connected, call the port's recover function

It also instantiates a second thread. This thread:
 - publishes to the 'connected', 'port', and 'search_regex' topics (see below)
   by reading those details from the port
 - subscribes to the 'request_connect' topic - connects/disconnects with the port according to this status
 - subscribes to 'state' topics with `command` or `cmd` in their name. Updates the local copy of the state with these new commanded values
 - signals the update thread if `request_state` is true
    - implements some logic that prevents this from getting signaled faster than the max state polling rate

A third thread. This thread:
 - simply publishes `true` to the 'request_state' for the node at the default state polling rate

It also instantiates a fourth thread. This thread:
 - polls the RX queue, ignores if None
 - if we got something from the queue, decode it using betterproto
 - if debug_message, publish on the 'debug' topic (see below)
 - if state message, publish on the 'state' topic (see below) 

Let's talk about message organization
 - app
    - devices
        -node
            -port
                - command
                    - request_connect
                    - request_state
                - status
                    - connected
                    - port
                    - serial_number
            -state
                **auto-generated based on protobuf messages, i.e.
                **replicate the hierarchy of Node_State message structure
                **topics will be prettified-versions of field names
                **with the same hierarchy as the Node_State and its submessages
                **parsed from JSON or dictionary representation of the betterproto
            -debug
                **message channel per `debug_level`, i.e.
                - info
                - warn
                - error

'''

from __future__ import annotations
import threading, logging
from typing import Optional
import betterproto

from host_device_serial import Host_Serial  # your lower layer  :contentReference[oaicite:7]{index=7}
from utilities.util_pubsub_dict import Pub_Sub_Dict

# Dynamically add '/T0VE Common/Proto-Host' (and submodules) to sys.path to ensure imports succeed,
# regardless of working directory or install status.
# This does not rely on Windows shortcuts.

import sys
import os

# Set up the common proto directory path (relative to this source file)
BASE_DIR = os.path.abspath(os.path.dirname(__file__))
T0VE_PARENT = os.path.abspath(os.path.join(BASE_DIR, '..', '..'))  # up to the project root
PROTO_HOST_PATH = os.path.join(T0VE_PARENT, 'T0VE Common', 'Proto-Host')

if os.path.isdir(PROTO_HOST_PATH) and PROTO_HOST_PATH not in sys.path:
    sys.path.insert(0, PROTO_HOST_PATH)

# Now import the generated betterproto messages.
# just import everything, somewhat indiscriminately
from app import * 


class Host_Device_State_Serdes:
    """
    Serialize/deserialize BetterProto `Communication` messages for a specific node and
    expose mirrored state via PyPubSub. Current design uses three threads:

    - Transmit: waits on `request_state_signal` or `default_poll_s`, sends local `NodeState`,
      and awaits an RX state message (else invokes `recover()` on timeout if still connected).
    - Receive: parses inbound frames; publishes `node_state` to `...state` and `debug_message` to `...debug`.
      Only `node_state` messages acknowledge TX.
    - Trigger/connect: reflects `...port.command.request_connect`, triggers `request_state_signal` when
      `...port.command.request_state` is true, and publishes `...port.status.{connected,port_name,serial_number}`.

    Notes:
    - Values publish only when changed.
    - TX intentionally polls at `default_poll_s` in absence of commands.
    - Auto-connect uses a case-sensitive serial-number regex; ensure device serials match formatting.
    """
    def __init__(
        self,
        *,
        node_index: int, 
        default_poll_s: float = 0.5,
        max_poll_s: float = 0.1,
        rx_timeout_s: float = 5.0,
        logger: Optional[logging.Logger] = None,
    ) -> None:

        #sanity check the node index, raise value error if not sane
        if(node_index not in [0, 1, 2, 3, 4, 15]):
            raise ValueError("Invalid Node index!")

        # identity / hierarchy
        string_node = str(node_index)
        self.node = "node_" + string_node.zfill(2)  #ensure two digits for node label
        self.root = f"app.devices.{self.node}"  # PyPubSub uses '.' separator by default

        self.log = logger or logging.getLogger(__name__ + self.node + ".serdes")

        # lower layer port
        # NOTE: serial-number regex is case-sensitive; adjust upstream if device serials may differ in case.
        device_serial_regex = r'^[0-9A-F]{24}_' + self.node.upper() + r'$'
        self.port = Host_Serial(device_serial_regex=device_serial_regex, logger=self.log)  # :contentReference[oaicite:8]{index=8}

        # timings
        self.default_poll_s = float(default_poll_s)
        self.max_poll_s = float(max_poll_s)
        self.rx_timeout_s = float(rx_timeout_s)

        # ============== NODE STATE PUBLISHING/CONTROL ============
        # maintained as a dictionary; whose topics will be automatically published when updated
        # this dictionary will also be configured to allow the `*.command.*` fields to be editable
        self.node_state_local = Pub_Sub_Dict(
            root_topic=f"{self.root}.state",
            dict_reference=NodeState().to_dict(include_default_values=True),
            logger=self.log
        )
        self._bind_state_command_topics()

        # ============== DEBUG MESSAGE PUBLISHING =============
        # maintained as a dictionary; whose topics will be automatically published when updated
        # no editable command fields for debug messages
        self.node_debug_local = Pub_Sub_Dict(
            root_topic=f"{self.root}.debug",
            dict_reference=Debug().to_dict(include_default_values=True),
            logger=self.log
        )

        # ============== SERIAL PORT STATE PUBLISHING/CONTROL ============
        # controls details about serial port operation; reports status about the serial port
        # aren't using protobuf for this, so need to first define the dictionary structure
        port_status_control_ref_dict = {
            "status": {
                "connected": False,
                "port_name": "",
                "serial_number": ""
            },
            "command": {
                "request_connect": True,
                "request_state": False
            }
        }
        self.port_status_control = Pub_Sub_Dict(
            root_topic=f"{self.root}.port",
            dict_reference=port_status_control_ref_dict,
            logger=self.log
        )
        self._bind_port_command_topics()

        #============== THREADING ==============
        # request/response coordination
        self.request_state_signal = threading.Event()
        self.rx_frame_received_signal = threading.Event()
        self.stop = threading.Event()

        # threads
        self.t_transmit = threading.Thread(target=self._transmit_thread, name=self.node + "_serdes_transmit", daemon=True)
        self.t_receive = threading.Thread(target=self._receive_thread, name=self.node + "_serdes_receive", daemon=True)
        self.t_trig_conn = threading.Thread(target=self._trigger_connect_thread, name=self.node + "_serdes_trig_conn", daemon=True)

        # go
        self.t_transmit.start()
        self.t_receive.start()
        self.t_trig_conn.start()        

    # ---------- Public API ----------
    def close(self) -> None:
        self.stop.set()
        try:
            self.port.close()
        except Exception:
            pass
        for t in (self.t_transmit, self.t_receive, self.t_trig_conn):
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
            self.request_state_signal.wait(self.default_poll_s)
            self.request_state_signal.clear()

            # if we're not connected, we can't do anything, skip everything below
            if not self.port.port_connected:
                continue

            # try serialize local state with betterProto
            try:
                # aggregate a state dictionary from our pub/sub ported dictionary
                # convert this dictionary into a betterproto `node_state` object 
                # create a `Communication` from this `node_state` object, and serialize into bytes
                current_state_dict = self.node_state_local.collect()
                node_state_local = NodeState().from_dict(current_state_dict)
                comm = Communication(node_state=node_state_local) 
                outbound_bytes = bytes(comm)
            except Exception as e:
                self.log.warning(f"encode failed: {e}")
                continue

            # fire and wait for acknowledgement (any inbound frame will set rx_frame_seen)
            self.rx_frame_received_signal.clear()
            self.port.write_frame(outbound_bytes)  # framed by Host_Serial  :contentReference[oaicite:9]{index=9}

            if not self.rx_frame_received_signal.wait(self.rx_timeout_s):
                # No RX -> check connection and try recover
                if self.port.port_connected:
                    self.log.info("RX timeout; attempting recover()")
                    self.port.recover()
                continue

    # ---------- Thread 2: RX consumer / deserializer ----------
    def _receive_thread(self) -> None:
        while not self.stop.is_set():
            frame = self.port.read_frame()

            #ifwe didn't get a payload, wait a bit and try again
            if frame is None:
                self.stop.wait(0.02)
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
            # and ONLY IN THIS CASE signal that we've received a response from the node
            if which == "node_state" and payload is not None:
                self.node_state_local.update(payload.to_dict()) #ignore default values to reduce traffic
                self.rx_frame_received_signal.set()             #notify that we received a state message

            #if payload is a debug message, publish payload as debug message
            # don't notify tx thread that we've received a response to our transmission (since debug messages are asynchronous + unrelated to commands)
            elif which == "debug_message" and payload is not None:
                #update the debug message we received; publish the debug fields 
                self.node_debug_local.update(payload.to_dict())
            
            #if payload is unknown, log a warning
            else:
                self.log.warning(f"unknown payload type: {which}")
    
    # ---------- Thread 3: IO control + command topics ----------
    def _trigger_connect_thread(self) -> None:
        while not self.stop.is_set():
            #grab the current port status/control 
            current_port_status_control = self.port_status_control.collect()

            #connect/disconnect the port depending on the state
            if current_port_status_control["command"]["request_connect"]:
                if not current_port_status_control["status"]["connected"]:
                    self.port.connect()
            else:
                if current_port_status_control["status"]["connected"]:
                    self.port.disconnect()

            #check if we need to request a state update
            #rate limit implicitly via execution delay in this thread
            if current_port_status_control["command"]["request_state"]:
                self.request_state_signal.set()
                current_port_status_control["command"]["request_state"] = False

            #update the port status
            current_port_status_control["status"]["connected"] = self.port.port_connected
            current_port_status_control["status"]["port_name"] = self.port.port_name
            current_port_status_control["status"]["serial_number"] = self.port.serial_number

            #update the port status control dictionary
            self.port_status_control.update(current_port_status_control)

            #rate limit by sleeping this thread 
            self.stop.wait(timeout=self.max_poll_s)

    # ---------- Topic bindings ----------
    #binding topics related to the COM port/control of this particular instance
    def _bind_port_command_topics(self) -> None:
        '''
        For the port status control dictionary, build a list of topics that are writable
        (i.e. have a `*.command.*` somewhere in the topic string)
        and update the dictionary to allow these topics to be writable.
        '''
        all_topics = self.port_status_control.get_all_topics()
        writable = tuple(t for t in all_topics if ".command." in t)
        self.port_status_control.update_writable_topics(writable)
        self.log.info(f"Subscribed to {len(writable)} command topics (serial port status/control)")


    #binding topics related to node state/protobuf
    def _bind_state_command_topics(self) -> None:
        """
        For the node state "ported" dictionary, build a list of topics that are writable 
        (i.e. have a `*.command.*` somewhere in the topic string)
        and update the dictionary to allow these topics to be writable.
        """
        # compute writable topics (commands only) from the flattened topic space
        all_topics = self.node_state_local.get_all_topics()
        writable = tuple(t for t in all_topics if ".command." in t)
        self.node_state_local.update_writable_topics(writable)
        self.log.info(f"Subscribed to {len(writable)} command topics (node state)")
    