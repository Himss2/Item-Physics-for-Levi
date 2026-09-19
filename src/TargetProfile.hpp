#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace itemphysics::profile {

// Strict profile for the supplied arm64-v8a libminecraftpe.so:
// SHA-256 b8a6351503d330628335a80e8131acd45291fa9a747465f0f34a31b2346847b4
// Build ID 712509dc14ccc233e91f267937dfb46ecdcc4b68
inline constexpr std::string_view kMinecraftModule = "libminecraftpe.so";
inline constexpr std::string_view kItemRendererRtti = "12ItemRenderer";
inline constexpr std::string_view kItemActorRtti = "9ItemActor";
inline constexpr std::size_t kItemRendererRenderVtableOffset = 0x18;
inline constexpr std::size_t kItemActorRemoveVtableOffset = 0x60;
inline constexpr std::size_t kItemActorEventVtableOffset = 0x228;
inline constexpr std::array<std::uint8_t, 20> kBuildId = {
    0x71, 0x25, 0x09, 0xDC, 0x14, 0xCC, 0xC2, 0x33, 0xE9, 0x1F,
    0x26, 0x79, 0x37, 0xDF, 0xB4, 0x6E, 0xCD, 0xCC, 0x4B, 0x68};

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

inline constexpr std::uintptr_t kItemRendererRenderRva = 0x0A7120DC;
inline constexpr std::uintptr_t kRenderItemGroupLikeRva = 0x0A711D0C;
inline constexpr std::uintptr_t kGetWorldMatrixRva = 0x0A953460;
inline constexpr std::uintptr_t kGetPartialTickRva = 0x0A953424;
inline constexpr std::uintptr_t kMatrixStackPushRva = 0x110AF780;
inline constexpr std::uintptr_t kMatrixStackRefDtorRva = 0x110AFE44;
inline constexpr std::uintptr_t kGetBlockTypeForRenderingRva = 0x0FFA4454;
inline constexpr std::uintptr_t kGetActorPositionRva = 0x0F563FB0;
inline constexpr std::uintptr_t kGetActorPreviousPositionRva = 0x0F56AC88;
inline constexpr std::uintptr_t kGetPosDeltaRva = 0x0F55E894;
inline constexpr std::uintptr_t kBlockGraphicsGetForBlockTypeRva = 0x0A65FA9C;
inline constexpr std::uintptr_t kBlockGraphicsGetForBlockRva = 0x0A65FAB0;
inline constexpr std::uintptr_t kBlockGraphicsGetBlockShapeRva = 0x0A6607D8;
inline constexpr std::uintptr_t kIsBlockShape3DRva = 0x0A6C82F4;
inline constexpr std::uintptr_t kRelativeShadowStorageRva = 0x0F1AC494;
inline constexpr std::uintptr_t kRelativeShadowEmplaceRva = 0x0F1AD33C;
inline constexpr std::uintptr_t kItemActorEventRva = 0x0FA29B18;
inline constexpr std::uintptr_t kActorRemoveRva = 0x0F56BE58;
inline constexpr std::uintptr_t kGetActorUniqueIdRva = 0x0F566F74;
inline constexpr std::uintptr_t kMergeRemoveSequenceRva = 0x0FA28D7C;
// ItemActor::normalTick calls source->remove() at 0xFA28D8C. At the
// following instruction x25 is still the source and x24 the destination.
inline constexpr std::uintptr_t kMergeRemoveReturnRva = 0x0FA28D90;

inline constexpr std::uint32_t kOnGroundFlagComponentHash = 0xC29078A0u;
inline constexpr std::uint32_t kVerticalCollisionFlagComponentHash =
    0xC6A02A9Au;
inline constexpr std::uint32_t kRelativeShadowOffsetComponentHash =
    0x7FD7A655u;

inline constexpr std::array<std::uint32_t, 24> kRenderFingerprint = {
    0xD103C3FFu, 0x6D072BEBu, 0x6D0823E9u, 0xA9097BFDu,
    0xF90053FBu, 0xA90B67FAu, 0xA90C5FF8u, 0xA90D57F6u,
    0xA90E4FF4u, 0x910243FDu, 0xD53BD05Bu, 0xF9401768u,
    0xF81D83A8u, 0xF9400053u, 0xB40022B3u, 0xAA0103F4u,
    0xAA0003F5u, 0xAA1303E0u, 0x52800801u, 0xAA0203F8u,
    0x9539577Cu, 0x360021C0u, 0x394ECE68u, 0x34002188u};

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
        0xA9BF7BFDu, 0x910003FDu, 0x956AA550u, 0xA8C17BFDu, 0x17FFFFA1u};

inline constexpr std::array<std::uint32_t, 5>
    kBlockGraphicsGetForBlockFingerprint = {
        0xA9BF7BFDu, 0x910003FDu, 0xF9403400u, 0x956AA54Au, 0x956E29F5u};

inline constexpr std::array<std::uint32_t, 2>
    kBlockGraphicsGetBlockShapeFingerprint = {0xB9401000u, 0xD65F03C0u};

inline constexpr std::array<std::uint32_t, 10> kIsBlockShape3DFingerprint = {
    0x7102781Fu, 0x54000148u, 0x2A0003E8u, 0xF0FC3449u, 0x91397929u,
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
    0x12001C41u, 0x1415DD63u, 0x17ED3385u};

inline constexpr std::array<std::uint32_t, 12> kActorRemoveFingerprint = {
    0xD10203FFu, 0xA9047BFDu, 0xF9002BF7u, 0xA90657F6u,
    0xA9074FF4u, 0x910103FDu, 0xD53BD055u, 0xF94016A8u,
    0xF81F83A8u, 0x39494408u, 0x370026C8u, 0xF9400809u};

inline constexpr std::array<std::uint32_t, 12>
    kGetActorUniqueIdFingerprint = {
        0xA9BE7BFDu, 0xF9000BF3u, 0x910003FDu, 0xF9400809u,
        0x528AF5EAu, 0x72A31F2Au, 0xB9404128u, 0xF9401D2Bu,
        0x4B0B0108u, 0x53037D08u, 0x51000508u, 0x8A0A010Cu};

inline constexpr std::array<std::uint32_t, 10>
    kMergeRemoveSequenceFingerprint = {
        0x1A89B108u, 0xB9042B08u, 0xF9400328u, 0xF9403108u,
        0xD63F0100u, 0xAA1903E0u, 0x97ECDABFu, 0xF9400008u,
        0x394ECB03u, 0x910063E4u};

} // namespace itemphysics::profile
