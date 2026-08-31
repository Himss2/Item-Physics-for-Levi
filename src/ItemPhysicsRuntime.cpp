#include "ItemPhysicsRuntime.hpp"
#include "TargetProfile.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <string_view>

namespace itemphysics {
namespace {

constexpr float kPi =
    3.14159265358979323846f;

constexpr float kDegToRad =
    kPi / 180.0f;

constexpr auto kStateTtl =
    std::chrono::seconds(8);

constexpr float kAtlasFlatLocalY =
    0.25f;

constexpr std::int32_t kSkullBlockShape =
    83;

constexpr float kThinYRatio =
    0.70f;

constexpr float kExtentEpsilon =
    0.0005f;

constexpr std::string_view kShieldId =
    "minecraft:shield";

constexpr std::string_view kBannerId =
    "minecraft:banner";

[[nodiscard]]
bool isThinGroundShape(
    std::int32_t shape) noexcept {

  switch (shape) {

  case 9:
  case 14:
  case 15:
  case 23:
  case 67:
  case 68:
  case 69:
  case 72:
  case 73:
  case 80:
  case 96:
  case 99:
  case 114:
  case 126:
  case 135:
  case 136:
    return true;

  default:
    return false;
  }
}

[[nodiscard]]
bool isTorchGroundShape(
    std::int32_t shape) noexcept {

  switch (shape) {

  case 1:
  case 2:
  case 90:
  case 101:
  case 155:
    return true;

  default:
    return false;
  }
}

[[nodiscard]]
bool isShapedGroundShape(
    std::int32_t shape) noexcept {

  switch (shape) {

  case 7:
  case 8:
  case 10:
  case 11:
  case 12:
  case 13:
  case 18:
  case 19:
  case 20:
  case 21:
  case 22:
  case 25:
  case 26:
  case 28:
  case 31:
  case 32:
  case 40:
  case 42:
  case 43:
  case 44:
  case 70:
  case 71:
  case 74:
  case 76:
  case 77:
  case 78:
  case 79:
  case 81:
  case 84:
  case 87:
  case 89:
  case 100:
  case 102:
  case 107:
  case 108:
  case 110:
  case 111:
  case 112:
  case 113:
  case 115:
  case 116:
  case 119:
  case 123:
  case 133:
    return true;

  default:
    return false;
  }
}

class MatrixPushScope {
public:
  MatrixPushScope(
      void *stack,
      ItemPhysicsRuntime::MatrixPushFn push,
      ItemPhysicsRuntime::MatrixRefDtorFn dtor)
      : mDtor(dtor) {

    if (stack &&
        push &&
        dtor) {

      mRef =
          push(
              stack,
              false);

      mActive =
          mRef.stack != nullptr &&
          mRef.mat != nullptr;
    }
  }

  MatrixPushScope(
      const MatrixPushScope &) = delete;

  MatrixPushScope &
  operator=(
      const MatrixPushScope &) = delete;

  ~MatrixPushScope() {

    if (mActive &&
        mDtor) {

      mDtor(
          &mRef);

      mRef.stack =
          nullptr;

      mRef.mat =
          nullptr;
    }
  }

  [[nodiscard]]
  Mat4 *matrix() noexcept {

    return mActive
               ? mRef.mat
               : nullptr;
  }

private:
  ItemPhysicsRuntime::MatrixStackRefAbi
      mRef{};

  ItemPhysicsRuntime::MatrixRefDtorFn
      mDtor{};

  bool
      mActive{};
};

} // namespace

ItemPhysicsRuntime *
ItemPhysicsRuntime::sInstance =
    nullptr;

void ItemPhysicsRuntime::applyConfig(
    const ItemPhysicsConfig &config) noexcept {

  mEnabled.store(
      config.enabled,
      std::memory_order_relaxed);

  mSingleModel.store(
      config.singleModel,
      std::memory_order_relaxed);

  mHideItemShadow.store(
      config.hideItemShadow,
      std::memory_order_relaxed);

  mRotationSpeed.store(
      static_cast<float>(
          config.rotationSpeed),
      std::memory_order_relaxed);

  mSettleSpeed.store(
      static_cast<float>(
          config.settleSpeed),
      std::memory_order_relaxed);

  mGroundTiltDeg.store(
      static_cast<float>(
          config.groundTilt),
      std::memory_order_relaxed);

  mHeightOffset.store(
      static_cast<float>(
          config.heightOffset),
      std::memory_order_relaxed);

  mBlockGroundHeight.store(
      static_cast<float>(
          config.blockGroundHeight),
      std::memory_order_relaxed);

  mThinBlockGroundHeight.store(
      static_cast<float>(
          config.thinBlockGroundHeight),
      std::memory_order_relaxed);

  mTorchGroundHeight.store(
      static_cast<float>(
          config.torchGroundHeight),
      std::memory_order_relaxed);

  mShapedBlockGroundHeight.store(
      static_cast<float>(
          config.shapedBlockGroundHeight),
      std::memory_order_relaxed);

  mSkullGroundHeight.store(
      static_cast<float>(
          config.skullGroundHeight),
      std::memory_order_relaxed);

  mShieldGroundHeight.store(
      static_cast<float>(
          config.shieldGroundHeight),
      std::memory_order_relaxed);

  mBannerGroundHeight.store(
      static_cast<float>(
          config.bannerGroundHeight),
      std::memory_order_relaxed);
}

bool ItemPhysicsRuntime::verifyProfile(
    const ResolvedVirtual &resolved,
    ll::mod::NativeMod &mod) const {

  const auto verifyWords =
      [&](
          std::uintptr_t address,
          const auto &fingerprint,
          const char *name) {

        const std::size_t bytes =
            fingerprint.size() *
            sizeof(std::uint32_t);

        if (!resolved.module.readable(
                address,
                bytes)) {

          mod.getLogger().warn(
              "{} is not readable",
              name);

          return false;
        }

        const auto *words =
            reinterpret_cast<
                const std::uint32_t *>(
                address);

        for (std::size_t i = 0;
             i <
             fingerprint.size();
             ++i) {

          if (words[i] !=
              fingerprint[i]) {

            mod.getLogger().warn(
                "Minecraft profile mismatch: "
                "{} word {}",
                name,
                i);

            return false;
          }
        }

        return true;
      };

  if (!verifyWords(
          resolved.target,
          profile::kRenderFingerprint,
          "ItemRenderer::render")) {

    return false;
  }

  if (!verifyWords(
          resolved.module.base +
              profile::
                  kGetBlockTypeForRenderingRva,

          profile::
              kGetBlockTypeForRenderingFingerprint,

          "ItemStackBase::getBlockTypeForRendering")) {

    return false;
  }

  if (!verifyWords(
          resolved.module.base +
              profile::
                  kBlockGraphicsGetForBlockTypeRva,

          profile::
              kBlockGraphicsGetForBlockTypeFingerprint,

          "BlockGraphics::getForBlock(BlockType)")) {

    return false;
  }

  if (!verifyWords(
          resolved.module.base +
              profile::
                  kBlockGraphicsGetBlockShapeRva,

          profile::
              kBlockGraphicsGetBlockShapeFingerprint,

          "BlockGraphics::getBlockShape")) {

    return false;
  }

  if (!verifyWords(
          resolved.module.base +
              profile::
                  kRelativeShadowStorageRva,

          profile::
              kRelativeShadowStorageFingerprint,

          "RelativeShadowOffsetComponent storage")) {

    return false;
  }

  if (!verifyWords(
          resolved.module.base +
              profile::
                  kRelativeShadowEmplaceRva,

          profile::
              kRelativeShadowEmplaceFingerprint,

          "RelativeShadowOffsetComponent emplace")) {

    return false;
  }

  const auto executable =
      [&](std::uintptr_t rva) {

        return resolved.module.executable(
            resolved.module.base +
            rva);
      };

  if (!executable(
          profile::
              kGetWorldMatrixRva) ||

      !executable(
          profile::
              kMatrixStackPushRva) ||

      !executable(
          profile::
              kMatrixStackRefDtorRva) ||

      !executable(
          profile::
              kGetBlockTypeForRenderingRva) ||

      !executable(
          profile::
              kBlockGraphicsGetForBlockTypeRva) ||

      !executable(
          profile::
              kBlockGraphicsGetForBlockRva) ||

      !executable(
          profile::
              kBlockGraphicsGetBlockShapeRva) ||

      !executable(
          profile::
              kRelativeShadowStorageRva) ||

      !executable(
          profile::
              kRelativeShadowEmplaceRva)) {

    mod.getLogger().warn(
        "Minecraft 1.26.45 renderer helper validation failed");

    return false;
  }

  return true;
}

bool ItemPhysicsRuntime::install(
    ll::mod::NativeMod &mod) {

  uninstall();

  auto resolved =
      resolveVirtualByRtti(
          profile::
              kMinecraftModule,

          profile::
              kItemRendererRtti,

          profile::
              kItemRendererRenderVtableOffset);

  if (!resolved) {

    mod.getLogger().warn(
        "Item Physics inactive: "
        "failed to resolve ItemRenderer");

    return false;
  }

  if (!verifyProfile(
          *resolved,
          mod)) {

    mod.getLogger().warn(
        "Item Physics safe passthrough: "
        "Minecraft profile unsupported");

    return false;
  }

  mMinecraftBase =
      resolved->module.base;

  mRenderTarget =
      resolved->target;

  mGetWorldMatrix =
      reinterpret_cast<
          GetWorldMatrixFn>(

          mMinecraftBase +
          profile::
              kGetWorldMatrixRva);

  mMatrixPush =
      reinterpret_cast<
          MatrixPushFn>(

          mMinecraftBase +
          profile::
              kMatrixStackPushRva);

  mMatrixRefDtor =
      reinterpret_cast<
          MatrixRefDtorFn>(

          mMinecraftBase +
          profile::
              kMatrixStackRefDtorRva);

  mGetBlockTypeForRendering =
      reinterpret_cast<
          GetBlockTypeForRenderingFn>(

          mMinecraftBase +
          profile::
              kGetBlockTypeForRenderingRva);

  mGetBlockGraphicsForBlockType =
      reinterpret_cast<
          BlockGraphicsGetForBlockTypeFn>(

          mMinecraftBase +
          profile::
              kBlockGraphicsGetForBlockTypeRva);

  mGetBlockGraphicsForBlock =
      reinterpret_cast<
          BlockGraphicsGetForBlockFn>(

          mMinecraftBase +
          profile::
              kBlockGraphicsGetForBlockRva);

  mGetBlockGraphicsShape =
      reinterpret_cast<
          BlockGraphicsGetBlockShapeFn>(

          mMinecraftBase +
          profile::
              kBlockGraphicsGetBlockShapeRva);

  mGetRelativeShadowStorage =
      reinterpret_cast<
          RelativeShadowStorageFn>(

          mMinecraftBase +
          profile::
              kRelativeShadowStorageRva);

  mEmplaceRelativeShadow =
      reinterpret_cast<
          RelativeShadowEmplaceFn>(

          mMinecraftBase +
          profile::
              kRelativeShadowEmplaceRva);

  sInstance =
      this;

  mOriginal =
      nullptr;

  mHook =
      std::make_unique<
          pl::memory::HookHandle>(

          reinterpret_cast<void *>(
              mRenderTarget),

          reinterpret_cast<void *>(
              &ItemPhysicsRuntime::
                  renderDetour),

          reinterpret_cast<void **>(
              &mOriginal),

          pl::memory::
              HookPriority::Normal);

  if (!mHook->installed() ||
      !mOriginal) {

    mod.getLogger().error(
        "Failed to hook ItemRenderer::render");

    mHook.reset();

    sInstance =
        nullptr;

    return false;
  }

  mProfileSupported.store(
      true,
      std::memory_order_relaxed);

  mod.getLogger().info(
      "Item Physics active: "
      "Minecraft 1.26.45 + Ground Height V4 + Atlas Item Shadow");

  return true;
}

void ItemPhysicsRuntime::uninstall() {

  mProfileSupported.store(
      false,
      std::memory_order_relaxed);

  if (mHook) {

    mHook->reset();

    mHook.reset();
  }

  if (sInstance ==
      this) {

    sInstance =
        nullptr;
  }

  mOriginal =
      nullptr;

  mGetWorldMatrix =
      nullptr;

  mMatrixPush =
      nullptr;

  mMatrixRefDtor =
      nullptr;

  mGetBlockTypeForRendering =
      nullptr;

  mGetBlockGraphicsForBlockType =
      nullptr;

  mGetBlockGraphicsForBlock =
      nullptr;

  mGetBlockGraphicsShape =
      nullptr;

  mGetRelativeShadowStorage =
      nullptr;

  mEmplaceRelativeShadow =
      nullptr;

  mRenderTarget =
      0;

  mMinecraftBase =
      0;

  clearStates();
}

void ItemPhysicsRuntime::clearStates() {

  std::lock_guard lock(
      mStateMutex);

  mStates.clear();

  mRenderCounter =
      0;
}

void ItemPhysicsRuntime::renderDetour(
    void *self,
    void *renderContext,
    void *renderData) {

  if (sInstance) {

    sInstance->onRender(
        self,
        renderContext,
        renderData);
  }
}

float ItemPhysicsRuntime::seededUnit(
    std::uint32_t seed) noexcept {

  seed ^=
      seed << 13;

  seed ^=
      seed >> 17;

  seed ^=
      seed << 5;

  return static_cast<float>(
             seed &
             0x00FFFFFFu) /
         16777215.0f;
}

float ItemPhysicsRuntime::wrapPi(
    float value) noexcept {

  while (value >
         kPi) {

    value -=
        2.0f *
        kPi;
  }

  while (value <
         -kPi) {

    value +=
        2.0f *
        kPi;
  }

  return value;
}

float ItemPhysicsRuntime::approachAngle(
    float current,
    float target,
    float alpha) noexcept {

  const float delta =
      wrapPi(
          target -
          current);

  return wrapPi(
      current +
      delta *
          std::clamp(
              alpha,
              0.0f,
              1.0f));
}

bool ItemPhysicsRuntime::libcxxStringEquals(
    std::uintptr_t stringAddress,
    std::string_view wanted) noexcept {

  if (!stringAddress) {

    return false;
  }

  const auto *raw =
      reinterpret_cast<
          const std::uint8_t *>(
          stringAddress);

  const std::uint8_t flag =
      raw[0];

  std::size_t length =
      0;

  const char *data =
      nullptr;

  if ((flag &
       1u) ==
      0) {

    length =
        static_cast<
            std::size_t>(
            flag >>
            1);

    data =
        reinterpret_cast<
            const char *>(
            raw +
            1);

  } else {

    length =
        *reinterpret_cast<
            const std::size_t *>(
            raw +
            8);

    data =
        *reinterpret_cast<
            const char *const *>(
            raw +
            16);
  }

  return data &&
         length ==
             wanted.size() &&

         std::memcmp(
             data,
             wanted.data(),
             length) ==
             0;
}

bool ItemPhysicsRuntime::tryGetRenderBlockShape(
    std::uintptr_t actorAddress,
    std::int32_t &shape) const noexcept {

  shape =
      -1;

  if (!actorAddress ||
      !mGetBlockTypeForRendering ||
      !mGetBlockGraphicsForBlockType ||
      !mGetBlockGraphicsShape) {

    return false;
  }

  const void *itemStackBase =
      reinterpret_cast<
          const void *>(

          actorAddress +
          profile::
              kItemStackBaseOffset);

  const void *weakPtrAddress =
      mGetBlockTypeForRendering(
          itemStackBase);

  if (!weakPtrAddress) {

    return false;
  }

  const auto counter =
      *reinterpret_cast<
          const std::uintptr_t *>(
          weakPtrAddress);

  if (!counter) {

    return false;
  }

  const auto blockType =
      *reinterpret_cast<
          const std::uintptr_t *>(
          counter);

  if (!blockType) {

    return false;
  }

  const void *graphics =
      mGetBlockGraphicsForBlockType(

          reinterpret_cast<
              const void *>(
              blockType));

  if (!graphics) {

    return false;
  }

  shape =
      mGetBlockGraphicsShape(
          graphics);

  return true;
}

bool ItemPhysicsRuntime::buildBlockRenderInfo(
    const void *block,
    BlockRenderInfo &info) const noexcept {

  info =
      {};

  if (!block) {

    return false;
  }

  if (mGetBlockGraphicsForBlock &&
      mGetBlockGraphicsShape) {

    const void *graphics =
        mGetBlockGraphicsForBlock(
            block);

    if (graphics) {

      info.blockShape =
          mGetBlockGraphicsShape(
              graphics);
    }
  }

  bool thinHorizontal =
      false;

  const auto blockAddress =
      reinterpret_cast<
          std::uintptr_t>(
          block);

  auto *blockType =
      *reinterpret_cast<
          void *const *>(

          blockAddress +
          profile::
              kBlockTypeOffset);

  if (blockType) {

    auto **vtable =
        *reinterpret_cast<
            void ***>(
            blockType);

    if (vtable) {

      constexpr std::size_t slot =
          profile::
              kBlockTypeGetVisualShapeVtableOffset /
          sizeof(void *);

      auto getVisualShape =
          reinterpret_cast<
              GetVisualShapeFn>(
              vtable[slot]);

      if (getVisualShape) {

        AabbAbi scratch{};

        const AabbAbi *bounds =
            getVisualShape(
                blockType,
                block,
                &scratch);

        if (bounds) {

          const float dx =
              bounds->maxX -
              bounds->minX;

          const float dy =
              bounds->maxY -
              bounds->minY;

          const float dz =
              bounds->maxZ -
              bounds->minZ;

          if (std::isfinite(dx) &&
              std::isfinite(dy) &&
              std::isfinite(dz) &&

              dx >
                  kExtentEpsilon &&

              dy >
                  kExtentEpsilon &&

              dz >
                  kExtentEpsilon) {

            const float horizontal =
                std::min(
                    dx,
                    dz);

            thinHorizontal =
                dy <=
                horizontal *
                    kThinYRatio;
          }
        }
      }
    }
  }

  const bool skull =
      info.blockShape ==
      kSkullBlockShape;

  info.keepHorizontal =
      thinHorizontal ||
      skull;

  info.valid =
      true;

  return true;
}

ItemPhysicsRuntime::ItemRenderTraits
ItemPhysicsRuntime::classifyItem(
    std::uintptr_t actorAddress) const noexcept {

  ItemRenderTraits traits{};

  std::int32_t rendererShape =
      -1;

  if (tryGetRenderBlockShape(
          actorAddress,
          rendererShape)) {

    traits.hasRenderShape =
        true;

    traits.renderShape =
        rendererShape;
  }

  if (traits.hasRenderShape &&
      traits.renderShape ==
          kSkullBlockShape) {

    traits.valid =
        true;

    traits.modelClass =
        ModelClass::
            BlockItem;

    traits.specialKind =
        SpecialKind::
            None;

    traits.block.valid =
        true;

    traits.block.blockShape =
        kSkullBlockShape;

    traits.block.keepHorizontal =
        true;

    return traits;
  }

  const auto block =
      *reinterpret_cast<
          const void *const *>(

          actorAddress +
          profile::
              kBlockPtrOffset);

  if (block) {

    traits.valid =
        true;

    traits.modelClass =
        ModelClass::
            BlockItem;

    traits.specialKind =
        SpecialKind::
            None;

    (void)buildBlockRenderInfo(
        block,
        traits.block);

    return traits;
  }

  const auto itemHandle =
      *reinterpret_cast<
          const std::uintptr_t *>(

          actorAddress +
          profile::
              kItemHandleOffset);

  if (!itemHandle) {

    return traits;
  }

  const auto item =
      *reinterpret_cast<
          const std::uintptr_t *>(
          itemHandle);

  if (!item) {

    return traits;
  }

  const auto identifier =
      item +
      profile::
          kItemIdentifierOffset;

  const bool shield =
      libcxxStringEquals(
          identifier,
          kShieldId);

  const bool banner =
      libcxxStringEquals(
          identifier,
          kBannerId);

  traits.valid =
      true;

  if (shield) {

    traits.modelClass =
        ModelClass::
            SpecialItem;

    traits.specialKind =
        SpecialKind::
            Shield;
  }

  else if (banner) {

    traits.modelClass =
        ModelClass::
            SpecialItem;

    traits.specialKind =
        SpecialKind::
            Banner;
  }

  else {

    traits.modelClass =
        ModelClass::
            FlatItem;

    traits.specialKind =
        SpecialKind::
            None;
  }

  return traits;
}

ItemPhysicsRuntime::PhysicsState &
ItemPhysicsRuntime::stateFor(
    std::uint32_t entityId,
    std::chrono::steady_clock::
        time_point now) {

  auto [it, inserted] =
      mStates.try_emplace(
          entityId);

  auto &state =
      it->second;

  if (inserted ||
      !state.initialized) {

    const float u =
        seededUnit(
            entityId ^
            0x9E3779B9u);

    const float v =
        seededUnit(
            entityId ^
            0x85EBCA6Bu);

    const float w =
        seededUnit(
            entityId ^
            0xC2B2AE35u);

    const float curve =
        std::max(
            0.0f,

            4.0f *
                    u *
                    u -

                4.0f *
                    u *
                    u *
                    u *
                    u);

    state.initialized =
        true;

    state.rotX =
        0.0f;

    state.rotY =
        0.0f;

    state.rotZ =
        0.0f;

    state.angularX =
        (v *
             2.0f -
         1.0f) *
        curve *
        kPi;

    state.angularZ =
        (w *
             2.0f -
         1.0f) *
        (1.0f -
         curve) *
        kPi;

    if (std::abs(
            state.angularX) +
            std::abs(
                state.angularZ) <
        0.20f) {

      state.angularX +=
          0.65f;
    }

    state.restYaw =
        seededUnit(
            entityId ^
            0x27D4EB2Du) *
        2.0f *
        kPi;

    state.wasGrounded =
        false;

    state.born =
        now;

    state.lastUpdate =
        now;

    state.lastSeen =
        now;
  }

  return state;
}

void ItemPhysicsRuntime::updateState(
    PhysicsState &state,
    const ItemRenderTraits &traits,
    bool grounded,
    std::chrono::steady_clock::
        time_point now) const {

  const float dt =
      std::clamp(
          std::chrono::duration<float>(
              now -
              state.lastUpdate)
              .count(),

          0.0f,
          0.20f);

  state.lastUpdate =
      now;

  state.lastSeen =
      now;

  const float frameFactor =
      std::min(
          dt *
              5.0f,
          1.0f);

  if (!grounded) {

    const float age =
        std::chrono::duration<float>(
            now -
            state.born)
            .count();

    const float ageFactor =
        std::min(
            age /
                4.0f,
            1.0f);

    const float fade =
        1.0f -
        ageFactor;

    const float speed =
        mRotationSpeed.load(
            std::memory_order_relaxed);

    const float step =
        fade *
        frameFactor *
        speed;

    state.rotX =
        wrapPi(
            state.rotX +
            state.angularX *
                step);

    state.rotZ =
        wrapPi(
            state.rotZ +
            state.angularZ *
                step);

    state.rotY =
        0.0f;

    state.wasGrounded =
        false;

    return;
  }

  const float settle =
      frameFactor *
      mSettleSpeed.load(
          std::memory_order_relaxed);

  if (traits.modelClass ==
          ModelClass::
              BlockItem &&

      traits.block.
          keepHorizontal) {

    state.rotX =
        approachAngle(
            state.rotX,
            0.0f,
            settle);

    state.rotZ =
        approachAngle(
            state.rotZ,
            0.0f,
            settle);

    state.rotY =
        approachAngle(
            state.rotY,
            state.restYaw,
            settle);
  }

  else {

    const float target =
        mGroundTiltDeg.load(
            std::memory_order_relaxed) *
        kDegToRad;

    state.rotX =
        approachAngle(
            state.rotX,
            target,
            settle);

    state.rotY =
        0.0f;
  }

  state.wasGrounded =
      true;
}

void ItemPhysicsRuntime::pruneStates(
    std::chrono::steady_clock::
        time_point now) {

  for (auto it =
           mStates.begin();

       it !=
       mStates.end();) {

    if (now -
            it->second.lastSeen >
        kStateTtl) {

      it =
          mStates.erase(
              it);

    } else {

      ++it;
    }
  }
}

void ItemPhysicsRuntime::updateItemShadowComponent(
    void *actor,
    bool grounded,
    bool hideShadow) const noexcept {

  if (!actor ||
      !mGetRelativeShadowStorage ||
      !mEmplaceRelativeShadow) {

    return;
  }

  const auto actorAddress =
      reinterpret_cast<
          std::uintptr_t>(
          actor);

  auto *registry =
      *reinterpret_cast<
          void **>(

          actorAddress +
          profile::
              kActorRegistryOffset);

  const auto entityId =
      *reinterpret_cast<
          const std::uint32_t *>(

          actorAddress +
          profile::
              kActorEntityIdOffset);

  if (!registry) {

    return;
  }

  void *storage =
      mGetRelativeShadowStorage(
          registry,
          profile::
              kRelativeShadowOffsetComponentHash);

  if (!storage) {

    return;
  }

  const float wanted =
      hideShadow

          ? std::numeric_limits<float>::max()

          : (grounded
                 ? 0.0f
                 : -0.5f);

  const auto storageAddress =
      reinterpret_cast<
          std::uintptr_t>(
          storage);

  const auto pagesBegin =
      *reinterpret_cast<
          const std::uintptr_t *>(
          storageAddress +
          0x08);

  const auto pagesEnd =
      *reinterpret_cast<
          const std::uintptr_t *>(
          storageAddress +
          0x10);

  bool present =
      false;

  std::uint32_t packedEntity =
      0;

  if (pagesBegin &&
      pagesEnd >=
          pagesBegin) {

    const auto pageCount =
        (pagesEnd -
         pagesBegin) /
        sizeof(
            std::uintptr_t);

    const auto pageIndex =
        (entityId >>
         11) &
        0x7Fu;

    if (pageIndex <
        pageCount) {

      const auto sparsePage =
          *reinterpret_cast<
              const std::uintptr_t *>(

              pagesBegin +
              pageIndex *
                  sizeof(
                      std::uintptr_t));

      if (sparsePage) {

        const auto sparseSlot =
            entityId &
            0x7FFu;

        packedEntity =
            *reinterpret_cast<
                const std::uint32_t *>(

                sparsePage +
                sparseSlot *
                    sizeof(
                        std::uint32_t));

        const auto generation =
            entityId &
            0xFFFC0000u;

        present =
            (packedEntity ^
             generation) <=
            0x3FFFEu;
      }
    }
  }

  if (!present) {

    const std::uint32_t entityCopy =
        entityId;

    (void)mEmplaceRelativeShadow(
        storage,
        &entityCopy,
        false,
        &wanted);

    return;
  }

  const auto denseIndex =
      packedEntity &
      0x3FFFFu;

  const auto componentPages =
      *reinterpret_cast<
          const std::uintptr_t *>(
          storageAddress +
          0x50);

  if (!componentPages) {

    return;
  }

  const auto componentPageIndex =
      denseIndex >>
      7;

  const auto componentPage =
      *reinterpret_cast<
          const std::uintptr_t *>(

          componentPages +
          componentPageIndex *
              sizeof(
                  std::uintptr_t));

  if (!componentPage) {

    return;
  }

  const auto componentSlot =
      denseIndex &
      0x7Fu;

  *reinterpret_cast<float *>(
      componentPage +
      componentSlot *
          sizeof(float)) =
      wanted;
}

bool ItemPhysicsRuntime::
    hasOnGroundComponent(
        void *actor) const noexcept {

  if (!actor) {

    return false;
  }

  const auto actorAddress =
      reinterpret_cast<
          std::uintptr_t>(
          actor);

  const auto registry =
      *reinterpret_cast<
          const std::uintptr_t *>(

          actorAddress +
          profile::
              kActorRegistryOffset);

  const auto entityId =
      *reinterpret_cast<
          const std::uint32_t *>(

          actorAddress +
          profile::
              kActorEntityIdOffset);

  if (!registry) {

    return false;
  }

  const auto bucketsBegin =
      *reinterpret_cast<
          const std::uintptr_t *>(
          registry +
          0x38);

  const auto bucketsEnd =
      *reinterpret_cast<
          const std::uintptr_t *>(
          registry +
          0x40);

  const auto nodesBase =
      *reinterpret_cast<
          const std::uintptr_t *>(
          registry +
          0x50);

  const auto sentinel =
      *reinterpret_cast<
          const std::uintptr_t *>(
          registry +
          0x58);

  if (!bucketsBegin ||
      !bucketsEnd ||
      bucketsEnd <=
          bucketsBegin ||
      !nodesBase) {

    return false;
  }

  const auto bucketBytes =
      bucketsEnd -
      bucketsBegin;

  if ((bucketBytes %
       sizeof(
           std::uintptr_t)) !=
          0 ||

      bucketBytes /
              sizeof(
                  std::uintptr_t) >
          (1u << 20)) {

    return false;
  }

  const auto bucketCount =
      bucketBytes /
      sizeof(
          std::uintptr_t);

  if (bucketCount ==
      0) {

    return false;
  }

  const auto index =
      (bucketCount -
       1) &
      profile::
          kOnGroundFlagComponentHash;

  std::int64_t nodeIndex =
      *reinterpret_cast<
          const std::int64_t *>(

          bucketsBegin +

          index *
              sizeof(
                  std::uintptr_t));

  std::uintptr_t node =
      0;

  for (int guard = 0;

       nodeIndex != -1 &&
       guard <
           4096;

       ++guard) {

    node =
        nodesBase +

        static_cast<
            std::uintptr_t>(
            nodeIndex) *
            32u;

    if (*reinterpret_cast<
            const std::uint32_t *>(
            node +
            8) ==

        profile::
            kOnGroundFlagComponentHash) {

      break;
    }

    nodeIndex =
        *reinterpret_cast<
            const std::int64_t *>(
            node);

    node =
        0;
  }

  if (!node ||
      node ==
          sentinel) {

    return false;
  }

  const auto storage =
      *reinterpret_cast<
          const std::uintptr_t *>(
          node +
          0x10);

  if (!storage) {

    return false;
  }

  const auto pagesBegin =
      *reinterpret_cast<
          const std::uintptr_t *>(
          storage +
          0x8);

  const auto pagesEnd =
      *reinterpret_cast<
          const std::uintptr_t *>(
          storage +
          0x10);

  if (!pagesBegin ||
      !pagesEnd ||
      pagesEnd <
          pagesBegin) {

    return false;
  }

  const auto pageCount =
      (pagesEnd -
       pagesBegin) /
      sizeof(
          std::uintptr_t);

  const auto pageIndex =
      (entityId >>
       11) &
      0x7Fu;

  if (pageIndex >=
      pageCount) {

    return false;
  }

  const auto page =
      *reinterpret_cast<
          const std::uintptr_t *>(

          pagesBegin +

          pageIndex *
              sizeof(
                  std::uintptr_t));

  if (!page) {

    return false;
  }

  const auto slot =
      entityId &
      0x7FFu;

  const auto generation =
      entityId &
      0xFFFC0000u;

  const auto slotValue =
      *reinterpret_cast<
          const std::uint32_t *>(

          page +

          slot *
              sizeof(
                  std::uint32_t));

  return (
             slotValue ^
             generation) <=
         0x3FFFEu;
}

void ItemPhysicsRuntime::onRender(
    void *self,
    void *renderContext,
    void *renderData) {

  const auto original =
      mOriginal;

  if (!original) {

    return;
  }

  if (!renderContext ||
      !renderData ||

      !mProfileSupported.load(
          std::memory_order_relaxed)) {

    original(
        self,
        renderContext,
        renderData);

    return;
  }

  const auto renderDataAddress =
      reinterpret_cast<
          std::uintptr_t>(
          renderData);

  auto *actor =
      *reinterpret_cast<
          void **>(

          renderDataAddress +
          profile::
              kRenderDataActorOffset);

  if (!actor) {

    original(
        self,
        renderContext,
        renderData);

    return;
  }

  const auto actorAddress =
      reinterpret_cast<
          std::uintptr_t>(
          actor);

  const bool grounded =
      hasOnGroundComponent(
          actor);

  const bool enabled =
      mEnabled.load(
          std::memory_order_relaxed);

  const bool hideShadow =
      enabled &&
      mHideItemShadow.load(
          std::memory_order_relaxed);

  updateItemShadowComponent(
      actor,
      grounded,
      hideShadow);

  if (!enabled) {

    original(
        self,
        renderContext,
        renderData);

    return;
  }

  const auto traits =
      classifyItem(
          actorAddress);

  if (!traits.valid) {

    original(
        self,
        renderContext,
        renderData);

    return;
  }

  const auto entityId =
      *reinterpret_cast<
          const std::uint32_t *>(

          actorAddress +
          profile::
              kActorEntityIdOffset);

  PhysicsState snapshot;

  {
    std::lock_guard lock(
        mStateMutex);

    const auto now =
        std::chrono::
            steady_clock::now();

    auto &state =
        stateFor(
            entityId,
            now);

    updateState(
        state,
        traits,
        grounded,
        now);

    snapshot =
        state;

    if ((++mRenderCounter &
         0xFFu) ==
        0) {

      pruneStates(
          now);
    }
  }

  auto &count =
      *reinterpret_cast<
          std::uint8_t *>(

          actorAddress +
          profile::
              kItemCountOffset);

  auto &inItemFrame =
      *reinterpret_cast<
          std::uint8_t *>(

          actorAddress +
          profile::
              kIsInItemFrameOffset);

  const auto oldCount =
      count;

  const auto oldInItemFrame =
      inItemFrame;

  if (mSingleModel.load(
          std::memory_order_relaxed)) {

    count =
        1;
  }

  inItemFrame =
      1;

  auto *position =
      reinterpret_cast<
          float *>(

          renderDataAddress +
          profile::
              kRenderDataPositionOffset);

  const float oldY =
      position[1];

  const bool ordinaryItem =
      traits.modelClass ==
      ModelClass::
          FlatItem;

  if (ordinaryItem) {

    position[1] =
        oldY +
        mHeightOffset.load(
            std::memory_order_relaxed);
  }

  if (grounded) {

    if (traits.modelClass ==
        ModelClass::
            SpecialItem) {

      switch (
          traits.specialKind) {

      case SpecialKind::Shield:

        position[1] +=
            mShieldGroundHeight.load(
                std::memory_order_relaxed);

        break;

      case SpecialKind::Banner:

        position[1] +=
            mBannerGroundHeight.load(
                std::memory_order_relaxed);

        break;

      case SpecialKind::None:
      default:

        break;
      }
    }

    else {

      std::int32_t groundShape =
          -1;

      if (traits.hasRenderShape) {

        groundShape =
            traits.renderShape;
      }

      else if (
          traits.modelClass ==
              ModelClass::
                  BlockItem) {

        groundShape =
            traits.block.blockShape;
      }

      if (groundShape >=
          0) {

        if (groundShape ==
            kSkullBlockShape) {

          position[1] +=
              mSkullGroundHeight.load(
                  std::memory_order_relaxed);
        }

        else if (
            (traits.modelClass ==
                 ModelClass::
                     BlockItem &&

             traits.block.
                 keepHorizontal) ||

            isThinGroundShape(
                groundShape)) {

          position[1] +=
              mThinBlockGroundHeight.load(
                  std::memory_order_relaxed);
        }

        else if (
            isTorchGroundShape(
                groundShape)) {

          position[1] +=
              mTorchGroundHeight.load(
                  std::memory_order_relaxed);
        }

        else if (
            isShapedGroundShape(
                groundShape)) {

          position[1] +=
              mShapedBlockGroundHeight.load(
                  std::memory_order_relaxed);
        }

        else {

          position[1] +=
              mBlockGroundHeight.load(
                  std::memory_order_relaxed);
        }
      }
    }
  }

  void *stack =
      mGetWorldMatrix

          ? mGetWorldMatrix(
                renderContext)

          : nullptr;

  {
    MatrixPushScope matrixScope(
        stack,
        mMatrixPush,
        mMatrixRefDtor);

    auto *matrix =
        matrixScope.matrix();

    if (!matrix) {

      position[1] =
          oldY;

      count =
          oldCount;

      inItemFrame =
          oldInItemFrame;

      original(
          self,
          renderContext,
          renderData);

      return;
    }

    const float x =
        position[0];

    const float y =
        position[1];

    const float z =
        position[2];

    postTranslate(
        *matrix,
        x,
        y,
        z);

    postRotateX(
        *matrix,
        snapshot.rotX);

    postRotateY(
        *matrix,
        snapshot.rotY);

    postRotateZ(
        *matrix,
        snapshot.rotZ);

    if (ordinaryItem) {

      postTranslate(
          *matrix,
          0.0f,
          kAtlasFlatLocalY,
          0.0f);
    }

    postTranslate(
        *matrix,
        -x,
        -y,
        -z);

    original(
        self,
        renderContext,
        renderData);
  }

  position[1] =
      oldY;

  count =
      oldCount;

  inItemFrame =
      oldInItemFrame;
}

} // namespace itemphysics
