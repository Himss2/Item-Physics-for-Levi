#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace itemphysics::profile {

// ============================================================
// Minecraft 1.26.45.1 ARM64
//
// SHA-256:
// 444e77434bdd3789a0d90978d06336a99831e78e52955e528258cc375dfa0557
//
// Build ID:
// 868e275cb295e9a275bb29d2258edc2f7dc48761
// ============================================================

inline constexpr std::string_view kMinecraftModule =
    "libminecraftpe.so";

inline constexpr std::string_view kItemRendererRtti =
    "12ItemRenderer";

inline constexpr std::size_t
    kItemRendererRenderVtableOffset =
        0x18;

// ============================================================
// ItemRenderer
// ============================================================

inline constexpr std::uintptr_t
    kItemRendererRenderRva =
        0x0A29F708;

// ============================================================
// ItemActor / ItemStackBase
//
// Layout confirmed again directly from
// ItemRenderer::render on 1.26.45.1.
// ============================================================

inline constexpr std::ptrdiff_t
    kActorRegistryOffset =
        0x10;

inline constexpr std::ptrdiff_t
    kActorEntityIdOffset =
        0x18;

// ItemStackBase begins at ItemActor + 0x390.

inline constexpr std::ptrdiff_t
    kItemHandleOffset =
        0x398;

inline constexpr std::ptrdiff_t
    kBlockPtrOffset =
        0x3A8;

inline constexpr std::ptrdiff_t
    kItemCountOffset =
        0x3B2;

inline constexpr std::ptrdiff_t
    kIsInItemFrameOffset =
        0x440;

// libc++ std::string identifier inside Item.
inline constexpr std::ptrdiff_t
    kItemIdentifierOffset =
        0xF0;

// ============================================================
// ActorRenderData
// ============================================================

inline constexpr std::ptrdiff_t
    kRenderDataActorOffset =
        0x00;

inline constexpr std::ptrdiff_t
    kRenderDataPositionOffset =
        0x10;

// ============================================================
// Matrix helpers - Minecraft 1.26.45.1
// ============================================================

// Tiny getter:
//
// ldr x8, [x0,#0x28]
// ldr x8, [x8,#0x18]
// add x0, x8,#0x48
// ret

inline constexpr std::uintptr_t
    kGetWorldMatrixRva =
        0x0A5C67C8;

// MatrixStack::push(...)
inline constexpr std::uintptr_t
    kMatrixStackPushRva =
        0x107CBFFC;

// Correct matching MatrixStack ref destructor.
//
// NOTE:
// 0x107CC618 contains another identical implementation,
// but ItemRenderer 1.26.45.1 itself uses the second copy:
//
// 0x107CC6C0
inline constexpr std::uintptr_t
    kMatrixStackRefDtorRva =
        0x107CC6C0;

// ============================================================
// BlockGraphics
// ============================================================

// BlockGraphics::getForBlock(Block const&)
//
// starts:
//
// ldr x0, [x0,#0x68]
//
// proving Block::mBlockType remains +0x68.
inline constexpr std::uintptr_t
    kBlockGraphicsGetForBlockRva =
        0x0A2189F0;

// BlockGraphics::getBlockShape()
//
// ldr w0, [x0,#0x10]
// ret
inline constexpr std::uintptr_t
    kBlockGraphicsGetBlockShapeRva =
        0x0A219718;

// Vanilla ItemRenderer BlockShape classifier.
inline constexpr std::uintptr_t
    kIsBlockShape3DRva =
        0x0A280E68;

// ============================================================
// Block / BlockType
// ============================================================

inline constexpr std::ptrdiff_t
    kBlockTypeOffset =
        0x68;

// Layout remained compatible in 1.26.45.1.
//
// BlockType vptr + 0x50
// = getVisualShape(...) slot.
inline constexpr std::size_t
    kBlockTypeGetVisualShapeVtableOffset =
        0x50;

// ============================================================
// ItemRenderer private helper
//
// Not currently hooked by the stable implementation.
// Kept here for continued RE.
//
// It still:
// - reads ItemActor +0x440
// - recognizes Skull = 0x53 / 83
// - contains skull translation -0.125
// ============================================================

inline constexpr std::uintptr_t
    kItemRendererRenderHelperRva =
        0x0A29EC90;

// ============================================================
// ECS
// ============================================================

inline constexpr std::uint32_t
    kOnGroundFlagComponentHash =
        0xC29078A0u;

// ============================================================
// Stronger ItemRenderer fingerprint
//
// IMPORTANT:
//
// Previous fingerprint contained only 16 words.
// Those 16 words are IDENTICAL between the old Minecraft
// binary and 1.26.45.1, which is why the stale profile was
// incorrectly accepted.
//
// This profile now uses 24 words.
//
// Word 21 differs between the two binaries, therefore the old
// binary and the new binary can no longer silently share this
// profile.
// ============================================================

inline constexpr std::array<
    std::uint32_t,
    24>
    kRenderFingerprint = {

        0xD103C3FFu,
        0x6D072BEBu,
        0x6D0823E9u,
        0xA9097BFDu,

        0xF90053FBu,
        0xA90B67FAu,
        0xA90C5FF8u,
        0xA90D57F6u,

        0xA90E4FF4u,
        0x910243FDu,
        0xD53BD05Bu,
        0xAA0003F5u,

        0xAA0203E0u,
        0xF9401768u,
        0xAA0203F8u,
        0xAA0103F3u,

        0xF81D83A8u,
        0x9401E94Bu,
        0xB40022C0u,
        0x52800801u,

        0xAA0003F4u,

        // Minecraft 1.26.45.1-specific call encoding.
        0x9527B25Eu,

        0x36002240u,
        0x394ECE88u,
};

// ============================================================
// Helper fingerprints
//
// These can be used next to harden verifyProfile() so future
// Minecraft updates fail safely instead of calling a stale RVA.
// ============================================================

inline constexpr std::array<
    std::uint32_t,
    4>
    kGetWorldMatrixFingerprint = {

        0xF9401408u,
        0xF9400D08u,
        0x91012100u,
        0xD65F03C0u,
};

inline constexpr std::array<
    std::uint32_t,
    12>
    kMatrixStackPushFingerprint = {

        0xA9BE7BFDu,
        0xA9014FF4u,
        0x910003FDu,
        0xAA0803F3u,

        0xF9401408u,
        0xAA0003F4u,
        0x52800029u,
        0x39010009u,

        0x36000061u,
        0xF9001A88u,
        0x3900E289u,
        0xF9401289u,
};

inline constexpr std::array<
    std::uint32_t,
    12>
    kMatrixStackRefDtorFingerprint = {

        0xA9BE7BFDu,
        0xA9014FF4u,
        0x910003FDu,
        0xF9400013u,

        0xB4000453u,
        0x3940E268u,
        0x52800029u,
        0x39010269u,

        0x360000E8u,
        0xA942AA68u,
        0xD1000509u,
        0xEB0A013Fu,
};

inline constexpr std::array<
    std::uint32_t,
    8>
    kBlockGraphicsGetForBlockFingerprint = {

        0xA9BF7BFDu,
        0x910003FDu,
        0xF9403400u,
        0x9556EC72u,

        0x955A115Fu,
        0x90041CC8u,
        0xF9454509u,
        0xB4000709u,
};

inline constexpr std::array<
    std::uint32_t,
    2>
    kBlockGraphicsGetBlockShapeFingerprint = {

        0xB9401000u,
        0xD65F03C0u,
};

} // namespace itemphysics::profile
