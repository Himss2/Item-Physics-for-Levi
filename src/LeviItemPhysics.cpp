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

constexpr std::string_view kModuleId = "item_physics.main";
constexpr std::string_view kEnabledKey = "enabled";
constexpr std::string_view kSingleModelKey = "singleModel";
constexpr std::string_view kHideItemShadowKey = "hideItemShadow";
constexpr std::string_view kRotationSpeedKey = "rotationSpeed";
constexpr std::string_view kOldRotationKey = "oldRotation";

bool parseBool(std::string_view value,bool fallback) {
  if (value=="true"||value=="1"||value=="on"||value=="enabled")
    return true;

  if (value=="false"||value=="0"||value=="off"||value=="disabled")
    return false;

  return fallback;
}

double parseDouble(std::string_view value,double fallback) {
  std::string text(value);
  char *end=nullptr;
  errno=0;

  const double parsed=std::strtod(text.c_str(),&end);

  if (end==text.c_str()||
      *end!='\0'||
      errno==ERANGE||
      !std::isfinite(parsed))
    return fallback;

  return parsed;
}

std::string boolText(bool value) {
  return value?"true":"false";
}

std::string numberText(double value) {
  std::ostringstream stream;
  stream<<value;
  return stream.str();
}

class LeviItemPhysicsMod {
public:
  static LeviItemPhysicsMod &instance() {
    static LeviItemPhysicsMod value;
    return value;
  }

  LeviItemPhysicsMod()
      :mSelf(*ll::mod::NativeMod::current()) {}

  bool load() {
    std::lock_guard lock(mConfigMutex);
    mConfig.emplace();

    if (!mConfig->load()) {
      mSelf.getLogger().error(
          "Failed to load Item Physics config");

      mConfig.reset();
      return false;
    }

    normalize(mConfig->value());

    if (!mConfig->save()) {
      mSelf.getLogger().warn(
          "Loaded config but failed to persist normalization");
    }

    mRuntime.applyConfig(mConfig->value());

    mSelf.getLogger().info(
        "Loaded Item Physics config from {}",
        mConfig->configPath().string());

    return true;
  }

  bool enable() {
    const auto snapshot=snapshotConfig();

    mRuntime.applyConfig(snapshot);

    const bool hookActive=
        mRuntime.install(mSelf);

    const bool registered=
        pl::modmenu::ModuleBuilder(
            std::string(kModuleId),
            "Item Physics")

        .modId(mSelf.getId())

        .description(
            "Java ItemPhysic-style dropped item physics with Bedrock renderer adaptation.")

        .defaultEnabled(snapshot.enabled)

        .onToggle(onToggle)

        .config(
            std::string(kSingleModelKey),
            "Single Model",
            pl::modmenu::ConfigType::Toggle,
            boolText(snapshot.singleModel))

        .config(
            std::string(kHideItemShadowKey),
            "Hide Item Shadow",
            pl::modmenu::ConfigType::Toggle,
            boolText(snapshot.hideItemShadow))

        .config(
            std::string(kRotationSpeedKey),
            "Tumble Speed",
            pl::modmenu::ConfigType::SliderFloat,
            numberText(snapshot.rotationSpeed),
            numberText(kMinRotationSpeed),
            numberText(kMaxRotationSpeed))

        .config(
            std::string(kOldRotationKey),
            "Old Rotation",
            pl::modmenu::ConfigType::Toggle,
            boolText(snapshot.oldRotation))

        .onConfigChanged(onConfigChanged)

        .registerModule();

    if (!registered) {
      mSelf.getLogger().error(
          "Failed to register Item Physics in Mod Menu");

      mRuntime.uninstall();
      return false;
    }

    mModuleRegistered=true;

    if (hookActive) {
      mSelf.getLogger().info(
          "Item Physics enabled: Java physics + native yaw + model pivot");
    } else {
      mSelf.getLogger().warn(
          "Item Physics registered in Mod Menu, but runtime hook is inactive for this Minecraft binary");
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

    std::lock_guard lock(mConfigMutex);
    mConfig.reset();

    return true;
  }

private:
  ll::mod::NativeMod &mSelf;
  ItemPhysicsRuntime mRuntime;
  std::mutex mConfigMutex;

  std::optional<
      pl::config::ConfigFile<ItemPhysicsConfig>>
      mConfig;

  bool mModuleRegistered{};

  ItemPhysicsConfig snapshotConfig() {
    std::lock_guard lock(mConfigMutex);

    if (!mConfig)
      return {};

    auto value=mConfig->value();

    normalize(value);

    return value;
  }

  void persistAndApplyLocked(
      std::string_view reason) {

    if (!mConfig)
      return;

    normalize(mConfig->value());

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

    instance().handleToggle(
        moduleId,
        enabled);
  }

  static void onConfigChanged(
      std::string_view moduleId,
      std::string_view key,
      std::string_view value) {

    instance().handleConfigChanged(
        moduleId,
        key,
        value);
  }

  void handleToggle(
      std::string_view moduleId,
      bool enabled) {

    if (moduleId!=kModuleId)
      return;

    std::lock_guard lock(mConfigMutex);

    if (!mConfig)
      return;

    mConfig->value().enabled=enabled;

    persistAndApplyLocked(
        "module toggle");
  }

  void handleConfigChanged(
      std::string_view moduleId,
      std::string_view key,
      std::string_view value) {

    if (moduleId!=kModuleId)
      return;

    std::lock_guard lock(mConfigMutex);

    if (!mConfig)
      return;

    auto &config=mConfig->value();

    if (key==kSingleModelKey) {
      config.singleModel=
          parseBool(
              value,
              config.singleModel);
    }

    else if (key==kHideItemShadowKey) {
      config.hideItemShadow=
          parseBool(
              value,
              config.hideItemShadow);
    }

    else if (key==kRotationSpeedKey) {
      config.rotationSpeed=
          parseDouble(
              value,
              config.rotationSpeed);
    }

    else if (key==kOldRotationKey) {
      config.oldRotation=
          parseBool(
              value,
              config.oldRotation);
    }

    else if (key==kEnabledKey) {
      config.enabled=
          parseBool(
              value,
              config.enabled);
    }

    else {
      return;
    }

    persistAndApplyLocked(key);
  }

  void unregisterMenu() {
    if (!mModuleRegistered)
      return;

    pl::modmenu::unregisterModule(
        kModuleId);

    mModuleRegistered=false;
  }
};

using RegisteredMod=LeviItemPhysicsMod;

}

PL_REGISTER_MOD(
    itemphysics::RegisteredMod,
    itemphysics::RegisteredMod::instance())
