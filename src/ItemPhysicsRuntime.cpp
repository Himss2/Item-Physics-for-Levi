#include "ItemPhysicsRuntime.hpp"
#include "TargetProfile.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace itemphysics {
namespace {

constexpr float kPi=3.14159265358979323846f;
constexpr float kHalfPi=kPi*0.5f;
constexpr float kDegToRad=kPi/180.0f;

constexpr float kJavaBaseTilt=kHalfPi;
constexpr float kJavaAirRollRate=10.0f;
constexpr float kJavaOldGroundRate=5.0f;
constexpr float kOldSnapEpsilon=5.0f*kDegToRad;

constexpr float kJavaBlockOffsetY=-0.20f;
constexpr float kJavaBlockOffsetZ=-0.08f;
constexpr float kJavaFlatOffsetZ=-0.04f;

constexpr float kNormalBlockPivotY=0.25f;
constexpr float kLargeBlockPivotY=0.50f;

constexpr std::int32_t kSkullShape=83;
constexpr std::int32_t kLargePivotShape=101;

constexpr auto kStateTtl=
    std::chrono::seconds(8);

constexpr std::string_view kShieldId=
    "minecraft:shield";

constexpr std::string_view kBannerId=
    "minecraft:banner";

struct Vec3Local {
  float x{};
  float y{};
  float z{};
};

thread_local bool gDroppedItemGroupActive=false;
thread_local bool gDroppedItemGroupGrounded=false;

bool invert3x3FromMat4(
    const Mat4 &m,
    float inv[9]) noexcept {

  const float a00=m.m[0],a01=m.m[4],a02=m.m[8];
  const float a10=m.m[1],a11=m.m[5],a12=m.m[9];
  const float a20=m.m[2],a21=m.m[6],a22=m.m[10];

  const float c00=a11*a22-a12*a21;
  const float c01=a02*a21-a01*a22;
  const float c02=a01*a12-a02*a11;

  const float c10=a12*a20-a10*a22;
  const float c11=a00*a22-a02*a20;
  const float c12=a02*a10-a00*a12;

  const float c20=a10*a21-a11*a20;
  const float c21=a01*a20-a00*a21;
  const float c22=a00*a11-a01*a10;

  const float det=
      a00*c00+
      a01*c10+
      a02*c20;

  if (!std::isfinite(det)||
      std::abs(det)<1.0e-8f)
    return false;

  const float d=1.0f/det;

  inv[0]=c00*d;
  inv[1]=c01*d;
  inv[2]=c02*d;

  inv[3]=c10*d;
  inv[4]=c11*d;
  inv[5]=c12*d;

  inv[6]=c20*d;
  inv[7]=c21*d;
  inv[8]=c22*d;

  return true;
}

Vec3Local transformVector3(
    const Mat4 &m,
    Vec3Local v) noexcept {

  return {
      m.m[0]*v.x+
          m.m[4]*v.y+
          m.m[8]*v.z,

      m.m[1]*v.x+
          m.m[5]*v.y+
          m.m[9]*v.z,

      m.m[2]*v.x+
          m.m[6]*v.y+
          m.m[10]*v.z
  };
}

Vec3Local transformByInverse3(
    const float inv[9],
    Vec3Local v) noexcept {

  return {
      inv[0]*v.x+
          inv[1]*v.y+
          inv[2]*v.z,

      inv[3]*v.x+
          inv[4]*v.y+
          inv[5]*v.z,

      inv[6]*v.x+
          inv[7]*v.y+
          inv[8]*v.z
  };
}

class MatrixPushScope {
public:
  MatrixPushScope(
      void *stack,
      ItemPhysicsRuntime::MatrixPushFn push,
      ItemPhysicsRuntime::MatrixRefDtorFn dtor)
      :mDtor(dtor) {

    if (!stack||
        !push||
        !dtor)
      return;

    mRef=push(
        stack,
        false);

    mActive=
        mRef.stack&&
        mRef.mat;
  }

  MatrixPushScope(
      const MatrixPushScope &)=delete;

  MatrixPushScope &
  operator=(
      const MatrixPushScope &)=delete;

  ~MatrixPushScope() {
    if (!mActive||
        !mDtor)
      return;

    mDtor(&mRef);

    mRef.stack=nullptr;
    mRef.mat=nullptr;
  }

  [[nodiscard]]
  Mat4 *matrix() noexcept {
    return mActive
               ?mRef.mat
               :nullptr;
  }

private:
  ItemPhysicsRuntime::MatrixStackRefAbi mRef{};
  ItemPhysicsRuntime::MatrixRefDtorFn mDtor{};
  bool mActive{};
};

}

ItemPhysicsRuntime *
ItemPhysicsRuntime::sInstance=nullptr;

void ItemPhysicsRuntime::applyConfig(
    const ItemPhysicsConfig &config) noexcept {

  mEnabled.store(
      config.enabled,
      std::memory_order_relaxed);

  mSingleModel.store(
      config.singleModel,
      std::memory_order_relaxed);

  mHideItemShadow.store(
      config.hideItemShadow,
      std::memory_order_relaxed);

  mOldRotation.store(
      config.oldRotation,
      std::memory_order_relaxed);

  mRotationSpeed.store(
      static_cast<float>(
          config.rotationSpeed),
      std::memory_order_relaxed);
}

bool ItemPhysicsRuntime::verifyProfile(
    const ResolvedVirtual &resolved,
    ll::mod::NativeMod &mod) const {

  const auto verifyWords=
      [&](
          std::uintptr_t address,
          const auto &fingerprint,
          const char *name) {

        const std::size_t bytes=
            fingerprint.size()*
            sizeof(std::uint32_t);

        if (!resolved.module.readable(
                address,
                bytes)) {

          mod.getLogger().warn(
              "{} is not readable",
              name);

          return false;
        }

        const auto *words=
            reinterpret_cast<
                const std::uint32_t *>(
                address);

        for (std::size_t i=0;
             i<fingerprint.size();
             ++i) {

          if (words[i]==fingerprint[i])
            continue;

          mod.getLogger().warn(
              "Minecraft profile mismatch: {} word {}",
              name,
              i);

          return false;
        }

        return true;
      };

  if (!verifyWords(
          resolved.target,
          profile::kRenderFingerprint,
          "ItemRenderer::render")||

      !verifyWords(
          resolved.module.base+
              profile::kRenderItemGroupLikeRva,
          profile::kRenderItemGroupLikeFingerprint,
          "ItemRenderer render-group helper")||

      !verifyWords(
          resolved.module.base+
              profile::kGetBlockTypeForRenderingRva,
          profile::kGetBlockTypeForRenderingFingerprint,
          "ItemStackBase::getBlockTypeForRendering")||

      !verifyWords(
          resolved.module.base+
              profile::kBlockGraphicsGetForBlockTypeRva,
          profile::kBlockGraphicsGetForBlockTypeFingerprint,
          "BlockGraphics::getForBlock(BlockType)")||

      !verifyWords(
          resolved.module.base+
              profile::kBlockGraphicsGetBlockShapeRva,
          profile::kBlockGraphicsGetBlockShapeFingerprint,
          "BlockGraphics::getBlockShape")||

      !verifyWords(
          resolved.module.base+
              profile::kIsBlockShape3DRva,
          profile::kIsBlockShape3DFingerprint,
          "BlockGraphics::isBlockShape3D")||

      !verifyWords(
          resolved.module.base+
              profile::kRelativeShadowStorageRva,
          profile::kRelativeShadowStorageFingerprint,
          "RelativeShadowOffsetComponent storage")||

      !verifyWords(
          resolved.module.base+
              profile::kRelativeShadowEmplaceRva,
          profile::kRelativeShadowEmplaceFingerprint,
          "RelativeShadowOffsetComponent emplace")) {

    return false;
  }

  const auto executable=
      [&](std::uintptr_t rva) {

        return resolved.module.executable(
            resolved.module.base+rva);
      };

  if (!executable(profile::kGetWorldMatrixRva)||
      !executable(profile::kMatrixStackPushRva)||
      !executable(profile::kMatrixStackRefDtorRva)||
      !executable(profile::kRenderItemGroupLikeRva)||
      !executable(profile::kGetBlockTypeForRenderingRva)||
      !executable(profile::kBlockGraphicsGetForBlockTypeRva)||
      !executable(profile::kBlockGraphicsGetForBlockRva)||
      !executable(profile::kBlockGraphicsGetBlockShapeRva)||
      !executable(profile::kIsBlockShape3DRva)||
      !executable(profile::kRelativeShadowStorageRva)||
      !executable(profile::kRelativeShadowEmplaceRva)) {

    mod.getLogger().warn(
        "Minecraft 1.26.45 renderer helper validation failed");

    return false;
  }

  return true;
}

bool ItemPhysicsRuntime::install(
    ll::mod::NativeMod &mod) {

  uninstall();

  auto resolved=
      resolveVirtualByRtti(
          profile::kMinecraftModule,
          profile::kItemRendererRtti,
          profile::kItemRendererRenderVtableOffset);

  if (!resolved) {
    mod.getLogger().warn(
        "Item Physics inactive: failed to resolve ItemRenderer");

    return false;
  }

  if (!verifyProfile(
          *resolved,
          mod)) {

    mod.getLogger().warn(
        "Item Physics safe passthrough: Minecraft profile unsupported");

    return false;
  }

  mMinecraftBase=
      resolved->module.base;

  mRenderTarget=
      resolved->target;

  mRenderItemGroupTarget=
      mMinecraftBase+
      profile::kRenderItemGroupLikeRva;

  mGetWorldMatrix=
      reinterpret_cast<GetWorldMatrixFn>(
          mMinecraftBase+
          profile::kGetWorldMatrixRva);

  mMatrixPush=
      reinterpret_cast<MatrixPushFn>(
          mMinecraftBase+
          profile::kMatrixStackPushRva);

  mMatrixRefDtor=
      reinterpret_cast<MatrixRefDtorFn>(
          mMinecraftBase+
          profile::kMatrixStackRefDtorRva);

  mGetBlockTypeForRendering=
      reinterpret_cast<GetBlockTypeForRenderingFn>(
          mMinecraftBase+
          profile::kGetBlockTypeForRenderingRva);

  mGetBlockGraphicsForBlockType=
      reinterpret_cast<BlockGraphicsGetForBlockTypeFn>(
          mMinecraftBase+
          profile::kBlockGraphicsGetForBlockTypeRva);

  mGetBlockGraphicsForBlock=
      reinterpret_cast<BlockGraphicsGetForBlockFn>(
          mMinecraftBase+
          profile::kBlockGraphicsGetForBlockRva);

  mGetBlockGraphicsShape=
      reinterpret_cast<BlockGraphicsGetBlockShapeFn>(
          mMinecraftBase+
          profile::kBlockGraphicsGetBlockShapeRva);

  mIsBlockShape3D=
      reinterpret_cast<IsBlockShape3DFn>(
          mMinecraftBase+
          profile::kIsBlockShape3DRva);

  mGetRelativeShadowStorage=
      reinterpret_cast<RelativeShadowStorageFn>(
          mMinecraftBase+
          profile::kRelativeShadowStorageRva);

  mEmplaceRelativeShadow=
      reinterpret_cast<RelativeShadowEmplaceFn>(
          mMinecraftBase+
          profile::kRelativeShadowEmplaceRva);

  sInstance=this;
  mOriginal=nullptr;

  mHook=
      std::make_unique<
          pl::memory::HookHandle>(

          reinterpret_cast<void *>(
              mRenderTarget),

          reinterpret_cast<void *>(
              &ItemPhysicsRuntime::renderDetour),

          reinterpret_cast<void **>(
              &mOriginal),

          pl::memory::HookPriority::Normal);

  if (!mHook->installed()||
      !mOriginal) {

    mod.getLogger().error(
        "Failed to hook ItemRenderer::render");

    mHook.reset();
    sInstance=nullptr;

    return false;
  }

  mRenderItemGroupOriginal=nullptr;

  mRenderItemGroupHook=
      std::make_unique<
          pl::memory::HookHandle>(

          reinterpret_cast<void *>(
              mRenderItemGroupTarget),

          reinterpret_cast<void *>(
              &ItemPhysicsRuntime::
                  renderItemGroupDetour),

          reinterpret_cast<void **>(
              &mRenderItemGroupOriginal),

          pl::memory::HookPriority::Normal);

  if (!mRenderItemGroupHook->installed()||
      !mRenderItemGroupOriginal) {

    mod.getLogger().error(
        "Failed to hook item render-group helper");

    mRenderItemGroupHook.reset();

    mHook->reset();
    mHook.reset();

    mOriginal=nullptr;
    sInstance=nullptr;

    return false;
  }

  mProfileSupported.store(
      true,
      std::memory_order_relaxed);

  mod.getLogger().info(
      "Item Physics active: Java ItemPhysic renderer port + native actor yaw");

  return true;
}

void ItemPhysicsRuntime::uninstall() {
  mProfileSupported.store(
      false,
      std::memory_order_relaxed);

  if (mRenderItemGroupHook) {
    mRenderItemGroupHook->reset();
    mRenderItemGroupHook.reset();
  }

  if (mHook) {
    mHook->reset();
    mHook.reset();
  }

  if (sInstance==this)
    sInstance=nullptr;

  mOriginal=nullptr;
  mRenderItemGroupOriginal=nullptr;

  mGetWorldMatrix=nullptr;
  mMatrixPush=nullptr;
  mMatrixRefDtor=nullptr;

  mGetBlockTypeForRendering=nullptr;
  mGetBlockGraphicsForBlockType=nullptr;
  mGetBlockGraphicsForBlock=nullptr;
  mGetBlockGraphicsShape=nullptr;
  mIsBlockShape3D=nullptr;

  mGetRelativeShadowStorage=nullptr;
  mEmplaceRelativeShadow=nullptr;

  mRenderTarget=0;
  mRenderItemGroupTarget=0;
  mMinecraftBase=0;

  clearStates();
}

void ItemPhysicsRuntime::clearStates() {
  std::lock_guard lock(mStateMutex);

  mStates.clear();
  mRenderCounter=0;
}

void ItemPhysicsRuntime::renderDetour(
    void *self,
    void *renderContext,
    void *renderData) {

  if (sInstance)
    sInstance->onRender(
        self,
        renderContext,
        renderData);
}

void ItemPhysicsRuntime::renderItemGroupDetour(
    void *self,
    void *renderContext,
    void *itemData,
    std::uint32_t copyCount,
    std::uint32_t flags,
    float scale,
    float animation) {

  if (sInstance) {
    sInstance->onRenderItemGroup(
        self,
        renderContext,
        itemData,
        copyCount,
        flags,
        scale,
        animation);
  }
}

void ItemPhysicsRuntime::onRenderItemGroup(
    void *self,
    void *renderContext,
    void *itemData,
    std::uint32_t copyCount,
    std::uint32_t flags,
    float scale,
    float animation) {

  const auto original=
      mRenderItemGroupOriginal;

  if (!original)
    return;

  if (!gDroppedItemGroupActive||
      !gDroppedItemGroupGrounded||
      !self||
      !renderContext||
      copyCount<=1||
      !mGetWorldMatrix||
      !mMatrixPush||
      !mMatrixRefDtor) {

    original(
        self,
        renderContext,
        itemData,
        copyCount,
        flags,
        scale,
        animation);

    return;
  }

  MatrixPushScope probe(
      mGetWorldMatrix(
          renderContext),
      mMatrixPush,
      mMatrixRefDtor);

  Mat4 *current=
      probe.matrix();

  if (!current) {
    original(
        self,
        renderContext,
        itemData,
        copyCount,
        flags,
        scale,
        animation);

    return;
  }

  const Mat4 matrixSnapshot=
      *current;

  float inverse[9]{};

  if (!invert3x3FromMat4(
          matrixSnapshot,
          inverse)) {

    original(
        self,
        renderContext,
        itemData,
        copyCount,
        flags,
        scale,
        animation);

    return;
  }

  constexpr std::size_t kOffsetBase=0x17C;
  constexpr std::size_t kOffsetStride=0x0C;
  constexpr std::uint32_t kMaxExtraCopies=3;

  const std::uint32_t extra=
      std::min<std::uint32_t>(
          copyCount-1,
          kMaxExtraCopies);

  std::lock_guard tableLock(
      mGroupOffsetMutex);

  Vec3Local saved[
      kMaxExtraCopies]{};

  auto *base=
      reinterpret_cast<
          std::uint8_t *>(
          self);

  for (std::uint32_t i=0;
       i<extra;
       ++i) {

    auto *offset=
        reinterpret_cast<Vec3Local *>(
            base+
            kOffsetBase+
            static_cast<std::size_t>(i)*
                kOffsetStride);

    saved[i]=*offset;

    Vec3Local world=
        transformVector3(
            matrixSnapshot,
            saved[i]);

    world.y=0.0f;

    *offset=
        transformByInverse3(
            inverse,
            world);
  }

  original(
      self,
      renderContext,
      itemData,
      copyCount,
      flags,
      scale,
      animation);

  for (std::uint32_t i=0;
       i<extra;
       ++i) {

    auto *offset=
        reinterpret_cast<Vec3Local *>(
            base+
            kOffsetBase+
            static_cast<std::size_t>(i)*
                kOffsetStride);

    *offset=saved[i];
  }
}

float ItemPhysicsRuntime::seededUnit(
    std::uint32_t seed) noexcept {

  seed^=seed<<13;
  seed^=seed>>17;
  seed^=seed<<5;

  return static_cast<float>(
             seed&
             0x00FFFFFFu)/
         16777215.0f;
}

float ItemPhysicsRuntime::wrapPi(
    float value) noexcept {

  return std::remainder(
      value,
      2.0f*kPi);
}

float ItemPhysicsRuntime::moveAngle(
    float current,
    float target,
    float maxStep) noexcept {

  const float delta=
      wrapPi(
          target-current);

  if (std::abs(delta)<=maxStep)
    return wrapPi(target);

  return wrapPi(
      current+
      std::copysign(
          maxStep,
          delta));
}

bool ItemPhysicsRuntime::libcxxStringEquals(
    std::uintptr_t stringAddress,
    std::string_view wanted) noexcept {

  if (!stringAddress)
    return false;

  const auto *raw=
      reinterpret_cast<
          const std::uint8_t *>(
          stringAddress);

  const std::uint8_t flag=
      raw[0];

  std::size_t length=0;
  const char *data=nullptr;

  if ((flag&1u)==0) {
    length=
        static_cast<std::size_t>(
            flag>>1);

    data=
        reinterpret_cast<
            const char *>(
            raw+1);
  } else {
    length=
        *reinterpret_cast<
            const std::size_t *>(
            raw+8);

    data=
        *reinterpret_cast<
            const char *const *>(
            raw+16);
  }

  return data&&
         length==wanted.size()&&
         std::memcmp(
             data,
             wanted.data(),
             length)==0;
}

bool ItemPhysicsRuntime::tryGetRenderBlockShape(
    std::uintptr_t actorAddress,
    std::int32_t &shape) const noexcept {

  shape=-1;

  if (!actorAddress||
      !mGetBlockTypeForRendering||
      !mGetBlockGraphicsForBlockType||
      !mGetBlockGraphicsShape)
    return false;

  const void *itemStackBase=
      reinterpret_cast<
          const void *>(
          actorAddress+
          profile::kItemStackBaseOffset);

  const void *weakPtrAddress=
      mGetBlockTypeForRendering(
          itemStackBase);

  if (!weakPtrAddress)
    return false;

  const auto counter=
      *reinterpret_cast<
          const std::uintptr_t *>(
          weakPtrAddress);

  if (!counter)
    return false;

  const auto blockType=
      *reinterpret_cast<
          const std::uintptr_t *>(
          counter);

  if (!blockType)
    return false;

  const void *graphics=
      mGetBlockGraphicsForBlockType(
          reinterpret_cast<
              const void *>(
              blockType));

  if (!graphics)
    return false;

  shape=
      mGetBlockGraphicsShape(
          graphics);

  return true;
}

bool ItemPhysicsRuntime::tryGetBlockShape(
    const void *block,
    std::int32_t &shape) const noexcept {

  shape=-1;

  if (!block||
      !mGetBlockGraphicsForBlock||
      !mGetBlockGraphicsShape)
    return false;

  const void *graphics=
      mGetBlockGraphicsForBlock(
          block);

  if (!graphics)
    return false;

  shape=
      mGetBlockGraphicsShape(
          graphics);

  return true;
}

ItemPhysicsRuntime::ItemRenderTraits
ItemPhysicsRuntime::classifyItem(
    std::uintptr_t actorAddress) const noexcept {

  ItemRenderTraits traits{};

  if (!actorAddress)
    return traits;

  auto useBlockShape=
      [&](std::int32_t shape) {

        traits.valid=true;
        traits.modelClass=
            ModelClass::BlockItem;

        traits.hasBlockShape=true;
        traits.blockShape=shape;

        traits.blockLike=
            mIsBlockShape3D
                ?mIsBlockShape3D(shape)
                :true;

        traits.pivotY=
            traits.blockLike
                ?((shape==kSkullShape||
                   shape==kLargePivotShape)
                      ?kLargeBlockPivotY
                      :kNormalBlockPivotY)
                :0.0f;
      };

  std::int32_t renderShape=-1;

  if (tryGetRenderBlockShape(
          actorAddress,
          renderShape)) {

    useBlockShape(
        renderShape);

    return traits;
  }

  const auto block=
      *reinterpret_cast<
          const void *const *>(
          actorAddress+
          profile::kBlockPtrOffset);

  if (block) {
    std::int32_t blockShape=-1;

    if (tryGetBlockShape(
            block,
            blockShape)) {

      useBlockShape(
          blockShape);
    } else {
      traits.valid=true;

      traits.modelClass=
          ModelClass::BlockItem;

      traits.blockLike=true;
      traits.pivotY=
          kNormalBlockPivotY;
    }

    return traits;
  }

  const auto itemHandle=
      *reinterpret_cast<
          const std::uintptr_t *>(
          actorAddress+
          profile::kItemHandleOffset);

  if (!itemHandle)
    return traits;

  const auto item=
      *reinterpret_cast<
          const std::uintptr_t *>(
          itemHandle);

  if (!item)
    return traits;

  const auto identifier=
      item+
      profile::kItemIdentifierOffset;

  traits.valid=true;
  traits.blockLike=false;
  traits.pivotY=0.0f;

  if (libcxxStringEquals(
          identifier,
          kShieldId)) {

    traits.modelClass=
        ModelClass::SpecialItem;

    traits.specialKind=
        SpecialKind::Shield;
  }

  else if (libcxxStringEquals(
               identifier,
               kBannerId)) {

    traits.modelClass=
        ModelClass::SpecialItem;

    traits.specialKind=
        SpecialKind::Banner;
  }

  else {
    traits.modelClass=
        ModelClass::FlatItem;

    traits.specialKind=
        SpecialKind::None;
  }

  return traits;
}

void *ItemPhysicsRuntime::findComponentStorage(
    void *actor,
    std::uint32_t componentHash) const noexcept {

  if (!actor)
    return nullptr;

  const auto actorAddress=
      reinterpret_cast<
          std::uintptr_t>(
          actor);

  const auto registry=
      *reinterpret_cast<
          const std::uintptr_t *>(
          actorAddress+
          profile::kActorRegistryOffset);

  if (!registry)
    return nullptr;

  const auto bucketsBegin=
      *reinterpret_cast<
          const std::uintptr_t *>(
          registry+0x38);

  const auto bucketsEnd=
      *reinterpret_cast<
          const std::uintptr_t *>(
          registry+0x40);

  const auto nodesBase=
      *reinterpret_cast<
          const std::uintptr_t *>(
          registry+0x50);

  const auto sentinel=
      *reinterpret_cast<
          const std::uintptr_t *>(
          registry+0x58);

  if (!bucketsBegin||
      !bucketsEnd||
      bucketsEnd<=bucketsBegin||
      !nodesBase)
    return nullptr;

  const auto bucketBytes=
      bucketsEnd-
      bucketsBegin;

  if ((bucketBytes%
       sizeof(std::uintptr_t))!=0||

      bucketBytes/
              sizeof(std::uintptr_t)>
          (1u<<20))
    return nullptr;

  const auto bucketCount=
      bucketBytes/
      sizeof(std::uintptr_t);

  if (!bucketCount)
    return nullptr;

  const auto bucket=
      (bucketCount-1)&
      componentHash;

  std::int64_t nodeIndex=
      *reinterpret_cast<
          const std::int64_t *>(
          bucketsBegin+
          bucket*
              sizeof(std::uintptr_t));

  for (int guard=0;
       nodeIndex!=-1&&
       guard<4096;
       ++guard) {

    const auto node=
        nodesBase+
        static_cast<
            std::uintptr_t>(
            nodeIndex)*
            32u;

    if (node==sentinel)
      return nullptr;

    if (*reinterpret_cast<
            const std::uint32_t *>(
            node+8)==
        componentHash) {

      const auto storage=
          *reinterpret_cast<
              const std::uintptr_t *>(
              node+0x10);

      return reinterpret_cast<void *>(
          storage);
    }

    nodeIndex=
        *reinterpret_cast<
            const std::int64_t *>(
            node);
  }

  return nullptr;
}

bool ItemPhysicsRuntime::findPackedEntity(
    void *storage,
    std::uint32_t entityId,
    std::uint32_t &packedEntity) const noexcept {

  packedEntity=0;

  if (!storage)
    return false;

  const auto storageAddress=
      reinterpret_cast<
          std::uintptr_t>(
          storage);

  const auto pagesBegin=
      *reinterpret_cast<
          const std::uintptr_t *>(
          storageAddress+0x08);

  const auto pagesEnd=
      *reinterpret_cast<
          const std::uintptr_t *>(
          storageAddress+0x10);

  if (!pagesBegin||
      !pagesEnd||
      pagesEnd<pagesBegin)
    return false;

  const auto bytes=
      pagesEnd-
      pagesBegin;

  if ((bytes%
       sizeof(std::uintptr_t))!=0)
    return false;

  const auto pageCount=
      bytes/
      sizeof(std::uintptr_t);

  const auto pageIndex=
      (entityId>>11)&
      0x7Fu;

  if (pageIndex>=pageCount)
    return false;

  const auto page=
      *reinterpret_cast<
          const std::uintptr_t *>(
          pagesBegin+
          pageIndex*
              sizeof(std::uintptr_t));

  if (!page)
    return false;

  const auto slot=
      entityId&
      0x7FFu;

  packedEntity=
      *reinterpret_cast<
          const std::uint32_t *>(
          page+
          slot*
              sizeof(std::uint32_t));

  const auto generation=
      entityId&
      0xFFFC0000u;

  return
      (packedEntity^generation)<=
      0x3FFFEu;
}

void *ItemPhysicsRuntime::findDenseComponent(
    void *actor,
    std::uint32_t componentHash,
    std::size_t stride) const noexcept {

  if (!actor||
      !stride)
    return nullptr;

  void *storage=
      findComponentStorage(
          actor,
          componentHash);

  if (!storage)
    return nullptr;

  const auto actorAddress=
      reinterpret_cast<
          std::uintptr_t>(
          actor);

  const auto entityId=
      *reinterpret_cast<
          const std::uint32_t *>(
          actorAddress+
          profile::kActorEntityIdOffset);

  std::uint32_t packed=0;

  if (!findPackedEntity(
          storage,
          entityId,
          packed))
    return nullptr;

  const auto dense=
      packed&
      0x3FFFFu;

  const auto storageAddress=
      reinterpret_cast<
          std::uintptr_t>(
          storage);

  const auto componentPages=
      *reinterpret_cast<
          const std::uintptr_t *>(
          storageAddress+0x50);

  if (!componentPages)
    return nullptr;

  const auto page=
      *reinterpret_cast<
          const std::uintptr_t *>(
          componentPages+
          (dense>>7)*
              sizeof(std::uintptr_t));

  if (!page)
    return nullptr;

  return reinterpret_cast<void *>(
      page+
      static_cast<std::uintptr_t>(
          dense&0x7Fu)*
          stride);
}

bool ItemPhysicsRuntime::getActorRotation(
    void *actor,
    Vec2Abi &rotation) const noexcept {

  rotation={};

  const auto *component=
      static_cast<
          const ActorRotationComponentAbi *>(
          findDenseComponent(
              actor,
              profile::kActorRotationComponentHash,
              sizeof(ActorRotationComponentAbi)));

  if (!component||
      !std::isfinite(component->rot.x)||
      !std::isfinite(component->rot.y))
    return false;

  rotation=
      component->rot;

  return true;
}

bool ItemPhysicsRuntime::hasOnGroundComponent(
    void *actor) const noexcept {

  if (!actor)
    return false;

  void *storage=
      findComponentStorage(
          actor,
          profile::kOnGroundFlagComponentHash);

  if (!storage)
    return false;

  const auto actorAddress=
      reinterpret_cast<
          std::uintptr_t>(
          actor);

  const auto entityId=
      *reinterpret_cast<
          const std::uint32_t *>(
          actorAddress+
          profile::kActorEntityIdOffset);

  std::uint32_t packed=0;

  return findPackedEntity(
      storage,
      entityId,
      packed);
}

void ItemPhysicsRuntime::updateItemShadowComponent(
    void *actor,
    bool grounded,
    bool hideShadow) const noexcept {

  if (!actor||
      !mGetRelativeShadowStorage||
      !mEmplaceRelativeShadow)
    return;

  const auto actorAddress=
      reinterpret_cast<
          std::uintptr_t>(
          actor);

  auto *registry=
      *reinterpret_cast<
          void **>(
          actorAddress+
          profile::kActorRegistryOffset);

  const auto entityId=
      *reinterpret_cast<
          const std::uint32_t *>(
          actorAddress+
          profile::kActorEntityIdOffset);

  if (!registry)
    return;

  void *storage=
      findComponentStorage(
          actor,
          profile::kRelativeShadowOffsetComponentHash);

  if (!storage) {
    storage=
        mGetRelativeShadowStorage(
            registry,
            profile::kRelativeShadowOffsetComponentHash);
  }

  if (!storage)
    return;

  const float wanted=
      hideShadow
          ?std::numeric_limits<float>::max()
          :(grounded
                ?0.0f
                :-0.5f);

  std::uint32_t packed=0;

  if (!findPackedEntity(
          storage,
          entityId,
          packed)) {

    const std::uint32_t entityCopy=
        entityId;

    (void)mEmplaceRelativeShadow(
        storage,
        &entityCopy,
        false,
        &wanted);

    return;
  }

  const auto dense=
      packed&
      0x3FFFFu;

  const auto storageAddress=
      reinterpret_cast<
          std::uintptr_t>(
          storage);

  const auto componentPages=
      *reinterpret_cast<
          const std::uintptr_t *>(
          storageAddress+0x50);

  if (!componentPages)
    return;

  const auto page=
      *reinterpret_cast<
          const std::uintptr_t *>(
          componentPages+
          (dense>>7)*
              sizeof(std::uintptr_t));

  if (!page)
    return;

  *reinterpret_cast<float *>(
      page+
      static_cast<std::uintptr_t>(
          dense&0x7Fu)*
          sizeof(float))=
      wanted;
}

ItemPhysicsRuntime::PhysicsState &
ItemPhysicsRuntime::stateFor(
    std::uint32_t entityId,
    std::chrono::steady_clock::time_point now) {

  auto [it,inserted]=
      mStates.try_emplace(
          entityId);

  auto &state=
      it->second;

  if (inserted||
      !state.initialized) {

    state.initialized=true;

    state.roll=0.0f;

    state.yaw=
        (seededUnit(
             entityId^
             0x27D4EB2Du)*
             2.0f-
         1.0f)*
        kPi;

    state.lastUpdate=now;
    state.lastSeen=now;
  }

  return state;
}

void ItemPhysicsRuntime::updateState(
    PhysicsState &state,
    bool blockLike,
    bool grounded,
    bool hasNativeRotation,
    float nativeYaw,
    std::chrono::steady_clock::time_point now) const {

  const float dt=
      std::clamp(
          std::chrono::duration<float>(
              now-
              state.lastUpdate)
              .count(),
          0.0f,
          0.05f);

  state.lastUpdate=now;
  state.lastSeen=now;

  if (hasNativeRotation&&
      std::isfinite(nativeYaw)) {

    state.yaw=
        wrapPi(
            nativeYaw);
  }

  const float speed=
      std::clamp(
          mRotationSpeed.load(
              std::memory_order_relaxed),
          0.0f,
          3.0f);

  if (!grounded) {
    state.roll=
        wrapPi(
            state.roll+
            kJavaAirRollRate*
                speed*
                dt);

    return;
  }

  if (!blockLike) {
    state.roll=0.0f;
    return;
  }

  if (!mOldRotation.load(
          std::memory_order_relaxed))
    return;

  const float target=
      std::round(
          state.roll/
          kHalfPi)*
      kHalfPi;

  const float error=
      std::abs(
          wrapPi(
              target-
              state.roll));

  if (error<=kOldSnapEpsilon) {
    state.roll=
        wrapPi(
            target);

    return;
  }

  state.roll=
      moveAngle(
          state.roll,
          target,
          kJavaOldGroundRate*
              speed*
              dt);
}

void ItemPhysicsRuntime::pruneStates(
    std::chrono::steady_clock::time_point now) {

  for (auto it=mStates.begin();
       it!=mStates.end();) {

    if (now-
            it->second.lastSeen>
        kStateTtl) {

      it=
          mStates.erase(
              it);
    } else {
      ++it;
    }
  }
}

void ItemPhysicsRuntime::onRender(
    void *self,
    void *renderContext,
    void *renderData) {

  const auto original=
      mOriginal;

  if (!original)
    return;

  if (!renderContext||
      !renderData||
      !mProfileSupported.load(
          std::memory_order_relaxed)) {

    original(
        self,
        renderContext,
        renderData);

    return;
  }

  const auto renderDataAddress=
      reinterpret_cast<
          std::uintptr_t>(
          renderData);

  auto *actor=
      *reinterpret_cast<
          void **>(
          renderDataAddress+
          profile::kRenderDataActorOffset);

  if (!actor) {
    original(
        self,
        renderContext,
        renderData);

    return;
  }

  const auto actorAddress=
      reinterpret_cast<
          std::uintptr_t>(
          actor);

  const bool grounded=
      hasOnGroundComponent(
          actor);

  const bool enabled=
      mEnabled.load(
          std::memory_order_relaxed);

  updateItemShadowComponent(
      actor,
      grounded,
      enabled&&
          mHideItemShadow.load(
              std::memory_order_relaxed));

  if (!enabled) {
    original(
        self,
        renderContext,
        renderData);

    return;
  }

  const ItemRenderTraits traits=
      classifyItem(
          actorAddress);

  if (!traits.valid) {
    original(
        self,
        renderContext,
        renderData);

    return;
  }

  Vec2Abi nativeRotation{};

  const bool hasNativeRotation=
      getActorRotation(
          actor,
          nativeRotation);

  const float nativeYaw=
      hasNativeRotation
          ?nativeRotation.y*
               kDegToRad
          :0.0f;

  const auto entityId=
      *reinterpret_cast<
          const std::uint32_t *>(
          actorAddress+
          profile::kActorEntityIdOffset);

  PhysicsState snapshot{};

  {
    std::lock_guard lock(
        mStateMutex);

    const auto now=
        std::chrono::
            steady_clock::now();

    auto &state=
        stateFor(
            entityId,
            now);

    updateState(
        state,
        traits.blockLike,
        grounded,
        hasNativeRotation,
        nativeYaw,
        now);

    snapshot=state;

    if ((++mRenderCounter&
         0xFFu)==0) {

      pruneStates(
          now);
    }
  }

  auto &count=
      *reinterpret_cast<
          std::uint8_t *>(
          actorAddress+
          profile::kItemCountOffset);

  auto &inItemFrame=
      *reinterpret_cast<
          std::uint8_t *>(
          actorAddress+
          profile::kIsInItemFrameOffset);

  const auto oldCount=count;
  const auto oldInItemFrame=
      inItemFrame;

  if (mSingleModel.load(
          std::memory_order_relaxed)) {
    count=1;
  }

  inItemFrame=1;

  auto *position=
      reinterpret_cast<
          float *>(
          renderDataAddress+
          profile::kRenderDataPositionOffset);

  void *stack=
      mGetWorldMatrix
          ?mGetWorldMatrix(
               renderContext)
          :nullptr;

  {
    MatrixPushScope matrixScope(
        stack,
        mMatrixPush,
        mMatrixRefDtor);

    Mat4 *matrix=
        matrixScope.matrix();

    if (!matrix) {
      count=oldCount;
      inItemFrame=oldInItemFrame;

      original(
          self,
          renderContext,
          renderData);

      return;
    }

    const float x=position[0];
    const float y=position[1];
    const float z=position[2];

    postTranslate(
        *matrix,
        x,
        y,
        z);

    postRotateX(
        *matrix,
        kJavaBaseTilt);

    postRotateZ(
        *matrix,
        snapshot.yaw);

    if (traits.blockLike) {
      postTranslate(
          *matrix,
          0.0f,
          kJavaBlockOffsetY,
          kJavaBlockOffsetZ);

      postTranslate(
          *matrix,
          0.0f,
          traits.pivotY,
          0.0f);

      postRotateY(
          *matrix,
          snapshot.roll);

      postTranslate(
          *matrix,
          0.0f,
          -traits.pivotY,
          0.0f);
    } else {
      postTranslate(
          *matrix,
          0.0f,
          0.0f,
          kJavaFlatOffsetZ);

      postRotateY(
          *matrix,
          snapshot.roll);
    }

    postTranslate(
        *matrix,
        -x,
        -y,
        -z);

    const bool previousActive=
        gDroppedItemGroupActive;

    const bool previousGrounded=
        gDroppedItemGroupGrounded;

    gDroppedItemGroupActive=
        !mSingleModel.load(
            std::memory_order_relaxed);

    gDroppedItemGroupGrounded=
        grounded;

    original(
        self,
        renderContext,
        renderData);

    gDroppedItemGroupActive=
        previousActive;

    gDroppedItemGroupGrounded=
        previousGrounded;
  }

  count=oldCount;
  inItemFrame=oldInItemFrame;
}

}
