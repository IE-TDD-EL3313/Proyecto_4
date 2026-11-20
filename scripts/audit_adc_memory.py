"""Check real XSA data/instruction routes against allocated ELF sections.

An address in the linker script is not sufficient: writable arrays must be
reachable through a processor data memory interface, not only M_AXI_IC.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import xml.etree.ElementTree as ET
import zipfile


def memory_ranges(xsa):
    with zipfile.ZipFile(xsa) as archive:
        root = ET.fromstring(archive.read("system_bd.hwh"))
    return [dict(n.attrib, base=int(n.get("BASEVALUE"), 0), high=int(n.get("HIGHVALUE"), 0))
            for n in root.findall(".//MEMRANGE")]


def data_accessible(ranges, address, size):
    return any(r["MEMTYPE"] == "MEMORY" and r["IS_DATA"] == "TRUE" and
               r["base"] <= address and address + size - 1 <= r["high"] for r in ranges)


def audit_elf(xsa, elf):
    ranges = memory_ranges(xsa)
    data = Path(elf).read_bytes()
    if data[:6] != b"\x7fELF\x01\x01" or struct.unpack_from("<H", data, 18)[0] != 243:
        raise ValueError("Expected little-endian ELF32 RISC-V")
    start = struct.unpack_from("<I", data, 32)[0]
    stride, count, names_index = struct.unpack_from("<HHH", data, 46)
    sections = [struct.unpack_from("<10I", data, start + i * stride) for i in range(count)]
    strings = sections[names_index]
    names = data[strings[4]:strings[4] + strings[5]]
    checked = []
    for section in sections:
        name_offset, _, flags, address, _, size, *_ = section
        if not (flags & 2) or not size:
            continue
        name = names[name_offset:].split(b"\0", 1)[0].decode("ascii")
        # Executable sections require an instruction route. Constants and all
        # writable/NOLOAD sections require an actual data RAM route.
        route = "IS_INSTRUCTION" if flags & 4 else "IS_DATA"
        valid = any(r["MEMTYPE"] == "MEMORY" and r[route] == "TRUE" and
                    r["base"] <= address and address + size - 1 <= r["high"] for r in ranges)
        if not valid:
            raise ValueError(f"{name} 0x{address:08x}+{size} has no {route} memory route in XSA")
        checked.append({"section": name, "base": address, "size": size, "route": route})
    return {"passed": True, "elf": Path(elf).name, "elf_sha256": hashlib.sha256(data).hexdigest(), "sections": checked,
            "data_ram": [r for r in ranges if r["MEMTYPE"] == "MEMORY" and r["IS_DATA"] == "TRUE"]}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--xsa", type=Path, required=True)
    parser.add_argument("--elf", type=Path, action="append", default=[])
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    ranges = memory_ranges(args.xsa)
    result = {"memory_ranges": ranges,
              "axi_bram_0x10000000_data_accessible": data_accessible(ranges, 0x10000000, 32768),
              "applications": [audit_elf(args.xsa, elf) for elf in args.elf]}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2), encoding="utf-8")
    print("PASS: allocated ELF sections have processor memory routes")
    print("AXI BRAM at 0x10000000 supports data access:", result["axi_bram_0x10000000_data_accessible"])


if __name__ == "__main__":
    main()
