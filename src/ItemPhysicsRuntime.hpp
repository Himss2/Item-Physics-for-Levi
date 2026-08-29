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

  ItemPhysicsRuntime(
      const ItemPhysicsRuntime &) = delete;

  ItemPhysicsRuntime &
  operator=(
      const ItemPhysicsRuntime &) = delete;

  void applyConfig(
      const ItemPhysicsConfig &config) noexcept;

  bool install(
      ll::mod::NativeMod &mod);

  void uninstall();

  void clearStates();

  [[nodiscard]]
  bool profileSupported() const noexcept {

    return mProfileSupported.load(
        std::memory_order_relaxed);
  }

public:
  using RenderFn =
      void (*)(
          void *,
          void *,
          void *);

  using GetWorldMatrixFn =
      void *(*)(void *);

  struct MatrixStackRefAbi {
    void *stack{};
    Mat4 *mat{};

    ~MatrixStackRefAbi() {}
  };

  using MatrixPushFn =
      MatrixStackRefAbi (*)(
          void *,
          bool);

  using MatrixRefDtorFn =
      void (*)(
          MatrixStackRefAbi *);

private:
  // ==========================================================
  // AABB
  // ==========================================================

  struct AabbAbi {
    float minX{};
    float minY{};
    float minZ{};

    float maxX{};
    float maxY{};
    float maxZ{};
  };

  static_assert(
      sizeof(AabbAbi) == 24);

  // ==========================================================
  // Model class
  // ==========================================================

  enum class ModelClass :
      std::uint8_t {

    FlatItem,

    BlockModel,

    Special3D,
  };

  struct BlockRenderInfo {
    bool valid{};

    std::int32_t
        blockShape{-1};

    bool
        keepHorizontal{};
  };

  struct ItemRenderTraits {
    bool valid{};

    ModelClass modelClass{
        ModelClass::FlatItem};

    BlockRenderInfo
        block{};
  };

  // ==========================================================
  // Physics
  // ==========================================================

  struct PhysicsState {
    bool initialized{};
    bool wasGrounded{};

    float rotX{};
    float rotY{};
    float rotZ{};

    float angularX{};
    float angularZ{};

    float restYaw{};

    std::chrono::steady_clock::time_point
        born{};

    std::chrono::steady_clock::time_point
        lastUpdate{};

    std::chrono::steady_clock::time_point
        lastSeen{};
  };

  // ==========================================================
  // Split mIsInItemFrame context
  // ==========================================================

  struct HelperOverrideContext {
    bool active{};

    void *actor{};

    std::uint8_t
        originalItemFrame{};
  };

  // ==========================================================
  // Minecraft helper types
  // ==========================================================

  using BlockGraphicsGetForBlockFn =
      void *(*)(
          const void *block);

  using BlockGraphicsGetBlockShapeFn =
      std::int32_t (*)(
          const void *graphics);

  using GetVisualShapeFn =
      const AabbAbi *(*)(
          void *blockType,
          const void *block,
          AabbAbi *scratch);

  // ==========================================================
  // ItemRenderer private helper
  //
  // RVA 0xA29ED30
  //
  // Call from ItemRenderer:
  //
  // x0 self
  // x1 BaseActorRenderContext
  // x2 ItemStackBase
  // x3 ItemActor
  // x4 Block
  // w5 BlockShape
  // w6 model count
  // s0 partial
  // ==========================================================

  using RenderHelperFn =
      void (*)(
          void *self,
          void *renderContext,
          void *itemStack,
          void *itemActor,
          void *block,
          std::int32_t blockShape,
          std::int32_t modelCount,
          float partialTick);

  // ==========================================================
  // Static hooks
  // ==========================================================

  static ItemPhysicsRuntime *
      sInstance;

  static thread_local
      HelperOverrideContext
          sHelperOverride;

  static void renderDetour(
      void *self,
      void *renderContext,
      void *renderData);

  static void renderHelperDetour(
      void *self,
      void *renderContext,
      void *itemStack,
      void *itemActor,
      void *block,
      std::int32_t blockShape,
      std::int32_t modelCount,
      float partialTick);

  // ==========================================================
  // Hook bodies
  // ==========================================================

  void onRender(
      void *self,
      void *renderContext,
      void *renderData);

  void onRenderHelper(
      void *self,
      void *renderContext,
      void *itemStack,
      void *itemActor,
      void *block,
      std::int32_t blockShape,
      std::int32_t modelCount,
      float partialTick);

  bool verifyProfile(
      const ResolvedVirtual &resolved,
      ll::mod::NativeMod &mod) const;

  // ==========================================================
  // Classification
  // ==========================================================

  [[nodiscard]]
  ItemRenderTraits classifyItem(
      std::uintptr_t actorAddress) const noexcept;

  [[nodiscard]]
  bool buildBlockRenderInfo(
      const void *block,
      BlockRenderInfo &info) const noexcept;

  [[nodiscard]]
  static bool blockShapeShouldRemainHorizontal(
      std::int32_t shape) noexcept;

  // ==========================================================
  // Physics / ECS
  // ==========================================================

  [[nodiscard]]
  bool hasOnGroundComponent(
      void *actor) const noexcept;

  PhysicsState &stateFor(
      std::uint32_t entityId,
      std::chrono::steady_clock::time_point now);

  void updateState(
      PhysicsState &state,
      const ItemRenderTraits &traits,
      bool grounded,
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

  // ==========================================================
  // Settings
  // ==========================================================

  std::atomic_bool
      mEnabled{true};

  std::atomic_bool
      mSingleModel{true};

  std::atomic<float>
      mRotationSpeed{1.0f};

  std::atomic<float>
      mSettleSpeed{3.0f};

  std::atomic<float>
      mGroundTiltDeg{90.0f};

  std::atomic<float>
      mHeightOffset{0.0f};

  std::atomic_bool
      mProfileSupported{false};

  // ==========================================================
  // Runtime addresses
  // ==========================================================

  std::uintptr_t
      mMinecraftBase{};

  std::uintptr_t
      mRenderTarget{};

  std::uintptr_t
      mRenderHelperTarget{};

  RenderFn
      mOriginal{};

  RenderHelperFn
      mOriginalHelper{};

  GetWorldMatrixFn
      mGetWorldMatrix{};

  MatrixPushFn
      mMatrixPush{};

  MatrixRefDtorFn
      mMatrixRefDtor{};

  BlockGraphicsGetForBlockFn
      mGetBlockGraphicsForBlock{};

  BlockGraphicsGetBlockShapeFn
      mGetBlockGraphicsShape{};

  std::unique_ptr<
      pl::memory::HookHandle>
      mHook;

  std::unique_ptr<
      pl::memory::HookHandle>
      mHelperHook;

  mutable std::mutex
      mStateMutex;

  std::unordered_map<
      std::uint32_t,
      PhysicsState>
      mStates;

  std::uint32_t
      mRenderCounter{};
};

} // namespace itemphysics
