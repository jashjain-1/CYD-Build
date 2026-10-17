#!/usr/bin/env python3
"""Inspect an existing ESP32 ELF without a toolchain or third-party packages.

Static section sizes are not runtime heap measurements. The hash identifies the
artifact; this tool does not assert that an old artifact matches current sources.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import struct
from pathlib import Path


def inspect_elf(path: Path) -> dict:
    data = path.read_bytes()
    if data[:6] != b"\x7fELF\x01\x01":
        raise ValueError("Expected a little-endian ELF32 image")
    header = struct.unpack_from("<16sHHIIIIIHHHHHH", data)
    if header[2] != 94:
        raise ValueError("Expected an Xtensa ELF (ESP32)")
    offset, entry_size, count, names_index = header[6], header[11], header[12], header[13]
    if entry_size != 40 or not count or names_index >= count:
        raise ValueError("Unsupported ELF section table")
    sections = [struct.unpack_from("<IIIIIIIIII", data, offset + i * entry_size) for i in range(count)]

    def region(section):
        start, length = section[4], section[5]
        if start + length > len(data):
            raise ValueError("Truncated ELF section")
        return data[start:start + length]

    def string(table, start):
        if start >= len(table):
            return "<invalid>"
        return table[start:].split(b"\0", 1)[0].decode("utf-8", errors="replace")

    names = region(sections[names_index])
    section_names = [string(names, s[0]) for s in sections]
    allocated = [
        {"name": name, "address": hex(s[3]), "bytes": s[5]}
        for name, s in zip(section_names, sections) if s[2] & 2 and s[5]
    ]
    objects = []
    for s in sections:
        if s[1] != 2:  # SHT_SYMTAB
            continue
        strings = region(sections[s[6]])
        symbols = region(s)
        if s[9] != 16:
            raise ValueError("Unsupported symbol size")
        for start in range(0, len(symbols), 16):
            name, address, size, info, _, section = struct.unpack_from("<IIIBBH", symbols, start)
            if (info & 15) == 1 and size and section < count:
                target = sections[section]
                # ESP32 linker scripts can mark flash.rodata writable in ELF;
                # section placement, not just ELF flags, determines SRAM use.
                if section_names[section] in (".dram0.data", ".dram0.bss", ".iram0.data", ".iram0.bss", ".rtc.data", ".rtc_noinit"):
                    objects.append({"name": string(strings, name), "bytes": size,
                                    "section": section_names[section], "address": hex(address)})
    return {
        "artifact": str(path.resolve()),
        "sha256": hashlib.sha256(data).hexdigest(),
        "scope": "existing artifact only; not a fresh build or runtime measurement",
        "dram_static_bytes": sum(s[5] for name, s in zip(section_names, sections)
                                 if name in (".dram0.data", ".dram0.bss")),
        "allocated_sections": allocated,
        "largest_static_ram_objects": sorted(objects, key=lambda obj: obj["bytes"], reverse=True)[:25],
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("elf", type=Path)
    parser.add_argument("--binary", type=Path)
    parser.add_argument("--partition-bytes", type=int, default=0x300000)
    parser.add_argument("--out", type=Path)
    args = parser.parse_args()
    if args.partition_bytes <= 0:
        parser.error("--partition-bytes must be positive")
    report = inspect_elf(args.elf)
    if args.binary:
        binary = args.binary.read_bytes()
        report["flash"] = {
            "binary": str(args.binary.resolve()), "sha256": hashlib.sha256(binary).hexdigest(),
            "binary_bytes": len(binary), "partition_bytes": args.partition_bytes,
            "headroom_percent": round(100 * (1 - len(binary) / args.partition_bytes), 2),
            "at_least_25_percent_headroom": len(binary) <= args.partition_bytes * 0.75,
        }
    rendered = json.dumps(report, indent=2)
    if args.out:
        args.out.parent.mkdir(parents=True, exist_ok=True)
        args.out.write_text(rendered + "\n", encoding="utf-8")
    print(rendered)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
