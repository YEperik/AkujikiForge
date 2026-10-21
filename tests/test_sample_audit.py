"""Unit tests for the research-only sample audit helper (no game assets required)."""
from __future__ import annotations
import struct
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from sample_audit import inspect_outer_izca, parse_sot  # noqa: E402


class OuterIzcaTests(unittest.TestCase):
    def test_size_equation_passes(self) -> None:
        payload = b"DATA"
        header_span = 16
        data = b"IZCA" + struct.pack("<III", len(payload), header_span, 0) + (b"\x00" * header_span) + payload
        self.assertEqual(len(data), 36)
        result = inspect_outer_izca(data)
        self.assertEqual(result["status"], "pass")
        self.assertEqual(result["calculated_size"], len(data))

    def test_size_equation_mismatch_is_reported(self) -> None:
        data = b"IZCA" + struct.pack("<III", 4, 16, 0) + (b"\x00" * 16) + b"BAD"
        self.assertEqual(inspect_outer_izca(data)["status"], "mismatch")

    def test_non_izca_is_not_applicable(self) -> None:
        self.assertEqual(inspect_outer_izca(b"NOTA"), {"applicable": False, "status": "not_applicable"})


class SotTests(unittest.TestCase):
    @staticmethod
    def one_record_sot(offset: int = 0x28, text: bytes = b"OK\x00") -> bytes:
        data = bytearray(0x2B)
        data[0:8] = b"SOT:BIN\x00"
        struct.pack_into("<I", data, 0x0C, 1)
        struct.pack_into("<II", data, 0x20, 0x12345678, offset)
        if offset < len(data):
            data[offset:min(offset + len(text), len(data))] = text[:max(0, len(data) - offset)]
        return bytes(data)

    def test_valid_string_is_extracted(self) -> None:
        rows, info = parse_sot("fixture.SOT", self.one_record_sot())
        self.assertEqual(info["status"], "parsed")
        self.assertEqual(info["valid_strings"], 1)
        self.assertEqual(rows[0]["text"], "OK")
        self.assertTrue(rows[0]["valid_nul_terminated"])

    def test_out_of_bounds_string_offset_is_rejected(self) -> None:
        rows, info = parse_sot("fixture.SOT", self.one_record_sot(offset=0x40))
        self.assertEqual(info["status"], "parsed")
        self.assertEqual(info["invalid_strings"], 1)
        self.assertFalse(rows[0]["valid_nul_terminated"])

    def test_unreasonable_record_count_is_rejected(self) -> None:
        data = bytearray(self.one_record_sot())
        struct.pack_into("<I", data, 0x0C, 99)
        rows, info = parse_sot("fixture.SOT", bytes(data))
        self.assertEqual(rows, [])
        self.assertEqual(info["status"], "invalid_record_count")


if __name__ == "__main__":
    unittest.main()
