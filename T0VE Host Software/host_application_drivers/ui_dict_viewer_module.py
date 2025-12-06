from host_application_drivers.ui_dict_viewer_frontend import DictViewerFrontend
from host_application_drivers.ui_dict_viewer_aggregator import DictViewerAggregator, Path

from typing import Any, Dict, Iterable, Optional
import logging  
from tkinter import ttk

class DictViewerModule:
    def __init__(   self,
                    *,
                    reference_dict: Dict[Any, Any],     
                    parent: ttk.Frame,
                    ui_topic_root: str = "app.ui",
                    editable_paths: Optional[Iterable[Path]] = None, 
                    layout_pattern: str = "thv", 
                    ui_max_publish_rate_s: float = 0.1,
                    logger: Optional[logging.Logger] = None) -> None:

        #create the frontend
        self.frontend = DictViewerFrontend(
            parent=parent,
            reference_dict=reference_dict,
            ui_topic_root=ui_topic_root,
            editable_paths=editable_paths,
            layout_pattern=layout_pattern,
            logger=logger,
        ) 
        
        #create the aggregator
        self.aggregator = DictViewerAggregator(
            reference_dict=reference_dict,
            editable_paths=editable_paths,
            ui_topic_root=ui_topic_root,
            ui_max_publish_rate_s=ui_max_publish_rate_s,
            logger=logger,
        )

          
