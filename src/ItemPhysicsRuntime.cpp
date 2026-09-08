#include "ItemPhysicsRuntime.hpp"
#include "StackVisualLayout.hpp"
#include "TargetProfile.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>

namespace itemphysics {
namespace {

constexpr float kRotationPerTick = 0.25f;
constexpr float kDefaultRotationSpeed = 1.0f;
constexpr float kBlockOffsetY = -0.20f;
constexpr float kBlockOffsetZ = -0.08f;
constexpr float kFlatOffsetZ = -0.04f;
constexpr float kBobOffsetScale = 0.007957747154594767f;
constexpr float kDefaultBlockScale = 0.25f;
constexpr float kFlatStackWorldStep = 0.055f;
constexpr float kBlockStackScaleStep = 0.32f;
constexpr float kMaxContinuousDeltaTicks = 10.0f;
constexpr float kWaterSurfaceLiftY = 0.125f;
constexpr std::uint32_t kWaterCycleTicks = 91u;
// One-tick samples of the approved waveform: 8 ticks at the bottom, a
// half-sine transition at approximately 0.10 radians/tick, 20 ticks at the
// top, then the mirrored transition. Linear partial-tick interpolation keeps
// motion smooth without a per-render libm call.
constexpr std::array<float, kWaterCycleTicks> kWaterBobSamples{
    -0.015000000f, -0.015000000f, -0.015000000f, -0.015000000f,
    -0.015000000f, -0.015000000f, -0.015000000f, -0.015000000f,
    -0.015000000f, -0.014925062f, -0.014700999f, -0.014330047f,
    -0.013815915f, -0.013163738f, -0.012380034f, -0.011472633f,
    -0.010450601f, -0.009324150f, -0.008104535f, -0.006803942f,
    -0.005435366f, -0.004012482f, -0.002549507f, -0.001061058f,
     0.000437993f,  0.001932667f,  0.003408031f,  0.004849344f,
     0.006242203f,  0.007572692f,  0.008827517f,  0.009994140f,
     0.011060906f,  0.012017154f,  0.012853331f,  0.013561082f,
     0.014133335f,  0.014564372f,  0.014849887f,  0.014987027f,
     0.015000000f,  0.015000000f,  0.015000000f,  0.015000000f,
     0.015000000f,  0.015000000f,  0.015000000f,  0.015000000f,
     0.015000000f,  0.015000000f,  0.015000000f,  0.015000000f,
     0.015000000f,  0.015000000f,  0.015000000f,  0.015000000f,
     0.015000000f,  0.015000000f,  0.015000000f,  0.015000000f,
     0.014974422f,  0.014812197f,  0.014501973f,  0.014046850f,
     0.013451376f,  0.012721500f,  0.011864516f,  0.010888985f,
     0.009804654f,  0.008622359f,  0.007353912f,  0.006011988f,
     0.004609993f,  0.003161937f,  0.001682288f,  0.000185830f,
    -0.001312485f, -0.002797686f, -0.004254933f, -0.005669666f,
    -0.007027750f, -0.008315615f, -0.009520393f, -0.010630047f,
    -0.011633488f, -0.012520692f, -0.013282793f, -0.013912176f,
    -0.014402554f, -0.014749027f, -0.014948131f};
constexpr float kFlatItemGroundY = -0.206f;
constexpr float kShapedBlockGroundY = -0.205f;
constexpr float kFullBlockGroundY = -0.087f;
constexpr float kHorizontalThinGroundY = -0.178f;
constexpr float kNormalHeadGroundY = 0.165f;
constexpr float kDragonHeadGroundY = 0.203f;
constexpr float kSpecialFallbackGroundY = 0.001f;
constexpr float kShieldGroundY = -0.147f;
constexpr float kBannerGroundY = -0.159f;
constexpr float kFenceGroundY = -0.171f;
constexpr float kScaffoldingGroundY = -0.081f;
constexpr std::int32_t kSkullShape = 83;
constexpr float kExtentEpsilon = 0.0005f;
constexpr float kThinYRatio = 0.70f;
constexpr std::uint32_t kWasInWaterFlagComponentHash = 0x78E89F39u;
constexpr std::uint8_t kWaterContactGraceTicks = 4u;

constexpr float kStablePositionEpsilon = 0.012f;
constexpr float kCollisionPositionEpsilon = 0.025f;
constexpr float kWakeVerticalSpeed = 0.085f;

constexpr const char *kShieldId = "minecraft:shield";
constexpr const char *kBannerId = "minecraft:banner";
constexpr const char *kDragonHeadId = "minecraft:dragon_head";

thread_local bool gForceSingleCopy = false;
thread_local float *gObservedModelScale = nullptr;

// Default Java-style stacks submit at most five copies; optional exact visual
// stacks may submit up to 64. These helpers consume one sine/cosine pair
// calculated for the ItemActor, so copy count never multiplies trigonometric
// work and the matrix equations remain identical to MatrixMath.hpp.
inline void postRotateXQuarter(Mat4 &matrix) noexcept {
  float c1[4];
  float c2[4];
  std::memcpy(c1, &matrix.m[4], sizeof(c1));
  std::memcpy(c2, &matrix.m[8], sizeof(c2));
  for (int row = 0; row < 4; ++row) {
    matrix.m[4 + row] = c2[row];
    matrix.m[8 + row] = -c1[row];
  }
}

inline void postRotateYKnown(Mat4 &matrix, float sine,
                             float cosine) noexcept {
  float c0[4];
  float c2[4];
  std::memcpy(c0, &matrix.m[0], sizeof(c0));
  std::memcpy(c2, &matrix.m[8], sizeof(c2));
  for (int row = 0; row < 4; ++row) {
    matrix.m[row] = cosine * c0[row] - sine * c2[row];
    matrix.m[8 + row] = sine * c0[row] + cosine * c2[row];
  }
}

inline void postRotateZKnown(Mat4 &matrix, float sine,
                             float cosine) noexcept {
  float c0[4];
  float c1[4];
  std::memcpy(c0, &matrix.m[0], sizeof(c0));
  std::memcpy(c1, &matrix.m[4], sizeof(c1));
  for (int row = 0; row < 4; ++row) {
    matrix.m[row] = cosine * c0[row] + sine * c1[row];
    matrix.m[4 + row] = -sine * c0[row] + cosine * c1[row];
  }
}

bool isThinGroundShape(std::int32_t shape) noexcept {
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

bool isTorchGroundShape(std::int32_t shape) noexcept {
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

bool isShapedGroundShape(std::int32_t shape) noexcept {
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
      {base + profile::kGetPosDeltaRva, "Actor::getPosDelta",
       matchesFingerprint(resolved.module, base + profile::kGetPosDeltaRva,
                          profile::kGetPosDeltaFingerprint)},
      {base + profile::kBlockGraphicsGetForBlockTypeRva,
       "BlockGraphics::getForBlock(BlockType)",
       matchesFingerprint(resolved.module,
                          base + profile::kBlockGraphicsGetForBlockTypeRva,
                          profile::kBlockGraphicsGetForBlockTypeFingerprint)},
      {base + profile::kBlockGraphicsGetForBlockRva,
       "BlockGraphics::getForBlock(Block)",
       matchesFingerprint(resolved.module,
                          base + profile::kBlockGraphicsGetForBlockRva,
                          profile::kBlockGraphicsGetForBlockFingerprint)},
      {base + profile::kBlockGraphicsGetBlockShapeRva,
       "BlockGraphics::getBlockShape",
       matchesFingerprint(resolved.module,
                          base + profile::kBlockGraphicsGetBlockShapeRva,
                          profile::kBlockGraphicsGetBlockShapeFingerprint)},
      {base + profile::kIsBlockShape3DRva, "BlockGraphics::isBlockShape3D",
       matchesFingerprint(resolved.module, base + profile::kIsBlockShape3DRva,
                          profile::kIsBlockShape3DFingerprint)},
      {base + profile::kRelativeShadowStorageRva,
       "RelativeShadowOffsetComponent storage",
       matchesFingerprint(resolved.module,
                          base + profile::kRelativeShadowStorageRva,
                          profile::kRelativeShadowStorageFingerprint)},
      {base + profile::kRelativeShadowEmplaceRva,
       "RelativeShadowOffsetComponent emplace",
       matchesFingerprint(resolved.module,
                          base + profile::kRelativeShadowEmplaceRva,
                          profile::kRelativeShadowEmplaceFingerprint)},
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
  mGetPosDelta = reinterpret_cast<GetPosDeltaFn>(
      mMinecraftBase + profile::kGetPosDeltaRva);
  mGetBlockGraphicsForBlockType =
      reinterpret_cast<BlockGraphicsGetForBlockTypeFn>(
          mMinecraftBase + profile::kBlockGraphicsGetForBlockTypeRva);
  mGetBlockGraphicsForBlock = reinterpret_cast<BlockGraphicsGetForBlockFn>(
      mMinecraftBase + profile::kBlockGraphicsGetForBlockRva);
  mGetBlockGraphicsShape = reinterpret_cast<BlockGraphicsGetBlockShapeFn>(
      mMinecraftBase + profile::kBlockGraphicsGetBlockShapeRva);
  mIsBlockShape3D = reinterpret_cast<IsBlockShape3DFn>(
      mMinecraftBase + profile::kIsBlockShape3DRva);
  mGetRelativeShadowStorage = reinterpret_cast<RelativeShadowStorageFn>(
      mMinecraftBase + profile::kRelativeShadowStorageRva);
  mEmplaceRelativeShadow = reinterpret_cast<RelativeShadowEmplaceFn>(
      mMinecraftBase + profile::kRelativeShadowEmplaceRva);

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
  mGetPosDelta = nullptr;
  mGetBlockTypeForRendering = nullptr;
  mGetBlockGraphicsForBlockType = nullptr;
  mGetBlockGraphicsForBlock = nullptr;
  mGetBlockGraphicsShape = nullptr;
  mIsBlockShape3D = nullptr;
  mGetRelativeShadowStorage = nullptr;
  mEmplaceRelativeShadow = nullptr;
  mMinecraftBase = 0;
  mRenderTarget = 0;
  mRenderItemGroupTarget = 0;
  clearStates();
}

void ItemPhysicsRuntime::clearStates() noexcept {
  mStates = {};
  mComponentStorageCache = {};
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

  auto &cache = mComponentStorageCache;
  if (cache.registry != registry || cache.begin != begin || cache.end != end ||
      cache.nodes != nodes || cache.sentinel != sentinel) {
    cache = {};
    cache.registry = registry;
    cache.begin = begin;
    cache.end = end;
    cache.nodes = nodes;
    cache.sentinel = sentinel;
  }

  void **cached = nullptr;
  switch (hash) {
  case profile::kOnGroundFlagComponentHash:
    cached = &cache.onGround;
    break;
  case profile::kVerticalCollisionFlagComponentHash:
    cached = &cache.verticalCollision;
    break;
  case kWasInWaterFlagComponentHash:
    cached = &cache.inWater;
    break;
  case profile::kRelativeShadowOffsetComponentHash:
    cached = &cache.relativeShadow;
    break;
  default:
    break;
  }
  if (cached && *cached)
    return *cached;

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
    if (*reinterpret_cast<const std::uint32_t *>(node + 8) == hash) {
      void *const storage =
          *reinterpret_cast<void *const *>(node + 0x10);
      if (cached)
        *cached = storage;
      return storage;
    }
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

bool ItemPhysicsRuntime::hasComponent(void *actor,
                                      std::uint32_t hash) const noexcept {
  if (!actor)
    return false;
  const auto address = reinterpret_cast<std::uintptr_t>(actor);
  const auto entity = *reinterpret_cast<const std::uint32_t *>(
      address + profile::kActorEntityIdOffset);
  std::uint32_t packed{};
  return findPackedEntity(findComponentStorage(actor, hash), entity, packed);
}

bool ItemPhysicsRuntime::hasOnGroundComponent(void *actor) const noexcept {
  return hasComponent(actor, profile::kOnGroundFlagComponentHash);
}

bool ItemPhysicsRuntime::updateItemShadowComponent(void *actor, bool grounded,
                                                   bool hide) const noexcept {
  if (!actor || !mGetRelativeShadowStorage || !mEmplaceRelativeShadow)
    return false;

  const auto actorAddress = reinterpret_cast<std::uintptr_t>(actor);
  auto *registry = *reinterpret_cast<void **>(
      actorAddress + profile::kActorRegistryOffset);
  const auto entity = *reinterpret_cast<const std::uint32_t *>(
      actorAddress + profile::kActorEntityIdOffset);
  if (!registry)
    return false;

  void *storage = findComponentStorage(
      actor, profile::kRelativeShadowOffsetComponentHash);
  if (!storage) {
    storage = mGetRelativeShadowStorage(
        registry, profile::kRelativeShadowOffsetComponentHash);
  }
  if (!storage)
    return false;

  const float wanted = hide ? std::numeric_limits<float>::max()
                            : (grounded ? 0.0f : -0.5f);
  std::uint32_t packed{};
  if (!findPackedEntity(storage, entity, packed)) {
    const std::uint32_t entityCopy = entity;
    (void)mEmplaceRelativeShadow(storage, &entityCopy, false, &wanted);
    return true;
  }

  const auto dense = packed & 0x3FFFFu;
  const auto storageAddress = reinterpret_cast<std::uintptr_t>(storage);
  const auto pages = *reinterpret_cast<const std::uintptr_t *>(
      storageAddress + 0x50);
  if (!pages)
    return false;
  const auto page = *reinterpret_cast<const std::uintptr_t *>(
      pages + (dense >> 7) * sizeof(std::uintptr_t));
  if (!page)
    return false;
  *reinterpret_cast<float *>(
      page + static_cast<std::uintptr_t>(dense & 0x7Fu) * sizeof(float)) =
      wanted;
  return true;
}

std::string_view
ItemPhysicsRuntime::itemIdentifier(std::uintptr_t actor) const noexcept {
  if (!actor)
    return {};

  const auto handle = *reinterpret_cast<const std::uintptr_t *>(
      actor + profile::kItemHandleOffset);
  if (!handle)
    return {};
  const auto item = *reinterpret_cast<const std::uintptr_t *>(handle);
  if (!item)
    return {};

  const auto *raw = reinterpret_cast<const std::uint8_t *>(
      item + profile::kItemIdentifierOffset);
  const std::uint8_t flag = raw[0];
  const char *data = nullptr;
  std::size_t length = 0;
  if ((flag & 1u) == 0) {
    length = static_cast<std::size_t>(flag >> 1);
    data = reinterpret_cast<const char *>(raw + 1);
  } else {
    length = *reinterpret_cast<const std::size_t *>(raw + 8);
    data = *reinterpret_cast<const char *const *>(raw + 16);
  }

  if (!data || length > 256u)
    return {};
  return {data, length};
}

bool ItemPhysicsRuntime::tryGetRenderBlockShape(
    std::uintptr_t actor, std::int32_t &shape) const noexcept {
  shape = -1;
  if (!actor || !mGetBlockTypeForRendering ||
      !mGetBlockGraphicsForBlockType || !mGetBlockGraphicsShape)
    return false;

  const void *weak = mGetBlockTypeForRendering(
      reinterpret_cast<const void *>(actor + profile::kItemStackBaseOffset));
  if (!weak)
    return false;
  const auto control = *reinterpret_cast<const std::uintptr_t *>(weak);
  if (!control)
    return false;
  const auto type = *reinterpret_cast<const std::uintptr_t *>(control);
  if (!type)
    return false;
  const void *graphics = mGetBlockGraphicsForBlockType(
      reinterpret_cast<const void *>(type));
  if (!graphics)
    return false;
  shape = mGetBlockGraphicsShape(graphics);
  return true;
}

bool ItemPhysicsRuntime::buildBlockRenderInfo(
    const void *block, BlockRenderInfo &info) const noexcept {
  info = {};
  info.blockShape = -1;
  if (!block)
    return false;

  if (mGetBlockGraphicsForBlock && mGetBlockGraphicsShape) {
    if (const void *graphics = mGetBlockGraphicsForBlock(block))
      info.blockShape = mGetBlockGraphicsShape(graphics);
  }

  const auto blockAddress = reinterpret_cast<std::uintptr_t>(block);
  auto *type = *reinterpret_cast<void *const *>(
      blockAddress + profile::kBlockTypeOffset);
  if (type) {
    auto **vtable = *reinterpret_cast<void ***>(type);
    if (vtable) {
      constexpr std::size_t slot =
          profile::kBlockTypeGetVisualShapeVtableOffset / sizeof(void *);
      const auto getVisualShape =
          reinterpret_cast<GetVisualShapeFn>(vtable[slot]);
      if (getVisualShape) {
        AabbAbi scratch{};
        if (const AabbAbi *box = getVisualShape(type, block, &scratch)) {
          const float dx = box->maxX - box->minX;
          const float dy = box->maxY - box->minY;
          const float dz = box->maxZ - box->minZ;
          if (std::isfinite(dx) && std::isfinite(dy) && std::isfinite(dz) &&
              dx > kExtentEpsilon && dy > kExtentEpsilon &&
              dz > kExtentEpsilon) {
            const float horizontalMin = std::min(dx, dz);
            const float horizontalMax = std::max(dx, dz);
            info.keepHorizontal = dy <= horizontalMin * kThinYRatio;
            info.verticalPlane =
                dy > 0.55f && horizontalMin <= 0.34f &&
                horizontalMax >= 0.68f;
            info.rodLike = dy > 0.55f && horizontalMax <= 0.60f;
          }
        }
      }
    }
  }

  if (info.blockShape == kSkullShape)
    info.keepHorizontal = false;
  return true;
}

ItemPhysicsRuntime::GroundCalibration
ItemPhysicsRuntime::calibrationForIdentifier(std::string_view id) noexcept {
  if (id == kShieldId)
    return GroundCalibration::Shield;
  if (id == kBannerId || id.ends_with("_banner"))
    return GroundCalibration::Banner;
  if (id == "minecraft:scaffolding")
    return GroundCalibration::Scaffolding;
  if (id == "minecraft:fence" || id == "minecraft:fence_gate" ||
      id.ends_with("_fence") || id.ends_with("_fence_gate"))
    return GroundCalibration::FenceFamily;
  return GroundCalibration::Default;
}

ItemPhysicsRuntime::ItemRenderTraits
ItemPhysicsRuntime::classifyItem(std::uintptr_t actor) const noexcept {
  ItemRenderTraits traits{};
  if (!actor)
    return traits;

  std::int32_t renderShape = -1;
  const bool hasRenderShape = tryGetRenderBlockShape(actor, renderShape);
  const auto *block = *reinterpret_cast<const void *const *>(
      actor + profile::kBlockPtrOffset);
  const auto id = itemIdentifier(actor);
  traits.calibration = calibrationForIdentifier(id);

  if (!block && !hasRenderShape) {
    const bool banner = id == kBannerId || id.ends_with("_banner");
    traits.valid = true;
    traits.block = false;
    traits.height =
        id == kShieldId || banner
            ? HeightClass::Special
            : HeightClass::FlatItem;
    return traits;
  }

  traits.valid = true;
  traits.block = true;
  BlockRenderInfo info{};
  if (block)
    (void)buildBlockRenderInfo(block, info);
  if (info.blockShape < 0 && hasRenderShape)
    info.blockShape = renderShape;
  const std::int32_t shape = hasRenderShape ? renderShape : info.blockShape;
  const bool structuralFlat =
      id == "minecraft:ladder" || id.ends_with("_fence") ||
      id.ends_with("_bars") || id.ends_with("_pane");
  const bool horizontalSurface =
      id == "minecraft:slab" || id.find("_slab") != std::string_view::npos ||
      id == "minecraft:trapdoor" || id.ends_with("_trapdoor") ||
      id == "minecraft:carpet" || id.ends_with("_carpet") ||
      id == "minecraft:pressure_plate" ||
      id.ends_with("_pressure_plate") ||
      id == "minecraft:rail" || id.ends_with("_rail") ||
      id == "minecraft:snow_layer" || id == "minecraft:lily_pad" ||
      id == "minecraft:waterlily" ||
      id.starts_with("minecraft:daylight_detector");

  if (shape == kSkullShape) {
    traits.height = HeightClass::Head;
    traits.dragonHead = id == kDragonHeadId;
  } else if (structuralFlat) {
    traits.height = HeightClass::ShapedBlock;
    traits.groundFlat = true;
  } else if (horizontalSurface) {
    traits.height = HeightClass::HorizontalThin;
    traits.groundFlat = true;
  } else if (info.keepHorizontal || isThinGroundShape(shape)) {
    traits.height = HeightClass::HorizontalThin;
    traits.groundFlat = true;
  } else if (info.verticalPlane || info.rodLike ||
             isTorchGroundShape(shape) || isShapedGroundShape(shape) ||
             (shape >= 0 && mIsBlockShape3D && !mIsBlockShape3D(shape))) {
    traits.height = HeightClass::ShapedBlock;
    // Fence, ladder, bars, panes, rods and torch-like models need the same
    // contact pose as a flat item. Other shaped 3D blocks (for example a
    // decorated pot) still preserve the exact airborne angle at contact.
    traits.groundFlat = info.verticalPlane || info.rodLike ||
                        isTorchGroundShape(shape);
  } else {
    traits.height = HeightClass::FullBlock;
  }
  return traits;
}

ItemPhysicsRuntime::VisualState &
ItemPhysicsRuntime::stateFor(std::uint32_t entity, std::int32_t age,
                             float bobOffset, float sample) noexcept {
  static_assert((kStateCapacity & (kStateCapacity - 1u)) == 0u);
  const auto initialize = [&](VisualState &state) -> VisualState & {
    state = {};
    state.entity = entity;
    state.lastSeen = mRenderCounter;
    state.lastAge = age;
    state.yRot = std::isfinite(bobOffset) ? bobOffset : 0.0f;
    state.waterBobPhase = static_cast<std::uint8_t>(
        (entity * 2654435761u) % kWaterCycleTicks);
    state.lastSample = sample;
    state.used = true;
    state.sampled = true;
    return state;
  };

  const std::size_t first =
      (static_cast<std::size_t>(entity) * 2654435761u) &
      (kStateCapacity - 1u);
  VisualState *oldest = &mStates[first];
  std::uint32_t oldestDistance = 0;

  for (std::size_t probe = 0; probe < kStateProbeCount; ++probe) {
    auto &state = mStates[(first + probe) & (kStateCapacity - 1u)];
    if (state.used && state.entity == entity) {
      if (age < state.lastAge)
        return initialize(state);
      state.lastSeen = mRenderCounter;
      return state;
    }
    if (!state.used)
      return initialize(state);

    const auto distance = mRenderCounter - state.lastSeen;
    if (distance >= oldestDistance) {
      oldestDistance = distance;
      oldest = &state;
    }
  }
  return initialize(*oldest);
}

bool ItemPhysicsRuntime::resolveGrounded(VisualState &state, void *actor,
                                         std::int32_t age,
                                         float worldY) const noexcept {
  if (age == state.lastProbeAge)
    return state.groundedLatched;

  const bool nativeGrounded = hasOnGroundComponent(actor);
  const bool verticalCollision = hasComponent(
      actor, profile::kVerticalCollisionFlagComponentHash);
  const bool nativeInWater =
      hasComponent(actor, kWasInWaterFlagComponentHash);

  if (nativeInWater) {
    state.inWater = true;
    state.waterMissTicks = 0;
  } else if (state.inWater &&
             state.waterMissTicks < kWaterContactGraceTicks) {
    ++state.waterMissTicks;
  } else {
    state.inWater = false;
    state.waterMissTicks = 0;
  }

  float verticalSpeed = 0.0f;
  bool hasMotion = false;
  if (mGetPosDelta) {
    if (const auto *motion = mGetPosDelta(actor);
        motion && std::isfinite(motion->x) && std::isfinite(motion->y) &&
        std::isfinite(motion->z)) {
      verticalSpeed = motion->y;
      hasMotion = true;
    }
  }

  if (nativeGrounded) {
    state.groundedLatched = true;
    state.stableContactTicks = 4;
    state.movingTicks = 0;
  } else if (state.inWater) {
    // Java's calculateFluid path keeps a floating item airborne. A stable
    // water-surface Y must never enter the mob-drop ground fallback.
    state.groundedLatched = false;
    state.stableContactTicks = 0;
    state.movingTicks = 0;
  }

  // Render may be called many times per game tick. The fallback deliberately
  // samples only when ItemActor::age advances, so FPS cannot make an airborne
  // item appear stable. VerticalCollision alone is also insufficient (it can
  // mean a ceiling). A stable world Y is the primary fallback because Bedrock
  // may retain a small gravity delta even while a mob-spawned item is already
  // blocked by the floor.
  if (age != state.lastProbeAge && std::isfinite(worldY)) {
    const bool hasPositionDelta = state.positionSampled;
    const float positionDelta =
        hasPositionDelta ? worldY - state.lastWorldY : 0.0f;
    const bool stablePosition =
        hasPositionDelta &&
        std::abs(positionDelta) <= kStablePositionEpsilon;
    const bool stableCollision =
        hasPositionDelta && verticalCollision &&
        std::abs(positionDelta) <= kCollisionPositionEpsilon;
    // Camera crouch changes render-space Y, but it does not change the
    // ItemActor velocity. Only actor motion may release an established ground
    // latch; this prevents sneak from restarting the tumble.
    const bool moving =
        hasMotion && std::abs(verticalSpeed) >= kWakeVerticalSpeed;

    if (!nativeGrounded && !state.inWater) {
      if (!state.groundedLatched) {
        state.stableContactTicks = stableCollision || stablePosition
                                       ? static_cast<std::uint8_t>(
                                             std::min<unsigned>(
                                                 state.stableContactTicks + 1u,
                                                 4u))
                                       : 0;
        const std::uint8_t requiredTicks = verticalCollision ? 2u : 4u;
        if (state.stableContactTicks >= requiredTicks)
          state.groundedLatched = true;
      } else {
        state.movingTicks =
            moving && !verticalCollision
                ? static_cast<std::uint8_t>(
                      std::min<unsigned>(state.movingTicks + 1u, 2u))
                : 0;
        if (state.movingTicks >= 2) {
          state.groundedLatched = false;
          state.stableContactTicks = 0;
          state.movingTicks = 0;
        }
      }
    }

    state.lastProbeAge = age;
    state.lastWorldY = worldY;
    state.positionSampled = true;
  }
  return nativeGrounded || state.groundedLatched;
}

void ItemPhysicsRuntime::updateRotation(VisualState &state,
                                        bool keepsLandingAngle,
                                        bool grounded, bool inWater,
                                        std::int32_t age,
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

  if (!keepsLandingAngle && grounded) {
    // Flat items and thin structural block models snap only on the first real
    // ground frame. There is deliberately no pre-contact alignment.
    state.xRot = 0.0f;
    return;
  }

  if (!grounded && !inWater) {
    // Keep the Java airborne tumble exact. Once Bedrock reports water, freeze
    // the last roll; the render path may add vertical motion but never spin.
    state.xRot += delta * kRotationPerTick * 2.0f * kDefaultRotationSpeed;
  }
  // Full 3D blocks freeze at their exact airborne angle after contact. Heads
  // reach this same boundary first; onRender then selects their fixed prone
  // pose only after confirmed contact. Nothing is pre-aligned in the air.
}

float ItemPhysicsRuntime::heightOffset(const ItemRenderTraits &traits,
                                       bool grounded) const noexcept {
  if (!grounded)
    return 0.0f;

  switch (traits.calibration) {
  case GroundCalibration::Shield:
    return kShieldGroundY;
  case GroundCalibration::Banner:
    return kBannerGroundY;
  case GroundCalibration::FenceFamily:
    return kFenceGroundY;
  case GroundCalibration::Scaffolding:
    return kScaffoldingGroundY;
  case GroundCalibration::Default:
    break;
  }

  switch (traits.height) {
  case HeightClass::FullBlock:
    return kFullBlockGroundY;
  case HeightClass::ShapedBlock:
    return kShapedBlockGroundY;
  case HeightClass::HorizontalThin:
    return kHorizontalThinGroundY;
  case HeightClass::Head:
    return traits.dragonHead ? kDragonHeadGroundY : kNormalHeadGroundY;
  case HeightClass::Special:
    return kSpecialFallbackGroundY;
  case HeightClass::FlatItem:
    return kFlatItemGroundY;
  }
  return 0.0f;
}

float ItemPhysicsRuntime::waterBobOffset(float sample,
                                         std::uint8_t phaseTick) const noexcept {
  if (!std::isfinite(sample) || sample < 0.0f ||
      sample > static_cast<float>(std::numeric_limits<std::uint32_t>::max()))
    return 0.0f;

  const auto wholeTick = static_cast<std::uint32_t>(sample);
  const float partial = sample - static_cast<float>(wholeTick);
  std::uint32_t index =
      wholeTick % kWaterCycleTicks + phaseTick % kWaterCycleTicks;
  if (index >= kWaterCycleTicks)
    index -= kWaterCycleTicks;
  const std::uint32_t next =
      index + 1u == kWaterCycleTicks ? 0u : index + 1u;
  const float from = kWaterBobSamples[index];
  return from + (kWaterBobSamples[next] - from) * partial;
}

float ItemPhysicsRuntime::renderWorldY(
    float originalWorldY, const ItemRenderTraits &traits, bool grounded,
    bool inWater, float sample, std::uint8_t phaseTick) const noexcept {
  const float waterSurfaceLift = inWater ? kWaterSurfaceLiftY : 0.0f;
  const float waterBob = inWater ? waterBobOffset(sample, phaseTick) : 0.0f;
  return originalWorldY + heightOffset(traits, grounded) + waterSurfaceLift +
         waterBob;
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

  auto *position = reinterpret_cast<float *>(
      renderAddress + profile::kRenderDataPositionOffset);
  const float originalWorldY = position[1];

  float partial = mGetPartialTick ? mGetPartialTick(ctx) : 0.0f;
  if (!std::isfinite(partial))
    partial = 0.0f;
  partial = std::clamp(partial, 0.0f, 1.0f);
  const float sample = static_cast<float>(age) + partial;

  ++mRenderCounter;
  auto &state = stateFor(entity, age, bobOffset, sample);
  const bool grounded = resolveGrounded(state, actor, age, originalWorldY);
  const bool enabled = mEnabled.load(std::memory_order_relaxed);
  const bool hideShadow =
      enabled && mHideItemShadow.load(std::memory_order_relaxed);
  const bool shadowChanged =
      !state.shadowInitialized || state.shadowHidden != hideShadow ||
      (!hideShadow && state.shadowGrounded != grounded);
  if (shadowChanged &&
      updateItemShadowComponent(actor, grounded, hideShadow)) {
    state.shadowInitialized = true;
    state.shadowHidden = hideShadow;
    state.shadowGrounded = grounded;
  }
  if (!enabled) {
    original(self, ctx, renderData);
    return;
  }

  if (!state.traitsSampled) {
    const auto classified = classifyItem(actorAddress);
    if (!classified.valid) {
      original(self, ctx, renderData);
      return;
    }
    state.traits = classified;
    state.traitsSampled = true;
  }
  const auto &traits = state.traits;
  updateRotation(state, traits.block && !traits.groundFlat, grounded,
                 state.inWater, age, sample);

  // Java keeps the first tick vanilla, but calculateRotation has already run.
  // This warm-up also learns Bedrock's route-specific model scale.
  if (sample < 1.0f) {
    float *const previousProbe = gObservedModelScale;
    gObservedModelScale = &state.modelScale;
    original(self, ctx, renderData);
    gObservedModelScale = previousProbe;
    return;
  }

  if (grounded && traits.height == HeightClass::Head) {
    // Bedrock's skull renderer owns extra model transforms (and Dragon Head
    // also owns an animated jaw), so an outer support estimate cannot make
    // every arbitrary frozen angle reliable. Preserve the complete Java flip
    // until real contact, then use one fixed prone pose. Native yRot remains
    // untouched, so each head can still point in a different horizontal
    // direction without ever selecting an up/down-facing rest alternative.
    state.xRot = 0.0f;
  }

  // Evaluate each changing angle once per ItemActor render. Multi-copy stacks
  // reuse these values rather than calling libm two to ten extra times.
  const float xRotSine = std::sin(state.xRot);
  const float xRotCosine = std::cos(state.xRot);
  const float yRotSine = std::sin(state.yRot);
  const float yRotCosine = std::cos(state.yRot);

  const auto count = static_cast<std::uint32_t>(
      *reinterpret_cast<const std::uint8_t *>(actorAddress +
                                             profile::kItemCountOffset));
  const bool singleModel = mSingleModel.load(std::memory_order_relaxed);
  const bool realItemModels =
      mRealItemModels.load(std::memory_order_relaxed);
  const auto copies =
      selectVisualCopyCount(count, singleModel, realItemModels);
  const bool compactCopyLayout = realItemModels && !singleModel;
  auto &frameFlag = *reinterpret_cast<std::uint8_t *>(
      actorAddress + profile::kIsInItemFrameOffset);
  const auto oldFrameFlag = frameFlag;
  frameFlag = 1;

  const float worldX = position[0];
  // Bedrock keeps the ItemActor centre approximately half of its 0.25-block
  // height below the visible water line. Apply one class-independent visual
  // lift while its native water component is live; do not alter actor motion.
  const float worldY = renderWorldY(originalWorldY, traits, grounded,
                                    state.inWater, sample,
                                    state.waterBobPhase);
  const float worldZ = position[2];
  position[1] = worldY;

  const float routeScale =
      state.modelScale > 0.0f ? state.modelScale : kDefaultBlockScale;
  const float stackStep =
      traits.block
          ? std::max(kFlatStackWorldStep,
                     routeScale * kBlockStackScaleStep)
          : kFlatStackWorldStep;
  const float stackDirectionX = yRotCosine;
  const float stackDirectionZ = yRotSine;
  bool rendered = false;
  for (std::uint32_t copy = 0; copy < copies; ++copy) {
    // Keep Java's approved 1..5-copy row unchanged while the optional exact
    // mode uses a compact XZ-only grid. No copy receives a Y displacement, so
    // visual stack count cannot reintroduce vertical stacking or floating.
    const StackXZOffset localOffset =
        compactCopyLayout
            ? compactGridOffset(copy, copies, stackStep)
            : centeredRowOffset(copy, copies, stackStep);
    const StackXZOffset worldOffset = rotateStackOffset(
        localOffset, stackDirectionZ, stackDirectionX);
    const float copyWorldX = worldOffset.x;
    const float copyWorldZ = worldOffset.z;

    MatrixPushScope scope(mGetWorldMatrix ? mGetWorldMatrix(ctx) : nullptr,
                          mMatrixPush, mMatrixRefDtor);
    Mat4 *matrix = scope.matrix();
    if (!matrix) {
      if (!rendered)
        original(self, ctx, renderData);
      break;
    }

    // Offset the copy before model rotation, in world XZ. This prevents an
    // arbitrary frozen item angle from turning a side offset into vertical
    // stacking or a floating copy.
    postTranslate(*matrix, worldX + copyWorldX, worldY,
                  worldZ + copyWorldZ);
    const bool horizontalSurfacePose =
        (grounded || state.inWater) &&
        traits.height == HeightClass::HorizontalThin;
    if (horizontalSurfacePose) {
      // A slab/trapdoor/carpet is already horizontal in its native block
      // model. Java's universal X+90 pose turns that thin axis vertical, and
      // changing xRot cannot undo it. At solid contact or while floating,
      // preserve the Java block translation in world space and retain only a
      // harmless surface yaw. Do not apply an AABB centre drop here: Bedrock's
      // item origin is not the visual AABB centre, and that extra subtraction
      // is what pushed slabs/trapdoors/carpets through the ground. The same
      // world-up basis is used while floating so these models cannot become
      // vertical at the water surface.
      postTranslate(*matrix, kBlockOffsetY * -yRotSine,
                    -kBlockOffsetZ,
                    kBlockOffsetY * yRotCosine);
      postRotateYKnown(*matrix, yRotSine, yRotCosine);
    } else {
      postRotateXQuarter(*matrix);
      postRotateZKnown(*matrix, yRotSine, yRotCosine);
    }

    if (!horizontalSurfacePose) {
      if (traits.block) {
        postTranslate(*matrix, 0.0f, kBlockOffsetY, kBlockOffsetZ);
        // Java has one block transform, including for every skull and Dragon
        // Head. A head-only local-Z pivot causes a visible horizontal orbit.
        postTranslate(*matrix, 0.0f, routeScale, 0.0f);
        postRotateYKnown(*matrix, xRotSine, xRotCosine);
        postTranslate(*matrix, 0.0f, -routeScale, 0.0f);
      } else {
        postTranslate(*matrix, 0.0f, 0.0f,
                      kFlatOffsetZ - bobOffset * kBobOffsetScale);
        postRotateYKnown(*matrix, xRotSine, xRotCosine);
      }
    }
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
  position[1] = originalWorldY;
}

} // namespace itemphysics
