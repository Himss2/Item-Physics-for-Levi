#include <array>
#include <atomic>
#include <memory>
#include <string_view>
#define private public
#include "ItemPhysicsRuntime.hpp"
#undef private
#include "TargetProfile.hpp"
#include <cassert>
#include <cmath>
#include <cstring>
#include <iostream>
#include <vector>

namespace {
using R = itemphysics::ItemPhysicsRuntime;
template <class T, std::size_t N>
void put(std::array<std::byte, N> &bytes, std::size_t at, T value) {
  assert(at + sizeof(T) <= N);
  std::memcpy(bytes.data() + at, &value, sizeof(T));
}

// Actual profile layout at the native boundary: a registry hash table with
// water, lava and contact sparse component pages, plus an ItemActor header.
struct Fixture {
  alignas(16) std::array<std::byte, 0x450> actor{};
  alignas(16) std::array<std::byte, 0x60> registry{};
  std::array<std::int64_t, 8> buckets{};
  alignas(16) std::array<std::byte, 128> nodes{};
  alignas(16) std::array<std::byte, 0x18> lavaStorage{}, waterStorage{},
      groundStorage{}, collisionStorage{};
  std::array<std::uintptr_t, 1> lavaPages{}, waterPages{}, groundPages{},
      collisionPages{};
  std::array<std::uint32_t, 2048> lavaPage{}, waterPage{}, groundPage{},
      collisionPage{};
  R::Vec3Abi motion{0.1f, -0.04f, -0.2f};
  R::Vec3Abi current{2.0f, 60.0f, 3.0f};
  R::Vec3Abi previous{2.0f, 60.0f, 3.0f};
  static constexpr std::uint32_t entity = 17;
  static constexpr std::uint32_t lavaHash = 0x832A2768;
  static constexpr std::uint32_t waterHash = 0x78E89F39;
  Fixture() {
    buckets.fill(-1);
    lavaPage.fill(0xFFFFFFFFu);
    waterPage.fill(0xFFFFFFFFu);
    groundPage.fill(0xFFFFFFFFu);
    collisionPage.fill(0xFFFFFFFFu);
    lavaPage[entity] = entity;
    lavaPages[0] = reinterpret_cast<std::uintptr_t>(lavaPage.data());
    waterPages[0] = reinterpret_cast<std::uintptr_t>(waterPage.data());
    groundPages[0] = reinterpret_cast<std::uintptr_t>(groundPage.data());
    collisionPages[0] =
        reinterpret_cast<std::uintptr_t>(collisionPage.data());
    const auto add = [&](unsigned index, std::uint32_t hash, auto &storage,
                         auto &pages) {
      put(nodes, index * 32, buckets[hash & 7]);
      buckets[hash & 7] = index;
      put(nodes, index * 32 + 8, hash);
      put(nodes, index * 32 + 16, static_cast<void *>(storage.data()));
      put(storage, 8, reinterpret_cast<std::uintptr_t>(pages.data()));
      put(storage, 16, reinterpret_cast<std::uintptr_t>(pages.data() + 1));
    };
    add(0, lavaHash, lavaStorage, lavaPages);
    add(1, waterHash, waterStorage, waterPages);
    add(2, itemphysics::profile::kOnGroundFlagComponentHash, groundStorage,
        groundPages);
    add(3, itemphysics::profile::kVerticalCollisionFlagComponentHash,
        collisionStorage, collisionPages);
    put(registry, 0x38, reinterpret_cast<std::uintptr_t>(buckets.data()));
    put(registry, 0x40, reinterpret_cast<std::uintptr_t>(buckets.data() + 8));
    put(registry, 0x50, reinterpret_cast<std::uintptr_t>(nodes.data()));
    put(registry, 0x58, reinterpret_cast<std::uintptr_t>(nodes.data() + 128));
    put(actor, 0x10, static_cast<void *>(registry.data()));
    put(actor, 0x18, entity);
  }
};

Fixture *active{};
Fixture *destination{};
unsigned motionReads{};
unsigned removeCalls{};
const void *expectedBlockTypeHandle{};
int graphicsMarker{};
constexpr std::int64_t sourceUniqueId = 0x10203040;
constexpr std::int64_t destinationUniqueId = 0x50607080;
const R::Vec3Abi *getMotion(const void *actor) {
  assert(active && actor == active->actor.data());
  ++motionReads;
  return &active->motion;
}
const R::Vec3Abi *getCurrent(const void *actor) {
  assert(active && actor == active->actor.data());
  return &active->current;
}
const R::Vec3Abi *getPrevious(const void *actor) {
  assert(active && actor == active->actor.data());
  return &active->previous;
}
const std::int64_t *getUniqueId(void *actor) {
  assert(active);
  if (actor == active->actor.data())
    return &sourceUniqueId;
  if (destination && actor == destination->actor.data())
    return &destinationUniqueId;
  assert(false && "unexpected actor in UniqueID fixture");
  return nullptr;
}
void nativeRemove(void *actor) {
  assert(active && actor == active->actor.data());
  ++removeCalls;
}
const void *getBlockTypeHandle(const void *stack) {
  assert(active);
  assert(stack == active->actor.data() +
                      itemphysics::profile::kItemStackBaseOffset);
  return expectedBlockTypeHandle;
}
void *getGraphicsForBlockType(const void *handle) {
  return handle == expectedBlockTypeHandle ? &graphicsMarker : nullptr;
}
std::int32_t getGraphicsShape(const void *graphics) {
  assert(graphics == &graphicsMarker);
  return 43;
}
void configure(R &r) {
  R::sInstance = &r;
  r.mProfileSupported.store(true);
  r.mGetPosDelta = getMotion;
  r.mGetActorPosition = getCurrent;
  r.mGetActorPreviousPosition = getPrevious;
  r.mGetActorUniqueId = getUniqueId;
}

// Exercise the real onRender matrix path. The native boundary below models
// only the verified position translation (0xA71224C..0xA7122D4). It does not
// pretend to reproduce resource-pack geometry or prove contact with terrain.
itemphysics::Mat4 renderMatrix{};
std::vector<itemphysics::Mat4> renderedMatrices;
void *worldMatrix(void *) { return &renderMatrix; }
R::MatrixStackRefAbi pushMatrix(void *stack, bool) {
  renderMatrix = {};
  for (int i : {0, 5, 10, 15}) renderMatrix.m[i] = 1.0f;
  return {stack, &renderMatrix};
}
void popMatrix(R::MatrixStackRefAbi *) {}
void captureRender(void *, void *, void *data) {
  auto matrix = renderMatrix;
  const auto *position = reinterpret_cast<const float *>(
      static_cast<const std::byte *>(data) +
      itemphysics::profile::kRenderDataPositionOffset);
  itemphysics::postTranslate(matrix, position[0], position[1], position[2]);
  renderedMatrices.push_back(matrix);
  assert(active->actor[itemphysics::profile::kIsInItemFrameOffset] ==
         std::byte{1});
}

void checkGroundedFlatRenderPhase() {
  using H = R::HeightClass;
  using F = itemphysics::DropFluidKind;
  constexpr float tau = 6.283185307179586f;
  constexpr float originalY = 12.125f;
  for (bool separate : {false, true}) {
    for (H height : {H::FlatItem, H::FullBlock, H::HorizontalThin,
                     H::ShapedBlock, H::Special}) {
      for (F fluid : {F::None, F::Water, F::Lava}) {
        for (bool contact : {false, true}) {
          // A phase sweep catches the previous up-to-0.05-block height spread.
          for (int step = 0; step <= 16; ++step) {
            R runtime;
            Fixture fixture;
            active = &fixture;
            configure(runtime);
            runtime.mOriginal = captureRender;
            runtime.mGetWorldMatrix = worldMatrix;
            runtime.mMatrixPush = pushMatrix;
            runtime.mMatrixRefDtor = popMatrix;
            runtime.mSeparateDropVisuals.store(separate);
            runtime.mSeparateDropTrackingAvailable.store(separate);
            const float phase = tau * float(step) / 16.0f;
            put(fixture.actor, itemphysics::profile::kItemAgeOffset, 40);
            put(fixture.actor, itemphysics::profile::kItemBobOffset, phase);
            put(fixture.actor, itemphysics::profile::kItemCountOffset,
                std::uint8_t{64});
            auto &state = runtime.stateFor(Fixture::entity, 40, phase, 40.0f);
            state.traitsSampled = true;
            state.traits.valid = true;
            state.traits.height = height;
            state.traits.block = height != H::FlatItem && height != H::Special;
            state.traits.groundFlat = height == H::HorizontalThin;
            state.lastProbeAge = 40;
            const bool grounded = contact && fluid == F::None;
            state.groundedLatched = grounded;
            state.fluid = fluid;
            state.shadowInitialized = true;
            state.shadowHidden = true;
            alignas(16) std::array<std::byte, 0x20> data{};
            put(data, 0, static_cast<void *>(fixture.actor.data()));
            put(data, 0x10, 3.0f);
            put(data, 0x14, originalY);
            put(data, 0x18, -2.0f);
            const auto savedData = data;
            const float base = runtime.renderWorldY(
                originalY, state.traits, grounded, fluid, 40.0f, 0, false);
            const float pivotLift = state.traits.block ? 0.08f :
                (grounded && height == H::FlatItem ? 0.09f :
                 0.04f + phase * 0.007957747154594767f);
            const bool retainedFlat = separate && grounded && height == H::FlatItem;
            if (retainedFlat) {
              // A merged drop keeps its own phase and world origin, but must
              // receive exactly the same contact support as the live sprite.
              itemphysics::DropVisualLineage source;
              runtime.mDropAnchors.observe(source, 1);
              runtime.mDropAnchors.observe(state.dropLineage, 63);
              itemphysics::DropVisualPose pose{};
              pose.worldX = fixture.current.x + 0.5f;
              pose.baseWorldY = fixture.current.y +
                               runtime.heightOffset(state.traits, true);
              pose.worldZ = fixture.current.z;
              pose.xRotCosine = 1.0f;
              pose.yRotSine = std::sin(tau - phase);
              pose.yRotCosine = std::cos(tau - phase);
              pose.bobOffset = tau - phase;
              pose.grounded = true;
              pose.stableGroundContact = true;
              assert(runtime.mDropAnchors.merge(source, pose, 1,
                                                state.dropLineage, 63, 64));
            }
            renderedMatrices.clear();
            runtime.onRender(nullptr, &runtime, data.data());
            assert(renderedMatrices.size() == (retainedFlat ? 6u : 5u));
            for (const auto &matrix : renderedMatrices) {
              // Several model points: yaw/copy displacement cannot alter Y.
              for (float x : {-0.5f, 0.0f, 0.5f}) {
                const float y = matrix.m[1] * x + matrix.m[5] * 0.25f +
                                matrix.m[13];
                const float expected = base + pivotLift +
                    (height == H::HorizontalThin &&
                     (grounded || fluid != F::None) ? 0.25f : 0.0f);
                if (std::abs(y - expected) >= 0.00001f)
                  std::cerr << "render height mismatch: class=" << int(height)
                            << " fluid=" << int(fluid)
                            << " grounded=" << grounded << " SDV=" << separate
                            << " phase=" << phase << " actual=" << y
                            << " expected=" << expected << '\n';
                assert(std::abs(y - expected) < 0.00001f);
              }
            }
            assert(data == savedData);
            assert(fixture.actor[itemphysics::profile::kIsInItemFrameOffset] ==
                   std::byte{0});
          }
        }
      }
    }
  }
  active = nullptr;
}
}

int main() {
  checkGroundedFlatRenderPhase();
  R r;
  using H = R::HeightClass;
  using C = R::GroundCalibration;
  struct GroundCase { H height; C calibration; bool dragon; float worldY; };
  const GroundCase groundCases[] = {
      {H::FlatItem, C::Default, false, 63.794f},
      {H::ShapedBlock, C::Default, false, 63.795f},
      {H::FullBlock, C::Default, false, 63.913f},
      {H::HorizontalThin, C::Default, false, 63.822f},
      {H::Head, C::Default, false, 64.165f},
      {H::Head, C::Default, true, 64.203f},
      {H::Special, C::Shield, false, 63.853f},
      {H::Special, C::Banner, false, 63.841f},
      {H::ShapedBlock, C::FenceFamily, false, 63.829f},
      {H::ShapedBlock, C::Scaffolding, false, 63.919f},
  };
  // A fluid correction accidentally added to the ground path would move these
  // device-calibrated poses. Check rendered positions, not private constants.
  for (const auto &row : groundCases) {
    R::ItemRenderTraits traits{};
    traits.height = row.height;
    traits.calibration = row.calibration;
    traits.dragonHead = row.dragon;
    assert(std::abs(r.renderWorldY(64.0f, traits, true,
        itemphysics::DropFluidKind::None, 40.0f, 0) - row.worldY) < 0.00001f);
    assert(r.renderWorldY(64.0f, traits, false,
        itemphysics::DropFluidKind::None, 40.0f, 0) == 64.0f);
  }
  using F = itemphysics::DropFluidKind;
  // Approved holds and amplitude survive unification. Lava takes twice the
  // elapsed time to reach the same waveform position as water.
  for (float tick : {0.0f, 4.0f, 8.0f, 91.0f})
    assert(std::abs(r.waterBobOffset(tick, 0.0f, F::Water) + 0.015f) < 1e-6f);
  for (float tick : {40.0f, 50.0f, 59.0f})
    assert(std::abs(r.waterBobOffset(tick, 0.0f, F::Water) - 0.015f) < 1e-6f);
  for (int n = 0; n < 728; ++n) {
    const float sample = float(n) * 0.25f;
    const float bob = r.waterBobOffset(sample, 0.0f, F::Water);
    assert(std::abs(bob) <= 0.015001f);
    assert(std::abs(r.waterBobOffset(sample * 2.0f, 0.0f, F::Lava) - bob) < 1e-6f);
  }

  // Water keeps the device-approved 0.15.x surface lift. Lava uses one lower
  // class-independent lift so every route meets the denser fluid at the same
  // visible surface instead of hovering above it.
  R::ItemRenderTraits flat{};
  flat.height = H::FlatItem;
  R::ItemRenderTraits full{};
  full.height = H::FullBlock;
  assert(std::abs(r.renderWorldY(64.0f, flat, false, F::Water,
                                40.0f, 0) - 64.140f) < 0.00001f);
  assert(std::abs(r.renderWorldY(64.0f, full, false, F::Water,
                                40.0f, 0) - 64.140f) < 0.00001f);
  for (H height : {H::ShapedBlock, H::HorizontalThin, H::Special}) {
    R::ItemRenderTraits traits{};
    traits.height = height;
    assert(std::abs(r.renderWorldY(64.0f, traits, false, F::Water,
                                  40.0f, 0) - 64.140f) < 0.00001f);
  }
  assert(std::abs(r.renderWorldY(64.0f, flat, false, F::Water,
                                40.0f, 0, false) - 64.125f) < 0.00001f);
  for (H height : {H::FlatItem, H::ShapedBlock, H::FullBlock,
                   H::HorizontalThin, H::Head, H::Special}) {
    R::ItemRenderTraits traits{};
    traits.height = height;
    assert(std::abs(r.renderWorldY(64.0f, traits, false, F::Lava,
                                  40.0f, 0, false) - 64.055f) < 0.00001f);
  }

  // A 2D/shaped item must be completely prone as soon as it enters liquid;
  // a full 3D block still freezes its last airborne angle.
  R::VisualState flatState{};
  flatState.sampled = true;
  flatState.lastSample = 5.0f;
  flatState.xRot = 0.75f;
  R::updateRotation(flatState, false, false, true, 6, 6.0f);
  assert(flatState.xRot == 0.0f);
  R::VisualState blockState{};
  blockState.sampled = true;
  blockState.lastSample = 5.0f;
  blockState.xRot = 0.75f;
  R::updateRotation(blockState, true, false, true, 6, 6.0f);
  assert(blockState.xRot == 0.75f);

  configure(r);
  active = new Fixture;

  // Regression: ItemStackBase returns a BlockType handle and
  // BlockGraphics::getForBlockType consumes that same handle. Unwrapping it
  // inside the mod passes an internal pointer as the wrong ABI type and
  // crashes as soon as a dropped block is first rendered on world entry.
  std::array<std::uintptr_t, 1> inner{{0x1234u}};
  std::array<std::uintptr_t, 1> blockTypeHandle{
      {reinterpret_cast<std::uintptr_t>(inner.data())}};
  expectedBlockTypeHandle = blockTypeHandle.data();
  r.mGetBlockTypeForRendering = getBlockTypeHandle;
  r.mGetBlockGraphicsForBlockType = getGraphicsForBlockType;
  r.mGetBlockGraphicsShape = getGraphicsShape;
  std::int32_t renderShape = -1;
  assert(r.tryGetRenderBlockShape(
      reinterpret_cast<std::uintptr_t>(active->actor.data()), renderShape));
  assert(renderShape == 43);

  // Native onGround may coexist with lava at the bottom: rendering must not
  // apply the calibrated ground lowering while the item is in fluid.
  active->groundPage[Fixture::entity] = Fixture::entity;
  R::VisualState state{};
  assert(!r.resolveGrounded(state, active->actor.data(), 10, 60.0f));
  assert(state.fluid == itemphysics::DropFluidKind::Lava);
  active->lavaPage[Fixture::entity] = 0xFFFFFFFFu;
  for (int age = 11; age < 16; ++age)
    (void)r.resolveGrounded(state, active->actor.data(), age, 60.0f);
  assert(state.fluid == itemphysics::DropFluidKind::None);
  assert(r.resolveGrounded(state, active->actor.data(), 16, 60.0f));

  // Stable ActorRenderData Y is not ground evidence: it is camera-relative.
  // Without native ground or vertical collision, a slowly moving actor may
  // never become a persistent dry anchor.
  active->groundPage[Fixture::entity] = 0xFFFFFFFFu;
  active->motion.y = -0.01f;
  R::VisualState airborne{};
  for (int age = 30; age < 38; ++age) {
    active->current.y -= 0.02f;
    assert(!r.resolveGrounded(airborne, active->actor.data(), age, 12.0f));
  }

  // Individually tiny movement is still movement. Four sub-epsilon steps
  // must not accumulate into a permanent ground latch while an item descends.
  active->motion.y = -0.01f;
  active->current.y = 60.0f;
  R::VisualState slowlyDescending{};
  bool slowDescentGrounded = false;
  for (int age = 38; age < 46; ++age) {
    active->current.y -= 0.01f;
    slowDescentGrounded =
        r.resolveGrounded(slowlyDescending,
                          active->actor.data(), age, 12.0f) ||
        slowDescentGrounded;
  }

  // The conservative mob-drop fallback remains available when vertical
  // collision and stable absolute Actor world Y agree for two game ticks.
  active->collisionPage[Fixture::entity] = Fixture::entity;
  active->motion.y = 0.0f;
  active->current.y = 59.0f;
  R::VisualState collided{};
  assert(!r.resolveGrounded(collided, active->actor.data(), 40, -8.0f));
  assert(!r.resolveGrounded(collided, active->actor.data(), 41, -8.0f));
  assert(r.resolveGrounded(collided, active->actor.data(), 42, -8.0f));
  assert(collided.groundedRenderTicks == 1u);
  assert(r.resolveGrounded(collided, active->actor.data(), 42, -8.0f));
  assert(collided.groundedRenderTicks == 1u);
  assert(r.resolveGrounded(collided, active->actor.data(), 43, -8.0f));
  assert(collided.groundedRenderTicks == 2u);

  // Some mob-spawned ItemActors stop on the floor without retaining the
  // VerticalCollision component. Stable absolute Actor Y plus negligible
  // native vertical motion must still settle the visual after four distinct
  // game ticks, otherwise the item keeps using the airborne rotation path.
  active->collisionPage[Fixture::entity] = 0xFFFFFFFFu;
  active->motion.y = 0.0f;
  active->current.y = 58.0f;
  R::VisualState mobDropWithoutCollision{};
  assert(!r.resolveGrounded(mobDropWithoutCollision,
                            active->actor.data(), 44, -8.0f));
  assert(!r.resolveGrounded(mobDropWithoutCollision,
                            active->actor.data(), 45, -8.0f));
  assert(!r.resolveGrounded(mobDropWithoutCollision,
                            active->actor.data(), 46, -8.0f));
  assert(!r.resolveGrounded(mobDropWithoutCollision,
                            active->actor.data(), 47, -8.0f));
  assert(r.resolveGrounded(mobDropWithoutCollision,
                           active->actor.data(), 48, -8.0f));

  // Collision evidence is a separate two-sample path. A single collision
  // sample must not reuse stationary fallback samples collected beforehand.
  active->motion.y = 0.0f;
  active->current.y = 57.0f;
  R::VisualState intermittentCollision{};
  assert(!r.resolveGrounded(intermittentCollision,
                            active->actor.data(), 70, -8.0f));
  assert(!r.resolveGrounded(intermittentCollision,
                            active->actor.data(), 71, -8.0f));
  active->collisionPage[Fixture::entity] = Fixture::entity;
  assert(!r.resolveGrounded(intermittentCollision,
                            active->actor.data(), 72, -8.0f));
  assert(r.resolveGrounded(intermittentCollision,
                           active->actor.data(), 73, -8.0f));
  assert(!slowDescentGrounded);

  // ECS entity slots can be reused within the same age. A new native UniqueID
  // must reset rotation, traits and retained lineage instead of inheriting a
  // stale actor's state.
  auto &firstIdentity = r.stateFor(Fixture::entity, 0, 0.1f, 0.0f,
                                   1001u, 0x1111u);
  firstIdentity.xRot = 2.0f;
  firstIdentity.traitsSampled = true;
  auto &secondIdentity = r.stateFor(Fixture::entity, 0, 0.2f, 0.0f,
                                    1002u, 0x1111u);
  assert(secondIdentity.xRot == 0.0f);
  assert(!secondIdentity.traitsSampled);
  assert(secondIdentity.uniqueId == 1002u);

  // Reading native water/lava state for rendering must never mutate the
  // actor's velocity. Minecraft remains the sole fluid-physics authority.
  active->groundPage[Fixture::entity] = 0xFFFFFFFFu;
  active->collisionPage[Fixture::entity] = Fixture::entity;
  active->waterPage[Fixture::entity] = Fixture::entity;
  active->lavaPage[Fixture::entity] = 0xFFFFFFFFu;
  active->motion.y = -0.04f;
  R::VisualState nativeWater{};
  for (int age = 50; age < 55; ++age) {
    active->current.y += 0.05f;
    assert(!r.resolveGrounded(nativeWater, active->actor.data(), age, 8.0f));
    assert(active->motion.y == -0.04f);
  }
  active->waterPage[Fixture::entity] = 0xFFFFFFFFu;
  active->lavaPage[Fixture::entity] = Fixture::entity;
  active->motion.y = -0.04f;
  R::VisualState nativeLava{};
  for (int age = 60; age < 65; ++age) {
    active->current.y += 0.025f;
    assert(!r.resolveGrounded(nativeLava, active->actor.data(), age, 8.0f));
    assert(active->motion.y == -0.04f);
  }

  // The Actor::remove bridge used by Separate Drop Visuals remains an exact
  // passthrough while tracking is disabled.
  r.mActorRemoveOriginal = nativeRemove;
  R::sInstance = &r;
  R::dispatchActorRemove(active->actor.data(), nullptr, 0);
  assert(removeCalls == 1);

  // Enabling tracking must retain both merge signals and still call the native
  // remove exactly once. This guards the merge-only hook after deleting the
  // unrelated ItemActor::normalTick fluid-physics detour.
  destination = new Fixture;
  constexpr std::uintptr_t itemActorVptr = 0x12345678u;
  put(active->actor, 0, itemActorVptr);
  put(destination->actor, 0, itemActorVptr);
  put(active->actor, itemphysics::profile::kItemCountOffset,
      static_cast<std::uint8_t>(2));
  put(destination->actor, itemphysics::profile::kItemCountOffset,
      static_cast<std::uint8_t>(5));
  put(active->actor, itemphysics::profile::kItemAgeOffset,
      static_cast<std::int32_t>(64));
  r.mItemActorVptr = itemActorVptr;
  r.mMinecraftBase = 0x10000000u;
  r.mSeparateDropVisuals.store(true, std::memory_order_relaxed);
  R::dispatchActorRemove(
      active->actor.data(), destination->actor.data(),
      r.mMinecraftBase + itemphysics::profile::kMergeRemoveReturnRva);
  assert(removeCalls == 2);
  assert(r.mHookSignalCount == 2u);
  assert(r.mHookSignals[0].kind == R::MergeSignalKind::ExactPair);
  assert(r.mHookSignals[0].actorId ==
         static_cast<std::uint64_t>(sourceUniqueId));
  assert(r.mHookSignals[0].otherId ==
         static_cast<std::uint64_t>(destinationUniqueId));
  assert(r.mHookSignals[0].count == 2u);
  assert(r.mHookSignals[0].oldCount == 3u);
  assert(r.mHookSignals[0].newCount == 5u);
  assert(r.mHookSignals[1].kind == R::MergeSignalKind::Removed);
  assert(r.mHookSignals[1].actorId ==
         static_cast<std::uint64_t>(sourceUniqueId));
  assert(r.mHookSignals[1].count == 2u);
  assert(r.mHookSignals[1].removal.valid);

  delete destination;
  destination = nullptr;

  delete active;
  active = nullptr;
  r.uninstall();
  std::cout << "fluid runtime boundaries passed\n";
}
