#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace itemphysics::profile {

// Strict profile for the supplied arm64-v8a libminecraftpe.so:
// SHA-256 444e77434bdd3789a0d90978d06336a99831e78e52955e528258cc375dfa0557
// Build ID 868e275cb295e9a275bb29d2258edc2f7dc48761
inline constexpr std::string_view kMinecraftModule = "libminecraftpe.so";
inline constexpr std::string_view kItemRendererRtti = "12ItemRenderer";
inline constexpr std::size_t kItemRendererRenderVtableOffset = 0x18;
inline constexpr std::array<std::uint8_t, 20> kBuildId = {
    0x86, 0x8E, 0x27, 0x5C, 0xB2, 0x95, 0xE9, 0xA2, 0x75, 0xBB,
    0x29, 0xD2, 0x25, 0x8E, 0xDC, 0x2F, 0x7D, 0xC4, 0x87, 0x61};

inline constexpr std::ptrdiff_t kActorRegistryOffset = 0x10;
inline constexpr std::ptrdiff_t kActorEntityIdOffset = 0x18;
inline constexpr std::ptrdiff_t kItemAgeOffset = 0x428;
inline constexpr std::ptrdiff_t kItemBobOffset = 0x434;
inline constexpr std::ptrdiff_t kIsInItemFrameOffset = 0x440;
inline constexpr std::ptrdiff_t kItemStackBaseOffset = 0x390;
inline constexpr std::ptrdiff_t kItemHandleOffset = 0x398;
inline constexpr std::ptrdiff_t kBlockPtrOffset = 0x3A8;
inline constexpr std::ptrdiff_t kItemCountOffset = 0x3B2;
inline constexpr std::ptrdiff_t kRenderDataActorOffset = 0x00;
inline constexpr std::ptrdiff_t kRenderDataPositionOffset = 0x10;
inline constexpr std::ptrdiff_t kItemIdentifierOffset = 0xF0;
inline constexpr std::ptrdiff_t kBlockTypeOffset = 0x68;
inline constexpr std::size_t kBlockTypeGetVisualShapeVtableOffset = 0x50;

inline constexpr std::uintptr_t kItemRendererRenderRva = 0x0A29F708;
inline constexpr std::uintptr_t kRenderItemGroupLikeRva = 0x0A29F338;
inline constexpr std::uintptr_t kGetWorldMatrixRva = 0x0A5C67C8;
inline constexpr std::uintptr_t kGetPartialTickRva = 0x0A5C678C;
inline constexpr std::uintptr_t kMatrixStackPushRva = 0x107CBFFC;
inline constexpr std::uintptr_t kMatrixStackRefDtorRva = 0x107CC6C0;
inline constexpr std::uintptr_t kGetBlockTypeForRenderingRva = 0x0F642ADC;
inline constexpr std::uintptr_t kGetPosDeltaRva = 0x0EC82A68;
inline constexpr std::uintptr_t kBlockGraphicsGetForBlockTypeRva = 0x0A2189DC;
inline constexpr std::uintptr_t kBlockGraphicsGetForBlockRva = 0x0A2189F0;
inline constexpr std::uintptr_t kBlockGraphicsGetBlockShapeRva = 0x0A219718;
inline constexpr std::uintptr_t kIsBlockShape3DRva = 0x0A280E68;

inline constexpr std::uint32_t kOnGroundFlagComponentHash = 0xC29078A0u;
inline constexpr std::uint32_t kVerticalCollisionFlagComponentHash =
    0xC6A02A9Au;

inline constexpr std::array<std::uint32_t, 24> kRenderFingerprint = {
    0xD103C3FFu, 0x6D072BEBu, 0x6D0823E9u, 0xA9097BFDu,
    0xF90053FBu, 0xA90B67FAu, 0xA90C5FF8u, 0xA90D57F6u,
    0xA90E4FF4u, 0x910243FDu, 0xD53BD05Bu, 0xAA0003F5u,
    0xAA0203E0u, 0xF9401768u, 0xAA0203F8u, 0xAA0103F3u,
    0xF81D83A8u, 0x9401E94Bu, 0xB40022C0u, 0x52800801u,
    0xAA0003F4u, 0x9527B25Eu, 0x36002240u, 0x394ECE88u};

inline constexpr std::array<std::uint32_t, 8>
    kRenderItemGroupFingerprint = {
        0xD102C3FFu, 0xFD0013ECu, 0x6D032BEBu, 0x6D0423E9u,
        0xA9057BFDu, 0xA9066FFCu, 0xA90767FAu, 0xA9085FF8u};

inline constexpr std::array<std::uint32_t, 4> kGetWorldMatrixFingerprint = {
    0xF9401408u, 0xF9400D08u, 0x91012100u, 0xD65F03C0u};

inline constexpr std::array<std::uint32_t, 2> kGetPartialTickFingerprint = {
    0xBD40B000u, 0xD65F03C0u};

inline constexpr std::array<std::uint32_t, 8> kMatrixStackPushFingerprint = {
    0xA9BE7BFDu, 0xA9014FF4u, 0x910003FDu, 0xAA0803F3u,
    0xF9401408u, 0xAA0003F4u, 0x52800029u, 0x39010009u};

inline constexpr std::array<std::uint32_t, 8>
    kMatrixStackRefDtorFingerprint = {
        0xA9BE7BFDu, 0xA9014FF4u, 0x910003FDu, 0xF9400013u,
        0xB4000453u, 0x3940E268u, 0x52800029u, 0x39010269u};

inline constexpr std::array<std::uint32_t, 7>
    kGetBlockTypeForRenderingFingerprint = {
        0xF9400408u, 0xB40000C8u, 0xF9400100u, 0xB4000080u,
        0xF9400008u, 0xF9401D01u, 0xD61F0020u};

inline constexpr std::array<std::uint32_t, 3> kGetPosDeltaFingerprint = {
    0xF9410408u, 0x91006100u, 0xD65F03C0u};

inline constexpr std::array<std::uint32_t, 5>
    kBlockGraphicsGetForBlockTypeFingerprint = {
        0xA9BF7BFDu, 0x910003FDu, 0x9556EC78u, 0xA8C17BFDu, 0x17FFFFA1u};

inline constexpr std::array<std::uint32_t, 5>
    kBlockGraphicsGetForBlockFingerprint = {
        0xA9BF7BFDu, 0x910003FDu, 0xF9403400u, 0x9556EC72u, 0x955A115Fu};

inline constexpr std::array<std::uint32_t, 2>
    kBlockGraphicsGetBlockShapeFingerprint = {0xB9401000u, 0xD65F03C0u};

inline constexpr std::array<std::uint32_t, 10> kIsBlockShape3DFingerprint = {
    0x7102781Fu, 0x54000148u, 0x2A0003E8u, 0xB0FC4EC9u, 0x9117E929u,
    0x100000AAu, 0x3868692Bu, 0x8B0B094Au, 0x52800020u, 0xD61F0140u};

} // namespace itemphysics::profile
