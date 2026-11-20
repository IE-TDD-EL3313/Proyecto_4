"""Receive acquisition-only frames and retain original UART bytes for review.

Offline --input supports package verification without a board. Only --port
needs pyserial. Passing frame checks does not measure ADC accuracy or WCET.
"""
import argparse
from datetime import datetime
import json
from pathlib import Path
import statistics
import time
from capture_receiver import FrameParser, save_capture


PERIODS = {"10": 10000, "15": 6666, "20": 5000}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    source = parser.add_mutually_exclusive_group(required=True)
    source.add_argument("--port")
    source.add_argument("--input", type=Path)
    parser.add_argument("--frequency", choices=PERIODS, default="20")
    parser.add_argument("--count", type=int, default=2)
    parser.add_argument("--max-wait", type=float, default=120.)
    parser.add_argument("--output-dir", type=Path, required=True)
    args = parser.parse_args()
    if args.count < 1 or not 0 < args.max_wait <= 86400:
        parser.error("count debe ser positivo y max-wait debe estar entre 0 y 86400 segundos")
    folder = args.output_dir / (datetime.now().strftime("%Y%m%d_%H%M%S") + "_" + args.frequency + "khz")
    original = folder
    suffix = 1
    while folder.exists():
        folder = original.with_name(original.name + f"_{suffix}");suffix += 1
    folder.mkdir(parents=True)
    frames = FrameParser()
    results = []
    error = None

    def consume(chunk):
        for capture in frames.feed(chunk):
            csv_path = save_capture(capture, folder)
            issues = []
            if capture.capture_kind != 2:issues.append("tipo distinto de RAW de verificacion")
            if len(capture.records) != 2048:issues.append("no contiene el banco completo de 2048 muestras")
            if capture.period_ticks != PERIODS[args.frequency]:issues.append("periodo distinto del ELF seleccionado")
            if capture.flags:issues.append("flags de captura corta o falla presentes")
            raw = [r[0] for r in capture.records]
            results.append({"capture_id": capture.capture_id, "csv": csv_path.name,
                "samples": len(raw), "period_ticks": capture.period_ticks, "fs_hz": capture.fs_hz,
                "first_seq": capture.first_seq, "first_ticks": capture.first_ticks,
                "duration_s": capture.duration_s, "flags": capture.flags,
                "raw_min": min(raw), "raw_max": max(raw), "raw_mean": statistics.mean(raw),
                "raw_stddev": statistics.pstdev(raw), "frame_checks_passed": not issues, "issues": issues})
            print(f"Captura {len(results)}/{args.count}: {len(raw)} muestras, "
                  f"periodo={capture.period_ticks}, flags={capture.flags}, archivo={csv_path.name}", flush=True)
            if len(results) < args.count and args.port:
                print("Para otra captura: soltar y presionar BTNC despues de terminar el envio.", flush=True)

    try:
        with (folder / "uart_original.bin").open("xb") as raw_stream:
            if args.input:
                with args.input.open("rb") as stream:
                    while chunk := stream.read(4096):
                        raw_stream.write(chunk);consume(chunk)
                frames.finish()
            else:
                try:
                    import serial
                except ImportError as exception:
                    raise ValueError("Falta pyserial: python -m pip install pyserial") from exception
                started = time.monotonic()
                with serial.Serial(args.port, baudrate=115200, bytesize=8, parity="N", stopbits=1,
                                   timeout=.1) as stream:
                    while len(results) < args.count and time.monotonic() - started < args.max_wait:
                        chunk = stream.read(1024);raw_stream.write(chunk);consume(chunk)
                if len(results) < args.count:
                    raise TimeoutError("No se completaron las capturas antes de max-wait")
    except (OSError, ValueError) as exception:
        error = str(exception)
    except KeyboardInterrupt:
        error = "recepcion interrumpida por el operador"
    complete = len(results) >= args.count
    passed = complete and not error and not frames.rejected and all(r["frame_checks_passed"] for r in results)
    report = {"source": "serial" if args.port else "file_replay", "port": args.port,
        "frequency_code": {"10": 0, "15": 1, "20": 2}[args.frequency],
        "expected_period_ticks": PERIODS[args.frequency], "requested_captures": args.count,
        "complete": complete, "frame_checks_passed": passed, "rejected_candidates": frames.rejected,
        "error": error, "captures": results,
        "physical_signal_accuracy_verified": False, "isr_wcet_verified": False,
        "operator_measurements_required": True}
    (folder / "informe_recepcion.json").write_text(json.dumps(report, indent=2, ensure_ascii=False), encoding="utf-8")
    print("Resultados:", folder, flush=True)
    if not passed:print("Revisar informe_recepcion.json:", error or "criterios de trama no cumplidos", flush=True)
    return 0 if passed else 1


if __name__ == "__main__":
    raise SystemExit(main())
