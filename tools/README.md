# Modbus Pipeline

A Python package designed to decode and serialize Blackbox Logger records containing Modbus frames.

## Installation

To install dependencies and package requirements, run:
```bash
# Generate protobuf description file
python3 scripts/generateProto.py
pip install .
```
Or with a virtual environment
```bash
python3 -m venv .venv
source .venv/bin/activate
# Generate protobuf description file
python3 scripts/generateProto.py
pip install -e .
```

## Usage Example
Parsing a log file:

```Python
from modbus_pipeline.log_parser import LogParser
from modbus_pipeline.serializer import Serializer

# Read binary data
with open("test_log.bin", "rb") as f:
    blob = f.read()

parser = LogParser(blob)
for entry in parser:
    frame = Serializer.deserialize(entry.data)
    print(frame)
```

## Included Scripts

| Script                     | Description                                            |
| :--------------------------| :----------------------------------------------------- |
| `generateProto.py`         | Generate protobuf description file                     |
| `generateSampleLogFile.py` | Generates a test `.bin` file with random sample frames |
| `parseLiveTelemetry.py`    | Reads live events over MQTT and parses them            |
| `parseLogFromFile.py`      | Parse Blackbox Logger binary dump                      |
| `sendModbusTransactions.py`| Transmit Modbus transactions over a serial port        |
