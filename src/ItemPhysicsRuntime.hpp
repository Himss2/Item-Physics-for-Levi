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
  // AABB ABI
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
  // Rendering class
  // ==========================================================

  enum class ModelClass :
      std::uint8_t {

    // sword/tool/armor/ingot/etc.
    FlatItem,

    // any ItemStack backed by Block const*
    BlockItem,

    // shield/banner
    SpecialItem,
  };

  struct BlockRenderInfo {
    bool valid{};

    std::int32_t
        blockShape{-1};

    // Very small compatibility layer over Atlas:
    //
    // slab/carpet/thin layer/head should not receive
    // Atlas's global final X=90 orientation.
    bool keepHorizontal{};
  };

  struct ItemRenderTraits {
    bool valid{};

    ModelClass modelClass{
        ModelClass::FlatItem};

    BlockRenderInfo block{};
  };

  // ==========================================================
  // Physics state
  // ==========================================================

  struct PhysicsState {
    bool initialized{};
    bool wasGrounded{};

    float rotX{};
    float rotY{};
    float rotZ{};

    float angularX{};
    float angularZ{};

    // Used ONLY for horizontal compatibility blocks.
    float restYaw{};

    std::chrono::steady_clock::time_point
        born{};

    std::chrono::steady_clock::time_point
        lastUpdate{};

    std::chrono::steady_clock::time_point
        lastSeen{};
  };

  // ==========================================================
  // Minecraft helpers
  // ==========================================================

  using BlockGraphicsGetForBlockFn =
      void *(*)(
          const void *block);

  using BlockGraphicsGetBlockShapeFn =
      std::int32_t (*)(
          const void *graphics);

  // AABB const&
  // BlockType::getVisualShape(
  //     Block const&,
  //     AABB& scratch
  // ) const
  using GetVisualShapeFn =
      const AabbAbi *(*)(
          void *blockType,
          const void *block,
          AabbAbi *scratch);

  // ==========================================================
  // Hook
  // ==========================================================

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

  // ==========================================================
  // ECS / physics
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
  // Config
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
      mHeightOffset{-0.38f};

  std::atomic_bool
      mProfileSupported{false};

  // ==========================================================
  // Runtime
  // ==========================================================

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

  // ==========================================================
  // Hook / state
  // ==========================================================

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
