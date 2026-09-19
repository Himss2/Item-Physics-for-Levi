#!/usr/bin/env python3
"""Validate the strict Minecraft 1.26.51.1 arm64 renderer profile."""
from __future__ import annotations

import hashlib
import struct
import sys
import zipfile
from pathlib import Path

EXPECTED_SHA256 = "b8a6351503d330628335a80e8131acd45291fa9a747465f0f34a31b2346847b4"

PROFILE = {
    "ItemRenderer::render": (
        0x0A7120DC,
        [
            0xD103C3FF, 0x6D072BEB, 0x6D0823E9, 0xA9097BFD,
            0xF90053FB, 0xA90B67FA, 0xA90C5FF8, 0xA90D57F6,
            0xA90E4FF4, 0x910243FD, 0xD53BD05B, 0xF9401768,
            0xF81D83A8, 0xF9400053, 0xB40022B3, 0xAA0103F4,
            0xAA0003F5, 0xAA1303E0, 0x52800801, 0xAA0203F8,
            0x9539577C, 0x360021C0, 0x394ECE68, 0x34002188,
        ],
    ),
    "render-group helper": (
        0x0A711D0C,
        [0xD102C3FF, 0xFD0013EC, 0x6D032BEB, 0x6D0423E9,
         0xA9057BFD, 0xA9066FFC, 0xA90767FA, 0xA9085FF8],
    ),
    "getWorldMatrix": (
        0x0A953460,
        [0xF9401408, 0xF9400D08, 0x91012100, 0xD65F03C0],
    ),
    "getPartialTick": (
        0x0A953424,
        [0xBD40B000, 0xD65F03C0],
    ),
    "MatrixStack::push": (
        0x110AF780,
        [0xA9BE7BFD, 0xA9014FF4, 0x910003FD, 0xAA0803F3,
         0xF9401408, 0xAA0003F4, 0x52800029, 0x39010009],
    ),
    "MatrixStackRef destructor": (
        0x110AFE44,
        [0xA9BE7BFD, 0xA9014FF4, 0x910003FD, 0xF9400013,
         0xB4000453, 0x3940E268, 0x52800029, 0x39010269],
    ),
    "getBlockTypeForRendering": (
        0x0FFA4454,
        [0xF9400408, 0xB40000C8, 0xF9400100, 0xB4000080,
         0xF9400008, 0xF9401D01, 0xD61F0020],
    ),
    "Actor::getPosition": (
        0x0F563FB0,
        [0xF9410400, 0xD65F03C0],
    ),
    "Actor::getPreviousPosition": (
        0x0F56AC88,
        [0xF9410408, 0x91003100, 0xD65F03C0],
    ),
    "Actor::getPosDelta": (
        0x0F55E894,
        [0xF9410408, 0x91006100, 0xD65F03C0],
    ),
    "BlockGraphics::getForBlock(BlockType)": (
        0x0A65FA9C,
        [0xA9BF7BFD, 0x910003FD, 0x956AA550, 0xA8C17BFD,
         0x17FFFFA1],
    ),
    "BlockGraphics::getForBlock(Block)": (
        0x0A65FAB0,
        [0xA9BF7BFD, 0x910003FD, 0xF9403400, 0x956AA54A,
         0x956E29F5],
    ),
    "BlockGraphics::getBlockShape": (
        0x0A6607D8,
        [0xB9401000, 0xD65F03C0],
    ),
    "BlockGraphics::isBlockShape3D": (
        0x0A6C82F4,
        [0x7102781F, 0x54000148, 0x2A0003E8, 0xF0FC3449,
         0x91397929, 0x100000AA, 0x3868692B, 0x8B0B094A,
         0x52800020, 0xD61F0140],
    ),
    "RelativeShadowOffsetComponent storage": (
        0x0F1AC494,
        [0xD10203FF, 0xA9057BFD, 0xF90033F5, 0xA9074FF4,
         0x910143FD, 0xD53BD055, 0xAA0003F4, 0x2A0103EA,
         0xF94016A8, 0xAA0003F3, 0xF81F83A8, 0xA9C3A688],
    ),
    "RelativeShadowOffsetComponent emplace": (
        0x0F1AD33C,
        [0xD10143FF, 0xA9017BFD, 0xA9025FF8, 0xA90357F6,
         0xA9044FF4, 0x910043FD, 0xD53BD058, 0xAA0303F6,
         0xAA1F03E3, 0xF9401708, 0xAA0003F3, 0xF90007E8],
    ),
    "ItemActor::handleEntityEvent": (
        0x0FA29B18,
        [0x12001C28, 0x7101151F, 0x54000081, 0x910E4000,
         0x12001C41, 0x1415DD63, 0x17ED3385],
    ),
    "Actor::remove": (
        0x0F56BE58,
        [0xD10203FF, 0xA9047BFD, 0xF9002BF7, 0xA90657F6,
         0xA9074FF4, 0x910103FD, 0xD53BD055, 0xF94016A8,
         0xF81F83A8, 0x39494408, 0x370026C8, 0xF9400809],
    ),
    "Actor unique ID accessor": (
        0x0F566F74,
        [0xA9BE7BFD, 0xF9000BF3, 0x910003FD, 0xF9400809,
         0x528AF5EA, 0x72A31F2A, 0xB9404128, 0xF9401D2B,
         0x4B0B0108, 0x53037D08, 0x51000508, 0x8A0A010C],
    ),
    "ItemActor merge/remove sequence": (
        0x0FA28D7C,
        [0x1A89B108, 0xB9042B08, 0xF9400328, 0xF9403108,
         0xD63F0100, 0xAA1903E0, 0x97ECDABF, 0xF9400008,
         0x394ECB03, 0x910063E4],
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
        print(f"usage: {Path(sys.argv[0]).name} /path/to/libminecraftpe.so-or-zip")
        return 2

    source = Path(sys.argv[1])
    try:
        if source.suffix.lower() == ".zip":
            with zipfile.ZipFile(source) as archive:
                data = archive.read("libminecraftpe.so")
        else:
            data = source.read_bytes()
    except (OSError, KeyError, zipfile.BadZipFile) as exc:
        print(f"input: FAIL ({exc})")
        return 1
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
