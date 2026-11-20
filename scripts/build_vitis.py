"""Build the Standalone SDT BSP and diagnostic ELF from the integrated XSA.

Run with AMD Vitis: vitis -s scripts/build_vitis.py
Generated workspaces use a short temporary path; sources in the repository
and the teammate's base Vivado project are never modified by this script.
"""
from pathlib import Path
import json
import os
import shutil
import subprocess
import struct
import sys
sys.path.insert(0, str(Path(__file__).resolve().parent))
from audit_adc_memory import audit_elf
import tempfile

import vitis


def main():
    root = Path(__file__).resolve().parents[1]
    output = root / "build/nexys4_integrated"
    xsa = output / "nexys4_control_hw.xsa"
    if not xsa.is_file():
        raise FileNotFoundError("Run scripts/build_nexys4.tcl first: integrated XSA required")
    scratch = Path(tempfile.mkdtemp(prefix="tdd4_vitis_"))
    workspace = scratch / "workspace"
    (output / "vitis_workspace.txt").write_text(str(workspace), encoding="utf-8")
    handoff = scratch / "control.xsa"
    shutil.copy2(xsa, handoff)
    client = vitis.create_client()
    try:
        client.set_workspace(str(workspace))
        platform = client.create_platform_component(
            name="nexys4_control", hw_design=str(handoff), os="standalone",
            cpu="microblaze_riscv_0", domain_name="control")
        platform.build()
        xpfm = client.find_platform_in_repos("nexys4_control")
        app = client.create_app_component(name="control_app", platform=xpfm,
                                         domain="control", template="empty_application")
        for folder in ("app", "drivers"):
            app.import_files(from_loc=str(root / "vitis" / folder),
                             dest_dir_in_cmp="src/" + folder)
        # Use the linker generated from this XSA, not an imported source linker.
        app.import_files(from_loc=str(root / "vitis/bsp"),
                         files=["main.c", "board_config.h"], dest_dir_in_cmp="src/bsp")
        app.set_app_config(key="USER_COMPILE_OPTIMIZATION_LEVEL", values="-Os")
        app.append_app_config(key="USER_LINK_LIBRARIES", values="m")
        app.set_app_config(key="USER_LINK_OTHER_FLAGS", values="-Wl,-Map=control_app.map")
        linker = app.get_ld_script()
        linker.set_stack_size("0x2000")
        linker.set_heap_size("0x1000")
        app.build()
        elf_files = list((workspace / "control_app").rglob("*.elf"))
        if len(elf_files) != 1:
            raise RuntimeError("Expected one linked ELF, found: " + str(elf_files))
        elf = output / "nexys4_control_diagnostic.elf"
        shutil.copy2(elf_files[0], elf)
        maps = list((workspace / "control_app").rglob("*.map"))
        if len(maps) != 1:
            raise RuntimeError("Expected one linker map: " + str(maps))
        shutil.copy2(maps[0], output / "nexys4_control_diagnostic.map")
        tool = Path(os.environ["XILINX_VITIS"]) / "gnu/riscv/nt/bin/riscv64-unknown-elf-size.exe"
        if tool.is_file():
            report = subprocess.check_output([str(tool), "-A", str(elf_files[0])], text=True, encoding="ascii")
            (output / "elf_sections.txt").write_text(report, encoding="utf-8")
            print(report)
        # ELF32 sections with SHF_ALLOC occupy FPGA RAM; debug sections do not.
        address_audit = audit_elf(handoff, elf)
        (output / "elf_address_audit.json").write_text(json.dumps(address_audit, indent=2), encoding="utf-8")
        data = elf.read_bytes()
        if data[:6] != b"\x7fELF\x01\x01" or struct.unpack_from("<H", data, 18)[0] != 243:
            raise RuntimeError("Expected little-endian ELF32 RISC-V")
        shoff = struct.unpack_from("<I", data, 32)[0]
        stride, count = struct.unpack_from("<HH", data, 46)
        allocated = []
        for index in range(count):
            _, _, flags, address, _, size, *_ = struct.unpack_from("<10I", data, shoff + index * stride)
            if flags & 2 and size:
                if address + size > 128 * 1024:
                    raise RuntimeError("Allocated section exceeds the shared 128 KiB LMB")
                allocated.append((address, size))
        high_water = max(address + size for address, size in allocated)
        memory = {"lmb_bytes": 128 * 1024, "allocated_section_bytes": sum(s for _, s in allocated),
                  "highest_allocated_end": high_water, "free_above_sections": 128 * 1024 - high_water,
                  "record_bank_bytes": 2 * 2048 * 12, "requested_stack_bytes": 8192,
                  "requested_heap_bytes": 4096, "execution_on_fpga": False}
        (output / "memory_usage.json").write_text(json.dumps(memory, indent=2), encoding="utf-8")
        print("MEMORY:", json.dumps(memory))
        (output / "vitis_build.json").write_text(json.dumps({
            "workspace": str(workspace), "xsa": str(xsa), "elf": str(elf),
            "mode": "diagnostic; board calibration/PR/protection/timing confirmations remain disabled",
        }, indent=2), encoding="utf-8")
        print("PASS: real SDT BSP and linked diagnostic ELF; not executed on FPGA")
    finally:
        vitis.dispose()


if __name__ == "__main__":
    main()
