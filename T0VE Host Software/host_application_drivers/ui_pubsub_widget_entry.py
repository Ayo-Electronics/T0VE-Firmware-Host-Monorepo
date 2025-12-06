import tkinter as tk
from tkinter import ttk
from host_application_drivers.ui_pubsub_widget_base import _SmartWidgetBase

'''
Specialized widget for string/int/float values; uses a Entry widget.
'''
class SmartEntryWidget(_SmartWidgetBase):
    def create_ui(self, parent, initial_value):
        self.var = tk.StringVar(value=str(initial_value))
        state = "normal" if self._editable else "readonly"
        
        # register the validator for the entry widget; pass the new proposed value
        vcmd = (self.register(self._validators[type(self._type_match_template)]), "%P")
        
        # create the entry widget with the validator, validating on keystroke entries
        self.widget = ttk.Entry(parent, textvariable=self.var, state=state,
                                validate="key", validatecommand=vcmd)
        self.widget.pack(fill="x", expand=False, anchor="n") #TODO: change back to expand=True if I don't like

        if self._editable:
            self.widget.bind("<Return>", self.publish_change)
            self.widget.bind("<FocusOut>", self.publish_change)

    def update_ui(self, new_value):
        if(self._editable):
            # if the field is editable, only push changes if the user is not editing
            # (prevents fighting cursor)
            if self.widget.focus_get() != self.widget:
                self.var.set(str(new_value))
        else:
            # if the field is not editable, push changes immediately
            self.var.set(str(new_value))

    def get_ui_value(self):
        val_str = self.var.get()
        
        # explicit casting to original type; 
        if type(self._type_match_template) is int:
            try:
                return int(val_str)
            except ValueError:
                self._log.warning(f"Invalid int value: {val_str}")
                return None #invalid int, subscriber should be graceful enough to handle error

        elif type(self._type_match_template) is float:
            try:
                return float(val_str)
            except ValueError:
                self._log.warning(f"Invalid float value: {val_str}")
                return None #invalid float, subscriber should be graceful enough to handle error
            
        return val_str


