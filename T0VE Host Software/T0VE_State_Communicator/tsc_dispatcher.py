from pubsub import pub
import logging
from typing import Any
from datetime import datetime
import copy

from host_application_drivers.util_flat_dict import FlatDict
from host_application_drivers.util_match_type_runtime import match_type
from host_application_drivers.state_proto_defs import DebugLevel, NodeState

###################################################################################################
# PORT STATUS
###################################################################################################

def DEFAULT_PORT_STATE() -> dict[str, Any]:
    '''
    Return a deep copy of the default port state dictionary.
    Using this function prevents shared mutable references.
    '''
    return copy.deepcopy({
        "command": {
            "request_connect": True,
            "refresh_state": False,
        },
        "status": {
            "connected": False,
            "port_name": "---",
            "serial_number": "---",
            "commands_enqueued": 0,
            "command_queue_space": 0,
        }
    })

def link_node_port_info(ui_port_topic_root: str, node_port_topic_root: str, logger: logging.Logger) -> None:
    '''
    reformat/forward various serdes <--> UI publishes to each other
    specifically regarding node port information
    '''
    # on the serdes --> UI side, the topic tree looks just like our port state dictionary
    # serdes publishes directly under its topic root; ui receives publishes under {topic_root}.entries.set
    # as such, we'll flatten our port state dict, and just forward publishes to our UI aggregator
    flattened_port_state = FlatDict.flatten(DEFAULT_PORT_STATE())
    port_state_topics = []
    for path in flattened_port_state.keys():
        port_state_topics.append(".".join([path_elem for path_elem in path]))
    serdes_port_topics_in = [f"{node_port_topic_root}.{topic}" for topic in port_state_topics]
    serdes_port_topics_out = [f"{node_port_topic_root}.{topic}" for topic in port_state_topics]
    ui_port_topics_out = [f"{ui_port_topic_root}.entries.set.{topic}" for topic in port_state_topics]
    ui_port_topics_in = [f"{ui_port_topic_root}.entries.get.{topic}" for topic in port_state_topics]
    

    #now subscribe to each of the serdes port topics, and forward the publishes to the corresponding UI port topics
    for serdes_port_topic_in, ui_port_topic_out in zip(serdes_port_topics_in, ui_port_topics_out):
        pub.subscribe(_forward_port_publish, serdes_port_topic_in, output_topic=ui_port_topic_out)
        logger.debug(f"Subscribed to {serdes_port_topic_in}")

    # on the UI --> serdes side, we'll see publishes from the `.entries.get` topic, with topic-wise publishes
    # when the UI updates them. Subscribe to the request_connect and refresh_state entry topics, and forward to the serdes
    # I know this function listens to ALL entry topics, but only the command ones are editable, so it'll be the only publishes we see
    # serdes will ignore status topic publishes anyway
    # the code is just easier to write this way
    for ui_port_topic_in, serdes_port_topic_out in zip(ui_port_topics_in, serdes_port_topics_out):
        pub.subscribe(_forward_port_publish, ui_port_topic_in, output_topic=serdes_port_topic_out)
        logger.debug(f"Subscribed to {ui_port_topic_in}")


# forwarding port publishes; can use this callback bidirectionally
# flatten the payload into entries and publish each entry to the UI entries topic
def _forward_port_publish(payload: Any = None, output_topic: str = None) -> None:
    #make sure we have a valid UI port topic
    if output_topic is None:
        return

    #forward the payload directly to the UI port topic
    pub.sendMessage(output_topic, payload=payload)


###################################################################################################
# NODE STATE
###################################################################################################

def link_node_state(ui_state_topic_root: str, node_state_topic_root: str, logger: logging.Logger) -> None:
    '''
    reformat/forward various serdes <--> UI publishes to each other
    specifically regarding node state information
    '''
    
    # on the serdes --> UI side, grab all the NodeState publishes to `status`
    # turn them into a dictionary via betterproto inbuilts, the publish to the
    # node state UI topic
    pub.subscribe(_on_node_state_status, f"{node_state_topic_root}.status", ui_state_topic_root=ui_state_topic_root)
    logger.debug(f"Subscribed to {node_state_topic_root}.status")

    # on the UI --> serdes side, we'll see publishes from the `.nested.get` topic, 
    # complete dictionaries of the node state
    # turn these into betterproto NodeState messages and publish them to the `command` topic
    pub.subscribe(_on_ui_node_state_entry_update, f"{ui_state_topic_root}.nested.get", node_state_topic_root=node_state_topic_root, logger=logger)
    logger.debug(f"Subscribed to {ui_state_topic_root}.nested.get")

def _on_node_state_status(payload: Any = None, ui_state_topic_root: str = None) -> None:
    #make sure we have a valid UI state topic root
    if ui_state_topic_root is None:
        return
    
    #make sure our payload is a NodeState message
    if not isinstance(payload, NodeState):
        return
    
    #turn the payload into a dictionary via betterproto inbuilts
    node_state_dict = payload.to_dict(include_default_values=True)
    pub.sendMessage(f"{ui_state_topic_root}.nested.set", payload=node_state_dict)

def _on_ui_node_state_entry_update(payload: Any = None, node_state_topic_root: str = None, logger: logging.Logger = None) -> None: 
    #make sure we have a valid logger
    if logger is None:
        return
    
    #make sure we have a valid node state root topic
    if node_state_topic_root is None:
        return
    
    #make sure our payload is a dictionary of the node state
    if not isinstance(payload, dict):
        return

    #turn the payload into a betterproto NodeState message
    try:
        node_state = NodeState.from_dict(payload)
        pub.sendMessage(f"{node_state_topic_root}.command", payload=node_state)
    except Exception as e:
        logger.error(f"_on_ui_node_state_entry_update: Error turning payload into NodeState message: {e}")
    

###################################################################################################
# NODE DEBUG 
###################################################################################################

def link_node_debug_termctrl(ui_debug_topic_root: str, port_status_topic_root: str, logger: logging.Logger) -> None:
    '''
    reformat/forward various serdes <--> UI publishes to each other
    this function clears the debug terminal on disconnect <--> connect events
    '''
    # Subscribe to the boolean connection flag the serdes actually publishes.
    # It lives at "<port_status_topic_root>.status.connected" (e.g. app.devices.node_00.port.status.connected).
    pub.subscribe(_on_port_status_dis_connect, f"{port_status_topic_root}.status.connected", ui_debug_topic_root=ui_debug_topic_root)
    logger.debug(f"Subscribed to {port_status_topic_root}.status.connected")

def link_node_debug_info(ui_debug_topic_root: str, node_debug_topic_root: str, logger: logging.Logger) -> None:
    '''
    reformat/forward various serdes <--> UI publishes to each other
    specifically regarding node debug information
    '''

    #from the debug message enums from protobuf, generate the debug levels
    debug_levels = [debug_level.name.upper() for debug_level in DebugLevel]

    #now subscribe to the debug topic root for each debug level
    for debug_level in debug_levels:
        pub.subscribe(_on_node_debug_info, f"{node_debug_topic_root}.{debug_level}", ui_debug_topic_root=ui_debug_topic_root, debug_level=debug_level)
        logger.debug(f"Subscribed to {node_debug_topic_root}.{debug_level}")

def _on_node_debug_info(payload: Any = None, ui_debug_topic_root: str = None, debug_level: str = None) -> None:
    #make sure we have a valid UI debug topic root
    if ui_debug_topic_root is None:
        return

    #make sure we have a valid debug level
    if debug_level is None:
        return  

    #and string-coerce the payload
    try:
        payload_str = str(payload)
    except Exception as e:
        return

    #format the debug message
    dbg_message = f"{datetime.now().strftime('%Y-%m-%d %H:%M:%S')}: [{debug_level.upper()}] {payload_str}"

    #forward the payload directly to the debug topic
    pub.sendMessage(f"{ui_debug_topic_root}.add", payload=dbg_message)

def _on_port_status_dis_connect(payload: Any = None, ui_debug_topic_root: str = None) -> None:   
    #make sure we have a valid UI debug topic root
    if ui_debug_topic_root is None:
        return

    # serdes publishes a boolean on the connected topic; ignore anything else
    if not isinstance(payload, bool):
        return

    #initialize a static variable that keeps track of connection status
    if not hasattr(_on_port_status_dis_connect, 'last_connect_status'):
        _on_port_status_dis_connect.last_connect_status = False  # Initialize on first call

    #clear debug terminal on changes from disconnect --> connect 
    if payload is True and _on_port_status_dis_connect.last_connect_status is False:
        pub.sendMessage(f"{ui_debug_topic_root}.clear", payload="")

    #update the last connect status
    _on_port_status_dis_connect.last_connect_status = payload
