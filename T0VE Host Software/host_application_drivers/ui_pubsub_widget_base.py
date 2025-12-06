import tkinter as tk
from tkinter import ttk
from typing import Any, Optional
from pubsub import pub
import copy
import logging

from host_application_drivers.util_match_type_runtime import match_type

class _SmartWidgetBase(ttk.Frame):
    """
    Base class that handles Pypubsub subscription, thread-safety, and strict type checking.
    Layout: [Label] [Input Widget]
    """
    def __init__(   self, 
                    *,
                    parent: ttk.Frame, 
                    label_text: str, 
                    initial_value: Any, 
                    editable: bool, 
                    listen_topic_string: str, 
                    publish_topic_string: str, 
                    logger: Optional[logging.Logger] = None, 
                    **kwargs) -> None:

        super().__init__(parent, **kwargs)
        self._listen_topic = listen_topic_string
        self._publish_topic = publish_topic_string
        self._editable = editable

        # Store a template/example value for strict validation, including nested/compound structures.
        # This will be passed to `match_type` to validate both type and structure (lists, tuples, dicts, etc.).
        self._type_match_template = copy.deepcopy(initial_value)

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
        pub.subscribe(self._on_backend_update, self._listen_topic)
        
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
        if not self._editable:
            return

        # lightweight value publish
        # backend will do type safety checking, UI widget will also do some type safety checking
        # but don't publish 'None' values to minimize issues/traffic
        val = self.get_ui_value()
        if(val is None):
            return
        pub.sendMessage(self._publish_topic, payload=val)


    def _validate_input_int(self, val: Any) -> bool:
        """
        Validate the input value as an integer.
        """
        # don't allow inputs if field isn't editable
        if not self._editable:
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
        if not self._editable:
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
        if not self._editable:
            return False

        #otherwise always allow entry for strings
        return True

    def _on_backend_update(self, payload=None, **kwargs):
        """
        Listener for Pypubsub. Runs in Backend Thread.
        Performs strict validation before bridging to Main Thread.
        """
        # sanity check payload type/structure against the initial example/template value
        if not match_type(payload, self._type_match_template):
            self._log.warning(
                f"_on_backend_update: Type mismatch on {self._listen_topic}: "
                f"{type(payload)} does not match template type {type(self._type_match_template)}"
            )
            return

        # if type check passed, update the UI safely
        # copy the payload to ensure any modifications to the original payload 
        # don't mess with the local copy
        self.after_idle(lambda: self._safe_ui_update(copy.deepcopy(payload)))

    def _safe_ui_update(self, new_value):
        """Runs on Main Thread."""
        self.update_ui(new_value)

    def _on_destroy(self, event):
        pub.unsubscribe(self._on_backend_update, self._listen_topic)
