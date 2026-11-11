"""Compile portable core objects for provisional RV32IMC/ILP32; no BSP or ELF link."""
import argparse
from pathlib import Path
import shutil
import subprocess
import sys


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--toolchain-dir", type=Path, help="directorio bin del toolchain RISC-V de Vitis")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    output = root / "build/riscv"
    output.mkdir(parents=True, exist_ok=True)

    def tool(name: str) -> str:
        name = "riscv64-unknown-elf-" + name
        if args.toolchain_dir:
            return str(args.toolchain_dir / (name + (".exe" if sys.platform == "win32" else "")))
        found = shutil.which(name)
        if not found:
            raise FileNotFoundError("No se encontró " + name + "; usar --toolchain-dir")
        return found

    try:
        sources = ["vitis/app/control_core.c", "vitis/app/capture.c", "vitis/app/control_runtime.c",
                   "vitis/drivers/tdd_mmio.c", "vitis/drivers/tdd_timer.c"]
        objects = []
        with (output / "compilacion.txt").open("w", encoding="utf-8") as log:
            def run(command: list[str]) -> str:
                result = subprocess.run(command, cwd=root, stdout=subprocess.PIPE,
                                        stderr=subprocess.STDOUT, text=True, timeout=60)
                log.write("RUN: " + " ".join(command) + "\n" + result.stdout + "\n")
                if result.returncode:
                    raise RuntimeError(result.stdout)
                return result.stdout

            for source in sources:
                # Keep object paths relative: the bundled Windows driver forwards
                # paths with spaces incorrectly to its assembler.
                destination = Path("build/riscv") / (Path(source).stem + ".o")
                run([tool("gcc"), "-march=rv32imc", "-mabi=ilp32", "-std=c11", "-Wall", "-Wextra",
                     "-Werror", "-pedantic", "-O2", "-ffreestanding", "-fno-fast-math", "-c", source,
                     "-o", str(destination)])
                objects.append(str(destination))
            print(run([tool("size"), *objects]))
            undefined = run([tool("nm"), "-u", "build/riscv/capture.o"])
            if "__atomic" in undefined or "__sync" in undefined:
                raise RuntimeError("La captura requiere helpers atómicos; revisar su coste antes de integrar")
        print("PASS: objetos RV32IMC compilados; enlace BSP, memoria final y WCET pendientes.")
        return 0
    except (OSError, RuntimeError, subprocess.TimeoutExpired) as error:
        print(error, file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
