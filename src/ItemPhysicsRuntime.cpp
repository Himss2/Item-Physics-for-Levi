#include "ItemPhysicsRuntime.hpp"
#include "TargetProfile.hpp"

#include <algorithm>
#include <array>
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

// Exact constant observed in Atlas constructor:
// ItemPhysics + 0x210
constexpr float kAtlasFlatPivotY =
    0.25f;

constexpr std::string_view kShieldId =
    "minecraft:shield";

constexpr std::string_view kBannerId =
    "minecraft:banner";

// ============================================================
// Matrix scope
// ============================================================

class MatrixPushScope {
public:
  MatrixPushScope(
      void *stack,
      ItemPhysicsRuntime::MatrixPushFn push,
      ItemPhysicsRuntime::MatrixRefDtorFn dtor)
      : mDtor(dtor) {

    if (stack && push && dtor) {
      mRef = push(stack, false);

      mActive =
          mRef.stack != nullptr &&
          mRef.mat != nullptr;
    }
  }

  MatrixPushScope(
      const MatrixPushScope &) = delete;

  MatrixPushScope &
  operator=(const MatrixPushScope &) = delete;

  ~MatrixPushScope() {
    if (mActive && mDtor) {
      mDtor(&mRef);

      mRef.stack = nullptr;
      mRef.mat = nullptr;
    }
  }

  [[nodiscard]]
  Mat4 *matrix() noexcept {
    return mActive
               ? mRef.mat
               : nullptr;
  }

private:
  ItemPhysicsRuntime::MatrixStackRefAbi mRef{};

  ItemPhysicsRuntime::MatrixRefDtorFn mDtor{};

  bool mActive{};
};

// ============================================================
// libc++ std::string reader
//
// Atlas reads the Item identifier directly from the libc++
// string at Item + 0xF0.
// ============================================================

bool libcxxStringEquals(
    std::uintptr_t stringAddress,
    std::string_view wanted) noexcept {

  if (!stringAddress) {
    return false;
  }

  const auto *raw =
      reinterpret_cast<const std::uint8_t *>(
          stringAddress);

  const std::uint8_t flag =
      raw[0];

  std::size_t length = 0;

  const char *data = nullptr;

  if ((flag & 1u) == 0) {
    // libc++ short-string representation.
    length =
        static_cast<std::size_t>(
            flag >> 1);

    data =
        reinterpret_cast<const char *>(
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
      length != wanted.size()) {
    return false;
  }

  return std::memcmp(
             data,
             wanted.data(),
             length) == 0;
}

// ============================================================
// Item classification reconstructed from Atlas:
//
// Actor + 0x398 -> item handle
// Actor + 0x3A8 -> Block*
//
// if Block* != nullptr:
//     block/3D path
//
// otherwise:
//     normal flat-item path,
//     except minecraft:shield and minecraft:banner.
// ============================================================

struct ItemRenderTraits {
  bool valid{};
  bool flatCorrection{};
};

ItemRenderTraits classifyItem(
    std::uintptr_t actorAddress) noexcept {

  const auto itemHandle =
      *reinterpret_cast<
          const std::uintptr_t *>(
          actorAddress +
          profile::kItemHandleOffset);

  if (!itemHandle) {
    return {};
  }

  const auto block =
      *reinterpret_cast<
          const std::uintptr_t *>(
          actorAddress +
          profile::kBlockPtrOffset);

  // A real block item does not receive Atlas's
  // -0.38 / +0.25 flat-item correction.
  if (block) {
    return {
        true,
        false
    };
  }

  // Atlas dereferences the ItemStack item handle once
  // before reading Item + 0xF0.
  const auto item =
      *reinterpret_cast<
          const std::uintptr_t *>(
          itemHandle);

  if (!item) {
    return {};
  }

  const auto identifierAddress =
      item +
      profile::kItemIdentifierOffset;

  const bool shield =
      libcxxStringEquals(
          identifierAddress,
          kShieldId);

  const bool banner =
      libcxxStringEquals(
          identifierAddress,
          kBannerId);

  if (shield || banner) {
    return {
        true,
        false
    };
  }

  // Swords, tools, armor, ingots, food, etc.
  return {
      true,
      true
  };
}

} // namespace

ItemPhysicsRuntime *
ItemPhysicsRuntime::sInstance = nullptr;

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
// Profile validation
// ============================================================

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
          profile::kMatrixStackRefDtorRva)) {

    mod.getLogger().warn(
        "Unsupported Minecraft profile: "
        "matrix helper RVA invalid");

    return false;
  }

  return true;
}

// ============================================================
// Hook installation
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

  sInstance = this;

  mOriginal = nullptr;

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

    sInstance = nullptr;

    return false;
  }

  mProfileSupported.store(
      true,
      std::memory_order_relaxed);

  mod.getLogger().info(
      "Item Physics hook active: "
      "ItemRenderer::render resolved at base+0x{:X}",
      static_cast<unsigned long long>(
          mRenderTarget -
          mMinecraftBase));

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

  if (sInstance == this) {
    sInstance = nullptr;
  }

  mOriginal = nullptr;

  mGetWorldMatrix = nullptr;
  mMatrixPush = nullptr;
  mMatrixRefDtor = nullptr;

  mRenderTarget = 0;
  mMinecraftBase = 0;

  clearStates();
}

// ============================================================
// State
// ============================================================

void ItemPhysicsRuntime::clearStates() {

  std::lock_guard lock(
      mStateMutex);

  mStates.clear();

  mRenderCounter = 0;
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

  seed ^= seed << 13;
  seed ^= seed >> 17;
  seed ^= seed << 5;

  return static_cast<float>(
             seed &
             0x00FFFFFFu) /
         16777215.0f;
}

float ItemPhysicsRuntime::wrapPi(
    float value) noexcept {

  while (value > kPi) {
    value -=
        2.0f * kPi;
  }

  while (value < -kPi) {
    value +=
        2.0f * kPi;
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

    // Atlas starts all three render angles at zero.
    state.rotX = 0.0f;
    state.rotY = 0.0f;
    state.rotZ = 0.0f;

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
// Atlas-style physics
// ============================================================

void ItemPhysicsRuntime::updateState(
    PhysicsState &state,
    std::uint32_t,
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
  // min(
  //   deltaUs * 5 / 1,000,000,
  //   1
  // )
  //
  // == min(dt * 5, 1)

  const float frameFactor =
      std::min(
          dt * 5.0f,
          1.0f);

  if (!grounded) {

    const float ageSeconds =
        std::chrono::duration<float>(
            now -
            state.born)
            .count();

    // Atlas:
    //
    // min(
    //   elapsed / 4,000,000,
    //   1
    // )

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

    // Atlas leaves Y at zero.
    state.rotY =
        0.0f;

  } else {

    // ItemPhysics + 0x2B0 = 90.0 degrees.
    const float target =
        mGroundTiltDeg.load(
            std::memory_order_relaxed) *
        kDegToRad;

    // Atlas:
    // settleBlend = frameFactor * 3
    //
    // User setting remains a multiplier around
    // the Atlas default of 3.

    const float alpha =
        frameFactor *
        mSettleSpeed.load(
            std::memory_order_relaxed);

    state.rotX =
        approachAngle(
            state.rotX,
            target,
            alpha);

    state.rotY =
        0.0f;

    // rotZ deliberately remains untouched.
    //
    // It becomes the random direction in the
    // horizontal floor plane.
  }

  state.wasGrounded =
      grounded;
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
          mStates.erase(it);

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

  if (bucketCount == 0) {
    return false;
  }

  const auto index =
      (bucketCount - 1) &
      profile::
          kOnGroundFlagComponentHash;

  std::int64_t nodeIndex =
      *reinterpret_cast<
          const std::int64_t *>(
          bucketsBegin +
          index *
              sizeof(
                  std::uintptr_t));

  std::uintptr_t node = 0;

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

    node = 0;
  }

  if (!node ||
      node == sentinel) {

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
      (entityId >> 11) &
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
// ItemRenderer hook
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
      *reinterpret_cast<void **>(
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

  // Atlas first verifies ItemStack.mItem.
  const auto itemTraits =
      classifyItem(
          actorAddress);

  if (!itemTraits.valid) {

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
        entityId,
        grounded,
        now);

    snapshot =
        state;

    if ((++mRenderCounter &
         0xFFu) == 0) {

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

    count = 1;
  }

  // Atlas uses this to bypass vanilla
  // dropped-item bobbing and spinning.
  inItemFrame = 1;

  auto *position =
      reinterpret_cast<float *>(
          renderDataAddress +
          profile::
              kRenderDataPositionOffset);

  const float oldY =
      position[1];

  // ----------------------------------------------------------
  // Atlas flat-item correction:
  //
  // ordinary non-block item:
  // position.y += -0.38
  //
  // block / shield / banner:
  // no correction
  // ----------------------------------------------------------

  if (itemTraits.flatCorrection) {

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

    // ========================================================
    // Atlas matrix sequence
    //
    // M = M * T(position)
    //
    // flat item:
    // M = M * T(0, +0.25, 0)
    //
    // M = M * Rx
    // M = M * Ry
    // M = M * Rz
    //
    // M = M * T(-position)
    //
    // Vanilla renderer subsequently applies position again.
    //
    // Effective result:
    //
    // block:
    //     T(position) * R
    //
    // flat:
    //     T(position) * T(0,.25,0) * R
    // ========================================================

    postTranslate(
        *matrix,
        x,
        y,
        z);

    if (itemTraits.flatCorrection) {

      postTranslate(
          *matrix,
          0.0f,
          kAtlasFlatPivotY,
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
