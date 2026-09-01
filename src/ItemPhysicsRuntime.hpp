#pragma once

#include "ItemPhysicsConfig.hpp"
#include "MatrixMath.hpp"
#include "RttiResolver.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string_view>
#include <unordered_map>

#include <pl/Mod.hpp>
#include <pl/memory/Hook.hpp>

namespace itemphysics {

class ItemPhysicsRuntime {
public:
  ItemPhysicsRuntime() = default;
  ItemPhysicsRuntime(const ItemPhysicsRuntime &) = delete;
  ItemPhysicsRuntime &operator=(const ItemPhysicsRuntime &) = delete;

  void applyConfig(const ItemPhysicsConfig &config) noexcept;
  bool install(ll::mod::NativeMod &mod);
  void uninstall();
  void clearStates();

  [[nodiscard]] bool profileSupported() const noexcept {
    return mProfileSupported.load(std::memory_order_relaxed);
  }

public:
  using RenderFn = void (*)(void *, void *, void *);

  using RenderItemGroupFn =
      void (*)(void *,
               void *,
               void *,
               std::uint32_t,
               std::uint32_t,
               float,
               float);

  using GetWorldMatrixFn = void *(*)(void *);

  struct MatrixStackRefAbi {
    void *stack{};
    Mat4 *mat{};
    ~MatrixStackRefAbi() {}
  };

  using MatrixPushFn = MatrixStackRefAbi (*)(void *, bool);
  using MatrixRefDtorFn = void (*)(MatrixStackRefAbi *);

private:
  struct AabbAbi {
    float minX{};
    float minY{};
    float minZ{};
    float maxX{};
    float maxY{};
    float maxZ{};
  };

  static_assert(sizeof(AabbAbi) == 24);

  struct QuatAbi {
    float w{1.0f};
    float x{};
    float y{};
    float z{};
  };

  enum class ModelClass : std::uint8_t {
    FlatItem,
    BlockItem,
    SpecialItem,
  };

  enum class SpecialKind : std::uint8_t {
    None,
    Shield,
    Banner,
  };

  struct BlockRenderInfo {
    bool valid{};
    std::int32_t blockShape{-1};
    bool keepHorizontal{};
  };

  struct ItemRenderTraits {
    bool valid{};
    ModelClass modelClass{ModelClass::FlatItem};
    SpecialKind specialKind{SpecialKind::None};
    BlockRenderInfo block{};
    bool hasRenderShape{};
    std::int32_t renderShape{-1};
  };

  struct PhysicsState {
    bool initialized{};
    bool wasGrounded{};
    bool launchImpulseApplied{};
    bool contactCaptured{};

    float fullRotX{};
    float fullRotY{};
    float fullRotZ{};

    QuatAbi orientation{};
    QuatAbi restOrientation{};

    float angularX{};
    float angularY{};
    float angularZ{};
    float restYaw{};

    std::chrono::steady_clock::time_point born{};
    std::chrono::steady_clock::time_point contactTime{};
    std::chrono::steady_clock::time_point lastUpdate{};
    std::chrono::steady_clock::time_point lastSeen{};
  };

  using GetBlockTypeForRenderingFn =
      const void *(*)(const void *itemStackBase);

  using BlockGraphicsGetForBlockTypeFn =
      void *(*)(const void *blockType);

  using BlockGraphicsGetForBlockFn =
      void *(*)(const void *block);

  using BlockGraphicsGetBlockShapeFn =
      std::int32_t (*)(const void *graphics);

  struct ShadowStorageEmplaceResultAbi {
    std::uintptr_t first{};
    std::uintptr_t second{};
  };

  using RelativeShadowStorageFn =
      void *(*)(void *registry, std::uint32_t componentHash);

  using RelativeShadowEmplaceFn =
      ShadowStorageEmplaceResultAbi (*)(
          void *storage,
          const std::uint32_t *entityId,
          bool forceBack,
          const float *value);

  using GetVisualShapeFn =
      const AabbAbi *(*)(void *blockType,
                         const void *block,
                         AabbAbi *scratch);

  static ItemPhysicsRuntime *sInstance;

  static void renderDetour(
      void *self,
      void *renderContext,
      void *renderData);

  static void renderItemGroupDetour(
      void *self,
      void *renderContext,
      void *itemData,
      std::uint32_t copyCount,
      std::uint32_t flags,
      float scale,
      float animation);

  void onRender(
      void *self,
      void *renderContext,
      void *renderData);

  void onRenderItemGroup(
      void *self,
      void *renderContext,
      void *itemData,
      std::uint32_t copyCount,
      std::uint32_t flags,
      float scale,
      float animation);

  bool verifyProfile(
      const ResolvedVirtual &resolved,
      ll::mod::NativeMod &mod) const;

  [[nodiscard]]
  ItemRenderTraits classifyItem(
      std::uintptr_t actorAddress) const noexcept;

  [[nodiscard]]
  bool buildBlockRenderInfo(
      const void *block,
      BlockRenderInfo &info) const noexcept;

  [[nodiscard]]
  bool tryGetRenderBlockShape(
      std::uintptr_t actorAddress,
      std::int32_t &shape) const noexcept;

  [[nodiscard]]
  bool hasOnGroundComponent(
      void *actor) const noexcept;

  void updateItemShadowComponent(
      void *actor,
      bool grounded,
      bool hideShadow) const noexcept;

  PhysicsState &stateFor(
      std::uint32_t entityId,
      std::chrono::steady_clock::time_point now);

  void updateState(
      PhysicsState &state,
      const ItemRenderTraits &traits,
      bool grounded,
      float verticalVelocity,
      std::chrono::steady_clock::time_point now) const;

  void pruneStates(
      std::chrono::steady_clock::time_point now);

  static float seededUnit(
      std::uint32_t seed) noexcept;

  static float wrapPi(
      float value) noexcept;

  static float approachAngle(
      float current,
      float target,
      float alpha) noexcept;

  static bool libcxxStringEquals(
      std::uintptr_t stringAddress,
      std::string_view wanted) noexcept;

  std::atomic_bool mEnabled{true};
  std::atomic_bool mSingleModel{true};
  std::atomic_bool mHideItemShadow{true};

  std::atomic<float> mRotationSpeed{1.0f};
  std::atomic<float> mSettleSpeed{3.0f};
  std::atomic<float> mGroundTiltDeg{90.0f};

  std::atomic<float> mHeightOffset{-0.38f};
  std::atomic<float> mBlockGroundHeight{-0.06f};
  std::atomic<float> mThinBlockGroundHeight{-0.08f};
  std::atomic<float> mTorchGroundHeight{-0.08f};
  std::atomic<float> mShapedBlockGroundHeight{-0.16f};
  std::atomic<float> mSkullGroundHeight{-0.22f};
  std::atomic<float> mShieldGroundHeight{-0.08f};
  std::atomic<float> mBannerGroundHeight{-0.075f};

  std::atomic_bool mProfileSupported{false};

  std::uintptr_t mMinecraftBase{};
  std::uintptr_t mRenderTarget{};
  std::uintptr_t mRenderItemGroupTarget{};

  RenderFn mOriginal{};
  RenderItemGroupFn mRenderItemGroupOriginal{};

  GetWorldMatrixFn mGetWorldMatrix{};
  MatrixPushFn mMatrixPush{};
  MatrixRefDtorFn mMatrixRefDtor{};

  GetBlockTypeForRenderingFn mGetBlockTypeForRendering{};
  BlockGraphicsGetForBlockTypeFn mGetBlockGraphicsForBlockType{};
  BlockGraphicsGetForBlockFn mGetBlockGraphicsForBlock{};
  BlockGraphicsGetBlockShapeFn mGetBlockGraphicsShape{};

  RelativeShadowStorageFn mGetRelativeShadowStorage{};
  RelativeShadowEmplaceFn mEmplaceRelativeShadow{};

  std::unique_ptr<pl::memory::HookHandle> mHook;
  std::unique_ptr<pl::memory::HookHandle> mRenderItemGroupHook;

  mutable std::mutex mStateMutex;
  mutable std::mutex mGroupOffsetMutex;

  std::unordered_map<std::uint32_t, PhysicsState> mStates;

  std::uint32_t mRenderCounter{};
};

}
