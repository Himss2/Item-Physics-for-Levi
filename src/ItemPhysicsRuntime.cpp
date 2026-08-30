#include "ItemPhysicsRuntime.hpp"
#include "TargetProfile.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string_view>

namespace itemphysics {
namespace {

constexpr float kPi =
    3.14159265358979323846f;

constexpr float kDegToRad =
    kPi / 180.0f;

constexpr auto kStateTtl =
    std::chrono::seconds(8);

// ============================================================================
// Atlas constants
// ============================================================================

constexpr float kAtlasFlatLocalY =
    0.25f;

// ============================================================================
// BlockShape
// ============================================================================

constexpr std::int32_t kSkullBlockShape =
    83;

// ============================================================================
// Thin horizontal detection
// ============================================================================

constexpr float kThinYRatio =
    0.70f;

constexpr float kExtentEpsilon =
    0.0005f;

// ============================================================================
// Ground contact V2
//
// IMPORTANT:
//
// Ground correction sekarang TIDAK menentukan rotasi.
//
// Rotasi item yang sudah benar tetap dipertahankan.
//
// Kita hanya mengubah position.y ketika entity benar-benar grounded.
// ============================================================================

// Head pada build -0.125 sebelumnya sudah bergerak ke arah benar,
// tetapi masih terlihat melayang.
//
// Sekarang test berikutnya:
// total correction = -0.25.
//
// HANYA Skull shape 83.
constexpr float kSkullGroundLowering =
    -0.25f;

// Cross-texture / torch / beberapa model scale 0.5.
//
// Correction sedikit lebih agresif karena origin model-nya
// terlihat lebih tinggi.
constexpr float kLargeScaleBlockGroundLowering =
    -0.125f;

// Fence / lever / brewing stand / flower pot / lantern / chain
// dan shaped 3D lainnya.
constexpr float kShapedBlockGroundLowering =
    -0.085f;

// Unknown non-full block.
//
// Conservative fallback.
constexpr float kFallbackBlockGroundLowering =
    -0.0625f;

// Shield / banner.
//
// Atlas tidak memasukkannya ke ordinary flat-item correction
// -0.38/+0.25, jadi mempunyai correction grounded sendiri.
constexpr float kSpecialItemGroundLowering =
    -0.12f;

// ============================================================================
// Atlas special item identifiers
// ============================================================================

constexpr std::string_view kShieldId =
    "minecraft:shield";

constexpr std::string_view kBannerId =
    "minecraft:banner";

// ============================================================================
// Matrix stack scope
// ============================================================================

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

      mRef.stack =
          nullptr;

      mRef.mat =
          nullptr;
    }
  }

  [[nodiscard]]
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

} // namespace

// ============================================================================
// Static
// ============================================================================

ItemPhysicsRuntime *
ItemPhysicsRuntime::sInstance =
    nullptr;

// ============================================================================
// Config
// ============================================================================

void ItemPhysicsRuntime::applyConfig(
    const ItemPhysicsConfig &config) noexcept {

  mEnabled.store(
      config.enabled,
      std::memory_order_relaxed);

  mSingleModel.store(
      config.singleModel,
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
}

// ============================================================================
// Profile verification
// ============================================================================

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
             i <
             fingerprint.size();
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

  // --------------------------------------------------------------------------
  // ItemRenderer
  // --------------------------------------------------------------------------

  if (!verifyWords(
          resolved.target,

          profile::
              kRenderFingerprint,

          "ItemRenderer::render")) {

    return false;
  }

  // --------------------------------------------------------------------------
  // Vanilla block type for rendering.
  // --------------------------------------------------------------------------

  if (!verifyWords(
          resolved.module.base +
              profile::
                  kGetBlockTypeForRenderingRva,

          profile::
              kGetBlockTypeForRenderingFingerprint,

          "ItemStackBase::getBlockTypeForRendering")) {

    return false;
  }

  // --------------------------------------------------------------------------
  // BlockGraphics(BlockType).
  // --------------------------------------------------------------------------

  if (!verifyWords(
          resolved.module.base +
              profile::
                  kBlockGraphicsGetForBlockTypeRva,

          profile::
              kBlockGraphicsGetForBlockTypeFingerprint,

          "BlockGraphics::getForBlock(BlockType)")) {

    return false;
  }

  // --------------------------------------------------------------------------
  // BlockShape getter.
  // --------------------------------------------------------------------------

  if (!verifyWords(
          resolved.module.base +
              profile::
                  kBlockGraphicsGetBlockShapeRva,

          profile::
              kBlockGraphicsGetBlockShapeFingerprint,

          "BlockGraphics::getBlockShape")) {

    return false;
  }

  const auto executable =
      [&](std::uintptr_t rva) {

        return resolved.module.executable(
            resolved.module.base +
            rva);
      };

  if (!executable(
          profile::
              kGetWorldMatrixRva) ||

      !executable(
          profile::
              kMatrixStackPushRva) ||

      !executable(
          profile::
              kMatrixStackRefDtorRva) ||

      !executable(
          profile::
              kGetBlockTypeForRenderingRva) ||

      !executable(
          profile::
              kBlockGraphicsGetForBlockTypeRva) ||

      !executable(
          profile::
              kBlockGraphicsGetForBlockRva) ||

      !executable(
          profile::
              kBlockGraphicsGetBlockShapeRva)) {

    mod.getLogger().warn(
        "Minecraft 1.26.45 renderer helper validation failed");

    return false;
  }

  return true;
}

// ============================================================================
// Install
// ============================================================================

bool ItemPhysicsRuntime::install(
    ll::mod::NativeMod &mod) {

  uninstall();

  auto resolved =
      resolveVirtualByRtti(
          profile::
              kMinecraftModule,

          profile::
              kItemRendererRtti,

          profile::
              kItemRendererRenderVtableOffset);

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

  // --------------------------------------------------------------------------
  // Matrix.
  // --------------------------------------------------------------------------

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

  // --------------------------------------------------------------------------
  // Vanilla rendering block lookup.
  // --------------------------------------------------------------------------

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

  // --------------------------------------------------------------------------
  // Existing Block const* lookup.
  // --------------------------------------------------------------------------

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

  sInstance =
      this;

  mOriginal =
      nullptr;

  // --------------------------------------------------------------------------
  // Only ItemRenderer::render is hooked.
  //
  // No private-helper hook.
  // --------------------------------------------------------------------------

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

  mProfileSupported.store(
      true,
      std::memory_order_relaxed);

  mod.getLogger().info(
      "Item Physics active: "
      "Minecraft 1.26.45 + Ground Contact V2");

  return true;
}

// ============================================================================
// Uninstall
// ============================================================================

void ItemPhysicsRuntime::uninstall() {

  mProfileSupported.store(
      false,
      std::memory_order_relaxed);

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

  mRenderTarget =
      0;

  mMinecraftBase =
      0;

  clearStates();
}

// ============================================================================
// Clear states
// ============================================================================

void ItemPhysicsRuntime::clearStates() {

  std::lock_guard lock(
      mStateMutex);

  mStates.clear();

  mRenderCounter =
      0;
}

// ============================================================================
// Detour
// ============================================================================

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

// ============================================================================
// Math
// ============================================================================

float ItemPhysicsRuntime::seededUnit(
    std::uint32_t seed) noexcept {

  seed ^=
      seed << 13;

  seed ^=
      seed >> 17;

  seed ^=
      seed << 5;

  return static_cast<float>(
             seed &
             0x00FFFFFFu) /
         16777215.0f;
}

float ItemPhysicsRuntime::wrapPi(
    float value) noexcept {

  while (value >
         kPi) {

    value -=
        2.0f *
        kPi;
  }

  while (value <
         -kPi) {

    value +=
        2.0f *
        kPi;
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

// ============================================================================
// libc++ std::string
// ============================================================================

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

  return data &&
         length ==
             wanted.size() &&

         std::memcmp(
             data,
             wanted.data(),
             length) ==
             0;
}

// ============================================================================
// Vanilla render BlockShape lookup
// ============================================================================

bool ItemPhysicsRuntime::tryGetRenderBlockShape(
    std::uintptr_t actorAddress,
    std::int32_t &shape) const noexcept {

  shape =
      -1;

  if (!actorAddress ||
      !mGetBlockTypeForRendering ||
      !mGetBlockGraphicsForBlockType ||
      !mGetBlockGraphicsShape) {

    return false;
  }

  // --------------------------------------------------------------------------
  // ItemStackBase inside ItemActor.
  // --------------------------------------------------------------------------

  const void *itemStackBase =
      reinterpret_cast<
          const void *>(

          actorAddress +
          profile::
              kItemStackBaseOffset);

  // --------------------------------------------------------------------------
  // WeakPtr<BlockType const>&.
  // --------------------------------------------------------------------------

  const void *weakPtrAddress =
      mGetBlockTypeForRendering(
          itemStackBase);

  if (!weakPtrAddress) {

    return false;
  }

  // WeakPtr<T>[0] -> SharedCounter<T>*
  const auto counter =
      *reinterpret_cast<
          const std::uintptr_t *>(
          weakPtrAddress);

  if (!counter) {

    return false;
  }

  // SharedCounter<T>[0] -> T*
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

// ============================================================================
// Existing block information.
//
// IMPORTANT:
//
// This still controls orientation compatibility for:
//
// - slab
// - carpet
// - thin blocks
// - head
//
// Do not use this AABB to decide generic orientation.
// ============================================================================

bool ItemPhysicsRuntime::buildBlockRenderInfo(
    const void *block,
    BlockRenderInfo &info) const noexcept {

  info =
      {};

  if (!block) {

    return false;
  }

  // --------------------------------------------------------------------------
  // BlockShape.
  // --------------------------------------------------------------------------

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

  // --------------------------------------------------------------------------
  // VisualShape.
  //
  // Only detects thin horizontal block models.
  // --------------------------------------------------------------------------

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

              dx >
                  kExtentEpsilon &&

              dy >
                  kExtentEpsilon &&

              dz >
                  kExtentEpsilon) {

            const float horizontal =
                std::min(
                    dx,
                    dz);

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

  info.valid =
      true;

  return true;
}

// ============================================================================
// Classification
// ============================================================================

ItemPhysicsRuntime::ItemRenderTraits
ItemPhysicsRuntime::classifyItem(
    std::uintptr_t actorAddress) const noexcept {

  ItemRenderTraits traits{};

  // ==========================================================================
  // Vanilla rendering lookup ONLY for Skull.
  //
  // This part is intentionally narrow because it is what fixed head rendering
  // without changing fence/torch/lever.
  // ==========================================================================

  std::int32_t rendererShape =
      -1;

  if (tryGetRenderBlockShape(
          actorAddress,
          rendererShape) &&

      rendererShape ==
          kSkullBlockShape) {

    traits.valid =
        true;

    traits.modelClass =
        ModelClass::
            BlockItem;

    traits.block.valid =
        true;

    traits.block.blockShape =
        kSkullBlockShape;

    traits.block.keepHorizontal =
        true;

    return traits;
  }

  // ==========================================================================
  // Existing block-backed path.
  // ==========================================================================

  const auto block =
      *reinterpret_cast<
          const void *const *>(

          actorAddress +
          profile::
              kBlockPtrOffset);

  if (block) {

    traits.valid =
        true;

    traits.modelClass =
        ModelClass::
            BlockItem;

    (void)buildBlockRenderInfo(
        block,
        traits.block);

    return traits;
  }

  // ==========================================================================
  // Non-block item.
  // ==========================================================================

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

  traits.valid =
      true;

  traits.modelClass =
      (shield ||
       banner)

          ? ModelClass::
                SpecialItem

          : ModelClass::
                FlatItem;

  return traits;
}

// ============================================================================
// Ground correction
//
// V2 deliberately uses BlockShape classes instead of AABB contact.
//
// Why:
//
// The previous AABB pass correctly described a bounding box but did not
// reproduce several ItemRenderer-specific display transforms.
//
// The consequence was:
//
// correction ≈ 0
//
// for many models that still visibly floated.
//
// This function changes ONLY vertical translation.
// ============================================================================

float ItemPhysicsRuntime::calculateBlockGroundCorrection(
    const void *block,
    std::int32_t shape,
    float rotX,
    float rotY,
    float rotZ) noexcept {

  // Preserve signature so no caller logic needs special branches.
  (void)block;
  (void)rotX;
  (void)rotY;
  (void)rotZ;

  // ==========================================================================
  // HEAD / SKULL
  // ==========================================================================

  if (shape ==
      kSkullBlockShape) {

    return
        kSkullGroundLowering;
  }

  // ==========================================================================
  // FULL BLOCK
  //
  // Leave exact position unchanged.
  // ==========================================================================

  if (shape ==
      0) {

    return
        0.0f;
  }

  // ==========================================================================
  // Horizontal models we already fixed.
  //
  // Do not lower them again because they could begin clipping.
  // ==========================================================================

  switch (shape) {

  case 9:
    // rail

  case 14:
    // bed

  case 15:
    // diode

  case 23:
    // lilypad

  case 67:
    // slab / block_half

  case 68:
    // top snow

  case 69:
    // tripwire

  case 72:
    // repeater

  case 73:
    // comparator

  case 80:
    // end portal

  case 96:
    // coral fan

  case 99:
    // trapdoor

  case 114:
    // campfire

  case 126:
    // sculk sensor

  case 135:
    // glow lichen

  case 136:
    // redstone wire

    return
        0.0f;

  default:

    break;
  }

  // ==========================================================================
  // Large-scale / cross-style models.
  // ==========================================================================

  switch (shape) {

  case 1:
    // cross texture

  case 2:
    // torch

  case 90:
    // doublesided cross texture

  case 101:
    // conduit

  case 155:

    return
        kLargeScaleBlockGroundLowering;

  default:

    break;
  }

  // ==========================================================================
  // Shaped 3D models.
  //
  // These retain EXACTLY the orientation they already have.
  // ==========================================================================

  switch (shape) {

  case 7:
    // door

  case 8:
    // ladder

  case 10:
    // stairs

  case 11:
    // fence

  case 12:
    // lever

  case 13:
    // cactus

  case 18:
    // iron fence

  case 19:
    // stem

  case 20:
    // vine

  case 21:
    // fence gate

  case 22:
    // chest

  case 25:
    // brewing stand

  case 26:
    // portal frame

  case 28:
    // cocoa

  case 31:
    // tree

  case 32:
    // cobblestone wall

  case 40:
    // double plant

  case 42:
    // flower pot

  case 43:
    // anvil

  case 44:
    // dragon egg

  case 70:
    // tripwire hook

  case 71:
    // cauldron

  case 74:
    // hopper

  case 76:
    // piston

  case 77:
    // beacon

  case 78:
    // chorus plant

  case 79:
    // chorus flower

  case 81:
    // end rod

  case 84:
    // facing block

  case 87:
    // double-side fence

  case 89:
    // shulker box

  case 100:
    // sea pickle

  case 102:
    // turtle egg

  case 107:
    // sign

  case 108:
    // bamboo

  case 110:
    // scaffolding

  case 111:
    // grindstone

  case 112:
    // bell

  case 113:
    // lantern

  case 115:
    // lectern

  case 116:
    // berry bush

  case 119:
    // stonecutter

  case 123:
    // chain

  case 133:
    // azalea-style block

    return
        kShapedBlockGroundLowering;

  default:

    break;
  }

  // ==========================================================================
  // Unknown non-full BlockShape.
  //
  // Small conservative lowering.
  // ==========================================================================

  return
      kFallbackBlockGroundLowering;
}

// ============================================================================
// State creation
// ============================================================================

ItemPhysicsRuntime::PhysicsState &
ItemPhysicsRuntime::stateFor(
    std::uint32_t entityId,
    std::chrono::steady_clock::
        time_point now) {

  auto [it, inserted] =
      mStates.try_emplace(
          entityId);

  auto &state =
      it->second;

  if (inserted ||
      !state.initialized) {

    const float u =
        seededUnit(
            entityId ^
            0x9E3779B9u);

    const float v =
        seededUnit(
            entityId ^
            0x85EBCA6Bu);

    const float w =
        seededUnit(
            entityId ^
            0xC2B2AE35u);

    const float curve =
        std::max(
            0.0f,

            4.0f *
                    u *
                    u -

                4.0f *
                    u *
                    u *
                    u *
                    u);

    state.initialized =
        true;

    state.rotX =
        0.0f;

    state.rotY =
        0.0f;

    state.rotZ =
        0.0f;

    state.angularX =
        (v *
             2.0f -
         1.0f) *
        curve *
        kPi;

    state.angularZ =
        (w *
             2.0f -
         1.0f) *
        (1.0f -
         curve) *
        kPi;

    if (std::abs(
            state.angularX) +
            std::abs(
                state.angularZ) <
        0.20f) {

      state.angularX +=
          0.65f;
    }

    state.restYaw =
        seededUnit(
            entityId ^
            0x27D4EB2Du) *
        2.0f *
        kPi;

    state.wasGrounded =
        false;

    state.born =
        now;

    state.lastUpdate =
        now;

    state.lastSeen =
        now;
  }

  return state;
}

// ============================================================================
// Physics update
// ============================================================================

void ItemPhysicsRuntime::updateState(
    PhysicsState &state,
    const ItemRenderTraits &traits,
    bool grounded,
    std::chrono::steady_clock::
        time_point now) const {

  const float dt =
      std::clamp(
          std::chrono::duration<float>(
              now -
              state.lastUpdate)
              .count(),

          0.0f,
          0.20f);

  state.lastUpdate =
      now;

  state.lastSeen =
      now;

  const float frameFactor =
      std::min(
          dt *
              5.0f,
          1.0f);

  // ==========================================================================
  // AIRBORNE
  // ==========================================================================

  if (!grounded) {

    const float age =
        std::chrono::duration<float>(
            now -
            state.born)
            .count();

    const float ageFactor =
        std::min(
            age /
                4.0f,
            1.0f);

    const float fade =
        1.0f -
        ageFactor;

    const float speed =
        mRotationSpeed.load(
            std::memory_order_relaxed);

    const float step =
        fade *
        frameFactor *
        speed;

    state.rotX =
        wrapPi(
            state.rotX +
            state.angularX *
                step);

    state.rotZ =
        wrapPi(
            state.rotZ +
            state.angularZ *
                step);

    state.rotY =
        0.0f;

    state.wasGrounded =
        false;

    return;
  }

  // ==========================================================================
  // GROUNDED
  // ==========================================================================

  const float settle =
      frameFactor *
      mSettleSpeed.load(
          std::memory_order_relaxed);

  // --------------------------------------------------------------------------
  // Horizontal compatibility blocks.
  //
  // Keep exactly the existing behavior.
  // --------------------------------------------------------------------------

  if (traits.modelClass ==
          ModelClass::
              BlockItem &&

      traits.block.
          keepHorizontal) {

    state.rotX =
        approachAngle(
            state.rotX,
            0.0f,
            settle);

    state.rotZ =
        approachAngle(
            state.rotZ,
            0.0f,
            settle);

    state.rotY =
        approachAngle(
            state.rotY,
            state.restYaw,
            settle);
  }

  // --------------------------------------------------------------------------
  // Atlas baseline.
  // --------------------------------------------------------------------------

  else {

    const float target =
        mGroundTiltDeg.load(
            std::memory_order_relaxed) *
        kDegToRad;

    state.rotX =
        approachAngle(
            state.rotX,
            target,
            settle);

    state.rotY =
        0.0f;

    // Atlas keeps rotZ from landing.
  }

  state.wasGrounded =
      true;
}

// ============================================================================
// Cleanup
// ============================================================================

void ItemPhysicsRuntime::pruneStates(
    std::chrono::steady_clock::
        time_point now) {

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

// ============================================================================
// OnGroundFlagComponent
// ============================================================================

bool ItemPhysicsRuntime::
    hasOnGroundComponent(
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
       sizeof(
           std::uintptr_t)) !=
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

  if (bucketCount ==
      0) {

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

  std::uintptr_t node =
      0;

  for (int guard = 0;

       nodeIndex != -1 &&
       guard < 4096;

       ++guard) {

    node =
        nodesBase +

        static_cast<
            std::uintptr_t>(
            nodeIndex) *
            32u;

    if (*reinterpret_cast<
            const std::uint32_t *>(
            node +
            8) ==

        profile::
            kOnGroundFlagComponentHash) {

      break;
    }

    nodeIndex =
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

  return (
             slotValue ^
             generation) <=
         0x3FFFEu;
}

// ============================================================================
// Render
// ============================================================================

void ItemPhysicsRuntime::onRender(
    void *self,
    void *renderContext,
    void *renderData) {

  const auto original =
      mOriginal;

  if (!original) {

    return;
  }

  if (!mEnabled.load(
          std::memory_order_relaxed) ||

      !renderContext ||
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

  const bool grounded =
      hasOnGroundComponent(
          actor);

  PhysicsState snapshot;

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

    updateState(
        state,
        traits,
        grounded,
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

  // ==========================================================================
  // Temporary ItemActor state
  // ==========================================================================

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

    count =
        1;
  }

  // ==========================================================================
  // Atlas behavior.
  // ==========================================================================

  inItemFrame =
      1;

  auto *position =
      reinterpret_cast<
          float *>(

          renderDataAddress +
          profile::
              kRenderDataPositionOffset);

  const float oldY =
      position[1];

  // ==========================================================================
  // Ordinary non-block item
  // ==========================================================================

  const bool ordinaryItem =
      traits.modelClass ==
      ModelClass::
          FlatItem;

  if (ordinaryItem) {

    position[1] =
        oldY +
        mHeightOffset.load(
            std::memory_order_relaxed);
  }

  // ==========================================================================
  // Ground contact V2 - BlockItem
  //
  // ONLY modifies Y.
  // ==========================================================================

  if (grounded &&
      traits.modelClass ==
          ModelClass::
              BlockItem) {

    const auto block =
        *reinterpret_cast<
            const void *const *>(

            actorAddress +
            profile::
                kBlockPtrOffset);

    position[1] +=
        calculateBlockGroundCorrection(
            block,
            traits.block.blockShape,
            snapshot.rotX,
            snapshot.rotY,
            snapshot.rotZ);
  }

  // ==========================================================================
  // Ground contact V2 - SpecialItem
  //
  // Shield/banner are excluded from Atlas's ordinary -0.38 path.
  // ==========================================================================

  if (grounded &&
      traits.modelClass ==
          ModelClass::
              SpecialItem) {

    position[1] +=
        kSpecialItemGroundLowering;
  }

  // ==========================================================================
  // MatrixStack
  // ==========================================================================

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

      // Restore before fallback.
      position[1] =
          oldY;

      count =
          oldCount;

      inItemFrame =
          oldInItemFrame;

      original(
          self,
          renderContext,
          renderData);

      return;
    }

    const float x =
        position[0];

    const float y =
        position[1];

    const float z =
        position[2];

    // ========================================================================
    // Existing Atlas matrix order.
    //
    // DO NOT change for ground-height stage.
    // ========================================================================

    postTranslate(
        *matrix,
        x,
        y,
        z);

    postRotateX(
        *matrix,
        snapshot.rotX);

    postRotateY(
        *matrix,
        snapshot.rotY);

    postRotateZ(
        *matrix,
        snapshot.rotZ);

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

    original(
        self,
        renderContext,
        renderData);
  }

  // ==========================================================================
  // Restore
  // ==========================================================================

  position[1] =
      oldY;

  count =
      oldCount;

  inItemFrame =
      oldInItemFrame;
}

} // namespace itemphysics
