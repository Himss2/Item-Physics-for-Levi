#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace itemphysics::profile {

inline constexpr std::string_view kMinecraftModule = "libminecraftpe.so";
inline constexpr std::string_view kItemRendererRtti = "12ItemRenderer";
inline constexpr std::size_t kItemRendererRenderVtableOffset = 0x18;

// ============================================================
// Actor / ItemActor layout
// Valid only for the fingerprinted Minecraft binary.
// ============================================================

inline constexpr std::ptrdiff_t kActorRegistryOffset = 0x10;
inline constexpr std::ptrdiff_t kActorEntityIdOffset = 0x18;

// ItemStackBase / ItemActor fields observed in Atlas.
inline constexpr std::ptrdiff_t kItemHandleOffset = 0x398;
inline constexpr std::ptrdiff_t kBlockPtrOffset = 0x3A8;
inline constexpr std::ptrdiff_t kItemCountOffset = 0x3B2;

inline constexpr std::ptrdiff_t kIsInItemFrameOffset = 0x440;

// Item identifier std::string inside Item.
inline constexpr std::ptrdiff_t kItemIdentifierOffset = 0xF0;

// ActorRenderData.
inline constexpr std::ptrdiff_t kRenderDataActorOffset = 0x00;
inline constexpr std::ptrdiff_t kRenderDataPositionOffset = 0x10;

// ============================================================
// Matrix helpers
// ============================================================

inline constexpr std::uintptr_t kGetWorldMatrixRva = 0x0A5C6868;
inline constexpr std::uintptr_t kMatrixStackPushRva = 0x107CC67C;
inline constexpr std::uintptr_t kMatrixStackRefDtorRva = 0x107CCD40;

// ============================================================
// ECS hashes
// ============================================================

inline constexpr std::uint32_t kOnGroundFlagComponentHash =
    0xC29078A0u;

// ============================================================
// Target fingerprint
// ============================================================

// SHA-256:
// 4492ce15ceda3bb4865788a50e8d35b1bbafd45b62ce441440240a693a97d749
//
// ItemRenderer::render:
// RVA 0xA29F7A8

inline constexpr std::array<std::uint32_t, 16> kRenderFingerprint = {
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

} // namespace itemphysics::profile
