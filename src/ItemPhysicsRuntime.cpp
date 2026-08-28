#include "ItemPhysicsRuntime.hpp"
#include "TargetProfile.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>

namespace itemphysics {
namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kDegToRad = kPi / 180.0f;
constexpr auto kStateTtl = std::chrono::seconds(8);

class MatrixPushScope {
public:
  MatrixPushScope(void *stack, ItemPhysicsRuntime::MatrixPushFn push,
                  ItemPhysicsRuntime::MatrixRefDtorFn dtor)
      : mDtor(dtor) {
    if (stack && push && dtor) {
      mRef = push(stack, false);
      mActive = mRef.stack != nullptr && mRef.mat != nullptr;
    }
  }
  MatrixPushScope(const MatrixPushScope &) = delete;
  MatrixPushScope &operator=(const MatrixPushScope &) = delete;
  ~MatrixPushScope() {
    if (mActive && mDtor) {
      mDtor(&mRef);
      mRef.stack = nullptr;
      mRef.mat = nullptr;
    }
  }
  [[nodiscard]] Mat4 *matrix() noexcept { return mActive ? mRef.mat : nullptr; }

private:
  ItemPhysicsRuntime::MatrixStackRefAbi mRef{};
  ItemPhysicsRuntime::MatrixRefDtorFn mDtor{};
  bool mActive{};
};

} // namespace

ItemPhysicsRuntime *ItemPhysicsRuntime::sInstance = nullptr;

void ItemPhysicsRuntime::applyConfig(const ItemPhysicsConfig &config) noexcept {
  mEnabled.store(config.enabled, std::memory_order_relaxed);
  mSingleModel.store(config.singleModel, std::memory_order_relaxed);
  mRotationSpeed.store(static_cast<float>(config.rotationSpeed),
                       std::memory_order_relaxed);
  mSettleSpeed.store(static_cast<float>(config.settleSpeed),
                     std::memory_order_relaxed);
  mGroundTiltDeg.store(static_cast<float>(config.groundTilt),
                       std::memory_order_relaxed);
  mHeightOffset.store(static_cast<float>(config.heightOffset),
                      std::memory_order_relaxed);
}

bool ItemPhysicsRuntime::verifyProfile(const ResolvedVirtual &resolved,
                                       ll::mod::NativeMod &mod) const {
  const auto target = resolved.target;
  constexpr auto bytes = profile::kRenderFingerprint.size() * sizeof(std::uint32_t);
  if (!resolved.module.readable(target, bytes)) {
    mod.getLogger().warn("ItemRenderer target is not readable for fingerprinting");
    return false;
  }
  const auto *words = reinterpret_cast<const std::uint32_t *>(target);
  for (std::size_t i = 0; i < profile::kRenderFingerprint.size(); ++i) {
    if (words[i] != profile::kRenderFingerprint[i]) {
      mod.getLogger().warn(
          "Unsupported Minecraft profile: ItemRenderer fingerprint mismatch at word {}",
          i);
      return false;
    }
  }

  const auto helperOk = [&](std::uintptr_t rva) {
    return resolved.module.executable(resolved.module.base + rva);
  };
  if (!helperOk(profile::kGetWorldMatrixRva) ||
      !helperOk(profile::kMatrixStackPushRva) ||
      !helperOk(profile::kMatrixStackRefDtorRva)) {
    mod.getLogger().warn("Unsupported Minecraft profile: matrix helper RVA invalid");
    return false;
  }
  return true;
}

bool ItemPhysicsRuntime::install(ll::mod::NativeMod &mod) {
  uninstall();

  auto resolved = resolveVirtualByRtti(profile::kMinecraftModule,
                                       profile::kItemRendererRtti,
                                       profile::kItemRendererRenderVtableOffset);
  if (!resolved) {
    mod.getLogger().warn("Item Physics inactive: failed to resolve 12ItemRenderer");
    return false;
  }
  if (!verifyProfile(*resolved, mod)) {
    mod.getLogger().warn(
        "Item Physics remains in safe passthrough mode; this libminecraftpe.so is not the analyzed profile");
    return false;
  }

  mMinecraftBase = resolved->module.base;
  mRenderTarget = resolved->target;
  mGetWorldMatrix = reinterpret_cast<GetWorldMatrixFn>(
      mMinecraftBase + profile::kGetWorldMatrixRva);
  mMatrixPush =
      reinterpret_cast<MatrixPushFn>(mMinecraftBase + profile::kMatrixStackPushRva);
  mMatrixRefDtor = reinterpret_cast<MatrixRefDtorFn>(
      mMinecraftBase + profile::kMatrixStackRefDtorRva);

  sInstance = this;
  mOriginal = nullptr;
  mHook = std::make_unique<pl::memory::HookHandle>(
      reinterpret_cast<void *>(mRenderTarget),
      reinterpret_cast<void *>(&ItemPhysicsRuntime::renderDetour),
      reinterpret_cast<void **>(&mOriginal), pl::memory::HookPriority::Normal);
  if (!mHook->installed() || !mOriginal) {
    mod.getLogger().error("Failed to hook ItemRenderer::render");
    mHook.reset();
    sInstance = nullptr;
    return false;
  }

  mProfileSupported.store(true, std::memory_order_relaxed);
  mod.getLogger().info(
      "Item Physics hook active: ItemRenderer::render resolved at base+0x{:X}",
      static_cast<unsigned long long>(mRenderTarget - mMinecraftBase));
  return true;
}

void ItemPhysicsRuntime::uninstall() {
  mProfileSupported.store(false, std::memory_order_relaxed);
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

void ItemPhysicsRuntime::clearStates() {
  std::lock_guard lock(mStateMutex);
  mStates.clear();
  mRenderCounter = 0;
}

void ItemPhysicsRuntime::renderDetour(void *self, void *renderContext,
                                      void *renderData) {
  if (sInstance) {
    sInstance->onRender(self, renderContext, renderData);
  }
}

float ItemPhysicsRuntime::seededUnit(std::uint32_t seed) noexcept {
  seed ^= seed << 13;
  seed ^= seed >> 17;
  seed ^= seed << 5;
  return static_cast<float>(seed & 0x00FFFFFFu) / 16777215.0f;
}

float ItemPhysicsRuntime::wrapPi(float value) noexcept {
  while (value > kPi) value -= 2.0f * kPi;
  while (value < -kPi) value += 2.0f * kPi;
  return value;
}

float ItemPhysicsRuntime::approachAngle(float current, float target,
                                        float alpha) noexcept {
  const float delta = wrapPi(target - current);
  return wrapPi(current + delta * std::clamp(alpha, 0.0f, 1.0f));
}

ItemPhysicsRuntime::PhysicsState &
ItemPhysicsRuntime::stateFor(std::uint32_t entityId,
                             std::chrono::steady_clock::time_point now) {
  auto [it, inserted] = mStates.try_emplace(entityId);
  auto &state = it->second;
  if (inserted || !state.initialized) {
    const float u = seededUnit(entityId ^ 0x9E3779B9u);
    const float v = seededUnit(entityId ^ 0x85EBCA6Bu);
    const float w = seededUnit(entityId ^ 0xC2B2AE35u);
    const float curve = std::max(0.0f, 4.0f * u * u - 4.0f * u * u * u * u);
    state.initialized = true;
    state.rotX = (v - 0.5f) * 0.2f;
    state.rotY = w * 2.0f * kPi;
    state.rotZ = (u - 0.5f) * 0.2f;
    state.angularX = (v * 2.0f - 1.0f) * curve * kPi;
    state.angularZ = (w * 2.0f - 1.0f) * (1.0f - curve) * kPi;
    if (std::abs(state.angularX) + std::abs(state.angularZ) < 0.20f) {
      state.angularX += 0.65f;
    }
    state.born = now;
    state.lastUpdate = now;
    state.lastSeen = now;
  }
  return state;
}

void ItemPhysicsRuntime::updateState(
    PhysicsState &state, std::uint32_t, bool grounded,
    std::chrono::steady_clock::time_point now) const {
  const float dt = std::clamp(
      std::chrono::duration<float>(now - state.lastUpdate).count(), 0.0f, 0.20f);
  state.lastUpdate = now;
  state.lastSeen = now;

  if (!grounded) {
    const float age = std::chrono::duration<float>(now - state.born).count();
    const float ageFade = 1.0f - std::clamp(age / 4.0f, 0.0f, 1.0f);
    const float speed = mRotationSpeed.load(std::memory_order_relaxed);
    state.rotX = wrapPi(state.rotX + state.angularX * speed * ageFade * dt);
    state.rotZ = wrapPi(state.rotZ + state.angularZ * speed * ageFade * dt);
  } else {
    const float target =
        mGroundTiltDeg.load(std::memory_order_relaxed) * kDegToRad;
    const float alpha =
        dt * mSettleSpeed.load(std::memory_order_relaxed);
    state.rotX = approachAngle(state.rotX, target, alpha);
  }
  state.wasGrounded = grounded;
}

void ItemPhysicsRuntime::pruneStates(std::chrono::steady_clock::time_point now) {
  for (auto it = mStates.begin(); it != mStates.end();) {
    if (now - it->second.lastSeen > kStateTtl) {
      it = mStates.erase(it);
    } else {
      ++it;
    }
  }
}

bool ItemPhysicsRuntime::hasOnGroundComponent(void *actor) const noexcept {
  if (!actor) return false;
  const auto actorAddress = reinterpret_cast<std::uintptr_t>(actor);
  const auto registry = *reinterpret_cast<const std::uintptr_t *>(
      actorAddress + profile::kActorRegistryOffset);
  const auto entityId = *reinterpret_cast<const std::uint32_t *>(
      actorAddress + profile::kActorEntityIdOffset);
  if (!registry) return false;

  const auto bucketsBegin =
      *reinterpret_cast<const std::uintptr_t *>(registry + 0x38);
  const auto bucketsEnd =
      *reinterpret_cast<const std::uintptr_t *>(registry + 0x40);
  const auto nodesBase =
      *reinterpret_cast<const std::uintptr_t *>(registry + 0x50);
  const auto sentinel =
      *reinterpret_cast<const std::uintptr_t *>(registry + 0x58);
  if (!bucketsBegin || !bucketsEnd || bucketsEnd <= bucketsBegin || !nodesBase) {
    return false;
  }
  const auto bucketBytes = bucketsEnd - bucketsBegin;
  if ((bucketBytes % sizeof(std::uintptr_t)) != 0 ||
      bucketBytes / sizeof(std::uintptr_t) > (1u << 20)) {
    return false;
  }
  const auto bucketCount = bucketBytes / sizeof(std::uintptr_t);
  if (bucketCount == 0) return false;
  const auto index = (bucketCount - 1) & profile::kOnGroundFlagComponentHash;
  std::int64_t nodeIndex = *reinterpret_cast<const std::int64_t *>(
      bucketsBegin + index * sizeof(std::uintptr_t));

  std::uintptr_t node = 0;
  for (int guard = 0; nodeIndex != -1 && guard < 4096; ++guard) {
    node = nodesBase + static_cast<std::uintptr_t>(nodeIndex) * 32u;
    if (*reinterpret_cast<const std::uint32_t *>(node + 8) ==
        profile::kOnGroundFlagComponentHash) {
      break;
    }
    nodeIndex = *reinterpret_cast<const std::int64_t *>(node);
    node = 0;
  }
  if (!node || node == sentinel) return false;

  const auto storage = *reinterpret_cast<const std::uintptr_t *>(node + 0x10);
  if (!storage) return false;
  const auto pagesBegin =
      *reinterpret_cast<const std::uintptr_t *>(storage + 0x8);
  const auto pagesEnd =
      *reinterpret_cast<const std::uintptr_t *>(storage + 0x10);
  if (!pagesBegin || !pagesEnd || pagesEnd < pagesBegin) return false;
  const auto pageCount = (pagesEnd - pagesBegin) / sizeof(std::uintptr_t);
  const auto pageIndex = (entityId >> 11) & 0x7Fu;
  if (pageIndex >= pageCount) return false;
  const auto page = *reinterpret_cast<const std::uintptr_t *>(
      pagesBegin + pageIndex * sizeof(std::uintptr_t));
  if (!page) return false;

  const auto slot = entityId & 0x7FFu;
  const auto generation = entityId & 0xFFFC0000u;
  const auto slotValue =
      *reinterpret_cast<const std::uint32_t *>(page + slot * sizeof(std::uint32_t));
  return (slotValue ^ generation) <= 0x3FFFEu;
}

void ItemPhysicsRuntime::onRender(void *self, void *renderContext,
                                  void *renderData) {
  const auto original = mOriginal;
  if (!original) {
    return;
  }
  if (!mEnabled.load(std::memory_order_relaxed) || !renderContext || !renderData ||
      !mProfileSupported.load(std::memory_order_relaxed)) {
    original(self, renderContext, renderData);
    return;
  }

  const auto renderDataAddress = reinterpret_cast<std::uintptr_t>(renderData);
  auto *actor = *reinterpret_cast<void **>(
      renderDataAddress + profile::kRenderDataActorOffset);
  if (!actor) {
    original(self, renderContext, renderData);
    return;
  }
  const auto actorAddress = reinterpret_cast<std::uintptr_t>(actor);
  const auto entityId = *reinterpret_cast<const std::uint32_t *>(
      actorAddress + profile::kActorEntityIdOffset);
  const bool grounded = hasOnGroundComponent(actor);

  PhysicsState snapshot;
  {
    std::lock_guard lock(mStateMutex);
    const auto now = std::chrono::steady_clock::now();
    auto &state = stateFor(entityId, now);
    updateState(state, entityId, grounded, now);
    snapshot = state;
    if ((++mRenderCounter & 0xFFu) == 0) {
      pruneStates(now);
    }
  }

  auto &count = *reinterpret_cast<std::uint8_t *>(
      actorAddress + profile::kItemCountOffset);
  auto &inItemFrame = *reinterpret_cast<std::uint8_t *>(
      actorAddress + profile::kIsInItemFrameOffset);
  const auto oldCount = count;
  const auto oldInItemFrame = inItemFrame;

  if (mSingleModel.load(std::memory_order_relaxed)) {
    count = 1;
  }
  inItemFrame = 1; // skips vanilla dropped-item bob/spin for this draw call.

  void *stack = mGetWorldMatrix ? mGetWorldMatrix(renderContext) : nullptr;
  {
    MatrixPushScope matrixScope(stack, mMatrixPush, mMatrixRefDtor);
    auto *matrix = matrixScope.matrix();
    if (!matrix) {
      count = oldCount;
      inItemFrame = oldInItemFrame;
      original(self, renderContext, renderData);
      return;
    }

    auto *position = reinterpret_cast<float *>(
        renderDataAddress + profile::kRenderDataPositionOffset);
    const float oldY = position[1];
    position[1] = oldY + mHeightOffset.load(std::memory_order_relaxed);
    const float x = position[0];
    const float y = position[1];
    const float z = position[2];
    rotateAround(*matrix, x, y, z, snapshot.rotX, snapshot.rotY, snapshot.rotZ);
    original(self, renderContext, renderData);
    position[1] = oldY;
  } // MatrixStackRef dtor pops before actor state is restored.

  count = oldCount;
  inItemFrame = oldInItemFrame;
}

} // namespace itemphysics
