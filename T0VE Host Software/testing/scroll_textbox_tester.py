import threading
import time
import tkinter as tk
from tkinter import ttk
from pubsub import pub

from host_application_drivers.ui_scrollable_textbox import ScrollableTextBox


def main() -> None:
    root = tk.Tk()
    root.title("ScrollableTextBox Tester")

    ui_topic_root = "test.scrollbox"

    container = ttk.Frame(root, padding=8)
    container.pack(fill="both", expand=True)

    # Widget under test
    scrollbox = ScrollableTextBox(
        container,
        ui_topic_root=ui_topic_root,
        max_num_lines=50,
        height=12,
        logger=None,
    )
    scrollbox.pack(fill="both", expand=True)

    # Controls
    controls = ttk.Frame(container)
    controls.pack(fill="x", pady=(8, 0))

    entry = ttk.Entry(controls, width=50)
    entry.insert(0, "Type a custom line and click a button...")
    entry.grid(row=0, column=0, columnspan=3, sticky="ew", padx=(0, 8))
    controls.grid_columnconfigure(0, weight=1)

    # Simple counters for demo lines
    api_counter = {"n": 0}
    pub_counter = {"n": 0}

    def add_via_api() -> None:
        api_counter["n"] += 1
        text = entry.get() or f"API line {api_counter['n']}"
        scrollbox.add_line(f"API[{api_counter['n']}]: {text}")

    def add_via_pubsub() -> None:
        pub_counter["n"] += 1
        text = entry.get() or f"Pub line {pub_counter['n']}"
        pub.sendMessage(f"{ui_topic_root}.add", payload=f"PUB[{pub_counter['n']}]: {text}")

    def clear_api() -> None:
        scrollbox.clear()

    def clear_pub() -> None:
        pub.sendMessage(f"{ui_topic_root}.clear")

    ttk.Button(controls, text="Add via API", command=add_via_api).grid(row=1, column=0, sticky="ew")
    ttk.Button(controls, text="Add via PubSub", command=add_via_pubsub).grid(row=1, column=1, sticky="ew", padx=8)
    ttk.Button(controls, text="Clear (API)", command=clear_api).grid(row=1, column=2, sticky="ew")
    ttk.Button(controls, text="Clear (PubSub)", command=clear_pub).grid(row=1, column=3, sticky="ew", padx=(8, 0))

    # Burst adders to test trimming behavior
    bulk_controls = ttk.Frame(container)
    bulk_controls.pack(fill="x", pady=(8, 0))

    def schedule_bulk_add(count: int, source: str) -> None:
        idx = {"i": 0}

        def step() -> None:
            if idx["i"] >= count:
                return
            idx["i"] += 1
            line = f"BULK-{source} #{idx['i']}"
            if source == "API":
                scrollbox.add_line(line)
            else:
                pub.sendMessage(f"{ui_topic_root}.add", payload=line)
            # Small delay between inserts to keep UI responsive
            root.after(10, step)

        step()

    ttk.Button(bulk_controls, text="Add 100 (API)", command=lambda: schedule_bulk_add(100, "API")).pack(side="left")
    ttk.Button(bulk_controls, text="Add 100 (PubSub)", command=lambda: schedule_bulk_add(100, "PUB")).pack(
        side="left", padx=(8, 0)
    )

    # Background thread demo to validate cross-thread safety (pubsub path)
    def threaded_publisher() -> None:
        for i in range(10):
            pub.sendMessage(f"{ui_topic_root}.add", payload=f"[Thread] message {i+1}")
            time.sleep(0.2)

    t = threading.Thread(target=threaded_publisher, daemon=True)
    t.start()

    root.mainloop()


if __name__ == "__main__":
    main()


