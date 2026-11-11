"""Run RTL, portable firmware and C-to-Python protocol tests (no FPGA required)."""
import argparse
from pathlib import Path
import subprocess
import sys

if hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", default="gcc")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    build = root / "build/host"
    build.mkdir(parents=True, exist_ok=True)
    commands = [
        ["iverilog", "-g2012", "-Wall", "-s", "tb_xadc_axi_lite", "-o", "build/host/xadc.vvp",
         "rtl/xadc_if/xadc_acquisition.sv", "rtl/xadc_if/xadc_axi_lite.sv", "tb/xadc_if/tb_xadc_axi_lite.sv"],
        ["vvp", "build/host/xadc.vvp"],
        [args.compiler, "-std=c11", "-Wall", "-Wextra", "-Werror", "-pedantic", "-O2",
         "vitis/app/control_core.c", "vitis/app/capture.c", "vitis/drivers/tdd_mmio.c",
         "tb/firmware/test_firmware.c", "-lm", "-o", "build/host/test_firmware.exe"],
        [str(build / "test_firmware.exe"), "build/host/captura_sintetica.bin"],
        [args.compiler, "-std=c11", "-Wall", "-Wextra", "-Werror", "-pedantic", "-O2",
         "vitis/app/control_core.c", "vitis/app/capture.c", "vitis/app/control_runtime.c",
         "vitis/drivers/tdd_mmio.c", "vitis/drivers/tdd_timer.c", "tb/firmware/test_runtime.c",
         "-lm", "-o", "build/host/test_runtime.exe"],
        [str(build / "test_runtime.exe")],
        [sys.executable, "-m", "unittest", "discover", "-s", "tb/firmware", "-p", "test_*.py", "-v"],
        [sys.executable, "scripts/capture_receiver.py", "--input", "build/host/captura_sintetica.bin",
         "--output-dir", "build/host/csv_sintetico"],
    ]
    with (build / "verificacion.txt").open("w", encoding="utf-8") as log:
        for command in commands:
            print("RUN:", " ".join(command), flush=True)
            try:
                result = subprocess.run(command, cwd=root, stdout=subprocess.PIPE,
                                        stderr=subprocess.STDOUT, text=True, timeout=60)
            except (OSError, subprocess.TimeoutExpired) as error:
                print(error, file=sys.stderr)
                return 1
            print(result.stdout, end="", flush=True)
            log.write("RUN: " + " ".join(command) + "\n" + result.stdout + "\n")
            if result.returncode:
                return result.returncode
    print("PASS: verificación local completa. Datos generados: build/host/ (sintéticos).")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
