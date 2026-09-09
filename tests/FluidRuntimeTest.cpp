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
// lava and on-ground sparse component pages, plus an ItemActor header.
struct Fixture {
  alignas(16) std::array<std::byte, 0x450> actor{};
  alignas(16) std::array<std::byte, 0x60> registry{};
  std::array<std::int64_t, 8> buckets{};
  alignas(16) std::array<std::byte, 64> nodes{};
  alignas(16) std::array<std::byte, 0x18> lavaStorage{}, groundStorage{};
  std::array<std::uintptr_t, 1> lavaPages{}, groundPages{};
  std::array<std::uint32_t, 2048> lavaPage{}, groundPage{};
  R::Vec3Abi motion{0.1f, -0.04f, -0.2f};
  static constexpr std::uint32_t entity = 17;
  static constexpr std::uint32_t lavaHash = 0x832A2768;
  Fixture() {
    buckets.fill(-1);
    lavaPage.fill(0xFFFFFFFFu);
    groundPage.fill(0xFFFFFFFFu);
    lavaPage[entity] = entity;
    lavaPages[0] = reinterpret_cast<std::uintptr_t>(lavaPage.data());
    groundPages[0] = reinterpret_cast<std::uintptr_t>(groundPage.data());
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
    put(registry, 0x38, reinterpret_cast<std::uintptr_t>(buckets.data()));
    put(registry, 0x40, reinterpret_cast<std::uintptr_t>(buckets.data() + 8));
    put(registry, 0x50, reinterpret_cast<std::uintptr_t>(nodes.data()));
    put(registry, 0x58, reinterpret_cast<std::uintptr_t>(nodes.data() + 64));
    put(actor, 0x10, static_cast<void *>(registry.data()));
    put(actor, 0x18, entity);
  }
};

Fixture *active{};
unsigned motionReads{};
const R::Vec3Abi *getMotion(const void *actor) {
  assert(active && actor == active->actor.data());
  ++motionReads;
  return &active->motion;
}
void configure(R &r) {
  R::sInstance = &r;
  r.mProfileSupported.store(true);
  r.mGetPosDelta = getMotion;
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

  // Full blocks keep the approved surface lift. Flatter/shaped models use a
  // lower liquid support so they do not hover above the water plane.
  R::ItemRenderTraits flat{};
  flat.height = H::FlatItem;
  R::ItemRenderTraits full{};
  full.height = H::FullBlock;
  assert(std::abs(r.renderWorldY(64.0f, flat, false, F::Water,
                                40.0f, 0) - 64.100f) < 0.00001f);
  assert(std::abs(r.renderWorldY(64.0f, full, false, F::Water,
                                40.0f, 0) - 64.140f) < 0.00001f);

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

  delete active;
  active = nullptr;
  r.uninstall();
  std::cout << "fluid runtime boundaries passed\n";
}
