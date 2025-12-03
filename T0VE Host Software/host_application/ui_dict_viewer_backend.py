from __future__ import annotations

from typing import Any, Dict, Iterable, Tuple, Optional
import threading
import copy
import logging

from pubsub import pub
from host_application.util_flat_dict import FlatDict

Path = Tuple[Any, ...]

class DictViewerBackend:
    '''
    Constructor for the DictViewerBackend class.
    - reference_dict: the reference dictionary to use for the backend (should be shared with the frontend)
    - ui_topic_root: the root topic to use for the pub/sub interface; all topics will be prefixed with this root
    - editable_paths: the paths to the dictionary that are editable by the UI (should be shared with the frontend)
    - ui_max_publish_rate_s: the maximum rate at which the backend can publish updated dictionary values to subscribers
    - logger: the logger to use for the backend

    NOTE: makes sure to start the frontend before the backend! This way, initial state pushes from the backend aren't
    dropped by the frontend

    topic format:
    [ui_topic_root].entries.[path1].[path2].[path3]...[pathN]   #publishes of individual dictionary entries
    [ui_topic_root].nested                                      #publishes of the entire nested dictionary
    '''
    def __init__(   self, 
                    reference_dict: Dict[Any, Any],
                    ui_topic_root: str,
                    editable_paths: Iterable[Path],
                    ui_max_publish_rate_s: float = 0.1,
                    logger: Optional[logging.Logger] = None) -> None:
        
        #deep copy the reference dictionary so upstream edits of the reference never change the local copy
        self._ui_topic_root = ui_topic_root
        self._editable_paths = set(editable_paths)
        self._log = logger or logging.getLogger(__name__ + ".dict_viewer_backend")

        # some other private variables
        # we'll flatten the dictionary to a single layer for easy spontaneous writes from publishers
        # publish cache ensures publishes only occur when dictionary values change; reduces pub/sub traffic
        # topic_for_path is a dictionary of paths to topics for easy lookup; can compute once and use later
        self._flat_dict = FlatDict.flatten(reference_dict)
        self._publish_cache: Dict[str, Any] = {}
        self._topic_for_path: Dict[Path, str] = {
            path: self._topic_from_path(path) for path in self._flat_dict.keys()
        }

        # threading events
        # lock ensures thread-safe access to the dictionary
        # ui_update_event is set when a UI-driven update is received
        # ui_update_publisher is a thread that publishes the edited when UI edits are made
        # stop is a thread stop event to best-effort close other threads upon shutdown
        self._lock = threading.RLock()
        self._ui_update_event = threading.Event()
        self._ui_update_publisher = threading.Thread(   target=self._ui_update_publisher_thread, 
                                                        name="dict_viewer_backend_ui_update_publisher_" + self._ui_topic_root, 
                                                        daemon=True)
        self._ui_max_publish_rate_s: float = ui_max_publish_rate_s
        self._stop = threading.Event()

        # initial publishes/subscriptions
        # need to do this after some threading primitives created
        # Subscribe to editable topics (UI -> backend).
        for path in self._editable_paths:
            if path in self._flat_dict:
                self._log.info(f"Subscribing to editable path: {path} -> topic '{self._topic_for_path[path]}'")
                self._subscribe_topic(path)
            else:
                self._log.warning(f"Editable path {path} not in reference dict; subscription skipped.")

        # Initial publish of the reference dict.
        #TODO: may not even be necessary, if same reference dict is used to initialize frontend and backend
        self._log.info("Performing initial push to publish all values.")
        for path, value in self._flat_dict.items():
            pub.sendMessage(self._topic_for_path[path], payload=value)

        #now start the UI update publisher now that everything is ready
        self._log.info("Starting UI update publisher thread.")
        self._ui_update_publisher.start()

    # -------------------------------------------------------------------------
    # Public API
    # -------------------------------------------------------------------------
    def close(self) -> None:
        """
        Close the backend and stop all threads.
        """
        self._log.info("Shutting down DictViewerBackend, stopping threads...")
        self._stop.set()
        self._ui_update_event.set() #get the ui update publisher to skip the wait on change
        try:
            self._ui_update_publisher.join(timeout=1.0)
        except Exception as e:
            self._log.warning(f"Exception during thread join: {e}")
        self._log.info("DictViewerBackend closed")

    def pull(self) -> Dict[Any, Any]:
        """
        Build and return a nested snapshot with the same structure as
        the reference dictionary, using current flattened values.

        Thread-safe.
        """
        #take a snapshot of the flat dictionary at the time of function call
        with self._lock:    
            flat_copy = copy.deepcopy(self._flat_dict)
        
        #unflatten the flat dictionary to make it a nested dictionary in the same form of reference
        nested = FlatDict.unflatten(flat_copy)
        return nested

    def push(self, nested_update: Dict[Any, Any]) -> None:
        """
        Apply a nested update (full or partial) to the backend and publish
        any changes out to their corresponding topics.

        - nested_update: nested dict whose paths are a subset of reference_dict.
        - _force_initial: internal flag used at construction time to publish
          all values once, even if they're equal to current ones.
        """
        flat_update = FlatDict.flatten(nested_update)

        with self._lock:
            for path, new_val in flat_update.items():
                #update the flattened dictionary
                #update function does all safety checks on the new val and path
                if self._update_flattened_dict(path, new_val):
                    #publish the change to the corresponding topic if safety check passes
                    #and if a new value was written to the flattened dictionary
                    pub.sendMessage(self._topic_for_path[path], payload=new_val)
                else:
                    self._log.debug(f"Push skipped for path {path}: update/validation failed or value unchanged.")

    def wait_ui_update(self, timeout: Optional[float] = None) -> bool:
        """
        Block until a UI-driven update is received (via pubsub) or until
        'timeout' elapses. Returns True if an update occurred.

        The flag is cleared before returning.
        """
        ok = self._ui_update_event.wait(timeout)
        if ok:
            self._ui_update_event.clear()
        return ok

    def is_ui_update(self, clear: bool = False) -> bool:
        """
        Check whether a UI-driven update has occurred since last clear.
        If 'clear' is True, the internal flag is cleared before returning.
        """
        flag = self._ui_update_event.is_set()
        if flag and clear:
            self._ui_update_event.clear()
        return flag

    # -------------------------------------------------------------------------
    # UI Update Publisher thread function
    # -------------------------------------------------------------------------

    def _ui_update_publisher_thread(self) -> None:
        """
        Thread function for publishing UI-driven updates.
        """
        self._log.debug("UI update publisher thread started.")
        while not self._stop.is_set():
            #wait for an update, then publish using 
            self.wait_ui_update()
            
            #publish the nested dictionary to the nested dictionary topic
            pub.sendMessage(f"{self._ui_topic_root}.nested", payload=self.pull())

            #rate limit the publish to the max publish rate
            self._stop.wait(self._ui_max_publish_rate_s)

    # -------------------------------------------------------------------------
    # Internal helpers: topics & subscriptions
    # -------------------------------------------------------------------------
    def _topic_from_path(self, path: Path) -> str:
        """
        Build a pubsub topic string from a path, e.g.:

            ui_topic_root = "ui.dict"
            path = ("cob_temp", "status", "temperature_celsius")

        -> "ui.dict.cob_temp.status.temperature_celsius"
        """
        suffix = ".".join(str(p) for p in path)
        return f"{self._ui_topic_root}.entries.{suffix}"

    def _subscribe_topic(self, _path: Path) -> None:
        """
        Subscribe to the topic for 'path'. Only called for editable paths.
        """
        #retrieve the pub/sub topic for the dictionary path
        topic = self._topic_for_path[_path]
        self._log.debug(f"Subscribing to topic '{topic}' for path {_path}")
        #subscribe to the topic
        #pass curried topic argument to message handler
        pub.subscribe(self._on_message, topic, path = _path)

    def _on_message(self, *, payload: Any = None, path: Path = None) -> None:
        """
        Generic callback for all UI-originated messages.

        - Uses _update_flattened_dict() to update internal state.
        - If updated, sets the UI update event and calls on_update callback.
        """
        # Guard: check payload and path validity
        if payload is None or path is None:
            self._log.warning("_on_message called with missing payload or path.")
            return
        if not isinstance(path, tuple):
            self._log.warning(f"_on_message got non-tuple path: {path!r}")
            return
        
        #update the flattened dictionary atomically
        with self._lock:
            updated = self._update_flattened_dict(path, payload)
            if not updated:
                return

            #set the ui update event to signal that a UI-driven update has occurred
            #only if the write to the dictionary was successful
            self._ui_update_event.set()

    # -------------------------------------------------------------------------
    # Internal helpers: updating / validation
    # -------------------------------------------------------------------------
    def _update_flattened_dict(self, path: Path, new_val: Any) -> bool:
        """
        Try to update the flattened dict at 'path' with 'new_val'.

        Returns True if the value was actually updated, False otherwise.

        Assumes the caller holds _lock.
        """
        #check if the new value is valid
        if not self._check_valid(path, new_val):
            return False

        #skip the update if the values are equal (return false since update isn't performed)
        if self._flat_dict[path] == new_val:
            return False

        #otherwise update the flattened dictionary
        self._flat_dict[path] = new_val
        return True

    def _check_valid(self, path: Path, new_val: Any) -> bool:
        """
        Return True if:
        - 'path' exists in the reference dict,
        - type matches the original type at that path,
        - for list/tuple values, lengths also match.

        NOTE: no casting is attempted here! so if new value is 0 and original type is 0.0, this will fail.
        """
        #check if the path exists in the reference dictionary
        if path not in self._flat_dict:
            self._log.warning(f"_check_valid: Path {path} not in reference dictionary.")
            return False

        #make sure types of the original and the new one match
        #ensure desired editable types are NOT none!
        if type(new_val) != type(self._flat_dict[path]):
            self._log.warning(f"_check_valid: Type mismatch on {path}: {type(new_val)} != {type(self._flat_dict[path])}")
            return False

        # for list, tuple, make sure size matches and type of elements match
        if isinstance(new_val, (list, tuple)):
            if len(new_val) != len(self._flat_dict[path]):
                self._log.warning(f"_check_valid: Tuple length mismatch on {path}: {len(new_val)} != {len(self._flat_dict[path])}")
                return False
            for i in range(len(new_val)): #check each element
                if type(new_val[i]) != type(self._flat_dict[path][i]):
                    self._log.warning(f"_check_valid: Tuple element {i} type mismatch on {path}")
                    return False

        # if we got here, then type check passed
        return True
