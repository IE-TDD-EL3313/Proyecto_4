import csv
import json
from pathlib import Path
import random
import struct
import sys
import tempfile
import unittest
import zlib

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))
from capture_receiver import FrameParser, HEADER, MAX_FRAME, save_capture


def frame(n=2048, fs=20000, capture_id=1, records=None):
    period = 100_000_000 // fs
    flags = int((n - 1) * period < 10_000_000)
    header = HEADER.pack(b"TDD4", 1, 1, 48, capture_id, n, fs, 0xFFFFFFFE,
                         0xFFFFF000, 2048.0, 1 / 1024, 4, flags, period, 4 * n)
    records = records if records is not None else [(k % 4096, -1000 + k % 2001) for k in range(n)]
    body = header + b"".join(struct.pack("<Hh", *record) for record in records)
    return body + struct.pack("<I", zlib.crc32(body))


class ReceiverTests(unittest.TestCase):
    def test_fragmentation_every_byte_and_magic_in_header(self):
        parser = FrameParser()
        packet = frame(capture_id=int.from_bytes(b"TDD4", "little"))
        result = []
        for value in packet:
            result.extend(parser.feed(bytes([value]), now=0))
        parser.finish()
        self.assertEqual(len(result), 1)
        self.assertEqual(len(result[0].records), 2048)
        self.assertAlmostEqual(result[0].duration_s, .10235)

    def test_crc_and_resynchronization(self):
        bad = bytearray(frame())
        bad[100] ^= 1
        parser = FrameParser()
        captures = parser.feed(b"ruido" + bad + frame(capture_id=2), now=0)
        self.assertEqual([c.capture_id for c in captures], [2])
        self.assertGreater(parser.rejected, 0)

    def test_truncation_and_timeout(self):
        parser = FrameParser()
        self.assertFalse(parser.feed(frame()[:100], now=0))
        with self.assertRaises(ValueError):
            parser.finish()
        parser.expire(now=3)
        self.assertEqual(len(parser.feed(frame(), now=3)), 1)

    def test_limits_and_invalid_frequency(self):
        for offset, value in [(12, 1000000), (16, 19999), (40, 0), (44, 0)]:
            bad = bytearray(frame())
            struct.pack_into("<I", bad, offset, value)
            parser = FrameParser()
            captures = parser.feed(bad + frame(capture_id=2), now=0)
            self.assertEqual([c.capture_id for c in captures], [2])
            self.assertLessEqual(len(parser.buffer), MAX_FRAME)

    def test_nonfinite_and_zero_calibration(self):
        for offset, value in [(28, float("nan")), (32, float("inf")), (32, 0.0)]:
            bad = bytearray(frame())
            struct.pack_into("<f", bad, offset, value)
            parser = FrameParser()
            self.assertEqual(len(parser.feed(bad + frame(), now=0)), 1)
            self.assertGreater(parser.rejected, 0)

    def test_raw_range_even_with_valid_crc(self):
        packet = frame(n=2, records=[(4096, 0), (2048, -500)])
        parser = FrameParser()
        self.assertFalse(parser.feed(packet, now=0))
        self.assertGreater(parser.rejected, 0)

    def test_duration_boundaries(self):
        for n, fs, short in [(2000, 20000, True), (2001, 20000, False),
                             (2048, 20000, False), (2048, 40000, True)]:
            capture, = FrameParser().feed(frame(n=n, fs=fs), now=0)
            self.assertEqual(bool(capture.flags & 1), short)

    def test_inconsistent_short_flag(self):
        packet = bytearray(frame())
        struct.pack_into("<H", packet, 38, 1)
        struct.pack_into("<I", packet, len(packet) - 4, zlib.crc32(packet[:-4]))
        parser = FrameParser()
        self.assertFalse(parser.feed(packet, now=0))
        self.assertGreater(parser.rejected, 0)

    def test_bounded_random_stream(self):
        rng = random.Random(3313)
        stream = b"".join(rng.randbytes(17000) + frame(capture_id=k) for k in range(10))
        captures = FrameParser().feed(stream, now=0)
        self.assertEqual([c.capture_id for c in captures], list(range(10)))

    def test_csv_metadata_wrap_and_no_overwrite(self):
        capture, = FrameParser().feed(frame(n=3), now=0)
        with tempfile.TemporaryDirectory() as folder:
            first = save_capture(capture, Path(folder))
            second = save_capture(capture, Path(folder))
            self.assertNotEqual(first, second)
            with first.open(encoding="utf-8", newline="") as source:
                rows = list(csv.DictReader(source))
            self.assertEqual([int(r["seq"]) for r in rows], [0xFFFFFFFE, 0xFFFFFFFF, 0])
            self.assertAlmostEqual(float(rows[0]["i_meas_A"]), -2)
            self.assertAlmostEqual(float(rows[0]["i_ref_A"]), -1)
            metadata = json.loads(first.with_suffix(".json").read_text(encoding="utf-8"))
            self.assertEqual(metadata["sample_count"], 3)
            self.assertFalse(metadata["covers_five_50hz_periods"])

    def test_c_to_python_interoperability(self):
        fixture = ROOT / "build/host/captura_sintetica.bin"
        self.assertTrue(fixture.is_file(), "ejecutar antes test_firmware")
        packet = fixture.read_bytes()
        self.assertEqual(len(packet), 8244)
        capture, = FrameParser().feed(packet, now=0)
        self.assertEqual(len(capture.records), 2048)
        self.assertEqual(capture.first_seq, 6000 - 2048)
        self.assertEqual(capture.flags, 0)
        self.assertAlmostEqual(capture.offset_code, 2048)
        self.assertTrue(all(0 <= raw <= 4095 for raw, _ in capture.records))


if __name__ == "__main__":
    unittest.main(verbosity=2)
