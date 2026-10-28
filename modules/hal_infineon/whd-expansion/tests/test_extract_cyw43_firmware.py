# Copyright (c) 2026 Adafruit Industries
#
# SPDX-License-Identifier: Apache-2.0
"""Golden byte layout, invalid input, and incremental build checks."""

import importlib.util
import os
import tempfile
import unittest
from pathlib import Path

SCRIPT = Path(__file__).resolve().parents[1] / "extract_cyw43_firmware.py"
SPEC = importlib.util.spec_from_file_location("extract_cyw43_firmware", SCRIPT)
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


def header(firmware, clm):
    data = firmware + bytes((-len(firmware)) % 512) + clm
    return (
        "static const unsigned char image[] CYW43_RESOURCE_ATTRIBUTE = {\n"
        + ",".join(f"0x{byte:02x}" for byte in data)
        + "};\n"
        + f"#define CYW43_WIFI_FW_LEN ({len(firmware)})\n"
        + f"#define CYW43_CLM_LEN ({len(clm)})\n"
    )


class ExtractTests(unittest.TestCase):
    def test_unpadded_images(self):
        self.assertEqual(
            MODULE.extract_images(header(b"\x01\x02\x03", b"CLM")),
            (b"\x01\x02\x03", b"CLM"),
        )

    def test_aligned_firmware_does_not_add_a_block(self):
        firmware = bytes(range(256)) * 2
        self.assertEqual(MODULE.extract_images(header(firmware, b"CLM")), (firmware, b"CLM"))

    def test_length_mismatch(self):
        with self.assertRaises(ValueError):
            MODULE.extract_images(header(b"FW", b"CLM").replace("CLM_LEN (3)", "CLM_LEN (4)"))

    def test_missing_metadata(self):
        with self.assertRaises(ValueError):
            MODULE.extract_images(header(b"FW", b"CLM").replace("CYW43_WIFI_FW_LEN", "OTHER"))

    def test_non_byte_tokens_are_not_evaluated(self):
        with self.assertRaises(ValueError):
            MODULE.extract_images(header(b"FW", b"CLM").replace("0x46", "dangerous_call()"))

    def test_comments_are_ignored(self):
        source = header(b"FW", b"CLM").replace("0x46", "/* not a byte: 0xff */ 0x46")
        self.assertEqual(MODULE.extract_images(source), (b"FW", b"CLM"))

    def test_zero_length_rejected(self):
        with self.assertRaises(ValueError):
            MODULE.extract_images(header(b"", b"CLM"))

    def test_unchanged_resource_preserves_timestamp(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "firmware.bin"
            MODULE.write_if_changed(path, b"FW")
            os.utime(path, ns=(1_000_000_000, 1_000_000_000))
            MODULE.write_if_changed(path, b"FW")
            self.assertEqual(path.stat().st_mtime_ns, 1_000_000_000)
            MODULE.write_if_changed(path, b"NEW")
            self.assertEqual(path.read_bytes(), b"NEW")


if __name__ == "__main__":
    unittest.main()
