import tkinter as tk
from tkinter import ttk
from typing import Any, Optional
from pubsub import pub
import logging

#TODO: add entry input validation helper

class _SmartWidgetBase(ttk.Frame):
    """
    Base class that handles Pypubsub subscription, thread-safety, and strict type checking.
    Layout: [Label] [Input Widget]
    """
    def __init__(self, parent, label_text: str, initial_value: Any, editable: bool, topic_string: str, logger: Optional[logging.Logger] = None, **kwargs):
        super().__init__(parent, **kwargs)
        self.topic = topic_string
        self.editable = editable
        
        # Store exact type for strict validation (Backend logic: type(new) == type(old))
        self.target_type = type(initial_value) 
        self.is_sequence = isinstance(initial_value, (list, tuple))
        self.target_len = len(initial_value) if self.is_sequence else 0
        self.target_elem_types = [type(elem) for elem in initial_value] if self.is_sequence else []

        # get/create a logger for this instance; pass to smart widgets
        self._log = logger or logging.getLogger(__name__ + "." + self.__class__.__name__)

        if(label_text is not None and label_text != ""):
            # Layout: Label on left, Widget container on right
            self._lbl = ttk.Label(self, text=label_text, width=20, anchor="w")
            self._lbl.pack(side="left", padx=(0, 10), anchor="n")
            
            self.content_frame = ttk.Frame(self)
            self.content_frame.pack(side="left", fill="x", expand=False, anchor="n") #TODO: change back to expand=True if I don't like
        else:
            # Layout: Widget container on left, no label
            self.content_frame = ttk.Frame(self)
            self.content_frame.pack(side="left", fill="x", expand=False, anchor="n") #TODO: change back to expand=True if I don't like

        #aggregate our validators by a type
        self._validators = {
            int: self._validate_input_int,
            float: self._validate_input_float,
            str: self._validate_input_string
        }

        # Create the UI Element specific to the subclass
        self.create_ui(self.content_frame, initial_value)

        # Subscribe to backend updates
        pub.subscribe(self._on_backend_update, self.topic)
        
        # Ensure we unsubscribe when the widget is destroyed
        self.bind("<Destroy>", self._on_destroy)

    def create_ui(self, parent, initial_value):
        """Override in subclasses."""
        pass

    def update_ui(self, new_value):
        """Override in subclasses."""
        pass

    def get_ui_value(self) -> Any:
        """
        Override in subclasses. 
        Must return value of type self.target_type or raise ValueError/TypeError.
        """
        return None

    def publish_change(self, *args):
        """
        Collects value, validates type/structure, and pushes to backend.
        Swallows validation errors (logs them) to prevent UI crashes.
        """
        # don't allow publishing if field isn't editable
        if not self.editable:
            return

        # lightweight value publish
        # backend will do type safety checking, UI widget will also do some type safety checking
        # but don't publish 'None' values to minimize issues/traffic
        val = self.get_ui_value()
        if(val is None):
            return
        pub.sendMessage(self.topic, payload=val)


    def _validate_input_int(self, val: Any) -> bool:
        """
        Validate the input value as an integer.
        """
        # don't allow inputs if field isn't editable
        if not self.editable:
            return False

        # Always allow empty string (user clearing field)
        if val == "":
            return True
        
        # allow intermediate sign entry
        if val in ("-", "+"):
            return True

        #otherwise just directly try int-casting
        try:
            int(val)
            return True
        except ValueError:
            return False
    
    def _validate_input_float(self, val: Any) -> bool:
        """
        Validate the input value.
        """
        # don't allow inputs if field isn't editable
        if not self.editable:
            return False

        # Always allow empty string (user clearing field)
        if val == "":
            return True
        
        # allow intermediate sign entry
        if val in ("-", "+", ".",  "-.", "+."):
            return True

        #otherwise just directly try float-casting
        try:
            float(val)
            return True
        except ValueError:
            return False

    def _validate_input_string(self, val: Any) -> bool:
        # don't allow inputs if field isn't editable
        if not self.editable:
            return False

        #otherwise always allow entry for strings
        return True

    def _on_backend_update(self, payload=None, **kwargs):
        """
        Listener for Pypubsub. Runs in Backend Thread.
        Performs strict validation before bridging to Main Thread.
        """
        # 1. Check Payload Existence
        if payload is None:
            return

        # 2. Strict Type Check (Incoming)
        # Note: We use strict type equality to match the backend's behavior
        if type(payload) is not self.target_type:
            # Edge case: If backend sends int but we expect float, it might be safe, 
            # but strict dict viewer usually forbids this. We will log and ignore.
            return

        # 3. Element-wise type checks for sequences
        if self.is_sequence:
            # check if the length of the payload is the same as the target length
            if len(payload) != self.target_len:
                return

            # iterate over the elements and check if they are the same type
            for i in range(self.target_len):
                if type(payload[i]) is not self.target_elem_types[i]:
                    return

        # 4. Schedule UI Update after Tkinter draws the current screen
        self.after_idle(lambda: self._safe_ui_update(payload))

    def _safe_ui_update(self, new_value):
        """Runs on Main Thread."""
        self.update_ui(new_value)

    def _on_destroy(self, event):
        pub.unsubscribe(self._on_backend_update, self.topic)
