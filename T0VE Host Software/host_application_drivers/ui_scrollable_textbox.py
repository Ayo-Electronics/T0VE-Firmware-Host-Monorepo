import tkinter as tk
from tkinter import ttk
from tkinter.scrolledtext import ScrolledText
from pubsub import pub
from collections import deque
from typing import Optional, Any, Deque
import logging

class ScrollableTextBox(ttk.Frame):
    """
    A read-only, scrollable text display that appends lines via pubsub or direct calls,
    enforces a maximum number of lines, and performs UI updates from the Tk main thread.

    Subscribed topics:
    - f\"{ui_topic_root}.add\": add a line (payload is coerced to str)
    - f\"{ui_topic_root}.clear\": clear the display
    """

    def __init__(
        self,
        *,
        parent: ttk.Frame,
        ui_topic_root: str,
        max_num_lines: int = 500,
        logger: Optional[logging.Logger] = None,
        height: int = 12,
        **kwargs: Any   #forwarding frame keywords to parent
    ) -> None:
        """
        Create a ScrollableTextBox.

        - ui_topic_root: Root name for pubsub topics (.add, .clear)
        - max_num_lines: Maximum number of retained lines (oldest dropped when exceeded)
        - logger: Optional logger with .debug/.warning
        - height: Initial height of the text widget in rows
        """
        super().__init__(parent, **kwargs)
        self.ui_topic_root: str = ui_topic_root
        self.max_num_lines: int = max_num_lines
        self.logger = logger

        # Internal line buffer with automatic trimming
        self._lines: Deque[str] = deque(maxlen=self.max_num_lines)

        # Frame layout
        self.text_widget = ScrolledText(self, wrap=tk.WORD, state=tk.DISABLED, height=height)
        self.text_widget.pack(fill="both", expand=True)

        # Subscribe to pubsub topics
        pub.subscribe(self._on_clear, f"{self.ui_topic_root}.clear")
        pub.subscribe(self._on_add, f"{self.ui_topic_root}.add")

        if self.logger:
            self.logger.debug(
                f"ScrollableTextBox initialized (max_num_lines={self.max_num_lines}, height={height}) "
                f"and subscribed to {self.ui_topic_root}.clear/add"
            )

    # ----- Public API -----

    def add_line(self, text: Any) -> None:
        """Add a line (coerced to str). Safe to call from any thread."""
        text_str = "" if text is None else str(text)
        self.after(0, self._add_line_ui, text_str)

    def clear(self) -> None:
        """Clear the display. Safe to call from any thread."""
        self.after(0, self._clear_ui)

    # ----- PubSub handlers (can be called from non-main threads) -----

    def _on_clear(self, payload: Optional[Any] = None, topic: Any = pub.AUTO_TOPIC) -> None:
        """Request a clear operation via main-thread marshal."""
        self.clear()
        if self.logger:
            self.logger.debug("ScrollableTextBox: Clear requested via pubsub")

    def _on_add(self, payload: Optional[Any] = None, topic: Any = pub.AUTO_TOPIC) -> None:
        """
        Request an add-line operation via main-thread marshal.
        Payload is coerced to str.
        """
        self.add_line("" if payload is None else payload)
        if self.logger:
            self.logger.debug("ScrollableTextBox: Add requested via pubsub")

    # ----- Internal UI-thread operations (run on Tk main loop) -----

    def _add_line_ui(self, text_str: str) -> None:
        try:
            self._lines.append(str(text_str))
        except TypeError:
            self.logger.warning(f"_add_line_ui: Error appending line: {text_str} could not be coerced to string")
            return
        self._update_text_widget()

    def _clear_ui(self) -> None:
        self._lines.clear()
        self._update_text_widget()

    def _update_text_widget(self) -> None:
        """Rewrite the text widget content from the internal line buffer."""
        self.text_widget.configure(state=tk.NORMAL)
        self.text_widget.delete("1.0", tk.END)
        if self._lines:
            self.text_widget.insert(tk.END, "\n".join(self._lines))
        self.text_widget.see(tk.END)
        self.text_widget.configure(state=tk.DISABLED)

    # ----- Lifecycle -----

    def destroy(self) -> None:
        """Unsubscribe from pubsub topics and stop timers before destroying the widget."""
        try:
            pub.unsubscribe(self._on_clear, f"{self.ui_topic_root}.clear")
        except Exception:
            pass
        try:
            pub.unsubscribe(self._on_add, f"{self.ui_topic_root}.add")
        except Exception:
            pass
        # Allow base class to perform destruction
        super().destroy()
