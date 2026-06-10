"""
Name:          provisionDevice.py
Description:   Provision device's certificates by nul-terminating each one, stitching them together and wrap them
               between a header and Modbus CRC16. Finally send them through esptool.
Author:        Abanoub Salah
Version:       1.0.0
Dependencies:  esptool
License:       MIT
"""

import os
import sys
import argparse
import struct
from typing import NamedTuple
from pathlib import Path
import tempfile
import esptool
import espsecure

HEADER_MAGIC = 0x43455254
PARTITION_LABEL_DEFAULT = 'certs'

class PartitionEntry(NamedTuple):
    """
    ESP partition table structure type layout
    """

    magic: int     # 2 Bytes (0xAA50)
    type_id: int   # 1 Byte
    subtype: int   # 1 Byte
    offset: int    # 4 Bytes (uint32)
    size: int      # 4 Bytes (uint32)
    label: bytes   # 16 Bytes (char array)
    flags: int     # 4 Bytes (uint32)

def crc16_modbus(data: bytes) -> int:
    """
    Calculate Modbus CRC-16
    """
    crc = 0xFFFF
    for b in data:
        crc ^= b
        for _ in range(8):
            crc = (crc >> 1) ^ 0xA001 if (crc & 1) else crc >> 1
    return crc & 0xFFFF

def get_offset_from_local_bin(partition_table_bin_path: Path, partition_label: str) -> int:
    """
    Parses the partition table binary using standard struct.
    """

    if not partition_table_bin_path.exists():
        print(f"Partition table binary not found: {partition_table_bin_path}")
        return -1

    print(f"Fetching '{partition_label}' partition offset...")

    # components/bootloader_support/include/esp_flash_partitions.h
    # esp_partition_info_t structure
    # |Magic|Type|SubType|Offset|Size|Name|Flags|
    # |  16 |  8 |   8   |  32  | 32 | 16B|  32 |
    PARTITION_FORMAT = "<HBBII16sI"
    ENTRY_SIZE = 32
    ESP_PARTITION_MAGIC = 0x50AA

    # Loop through the binary data in 32-byte chunks
    with open(partition_table_bin_path, "rb") as f:
        while (chunk := f.read(ENTRY_SIZE)):
            if len(chunk) < ENTRY_SIZE:
                break

            # Unpack according to Espressif's specification
            # type, subtype, offset, size, label, flags
            entry = PartitionEntry._make(struct.unpack(PARTITION_FORMAT, chunk))

            # Invalid entry
            if entry.magic != ESP_PARTITION_MAGIC:
                continue

            label = entry.label.split(b'\x00', 1)[0].decode('ascii', errors='ignore')

            if label == partition_label:
                return entry.offset

    return -1

def create_provisioning_blob(client_crt_path: Path, client_key_path: Path, ca_crt_path: Path) -> bytes:
    """
    Packages mTLS certificates/key encased by header and Modbus CRC16 appended at the end
    """

    blob = b''
    try:
        client_data = client_crt_path.read_bytes().strip() + b'\x00'
        key_data = client_key_path.read_bytes().strip() + b'\x00'
        ca_data = ca_crt_path.read_bytes().strip() + b'\x00'

        # construct header struct
        # | magic |  client_ca  |client_ca_len|  client_key  |client_key_len|   ca  | ca_len|
        # |32-bits|   32-bits   |   32-bits   |    32-bits   |    32-bits   |32-bits|32-bits|
        offset_client = 32
        offset_key = offset_client + len(client_data)
        offset_ca = offset_key + len(key_data)

        header = struct.pack("<IIIIIII", HEADER_MAGIC, offset_client, len(client_data), offset_key, len(key_data), offset_ca, len(ca_data))
        header = header.ljust(32, b'\x00')

        payload = header + client_data + key_data + ca_data

        # Making sure blob size is 32-Bytes aligned for correct flash memory mapping
        # Calculating (padding size - 2) to leave space for CRC
        padding_size = (32 - ((len(payload) + 2) % 32)) % 32
        payload += b'\x00' * padding_size
        blob = payload + struct.pack("<H", crc16_modbus(payload))

        assert (len(blob) % 32 == 0), "Blob size alignment verification failed!"

    except FileNotFoundError as e:
        print(f"Missing certificate: {e}")
    except Exception as e:
        print(f"Blob creation error: {e}")
        blob = b''

    return blob

def encrypt_blob(plain_blob: bytes, key_file_path: Path, physical_address: int) -> bytes:
    """
    Encrypts the plain blob using provided key before flashing.
    """

    encrypted_blob = b''

    if not key_file_path.exists():
        print(f"Error: Flash encryption key '{key_file_path}' not found")
        return encrypted_blob
    elif key_file_path.stat().st_size != 32:
        print(f"Error: Key '{key_file_path}' is {key_file_path.stat().st_size} bytes. Must be exactly 32 bytes.")
        return encrypted_blob
    else:
        print(f"Encrypting blob for address {hex(physical_address)}...")

    # Create temp in/out files needed for espsecure tool
    fd_in, path_in = tempfile.mkstemp(suffix=".bin")
    fd_out, path_out = tempfile.mkstemp(suffix=".bin")
    # Closing files before passing for safe access
    os.close(fd_in)
    os.close(fd_out)
    try:
        Path(path_in).write_bytes(plain_blob)

        espsecure_args = [
            "encrypt-flash-data",
            "--keyfile", str(key_file_path),
            "--address", str(physical_address),
            "--output", path_out,
            path_in
        ]

        espsecure.main(espsecure_args)

        encrypted_blob = Path(path_out).read_bytes()
    except SystemExit as e:
        if e.code != 0:
            raise RuntimeError(f"espsecure exited with code {e.code}")
    except Exception as e:
        print(f"Encryption failed: {e}")
        encrypted_blob = b''
    finally:
        # Clean-up
        Path(path_in).unlink(missing_ok=True)
        Path(path_out).unlink(missing_ok=True)

    return encrypted_blob


def flash_partition(blob: bytes, address: int, chip: str, port: str, baudrate: int, encrypt_flag: bool) -> bool:
    """
    Flash blob to partition using esptool
    """

    print(f"Flashing partition...")

    # Create temp file and close it for safe access (needed for esptool)
    fd_in, path_in = tempfile.mkstemp(suffix=".bin")
    os.close(fd_in)
    try:
        Path(path_in).write_bytes(blob)

        esptool_args = ["--chip", chip]

        if port:
            esptool_args.extend(["--port", port])

        esptool_args.extend(["--baud", str(baudrate), "write-flash"])

        if encrypt_flag:
            esptool_args.append("--encrypt")
            print("Using esptool's (--encrypt) flag...")

        esptool_args.extend([str(hex(address)), path_in])

        esptool.main(esptool_args)
        print("Flash successful!")
    except SystemExit as e:
        if e.code != 0:
            raise RuntimeError(f"esptool exited with code {e.code}")
    except Exception as e:
        print(f"Flash error: {e}")
        return False
    finally:
        Path(path_in).unlink(missing_ok=True)

    return True

def main():
    parser = argparse.ArgumentParser(description="Provisioner tool", formatter_class=argparse.ArgumentDefaultsHelpFormatter)
    parser.add_argument(
        "--mode", 
        choices=["plain", "host-encrypted", "device-encrypted"], 
        default="plain",
        help="Flashing modes: 'plain' sends unencrypted data, 'host-encrypted' uses a local key file, "
             "'device-encrypted' uses esptool's --encrypt option."
    )

    parser.add_argument("--part-table-bin", default=Path("../build/partition_table/partition-table.bin"), type=Path, help="Path to local build partition-table.bin")
    parser.add_argument("--partition-label", default=PARTITION_LABEL_DEFAULT, type=str, help="Partition label")
    parser.add_argument("--part-enc-key", default = None, type=Path, help="Partition encryption key")

    parser.add_argument("--cert", default=Path("../main/certs/client.crt"), type=Path, help="Client certificate")
    parser.add_argument("--key", default=Path("../main/certs/client.key"), type=Path, help="Client key")
    parser.add_argument("--cafile", default=Path("../main/certs/ca.crt"), type=Path, help="CA certificate")

    parser.add_argument("--chip", default="esp32s3", type=str, help="Chip name")
    parser.add_argument("--port", default=None, type=str, help="UART port")
    parser.add_argument("--baudrate", default=921600, type=int, help="UART baudrate")

    args = parser.parse_args()

    blob = create_provisioning_blob(
        args.cert.resolve(),
        args.key.resolve(),
        args.cafile.resolve()
        )
    if not blob:
        print("Failed to create partition blob!")
        sys.exit(1)
    else:
        print(f"Partition binary created successfully with size {len(blob)} Byte(s)")

    address = get_offset_from_local_bin(args.part_table_bin.resolve(), args.partition_label)
    if address < 0:
        print(f"Label '{args.partition_label}' was not found!")
        sys.exit(1)
    else:
        print(f"Found '{args.partition_label}' partition mapped to address: {hex(address)}")

    target_blob = blob
    needs_device_side_encryption = False

    if args.mode == "host-encrypted":
        if not args.part_enc_key:
            print("Missing encryption key!")
            sys.exit(1)

        target_blob = encrypt_blob(blob, args.part_enc_key.resolve(), address)
        if not target_blob:
            print("Failed to encryp blob!")
            sys.exit(1)
    elif args.mode == "device-encrypted":
        needs_device_side_encryption = True

    try:
        flash_partition(
            target_blob,
            address,
            args.chip,
            args.port,
            args.baudrate,
            needs_device_side_encryption
        )
    except Exception as e:
        print(f"Execution failed: {e}")

if __name__ == "__main__":
    main()
