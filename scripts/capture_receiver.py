"""Decode bounded TDD4 v1 captures from a binary file or an optional serial port."""
from __future__ import annotations

import argparse
import csv
from dataclasses import asdict, dataclass
import json
import math
from pathlib import Path
import struct
import sys
import time
import zlib

MAGIC = b"TDD4"
HEADER = struct.Struct("<4sBBHIIIIIffHHII")
RECORD = struct.Struct("<Hh")
MAX_SAMPLES = 2048
MAX_FRAME = 48 + 4 * MAX_SAMPLES + 4


@dataclass(frozen=True)
class Capture:
    capture_id: int
    fs_hz: int
    first_seq: int
    first_ticks: int
    offset_code: float
    gain_a_per_code: float
    flags: int
    period_ticks: int
    records: tuple[tuple[int, int], ...]

    @property
    def duration_s(self) -> float:
        return (len(self.records) - 1) * self.period_ticks / 100_000_000


def inspect_header(data: bytes | bytearray) -> tuple:
    fields = HEADER.unpack_from(data)
    magic, version, kind, header_bytes, _, n, fs, _, _, offset, gain, size, flags, period, payload = fields
    if (magic, version, kind, header_bytes, size) != (MAGIC, 1, 1, 48, 4):
        raise ValueError("cabecera o versión no compatible")
    if not 1 <= n <= MAX_SAMPLES or payload != n * 4:
        raise ValueError("longitud de captura inválida")
    if fs <= 0 or period <= 0 or fs * period != 100_000_000:
        raise ValueError("frecuencia y período de ticks incompatibles")
    if not math.isfinite(offset) or not math.isfinite(gain) or gain == 0:
        raise ValueError("calibración inválida")
    if flags & ~7 or bool(flags & 1) != ((n - 1) * period < 10_000_000):
        raise ValueError("flags incompatibles con la duración")
    return fields


class FrameParser:
    """Keep at most one maximum frame plus a small input chunk; resync after CRC errors.

    Call expire() while a live stream is idle. A timeout drops the incomplete
    candidate, so a partial packet cannot keep a later capture blocked forever.
    """

    def __init__(self, timeout_s: float = 2.0):
        if not math.isfinite(timeout_s) or timeout_s <= 0:
            raise ValueError("timeout inválido")
        self.timeout_s = timeout_s
        self.buffer = bytearray()
        self.started: float | None = None
        self.rejected = 0

    def _parse(self, now: float) -> list[Capture]:
        found = []
        while True:
            start = self.buffer.find(MAGIC)
            if start < 0:
                self.buffer[:] = self.buffer[-3:]
                self.started = None
                break
            if start:
                del self.buffer[:start]
                self.started = None
            if self.started is None:
                self.started = now
            if len(self.buffer) < HEADER.size:
                break
            try:
                fields = inspect_header(self.buffer)
            except ValueError:
                self.rejected += 1
                del self.buffer[0]
                self.started = None
                continue
            _, _, _, _, capture_id, n, fs, seq, ticks, offset, gain, _, flags, period, payload = fields
            total = HEADER.size + payload + 4
            if len(self.buffer) < total:
                break
            crc = struct.unpack_from("<I", self.buffer, total - 4)[0]
            records = tuple(RECORD.iter_unpack(self.buffer[48:total - 4]))
            if zlib.crc32(self.buffer[:total - 4]) != crc or any(raw > 4095 for raw, _ in records):
                self.rejected += 1
                del self.buffer[0]
                self.started = None
                continue
            found.append(Capture(capture_id, fs, seq, ticks, offset, gain, flags, period, records))
            del self.buffer[:total]
            self.started = None
        return found

    def feed(self, data: bytes, now: float | None = None) -> list[Capture]:
        now = time.monotonic() if now is None else now
        result = self.expire(now)
        for start in range(0, len(data), 1024):
            self.buffer.extend(data[start:start + 1024])
            result.extend(self._parse(now))
            assert len(self.buffer) <= MAX_FRAME + 1024
        return result

    def expire(self, now: float | None = None) -> list[Capture]:
        now = time.monotonic() if now is None else now
        if self.started is not None and now - self.started >= self.timeout_s:
            self.rejected += 1
            del self.buffer[0]
            self.started = None
            return self._parse(now)
        return []

    def finish(self) -> None:
        if self.started is not None:
            raise ValueError("captura truncada al terminar el archivo")


def save_capture(capture: Capture, output_dir: Path) -> Path:
    output_dir.mkdir(parents=True, exist_ok=True)
    base = output_dir / f"captura_{capture.capture_id:08d}"
    # Never overwrite an earlier measurement with a reused firmware capture ID.
    suffix = 1
    while base.with_suffix(".csv").exists() or base.with_suffix(".json").exists():
        base = output_dir / f"captura_{capture.capture_id:08d}_{suffix}"
        suffix += 1
    with base.with_suffix(".csv").open("x", newline="", encoding="utf-8") as stream:
        writer = csv.writer(stream)
        writer.writerow(["capture_id", "sample_index", "seq", "time_s", "raw_adc", "i_meas_A", "i_ref_A"])
        for index, (raw, reference_ma) in enumerate(capture.records):
            writer.writerow([capture.capture_id, index, (capture.first_seq + index) & 0xFFFFFFFF,
                             index * capture.period_ticks / 100_000_000, raw,
                             (raw - capture.offset_code) * capture.gain_a_per_code, reference_ma / 1000])
    metadata = asdict(capture)
    del metadata["records"]
    metadata.update(sample_count=len(capture.records), duration_s=capture.duration_s,
                    covers_five_50hz_periods=not bool(capture.flags & 1))
    with base.with_suffix(".json").open("x", encoding="utf-8") as stream:
        json.dump(metadata, stream, ensure_ascii=False, indent=2, allow_nan=False)
    return base.with_suffix(".csv")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    source = parser.add_mutually_exclusive_group(required=True)
    source.add_argument("--input", type=Path, help="archivo binario capturado")
    source.add_argument("--port", help="puerto serie, por ejemplo COM5; requiere pyserial")
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--count", type=int, default=1, help="capturas a recibir en modo serie")
    args = parser.parse_args()
    frames = FrameParser()
    received = 0

    def consume(captures: list[Capture]) -> None:
        nonlocal received
        for capture in captures:
            print(save_capture(capture, args.output_dir))
            received += 1

    try:
        if args.input:
            with args.input.open("rb") as stream:
                while chunk := stream.read(4096):
                    consume(frames.feed(chunk))
            frames.finish()
        else:
            if args.count < 1:
                parser.error("--count debe ser positivo")
            try:
                import serial
            except ImportError:
                parser.error("el modo serie requiere pyserial; el modo --input usa solo la biblioteca estándar")
            with serial.Serial(args.port, baudrate=115200, bytesize=8, parity="N", stopbits=1,
                               timeout=0.1) as stream:
                while received < args.count:
                    consume(frames.feed(stream.read(1024)))
        if not received:
            raise ValueError("no se encontró ninguna captura válida")
        if frames.rejected:
            print(f"Tramas rechazadas: {frames.rejected}", file=sys.stderr)
        return 0
    except (OSError, ValueError) as error:
        print(f"Error: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
