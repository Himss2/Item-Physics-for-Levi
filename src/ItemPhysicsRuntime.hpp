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
  // ==========================================================================
  // AABB ABI
  // ==========================================================================

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

  // ==========================================================================
  // Model classification
  // ==========================================================================

  enum class ModelClass :
      std::uint8_t {

    // sword, pickaxe, armor icon,
    // ingot, food, etc.
    FlatItem,

    // Any ItemStack backed by Block const*.
    //
    // Orientation is calculated from VisualShape.
    BlockModel,

    // Non-block special renderer:
    // shield / banner.
    Special3D,
  };

  struct BlockRenderInfo {
    bool valid{};

    std::int32_t blockShape{-1};

    bool vanilla3D{};

    AabbAbi bounds{};

    // Approximate ItemRenderer scale.
    float renderScale{0.5f};

    // Final resting rotation.
    float targetRotX{};
    float targetRotZ{};

    // Local visual center, already converted
    // into renderer/world scale.
    float pivotX{};
    float pivotY{};
    float pivotZ{};
  };

  struct ItemRenderTraits {
    bool valid{};

    ModelClass modelClass{
        ModelClass::FlatItem};

    BlockRenderInfo block{};
  };

  // ==========================================================================
  // Physics state
  // ==========================================================================

  struct PhysicsState {
    bool initialized{};
    bool wasGrounded{};

    float rotX{};
    float rotY{};
    float rotZ{};

    float angularX{};
    float angularZ{};

    std::chrono::steady_clock::time_point
        born{};

    std::chrono::steady_clock::time_point
        lastUpdate{};

    std::chrono::steady_clock::time_point
        lastSeen{};
  };

  // ==========================================================================
  // Minecraft helpers
  // ==========================================================================

  using BlockGraphicsGetForBlockFn =
      void *(*)(
          const void *block);

  using BlockGraphicsGetBlockShapeFn =
      std::int32_t (*)(
          const void *graphics);

  using IsBlockShape3DFn =
      bool (*)(
          std::int32_t shape);

  using GetVisualShapeFn =
      AabbAbi *(*)(
          void *blockType,
          const void *block,
          AabbAbi *output);

  // ==========================================================================
  // Hook
  // ==========================================================================

  static ItemPhysicsRuntime *
      sInstance;

  static void renderDetour(
      void *self,
      void *renderContext,
      void *renderData);

  void onRender(
      void *self,
      void *renderContext,
      void *renderData);

  bool verifyProfile(
      const ResolvedVirtual &resolved,
      ll::mod::NativeMod &mod) const;

  // ==========================================================================
  // Classification / block geometry
  // ==========================================================================

  [[nodiscard]]
  ItemRenderTraits classifyItem(
      std::uintptr_t actorAddress) const noexcept;

  [[nodiscard]]
  bool buildBlockRenderInfo(
      const void *block,
      BlockRenderInfo &info) const noexcept;

  [[nodiscard]]
  static float computeBlockGroundOffset(
      const BlockRenderInfo &info,
      float rotX,
      float rotZ) noexcept;

  // ==========================================================================
  // ECS / physics
  // ==========================================================================

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

  // ==========================================================================
  // Item identifier
  // ==========================================================================

  static bool libcxxStringEquals(
      std::uintptr_t stringAddress,
      std::string_view wanted) noexcept;

  // ==========================================================================
  // Config
  // ==========================================================================

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
      mHeightOffset{-0.38f};

  std::atomic_bool
      mProfileSupported{false};

  // ==========================================================================
  // Minecraft addresses
  // ==========================================================================

  std::uintptr_t
      mMinecraftBase{};

  std::uintptr_t
      mRenderTarget{};

  RenderFn
      mOriginal{};

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

  IsBlockShape3DFn
      mIsBlockShape3D{};

  // ==========================================================================
  // Hook/state
  // ==========================================================================

  std::unique_ptr<
      pl::memory::HookHandle>
      mHook;

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
