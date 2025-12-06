import tkinter as tk
from tkinter import ttk
from host_application_drivers.ui_pubsub_widget_base import _SmartWidgetBase

'''
Specialized widget for enum values; uses a Combobox widget.
'''

class SmartEnumWidget(_SmartWidgetBase):
    def create_ui(self, parent, initial_value):
        self.enum_cls = type(self._type_match_template)
        self.options = [e.name for e in self.enum_cls]
        
        self.var = tk.StringVar(value=initial_value.name)
        
        if self._editable:
            self.widget = ttk.Combobox(parent, textvariable=self.var, values=self.options, state="readonly")
            self.widget.bind("<<ComboboxSelected>>", self.publish_change)
        else:
            self.widget = ttk.Entry(parent, textvariable=self.var, state="readonly")
            
        self.widget.pack(fill="x", expand=False, anchor="n") #TODO: change back to expand=True if I don't like

    def update_ui(self, new_value):
        if isinstance(new_value, self.enum_cls):
            self.var.set(new_value.name)

    def get_ui_value(self):
        name = self.var.get()
        try:
            return self.enum_cls[name] # Raises KeyError if invalid name
        except KeyError:
            self._log.warning(f"Invalid enum name: {name}")
            return None #invalid name, subscriber should be graceful enough to handle error