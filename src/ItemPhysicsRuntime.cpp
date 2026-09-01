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
constexpr float kDegToRad = kPi / 180.0f;
constexpr auto kStateTtl = std::chrono::seconds(8);
constexpr float kAtlasFlatLocalY = 0.25f;
constexpr float kMinecraftTicksPerSecond = 20.0f;
constexpr std::int32_t kSkullBlockShape = 83;
constexpr float kThinYRatio = 0.70f;
constexpr float kExtentEpsilon = 0.0005f;

constexpr std::string_view kShieldId = "minecraft:shield";
constexpr std::string_view kBannerId = "minecraft:banner";

constexpr float kFullBlockAirDamping = 3.20f;
constexpr float kFullBlockWakeThreshold = 0.015f;

constexpr float kNonFullWakeThreshold = 0.030f;

constexpr float kAirDampingFlat = 2.35f;
constexpr float kAirDampingThin = 2.65f;
constexpr float kAirDampingHead = 3.10f;
constexpr float kAirDampingShaped = 2.50f;
constexpr float kAirDampingSpecial = 2.45f;

constexpr float kMaxAngularFlat = 2.25f;
constexpr float kMaxAngularThin = 1.85f;
constexpr float kMaxAngularHead = 1.55f;
constexpr float kMaxAngularShaped = 1.95f;
constexpr float kMaxAngularSpecial = 2.00f;

constexpr float kBaseNaturalSpin = 0.55f;
constexpr float kHorizontalSpinGain = 0.38f;
constexpr float kVerticalSpinGain = 0.09f;
constexpr float kMaxNaturalSpin = 4.75f;

constexpr float kPreSettleDelay = 0.08f;
constexpr float kPreSettleRamp = 0.72f;

constexpr float kPreSettleSpringFlat = 11.0f;
constexpr float kPreSettleSpringThin = 12.0f;
constexpr float kPreSettleSpringHead = 12.5f;
constexpr float kPreSettleSpringShaped = 11.5f;
constexpr float kPreSettleSpringSpecial = 10.5f;

constexpr float kPreSettleDampingFlat = 4.1f;
constexpr float kPreSettleDampingThin = 4.5f;
constexpr float kPreSettleDampingHead = 4.8f;
constexpr float kPreSettleDampingShaped = 4.2f;
constexpr float kPreSettleDampingSpecial = 4.0f;

constexpr float kContactSpringFlat = 20.0f;
constexpr float kContactSpringThin = 22.0f;
constexpr float kContactSpringHead = 22.0f;
constexpr float kContactSpringShaped = 21.0f;
constexpr float kContactSpringSpecial = 19.0f;

constexpr float kContactDampingFlat = 7.8f;
constexpr float kContactDampingThin = 8.4f;
constexpr float kContactDampingHead = 8.6f;
constexpr float kContactDampingShaped = 8.0f;
constexpr float kContactDampingSpecial = 7.6f;

constexpr float kMaxContactAngularFlat = 2.10f;
constexpr float kMaxContactAngularThin = 1.80f;
constexpr float kMaxContactAngularHead = 1.55f;
constexpr float kMaxContactAngularShaped = 1.85f;
constexpr float kMaxContactAngularSpecial = 1.95f;

constexpr float kContactStopAngular = 0.020f;
constexpr float kContactStopError = 0.008f;

constexpr float kGroundFlickerSpeedThreshold = 0.080f;
constexpr float kGroundFlickerVerticalThreshold = 0.050f;

struct Vec3MotionAbi {
  float x{};
  float y{};
  float z{};
};

static_assert(sizeof(Vec3MotionAbi) == 12);

using GetPosDeltaFn =
    const Vec3MotionAbi *(*)(const void *actor);

struct Vec3Local {
  float x{};
  float y{};
  float z{};
};

struct QuatLocal {
  float w{1.0f};
  float x{};
  float y{};
  float z{};
};

thread_local bool gDroppedItemGroupActive = false;
thread_local bool gDroppedItemGroupGrounded = false;

[[nodiscard]]
float dot3(Vec3Local a, Vec3Local b) noexcept {
  return a.x * b.x + a.y * b.y + a.z * b.z;
}

[[nodiscard]]
Vec3Local cross3(Vec3Local a, Vec3Local b) noexcept {
  return {
      a.y * b.z - a.z * b.y,
      a.z * b.x - a.x * b.z,
      a.x * b.y - a.y * b.x};
}

[[nodiscard]]
float length3(Vec3Local v) noexcept {
  return std::sqrt(dot3(v, v));
}

[[nodiscard]]
Vec3Local normalize3(
    Vec3Local v,
    Vec3Local fallback = {1.0f, 0.0f, 0.0f}) noexcept {

  const float len = length3(v);

  if (!std::isfinite(len) || len < 1.0e-6f) {
    return fallback;
  }

  const float inv = 1.0f / len;

  return {
      v.x * inv,
      v.y * inv,
      v.z * inv};
}

[[nodiscard]]
QuatLocal normalizeQ(QuatLocal q) noexcept {
  const float len =
      std::sqrt(
          q.w * q.w +
          q.x * q.x +
          q.y * q.y +
          q.z * q.z);

  if (!std::isfinite(len) || len < 1.0e-7f) {
    return {};
  }

  const float inv = 1.0f / len;

  return {
      q.w * inv,
      q.x * inv,
      q.y * inv,
      q.z * inv};
}

[[nodiscard]]
QuatLocal mulQ(QuatLocal a, QuatLocal b) noexcept {
  return {
      a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z,
      a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
      a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
      a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w};
}

[[nodiscard]]
QuatLocal axisAngleQ(
    Vec3Local axis,
    float angle) noexcept {

  axis = normalize3(axis);

  const float half = 0.5f * angle;
  const float s = std::sin(half);

  return normalizeQ({
      std::cos(half),
      axis.x * s,
      axis.y * s,
      axis.z * s});
}

[[nodiscard]]
Vec3Local rotateQ(
    QuatLocal q,
    Vec3Local v) noexcept {

  q = normalizeQ(q);

  const Vec3Local u{
      q.x,
      q.y,
      q.z};

  const float s = q.w;

  const Vec3Local uv =
      cross3(u, v);

  const Vec3Local uuv =
      cross3(u, uv);

  return {
      v.x + 2.0f * (s * uv.x + uuv.x),
      v.y + 2.0f * (s * uv.y + uuv.y),
      v.z + 2.0f * (s * uv.z + uuv.z)};
}

[[nodiscard]]
QuatLocal fromToQ(
    Vec3Local from,
    Vec3Local to) noexcept {

  from = normalize3(from);
  to = normalize3(to);

  const float d =
      std::clamp(
          dot3(from, to),
          -1.0f,
          1.0f);

  if (d > 0.999999f) {
    return {};
  }

  if (d < -0.999999f) {
    Vec3Local axis =
        cross3(
            from,
            {1.0f, 0.0f, 0.0f});

    if (length3(axis) < 1.0e-4f) {
      axis =
          cross3(
              from,
              {0.0f, 0.0f, 1.0f});
    }

    return axisAngleQ(
        axis,
        kPi);
  }

  const Vec3Local c =
      cross3(from, to);

  return normalizeQ({
      1.0f + d,
      c.x,
      c.y,
      c.z});
}

[[nodiscard]]
Vec3Local shortestAngularError(
    QuatLocal current,
    QuatLocal target) noexcept {

  current = normalizeQ(current);
  target = normalizeQ(target);

  const QuatLocal inverseCurrent{
      current.w,
      -current.x,
      -current.y,
      -current.z};

  QuatLocal delta =
      normalizeQ(
          mulQ(
              target,
              inverseCurrent));

  if (delta.w < 0.0f) {
    delta.w = -delta.w;
    delta.x = -delta.x;
    delta.y = -delta.y;
    delta.z = -delta.z;
  }

  const float vectorLength =
      std::sqrt(
          delta.x * delta.x +
          delta.y * delta.y +
          delta.z * delta.z);

  if (vectorLength < 1.0e-7f) {
    return {};
  }

  const float angle =
      2.0f *
      std::atan2(
          vectorLength,
          std::clamp(
              delta.w,
              -1.0f,
              1.0f));

  const float scale =
      angle /
      vectorLength;

  return {
      delta.x * scale,
      delta.y * scale,
      delta.z * scale};
}

[[nodiscard]]
QuatLocal integrateWorldAngular(
    QuatLocal q,
    Vec3Local omega,
    float dt) noexcept {

  const float speed =
      length3(omega);

  if (speed < 1.0e-5f ||
      dt <= 0.0f) {
    return q;
  }

  const QuatLocal dq =
      axisAngleQ(
          omega,
          speed * dt);

  return normalizeQ(
      mulQ(
          dq,
          q));
}

void postRotateQuat(
    Mat4 &matrix,
    QuatLocal q) noexcept {

  q = normalizeQ(q);

  const float xx = q.x * q.x;
  const float yy = q.y * q.y;
  const float zz = q.z * q.z;
  const float xy = q.x * q.y;
  const float xz = q.x * q.z;
  const float yz = q.y * q.z;
  const float wx = q.w * q.x;
  const float wy = q.w * q.y;
  const float wz = q.w * q.z;

  const float r00 =
      1.0f - 2.0f * (yy + zz);

  const float r01 =
      2.0f * (xy - wz);

  const float r02 =
      2.0f * (xz + wy);

  const float r10 =
      2.0f * (xy + wz);

  const float r11 =
      1.0f - 2.0f * (xx + zz);

  const float r12 =
      2.0f * (yz - wx);

  const float r20 =
      2.0f * (xz - wy);

  const float r21 =
      2.0f * (yz + wx);

  const float r22 =
      1.0f - 2.0f * (xx + yy);

  float c0[4];
  float c1[4];
  float c2[4];

  std::memcpy(
      c0,
      &matrix.m[0],
      sizeof(c0));

  std::memcpy(
      c1,
      &matrix.m[4],
      sizeof(c1));

  std::memcpy(
      c2,
      &matrix.m[8],
      sizeof(c2));

  for (int row = 0; row < 4; ++row) {
    matrix.m[row] =
        c0[row] * r00 +
        c1[row] * r10 +
        c2[row] * r20;

    matrix.m[4 + row] =
        c0[row] * r01 +
        c1[row] * r11 +
        c2[row] * r21;

    matrix.m[8 + row] =
        c0[row] * r02 +
        c1[row] * r12 +
        c2[row] * r22;
  }
}

[[nodiscard]]
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

  const float id = 1.0f / det;

  inv[0] = c00 * id;
  inv[1] = c01 * id;
  inv[2] = c02 * id;

  inv[3] = c10 * id;
  inv[4] = c11 * id;
  inv[5] = c12 * id;

  inv[6] = c20 * id;
  inv[7] = c21 * id;
  inv[8] = c22 * id;

  return true;
}

[[nodiscard]]
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

[[nodiscard]]
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

[[nodiscard]]
bool isThinGroundShape(
    std::int32_t shape) noexcept {

  switch (shape) {
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

[[nodiscard]]
bool isTorchGroundShape(
    std::int32_t shape) noexcept {

  switch (shape) {
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

[[nodiscard]]
bool isShapedGroundShape(
    std::int32_t shape) noexcept {

  switch (shape) {
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
      : mDtor(dtor) {

    if (stack &&
        push &&
        dtor) {

      mRef =
          push(
              stack,
              false);

      mActive =
          mRef.stack != nullptr &&
          mRef.mat != nullptr;
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

      mRef.stack = nullptr;
      mRef.mat = nullptr;
    }
  }

  [[nodiscard]]
  Mat4 *matrix() noexcept {
    return mActive
               ? mRef.mat
               : nullptr;
  }

private:
  ItemPhysicsRuntime::MatrixStackRefAbi mRef{};
  ItemPhysicsRuntime::MatrixRefDtorFn mDtor{};
  bool mActive{};
};

}

ItemPhysicsRuntime *
ItemPhysicsRuntime::sInstance =
    nullptr;

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

  mRotationSpeed.store(
      static_cast<float>(
          config.rotationSpeed),
      std::memory_order_relaxed);

  mSettleSpeed.store(
      static_cast<float>(
          config.settleSpeed),
      std::memory_order_relaxed);

  mGroundTiltDeg.store(
      static_cast<float>(
          config.groundTilt),
      std::memory_order_relaxed);

  mHeightOffset.store(
      static_cast<float>(
          config.heightOffset),
      std::memory_order_relaxed);

  mBlockGroundHeight.store(
      static_cast<float>(
          config.blockGroundHeight),
      std::memory_order_relaxed);

  mThinBlockGroundHeight.store(
      static_cast<float>(
          config.thinBlockGroundHeight),
      std::memory_order_relaxed);

  mTorchGroundHeight.store(
      static_cast<float>(
          config.torchGroundHeight),
      std::memory_order_relaxed);

  mShapedBlockGroundHeight.store(
      static_cast<float>(
          config.shapedBlockGroundHeight),
      std::memory_order_relaxed);

  mSkullGroundHeight.store(
      static_cast<float>(
          config.skullGroundHeight),
      std::memory_order_relaxed);

  mShieldGroundHeight.store(
      static_cast<float>(
          config.shieldGroundHeight),
      std::memory_order_relaxed);

  mBannerGroundHeight.store(
      static_cast<float>(
          config.bannerGroundHeight),
      std::memory_order_relaxed);
}

bool ItemPhysicsRuntime::verifyProfile(
    const ResolvedVirtual &resolved,
    ll::mod::NativeMod &mod) const {

  const auto verifyWords =
      [&](
          std::uintptr_t address,
          const auto &fingerprint,
          const char *name) {

        const std::size_t bytes =
            fingerprint.size() *
            sizeof(std::uint32_t);

        if (!resolved.module.readable(
                address,
                bytes)) {

          mod.getLogger().warn(
              "{} is not readable",
              name);

          return false;
        }

        const auto *words =
            reinterpret_cast<
                const std::uint32_t *>(
                address);

        for (std::size_t i = 0;
             i < fingerprint.size();
             ++i) {

          if (words[i] !=
              fingerprint[i]) {

            mod.getLogger().warn(
                "Minecraft profile mismatch: "
                "{} word {}",
                name,
                i);

            return false;
          }
        }

        return true;
      };

  if (!verifyWords(
          resolved.target,
          profile::kRenderFingerprint,
          "ItemRenderer::render")) {
    return false;
  }

  if (!verifyWords(
          resolved.module.base +
              profile::kGetPosDeltaRva,
          profile::kGetPosDeltaFingerprint,
          "Actor::getPosDelta")) {
    return false;
  }

  if (!verifyWords(
          resolved.module.base +
              profile::kRenderItemGroupLikeRva,
          profile::kRenderItemGroupLikeFingerprint,
          "ItemRenderer render-group helper")) {
    return false;
  }

  if (!verifyWords(
          resolved.module.base +
              profile::kGetBlockTypeForRenderingRva,
          profile::kGetBlockTypeForRenderingFingerprint,
          "ItemStackBase::getBlockTypeForRendering")) {
    return false;
  }

  if (!verifyWords(
          resolved.module.base +
              profile::kBlockGraphicsGetForBlockTypeRva,
          profile::kBlockGraphicsGetForBlockTypeFingerprint,
          "BlockGraphics::getForBlock(BlockType)")) {
    return false;
  }

  if (!verifyWords(
          resolved.module.base +
              profile::kBlockGraphicsGetBlockShapeRva,
          profile::kBlockGraphicsGetBlockShapeFingerprint,
          "BlockGraphics::getBlockShape")) {
    return false;
  }

  if (!verifyWords(
          resolved.module.base +
              profile::kRelativeShadowStorageRva,
          profile::kRelativeShadowStorageFingerprint,
          "RelativeShadowOffsetComponent storage")) {
    return false;
  }

  if (!verifyWords(
          resolved.module.base +
              profile::kRelativeShadowEmplaceRva,
          profile::kRelativeShadowEmplaceFingerprint,
          "RelativeShadowOffsetComponent emplace")) {
    return false;
  }

  const auto executable =
      [&](std::uintptr_t rva) {
        return resolved.module.executable(
            resolved.module.base +
            rva);
      };

  if (!executable(
          profile::kGetWorldMatrixRva) ||

      !executable(
          profile::kMatrixStackPushRva) ||

      !executable(
          profile::kMatrixStackRefDtorRva) ||

      !executable(
          profile::kGetPosDeltaRva) ||

      !executable(
          profile::kRenderItemGroupLikeRva) ||

      !executable(
          profile::kGetBlockTypeForRenderingRva) ||

      !executable(
          profile::kBlockGraphicsGetForBlockTypeRva) ||

      !executable(
          profile::kBlockGraphicsGetForBlockRva) ||

      !executable(
          profile::kBlockGraphicsGetBlockShapeRva) ||

      !executable(
          profile::kRelativeShadowStorageRva) ||

      !executable(
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

  auto resolved =
      resolveVirtualByRtti(
          profile::kMinecraftModule,
          profile::kItemRendererRtti,
          profile::kItemRendererRenderVtableOffset);

  if (!resolved) {
    mod.getLogger().warn(
        "Item Physics inactive: "
        "failed to resolve ItemRenderer");

    return false;
  }

  if (!verifyProfile(
          *resolved,
          mod)) {

    mod.getLogger().warn(
        "Item Physics safe passthrough: "
        "Minecraft profile unsupported");

    return false;
  }

  mMinecraftBase =
      resolved->module.base;

  mRenderTarget =
      resolved->target;

  mRenderItemGroupTarget =
      mMinecraftBase +
      profile::kRenderItemGroupLikeRva;

  mGetWorldMatrix =
      reinterpret_cast<
          GetWorldMatrixFn>(
          mMinecraftBase +
          profile::kGetWorldMatrixRva);

  mMatrixPush =
      reinterpret_cast<
          MatrixPushFn>(
          mMinecraftBase +
          profile::kMatrixStackPushRva);

  mMatrixRefDtor =
      reinterpret_cast<
          MatrixRefDtorFn>(
          mMinecraftBase +
          profile::kMatrixStackRefDtorRva);

  mGetBlockTypeForRendering =
      reinterpret_cast<
          GetBlockTypeForRenderingFn>(
          mMinecraftBase +
          profile::kGetBlockTypeForRenderingRva);

  mGetBlockGraphicsForBlockType =
      reinterpret_cast<
          BlockGraphicsGetForBlockTypeFn>(
          mMinecraftBase +
          profile::kBlockGraphicsGetForBlockTypeRva);

  mGetBlockGraphicsForBlock =
      reinterpret_cast<
          BlockGraphicsGetForBlockFn>(
          mMinecraftBase +
          profile::kBlockGraphicsGetForBlockRva);

  mGetBlockGraphicsShape =
      reinterpret_cast<
          BlockGraphicsGetBlockShapeFn>(
          mMinecraftBase +
          profile::kBlockGraphicsGetBlockShapeRva);

  mGetRelativeShadowStorage =
      reinterpret_cast<
          RelativeShadowStorageFn>(
          mMinecraftBase +
          profile::kRelativeShadowStorageRva);

  mEmplaceRelativeShadow =
      reinterpret_cast<
          RelativeShadowEmplaceFn>(
          mMinecraftBase +
          profile::kRelativeShadowEmplaceRva);

  sInstance = this;
  mOriginal = nullptr;

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
    sInstance = nullptr;

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

    mOriginal = nullptr;
    sInstance = nullptr;

    return false;
  }

  mProfileSupported.store(
      true,
      std::memory_order_relaxed);

  mod.getLogger().info(
      "Item Physics active: "
      "1.26.45 early settle + planar stacks");

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

  if (sInstance == this) {
    sInstance = nullptr;
  }

  mOriginal = nullptr;
  mRenderItemGroupOriginal = nullptr;
  mGetWorldMatrix = nullptr;
  mMatrixPush = nullptr;
  mMatrixRefDtor = nullptr;
  mGetBlockTypeForRendering = nullptr;
  mGetBlockGraphicsForBlockType = nullptr;
  mGetBlockGraphicsForBlock = nullptr;
  mGetBlockGraphicsShape = nullptr;
  mGetRelativeShadowStorage = nullptr;
  mEmplaceRelativeShadow = nullptr;

  mRenderTarget = 0;
  mRenderItemGroupTarget = 0;
  mMinecraftBase = 0;

  clearStates();
}

void ItemPhysicsRuntime::clearStates() {
  std::lock_guard lock(
      mStateMutex);

  mStates.clear();
  mRenderCounter = 0;
}

void ItemPhysicsRuntime::renderDetour(
    void *self,
    void *renderContext,
    void *renderData) {

  if (sInstance) {
    sInstance->onRender(
        self,
        renderContext,
        renderData);
  }
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

  const auto original =
      mRenderItemGroupOriginal;

  if (!original) {
    return;
  }

  if (!gDroppedItemGroupActive ||
      !gDroppedItemGroupGrounded ||
      !self ||
      !renderContext ||
      copyCount <= 1 ||
      !mGetWorldMatrix ||
      !mMatrixPush ||
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

  void *stack =
      mGetWorldMatrix(
          renderContext);

  MatrixPushScope probe(
      stack,
      mMatrixPush,
      mMatrixRefDtor);

  Mat4 *current =
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

  const Mat4 matrixSnapshot =
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

  constexpr std::size_t kOffsetBase = 0x17C;
  constexpr std::size_t kOffsetStride = 0x0C;
  constexpr std::uint32_t kMaxExtraCopies = 3;

  const std::uint32_t extra =
      std::min<std::uint32_t>(
          copyCount - 1,
          kMaxExtraCopies);

  std::lock_guard tableLock(
      mGroupOffsetMutex);

  Vec3Local saved[
      kMaxExtraCopies]{};

  auto *base =
      reinterpret_cast<
          std::uint8_t *>(
          self);

  for (std::uint32_t i = 0;
       i < extra;
       ++i) {

    auto *offset =
        reinterpret_cast<
            Vec3Local *>(
            base +
            kOffsetBase +
            static_cast<std::size_t>(i) *
                kOffsetStride);

    saved[i] =
        *offset;

    Vec3Local world =
        transformVector3(
            matrixSnapshot,
            saved[i]);

    world.y = 0.0f;

    *offset =
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

  for (std::uint32_t i = 0;
       i < extra;
       ++i) {

    auto *offset =
        reinterpret_cast<
            Vec3Local *>(
            base +
            kOffsetBase +
            static_cast<std::size_t>(i) *
                kOffsetStride);

    *offset =
        saved[i];
  }
}

float ItemPhysicsRuntime::seededUnit(
    std::uint32_t seed) noexcept {

  seed ^= seed << 13;
  seed ^= seed >> 17;
  seed ^= seed << 5;

  return static_cast<float>(
             seed &
             0x00FFFFFFu) /
         16777215.0f;
}

float ItemPhysicsRuntime::wrapPi(
    float value) noexcept {

  while (value > kPi) {
    value -= 2.0f * kPi;
  }

  while (value < -kPi) {
    value += 2.0f * kPi;
  }

  return value;
}

float ItemPhysicsRuntime::approachAngle(
    float current,
    float target,
    float alpha) noexcept {

  const float delta =
      wrapPi(
          target -
          current);

  return wrapPi(
      current +
      delta *
          std::clamp(
              alpha,
              0.0f,
              1.0f));
}

bool ItemPhysicsRuntime::libcxxStringEquals(
    std::uintptr_t stringAddress,
    std::string_view wanted) noexcept {

  if (!stringAddress) {
    return false;
  }

  const auto *raw =
      reinterpret_cast<
          const std::uint8_t *>(
          stringAddress);

  const std::uint8_t flag =
      raw[0];

  std::size_t length = 0;
  const char *data = nullptr;

  if ((flag & 1u) == 0) {
    length =
        static_cast<std::size_t>(
            flag >> 1);

    data =
        reinterpret_cast<
            const char *>(
            raw + 1);

  } else {
    length =
        *reinterpret_cast<
            const std::size_t *>(
            raw + 8);

    data =
        *reinterpret_cast<
            const char *const *>(
            raw + 16);
  }

  return
      data &&
      length == wanted.size() &&
      std::memcmp(
          data,
          wanted.data(),
          length) == 0;
}

bool ItemPhysicsRuntime::tryGetRenderBlockShape(
    std::uintptr_t actorAddress,
    std::int32_t &shape) const noexcept {

  shape = -1;

  if (!actorAddress ||
      !mGetBlockTypeForRendering ||
      !mGetBlockGraphicsForBlockType ||
      !mGetBlockGraphicsShape) {
    return false;
  }

  const void *itemStackBase =
      reinterpret_cast<
          const void *>(
          actorAddress +
          profile::
              kItemStackBaseOffset);

  const void *weakPtrAddress =
      mGetBlockTypeForRendering(
          itemStackBase);

  if (!weakPtrAddress) {
    return false;
  }

  const auto counter =
      *reinterpret_cast<
          const std::uintptr_t *>(
          weakPtrAddress);

  if (!counter) {
    return false;
  }

  const auto blockType =
      *reinterpret_cast<
          const std::uintptr_t *>(
          counter);

  if (!blockType) {
    return false;
  }

  const void *graphics =
      mGetBlockGraphicsForBlockType(
          reinterpret_cast<
              const void *>(
              blockType));

  if (!graphics) {
    return false;
  }

  shape =
      mGetBlockGraphicsShape(
          graphics);

  return true;
}

bool ItemPhysicsRuntime::buildBlockRenderInfo(
    const void *block,
    BlockRenderInfo &info) const noexcept {

  info = {};

  if (!block) {
    return false;
  }

  if (mGetBlockGraphicsForBlock &&
      mGetBlockGraphicsShape) {

    const void *graphics =
        mGetBlockGraphicsForBlock(
            block);

    if (graphics) {
      info.blockShape =
          mGetBlockGraphicsShape(
              graphics);
    }
  }

  bool thinHorizontal =
      false;

  const auto blockAddress =
      reinterpret_cast<
          std::uintptr_t>(
          block);

  auto *blockType =
      *reinterpret_cast<
          void *const *>(
          blockAddress +
          profile::
              kBlockTypeOffset);

  if (blockType) {
    auto **vtable =
        *reinterpret_cast<
            void ***>(
            blockType);

    if (vtable) {
      constexpr std::size_t slot =
          profile::
              kBlockTypeGetVisualShapeVtableOffset /
          sizeof(void *);

      auto getVisualShape =
          reinterpret_cast<
              GetVisualShapeFn>(
              vtable[slot]);

      if (getVisualShape) {
        AabbAbi scratch{};

        const AabbAbi *bounds =
            getVisualShape(
                blockType,
                block,
                &scratch);

        if (bounds) {
          const float dx =
              bounds->maxX -
              bounds->minX;

          const float dy =
              bounds->maxY -
              bounds->minY;

          const float dz =
              bounds->maxZ -
              bounds->minZ;

          if (std::isfinite(dx) &&
              std::isfinite(dy) &&
              std::isfinite(dz) &&
              dx > kExtentEpsilon &&
              dy > kExtentEpsilon &&
              dz > kExtentEpsilon) {

            const float horizontal =
                std::min(dx, dz);

            thinHorizontal =
                dy <=
                horizontal *
                    kThinYRatio;
          }
        }
      }
    }
  }

  const bool skull =
      info.blockShape ==
      kSkullBlockShape;

  info.keepHorizontal =
      thinHorizontal ||
      skull;

  info.valid = true;

  return true;
}

ItemPhysicsRuntime::ItemRenderTraits
ItemPhysicsRuntime::classifyItem(
    std::uintptr_t actorAddress) const noexcept {

  ItemRenderTraits traits{};

  std::int32_t rendererShape =
      -1;

  if (tryGetRenderBlockShape(
          actorAddress,
          rendererShape)) {

    traits.hasRenderShape = true;
    traits.renderShape = rendererShape;
  }

  if (traits.hasRenderShape &&
      traits.renderShape ==
          kSkullBlockShape) {

    traits.valid = true;
    traits.modelClass = ModelClass::BlockItem;
    traits.specialKind = SpecialKind::None;

    traits.block.valid = true;
    traits.block.blockShape =
        kSkullBlockShape;

    traits.block.keepHorizontal = true;

    return traits;
  }

  const auto block =
      *reinterpret_cast<
          const void *const *>(
          actorAddress +
          profile::
              kBlockPtrOffset);

  if (block) {
    traits.valid = true;
    traits.modelClass = ModelClass::BlockItem;
    traits.specialKind = SpecialKind::None;

    (void)buildBlockRenderInfo(
        block,
        traits.block);

    return traits;
  }

  const auto itemHandle =
      *reinterpret_cast<
          const std::uintptr_t *>(
          actorAddress +
          profile::
              kItemHandleOffset);

  if (!itemHandle) {
    return traits;
  }

  const auto item =
      *reinterpret_cast<
          const std::uintptr_t *>(
          itemHandle);

  if (!item) {
    return traits;
  }

  const auto identifier =
      item +
      profile::
          kItemIdentifierOffset;

  const bool shield =
      libcxxStringEquals(
          identifier,
          kShieldId);

  const bool banner =
      libcxxStringEquals(
          identifier,
          kBannerId);

  traits.valid = true;

  if (shield) {
    traits.modelClass = ModelClass::SpecialItem;
    traits.specialKind = SpecialKind::Shield;

  } else if (banner) {
    traits.modelClass = ModelClass::SpecialItem;
    traits.specialKind = SpecialKind::Banner;

  } else {
    traits.modelClass = ModelClass::FlatItem;
    traits.specialKind = SpecialKind::None;
  }

  return traits;
}

ItemPhysicsRuntime::PhysicsState &
ItemPhysicsRuntime::stateFor(
    std::uint32_t entityId,
    std::chrono::steady_clock::time_point now) {

  auto [it, inserted] =
      mStates.try_emplace(
          entityId);

  auto &state =
      it->second;

  if (inserted ||
      !state.initialized) {

    state.initialized = true;
    state.wasGrounded = true;
    state.launchImpulseApplied = false;
    state.contactCaptured = false;

    state.fullRotX = 0.0f;
    state.fullRotY = 0.0f;
    state.fullRotZ = 0.0f;

    state.orientation = {};
    state.restOrientation = {};

    state.angularX = 0.0f;
    state.angularY = 0.0f;
    state.angularZ = 0.0f;

    state.restYaw =
        seededUnit(
            entityId ^
            0x27D4EB2Du) *
        2.0f *
        kPi;

    state.born = now;
    state.contactTime = now;
    state.lastUpdate = now;
    state.lastSeen = now;
  }

  return state;
}

void ItemPhysicsRuntime::updateState(
    PhysicsState &state,
    const ItemRenderTraits &traits,
    bool grounded,
    float verticalVelocity,
    std::chrono::steady_clock::time_point now) const {

  const float dt =
      std::clamp(
          std::chrono::duration<float>(
              now -
              state.lastUpdate)
              .count(),
          0.0f,
          0.10f);

  state.lastUpdate = now;
  state.lastSeen = now;

  std::int32_t shape = -1;

  if (traits.hasRenderShape) {
    shape = traits.renderShape;

  } else if (
      traits.modelClass ==
      ModelClass::BlockItem) {

    shape =
        traits.block.blockShape;
  }

  const bool head =
      shape ==
      kSkullBlockShape;

  const bool thin =
      !head &&
      ((traits.modelClass ==
            ModelClass::BlockItem &&
        traits.block.keepHorizontal) ||
       (shape >= 0 &&
        isThinGroundShape(shape)));

  const bool shaped =
      shape >= 0 &&
      (isTorchGroundShape(shape) ||
       isShapedGroundShape(shape));

  const bool special =
      traits.modelClass ==
      ModelClass::SpecialItem;

  const bool flat =
      traits.modelClass ==
          ModelClass::FlatItem &&
      !shaped;

  const bool fullBlock =
      traits.modelClass ==
          ModelClass::BlockItem &&
      !head &&
      !thin &&
      !shaped;

  if (fullBlock) {
    if (!grounded) {
      if (dt > 0.0f) {
        state.fullRotX =
            wrapPi(
                state.fullRotX +
                state.angularX *
                    dt);

        state.fullRotZ =
            wrapPi(
                state.fullRotZ +
                state.angularZ *
                    dt);

        const float damping =
            std::exp(
                -kFullBlockAirDamping *
                dt);

        state.angularX *= damping;
        state.angularZ *= damping;
      }

      state.fullRotY = 0.0f;
      state.wasGrounded = false;
      state.contactCaptured = false;

      return;
    }

    const float target =
        mGroundTiltDeg.load(
            std::memory_order_relaxed) *
        kDegToRad;

    const float speed =
        std::max(
            0.0f,
            mSettleSpeed.load(
                std::memory_order_relaxed));

    const float alpha =
        dt > 0.0f
            ? 1.0f -
                  std::exp(
                      -3.0f *
                      speed *
                      dt)
            : 0.0f;

    state.fullRotX =
        approachAngle(
            state.fullRotX,
            target,
            alpha);

    state.fullRotY = 0.0f;
    state.angularX = 0.0f;
    state.angularY = 0.0f;
    state.angularZ = 0.0f;
    state.wasGrounded = true;

    return;
  }

  auto currentQ =
      [&]() {

        return normalizeQ({
            state.orientation.w,
            state.orientation.x,
            state.orientation.y,
            state.orientation.z});
      };

  auto storeQ =
      [&](QuatLocal q) {

        q = normalizeQ(q);

        state.orientation = {
            q.w,
            q.x,
            q.y,
            q.z};
      };

  auto supportTarget =
      [&](QuatLocal q) -> QuatLocal {

        q = normalizeQ(q);

        if (flat ||
            special) {

          const Vec3Local normal =
              rotateQ(
                  q,
                  {0.0f, 0.0f, 1.0f});

          const Vec3Local target =
              normal.y >= 0.0f
                  ? Vec3Local{
                        0.0f,
                        1.0f,
                        0.0f}
                  : Vec3Local{
                        0.0f,
                        -1.0f,
                        0.0f};

          return normalizeQ(
              mulQ(
                  fromToQ(
                      normal,
                      target),
                  q));
        }

        if (head ||
            thin) {

          const Vec3Local normal =
              rotateQ(
                  q,
                  {0.0f, 1.0f, 0.0f});

          const Vec3Local target =
              normal.y >= 0.0f
                  ? Vec3Local{
                        0.0f,
                        1.0f,
                        0.0f}
                  : Vec3Local{
                        0.0f,
                        -1.0f,
                        0.0f};

          return normalizeQ(
              mulQ(
                  fromToQ(
                      normal,
                      target),
                  q));
        }

        if (shaped) {
          const Vec3Local axis =
              rotateQ(
                  q,
                  {0.0f, 1.0f, 0.0f});

          Vec3Local horizontal{
              axis.x,
              0.0f,
              axis.z};

          if (length3(horizontal) <
              0.08f) {

            const Vec3Local side =
                rotateQ(
                    q,
                    {1.0f, 0.0f, 0.0f});

            horizontal = {
                side.x,
                0.0f,
                side.z};

            if (length3(horizontal) <
                0.08f) {

              horizontal = {
                  std::cos(
                      state.restYaw),
                  0.0f,
                  std::sin(
                      state.restYaw)};
            }
          }

          horizontal =
              normalize3(
                  horizontal,
                  {
                      std::cos(
                          state.restYaw),
                      0.0f,
                      std::sin(
                          state.restYaw)});

          if (dot3(
                  axis,
                  horizontal) <
              0.0f) {

            horizontal.x =
                -horizontal.x;

            horizontal.z =
                -horizontal.z;
          }

          return normalizeQ(
              mulQ(
                  fromToQ(
                      axis,
                      horizontal),
                  q));
        }

        return q;
      };

  auto integrateSpring =
      [&](QuatLocal target,
          float spring,
          float damping,
          float maxAngular,
          float strength,
          float freeDamping) {

        QuatLocal q =
            currentQ();

        Vec3Local omega{
            state.angularX,
            state.angularY,
            state.angularZ};

        if (freeDamping > 0.0f) {
          const float d =
              std::exp(
                  -freeDamping *
                  dt);

          omega.x *= d;
          omega.y *= d;
          omega.z *= d;
        }

        if (strength > 0.0f) {
          const Vec3Local error =
              shortestAngularError(
                  q,
                  target);

          const float k =
              spring *
              strength;

          const float c =
              damping *
              std::sqrt(
                  std::max(
                      strength,
                      0.05f));

          omega.x +=
              (error.x * k -
               omega.x * c) *
              dt;

          omega.y +=
              (error.y * k -
               omega.y * c) *
              dt;

          omega.z +=
              (error.z * k -
               omega.z * c) *
              dt;
        }

        const float magnitude =
            length3(omega);

        if (magnitude >
                maxAngular &&
            magnitude >
                1.0e-5f) {

          const float factor =
              maxAngular /
              magnitude;

          omega.x *= factor;
          omega.y *= factor;
          omega.z *= factor;
        }

        q =
            integrateWorldAngular(
                q,
                omega,
                dt);

        if (strength > 0.0f) {
          const float error =
              length3(
                  shortestAngularError(
                      q,
                      target));

          if (error <
                  kContactStopError &&
              length3(omega) <
                  kContactStopAngular) {

            q = target;
            omega = {};
          }
        }

        storeQ(q);

        state.angularX = omega.x;
        state.angularY = omega.y;
        state.angularZ = omega.z;
      };

  float airDamping =
      kAirDampingFlat;

  float preSpring =
      kPreSettleSpringFlat;

  float preDamping =
      kPreSettleDampingFlat;

  float contactSpring =
      kContactSpringFlat;

  float contactDamping =
      kContactDampingFlat;

  float maxAir =
      kMaxAngularFlat;

  float maxContact =
      kMaxContactAngularFlat;

  if (head) {
    airDamping = kAirDampingHead;
    preSpring = kPreSettleSpringHead;
    preDamping = kPreSettleDampingHead;
    contactSpring = kContactSpringHead;
    contactDamping = kContactDampingHead;
    maxAir = kMaxAngularHead;
    maxContact = kMaxContactAngularHead;

  } else if (thin) {
    airDamping = kAirDampingThin;
    preSpring = kPreSettleSpringThin;
    preDamping = kPreSettleDampingThin;
    contactSpring = kContactSpringThin;
    contactDamping = kContactDampingThin;
    maxAir = kMaxAngularThin;
    maxContact = kMaxContactAngularThin;

  } else if (shaped) {
    airDamping = kAirDampingShaped;
    preSpring = kPreSettleSpringShaped;
    preDamping = kPreSettleDampingShaped;
    contactSpring = kContactSpringShaped;
    contactDamping = kContactDampingShaped;
    maxAir = kMaxAngularShaped;
    maxContact = kMaxContactAngularShaped;

  } else if (special) {
    airDamping = kAirDampingSpecial;
    preSpring = kPreSettleSpringSpecial;
    preDamping = kPreSettleDampingSpecial;
    contactSpring = kContactSpringSpecial;
    contactDamping = kContactDampingSpecial;
    maxAir = kMaxAngularSpecial;
    maxContact = kMaxContactAngularSpecial;
  }

  if (!grounded) {
    const float age =
        std::max(
            0.0f,
            std::chrono::duration<float>(
                now -
                state.born)
                .count());

    float ageT =
        std::clamp(
            (age -
             kPreSettleDelay) /
                kPreSettleRamp,
            0.0f,
            1.0f);

    ageT =
        ageT *
        ageT *
        (3.0f -
         2.0f *
             ageT);

    const float descent =
        std::clamp(
            (0.045f -
             verticalVelocity) /
                0.18f,
            0.0f,
            1.0f);

    const float strength =
        ageT *
        (0.30f +
         0.70f *
             descent);

    const QuatLocal target =
        supportTarget(
            currentQ());

    state.restOrientation = {
        target.w,
        target.x,
        target.y,
        target.z};

    integrateSpring(
        target,
        preSpring,
        preDamping,
        maxAir,
        strength,
        airDamping);

    if (std::abs(
            state.angularX) <
        0.006f) {
      state.angularX = 0.0f;
    }

    if (std::abs(
            state.angularY) <
        0.006f) {
      state.angularY = 0.0f;
    }

    if (std::abs(
            state.angularZ) <
        0.006f) {
      state.angularZ = 0.0f;
    }

    state.wasGrounded = false;
    state.contactCaptured = false;

    return;
  }

  if (!state.contactCaptured ||
      !state.wasGrounded) {

    const QuatLocal target =
        supportTarget(
            currentQ());

    state.restOrientation = {
        target.w,
        target.x,
        target.y,
        target.z};

    state.contactCaptured = true;
    state.contactTime = now;
  }

  state.wasGrounded = true;

  const QuatLocal target =
      normalizeQ({
          state.restOrientation.w,
          state.restOrientation.x,
          state.restOrientation.y,
          state.restOrientation.z});

  const float settle =
      std::clamp(
          mSettleSpeed.load(
              std::memory_order_relaxed) /
              3.0f,
          0.45f,
          2.0f);

  integrateSpring(
      target,
      contactSpring,
      contactDamping,
      maxContact,
      settle,
      0.0f);
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
          mStates.erase(it);

    } else {
      ++it;
    }
  }
}

void ItemPhysicsRuntime::updateItemShadowComponent(
    void *actor,
    bool grounded,
    bool hideShadow) const noexcept {

  if (!actor ||
      !mGetRelativeShadowStorage ||
      !mEmplaceRelativeShadow) {
    return;
  }

  const auto actorAddress =
      reinterpret_cast<
          std::uintptr_t>(
          actor);

  auto *registry =
      *reinterpret_cast<
          void **>(
          actorAddress +
          profile::
              kActorRegistryOffset);

  const auto entityId =
      *reinterpret_cast<
          const std::uint32_t *>(
          actorAddress +
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
      hideShadow
          ? std::numeric_limits<float>::max()
          : (grounded
                 ? 0.0f
                 : -0.5f);

  const auto storageAddress =
      reinterpret_cast<
          std::uintptr_t>(
          storage);

  const auto pagesBegin =
      *reinterpret_cast<
          const std::uintptr_t *>(
          storageAddress +
          0x08);

  const auto pagesEnd =
      *reinterpret_cast<
          const std::uintptr_t *>(
          storageAddress +
          0x10);

  bool present = false;
  std::uint32_t packedEntity = 0;

  if (pagesBegin &&
      pagesEnd >=
          pagesBegin) {

    const auto pageCount =
        (pagesEnd -
         pagesBegin) /
        sizeof(
            std::uintptr_t);

    const auto pageIndex =
        (entityId >>
         11) &
        0x7Fu;

    if (pageIndex <
        pageCount) {

      const auto sparsePage =
          *reinterpret_cast<
              const std::uintptr_t *>(
              pagesBegin +
              pageIndex *
                  sizeof(
                      std::uintptr_t));

      if (sparsePage) {
        const auto sparseSlot =
            entityId &
            0x7FFu;

        packedEntity =
            *reinterpret_cast<
                const std::uint32_t *>(
                sparsePage +
                sparseSlot *
                    sizeof(
                        std::uint32_t));

        const auto generation =
            entityId &
            0xFFFC0000u;

        present =
            (packedEntity ^
             generation) <=
            0x3FFFEu;
      }
    }
  }

  if (!present) {
    const std::uint32_t entityCopy =
        entityId;

    (void)mEmplaceRelativeShadow(
        storage,
        &entityCopy,
        false,
        &wanted);

    return;
  }

  const auto denseIndex =
      packedEntity &
      0x3FFFFu;

  const auto componentPages =
      *reinterpret_cast<
          const std::uintptr_t *>(
          storageAddress +
          0x50);

  if (!componentPages) {
    return;
  }

  const auto componentPageIndex =
      denseIndex >>
      7;

  const auto componentPage =
      *reinterpret_cast<
          const std::uintptr_t *>(
          componentPages +
          componentPageIndex *
              sizeof(
                  std::uintptr_t));

  if (!componentPage) {
    return;
  }

  const auto componentSlot =
      denseIndex &
      0x7Fu;

  *reinterpret_cast<float *>(
      componentPage +
      componentSlot *
          sizeof(float)) =
      wanted;
}

bool ItemPhysicsRuntime::hasOnGroundComponent(
    void *actor) const noexcept {

  if (!actor) {
    return false;
  }

  const auto actorAddress =
      reinterpret_cast<
          std::uintptr_t>(
          actor);

  const auto registry =
      *reinterpret_cast<
          const std::uintptr_t *>(
          actorAddress +
          profile::
              kActorRegistryOffset);

  const auto entityId =
      *reinterpret_cast<
          const std::uint32_t *>(
          actorAddress +
          profile::
              kActorEntityIdOffset);

  if (!registry) {
    return false;
  }

  const auto bucketsBegin =
      *reinterpret_cast<
          const std::uintptr_t *>(
          registry +
          0x38);

  const auto bucketsEnd =
      *reinterpret_cast<
          const std::uintptr_t *>(
          registry +
          0x40);

  const auto nodesBase =
      *reinterpret_cast<
          const std::uintptr_t *>(
          registry +
          0x50);

  const auto sentinel =
      *reinterpret_cast<
          const std::uintptr_t *>(
          registry +
          0x58);

  if (!bucketsBegin ||
      !bucketsEnd ||
      bucketsEnd <=
          bucketsBegin ||
      !nodesBase) {
    return false;
  }

  const auto bucketBytes =
      bucketsEnd -
      bucketsBegin;

  if ((bucketBytes %
       sizeof(std::uintptr_t)) !=
          0 ||

      bucketBytes /
              sizeof(
                  std::uintptr_t) >
          (1u << 20)) {
    return false;
  }

  const auto bucketCount =
      bucketBytes /
      sizeof(
          std::uintptr_t);

  if (bucketCount == 0) {
    return false;
  }

  const auto index =
      (bucketCount -
       1) &
      profile::
          kOnGroundFlagComponentHash;

  std::int64_t nodeIndex =
      *reinterpret_cast<
          const std::int64_t *>(
          bucketsBegin +
          index *
              sizeof(
                  std::uintptr_t));

  std::uintptr_t node = 0;

  for (int guard = 0;
       nodeIndex != -1 &&
       guard < 4096;
       ++guard) {

    node =
        nodesBase +
        static_cast<std::uintptr_t>(
            nodeIndex) *
            32u;

    if (*reinterpret_cast<
            const std::uint32_t *>(
            node + 8) ==
        profile::
            kOnGroundFlagComponentHash) {
      break;
    }

    nodeIndex =
        *reinterpret_cast<
            const std::int64_t *>(
            node);

    node = 0;
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
      (entityId >>
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

  const auto slot =
      entityId &
      0x7FFu;

  const auto generation =
      entityId &
      0xFFFC0000u;

  const auto slotValue =
      *reinterpret_cast<
          const std::uint32_t *>(
          page +
          slot *
              sizeof(
                  std::uint32_t));

  return
      (slotValue ^
       generation) <=
      0x3FFFEu;
}

void ItemPhysicsRuntime::onRender(
    void *self,
    void *renderContext,
    void *renderData) {

  const auto original =
      mOriginal;

  if (!original) {
    return;
  }

  if (!renderContext ||
      !renderData ||
      !mProfileSupported.load(
          std::memory_order_relaxed)) {

    original(
        self,
        renderContext,
        renderData);

    return;
  }

  const auto renderDataAddress =
      reinterpret_cast<
          std::uintptr_t>(
          renderData);

  auto *actor =
      *reinterpret_cast<
          void **>(
          renderDataAddress +
          profile::
              kRenderDataActorOffset);

  if (!actor) {
    original(
        self,
        renderContext,
        renderData);

    return;
  }

  const auto actorAddress =
      reinterpret_cast<
          std::uintptr_t>(
          actor);

  const bool grounded =
      hasOnGroundComponent(
          actor);

  const bool enabled =
      mEnabled.load(
          std::memory_order_relaxed);

  const bool hideShadow =
      enabled &&
      mHideItemShadow.load(
          std::memory_order_relaxed);

  updateItemShadowComponent(
      actor,
      grounded,
      hideShadow);

  if (!enabled) {
    original(
        self,
        renderContext,
        renderData);

    return;
  }

  const auto traits =
      classifyItem(
          actorAddress);

  if (!traits.valid) {
    original(
        self,
        renderContext,
        renderData);

    return;
  }

  const auto entityId =
      *reinterpret_cast<
          const std::uint32_t *>(
          actorAddress +
          profile::
              kActorEntityIdOffset);

  PhysicsState snapshot;

  bool physicsGrounded =
      grounded;

  bool fullBlockMotionModel =
      false;

  {
    std::lock_guard lock(
        mStateMutex);

    const auto now =
        std::chrono::
            steady_clock::now();

    auto &state =
        stateFor(
            entityId,
            now);

    std::int32_t motionShape =
        -1;

    if (traits.hasRenderShape) {
      motionShape =
          traits.renderShape;

    } else if (
        traits.modelClass ==
        ModelClass::BlockItem) {

      motionShape =
          traits.block.blockShape;
    }

    const bool head =
        motionShape ==
        kSkullBlockShape;

    const bool thin =
        !head &&
        ((traits.modelClass ==
              ModelClass::BlockItem &&
          traits.block.keepHorizontal) ||
         (motionShape >= 0 &&
          isThinGroundShape(
              motionShape)));

    const bool shaped =
        motionShape >= 0 &&
        (isTorchGroundShape(
             motionShape) ||
         isShapedGroundShape(
             motionShape));

    const bool special =
        traits.modelClass ==
        ModelClass::SpecialItem;

    const bool flat =
        traits.modelClass ==
            ModelClass::FlatItem &&
        !shaped;

    fullBlockMotionModel =
        traits.modelClass ==
            ModelClass::BlockItem &&
        !head &&
        !thin &&
        !shaped;

    Vec3MotionAbi motion{};
    bool hasMotion = false;

    const auto getPosDelta =
        reinterpret_cast<
            GetPosDeltaFn>(
            mMinecraftBase +
            profile::
                kGetPosDeltaRva);

    if (getPosDelta) {
      const auto *nativeMotion =
          getPosDelta(
              actor);

      if (nativeMotion &&
          std::isfinite(
              nativeMotion->x) &&
          std::isfinite(
              nativeMotion->y) &&
          std::isfinite(
              nativeMotion->z)) {

        motion =
            *nativeMotion;

        hasMotion = true;
      }
    }

    const float horizontalTickSpeed =
        hasMotion
            ? std::sqrt(
                  motion.x *
                      motion.x +
                  motion.z *
                      motion.z)
            : 0.0f;

    const float totalTickSpeed =
        hasMotion
            ? std::sqrt(
                  horizontalTickSpeed *
                      horizontalTickSpeed +
                  motion.y *
                      motion.y)
            : 0.0f;

    if (!physicsGrounded &&
        state.contactCaptured &&
        hasMotion &&
        totalTickSpeed <
            kGroundFlickerSpeedThreshold &&
        std::abs(
            motion.y) <
            kGroundFlickerVerticalThreshold) {

      physicsGrounded = true;
    }

    if (!physicsGrounded &&
        !state.launchImpulseApplied &&
        hasMotion) {

      const float wake =
          fullBlockMotionModel
              ? kFullBlockWakeThreshold
              : kNonFullWakeThreshold;

      if (totalTickSpeed >=
          wake) {

        const float horizontalSpeed =
            horizontalTickSpeed *
            kMinecraftTicksPerSecond;

        const float verticalSpeed =
            std::abs(
                motion.y) *
            kMinecraftTicksPerSecond;

        const float control =
            std::clamp(
                mRotationSpeed.load(
                    std::memory_order_relaxed),
                0.0f,
                3.0f);

        float spin =
            (kBaseNaturalSpin +
             horizontalSpeed *
                 kHorizontalSpinGain +
             verticalSpeed *
                 kVerticalSpinGain) *
            control;

        spin =
            std::clamp(
                spin,
                0.0f,
                kMaxNaturalSpin);

        Vec3Local axis{};

        if (horizontalTickSpeed >
            1.0e-4f) {

          axis = {
              motion.z /
                  horizontalTickSpeed,
              0.0f,
              -motion.x /
                  horizontalTickSpeed};

        } else {
          axis = {
              std::cos(
                  state.restYaw),
              0.0f,
              std::sin(
                  state.restYaw)};
        }

        const float variation =
            fullBlockMotionModel
                ? 0.18f
                : 0.07f;

        axis.x +=
            std::cos(
                state.restYaw) *
            variation;

        axis.z +=
            std::sin(
                state.restYaw) *
            variation;

        axis =
            normalize3(axis);

        float modelScale = 0.82f;
        float cap = kMaxNaturalSpin;

        if (head) {
          modelScale = 0.36f;
          cap = kMaxAngularHead;

        } else if (thin) {
          modelScale = 0.48f;
          cap = kMaxAngularThin;

        } else if (shaped) {
          modelScale = 0.50f;
          cap = kMaxAngularShaped;

        } else if (special) {
          modelScale = 0.54f;
          cap = kMaxAngularSpecial;

        } else if (flat) {
          modelScale = 0.56f;
          cap = kMaxAngularFlat;
        }

        Vec3Local omega{
            axis.x *
                spin *
                modelScale,
            0.0f,
            axis.z *
                spin *
                modelScale};

        const float omegaLength =
            length3(omega);

        if (omegaLength >
                cap &&
            omegaLength >
                1.0e-5f) {

          const float factor =
              cap /
              omegaLength;

          omega.x *= factor;
          omega.y *= factor;
          omega.z *= factor;
        }

        state.angularX = omega.x;
        state.angularY = omega.y;
        state.angularZ = omega.z;

        state.launchImpulseApplied = true;
        state.born = now;
      }
    }

    updateState(
        state,
        traits,
        physicsGrounded,
        hasMotion
            ? motion.y
            : 0.0f,
        now);

    snapshot = state;

    if ((++mRenderCounter &
         0xFFu) == 0) {
      pruneStates(now);
    }
  }

  auto &count =
      *reinterpret_cast<
          std::uint8_t *>(
          actorAddress +
          profile::
              kItemCountOffset);

  auto &inItemFrame =
      *reinterpret_cast<
          std::uint8_t *>(
          actorAddress +
          profile::
              kIsInItemFrameOffset);

  const auto oldCount =
      count;

  const auto oldInItemFrame =
      inItemFrame;

  if (mSingleModel.load(
          std::memory_order_relaxed)) {
    count = 1;
  }

  inItemFrame = 1;

  auto *position =
      reinterpret_cast<
          float *>(
          renderDataAddress +
          profile::
              kRenderDataPositionOffset);

  const float oldY =
      position[1];

  const bool ordinaryItem =
      traits.modelClass ==
      ModelClass::FlatItem;

  if (ordinaryItem) {
    position[1] =
        oldY +
        mHeightOffset.load(
            std::memory_order_relaxed);
  }

  if (grounded) {
    if (traits.modelClass ==
        ModelClass::SpecialItem) {

      switch (
          traits.specialKind) {

      case SpecialKind::Shield:
        position[1] +=
            mShieldGroundHeight.load(
                std::memory_order_relaxed);
        break;

      case SpecialKind::Banner:
        position[1] +=
            mBannerGroundHeight.load(
                std::memory_order_relaxed);
        break;

      case SpecialKind::None:
      default:
        break;
      }

    } else {
      std::int32_t groundShape =
          -1;

      if (traits.hasRenderShape) {
        groundShape =
            traits.renderShape;

      } else if (
          traits.modelClass ==
          ModelClass::BlockItem) {

        groundShape =
            traits.block.blockShape;
      }

      if (groundShape >= 0) {
        if (groundShape ==
            kSkullBlockShape) {

          position[1] +=
              mSkullGroundHeight.load(
                  std::memory_order_relaxed);

        } else if (
            (traits.modelClass ==
                 ModelClass::BlockItem &&
             traits.block.keepHorizontal) ||
            isThinGroundShape(
                groundShape)) {

          position[1] +=
              mThinBlockGroundHeight.load(
                  std::memory_order_relaxed);

        } else if (
            isTorchGroundShape(
                groundShape)) {

          position[1] +=
              mTorchGroundHeight.load(
                  std::memory_order_relaxed);

        } else if (
            isShapedGroundShape(
                groundShape)) {

          position[1] +=
              mShapedBlockGroundHeight.load(
                  std::memory_order_relaxed);

        } else {
          position[1] +=
              mBlockGroundHeight.load(
                  std::memory_order_relaxed);
        }
      }
    }
  }

  void *stack =
      mGetWorldMatrix
          ? mGetWorldMatrix(
                renderContext)
          : nullptr;

  {
    MatrixPushScope matrixScope(
        stack,
        mMatrixPush,
        mMatrixRefDtor);

    auto *matrix =
        matrixScope.matrix();

    if (!matrix) {
      position[1] = oldY;
      count = oldCount;
      inItemFrame = oldInItemFrame;

      original(
          self,
          renderContext,
          renderData);

      return;
    }

    const float x = position[0];
    const float y = position[1];
    const float z = position[2];

    postTranslate(
        *matrix,
        x,
        y,
        z);

    if (fullBlockMotionModel) {
      postRotateX(
          *matrix,
          snapshot.fullRotX);

      postRotateY(
          *matrix,
          snapshot.fullRotY);

      postRotateZ(
          *matrix,
          snapshot.fullRotZ);

    } else {
      const QuatLocal q{
          snapshot.orientation.w,
          snapshot.orientation.x,
          snapshot.orientation.y,
          snapshot.orientation.z};

      postRotateQuat(
          *matrix,
          q);
    }

    if (ordinaryItem) {
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

    const bool previousActive =
        gDroppedItemGroupActive;

    const bool previousGrounded =
        gDroppedItemGroupGrounded;

    gDroppedItemGroupActive =
        !mSingleModel.load(
            std::memory_order_relaxed);

    gDroppedItemGroupGrounded =
        grounded;

    original(
        self,
        renderContext,
        renderData);

    gDroppedItemGroupActive =
        previousActive;

    gDroppedItemGroupGrounded =
        previousGrounded;
  }

  position[1] = oldY;
  count = oldCount;
  inItemFrame = oldInItemFrame;
}

}
