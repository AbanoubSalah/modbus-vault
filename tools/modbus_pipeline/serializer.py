"""
Core serialization/deserialization functionality

This module provides the core functionality for serializing/deserializing provided
data according to 'modbus.proto' file included with the project
"""

__author__ = "Abanoub Salah"
__version__ = "0.1.0"
__license__ = "MIT"
__all__ = ["Serializer"]

from typing import Optional
from . import modbus_pb2
from . import modbus

class Serializer:
    """
    Provide functionality for:
        Serialize Modbus frame
        Deserialize Modbus frame
    """

    @staticmethod
    def serialize(frame: modbus.ModbusFrame) -> bytes:
        # Prepare Protobuf Payload
        entry = modbus_pb2.ModbusFrame()
        entry.slave_address = frame.slave_address
        entry.function_code = frame.function_code
        entry.timestamp_us = frame.timestamp_us
        entry.data = frame.data
    
        serialized_data = entry.SerializeToString()

        return serialized_data

    @staticmethod
    def deserialize(payload: bytes) -> Optional[modbus.ModbusFrame]:
        try:
            entry = modbus_pb2.ModbusFrame()
            entry.ParseFromString(payload)
            return modbus.ModbusFrame(entry.timestamp_us, entry.slave_address, entry.function_code, entry.data)
        except Exception as e:
            print(f'Serializer error: {e}')
            return None
