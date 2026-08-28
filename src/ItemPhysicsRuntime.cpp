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

constexpr float kHalfPi =
    kPi * 0.5f;

constexpr float kDegToRad =
    kPi / 180.0f;

constexpr auto kStateTtl =
    std::chrono::seconds(8);

// Atlas ordinary-item pivot.
//
// IMPORTANT:
// Atlas applies this only to ordinary
// NON-BLOCK items.
constexpr float kAtlasFlatPivotY =
    0.25f;

constexpr std::string_view
    kShieldId =
        "minecraft:shield";

constexpr std::string_view
    kBannerId =
        "minecraft:banner";

// Small tolerance used when deciding which
// AABB dimension should become the vertical axis.
constexpr float kExtentEpsilon =
    0.0005f;

// ============================================================================
// Matrix stack scope
// ============================================================================

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

// ============================================================================
// Config
// ============================================================================

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

// ============================================================================
// Profile validation
// ============================================================================

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
        "ItemRenderer target is not readable");

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
          profile::kBlockGraphicsGetForBlockRva) ||

      !executable(
          profile::kBlockGraphicsGetBlockShapeRva) ||

      !executable(
          profile::kIsBlockShape3DRva)) {

    mod.getLogger().warn(
        "Unsupported Minecraft profile: "
        "renderer helper RVA validation failed");

    return false;
  }

  return true;
}

// ============================================================================
// Install
// ============================================================================

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
      "Item Physics active at ItemRenderer +0x{:X}",
      static_cast<
          unsigned long long>(
          mRenderTarget -
          mMinecraftBase));

  return true;
}

// ============================================================================
// Uninstall
// ============================================================================

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

// ============================================================================
// State
// ============================================================================

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

// ============================================================================
// Basic math
// ============================================================================

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

// ============================================================================
// libc++ std::string
// ============================================================================

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
            flag >> 1);

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

// ============================================================================
// Block geometry
// ============================================================================

bool ItemPhysicsRuntime::buildBlockRenderInfo(
    const void *block,
    BlockRenderInfo &info) const noexcept {

  if (!block) {
    return false;
  }

  info =
      {};

  // --------------------------------------------------------------------------
  // BlockGraphics / BlockShape
  // --------------------------------------------------------------------------

  if (mGetBlockGraphicsForBlock &&
      mGetBlockGraphicsShape) {

    const void *graphics =
        mGetBlockGraphicsForBlock(
            block);

    if (graphics) {

      info.blockShape =
          mGetBlockGraphicsShape(
              graphics);

      if (mIsBlockShape3D) {

        info.vanilla3D =
            mIsBlockShape3D(
                info.blockShape);
      }
    }
  }

  // Vanilla renderer uses a smaller transform
  // for its non-3D block-shape path.
  info.renderScale =
      info.vanilla3D
          ? 0.50f
          : 0.25f;

  // --------------------------------------------------------------------------
  // BlockType
  // --------------------------------------------------------------------------

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

  if (!blockType) {
    return false;
  }

  auto **vtable =
      *reinterpret_cast<
          void ***>(
          blockType);

  if (!vtable) {
    return false;
  }

  constexpr std::size_t slot =
      profile::
          kBlockTypeGetVisualShapeVtableOffset /
      sizeof(void *);

  auto getVisualShape =
      reinterpret_cast<
          GetVisualShapeFn>(
          vtable[slot]);

  if (!getVisualShape) {
    return false;
  }

  AabbAbi bounds{};

  getVisualShape(
      blockType,
      block,
      &bounds);

  const auto finite =
      [](float value) {

        return std::isfinite(
            value);
      };

  if (!finite(bounds.minX) ||
      !finite(bounds.minY) ||
      !finite(bounds.minZ) ||

      !finite(bounds.maxX) ||
      !finite(bounds.maxY) ||
      !finite(bounds.maxZ)) {

    return false;
  }

  const float dx =
      bounds.maxX -
      bounds.minX;

  const float dy =
      bounds.maxY -
      bounds.minY;

  const float dz =
      bounds.maxZ -
      bounds.minZ;

  if (!(dx >
        kExtentEpsilon) ||

      !(dy >
        kExtentEpsilon) ||

      !(dz >
        kExtentEpsilon)) {

    return false;
  }

  info.bounds =
      bounds;

  // --------------------------------------------------------------------------
  // Visual center.
  //
  // Rotation will happen around the model's real AABB center instead of
  // ItemActor origin.
  // --------------------------------------------------------------------------

  info.pivotX =
      (bounds.minX +
       bounds.maxX) *
      0.5f *
      info.renderScale;

  info.pivotY =
      (bounds.minY +
       bounds.maxY) *
      0.5f *
      info.renderScale;

  info.pivotZ =
      (bounds.minZ +
       bounds.maxZ) *
      0.5f *
      info.renderScale;

  // --------------------------------------------------------------------------
  // Pick resting face.
  //
  // Largest face should touch the floor.
  //
  // Equivalently:
  // the smallest dimension becomes vertical.
  //
  // IMPORTANT:
  // ties prefer Y.
  //
  // This keeps cube/head upright instead of randomly flipping.
  // --------------------------------------------------------------------------

  const bool ySmallest =
      dy <=
          dx +
              kExtentEpsilon &&
      dy <=
          dz +
              kExtentEpsilon;

  if (ySmallest) {

    // Slab, carpet, snow layer, head/cube tie...
    info.targetRotX =
        0.0f;

    info.targetRotZ =
        0.0f;

  } else if (
      dx <=
      dz +
          kExtentEpsilon) {

    // X becomes world Y.
    //
    // Good for tall narrow shapes such as:
    // fence / torch / lever-like geometry.
    info.targetRotX =
        0.0f;

    info.targetRotZ =
        kHalfPi;

  } else {

    // Z becomes world Y.
    info.targetRotX =
        kHalfPi;

    info.targetRotZ =
        0.0f;
  }

  info.valid =
      true;

  return true;
}

// ============================================================================
// Calculate block ground contact
// ============================================================================

float ItemPhysicsRuntime::computeBlockGroundOffset(
    const BlockRenderInfo &info,
    float rotX,
    float rotZ) noexcept {

  if (!info.valid) {
    return 0.0f;
  }

  const float scale =
      info.renderScale;

  const float centerX =
      (info.bounds.minX +
       info.bounds.maxX) *
      0.5f *
      scale;

  const float centerY =
      (info.bounds.minY +
       info.bounds.maxY) *
      0.5f *
      scale;

  const float centerZ =
      (info.bounds.minZ +
       info.bounds.maxZ) *
      0.5f *
      scale;

  const float cosX =
      std::cos(
          rotX);

  const float sinX =
      std::sin(
          rotX);

  const float cosZ =
      std::cos(
          rotZ);

  const float sinZ =
      std::sin(
          rotZ);

  float rotatedMinY =
      std::numeric_limits<float>::
          infinity();

  const float xs[2] = {
      info.bounds.minX *
          scale,
      info.bounds.maxX *
          scale,
  };

  const float ys[2] = {
      info.bounds.minY *
          scale,
      info.bounds.maxY *
          scale,
  };

  const float zs[2] = {
      info.bounds.minZ *
          scale,
      info.bounds.maxZ *
          scale,
  };

  for (float px : xs) {
    for (float py : ys) {
      for (float pz : zs) {

        // Move to visual center.
        float x =
            px -
            centerX;

        float y =
            py -
            centerY;

        float z =
            pz -
            centerZ;

        // Matrix order used by the mod:
        //
        // postRotateX()
        // postRotateZ()
        //
        // For column vectors the point sees
        // Rz first, then Rx.

        const float zRotX =
            cosZ *
                x -
            sinZ *
                y;

        const float zRotY =
            sinZ *
                x +
            cosZ *
                y;

        const float zRotZ =
            z;

        const float xRotY =
            cosX *
                zRotY -
            sinX *
                zRotZ;

        const float finalY =
            centerY +
            xRotY;

        rotatedMinY =
            std::min(
                rotatedMinY,
                finalY);
      }
    }
  }

  if (!std::isfinite(
          rotatedMinY)) {

    return 0.0f;
  }

  // Identity orientation is our known-good baseline.
  //
  // This means:
  //
  // slab/head remain exactly where the previous
  // successful build placed them.
  //
  // Only the amount caused by rotation is corrected.
  const float baselineMinY =
      info.bounds.minY *
      scale;

  return baselineMinY -
         rotatedMinY;
}

// ============================================================================
// Item classification
// ============================================================================

ItemPhysicsRuntime::ItemRenderTraits
ItemPhysicsRuntime::classifyItem(
    std::uintptr_t actorAddress) const noexcept {

  ItemRenderTraits traits{};

  // --------------------------------------------------------------------------
  // Block-backed ItemStack
  // --------------------------------------------------------------------------

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

    buildBlockRenderInfo(
        block,
        traits.block);

    return traits;
  }

  // --------------------------------------------------------------------------
  // Ordinary Item
  // --------------------------------------------------------------------------

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

// ============================================================================
// Physics state creation
// ============================================================================

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

// ============================================================================
// Physics update
// ============================================================================

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

  // --------------------------------------------------------------------------
  // Airborne
  // --------------------------------------------------------------------------

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

  // --------------------------------------------------------------------------
  // Ground
  // --------------------------------------------------------------------------

  const float settle =
      frameFactor *
      mSettleSpeed.load(
          std::memory_order_relaxed);

  switch (
      traits.modelClass) {

  // --------------------------------------------------------------------------
  // BLOCK MODEL
  //
  // Target orientation is generated from VisualShape AABB.
  // --------------------------------------------------------------------------

  case ModelClass::BlockModel: {

    const float targetX =
        traits.block.valid
            ? traits.block.targetRotX
            : 0.0f;

    const float targetZ =
        traits.block.valid
            ? traits.block.targetRotZ
            : 0.0f;

    state.rotX =
        approachAngle(
            state.rotX,
            targetX,
            settle);

    state.rotZ =
        approachAngle(
            state.rotZ,
            targetZ,
            settle);

    state.rotY =
        0.0f;

    break;
  }

  // --------------------------------------------------------------------------
  // Sword/tool/normal item.
  //
  // Atlas target:
  // 90 degrees.
  // --------------------------------------------------------------------------

  case ModelClass::FlatItem:
  case ModelClass::Special3D: {

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

    // rotZ intentionally stays at the
    // landing orientation.

    break;
  }
  }

  state.wasGrounded =
      true;
}

// ============================================================================
// State cleanup
// ============================================================================

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

// ============================================================================
// OnGroundFlagComponent
// ============================================================================

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

// ============================================================================
// Render
// ============================================================================

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

  // --------------------------------------------------------------------------
  // Temporary ItemActor state
  // --------------------------------------------------------------------------

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
  // skips vanilla bob + spin.
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

  // --------------------------------------------------------------------------
  // Position correction
  // --------------------------------------------------------------------------

  float atlasPivotY =
      0.0f;

  if (traits.modelClass ==
      ModelClass::FlatItem) {

    // Exact Atlas ordinary-item correction.
    position[1] =
        oldY +
        mHeightOffset.load(
            std::memory_order_relaxed);

    atlasPivotY =
        kAtlasFlatPivotY;

  } else if (
      traits.modelClass ==
      ModelClass::BlockModel &&
      grounded &&
      traits.block.valid) {

    // Geometry-driven block contact.
    position[1] =
        oldY +
        computeBlockGroundOffset(
            traits.block,
            snapshot.rotX,
            snapshot.rotZ);

  } else {

    // Block models and shield/banner do NOT receive
    // Atlas's -0.38 / +0.25 ordinary-item correction.
    position[1] =
        oldY;
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

    // ------------------------------------------------------------------------
    // BLOCK MODEL
    //
    // Rotate around the actual VisualShape center.
    // ------------------------------------------------------------------------

    if (traits.modelClass ==
            ModelClass::BlockModel &&
        traits.block.valid) {

      const float pivotX =
          traits.block.pivotX;

      const float pivotY =
          traits.block.pivotY;

      const float pivotZ =
          traits.block.pivotZ;

      postTranslate(
          *matrix,

          x +
              pivotX,

          y +
              pivotY,

          z +
              pivotZ);

      postRotateX(
          *matrix,
          snapshot.rotX);

      postRotateY(
          *matrix,
          snapshot.rotY);

      postRotateZ(
          *matrix,
          snapshot.rotZ);

      postTranslate(
          *matrix,

          -(x +
            pivotX),

          -(y +
            pivotY),

          -(z +
            pivotZ));
    }

    // ------------------------------------------------------------------------
    // ORDINARY ITEM / SPECIAL ITEM
    // ------------------------------------------------------------------------

    else {

      postTranslate(
          *matrix,
          x,
          y,
          z);

      if (atlasPivotY !=
          0.0f) {

        // Intentionally NOT undone.
        //
        // This matches Atlas's real matrix sequence.
        postTranslate(
            *matrix,
            0.0f,
            atlasPivotY,
            0.0f);
      }

      postRotateX(
          *matrix,
          snapshot.rotX);

      postRotateY(
          *matrix,
          snapshot.rotY);

      postRotateZ(
          *matrix,
          snapshot.rotZ);

      postTranslate(
          *matrix,
          -x,
          -y,
          -z);
    }

    original(
        self,
        renderContext,
        renderData);
  }

  // --------------------------------------------------------------------------
  // Restore everything modified for this draw.
  // --------------------------------------------------------------------------

  position[1] =
      oldY;

  count =
      oldCount;

  inItemFrame =
      oldInItemFrame;
}

} // namespace itemphysics
