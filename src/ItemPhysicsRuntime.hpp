#pragma once

#include "MatrixMath.hpp"
#include "RttiResolver.hpp"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>

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
  struct VisualState {
    std::uint32_t entity{};
    std::uint32_t lastSeen{};
    std::int32_t lastAge{-1};
    float xRot{};
    float yRot{};
    float lastSample{};
    float modelScale{};
    bool used{};
    bool sampled{};
  };

  using GetBlockTypeForRenderingFn = const void *(*)(const void *);

  static constexpr std::size_t kStateCapacity = 512;

  static ItemPhysicsRuntime *sInstance;
  static void renderDetour(void *, void *, void *);
  static void renderItemGroupDetour(void *, void *, void *, std::uint32_t,
                                    std::uint32_t, float, float);

  void onRender(void *, void *, void *);
  void onRenderItemGroup(void *, void *, void *, std::uint32_t, std::uint32_t,
                         float, float);

  [[nodiscard]] bool verifyProfile(const ResolvedVirtual &,
                                   ll::mod::NativeMod &) const;
  [[nodiscard]] bool isBlockItem(std::uintptr_t) const noexcept;
  [[nodiscard]] bool hasOnGroundComponent(void *) const noexcept;
  [[nodiscard]] void *findComponentStorage(void *, std::uint32_t) const noexcept;
  [[nodiscard]] bool findPackedEntity(void *, std::uint32_t,
                                      std::uint32_t &) const noexcept;

  VisualState &stateFor(std::uint32_t, std::int32_t, float, float) noexcept;
  static void updateRotation(VisualState &, bool, bool, std::int32_t,
                             float) noexcept;
  static std::uint32_t javaCopyCount(std::uint32_t) noexcept;

  std::atomic_bool mProfileSupported{false};
  std::uintptr_t mMinecraftBase{};
  std::uintptr_t mRenderTarget{};
  std::uintptr_t mRenderItemGroupTarget{};

  RenderFn mOriginal{};
  RenderItemGroupFn mRenderItemGroupOriginal{};
  GetWorldMatrixFn mGetWorldMatrix{};
  GetPartialTickFn mGetPartialTick{};
  MatrixPushFn mMatrixPush{};
  MatrixRefDtorFn mMatrixRefDtor{};
  GetBlockTypeForRenderingFn mGetBlockTypeForRendering{};

  std::unique_ptr<pl::memory::HookHandle> mHook;
  std::unique_ptr<pl::memory::HookHandle> mRenderItemGroupHook;
  std::array<VisualState, kStateCapacity> mStates{};
  std::uint32_t mRenderCounter{};
};

} // namespace itemphysics
