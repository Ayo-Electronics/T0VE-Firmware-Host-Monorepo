import tkinter as tk
from tkinter import ttk
from enum import Enum
from typing import Any, Tuple, List, Optional
import logging

#widget specialization imports
from host_application.ui_pubsub_widget_base import _SmartWidgetBase
from host_application.ui_pubsub_widget_bool import SmartBoolWidget
from host_application.ui_pubsub_widget_enum import SmartEnumWidget
from host_application.ui_pubsub_widget_entry import SmartEntryWidget
from host_application.ui_pubsub_widget_list import SmartListFrame

class SmartWidgetFactory:
    """
    Static factory class to generate "smart" pypubsub-connected widgets.
    """

    @staticmethod
    def make_connected_widget(parent: tk.Widget, 
                              label_text: str,
                              initial_value: Any, 
                              editable: bool, 
                              topic_string: str,
                              logger: Optional[logging.Logger] = None) -> tk.Widget:
        """
        Main entry point. Analyzes type and returns the configured widget.
        """
        # get/create a logger for this instance; pass to smart
        logger = logger or logging.getLogger(__name__ + ".SmartWidgetFactory")

        # 1. Handle Lists/Tuples (Recursive/Composite Widget)
        if isinstance(initial_value, (list, tuple)):
            return SmartListFrame(parent, label_text, initial_value, editable, topic_string)

        # 2. Handle Enums
        if isinstance(initial_value, Enum):
            return SmartEnumWidget(parent, label_text, initial_value, editable, topic_string)

        # 3. Handle Booleans
        if isinstance(initial_value, bool):
            return SmartBoolWidget(parent, label_text, initial_value, editable, topic_string)

        # 4. Handle Standard Primitives (Int, Float, String)
        if isinstance(initial_value, (int, float, str)):
            return SmartEntryWidget(parent, label_text, initial_value, editable, topic_string)

        # Fallback for unknown types
        f = ttk.Frame(parent)
        ttk.Label(f, text=label_text).pack(side="left")
        ttk.Label(f, text=f"Unsupported Type: {type(initial_value)}").pack(side="right")
        return f
