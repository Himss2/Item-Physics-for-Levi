#include "ItemPhysicsRuntime.hpp"
#include "TargetProfile.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string_view>

namespace itemphysics {
namespace {

constexpr float kPi =
    3.14159265358979323846f;

constexpr float kDegToRad =
    kPi / 180.0f;

constexpr auto kStateTtl =
    std::chrono::seconds(8);

constexpr float kThinYRatio =
    0.70f;

constexpr float kExtentEpsilon =
    0.0005f;

constexpr std::string_view
    kShieldId =
        "minecraft:shield";

constexpr std::string_view
    kBannerId =
        "minecraft:banner";

// ============================================================
// MatrixStack RAII
// ============================================================

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

thread_local
    ItemPhysicsRuntime::
        HelperOverrideContext
            ItemPhysicsRuntime::
                sHelperOverride{};

// ============================================================
// Config
// ============================================================

void ItemPhysicsRuntime::applyConfig(
    const ItemPhysicsConfig &config) noexcept {

  mEnabled.store(
      config.enabled,
      std::memory_order_relaxed);

  mSingleModel.store(
      config.singleModel,
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
}

// ============================================================
// Profile verification
// ============================================================

bool ItemPhysicsRuntime::verifyProfile(
    const ResolvedVirtual &resolved,
    ll::mod::NativeMod &mod) const {

  const auto verifyWords =
      [&](
          std::uintptr_t address,
          const auto &fingerprint,
          std::string_view label) {

        const std::size_t bytes =
            fingerprint.size() *
            sizeof(std::uint32_t);

        if (!resolved.module.readable(
                address,
                bytes)) {

          mod.getLogger().warn(
              "{} is not readable",
              label);

          return false;
        }

        const auto *words =
            reinterpret_cast<
                const std::uint32_t *>(
                address);

        for (std::size_t i = 0;
             i < fingerprint.size();
             ++i) {

          if (words[i] !=
              fingerprint[i]) {

            mod.getLogger().warn(
                "Unsupported Minecraft profile: "
                "{} fingerprint mismatch at word {}",
                label,
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

  const auto helperTarget =
      resolved.module.base +
      profile::
          kItemRendererRenderHelperRva;

  if (!verifyWords(
          helperTarget,
          profile::kRenderHelperFingerprint,
          "ItemRenderer render helper")) {

    return false;
  }

  const auto executable =
      [&](std::uintptr_t rva) {

        return resolved.module.executable(
            resolved.module.base +
            rva);
      };

  if (!executable(
          profile::kGetWorldMatrixRva) ||

      !executable(
          profile::kMatrixStackPushRva) ||

      !executable(
          profile::kMatrixStackRefDtorRva) ||

      !executable(
          profile::kItemRendererRenderHelperRva) ||

      !executable(
          profile::kBlockGraphicsGetForBlockRva) ||

      !executable(
          profile::kBlockGraphicsGetBlockShapeRva)) {

    mod.getLogger().warn(
        "Unsupported Minecraft profile: "
        "renderer helper RVA validation failed");

    return false;
  }

  return true;
}

// ============================================================
// Install
// ============================================================

bool ItemPhysicsRuntime::install(
    ll::mod::NativeMod &mod) {

  uninstall();

  auto resolved =
      resolveVirtualByRtti(
          profile::kMinecraftModule,
          profile::kItemRendererRtti,
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
        "Item Physics remains in passthrough mode");

    return false;
  }

  mMinecraftBase =
      resolved->module.base;

  mRenderTarget =
      resolved->target;

  mRenderHelperTarget =
      mMinecraftBase +
      profile::
          kItemRendererRenderHelperRva;

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

  sInstance =
      this;

  mOriginal =
      nullptr;

  mOriginalHelper =
      nullptr;

  // ==========================================================
  // Hook helper FIRST.
  //
  // Outer ItemRenderer hook depends on this.
  // ==========================================================

  mHelperHook =
      std::make_unique<
          pl::memory::HookHandle>(

          reinterpret_cast<void *>(
              mRenderHelperTarget),

          reinterpret_cast<void *>(
              &ItemPhysicsRuntime::
                  renderHelperDetour),

          reinterpret_cast<void **>(
              &mOriginalHelper),

          pl::memory::
              HookPriority::Normal);

  if (!mHelperHook->installed() ||
      !mOriginalHelper) {

    mod.getLogger().error(
        "Failed to hook ItemRenderer private render helper");

    mHelperHook.reset();

    sInstance =
        nullptr;

    return false;
  }

  // ==========================================================
  // Main ItemRenderer hook
  // ==========================================================

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

    mHelperHook->reset();
    mHelperHook.reset();

    mOriginalHelper =
        nullptr;

    sInstance =
        nullptr;

    return false;
  }

  mProfileSupported.store(
      true,
      std::memory_order_relaxed);

  mod.getLogger().info(
      "Item Physics active: "
      "render=base+0x{:X}, helper=base+0x{:X}",

      static_cast<
          unsigned long long>(
          mRenderTarget -
          mMinecraftBase),

      static_cast<
          unsigned long long>(
          mRenderHelperTarget -
          mMinecraftBase));

  return true;
}

// ============================================================
// Uninstall
// ============================================================

void ItemPhysicsRuntime::uninstall() {

  mProfileSupported.store(
      false,
      std::memory_order_relaxed);

  if (mHook) {

    mHook->reset();
    mHook.reset();
  }

  if (mHelperHook) {

    mHelperHook->reset();
    mHelperHook.reset();
  }

  if (sInstance ==
      this) {

    sInstance =
        nullptr;
  }

  mOriginal =
      nullptr;

  mOriginalHelper =
      nullptr;

  mGetWorldMatrix =
      nullptr;

  mMatrixPush =
      nullptr;

  mMatrixRefDtor =
      nullptr;

  mGetBlockGraphicsForBlock =
      nullptr;

  mGetBlockGraphicsShape =
      nullptr;

  mRenderTarget =
      0;

  mRenderHelperTarget =
      0;

  mMinecraftBase =
      0;

  sHelperOverride =
      {};

  clearStates();
}

// ============================================================
// State reset
// ============================================================

void ItemPhysicsRuntime::clearStates() {

  std::lock_guard lock(
      mStateMutex);

  mStates.clear();

  mRenderCounter =
      0;
}

// ============================================================
// Main detour
// ============================================================

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

// ============================================================
// Private helper detour
// ============================================================

void ItemPhysicsRuntime::renderHelperDetour(
    void *self,
    void *renderContext,
    void *itemStack,
    void *itemActor,
    void *block,
    std::int32_t blockShape,
    std::int32_t modelCount,
    float partialTick) {

  if (sInstance) {

    sInstance->onRenderHelper(
        self,
        renderContext,
        itemStack,
        itemActor,
        block,
        blockShape,
        modelCount,
        partialTick);
  }
}

// ============================================================
// Private helper body
// ============================================================

void ItemPhysicsRuntime::onRenderHelper(
    void *self,
    void *renderContext,
    void *itemStack,
    void *itemActor,
    void *block,
    std::int32_t blockShape,
    std::int32_t modelCount,
    float partialTick) {

  const auto original =
      mOriginalHelper;

  if (!original) {
    return;
  }

  if (!sHelperOverride.active ||
      !itemActor ||
      itemActor !=
          sHelperOverride.actor) {

    original(
        self,
        renderContext,
        itemStack,
        itemActor,
        block,
        blockShape,
        modelCount,
        partialTick);

    return;
  }

  auto &inItemFrame =
      *reinterpret_cast<
          std::uint8_t *>(

          reinterpret_cast<
              std::uintptr_t>(
              itemActor) +

          profile::
              kIsInItemFrameOffset);

  const auto mainValue =
      inItemFrame;

  // ==========================================================
  // CRITICAL FIX
  //
  // ItemRenderer MAIN already saw:
  //
  // mIsInItemFrame = true
  //
  // therefore bob/spin has already been skipped.
  //
  // Now restore the real value ONLY while the private helper
  // executes.
  //
  // This restores:
  //
  // - Skull -0.125 correction
  // - torch/lever special transforms
  // - normal block item transforms
  // - normal dropped-item display context
  //
  // without restoring vanilla bob/spin.
  // ==========================================================

  inItemFrame =
      sHelperOverride.
          originalItemFrame;

  original(
      self,
      renderContext,
      itemStack,
      itemActor,
      block,
      blockShape,
      modelCount,
      partialTick);

  inItemFrame =
      mainValue;
}

// ============================================================
// Math
// ============================================================

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

// ============================================================
// libc++ string
// ============================================================

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

// ============================================================
// Horizontal block shapes
// ============================================================

bool ItemPhysicsRuntime::
    blockShapeShouldRemainHorizontal(
        std::int32_t shape) noexcept {

  switch (shape) {

  case 9:
    // rail

  case 14:
    // bed

  case 15:
    // diode

  case 23:
    // lilypad

  case 67:
    // slab / block_half

  case 68:
    // top snow

  case 69:
    // tripwire

  case 72:
    // repeater

  case 73:
    // comparator

  case 80:
    // end portal

  case 83:
    // skull / head

  case 96:
    // coral fan

  case 99:
    // trapdoor

  case 114:
    // campfire

  case 126:
    // sculk sensor

  case 135:
    // glow lichen

  case 136:
    // redstone wire

  case 153:
    // carpet-style / pale moss

    return true;

  default:

    return false;
  }
}

// ============================================================
// Block info
// ============================================================

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

  bool thinY =
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

            thinY =
                dy <=
                std::min(
                    dx,
                    dz) *
                    kThinYRatio;
          }
        }
      }
    }
  }

  info.keepHorizontal =
      thinY ||

      blockShapeShouldRemainHorizontal(
          info.blockShape);

  info.valid =
      true;

  return true;
}

// ============================================================
// Classification
// ============================================================

ItemPhysicsRuntime::ItemRenderTraits
ItemPhysicsRuntime::classifyItem(
    std::uintptr_t actorAddress) const noexcept {

  ItemRenderTraits traits{};

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
            BlockModel;

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

  traits.modelClass =
      (shield ||
       banner)

          ? ModelClass::
                Special3D

          : ModelClass::
                FlatItem;

  return traits;
}

// ============================================================
// Physics state
// ============================================================

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

    state.restYaw =
        seededUnit(
            entityId ^
            0x27D4EB2Du) *
        2.0f *
        kPi;

    if (std::abs(
            state.angularX) +
            std::abs(
                state.angularZ) <
        0.20f) {

      state.angularX +=
          0.65f;
    }

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

// ============================================================
// Physics update
// ============================================================

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

    const float fade =
        1.0f -
        std::min(
            age /
                4.0f,
            1.0f);

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
              BlockModel &&

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

  } else {

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

    // rotZ remains the landing direction.
  }

  state.wasGrounded =
      true;
}

// ============================================================
// State cleanup
// ============================================================

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

// ============================================================
// OnGroundFlagComponent
// ============================================================

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
       guard < 4096;

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

// ============================================================
// Main ItemRenderer body
// ============================================================

void ItemPhysicsRuntime::onRender(
    void *self,
    void *renderContext,
    void *renderData) {

  const auto original =
      mOriginal;

  if (!original) {
    return;
  }

  if (!mEnabled.load(
          std::memory_order_relaxed) ||

      !renderContext ||
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

  const bool grounded =
      hasOnGroundComponent(
          actor);

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

  const auto previousOverride =
      sHelperOverride;

  if (mSingleModel.load(
          std::memory_order_relaxed)) {

    count =
        1;
  }

  // ==========================================================
  // SPLIT FLAG
  //
  // Outer ItemRenderer:
  //
  // TRUE
  // → skip bob/spin
  //
  // Private helper:
  //
  // ORIGINAL VALUE
  // → retain correct model transforms
  // ==========================================================

  inItemFrame =
      1;

  sHelperOverride.active =
      true;

  sHelperOverride.actor =
      actor;

  sHelperOverride.originalItemFrame =
      oldInItemFrame;

  auto *position =
      reinterpret_cast<
          float *>(

          renderDataAddress +
          profile::
              kRenderDataPositionOffset);

  const float oldY =
      position[1];

  // ==========================================================
  // Old Atlas correction removed.
  //
  // No:
  // -0.38
  // +0.25
  //
  // Vanilla helper now gives us normal dropped-item transforms.
  //
  // heightOffset remains only as optional fine adjustment.
  // Default after migration: 0.
  // ==========================================================

  if (traits.modelClass ==
      ModelClass::
          FlatItem) {

    position[1] =
        oldY +
        mHeightOffset.load(
            std::memory_order_relaxed);
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

    if (matrix) {

      const float x =
          position[0];

      const float y =
          position[1];

      const float z =
          position[2];

      rotateAround(
          *matrix,
          x,
          y,
          z,
          snapshot.rotX,
          snapshot.rotY,
          snapshot.rotZ);
    }

    // Always call through.
    //
    // Even when MatrixStack push failed, helper split still keeps
    // vanilla model placement intact.
    original(
        self,
        renderContext,
        renderData);
  }

  sHelperOverride =
      previousOverride;

  position[1] =
      oldY;

  count =
      oldCount;

  inItemFrame =
      oldInItemFrame;
}

} // namespace itemphysics
