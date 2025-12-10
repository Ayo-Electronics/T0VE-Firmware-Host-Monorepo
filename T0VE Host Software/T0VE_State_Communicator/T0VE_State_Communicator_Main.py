'''
T0VE State Communicator
Use this application to communicate with a single node of the T0VE system.
This is basically a GUI wrapper around the raw protobuf messages. To use:
1) Run this application
2) GUI should first ask you which node you'd like to communicate with via a dropdown
3) Once selected, the GUI will launch another window displaying the port status, state of the selected node, and debug messages received from the node
4) Editable fields will be able to take your input, and immediately send them down to the node
    - entry fields will send when the "enter" key is pressed or the focus changes
    - checkboxes will send when the checkbox is toggled
    - dropdowns will send when the selection changes
    - NOTE: some editable fields may change by themselves--some of these are cleared/sanity checked by the node 
'''

from mimetypes import init
import tkinter as tk
from tkinter import ttk
from typing import List
import logging
from pubsub import pub
import pprint

import sv_ttk

from host_application_drivers.host_device_state_serdes import HostDeviceStateSerdes
from host_application_drivers.ui_dict_viewer_module import DictViewerModule
from host_application_drivers.ui_scrollable_frame import ScrollableFrame
from host_application_drivers.state_proto_node_default import NodeStateDefaults
from host_application_drivers.state_proto_defs import Communication, NodeState, Debug
from host_application_drivers.util_flat_dict import FlatDict
from host_application_drivers.ui_scrollable_textbox import ScrollableTextBox
from T0VE_State_Communicator.tsc_dispatcher import *

def connection_prompt(  root: tk.Tk, 
                        options: List[str], 
                        title: str = "T0VE State Communicator - Select Node", 
                        prompt: str = "Select which node you'd like to communicate with:") -> str:
    """
    Displays a modal selection dialog with given options. 
    Blocks until user makes a selection and clicks OK (or presses Enter).
    Returns the selected value as a string.
    """
    # Create a Toplevel for modal dialog if root already shown, else use root
    root.title(title)
    root.resizable(False, False)  # Make the window non-resizeable

    selected_var = tk.StringVar(value=options[0])
    selection = {"value": None}

    def submit():
        selection["value"] = selected_var.get()
        root.quit()     # ends mainloop for this window only
        root.destroy()  # closes the window

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

    # themeing using sv-ttk
    sv_ttk.set_theme("light")

    # Run this window's event loop until selection
    root.mainloop()

    return selection["value"]

if __name__ == "__main__":

    #instantiate a logger up front
    logger = logging.getLogger("t0ve.state_communicator")
    logger.setLevel(logging.INFO)
    if not logger.handlers:
        handler = logging.StreamHandler()
        handler.setFormatter(
            logging.Formatter("%(asctime)s [%(levelname)s] %(name)s: %(message)s")
        )
        logger.addHandler(handler)

    #start by launching a GUI window asking for the node name
    root = tk.Tk()
    node_options = ["0", "1", "2", "3", "4", "15", "Any"]
    selected = connection_prompt(
        root,
        node_options,
        title="T0VE State Communicator - Select Node",
        prompt="Select which node you'd like to communicate with:"
    )
    if selected is None:
        logger.error("No node selected, exiting...")
        exit()
    
    # figure out all the root topics as necessary
    node_state_root_topic = f"app.devices.node_{selected.zfill(2)}"
    node_debug_root_topic = f"app.devices.node_{selected.zfill(2)}.debug"
    node_port_root_topic = f"app.devices.node_{selected.zfill(2)}.port"

    ui_state_topic_root = f"app.ui.node_state"
    ui_debug_topic_root = f"app.ui.debug_terminal"
    ui_port_topic_root = f"app.ui.port"

    logger.info(f"Node state root topic: {node_state_root_topic}")
    logger.info(f"Node debug root topic: {node_debug_root_topic}")
    logger.info(f"Node port root topic: {node_port_root_topic}")
    logger.info(f"UI state topic root: {ui_state_topic_root}")
    logger.info(f"UI debug topic root: {ui_debug_topic_root}")
    logger.info(f"UI port topic root: {ui_port_topic_root}")

    # create a message dispatcher--sits in between UI elements and host/device state serdes
    link_node_port_info(ui_port_topic_root, node_port_root_topic, logger)
    link_node_debug_info(ui_debug_topic_root, node_debug_root_topic, logger)
    link_node_debug_termctrl(ui_debug_topic_root, node_port_root_topic, logger)
    link_node_state(ui_state_topic_root, node_state_root_topic, logger)

    # ============= MAIN GUI LAYOUT ==============
    # UI window contains three elements (window will be scrollable):
    # 1) port status information as a dict viewer (left on the window, fixed width (or adjustable divider))
    # 2) state of the selected node as a dict viewer (right on the window, rest of the width)
    # 3) debug messages as a scrollable textbox (bottom of the window, min height (may be adjustable), rest of the width)

    root = tk.Tk()
    root.title("T0VE State Communicator")

    # Create a PanedWindow (main container) with vertical split: top/bottom
    main_pane = tk.PanedWindow(root, orient=tk.VERTICAL, sashrelief=tk.SUNKEN, sashwidth=6, showhandle=False)
    main_pane.pack(fill="both", expand=True)

    # Top paned window: horizontal split (left/right)
    top_pane = tk.PanedWindow(main_pane, orient=tk.HORIZONTAL, sashrelief=tk.SUNKEN, sashwidth=6, showhandle=False)
    main_pane.add(top_pane, stretch="always", minsize=200)

    ###### PORT STATUS ######
    editable_port_paths = [key for key in FlatDict.flatten(DEFAULT_PORT_STATE()).keys() if "command" in key]
    logger.info(f"Editable port paths: {editable_port_paths}")

    port_status_frame = ScrollableFrame(top_pane)
    port_status_dict_viewer = DictViewerModule(
        reference_dict=DEFAULT_PORT_STATE(),
        editable_paths=editable_port_paths,
        layout_pattern="vv",
        parent=port_status_frame.interior,
        ui_topic_root=ui_port_topic_root,
        logger=logger
    )
    # Ensure the frontend frame is managed within the scrollable interior
    port_status_dict_viewer.frontend.pack(fill="both", expand=True)
    top_pane.add(port_status_frame, minsize=250, stretch="always")

    ###### NODE STATE ######
    # build initial node state dictionary from default settings
    initial_node_state = NodeStateDefaults.default_all_no_eeprom()
    initial_node_state_dict = initial_node_state.to_dict(include_default_values=True)
    
    #flatten our default state, create editable paths
    paths = FlatDict.flatten(initial_node_state_dict).keys()
    paths_editable = [path for path in paths if any("command" in str(p) for p in path)]
    paths_editable.append(("doSystemReset",))
    paths_editable.remove(('comms', 'command', 'allowConnection')) #don't let the user disable connections, will lock out until reset

    #and build our UI elements
    node_state_frame = ScrollableFrame(top_pane)
    node_state_dict_viewer = DictViewerModule(
        reference_dict=initial_node_state_dict,
        editable_paths=paths_editable,
        layout_pattern="thvv",
        parent=node_state_frame.interior,
        ui_topic_root=ui_state_topic_root,
        logger=logger
    )
    node_state_dict_viewer.frontend.pack(fill="both", expand=True)
    top_pane.add(node_state_frame, minsize=300, stretch="always")

    ###### DEBUG OUTPUT ######
    debug_frame = ScrollableTextBox(
        parent=main_pane, 
        ui_topic_root=ui_debug_topic_root,
        max_num_lines=100,
        height=12,
        logger=logger,
        padding=10
    )
    main_pane.add(debug_frame, minsize=100, stretch="always")


    ######## INSTANTIATE SERDES #########
    #given the selected node, set up the serdes accordingly
    #and additionally pick out the pub/sub topics for state messages, debug messages, and port status
    serdes = HostDeviceStateSerdes(
        node_index=selected,
        default_poll_s=2.0,
        max_poll_s=0.1,
        rx_timeout_s=10.0,
        logger=logger
    )

    ###### RUN THE MAIN LOOP ######
    sv_ttk.set_theme("light")
    root.mainloop()
