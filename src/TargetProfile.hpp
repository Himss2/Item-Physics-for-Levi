#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace itemphysics::profile {

inline constexpr std::string_view kMinecraftModule =
    "libminecraftpe.so";

inline constexpr std::string_view kItemRendererRtti =
    "12ItemRenderer";

inline constexpr std::size_t
    kItemRendererRenderVtableOffset = 0x18;

// ============================================================
// ItemActor / ItemStack
// ============================================================

inline constexpr std::ptrdiff_t
    kActorRegistryOffset = 0x10;

inline constexpr std::ptrdiff_t
    kActorEntityIdOffset = 0x18;

inline constexpr std::ptrdiff_t
    kItemHandleOffset = 0x398;

inline constexpr std::ptrdiff_t
    kBlockPtrOffset = 0x3A8;

inline constexpr std::ptrdiff_t
    kItemCountOffset = 0x3B2;

inline constexpr std::ptrdiff_t
    kIsInItemFrameOffset = 0x440;

inline constexpr std::ptrdiff_t
    kItemIdentifierOffset = 0xF0;

// ============================================================
// ActorRenderData
// ============================================================

inline constexpr std::ptrdiff_t
    kRenderDataActorOffset = 0x00;

inline constexpr std::ptrdiff_t
    kRenderDataPositionOffset = 0x10;

// ============================================================
// MatrixStack
// ============================================================

inline constexpr std::uintptr_t
    kGetWorldMatrixRva =
        0x0A5C6868;

inline constexpr std::uintptr_t
    kMatrixStackPushRva =
        0x107CC67C;

inline constexpr std::uintptr_t
    kMatrixStackRefDtorRva =
        0x107CCD40;

// ============================================================
// ItemRenderer private helper
//
// ItemRenderer::render eventually calls:
//
// RVA 0xA29ED30
//
// This helper reads ItemActor::mIsInItemFrame AGAIN and uses
// it for model-specific transforms.
//
// We hook this separately so:
//
// outer ItemRenderer sees true  -> no bob/spin
// private helper sees original -> proper dropped-item model
// ============================================================

inline constexpr std::uintptr_t
    kItemRendererRenderHelperRva =
        0x0A29ED30;

// ============================================================
// BlockGraphics
// ============================================================

inline constexpr std::uintptr_t
    kBlockGraphicsGetForBlockRva =
        0x0A218A90;

inline constexpr std::uintptr_t
    kBlockGraphicsGetBlockShapeRva =
        0x0A2197B8;

// ============================================================
// Block / BlockType
// ============================================================

inline constexpr std::ptrdiff_t
    kBlockTypeOffset = 0x68;

inline constexpr std::size_t
    kBlockTypeGetVisualShapeVtableOffset =
        0x50;

// ============================================================
// ECS
// ============================================================

inline constexpr std::uint32_t
    kOnGroundFlagComponentHash =
        0xC29078A0u;

// ============================================================
// ItemRenderer::render fingerprint
// ============================================================

inline constexpr std::array<
    std::uint32_t,
    16>
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
};

// ============================================================
// Private render helper fingerprint
//
// RVA 0xA29ED30
// ============================================================

inline constexpr std::array<
    std::uint32_t,
    16>
    kRenderHelperFingerprint = {

        0xD10483FFu,
        0x6D0B23E9u,
        0xA90C7BFDu,
        0xA90D6FFCu,

        0xA90E67FAu,
        0xA90F5FF8u,
        0xA91057F6u,
        0xA9114FF4u,

        0x910303FDu,
        0xD53BD05Bu,
        0xAA0003F6u,
        0xAA0103E0u,

        0xF9401768u,
        0x2A0603F3u,
        0x1E204008u,
        0x2A0503F9u,
};

} // namespace itemphysics::profile
