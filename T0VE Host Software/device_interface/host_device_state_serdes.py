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
import threading, time, logging
from queue import Empty
from typing import Any, Dict, Iterable, Tuple, Optional

from pubsub import pub  # PyPubSub
from host_device_serial import Host_Serial  # your lower layer  :contentReference[oaicite:7]{index=7}

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
from app_messages.node_state import NodeState
from app_messages.debug_message import DebugMessage
from app_messages.communication import Communication


class HostDeviceStateSerdes:
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
        device_serial_regex = r'^[0-9A-F]{24}_' + self.node.upper() + r'$'
        self.port = Host_Serial(device_serial_regex=device_serial_regex, logger=self.log)  # :contentReference[oaicite:8]{index=8}

        # timings
        self.default_poll_s = float(default_poll_s)
        self.max_poll_s = float(max_poll_s)
        self.rx_timeout_s = float(rx_timeout_s)

        # local state cache (authoritative host-side)
        #initialize with protobuf defaults
        self.state_local = NodeState()

        # cache of last published topic values
        # ensures that we don't publish when there's no real state change
        self.last_pub: Dict[str, Any] = {}

        #request state coordination--this is the one pub/sub requests will set
        #will be read by a thread that rate limits the requests, and set the actual
        #`request_state_signal` that controls the transmitter
        self.request_state_signal_rate_limiter = threading.Event()

        # request/response coordination
        self.connect_signal = threading.Event()
        self.request_state_signal = threading.Event()
        self.rx_frame_received_signal = threading.Event()
        self.stop = threading.Event()
        self.last_state_pub_ts = 0.0

        # threads
        self.t_transmit = threading.Thread(target=self._transmit_thread, name=self.node + "_serdes_transmit", daemon=True)
        self.t_receive = threading.Thread(target=self._receive_thread, name=self.node + "_serdes_receive", daemon=True)
        self.t_trig_conn = threading.Thread(target=self._trigger_connect_thread, name=self.node + "_serdes_trig_conn", daemon=True)


        # wiring of topics (subscribe + initial publishes)
        #TODO: bind to all protobuf commands
        self._bind_topics_and_publish_static()

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
                comm = Communication(node_state=self.state_local)   #TODO: make sure syntax is correct given message labels
                outbound_bytes = comm.to_bytes()
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

            #if we didn't get a payload, wait a bit and try again
            if frame is None:
                self.stop.wait(0.02)
                continue

            #deserialize/parse the protobuf
            try:
                comm = Communication.from_bytes(frame)
            except Exception as e:
                self.log.warning(f"decode failed: {e}")
                continue

            # route by payload kind
            if comm.debug_message is not None:
                self._publish_debug(comm.debug_message)

            if comm.node_state is not None:
                self._publish_state_tree(comm.node_state)
                self.rx_frame_received_signal.set() #notify that we received a state message
        
    
    # ---------- Thread 3: IO control + command topics ----------
    def _trigger_connect_thread(self) -> None:
        while not self.stop.is_set():
            # publish connection details, change only
            self._pub_change(f"{self.root}.port.status.connected", self.port.port_connected)
            self._pub_change(f"{self.root}.port.status.port_name", self.port.port_name)
            self._pub_change(f"{self.root}.port.status.serial_number", self.port.serial_number)

            #check if we need to request a state update
            #rate limit implicitly via execution delay in this thread
            if self.request_state_signal_rate_limiter.is_set():
                self.request_state_signal_rate_limiter.clear()
                self.request_state_signal.set()

            #rate limit by sleeping this thread 
            self.stop.wait(timeout=self.max_poll_s)

    # ---------- Topic bindings ----------
    def _bind_topics_and_publish_static(self) -> None:
        # commands (handlers are listed below)
        pub.subscribe(self._on_request_connect, f"{self.root}.port.command.request_connect")
        pub.subscribe(self._on_request_state,   f"{self.root}.port.command.request_state")

        #TODO: bind all command topics to handlers!

    #requests to allow/disallow connections for this node
    def _on_request_connect(self, value: bool) -> None:
        if bool(value):
            self.port.connect()
        else:
            self.port.disconnect()

    #request a state update for this node; deasserted when thread signal is set
    #actual signal assertion is rate limited so we don't DDoS the serial port lol
    def _on_request_state(self, value: bool) -> None:
        if value:
            self.request_state_signal_rate_limiter.set()

    # ---------- Debug publishing ----------
    def _publish_debug(self, dbg: Debug) -> None:
        # Normalize level -> topic
        level = (dbg.level.name if hasattr(dbg.level, "name") else str(dbg.level)).lower()
        self._pub_change(f"{self.root}.debug.{level}", dbg.msg)

    # ---------- State tree publishing ----------
    def _publish_state_tree(self, state: NodeState) -> None:
        """
        Walk the message and publish every leaf under app.devices.<node>.state...
        Only changes are emitted.
        """
        if hasattr(state, "to_dict"):
            tree = state.to_dict()
        else:
            # Very safe fallback; BetterProto should have to_dict()
            tree = {k: getattr(state, k) for k in dir(state) if not k.startswith("_") and not callable(getattr(state, k))}
        self._walk_and_publish(f"{self.root}.state", tree)

    def _walk_and_publish(self, base: str, obj: Any) -> None:
        if obj is None:
            return
        if isinstance(obj, dict):
            for k, v in obj.items():
                self._walk_and_publish(f"{base}.{k}", v)
        elif isinstance(obj, (list, tuple)):
            for i, v in enumerate(obj):
                self._walk_and_publish(f"{base}.{i}", v)
        else:
            # leaves: normalize enums/bytes
            v = self._normalize(obj)
            self._pub_change(base, v)

    # ---------- Utilities ----------
    #TODO: not sure if I like this normalization
    def _normalize_leaf(self, v: Any) -> Any:
        # enums -> names, bytes -> hex; else pass-through
        try:
            import enum
            if isinstance(v, enum.Enum):
                return v.name
        except Exception:
            pass
        if isinstance(v, (bytes, bytearray)):
            return v.hex()
        return v

    def _pub_change(self, topic: str, value: Any) -> None:
        old = self._last_pub.get(topic, object())
        if old != value:
            self._last_pub[topic] = value
            pub.sendMessage(topic, data=value)