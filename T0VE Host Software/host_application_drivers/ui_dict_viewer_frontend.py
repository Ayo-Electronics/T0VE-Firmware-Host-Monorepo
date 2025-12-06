# ui_dict_viewer_frontend.py

from __future__ import annotations

from tkinter import ttk
from typing import Any, Dict, Iterable, Optional, Sequence
import logging

from host_application_drivers.ui_pubsub_widget_factory import SmartWidgetFactory
from host_application_drivers.ui_dict_viewer_aggregator import Path


class DictViewerFrontend(ttk.Frame):
    """
    Tkinter UI that visualizes a nested dictionary using pubsub-connected widgets.

    It does **not** own the data; it just:
      - walks the reference_dict shape,
      - for each leaf builds a SmartWidget connected to the matching topic,
      - lays out sections using tabs / horizontal / vertical tiling.

    Topic convention (must match DictViewerAggregator):
        listens to updates from aggregator:     <ui_topic_root>.frontend.set.<key1>.<key2>....<keyN>
        publishes updates to aggregator:        <ui_topic_root>.frontend.get.<key1>.<key2>....<keyN>

    Parameters
    ----------
    reference_dict : dict
        The nested dictionary to visualize. This should match the aggregator's reference dict.
    ui_topic_root : str
        Root string for pubsub topics; same value you pass to DictViewerAggregator.
    editable_paths : Iterable[Path], optional
        Iterable of tuple paths that are editable. Non-listed paths are read-only.
        Paths are tuples of keys, e.g. ("cob_temp", "status", "temperature_celsius").
    layout_pattern : str
        Pattern of 't'|'h'|'v' specifying layout per depth:
            't' -> tabs
            'h' -> horizontal tiling (wrap to new row)
            'v' -> vertical tiling (wrap to new column)
        Example: "thvv" = top-level tabs, then horizontal, then vertical, then vertical.
    """

    def __init__(
        self,
        *,
        parent: ttk.Frame,
        reference_dict: Dict[Any, Any],
        ui_topic_root: str,
        editable_paths: Optional[Iterable[Path]] = None,
        layout_pattern: str = "v",
        logger: Optional[logging.Logger] = None,
        **kwargs,
    ) -> None:
        super().__init__(parent, **kwargs)
        self._log = logger or logging.getLogger(__name__ + ".DictViewerFrontend")

        self._ref = reference_dict
        self._ui_topic_root = ui_topic_root
        self._layout_pattern = layout_pattern or "v"

        # Normalize editable paths into a set of tuples
        if editable_paths is None:
            self._editable_paths = set()
        else:
            self._editable_paths = {tuple(p) for p in editable_paths}

        # Basic style tweaks for tabs and headers
        style = ttk.Style(self)
        style.configure("TNotebook.Tab", padding=(10, 4))
        style.configure("TabHeader.TLabel", font=("TkDefaultFont", 10, "bold"))

        # Build UI from reference dict shape
        self._build_dict_ui(self, self._ref, depth=0, path=())

    # ------------------------------------------------------------------
    # Helpers
    # ------------------------------------------------------------------
    def _mode_for_depth(self, depth: int) -> str:
        """
        Return layout mode for this depth:
          'h' = horizontal tiling
          'v' = vertical tiling
          't' = tabs
          If depth is greater than the length of the layout pattern, use the last character of the layout pattern
        """
        if depth < len(self._layout_pattern):
            c = self._layout_pattern[depth].lower()
        else:
            c = self._layout_pattern[-1].lower()

        if c.startswith("h"):
            return "h"
        if c.startswith("t"):
            return "t"
        return "v"  # default

    def _listen_topic_for_path(self, path: Path) -> str:
        """
        Build a pubsub topic string for a given dict path such that it matches
        DictViewerAggregator._listen_topic_for_path:
            <ui_topic_root>.frontend.set.<key1>.<key2>...<keyN>
        """
        suffix = ".".join(str(p) for p in path)
        return f"{self._ui_topic_root}.frontend.set.{suffix}"

    def _publish_topic_for_path(self, path: Path) -> str:
        """
        Build a pubsub topic string for a given dict path such that it matches
        DictViewerAggregator._publish_topic_for_path:
            <ui_topic_root>.frontend.get.<key1>.<key2>...<keyN>
        """
        suffix = ".".join(str(p) for p in path)
        return f"{self._ui_topic_root}.frontend.get.{suffix}"
        
    def _is_editable(self, path: Path) -> bool:
        """Return True if this leaf path is editable."""
        return path in self._editable_paths

    # ------------------------------------------------------------------
    # UI building
    # ------------------------------------------------------------------
    def _build_dict_ui(
        self,
        parent: ttk.Frame,
        data: Dict[Any, Any],
        depth: int,
        path: Path,
    ) -> None:
        """
        Recursively create UI elements for a nested dictionary.

        Each depth level is laid out according to:
          - 'h': grid, row-first (horizontal) with wrapping
          - 'v': grid, column-first (vertical) with wrapping
          - 't': tabs (one tab per key, with replicated header bar)

        Leaves that are None are not rendered.
        """
        # Filter out None-valued leaves at this level
        items = [(k, v) for k, v in data.items() if v is not None]
        if not items:
            return

        mode = self._mode_for_depth(depth)

        if mode == "t":
            self._build_tabs(parent, items, depth, path)
        else:
            self._build_tiled(parent, items, depth, path, mode)

    def _build_tabs(
        self,
        parent: ttk.Frame,
        items: Sequence[tuple[Any, Any]],
        depth: int,
        path: Path,
    ) -> None:
        """
        Build a tabbed layout for this level. Each key at this level is a tab.
        """
        level_frame = ttk.Frame(parent)
        level_frame.pack(fill="both", expand=True, anchor="nw", pady=4, padx=4)

        notebook = ttk.Notebook(level_frame)
        notebook.pack(fill="both", expand=True)

        for key, value in items:
            child_path = path + (key,)
            tab = ttk.Frame(notebook)
            # Extra padding before/after tab label
            notebook.add(tab, text=f" {key} ")

            # Header bar inside the tab:  --- key ---
            header = ttk.Frame(tab)
            header.pack(fill="x", pady=(4, 4), padx=4)

            left_sep = ttk.Separator(header, orient="horizontal")
            left_sep.grid(row=0, column=0, sticky="ew", padx=(0, 4))

            header_label = ttk.Label(header, text=str(key), style="TabHeader.TLabel")
            header_label.grid(row=0, column=1)

            right_sep = ttk.Separator(header, orient="horizontal")
            right_sep.grid(row=0, column=2, sticky="ew", padx=(4, 0))

            header.grid_columnconfigure(0, weight=1)
            header.grid_columnconfigure(2, weight=1)

            # Content area for the tab
            content = ttk.Frame(tab)
            content.pack(fill="both", expand=True, padx=4, pady=(0, 4))

            if isinstance(value, dict):
                self._build_dict_ui(content, value, depth + 1, child_path)
            else:
                self._create_leaf_widget(content, key, value, child_path)

    def _build_tiled(
        self,
        parent: ttk.Frame,
        items: Sequence[tuple[Any, Any]],
        depth: int,
        path: Path,
        mode: str,
    ) -> None:
        """
        Build a tiled layout (horizontal 'h' or vertical 'v') for this level.
        """
        level_frame = ttk.Frame(parent)
        level_frame.pack(fill="both", expand=True, anchor="nw", pady=4, padx=4)

        n = len(items)

        if mode == "h":
            # Horizontal stacking with wrapping into new rows
            max_cols = min(n, 4)  # up to 4 columns per row
            for idx, (key, value) in enumerate(items):
                row = idx // max_cols
                col = idx % max_cols

                child_path = path + (key,)

                #draw a block around any downstream children to indicate "containment"
                block = ttk.LabelFrame(level_frame, text=str(key), padding=4)
                block.grid(row=row, column=col, sticky="nsew", padx=4, pady=4)

                #build children
                if isinstance(value, dict):
                    self._build_dict_ui(block, value, depth + 1, child_path)
                else:
                    self._create_leaf_widget(block, key, value, child_path)

            # Make columns expand equally
            for c in range(max_cols):
                level_frame.grid_columnconfigure(c, weight=1)

        else:  # mode == "v"
            # Vertical stacking with wrapping into new columns
            max_rows = min(n, 8)  # up to 8 rows per column
            for idx, (key, value) in enumerate(items):
                col = idx // max_rows
                row = idx % max_rows

                child_path = path + (key,)

                #draw a block around any downstream children to indicate "containment"
                block = ttk.LabelFrame(level_frame, text=str(key), padding=4)
                block.grid(row=row, column=col, sticky="nsew", padx=4, pady=4)

                #build children
                if isinstance(value, dict):
                    self._build_dict_ui(block, value, depth + 1, child_path)
                else:
                    self._create_leaf_widget(block, key, value, child_path)

            # Make columns expand equally
            max_cols = (n + max_rows - 1) // max_rows
            for c in range(max_cols):
                level_frame.grid_columnconfigure(c, weight=1)

    def _create_leaf_widget(
        self,
        parent: ttk.Frame,
        key: Any,
        value: Any,
        path: Path,
    ) -> None:
        """
        Create a SmartWidgetFactory-based widget for a leaf.
        """
        listen_topic_string = self._listen_topic_for_path(path)
        publish_topic_string = self._publish_topic_for_path(path)
        editable = self._is_editable(path)

        # The SmartWidgetFactory handles creating the label + widget and wiring pubsub.
        widget = SmartWidgetFactory.make_connected_widget(
            parent=parent,
            label_text="",
            initial_value=value,
            editable=editable,
            listen_topic_string=listen_topic_string,
            publish_topic_string=publish_topic_string,
        )
        widget.pack(fill="x", expand=True, anchor="w", pady=1)

