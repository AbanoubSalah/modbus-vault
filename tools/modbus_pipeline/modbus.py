"""
Core Modbus frame type

This module provides the core type needed for passing Modbus frame around other
used modules
"""

__author__ = "Abanoub Salah"
__version__ = "0.1.0"
__license__ = "MIT"
__all__ = ["ModbusFrame"]

class ModbusFrame:
    """
    Organizes, represents Modbus frames for printing, and prepares frame data for JSON serialization.
    """
    def __init__(self, timestamp_us: int, slave_address: int, function_code: int, data: bytes):
        self.timestamp_us = timestamp_us
        self.slave_address = slave_address
        self.function_code = function_code
        self.data = data

    def __str__(self):
        data_in_hex = self.data.hex().upper()
        formatted_payload = " 0x".join(data_in_hex[i:i+2] for i in range(0, len(data_in_hex), 2))
        return f"""Modbus Frame:
-------------
Timestamp (us): {self.timestamp_us}
Slave Address : {self.slave_address}
Function Code : {self.function_code}
Data Length   : {len(self.data)}
Raw Data      : [0x{formatted_payload}]
"""

    def to_dict(self):
        return {
            "Slave Address": self.slave_address,
            "Function Code": self.function_code,
            "timestamp us": self.timestamp_us,
            "Length": len(self.data),
            "Data": self.data.hex()
            }