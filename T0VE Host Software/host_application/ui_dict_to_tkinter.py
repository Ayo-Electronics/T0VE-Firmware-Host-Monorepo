import tkinter as tk
from tkinter import ttk
from typing import Dict, List, Union
from enum import Enum


LeafValue = Union[str, int, bool, List[int], List[bool], Enum]
NestedDict = Dict[str, Union["NestedDict", LeafValue, None]]


class ScrollableFrame(ttk.Frame):
    """A frame with both vertical and horizontal scrollbars."""
    def __init__(self, parent, *args, **kwargs):
        super().__init__(parent, *args, **kwargs)

        self.canvas = tk.Canvas(self, highlightthickness=0)
        self.v_scrollbar = ttk.Scrollbar(self, orient="vertical", command=self.canvas.yview)
        self.h_scrollbar = ttk.Scrollbar(self, orient="horizontal", command=self.canvas.xview)

        self.canvas.configure(
            yscrollcommand=self.v_scrollbar.set,
            xscrollcommand=self.h_scrollbar.set,
        )

        self.canvas.grid(row=0, column=0, sticky="nsew")
        self.v_scrollbar.grid(row=0, column=1, sticky="ns")
        self.h_scrollbar.grid(row=1, column=0, sticky="ew")

        self.grid_rowconfigure(0, weight=1)
        self.grid_columnconfigure(0, weight=1)

        # Interior frame inside the canvas
        self.interior = ttk.Frame(self.canvas)
        self.interior_id = self.canvas.create_window((0, 0), window=self.interior, anchor="nw")

        # Configure scrollregion
        self.interior.bind("<Configure>", self._on_interior_configure)
        self.canvas.bind("<Configure>", self._on_canvas_configure)

    def _on_interior_configure(self, event):
        # Update scroll region to encompass the inner frame
        self.canvas.configure(scrollregion=self.canvas.bbox("all"))

    def _on_canvas_configure(self, event):
        # Optionally stretch the interior to canvas width
        canvas_width = event.width
        self.canvas.itemconfig(self.interior_id, width=canvas_width)


class StateViewerApp(tk.Tk):
    def __init__(self, data: NestedDict, title: str = "State Viewer", layout_pattern: str = "v"):
        super().__init__()
        self.title(title)
        self.layout_pattern = layout_pattern or "v"

        # Keep references to all tk.Variable objects so they don't get GC'ed
        self._vars: List[tk.Variable] = []

        # Global styles
        style = ttk.Style(self)
        style.configure("TNotebook.Tab", padding=(10, 4))
        style.configure("TabHeader.TLabel", font=("TkDefaultFont", 10, "bold"))

        # Root container with scrollbars
        scroll = ScrollableFrame(self)
        scroll.pack(fill="both", expand=True)
        container = scroll.interior

        # Build UI for the nested dict
        self.build_dict_ui(container, data, depth=0)

        # Reasonable starting size; scrollbars handle overflow
        self.update_idletasks()
        self.minsize(600, 400)

    def _mode_for_depth(self, depth: int) -> str:
        """
        Return layout mode for this depth:
          'h' = horizontal tiling
          'v' = vertical tiling
          't' = tabs
        """
        if depth < len(self.layout_pattern):
            c = self.layout_pattern[depth].lower()
        else:
            c = self.layout_pattern[-1].lower()

        if c.startswith("h"):
            return "h"
        if c.startswith("t"):
            return "t"
        return "v"  # default

    def build_dict_ui(self, parent: tk.Widget, data: NestedDict, depth: int = 0):
        """
        Recursively create UI elements for a nested dictionary.

        Each depth level is laid out according to:
        - 'h': grid, row-first (horizontal) with wrapping
        - 'v': grid, column-first (vertical) with wrapping
        - 't': tabs (one tab per key, with replicated header bar)
        """
        # Filter out None leaves
        items = [(k, v) for k, v in data.items() if v is not None]
        if not items:
            return

        mode = self._mode_for_depth(depth)

        if mode == "t":
            # Tabs at this level
            level_frame = ttk.Frame(parent)
            level_frame.pack(fill="both", expand=True, anchor="nw", pady=4, padx=4)

            notebook = ttk.Notebook(level_frame)
            notebook.pack(fill="both", expand=True)

            for key, value in items:
                tab = ttk.Frame(notebook)
                # Extra text padding: leading & trailing spaces
                notebook.add(tab, text=f" {key} ")

                # Header bar inside the tab:  --- key ---
                header = ttk.Frame(tab)
                header.pack(fill="x", pady=(4, 4), padx=4)

                left_sep = ttk.Separator(header, orient="horizontal")
                left_sep.grid(row=0, column=0, sticky="ew", padx=(0, 4))

                header_label = ttk.Label(header, text=key, style="TabHeader.TLabel")
                header_label.grid(row=0, column=1)

                right_sep = ttk.Separator(header, orient="horizontal")
                right_sep.grid(row=0, column=2, sticky="ew", padx=(4, 0))

                header.grid_columnconfigure(0, weight=1)
                header.grid_columnconfigure(2, weight=1)

                # Content area for nested dictionary or leaf widgets
                content = ttk.Frame(tab)
                content.pack(fill="both", expand=True, padx=4, pady=(0, 4))

                if isinstance(value, dict):
                    # Child dict: next depth inside content
                    self.build_dict_ui(content, value, depth=depth + 1)
                else:
                    # Leaf: just render value widget
                    self._create_leaf_content(content, value)

        else:
            # Tiled layout (horizontal or vertical) using a grid
            level_frame = ttk.Frame(parent)
            level_frame.pack(fill="both", expand=True, anchor="nw", pady=4, padx=4)

            n = len(items)

            if mode == "h":
                # Horizontal stacking with wrapping into new rows
                max_cols = min(n, 4)  # up to 4 columns per row
                for idx, (key, value) in enumerate(items):
                    row = idx // max_cols
                    col = idx % max_cols

                    block = ttk.LabelFrame(level_frame, text=key, padding=4)
                    block.grid(row=row, column=col, sticky="nsew", padx=4, pady=4)

                    if isinstance(value, dict):
                        self.build_dict_ui(block, value, depth=depth + 1)
                    else:
                        self._create_leaf_content(block, value)

                # Make columns expand equally
                for c in range(max_cols):
                    level_frame.grid_columnconfigure(c, weight=1)

            else:  # mode == "v"
                # Vertical stacking with wrapping into new columns
                max_rows = min(n, 8)  # up to 8 rows per column
                for idx, (key, value) in enumerate(items):
                    col = idx // max_rows
                    row = idx % max_rows

                    block = ttk.LabelFrame(level_frame, text=key, padding=4)
                    block.grid(row=row, column=col, sticky="nsew", padx=4, pady=4)

                    if isinstance(value, dict):
                        self.build_dict_ui(block, value, depth=depth + 1)
                    else:
                        self._create_leaf_content(block, value)

                # Make columns expand equally
                max_cols = (n + max_rows - 1) // max_rows
                for c in range(max_cols):
                    level_frame.grid_columnconfigure(c, weight=1)

    def _create_leaf_content(self, parent: tk.Widget, value: LeafValue):
        # Enum support: display by enum name
        from enum import Enum as _Enum  # avoid mypy confusion
        if isinstance(value, _Enum):
            self._create_str_widget(parent, value.name)
            return

        # Dispatch based on type
        if isinstance(value, bool):
            self._create_bool_widget(parent, value)
        elif isinstance(value, int):
            self._create_int_widget(parent, value)
        elif isinstance(value, str):
            self._create_str_widget(parent, value)
        elif isinstance(value, list):
            self._create_list_widget(parent, value)
        else:
            # Fallback: just show repr
            self._create_str_widget(parent, repr(value))

    def _create_bool_widget(self, parent: tk.Widget, value: bool):
        row = ttk.Frame(parent)
        row.pack(fill="x", anchor="w")
        var = tk.BooleanVar(value=value)
        self._vars.append(var)  # keep a reference
        cb = ttk.Checkbutton(row, variable=var)
        cb.state(["disabled"])  # display only (for now)
        cb.pack(side="left")

    def _create_int_widget(self, parent: tk.Widget, value: int):
        row = ttk.Frame(parent)
        row.pack(fill="x", anchor="w")
        var = tk.StringVar(value=str(value))
        self._vars.append(var)  # keep a reference
        entry = ttk.Entry(row, textvariable=var, width=12, state="readonly")
        entry.pack(side="left")

    def _create_str_widget(self, parent: tk.Widget, value: str):
        row = ttk.Frame(parent)
        row.pack(fill="x", anchor="w")
        var = tk.StringVar(value=value)
        self._vars.append(var)  # keep a reference
        entry = ttk.Entry(row, textvariable=var, width=30, state="readonly")
        entry.pack(side="left")

    def _create_list_widget(self, parent: tk.Widget, value: List[Union[int, bool]]):
        """
        For now, treat lists of ints/bools as a leaf: display them in a Listbox.
        """
        row = ttk.Frame(parent)
        row.pack(fill="both", anchor="w", expand=True)

        listbox = tk.Listbox(row, height=min(5, max(1, len(value))))
        listbox.pack(fill="both", expand=True)

        for item in value:
            listbox.insert(tk.END, str(item))

        # Display only – disable selection by preventing focus
        listbox.configure(exportselection=False)
        listbox.bind("<FocusIn>", lambda e: self.focus())


def show_state(
    data: NestedDict,
    title: str = "State Viewer",
    layout_pattern: str = "v",
):
    """
    Convenience function to pop up a window for a given nested state dict.

    layout_pattern: string like "thvv" where each char specifies the
    layout at the corresponding depth:
      't' = tabs
      'h' = horizontal tiling with wrapping
      'v' = vertical tiling with wrapping

    If depth exceeds the pattern length, the last character is reused.
    """
    app = StateViewerApp(data, title=title, layout_pattern=layout_pattern)
    app.mainloop()

if __name__ == "__main__":
    # for this test, render the default command states without eeprom
    import betterproto
    import pprint
    from host_application.state_proto_node_default import NodeStateDefaults
    example_state = NodeStateDefaults.default_all_no_eeprom()
    pprint.pprint(example_state.to_dict(include_default_values=True), width=120, compact=False)

    show_state(example_state.to_dict(include_default_values=True), title="Example State Viewer", layout_pattern="thvv")
