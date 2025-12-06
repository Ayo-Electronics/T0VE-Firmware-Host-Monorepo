"""
Thin re-export module for protobuf definitions under:
  <repo_root>/T0VE Common/Proto-Host/app

Usage:
    from host_application_drivers.state_proto_defs import Communication, NodeState, Debug

This module first tries to import `app` directly. If not found, it adds the
expected sibling path to sys.path based on this file's location and retries.
"""

from __future__ import annotations
import importlib
import os
import sys
from types import ModuleType


def _import_app_module() -> ModuleType:
    try:
        return importlib.import_module("app")
    except Exception:
        # Compute repo-relative path to "T0VE Common/Proto-Host"
        base_dir = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
        candidate = os.path.join(base_dir, "T0VE Common", "Proto-Host")
        if os.path.isdir(candidate) and candidate not in sys.path:
            sys.path.append(candidate)
        # Retry import (propagate if it still fails)
        return importlib.import_module("app")


_app: ModuleType = _import_app_module()

# Re-export public names from `app`
__all__ = tuple(name for name in dir(_app) if not name.startswith("_"))
globals().update({name: getattr(_app, name) for name in __all__})


