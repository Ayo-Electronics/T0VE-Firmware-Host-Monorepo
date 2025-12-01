from __future__ import annotations
import logging
import threading
import copy
import pprint
from typing import Any, Dict, Iterable, Tuple, Optional, Callable

from pubsub import pub  # PyPubSub

class Pub_Sub_Dict:
    def __init__(
        self, 
        *,
        root_topic: str,
        dict_reference: Dict[str, Any],
        writable_topics: Optional[Iterable[str]] = None,
        logger: Optional[logging.Logger] = None
    ) -> None:
        self._root_topic = root_topic

        # and save the logger for debugging this process
        self._logger = logger or logging.getLogger(__name__ + self._root_topic + ".pubsub_dict")

        # lock for thread-safe state and subscription updates
        self._lock = threading.RLock()

        # save the original dictionary so we have a reference for structure and types
        # but also make a flattened version of the dictionary for easy pub/subbing
        # NOTE: the internal flattened dictionary will have the root topic prepended to all keys
        #       this makes implementation of the publish/subscribe system super straightforward, as keys can directly be used as publish topics
        #       however, this means key names will not 100% match up to those in the original dictionary
        self._dict_reference = dict_reference
        # Pretty print the dict reference for debugging purposes
        self._logger.debug(
            "Initial dict_reference structure for %s:\n%s",
            self._root_topic,
            pprint.pformat(self._dict_reference, width=120, compact=False)
        )

        self._dict_flattened = self._flatten_dictionary(root_topic=self._root_topic, dict_reference=self._dict_reference)
        self._logger.debug(
            "Initial flattened dictionary (%d entries):\n%s",
            len(self._dict_flattened),
            pprint.pformat(self._dict_flattened, width=120, compact=False),
        )

        # initialize writable topics (filtered to known/valid keys) and subscribe
        # capture proposed writable topics; tolerate None
        proposed_writable = set(writable_topics or [])
        known_topics = set(self._dict_flattened.keys())
        self._writable_topics = proposed_writable.intersection(known_topics)

        #publish all the initial values of all the topics
        self._publish_topics(known_topics)

        # then subscribe to writable topics
        if self._writable_topics:
            self._subscribe_to_topics(self._writable_topics)

    ######################
    ##### PUBLIC API #####
    ######################

    def update(self, new_dict: Dict[str, Any]) -> None:
        '''
        Bulk setter method for updating the dictionary
        '''
        #flatten this new dictionary, prepend all entries with the appropriate topic string
        flattened_new_dict = self._flatten_dictionary(root_topic=self._root_topic, dict_reference=new_dict)
        
        #for every entry, update the flattened dictionary
        #this function will do the sanity checking, making sure the topic actually exists in our flattened dictionary
        for topic, value in flattened_new_dict.items():
            self._update_flattened_dictionary(data=value, topic_string=topic)

        #and publish the new values written to the flattened dictionary
        #this function also gracefully handles any issues with topics not in our flattened dictionary
        self._publish_topics(flattened_new_dict.keys())
    
    def collect(self) -> Dict[str, Any]:
        '''
        Bulk getter method for getting the current dictionary
        This returns a dictionary in the same format/topology as the reference dictionary, with the exact same keys
        Values are pulled from the flattened dictionary 
        '''
        # make a deep copy of the reference dictionary so we don't mutate the reference structure
        collected_dict = copy.deepcopy(self._dict_reference)

        #when we walk the dictionary next, copy the value from the flattened dictionary when we hit a leaf
        def _collect(topic: str, _value: Any) -> Any:
            #return the value looked up in the flattened dictionary
            return self._dict_flattened[topic]
        
        #walk the dictionary, collecting the values from the flattened dictionary
        self._walk_tree(self._root_topic, collected_dict, _collect)
        return collected_dict

    def get_all_topics(self) -> Iterable[str]:
        '''
        Get all the topics in the flattened dictionary, i.e. all topics the dictionary writer can publish to
        NOTE: this is the same as the keys of the flattened dictionary, PREPENDED WITH ROOT TOPIC
        Useful for generating the list of writeable topics if that can be done procedurally based on key names
        '''
        return tuple(self._dict_flattened.keys())

    def get_writable_topics(self) -> Iterable[str]:
        '''
        Get the list of active writable topics
        '''
        return self._writable_topics.copy()

    def update_writable_topics(self, new_topics: Iterable[str]) -> None:
        '''
        Update the list of writable topics
        '''
        #convert to set for fast intersection; tolerate None -> empty set
        proposed = set(new_topics or [])

        # filter to known topics only
        known_topics = set(self._dict_flattened.keys())
        new_topics_set = proposed.intersection(known_topics)

        with self._lock:
            #unsubscribe from the topics that are no longer writable
            #difference operation returns items in `writable_topics` that aren't in the `new_topics_set`
            to_unsub = self._writable_topics.difference(new_topics_set)
            if to_unsub:
                self._unsubscribe_from_topics(to_unsub)

            #subscribe to the new topics
            #difference operation returns items in `new_topics` that weren't in `_writable_topics`
            to_sub = new_topics_set.difference(self._writable_topics)
            if to_sub:
                self._subscribe_to_topics(to_sub)

            #save the new topics we're allowing write access to 
            self._writable_topics = new_topics_set

    ############################
    ##### HELPER FUNCTIONS #####
    ############################
    def _walk_tree(self, base: str, obj: Any, on_leaf: Callable[[str, Any], Any]) -> Any:
        """
        Generic walker for dict/list trees.
        - Calls on_leaf(topic, value) for each leaf.
        - If on_leaf returns a non-None value, that value replaces the leaf.
        - If on_leaf returns None, the original leaf value is preserved.
        - Dicts and lists are mutated in place; tuples are rebuilt.
        """
        #don't do anything for `None` objects
        if obj is None:
            return None

        #for dictionaries, append the keys to the key-chain (topic string), recurse over the values at the keys
        if isinstance(obj, dict):
            for k, v in obj.items():
                new_v = self._walk_tree(f"{base}.{k}", v, on_leaf)
                if new_v is not None:
                    obj[k] = new_v
            return obj

        #for lists/tuples, append the index to the key-chain (topic string), recurse over the values at the indices
        if isinstance(obj, list):
            for i, v in enumerate(obj):
                new_v = self._walk_tree(f"{base}.{i}", v, on_leaf)
                if new_v is not None:
                    obj[i] = new_v
            return obj

        if isinstance(obj, tuple):
            new_items = []
            for i, v in enumerate(obj):
                new_v = self._walk_tree(f"{base}.{i}", v, on_leaf)
                new_items.append(v if new_v is None else new_v)
            return tuple(new_items)
        
        #if we're here, we've found a leaf, run the callback function on the leaf
        #pass key-chain (i.e. topic string), and the particular object at the topic string
        result = on_leaf(base, obj)

        #if the result is not None and the type is compatible with the original object, return the result
        #otherwise, return the original object
        return result if (result is not None and type(result) is type(obj)) else obj

    def _flatten_dictionary(self, root_topic: str, dict_reference: Dict[str, Any]) -> dict[str, Any]:
        """
        Recursively traverse the dictionary and lists to collect all possible topics (leaf paths).
        The topics are formatted as strings with the format "{root_topic}.<...>.<...>.<...>",
        following both dictionary keys and list indices; the function expands list entries into their own 
        dictionary entries, using a stringified index as the key.

        This function will return a flattened dictionary with the format {<root_topic>.<...>.<...>.<...>: <value>}.
        """
        #our "leaf function" adds a dictionary entry with the specified value found
        flattened_dict: dict[str, Any] = {}
        def _collect(topic: str, _value: Any) -> Any:
            flattened_dict[topic] = _value
            return None

        #walk the tree, collecting dictionary entries, return the complete dictionary at the end
        self._walk_tree(root_topic, dict_reference, _collect)
        return flattened_dict

    def _subscribe_to_topics(self, topics: Iterable[str]) -> None:
        '''
        Subscribe to the given topics
        '''
        for topic in topics:
            #subscribe to the specified topic; pass the topic name to the subscription handler too
            #new value for the particular topic should be delivered to the `data` keyword
            #and make sure we don't publish on write --> would lead to an infinite call to `_update_flattened_dictionary`
            pub.subscribe(self._update_flattened_dictionary, topic, topic_string=topic)

    def _unsubscribe_from_topics(self, topics: Iterable[str]) -> None:
        '''
        Unsubscribe from the given topics
        '''
        #go through the topics collection and unsubscribe to those specified topics
        #only unsubscribe if we're actually subscribed to the topic
        for topic in topics:
            if(pub.isSubscribed(self._update_flattened_dictionary, topic)):
                pub.unsubscribe(self._update_flattened_dictionary, topic)

    def _update_flattened_dictionary(self, data: Any = None, topic_string: str = "", **kwargs) -> None:
        '''
        Function to update the flattened dictionary with the given data and topic string
        Also used as a callback for PyPubSub subscriptions
        '''

        cast_data = None
        with self._lock:
            #sanity check that the topic string is actually in the flattened dictionary
            if(topic_string not in self._dict_flattened):
                self._logger.warning(f"Topic string {topic_string} not found in flattened dictionary")
                return

            #get the type of the value presently at the key location
            current_type = type(self._dict_flattened[topic_string])

            #try to cast the data to the current type
            try:
                cast_data = current_type(data)
            except Exception as e:
                #if the cast is unsuccessful, log the error and return
                self._logger.warning(f"Failed to cast data to type {current_type} for topic {topic_string}: {e}")
                return

            #if we're here, the cast was successful; write the cast value into the position in the dictionary
            self._dict_flattened[topic_string] = cast_data
    
    def _publish_topics(self, topics: Iterable[str]) -> None:
        '''
        Publish the given topics
        '''
        #go through topics
        for topic in topics:
            #only publish topics that are actually in our flattened dictionary
            if(topic in self._dict_flattened):
                pub.sendMessage(topic, data=self._dict_flattened[topic])
            else:
                self._logger.warning(f"Topic {topic} not found in flattened dictionary, cannot publish")
