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
inline constexpr std::string_view kItemActorRtti = "9ItemActor";
inline constexpr std::size_t kItemRendererRenderVtableOffset = 0x18;
inline constexpr std::size_t kItemActorRemoveVtableOffset = 0x60;
inline constexpr std::size_t kItemActorEventVtableOffset = 0x228;
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
inline constexpr std::uintptr_t kGetActorPositionRva = 0x0EC7A020;
inline constexpr std::uintptr_t kGetActorPreviousPositionRva = 0x0EC8EAAC;
inline constexpr std::uintptr_t kGetPosDeltaRva = 0x0EC82A68;
inline constexpr std::uintptr_t kBlockGraphicsGetForBlockTypeRva = 0x0A2189DC;
inline constexpr std::uintptr_t kBlockGraphicsGetForBlockRva = 0x0A2189F0;
inline constexpr std::uintptr_t kBlockGraphicsGetBlockShapeRva = 0x0A219718;
inline constexpr std::uintptr_t kIsBlockShape3DRva = 0x0A280E68;
inline constexpr std::uintptr_t kRelativeShadowStorageRva = 0x0E96A7E4;
inline constexpr std::uintptr_t kRelativeShadowEmplaceRva = 0x0E96B68C;
inline constexpr std::uintptr_t kItemActorEventRva = 0x0F12537C;
inline constexpr std::uintptr_t kItemActorNormalTickRva = 0x0F124154;
inline constexpr std::uintptr_t kActorRemoveRva = 0x0EC8FC7C;
inline constexpr std::uintptr_t kGetActorUniqueIdRva = 0x0EC8B12C;
inline constexpr std::uintptr_t kActorIsClientSideRva = 0x0EC8E9D8;
inline constexpr std::uintptr_t kItemStackIsFireResistantRva = 0x0F63FC00;
inline constexpr std::uintptr_t kMergeRemoveSequenceRva = 0x0F1245D8;
// ItemActor::normalTick calls source->remove() at 0xF1245E8. At the
// following instruction x25 is still the source and x24 the destination.
inline constexpr std::uintptr_t kMergeRemoveReturnRva = 0x0F1245EC;

inline constexpr std::uint32_t kOnGroundFlagComponentHash = 0xC29078A0u;
inline constexpr std::uint32_t kVerticalCollisionFlagComponentHash =
    0xC6A02A9Au;
inline constexpr std::uint32_t kRelativeShadowOffsetComponentHash =
    0x7FD7A655u;

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

inline constexpr std::array<std::uint32_t, 2> kGetActorPositionFingerprint = {
    0xF9410400u, 0xD65F03C0u};

inline constexpr std::array<std::uint32_t, 3>
    kGetActorPreviousPositionFingerprint = {
        0xF9410408u, 0x91003100u, 0xD65F03C0u};

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

inline constexpr std::array<std::uint32_t, 12>
    kRelativeShadowStorageFingerprint = {
        0xD10203FFu, 0xA9057BFDu, 0xF90033F5u, 0xA9074FF4u,
        0x910143FDu, 0xD53BD055u, 0xAA0003F4u, 0x2A0103EAu,
        0xF94016A8u, 0xAA0003F3u, 0xF81F83A8u, 0xA9C3A688u};

inline constexpr std::array<std::uint32_t, 12>
    kRelativeShadowEmplaceFingerprint = {
        0xD10143FFu, 0xA9017BFDu, 0xA9025FF8u, 0xA90357F6u,
        0xA9044FF4u, 0x910043FDu, 0xD53BD058u, 0xAA0303F6u,
        0xAA1F03E3u, 0xF9401708u, 0xAA0003F3u, 0xF90007E8u};

inline constexpr std::array<std::uint32_t, 7> kItemActorEventFingerprint = {
    0x12001C28u, 0x7101151Fu, 0x54000081u, 0x910E4000u,
    0x12001C41u, 0x14146547u, 0x17EDD3E5u};

inline constexpr std::array<std::uint32_t, 16>
    kItemActorNormalTickFingerprint = {
        0xD10383FFu, 0xFD003BE8u, 0xA9087BFDu, 0xA9096FFCu,
        0xA90A67FAu, 0xA90B5FF8u, 0xA90C57F6u, 0xA90D4FF4u,
        0x910203FDu, 0xD53BD05Au, 0xAA0003F3u, 0xF9401748u,
        0xF81E83A8u, 0xB9442C09u, 0x71000528u, 0x540000ABu};

inline constexpr std::array<std::uint32_t, 12> kActorRemoveFingerprint = {
    0xD10203FFu, 0xA9047BFDu, 0xF9002BF7u, 0xA90657F6u,
    0xA9074FF4u, 0x910103FDu, 0xD53BD055u, 0xF94016A8u,
    0xF81F83A8u, 0x39494408u, 0x370026C8u, 0xF9400809u};

inline constexpr std::array<std::uint32_t, 12>
    kGetActorUniqueIdFingerprint = {
        0xA9BE7BFDu, 0xF9000BF3u, 0x910003FDu, 0xF9400809u,
        0x528AF5EAu, 0x72A31F2Au, 0xB9404128u, 0xF9401D2Bu,
        0x4B0B0108u, 0x53037D08u, 0x51000508u, 0x8A0A010Cu};

inline constexpr std::array<std::uint32_t, 7>
    kActorIsClientSideFingerprint = {
        0xF940E800u, 0xB4000080u, 0xF9400008u, 0xF944F901u,
        0xD61F0020u, 0x52800020u, 0xD65F03C0u};

inline constexpr std::array<std::uint32_t, 7>
    kItemStackIsFireResistantFingerprint = {
        0xF9400408u, 0xB4000088u, 0xF9400100u, 0xB4000040u,
        0x1400A765u, 0x2A1F03E0u, 0xD65F03C0u};

inline constexpr std::array<std::uint32_t, 10>
    kMergeRemoveSequenceFingerprint = {
        0x1A89B108u, 0xB9042B08u, 0xF9400328u, 0xF9403108u,
        0xD63F0100u, 0xAA1903E0u, 0x97ED7D27u, 0xF9400008u,
        0x394ECB03u, 0x910063E4u};

} // namespace itemphysics::profile
