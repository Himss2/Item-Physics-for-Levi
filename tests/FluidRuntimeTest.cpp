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

namespace {
using R = itemphysics::ItemPhysicsRuntime;
template <class T, std::size_t N>
void put(std::array<std::byte, N> &bytes, std::size_t at, T value) {
  assert(at + sizeof(T) <= N);
  std::memcpy(bytes.data() + at, &value, sizeof(T));
}

// Actual profile layout at the native boundary: a registry hash table with
// water/lava and contact sparse component pages, plus an ItemActor header.
struct Fixture {
  alignas(16) std::array<std::byte, 0x450> actor{};
  alignas(16) std::array<std::byte, 0x60> registry{};
  std::array<std::int64_t, 8> buckets{};
  alignas(16) std::array<std::byte, 128> nodes{};
  alignas(16) std::array<std::byte, 0x18> waterStorage{}, lavaStorage{},
      groundStorage{}, collisionStorage{};
  std::array<std::uintptr_t, 1> waterPages{}, lavaPages{}, groundPages{},
      collisionPages{};
  std::array<std::uint32_t, 2048> waterPage{}, lavaPage{}, groundPage{},
      collisionPage{};
  R::Vec3Abi motion{0.1f, -0.04f, -0.2f};
  R::Vec3Abi current{2.0f, 60.0f, 3.0f};
  R::Vec3Abi previous{2.0f, 60.0f, 3.0f};
  static constexpr std::uint32_t entity = 17;
  static constexpr std::uint32_t waterHash = 0x78E89F39;
  static constexpr std::uint32_t lavaHash = 0x832A2768;
  Fixture() {
    buckets.fill(-1);
    waterPage.fill(0xFFFFFFFFu);
    lavaPage.fill(0xFFFFFFFFu);
    groundPage.fill(0xFFFFFFFFu);
    collisionPage.fill(0xFFFFFFFFu);
    lavaPage[entity] = entity;
    waterPages[0] = reinterpret_cast<std::uintptr_t>(waterPage.data());
    lavaPages[0] = reinterpret_cast<std::uintptr_t>(lavaPage.data());
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
    add(1, itemphysics::profile::kOnGroundFlagComponentHash, groundStorage,
        groundPages);
    add(2, itemphysics::profile::kVerticalCollisionFlagComponentHash,
        collisionStorage, collisionPages);
    add(3, waterHash, waterStorage, waterPages);
    put(registry, 0x38, reinterpret_cast<std::uintptr_t>(buckets.data()));
    put(registry, 0x40, reinterpret_cast<std::uintptr_t>(buckets.data() + 8));
    put(registry, 0x50, reinterpret_cast<std::uintptr_t>(nodes.data()));
    put(registry, 0x58, reinterpret_cast<std::uintptr_t>(nodes.data() + 128));
    put(actor, 0x10, static_cast<void *>(registry.data()));
    put(actor, 0x18, entity);
  }
};

Fixture *active{};
unsigned motionReads{};
unsigned normalTickCalls{};
bool clientSide{};
bool fireResistant{true};
bool removeDuringTick{};
unsigned removeCalls{};
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
  assert(active && actor == active->actor.data());
  static const std::int64_t id = 0x10203040;
  return &id;
}
bool isClientSide(const void *actor) {
  assert(active && actor == active->actor.data());
  return clientSide;
}
bool isFireResistant(const void *stack) {
  assert(active && stack == active->actor.data() +
                                itemphysics::profile::kItemStackBaseOffset);
  return fireResistant;
}
void nativeNormalTick(void *actor) {
  assert(active && actor == active->actor.data());
  ++normalTickCalls;
  if (removeDuringTick)
    R::dispatchActorRemove(actor, nullptr, 0);
}
void nativeRemove(void *actor) {
  assert(active && actor == active->actor.data());
  ++removeCalls;
}
void configure(R &r) {
  R::sInstance = &r;
  r.mProfileSupported.store(true);
  r.mGetPosDelta = getMotion;
  r.mGetActorPosition = getCurrent;
  r.mGetActorPreviousPosition = getPrevious;
  r.mGetActorUniqueId = getUniqueId;
}
}

int main() {
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

  // Every render route keeps the device-approved 0.15.x surface lift so flat,
  // shaped, special and full-block models share one water/lava baseline.
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
  assert(std::abs(r.renderWorldY(64.0f, flat, false, F::Lava,
                                40.0f, 0, false) - 64.125f) < 0.00001f);
  assert(std::abs(r.renderWorldY(64.0f, full, false, F::Lava,
                                40.0f, 0, false) - 64.125f) < 0.00001f);

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

  // Once native ascent and stability have confirmed the surface, a missing
  // WasInLava flag must not make ordinary models suddenly fall back to their
  // dry render height. A real vertical wake still clears the retained class.
  active->groundPage[Fixture::entity] = 0xFFFFFFFFu;
  active->collisionPage[Fixture::entity] = 0xFFFFFFFFu;
  active->lavaPage[Fixture::entity] = Fixture::entity;
  active->current.y = 60.0f;
  active->motion.y = -0.08f;
  R::VisualState surface{};
  assert(!r.resolveGrounded(surface, active->actor.data(), 20, 60.0f));
  (void)surface.fluidBase.update(60.0f, 60.0f, -0.08f, 20,
                                 F::Lava, false);
  active->current.y = 59.85f;
  (void)surface.fluidBase.update(59.85f, 59.85f, -0.08f, 21,
                                 F::Lava, false);
  active->current.y = 59.95f;
  (void)surface.fluidBase.update(59.95f, 59.95f, 0.10f, 22,
                                 F::Lava, false);
  assert(surface.fluidBase.surfaceCandidate());
  assert(!surface.fluidBase.bobbing());
  active->lavaPage[Fixture::entity] = 0xFFFFFFFFu;
  active->current.y = 60.0f;
  active->motion.y = 0.0f;
  for (int age = 23; age < 33; ++age) {
    assert(!r.resolveGrounded(surface, active->actor.data(), age, 60.0f));
    (void)surface.fluidBase.update(60.0f, 60.0f, 0.0f, age,
                                   F::Lava, false);
  }
  assert(surface.fluidBase.bobbing());
  assert(surface.fluid == F::Lava);
  active->motion.y = 0.10f;
  assert(!r.resolveGrounded(surface, active->actor.data(), 33, 60.0f));
  assert(surface.fluid == F::None);

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
  assert(r.resolveGrounded(collided, active->actor.data(), 43, -8.0f));
  assert(collided.groundedRenderTicks == 2u);

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

  // The recovery calls native normalTick exactly once and remains dormant
  // through native descent. Water gets one release impulse after three stable
  // bottom ticks even for an ordinary item; it is not a persistent floor.
  constexpr std::uintptr_t itemActorVptr = 0x12345678u;
  put(active->actor, 0, itemActorVptr);
  r.mItemActorVptr = itemActorVptr;
  r.mNormalTickOriginal = nativeNormalTick;
  r.mIsClientSide = isClientSide;
  r.mIsFireResistant = isFireResistant;
  active->lavaPage[Fixture::entity] = 0xFFFFFFFFu;
  active->waterPage[Fixture::entity] = Fixture::entity;
  active->groundPage[Fixture::entity] = 0xFFFFFFFFu;
  active->collisionPage[Fixture::entity] = 0xFFFFFFFFu;
  const auto tick = [&](int age, float worldY, float speed,
                        bool bottom = false) {
    put(active->actor, itemphysics::profile::kItemAgeOffset, age);
    active->current.y = worldY;
    active->motion.y = speed;
    active->groundPage[Fixture::entity] =
        bottom ? Fixture::entity : 0xFFFFFFFFu;
    active->collisionPage[Fixture::entity] =
        bottom ? Fixture::entity : 0xFFFFFFFFu;
    r.onNormalTick(active->actor.data());
  };
  normalTickCalls = 0;
  fireResistant = false;
  tick(1, 64.0f, -0.10f);
  tick(2, 63.7f, -0.08f);
  tick(3, 63.2f, -0.04f, true);
  tick(4, 63.2f, -0.04f, true);
  assert(active->motion.y == -0.04f);
  tick(5, 63.2f, -0.04f, true);
  assert(std::abs(active->motion.y - 0.06f) < 1e-6f);
  assert(normalTickCalls == 5);

  // Clearing contact after the water impulse leaves the next native speed
  // untouched. Switching to lava resets the detector; only a fire-resistant
  // local actor receives the one-shot release.
  tick(6, 63.26f, 0.03f);
  assert(std::abs(active->motion.y - 0.03f) < 1e-6f);
  active->waterPage[Fixture::entity] = 0xFFFFFFFFu;
  active->lavaPage[Fixture::entity] = Fixture::entity;
  fireResistant = true;
  tick(10, 64.0f, -0.10f);
  tick(11, 63.7f, -0.08f);
  tick(12, 63.2f, -0.04f, true);
  tick(13, 63.2f, -0.04f, true);
  tick(14, 63.2f, -0.04f, true);
  assert(std::abs(active->motion.y - 0.06f) < 1e-6f);
  tick(15, 63.26f, 0.02f);
  assert(std::abs(active->motion.y - 0.02f) < 1e-6f);

  fireResistant = false;
  tick(20, 64.0f, -0.10f);
  tick(21, 63.7f, -0.08f);
  tick(22, 63.2f, -0.04f, true);
  tick(23, 63.2f, -0.04f, true);
  tick(24, 63.2f, -0.04f, true);
  assert(active->motion.y == -0.04f);
  fireResistant = true;

  clientSide = true;
  tick(25, 63.2f, -0.02f, true);
  assert(active->motion.y == -0.02f);
  clientSide = false;
  r.mActorRemoveOriginal = nativeRemove;
  removeDuringTick = true;
  tick(26, 63.2f, -0.03f, true);
  removeDuringTick = false;
  assert(removeCalls == 1);
  assert(active->motion.y == -0.03f);

  delete active;
  active = nullptr;
  r.uninstall();
  std::cout << "fluid runtime boundaries passed\n";
}
