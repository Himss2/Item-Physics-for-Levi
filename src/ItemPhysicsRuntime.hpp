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

  using RenderFn = void (*)(void *, void *, void *);
  using RenderItemGroupFn =
      void (*)(void *, void *, void *, std::uint32_t, std::uint32_t, float, float);
  using GetWorldMatrixFn = void *(*)(void *);

  struct MatrixStackRefAbi {
    void *stack{};
    Mat4 *mat{};
    ~MatrixStackRefAbi() {}
  };

  using MatrixPushFn = MatrixStackRefAbi (*)(void *, bool);
  using MatrixRefDtorFn = void (*)(MatrixStackRefAbi *);

private:
  enum class ModelClass : std::uint8_t {
    FlatItem,
    BlockItem,
    SpecialItem
  };

  enum class SpecialKind : std::uint8_t {
    None,
    Shield,
    Banner
  };

  struct ItemRenderTraits {
    bool valid{};
    ModelClass modelClass{ModelClass::FlatItem};
    SpecialKind specialKind{SpecialKind::None};
    bool hasBlockShape{};
    std::int32_t blockShape{-1};

    [[nodiscard]] bool blockLike() const noexcept {
      return modelClass != ModelClass::FlatItem;
    }
  };

  struct PhysicsState {
    bool initialized{};
    bool wasGrounded{true};
    float roll{};
    float yaw{};
    std::chrono::steady_clock::time_point lastUpdate{};
    std::chrono::steady_clock::time_point lastSeen{};
  };

  using GetBlockTypeForRenderingFn = const void *(*)(const void *);
  using BlockGraphicsGetForBlockTypeFn = void *(*)(const void *);
  using BlockGraphicsGetForBlockFn = void *(*)(const void *);
  using BlockGraphicsGetBlockShapeFn = std::int32_t (*)(const void *);

  struct ShadowStorageEmplaceResultAbi {
    std::uintptr_t first{};
    std::uintptr_t second{};
  };

  using RelativeShadowStorageFn = void *(*)(void *, std::uint32_t);
  using RelativeShadowEmplaceFn =
      ShadowStorageEmplaceResultAbi (*)(
          void *,
          const std::uint32_t *,
          bool,
          const float *);

  static ItemPhysicsRuntime *sInstance;

  static void renderDetour(void *, void *, void *);

  static void renderItemGroupDetour(
      void *,
      void *,
      void *,
      std::uint32_t,
      std::uint32_t,
      float,
      float);

  void onRender(void *, void *, void *);

  void onRenderItemGroup(
      void *,
      void *,
      void *,
      std::uint32_t,
      std::uint32_t,
      float,
      float);

  bool verifyProfile(
      const ResolvedVirtual &,
      ll::mod::NativeMod &) const;

  [[nodiscard]]
  ItemRenderTraits classifyItem(
      std::uintptr_t) const noexcept;

  [[nodiscard]]
  bool tryGetRenderBlockShape(
      std::uintptr_t,
      std::int32_t &) const noexcept;

  [[nodiscard]]
  bool tryGetBlockShape(
      const void *,
      std::int32_t &) const noexcept;

  [[nodiscard]]
  bool hasOnGroundComponent(
      void *) const noexcept;

  void updateItemShadowComponent(
      void *,
      bool,
      bool) const noexcept;

  PhysicsState &stateFor(
      std::uint32_t,
      std::chrono::steady_clock::time_point);

  void updateState(
      PhysicsState &,
      bool,
      bool,
      bool,
      float,
      float,
      std::chrono::steady_clock::time_point) const;

  void pruneStates(
      std::chrono::steady_clock::time_point);

  static float seededUnit(
      std::uint32_t) noexcept;

  static float wrapPi(
      float) noexcept;

  static float moveAngle(
      float,
      float,
      float) noexcept;

  static bool libcxxStringEquals(
      std::uintptr_t,
      std::string_view) noexcept;

  std::atomic_bool mEnabled{true};
  std::atomic_bool mSingleModel{true};
  std::atomic_bool mHideItemShadow{true};

  std::atomic<float> mRotationSpeed{1.0f};
  std::atomic<float> mSettleSpeed{3.0f};

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
