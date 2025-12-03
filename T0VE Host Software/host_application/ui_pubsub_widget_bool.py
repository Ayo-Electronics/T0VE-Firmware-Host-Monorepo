import tkinter as tk
from tkinter import ttk
from host_application.ui_pubsub_widget_base import _SmartWidgetBase

'''
Specialized widget for boolean values; uses a Checkbutton widget.
'''
class SmartBoolWidget(_SmartWidgetBase):
    def create_ui(self, parent, initial_value):
        self.var = tk.BooleanVar(value=initial_value)
        state = "normal" if self.editable else "disabled"
        
        # We don't need text here because the Base class handles the Label
        self.widget = ttk.Checkbutton(
            parent, 
            variable=self.var, 
            command=self.publish_change,
            state=state
        )
        self.widget.pack(anchor="w")

    def update_ui(self, new_value):
        if self.var.get() != new_value:
            self.var.set(new_value)

    def get_ui_value(self):
        return self.var.get()