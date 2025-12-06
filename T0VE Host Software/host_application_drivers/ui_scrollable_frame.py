from tkinter import ttk
import tkinter as tk


class ScrollableFrame(ttk.Frame):
    """
    A frame with vertical and horizontal scrollbars, containing an interior frame.
    Use .interior as the parent for your content.
    """
    def __init__(self, parent, *args, **kwargs):
        super().__init__(parent, *args, **kwargs)

        self._canvas = tk.Canvas(self, highlightthickness=0)
        self._v_scrollbar = ttk.Scrollbar(self, orient="vertical", command=self._canvas.yview)
        self._h_scrollbar = ttk.Scrollbar(self, orient="horizontal", command=self._canvas.xview)

        self._canvas.configure(
            yscrollcommand=self._v_scrollbar.set,
            xscrollcommand=self._h_scrollbar.set,
        )

        self._canvas.grid(row=0, column=0, sticky="nsew")
        self._v_scrollbar.grid(row=0, column=1, sticky="ns")
        self._h_scrollbar.grid(row=1, column=0, sticky="ew")

        self.grid_rowconfigure(0, weight=1)
        self.grid_columnconfigure(0, weight=1)

        # Interior frame inside the canvas
        self._interior = ttk.Frame(self._canvas)
        self._interior_id = self._canvas.create_window((0, 0), window=self._interior, anchor="nw")

        # Configure scrollregion
        self._interior.bind("<Configure>", self._on_interior_configure)
        self._canvas.bind("<Configure>", self._on_canvas_configure)

    def _on_interior_configure(self, event):
        # Update scroll region to encompass the inner frame
        self._canvas.configure(scrollregion=self._canvas.bbox("all"))

    def _on_canvas_configure(self, event):
        # Optionally stretch the interior to canvas width
        canvas_width = event.width
        self._canvas.itemconfig(self._interior_id, width=canvas_width)

    @property
    def interior(self) -> ttk.Frame:
        return self._interior
        