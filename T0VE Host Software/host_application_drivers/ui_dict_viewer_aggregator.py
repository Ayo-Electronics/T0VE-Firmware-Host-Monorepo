from __future__ import annotations

from typing import Any, Dict, Iterable, Tuple, Optional
import threading
import copy
import logging

from pubsub import pub
from host_application_drivers.util_flat_dict import FlatDict
from host_application_drivers.util_match_type_runtime import match_type

Path = Tuple[Any, ...]

class DictViewerAggregator:
    '''
    Constructor for the DictViewerAggregator class.
    - reference_dict: the reference dictionary to use for the aggregator (should be shared with the frontend)
    - ui_topic_root: the root topic to use for the pub/sub interface; all topics will be prefixed with this root
    - ui_max_publish_rate_s: the maximum rate at which the aggregator can publish updated dictionary values to subscribers
    - logger: the logger to use for the aggregator

    topic format:
    [ui_topic_root].frontend.set.[path1].[path2].[path3]...[pathN]  #listener of all individual dictionary entries (UI side, DON'T TOUCH)
    [ui_topic_root].frontend.get.[path1].[path2].[path3]...[pathN]  #publisher of all individual dictionary entries (UI side, DON'T TOUCH)
    [ui_topic_root].entries.set.[path1].[path2].[path3]...[pathN]   #listener of individual dictionary entries 
    [ui_topic_root].entries.get.[path1].[path2].[path3]...[pathN]   #publisher of individual dictionary entries 
    [ui_topic_root].nested.set                                      #listener of the entire nested dictionary 
    [ui_topic_root].nested.get                                      #publisher of the entire nested dictionary 

    notes:
     - aggregator will only subscribe to editable paths in frontend.get to ensure full respect of read-only topics
     - aggregator only publishes to frontend.set when we receive a entry/nested.set publish or similar API call
     - external users can push to entries.set or nested.set to update UI elements
        - aggregator will forward these publishes to frontend.set topic to update UI elements
     - external users can subscribe to entries.get or nested.get to get the current value of the dictionary
        - aggregator will forward publishes to frontend.get to entries.get
        - aggregator will only publish to nested.get when it receives a frontend.get publish
    '''

    '''
    TODO:
     - dual-port all topics; 
     - nested publish only on entry updates (i.e. from entries.get event)
    '''

    def __init__(   self, 
                    *,
                    reference_dict: Dict[Any, Any],
                    editable_paths: Optional[Iterable[Path]] = None,
                    ui_topic_root: str = "app.ui",
                    ui_max_publish_rate_s: float = 0.1,
                    logger: Optional[logging.Logger] = None) -> None:

        # get/create a logger for this instance; pass to aggregator
        self._log = logger or logging.getLogger(__name__ + ".DictViewerAggregator")
        
        #deep copy the reference dictionary so upstream edits of the reference never change the local copy
        self._ui_topic_root = ui_topic_root

        # some other private variables
        # we'll flatten the dictionary to a single layer for easy spontaneous writes from publishers
        # publish cache ensures publishes only occur when dictionary values change; reduces pub/sub traffic
        # topic_for_path is a dictionary of paths to topics for easy lookup; can compute once and use later
        self._flat_dict = FlatDict.flatten(reference_dict)
        self._publish_cache: Dict[str, Any] = {}
        self._topic_for_path: Dict[Path, str] = {path: self._topic_from_path(path) for path in self._flat_dict.keys()}

        # threading events
        # lock ensures thread-safe access to the dictionary
        # ui_update_event is set when a UI-driven update is received
        # ui_update_publisher is a thread that publishes the edited when UI edits are made
        # stop is a thread stop event to best-effort close other threads upon shutdown
        self._lock = threading.RLock()
        self._ui_update_event = threading.Event()
        self._ui_update_publisher = threading.Thread(   target=self._ui_update_publisher_thread, 
                                                        name="dict_viewer_aggregator_ui_update_publisher_" + self._ui_topic_root, 
                                                        daemon=True)
        self._ui_max_publish_rate_s: float = ui_max_publish_rate_s
        self._stop = threading.Event()

        ###### SUBSCRIPTION SETUP ######
        # subscribe to external path publishes
        for path in self._flat_dict.keys():
            pub.subscribe(  self._on_external_path_publish,                                     # callback
                            f"{self._ui_topic_root}.entries.set.{self._topic_for_path[path]}",  # topic
                            path=path)                                                          # curried path argument

        # subscribe to external nested publishes
        pub.subscribe(  self._on_external_nested_publish,       # callback
                        f"{self._ui_topic_root}.nested.set")    # topic

        # subscribe to frontend widget publishes
        # ONLY FOR EDITABLE TOPICS
        # normalize editable_paths so None means "no editable paths"
        editable_paths = list(editable_paths) if editable_paths is not None else []
        for editable_path in editable_paths:
            if editable_path in self._flat_dict:
                pub.subscribe(  self._on_frontend_widget_publish,                                               # callback
                                f"{self._ui_topic_root}.frontend.get.{self._topic_for_path[editable_path]}",    # topic
                                path=editable_path)                                                             # curried path argument
            else:
                self._log.warning(f"DictViewerAggregator: Editable path {editable_path} not in reference dictionary.")

        ###### INITIAL PUBLISHES ######
        # publish all flat dict entries to the `entries.set` topic
        for path in self._flat_dict.keys():
            pub.sendMessage(f"{self._ui_topic_root}.entries.set.{self._topic_for_path[path]}", payload=self._flat_dict[path])

        # publish the nested dictionary to the `nested.get` topic
        pub.sendMessage(f"{self._ui_topic_root}.nested.get", payload=self.pull())

        # and publish all entries to the frontend widgets
        for path in self._flat_dict.keys():
            pub.sendMessage(f"{self._ui_topic_root}.frontend.set.{self._topic_for_path[path]}", payload=self._flat_dict[path])

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
        Apply a nested update (full or partial) to the aggregator and publish
        any changes out to their corresponding topics.
        Update frontend with all edited topics, but publish to `get` topics
        """
        updated_paths = self._push_no_publish(nested_update)
        if updated_paths:
            #if we performed an update, publish entry-wise updates to frontend
            for path in updated_paths:
                pub.sendMessage(f"{self._ui_topic_root}.frontend.set.{self._topic_from_path(path)}", payload=self._flat_dict[path])

    def pull_path(self, path: Path) -> Any:
        """
        Pull a value from a specific path in the aggregator
        """
        if path not in self._flat_dict:
            self._log.warning(f"pull_path: Path {path} not in reference dictionary.")
            return None

        with self._lock:
            return self._flat_dict[path]

    def push_path(self, path: Path, new_val: Any) -> None:
        """
        Push a new value to a specific path to update the dictionary
        Update frontend, but publish to `get` topics
        """
        updated = self._push_path_no_publish(path, new_val)
        if updated:
            #if we performed an update, publish to the frontend.set topic
            pub.sendMessage(f"{self._ui_topic_root}.frontend.set.{self._topic_from_path(path)}", payload=new_val)

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
            pub.sendMessage(f"{self._ui_topic_root}.nested.get", payload=self.pull())

            #rate limit the publish to the max publish rate
            self._stop.wait(self._ui_max_publish_rate_s)

    # -------------------------------------------------------------------------
    # Internal helpers: message handlers
    # -------------------------------------------------------------------------

    def _on_frontend_widget_publish(self, payload: Any = None, path: Path = None) -> None:
        # forward to internal helper directly
        updated = self._push_path_no_publish(path, payload)
        if updated:
            #if we get a UI publish, push the topic to the pathwise `get` topic
            pub.sendMessage(f"{self._ui_topic_root}.entries.get.{self._topic_from_path(path)}", payload=payload)

            #and schedule a nested dictionary broadcast
            self._ui_update_event.set()

    def _on_external_path_publish(self, payload: Any = None, path: Path = None) -> None:
        # directly forward to API push--takes care of sanity checking parameters
        # and publishing to frontend on change
        self.push_path(path, payload)

    def _on_external_nested_publish(self, payload: Any = None) -> None:
        # directly forward to API push--takes care of sanity checking parameters
        # and publishing to frontend on change
        self.push(payload)

    # -------------------------------------------------------------------------
    # Internal helpers
    # -------------------------------------------------------------------------
    def _topic_from_path(self, path: Path) -> str:
        """
        Build a pubsub outbound topic string from a path
        UI elements will subscribe to this topic to get the current value of the dictionary
        and update the UI elements accordingly
        """
        return ".".join(str(p) for p in path)

    def _push_path_no_publish(self, path: Path, new_val: Any) -> bool:
        '''
        push value to a specific path in the backend without publishing the change to the corresponding topic
        useful for callbacks that originated from the pub/sub system as to avoid double publishes
        '''
        with self._lock:
            #sanity checking happens inside _update function
            updated = self._update_flattened_dict(path, copy.deepcopy(new_val))
            if not updated:
                return False

            # if we got here, means we updated the dict
            return True

    def _push_no_publish(self, nested_update: Dict[Any, Any]) -> set[Path]:
        '''
        push a nested update to the backend without publishing the change to the corresponding topics
        useful for callbacks that originated from the pub/sub system as to avoid double publishes
        '''
        flat_update = FlatDict.flatten(nested_update)

        #keep track of paths we updated so we can publish later if needed
        updated_paths: set[Path] = set()

        #go through all paths in our flattened dictionary and try to publish
        for path, new_val in flat_update.items():
            if self._push_path_no_publish(path, new_val):
                updated_paths.add(path)
            else:
                #self._log.debug(f"Push skipped for path {path}: update/validation failed or value unchanged.")
                pass
                

        #return the set of paths we updated so the caller can publish later if needed
        return updated_paths

    def _update_flattened_dict(self, path: Path, new_val: Any) -> bool:
        """
        Try to update the flattened dict at 'path' with 'new_val'.

        Returns True if the value was actually updated, False otherwise.

        Assumes the caller holds _lock.
        """

        #check if the path exists in the reference dictionary
        if path not in self._flat_dict:
            self._log.warning(f"_update_flattened_dict: Path {path} not in reference dictionary.")
            return False

        if not match_type(new_val, self._flat_dict[path]):
            self._log.warning(f"_update_flattened_dict: Type mismatch on {path}: {type(new_val)} != {type(self._flat_dict[path])}")
            return False

        #skip the update if the values are equal (return false since update isn't performed)
        # if self._flat_dict[path] == new_val:
        #     return False

        #otherwise update the flattened dictionary
        self._flat_dict[path] = new_val
        return True
