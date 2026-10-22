#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Extract WHD's WLAN and CLM resources from a Pico SDK combined C array."""

import argparse
import re
from pathlib import Path


def extract_images(source):
    """Return unpadded WLAN firmware and CLM, checking the SDK's length metadata."""
    sizes = []
    for name in ("CYW43_WIFI_FW_LEN", "CYW43_CLM_LEN"):
        match = re.search(rf"^\s*#define\s+{name}\s+\((\d+)\)", source, re.MULTILINE)
        if match is None or int(match[1]) == 0:
            raise ValueError(f"Missing or invalid {name}")
        sizes.append(int(match[1]))
    match = re.search(
        r"static\s+const\s+unsigned\s+char\s+\w+\s*\[\s*\]"
        r"\s+CYW43_RESOURCE_ATTRIBUTE\s*=\s*\{(.*?)\};",
        source,
        re.DOTALL,
    )
    if match is None:
        raise ValueError("Missing combined firmware byte array")
    body = re.sub(r"/\*.*?\*/|//[^\n]*", "", match[1], flags=re.DOTALL)
    tokens = [token.strip() for token in body.split(",") if token.strip()]
    if not all(re.fullmatch(r"0x[0-9a-fA-F]{1,2}", token) for token in tokens):
        raise ValueError("Firmware array contains a non-byte token")
    data = bytes(int(token, 16) for token in tokens)
    firmware_size, clm_size = sizes
    clm_offset = (firmware_size + 511) & ~511
    if len(data) != clm_offset + clm_size:
        raise ValueError("Combined firmware length disagrees with SDK metadata")
    return data[:firmware_size], data[clm_offset:]


def write_if_changed(path, content):
    """Keep timestamps stable when CMake configures an unchanged resource."""
    if not path.exists() or path.read_bytes() != content:
        path.write_bytes(content)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", required=True, type=Path)
    parser.add_argument("--output-directory", required=True, type=Path)
    args = parser.parse_args()
    firmware, clm = extract_images(args.input.read_text(encoding="utf-8"))
    args.output_directory.mkdir(parents=True, exist_ok=True)
    write_if_changed(args.output_directory / "firmware.bin", firmware)
    write_if_changed(args.output_directory / "clm.blob", clm)


if __name__ == "__main__":
    main()
