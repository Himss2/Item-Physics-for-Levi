#pragma once

#include "DropVisualState.hpp"
#include "MatrixMath.hpp"
#include "RttiResolver.hpp"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string_view>

#include <pl/Mod.hpp>
#include <pl/memory/Hook.hpp>

namespace itemphysics {

class ItemPhysicsRuntime {
public:
  ItemPhysicsRuntime() = default;
  ItemPhysicsRuntime(const ItemPhysicsRuntime &) = delete;
  ItemPhysicsRuntime &operator=(const ItemPhysicsRuntime &) = delete;

  bool install(ll::mod::NativeMod &);
  void uninstall();
  void clearStates() noexcept;
  void setEnabled(bool enabled) noexcept {
    mEnabled.store(enabled, std::memory_order_relaxed);
  }
  void setSingleModel(bool enabled) noexcept {
    mSingleModel.store(enabled, std::memory_order_relaxed);
  }
  void setSeparateDropVisuals(bool enabled) noexcept {
    const bool previous =
        mSeparateDropVisuals.exchange(enabled, std::memory_order_relaxed);
    if (previous != enabled)
      mClearDropVisualsRequested.store(true, std::memory_order_release);
  }
  void setHideItemShadow(bool enabled) noexcept {
    mHideItemShadow.store(enabled, std::memory_order_relaxed);
  }
  [[nodiscard]] bool profileSupported() const noexcept {
    return mProfileSupported.load(std::memory_order_relaxed);
  }

  using RenderFn = void (*)(void *, void *, void *);
  using RenderItemGroupFn = void (*)(void *, void *, void *, std::uint32_t,
                                     std::uint32_t, float, float);
  using GetWorldMatrixFn = void *(*)(void *);
  using GetPartialTickFn = float (*)(const void *);

  // MatrixStack::MatrixStackRef has a non-trivial destructor in Minecraft.
  // Keeping this ABI type non-trivial makes Clang use the correct sret ABI.
  struct MatrixStackRefAbi {
    void *stack{};
    Mat4 *mat{};
    ~MatrixStackRefAbi() {}
  };

  using MatrixPushFn = MatrixStackRefAbi (*)(void *, bool);
  using MatrixRefDtorFn = void (*)(MatrixStackRefAbi *);
  using ActorEventFn = void (*)(void *, std::uint32_t, std::uint32_t);
  using ActorRemoveFn = void (*)(void *);
  using GetActorUniqueIdFn = const std::int64_t *(*)(void *);

  // Called by the AArch64 entry bridge before forwarding Actor::remove.
  // Public only so the C-linkage bridge can keep a stable, unmangled target.
  static void dispatchActorRemove(void *, void *, std::uintptr_t);

private:
  struct AabbAbi {
    float minX{}, minY{}, minZ{}, maxX{}, maxY{}, maxZ{};
  };
  static_assert(sizeof(AabbAbi) == 24);

  struct Vec3Abi {
    float x{}, y{}, z{};
  };

  enum class HeightClass : std::uint8_t {
    FullBlock,
    ShapedBlock,
    FlatItem,
    HorizontalThin,
    Head,
    Special
  };

  enum class GroundCalibration : std::uint8_t {
    Default,
    Shield,
    Banner,
    FenceFamily,
    Scaffolding
  };

  struct BlockRenderInfo {
    bool keepHorizontal{};
    bool verticalPlane{};
    bool rodLike{};
    std::int32_t blockShape{-1};
  };

  struct ItemRenderTraits {
    bool valid{};
    bool block{};
    bool groundFlat{};
    bool dragonHead{};
    HeightClass height{HeightClass::FlatItem};
    GroundCalibration calibration{GroundCalibration::Default};
  };

  struct VisualState {
    ItemRenderTraits traits{};
    std::uint32_t entity{};
    std::uint32_t lastSeen{};
    std::int32_t lastAge{-1};
    float xRot{};
    float yRot{};
    float lastSample{};
    float lastWorldY{};
    float lastVerticalSpeed{};
    float modelScale{};
    DropVisualPose dropPose{};
    DropVisualLineage dropLineage{};
    FluidVisualBase fluidBase{};
    std::uint64_t uniqueId{};
    std::uintptr_t registry{};
    std::uintptr_t itemTypeKey{};
    std::uintptr_t blockKey{};
    std::int32_t lastProbeAge{-1};
    std::uint8_t stableContactTicks{};
    std::uint8_t movingTicks{};
    std::uint8_t fluidMissTicks{};
    std::uint8_t waterBobPhase{};
    bool used{};
    bool sampled{};
    bool traitsSampled{};
    bool positionSampled{};
    bool groundedLatched{};
    DropFluidKind fluid{DropFluidKind::None};
    bool shadowInitialized{};
    bool shadowHidden{};
    bool shadowGrounded{};
    bool dropIdentitySampled{};
    bool dropPoseSampled{};
  };

  enum class MergeSignalKind : std::uint8_t {
    None,
    ExactPair,
    CountChanged,
    Removed
  };

  struct MergeSignal {
    MergeSignalKind kind{};
    std::uint64_t actorId{};
    std::uint64_t otherId{};
    std::uintptr_t registry{};
    std::uint32_t sequence{};
    std::uint32_t renderStamp{};
    std::uint16_t count{};
    std::uint16_t oldCount{};
    std::uint16_t newCount{};
    std::uint8_t attempts{};
  };

  struct ComponentStorageCache {
    std::uintptr_t registry{};
    std::uintptr_t begin{};
    std::uintptr_t end{};
    std::uintptr_t nodes{};
    std::uintptr_t sentinel{};
    void *onGround{};
    void *verticalCollision{};
    void *inWater{};
    void *inLava{};
    void *relativeShadow{};
  };

  using GetPosDeltaFn = const Vec3Abi *(*)(const void *);
  using GetBlockTypeForRenderingFn = const void *(*)(const void *);
  using BlockGraphicsGetForBlockTypeFn = void *(*)(const void *);
  using BlockGraphicsGetForBlockFn = void *(*)(const void *);
  using BlockGraphicsGetBlockShapeFn = std::int32_t (*)(const void *);
  using IsBlockShape3DFn = bool (*)(std::int32_t);
  using GetVisualShapeFn = const AabbAbi *(*)(void *, const void *, AabbAbi *);
  struct ShadowStorageEmplaceResultAbi {
    std::uintptr_t first{}, second{};
  };
  using RelativeShadowStorageFn = void *(*)(void *, std::uint32_t);
  using RelativeShadowEmplaceFn = ShadowStorageEmplaceResultAbi (*)(
      void *, const std::uint32_t *, bool, const float *);

  static constexpr std::size_t kStateCapacity = 512;
  static constexpr std::size_t kStateProbeCount = 8;
  static constexpr std::size_t kDropAnchorCapacity = 96;
  static constexpr std::size_t kMaxDropAnchorsPerLineage = 16;
  static constexpr std::size_t kHookSignalCapacity = 64;
  static constexpr std::size_t kPendingSignalCapacity = 128;

  static ItemPhysicsRuntime *sInstance;
  static void renderDetour(void *, void *, void *);
  static void renderItemGroupDetour(void *, void *, void *, std::uint32_t,
                                    std::uint32_t, float, float);
  static void actorEventDetour(void *, std::uint32_t, std::uint32_t);

  void onRender(void *, void *, void *);
  void onRenderItemGroup(void *, void *, void *, std::uint32_t, std::uint32_t,
                         float, float);

  [[nodiscard]] bool verifyProfile(const ResolvedVirtual &,
                                   const ResolvedVirtual &,
                                   ll::mod::NativeMod &) const;
  [[nodiscard]] ItemRenderTraits classifyItem(std::uintptr_t) const noexcept;
  [[nodiscard]] static GroundCalibration
  calibrationForIdentifier(std::string_view) noexcept;
  [[nodiscard]] bool buildBlockRenderInfo(const void *,
                                          BlockRenderInfo &) const noexcept;
  [[nodiscard]] bool tryGetRenderBlockShape(std::uintptr_t,
                                            std::int32_t &) const noexcept;
  [[nodiscard]] std::string_view
  itemIdentifier(std::uintptr_t) const noexcept;
  [[nodiscard]] bool hasComponent(void *, std::uint32_t,
                                   bool cacheStorage = true) const noexcept;
  [[nodiscard]] bool hasOnGroundComponent(void *) const noexcept;
  [[nodiscard]] void *findComponentStorage(void *, std::uint32_t,
                                            bool cacheStorage = true) const noexcept;
  [[nodiscard]] bool findPackedEntity(void *, std::uint32_t,
                                      std::uint32_t &) const noexcept;
  [[nodiscard]] bool updateItemShadowComponent(void *, bool,
                                               bool) const noexcept;

  VisualState &stateFor(std::uint32_t, std::int32_t, float, float) noexcept;
  [[nodiscard]] bool resolveGrounded(VisualState &, void *, std::int32_t,
                                     float) const noexcept;
  static void updateRotation(VisualState &, bool, bool, bool, std::int32_t,
                             float) noexcept;
  [[nodiscard]] float heightOffset(const ItemRenderTraits &,
                                   bool) const noexcept;
  [[nodiscard]] float waterBobOffset(float sample,
                                     float sampleBias,
                                     DropFluidKind fluid) const noexcept;
  [[nodiscard]] float renderWorldY(float originalWorldY,
                                   const ItemRenderTraits &, bool grounded,
                                   DropFluidKind fluid, float sample,
                                   std::uint8_t phaseTick) const noexcept;
  [[nodiscard]] std::uint64_t actorUniqueId(void *) const noexcept;
  [[nodiscard]] std::uintptr_t itemTypeKey(std::uintptr_t) const noexcept;
  void enqueueMergeSignal(MergeSignal) noexcept;
  void drainMergeSignals() noexcept;
  void appendPendingSignal(MergeSignal) noexcept;
  void clearDropVisuals() noexcept;
  [[nodiscard]] VisualState *findStateByUniqueId(
      std::uint64_t, std::uintptr_t = 0) noexcept;
  [[nodiscard]] bool hasPendingCountChange(std::uint64_t,
                                           std::uintptr_t) const noexcept;
  void processPendingMerges(VisualState &, float, unsigned = 0) noexcept;
  void applyMergedLineage(VisualState &, VisualState &, std::uint16_t,
                          std::uint16_t, std::uint16_t, float,
                          MergeSignal *, MergeSignal *,
                          MergeSignal *) noexcept;
  void collapseUnresolvedCount(VisualState &, std::uint16_t,
                               MergeSignal &) noexcept;
  std::atomic_bool mEnabled{true};
  std::atomic_bool mSingleModel{false};
  std::atomic_bool mSeparateDropVisuals{false};
  std::atomic_bool mSeparateDropTrackingAvailable{false};
  std::atomic_bool mHideItemShadow{true};
  std::atomic_bool mProfileSupported{false};
  std::atomic_bool mClearDropVisualsRequested{false};
  std::uintptr_t mMinecraftBase{};
  std::uintptr_t mRenderTarget{};
  std::uintptr_t mRenderItemGroupTarget{};
  std::uintptr_t mActorEventTarget{};
  std::uintptr_t mActorRemoveTarget{};
  std::uintptr_t mItemActorVptr{};

  RenderFn mOriginal{};
  RenderItemGroupFn mRenderItemGroupOriginal{};
  GetWorldMatrixFn mGetWorldMatrix{};
  GetPartialTickFn mGetPartialTick{};
  MatrixPushFn mMatrixPush{};
  MatrixRefDtorFn mMatrixRefDtor{};
  ActorEventFn mActorEventOriginal{};
  ActorRemoveFn mActorRemoveOriginal{};
  GetActorUniqueIdFn mGetActorUniqueId{};
  GetPosDeltaFn mGetPosDelta{};
  GetBlockTypeForRenderingFn mGetBlockTypeForRendering{};
  BlockGraphicsGetForBlockTypeFn mGetBlockGraphicsForBlockType{};
  BlockGraphicsGetForBlockFn mGetBlockGraphicsForBlock{};
  BlockGraphicsGetBlockShapeFn mGetBlockGraphicsShape{};
  IsBlockShape3DFn mIsBlockShape3D{};
  RelativeShadowStorageFn mGetRelativeShadowStorage{};
  RelativeShadowEmplaceFn mEmplaceRelativeShadow{};

  std::unique_ptr<pl::memory::HookHandle> mHook;
  std::unique_ptr<pl::memory::HookHandle> mRenderItemGroupHook;
  std::unique_ptr<pl::memory::HookHandle> mActorEventHook;
  std::unique_ptr<pl::memory::HookHandle> mActorRemoveHook;
  std::array<VisualState, kStateCapacity> mStates{};
  DropVisualAnchorPool<kDropAnchorCapacity> mDropAnchors{};
  std::array<MergeSignal, kHookSignalCapacity> mHookSignals{};
  std::array<MergeSignal, kPendingSignalCapacity> mPendingSignals{};
  std::atomic_flag mSignalLock = ATOMIC_FLAG_INIT;
  std::atomic_bool mHookSignalsPending{false};
  std::atomic_uint32_t mSignalSequence{};
  std::uint16_t mHookSignalWrite{};
  std::uint16_t mHookSignalCount{};
  std::uint16_t mStateSweepCursor{};
  mutable ComponentStorageCache mComponentStorageCache{};
  std::uint32_t mRenderCounter{};
};

} // namespace itemphysics
