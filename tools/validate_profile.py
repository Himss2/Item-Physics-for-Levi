#!/usr/bin/env python3
"""Validate the strict Minecraft 1.26.45.1 arm64 renderer profile."""
from __future__ import annotations

import hashlib
import struct
import sys
from pathlib import Path

EXPECTED_SHA256 = "444e77434bdd3789a0d90978d06336a99831e78e52955e528258cc375dfa0557"

PROFILE = {
    "ItemRenderer::render": (
        0x0A29F708,
        [
            0xD103C3FF, 0x6D072BEB, 0x6D0823E9, 0xA9097BFD,
            0xF90053FB, 0xA90B67FA, 0xA90C5FF8, 0xA90D57F6,
            0xA90E4FF4, 0x910243FD, 0xD53BD05B, 0xAA0003F5,
            0xAA0203E0, 0xF9401768, 0xAA0203F8, 0xAA0103F3,
            0xF81D83A8, 0x9401E94B, 0xB40022C0, 0x52800801,
            0xAA0003F4, 0x9527B25E, 0x36002240, 0x394ECE88,
        ],
    ),
    "render-group helper": (
        0x0A29F338,
        [0xD102C3FF, 0xFD0013EC, 0x6D032BEB, 0x6D0423E9,
         0xA9057BFD, 0xA9066FFC, 0xA90767FA, 0xA9085FF8],
    ),
    "getWorldMatrix": (
        0x0A5C67C8,
        [0xF9401408, 0xF9400D08, 0x91012100, 0xD65F03C0],
    ),
    "getPartialTick": (
        0x0A5C678C,
        [0xBD40B000, 0xD65F03C0],
    ),
    "MatrixStack::push": (
        0x107CBFFC,
        [0xA9BE7BFD, 0xA9014FF4, 0x910003FD, 0xAA0803F3,
         0xF9401408, 0xAA0003F4, 0x52800029, 0x39010009],
    ),
    "MatrixStackRef destructor": (
        0x107CC6C0,
        [0xA9BE7BFD, 0xA9014FF4, 0x910003FD, 0xF9400013,
         0xB4000453, 0x3940E268, 0x52800029, 0x39010269],
    ),
    "getBlockTypeForRendering": (
        0x0F642ADC,
        [0xF9400408, 0xB40000C8, 0xF9400100, 0xB4000080,
         0xF9400008, 0xF9401D01, 0xD61F0020],
    ),
}


def load_segments(data: bytes) -> list[tuple[int, int, int]]:
    if data[:4] != b"\x7fELF" or data[4] != 2 or data[5] != 1:
        raise ValueError("expected ELF64 little-endian")
    phoff = struct.unpack_from("<Q", data, 0x20)[0]
    phentsize = struct.unpack_from("<H", data, 0x36)[0]
    phnum = struct.unpack_from("<H", data, 0x38)[0]
    segments: list[tuple[int, int, int]] = []
    for index in range(phnum):
        off = phoff + index * phentsize
        values = struct.unpack_from("<IIQQQQQQ", data, off)
        if values[0] == 1:  # PT_LOAD
            segments.append((values[3], values[2], values[5]))
    return segments


def rva_to_file_offset(segments: list[tuple[int, int, int]], rva: int) -> int:
    for vaddr, file_offset, file_size in segments:
        if vaddr <= rva < vaddr + file_size:
            return file_offset + rva - vaddr
    raise ValueError(f"RVA 0x{rva:X} not present in a file-backed PT_LOAD segment")


def main() -> int:
    if len(sys.argv) != 2:
        print(f"usage: {Path(sys.argv[0]).name} /path/to/libminecraftpe.so")
        return 2

    data = Path(sys.argv[1]).read_bytes()
    digest = hashlib.sha256(data).hexdigest()
    sha_ok = digest == EXPECTED_SHA256
    print(f"sha256: {digest}")
    print(f"full SHA: {'MATCH' if sha_ok else 'MISMATCH'}")

    try:
        segments = load_segments(data)
        profile_ok = True
        for name, (rva, expected) in PROFILE.items():
            offset = rva_to_file_offset(segments, rva)
            actual = list(struct.unpack_from(f"<{len(expected)}I", data, offset))
            matched = actual == expected
            profile_ok &= matched
            print(f"{name} @ 0x{rva:X}: {'MATCH' if matched else 'MISMATCH'}")
    except (ValueError, struct.error) as exc:
        print(f"profile: FAIL ({exc})")
        return 1

    return 0 if sha_ok and profile_ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
