# Protobuf Build System (Host ↔ Device)

This repository provides a unified way to generate protobuf message bindings for both:

- **Firmware (C / STM32)** using [NanoPB](https://github.com/nanopb/nanopb)  
- **Host (Python)** using [BetterProto](https://github.com/danielgtaylor/python-betterproto)  

It ensures that the same `.proto` definitions drive both sides, with version-locked runtimes and automatic cleanup to avoid stale files.

---

## Directory Layout

```
/host_device_common
   /Proto-Defs          # .proto and .options files
   /Proto-Host
       /proto_messages  # Generated Python (BetterProto) files
   /Proto-Firmware      # Generated NanoPB .pb.[ch] + runtime (pb.h, pb_common/encode/decode)
   /Proto-Necessities   # requirements.txt (locked dependencies)
   genproto.py          # Unified generator script
   genproto.py - README.md   # This file
```

---

## Usage

### Initial setup
```bash
py genproto.py --install
```
This installs/upgrades required pip dependencies:
- `betterproto[compiler]` (BetterProto + plugin)
- `grpclib`
- `grpcio-tools`
- `nanopb`

### Normal generation
```bash
py genproto.py
```
This regenerates both Python and C code. The script always **cleans old outputs first** to avoid stale files.

### Diagnostics
```bash
py genproto.py --diagnose
```
Prints plugin paths, versions, and enables verbose NanoPB logging.

### Clean only
```bash
py genproto.py --clean
```
Deletes all generated code from `Proto-Host` and `Proto-Firmware`.

---

## How the Generator Works

`genproto.py` automates all steps:

1. **Dependency Management**
   - Uses `pip show` and path checks to locate plugins.
   - Ensures `protoc-gen-python_betterproto` and `protoc-gen-nanopb` exist.
   - Downloads NanoPB runtime source files (`pb.h`, `pb_common.*`, `pb_encode.*`, `pb_decode.*`)
     matching the installed `nanopb` pip package version.

2. **Cleaning Before Build**
   - Deletes all previously generated files (`*.py`, `*.pb.[ch]`, NanoPB runtime).
   - Recreates a fresh `__init__.py` in `Proto-Host/proto_messages`.

3. **Code Generation**
   - **BetterProto (Python)**:
     ```bash
     protoc -I Proto-Defs --plugin=protoc-gen-python_betterproto=...             --python_betterproto_out=Proto-Host/proto_messages app_messages.proto
     ```
     Output: dataclasses (e.g. `App_Messages.py`).
   - **NanoPB (C)**:
     ```bash
     protoc -I Proto-Defs --plugin=protoc-gen-nanopb=...             --nanopb_out=Proto-Firmware --nanopb_opt=-IProto-Defs app_messages.proto
     ```
     Output: `.pb.c/.pb.h` and runtime headers/sources.

4. **`.options` Handling**
   - NanoPB automatically applies `.options` from `Proto-Defs/`.
   - Syntax rules:
     - `Message.field max_size:32` (if no package)
     - `package.Message.field max_size:32` (if package used in `.proto`)
     - Use `#` for comments, not `//`.
   - Example:
     ```
     demo.Simple.note max_size:32
     ```

---

## Testing Python Output

Example using BetterProto:

```python
from proto_messages import App_Messages

msg = App_Messages.Command(command_id=42, command_data="Hello!")
print("Original:", msg)

data = msg.SerializeToString()
print("Encoded:", data)

decoded = App_Messages.Command().parse(data)
print("Decoded:", decoded)

assert decoded == msg
```

Run with:
```bash
python test_app_messages.py
```

Expected output:
```
Original: Command(command_id=42, command_data='Hello!')
Encoded: b'...'
Decoded: Command(command_id=42, command_data='Hello!')
✅ Round-trip encode/decode successful!
```

---

## Cross-Verification Between Host and Device

To ensure host (Python) ↔ device (STM32) compatibility:

### Encode in Python → Decode in C
```python
# Python
msg = App_Messages.Command(command_id=99, command_data="ping")
data = msg.SerializeToString()
# send `data` over USB bulk pipe
```

On STM32:
```c
demo_Command cmd = demo_Command_init_default;
pb_istream_t stream = pb_istream_from_buffer(buffer, length);
bool status = pb_decode(&stream, demo_Command_fields, &cmd);

if (status) {
    // cmd.command_id == 99
    // cmd.command_data == "ping"
}
```

---

### Encode in C → Decode in Python
STM32 side:
```c
demo_Command cmd = demo_Command_init_default;
cmd.command_id = 123;
strncpy(cmd.command_data, "pong", sizeof(cmd.command_data));

uint8_t buffer[128];
pb_ostream_t stream = pb_ostream_from_buffer(buffer, sizeof(buffer));
pb_encode(&stream, demo_Command_fields, &cmd);
// send buffer over USB
```

Python side:
```python
msg = App_Messages.Command().parse(data)
print(msg)  # Command(command_id=123, command_data="pong")
```

---

## Notes

- **BetterProto**: focuses on Proto3, but can parse many Proto2 files. If you rely heavily on Proto2-only features (e.g. `extensions`), generation may fail.
- **NanoPB**: excellent for embedded, lightweight C runtime.
- The generator script ensures the **NanoPB runtime version matches the installed pip package version** for reproducibility.

---

## Summary

- Define your messages once in `Proto-Defs/*.proto` (+ `.options` if needed).  
- Run `py genproto.py` to generate fresh host/device code.  
- Python host: import from `proto_messages`.  
- STM32 device: include from `Proto-Firmware`.  
- Test with round-trip encoding to confirm correctness.


---

## Troubleshooting / Common Pitfalls

### 1. `.options` file not applied
- Make sure the syntax matches NanoPB’s expectations.
- Use `Message.field max_size:NN` or `package.Message.field max_size:NN` if your `.proto` has a package.
- Comments must begin with `#`, not `//`.

### 2. Stale files in `Proto-Host`
- BetterProto generates plain `.py` files (e.g. `App_Messages.py`), not `*_pb2.py`.
- The generator now always cleans before regeneration, but if you see unexpected behavior, run:
  ```bash
  py genproto.py --clean
  ```

### 3. Plugin executable not found
- If you see an error about `protoc-gen-nanopb` or `protoc-gen-python_betterproto`:
  - Re-run `py genproto.py --install`
  - Ensure your `pip` user base `Scripts/` directory is on `PATH`.
  - On Windows this is often: `%APPDATA%\Python\Python3XX\Scripts`

### 4. Spaces in paths (Windows)
- Protoc plugins sometimes fail if your repo path contains spaces (e.g. `OneDrive/My Documents`).
- Move the repo to a space-free path if you hit strange plugin errors.

### 5. Version mismatches
- This system locks NanoPB runtime to the version installed via pip.
- If regenerating after upgrading `nanopb`, old runtime headers may cause warnings — always re-run generation.

### 6. BetterProto limitations
- BetterProto is strongest with **Proto3**.  
- It can parse many Proto2 messages, but advanced Proto2-only features (like `extensions`) are not supported.

