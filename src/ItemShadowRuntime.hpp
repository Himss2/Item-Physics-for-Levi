#pragma once

#include "RttiResolver.hpp"
#include "TargetProfile.hpp"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>

#include <pl/Mod.hpp>
#include <pl/memory/Hook.hpp>

namespace itemphysics {

// ============================================================================
// ItemShadowRuntime
//
// Minecraft Bedrock 1.26.45.1 ARM64
//
// Purpose:
//   suppress ONLY the SimpleItemShadow frame-renderer component used by the
//   ordinary blob/disc shadow under dropped items.
//
// Not touched:
//   - ActorShadow
//   - SimpleShadow
//   - DynamicShadowCaster
//   - StaticShadowCaster
//   - ItemRenderer transforms
//   - dropped-item physics
//
// This class is intentionally header-only so the existing CMakeLists.txt does
// not need another source entry.
// ============================================================================

class ItemShadowRuntime {
public:
  ItemShadowRuntime() = default;

  ItemShadowRuntime(
      const ItemShadowRuntime &) = delete;

  ItemShadowRuntime &
  operator=(
      const ItemShadowRuntime &) = delete;

  // ==========================================================================
  // Master state
  //
  // Hooks stay installed while the native mod is enabled. When Item Physics
  // is toggled OFF in Mod Menu, the detours simply call the original Minecraft
  // predicates so dropped-item shadows return immediately.
  // ==========================================================================

  void setEnabled(
      bool enabled) noexcept {

    mEnabled.store(
        enabled,
        std::memory_order_relaxed);
  }

  [[nodiscard]]
  bool installed() const noexcept {

    return mInstalled.load(
        std::memory_order_relaxed);
  }

  // ==========================================================================
  // Install
  // ==========================================================================

  bool install(
      ll::mod::NativeMod &mod) {

    uninstall();

    auto module =
        findLoadedModule(
            profile::
                kMinecraftModule);

    if (!module) {

      mod.getLogger().warn(
          "Dropped-item shadow removal inactive: "
          "libminecraftpe.so was not found");

      return false;
    }

    mMinecraftBase =
        module->base;

    const std::uintptr_t simpleTarget =
        mMinecraftBase +
        kSimpleItemShadowPredicateRva;

    const std::uintptr_t atlassedTarget =
        mMinecraftBase +
        kAtlassedSimpleItemShadowPredicateRva;

    // ------------------------------------------------------------------------
    // Fingerprint both exact SimpleItemShadow helpers before hooking.
    //
    // A later Minecraft update therefore cannot turn these hard-coded RVAs
    // into an accidental call to unrelated code.
    // ------------------------------------------------------------------------

    if (!verifyWords(
            *module,
            simpleTarget,
            kSimpleItemShadowPredicateFingerprint)) {

      mod.getLogger().warn(
          "Dropped-item shadow removal safe passthrough: "
          "SimpleItemShadow predicate fingerprint mismatch");

      mMinecraftBase =
          0;

      return false;
    }

    if (!verifyWords(
            *module,
            atlassedTarget,
            kAtlassedSimpleItemShadowPredicateFingerprint)) {

      mod.getLogger().warn(
          "Dropped-item shadow removal safe passthrough: "
          "atlassed SimpleItemShadow predicate fingerprint mismatch");

      mMinecraftBase =
          0;

      return false;
    }

    sInstance =
        this;

    mSimpleItemShadowOriginal =
        nullptr;

    mAtlassedSimpleItemShadowOriginal =
        nullptr;

    // ------------------------------------------------------------------------
    // Normal / non-atlassed SimpleItemShadow pass.
    // ------------------------------------------------------------------------

    mSimpleItemShadowHook =
        std::make_unique<
            pl::memory::HookHandle>(

            reinterpret_cast<void *>(
                simpleTarget),

            reinterpret_cast<void *>(
                &ItemShadowRuntime::
                    simpleItemShadowDetour),

            reinterpret_cast<void **>(
                &mSimpleItemShadowOriginal),

            pl::memory::
                HookPriority::Normal);

    if (!mSimpleItemShadowHook->installed() ||
        !mSimpleItemShadowOriginal) {

      mod.getLogger().warn(
          "Dropped-item shadow removal inactive: "
          "failed to hook SimpleItemShadow predicate");

      uninstall();

      return false;
    }

    // ------------------------------------------------------------------------
    // Atlassed SimpleItemShadow pass.
    //
    // Minecraft has a second extraction path when the shadow object carries
    // AtlassedComponent. Hooking both paths prevents the blob shadow from
    // reappearing under a different renderer path.
    // ------------------------------------------------------------------------

    mAtlassedSimpleItemShadowHook =
        std::make_unique<
            pl::memory::HookHandle>(

            reinterpret_cast<void *>(
                atlassedTarget),

            reinterpret_cast<void *>(
                &ItemShadowRuntime::
                    atlassedSimpleItemShadowDetour),

            reinterpret_cast<void **>(
                &mAtlassedSimpleItemShadowOriginal),

            pl::memory::
                HookPriority::Normal);

    if (!mAtlassedSimpleItemShadowHook->installed() ||
        !mAtlassedSimpleItemShadowOriginal) {

      mod.getLogger().warn(
          "Dropped-item shadow removal inactive: "
          "failed to hook atlassed SimpleItemShadow predicate");

      uninstall();

      return false;
    }

    mInstalled.store(
        true,
        std::memory_order_relaxed);

    mod.getLogger().info(
        "Dropped-item SimpleItemShadow suppression active");

    return true;
  }

  // ==========================================================================
  // Uninstall
  // ==========================================================================

  void uninstall() {

    mInstalled.store(
        false,
        std::memory_order_relaxed);

    // Reverse installation order.
    if (mAtlassedSimpleItemShadowHook) {

      mAtlassedSimpleItemShadowHook->reset();

      mAtlassedSimpleItemShadowHook.reset();
    }

    if (mSimpleItemShadowHook) {

      mSimpleItemShadowHook->reset();

      mSimpleItemShadowHook.reset();
    }

    mAtlassedSimpleItemShadowOriginal =
        nullptr;

    mSimpleItemShadowOriginal =
        nullptr;

    mMinecraftBase =
        0;

    if (sInstance ==
        this) {

      sInstance =
          nullptr;
    }
  }

private:
  // ==========================================================================
  // ABI
  //
  // Both RE targets are std::__function predicate operator() implementations.
  //
  // AArch64 use observed:
  //
  //   X0 = predicate wrapper / this
  //   X1 = candidate/filter context
  //   W0 = bool result
  // ==========================================================================

  using ShadowPredicateFn =
      bool (*)(
          void *,
          void *);

  // ==========================================================================
  // RE target #1
  //
  // Exact template instance:
  //
  // dragon::framerenderer::drawutils::extractAndDrawWithExclusion<
  //     defaultpasses::Opaque,
  //     mce::framebuilder::gamecomponents::SimpleItemShadow,
  //     ...>
  //
  // Predicate operator() RVA:
  //
  //   0x106AE260
  //
  // The function pointer has one relocation in this binary and belongs to
  // the vtable for this exact SimpleItemShadow std::__function type.
  // ==========================================================================

  inline static constexpr std::uintptr_t
      kSimpleItemShadowPredicateRva =
          0x106AE260;

  // ==========================================================================
  // RE target #2
  //
  // Exact template instance:
  //
  // dragon::framerenderer::drawutils::extractAndDrawWithFilter<
  //     AtlassedComponent,
  //     defaultpasses::Opaque,
  //     mce::framebuilder::gamecomponents::SimpleItemShadow,
  //     ...>
  //
  // Predicate operator() RVA:
  //
  //   0x106C2088
  //
  // This function pointer is also unique to its exact template vtable.
  // ==========================================================================

  inline static constexpr std::uintptr_t
      kAtlassedSimpleItemShadowPredicateRva =
          0x106C2088;

  // ==========================================================================
  // Fingerprints - Minecraft 1.26.45.1
  // ==========================================================================

  inline static constexpr std::array<
      std::uint32_t,
      12>
      kSimpleItemShadowPredicateFingerprint = {

          0xF9400408u,
          0xA9402C2Au,
          0xA9402109u,
          0xEB08013Fu,

          0xFA401944u,
          0xFA401964u,
          0x54000220u,
          0x3940014Cu,

          0x360001ECu,
          0xF940016Bu,
          0xB40001ABu,
          0x7940214Au,
  };

  inline static constexpr std::array<
      std::uint32_t,
      12>
      kAtlassedSimpleItemShadowPredicateFingerprint = {

          0xD10103FFu,
          0xA9017BFDu,
          0xA90257F6u,
          0xA9034FF4u,

          0x910043FDu,
          0xD53BD053u,
          0xF9401668u,
          0xF90007E8u,

          0xF9400028u,
          0xB40007C8u,
          0xF940042Au,
          0xB400078Au,
  };

  // ==========================================================================
  // Fingerprint verifier
  // ==========================================================================

  template <std::size_t N>
  [[nodiscard]]
  static bool verifyWords(
      const ModuleView &module,
      std::uintptr_t address,
      const std::array<std::uint32_t, N> &fingerprint) noexcept {

    const std::size_t bytes =
        N *
        sizeof(std::uint32_t);

    if (!module.readable(
            address,
            bytes) ||
        !module.executable(
            address)) {

      return false;
    }

    const auto *words =
        reinterpret_cast<
            const std::uint32_t *>(
            address);

    for (std::size_t i = 0;
         i < N;
         ++i) {

      if (words[i] !=
          fingerprint[i]) {

        return false;
      }
    }

    return true;
  }

  // ==========================================================================
  // Detours
  //
  // Original predicate semantics:
  //
  //   true  = candidate passes the draw filter
  //   false = candidate is excluded
  //
  // Therefore returning false on the exact SimpleItemShadow pass removes only
  // that component from rendering.
  //
  // No Actor pointer, ItemActor state, matrix, or global shadow flag is
  // modified.
  // ==========================================================================

  static bool simpleItemShadowDetour(
      void *self,
      void *candidate) {

    auto *instance =
        sInstance;

    if (instance &&
        instance->shouldSuppress()) {

      return false;
    }

    const auto original =
        instance
            ? instance->mSimpleItemShadowOriginal
            : nullptr;

    return original
               ? original(
                     self,
                     candidate)
               : false;
  }

  static bool atlassedSimpleItemShadowDetour(
      void *self,
      void *candidate) {

    auto *instance =
        sInstance;

    if (instance &&
        instance->shouldSuppress()) {

      return false;
    }

    const auto original =
        instance
            ? instance->mAtlassedSimpleItemShadowOriginal
            : nullptr;

    return original
               ? original(
                     self,
                     candidate)
               : false;
  }

  [[nodiscard]]
  bool shouldSuppress() const noexcept {

    return mEnabled.load(
        std::memory_order_relaxed);
  }

  // ==========================================================================
  // State
  // ==========================================================================

  inline static ItemShadowRuntime *
      sInstance =
          nullptr;

  std::atomic_bool
      mEnabled{true};

  std::atomic_bool
      mInstalled{false};

  std::uintptr_t
      mMinecraftBase{};

  ShadowPredicateFn
      mSimpleItemShadowOriginal{};

  ShadowPredicateFn
      mAtlassedSimpleItemShadowOriginal{};

  std::unique_ptr<
      pl::memory::HookHandle>
      mSimpleItemShadowHook;

  std::unique_ptr<
      pl::memory::HookHandle>
      mAtlassedSimpleItemShadowHook;
};

} // namespace itemphysics
