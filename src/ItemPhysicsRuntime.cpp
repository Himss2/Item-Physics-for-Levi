#include "ItemPhysicsRuntime.hpp"
#include "TargetProfile.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string_view>

namespace itemphysics {
namespace {

// ============================================================================
// General
// ============================================================================

constexpr float kPi =
    3.14159265358979323846f;

constexpr float kDegToRad =
    kPi / 180.0f;

constexpr auto kStateTtl =
    std::chrono::seconds(8);

// ============================================================================
// Timing
// ============================================================================

// Ignore absurdly small dt values when estimating velocity.
constexpr float kMinVelocityDt =
    0.0005f;

// A long pause / frame stall should not generate an absurd velocity.
constexpr float kMaxVelocityDt =
    0.20f;

// Animation integration itself is capped.
//
// Landing spring uses additional internal substeps.
constexpr float kMaxPhysicsDt =
    0.05f;

// ============================================================================
// Natural airborne physics
// ============================================================================

// How quickly estimated position velocity follows real render movement.
constexpr float kVelocityTrackingResponse =
    12.0f;

// How quickly angular velocity changes toward the motion-based tumble axis.
constexpr float kAirSpinResponse =
    5.0f;

// Mild inertia loss while airborne.
//
// Deliberately very low.
// A thrown item should not stop spinning after several seconds like the
// previous age-based implementation did.
constexpr float kAirAngularDrag =
    0.12f;

// Maximum angular velocity allowed.
//
// Prevents broken position deltas / teleports from creating insane rotation.
constexpr float kMaxAngularSpeed =
    9.0f;

// Maximum linear speed used when calculating tumble strength.
constexpr float kMaxTrackedLinearSpeed =
    12.0f;

// ============================================================================
// Landing spring
// ============================================================================

// < 1.0 = slightly underdamped.
//
// 0.82 gives a very small natural overshoot without making the item bounce
// around for a long time.
constexpr float kLandingDampingRatio =
    0.82f;

// Integration substep for spring stability across low/high FPS.
constexpr float kSpringMaxStep =
    1.0f / 120.0f;

// Once this close to rest, remove tiny floating point vibration.
constexpr float kRestAngleEpsilon =
    0.0015f;

constexpr float kRestVelocityEpsilon =
    0.02f;

// ============================================================================
// Atlas
// ============================================================================

constexpr float kAtlasFlatLocalY =
    0.25f;

// ============================================================================
// BlockShape
// ============================================================================

constexpr std::int32_t
    kSkullBlockShape =
        83;

// ============================================================================
// Thin block detection
// ============================================================================

constexpr float kThinYRatio =
    0.70f;

constexpr float kExtentEpsilon =
    0.0005f;

// ============================================================================
// Item identifiers
// ============================================================================

constexpr std::string_view
    kShieldId =
        "minecraft:shield";

constexpr std::string_view
    kBannerId =
        "minecraft:banner";

// ============================================================================
// Ground-height categories
//
// Ground classification remains independent from animation.
// ============================================================================

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

// ============================================================================
// MatrixStack RAII
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

// ============================================================================
// Static runtime
// ============================================================================

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

  // ==========================================================================
  // Ground height
  // ==========================================================================

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

// ============================================================================
// Profile verification
// ============================================================================

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

          profile::
              kRenderFingerprint,

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
              kBlockGraphicsGetBlockShapeRva)) {

    mod.getLogger().warn(
        "Minecraft 1.26.45 renderer helper validation failed");

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

  // --------------------------------------------------------------------------
  // Matrix
  // --------------------------------------------------------------------------

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

  // --------------------------------------------------------------------------
  // Vanilla render block lookup
  // --------------------------------------------------------------------------

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

  // --------------------------------------------------------------------------
  // Block pointer path
  // --------------------------------------------------------------------------

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

  // ==========================================================================
  // ItemRenderer::render only
  // ==========================================================================

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
      "Minecraft 1.26.45 + Natural Throw Physics V5");

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

  mGetBlockTypeForRendering =
      nullptr;

  mGetBlockGraphicsForBlockType =
      nullptr;

  mGetBlockGraphicsForBlock =
      nullptr;

  mGetBlockGraphicsShape =
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

// ============================================================================
// Detour
// ============================================================================

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
// Math
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

// ============================================================================
// Damped angular spring
//
// x'' + 2*zeta*w*x' + w^2*x = 0
//
// We use semi-implicit Euler with small substeps.
//
// Advantages:
//
// - no snap
// - natural inertia
// - slight overshoot
// - stable at varying FPS
// ============================================================================

void ItemPhysicsRuntime::springAngle(
    float &current,
    float &velocity,
    float target,
    float angularFrequency,
    float dampingRatio,
    float dt) noexcept {

  if (dt <=
      0.0f) {

    return;
  }

  angularFrequency =
      std::max(
          angularFrequency,
          0.01f);

  dampingRatio =
      std::max(
          dampingRatio,
          0.0f);

  float remaining =
      dt;

  while (remaining >
         0.0f) {

    const float step =
        std::min(
            remaining,
            kSpringMaxStep);

    const float error =
        wrapPi(
            target -
            current);

    const float stiffness =
        angularFrequency *
        angularFrequency;

    const float damping =
        2.0f *
        dampingRatio *
        angularFrequency;

    const float acceleration =
        stiffness *
            error -
        damping *
            velocity;

    velocity +=
        acceleration *
        step;

    velocity =
        std::clamp(
            velocity,
            -kMaxAngularSpeed,
            kMaxAngularSpeed);

    current =
        wrapPi(
            current +
            velocity *
                step);

    remaining -=
        step;
  }

  const float finalError =
      std::abs(
          wrapPi(
              target -
              current));

  if (finalError <
          kRestAngleEpsilon &&
      std::abs(
          velocity) <
          kRestVelocityEpsilon) {

    current =
        wrapPi(
            target);

    velocity =
        0.0f;
  }
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

// ============================================================================
// Vanilla renderer BlockShape
// ============================================================================

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

// ============================================================================
// Block info
// ============================================================================

bool ItemPhysicsRuntime::buildBlockRenderInfo(
    const void *block,
    BlockRenderInfo &info) const noexcept {

  info =
      {};

  if (!block) {

    return false;
  }

  // --------------------------------------------------------------------------
  // BlockShape
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
    }
  }

  bool thinHorizontal =
      false;

  // --------------------------------------------------------------------------
  // VisualShape
  //
  // ONLY orientation compatibility.
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

// ============================================================================
// Classification
// ============================================================================

ItemPhysicsRuntime::ItemRenderTraits
ItemPhysicsRuntime::classifyItem(
    std::uintptr_t actorAddress) const noexcept {

  ItemRenderTraits traits{};

  // ==========================================================================
  // Actual ItemRenderer shape
  // ==========================================================================

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

  // ==========================================================================
  // Skull override
  //
  // Keep the successful head orientation behavior.
  // ==========================================================================

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

  // ==========================================================================
  // Existing Block pointer
  // ==========================================================================

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

  // ==========================================================================
  // Non-block item
  // ==========================================================================

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

// ============================================================================
// State creation
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

    // ------------------------------------------------------------------------
    // Deterministic fallback axis.
    //
    // Used before enough position history exists to infer the real throw
    // direction.
    // ------------------------------------------------------------------------

    float axisX =
        v *
            2.0f -
        1.0f;

    float axisZ =
        w *
            2.0f -
        1.0f;

    const float axisLength =
        std::sqrt(
            axisX *
                axisX +
            axisZ *
                axisZ);

    if (axisLength >
        0.0001f) {

      axisX /=
          axisLength;

      axisZ /=
          axisLength;

    } else {

      axisX =
          0.7071067f;

      axisZ =
          0.7071067f;
    }

    const float initialSpin =
        1.2f +
        u *
            1.4f;

    state.initialized =
        true;

    state.wasGrounded =
        false;

    state.hasPosition =
        false;

    state.hasLandingRest =
        false;

    state.rotX =
        0.0f;

    state.rotY =
        0.0f;

    state.rotZ =
        0.0f;

    state.angularX =
        axisX *
        initialSpin;

    state.angularY =
        (u -
         0.5f) *
        0.35f;

    state.angularZ =
        axisZ *
        initialSpin;

    state.velocityX =
        0.0f;

    state.velocityY =
        0.0f;

    state.velocityZ =
        0.0f;

    state.lastX =
        0.0f;

    state.lastY =
        0.0f;

    state.lastZ =
        0.0f;

    state.restYaw =
        seededUnit(
            entityId ^
            0x27D4EB2Du) *
        2.0f *
        kPi;

    state.landingRestZ =
        0.0f;

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
// Natural physics update V5
// ============================================================================

void ItemPhysicsRuntime::updateState(
    PhysicsState &state,
    const ItemRenderTraits &traits,
    bool grounded,
    float positionX,
    float positionY,
    float positionZ,
    std::chrono::steady_clock::
        time_point now) const {

  // ==========================================================================
  // Time
  // ==========================================================================

  const float rawDt =
      std::chrono::duration<float>(
          now -
          state.lastUpdate)
          .count();

  state.lastUpdate =
      now;

  state.lastSeen =
      now;

  const float dt =
      std::clamp(
          rawDt,
          0.0f,
          kMaxPhysicsDt);

  // ==========================================================================
  // Estimate ItemActor movement
  //
  // We do not modify Minecraft's actual entity velocity.
  //
  // We only observe render position changes to choose a visually appropriate
  // tumble axis.
  // ==========================================================================

  if (!state.hasPosition ||
      rawDt >
          kMaxVelocityDt) {

    state.lastX =
        positionX;

    state.lastY =
        positionY;

    state.lastZ =
        positionZ;

    state.hasPosition =
        true;

  } else if (
      rawDt >=
      kMinVelocityDt) {

    float rawVelocityX =
        (positionX -
         state.lastX) /
        rawDt;

    float rawVelocityY =
        (positionY -
         state.lastY) /
        rawDt;

    float rawVelocityZ =
        (positionZ -
         state.lastZ) /
        rawDt;

    state.lastX =
        positionX;

    state.lastY =
        positionY;

    state.lastZ =
        positionZ;

    // ------------------------------------------------------------------------
    // Clamp bad/teleport deltas.
    // ------------------------------------------------------------------------

    const float rawSpeed =
        std::sqrt(
            rawVelocityX *
                rawVelocityX +

            rawVelocityY *
                rawVelocityY +

            rawVelocityZ *
                rawVelocityZ);

    if (rawSpeed >
            kMaxTrackedLinearSpeed &&
        rawSpeed >
            0.0001f) {

      const float scale =
          kMaxTrackedLinearSpeed /
          rawSpeed;

      rawVelocityX *=
          scale;

      rawVelocityY *=
          scale;

      rawVelocityZ *=
          scale;
    }

    // ------------------------------------------------------------------------
    // Exponential low-pass.
    //
    // This avoids jitter from tiny renderer position differences.
    // ------------------------------------------------------------------------

    const float velocityAlpha =
        1.0f -
        std::exp(
            -kVelocityTrackingResponse *
            rawDt);

    state.velocityX +=
        (rawVelocityX -
         state.velocityX) *
        velocityAlpha;

    state.velocityY +=
        (rawVelocityY -
         state.velocityY) *
        velocityAlpha;

    state.velocityZ +=
        (rawVelocityZ -
         state.velocityZ) *
        velocityAlpha;
  }

  // No meaningful frame elapsed.
  if (dt <=
      0.0f) {

    return;
  }

  // ==========================================================================
  // AIRBORNE
  // ==========================================================================

  if (!grounded) {

    // We left the ground again.
    state.hasLandingRest =
        false;

    const float horizontalSpeed =
        std::sqrt(
            state.velocityX *
                state.velocityX +

            state.velocityZ *
                state.velocityZ);

    const float verticalSpeed =
        std::abs(
            state.velocityY);

    // ------------------------------------------------------------------------
    // Tumble strength
    //
    // Slow dropped item:
    //      still gently rotates.
    //
    // Thrown item:
    //      rotates faster.
    //
    // Tumble Speed from Mod Menu remains the user multiplier.
    // ------------------------------------------------------------------------

    const float tumbleMultiplier =
        mRotationSpeed.load(
            std::memory_order_relaxed);

    float desiredSpin =
        1.25f +

        std::min(
            horizontalSpeed,
            6.0f) *
            0.34f +

        std::min(
            verticalSpeed,
            4.0f) *
            0.12f;

    desiredSpin *=
        tumbleMultiplier;

    desiredSpin =
        std::clamp(
            desiredSpin,
            0.25f,
            kMaxAngularSpeed);

    // ------------------------------------------------------------------------
    // Tumble axis
    //
    // A real object travelling horizontally tends to roll around an axis
    // perpendicular to its movement.
    //
    // Minecraft movement:
    //
    // velocity X/Z
    //
    // visual tumble:
    //
    // X axis <- Z motion
    // Z axis <- -X motion
    // ------------------------------------------------------------------------

    float desiredX =
        0.0f;

    float desiredZ =
        0.0f;

    if (horizontalSpeed >
        0.05f) {

      desiredX =
          state.velocityZ /
          horizontalSpeed;

      desiredZ =
          -state.velocityX /
          horizontalSpeed;

    } else {

      // No reliable throw direction yet.
      //
      // Use deterministic axis based on per-entity rest yaw.
      desiredX =
          std::cos(
              state.restYaw);

      desiredZ =
          std::sin(
              state.restYaw);
    }

    // A small seeded bias prevents every throw travelling in the same
    // direction from looking mechanically identical.
    const float biasX =
        std::cos(
            state.restYaw *
            1.73f) *
        0.18f;

    const float biasZ =
        std::sin(
            state.restYaw *
            1.37f) *
        0.18f;

    desiredX +=
        biasX;

    desiredZ +=
        biasZ;

    const float desiredAxisLength =
        std::sqrt(
            desiredX *
                desiredX +
            desiredZ *
                desiredZ);

    if (desiredAxisLength >
        0.0001f) {

      desiredX /=
          desiredAxisLength;

      desiredZ /=
          desiredAxisLength;
    }

    const float targetAngularX =
        desiredX *
        desiredSpin;

    const float targetAngularZ =
        desiredZ *
        desiredSpin;

    // Keep Y rotation subtle.
    //
    // Full yaw spinning makes swords/tools look like a flat card rotating on a
    // turntable, which is less natural than actual tumbling.
    const float targetAngularY =
        std::sin(
            state.restYaw *
            1.91f) *
        desiredSpin *
        0.12f;

    // ------------------------------------------------------------------------
    // Smoothly steer existing angular momentum toward movement-based spin.
    //
    // This prevents an abrupt axis switch when the item is first thrown.
    // ------------------------------------------------------------------------

    const float spinAlpha =
        1.0f -
        std::exp(
            -kAirSpinResponse *
            dt);

    state.angularX +=
        (targetAngularX -
         state.angularX) *
        spinAlpha;

    state.angularY +=
        (targetAngularY -
         state.angularY) *
        spinAlpha;

    state.angularZ +=
        (targetAngularZ -
         state.angularZ) *
        spinAlpha;

    // ------------------------------------------------------------------------
    // Mild angular drag
    //
    // Unlike the old implementation:
    //
    // NO age / 4 second fade-to-zero.
    //
    // If an item remains airborne, it keeps tumbling naturally.
    // ------------------------------------------------------------------------

    const float drag =
        std::exp(
            -kAirAngularDrag *
            dt);

    state.angularX *=
        drag;

    state.angularY *=
        drag;

    state.angularZ *=
        drag;

    state.angularX =
        std::clamp(
            state.angularX,
            -kMaxAngularSpeed,
            kMaxAngularSpeed);

    state.angularY =
        std::clamp(
            state.angularY,
            -kMaxAngularSpeed,
            kMaxAngularSpeed);

    state.angularZ =
        std::clamp(
            state.angularZ,
            -kMaxAngularSpeed,
            kMaxAngularSpeed);

    // ------------------------------------------------------------------------
    // Integrate orientation
    // ------------------------------------------------------------------------

    state.rotX =
        wrapPi(
            state.rotX +
            state.angularX *
                dt);

    state.rotY =
        wrapPi(
            state.rotY +
            state.angularY *
                dt);

    state.rotZ =
        wrapPi(
            state.rotZ +
            state.angularZ *
                dt);

    state.wasGrounded =
        false;

    return;
  }

  // ==========================================================================
  // GROUNDED
  // ==========================================================================

  const bool justLanded =
      !state.wasGrounded;

  if (justLanded ||
      !state.hasLandingRest) {

    // ------------------------------------------------------------------------
    // Preserve landing roll for ordinary Atlas-style models.
    //
    // This keeps the visual result compatible with the working build while
    // replacing the abrupt settle behavior with a physical spring.
    // ------------------------------------------------------------------------

    state.landingRestZ =
        state.rotZ;

    state.hasLandingRest =
        true;

    // Do not kill air momentum instantly.
    //
    // Just clamp it to a sensible impact range and let damping remove it.
    state.angularX =
        std::clamp(
            state.angularX,
            -6.0f,
            6.0f);

    state.angularY =
        std::clamp(
            state.angularY,
            -6.0f,
            6.0f);

    state.angularZ =
        std::clamp(
            state.angularZ,
            -6.0f,
            6.0f);
  }

  // ==========================================================================
  // Settle strength
  //
  // Existing Settle Speed slider controls spring frequency.
  //
  // low:
  //     soft / slow landing
  //
  // high:
  //     quick settle
  // ==========================================================================

  const float settleSpeed =
      mSettleSpeed.load(
          std::memory_order_relaxed);

  const float springFrequency =
      2.0f +
      settleSpeed *
          2.25f;

  // ==========================================================================
  // Horizontal compatibility models
  //
  // head
  // slab
  // carpet
  // etc.
  //
  // Final target remains EXACTLY the same:
  //
  // X -> 0
  // Z -> 0
  // Y -> random rest yaw
  //
  // Only the transition is now spring-based.
  // ==========================================================================

  if (traits.modelClass ==
          ModelClass::
              BlockItem &&

      traits.block.
          keepHorizontal) {

    springAngle(
        state.rotX,
        state.angularX,
        0.0f,
        springFrequency,
        kLandingDampingRatio,
        dt);

    springAngle(
        state.rotY,
        state.angularY,
        state.restYaw,
        springFrequency,
        kLandingDampingRatio,
        dt);

    springAngle(
        state.rotZ,
        state.angularZ,
        0.0f,
        springFrequency,
        kLandingDampingRatio,
        dt);
  }

  // ==========================================================================
  // Existing Atlas grounded orientation
  //
  // Final target remains:
  //
  // X -> Ground Angle
  // Y -> 0
  // Z -> angle captured at impact
  //
  // So we're changing animation, NOT final item pose.
  // ==========================================================================

  else {

    const float targetX =
        mGroundTiltDeg.load(
            std::memory_order_relaxed) *
        kDegToRad;

    springAngle(
        state.rotX,
        state.angularX,
        targetX,
        springFrequency,
        kLandingDampingRatio,
        dt);

    springAngle(
        state.rotY,
        state.angularY,
        0.0f,
        springFrequency,
        kLandingDampingRatio,
        dt);

    springAngle(
        state.rotZ,
        state.angularZ,
        state.landingRestZ,
        springFrequency,
        kLandingDampingRatio,
        dt);
  }

  // Linear motion estimate is visual information only.
  //
  // Dampen it while grounded so an old throw velocity cannot affect a later
  // airborne transition too strongly.
  const float groundVelocityDrag =
      std::exp(
          -8.0f *
          dt);

  state.velocityX *=
      groundVelocityDrag;

  state.velocityY *=
      groundVelocityDrag;

  state.velocityZ *=
      groundVelocityDrag;

  state.wasGrounded =
      true;
}

// ============================================================================
// Cleanup
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

  // ==========================================================================
  // Read position BEFORE applying any Item Physics vertical corrections.
  //
  // This is important because movement estimation must observe Minecraft's
  // actual ItemActor motion, not our custom ground-height slider.
  // ==========================================================================

  auto *position =
      reinterpret_cast<
          float *>(

          renderDataAddress +
          profile::
              kRenderDataPositionOffset);

  const float actorX =
      position[0];

  const float actorY =
      position[1];

  const float actorZ =
      position[2];

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
        actorX,
        actorY,
        actorZ,
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

  // ==========================================================================
  // Temporary ItemActor state
  // ==========================================================================

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

  // ==========================================================================
  // Atlas renderer context
  // ==========================================================================

  inItemFrame =
      1;

  const float oldY =
      position[1];

  // ==========================================================================
  // Ordinary flat items
  // ==========================================================================

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

  // ==========================================================================
  // Ground-height sliders
  //
  // UNCHANGED by Natural Throw Physics V5.
  // ==========================================================================

  if (grounded) {

    // ------------------------------------------------------------------------
    // Shield / banner
    // ------------------------------------------------------------------------

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

    // ------------------------------------------------------------------------
    // Block-rendered category
    // ------------------------------------------------------------------------

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

        // --------------------------------------------------------------------
        // Head / skull
        // --------------------------------------------------------------------

        if (groundShape ==
            kSkullBlockShape) {

          position[1] +=
              mSkullGroundHeight.load(
                  std::memory_order_relaxed);
        }

        // --------------------------------------------------------------------
        // Thin block
        // --------------------------------------------------------------------

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

        // --------------------------------------------------------------------
        // Torch / cross
        // --------------------------------------------------------------------

        else if (
            isTorchGroundShape(
                groundShape)) {

          position[1] +=
              mTorchGroundHeight.load(
                  std::memory_order_relaxed);
        }

        // --------------------------------------------------------------------
        // Fence / lantern / shaped block
        // --------------------------------------------------------------------

        else if (
            isShapedGroundShape(
                groundShape)) {

          position[1] +=
              mShapedBlockGroundHeight.load(
                  std::memory_order_relaxed);
        }

        // --------------------------------------------------------------------
        // Generic block
        // --------------------------------------------------------------------

        else {

          position[1] +=
              mBlockGroundHeight.load(
                  std::memory_order_relaxed);
        }
      }
    }
  }

  // ==========================================================================
  // MatrixStack
  // ==========================================================================

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

    // ========================================================================
    // Matrix order remains unchanged.
    // ========================================================================

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

  // ==========================================================================
  // Restore
  // ==========================================================================

  position[1] =
      oldY;

  count =
      oldCount;

  inItemFrame =
      oldInItemFrame;
}

} // namespace itemphysics
