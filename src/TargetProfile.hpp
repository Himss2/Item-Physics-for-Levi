#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace itemphysics::profile {

// ============================================================================
// Minecraft Bedrock 1.26.45.1 ARM64
//
// SHA-256:
// 444e77434bdd3789a0d90978d06336a99831e78e52955e528258cc375dfa0557
//
// Build ID:
// 868e275cb295e9a275bb29d2258edc2f7dc48761
// ============================================================================

inline constexpr std::string_view kMinecraftModule =
    "libminecraftpe.so";

inline constexpr std::string_view kItemRendererRtti =
    "12ItemRenderer";

inline constexpr std::size_t
    kItemRendererRenderVtableOffset =
        0x18;

// ============================================================================
// ItemActor / ItemStackBase
// ============================================================================

inline constexpr std::ptrdiff_t
    kActorRegistryOffset =
        0x10;

inline constexpr std::ptrdiff_t
    kActorEntityIdOffset =
        0x18;

inline constexpr std::ptrdiff_t
    kItemStackBaseOffset =
        0x390;

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

inline constexpr std::ptrdiff_t
    kItemIdentifierOffset =
        0xF0;

// ============================================================================
// ActorRenderData
// ============================================================================

inline constexpr std::ptrdiff_t
    kRenderDataActorOffset =
        0x00;

inline constexpr std::ptrdiff_t
    kRenderDataPositionOffset =
        0x10;

// ============================================================================
// MatrixStack
// ============================================================================

inline constexpr std::uintptr_t
    kGetWorldMatrixRva =
        0x0A5C67C8;

inline constexpr std::uintptr_t
    kMatrixStackPushRva =
        0x107CBFFC;

inline constexpr std::uintptr_t
    kMatrixStackRefDtorRva =
        0x107CC6C0;

// ============================================================================
// ItemStackBase::getBlockTypeForRendering
// ============================================================================

inline constexpr std::uintptr_t
    kGetBlockTypeForRenderingRva =
        0x0F642ADC;

// ============================================================================
// BlockGraphics
// ============================================================================

inline constexpr std::uintptr_t
    kBlockGraphicsGetForBlockTypeRva =
        0x0A2189DC;

inline constexpr std::uintptr_t
    kBlockGraphicsGetForBlockRva =
        0x0A2189F0;

inline constexpr std::uintptr_t
    kBlockGraphicsGetBlockShapeRva =
        0x0A219718;

inline constexpr std::uintptr_t
    kIsBlockShape3DRva =
        0x0A280E68;

// ============================================================================
// Block / BlockType
// ============================================================================

inline constexpr std::ptrdiff_t
    kBlockTypeOffset =
        0x68;

inline constexpr std::size_t
    kBlockTypeGetVisualShapeVtableOffset =
        0x50;

// ============================================================================
// ItemRenderer private helper
// ============================================================================

inline constexpr std::uintptr_t
    kItemRendererRenderHelperRva =
        0x0A29EC90;

// ============================================================================
// ECS
// ============================================================================

inline constexpr std::uint32_t
    kOnGroundFlagComponentHash =
        0xC29078A0u;

// ============================================================================
// ItemRenderer::render fingerprint
// ============================================================================

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
        0x9527B25Eu,
        0x36002240u,
        0x394ECE88u,
};

// ============================================================================
// getBlockTypeForRendering fingerprint
// ============================================================================

inline constexpr std::array<
    std::uint32_t,
    7>
    kGetBlockTypeForRenderingFingerprint = {

        0xF9400408u,
        0xB40000C8u,
        0xF9400100u,
        0xB4000080u,

        0xF9400008u,
        0xF9401D01u,
        0xD61F0020u,
};

// ============================================================================
// BlockGraphics::getForBlock(BlockType)
// ============================================================================

inline constexpr std::array<
    std::uint32_t,
    5>
    kBlockGraphicsGetForBlockTypeFingerprint = {

        0xA9BF7BFDu,
        0x910003FDu,
        0x9556EC78u,
        0xA8C17BFDu,
        0x17FFFFA1u,
};

// ============================================================================
// BlockGraphics::getBlockShape
// ============================================================================

inline constexpr std::array<
    std::uint32_t,
    2>
    kBlockGraphicsGetBlockShapeFingerprint = {

        0xB9401000u,
        0xD65F03C0u,
};

} // namespace itemphysics::profile
