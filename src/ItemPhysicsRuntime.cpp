#include "ItemPhysicsRuntime.hpp"
#include "TargetProfile.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace itemphysics {
namespace {

constexpr float kHalfPi = 1.57079632679489661923f;
constexpr float kRotationPerTick = 0.25f;
constexpr float kDefaultRotationSpeed = 1.0f;
constexpr float kBlockOffsetY = -0.20f;
constexpr float kBlockOffsetZ = -0.08f;
constexpr float kFlatOffsetZ = -0.04f;
constexpr float kBobOffsetScale = 0.007957747154594767f;
constexpr float kFlatCopyCenterStep = 0.09375f;
constexpr float kFlatModelScale = 0.50f;
constexpr float kDefaultBlockScale = 0.25f;
constexpr float kMaxContinuousDeltaTicks = 10.0f;

thread_local bool gForceSingleCopy = false;
thread_local float *gObservedModelScale = nullptr;

class MatrixPushScope {
public:
  MatrixPushScope(void *stack, ItemPhysicsRuntime::MatrixPushFn push,
                  ItemPhysicsRuntime::MatrixRefDtorFn dtor)
      : mDtor(dtor) {
    if (stack && push && dtor) {
      mRef = push(stack, false);
      mActive = mRef.stack && mRef.mat;
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

class JavaRandom {
public:
  explicit JavaRandom(std::uint32_t seed) noexcept {
    const auto signedSeed = static_cast<std::uint64_t>(
        static_cast<std::int64_t>(static_cast<std::int32_t>(seed)));
    mSeed = (signedSeed ^ kMultiplier) & kMask;
  }

  [[nodiscard]] float nextFloat() noexcept {
    mSeed = (mSeed * kMultiplier + kAddend) & kMask;
    return static_cast<float>(mSeed >> 24) / 16777216.0f;
  }

private:
  static constexpr std::uint64_t kMultiplier = 0x5DEECE66DULL;
  static constexpr std::uint64_t kAddend = 0xBULL;
  static constexpr std::uint64_t kMask = (1ULL << 48) - 1;
  std::uint64_t mSeed{};
};

template <typename Fingerprint>
bool matchesFingerprint(const ModuleView &module, std::uintptr_t address,
                        const Fingerprint &fingerprint) noexcept {
  const auto bytes = fingerprint.size() * sizeof(std::uint32_t);
  if (!module.readable(address, bytes))
    return false;

  const auto *words = reinterpret_cast<const std::uint32_t *>(address);
  for (std::size_t i = 0; i < fingerprint.size(); ++i) {
    if (words[i] != fingerprint[i])
      return false;
  }
  return true;
}

} // namespace

ItemPhysicsRuntime *ItemPhysicsRuntime::sInstance = nullptr;

bool ItemPhysicsRuntime::verifyProfile(const ResolvedVirtual &resolved,
                                       ll::mod::NativeMod &mod) const {
  const auto base = resolved.module.base;
  const auto expectedRender = base + profile::kItemRendererRenderRva;

  if (!resolved.module.hasBuildId ||
      resolved.module.buildId != profile::kBuildId) {
    mod.getLogger().warn(
        "Minecraft 1.26.45.1 profile mismatch: GNU Build ID");
    return false;
  }

  struct Check {
    std::uintptr_t address;
    const char *name;
    bool matched;
  };

  const Check checks[] = {
      {resolved.target, "ItemRenderer::render",
       resolved.target == expectedRender &&
           matchesFingerprint(resolved.module, resolved.target,
                              profile::kRenderFingerprint)},
      {base + profile::kRenderItemGroupLikeRva, "item render-group helper",
       matchesFingerprint(resolved.module,
                          base + profile::kRenderItemGroupLikeRva,
                          profile::kRenderItemGroupFingerprint)},
      {base + profile::kGetWorldMatrixRva, "getWorldMatrix",
       matchesFingerprint(resolved.module, base + profile::kGetWorldMatrixRva,
                          profile::kGetWorldMatrixFingerprint)},
      {base + profile::kGetPartialTickRva, "getPartialTick",
       matchesFingerprint(resolved.module, base + profile::kGetPartialTickRva,
                          profile::kGetPartialTickFingerprint)},
      {base + profile::kMatrixStackPushRva, "MatrixStack::push",
       matchesFingerprint(resolved.module, base + profile::kMatrixStackPushRva,
                          profile::kMatrixStackPushFingerprint)},
      {base + profile::kMatrixStackRefDtorRva, "MatrixStackRef::~MatrixStackRef",
       matchesFingerprint(resolved.module,
                          base + profile::kMatrixStackRefDtorRva,
                          profile::kMatrixStackRefDtorFingerprint)},
      {base + profile::kGetBlockTypeForRenderingRva,
       "ItemStackBase::getBlockTypeForRendering",
       matchesFingerprint(resolved.module,
                          base + profile::kGetBlockTypeForRenderingRva,
                          profile::kGetBlockTypeForRenderingFingerprint)},
  };

  for (const auto &check : checks) {
    if (!check.matched || !resolved.module.executable(check.address)) {
      mod.getLogger().warn("Minecraft 1.26.45.1 profile mismatch: {}",
                           check.name);
      return false;
    }
  }
  return true;
}

bool ItemPhysicsRuntime::install(ll::mod::NativeMod &mod) {
  uninstall();

  auto resolved = resolveVirtualByRtti(
      profile::kMinecraftModule, profile::kItemRendererRtti,
      profile::kItemRendererRenderVtableOffset);
  if (!resolved) {
    mod.getLogger().warn(
        "Item Physics inactive: ItemRenderer RTTI resolution failed");
    return false;
  }
  if (!verifyProfile(*resolved, mod)) {
    mod.getLogger().warn(
        "Item Physics inactive: unsupported libminecraftpe.so (safe passthrough)");
    return false;
  }

  mMinecraftBase = resolved->module.base;
  mRenderTarget = resolved->target;
  mRenderItemGroupTarget =
      mMinecraftBase + profile::kRenderItemGroupLikeRva;
  mGetWorldMatrix = reinterpret_cast<GetWorldMatrixFn>(
      mMinecraftBase + profile::kGetWorldMatrixRva);
  mGetPartialTick = reinterpret_cast<GetPartialTickFn>(
      mMinecraftBase + profile::kGetPartialTickRva);
  mMatrixPush = reinterpret_cast<MatrixPushFn>(
      mMinecraftBase + profile::kMatrixStackPushRva);
  mMatrixRefDtor = reinterpret_cast<MatrixRefDtorFn>(
      mMinecraftBase + profile::kMatrixStackRefDtorRva);
  mGetBlockTypeForRendering = reinterpret_cast<GetBlockTypeForRenderingFn>(
      mMinecraftBase + profile::kGetBlockTypeForRenderingRva);

  sInstance = this;
  mHook = std::make_unique<pl::memory::HookHandle>(
      reinterpret_cast<void *>(mRenderTarget),
      reinterpret_cast<void *>(&ItemPhysicsRuntime::renderDetour),
      reinterpret_cast<void **>(&mOriginal),
      pl::memory::HookPriority::Normal);
  if (!mHook->installed() || !mOriginal) {
    mod.getLogger().error("Failed to hook ItemRenderer::render");
    mHook.reset();
    sInstance = nullptr;
    return false;
  }

  mRenderItemGroupHook = std::make_unique<pl::memory::HookHandle>(
      reinterpret_cast<void *>(mRenderItemGroupTarget),
      reinterpret_cast<void *>(&ItemPhysicsRuntime::renderItemGroupDetour),
      reinterpret_cast<void **>(&mRenderItemGroupOriginal),
      pl::memory::HookPriority::Normal);
  if (!mRenderItemGroupHook->installed() || !mRenderItemGroupOriginal) {
    mod.getLogger().error("Failed to hook item render-group helper");
    mRenderItemGroupHook.reset();
    mHook->reset();
    mHook.reset();
    mOriginal = nullptr;
    sInstance = nullptr;
    return false;
  }

  mProfileSupported.store(true, std::memory_order_relaxed);
  mod.getLogger().info(
      "Item Physics visual core active for Minecraft 1.26.45.1");
  return true;
}

void ItemPhysicsRuntime::uninstall() {
  mProfileSupported.store(false, std::memory_order_relaxed);
  if (mRenderItemGroupHook) {
    mRenderItemGroupHook->reset();
    mRenderItemGroupHook.reset();
  }
  if (mHook) {
    mHook->reset();
    mHook.reset();
  }
  if (sInstance == this)
    sInstance = nullptr;

  mOriginal = nullptr;
  mRenderItemGroupOriginal = nullptr;
  mGetWorldMatrix = nullptr;
  mGetPartialTick = nullptr;
  mMatrixPush = nullptr;
  mMatrixRefDtor = nullptr;
  mGetBlockTypeForRendering = nullptr;
  mMinecraftBase = 0;
  mRenderTarget = 0;
  mRenderItemGroupTarget = 0;
  clearStates();
}

void ItemPhysicsRuntime::clearStates() noexcept {
  mStates = {};
  mRenderCounter = 0;
}

void ItemPhysicsRuntime::renderDetour(void *self, void *ctx, void *renderData) {
  if (sInstance)
    sInstance->onRender(self, ctx, renderData);
}

void ItemPhysicsRuntime::renderItemGroupDetour(
    void *self, void *ctx, void *itemData, std::uint32_t count,
    std::uint32_t flags, float scale, float animation) {
  if (sInstance)
    sInstance->onRenderItemGroup(self, ctx, itemData, count, flags, scale,
                                 animation);
}

void ItemPhysicsRuntime::onRenderItemGroup(
    void *self, void *ctx, void *itemData, std::uint32_t count,
    std::uint32_t flags, float scale, float animation) {
  const auto original = mRenderItemGroupOriginal;
  if (!original)
    return;

  if (gObservedModelScale && std::isfinite(scale) && scale > 0.0f &&
      scale <= 2.0f)
    *gObservedModelScale = scale;

  original(self, ctx, itemData, gForceSingleCopy ? 1u : count, flags, scale,
           animation);
}

void *ItemPhysicsRuntime::findComponentStorage(void *actor,
                                               std::uint32_t hash) const noexcept {
  if (!actor)
    return nullptr;

  const auto address = reinterpret_cast<std::uintptr_t>(actor);
  const auto registry = *reinterpret_cast<const std::uintptr_t *>(
      address + profile::kActorRegistryOffset);
  if (!registry)
    return nullptr;

  const auto begin =
      *reinterpret_cast<const std::uintptr_t *>(registry + 0x38);
  const auto end = *reinterpret_cast<const std::uintptr_t *>(registry + 0x40);
  const auto nodes =
      *reinterpret_cast<const std::uintptr_t *>(registry + 0x50);
  const auto sentinel =
      *reinterpret_cast<const std::uintptr_t *>(registry + 0x58);
  if (!begin || !end || end <= begin || !nodes)
    return nullptr;

  const auto bytes = end - begin;
  if (bytes % sizeof(std::uintptr_t))
    return nullptr;
  const auto count = bytes / sizeof(std::uintptr_t);
  if (!count || count > (1u << 20))
    return nullptr;

  std::int64_t index = *reinterpret_cast<const std::int64_t *>(
      begin + ((count - 1) & hash) * sizeof(std::uintptr_t));
  for (unsigned guard = 0; index != -1 && guard < 4096; ++guard) {
    const auto node = nodes + static_cast<std::uintptr_t>(index) * 32u;
    if (node == sentinel)
      return nullptr;
    if (*reinterpret_cast<const std::uint32_t *>(node + 8) == hash)
      return *reinterpret_cast<void *const *>(node + 0x10);
    index = *reinterpret_cast<const std::int64_t *>(node);
  }
  return nullptr;
}

bool ItemPhysicsRuntime::findPackedEntity(void *storage, std::uint32_t entity,
                                          std::uint32_t &packed) const noexcept {
  packed = 0;
  if (!storage)
    return false;

  const auto address = reinterpret_cast<std::uintptr_t>(storage);
  const auto begin =
      *reinterpret_cast<const std::uintptr_t *>(address + 0x08);
  const auto end = *reinterpret_cast<const std::uintptr_t *>(address + 0x10);
  if (!begin || !end || end < begin)
    return false;

  const auto pageIndex = (entity >> 11) & 0x7Fu;
  const auto pageCount = (end - begin) / sizeof(std::uintptr_t);
  if (pageIndex >= pageCount)
    return false;
  const auto page = *reinterpret_cast<const std::uintptr_t *>(
      begin + pageIndex * sizeof(std::uintptr_t));
  if (!page)
    return false;

  packed = *reinterpret_cast<const std::uint32_t *>(
      page + (entity & 0x7FFu) * sizeof(std::uint32_t));
  return (packed ^ (entity & 0xFFFC0000u)) <= 0x3FFFEu;
}

bool ItemPhysicsRuntime::hasOnGroundComponent(void *actor) const noexcept {
  if (!actor)
    return false;
  const auto address = reinterpret_cast<std::uintptr_t>(actor);
  const auto entity = *reinterpret_cast<const std::uint32_t *>(
      address + profile::kActorEntityIdOffset);
  std::uint32_t packed{};
  return findPackedEntity(
      findComponentStorage(actor, profile::kOnGroundFlagComponentHash), entity,
      packed);
}

bool ItemPhysicsRuntime::isBlockItem(std::uintptr_t actor) const noexcept {
  if (!actor)
    return false;

  if (*reinterpret_cast<void *const *>(actor + profile::kBlockPtrOffset))
    return true;
  if (!mGetBlockTypeForRendering)
    return false;

  const void *weak = mGetBlockTypeForRendering(
      reinterpret_cast<const void *>(actor + profile::kItemStackBaseOffset));
  if (!weak)
    return false;
  const auto control =
      *reinterpret_cast<const std::uintptr_t *>(weak);
  if (!control)
    return false;
  return *reinterpret_cast<const std::uintptr_t *>(control) != 0;
}

ItemPhysicsRuntime::VisualState &
ItemPhysicsRuntime::stateFor(std::uint32_t entity, std::int32_t age,
                             float bobOffset, float sample) noexcept {
  VisualState *freeSlot = nullptr;
  VisualState *oldest = &mStates[0];
  std::uint32_t oldestDistance = 0;

  for (auto &state : mStates) {
    if (state.used && state.entity == entity) {
      if (age < state.lastAge) {
        state = {};
        state.entity = entity;
        state.yRot = std::isfinite(bobOffset) ? bobOffset : 0.0f;
        state.lastAge = age;
        state.lastSample = sample;
        state.used = true;
        state.sampled = true;
      }
      state.lastSeen = mRenderCounter;
      return state;
    }
    if (!state.used && !freeSlot)
      freeSlot = &state;
    if (state.used) {
      const auto distance = mRenderCounter - state.lastSeen;
      if (distance >= oldestDistance) {
        oldestDistance = distance;
        oldest = &state;
      }
    }
  }

  auto &state = *(freeSlot ? freeSlot : oldest);
  state = {};
  state.entity = entity;
  state.lastSeen = mRenderCounter;
  state.lastAge = age;
  state.yRot = std::isfinite(bobOffset) ? bobOffset : 0.0f;
  state.lastSample = sample;
  state.used = true;
  state.sampled = true;
  return state;
}

void ItemPhysicsRuntime::updateRotation(VisualState &state, bool block,
                                        bool grounded, std::int32_t age,
                                        float sample) noexcept {
  if (!std::isfinite(sample))
    return;

  float delta = 0.0f;
  if (state.sampled)
    delta = sample - state.lastSample;
  state.sampled = true;
  state.lastSample = sample;
  state.lastAge = age;

  if (!std::isfinite(delta) || delta < 0.0f ||
      delta > kMaxContinuousDeltaTicks)
    return;

  if (!block && grounded) {
    // Java ItemPhysic snaps every non-block item only on the first real
    // on-ground frame. There is deliberately no pre-contact alignment.
    state.xRot = 0.0f;
    return;
  }

  if (!grounded)
    state.xRot += delta * kRotationPerTick * 2.0f * kDefaultRotationSpeed;
  // With Java's default oldRotation=false, block items freeze at their exact
  // airborne angle after contact. They are not spring-aligned to a face.
}

std::uint32_t ItemPhysicsRuntime::javaCopyCount(std::uint32_t count) noexcept {
  if (count > 48)
    return 5;
  if (count > 32)
    return 4;
  if (count > 16)
    return 3;
  if (count > 1)
    return 2;
  return 1;
}

void ItemPhysicsRuntime::onRender(void *self, void *ctx, void *renderData) {
  const auto original = mOriginal;
  if (!original)
    return;
  if (!mProfileSupported.load(std::memory_order_relaxed) || !ctx ||
      !renderData) {
    original(self, ctx, renderData);
    return;
  }

  const auto renderAddress = reinterpret_cast<std::uintptr_t>(renderData);
  auto *actor = *reinterpret_cast<void **>(
      renderAddress + profile::kRenderDataActorOffset);
  if (!actor) {
    original(self, ctx, renderData);
    return;
  }

  const auto actorAddress = reinterpret_cast<std::uintptr_t>(actor);
  const auto entity = *reinterpret_cast<const std::uint32_t *>(
      actorAddress + profile::kActorEntityIdOffset);
  const auto rawAge = *reinterpret_cast<const std::int32_t *>(
      actorAddress + profile::kItemAgeOffset);
  const auto age = std::max(rawAge, 0);
  const auto bobOffset = *reinterpret_cast<const float *>(
      actorAddress + profile::kItemBobOffset);

  float partial = mGetPartialTick ? mGetPartialTick(ctx) : 0.0f;
  if (!std::isfinite(partial))
    partial = 0.0f;
  partial = std::clamp(partial, 0.0f, 1.0f);
  const float sample = static_cast<float>(age) + partial;
  const bool grounded = hasOnGroundComponent(actor);
  const bool block = isBlockItem(actorAddress);

  ++mRenderCounter;
  auto &state = stateFor(entity, age, bobOffset, sample);
  updateRotation(state, block, grounded, age, sample);

  // Java keeps the first tick vanilla, but calculateRotation has already run.
  // This warm-up also learns Bedrock's route-specific model scale.
  if (sample < 1.0f) {
    float *const previousProbe = gObservedModelScale;
    gObservedModelScale = &state.modelScale;
    original(self, ctx, renderData);
    gObservedModelScale = previousProbe;
    return;
  }

  const auto count = static_cast<std::uint32_t>(
      *reinterpret_cast<const std::uint8_t *>(actorAddress +
                                             profile::kItemCountOffset));
  const auto copies = javaCopyCount(count);
  auto &frameFlag = *reinterpret_cast<std::uint8_t *>(
      actorAddress + profile::kIsInItemFrameOffset);
  const auto oldFrameFlag = frameFlag;
  frameFlag = 1;

  const auto *position = reinterpret_cast<const float *>(
      renderAddress + profile::kRenderDataPositionOffset);
  const float worldX = position[0];
  const float worldY = position[1];
  const float worldZ = position[2];

  JavaRandom random(entity);
  bool rendered = false;
  for (std::uint32_t copy = 0; copy < copies; ++copy) {
    float copyX = 0.0f;
    float copyY = 0.0f;
    float copyZ = 0.0f;

    const float routeScale =
        state.modelScale > 0.0f ? state.modelScale : kDefaultBlockScale;
    if (block && copy > 0) {
      copyX = (random.nextFloat() * 2.0f - 1.0f) * routeScale;
      copyY = (random.nextFloat() * 2.0f - 1.0f) * routeScale;
      copyZ = (random.nextFloat() * 2.0f - 1.0f) * routeScale;
    } else if (!block) {
      copyZ = -kFlatCopyCenterStep * static_cast<float>(copies - 1) * 0.5f +
              kFlatCopyCenterStep * kFlatModelScale *
                  static_cast<float>(copy);
    }

    MatrixPushScope scope(mGetWorldMatrix ? mGetWorldMatrix(ctx) : nullptr,
                          mMatrixPush, mMatrixRefDtor);
    Mat4 *matrix = scope.matrix();
    if (!matrix) {
      if (!rendered)
        original(self, ctx, renderData);
      break;
    }

    postTranslate(*matrix, worldX, worldY, worldZ);
    postRotateX(*matrix, kHalfPi);
    postRotateZ(*matrix, state.yRot);

    if (block) {
      postTranslate(*matrix, 0.0f, kBlockOffsetY, kBlockOffsetZ);
      postTranslate(*matrix, 0.0f, routeScale, 0.0f);
      postRotateY(*matrix, state.xRot);
      postTranslate(*matrix, 0.0f, -routeScale, 0.0f);
    } else {
      postTranslate(*matrix, 0.0f, 0.0f,
                    kFlatOffsetZ - bobOffset * kBobOffsetScale);
      postRotateY(*matrix, state.xRot);
    }
    postTranslate(*matrix, copyX, copyY, copyZ);
    postTranslate(*matrix, -worldX, -worldY, -worldZ);

    const bool previousForce = gForceSingleCopy;
    float *const previousProbe = gObservedModelScale;
    gForceSingleCopy = true;
    gObservedModelScale = &state.modelScale;
    original(self, ctx, renderData);
    gObservedModelScale = previousProbe;
    gForceSingleCopy = previousForce;
    rendered = true;
  }

  frameFlag = oldFrameFlag;
}

} // namespace itemphysics
