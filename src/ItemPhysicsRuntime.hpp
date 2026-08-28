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

    // Non-trivial destructor preserves the AArch64
    // MatrixStack::push() hidden-sret ABI.
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
  // ---------------------------------------------------------------------------
  // Model classification
  //
  // FlatItem:
  // sword/tool/armor/ingot/etc.
  //
  // FlatBlock:
  // block-backed item that vanilla ItemRenderer renders through its generated
  // / non-3D block-shape path: torch, lever, rail, ladder, lantern, chain, etc.
  //
  // Block3D:
  // normal 3D block model: cube, slab, carpet-like full block model, skull,
  // chest, shulker, trapdoor, etc.
  //
  // Special3D:
  // non-block renderer special case, currently shield/banner.
  // ---------------------------------------------------------------------------

  enum class ModelClass : std::uint8_t {
    FlatItem,
    FlatBlock,
    Block3D,
    Special3D,
  };

  struct ItemRenderTraits {
    bool valid{};

    ModelClass modelClass{
        ModelClass::FlatItem};

    std::int32_t blockShape{-1};
  };

  struct PhysicsState {
    bool initialized{};
    bool wasGrounded{};

    float rotX{};
    float rotY{};
    float rotZ{};

    float angularX{};
    float angularZ{};

    // Stable random yaw for 3D blocks.
    //
    // Block models settle upright but still point in
    // different horizontal directions.
    float restYaw{};

    std::chrono::steady_clock::time_point
        born{};

    std::chrono::steady_clock::time_point
        lastUpdate{};

    std::chrono::steady_clock::time_point
        lastSeen{};
  };

  // ---------------------------------------------------------------------------
  // Vanilla renderer helpers
  // ---------------------------------------------------------------------------

  using BlockGraphicsGetForBlockFn =
      void *(*)(
          const void *block);

  using BlockGraphicsGetBlockShapeFn =
      std::int32_t (*)(
          const void *graphics);

  using IsBlockShape3DFn =
      bool (*)(
          std::int32_t shape);

  // ---------------------------------------------------------------------------
  // Hook
  // ---------------------------------------------------------------------------

  static ItemPhysicsRuntime *sInstance;

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

  // ---------------------------------------------------------------------------
  // Model classification
  // ---------------------------------------------------------------------------

  [[nodiscard]]
  ItemRenderTraits classifyItem(
      std::uintptr_t actorAddress) const noexcept;

  // ---------------------------------------------------------------------------
  // Physics
  // ---------------------------------------------------------------------------

  [[nodiscard]]
  bool hasOnGroundComponent(
      void *actor) const noexcept;

  PhysicsState &stateFor(
      std::uint32_t entityId,
      std::chrono::steady_clock::time_point now);

  void updateState(
      PhysicsState &state,
      ModelClass modelClass,
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

  // ---------------------------------------------------------------------------
  // Item identifier helper
  // ---------------------------------------------------------------------------

  static bool libcxxStringEquals(
      std::uintptr_t stringAddress,
      std::string_view wanted) noexcept;

  // ---------------------------------------------------------------------------
  // Config state
  // ---------------------------------------------------------------------------

  std::atomic_bool mEnabled{
      true};

  std::atomic_bool mSingleModel{
      true};

  std::atomic<float> mRotationSpeed{
      1.0f};

  std::atomic<float> mSettleSpeed{
      3.0f};

  std::atomic<float> mGroundTiltDeg{
      90.0f};

  std::atomic<float> mHeightOffset{
      -0.38f};

  std::atomic_bool mProfileSupported{
      false};

  // ---------------------------------------------------------------------------
  // Minecraft functions
  // ---------------------------------------------------------------------------

  std::uintptr_t mMinecraftBase{};
  std::uintptr_t mRenderTarget{};

  RenderFn mOriginal{};

  GetWorldMatrixFn mGetWorldMatrix{};
  MatrixPushFn mMatrixPush{};
  MatrixRefDtorFn mMatrixRefDtor{};

  BlockGraphicsGetForBlockFn
      mGetBlockGraphicsForBlock{};

  BlockGraphicsGetBlockShapeFn
      mGetBlockGraphicsShape{};

  IsBlockShape3DFn
      mIsBlockShape3D{};

  // ---------------------------------------------------------------------------
  // Hook/state
  // ---------------------------------------------------------------------------

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
