import logging
import os
import sys
import time
from pubsub import pub


def run_serdes_demo(node_index: int = 15, run_seconds: float = 60.0) -> None:
    """
    Instantiate Host_Device_State_Serdes with a console logger and let it run.
    """
    # Ensure we can import from device_interface/
    here = os.path.abspath(os.path.dirname(__file__))
    device_interface_dir = os.path.abspath(os.path.join(here, "..", "device_interface"))
    if device_interface_dir not in sys.path:
        sys.path.insert(0, device_interface_dir)

    from host_device_state_serdes import Host_Device_State_Serdes  # noqa: E402

    # Logger
    logger = logging.getLogger("t0ve.testing.serdes")
    logger.setLevel(logging.DEBUG)
    if not logger.handlers:
        handler = logging.StreamHandler()
        handler.setFormatter(
            logging.Formatter("%(asctime)s [%(levelname)s] %(name)s: %(message)s")
        )
        logger.addHandler(handler)

    serdes = Host_Device_State_Serdes(node_index=node_index, logger=logger)

    # Subscribe to the node's debug root topic; PyPubSub will also deliver subtopics
    debug_root = f"app.devices.node_{node_index:02d}.debug"

    def _on_debug(data=None, topic=pub.AUTO_TOPIC):
        try:
            topic_name = topic.getName() if topic is not None else ""
        except Exception:
            topic_name = ""
        logger.info(f"{topic_name}: {data}")

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

