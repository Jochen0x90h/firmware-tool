#!/usr/bin/env python3
"""
Firmware Tool - Python Version

Creates a firmware file with the following format:

Field                                      | Type
-------------------------------------------|---------------------------------
Device identifier                          | 0-padded string of 16 characters
Flags for future extensions                | uint32_t
Size of all sections (without file header) | uint32_t
Per section:                               |
Section Identifier                         | uint32_t
Section CRC                                | uint32_t
Section size (without section header)      | uint32_t
Section data                               | Byte data

Usage:
    firmware-tool.py <output file> <device-id> <id1> <file1> <id2> <file2> ...

Example:
    firmware-tool.py firmware.fw EP1000 1 %application% 10 %nextion_GUI%
"""

import sys
import os
import time
import struct
import binascii
import datetime
from pathlib import Path
from typing import List, Tuple


# CRC-32 table
CRC_POLYNOMIAL = 0x04C11DB7
crc_table = [0] * 256


def init_crc_table():
    for i in range(256):
        remainder = i << 24
        for _ in range(8):
            if remainder & 0x80000000:
                remainder = (remainder << 1) ^ CRC_POLYNOMIAL
            else:
                remainder = (remainder << 1)
        crc_table[i] = remainder & 0xFFFFFFFF

def calc_crc32(data: bytes, init_crc: int = 0xFFFFFFFF) -> int:
    crc = init_crc

    length = len(data)
    padded_length = (length + 3) & ~3   # nächstes Vielfaches von 4

    i = 0
    while i < padded_length:
        # chunk of 4 bytes with zero padding
        chunk = data[i:i+4].ljust(4, b'\x00')

        # MSB first
        for byte in reversed(chunk):
            crc = crc_table[(crc >> 24) ^ byte] ^ ((crc << 8) & 0xFFFFFFFF)

        i += 4

    return crc & 0xFFFFFFFF


def format_time(path: Path) -> str:
    """Get file modification date"""
    try:
        mtime = path.stat().st_mtime
        dt = datetime.datetime.fromtimestamp(mtime)
        return dt.strftime("%H:%M")
    except Exception:
        return "??:??"


def main():
    if len(sys.argv) < 5 or (len(sys.argv) - 3) % 2 != 0:
        print(__doc__)
        print("Fehler: Ungültige Anzahl an Argumenten")
        sys.exit(1)

    init_crc_table()

    out_path = Path(sys.argv[1])
    device_id_str = sys.argv[2]

    print("firmware-tool (python)")
    print(f"Erstelle Firmware-Datei: {out_path}")
    print(f"Device: {device_id_str!r}")

    # pad device ID to 16 bytes
    device_id = device_id_str.encode('ascii').ljust(16, b'\0')[:16]

    # parse arguments: id + file pairs
    sections: List[Tuple[int, Path]] = []
    i = 3
    while i < len(sys.argv):
        try:
            section_id = int(sys.argv[i])
        except ValueError:
            print(f"Error: '{sys.argv[i]}' is not a valid section-ID (must be an integer)")
            sys.exit(1)

        file_path = Path(sys.argv[i + 1])
        if not file_path.is_file():
            print(f"Error: File not found or not a file: {file_path}")
            sys.exit(1)

        sections.append((section_id, file_path))
        i += 2

    # calc total size of data sections including headers
    total_size = 0
    for _, path in sections:
        size = path.stat().st_size
        total_size += 12 + size  # 4+4+4 bytes header per sektion

    # create output file
    with open(out_path, "wb") as fw:
        # write file header
        fw.write(device_id)
        fw.write(struct.pack("<I", 0))          # flags
        fw.write(struct.pack("<I", total_size)) # total size of sections

        # write sections
        for section_id, path in sections:
            data = path.read_bytes()
            size = len(data)

            # calc CRC
            crc = calc_crc32(data)

            # write section header
            fw.write(struct.pack("<I", section_id))   # ID
            fw.write(struct.pack("<I", crc))          # CRC
            fw.write(struct.pack("<I", size))         # size

            # write data
            fw.write(data)

            # output info
            print(f"section {section_id:3d}  "
                  f"size {size:6d} Bytes ({size//1024:4d}k)  "
                  f"crc {crc:08X}  "
                  f"time {format_time(path)}  "
                  f"→ {path.name}")

    print("\nFertig.")


if __name__ == "__main__":
    main()
