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
  // ==========================================================================
  // Hook ABI
  // ==========================================================================

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
  // AABB
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
      sizeof(AabbAbi) ==
      24);

  // ==========================================================================
  // Model classification
  // ==========================================================================

  enum class ModelClass :
      std::uint8_t {

    FlatItem,

    BlockItem,

    SpecialItem,
  };

  enum class SpecialKind :
      std::uint8_t {

    None,

    Shield,

    Banner,
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

    SpecialKind specialKind{
        SpecialKind::None};

    BlockRenderInfo
        block{};

    // Actual BlockShape used by ItemRenderer.
    //
    // Ground-height selection uses this.
    // It does NOT automatically control orientation.
    bool
        hasRenderShape{};

    std::int32_t
        renderShape{-1};
  };

  // ==========================================================================
  // Physics state
  //
  // V5:
  //
  // State sekarang menyimpan:
  //
  // - actual angular velocity
  // - estimated linear velocity
  // - last render position
  // - landing rest orientation
  //
  // Ini membuat animasi benar-benar temporal dan tidak hanya berdasarkan
  // umur entity.
  // ==========================================================================

  struct PhysicsState {
    bool initialized{};

    bool wasGrounded{};

    bool hasPosition{};

    bool hasLandingRest{};

    // ------------------------------------------------------------------------
    // Orientation
    // ------------------------------------------------------------------------

    float rotX{};
    float rotY{};
    float rotZ{};

    // ------------------------------------------------------------------------
    // Angular velocity, radians / second
    // ------------------------------------------------------------------------

    float angularX{};
    float angularY{};
    float angularZ{};

    // ------------------------------------------------------------------------
    // Estimated world velocity
    // ------------------------------------------------------------------------

    float velocityX{};
    float velocityY{};
    float velocityZ{};

    // ------------------------------------------------------------------------
    // Previous render position
    // ------------------------------------------------------------------------

    float lastX{};
    float lastY{};
    float lastZ{};

    // ------------------------------------------------------------------------
    // Rest orientation
    // ------------------------------------------------------------------------

    float restYaw{};

    // Non-horizontal items keep the Z orientation they had when landing.
    //
    // This preserves the final orientation behavior of the already-working
    // Atlas implementation while allowing it to settle naturally.
    float landingRestZ{};

    // ------------------------------------------------------------------------
    // Time
    // ------------------------------------------------------------------------

    std::chrono::steady_clock::time_point
        born{};

    std::chrono::steady_clock::time_point
        lastUpdate{};

    std::chrono::steady_clock::time_point
        lastSeen{};
  };

  // ==========================================================================
  // Minecraft ABI
  // ==========================================================================

  using GetBlockTypeForRenderingFn =
      const void *(*)(
          const void *itemStackBase);

  using BlockGraphicsGetForBlockTypeFn =
      void *(*)(
          const void *blockType);

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
  // Classification
  // ==========================================================================

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

  // ==========================================================================
  // Physics / ECS
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
      float positionX,
      float positionY,
      float positionZ,
      std::chrono::steady_clock::time_point now) const;

  void pruneStates(
      std::chrono::steady_clock::time_point now);

  // ==========================================================================
  // Math
  // ==========================================================================

  static float seededUnit(
      std::uint32_t seed) noexcept;

  static float wrapPi(
      float value) noexcept;

  // Damped angular spring.
  //
  // Used only during landing/grounded settle.
  static void springAngle(
      float &current,
      float &velocity,
      float target,
      float angularFrequency,
      float dampingRatio,
      float dt) noexcept;

  static bool libcxxStringEquals(
      std::uintptr_t stringAddress,
      std::string_view wanted) noexcept;

  // ==========================================================================
  // Core config
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

  // Original Atlas ordinary-item height.
  std::atomic<float>
      mHeightOffset{-0.38f};

  // ==========================================================================
  // Ground-height sliders
  // ==========================================================================

  std::atomic<float>
      mBlockGroundHeight{-0.06f};

  std::atomic<float>
      mThinBlockGroundHeight{-0.08f};

  std::atomic<float>
      mTorchGroundHeight{-0.08f};

  std::atomic<float>
      mShapedBlockGroundHeight{-0.16f};

  std::atomic<float>
      mSkullGroundHeight{-0.22f};

  std::atomic<float>
      mShieldGroundHeight{-0.08f};

  std::atomic<float>
      mBannerGroundHeight{-0.075f};

  std::atomic_bool
      mProfileSupported{false};

  // ==========================================================================
  // Minecraft runtime
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

  GetBlockTypeForRenderingFn
      mGetBlockTypeForRendering{};

  BlockGraphicsGetForBlockTypeFn
      mGetBlockGraphicsForBlockType{};

  BlockGraphicsGetForBlockFn
      mGetBlockGraphicsForBlock{};

  BlockGraphicsGetBlockShapeFn
      mGetBlockGraphicsShape{};

  // ==========================================================================
  // Hook / states
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
