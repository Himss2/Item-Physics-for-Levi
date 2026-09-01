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
    kItemRendererRenderVtableOffset =
        0x18;

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

inline constexpr std::ptrdiff_t
    kRenderDataActorOffset =
        0x00;

inline constexpr std::ptrdiff_t
    kRenderDataPositionOffset =
        0x10;

inline constexpr std::uintptr_t
    kGetWorldMatrixRva =
        0x0A5C67C8;

inline constexpr std::uintptr_t
    kMatrixStackPushRva =
        0x107CBFFC;

inline constexpr std::uintptr_t
    kMatrixStackRefDtorRva =
        0x107CC6C0;

inline constexpr std::uintptr_t
    kGetPosDeltaRva =
        0x0EC82A68;

inline constexpr std::uintptr_t
    kRenderItemGroupLikeRva =
        0x0A29F338;

inline constexpr std::uintptr_t
    kGetBlockTypeForRenderingRva =
        0x0F642ADC;

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

inline constexpr std::ptrdiff_t
    kBlockTypeOffset =
        0x68;

inline constexpr std::size_t
    kBlockTypeGetVisualShapeVtableOffset =
        0x50;

inline constexpr std::uintptr_t
    kItemRendererRenderHelperRva =
        0x0A29EC90;

inline constexpr std::uint32_t
    kOnGroundFlagComponentHash =
        0xC29078A0u;

inline constexpr std::uint32_t
    kRelativeShadowOffsetComponentHash =
        0x7FD7A655u;

inline constexpr std::uintptr_t
    kRelativeShadowStorageRva =
        0x0E96A7E4;

inline constexpr std::uintptr_t
    kRelativeShadowEmplaceRva =
        0x0E96B68C;

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

inline constexpr std::array<
    std::uint32_t,
    3>
    kGetPosDeltaFingerprint = {

        0xF9410408u,
        0x91006100u,
        0xD65F03C0u,
};

inline constexpr std::array<
    std::uint32_t,
    8>
    kRenderItemGroupLikeFingerprint = {

        0xD102C3FFu,
        0xFD0013ECu,
        0x6D032BEBu,
        0x6D0423E9u,

        0xA9057BFDu,
        0xA9066FFCu,
        0xA90767FAu,
        0xA9085FF8u,
};

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

inline constexpr std::array<
    std::uint32_t,
    2>
    kBlockGraphicsGetBlockShapeFingerprint = {

        0xB9401000u,
        0xD65F03C0u,
};

inline constexpr std::array<
    std::uint32_t,
    12>
    kRelativeShadowStorageFingerprint = {

        0xD10203FFu,
        0xA9057BFDu,
        0xF90033F5u,
        0xA9074FF4u,

        0x910143FDu,
        0xD53BD055u,
        0xAA0003F4u,
        0x2A0103EAu,

        0xF94016A8u,
        0xAA0003F3u,
        0xF81F83A8u,
        0xA9C3A688u,
};

inline constexpr std::array<
    std::uint32_t,
    12>
    kRelativeShadowEmplaceFingerprint = {

        0xD10143FFu,
        0xA9017BFDu,
        0xA9025FF8u,
        0xA90357F6u,

        0xA9044FF4u,
        0x910043FDu,
        0xD53BD058u,
        0xAA0303F6u,

        0xAA1F03E3u,
        0xF9401708u,
        0xAA0003F3u,
        0xF90007E8u,
};

}
