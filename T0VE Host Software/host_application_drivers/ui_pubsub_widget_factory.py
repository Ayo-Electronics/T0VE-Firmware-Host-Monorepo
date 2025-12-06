import tkinter as tk
from tkinter import ttk
from enum import Enum
from typing import Any, Tuple, List, Optional
import logging

#widget specialization imports
from host_application_drivers.ui_pubsub_widget_base import _SmartWidgetBase
from host_application_drivers.ui_pubsub_widget_bool import SmartBoolWidget
from host_application_drivers.ui_pubsub_widget_enum import SmartEnumWidget
from host_application_drivers.ui_pubsub_widget_entry import SmartEntryWidget
from host_application_drivers.ui_pubsub_widget_list import SmartListFrame

class SmartWidgetFactory:
    """
    Static factory class to generate "smart" pypubsub-connected widgets.
    """

    @staticmethod
    def make_connected_widget(  parent: ttk.Frame, 
                                label_text: str,
                                initial_value: Any, 
                                editable: bool, 
                                listen_topic_string: str,
                                publish_topic_string: str,
                                logger: Optional[logging.Logger] = None) -> tk.Widget:
        """
        Main entry point. Analyzes type and returns the configured widget.
        """
        # get/create a logger for this instance; pass to smart
        logger = logger or logging.getLogger(__name__ + ".SmartWidgetFactory")

        # 1. Handle Lists/Tuples (Recursive/Composite Widget)
        if isinstance(initial_value, (list, tuple)):
            return SmartListFrame(  parent=parent, 
                                    label_text=label_text, 
                                    initial_value=initial_value, 
                                    editable=editable, 
                                    listen_topic_string=listen_topic_string, 
                                    publish_topic_string=publish_topic_string,
                                    logger=logger)

        # 2. Handle Enums
        if isinstance(initial_value, Enum):
            return SmartEnumWidget( parent=parent, 
                                    label_text=label_text, 
                                    initial_value=initial_value, 
                                    editable=editable, 
                                    listen_topic_string=listen_topic_string, 
                                    publish_topic_string=publish_topic_string,
                                    logger=logger)

        # 3. Handle Booleans
        if isinstance(initial_value, bool):
            return SmartBoolWidget( parent=parent, 
                                    label_text=label_text, 
                                    initial_value=initial_value, 
                                    editable=editable, 
                                    listen_topic_string=listen_topic_string, 
                                    publish_topic_string=publish_topic_string,
                                    logger=logger)

        # 4. Handle Standard Primitives (Int, Float, String)
        if isinstance(initial_value, (int, float, str)):
            return SmartEntryWidget(parent=parent, 
                                    label_text=label_text, 
                                    initial_value=initial_value, 
                                    editable=editable, 
                                    listen_topic_string=listen_topic_string, 
                                    publish_topic_string=publish_topic_string,
                                    logger=logger)

        # Fallback for unknown types
        f = ttk.Frame(parent)
        ttk.Label(f, text=label_text).pack(side="left")
        ttk.Label(f, text=f"Unsupported Type: {type(initial_value)}").pack(side="right")
        return f
