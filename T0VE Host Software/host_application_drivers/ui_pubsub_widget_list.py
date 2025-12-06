import tkinter as tk
from tkinter import ttk
from host_application_drivers.ui_pubsub_widget_base import _SmartWidgetBase

'''
Specialized widget for list/tuple values; uses a Frame widget.
'''
class SmartListFrame(_SmartWidgetBase):
    def create_ui(self, parent, initial_value):
        self.children_widgets = []
        for i, item in enumerate(initial_value):
            row = ttk.Frame(parent)
            row.pack(fill="x", pady=1)
            ttk.Label(row, text=f"[{i}]", width=4, anchor="e").pack(side="left", padx=2) #label with index
            
            # create the appropriate child widget for the item type at the specified index
            child_widget = self._make_child_widget(row, item, i)
            child_widget.pack(side="left", fill="x", expand=False, anchor="n") #TODO: change back to expand=True if I don't like
            self.children_widgets.append(child_widget)

        # initialize our aggregate state using our initial value push
        self.current_value = initial_value

    def _make_child_widget(self, parent, value, index):
        #booleans get a check button
        if isinstance(value, bool):
            var = tk.BooleanVar(value=value)
            w = ttk.Checkbutton(parent, variable=var, 
                                command=lambda: self._on_child_change(index, var.get()))
            w.var = var

        # int/float/str get an entry widget
        elif isinstance(value, (int, float, str)):
            var = tk.StringVar(value=str(value))
            
            # register the validator for the entry widget; pass the new proposed value
            target_type = type(value)
            vcmd = (self.register(self._validators[target_type]), '%P')
            
            # create the entry widget with the validator, validating on keystroke entries
            w = ttk.Entry(parent, textvariable=var, validate="key", validatecommand=vcmd)
            
            # fire off this lambda functions on the return and focus out events; calls the `on_child_change` method
            cmd = lambda e: self._on_child_change(index, var.get(), target_type)
            w.bind("<Return>", cmd)
            w.bind("<FocusOut>", cmd)
            w.var = var
        
        # if it's something else we don't support, just create a label and string cast
        else:
            w = ttk.Label(parent, text=str(value))
        
        # if the widget is not editable, set the state to disabled
        if not self._editable and hasattr(w, 'state'):
            w.state(['disabled'])
        return w

    '''
    Normally, we'd directly bind the change event to the publish_change method.
    However, since we're a composite element that represents just a single variable, we 
    have to do some aggregation before we can publish the change.
    This function does that aggregation, and then explicitly calls the publish_change method.
    
    The `publish_change` method then pulls this updated aggregate value from `get_ui_value`, which reports
    this aggregated state
    '''
    def _on_child_change(self, index, new_val_raw, cast_type=None):
        try:
            # cast the passed in value from the sub-widget to the specified type if desired
            final_val = new_val_raw
            if cast_type:
                final_val = cast_type(new_val_raw)

            # cast our container into a list (useful if container is a tuple)
            # and update the value corresponding to the updated child
            current_list = list(self.current_value)
            current_list[index] = final_val
            
            # cast our container back into a tuple (if it was a tuple originally)
            reconstructed = tuple(current_list) if type(self._type_match_template) is tuple else current_list
            
            # push this updated list to the aggregate state, and publish
            self.current_value = reconstructed        
        #if casting fails for either of the two steps, just hit da bricks
        except ValueError:
            self._log.warning(f"Invalid value: {new_val_raw} for index {index}")
            return

        # publish the change
        self.publish_change()

    def update_ui(self, new_value):
        # update our aggregate state using the new value
        self.current_value = new_value

        # iterate over the children widgets and update the value of the corresponding child widget
        for i, widget in enumerate(self.children_widgets):
            if hasattr(widget, 'var'):
                # get the value from the new value list at the corresponding index
                val = self.current_value[i]
                if isinstance(val, bool):
                     widget.var.set(val)
                else:
                    # if the widget has focus, don't update the value (don't overwrite the user input)
                    if widget.focus_get() != widget:
                        widget.var.set(str(val))

    def get_ui_value(self):
        # return the current value of the aggregate state
        return self.current_value