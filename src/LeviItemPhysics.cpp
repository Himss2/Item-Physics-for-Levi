#include "ItemPhysicsConfig.hpp"
#include "ItemPhysicsRuntime.hpp"
#include "ItemShadowRuntime.hpp"

#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>

#include <pl/Mod.hpp>
#include <pl/ModMenu.hpp>

namespace itemphysics {

// ============================================================================
// Module
// ============================================================================

constexpr std::string_view kModuleId =
    "item_physics.main";

// ============================================================================
// Config keys
// ============================================================================

constexpr std::string_view kEnabledKey =
    "enabled";

constexpr std::string_view kSingleModelKey =
    "singleModel";

constexpr std::string_view kRotationSpeedKey =
    "rotationSpeed";

constexpr std::string_view kSettleSpeedKey =
    "settleSpeed";

constexpr std::string_view kGroundTiltKey =
    "groundTilt";

constexpr std::string_view kHeightOffsetKey =
    "heightOffset";

// ============================================================================
// Ground Height V4
// ============================================================================

constexpr std::string_view kBlockGroundHeightKey =
    "blockGroundHeight";

constexpr std::string_view kThinBlockGroundHeightKey =
    "thinBlockGroundHeight";

constexpr std::string_view kTorchGroundHeightKey =
    "torchGroundHeight";

constexpr std::string_view kShapedBlockGroundHeightKey =
    "shapedBlockGroundHeight";

constexpr std::string_view kSkullGroundHeightKey =
    "skullGroundHeight";

constexpr std::string_view kShieldGroundHeightKey =
    "shieldGroundHeight";

constexpr std::string_view kBannerGroundHeightKey =
    "bannerGroundHeight";

// ============================================================================
// Parsing helpers
// ============================================================================

bool parseBool(
    std::string_view value,
    bool fallback) {

  if (value == "true" ||
      value == "1" ||
      value == "on" ||
      value == "enabled") {

    return true;
  }

  if (value == "false" ||
      value == "0" ||
      value == "off" ||
      value == "disabled") {

    return false;
  }

  return fallback;
}

double parseDouble(
    std::string_view value,
    double fallback) {

  std::string text(
      value);

  char *end =
      nullptr;

  errno =
      0;

  const double parsed =
      std::strtod(
          text.c_str(),
          &end);

  if (end ==
          text.c_str() ||

      *end != '\0' ||

      errno ==
          ERANGE ||

      !std::isfinite(
          parsed)) {

    return fallback;
  }

  return parsed;
}

// ============================================================================
// Mod Menu value conversion
// ============================================================================

std::string boolText(
    bool value) {

  return value
             ? "true"
             : "false";
}

std::string numberText(
    double value) {

  std::ostringstream stream;

  stream <<
      value;

  return stream.str();
}

// ============================================================================
// Levi Item Physics
// ============================================================================

class LeviItemPhysicsMod {
public:
  static LeviItemPhysicsMod &
  instance() {

    static LeviItemPhysicsMod
        value;

    return value;
  }

  LeviItemPhysicsMod()
      : mSelf(
            *ll::mod::
                NativeMod::current()) {}

  // ==========================================================================
  // LOAD
  // ==========================================================================

  bool load() {

    std::lock_guard lock(
        mConfigMutex);

    mConfig.emplace();

    if (!mConfig->load()) {

      mSelf.getLogger().error(
          "Failed to load Item Physics config");

      mConfig.reset();

      return false;
    }

    normalize(
        mConfig->value());

    if (!mConfig->save()) {

      mSelf.getLogger().warn(
          "Loaded config but failed to persist normalization");
    }

    mRuntime.applyConfig(
        mConfig->value());

    // Shadow suppression follows the master Item Physics toggle.
    mShadowRuntime.setEnabled(
        mConfig->value().enabled);

    mSelf.getLogger().info(
        "Loaded Item Physics config from {}",
        mConfig->configPath().string());

    return true;
  }

  // ==========================================================================
  // ENABLE
  // ==========================================================================

  bool enable() {

    const auto snapshot =
        snapshotConfig();

    mRuntime.applyConfig(
        snapshot);

    mShadowRuntime.setEnabled(
        snapshot.enabled);

    const bool physicsHookActive =
        mRuntime.install(
            mSelf);

    // ------------------------------------------------------------------------
    // Shadow hook is deliberately separate from ItemRenderer physics.
    //
    // A shadow-hook failure does NOT disable the physics runtime.
    // ------------------------------------------------------------------------

    const bool shadowHookActive =
        mShadowRuntime.install(
            mSelf);

    // ========================================================================
    // MOD MENU
    // ========================================================================

    const bool registered =
        pl::modmenu::
            ModuleBuilder(
                std::string(
                    kModuleId),

                "Item Physics")

            // ----------------------------------------------------------------
            // Module
            // ----------------------------------------------------------------

            .modId(
                mSelf.getId())

            .description(
                "Atlas/Java-style dropped item physics with "
                "dropped-item shadow suppression.")

            .defaultEnabled(
                snapshot.enabled)

            .onToggle(
                onToggle)

            // ================================================================
            // GENERAL
            // ================================================================

            .config(
                std::string(
                    kSingleModelKey),

                "Single Model",

                pl::modmenu::
                    ConfigType::
                        Toggle,

                boolText(
                    snapshot.singleModel))

            // ================================================================
            // PHYSICS
            // ================================================================

            .config(
                std::string(
                    kRotationSpeedKey),

                "Tumble Speed",

                pl::modmenu::
                    ConfigType::
                        SliderFloat,

                numberText(
                    snapshot.rotationSpeed),

                numberText(
                    kMinRotationSpeed),

                numberText(
                    kMaxRotationSpeed))

            .config(
                std::string(
                    kSettleSpeedKey),

                "Settle Speed",

                pl::modmenu::
                    ConfigType::
                        SliderFloat,

                numberText(
                    snapshot.settleSpeed),

                numberText(
                    kMinSettleSpeed),

                numberText(
                    kMaxSettleSpeed))

            .config(
                std::string(
                    kGroundTiltKey),

                "Ground Angle",

                pl::modmenu::
                    ConfigType::
                        SliderFloat,

                numberText(
                    snapshot.groundTilt),

                numberText(
                    kMinGroundTilt),

                numberText(
                    kMaxGroundTilt))

            // ================================================================
            // ORDINARY FLAT ITEM
            // ================================================================

            .config(
                std::string(
                    kHeightOffsetKey),

                "Flat Item Height",

                pl::modmenu::
                    ConfigType::
                        SliderFloat,

                numberText(
                    snapshot.heightOffset),

                numberText(
                    kMinHeightOffset),

                numberText(
                    kMaxHeightOffset))

            // ================================================================
            // GROUND HEIGHT V4
            // ================================================================

            .config(
                std::string(
                    kBlockGroundHeightKey),

                "Block Ground Height",

                pl::modmenu::
                    ConfigType::
                        SliderFloat,

                numberText(
                    snapshot.blockGroundHeight),

                numberText(
                    kMinGroundHeight),

                numberText(
                    kMaxGroundHeight))

            .config(
                std::string(
                    kThinBlockGroundHeightKey),

                "Thin Block Height",

                pl::modmenu::
                    ConfigType::
                        SliderFloat,

                numberText(
                    snapshot.thinBlockGroundHeight),

                numberText(
                    kMinGroundHeight),

                numberText(
                    kMaxGroundHeight))

            .config(
                std::string(
                    kTorchGroundHeightKey),

                "Torch / Cross Height",

                pl::modmenu::
                    ConfigType::
                        SliderFloat,

                numberText(
                    snapshot.torchGroundHeight),

                numberText(
                    kMinGroundHeight),

                numberText(
                    kMaxGroundHeight))

            .config(
                std::string(
                    kShapedBlockGroundHeightKey),

                "Shaped Block Height",

                pl::modmenu::
                    ConfigType::
                        SliderFloat,

                numberText(
                    snapshot.shapedBlockGroundHeight),

                numberText(
                    kMinGroundHeight),

                numberText(
                    kMaxGroundHeight))

            .config(
                std::string(
                    kSkullGroundHeightKey),

                "Head / Skull Height",

                pl::modmenu::
                    ConfigType::
                        SliderFloat,

                numberText(
                    snapshot.skullGroundHeight),

                numberText(
                    kMinGroundHeight),

                numberText(
                    kMaxGroundHeight))

            .config(
                std::string(
                    kShieldGroundHeightKey),

                "Shield Height",

                pl::modmenu::
                    ConfigType::
                        SliderFloat,

                numberText(
                    snapshot.shieldGroundHeight),

                numberText(
                    kMinGroundHeight),

                numberText(
                    kMaxGroundHeight))

            .config(
                std::string(
                    kBannerGroundHeightKey),

                "Banner Height",

                pl::modmenu::
                    ConfigType::
                        SliderFloat,

                numberText(
                    snapshot.bannerGroundHeight),

                numberText(
                    kMinGroundHeight),

                numberText(
                    kMaxGroundHeight))

            // ================================================================
            // CONFIG CALLBACK
            // ================================================================

            .onConfigChanged(
                onConfigChanged)

            .registerModule();

    if (!registered) {

      mSelf.getLogger().error(
          "Failed to register Item Physics in Mod Menu");

      mShadowRuntime.uninstall();
      mRuntime.uninstall();

      return false;
    }

    mModuleRegistered =
        true;

    if (physicsHookActive) {

      mSelf.getLogger().info(
          "Item Physics renderer hook active");

    } else {

      mSelf.getLogger().warn(
          "Item Physics registered in Mod Menu, "
          "but ItemRenderer physics hook is inactive");
    }

    if (shadowHookActive) {

      mSelf.getLogger().info(
          "Dropped-item shadow suppression hook active");

    } else {

      mSelf.getLogger().warn(
          "Dropped-item shadow suppression is inactive; "
          "physics remains available");
    }

    return true;
  }

  // ==========================================================================
  // DISABLE
  // ==========================================================================

  bool disable() {

    unregisterMenu();

    mShadowRuntime.uninstall();
    mRuntime.uninstall();

    mSelf.getLogger().info(
        "Item Physics disabled");

    return true;
  }

  // ==========================================================================
  // UNLOAD
  // ==========================================================================

  bool unload() {

    unregisterMenu();

    mShadowRuntime.uninstall();
    mRuntime.uninstall();

    std::lock_guard lock(
        mConfigMutex);

    mConfig.reset();

    return true;
  }

private:
  // ==========================================================================
  // Runtime
  // ==========================================================================

  ll::mod::NativeMod &
      mSelf;

  ItemPhysicsRuntime
      mRuntime;

  // Shadow rendering is kept separate from ItemPhysicsRuntime so this change
  // cannot alter item transform, ground-height categories or landing physics.
  ItemShadowRuntime
      mShadowRuntime;

  // ==========================================================================
  // Config
  // ==========================================================================

  std::mutex
      mConfigMutex;

  std::optional<
      pl::config::
          ConfigFile<
              ItemPhysicsConfig>>
      mConfig;

  bool
      mModuleRegistered{};

  // ==========================================================================
  // Config snapshot
  // ==========================================================================

  ItemPhysicsConfig
  snapshotConfig() {

    std::lock_guard lock(
        mConfigMutex);

    if (!mConfig) {

      return {};
    }

    auto value =
        mConfig->value();

    normalize(
        value);

    return value;
  }

  // ==========================================================================
  // Persist + live apply
  // ==========================================================================

  void persistAndApplyLocked(
      std::string_view reason) {

    if (!mConfig) {

      return;
    }

    normalize(
        mConfig->value());

    mRuntime.applyConfig(
        mConfig->value());

    // Hooks remain installed while the native mod is active.
    //
    // If the user switches Item Physics OFF, shadow detours stop suppressing
    // SimpleItemShadow and immediately delegate to vanilla again.
    mShadowRuntime.setEnabled(
        mConfig->value().enabled);

    if (mConfig->save()) {

      mSelf.getLogger().info(
          "Persisted Item Physics config after {}",
          reason);

    } else {

      mSelf.getLogger().warn(
          "Failed to persist Item Physics config after {}",
          reason);
    }
  }

  // ==========================================================================
  // Static callbacks
  // ==========================================================================

  static void onToggle(
      std::string_view moduleId,
      bool enabled) {

    instance().
        handleToggle(
            moduleId,
            enabled);
  }

  static void onConfigChanged(
      std::string_view moduleId,
      std::string_view key,
      std::string_view value) {

    instance().
        handleConfigChanged(
            moduleId,
            key,
            value);
  }

  // ==========================================================================
  // Module toggle
  // ==========================================================================

  void handleToggle(
      std::string_view moduleId,
      bool enabled) {

    if (moduleId !=
        kModuleId) {

      return;
    }

    std::lock_guard lock(
        mConfigMutex);

    if (!mConfig) {

      return;
    }

    mConfig->value().enabled =
        enabled;

    persistAndApplyLocked(
        "module toggle");
  }

  // ==========================================================================
  // Config callback
  // ==========================================================================

  void handleConfigChanged(
      std::string_view moduleId,
      std::string_view key,
      std::string_view value) {

    if (moduleId !=
        kModuleId) {

      return;
    }

    std::lock_guard lock(
        mConfigMutex);

    if (!mConfig) {

      return;
    }

    auto &config =
        mConfig->value();

    // ========================================================================
    // General
    // ========================================================================

    if (key ==
        kSingleModelKey) {

      config.singleModel =
          parseBool(
              value,
              config.singleModel);
    }

    // ========================================================================
    // Physics
    // ========================================================================

    else if (
        key ==
        kRotationSpeedKey) {

      config.rotationSpeed =
          parseDouble(
              value,
              config.rotationSpeed);
    }

    else if (
        key ==
        kSettleSpeedKey) {

      config.settleSpeed =
          parseDouble(
              value,
              config.settleSpeed);
    }

    else if (
        key ==
        kGroundTiltKey) {

      config.groundTilt =
          parseDouble(
              value,
              config.groundTilt);
    }

    // ========================================================================
    // Ordinary flat-item height
    // ========================================================================

    else if (
        key ==
        kHeightOffsetKey) {

      config.heightOffset =
          parseDouble(
              value,
              config.heightOffset);
    }

    // ========================================================================
    // Ground Height V4
    // ========================================================================

    else if (
        key ==
        kBlockGroundHeightKey) {

      config.blockGroundHeight =
          parseDouble(
              value,
              config.blockGroundHeight);
    }

    else if (
        key ==
        kThinBlockGroundHeightKey) {

      config.thinBlockGroundHeight =
          parseDouble(
              value,
              config.thinBlockGroundHeight);
    }

    else if (
        key ==
        kTorchGroundHeightKey) {

      config.torchGroundHeight =
          parseDouble(
              value,
              config.torchGroundHeight);
    }

    else if (
        key ==
        kShapedBlockGroundHeightKey) {

      config.shapedBlockGroundHeight =
          parseDouble(
              value,
              config.shapedBlockGroundHeight);
    }

    else if (
        key ==
        kSkullGroundHeightKey) {

      config.skullGroundHeight =
          parseDouble(
              value,
              config.skullGroundHeight);
    }

    else if (
        key ==
        kShieldGroundHeightKey) {

      config.shieldGroundHeight =
          parseDouble(
              value,
              config.shieldGroundHeight);
    }

    else if (
        key ==
        kBannerGroundHeightKey) {

      config.bannerGroundHeight =
          parseDouble(
              value,
              config.bannerGroundHeight);
    }

    // ========================================================================
    // Enabled
    // ========================================================================

    else if (
        key ==
        kEnabledKey) {

      config.enabled =
          parseBool(
              value,
              config.enabled);
    }

    else {

      return;
    }

    // ========================================================================
    // Live apply
    // ========================================================================

    persistAndApplyLocked(
        key);
  }

  // ==========================================================================
  // Mod Menu unregister
  // ==========================================================================

  void unregisterMenu() {

    if (!mModuleRegistered) {

      return;
    }

    pl::modmenu::
        unregisterModule(
            kModuleId);

    mModuleRegistered =
        false;
  }
};

using RegisteredMod =
    LeviItemPhysicsMod;

} // namespace itemphysics

PL_REGISTER_MOD(
    itemphysics::RegisteredMod,
    itemphysics::RegisteredMod::instance())
