"""
Core Blackbox Logger parsing functionality

This module provides the core functionality for parsing/creating Blackbox Logger
entries from constructing an entry to parsing one
"""

__author__ = "Abanoub Salah"
__version__ = "0.1.0"
__license__ = "MIT"
__all__ = ["LogEntry", "LogParser"]

import sys
import struct
from typing import Generator, Optional

class LogEntry:
    """
    Represents a Blackbox Logger entry as defined in the project, consisting of a unique ID and payload data

    Attributes:
        entry_id (int): The unique identifier of the log entry.
        payload (bytes): The raw data/payload for the log entry.
    """
    def __init__(self, entry_id: int, payload: bytes):
        self.id = entry_id
        self.data = payload

    def __str__(self):
        return f"""Log Entry:
---------
Entry ID: {self.id}
Entry Data: {self.data.hex()}
"""


class LogParser:
    """
    Parses Blackbox Logger entries from a binary blob of bytes

    This class provides the following functionality:
        - Iterates over entries present in the blob with the defined project schema
        - Validates CRC and magic bytes for data integrity
        - Creates new log entries from given payload data
    """
    START_MAGIC = 0x5355424D
    END_MAGIC   = 0x4D425553
    ALIGN       = 4

    HEADER_FMT = "<I I I"  # header_magic, id, len
    TAIL_FMT = "<H I"  # CRC, tail_magic
    HEADER_SIZE = struct.calcsize(HEADER_FMT)
    TAIL_SIZE = struct.calcsize(TAIL_FMT)

    def __init__(self, blob: bytes):
        self.blob = blob
        self.cur_offset = 0
        self.last_error_message = ''
        self.cur_entry = self._parse_entry()

    def __iter__(self):
        return self

    def __next__(self):
        # Check if we have reached the end of log
        if self.cur_entry is None:
            raise StopIteration

        current_entry = self.cur_entry
        self.cur_entry = self._parse_entry()
        return current_entry

    def _parse_entry(self) -> Optional[LogEntry]:
        if self.cur_offset + self.HEADER_SIZE > len(self.blob):
            self.last_error_message = f'Non-existent entry at offset {self.cur_offset}'
            return None

        header_magic, entry_id, length = struct.unpack_from(self.HEADER_FMT, self.blob, self.cur_offset)

        if header_magic != self.START_MAGIC:
            self.last_error_message = f'Bad entry magic at offset {self.cur_offset}'
            return None

        total_size = self.HEADER_SIZE + length + self.TAIL_SIZE
        aligned_size = self._align_up(total_size)

        if self.cur_offset + total_size > len(self.blob):
            self.last_error_message = f'Corrupted entry at offset {self.cur_offset}'
            return None

        payload_start = self.cur_offset + self.HEADER_SIZE
        payload_end = payload_start + length

        payload = self.blob[payload_start:payload_end]

        stored_crc, end_magic = struct.unpack_from(self.TAIL_FMT, self.blob, payload_end)

        # Validate end magic
        if end_magic != self.END_MAGIC:
            self.last_error_message = f'Bad end magic at offset {self.cur_offset}'
            return None

        # Validate CRC
        crc_data = self.blob[self.cur_offset:payload_end]
        calc_crc = self.crc16_modbus(crc_data)

        if calc_crc != stored_crc:
            self.last_error_message = f'CRC mismatch at offset {self.cur_offset}'
            return None
        else:
            self.cur_offset += aligned_size

        return LogEntry(entry_id, payload)

    def get_last_error(self) -> str:
        return self.last_error_message

    @staticmethod
    def _align_up(x: int, align: Optional[int] = None) -> int:
        if align == None:
            align = LogParser.ALIGN
        return (x + (align - 1)) & ~(align - 1)

    @staticmethod
    def crc16_modbus(data: bytes) -> int:
        crc = 0xFFFF
        for b in data:
            crc ^= b
            for _ in range(8):
                if crc & 1:
                    crc = (crc >> 1) ^ 0xA001
                else:
                    crc >>= 1
        return crc & 0xFFFF

    @staticmethod
    def create_entry(entry_id: int, payload: bytes) -> bytes:
        # Build Header
        header = struct.pack(LogParser.HEADER_FMT, LogParser.START_MAGIC, entry_id, len(payload))

        # Calculate CRC over Header + Payload
        full_data = header + payload
        crc = LogParser.crc16_modbus(full_data)

        # Build Tail
        tail = struct.pack(LogParser.TAIL_FMT, crc, LogParser.END_MAGIC)

        # Align size
        total_size = len(header) + len(payload) + len(tail)
        aligned_size = LogParser._align_up(total_size)

        return header + payload + tail + b'\x00' * (aligned_size - total_size)
