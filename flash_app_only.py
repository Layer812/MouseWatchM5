#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 Layer812
# StopWatch BLE Mouse R1: read live partition layout & boot selection; FLASH APP ONLY.
# Python standard library + pyserial (installed by PlatformIO) + esptool.py from PlatformIO.
from __future__ import annotations

import argparse
import hashlib
import os
from pathlib import Path
import re
import struct
import subprocess
import sys
import tempfile
import zlib
from dataclasses import dataclass

FLASH_SIZE = 16 * 1024 * 1024
PT_OFFSET = 0x8000
SECTOR = 0x1000

class FlashAbort(Exception):
    pass


def ensure(ok, reason):
    if not ok:
        raise FlashAbort(reason)


@dataclass(frozen=True)
class Part:
    label: str
    typ: int
    sub: int
    offset: int
    size: int
    flags: int


def parse_partition_table(blob: bytes) -> list[Part]:
    ensure(len(blob) == SECTOR, "Partition table read length is not 0x1000")
    result = []
    md5_seen = False
    terminator_seen = False
    for loc in range(0, 0xC00, 32):
        entry = blob[loc:loc + 32]
        if entry == bytes([0xFF]) * 32:
            terminator_seen = True
            break
        magic = struct.unpack_from('<H', entry)[0]
        if magic == 0xEBEB:
            ensure(entry[2:16] == bytes([0xFF])*14, "Unexpected partition MD5 record")
            checksum = hashlib.md5(blob[:loc]).digest()
            ensure(entry[16:32] == checksum, "Partition table MD5 mismatch")
            md5_seen = True
            terminator_seen = True
            break
        ensure(magic == 0x50AA, f"Invalid partition entry at +0x{loc:x}")
        _, typ, sub, offset, size, label, flags = struct.unpack('<HBBII16sI', entry)
        name = label.split(b'\x00', 1)[0].decode('ascii', errors='replace')
        ensure(name and size, "Empty partition label or zero size")
        ensure(offset % SECTOR == 0 and size % SECTOR == 0, f"Unaligned partition: {name}")
        ensure(offset >= PT_OFFSET + SECTOR and offset + size <= FLASH_SIZE,
               f"Partition outside 16MB flash or overlaps partition table: {name}")
        if typ == 0:
            ensure(offset % 0x10000 == 0, f"APP not aligned to 64KB: {name}")
        result.append(Part(name, typ, sub, offset, size, flags))
    ensure(terminator_seen and result, "No complete partition table")
    ensure(len({p.label for p in result}) == len(result), "Duplicate partition label")
    for i, a in enumerate(result):
        for b in result[i+1:]:
            ensure(a.offset + a.size <= b.offset or b.offset + b.size <= a.offset,
                   f"Overlapping partitions: {a.label}, {b.label}")
    return result


def resolve_boot_partition(parts: list[Part], ota_bytes: bytes | None) -> tuple[Part, str]:
    factories = [p for p in parts if p.typ == 0 and p.sub == 0]
    otas = sorted([p for p in parts if p.typ == 0 and 0x10 <= p.sub <= 0x1F], key=lambda p:p.sub)
    ensure(len(factories) < 2, "Multiple factory partitions")
    if not otas:
        ensure(len(factories) == 1, "No factory/OTA application partition")
        return factories[0], "factory only"
    ensure(all(p.sub == 0x10 + i for i,p in enumerate(otas)), "OTA partitions are not contiguous")
    ensure(ota_bytes is not None and len(ota_bytes) >= 0x2000, "otadata unavailable")
    records = []
    invalid_nonempty = []
    for k in range(2):
        raw = ota_bytes[k*SECTOR:k*SECTOR+32]
        seq, state, crc = struct.unpack_from('<I20xII', raw, 0)
        if raw == bytes([0xFF])*32:
            continue
        if seq == 0xFFFFFFFF or state in (3,4):  # INVALID, ABORTED
            continue
        check = zlib.crc32(struct.pack('<I',seq), 0xFFFFFFFF) & 0xffffffff
        if crc != check:
            invalid_nonempty.append(k)
            continue
        ensure(state in (2, 0xFFFFFFFF),
               "Selected OTA state is NEW/PENDING or unknown; next boot cannot be guaranteed")
        records.append((seq,k,state))
    ensure(not invalid_nonempty, f"Invalid non-empty OTA CRC in sector(s) {invalid_nonempty}")
    if not records:
        # Only an erased otadata state is considered a predictable default.
        ensure(ota_bytes[:32] == bytes([0xFF])*32 and
               ota_bytes[SECTOR:SECTOR+32] == bytes([0xFF])*32,
               "No valid OTA selection; refusing fallback guess")
        if factories:
            return factories[0], "erased otadata => factory"
        return otas[0], "erased otadata => ota_0"
    # For very large or wrapped seq differences, refuse ambiguous ordering.
    if len(records) == 2:
        diff = abs(records[0][0] - records[1][0])
        ensure(diff < 0x80000000, "OTA sequence rollover ordering is ambiguous")
        ensure(records[0][0] != records[1][0], "Conflicting equal OTA sequences")
    newest = max(records, key=lambda x:x[0])
    index = (newest[0] - 1) % len(otas)
    selected = otas[index]
    ensure(selected.sub == 0x10 + index, "OTA index mismatch")
    return selected, f"otadata[{newest[1]}] seq={newest[0]} => {selected.label}"


def validate_app_image(path: Path, selected: Part):
    ensure(not(selected.flags & 0x1), f"Selected APP {selected.label} is encrypted; unsupported")
    ensure(path.is_file(), f"Firmware file missing: {path}")
    size = path.stat().st_size
    ensure(40 < size <= selected.size, f"APP image size {size} exceeds/invalid for {selected.label} size 0x{selected.size:x}")
    ensure(selected.offset + ((size+SECTOR-1)//SECTOR)*SECTOR <= selected.offset+selected.size,
           "Erase range crosses APP partition")
    with path.open('rb') as fh:
        header = fh.read(32)
    ensure(len(header) >= 24 and header[0] == 0xE9, "Invalid ESP application image magic")
    ensure(1 <= header[1] <= 16, "Invalid ESP segment count")
    chip_id = struct.unpack_from('<H', header, 12)[0]
    ensure(chip_id == 0x0009, f"Wrong image chip_id=0x{chip_id:04x}; expected ESP32-S3 (0x0009)")
    return size, hashlib.sha256(path.read_bytes()).hexdigest()


def choose_port(explicit: str | None) -> str:
    if explicit:
        ensure(bool(re.fullmatch(r'COM\d+', explicit.upper())), "Port must be COM number e.g. COM16")
        return explicit.upper()
    try:
        from serial.tools import list_ports
        devices = list(list_ports.comports())
    except ImportError as err:
        raise FlashAbort(f"Cannot enumerate serial ports ({err}); provide COM port") from err
    esp_ports = [p.device for p in devices if p.vid == 0x303A and p.pid in (0x1001, 0x0002, 0x4001, 0x4002)]
    if len(esp_ports) == 1:
        return esp_ports[0]
    ports = ", ".join(p.device for p in devices) or "none"
    raise FlashAbort(f"Cannot uniquely identify StopWatch COM port (found {ports}). Run .cmd COM16 (replace COM16 as needed)")


def run_esptool(py: Path, esptool: Path, port: str, *args: str):
    cmd = [str(py), str(esptool), '--chip', 'esp32s3', '--port', port,
           '--baud', '460800', '--before', 'default_reset', '--after', 'no_reset', *args]
    print('> ', ' '.join(cmd), flush=True)
    subprocess.run(cmd, check=True)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--port', default=None)
    parser.add_argument('--esptool', required=True, type=Path)
    parser.add_argument('--firmware', required=True, type=Path)
    parser.add_argument('--python', default=sys.executable, type=Path)
    args = parser.parse_args()
    ensure(args.esptool.exists(), f"PlatformIO esptool missing: {args.esptool}")
    port = choose_port(args.port)
    print(f"StopWatch BLE Mouse -- APP-ONLY COM={port}", flush=True)
    with tempfile.TemporaryDirectory(prefix='stopwatch_app_only_') as tmp:
        table_path = Path(tmp)/'partition_readonly.bin'
        run_esptool(args.python, args.esptool, port, 'read_flash', hex(PT_OFFSET), hex(SECTOR), str(table_path))
        parts = parse_partition_table(table_path.read_bytes())
        for p in parts:
            print(f"  {p.label:<14} typ=0x{p.typ:02x} sub=0x{p.sub:02x} 0x{p.offset:06x} +0x{p.size:x}")
        otaparts = [p for p in parts if p.typ == 1 and p.sub == 0]
        ensure(len(otaparts) <= 1, "Multiple otadata partitions")
        ota = None
        if otaparts:
            ensure(otaparts[0].size >= 0x2000, "otadata too small")
            ota_path = Path(tmp)/'otadata_readonly.bin'
            run_esptool(args.python, args.esptool, port, 'read_flash',
                        hex(otaparts[0].offset), hex(0x2000), str(ota_path))
            ota = ota_path.read_bytes()
        target, explanation = resolve_boot_partition(parts, ota)
        size, sha = validate_app_image(args.firmware, target)
        # Confirm a plausible image already occupies boot selection; avoids fallback mismatch.
        active_header_path = Path(tmp)/'active_header_readonly.bin'
        run_esptool(args.python, args.esptool, port, 'read_flash',
                    hex(target.offset), hex(SECTOR), str(active_header_path))
        old = active_header_path.read_bytes()
        ensure(len(old) == SECTOR and old[0] == 0xE9,
               "Selected APP slot is not bootable-looking; fallback is possible; no flash")
        print(f"PASS target={target.label} offset=0x{target.offset:06x} limit=0x{target.size:x}")
        print(f"PASS boot selection: {explanation}")
        print(f"PASS image bytes={size} sha256={sha}")
        print("FLASH: write exactly ONE selected APP image. No bootloader/partition/NVS/otadata/storage/erase_flash.",flush=True)
        cmd = [str(args.python), str(args.esptool), '--chip', 'esp32s3', '--port', port,
               '--baud', '460800', '--before', 'default_reset', '--after', 'hard_reset',
               'write_flash', hex(target.offset), str(args.firmware)]
        print('> ', ' '.join(cmd), flush=True)
        subprocess.run(cmd, check=True)
        print(f"PASS: APP-ONLY flash completed to {target.label} at 0x{target.offset:06x}",flush=True)


if __name__ == '__main__':
    try:
        main()
    except (FlashAbort, subprocess.CalledProcessError, OSError) as exc:
        print(f"\nABORT: {exc}", file=sys.stderr, flush=True)
        sys.exit(1)
