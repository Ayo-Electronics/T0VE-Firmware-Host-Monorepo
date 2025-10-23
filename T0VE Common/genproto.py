#!/usr/bin/env python
# -*- coding: utf-8 -*-

"""
Generate protobuf code for:
  - Host (Python via BetterProto)
  - Firmware (C via NanoPB)

Repo layout:
  /Proto-Defs        <-- .proto + .options
  /Proto-Host        <-- generated Python
  /Proto-Firmware    <-- generated NanoPB .c/.h + runtime (pb_*.c/.h)
  /Proto-Necessities <-- requirements.txt
  genproto.py        <-- this script
"""

import os
import sys
import shutil
import zipfile
import urllib.request
import io
from pathlib import Path
from typing import List, Optional, Tuple
from contextlib import contextmanager

# ---------- Config ----------
REQS = [
    "betterproto[compiler]>=2.0.0",
    "grpclib>=0.4",
    "grpcio-tools>=1.56",
    "nanopb>=0.4",
]
PY_PKG_NAME = "proto_messages"
# ----------------------------

ROOT = Path(__file__).resolve().parent
DIR_DEFS = ROOT / "Proto-Defs"
DIR_HOST = ROOT / "Proto-Host"
DIR_FW = ROOT / "Proto-Firmware"
DIR_NEED = ROOT / "Proto-Necessities"

def _posix_rel(path: Path, start: Path) -> str:
    """Relative path from start, in POSIX form (forward slashes)."""
    return Path(os.path.relpath(path, start)).as_posix()

def _run(cmd: List[str]) -> int:
    import subprocess
    return subprocess.call(cmd, shell=False)

def _pip_install(pkgs: List[str], upgrade: bool = True) -> None:
    cmd = [sys.executable, "-m", "pip", "install"]
    if upgrade:
        cmd.append("--upgrade")
    cmd.extend(pkgs)
    print(">> Installing:", " ".join(pkgs))
    rc = _run(cmd)
    if rc != 0:
        raise SystemExit("pip install failed.")

def _pip_show(pkg: str) -> dict:
    import subprocess
    try:
        r = subprocess.run(
            [sys.executable, "-m", "pip", "show", pkg],
            capture_output=True, text=True, check=True
        )
    except subprocess.CalledProcessError as e:
        raise RuntimeError(f"'pip show {pkg}' failed: {e}") from e
    out = {}
    for line in r.stdout.splitlines():
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

def find_nanopb_plugin_and_version() -> Tuple[Path, str, Path]:
    info = _pip_show("nanopb")
    version = info.get("Version")
    if not version:
        raise RuntimeError("nanopb not found (no Version from pip show).")
    location = Path(info.get("Location", "")).resolve()
    if not location:
        raise RuntimeError("nanopb installed but pip didn't return Location.")
    base = location.parent

    # usual venv layout
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
    info = _pip_show("betterproto")
    location = Path(info.get("Location", "")).resolve()
    base = location.parent
    names = ["protoc-gen-python_betterproto", "protoc-gen-python-betterproto"]
    exts = [".exe"] if os.name == "nt" else [""]

    cands: List[Path] = []
    for nm in names:
        for ext in exts:
            if os.name == "nt":
                cands.append(base / "Scripts" / f"{nm}{ext}")
            else:
                cands.append(base / "bin" / f"{nm}{ext}")
    for nm in names:
        w = shutil.which(nm)
        if w:
            cands.append(Path(w))
    for c in cands:
        if c and c.exists():
            return c.resolve()
    raise RuntimeError(
        "Could not locate BetterProto plugin (protoc-gen-python_betterproto). "
        "Try: py -m pip install --upgrade \"betterproto[compiler]>=2.0.0\""
    )

def ensure_deps():
    import importlib.util
    if importlib.util.find_spec("betterproto") is None:
        _pip_install(["betterproto>=2.0.0"])
    try:
        _ = find_betterproto_plugin()
    except RuntimeError:
        _pip_install(["betterproto[compiler]>=2.0.0"])
    if importlib.util.find_spec("grpclib") is None:
        _pip_install(["grpclib>=0.4"])
    try:
        from grpc_tools import protoc as _  # noqa
    except Exception:
        _pip_install(["grpcio-tools>=1.56"])
    if importlib.util.find_spec("nanopb") is None:
        _pip_install(["nanopb>=0.4"])

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
    from grpc_tools import protoc as _protoc
    return _protoc.main(args)

@contextmanager
def pushd(path: Path):
    prev = Path.cwd()
    os.chdir(path)
    try:
        yield
    finally:
        os.chdir(prev)

def clean():
    removed = 0
    for f in (DIR_HOST / PY_PKG_NAME).glob("*.py"):
        if f.name != "__init__.py":
            try:
                f.unlink(); removed += 1
            except Exception:
                pass
    (DIR_HOST / PY_PKG_NAME / "__init__.py").write_text("# Generated BetterProto Python package\n")
    for f in DIR_FW.glob("*"):
        try:
            if f.suffix in (".c", ".h"):
                f.unlink(); removed += 1
        except Exception:
            pass
    print(f">> Cleaned {removed} generated files.")

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

def generate_for(protos: List[Path], diagnose: bool = False):
    clean()

    nanopb_plugin, nanopb_version, _ = find_nanopb_plugin_and_version()
    betterproto_plugin = find_betterproto_plugin()
    bp_version = _pip_show("betterproto").get("Version", "unknown")
    if diagnose:
        print(f">> Using nanopb plugin exe: {nanopb_plugin}")
        print(f">> Using NanoPB version: {nanopb_version}")
        print(f">> Using BetterProto plugin exe: {betterproto_plugin}")
        print(f">> Detected BetterProto version: {bp_version}")

    fetch_nanopb_runtime(nanopb_version)

    # Use POSIX relative paths to avoid Windows colon issues and to tolerate spaces.
    inc_dir_rel = _posix_rel(DIR_DEFS, ROOT)
    out_py_rel  = _posix_rel(DIR_HOST / PY_PKG_NAME, ROOT)
    out_c_rel   = _posix_rel(DIR_FW, ROOT)

    # Build proto file list relative to Proto-Defs (so includes are clean)
    proto_rels = [Path(os.path.relpath(p, DIR_DEFS)).as_posix() for p in protos]

    # Compose common args
    common_inc = ["-I", inc_dir_rel]

    with pushd(ROOT):
        # ---- BetterProto (Python) ----
        for pr in proto_rels:
            print(f">> Generating Python (BetterProto) for {pr}")
            py_args = [
                "protoc",
                *common_inc,
                f"--plugin=protoc-gen-python_betterproto={betterproto_plugin}",
                f"--python_betterproto_out={out_py_rel}",
                pr,
            ]
            if diagnose:
                print("   protoc args:", py_args)
            if protoc(py_args) != 0:
                raise SystemExit(f"BetterProto codegen failed for {pr}")

        # ---- NanoPB (C) ----
        # IMPORTANT: on Windows, keep --nanopb_out as a bare directory (no embedded options),
        # and pass options through --nanopb_opt to avoid 'C:\' colon parsing issues.
        base_opts = [f"-I{inc_dir_rel}"]
        if diagnose:
            base_opts.append("-v")

        plugin_nanopb = f"--plugin=protoc-gen-nanopb={nanopb_plugin}"
        nanopb_out    = f"--nanopb_out={out_c_rel}"

        for pr in proto_rels:
            print(f">> Generating NanoPB C for {pr}")
            c_args = [
                "protoc",
                *common_inc,
                plugin_nanopb,
                nanopb_out,
                # pass each option via separate --nanopb_opt=...
                *[f"--nanopb_opt={opt}" for opt in base_opts],
                pr,
            ]
            if diagnose:
                print("   protoc args:", c_args)
            if protoc(c_args) != 0:
                raise SystemExit(f"NanoPB codegen failed for {pr}")

def main(argv: List[str]):
    import argparse
    ap = argparse.ArgumentParser(
        description="Generate BetterProto (Python) and NanoPB (C) code from .proto files."
    )
    ap.add_argument("--clean", action="store_true", help="Remove generated files and exit.")
    ap.add_argument("--install", action="store_true", help="(Re)install required Python packages and exit.")
    ap.add_argument("--proto", nargs="+", help="Only (re)generate these .proto files (by filename).")
    ap.add_argument("--diagnose", action="store_true", help="Print diagnostic info and exact protoc args.")
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

    missing_opts = []
    for p in protos:
        if not p.with_suffix(".options").exists():
            missing_opts.append(p.with_suffix(".options").name)
    if missing_opts:
        print(">> Note: No .options found for:", ", ".join(missing_opts))

    generate_for(protos, diagnose=args.diagnose)
    print("\n✅ Done.")
    print(f" - Python (BetterProto): {(DIR_HOST / PY_PKG_NAME).resolve()}")
    print(f" - Firmware (NanoPB):   {DIR_FW.resolve()} (runtime + generated .c/.h)")
    print(f" - Utilities:           {DIR_NEED.resolve()}")

if __name__ == "__main__":
    main(sys.argv[1:])
