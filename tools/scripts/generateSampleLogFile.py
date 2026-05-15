"""
Name:          generateSampleLogFile.py
Description:   A script for generating a sample Blackbox Logger dump of chosen number of
               entries from a set of randomly chosen preset frames.
Author:        Abanoub Salah
Version:       1.0.0
Dependencies:  modbus_pipeline
License:       MIT
"""

import modbus_pipeline
from modbus_pipeline.modbus import ModbusFrame
from modbus_pipeline.log_parser import LogParser
from modbus_pipeline.serializer import Serializer
import os
import random
from datetime import datetime
import argparse

# Pre-Generated Modbus frames
modbus_frames_hex = [
    '010300000001840A',
    '010300000002C40B',
    '01030000000305CB',
    '010300000004440B',
    '01030000000585CB',
    '010300000006C50A',
    '010300000007040B',
    '01030000000845CB',
    '010300000009840B',
    '01030000000AC5CB',
    '02030000000185C9',
    '020300000002C5C8',
    '0203000000030408',
    '02030000000445C8',
    '0203000000058408',
    '0303000000018448',
    '030300000002C449',
    '0303000000030589',
    '0303000000044449',
    '0303000000058589',
    '040300000001858B',
    '040300000002C58A',
    '040300000003044A',
    '040300000004458A',
    '040300000005844A',
    '05030000000185CA',
    '050300000002C5CB',
    '050300000003040B',
    '05030000000445CB',
    '050300000005840B',
    '060300000001850E',
    '060300000002C50F',
    '06030000000304CF',
    '060300000004450F',
    '06030000000584CF',
    '07030000000184CC',
    '070300000002C4CD',
    '070300000003050D',
    '07030000000444CD',
    '070300000005850D',
    '080300000001848F',
    '080300000002C48E',
    '080300000003054E',
    '080300000004448E',
    '080300000005854E',
    '010400000001300A',
    '010400000002700B',
    '02040000000131C9',
    '0304000000013048',
    '050400000001310A'
]

def create_entry(entry_id: int, entry_timestamp_us: int, slave_addr: int, func_code: int, entry_data_bytes: bytes):
    frame = ModbusFrame(entry_timestamp_us, slave_addr, func_code, entry_data_bytes)

    serialized_frame = Serializer.serialize(frame)

    payload = LogParser.create_entry(entry_id, serialized_frame)

    return payload

def main():
    parser = argparse.ArgumentParser(description="Blackbox Logger File Generator")
    parser.add_argument("--file", default="test_log.bin", help="Binary dump file")
    parser.add_argument("--records-count", type=int, default=3, help="Number of records generated")

    args = parser.parse_args()

    if args.records_count <= 0:
        print("Records count must be bigger than zero")
        print("Aborting...")
        exit(0)

    if os.path.exists(args.file):
        response = input(f"File '{args.file}' exists, overwrite it (y)es / (n)o? ")
        if response.lower().startswith("n"):
            print("Aborting...")
            exit(0)

    # Generate the file
    try:
        with open(args.file, "wb") as f:
            # Create test entries
            id = 0
            for idx in range(args.records_count):
                # Pick a random frame
                raw_frame = random.choice(modbus_frames_hex)
                # Get current time
                now = datetime.now()
                # Create log Entry
                log_entry = create_entry(id, now.microsecond, int(raw_frame[0:2], 16), int(raw_frame[2:4], 16), bytes.fromhex(raw_frame))
                f.write(log_entry)
                id += 1
            print(f"Successfully created {args.records_count} records saved to {args.file}")
    except Exception as e:
        print(f"Generation error: {e}")

if __name__ == "__main__":
    main()