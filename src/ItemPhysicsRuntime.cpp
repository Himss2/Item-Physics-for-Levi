#include "ItemPhysicsRuntime.hpp"
#include "TargetProfile.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <string_view>

namespace itemphysics {
namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kDeg = kPi / 180.0f;

constexpr float kBaseTilt = 86.40f * kDeg;

constexpr float kJavaAirRollRate = 10.0f;
constexpr float kFlatSettleRate = 6.0f;

constexpr float kMotionWake = 0.001f;
constexpr float kGroundFlickerSpeed = 0.080f;
constexpr float kGroundFlickerVertical = 0.050f;

constexpr float kAtlasFlatLocalY = 0.25f;

constexpr auto kStateTtl =
    std::chrono::seconds(8);

constexpr float kFlatHeight = -0.09f;
constexpr float kBlockHeight = 0.00f;
constexpr float kThinHeight = -0.11f;
constexpr float kTorchHeight = -0.11f;
constexpr float kShapedHeight = -0.10f;
constexpr float kSkullHeight = -0.25f;
constexpr float kShieldHeight = -0.10f;
constexpr float kBannerHeight = -0.10f;

constexpr std::int32_t kSkullShape = 83;

constexpr std::string_view kShieldId =
    "minecraft:shield";

constexpr std::string_view kBannerId =
    "minecraft:banner";

struct Vec3MotionAbi {
  float x{};
  float y{};
  float z{};
};

static_assert(
    sizeof(Vec3MotionAbi) == 12);

using GetPosDeltaFn =
    const Vec3MotionAbi *(*)(const void *);

struct Vec3Local {
  float x{};
  float y{};
  float z{};
};

thread_local bool
    gDroppedItemGroupActive = false;

thread_local bool
    gDroppedItemGroupGrounded = false;

bool isThinGroundShape(
    std::int32_t s) noexcept {

  switch (s) {
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

bool isTorchGroundShape(
    std::int32_t s) noexcept {

  switch (s) {
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

bool isShapedGroundShape(
    std::int32_t s) noexcept {

  switch (s) {
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

bool invert3x3FromMat4(
    const Mat4 &m,
    float inv[9]) noexcept {

  const float a00 = m.m[0];
  const float a01 = m.m[4];
  const float a02 = m.m[8];

  const float a10 = m.m[1];
  const float a11 = m.m[5];
  const float a12 = m.m[9];

  const float a20 = m.m[2];
  const float a21 = m.m[6];
  const float a22 = m.m[10];

  const float c00 =
      a11 * a22 -
      a12 * a21;

  const float c01 =
      a02 * a21 -
      a01 * a22;

  const float c02 =
      a01 * a12 -
      a02 * a11;

  const float c10 =
      a12 * a20 -
      a10 * a22;

  const float c11 =
      a00 * a22 -
      a02 * a20;

  const float c12 =
      a02 * a10 -
      a00 * a12;

  const float c20 =
      a10 * a21 -
      a11 * a20;

  const float c21 =
      a01 * a20 -
      a00 * a21;

  const float c22 =
      a00 * a11 -
      a01 * a10;

  const float det =
      a00 * c00 +
      a01 * c10 +
      a02 * c20;

  if (!std::isfinite(det) ||
      std::abs(det) < 1.0e-8f) {

    return false;
  }

  const float d =
      1.0f /
      det;

  inv[0] = c00 * d;
  inv[1] = c01 * d;
  inv[2] = c02 * d;

  inv[3] = c10 * d;
  inv[4] = c11 * d;
  inv[5] = c12 * d;

  inv[6] = c20 * d;
  inv[7] = c21 * d;
  inv[8] = c22 * d;

  return true;
}

Vec3Local transformVector3(
    const Mat4 &m,
    Vec3Local v) noexcept {

  return {
      m.m[0] * v.x +
          m.m[4] * v.y +
          m.m[8] * v.z,

      m.m[1] * v.x +
          m.m[5] * v.y +
          m.m[9] * v.z,

      m.m[2] * v.x +
          m.m[6] * v.y +
          m.m[10] * v.z};
}

Vec3Local transformByInverse3(
    const float inv[9],
    Vec3Local v) noexcept {

  return {
      inv[0] * v.x +
          inv[1] * v.y +
          inv[2] * v.z,

      inv[3] * v.x +
          inv[4] * v.y +
          inv[5] * v.z,

      inv[6] * v.x +
          inv[7] * v.y +
          inv[8] * v.z};
}

class MatrixPushScope {
public:
  MatrixPushScope(
      void *stack,
      ItemPhysicsRuntime::MatrixPushFn push,
      ItemPhysicsRuntime::MatrixRefDtorFn dtor)
      : mDtor(dtor) {

    if (stack &&
        push &&
        dtor) {

      mRef =
          push(
              stack,
              false);

      mActive =
          mRef.stack &&
          mRef.mat;
    }
  }

  MatrixPushScope(
      const MatrixPushScope &) = delete;

  MatrixPushScope &
  operator=(
      const MatrixPushScope &) = delete;

  ~MatrixPushScope() {

    if (mActive &&
        mDtor) {

      mDtor(
          &mRef);

      mRef.stack =
          nullptr;

      mRef.mat =
          nullptr;
    }
  }

  Mat4 *matrix() noexcept {

    return mActive
               ? mRef.mat
               : nullptr;
  }

private:
  ItemPhysicsRuntime::MatrixStackRefAbi
      mRef{};

  ItemPhysicsRuntime::MatrixRefDtorFn
      mDtor{};

  bool
      mActive{};
};

}

ItemPhysicsRuntime *
ItemPhysicsRuntime::sInstance =
    nullptr;

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

  mRotationSpeed.store(
      static_cast<float>(
          c.rotationSpeed),
      std::memory_order_relaxed);

  mSettleSpeed.store(
      static_cast<float>(
          c.settleSpeed),
      std::memory_order_relaxed);
}

bool ItemPhysicsRuntime::verifyProfile(
    const ResolvedVirtual &r,
    ll::mod::NativeMod &mod) const {

  const auto verify =
      [&](
          std::uintptr_t address,
          const auto &fp,
          const char *name) {

        const std::size_t bytes =
            fp.size() *
            sizeof(std::uint32_t);

        if (!r.module.readable(
                address,
                bytes)) {

          mod.getLogger().warn(
              "{} is not readable",
              name);

          return false;
        }

        const auto *w =
            reinterpret_cast<
                const std::uint32_t *>(
                address);

        for (std::size_t i = 0;
             i < fp.size();
             ++i) {

          if (w[i] != fp[i]) {

            mod.getLogger().warn(
                "Minecraft profile mismatch: {} word {}",
                name,
                i);

            return false;
          }
        }

        return true;
      };

  if (!verify(
          r.target,
          profile::kRenderFingerprint,
          "ItemRenderer::render") ||

      !verify(
          r.module.base +
              profile::kGetPosDeltaRva,
          profile::kGetPosDeltaFingerprint,
          "Actor::getPosDelta") ||

      !verify(
          r.module.base +
              profile::kRenderItemGroupLikeRva,
          profile::kRenderItemGroupLikeFingerprint,
          "ItemRenderer render-group helper") ||

      !verify(
          r.module.base +
              profile::kGetBlockTypeForRenderingRva,
          profile::kGetBlockTypeForRenderingFingerprint,
          "ItemStackBase::getBlockTypeForRendering") ||

      !verify(
          r.module.base +
              profile::kBlockGraphicsGetForBlockTypeRva,
          profile::kBlockGraphicsGetForBlockTypeFingerprint,
          "BlockGraphics::getForBlock(BlockType)") ||

      !verify(
          r.module.base +
              profile::kBlockGraphicsGetBlockShapeRva,
          profile::kBlockGraphicsGetBlockShapeFingerprint,
          "BlockGraphics::getBlockShape") ||

      !verify(
          r.module.base +
              profile::kRelativeShadowStorageRva,
          profile::kRelativeShadowStorageFingerprint,
          "RelativeShadowOffsetComponent storage") ||

      !verify(
          r.module.base +
              profile::kRelativeShadowEmplaceRva,
          profile::kRelativeShadowEmplaceFingerprint,
          "RelativeShadowOffsetComponent emplace")) {

    return false;
  }

  const auto ex =
      [&](std::uintptr_t rva) {

        return r.module.executable(
            r.module.base +
            rva);
      };

  if (!ex(
          profile::kGetWorldMatrixRva) ||

      !ex(
          profile::kMatrixStackPushRva) ||

      !ex(
          profile::kMatrixStackRefDtorRva) ||

      !ex(
          profile::kGetPosDeltaRva) ||

      !ex(
          profile::kRenderItemGroupLikeRva) ||

      !ex(
          profile::kGetBlockTypeForRenderingRva) ||

      !ex(
          profile::kBlockGraphicsGetForBlockTypeRva) ||

      !ex(
          profile::kBlockGraphicsGetForBlockRva) ||

      !ex(
          profile::kBlockGraphicsGetBlockShapeRva) ||

      !ex(
          profile::kRelativeShadowStorageRva) ||

      !ex(
          profile::kRelativeShadowEmplaceRva)) {

    mod.getLogger().warn(
        "Minecraft 1.26.45 renderer helper validation failed");

    return false;
  }

  return true;
}

bool ItemPhysicsRuntime::install(
    ll::mod::NativeMod &mod) {

  uninstall();

  auto r =
      resolveVirtualByRtti(
          profile::kMinecraftModule,
          profile::kItemRendererRtti,
          profile::kItemRendererRenderVtableOffset);

  if (!r) {

    mod.getLogger().warn(
        "Item Physics inactive: failed to resolve ItemRenderer");

    return false;
  }

  if (!verifyProfile(
          *r,
          mod)) {

    mod.getLogger().warn(
        "Item Physics safe passthrough: Minecraft profile unsupported");

    return false;
  }

  mMinecraftBase =
      r->module.base;

  mRenderTarget =
      r->target;

  mRenderItemGroupTarget =
      mMinecraftBase +
      profile::
          kRenderItemGroupLikeRva;

  mGetWorldMatrix =
      reinterpret_cast<
          GetWorldMatrixFn>(
          mMinecraftBase +
          profile::
              kGetWorldMatrixRva);

  mMatrixPush =
      reinterpret_cast<
          MatrixPushFn>(
          mMinecraftBase +
          profile::
              kMatrixStackPushRva);

  mMatrixRefDtor =
      reinterpret_cast<
          MatrixRefDtorFn>(
          mMinecraftBase +
          profile::
              kMatrixStackRefDtorRva);

  mGetBlockTypeForRendering =
      reinterpret_cast<
          GetBlockTypeForRenderingFn>(
          mMinecraftBase +
          profile::
              kGetBlockTypeForRenderingRva);

  mGetBlockGraphicsForBlockType =
      reinterpret_cast<
          BlockGraphicsGetForBlockTypeFn>(
          mMinecraftBase +
          profile::
              kBlockGraphicsGetForBlockTypeRva);

  mGetBlockGraphicsForBlock =
      reinterpret_cast<
          BlockGraphicsGetForBlockFn>(
          mMinecraftBase +
          profile::
              kBlockGraphicsGetForBlockRva);

  mGetBlockGraphicsShape =
      reinterpret_cast<
          BlockGraphicsGetBlockShapeFn>(
          mMinecraftBase +
          profile::
              kBlockGraphicsGetBlockShapeRva);

  mGetRelativeShadowStorage =
      reinterpret_cast<
          RelativeShadowStorageFn>(
          mMinecraftBase +
          profile::
              kRelativeShadowStorageRva);

  mEmplaceRelativeShadow =
      reinterpret_cast<
          RelativeShadowEmplaceFn>(
          mMinecraftBase +
          profile::
              kRelativeShadowEmplaceRva);

  sInstance =
      this;

  mOriginal =
      nullptr;

  mHook =
      std::make_unique<
          pl::memory::HookHandle>(
          reinterpret_cast<void *>(
              mRenderTarget),

          reinterpret_cast<void *>(
              &ItemPhysicsRuntime::
                  renderDetour),

          reinterpret_cast<void **>(
              &mOriginal),

          pl::memory::
              HookPriority::Normal);

  if (!mHook->installed() ||
      !mOriginal) {

    mod.getLogger().error(
        "Failed to hook ItemRenderer::render");

    mHook.reset();

    sInstance =
        nullptr;

    return false;
  }

  mRenderItemGroupOriginal =
      nullptr;

  mRenderItemGroupHook =
      std::make_unique<
          pl::memory::HookHandle>(
          reinterpret_cast<void *>(
              mRenderItemGroupTarget),

          reinterpret_cast<void *>(
              &ItemPhysicsRuntime::
                  renderItemGroupDetour),

          reinterpret_cast<void **>(
              &mRenderItemGroupOriginal),

          pl::memory::
              HookPriority::Normal);

  if (!mRenderItemGroupHook->installed() ||
      !mRenderItemGroupOriginal) {

    mod.getLogger().error(
        "Failed to hook item render-group helper");

    mRenderItemGroupHook.reset();

    mHook->reset();
    mHook.reset();

    mOriginal =
        nullptr;

    sInstance =
        nullptr;

    return false;
  }

  mProfileSupported.store(
      true,
      std::memory_order_relaxed);

  mod.getLogger().info(
      "Item Physics active: Java ItemPhysic scalar-roll renderer");

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

  if (sInstance ==
      this) {

    sInstance =
        nullptr;
  }

  mOriginal =
      nullptr;

  mRenderItemGroupOriginal =
      nullptr;

  mGetWorldMatrix =
      nullptr;

  mMatrixPush =
      nullptr;

  mMatrixRefDtor =
      nullptr;

  mGetBlockTypeForRendering =
      nullptr;

  mGetBlockGraphicsForBlockType =
      nullptr;

  mGetBlockGraphicsForBlock =
      nullptr;

  mGetBlockGraphicsShape =
      nullptr;

  mGetRelativeShadowStorage =
      nullptr;

  mEmplaceRelativeShadow =
      nullptr;

  mRenderTarget =
      0;

  mRenderItemGroupTarget =
      0;

  mMinecraftBase =
      0;

  clearStates();
}

void ItemPhysicsRuntime::clearStates() {

  std::lock_guard lock(
      mStateMutex);

  mStates.clear();

  mRenderCounter =
      0;
}

void ItemPhysicsRuntime::renderDetour(
    void *a,
    void *b,
    void *c) {

  if (sInstance) {

    sInstance->onRender(
        a,
        b,
        c);
  }
}

void ItemPhysicsRuntime::renderItemGroupDetour(
    void *a,
    void *b,
    void *c,
    std::uint32_t d,
    std::uint32_t e,
    float f,
    float g) {

  if (sInstance) {

    sInstance->onRenderItemGroup(
        a,
        b,
        c,
        d,
        e,
        f,
        g);
  }
}

void ItemPhysicsRuntime::onRenderItemGroup(
    void *self,
    void *ctx,
    void *itemData,
    std::uint32_t count,
    std::uint32_t flags,
    float scale,
    float animation) {

  const auto original =
      mRenderItemGroupOriginal;

  if (!original) {

    return;
  }

  if (!gDroppedItemGroupActive ||
      !gDroppedItemGroupGrounded ||
      !self ||
      !ctx ||
      count <= 1 ||
      !mGetWorldMatrix ||
      !mMatrixPush ||
      !mMatrixRefDtor) {

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
      mGetWorldMatrix(
          ctx),
      mMatrixPush,
      mMatrixRefDtor);

  Mat4 *current =
      probe.matrix();

  if (!current) {

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

  const Mat4 snapshot =
      *current;

  float inverse[9]{};

  if (!invert3x3FromMat4(
          snapshot,
          inverse)) {

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

  constexpr std::size_t
      baseOffset = 0x17C;

  constexpr std::size_t
      stride = 0x0C;

  constexpr std::uint32_t
      maxExtra = 3;

  const std::uint32_t extra =
      std::min<std::uint32_t>(
          count - 1,
          maxExtra);

  std::lock_guard tableLock(
      mGroupOffsetMutex);

  Vec3Local saved[
      maxExtra]{};

  auto *base =
      reinterpret_cast<
          std::uint8_t *>(
          self);

  for (std::uint32_t i = 0;
       i < extra;
       ++i) {

    auto *off =
        reinterpret_cast<
            Vec3Local *>(
            base +
            baseOffset +
            static_cast<std::size_t>(
                i) *
                stride);

    saved[i] =
        *off;

    Vec3Local world =
        transformVector3(
            snapshot,
            saved[i]);

    world.y =
        0.0f;

    *off =
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

  for (std::uint32_t i = 0;
       i < extra;
       ++i) {

    auto *off =
        reinterpret_cast<
            Vec3Local *>(
            base +
            baseOffset +
            static_cast<std::size_t>(
                i) *
                stride);

    *off =
        saved[i];
  }
}

float ItemPhysicsRuntime::seededUnit(
    std::uint32_t s) noexcept {

  s ^=
      s << 13;

  s ^=
      s >> 17;

  s ^=
      s << 5;

  return static_cast<float>(
             s &
             0x00FFFFFFu) /
         16777215.0f;
}

float ItemPhysicsRuntime::wrapPi(
    float v) noexcept {

  return std::remainder(
      v,
      2.0f *
          kPi);
}

float ItemPhysicsRuntime::moveAngle(
    float current,
    float target,
    float maxStep) noexcept {

  const float d =
      wrapPi(
          target -
          current);

  if (std::abs(d) <=
      maxStep) {

    return wrapPi(
        target);
  }

  return wrapPi(
      current +
      std::copysign(
          maxStep,
          d));
}

bool ItemPhysicsRuntime::libcxxStringEquals(
    std::uintptr_t address,
    std::string_view wanted) noexcept {

  if (!address) {

    return false;
  }

  const auto *raw =
      reinterpret_cast<
          const std::uint8_t *>(
          address);

  const std::uint8_t flag =
      raw[0];

  std::size_t length =
      0;

  const char *data =
      nullptr;

  if ((flag &
       1u) ==
      0) {

    length =
        static_cast<
            std::size_t>(
            flag >>
            1);

    data =
        reinterpret_cast<
            const char *>(
            raw +
            1);

  } else {

    length =
        *reinterpret_cast<
            const std::size_t *>(
            raw +
            8);

    data =
        *reinterpret_cast<
            const char *const *>(
            raw +
            16);
  }

  return
      data &&
      length ==
          wanted.size() &&

      std::memcmp(
          data,
          wanted.data(),
          length) ==
          0;
}

bool ItemPhysicsRuntime::tryGetRenderBlockShape(
    std::uintptr_t actor,
    std::int32_t &shape) const noexcept {

  shape =
      -1;

  if (!actor ||
      !mGetBlockTypeForRendering ||
      !mGetBlockGraphicsForBlockType ||
      !mGetBlockGraphicsShape) {

    return false;
  }

  const void *stack =
      reinterpret_cast<
          const void *>(
          actor +
          profile::
              kItemStackBaseOffset);

  const void *weak =
      mGetBlockTypeForRendering(
          stack);

  if (!weak) {

    return false;
  }

  const auto counter =
      *reinterpret_cast<
          const std::uintptr_t *>(
          weak);

  if (!counter) {

    return false;
  }

  const auto type =
      *reinterpret_cast<
          const std::uintptr_t *>(
          counter);

  if (!type) {

    return false;
  }

  const void *graphics =
      mGetBlockGraphicsForBlockType(
          reinterpret_cast<
              const void *>(
              type));

  if (!graphics) {

    return false;
  }

  shape =
      mGetBlockGraphicsShape(
          graphics);

  return true;
}

bool ItemPhysicsRuntime::tryGetBlockShape(
    const void *block,
    std::int32_t &shape) const noexcept {

  shape =
      -1;

  if (!block ||
      !mGetBlockGraphicsForBlock ||
      !mGetBlockGraphicsShape) {

    return false;
  }

  const void *graphics =
      mGetBlockGraphicsForBlock(
          block);

  if (!graphics) {

    return false;
  }

  shape =
      mGetBlockGraphicsShape(
          graphics);

  return true;
}

ItemPhysicsRuntime::ItemRenderTraits
ItemPhysicsRuntime::classifyItem(
    std::uintptr_t actor) const noexcept {

  ItemRenderTraits t{};

  std::int32_t shape =
      -1;

  if (tryGetRenderBlockShape(
          actor,
          shape)) {

    t.valid =
        true;

    t.modelClass =
        ModelClass::BlockItem;

    t.hasBlockShape =
        true;

    t.blockShape =
        shape;

    return t;
  }

  const auto block =
      *reinterpret_cast<
          const void *const *>(
          actor +
          profile::
              kBlockPtrOffset);

  if (block) {

    t.valid =
        true;

    t.modelClass =
        ModelClass::BlockItem;

    t.hasBlockShape =
        tryGetBlockShape(
            block,
            t.blockShape);

    return t;
  }

  const auto handle =
      *reinterpret_cast<
          const std::uintptr_t *>(
          actor +
          profile::
              kItemHandleOffset);

  if (!handle) {

    return t;
  }

  const auto item =
      *reinterpret_cast<
          const std::uintptr_t *>(
          handle);

  if (!item) {

    return t;
  }

  const auto id =
      item +
      profile::
          kItemIdentifierOffset;

  t.valid =
      true;

  if (libcxxStringEquals(
          id,
          kShieldId)) {

    t.modelClass =
        ModelClass::SpecialItem;

    t.specialKind =
        SpecialKind::Shield;

  } else if (
      libcxxStringEquals(
          id,
          kBannerId)) {

    t.modelClass =
        ModelClass::SpecialItem;

    t.specialKind =
        SpecialKind::Banner;

  } else {

    t.modelClass =
        ModelClass::FlatItem;
  }

  return t;
}

ItemPhysicsRuntime::PhysicsState &
ItemPhysicsRuntime::stateFor(
    std::uint32_t id,
    std::chrono::steady_clock::time_point now) {

  auto [it, inserted] =
      mStates.try_emplace(
          id);

  auto &s =
      it->second;

  if (inserted ||
      !s.initialized) {

    s.initialized =
        true;

    s.wasGrounded =
        true;

    s.roll =
        0.0f;

    s.yaw =
        (seededUnit(
             id ^
             0x27D4EB2Du) *
             2.0f -
         1.0f) *
        kPi;

    s.lastUpdate =
        now;

    s.lastSeen =
        now;
  }

  return s;
}

void ItemPhysicsRuntime::updateState(
    PhysicsState &s,
    bool blockLike,
    bool grounded,
    bool hasMotion,
    float motionX,
    float motionZ,
    std::chrono::steady_clock::time_point now) const {

  const float dt =
      std::clamp(
          std::chrono::duration<float>(
              now -
              s.lastUpdate)
              .count(),
          0.0f,
          0.05f);

  s.lastUpdate =
      now;

  s.lastSeen =
      now;

  if (!grounded) {

    const float h2 =
        motionX *
            motionX +
        motionZ *
            motionZ;

    if (s.wasGrounded &&
        hasMotion &&
        h2 >
            kMotionWake *
                kMotionWake) {

      s.yaw =
          std::atan2(
              motionZ,
              motionX);
    }

    const float speed =
        std::clamp(
            mRotationSpeed.load(
                std::memory_order_relaxed),
            0.0f,
            3.0f);

    s.roll =
        wrapPi(
            s.roll +
            kJavaAirRollRate *
                speed *
                dt);

    s.wasGrounded =
        false;

    return;
  }

  if (!blockLike) {

    const float target =
        std::round(
            s.roll /
            kPi) *
        kPi;

    const float settle =
        std::max(
            0.0f,
            mSettleSpeed.load(
                std::memory_order_relaxed));

    s.roll =
        moveAngle(
            s.roll,
            target,
            kFlatSettleRate *
                settle *
                dt);
  }

  s.wasGrounded =
      true;
}

void ItemPhysicsRuntime::pruneStates(
    std::chrono::steady_clock::time_point now) {

  for (auto it =
           mStates.begin();

       it !=
       mStates.end();) {

    if (now -
            it->second.lastSeen >
        kStateTtl) {

      it =
          mStates.erase(
              it);

    } else {

      ++it;
    }
  }
}

void ItemPhysicsRuntime::updateItemShadowComponent(
    void *actor,
    bool grounded,
    bool hide) const noexcept {

  if (!actor ||
      !mGetRelativeShadowStorage ||
      !mEmplaceRelativeShadow) {

    return;
  }

  const auto a =
      reinterpret_cast<
          std::uintptr_t>(
          actor);

  auto *registry =
      *reinterpret_cast<
          void **>(
          a +
          profile::
              kActorRegistryOffset);

  const auto entity =
      *reinterpret_cast<
          const std::uint32_t *>(
          a +
          profile::
              kActorEntityIdOffset);

  if (!registry) {

    return;
  }

  void *storage =
      mGetRelativeShadowStorage(
          registry,
          profile::
              kRelativeShadowOffsetComponentHash);

  if (!storage) {

    return;
  }

  const float wanted =
      hide
          ? std::numeric_limits<float>::max()
          : (grounded
                 ? 0.0f
                 : -0.5f);

  const auto sa =
      reinterpret_cast<
          std::uintptr_t>(
          storage);

  const auto begin =
      *reinterpret_cast<
          const std::uintptr_t *>(
          sa +
          0x08);

  const auto end =
      *reinterpret_cast<
          const std::uintptr_t *>(
          sa +
          0x10);

  bool present =
      false;

  std::uint32_t packed =
      0;

  if (begin &&
      end >=
          begin) {

    const auto pageCount =
        (end -
         begin) /
        sizeof(
            std::uintptr_t);

    const auto pageIndex =
        (entity >>
         11) &
        0x7Fu;

    if (pageIndex <
        pageCount) {

      const auto page =
          *reinterpret_cast<
              const std::uintptr_t *>(
              begin +
              pageIndex *
                  sizeof(
                      std::uintptr_t));

      if (page) {

        packed =
            *reinterpret_cast<
                const std::uint32_t *>(
                page +
                (entity &
                 0x7FFu) *
                    sizeof(
                        std::uint32_t));

        present =
            (packed ^
             (entity &
              0xFFFC0000u)) <=
            0x3FFFEu;
      }
    }
  }

  if (!present) {

    const std::uint32_t copy =
        entity;

    (void)mEmplaceRelativeShadow(
        storage,
        &copy,
        false,
        &wanted);

    return;
  }

  const auto dense =
      packed &
      0x3FFFFu;

  const auto pages =
      *reinterpret_cast<
          const std::uintptr_t *>(
          sa +
          0x50);

  if (!pages) {

    return;
  }

  const auto page =
      *reinterpret_cast<
          const std::uintptr_t *>(
          pages +
          (dense >>
           7) *
              sizeof(
                  std::uintptr_t));

  if (!page) {

    return;
  }

  *reinterpret_cast<float *>(
      page +
      (dense &
       0x7Fu) *
          sizeof(float)) =
      wanted;
}

bool ItemPhysicsRuntime::hasOnGroundComponent(
    void *actor) const noexcept {

  if (!actor) {

    return false;
  }

  const auto a =
      reinterpret_cast<
          std::uintptr_t>(
          actor);

  const auto registry =
      *reinterpret_cast<
          const std::uintptr_t *>(
          a +
          profile::
              kActorRegistryOffset);

  const auto entity =
      *reinterpret_cast<
          const std::uint32_t *>(
          a +
          profile::
              kActorEntityIdOffset);

  if (!registry) {

    return false;
  }

  const auto begin =
      *reinterpret_cast<
          const std::uintptr_t *>(
          registry +
          0x38);

  const auto end =
      *reinterpret_cast<
          const std::uintptr_t *>(
          registry +
          0x40);

  const auto nodes =
      *reinterpret_cast<
          const std::uintptr_t *>(
          registry +
          0x50);

  const auto sentinel =
      *reinterpret_cast<
          const std::uintptr_t *>(
          registry +
          0x58);

  if (!begin ||
      !end ||
      end <=
          begin ||
      !nodes) {

    return false;
  }

  const auto bytes =
      end -
      begin;

  if (bytes %
              sizeof(
                  std::uintptr_t) !=
          0 ||

      bytes /
              sizeof(
                  std::uintptr_t) >
          (1u << 20)) {

    return false;
  }

  const auto count =
      bytes /
      sizeof(
          std::uintptr_t);

  if (!count) {

    return false;
  }

  const auto bucket =
      (count -
       1) &
      profile::
          kOnGroundFlagComponentHash;

  std::int64_t index =
      *reinterpret_cast<
          const std::int64_t *>(
          begin +
          bucket *
              sizeof(
                  std::uintptr_t));

  std::uintptr_t node =
      0;

  for (int guard = 0;
       index != -1 &&
       guard < 4096;
       ++guard) {

    node =
        nodes +
        static_cast<
            std::uintptr_t>(
            index) *
            32u;

    if (*reinterpret_cast<
            const std::uint32_t *>(
            node +
            8) ==
        profile::
            kOnGroundFlagComponentHash) {

      break;
    }

    index =
        *reinterpret_cast<
            const std::int64_t *>(
            node);

    node =
        0;
  }

  if (!node ||
      node ==
          sentinel) {

    return false;
  }

  const auto storage =
      *reinterpret_cast<
          const std::uintptr_t *>(
          node +
          0x10);

  if (!storage) {

    return false;
  }

  const auto pagesBegin =
      *reinterpret_cast<
          const std::uintptr_t *>(
          storage +
          0x8);

  const auto pagesEnd =
      *reinterpret_cast<
          const std::uintptr_t *>(
          storage +
          0x10);

  if (!pagesBegin ||
      !pagesEnd ||
      pagesEnd <
          pagesBegin) {

    return false;
  }

  const auto pageCount =
      (pagesEnd -
       pagesBegin) /
      sizeof(
          std::uintptr_t);

  const auto pageIndex =
      (entity >>
       11) &
      0x7Fu;

  if (pageIndex >=
      pageCount) {

    return false;
  }

  const auto page =
      *reinterpret_cast<
          const std::uintptr_t *>(
          pagesBegin +
          pageIndex *
              sizeof(
                  std::uintptr_t));

  if (!page) {

    return false;
  }

  const auto slotValue =
      *reinterpret_cast<
          const std::uint32_t *>(
          page +
          (entity &
           0x7FFu) *
              sizeof(
                  std::uint32_t));

  return
      (slotValue ^
       (entity &
        0xFFFC0000u)) <=
      0x3FFFEu;
}

void ItemPhysicsRuntime::onRender(
    void *self,
    void *ctx,
    void *renderData) {

  const auto original =
      mOriginal;

  if (!original) {

    return;
  }

  if (!ctx ||
      !renderData ||
      !mProfileSupported.load(
          std::memory_order_relaxed)) {

    original(
        self,
        ctx,
        renderData);

    return;
  }

  const auto rd =
      reinterpret_cast<
          std::uintptr_t>(
          renderData);

  auto *actor =
      *reinterpret_cast<
          void **>(
          rd +
          profile::
              kRenderDataActorOffset);

  if (!actor) {

    original(
        self,
        ctx,
        renderData);

    return;
  }

  const auto a =
      reinterpret_cast<
          std::uintptr_t>(
          actor);

  const bool grounded =
      hasOnGroundComponent(
          actor);

  const bool enabled =
      mEnabled.load(
          std::memory_order_relaxed);

  updateItemShadowComponent(
      actor,
      grounded,
      enabled &&
          mHideItemShadow.load(
              std::memory_order_relaxed));

  if (!enabled) {

    original(
        self,
        ctx,
        renderData);

    return;
  }

  const auto traits =
      classifyItem(
          a);

  if (!traits.valid) {

    original(
        self,
        ctx,
        renderData);

    return;
  }

  Vec3MotionAbi motion{};

  bool hasMotion =
      false;

  const auto getPosDelta =
      reinterpret_cast<
          GetPosDeltaFn>(
          mMinecraftBase +
          profile::
              kGetPosDeltaRva);

  if (getPosDelta) {

    if (const auto *p =
            getPosDelta(
                actor);

        p &&
        std::isfinite(
            p->x) &&
        std::isfinite(
            p->y) &&
        std::isfinite(
            p->z)) {

      motion =
          *p;

      hasMotion =
          true;
    }
  }

  const float totalSpeed =
      hasMotion
          ? std::sqrt(
                motion.x *
                    motion.x +
                motion.y *
                    motion.y +
                motion.z *
                    motion.z)
          : 0.0f;

  bool physicsGrounded =
      grounded;

  PhysicsState snapshot{};

  const auto entity =
      *reinterpret_cast<
          const std::uint32_t *>(
          a +
          profile::
              kActorEntityIdOffset);

  {
    std::lock_guard lock(
        mStateMutex);

    const auto now =
        std::chrono::
            steady_clock::now();

    auto &state =
        stateFor(
            entity,
            now);

    if (!physicsGrounded &&
        state.wasGrounded &&
        hasMotion &&
        totalSpeed <
            kGroundFlickerSpeed &&
        std::abs(
            motion.y) <
            kGroundFlickerVertical) {

      physicsGrounded =
          true;
    }

    updateState(
        state,
        traits.blockLike(),
        physicsGrounded,
        hasMotion,
        motion.x,
        motion.z,
        now);

    snapshot =
        state;

    if ((++mRenderCounter &
         0xFFu) ==
        0) {

      pruneStates(
          now);
    }
  }

  auto &count =
      *reinterpret_cast<
          std::uint8_t *>(
          a +
          profile::
              kItemCountOffset);

  auto &frame =
      *reinterpret_cast<
          std::uint8_t *>(
          a +
          profile::
              kIsInItemFrameOffset);

  const auto oldCount =
      count;

  const auto oldFrame =
      frame;

  if (mSingleModel.load(
          std::memory_order_relaxed)) {

    count =
        1;
  }

  frame =
      1;

  auto *position =
      reinterpret_cast<
          float *>(
          rd +
          profile::
              kRenderDataPositionOffset);

  const float oldY =
      position[1];

  const bool flat =
      traits.modelClass ==
      ModelClass::FlatItem;

  if (flat) {

    position[1] =
        oldY +
        kFlatHeight;
  }

  if (grounded) {

    if (traits.specialKind ==
        SpecialKind::Shield) {

      position[1] +=
          kShieldHeight;

    } else if (
        traits.specialKind ==
        SpecialKind::Banner) {

      position[1] +=
          kBannerHeight;

    } else if (
        traits.hasBlockShape) {

      const auto shape =
          traits.blockShape;

      if (shape ==
          kSkullShape) {

        position[1] +=
            kSkullHeight;

      } else if (
          isThinGroundShape(
              shape)) {

        position[1] +=
            kThinHeight;

      } else if (
          isTorchGroundShape(
              shape)) {

        position[1] +=
            kTorchHeight;

      } else if (
          isShapedGroundShape(
              shape)) {

        position[1] +=
            kShapedHeight;

      } else {

        position[1] +=
            kBlockHeight;
      }
    }
  }

  MatrixPushScope scope(
      mGetWorldMatrix
          ? mGetWorldMatrix(
                ctx)
          : nullptr,
      mMatrixPush,
      mMatrixRefDtor);

  Mat4 *matrix =
      scope.matrix();

  if (!matrix) {

    position[1] =
        oldY;

    count =
        oldCount;

    frame =
        oldFrame;

    original(
        self,
        ctx,
        renderData);

    return;
  }

  const float x =
      position[0];

  const float y =
      position[1];

  const float z =
      position[2];

  postTranslate(
      *matrix,
      x,
      y,
      z);

  postRotateX(
      *matrix,
      kBaseTilt);

  postRotateZ(
      *matrix,
      snapshot.yaw);

  postRotateY(
      *matrix,
      snapshot.roll);

  if (flat) {

    postTranslate(
        *matrix,
        0.0f,
        kAtlasFlatLocalY,
        0.0f);
  }

  postTranslate(
      *matrix,
      -x,
      -y,
      -z);

  const bool prevActive =
      gDroppedItemGroupActive;

  const bool prevGround =
      gDroppedItemGroupGrounded;

  gDroppedItemGroupActive =
      !mSingleModel.load(
          std::memory_order_relaxed);

  gDroppedItemGroupGrounded =
      grounded;

  original(
      self,
      ctx,
      renderData);

  gDroppedItemGroupActive =
      prevActive;

  gDroppedItemGroupGrounded =
      prevGround;

  position[1] =
      oldY;

  count =
      oldCount;

  frame =
      oldFrame;
}

}
