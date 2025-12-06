import tkinter as tk
from tkinter import ttk
from typing import Any, Dict, Iterable, Optional
from host_application_drivers.ui_dict_viewer_module import DictViewerModule, Path

import betterproto
import pprint
from host_application_drivers.state_proto_node_default import NodeStateDefaults
from host_application_drivers.util_flat_dict import FlatDict
from host_application_drivers.ui_scrollable_frame import ScrollableFrame
import logging
from pubsub import pub

if __name__ == "__main__":

    # Logger
    logger = logging.getLogger("t0ve.testing.dict_viewer_test")
    logger.setLevel(logging.DEBUG)
    if not logger.handlers:
        handler = logging.StreamHandler()
        handler.setFormatter(
            logging.Formatter("%(asctime)s [%(levelname)s] %(name)s: %(message)s")
        )
        logger.addHandler(handler)

    #subscribe to dictionary publishes
    #will just pretty-print the entire dictionary when it receives a publish
    def _on_dictionary(payload=None, topic=pub.AUTO_TOPIC):
        try:
            topic_name = topic.getName() if topic is not None else ""
        except Exception:
            topic_name = ""
        if isinstance(payload, dict):
            logger.info(f"{topic_name}: Dictionary message:\n{pprint.pformat(payload, indent=2)}")
        else:
            logger.info(f"{topic_name}: Received non-dictionary payload: {payload!r}")

    # aggregator publishes nested snapshots on the `.nested.get` topic
    pub.subscribe(_on_dictionary, "app.ui.nested.get")

    #create a dictionary from the 
    example_state = NodeStateDefaults.default_all_no_eeprom()
    example_state_dict = example_state.to_dict(include_default_values=True)
    pprint.pprint(example_state_dict, width=120, compact=False)

    #flatten our default state, create editable paths
    paths = FlatDict.flatten(example_state_dict).keys()
    paths_editable = [path for path in paths if any("command" in str(p) for p in path)]
    paths_editable.append(("doSystemReset",))
    paths_editable.remove(('comms', 'command', 'allowConnection')) #don't let the user disable connections, will lock out until reset

    #create our Tkinter window
    root = tk.Tk()
    root.title("Dict Viewer Test")

    scrollable_frame = ScrollableFrame(root)
    scrollable_frame.pack(fill="both", expand=True)

    #create our DictViewerModule
    dict_viewer = DictViewerModule(
        reference_dict=example_state_dict,
        parent=scrollable_frame.interior,
        ui_topic_root="app.ui",
        editable_paths=paths_editable,
        layout_pattern="thvv",
        logger=logger,
    )
    dict_viewer.frontend.pack(fill="both", expand=True)

    root.mainloop()