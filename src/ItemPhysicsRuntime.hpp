#pragma once

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
  void setHideItemShadow(bool enabled) noexcept {
    mHideItemShadow.store(enabled, std::memory_order_relaxed);
  }
  void setFlatGroundHeight(float height) noexcept;
  void setShapedGroundHeight(float height) noexcept;
  void setFullBlockGroundHeight(float height) noexcept;
  void setHorizontalThinGroundHeight(float height) noexcept;
  void setSpecialGroundHeight(float height) noexcept;
  void setNormalHeadGroundHeight(float height) noexcept;
  void setDragonHeadGroundHeight(float height) noexcept;
  void setWaterBobSpeed(float multiplier) noexcept;

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
    float modelScale{};
    std::int32_t lastProbeAge{-1};
    std::uint8_t stableContactTicks{};
    std::uint8_t movingTicks{};
    std::uint8_t waterMissTicks{};
    bool used{};
    bool sampled{};
    bool traitsSampled{};
    bool positionSampled{};
    bool groundedLatched{};
    bool inWater{};
    bool shadowInitialized{};
    bool shadowHidden{};
    bool shadowGrounded{};
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

  static ItemPhysicsRuntime *sInstance;
  static void renderDetour(void *, void *, void *);
  static void renderItemGroupDetour(void *, void *, void *, std::uint32_t,
                                    std::uint32_t, float, float);

  void onRender(void *, void *, void *);
  void onRenderItemGroup(void *, void *, void *, std::uint32_t, std::uint32_t,
                         float, float);

  [[nodiscard]] bool verifyProfile(const ResolvedVirtual &,
                                   ll::mod::NativeMod &) const;
  [[nodiscard]] ItemRenderTraits classifyItem(std::uintptr_t) const noexcept;
  [[nodiscard]] bool buildBlockRenderInfo(const void *,
                                          BlockRenderInfo &) const noexcept;
  [[nodiscard]] bool tryGetRenderBlockShape(std::uintptr_t,
                                            std::int32_t &) const noexcept;
  [[nodiscard]] std::string_view
  itemIdentifier(std::uintptr_t) const noexcept;
  [[nodiscard]] bool hasComponent(void *, std::uint32_t) const noexcept;
  [[nodiscard]] bool hasOnGroundComponent(void *) const noexcept;
  [[nodiscard]] void *findComponentStorage(void *, std::uint32_t) const noexcept;
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
                                     float phase) const noexcept;
  [[nodiscard]] float renderWorldY(float originalWorldY,
                                   const ItemRenderTraits &, bool grounded,
                                   bool inWater, float sample,
                                   float phase) const noexcept;
  static std::uint32_t javaCopyCount(std::uint32_t) noexcept;

  std::atomic_bool mEnabled{true};
  std::atomic_bool mSingleModel{false};
  std::atomic_bool mHideItemShadow{true};
  std::atomic_bool mProfileSupported{false};
  std::atomic<float> mFlatGroundHeight{-0.150f};
  std::atomic<float> mShapedGroundHeight{-0.165f};
  std::atomic<float> mFullBlockGroundHeight{-0.035f};
  std::atomic<float> mHorizontalThinGroundHeight{-0.145f};
  std::atomic<float> mSpecialGroundHeight{-0.140f};
  std::atomic<float> mNormalHeadGroundHeight{0.015f};
  std::atomic<float> mDragonHeadGroundHeight{-0.015f};
  std::atomic<float> mWaterBobSpeed{1.0f};
  std::uintptr_t mMinecraftBase{};
  std::uintptr_t mRenderTarget{};
  std::uintptr_t mRenderItemGroupTarget{};

  RenderFn mOriginal{};
  RenderItemGroupFn mRenderItemGroupOriginal{};
  GetWorldMatrixFn mGetWorldMatrix{};
  GetPartialTickFn mGetPartialTick{};
  MatrixPushFn mMatrixPush{};
  MatrixRefDtorFn mMatrixRefDtor{};
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
  std::array<VisualState, kStateCapacity> mStates{};
  mutable ComponentStorageCache mComponentStorageCache{};
  std::uint32_t mRenderCounter{};
};

} // namespace itemphysics
