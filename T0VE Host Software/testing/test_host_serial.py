#!/usr/bin/env python3

import sys
import logging
import threading
import argparse
from pathlib import Path
from typing import Optional, List

# Ensure project root is on sys.path so we can import device_interface.* regardless of CWD
PROJECT_ROOT = Path(__file__).resolve().parent.parent
if str(PROJECT_ROOT) not in sys.path:
    sys.path.insert(0, str(PROJECT_ROOT))

from host_application.host_device_serial import HostSerial


def setup_logger() -> logging.Logger:
    """Configure root logger to print all log messages to stdout and return a module logger."""
    root = logging.getLogger()
    root.setLevel(logging.DEBUG)

    if not root.handlers:
        console_handler = logging.StreamHandler(stream=sys.stdout)
        console_handler.setLevel(logging.DEBUG)
        console_handler.setFormatter(
            logging.Formatter(
                fmt="[%(asctime)s] %(levelname)s %(name)s: %(message)s",
                datefmt="%H:%M:%S",
            )
        )
        root.addHandler(console_handler)

    # Use a named logger that propagates to root (no extra handlers to avoid duplicates)
    logger = logging.getLogger("test_host_serial")
    logger.propagate = True
    return logger


def rx_poller(stop_event: threading.Event, host: HostSerial, logger: logging.Logger) -> None:
    """Continuously poll the Host_Serial RX queue and log received frames as INFO."""
    while not stop_event.is_set():
        frame = host.read_frame()
        if frame is None:
            # Sleep briefly without busy-waiting
            stop_event.wait(0.05)
            continue

        # Log payload in both hex and ASCII for readability
        try:
            hex_dump = frame.hex(' ')
        except Exception:
            hex_dump = ''
        try:
            ascii_dump = ''.join(chr(b) if 32 <= b < 127 else '.' for b in frame)
        except Exception:
            ascii_dump = ''
        logger.info(f"RX frame ({len(frame)} bytes) HEX: {hex_dump} ASCII: {ascii_dump}")


def main(argv: Optional[List[str]] = None) -> int:
    parser = argparse.ArgumentParser(
        description="Connect to a device by serial-number regex using Host_Serial and log RX frames."
    )
    #note: when passing in an external regex, don't include the `r` prefix. Just include the regex in single quotes
    #e.g. >>> python .\test_host_serial.py -r '^[0-9A-F]{24}_NODE_15$'
    parser.add_argument(
        "-r",
        "--regex",
        default=r'^[0-9A-F]{24}_NODE_(?:0[0-9]|1[0-5])$',
        help="Regular expression to match the device serial number (default matches 24 hex + _NODE_00-15)",
    )
    args = parser.parse_args(argv)

    logger = setup_logger()

    stop_event = threading.Event()

    try:
        # Pass our logger into Host_Serial so all class logs stream to the console
        with HostSerial(device_serial_regex=args.regex, logger=logger) as host:
            logger.info(f"Host_Serial instantiated for regex {args.regex}")

            # Immediately request a connection
            host.connect()
            logger.info("connect() requested; listening for frames (Ctrl+C to exit)...")

            # Spawn RX poller thread
            poller = threading.Thread(
                target=rx_poller,
                args=(stop_event, host, logger),
                name="RX-Poller",
                daemon=True,
            )
            poller.start()

            # Keep main thread alive until interrupted
            while not stop_event.is_set():
                stop_event.wait(1.0)

    except KeyboardInterrupt:
        logger.info("KeyboardInterrupt received; shutting down...")
    finally:
        stop_event.set()

    return 0


if __name__ == "__main__":
    raise SystemExit(main())


