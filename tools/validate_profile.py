#!/usr/bin/env python3
"""Validate the v0.1-dev libminecraftpe.so profile without external packages."""
from __future__ import annotations

import hashlib
import struct
import sys
from pathlib import Path

EXPECTED_SHA256 = "4492ce15ceda3bb4865788a50e8d35b1bbafd45b62ce441440240a693a97d749"
RENDER_RVA = 0x0A29F7A8
EXPECTED_WORDS = [
    0xD103C3FF, 0x6D072BEB, 0x6D0823E9, 0xA9097BFD,
    0xF90053FB, 0xA90B67FA, 0xA90C5FF8, 0xA90D57F6,
    0xA90E4FF4, 0x910243FD, 0xD53BD05B, 0xAA0003F5,
    0xAA0203E0, 0xF9401768, 0xAA0203F8, 0xAA0103F3,
]


def rva_to_file_offset(data: bytes, rva: int) -> int:
    if data[:4] != b"\x7fELF" or data[4] != 2 or data[5] != 1:
        raise ValueError("expected ELF64 little-endian")
    # Elf64_Ehdr offsets: phoff@0x20, phentsize@0x36, phnum@0x38.
    phoff = struct.unpack_from("<Q", data, 0x20)[0]
    phentsize = struct.unpack_from("<H", data, 0x36)[0]
    phnum = struct.unpack_from("<H", data, 0x38)[0]
    for index in range(phnum):
        off = phoff + index * phentsize
        p_type, _p_flags, p_offset, p_vaddr, _p_paddr, p_filesz, _p_memsz, _p_align = \
            struct.unpack_from("<IIQQQQQQ", data, off)
        if p_type == 1 and p_vaddr <= rva < p_vaddr + p_filesz:  # PT_LOAD
            return p_offset + (rva - p_vaddr)
    raise ValueError(f"RVA 0x{rva:X} not present in a file-backed PT_LOAD segment")


def main() -> int:
    if len(sys.argv) != 2:
        print(f"usage: {Path(sys.argv[0]).name} /path/to/libminecraftpe.so")
        return 2
    path = Path(sys.argv[1])
    data = path.read_bytes()
    digest = hashlib.sha256(data).hexdigest()
    print(f"sha256: {digest}")
    print(f"expected: {EXPECTED_SHA256}")
    try:
        file_off = rva_to_file_offset(data, RENDER_RVA)
    except ValueError as exc:
        print(f"profile: FAIL ({exc})")
        return 1
    words = list(struct.unpack_from("<16I", data, file_off))
    fingerprint_ok = words == EXPECTED_WORDS
    print(f"ItemRenderer::render RVA: 0x{RENDER_RVA:X}")
    print(f"fingerprint: {'MATCH' if fingerprint_ok else 'MISMATCH'}")
    print(f"full SHA: {'MATCH' if digest == EXPECTED_SHA256 else 'MISMATCH'}")
    return 0 if fingerprint_ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
