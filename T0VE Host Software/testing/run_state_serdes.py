import logging
import os
import sys
import time
from pubsub import pub
import pprint
import betterproto

# import the NodeState message definition
from host_application.state_proto_defs import NodeState


def run_serdes_demo(node_index: int = 15, run_seconds: float = 60.0) -> None:
    """
    Instantiate HostDeviceStateSerdes with a console logger and let it run.
    """
    from host_application.host_device_state_serdes import HostDeviceStateSerdes  # noqa: E402

    # Logger
    logger = logging.getLogger("t0ve.testing.serdes")
    logger.setLevel(logging.DEBUG)
    if not logger.handlers:
        handler = logging.StreamHandler()
        handler.setFormatter(
            logging.Formatter("%(asctime)s [%(levelname)s] %(name)s: %(message)s")
        )
        logger.addHandler(handler)

    serdes = HostDeviceStateSerdes(node_index=node_index, logger=logger)

    # Subscribe to the node's debug root topic; PyPubSub will also deliver subtopics
    debug_root = f"app.devices.node_{node_index:02d}.debug"
    status_topic = f"app.devices.node_{node_index:02d}.status"
    port_topic = f"app.devices.node_{node_index:02d}.port"

    def _on_debug(payload=None, topic=pub.AUTO_TOPIC):
        try:
            topic_name = topic.getName() if topic is not None else ""
        except Exception:
            topic_name = ""
        logger.info(f"{topic_name}: {payload}")

    def _on_status(payload=None, topic=pub.AUTO_TOPIC):
        try:
            topic_name = topic.getName() if topic is not None else ""
        except Exception:
            topic_name = ""
        if isinstance(payload, NodeState):
            # Convert NodeState to dict for pretty printing
            as_dict = payload.to_dict(casing=betterproto.Casing.SNAKE, include_default_values=True)
            logger.info(f"{topic_name}: NodeState message:\n{pprint.pformat(as_dict, indent=2)}")
        else:
            logger.info(f"{topic_name}: Received non-NodeState payload: {payload!r}")

    def _on_port(payload=None, topic=pub.AUTO_TOPIC):
        try:
            topic_name = topic.getName() if topic is not None else ""
        except Exception:
            topic_name = ""
        print(f"{topic_name}: {payload}")

    pub.subscribe(_on_status, status_topic)
    pub.subscribe(_on_port, port_topic)
    pub.subscribe(_on_debug, debug_root)
    # NOTE: Depending on PyPubSub configuration, subscribing to the root topic may or may not
    # receive subtopic messages automatically. If no events arrive, consider subscribing to
    # specific leaf topics under the debug tree instead.

    # Allow the underlying serial layer to connect and run
    try:
        serdes.port.connect()
        end = time.time() + float(run_seconds)
        while time.time() < end:
            time.sleep(0.2)
    except KeyboardInterrupt:
        pass
    finally:
        serdes.close()


if __name__ == "__main__":
    run_serdes_demo()

