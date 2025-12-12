'''
Test File Request GUI

This script exercises the file request functionality of the HostDeviceStateSerdes.
Usage:
1) Run this script
2) Select which node to connect to
3) Use the GUI to search for files on the node and download them locally

File reading works by issuing NeuralMemFileAccess requests segment-by-segment,
reconstructing the file locally.
'''

import tkinter as tk
from tkinter import ttk
from typing import List, Optional, Dict
import logging
import os
import threading
import time
from pubsub import pub

import sv_ttk

from host_application_drivers.host_device_state_serdes import HostDeviceStateSerdes
import betterproto
from host_application_drivers.state_proto_defs import (
    NeuralMemFileRequest, 
    NeuralMemFileList, 
    NeuralMemFileInfo,
    NeuralMemFileAccess
)

# Maximum chunk size for file reads
MAX_CHUNK_SIZE = 16384

# UI update throttling - only update every N chunks or N bytes
UI_UPDATE_INTERVAL_BYTES = 4096  # Update progress every 4KB


def connection_prompt(
    root: tk.Tk,
    options: List[str],
    title: str = "File Request Test - Select Node",
    prompt: str = "Select which node you'd like to connect to:"
) -> Optional[str]:
    """
    Displays a modal selection dialog with given options.
    Blocks until user makes a selection and clicks OK (or presses Enter).
    Returns the selected value as a string, or None if window was closed.
    """
    root.title(title)
    root.resizable(False, False)

    selected_var = tk.StringVar(value=options[0])
    selection = {"value": None}

    def submit():
        selection["value"] = selected_var.get()
        root.quit()
        root.destroy()

    # Frame for alignment
    row_frame = ttk.Frame(root)
    row_frame.pack(pady=30, padx=30)

    prompt_label = ttk.Label(row_frame, text=prompt)
    prompt_label.pack(side="left", padx=(0, 10))

    opt_menu = ttk.OptionMenu(row_frame, selected_var, options[0], *options)
    opt_menu.pack(side="left", padx=(0, 10))

    launch_btn = ttk.Button(row_frame, text="OK", command=submit)
    launch_btn.pack(side="left")

    # Enter key submits
    root.bind("<Return>", lambda event: submit())

    # Theming
    sv_ttk.set_theme("light")

    root.mainloop()
    return selection["value"]


class FileRequestTestGUI:
    """
    Main GUI for testing file requests.
    Communicates with HostDeviceStateSerdes exclusively via pub/sub.
    """

    def __init__(self, root: tk.Tk, node_index: str, logger: logging.Logger):
        self.root = root
        self.node_index = node_index
        self.logger = logger

        # Topic roots for pub/sub
        self.node_root = f"app.devices.node_{node_index.zfill(2)}"
        self.port_status_topic = f"{self.node_root}.port.status"
        self.port_command_topic = f"{self.node_root}.port.command"
        self.file_request_topic = f"{self.node_root}.file_request"
        self.file_response_topic = f"{self.node_root}.file_response"

        # State tracking
        self.is_connected = False
        self.file_list: List[NeuralMemFileInfo] = []
        self.pending_file_response: Optional[NeuralMemFileAccess] = None
        self.file_response_event = threading.Event()

        # Build UI
        self._build_ui()

        # Subscribe to pub/sub topics
        self._setup_subscriptions()

        # Instantiate the serdes (this starts communication threads)
        # Use shorter timeouts for faster file transfer
        # - Longer default_poll_s reduces state update frequency during transfers
        # - Shorter rx_timeout_s means faster retry on failed chunks
        self.serdes = HostDeviceStateSerdes(
            node_index=node_index,
            default_poll_s=5.0,   # Less frequent state polling (was 2.0)
            max_poll_s=0.2,       # Slower trigger polling (was 0.1)
            rx_timeout_s=3.0,     # Faster timeout for quicker retry (was 10.0)
            logger=logger
        )

        # Handle window close
        self.root.protocol("WM_DELETE_WINDOW", self._on_close)

        # Initial UI state
        self._update_ui_state()

    def _build_ui(self):
        """Construct all UI elements."""
        self.root.title(f"File Request Test - Node {self.node_index}")
        self.root.resizable(True, True)
        self.root.minsize(500, 350)

        # Main container with padding
        main_frame = ttk.Frame(self.root, padding=20)
        main_frame.pack(fill="both", expand=True)

        # ===== CONNECTION STATUS =====
        status_frame = ttk.LabelFrame(main_frame, text="Connection Status", padding=10)
        status_frame.pack(fill="x", pady=(0, 15))

        # Connection indicator
        conn_row = ttk.Frame(status_frame)
        conn_row.pack(fill="x")

        ttk.Label(conn_row, text="Status:").pack(side="left")
        self.status_label = ttk.Label(conn_row, text="Disconnected", foreground="red")
        self.status_label.pack(side="left", padx=(5, 20))

        # Connect/Disconnect button
        self.connect_btn = ttk.Button(conn_row, text="Connect", command=self._on_connect_toggle)
        self.connect_btn.pack(side="left")

        # Port info
        info_row = ttk.Frame(status_frame)
        info_row.pack(fill="x", pady=(5, 0))

        ttk.Label(info_row, text="Port:").pack(side="left")
        self.port_label = ttk.Label(info_row, text="---")
        self.port_label.pack(side="left", padx=(5, 20))

        ttk.Label(info_row, text="Serial:").pack(side="left")
        self.serial_label = ttk.Label(info_row, text="---")
        self.serial_label.pack(side="left", padx=(5, 0))

        # ===== FILE OPERATIONS =====
        file_frame = ttk.LabelFrame(main_frame, text="File Operations", padding=10)
        file_frame.pack(fill="both", expand=True, pady=(0, 15))

        # Row 1: Search for files
        search_row = ttk.Frame(file_frame)
        search_row.pack(fill="x", pady=(0, 10))

        self.search_btn = ttk.Button(search_row, text="Search for Files", command=self._on_search_files)
        self.search_btn.pack(side="left")

        self.search_status = ttk.Label(search_row, text="")
        self.search_status.pack(side="left", padx=(10, 0))

        # Row 2: File selection dropdown
        select_row = ttk.Frame(file_frame)
        select_row.pack(fill="x", pady=(0, 10))

        ttk.Label(select_row, text="Select File:").pack(side="left")

        self.file_var = tk.StringVar(value="(no files)")
        self.file_dropdown = ttk.Combobox(select_row, textvariable=self.file_var, state="readonly", width=50)
        self.file_dropdown.pack(side="left", padx=(10, 0), fill="x", expand=True)

        # Row 3: Local filename entry
        name_row = ttk.Frame(file_frame)
        name_row.pack(fill="x", pady=(0, 10))

        ttk.Label(name_row, text="Save As:").pack(side="left")

        self.local_name_var = tk.StringVar(value="")
        self.local_name_entry = ttk.Entry(name_row, textvariable=self.local_name_var, width=50)
        self.local_name_entry.pack(side="left", padx=(10, 0), fill="x", expand=True)

        # Row 4: Read file button
        read_row = ttk.Frame(file_frame)
        read_row.pack(fill="x", pady=(0, 10))

        self.read_btn = ttk.Button(read_row, text="Read File", command=self._on_read_file)
        self.read_btn.pack(side="left")

        self.read_status = ttk.Label(read_row, text="")
        self.read_status.pack(side="left", padx=(10, 0))

        # ===== PROGRESS =====
        progress_frame = ttk.Frame(file_frame)
        progress_frame.pack(fill="x", pady=(10, 0))

        self.progress_var = tk.DoubleVar(value=0)
        self.progress_bar = ttk.Progressbar(progress_frame, variable=self.progress_var, maximum=100)
        self.progress_bar.pack(fill="x")

        self.progress_label = ttk.Label(progress_frame, text="")
        self.progress_label.pack(pady=(5, 0))

        # ===== STATS =====
        stats_frame = ttk.LabelFrame(main_frame, text="Transfer Statistics", padding=10)
        stats_frame.pack(fill="x", pady=(10, 15))

        self.stats_label = ttk.Label(stats_frame, text="No transfer yet")
        self.stats_label.pack()

        # ===== LOG OUTPUT =====
        log_frame = ttk.LabelFrame(main_frame, text="Log", padding=10)
        log_frame.pack(fill="both", expand=True)

        self.log_text = tk.Text(log_frame, height=8, state="disabled", wrap="word")
        self.log_text.pack(fill="both", expand=True, side="left")

        log_scroll = ttk.Scrollbar(log_frame, orient="vertical", command=self.log_text.yview)
        log_scroll.pack(side="right", fill="y")
        self.log_text.config(yscrollcommand=log_scroll.set)

    def _setup_subscriptions(self):
        """Subscribe to relevant pub/sub topics."""
        # Port status updates
        pub.subscribe(self._on_port_connected, f"{self.port_status_topic}.connected")
        pub.subscribe(self._on_port_name, f"{self.port_status_topic}.port_name")
        pub.subscribe(self._on_port_serial, f"{self.port_status_topic}.serial_number")

        # File responses
        pub.subscribe(self._on_file_response, self.file_response_topic)

    def _on_port_connected(self, payload: bool):
        """Handle connection status updates."""
        self.is_connected = payload
        self.root.after(0, self._update_ui_state)

    def _on_port_name(self, payload: str):
        """Handle port name updates."""
        self.root.after(0, lambda: self.port_label.config(text=payload))

    def _on_port_serial(self, payload: str):
        """Handle serial number updates."""
        self.root.after(0, lambda: self.serial_label.config(text=payload))

    def _on_file_response(self, payload: NeuralMemFileRequest):
        """Handle file response from node."""
        self.logger.info(f"Received file response") #: {payload}")

        # Use betterproto's which_one_of to safely discriminate the oneof payload
        which, inner_payload = betterproto.which_one_of(payload, "payload")

        if which == "file_list" and inner_payload is not None:
            # This is a file list response
            file_list: NeuralMemFileList = inner_payload
            self.file_list = file_list.files if file_list.files else []
            self.root.after(0, self._update_file_dropdown)
            self.root.after(0, lambda: self.search_status.config(text=f"Found {len(self.file_list)} file(s)"))
            self._log(f"Found {len(self.file_list)} file(s) on node")

        elif which == "file_access" and inner_payload is not None:
            # This is a file access response
            self.pending_file_response = inner_payload
            self.file_response_event.set()
        
        else:
            self._log(f"Unknown file response type: {which}")

    def _update_file_dropdown(self):
        """Update the file dropdown with current file list."""
        if not self.file_list:
            self.file_dropdown["values"] = ["(no files)"]
            self.file_var.set("(no files)")
            return

        # Format: "filename (size bytes)"
        items = [f"{f.filename} ({f.filesize} bytes)" for f in self.file_list]
        self.file_dropdown["values"] = items
        if items:
            self.file_var.set(items[0])
            # Auto-populate local filename
            self.local_name_var.set(self.file_list[0].filename)

    def _update_ui_state(self):
        """Enable/disable UI elements based on connection state."""
        if self.is_connected:
            self.status_label.config(text="Connected", foreground="green")
            self.connect_btn.config(text="Disconnect")
            state = "normal"
        else:
            self.status_label.config(text="Disconnected", foreground="red")
            self.connect_btn.config(text="Connect")
            state = "disabled"

        # Enable/disable file operation controls
        self.search_btn.config(state=state)
        self.file_dropdown.config(state="readonly" if self.is_connected else "disabled")
        self.local_name_entry.config(state=state)
        self.read_btn.config(state=state)

    def _on_connect_toggle(self):
        """Handle connect/disconnect button click."""
        pub.sendMessage(f"{self.port_command_topic}.request_connect", payload=not self.is_connected)

    def _on_search_files(self):
        """Send a file list request to the node."""
        self._log("Searching for files...")
        self.search_status.config(text="Searching...")

        # Send an empty NeuralMemFileList in a NeuralMemFileRequest
        file_list_request = NeuralMemFileRequest(file_list=NeuralMemFileList(files=[]))
        pub.sendMessage(self.file_request_topic, payload=file_list_request)

    def _on_read_file(self):
        """Start reading the selected file from the node."""
        # Get selected file
        selected = self.file_var.get()
        if selected == "(no files)" or not self.file_list:
            self._log("No file selected")
            return

        # Find the file info
        selected_idx = self.file_dropdown.current()
        if selected_idx < 0 or selected_idx >= len(self.file_list):
            self._log("Invalid file selection")
            return

        file_info = self.file_list[selected_idx]
        local_name = self.local_name_var.get().strip()

        if not local_name:
            self._log("Please enter a local filename")
            return

        # Disable UI during transfer
        self._set_transfer_ui_state(False)

        # Start file transfer in background thread
        transfer_thread = threading.Thread(
            target=self._transfer_file,
            args=(file_info, local_name),
            daemon=True
        )
        transfer_thread.start()

    def _set_transfer_ui_state(self, enabled: bool):
        """Enable/disable UI during file transfer."""
        state = "normal" if enabled else "disabled"
        self.search_btn.config(state=state)
        self.file_dropdown.config(state="readonly" if enabled else "disabled")
        self.local_name_entry.config(state=state)
        self.read_btn.config(state=state)
        self.connect_btn.config(state=state)

    def _transfer_file(self, file_info: NeuralMemFileInfo, local_name: str):
        """
        Transfer a file from the node segment-by-segment.
        Runs in a background thread.
        """
        filename = file_info.filename
        filesize = file_info.filesize
        
        self._log(f"Starting transfer of '{filename}' ({filesize} bytes)")
        self.root.after(0, lambda: self.read_status.config(text="Transferring..."))
        self.root.after(0, lambda: self.progress_var.set(0))
        self.root.after(0, lambda: self.stats_label.config(text="Transfer in progress..."))

        # Buffer for reconstructed file
        file_data = bytearray()
        offset = 0
        max_retries = 3
        last_ui_update = 0  # Track bytes since last UI update
        chunk_timeout = 2.0  # Shorter timeout for faster retry (was 10.0)
        
        # Timing and stats
        start_time = time.perf_counter()
        chunks_transferred = 0
        retries_total = 0

        while offset < filesize:
            # Calculate chunk size
            remaining = filesize - offset
            chunk_size = min(remaining, MAX_CHUNK_SIZE)

            # Try to read this chunk with retries
            success = False
            for attempt in range(max_retries):
                # Only log on retry attempts to reduce overhead
                if attempt > 0:
                    self._log(f"Retry {attempt + 1} for offset {offset}")
                    retries_total += 1

                # Prepare read request
                read_request = NeuralMemFileRequest(
                    file_access=NeuralMemFileAccess(
                        filename=filename,
                        offset=offset,
                        read_nwrite=True,
                        data=bytes(chunk_size)  # Length determines how much to read
                    )
                )

                # Clear previous response and send request
                self.file_response_event.clear()
                self.pending_file_response = None
                pub.sendMessage(self.file_request_topic, payload=read_request)

                # Wait for response (shorter timeout for faster retry)
                if self.file_response_event.wait(timeout=chunk_timeout):
                    response = self.pending_file_response
                    if response is not None and len(response.data) > 0:
                        # Verify response matches our request
                        if response.filename == filename and response.offset == offset and response.read_nwrite:
                            file_data.extend(response.data)
                            offset += len(response.data)
                            last_ui_update += len(response.data)
                            chunks_transferred += 1
                            success = True

                            # Throttled UI updates - only update every UI_UPDATE_INTERVAL_BYTES
                            if last_ui_update >= UI_UPDATE_INTERVAL_BYTES or offset >= filesize:
                                progress = (offset / filesize) * 100
                                elapsed = time.perf_counter() - start_time
                                speed = offset / elapsed if elapsed > 0 else 0
                                self.root.after(0, lambda p=progress: self.progress_var.set(p))
                                self.root.after(0, lambda o=offset, s=filesize, spd=speed: 
                                    self.progress_label.config(text=f"{o} / {s} bytes ({spd/1024:.1f} KB/s)"))
                                last_ui_update = 0
                            break
                        else:
                            self._log(f"Response mismatch: expected {filename}@{offset}, got {response.filename}@{response.offset}")
                    else:
                        self._log(f"Empty data in response, retrying...")
                else:
                    self._log(f"Timeout waiting for response, retrying...")

            if not success:
                elapsed = time.perf_counter() - start_time
                stats = f"Failed after {elapsed:.2f}s, {chunks_transferred} chunks, {retries_total} retries"
                self._log(f"Failed to read chunk at offset {offset} after {max_retries} attempts")
                self.root.after(0, lambda: self.read_status.config(text="Transfer failed"))
                self.root.after(0, lambda s=stats: self.stats_label.config(text=s))
                self.root.after(0, lambda: self._set_transfer_ui_state(True))
                return

        # Calculate final stats
        elapsed = time.perf_counter() - start_time
        speed = filesize / elapsed if elapsed > 0 else 0

        # Write file to disk
        try:
            # Save in the script's directory
            script_dir = os.path.dirname(os.path.abspath(__file__))
            local_path = os.path.join(script_dir, local_name)

            with open(local_path, 'wb') as f:
                f.write(file_data)

            stats = f"{filesize} bytes in {elapsed:.2f}s ({speed/1024:.1f} KB/s), {chunks_transferred} chunks, {retries_total} retries"
            self._log(f"File saved to: {local_path}")
            self._log(stats)
            self.root.after(0, lambda: self.read_status.config(text=f"Saved: {local_name}"))
            self.root.after(0, lambda: self.progress_var.set(100))
            self.root.after(0, lambda: self.progress_label.config(text=f"Complete: {len(file_data)} bytes"))
            self.root.after(0, lambda s=stats: self.stats_label.config(text=s))

        except Exception as e:
            self._log(f"Failed to save file: {e}")
            self.root.after(0, lambda: self.read_status.config(text="Save failed"))

        # Re-enable UI
        self.root.after(0, lambda: self._set_transfer_ui_state(True))

    def _log(self, message: str):
        """Add a message to the log text widget."""
        self.logger.info(message)

        def append():
            self.log_text.config(state="normal")
            self.log_text.insert("end", message + "\n")
            self.log_text.see("end")
            self.log_text.config(state="disabled")

        self.root.after(0, append)

    def _on_close(self):
        """Handle window close."""
        self.logger.info("Closing file request test GUI")
        try:
            self.serdes.close()
        except Exception:
            pass
        self.root.destroy()


def main():
    # Set up logging
    logger = logging.getLogger("t0ve.file_request_test")
    logger.setLevel(logging.INFO)
    if not logger.handlers:
        handler = logging.StreamHandler()
        handler.setFormatter(
            logging.Formatter("%(asctime)s [%(levelname)s] %(name)s: %(message)s")
        )
        logger.addHandler(handler)

    # Node selection dialog
    root = tk.Tk()
    node_options = ["0", "1", "2", "3", "4", "15", "Any"]
    selected = connection_prompt(
        root,
        node_options,
        title="File Request Test - Select Node",
        prompt="Select which node you'd like to connect to:"
    )

    if selected is None:
        logger.error("No node selected, exiting...")
        return

    logger.info(f"Selected node: {selected}")

    # Launch main GUI
    root = tk.Tk()
    sv_ttk.set_theme("light")

    app = FileRequestTestGUI(root, selected, logger)
    root.mainloop()


if __name__ == "__main__":
    main()

