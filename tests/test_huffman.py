"""Integration checks for the command-line Huffman compressor (stdlib only)."""
import os
from pathlib import Path
import random
import struct
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
PROGRAM = Path(os.environ.get("HUFFMAN_PROGRAM", ROOT / "main")).resolve()


class HuffmanTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.folder = Path(self.directory.name)

    def run_tool(self, *args, success=True):
        result = subprocess.run([str(PROGRAM), *map(str, args)],
                                cwd=self.folder, capture_output=True, text=True)
        self.assertEqual(result.returncode == 0, success, result.stderr)
        return result

    def round_trip(self, data):
        source = self.folder / "input.bin"
        packed = self.folder / "packed.huff"
        restored = self.folder / "restored.bin"
        source.write_bytes(data)
        result = self.run_tool("-c", source, "-o", packed)
        self.assertIn("Huffman Code", result.stdout)
        self.run_tool("-d", packed, "-o", restored)
        self.assertEqual(restored.read_bytes(), data)
        return packed.read_bytes()

    def test_round_trips(self):
        cases = [b"", b"x", b"x" * 10001, b"\x00" * 17,
                 bytes(range(256)) * 32, b"abcde" * 137,
                 "Hello, café! नमस्ते 🌍\n".encode(),
                 random.Random(42).randbytes(50000)]
        for data in cases:
            with self.subTest(size=len(data), prefix=data[:8]):
                self.round_trip(data)

    def test_header_and_bit_packing(self):
        # b: 0, a: 1. Five bits (11100) packed MSB-first with zero padding.
        packed = self.round_trip(b"aaabb")
        self.assertEqual(packed, b"HUF1\x00\x02" + b"a" + struct.pack(">Q", 3)
                         + b"b" + struct.pack(">Q", 2) + b"\xe0")
        self.assertEqual(self.round_trip(b""), b"HUF1\x00\x00")

    def test_compressible_data_is_smaller_and_deterministic(self):
        data = b"this is a simple compression tool\n" * 10000
        first = self.round_trip(data)
        self.assertLess(len(first), len(data))
        self.assertEqual(first, self.round_trip(data))
        equal_weights = bytes(range(256)) * 7
        self.assertEqual(self.round_trip(equal_weights), self.round_trip(equal_weights))

    def test_existing_project_sample(self):
        source = ROOT / "test.txt"
        if not source.exists():
            self.skipTest("Optional project sample is absent")
        data = source.read_bytes()
        packed = self.round_trip(data)
        self.assertLess(len(packed), len(data))

    def test_malformed_files_preserve_existing_output(self):
        valid = self.round_trip(b"aaabb")
        single = self.round_trip(b"x")
        entry = lambda symbol, count: bytes([symbol]) + struct.pack(">Q", count)
        cases = [b"", b"BAD!\x00\x00", b"HUF1", b"HUF1\x01\x01",
                 b"HUF1\x00\x01a", b"HUF1\x00\x01" + entry(97, 0),
                 b"HUF1\x00\x02" + entry(97, 1) + entry(97, 1),
                 b"HUF1\x00\x02" + entry(97, 2**64 - 1) + entry(98, 1),
                 # Total frequency fits, but the encoded bit count overflows.
                 b"HUF1\x00\x03" + entry(97, 2**63 - 1)
                 + entry(98, 2**62) + entry(99, 2**62),
                 valid[:-1], valid + b"\x00", valid[:-1] + b"\xe1",
                 valid[:-1] + b"\xf8", single[:-1] + b"\x80",
                 b"HUF1\x00\x00\x00"]
        output = self.folder / "preserved.bin"
        for data in cases:
            with self.subTest(data=data):
                output.write_bytes(b"existing output")
                source = self.folder / "bad.huff"
                source.write_bytes(data)
                self.run_tool("-d", source, "-o", output, success=False)
                self.assertEqual(output.read_bytes(), b"existing output")
                self.assertFalse(list(self.folder.glob("*.tmp.*")))

    def test_truncation_at_every_byte(self):
        valid = self.round_trip(b"abracadabra" * 5)
        source = self.folder / "truncated.huff"
        output = self.folder / "unused.bin"
        for size in range(len(valid)):
            source.write_bytes(valid[:size])
            self.run_tool("-d", source, "-o", output, success=False)
            self.assertFalse(output.exists())

    def test_same_file_and_hardlink_are_rejected(self):
        source = self.folder / "input.txt"
        source.write_bytes(b"keep this content")
        alias = self.folder / "alias.txt"
        alias.hardlink_to(source)
        for target in [source, alias]:
            self.run_tool("-c", source, "-o", target, success=False)
            self.assertEqual(source.read_bytes(), b"keep this content")

    def test_cli_and_table_mode(self):
        source = self.folder / "test.txt"
        source.write_bytes(b"abc\n")
        self.assertIn("Huffman Code", self.run_tool().stdout)
        self.assertIn("Huffman Code", self.run_tool(source).stdout)
        self.assertIn("Usage:", self.run_tool("--help").stdout)
        packed = self.folder / "long options.huff"
        self.run_tool("--compress", source, "--output", packed)
        self.run_tool("--decompress", packed, "--output", source)
        self.assertEqual(source.read_bytes(), b"abc\n")
        for args in [("-c",), ("-d", source), ("-c", "-d", source),
                     ("--unknown",), (source, source), ("-o",),
                     ("-o", packed), ("-c", source, "-o", packed, "-o", source),
                     ("-c", "missing.txt", "-o", packed),
                     ("-c", source, "-o", self.folder / "missing" / "out.huff")]:
            self.run_tool(*args, success=False)


if __name__ == "__main__":
    unittest.main()
