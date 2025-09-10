#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""
Generate protobuf code for Host (Python via BetterProto) and Firmware (NanoPB C).

Repo layout:
/host_device_common
   /Proto-Necessities   <-- requirements.txt lives here
   /Proto-Firmware      <-- generated NanoPB .c/.h + runtime (pb.h, pb_common/encode/decode)
   /Proto-Host          <-- generated Python classes (BetterProto)
   /Proto-Defs          <-- .proto + .options files
   genproto.py          <-- this script
"""

import os
import sys
import subprocess
import shutil
import urllib.request, zipfile, io
from pathlib import Path
from typing import List, Optional

# ---------- Config ----------
REQS = [
    "betterproto[compiler]>=1.2.5",
    "grpclib>=0.4",
    "grpcio-tools>=1.56",
    "nanopb>=0.4",
]
PY_PKG_NAME = "proto_messages"
# ----------------------------

ROOT = Path(__file__).resolve().parent
DIR_DEFS = ROOT / "Proto-Defs"
DIR_HOST = ROOT / "Proto-Host"
DIR_FW   = ROOT / "Proto-Firmware"
DIR_NEED = ROOT / "Proto-Necessities"

# -------------------------------------------------------------------

def run(cmd: List[str]) -> int:
    return subprocess.call(cmd, shell=False)

def pip_install(pkgs: List[str], upgrade: bool = True) -> None:
    cmd = [sys.executable, "-m", "pip", "install"]
    if upgrade:
        cmd.append("--upgrade")
    cmd.extend(pkgs)
    print(">> Installing:", " ".join(pkgs))
    rc = run(cmd)
    if rc != 0:
        raise SystemExit("pip install failed.")

def pip_show(pkg: str) -> dict:
    """Return dict of fields from pip show <pkg>."""
    result = subprocess.run(
        [sys.executable, "-m", "pip", "show", pkg],
        capture_output=True, text=True, check=True
    )
    out = {}
    for line in result.stdout.splitlines():
        if ":" in line:
            k, v = line.split(":", 1)
            out[k.strip()] = v.strip()
    return out

def ensure_layout():
    (DIR_HOST / PY_PKG_NAME).mkdir(parents=True, exist_ok=True)
    DIR_FW.mkdir(parents=True, exist_ok=True)
    DIR_NEED.mkdir(parents=True, exist_ok=True)
    (DIR_HOST / PY_PKG_NAME / "__init__.py").write_text("# Generated BetterProto Python package\n")
    (DIR_NEED / "requirements.txt").write_text("\n".join(REQS) + "\n")

def find_nanopb_plugin_and_version() -> (Path, str, Path):
    """Find protoc-gen-nanopb exe, version, and package location."""
    info = pip_show("nanopb")
    version = info.get("Version")
    location = Path(info.get("Location", "")).resolve()
    base = location.parent
    if os.name == "nt":
        plugin = base / "Scripts" / "protoc-gen-nanopb.exe"
    else:
        plugin = base / "bin" / "protoc-gen-nanopb"
    if not plugin.exists():
        found = shutil.which("protoc-gen-nanopb")
        if found:
            plugin = Path(found)
        else:
            raise RuntimeError(f"Could not find protoc-gen-nanopb at {plugin}")
    return plugin.resolve(), version, location

def find_betterproto_plugin() -> Path:
    """
    Find the BetterProto 1.x plugin binary:
      protoc-gen-python_betterproto(.exe)
    """
    info = pip_show("betterproto")
    location = Path(info.get("Location", "")).resolve()
    base = location.parent

    names = ["protoc-gen-python_betterproto", "protoc-gen-python-betterproto"]
    exts  = [".exe"] if os.name == "nt" else [""]

    candidates = []
    for nm in names:
        for ext in exts:
            if os.name == "nt":
                candidates.append(base / "Scripts" / f"{nm}{ext}")
            else:
                candidates.append(base / "bin" / f"{nm}{ext}")

    for nm in names:
        w = shutil.which(nm)
        if w:
            candidates.append(Path(w))

    for c in candidates:
        if c and Path(c).exists():
            return Path(c).resolve()

    raise RuntimeError(
        "Could not locate the BetterProto plugin (protoc-gen-python_betterproto). "
        "Try: py -m pip install --upgrade \"betterproto[compiler]>=1.2.5\""
    )

def ensure_deps():
    """Ensure betterproto (with compiler), grpclib, grpcio-tools, and nanopb are installed/upgraded."""
    import importlib.util

    # base package
    if importlib.util.find_spec("betterproto") is None:
        pip_install(["betterproto>=1.2.5"])

    # compiler plugin
    try:
        _ = find_betterproto_plugin()
    except RuntimeError:
        pip_install(["betterproto[compiler]>=1.2.5"])

    # grpclib
    if importlib.util.find_spec("grpclib") is None:
        pip_install(["grpclib>=0.4"])

    # grpcio-tools
    try:
        from grpc_tools import protoc  # noqa
    except ImportError:
        pip_install(["grpcio-tools>=1.56"])

    # nanopb
    if importlib.util.find_spec("nanopb") is None:
        pip_install(["nanopb>=0.4"])

def fetch_nanopb_runtime(version: str):
    needed = [
        "pb.h",
        "pb_common.c", "pb_common.h",
        "pb_encode.c", "pb_encode.h",
        "pb_decode.c", "pb_decode.h",
    ]

    if all((DIR_FW / n).exists() for n in needed):
        print(">> NanoPB runtime already present in Proto-Firmware.")
        return

    url = f"https://github.com/nanopb/nanopb/archive/refs/tags/{version}.zip"
    print(f">> Downloading NanoPB runtime {version} from {url}")

    try:
        with urllib.request.urlopen(url) as resp:
            data = resp.read()
    except Exception as e:
        raise RuntimeError(f"Failed to download NanoPB {version} release: {e}")

    with zipfile.ZipFile(io.BytesIO(data)) as z:
        prefix = f"nanopb-{version}/"
        for name in needed:
            arcname = prefix + name
            try:
                with z.open(arcname) as src, open(DIR_FW / name, "wb") as dst:
                    shutil.copyfileobj(src, dst)
                    print(f">> Wrote {name}")
            except KeyError:
                print(f"!! {name} not found in release archive {version}")
    print(">> NanoPB runtime updated.")

def protoc(args: List[str]) -> int:
    """Use grpc_tools.protoc with given args list."""
    from grpc_tools import protoc as _protoc
    return _protoc.main(args)

def clean():
    removed = 0
    # Remove all generated Python files in Proto-Host
    for f in (DIR_HOST / PY_PKG_NAME).glob("*.py"):
        try:
            if f.name != "__init__.py":
                f.unlink()
                removed += 1
        except Exception:
            pass

    # Always refresh __init__.py
    (DIR_HOST / PY_PKG_NAME / "__init__.py").write_text(
        "# Generated BetterProto Python package\n"
    )

    # Remove all generated firmware files
    for f in DIR_FW.glob("*"):
        try:
            f.unlink()
            removed += 1
        except Exception:
            pass

    print(f">> Cleaned {removed} generated files.")

def generate_for(protos: List[Path], diagnose: bool = False):
    # Always start fresh
    clean()

    nanopb_plugin, nanopb_version, _ = find_nanopb_plugin_and_version()
    betterproto_plugin = find_betterproto_plugin()
    bp_version = pip_show("betterproto").get("Version", "unknown")

    if diagnose:
        print(f">> Using nanopb plugin exe: {nanopb_plugin}")
        print(f">> Using NanoPB version: {nanopb_version}")
        print(f">> Using BetterProto plugin exe: {betterproto_plugin}")
        print(f">> Detected BetterProto version: {bp_version}")

    fetch_nanopb_runtime(nanopb_version)
    inc_dir = str(DIR_DEFS)

    # --- Python (BetterProto) ---
    for p in protos:
        proto = str(p)
        print(f">> Generating Python (BetterProto) for {p.name}")
        py_args = [
            "protoc",
            "-I", inc_dir,
            f"--plugin=protoc-gen-python_betterproto={betterproto_plugin}",
            f"--python_betterproto_out={DIR_HOST / PY_PKG_NAME}",
            proto,
        ]
        if protoc(py_args) != 0:
            raise SystemExit(f"BetterProto codegen failed for {p}")

    # --- NanoPB C/H ---
    plugin_nanopb = f"--plugin=protoc-gen-nanopb={nanopb_plugin}"
    for p in protos:
        proto = str(p)
        print(f">> Generating NanoPB C for {p.name}")
        nanopb_opts = [f"--nanopb_opt=-I{inc_dir}"]
        if diagnose:
            nanopb_opts.append("--nanopb_opt=-v")
        c_args = [
            "protoc",
            "-I", inc_dir,
            plugin_nanopb,
            f"--nanopb_out={DIR_FW}",
            *nanopb_opts,
            proto,
        ]
        if protoc(c_args) != 0:
            raise SystemExit(f"NanoPB codegen failed for {p}")

def discover_protos(selected: Optional[List[str]] = None) -> List[Path]:
    if selected:
        out = []
        for name in selected:
            p = DIR_DEFS / name
            if not p.exists():
                raise SystemExit(f"Proto not found: {name}")
            if p.suffix != ".proto":
                raise SystemExit(f"Not a .proto file: {name}")
            out.append(p)
        return out
    return sorted(DIR_DEFS.glob("*.proto"))

def main(argv: List[str]):
    import argparse
    ap = argparse.ArgumentParser(description="Generate BetterProto (Python) and NanoPB (C) code from .proto files.")
    ap.add_argument("--clean", action="store_true", help="Remove generated files and exit.")
    ap.add_argument("--install", action="store_true", help="(Re)install required Python packages and exit.")
    ap.add_argument("--proto", nargs="+", help="Only (re)generate these .proto files.")
    ap.add_argument("--diagnose", action="store_true", help="Print diagnostic info and enable verbose NanoPB plugin output.")
    args = ap.parse_args(argv)

    ensure_layout()

    if args.clean:
        clean()
        return

    if args.install:
        ensure_deps()
        print(">> Dependencies installed/updated.")
        return

    ensure_deps()

    protos = discover_protos(args.proto)
    if not protos:
        print(f"No .proto files found in {DIR_DEFS}")
        return

    generate_for(protos, diagnose=args.diagnose)
    print("\n✅ Done.")
    print(f" - Python (BetterProto): {DIR_HOST / PY_PKG_NAME}")
    print(f" - Firmware (NanoPB):   {DIR_FW} (includes runtime + generated .c/.h)")
    print(f" - Utilities:           {DIR_NEED}")

if __name__ == "__main__":
    main(sys.argv[1:])
