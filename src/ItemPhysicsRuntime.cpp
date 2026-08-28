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

// Atlas ordinary flat-item pivot.
constexpr float kFlatPivotY =
    0.25f;

// Non-block special 3D models must not use the
// full -0.38 flat-item correction.
//
// Java ItemPhysic has a separate ~0.2 transform path
// for GUI-3D models; use the same magnitude here.
constexpr float kSpecial3DPivotY =
    0.20f;

constexpr float kSpecial3DHeightOffset =
    -0.20f;

constexpr std::string_view kShieldId =
    "minecraft:shield";

constexpr std::string_view kBannerId =
    "minecraft:banner";

// -----------------------------------------------------------------------------
// MatrixStack scope
// -----------------------------------------------------------------------------

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

  bool mActive{};
};

} // namespace

ItemPhysicsRuntime *
ItemPhysicsRuntime::sInstance =
    nullptr;

// =============================================================================
// Config
// =============================================================================

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

// =============================================================================
// Target validation
// =============================================================================

bool ItemPhysicsRuntime::verifyProfile(
    const ResolvedVirtual &resolved,
    ll::mod::NativeMod &mod) const {

  const auto target =
      resolved.target;

  constexpr auto bytes =
      profile::kRenderFingerprint.size() *
      sizeof(std::uint32_t);

  if (!resolved.module.readable(
          target,
          bytes)) {

    mod.getLogger().warn(
        "ItemRenderer target is not readable for fingerprinting");

    return false;
  }

  const auto *words =
      reinterpret_cast<
          const std::uint32_t *>(
          target);

  for (std::size_t i = 0;
       i <
       profile::kRenderFingerprint.size();
       ++i) {

    if (words[i] !=
        profile::kRenderFingerprint[i]) {

      mod.getLogger().warn(
          "Unsupported Minecraft profile: "
          "ItemRenderer fingerprint mismatch at word {}",
          i);

      return false;
    }
  }

  const auto helperOk =
      [&](std::uintptr_t rva) {

        return resolved.module.executable(
            resolved.module.base +
            rva);
      };

  if (!helperOk(
          profile::kGetWorldMatrixRva) ||

      !helperOk(
          profile::kMatrixStackPushRva) ||

      !helperOk(
          profile::kMatrixStackRefDtorRva) ||

      !helperOk(
          profile::kBlockGraphicsGetForBlockRva) ||

      !helperOk(
          profile::kBlockGraphicsGetBlockShapeRva) ||

      !helperOk(
          profile::kIsBlockShape3DRva)) {

    mod.getLogger().warn(
        "Unsupported Minecraft profile: "
        "one or more renderer helper RVAs are invalid");

    return false;
  }

  return true;
}

// =============================================================================
// Install
// =============================================================================

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
        "failed to resolve 12ItemRenderer");

    return false;
  }

  if (!verifyProfile(
          *resolved,
          mod)) {

    mod.getLogger().warn(
        "Item Physics remains in safe passthrough mode; "
        "this libminecraftpe.so is not the analyzed profile");

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

  // ---------------------------------------------------------------------------
  // Vanilla BlockGraphics functions.
  // ---------------------------------------------------------------------------

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

  mIsBlockShape3D =
      reinterpret_cast<
          IsBlockShape3DFn>(
          mMinecraftBase +
          profile::
              kIsBlockShape3DRva);

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
      "Item Physics hook active: "
      "ItemRenderer::render base+0x{:X}; "
      "vanilla BlockShape classifier enabled",

      static_cast<
          unsigned long long>(
          mRenderTarget -
          mMinecraftBase));

  return true;
}

// =============================================================================
// Uninstall
// =============================================================================

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

  mGetBlockGraphicsForBlock =
      nullptr;

  mGetBlockGraphicsShape =
      nullptr;

  mIsBlockShape3D =
      nullptr;

  mRenderTarget =
      0;

  mMinecraftBase =
      0;

  clearStates();
}

// =============================================================================
// State reset
// =============================================================================

void ItemPhysicsRuntime::clearStates() {

  std::lock_guard lock(
      mStateMutex);

  mStates.clear();

  mRenderCounter =
      0;
}

// =============================================================================
// Hook callback
// =============================================================================

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

// =============================================================================
// Math helpers
// =============================================================================

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

// =============================================================================
// libc++ std::string reader
//
// Used only for the shield/banner special renderer path.
// =============================================================================

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

  if ((flag & 1u) ==
      0) {

    // libc++ short-string representation.
    length =
        static_cast<
            std::size_t>(
            flag >> 1);

    data =
        reinterpret_cast<
            const char *>(
            raw + 1);

  } else {

    // libc++ long-string representation.
    length =
        *reinterpret_cast<
            const std::size_t *>(
            raw + 8);

    data =
        *reinterpret_cast<
            const char *const *>(
            raw + 16);
  }

  if (!data ||
      length !=
          wanted.size()) {

    return false;
  }

  return std::memcmp(
             data,
             wanted.data(),
             length) ==
         0;
}

// =============================================================================
// Vanilla model classification
// =============================================================================

ItemPhysicsRuntime::ItemRenderTraits
ItemPhysicsRuntime::classifyItem(
    std::uintptr_t actorAddress) const noexcept {

  ItemRenderTraits traits{};

  // ---------------------------------------------------------------------------
  // BLOCK-BACKED ITEM
  // ---------------------------------------------------------------------------

  const auto block =
      *reinterpret_cast<
          const std::uintptr_t *>(
          actorAddress +
          profile::
              kBlockPtrOffset);

  if (block) {

    traits.valid =
        true;

    // Safe fallback if BlockGraphics has not been initialized.
    traits.modelClass =
        ModelClass::Block3D;

    if (mGetBlockGraphicsForBlock &&
        mGetBlockGraphicsShape &&
        mIsBlockShape3D) {

      const void *graphics =
          mGetBlockGraphicsForBlock(
              reinterpret_cast<
                  const void *>(
                  block));

      if (graphics) {

        traits.blockShape =
            mGetBlockGraphicsShape(
                graphics);

        // ---------------------------------------------------------------------
        // This is the EXACT classifier used by ItemRenderer::render.
        //
        // Verified examples for this binary:
        //
        // 3D:
        //   Block                0
        //   Stairs              10
        //   Fence               11
        //   Chest               22
        //   BlockHalf           67
        //   TopSnow             68
        //   Skull               83
        //   ShulkerBox          89
        //   Trapdoor            99
        //   PaleMossCarpet     153
        //
        // Flat/generated:
        //   Torch                2
        //   Ladder               8
        //   Rail                 9
        //   Lever               12
        //   Lantern            113
        //   Chain              123
        //
        // This is precisely the distinction our old
        // Block* != nullptr classifier was missing.
        // ---------------------------------------------------------------------

        const bool vanilla3D =
            mIsBlockShape3D(
                traits.blockShape);

        traits.modelClass =
            vanilla3D
                ? ModelClass::Block3D
                : ModelClass::FlatBlock;
      }
    }

    return traits;
  }

  // ---------------------------------------------------------------------------
  // NON-BLOCK ITEM
  // ---------------------------------------------------------------------------

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

  const auto identifierAddress =
      item +
      profile::
          kItemIdentifierOffset;

  const bool shield =
      libcxxStringEquals(
          identifierAddress,
          kShieldId);

  const bool banner =
      libcxxStringEquals(
          identifierAddress,
          kBannerId);

  traits.valid =
      true;

  traits.modelClass =
      (shield ||
       banner)

          ? ModelClass::Special3D
          : ModelClass::FlatItem;

  return traits;
}

// =============================================================================
// Physics state
// =============================================================================

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

    // Atlas starts render rotations at zero.
    state.rotX =
        0.0f;

    state.rotY =
        0.0f;

    state.rotZ =
        0.0f;

    state.angularX =
        (v * 2.0f -
         1.0f) *
        curve *
        kPi;

    state.angularZ =
        (w * 2.0f -
         1.0f) *
        (1.0f -
         curve) *
        kPi;

    // Stable horizontal direction for 3D blocks.
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

// =============================================================================
// Atlas-style physics + Bedrock model classification
// =============================================================================

void ItemPhysicsRuntime::updateState(
    PhysicsState &state,
    ModelClass modelClass,
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

  // Atlas:
  //
  // frameFactor = min(dt * 5, 1)
  const float frameFactor =
      std::min(
          dt * 5.0f,
          1.0f);

  // ---------------------------------------------------------------------------
  // AIR
  // ---------------------------------------------------------------------------

  if (!grounded) {

    const float ageSeconds =
        std::chrono::duration<float>(
            now -
            state.born)
            .count();

    // Atlas tumble fades over ~4 seconds.
    const float ageFactor =
        std::min(
            ageSeconds /
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
  }

  // ---------------------------------------------------------------------------
  // GROUND
  // ---------------------------------------------------------------------------

  else {

    const float alpha =
        frameFactor *
        mSettleSpeed.load(
            std::memory_order_relaxed);

    // -------------------------------------------------------------------------
    // REAL 3D BLOCK MODEL
    //
    // Old implementation:
    //
    // every grounded item -> X = 90°
    //
    // This is what made:
    // - slabs
    // - carpet-like models
    // - snow layers
    // - skulls/heads
    // - trapdoors
    //
    // rotate onto the wrong side.
    //
    // Vanilla ItemRenderer says these are 3D block models, so let them settle
    // back to their natural block orientation.
    // -------------------------------------------------------------------------

    if (modelClass ==
        ModelClass::Block3D) {

      state.rotX =
          approachAngle(
              state.rotX,
              0.0f,
              alpha);

      state.rotZ =
          approachAngle(
              state.rotZ,
              0.0f,
              alpha);

      state.rotY =
          0.0f;
    }

    // -------------------------------------------------------------------------
    // FLAT ITEM / FLAT BLOCK / SPECIAL 3D ITEM
    //
    // Sword/tool/armor and renderer-flat block shapes retain the successful
    // Atlas item-frame-on-floor behavior.
    // -------------------------------------------------------------------------

    else {

      const float target =
          mGroundTiltDeg.load(
              std::memory_order_relaxed) *
          kDegToRad;

      state.rotX =
          approachAngle(
              state.rotX,
              target,
              alpha);

      state.rotY =
          0.0f;

      // rotZ deliberately remains unchanged.
      //
      // After X reaches ~90 degrees, Z is the random
      // rotation inside the horizontal floor plane.
    }
  }

  state.wasGrounded =
      grounded;
}

// =============================================================================
// State cleanup
// =============================================================================

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

// =============================================================================
// OnGroundFlagComponent lookup
// =============================================================================

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

// =============================================================================
// ItemRenderer::render detour body
// =============================================================================

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

  // ---------------------------------------------------------------------------
  // Ask vanilla ItemRenderer how this model should be classified.
  // ---------------------------------------------------------------------------

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
        traits.modelClass,
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

  // ---------------------------------------------------------------------------
  // Temporary ItemActor fields.
  // ---------------------------------------------------------------------------

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

  // Atlas trick:
  //
  // suppress vanilla dropped-item bobbing/spinning.
  inItemFrame =
      1;

  // ---------------------------------------------------------------------------
  // Model-specific vertical/pivot correction.
  // ---------------------------------------------------------------------------

  auto *position =
      reinterpret_cast<
          float *>(
          renderDataAddress +
          profile::
              kRenderDataPositionOffset);

  const float oldY =
      position[1];

  float localPivotY =
      0.0f;

  switch (
      traits.modelClass) {

  // ---------------------------------------------------------------------------
  // Ordinary item:
  //
  // sword/tool/armor/ingot/etc.
  // ---------------------------------------------------------------------------

  case ModelClass::FlatItem:

    position[1] =
        oldY +
        mHeightOffset.load(
            std::memory_order_relaxed);

    localPivotY =
        kFlatPivotY;

    break;

  // ---------------------------------------------------------------------------
  // Block-backed but vanilla renderer says it is NOT a 3D block model.
  //
  // This is the major ground-height fix for:
  //
  // torch
  // lever
  // rail
  // ladder
  // lantern
  // chain
  // etc.
  //
  // The previous Block* check incorrectly kept these on the generic block
  // path, so they did not receive the -0.38 floor correction and appeared to
  // float.
  // ---------------------------------------------------------------------------

  case ModelClass::FlatBlock:

    position[1] =
        oldY +
        mHeightOffset.load(
            std::memory_order_relaxed);

    localPivotY =
        kFlatPivotY;

    break;

  // ---------------------------------------------------------------------------
  // Shield/banner.
  //
  // Do not apply the full -0.38 flat correction.
  // ---------------------------------------------------------------------------

  case ModelClass::Special3D:

    position[1] =
        oldY +
        kSpecial3DHeightOffset;

    localPivotY =
        kSpecial3DPivotY;

    break;

  // ---------------------------------------------------------------------------
  // Real 3D block model.
  //
  // No global -0.38 correction and, after landing, no forced X=90.
  //
  // This is the fix for:
  //
  // slab
  // carpet-like 3D block models
  // skull/head
  // snow layer
  // chest/shulker
  // trapdoor
  // full cube blocks
  // ---------------------------------------------------------------------------

  case ModelClass::Block3D:

    position[1] =
        oldY;

    break;
  }

  // ---------------------------------------------------------------------------
  // Matrix
  // ---------------------------------------------------------------------------

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

    // -------------------------------------------------------------------------
    // Atlas matrix sequence starts by translating to dropped-item position.
    // -------------------------------------------------------------------------

    postTranslate(
        *matrix,
        x,
        y,
        z);

    // Atlas-style local pivot for generated/flat model classes.
    if (localPivotY !=
        0.0f) {

      postTranslate(
          *matrix,
          0.0f,
          localPivotY,
          0.0f);
    }

    // -------------------------------------------------------------------------
    // TRUE 3D BLOCK
    //
    // Do not use the flat-item X=90 final pose.
    //
    // Airborne:
    //   still tumbles naturally.
    //
    // Ground:
    //   X/Z settle to zero.
    //   stable random Y keeps block direction varied.
    // -------------------------------------------------------------------------

    if (traits.modelClass ==
        ModelClass::Block3D) {

      postRotateY(
          *matrix,
          snapshot.restYaw);

      postRotateX(
          *matrix,
          snapshot.rotX);

      postRotateZ(
          *matrix,
          snapshot.rotZ);
    }

    // -------------------------------------------------------------------------
    // FLAT / GENERATED / SPECIAL MODEL
    //
    // Keep the orientation already proven to look correct for sword/tool/etc.
    // -------------------------------------------------------------------------

    else {

      postRotateX(
          *matrix,
          snapshot.rotX);

      postRotateY(
          *matrix,
          snapshot.rotY);

      postRotateZ(
          *matrix,
          snapshot.rotZ);
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

  // ---------------------------------------------------------------------------
  // Restore vanilla state.
  // ---------------------------------------------------------------------------

  position[1] =
      oldY;

  count =
      oldCount;

  inItemFrame =
      oldInItemFrame;
}

} // namespace itemphysics
