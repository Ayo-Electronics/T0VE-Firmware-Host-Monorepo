# genproto.py

## Overview

`genproto.py` is a utility script designed to automate the generation of
protocol buffer (protobuf) files and related artifacts. It simplifies
workflows that rely on gRPC, protobuf definitions, or code generation
for multiple languages by providing a reproducible and consistent build
pipeline.

------------------------------------------------------------------------

## How It Works

1.  **Input Definitions**\
    The script takes `.proto` files as inputs. These files define data
    structures and services using the Protocol Buffers language.

2.  **Code Generation**\
    Using `protoc` (the Protocol Buffers compiler), the script generates
    source code in one or more target languages (e.g., Python, Go,
    Java).

    -   It may invoke plugins (such as `grpc_python_plugin`) for gRPC
        stubs.
    -   Output paths are configured to keep generated code organized.

3.  **File Management**\
    The script typically:

    -   Ensures output directories exist.
    -   Cleans or overwrites old generated files.
    -   Verifies successful generation by checking file presence or
        `protoc` exit codes.

4.  **Extensibility**\
    The script can be extended to:

    -   Handle multiple proto directories.
    -   Apply language-specific post-processing (e.g., formatting,
        imports).

------------------------------------------------------------------------

## Directory Structure

A typical project using `genproto.py` might look like this:

    project-root/
    ├── genproto.py              # The script itself
    ├── protos/                  # Source .proto files
    │   ├── service1.proto
    │   ├── service2.proto
    │   └── common/
    │       └── types.proto
    ├── generated/               # Generated output code
    │   ├── python/              # Python stubs
    │   │   ├── service1_pb2.py
    │   │   ├── service1_pb2_grpc.py
    │   │   └── ...
    │   └── go/                  # Go stubs (if generated)
    │       ├── service1.pb.go
    │       └── ...
    └── tests/                   # Unit tests for generated code

------------------------------------------------------------------------

## Usage

### Prerequisites

-   Install [Protocol Buffers Compiler
    (`protoc`)](https://grpc.io/docs/protoc-installation/).
-   Install any required plugins for your target languages (e.g.,
    `pip install grpcio-tools` for Python).

### Command

Run the script from the command line:

``` bash
python genproto.py --proto_path=./protos --out=./generated --lang=python
```

### Common Arguments

-   `--proto_path`: Path to the directory containing `.proto` files.
-   `--out`: Output directory for generated code.
-   `--lang`: Target language (e.g., `python`, `go`, `java`).
-   `--clean`: (Optional) Clear the output directory before generation.

------------------------------------------------------------------------

## Common Pitfalls

1.  **Missing `protoc` in PATH**\
    Ensure `protoc` is installed and accessible globally
    (`protoc --version` should work).

2.  **Plugin Errors**\
    Some languages require plugins (`protoc-gen-grpc-*`). Check
    installation paths.

3.  **Import Resolution**\
    If `.proto` files import others, the `--proto_path` must include all
    relevant directories.

4.  **Permission Issues**\
    Ensure the script has write access to the output directory.

------------------------------------------------------------------------

## Verifying the Output

1.  **Check File Creation**\
    Confirm that generated files exist in the target output directory.

2.  **Compile and Run**

    -   For Python: `python -m py_compile generated/*.py`
    -   For Go: `go build ./generated/...`

3.  **Test gRPC Services**\
    Implement a basic client/server using the generated stubs and ensure
    they communicate successfully.

4.  **Regeneration Consistency**\
    Running the script twice should produce identical output
    (idempotency).

------------------------------------------------------------------------

## Example Workflow

``` bash
# Clean and regenerate Python protobuf files
python genproto.py --proto_path=./api/protos --out=./api/generated --lang=python --clean

# Compile generated files to check correctness
python -m py_compile api/generated/*.py
```

------------------------------------------------------------------------

## Contributing

If extending the script for new languages or workflows: 1. Add CLI
options for new targets.\
2. Update the code generation logic.\
3. Document the changes here.

------------------------------------------------------------------------

## License

This script is provided under the MIT License. See `LICENSE` for
details.
