'''
Host serial interface (physical/protocol layer) for USB Virtual COM Port (VCP) link between Python host software and a USB device.

General philosopy of this class is creating an asynchronous interface to the serial port managed by separate threads. 
Data transfer in and out of the serial port is done via queues, for atomic, independent operation of the serial port and the host software.
Threads in the system include:
 1) Port management thread: manages the connection/disconnection of the port
 2) TX thread: drains the TX queue and writes to the port
 3) RX thread: reads from the port and enqueues any new frames into the RX queue

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
from queue import Queue, Empty, Full            #sharing information between threads

try:
    import serial   #pyserial; make sure the module is installed
    from serial.tools import list_ports  # enumerate available ports
except Exception:   #pragma: no cover - pyserial may not be installed at edit time
    raise ImportError("pyserial is not installed! Please install it with 'pip install pyserial'")


 
class HostSerial:
    def __init__(
        self,
        *,
        device_serial_regex: Optional[str] = None,
        start_code: int = 0xEE,
        serial_buffer_size: int = 32768,
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
        self._serial_buffer_size: int = serial_buffer_size       #OS-level serial buffer size (Windows)
        self._logger = logger or logging.getLogger(__name__ + ".HostSerial")
        self._port_error_do_shutdown_signal = threading.Event()

        ######### TX-RELATED #########
        self._tx_queue: Queue[bytes] = Queue(maxsize=8)      #queue for outgoing bytes

        ######### RX-RELATED #########
        self._rx_buffer = bytearray()                   #buffer for incoming bytes
        self._rx_clear_signal = threading.Event()       #signal to clear the rx buffer (buffer clearing handled in the receive thread)
        self._rx_queue: Queue[bytes] = Queue()   #queue for completed RX frames

        ######### SPAWN THREAD ########
        self._stop_signal = threading.Event()
        self._tx_thread = threading.Thread(target=self._run_tx, name=f"host_serial_tx_{device_serial_regex or ''}", daemon=True)
        self._rx_thread = threading.Thread(target=self._run_rx, name=f"host_serial_rx_{device_serial_regex or ''}", daemon=True)
        self._port_thread = threading.Thread(target=self._run_port, name=f"host_serial_port_{device_serial_regex or ''}", daemon=True)
        # self._tx_thread.start()   #start the TX thread in `handle_connect`
        # self._rx_thread.start()   #start the RX thread in `handle_connect`
        self._port_thread.start()

    #=================== LIFECYCLE CONTROL ================
    def close(self, *, join_timeout: float = 1.0) -> None:
        """
        Request a graceful shutdown of the worker thread and wait briefly.
        Idempotent and safe to call multiple times.
        """
        try:
            self._stop_signal.set()
        except Exception:
            return  #If construction failed part-way, `_stop_signal` may not exist.

        try:
            self._tx_thread.join(timeout=join_timeout)
            self._rx_thread.join(timeout=join_timeout)
            self._port_thread.join(timeout=join_timeout)
        except Exception:
            pass    #Never raise during shutdown paths.

    def __enter__(self) -> "HostSerial":
        """Support `with` statement usage for deterministic shutdown."""
        return self

    def __exit__(self, exc_type, exc, tb) -> None:
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
        if(self._allowing_connections):
            return
        self._allowing_connections = True
        self._logger.info("connect() requested")

    def disconnect(self) -> None:
        '''
        Disconnect the port
        '''
        if(not self._allowing_connections):
            return
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
        try:
            self._tx_queue.put_nowait(frame)
        except Full:
            self._logger.warning(f"TX queue full (max 4); dropping frame ({length} bytes)")

    #### RX ####
    def read_frame(self, wait: bool = False, timeout: float = 0.1) -> Optional[bytes]:
        '''
        Read a frame from the RX queue. If no frame is available, return None.
        '''
        try:    
            if wait:
                return self._rx_queue.get(timeout=timeout)
            else:
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
                return

            #also check to see if we've disconnnected
            #in which case, recovery is irrelevant
            if not self._port_connected:
                return

            #otherwise, drop a 0 directly into the tx queue
            #and wait a little for the thread to process it
            try:
                self._tx_queue.put_nowait(b"\x00")
            except Full:
                self._logger.warning(f"TX queue full (max {self._tx_queue.maxsize}) during recover(); dropping byte 0x00")
            self._stop_signal.wait(float(inter_delay_s))

    #=================== THREAD FUNCTIONS =================
    #------------------- THREAD 1: TX -------------------
    def _run_tx(self) -> None:
        '''
        This thread function drains the TX queue and writes to the port.
        Uses blocking get with timeout to avoid spinning.
        '''
        #kill the transmit thread with the stop signal or the port error signal
        #in the case of a port error, thread will be restarted when we successfully reconnect
        while not self._stop_signal.is_set() and not self._port_error_do_shutdown_signal.is_set():
            # Block waiting for TX data (with timeout to check stop signal)
            try:
                to_transmit = self._tx_queue.get(timeout=0.1)
            except Empty:
                continue  # Timeout - loop back to check stop signal

            # Write to port
            try:
                self._logger.debug(f"TX {len(to_transmit)} bytes")
                self._port.write(to_transmit)
                self._port.flush()
            except Exception as exc:    #catch all excepitons, and consider them port issues
                self._logger.warning(f"Serial exception during TX: {exc}")
                self._port_error_do_shutdown_signal.set()  # Signal port thread to handle

    #------------------- THREAD 2: RX -------------------
    def _run_rx(self) -> None:
        '''
        This thread function reads from the port and enqueues any new frames into the RX queue.
        Uses blocking reads with timeout for efficiency.
        '''
        while not self._stop_signal.is_set() and not self._port_error_do_shutdown_signal.is_set():
            # Read from port + handle RX buffer flush - use blocking read for efficiency
            try:
                # Check if we need to clear the RX buffer
                if self._rx_clear_signal.is_set():
                    self._flush_rx_buffer()
                    self._rx_clear_signal.clear()

                # Check for pending data first
                pending = self._port.in_waiting
                if pending > 0:
                    # Data available - read it all
                    data = self._port.read(pending)
                else:
                    # No data - do short blocking read (port timeout handles this)
                    # Note: port.timeout should be set to a small value (e.g., 0.01-0.1s)
                    data = self._port.read(1)
                    # If we got a byte, grab any additional pending data
                    if data:
                        more = self._port.in_waiting
                        if more > 0:
                            data += self._port.read(more)

            except Exception as exc:    #consider all exceptions as issues with the port
                self._logger.warning(f"Serial exception during RX: {exc}")
                self._port_error_do_shutdown_signal.set()
                continue

            # Process received data (if any)
            if data:
                self._rx_buffer.extend(data)
                self._logger.debug(f"RX {len(data)} bytes")
                self._process_rx_buffer()

    #------------------- THREAD 3: PORT -------------------
    def _run_port(self) -> None:
        '''
        This thread function manages the serial port operation
         - connects/disconnects the port depending on desired connection state
         - manages flow control lines of the port
         - handles errors accessing the port
        '''
        #loop until we're told to stop
        while(not self._stop_signal.is_set()):
            #check to see if we need to connect/disconnect the port
            self._check_do_dis_connect()

            #manage flow control lines of the port
            try:
                self._manage_flow_control()
            except Exception as exc:
                self._logger.warning(f"Serial exception during Flow Control: {exc}")
                self._port_error_do_shutdown_signal.set()
                continue #immediately shut donw the port on error

            #sleep for a bit to avoid busy-waiting
            #shorter when connected (to catch errors quickly), longer when disconnected
            sleep_time = 0.1 if self._port_connected else 0.5
            self._stop_signal.wait(sleep_time)

    #============================== HELPER FUNCTIONS =============================
    #------------------- TX Helpers -------------------
    def _flush_tx_buffer(self) -> None:
        #just drain the transmit queue until it's empty
        while(True):
            try:
                self._tx_queue.get_nowait();
            except Empty:
                return

    #------------------- RX Helpers -------------------
    def _flush_rx_buffer(self) -> None:
        '''
        Flush the RX buffer
        '''
        self._logger.debug(f"Clearing RX buffer ({len(self._rx_buffer)} bytes)")
        self._rx_buffer.clear()
        
        try: #attempt to reset the port's receive buffer
            self._port.reset_input_buffer()
        except Exception as exc:
            #raise any exceptions upstream
            self._logger.warning(f"Serial exception during Buffer Clear: {exc}")
            raise exc

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

    #------------------- PORT Helpers -------------------
    def _check_do_dis_connect(self) -> None:
        '''
        Check to see if we need to connect/disconnect the port
        '''
        #if we are connected currently and we want to don't want to connect
        #or if there was some error with a serial port operation --> disconnect
        if(
            (self._port_connected and not self._allowing_connections)
            or self._port_error_do_shutdown_signal.is_set()
        ):
            #start by shutting down the TX and RX threads using the shutdown signal
            try:
                self._port_error_do_shutdown_signal.set()
                self._tx_thread.join(timeout=1)
                self._rx_thread.join(timeout=1)
            except Exception:
                pass #don't raise trying to shutdown port

            #now disconnect the port
            self._handle_disconnect()

            #and reset the shutdown/error signal since shutdown has been handled
            self._port_error_do_shutdown_signal.clear()

        #if we aren't connected currently and we want to connect
        #and if we have a proper node target (serial regex)
        elif(
            not self._port_connected
            and self._allowing_connections
            and self._serial_regex is not None
        ):
            self._handle_connect()
            if(self._port_connected):   #if we were able to successfully connect to the serial port, start tx/rx threads
                self._flush_rx_buffer() #flush any stale RX data
                self._flush_tx_buffer() #flush any stale inbound tx packets
                self._tx_thread.start() #start TX thread
                self._rx_thread.start() #start RX thread

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
                timeout=0.1,        # 100ms read timeout for efficient blocking reads
                write_timeout=1.0,  # Don't block forever on writes
            )
            
            # Request larger OS serial buffers (Windows-specific, ~no-op on other platforms)
            # Default is 4KB which causes fragmented reads for large transfers
            try:
                self._port.set_buffer_size(rx_size=self._serial_buffer_size, tx_size=self._serial_buffer_size)
                self._logger.debug(f"Set serial buffer size to {self._serial_buffer_size} bytes")
            except (AttributeError, serial.SerialException):
                self._logger.debug("Could not set buffer size (platform may not support it)")
            
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

    def _manage_flow_control(self) -> None:
        '''
        Manage the flow control lines of the port
        '''
        # Grab local reference (avoid race with disconnect)
        port = self._port
        if port is None:
            return
            
        # Set DTR/RTS based on connection state
        try:
            if self._port_connected:
                port.dtr = True
                port.rts = True
            else:
                port.dtr = False
                port.rts = False
        except Exception as exc:
            # Port may have been closed/disconnected, raise to caller
            raise exc
            
