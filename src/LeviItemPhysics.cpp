#include "ItemPhysicsConfig.hpp"
#include "ItemPhysicsRuntime.hpp"

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

constexpr std::string_view kModuleId =
    "item_physics.main";

constexpr std::string_view kEnabledKey =
    "enabled";

constexpr std::string_view kSingleModelKey =
    "singleModel";

constexpr std::string_view kHideItemShadowKey =
    "hideItemShadow";

constexpr std::string_view kRotationSpeedKey =
    "rotationSpeed";

constexpr std::string_view kSettleSpeedKey =
    "settleSpeed";

constexpr std::string_view kGroundTiltKey =
    "groundTilt";

constexpr std::string_view kHeightOffsetKey =
    "heightOffset";

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

class LeviItemPhysicsMod {
public:
  static LeviItemPhysicsMod &
  instance() {

    static LeviItemPhysicsMod
        value;

    return
        value;
  }

  LeviItemPhysicsMod()
      : mSelf(
            *ll::mod::
                NativeMod::current()) {}

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

    mSelf.getLogger().info(
        "Loaded Item Physics config from {}",
        mConfig->configPath().string());

    return true;
  }

  bool enable() {

    const auto snapshot =
        snapshotConfig();

    mRuntime.applyConfig(
        snapshot);

    const bool hookActive =
        mRuntime.install(
            mSelf);

    const bool registered =
        pl::modmenu::
            ModuleBuilder(
                std::string(
                    kModuleId),

                "Item Physics")

            .modId(
                mSelf.getId())

            .description(
                "Atlas/Java-style dropped item physics. "
                "Client-side renderer only.")

            .defaultEnabled(
                snapshot.enabled)

            .onToggle(
                onToggle)

            .config(
                std::string(
                    kSingleModelKey),

                "Single Model",

                pl::modmenu::
                    ConfigType::
                        Toggle,

                boolText(
                    snapshot.singleModel))

            .config(
                std::string(
                    kHideItemShadowKey),

                "Hide Item Shadow",

                pl::modmenu::
                    ConfigType::
                        Toggle,

                boolText(
                    snapshot.hideItemShadow))

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

            .onConfigChanged(
                onConfigChanged)

            .registerModule();

    if (!registered) {

      mSelf.getLogger().error(
          "Failed to register Item Physics in Mod Menu");

      mRuntime.uninstall();

      return false;
    }

    mModuleRegistered =
        true;

    if (hookActive) {

      mSelf.getLogger().info(
          "Item Physics enabled and registered in Mod Menu "
          "with Ground Height V4 sliders + Item Shadow toggle");

    } else {

      mSelf.getLogger().warn(
          "Item Physics registered in Mod Menu, "
          "but runtime hook is inactive for this Minecraft binary");
    }

    return true;
  }

  bool disable() {

    unregisterMenu();

    mRuntime.uninstall();

    mSelf.getLogger().info(
        "Item Physics disabled");

    return true;
  }

  bool unload() {

    unregisterMenu();

    mRuntime.uninstall();

    std::lock_guard lock(
        mConfigMutex);

    mConfig.reset();

    return true;
  }

private:

  ll::mod::NativeMod &
      mSelf;

  ItemPhysicsRuntime
      mRuntime;

  std::mutex
      mConfigMutex;

  std::optional<
      pl::config::
          ConfigFile<
              ItemPhysicsConfig>>
      mConfig;

  bool
      mModuleRegistered{};

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

    return
        value;
  }

  void persistAndApplyLocked(
      std::string_view reason) {

    if (!mConfig) {

      return;
    }

    normalize(
        mConfig->value());

    mRuntime.applyConfig(
        mConfig->value());

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

    if (key ==
        kSingleModelKey) {

      config.singleModel =
          parseBool(
              value,
              config.singleModel);
    }

    else if (
        key ==
        kHideItemShadowKey) {

      config.hideItemShadow =
          parseBool(
              value,
              config.hideItemShadow);
    }

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

    else if (
        key ==
        kHeightOffsetKey) {

      config.heightOffset =
          parseDouble(
              value,
              config.heightOffset);
    }

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

    persistAndApplyLocked(
        key);
  }

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
