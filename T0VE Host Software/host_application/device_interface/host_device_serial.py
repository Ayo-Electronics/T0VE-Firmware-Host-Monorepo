'''
Host serial interface (physical/protocol layer) for USB Virtual COM Port (VCP) link between Python host software and a USB device.

General philosopy of this class is creating an asynchronous interface to the serial port managed by its own thread.
Data transfer in and out of the serial port is done via queues, for atomic, independent operation of the serial port and the host software.

For simplest, most straight-forward operation, no events are exposed from this class--external facing API will be polling-based.
i.e. view immediate connection status and check/pop from the receive queue with helper functions. 

A utility "recovery" function will help recover stuck downstream devices by sending 0's to the port until a message has been received.
This function should never throw exceptions as long as correct types are passed in.
'''


from __future__ import annotations              #as I understand, this helps with cyclical dependencies and type checking

import threading                                #concurrency
import logging
from typing import Optional                     #type hints
import re                                        #regex for serial number matching
from queue import Queue, Empty                  #sharing information between threads

try:
    import serial   #pyserial; make sure the module is installed
    from serial.tools import list_ports  # enumerate available ports
except Exception:   #pragma: no cover - pyserial may not be installed at edit time
    raise ImportError("pyserial is not installed! Please install it with 'pip install pyserial'")


 
class Host_Serial:
    def __init__(
        self,
        *,
        device_serial_regex: Optional[str] = None,
        start_code: int = 0xEE,
        logger: Optional[logging.Logger] = None,
    ) -> None:

        ######### PORT-RELATED #########
        self._allowing_connections: bool = True                  #might *technically* need an atomic guard, but only one thread reads, other writes
        self._serial_regex_str: Optional[str] = device_serial_regex
        self._serial_regex: Optional[re.Pattern[str]] = (
            re.compile(device_serial_regex) if device_serial_regex else None
        )
        self._port: Optional[serial.Serial] = None
        self._port_connected: bool = False                      #might *technically* need an atomic guard, but only one thread reads, other writes
        self._connected_port_name: Optional[str] = None          #e.g. COM3
        self._connected_serial_number: Optional[str] = None      #matched device serial
        self._start_code: int = start_code
        self._logger = logger or logging.getLogger(__name__ + ".Host_Serial")

        ######### TX-RELATED #########
        self._tx_queue: Queue[bytes] = Queue()      #queue for outgoing bytes

        ######### RX-RELATED #########
        self._rx_buffer = bytearray()                   #buffer for incoming bytes
        self._rx_clear_signal = threading.Event()       #signal to clear the rx buffer (buffer clearing handled in the receive thread)
        self._rx_queue: Queue[bytes] = Queue()          #queue for completed RX frames

        ######### SPAWN THREAD ########
        self._thread_stop_signal = threading.Event()
        self._thread = threading.Thread(
            target=self._run_port,
            name=f"Host_Serial {device_serial_regex or ''}",
            daemon=True,
        )
        self._thread.start()

    #=================== LIFECYCLE CONTROL ================
    def close(self, *, join_timeout: float = 1.0) -> None:
        """
        Request a graceful shutdown of the worker thread and wait briefly.
        Idempotent and safe to call multiple times.
        """
        try:
            self._thread_stop_signal.set()
        except Exception:
            return  #If construction failed part-way, `_thread_stop_signal` may not exist.

        try:
            self._thread.join(timeout=join_timeout)
        except Exception:
            pass    #Never raise during shutdown paths.

    def __enter__(self) -> "Host_Serial":
        """Support `with` statement usage for deterministic shutdown."""
        return self

    def __exit__(self, exc_type, exc, tb) -> None:  # noqa: D401 - short and clear
        self.close()

    def __del__(self) -> None:
        """Best-effort cleanup to stop the worker if not already closed."""
        try:
            self.close()
        except Exception:
            pass    #Suppress all exceptions in destructors.

    #=================== PUBLIC API ==================
    #### CONNECTION ####
    def connect(self) -> None:
        '''
        Connect the port
        '''
        self._allowing_connections = True
        self._logger.info("connect() requested")

    def disconnect(self) -> None:
        '''
        Disconnect the port
        '''
        self._allowing_connections = False
        self._logger.info("disconnect() requested")
    
    @property
    def port_connected(self) -> bool:
        return self._port_connected

    @property
    def port_name(self) -> Optional[str]:
        return self._connected_port_name

    @property
    def serial_regex(self) -> Optional[str]:
        return self._serial_regex_str

    @property
    def serial_number(self) -> Optional[str]:
        return self._connected_serial_number

    #### TX ####
    def write_frame(self, payload: bytes) -> None:
        """
        Frame and enqueue a payload for transmission; if the queue is full, the payload is not enqueued.
        The TX framing is: START_CODE (1 byte) + length (2 bytes, big-endian)
        + payload bytes. Thread-safe and non-blocking.
        """
        #explicitly convert our payload to bytes, extract length of the data
        data = bytes(payload)
        length = len(data)

        #sanity check our length
        if length > 0xFFFF:
            raise ValueError("payload too large for 16-bit length field")

        #build our header, see framing note above
        header = bytes(((self._start_code & 0xFF), (length >> 8) & 0xFF, length & 0xFF))
        
        #assemble our frame and enqueue
        frame = header + data
        self._tx_queue.put(frame)

    #### RX ####
    def read_frame(self) -> Optional[bytes]:
        '''
        Read a frame from the RX queue. If no frame is available, return None.
        '''
        try:    
            return self._rx_queue.get_nowait()
        except Empty:
            return None
    
    def clear_receive_buffer(self) -> None:
        '''
        Clear the receive buffer.
        '''
        #defer clearing to the thread--ensures only single thread writes to RX buffer (good for atomicity)
        self._rx_clear_signal.set() 

    #### RECOVERY ####
    def recover(
        self,
        *,
        attempts: int = 65536,
        inter_delay_s: float = 0.02,
    ) -> None:
        '''
        Recover from a disconnect by sending 0's to the port until a message has been received.
        '''
        for _ in range(int(attempts)):
            #check to see if we've received a complete frame
            #if we've received a frame from the device, means we've recovered
            if(not self._rx_queue.empty()):
                return None

            #also check to see if we've disconnnected
            #in which case, recovery is irrelevant
            if(not self._port_connected):
                return None

            #otherwise, drop a 0 directly into the tx queue
            #and wait a little for the thread to process it
            self._tx_queue.put(b"\x00")
            self._thread_stop_signal.wait(float(inter_delay_s))

    #=================== THREAD FUNCTIONS =================
    def _run_port(self) -> None:
        '''
        This thread function manages the serial port operation
         - connects/disconnects the port depending on desired connection state
         - pushes bytes out of the port
         - pulls bytes in from the port
        '''
        #loop until we're told to stop
        while(not self._thread_stop_signal.is_set()):
            #check to see if we need to connect/disconnect the port
            self._check_do_connect()

            #check if we need to clear the RX buffer
            if(self._rx_clear_signal.is_set()):
                self._flush_rx_buffer()

            #manage flow control lines of the port
            self._manage_flow_control()

            #if we're connected
            if(self._port_connected):
                try:
                    #drain the TX queue and read from the port
                    self._do_tx()
                    self._do_rx()

                    #and process any new frames we have in our RX buffer
                    self._process_rx_buffer()

                    #delay a tight amount
                    self._thread_stop_signal.wait(0.01)
                
                #if any exceptions happen during operation, disconnect the port
                except serial.SerialException as exc:
                    self._logger.warning(f"Serial exception during operation: {exc}")
                    self._handle_disconnect()

            #otherwise, we're not connected
            else:
                #flush the tx queue, and clear the rx buffer
                self._flush_tx_queue()
                self._flush_rx_buffer()

                #delay a larger amount when disconnected to not spam the port with connect requests
                self._thread_stop_signal.wait(0.5)
    
    def _flush_tx_queue(self) -> None:
        '''
        Flush the TX queue
        '''
        flushed = 0
        while not self._tx_queue.empty():
            self._tx_queue.get_nowait()
            flushed += 1
        if flushed:
            self._logger.debug(f"Flushed {flushed} TX items")

    def _flush_rx_buffer(self) -> None:
        '''
        Flush the RX buffer
        '''
        self._logger.debug(f"Clearing RX buffer ({len(self._rx_buffer)} bytes)")
        self._rx_buffer.clear()
        if(self._port is not None):
            self._port.reset_input_buffer() 

    def _check_do_connect(self) -> None:
        '''
        Check to see if we need to connect/disconnect the port
        '''
        #if we aren't connected currently and we want to connect (and we have a defined serial regex)
        if(
            not self._port_connected
            and self._allowing_connections
            and self._serial_regex is not None
        ):
            self._handle_connect()

        #and if we are connected currently and we want to don't want to connect, disconnect
        if(self._port_connected and not self._allowing_connections):
            self._handle_disconnect()

    def _handle_connect(self) -> None:
        '''
        Handle the connection of the port by discovering a device whose serial number
        matches the configured regular expression.
        '''
        #try to discover and connect a matching port
        #if any exceptions happen during connect, ride through and try again next time
        try:
            if self._serial_regex is None:
                self._logger.debug("No serial-number regex configured; skipping connect attempt")
                return

            # enumerate available ports and find first matching device serial number
            candidate = None
            candidate_sn = None
            ports = list(list_ports.comports())
            for p in ports:
                sn = getattr(p, 'serial_number', None)
                if sn is None:
                    continue
                if self._serial_regex.search(str(sn)):
                    candidate = p
                    candidate_sn = str(sn)
                    break

            if candidate is None:
                self._logger.debug(
                    f"No ports matched serial regex '{self._serial_regex.pattern if self._serial_regex else ''}'"
                )
                return

            self._logger.debug(
                f"Attempting to open port {candidate.device} (serial {candidate_sn})"
            )
            self._port = serial.Serial(  # type: ignore[attr-defined]
                port=candidate.device,
                baudrate=115200,
                timeout=0,
            )
            self._connected_port_name = str(candidate.device)
            self._connected_serial_number = candidate_sn
            self._port_connected = True
            self._logger.info(
                f"Port opened: {self._connected_port_name} (serial {self._connected_serial_number})"
            )
        except serial.SerialException as exc:
            self._logger.warning(
                f"Port open failed for {getattr(candidate, 'device', 'UNKNOWN')}: {exc}"
            )
            self._port = None
    
    def _handle_disconnect(self) -> None: 
        '''
        Handle the disconnection of the port
        '''
        #try to disconnect the port
        #if any exceptions happen during disconnect, ride through assuming port closed due to disconnect
        try:
            if self._port is not None:
                self._logger.debug(f"Closing port {self._connected_port_name}")
                self._port.close()
        except serial.SerialException as exc:
            self._logger.warning(f"Serial exception during port close: {exc}")
            self._logger.debug("Ignoring exception during port close")
        self._port_connected = False
        self._port = None
        self._connected_port_name = None
        self._connected_serial_number = None
        self._logger.info("Port disconnected")

    def _do_tx(self) -> None:
        '''
        Drain the TX queue and write to the port
        '''
        #dont' try to do anything if we don't have a valid port
        if self._port is None:
            return

        #loop until the TX queue is empty
        while not self._tx_queue.empty():
            #grab the bytes we want to transmit
            to_transmit = self._tx_queue.get_nowait()

            #and try to write the bytes to the port
            #if any exceptions happen during transmit, bubble up
            #used to detect whether the device disconnected 
            try:
                self._logger.debug(f"TX {len(to_transmit)} bytes")
                self._port.write(to_transmit)
                self._port.flush()
            except serial.SerialException as exc:
                self._logger.warning(f"Serial exception during TX: {exc}")
                raise exc
    
    def _do_rx(self) -> None:
        '''
        Read from the port and enqueue any new frames
        '''
        #dont' try to do anything if we don't have a valid port
        if self._port is None:
            return
        
        #get the number of bytes available to read
        try:
            pending = self._port.in_waiting
        except serial.SerialException as exc:
            self._logger.warning(f"Serial exception during RX: {exc}")
            raise exc
        if pending <= 0:
            return

        #read the bytes from the port
        #if any exceptions happen during read, bubble up
        #used to detect whether the device disconnected 
        try:
            data = self._port.read(pending)
        except serial.SerialException as exc:
            self._logger.warning(f"Serial exception during RX: {exc}")
            raise exc
        if not data:
            return

        #and push the bytes to the back of the buffer
        self._rx_buffer.extend(data)
        self._logger.debug(f"RX {len(data)} bytes")

    def _process_rx_buffer(self) -> None:
        '''
        Process any new frames we have in our RX buffer
        '''
        while True:
            #alias for buffer for brevity
            buf = self._rx_buffer
            
            # 1a) Seek to the next START_CODE
            start_idx = None
            for i, b in enumerate(buf):
                if b == (self._start_code & 0xFF):
                    start_idx = i
                    break

            if start_idx is None:
                if(len(buf) > 0):
                    # No start marker at all; purge the buffer and break
                    self._logger.debug(f"RX buffer cleared (no start code): {len(buf)} bytes")
                    buf.clear() 
                break   #but don't continue parsing in any case

            # 1b) Discard noise before the start marker
            if start_idx > 0:
                del buf[:start_idx]

            # 2a) Need at least 3 bytes (1 start + 2 length)
            if len(buf) < 3:
                break

            # 2b) Parse length
            length = (buf[1] << 8) | buf[2]
            total_needed = 1 + 2 + length
            if len(buf) < total_needed:
                # Wait for more data
                break

            # 3) Extract payload, remove from buffer, and push to receive frame queue
            payload = bytes(buf[3:3 + length])
            del buf[:total_needed]
            self._rx_queue.put(payload)
            self._logger.debug(f"Frame received: {length} bytes")

    def _manage_flow_control(self) -> None:
        '''
        Manage the flow control lines of the port
        '''
        #if we're connected, set the data terminal ready/ready-to-send lines
        #otherwise deassert them
        if(self._port is not None):
            if(self._port_connected):
                self._port.dtr = True
                self._port.rts = True
            else:
                self._port.dtr = False
                self._port.rts = False
