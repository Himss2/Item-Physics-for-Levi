#pragma once

#include "ItemPhysicsConfig.hpp"
#include "MatrixMath.hpp"
#include "RttiResolver.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <unordered_map>

#include <pl/Mod.hpp>
#include <pl/memory/Hook.hpp>

namespace itemphysics {

class ItemPhysicsRuntime {
public:
  ItemPhysicsRuntime() = default;

  ItemPhysicsRuntime(const ItemPhysicsRuntime &) = delete;
  ItemPhysicsRuntime &
  operator=(const ItemPhysicsRuntime &) = delete;

  void applyConfig(
      const ItemPhysicsConfig &config) noexcept;

  bool install(ll::mod::NativeMod &mod);

  void uninstall();

  void clearStates();

  [[nodiscard]]
  bool profileSupported() const noexcept {
    return mProfileSupported.load(
        std::memory_order_relaxed);
  }

public:
  using RenderFn =
      void (*)(void *, void *, void *);

  using GetWorldMatrixFn =
      void *(*)(void *);

  struct MatrixStackRefAbi {
    void *stack{};
    Mat4 *mat{};

    // Required to preserve the AArch64 hidden-sret ABI.
    ~MatrixStackRefAbi() {}
  };

  using MatrixPushFn =
      MatrixStackRefAbi (*)(void *, bool);

  using MatrixRefDtorFn =
      void (*)(MatrixStackRefAbi *);

private:
  struct PhysicsState {
    bool initialized{};
    bool wasGrounded{};

    float rotX{};
    float rotY{};
    float rotZ{};

    float angularX{};
    float angularZ{};

    std::chrono::steady_clock::time_point born{};
    std::chrono::steady_clock::time_point lastUpdate{};
    std::chrono::steady_clock::time_point lastSeen{};
  };

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

  bool hasOnGroundComponent(
      void *actor) const noexcept;

  PhysicsState &stateFor(
      std::uint32_t entityId,
      std::chrono::steady_clock::time_point now);

  void updateState(
      PhysicsState &state,
      std::uint32_t entityId,
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

  std::atomic_bool mEnabled{true};
  std::atomic_bool mSingleModel{true};

  std::atomic<float> mRotationSpeed{1.0f};
  std::atomic<float> mSettleSpeed{3.0f};

  // Atlas default.
  std::atomic<float> mGroundTiltDeg{90.0f};

  // Ordinary flat items only.
  std::atomic<float> mHeightOffset{-0.38f};

  std::atomic_bool mProfileSupported{false};

  std::uintptr_t mMinecraftBase{};
  std::uintptr_t mRenderTarget{};

  RenderFn mOriginal{};

  GetWorldMatrixFn mGetWorldMatrix{};
  MatrixPushFn mMatrixPush{};
  MatrixRefDtorFn mMatrixRefDtor{};

  std::unique_ptr<pl::memory::HookHandle> mHook;

  mutable std::mutex mStateMutex;

  std::unordered_map<
      std::uint32_t,
      PhysicsState>
      mStates;

  std::uint32_t mRenderCounter{};
};

} // namespace itemphysics
