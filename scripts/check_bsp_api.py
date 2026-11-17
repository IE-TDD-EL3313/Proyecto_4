"""Compile the board entry point against installed AMD headers, without a BSP link.

Generated configuration headers below are explicit API-test fixtures. They do
not replace an SDT BSP or authorize programming a board.
"""
import argparse
from pathlib import Path
import subprocess


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--vitis", type=Path, default=Path("C:/AMDDesignTools/2026.1/Vitis"))
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    output = root / "build/bsp_api_check"
    output.mkdir(parents=True, exist_ok=True)
    fixtures = {
        "bspconfig.h": "#define XPAR_CPU_ID 0\n",
        "xparameters.h": "#define XPAR_MICROBLAZE_RISCV_ADDR_SIZE 32\n",
        "xintc_drv_config.h": "#define XPAR_INTC_MAX_NUM_INTR_INPUTS 2\n#define XPAR_INTC_NUM_INSTANCES 1\n",
    }
    for name, content in fixtures.items():
        (output / name).write_text("/* API-test fixture, not an exported BSP. */\n" + content)
    embedded = args.vitis / "data/embeddedsw"
    includes = [Path("build/bsp_api_check"), embedded / "lib/bsp/standalone_v9_5/src/common",
                embedded / "lib/bsp/standalone_v9_5/src/riscv",
                embedded / "XilinxProcessorIPLib/drivers/intc_v3_22/src"]
    compiler = args.vitis / "gnu/riscv/nt/bin/riscv64-unknown-elf-gcc.exe"
    command = [str(compiler), "-march=rv32imc", "-mabi=ilp32", "-std=c11", "-Wall",
               "-Wextra", "-Werror", "-O2", "-DSDT",
               *["-I" + str(path) for path in includes], "-c", "vitis/bsp/main.c",
               "-o", "build/bsp_api_check/main.o"]
    result = subprocess.run(command, cwd=root, capture_output=True, text=True, timeout=60)
    (output / "compile.log").write_text(str(command) + "\n" + result.stdout + result.stderr)
    print(result.stdout + result.stderr, end="")
    if not result.returncode:
        print("PASS: board entry point API check with AMD headers; SDT BSP/ELF link and execution pending.")
    return result.returncode


if __name__ == "__main__":
    raise SystemExit(main())
