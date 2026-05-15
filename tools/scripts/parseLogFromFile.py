"""
Name:          parseLogFromFile.py
Description:   A script for parsing Blackbox Logger binary dump, with option to
               display entries formatted text or json with entries limiting option.
Author:        Abanoub Salah
Version:       1.0.0
Dependencies:  modbus_pipeline
License:       MIT
"""

from modbus_pipeline.log_parser import LogParser
from modbus_pipeline.serializer import Serializer
import argparse
import json

def main():
    parser = argparse.ArgumentParser(description="Blackbox Logger Log Parser")
    parser.add_argument("file", help="Binary dump file")
    parser.add_argument("--json", action="store_true", help="Output JSON")
    parser.add_argument("--limit", type=int, default=None, help="Limit entries")

    args = parser.parse_args()

    with open(args.file, "rb") as f:
        blob = f.read()

    frames = []
    count = 0

    parser = LogParser(blob)
    for entry in parser:
        if args.limit and count >= args.limit:
            break

        frame = Serializer.deserialize(entry.data)
        if frame:
            if args.json:
                frames.append(frame.to_dict())
                frames[-1]['id'] = entry.id
            else:
                print(f'{frame}ID: {entry.id}')

        count += 1

    if args.json:
        print(json.dumps(frames, indent=2))

if __name__ == "__main__":
    main()
