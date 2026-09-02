#pragma once
#include "ItemPhysicsConfig.hpp"
#include "MatrixMath.hpp"
#include "RttiResolver.hpp"
#include <atomic>
#include <chrono>
#include <cstddef>
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
  ItemPhysicsRuntime(const ItemPhysicsRuntime&) = delete;
  ItemPhysicsRuntime& operator=(const ItemPhysicsRuntime&) = delete;
  void applyConfig(const ItemPhysicsConfig&) noexcept;
  bool install(ll::mod::NativeMod&);
  void uninstall();
  void clearStates();
  [[nodiscard]] bool profileSupported() const noexcept { return mProfileSupported.load(std::memory_order_relaxed); }

  using RenderFn = void (*)(void*,void*,void*);
  using RenderItemGroupFn = void (*)(void*,void*,void*,std::uint32_t,std::uint32_t,float,float);
  using GetWorldMatrixFn = void *(*)(void*);
  struct MatrixStackRefAbi { void *stack{}; Mat4 *mat{}; ~MatrixStackRefAbi(){} };
  using MatrixPushFn = MatrixStackRefAbi (*)(void*,bool);
  using MatrixRefDtorFn = void (*)(MatrixStackRefAbi*);

private:
  struct AabbAbi { float minX{},minY{},minZ{},maxX{},maxY{},maxZ{}; };
  static_assert(sizeof(AabbAbi)==24);
  struct Vec2Abi { float x{},y{}; };
  struct Vec3Abi { float x{},y{},z{}; };
  struct QuatAbi { float w{1.0f},x{},y{},z{}; };

  enum class ModelClass : std::uint8_t { FlatItem,BlockItem,SpecialItem };
  enum class SpecialKind : std::uint8_t { None,Shield,Banner };
  enum class MotionFamily : std::uint8_t { FullBlock,ShapedBlock,FlatItem,HorizontalThin,Head,Special };

  struct BlockRenderInfo {
    bool valid{};
    bool keepHorizontal{};
    bool verticalPlane{};
    bool rodLike{};
    std::int32_t blockShape{-1};
  };

  struct ItemRenderTraits {
    bool valid{};
    bool hasRenderShape{};
    std::int32_t renderShape{-1};
    ModelClass modelClass{ModelClass::FlatItem};
    SpecialKind specialKind{SpecialKind::None};
    MotionFamily family{MotionFamily::FlatItem};
    BlockRenderInfo block{};
  };

  struct PhysicsState {
    bool initialized{};
    bool wasGrounded{true};
    bool launchImpulseApplied{};
    bool airTargetCaptured{};
    float javaRoll{};
    float yaw{};
    QuatAbi orientation{};
    QuatAbi restOrientation{};
    float angularX{},angularY{},angularZ{};
    float restYaw{};
    float headTilt{};
    float headDirection{1.0f};
    std::chrono::steady_clock::time_point born{},lastUpdate{},lastSeen{};
  };

  using GetPosDeltaFn = const Vec3Abi *(*)(const void*);
  using GetBlockTypeForRenderingFn = const void *(*)(const void*);
  using BlockGraphicsGetForBlockTypeFn = void *(*)(const void*);
  using BlockGraphicsGetForBlockFn = void *(*)(const void*);
  using BlockGraphicsGetBlockShapeFn = std::int32_t (*)(const void*);
  using IsBlockShape3DFn = bool (*)(std::int32_t);
  using GetVisualShapeFn = const AabbAbi *(*)(void*,const void*,AabbAbi*);

  struct ShadowStorageEmplaceResultAbi { std::uintptr_t first{},second{}; };
  using RelativeShadowStorageFn = void *(*)(void*,std::uint32_t);
  using RelativeShadowEmplaceFn = ShadowStorageEmplaceResultAbi (*)(void*,const std::uint32_t*,bool,const float*);

  static ItemPhysicsRuntime *sInstance;
  static void renderDetour(void*,void*,void*);
  static void renderItemGroupDetour(void*,void*,void*,std::uint32_t,std::uint32_t,float,float);
  void onRender(void*,void*,void*);
  void onRenderItemGroup(void*,void*,void*,std::uint32_t,std::uint32_t,float,float);

  bool verifyProfile(const ResolvedVirtual&,ll::mod::NativeMod&) const;
  [[nodiscard]] ItemRenderTraits classifyItem(std::uintptr_t) const noexcept;
  [[nodiscard]] bool buildBlockRenderInfo(const void*,BlockRenderInfo&) const noexcept;
  [[nodiscard]] bool tryGetRenderBlockShape(std::uintptr_t,std::int32_t&) const noexcept;
  [[nodiscard]] bool getActorYaw(void*,float&) const noexcept;
  [[nodiscard]] bool hasOnGroundComponent(void*) const noexcept;
  [[nodiscard]] void *findComponentStorage(void*,std::uint32_t) const noexcept;
  [[nodiscard]] bool findPackedEntity(void*,std::uint32_t,std::uint32_t&) const noexcept;
  void updateItemShadowComponent(void*,bool,bool) const noexcept;

  PhysicsState &stateFor(std::uint32_t,std::chrono::steady_clock::time_point,bool,float);
  void updateState(PhysicsState&,const ItemRenderTraits&,bool,const Vec3Abi&,bool,std::chrono::steady_clock::time_point) const;
  void pruneStates(std::chrono::steady_clock::time_point);

  static float seededUnit(std::uint32_t) noexcept;
  static float wrapPi(float) noexcept;
  static float moveAngle(float,float,float) noexcept;
  static bool libcxxStringEquals(std::uintptr_t,std::string_view) noexcept;

  std::atomic_bool mEnabled{true};
  std::atomic_bool mSingleModel{false};
  std::atomic_bool mHideItemShadow{true};
  std::atomic_bool mOldRotation{false};
  std::atomic<float> mRotationSpeed{1.0f};
  std::atomic_bool mProfileSupported{false};

  std::uintptr_t mMinecraftBase{},mRenderTarget{},mRenderItemGroupTarget{};
  RenderFn mOriginal{};
  RenderItemGroupFn mRenderItemGroupOriginal{};
  GetWorldMatrixFn mGetWorldMatrix{};
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
  std::unique_ptr<pl::memory::HookHandle> mHook,mRenderItemGroupHook;
  mutable std::mutex mStateMutex,mGroupOffsetMutex;
  std::unordered_map<std::uint32_t,PhysicsState> mStates;
  std::uint32_t mRenderCounter{};
};
}
