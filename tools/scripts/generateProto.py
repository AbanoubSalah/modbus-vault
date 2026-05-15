"""
Name:          generateProto.py
Description:   A script for generating protobuf description file.
Author:        Abanoub Salah
Version:       1.0.0
Dependencies:  protoc
License:       MIT
"""

import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

# Proto file path in project tree
PROTO = ROOT / "components" / "serializer" / "proto" / "modbus.proto"
OUT = ROOT / "tools" / "modbus_pipeline"

OUT.mkdir(parents=True, exist_ok=True)

subprocess.check_call([
    "protoc",
    f"--proto_path={PROTO.parent}",
    f"--python_out={OUT}",
    str(PROTO),
])
