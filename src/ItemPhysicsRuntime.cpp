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
constexpr auto kStateTtl=std::chrono::seconds(8);
constexpr std::int32_t kSkullShape=83;
constexpr float kExtentEpsilon=0.0005f;
constexpr float kThinYRatio=0.70f;
constexpr std::string_view kShieldId="minecraft:shield";
constexpr std::string_view kBannerId="minecraft:banner";

constexpr float kJavaBaseTilt=kHalfPi;
constexpr float kJavaAirRollRate=10.0f;
constexpr float kJavaOldGroundRate=5.0f;
constexpr float kJavaBlockPivotY=0.20f;
constexpr float kJavaBlockOffsetY=-0.20f;
constexpr float kJavaBlockOffsetZ=-0.08f;
constexpr float kPlanarPreAlignStartY=0.035f;
constexpr float kPlanarPreAlignFullY=-0.12f;
constexpr float kPlanarPreAlignRate=8.0f;
constexpr float kPlanarGroundRate=14.0f;
constexpr float kShapedMaxGroundTilt=35.0f*kDegToRad;

constexpr float kFlatHeight=-0.09f;
constexpr float kThinHeight=-0.11f;
constexpr float kShapedHeight=-0.10f;
constexpr float kHeadHeight=-0.25f;
constexpr float kSpecialHeight=-0.10f;
constexpr float kAtlasFlatLocalY=0.25f;

constexpr float kMinecraftTicksPerSecond=20.0f;
constexpr float kSensitiveWake=0.010f;
constexpr float kBaseNaturalSpin=0.72f;
constexpr float kHorizontalSpinGain=0.40f;
constexpr float kVerticalSpinGain=0.08f;
constexpr float kMaxFlatAngular=3.15f;
constexpr float kMaxThinAngular=2.45f;
constexpr float kMaxSpecialAngular=2.75f;
constexpr float kAirDampingFlat=2.15f;
constexpr float kAirDampingThin=2.45f;
constexpr float kAirDampingSpecial=2.25f;
constexpr float kPreSettleDelay=0.040f;
constexpr float kPreSettleRamp=0.260f;
constexpr float kPreSpringFlat=27.0f;
constexpr float kPreSpringThin=29.0f;
constexpr float kPreSpringSpecial=27.0f;
constexpr float kPreDampingFlat=8.6f;
constexpr float kPreDampingThin=9.2f;
constexpr float kPreDampingSpecial=8.7f;
constexpr float kContactSpringFlat=34.0f;
constexpr float kContactSpringThin=36.0f;
constexpr float kContactSpringSpecial=34.0f;
constexpr float kContactDampingFlat=11.4f;
constexpr float kContactDampingThin=12.0f;
constexpr float kContactDampingSpecial=11.5f;
constexpr float kStopAngular=0.020f;
constexpr float kStopError=0.0045f;

constexpr float kHeadAirTilt=24.0f*kDegToRad;
constexpr float kHeadAirResponse=6.5f;
constexpr float kHeadGroundResponse=10.0f;

struct Vec3Local { float x{},y{},z{}; };
struct QuatLocal { float w{1.0f},x{},y{},z{}; };
thread_local bool gDroppedItemGroupActive=false;
thread_local bool gDroppedItemGroupGrounded=false;

float dot3(Vec3Local a,Vec3Local b) noexcept { return a.x*b.x+a.y*b.y+a.z*b.z; }
Vec3Local cross3(Vec3Local a,Vec3Local b) noexcept { return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x}; }
float length3(Vec3Local v) noexcept { return std::sqrt(dot3(v,v)); }

Vec3Local normalize3(Vec3Local v,Vec3Local fallback={1,0,0}) noexcept {
  const float l=length3(v);
  if(!std::isfinite(l)||l<1e-6f)return fallback;
  const float s=1.0f/l;
  return {v.x*s,v.y*s,v.z*s};
}

QuatLocal normalizeQ(QuatLocal q) noexcept {
  const float l=std::sqrt(q.w*q.w+q.x*q.x+q.y*q.y+q.z*q.z);
  if(!std::isfinite(l)||l<1e-7f)return {};
  const float s=1.0f/l;
  return {q.w*s,q.x*s,q.y*s,q.z*s};
}

QuatLocal mulQ(QuatLocal a,QuatLocal b) noexcept {
  return {
      a.w*b.w-a.x*b.x-a.y*b.y-a.z*b.z,
      a.w*b.x+a.x*b.w+a.y*b.z-a.z*b.y,
      a.w*b.y-a.x*b.z+a.y*b.w+a.z*b.x,
      a.w*b.z+a.x*b.y-a.y*b.x+a.z*b.w
  };
}

QuatLocal axisAngleQ(Vec3Local axis,float angle) noexcept {
  axis=normalize3(axis);
  const float h=angle*0.5f,s=std::sin(h);
  return normalizeQ({std::cos(h),axis.x*s,axis.y*s,axis.z*s});
}

Vec3Local rotateQ(QuatLocal q,Vec3Local v) noexcept {
  q=normalizeQ(q);
  const Vec3Local u{q.x,q.y,q.z};
  const Vec3Local uv=cross3(u,v),uuv=cross3(u,uv);
  return {
      v.x+2*(q.w*uv.x+uuv.x),
      v.y+2*(q.w*uv.y+uuv.y),
      v.z+2*(q.w*uv.z+uuv.z)
  };
}

QuatLocal fromToQ(Vec3Local from,Vec3Local to) noexcept {
  from=normalize3(from);
  to=normalize3(to);
  const float d=std::clamp(dot3(from,to),-1.0f,1.0f);
  if(d>0.999999f)return {};
  if(d<-0.999999f){
    Vec3Local axis=cross3(from,{1,0,0});
    if(length3(axis)<1e-4f)axis=cross3(from,{0,0,1});
    return axisAngleQ(axis,kPi);
  }
  const Vec3Local c=cross3(from,to);
  return normalizeQ({1+d,c.x,c.y,c.z});
}

Vec3Local shortestAngularError(QuatLocal current,QuatLocal target) noexcept {
  current=normalizeQ(current);
  target=normalizeQ(target);

  QuatLocal d=normalizeQ(
      mulQ(
          target,
          {current.w,-current.x,-current.y,-current.z}));

  if(d.w<0){
    d.w=-d.w;
    d.x=-d.x;
    d.y=-d.y;
    d.z=-d.z;
  }

  const float vl=std::sqrt(
      d.x*d.x+
      d.y*d.y+
      d.z*d.z);

  if(vl<1e-7f)return {};

  const float a=2*std::atan2(
      vl,
      std::clamp(d.w,-1.0f,1.0f));

  const float s=a/vl;

  return {
      d.x*s,
      d.y*s,
      d.z*s
  };
}

QuatLocal integrateWorldAngular(
    QuatLocal q,
    Vec3Local omega,
    float dt) noexcept {

  const float speed=length3(omega);

  if(speed<1e-5f||dt<=0)
    return q;

  return normalizeQ(
      mulQ(
          axisAngleQ(omega,speed*dt),
          q));
}

void postRotateQuat(
    Mat4 &m,
    QuatLocal q) noexcept {

  q=normalizeQ(q);

  const float xx=q.x*q.x;
  const float yy=q.y*q.y;
  const float zz=q.z*q.z;
  const float xy=q.x*q.y;
  const float xz=q.x*q.z;
  const float yz=q.y*q.z;
  const float wx=q.w*q.x;
  const float wy=q.w*q.y;
  const float wz=q.w*q.z;

  const float r00=1-2*(yy+zz);
  const float r01=2*(xy-wz);
  const float r02=2*(xz+wy);

  const float r10=2*(xy+wz);
  const float r11=1-2*(xx+zz);
  const float r12=2*(yz-wx);

  const float r20=2*(xz-wy);
  const float r21=2*(yz+wx);
  const float r22=1-2*(xx+yy);

  float c0[4],c1[4],c2[4];

  std::memcpy(c0,&m.m[0],sizeof(c0));
  std::memcpy(c1,&m.m[4],sizeof(c1));
  std::memcpy(c2,&m.m[8],sizeof(c2));

  for(int r=0;r<4;++r){
    m.m[r]=
        c0[r]*r00+
        c1[r]*r10+
        c2[r]*r20;

    m.m[4+r]=
        c0[r]*r01+
        c1[r]*r11+
        c2[r]*r21;

    m.m[8+r]=
        c0[r]*r02+
        c1[r]*r12+
        c2[r]*r22;
  }
}

bool invert3x3FromMat4(
    const Mat4 &m,
    float inv[9]) noexcept {

  const float a00=m.m[0];
  const float a01=m.m[4];
  const float a02=m.m[8];
  const float a10=m.m[1];
  const float a11=m.m[5];
  const float a12=m.m[9];
  const float a20=m.m[2];
  const float a21=m.m[6];
  const float a22=m.m[10];

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

  if(!std::isfinite(det)||
     std::abs(det)<1e-8f)
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
    const Mat4&m,
    Vec3Local v) noexcept {

  return {
      m.m[0]*v.x+m.m[4]*v.y+m.m[8]*v.z,
      m.m[1]*v.x+m.m[5]*v.y+m.m[9]*v.z,
      m.m[2]*v.x+m.m[6]*v.y+m.m[10]*v.z
  };
}

Vec3Local transformByInverse3(
    const float inv[9],
    Vec3Local v) noexcept {

  return {
      inv[0]*v.x+inv[1]*v.y+inv[2]*v.z,
      inv[3]*v.x+inv[4]*v.y+inv[5]*v.z,
      inv[6]*v.x+inv[7]*v.y+inv[8]*v.z
  };
}

bool isThinGroundShape(std::int32_t s) noexcept {
  switch(s){
  case 9:
  case 14:
  case 15:
  case 23:
  case 67:
  case 68:
  case 69:
  case 72:
  case 73:
  case 80:
  case 96:
  case 99:
  case 114:
  case 126:
  case 135:
  case 136:
    return true;
  default:
    return false;
  }
}

bool isTorchGroundShape(std::int32_t s) noexcept {
  switch(s){
  case 1:
  case 2:
  case 90:
  case 101:
  case 155:
    return true;
  default:
    return false;
  }
}

bool isShapedGroundShape(std::int32_t s) noexcept {
  switch(s){
  case 7:
  case 8:
  case 10:
  case 11:
  case 12:
  case 13:
  case 18:
  case 19:
  case 20:
  case 21:
  case 22:
  case 25:
  case 26:
  case 28:
  case 31:
  case 32:
  case 40:
  case 42:
  case 43:
  case 44:
  case 70:
  case 71:
  case 74:
  case 76:
  case 77:
  case 78:
  case 79:
  case 81:
  case 84:
  case 87:
  case 89:
  case 100:
  case 102:
  case 107:
  case 108:
  case 110:
  case 111:
  case 112:
  case 113:
  case 115:
  case 116:
  case 119:
  case 123:
  case 133:
    return true;
  default:
    return false;
  }
}

class MatrixPushScope {
public:
  MatrixPushScope(
      void *stack,
      ItemPhysicsRuntime::MatrixPushFn push,
      ItemPhysicsRuntime::MatrixRefDtorFn dtor)
      :mDtor(dtor){

    if(stack&&push&&dtor){
      mRef=push(stack,false);
      mActive=mRef.stack&&mRef.mat;
    }
  }

  MatrixPushScope(
      const MatrixPushScope&)=delete;

  MatrixPushScope&
  operator=(
      const MatrixPushScope&)=delete;

  ~MatrixPushScope(){
    if(mActive&&mDtor){
      mDtor(&mRef);
      mRef.stack=nullptr;
      mRef.mat=nullptr;
    }
  }

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

ItemPhysicsRuntime *ItemPhysicsRuntime::sInstance=nullptr;

void ItemPhysicsRuntime::applyConfig(
    const ItemPhysicsConfig &c) noexcept {

  mEnabled.store(
      c.enabled,
      std::memory_order_relaxed);

  mSingleModel.store(
      c.singleModel,
      std::memory_order_relaxed);

  mHideItemShadow.store(
      c.hideItemShadow,
      std::memory_order_relaxed);

  mOldRotation.store(
      c.oldRotation,
      std::memory_order_relaxed);

  mRotationSpeed.store(
      static_cast<float>(
          c.rotationSpeed),
      std::memory_order_relaxed);
}

bool ItemPhysicsRuntime::verifyProfile(
    const ResolvedVirtual &r,
    ll::mod::NativeMod &mod) const {

  const auto verify=
      [&](
          std::uintptr_t address,
          const auto &fp,
          const char *name){

        const std::size_t bytes=
            fp.size()*
            sizeof(std::uint32_t);

        if(!r.module.readable(
                address,
                bytes)){

          mod.getLogger().warn(
              "{} is not readable",
              name);

          return false;
        }

        const auto *w=
            reinterpret_cast<
                const std::uint32_t*>(
                address);

        for(std::size_t i=0;
            i<fp.size();
            ++i){

          if(w[i]!=fp[i]){
            mod.getLogger().warn(
                "Minecraft profile mismatch: {} word {}",
                name,
                i);

            return false;
          }
        }

        return true;
      };

  if(!verify(
         r.target,
         profile::kRenderFingerprint,
         "ItemRenderer::render")||

     !verify(
         r.module.base+
             profile::kGetPosDeltaRva,
         profile::kGetPosDeltaFingerprint,
         "Actor::getPosDelta")||

     !verify(
         r.module.base+
             profile::kRenderItemGroupLikeRva,
         profile::kRenderItemGroupLikeFingerprint,
         "ItemRenderer render-group helper")||

     !verify(
         r.module.base+
             profile::kGetBlockTypeForRenderingRva,
         profile::kGetBlockTypeForRenderingFingerprint,
         "ItemStackBase::getBlockTypeForRendering")||

     !verify(
         r.module.base+
             profile::kBlockGraphicsGetForBlockTypeRva,
         profile::kBlockGraphicsGetForBlockTypeFingerprint,
         "BlockGraphics::getForBlock(BlockType)")||

     !verify(
         r.module.base+
             profile::kBlockGraphicsGetBlockShapeRva,
         profile::kBlockGraphicsGetBlockShapeFingerprint,
         "BlockGraphics::getBlockShape")||

     !verify(
         r.module.base+
             profile::kIsBlockShape3DRva,
         profile::kIsBlockShape3DFingerprint,
         "BlockGraphics::isBlockShape3D")||

     !verify(
         r.module.base+
             profile::kRelativeShadowStorageRva,
         profile::kRelativeShadowStorageFingerprint,
         "RelativeShadowOffsetComponent storage")||

     !verify(
         r.module.base+
             profile::kRelativeShadowEmplaceRva,
         profile::kRelativeShadowEmplaceFingerprint,
         "RelativeShadowOffsetComponent emplace"))
    return false;

  const auto ex=
      [&](std::uintptr_t rva){
        return r.module.executable(
            r.module.base+rva);
      };

  if(!ex(profile::kGetWorldMatrixRva)||
     !ex(profile::kMatrixStackPushRva)||
     !ex(profile::kMatrixStackRefDtorRva)||
     !ex(profile::kGetPosDeltaRva)||
     !ex(profile::kRenderItemGroupLikeRva)||
     !ex(profile::kGetBlockTypeForRenderingRva)||
     !ex(profile::kBlockGraphicsGetForBlockTypeRva)||
     !ex(profile::kBlockGraphicsGetForBlockRva)||
     !ex(profile::kBlockGraphicsGetBlockShapeRva)||
     !ex(profile::kIsBlockShape3DRva)||
     !ex(profile::kRelativeShadowStorageRva)||
     !ex(profile::kRelativeShadowEmplaceRva)){

    mod.getLogger().warn(
        "Minecraft 1.26.45 renderer helper validation failed");

    return false;
  }

  return true;
}

bool ItemPhysicsRuntime::install(
    ll::mod::NativeMod &mod){

  uninstall();

  auto r=
      resolveVirtualByRtti(
          profile::kMinecraftModule,
          profile::kItemRendererRtti,
          profile::kItemRendererRenderVtableOffset);

  if(!r){
    mod.getLogger().warn(
        "Item Physics inactive: failed to resolve ItemRenderer");

    return false;
  }

  if(!verifyProfile(*r,mod)){
    mod.getLogger().warn(
        "Item Physics safe passthrough: Minecraft profile unsupported");

    return false;
  }

  mMinecraftBase=r->module.base;
  mRenderTarget=r->target;

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

  mGetPosDelta=
      reinterpret_cast<GetPosDeltaFn>(
          mMinecraftBase+
          profile::kGetPosDeltaRva);

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
          reinterpret_cast<void*>(
              mRenderTarget),
          reinterpret_cast<void*>(
              &ItemPhysicsRuntime::renderDetour),
          reinterpret_cast<void**>(
              &mOriginal),
          pl::memory::HookPriority::Normal);

  if(!mHook->installed()||
     !mOriginal){

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
          reinterpret_cast<void*>(
              mRenderItemGroupTarget),
          reinterpret_cast<void*>(
              &ItemPhysicsRuntime::renderItemGroupDetour),
          reinterpret_cast<void**>(
              &mRenderItemGroupOriginal),
          pl::memory::HookPriority::Normal);

  if(!mRenderItemGroupHook->installed()||
     !mRenderItemGroupOriginal){

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
      "Item Physics active: Java core + Bedrock family landing v9");

  return true;
}

void ItemPhysicsRuntime::uninstall(){
  mProfileSupported.store(
      false,
      std::memory_order_relaxed);

  if(mRenderItemGroupHook){
    mRenderItemGroupHook->reset();
    mRenderItemGroupHook.reset();
  }

  if(mHook){
    mHook->reset();
    mHook.reset();
  }

  if(sInstance==this)
    sInstance=nullptr;

  mOriginal=nullptr;
  mRenderItemGroupOriginal=nullptr;
  mGetWorldMatrix=nullptr;
  mMatrixPush=nullptr;
  mMatrixRefDtor=nullptr;
  mGetPosDelta=nullptr;
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

void ItemPhysicsRuntime::clearStates(){
  std::lock_guard lock(
      mStateMutex);

  mStates.clear();
  mRenderCounter=0;
}

void ItemPhysicsRuntime::renderDetour(
    void*a,
    void*b,
    void*c){

  if(sInstance)
    sInstance->onRender(
        a,
        b,
        c);
}

void ItemPhysicsRuntime::renderItemGroupDetour(
    void*a,
    void*b,
    void*c,
    std::uint32_t d,
    std::uint32_t e,
    float f,
    float g){

  if(sInstance)
    sInstance->onRenderItemGroup(
        a,
        b,
        c,
        d,
        e,
        f,
        g);
}

void ItemPhysicsRuntime::onRenderItemGroup(
    void *self,
    void *ctx,
    void *itemData,
    std::uint32_t count,
    std::uint32_t flags,
    float scale,
    float animation){

  const auto original=
      mRenderItemGroupOriginal;

  if(!original)
    return;

  if(!gDroppedItemGroupActive||
     !gDroppedItemGroupGrounded||
     !self||
     !ctx||
     count<=1||
     !mGetWorldMatrix||
     !mMatrixPush||
     !mMatrixRefDtor){

    original(
        self,
        ctx,
        itemData,
        count,
        flags,
        scale,
        animation);

    return;
  }

  MatrixPushScope probe(
      mGetWorldMatrix(ctx),
      mMatrixPush,
      mMatrixRefDtor);

  Mat4 *current=
      probe.matrix();

  if(!current){
    original(
        self,
        ctx,
        itemData,
        count,
        flags,
        scale,
        animation);

    return;
  }

  const Mat4 snapshot=
      *current;

  float inverse[9]{};

  if(!invert3x3FromMat4(
          snapshot,
          inverse)){

    original(
        self,
        ctx,
        itemData,
        count,
        flags,
        scale,
        animation);

    return;
  }

  constexpr std::size_t baseOffset=0x17C;
  constexpr std::size_t stride=0x0C;
  constexpr std::uint32_t maxExtra=3;

  const std::uint32_t extra=
      std::min<std::uint32_t>(
          count-1,
          maxExtra);

  std::lock_guard tableLock(
      mGroupOffsetMutex);

  Vec3Local saved[maxExtra]{};

  auto *base=
      reinterpret_cast<
          std::uint8_t*>(
          self);

  for(std::uint32_t i=0;
      i<extra;
      ++i){

    auto *off=
        reinterpret_cast<
            Vec3Local*>(
            base+
            baseOffset+
            static_cast<std::size_t>(i)*
                stride);

    saved[i]=*off;

    Vec3Local world=
        transformVector3(
            snapshot,
            saved[i]);

    world.y=0;

    *off=
        transformByInverse3(
            inverse,
            world);
  }

  original(
      self,
      ctx,
      itemData,
      count,
      flags,
      scale,
      animation);

  for(std::uint32_t i=0;
      i<extra;
      ++i){

    auto *off=
        reinterpret_cast<
            Vec3Local*>(
            base+
            baseOffset+
            static_cast<std::size_t>(i)*
                stride);

    *off=saved[i];
  }
}

float ItemPhysicsRuntime::seededUnit(
    std::uint32_t s) noexcept {

  s^=s<<13;
  s^=s>>17;
  s^=s<<5;

  return static_cast<float>(
             s&0x00FFFFFFu)/
         16777215.0f;
}

float ItemPhysicsRuntime::wrapPi(
    float v) noexcept {

  return std::remainder(
      v,
      2*kPi);
}

float ItemPhysicsRuntime::moveAngle(
    float current,
    float target,
    float maxStep) noexcept {

  const float d=
      wrapPi(
          target-current);

  if(std::abs(d)<=maxStep)
    return wrapPi(target);

  return wrapPi(
      current+
      std::copysign(
          maxStep,
          d));
}

bool ItemPhysicsRuntime::libcxxStringEquals(
    std::uintptr_t address,
    std::string_view wanted) noexcept {

  if(!address)
    return false;

  const auto *raw=
      reinterpret_cast<
          const std::uint8_t*>(
          address);

  const std::uint8_t flag=
      raw[0];

  std::size_t len=0;
  const char *data=nullptr;

  if((flag&1u)==0){
    len=
        static_cast<std::size_t>(
            flag>>1);

    data=
        reinterpret_cast<
            const char*>(
            raw+1);
  }else{
    len=
        *reinterpret_cast<
            const std::size_t*>(
            raw+8);

    data=
        *reinterpret_cast<
            const char*const*>(
            raw+16);
  }

  return data&&
         len==wanted.size()&&
         std::memcmp(
             data,
             wanted.data(),
             len)==0;
}

bool ItemPhysicsRuntime::tryGetRenderBlockShape(
    std::uintptr_t actor,
    std::int32_t &shape) const noexcept {

  shape=-1;

  if(!actor||
     !mGetBlockTypeForRendering||
     !mGetBlockGraphicsForBlockType||
     !mGetBlockGraphicsShape)
    return false;

  const void *weak=
      mGetBlockTypeForRendering(
          reinterpret_cast<
              const void*>(
              actor+
              profile::kItemStackBaseOffset));

  if(!weak)
    return false;

  const auto counter=
      *reinterpret_cast<
          const std::uintptr_t*>(
          weak);

  if(!counter)
    return false;

  const auto type=
      *reinterpret_cast<
          const std::uintptr_t*>(
          counter);

  if(!type)
    return false;

  const void *graphics=
      mGetBlockGraphicsForBlockType(
          reinterpret_cast<
              const void*>(
              type));

  if(!graphics)
    return false;

  shape=
      mGetBlockGraphicsShape(
          graphics);

  return true;
}

bool ItemPhysicsRuntime::buildBlockRenderInfo(
    const void *block,
    BlockRenderInfo &info) const noexcept {

  info={};

  if(!block)
    return false;

  if(mGetBlockGraphicsForBlock&&
     mGetBlockGraphicsShape){

    if(const void *g=
           mGetBlockGraphicsForBlock(
               block))

      info.blockShape=
          mGetBlockGraphicsShape(
              g);
  }

  const auto addr=
      reinterpret_cast<
          std::uintptr_t>(
          block);

  auto *type=
      *reinterpret_cast<
          void*const*>(
          addr+
          profile::kBlockTypeOffset);

  if(type){
    auto **vt=
        *reinterpret_cast<
            void***>(
            type);

    if(vt){
      constexpr std::size_t slot=
          profile::kBlockTypeGetVisualShapeVtableOffset/
          sizeof(void*);

      auto fn=
          reinterpret_cast<
              GetVisualShapeFn>(
              vt[slot]);

      if(fn){
        AabbAbi scratch{};

        if(const AabbAbi *b=
               fn(
                   type,
                   block,
                   &scratch)){

          const float dx=
              b->maxX-b->minX;

          const float dy=
              b->maxY-b->minY;

          const float dz=
              b->maxZ-b->minZ;

          if(std::isfinite(dx)&&
             std::isfinite(dy)&&
             std::isfinite(dz)&&
             dx>kExtentEpsilon&&
             dy>kExtentEpsilon&&
             dz>kExtentEpsilon){

            const float hMin=
                std::min(
                    dx,
                    dz);

            const float hMax=
                std::max(
                    dx,
                    dz);

            info.keepHorizontal=
                dy<=
                hMin*
                    kThinYRatio;

            info.verticalPlane=
                dy>0.55f&&
                hMin<=0.34f&&
                hMax>=0.68f;

            info.rodLike=
                dy>0.55f&&
                hMax<=0.60f;
          }
        }
      }
    }
  }

  if(info.blockShape==kSkullShape)
    info.keepHorizontal=false;

  info.valid=true;

  return true;
}

ItemPhysicsRuntime::ItemRenderTraits
ItemPhysicsRuntime::classifyItem(
    std::uintptr_t actor) const noexcept {

  ItemRenderTraits t{};

  if(!actor)
    return t;

  std::int32_t rendererShape=-1;

  if(tryGetRenderBlockShape(
         actor,
         rendererShape)){

    t.hasRenderShape=true;
    t.renderShape=rendererShape;
  }

  const auto block=
      *reinterpret_cast<
          const void*const*>(
          actor+
          profile::kBlockPtrOffset);

  if(block){
    t.valid=true;
    t.modelClass=
        ModelClass::BlockItem;

    (void)buildBlockRenderInfo(
        block,
        t.block);

    if(t.block.blockShape<0&&
       t.hasRenderShape)

      t.block.blockShape=
          t.renderShape;
  }

  else if(t.hasRenderShape){
    t.valid=true;
    t.modelClass=
        ModelClass::BlockItem;

    t.block.valid=true;
    t.block.blockShape=
        t.renderShape;
  }

  else{
    const auto handle=
        *reinterpret_cast<
            const std::uintptr_t*>(
            actor+
            profile::kItemHandleOffset);

    if(!handle)
      return t;

    const auto item=
        *reinterpret_cast<
            const std::uintptr_t*>(
            handle);

    if(!item)
      return t;

    const auto id=
        item+
        profile::kItemIdentifierOffset;

    t.valid=true;

    if(libcxxStringEquals(
           id,
           kShieldId)){

      t.modelClass=
          ModelClass::SpecialItem;

      t.specialKind=
          SpecialKind::Shield;

      t.family=
          MotionFamily::Special;
    }

    else if(libcxxStringEquals(
                id,
                kBannerId)){

      t.modelClass=
          ModelClass::SpecialItem;

      t.specialKind=
          SpecialKind::Banner;

      t.family=
          MotionFamily::Special;
    }

    else{
      t.modelClass=
          ModelClass::FlatItem;

      t.family=
          MotionFamily::FlatItem;
    }

    return t;
  }

  const std::int32_t shape=
      t.hasRenderShape
          ?t.renderShape
          :t.block.blockShape;

  if(shape==kSkullShape){
    t.family=
        MotionFamily::Head;

    return t;
  }

  if(t.block.keepHorizontal||
     isThinGroundShape(
         shape)){

    t.family=
        MotionFamily::HorizontalThin;

    return t;
  }

  if(t.block.verticalPlane){
    t.family=
        MotionFamily::ShapedBlock;

    return t;
  }

  if(t.block.rodLike||
     isTorchGroundShape(
         shape)||
     isShapedGroundShape(
         shape)){

    t.family=
        MotionFamily::ShapedBlock;

    return t;
  }

  if(shape>=0&&
     mIsBlockShape3D&&
     !mIsBlockShape3D(
         shape)){

    t.family=
        MotionFamily::ShapedBlock;

    return t;
  }

  t.family=
      MotionFamily::FullBlock;

  return t;
}

bool ItemPhysicsRuntime::getActorYaw(
    void *actor,
    float &yaw) const noexcept {

  yaw=0;

  if(!actor)
    return false;

  const auto a=
      reinterpret_cast<
          std::uintptr_t>(
          actor);

  if(*reinterpret_cast<
         const std::uint8_t*>(
         a+
         profile::kItemRenderAdjustmentsEngagedOffset)){

    const float firstYaw=
        *reinterpret_cast<
            const float*>(
            a+
            profile::kItemFirstRenderedYawOffset);

    if(std::isfinite(
           firstYaw)){

      yaw=
          firstYaw*
          kDegToRad;

      return true;
    }
  }

  const auto p=
      *reinterpret_cast<
          const std::uintptr_t*>(
          a+
          profile::kActorRotationComponentPtrOffset);

  if(!p)
    return false;

  const Vec2Abi r=
      *reinterpret_cast<
          const Vec2Abi*>(
          p);

  if(!std::isfinite(r.x)||
     !std::isfinite(r.y))
    return false;

  yaw=
      r.y*
      kDegToRad;

  return true;
}

void *ItemPhysicsRuntime::findComponentStorage(
    void *actor,
    std::uint32_t hash) const noexcept {

  if(!actor)
    return nullptr;

  const auto a=
      reinterpret_cast<
          std::uintptr_t>(
          actor);

  const auto registry=
      *reinterpret_cast<
          const std::uintptr_t*>(
          a+
          profile::kActorRegistryOffset);

  if(!registry)
    return nullptr;

  const auto begin=
      *reinterpret_cast<
          const std::uintptr_t*>(
          registry+0x38);

  const auto end=
      *reinterpret_cast<
          const std::uintptr_t*>(
          registry+0x40);

  const auto nodes=
      *reinterpret_cast<
          const std::uintptr_t*>(
          registry+0x50);

  const auto sentinel=
      *reinterpret_cast<
          const std::uintptr_t*>(
          registry+0x58);

  if(!begin||
     !end||
     end<=begin||
     !nodes)
    return nullptr;

  const auto bytes=
      end-begin;

  if(bytes%
         sizeof(std::uintptr_t)||
     bytes/
         sizeof(std::uintptr_t)>
         (1u<<20))
    return nullptr;

  const auto count=
      bytes/
      sizeof(std::uintptr_t);

  if(!count)
    return nullptr;

  std::int64_t index=
      *reinterpret_cast<
          const std::int64_t*>(
          begin+
          ((count-1)&hash)*
              sizeof(std::uintptr_t));

  for(int guard=0;
      index!=-1&&
      guard<4096;
      ++guard){

    const auto node=
        nodes+
        static_cast<
            std::uintptr_t>(
            index)*
            32u;

    if(node==sentinel)
      return nullptr;

    if(*reinterpret_cast<
           const std::uint32_t*>(
           node+8)==hash){

      const auto storage=
          *reinterpret_cast<
              const std::uintptr_t*>(
              node+0x10);

      return reinterpret_cast<void*>(
          storage);
    }

    index=
        *reinterpret_cast<
            const std::int64_t*>(
            node);
  }

  return nullptr;
}

bool ItemPhysicsRuntime::findPackedEntity(
    void *storage,
    std::uint32_t entity,
    std::uint32_t &packed) const noexcept {

  packed=0;

  if(!storage)
    return false;

  const auto s=
      reinterpret_cast<
          std::uintptr_t>(
          storage);

  const auto begin=
      *reinterpret_cast<
          const std::uintptr_t*>(
          s+0x08);

  const auto end=
      *reinterpret_cast<
          const std::uintptr_t*>(
          s+0x10);

  if(!begin||
     !end||
     end<begin)
    return false;

  const auto count=
      (end-begin)/
      sizeof(std::uintptr_t);

  const auto pi=
      (entity>>11)&
      0x7Fu;

  if(pi>=count)
    return false;

  const auto page=
      *reinterpret_cast<
          const std::uintptr_t*>(
          begin+
          pi*
              sizeof(std::uintptr_t));

  if(!page)
    return false;

  packed=
      *reinterpret_cast<
          const std::uint32_t*>(
          page+
          (entity&0x7FFu)*
              sizeof(std::uint32_t));

  return
      (packed^
       (entity&
        0xFFFC0000u))<=
      0x3FFFEu;
}

bool ItemPhysicsRuntime::hasOnGroundComponent(
    void *actor) const noexcept {

  if(!actor)
    return false;

  void *storage=
      findComponentStorage(
          actor,
          profile::kOnGroundFlagComponentHash);

  if(!storage)
    return false;

  const auto a=
      reinterpret_cast<
          std::uintptr_t>(
          actor);

  const auto entity=
      *reinterpret_cast<
          const std::uint32_t*>(
          a+
          profile::kActorEntityIdOffset);

  std::uint32_t packed{};

  return findPackedEntity(
      storage,
      entity,
      packed);
}

void ItemPhysicsRuntime::updateItemShadowComponent(
    void *actor,
    bool grounded,
    bool hide) const noexcept {

  if(!actor||
     !mGetRelativeShadowStorage||
     !mEmplaceRelativeShadow)
    return;

  const auto a=
      reinterpret_cast<
          std::uintptr_t>(
          actor);

  auto *registry=
      *reinterpret_cast<
          void**>(
          a+
          profile::kActorRegistryOffset);

  const auto entity=
      *reinterpret_cast<
          const std::uint32_t*>(
          a+
          profile::kActorEntityIdOffset);

  if(!registry)
    return;

  void *storage=
      findComponentStorage(
          actor,
          profile::kRelativeShadowOffsetComponentHash);

  if(!storage)
    storage=
        mGetRelativeShadowStorage(
            registry,
            profile::kRelativeShadowOffsetComponentHash);

  if(!storage)
    return;

  const float wanted=
      hide
          ?std::numeric_limits<float>::max()
          :(grounded
                ?0.0f
                :-0.5f);

  std::uint32_t packed{};

  if(!findPackedEntity(
         storage,
         entity,
         packed)){

    const std::uint32_t copy=
        entity;

    (void)mEmplaceRelativeShadow(
        storage,
        &copy,
        false,
        &wanted);

    return;
  }

  const auto dense=
      packed&
      0x3FFFFu;

  const auto s=
      reinterpret_cast<
          std::uintptr_t>(
          storage);

  const auto pages=
      *reinterpret_cast<
          const std::uintptr_t*>(
          s+0x50);

  if(!pages)
    return;

  const auto page=
      *reinterpret_cast<
          const std::uintptr_t*>(
          pages+
          (dense>>7)*
              sizeof(std::uintptr_t));

  if(!page)
    return;

  *reinterpret_cast<float*>(
      page+
      static_cast<std::uintptr_t>(
          dense&0x7Fu)*
          sizeof(float))=
      wanted;
}

ItemPhysicsRuntime::PhysicsState&
ItemPhysicsRuntime::stateFor(
    std::uint32_t id,
    std::chrono::steady_clock::time_point now,
    bool hasYaw,
    float yaw){

  auto [it,inserted]=
      mStates.try_emplace(
          id);

  auto &s=
      it->second;

  if(inserted||
     !s.initialized){

    s={};

    s.initialized=true;
    s.wasGrounded=true;

    s.yaw=
        hasYaw
            ?wrapPi(yaw)
            :(seededUnit(
                  id^
                  0x27D4EB2Du)*
                  2-
              1)*
              kPi;

    s.restYaw=
        s.yaw;

    s.headDirection=
        seededUnit(
            id^
            0x9E3779B9u)<
                0.5f
            ?-1.0f
            :1.0f;

    s.born=
        s.lastUpdate=
        s.lastSeen=
        now;
  }

  return s;
}

void ItemPhysicsRuntime::updateState(
    PhysicsState &s,
    const ItemRenderTraits &t,
    bool grounded,
    const Vec3Abi &motion,
    bool hasMotion,
    std::chrono::steady_clock::time_point now) const {

  const float dt=
      std::clamp(
          std::chrono::duration<float>(
              now-
              s.lastUpdate)
              .count(),
          0.0f,
          0.05f);

  s.lastUpdate=now;
  s.lastSeen=now;

  const bool enteredAir=
      !grounded&&
      s.wasGrounded;

  if(enteredAir){
    s.born=now;
    s.launchImpulseApplied=false;
    s.airTargetCaptured=false;
    s.restOrientation={};
  }

  const float control=
      std::clamp(
          mRotationSpeed.load(
              std::memory_order_relaxed),
          0.0f,
          3.0f);

  if(t.family==MotionFamily::FullBlock||
     t.family==MotionFamily::ShapedBlock){

    const auto safeShapedTarget=
        [&](float roll){

          const float base=
              std::round(
                  roll/
                  kPi)*
              kPi;

          const float error=
              wrapPi(
                  roll-
                  base);

          if(std::abs(error)<=
             kShapedMaxGroundTilt)
            return roll;

          return wrapPi(
              base+
              std::copysign(
                  kShapedMaxGroundTilt,
                  error));
        };

    if(!grounded){
      s.javaRoll=
          wrapPi(
              s.javaRoll+
              kJavaAirRollRate*
                  control*
                  dt);

      if(t.family==
         MotionFamily::ShapedBlock){

        const float descent=
            hasMotion
                ?std::clamp(
                     (kPlanarPreAlignStartY-
                      motion.y)/
                         (kPlanarPreAlignStartY-
                          kPlanarPreAlignFullY),
                     0.0f,
                     1.0f)
                :0.0f;

        if(descent>0){
          const float target=
              safeShapedTarget(
                  s.javaRoll);

          s.javaRoll=
              moveAngle(
                  s.javaRoll,
                  target,
                  (kJavaAirRollRate*
                       control+
                   kPlanarPreAlignRate)*
                      descent*
                      dt);
        }
      }
    }

    else if(t.family==
            MotionFamily::ShapedBlock){

      const float target=
          safeShapedTarget(
              s.javaRoll);

      s.javaRoll=
          moveAngle(
              s.javaRoll,
              target,
              kPlanarGroundRate*
                  dt);
    }

    else if(mOldRotation.load(
                std::memory_order_relaxed)){

      const float target=
          std::round(
              s.javaRoll/
              kHalfPi)*
          kHalfPi;

      s.javaRoll=
          moveAngle(
              s.javaRoll,
              target,
              kJavaOldGroundRate*
                  control*
                  dt);
    }

    s.wasGrounded=
        grounded;

    return;
  }

  if(t.family==
     MotionFamily::Head){

    const float age=
        std::max(
            0.0f,
            std::chrono::duration<float>(
                now-
                s.born)
                .count());

    const float target=
        grounded
            ?0.0f
            :s.headDirection*
                 kHeadAirTilt*
                 std::clamp(
                     age/
                         0.22f,
                     0.0f,
                     1.0f);

    const float rate=
        grounded
            ?kHeadGroundResponse
            :kHeadAirResponse;

    const float alpha=
        dt>0
            ?1.0f-
                 std::exp(
                     -rate*
                     dt)
            :0.0f;

    s.headTilt+=
        (target-
         s.headTilt)*
        alpha;

    s.wasGrounded=
        grounded;

    return;
  }

  auto getQ=
      [&](){

        return normalizeQ({
            s.orientation.w,
            s.orientation.x,
            s.orientation.y,
            s.orientation.z});
      };

  auto putQ=
      [&](QuatLocal q){

        q=
            normalizeQ(
                q);

        s.orientation={
            q.w,
            q.x,
            q.y,
            q.z};
      };

  const bool thin=
      t.family==
      MotionFamily::HorizontalThin;

  const auto supportTarget=
      [&](QuatLocal q){

        q=
            normalizeQ(
                q);

        const Vec3Local axis=
            rotateQ(
                q,
                thin
                    ?Vec3Local{0,1,0}
                    :Vec3Local{0,0,1});

        const Vec3Local target=
            axis.y>=0
                ?Vec3Local{0,1,0}
                :Vec3Local{0,-1,0};

        return normalizeQ(
            mulQ(
                fromToQ(
                    axis,
                    target),
                q));
      };

  const float maxAngular=
      thin
          ?kMaxThinAngular
          :(t.family==
                MotionFamily::Special
                ?kMaxSpecialAngular
                :kMaxFlatAngular);

  const float airDamping=
      thin
          ?kAirDampingThin
          :(t.family==
                MotionFamily::Special
                ?kAirDampingSpecial
                :kAirDampingFlat);

  const float preSpring=
      thin
          ?kPreSpringThin
          :(t.family==
                MotionFamily::Special
                ?kPreSpringSpecial
                :kPreSpringFlat);

  const float preDamping=
      thin
          ?kPreDampingThin
          :(t.family==
                MotionFamily::Special
                ?kPreDampingSpecial
                :kPreDampingFlat);

  const float contactSpring=
      thin
          ?kContactSpringThin
          :(t.family==
                MotionFamily::Special
                ?kContactSpringSpecial
                :kContactSpringFlat);

  const float contactDamping=
      thin
          ?kContactDampingThin
          :(t.family==
                MotionFamily::Special
                ?kContactDampingSpecial
                :kContactDampingFlat);

  if(!grounded&&
     !s.launchImpulseApplied){

    const float hs=
        hasMotion
            ?std::sqrt(
                 motion.x*
                     motion.x+
                 motion.z*
                     motion.z)
            :0.0f;

    const float total=
        hasMotion
            ?std::sqrt(
                 hs*
                     hs+
                 motion.y*
                     motion.y)
            :0.0f;

    if(total>=
           kSensitiveWake||
       enteredAir){

      Vec3Local axis;

      if(hs>1e-4f)
        axis={
            -motion.z/
                hs,
            0,
            motion.x/
                hs};
      else
        axis={
            -std::sin(
                s.yaw),
            0,
            std::cos(
                s.yaw)};

      axis=
          normalize3(
              axis);

      const float horizontal=
          hs*
          kMinecraftTicksPerSecond;

      const float vertical=
          hasMotion
              ?std::abs(
                   motion.y)*
                   kMinecraftTicksPerSecond
              :0.0f;

      float spin=
          (kBaseNaturalSpin+
           horizontal*
               kHorizontalSpinGain+
           vertical*
               kVerticalSpinGain)*
          control;

      spin=
          std::clamp(
              spin,
              0.25f,
              maxAngular);

      s.angularX=
          axis.x*
          spin;

      s.angularY=0;

      s.angularZ=
          axis.z*
          spin;

      s.launchImpulseApplied=
          true;
    }
  }

  const auto integrate=
      [&](QuatLocal target,
          float spring,
          float damping,
          float strength,
          float freeDamping){

        QuatLocal q=
            getQ();

        Vec3Local omega{
            s.angularX,
            s.angularY,
            s.angularZ};

        if(freeDamping>0){
          const float d=
              std::exp(
                  -freeDamping*
                  dt);

          omega.x*=d;
          omega.y*=d;
          omega.z*=d;
        }

        if(strength>0){
          const Vec3Local err=
              shortestAngularError(
                  q,
                  target);

          const float k=
              spring*
              strength;

          const float c=
              damping*
              std::sqrt(
                  std::max(
                      strength,
                      0.05f));

          omega.x+=
              (err.x*k-
               omega.x*c)*
              dt;

          omega.y+=
              (err.y*k-
               omega.y*c)*
              dt;

          omega.z+=
              (err.z*k-
               omega.z*c)*
              dt;
        }

        const float speed=
            length3(
                omega);

        if(speed>
               maxAngular&&
           speed>
               1e-5f){

          const float f=
              maxAngular/
              speed;

          omega.x*=f;
          omega.y*=f;
          omega.z*=f;
        }

        q=
            integrateWorldAngular(
                q,
                omega,
                dt);

        if(strength>0){
          const float err=
              length3(
                  shortestAngularError(
                      q,
                      target));

          if(err<
                 kStopError&&
             length3(
                 omega)<
                 kStopAngular){

            q=target;
            omega={};
          }
        }

        putQ(q);

        s.angularX=
            omega.x;

        s.angularY=
            omega.y;

        s.angularZ=
            omega.z;
      };

  if(!grounded){
    const float age=
        std::max(
            0.0f,
            std::chrono::duration<float>(
                now-
                s.born)
                .count());

    if(age>=
           kPreSettleDelay&&
       !s.airTargetCaptured){

      const auto target=
          supportTarget(
              getQ());

      s.restOrientation={
          target.w,
          target.x,
          target.y,
          target.z};

      s.airTargetCaptured=
          true;
    }

    QuatLocal target=
        getQ();

    float strength=0;

    if(s.airTargetCaptured){
      target=
          normalizeQ({
              s.restOrientation.w,
              s.restOrientation.x,
              s.restOrientation.y,
              s.restOrientation.z});

      float a=
          std::clamp(
              (age-
               kPreSettleDelay)/
                  kPreSettleRamp,
              0.0f,
              1.0f);

      a=
          a*
          a*
          (3-
           2*
               a);

      const float descent=
          hasMotion
              ?std::clamp(
                   (0.055f-
                    motion.y)/
                       0.18f,
                   0.0f,
                   1.0f)
              :0.5f;

      strength=
          a*
          (0.58f+
           0.42f*
               descent);
    }

    integrate(
        target,
        preSpring,
        preDamping,
        strength,
        airDamping);
  }

  else{
    if(!s.airTargetCaptured){
      const auto target=
          supportTarget(
              getQ());

      s.restOrientation={
          target.w,
          target.x,
          target.y,
          target.z};

      s.airTargetCaptured=
          true;
    }

    const QuatLocal target=
        normalizeQ({
            s.restOrientation.w,
            s.restOrientation.x,
            s.restOrientation.y,
            s.restOrientation.z});

    integrate(
        target,
        contactSpring,
        contactDamping,
        1.0f,
        0.0f);
  }

  s.wasGrounded=
      grounded;
}

void ItemPhysicsRuntime::pruneStates(
    std::chrono::steady_clock::time_point now){

  for(auto it=
          mStates.begin();
      it!=
          mStates.end();){

    if(now-
           it->second.lastSeen>
       kStateTtl)

      it=
          mStates.erase(
              it);

    else
      ++it;
  }
}

void ItemPhysicsRuntime::onRender(
    void *self,
    void *ctx,
    void *renderData){

  const auto original=
      mOriginal;

  if(!original)
    return;

  if(!ctx||
     !renderData||
     !mProfileSupported.load(
         std::memory_order_relaxed)){

    original(
        self,
        ctx,
        renderData);

    return;
  }

  const auto rd=
      reinterpret_cast<
          std::uintptr_t>(
          renderData);

  auto *actor=
      *reinterpret_cast<
          void**>(
          rd+
          profile::kRenderDataActorOffset);

  if(!actor){
    original(
        self,
        ctx,
        renderData);

    return;
  }

  const auto a=
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

  if(!enabled){
    original(
        self,
        ctx,
        renderData);

    return;
  }

  const auto traits=
      classifyItem(
          a);

  if(!traits.valid){
    original(
        self,
        ctx,
        renderData);

    return;
  }

  Vec3Abi motion{};
  bool hasMotion=false;

  if(mGetPosDelta){
    if(const auto *p=
           mGetPosDelta(
               actor);

       p&&
       std::isfinite(p->x)&&
       std::isfinite(p->y)&&
       std::isfinite(p->z)){

      motion=*p;
      hasMotion=true;
    }
  }

  float nativeYaw=0;

  const bool hasYaw=
      getActorYaw(
          actor,
          nativeYaw);

  const auto entity=
      *reinterpret_cast<
          const std::uint32_t*>(
          a+
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
            entity,
            now,
            hasYaw,
            nativeYaw);

    updateState(
        state,
        traits,
        grounded,
        motion,
        hasMotion,
        now);

    snapshot=state;

    if((++mRenderCounter&
        0xFFu)==0)

      pruneStates(
          now);
  }

  auto &count=
      *reinterpret_cast<
          std::uint8_t*>(
          a+
          profile::kItemCountOffset);

  auto &frame=
      *reinterpret_cast<
          std::uint8_t*>(
          a+
          profile::kIsInItemFrameOffset);

  const auto oldCount=
      count;

  const auto oldFrame=
      frame;

  if(mSingleModel.load(
         std::memory_order_relaxed))

    count=1;

  frame=1;

  auto *position=
      reinterpret_cast<
          float*>(
          rd+
          profile::kRenderDataPositionOffset);

  const float oldY=
      position[1];

  if(traits.family==
     MotionFamily::FlatItem)

    position[1]+=
        kFlatHeight;

  if(grounded){
    switch(traits.family){
    case MotionFamily::HorizontalThin:
      position[1]+=
          kThinHeight;
      break;

    case MotionFamily::ShapedBlock:
      position[1]+=
          kShapedHeight;
      break;

    case MotionFamily::Head:
      position[1]+=
          kHeadHeight;
      break;

    case MotionFamily::Special:
      position[1]+=
          kSpecialHeight;
      break;

    default:
      break;
    }
  }

  MatrixPushScope scope(
      mGetWorldMatrix
          ?mGetWorldMatrix(
               ctx)
          :nullptr,
      mMatrixPush,
      mMatrixRefDtor);

  Mat4 *matrix=
      scope.matrix();

  if(!matrix){
    position[1]=oldY;
    count=oldCount;
    frame=oldFrame;

    original(
        self,
        ctx,
        renderData);

    return;
  }

  const float x=
      position[0];

  const float y=
      position[1];

  const float z=
      position[2];

  postTranslate(
      *matrix,
      x,
      y,
      z);

  if(traits.family==
         MotionFamily::FullBlock||
     traits.family==
         MotionFamily::ShapedBlock){

    postRotateX(
        *matrix,
        kJavaBaseTilt);

    postRotateZ(
        *matrix,
        snapshot.yaw);

    postTranslate(
        *matrix,
        0,
        kJavaBlockOffsetY,
        kJavaBlockOffsetZ);

    postTranslate(
        *matrix,
        0,
        kJavaBlockPivotY,
        0);

    postRotateY(
        *matrix,
        snapshot.javaRoll);

    postTranslate(
        *matrix,
        0,
        -kJavaBlockPivotY,
        0);
  }

  else if(traits.family==
          MotionFamily::Head){

    postRotateY(
        *matrix,
        snapshot.yaw);

    postRotateX(
        *matrix,
        snapshot.headTilt);

    postRotateZ(
        *matrix,
        snapshot.headTilt*
            0.22f);
  }

  else{
    postRotateQuat(
        *matrix,
        {
            snapshot.orientation.w,
            snapshot.orientation.x,
            snapshot.orientation.y,
            snapshot.orientation.z
        });

    if(traits.family==
       MotionFamily::FlatItem)

      postTranslate(
          *matrix,
          0,
          kAtlasFlatLocalY,
          0);
  }

  postTranslate(
      *matrix,
      -x,
      -y,
      -z);

  const bool prevA=
      gDroppedItemGroupActive;

  const bool prevG=
      gDroppedItemGroupGrounded;

  gDroppedItemGroupActive=
      !mSingleModel.load(
          std::memory_order_relaxed);

  gDroppedItemGroupGrounded=
      grounded;

  original(
      self,
      ctx,
      renderData);

  gDroppedItemGroupActive=
      prevA;

  gDroppedItemGroupGrounded=
      prevG;

  position[1]=oldY;
  count=oldCount;
  frame=oldFrame;
}

}
