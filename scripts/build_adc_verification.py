"""Run with vitis -s scripts/build_adc_verification.py.

Build three acquisition-only applications against the integrated 128 KiB XSA.
No controller calibration/PR flags are enabled and no power gates are requested.
"""
from pathlib import Path
import hashlib
import json
import os
import shutil
import struct
import subprocess
import tempfile
import vitis
import sys
sys.path.insert(0, str(Path(__file__).resolve().parent))
from audit_adc_memory import audit_elf


def main():
    root = Path(__file__).resolve().parents[1]
    output = root / "build/adc_verification/firmware"
    output.mkdir(parents=True, exist_ok=True)
    handoff = root / "build/nexys4_integrated/nexys4_control_hw.xsa"
    scratch = Path(tempfile.mkdtemp(prefix="tdd4_adc_fw_"))
    xsa = scratch / "control.xsa"
    shutil.copy2(handoff, xsa)
    workspace = scratch / "workspace"
    (output / "workspace.txt").write_text(str(workspace), encoding="utf-8")
    client = vitis.create_client()
    products = []
    try:
        client.set_workspace(str(workspace))
        platform = client.create_platform_component(name="adc_platform", hw_design=str(xsa),
            os="standalone", cpu="microblaze_riscv_0", domain_name="adc")
        platform.build()
        xpfm = client.find_platform_in_repos("adc_platform")
        for frequency, label in enumerate(("10khz", "15khz_nominal", "20khz")):
            name = "adc_verify_" + label
            app = client.create_app_component(name=name, platform=xpfm, domain="adc", template="empty_application")
            app.import_files(from_loc=str(root / "vitis/app"), dest_dir_in_cmp="src/app",
                files=["adc_verify.c", "adc_verify.h", "capture.c", "capture.h", "timebase.h", "control_core.h"])
            app.import_files(from_loc=str(root / "vitis/drivers"), dest_dir_in_cmp="src/drivers")
            app.import_files(from_loc=str(root / "vitis/bsp"), files=["board_config.h"], dest_dir_in_cmp="src/bsp")
            app.import_files(from_loc=str(root / "vitis/bsp"), files=["adc_verify_main.c"], dest_dir_in_cmp="src/bsp")
            app.set_app_config(key="USER_COMPILE_OPTIMIZATION_LEVEL", values="-Os")
            app.append_app_config(key="USER_COMPILE_DEFINITIONS", values=f"TDD_ADC_VERIFY_FREQ_CODE={frequency}")
            app.append_app_config(key="USER_LINK_LIBRARIES", values="m")
            app.set_app_config(key="USER_LINK_OTHER_FLAGS", values=f"-Wl,-Map={name}.map")
            ld = app.get_ld_script();ld.set_stack_size("0x2000");ld.set_heap_size("0x1000")
            app.build()
            elf, = (workspace / name).rglob("*.elf")
            map_file, = (workspace / name).rglob("*.map")
            address_audit = audit_elf(xsa, elf)
            (output / (name + "_address_audit.json")).write_text(json.dumps(address_audit, indent=2), encoding="utf-8")
            data = elf.read_bytes()
            if data[:6] != b"\x7fELF\x01\x01":
                raise RuntimeError("Expected ELF32 little endian")
            start = struct.unpack_from("<I", data, 32)[0]
            stride, count = struct.unpack_from("<HH", data, 46)
            sections = [struct.unpack_from("<10I", data, start + i * stride) for i in range(count)]
            high = max(s[3] + s[5] for s in sections if s[2] & 2 and s[5])
            if high > 131072:
                raise RuntimeError("Application exceeds shared local memory")
            shutil.copy2(elf, output / (name + ".elf"))
            shutil.copy2(map_file, output / (name + ".map"))
            size = Path(os.environ["XILINX_VITIS"]) / "gnu/riscv/nt/bin/riscv64-unknown-elf-size.exe"
            report = subprocess.check_output([str(size), "-A", str(elf)], text=True, encoding="ascii")
            (output / (name + "_sections.txt")).write_text(report, encoding="utf-8")
            products.append({"elf": name + ".elf", "frequency_code": frequency,
                "period_ticks": (10000, 6666, 5000)[frequency], "memory_end": high,
                "free_above_sections": 131072 - high, "gate_release": False})
        (output / "summary.json").write_text(json.dumps({"products": products,
            "xsa_sha256": hashlib.sha256(handoff.read_bytes()).hexdigest(),
            "workspace": str(workspace), "executed_on_fpga": False}, indent=2), encoding="utf-8")
        print("PASS: three acquisition-only ELF applications linked; physical verification remains required")
    finally:
        vitis.dispose()


if __name__ == "__main__":
    main()
