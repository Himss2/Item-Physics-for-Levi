#include "ItemPhysicsRuntime.hpp"
#include "ItemPhysicsConfig.hpp"

#include <mutex>
#include <optional>
#include <string>
#include <string_view>

#include <pl/Mod.hpp>
#include <pl/ModMenu.hpp>

namespace itemphysics {

constexpr std::string_view kModuleId = "item_physics.main";
constexpr std::string_view kSingleModelKey = "singleModel";
constexpr std::string_view kSeparateDropVisualsKey = "separateDropVisuals";
constexpr std::string_view kHideItemShadowKey = "hideItemShadow";

bool parseMenuBool(std::string_view value, bool fallback) noexcept {
  if (value == "true" || value == "1" || value == "on" ||
      value == "enabled")
    return true;
  if (value == "false" || value == "0" || value == "off" ||
      value == "disabled")
    return false;
  return fallback;
}

class LeviItemPhysicsMod {
public:
  static LeviItemPhysicsMod &instance() {
    static LeviItemPhysicsMod value;
    return value;
  }

  LeviItemPhysicsMod() : mSelf(*ll::mod::NativeMod::current()) {}

  bool load() {
    std::scoped_lock lock(mConfigMutex);
    mConfigFile.emplace();
    if (!mConfigFile->load()) {
      mSelf.getLogger().error("Failed to load Item Physics config");
      mConfigFile.reset();
      return false;
    }
    normalize(mConfigFile->value());
    if (!mConfigFile->save()) {
      mSelf.getLogger().error("Failed to persist Item Physics config");
      mConfigFile.reset();
      return false;
    }
    mSelf.getLogger().info(
        "Levi Item Physics 0.16.7: Minecraft 1.26.51.1 compatibility profile loaded");
    return true;
  }

  bool enable() {
    // A profile mismatch is intentionally non-fatal: the mod remains loaded and
    // performs no writes or hooks against an unknown Minecraft binary.
    const auto settings = configSnapshot();
    mRuntime.setEnabled(true);
    mRuntime.setSingleModel(settings.singleModel);
    mRuntime.setSeparateDropVisuals(settings.separateDropVisuals);
    mRuntime.setHideItemShadow(settings.hideItemShadow);
    const bool hookActive = mRuntime.install(mSelf);
    if (!hookActive)
      mSelf.getLogger().warn(
          "Levi Item Physics is loaded but inactive on this Minecraft binary");

    const bool registered =
        pl::modmenu::ModuleBuilder(std::string(kModuleId), "Item Physics")
            .modId(mSelf.getId())
            .description(
                "Java ItemPhysic 1.8.15-style dropped-item rendering.")
            .defaultEnabled(true)
            .onToggle(onToggle)
            .config(std::string(kSingleModelKey), "Single Model",
                    pl::modmenu::ConfigType::Toggle,
                    boolText(settings.singleModel))
            .config(std::string(kSeparateDropVisualsKey),
                    "Separate Drop Visuals",
                    pl::modmenu::ConfigType::Toggle,
                    boolText(settings.separateDropVisuals))
            .config(std::string(kHideItemShadowKey), "Hide Item Shadow",
                    pl::modmenu::ConfigType::Toggle,
                    boolText(settings.hideItemShadow))
            .onConfigChanged(onConfigChanged)
            .registerModule();
    if (!registered) {
      mSelf.getLogger().error("Failed to register Item Physics in Mod Menu");
      mRuntime.uninstall();
      return false;
    }
    mModuleRegistered = true;
    return true;
  }

  bool disable() {
    unregisterMenu();
    mRuntime.uninstall();
    mSelf.getLogger().info("Levi Item Physics disabled");
    return true;
  }

  bool unload() {
    unregisterMenu();
    mRuntime.uninstall();
    std::scoped_lock lock(mConfigMutex);
    mConfigFile.reset();
    return true;
  }

private:
  static std::string boolText(bool value) {
    return value ? "true" : "false";
  }

  ItemPhysicsConfig configSnapshot() {
    std::scoped_lock lock(mConfigMutex);
    return mConfigFile ? mConfigFile->value() : ItemPhysicsConfig{};
  }

  static void onToggle(std::string_view moduleId, bool enabled) {
    if (moduleId == kModuleId)
      instance().mRuntime.setEnabled(enabled);
  }

  static void onConfigChanged(std::string_view moduleId, std::string_view key,
                              std::string_view value) {
    if (moduleId != kModuleId)
      return;
    instance().applyConfigChange(key, value);
  }

  void applyConfigChange(std::string_view key, std::string_view value) {
    bool parsed = false;
    enum class Changed { None, Single, Separate, Shadow } changed{};
    {
      std::scoped_lock lock(mConfigMutex);
      if (!mConfigFile)
        return;
      auto &config = mConfigFile->value();
      if (key == kSingleModelKey) {
        parsed = parseMenuBool(value, config.singleModel);
        config.singleModel = parsed;
        changed = Changed::Single;
      } else if (key == kSeparateDropVisualsKey) {
        parsed = parseMenuBool(value, config.separateDropVisuals);
        config.separateDropVisuals = parsed;
        changed = Changed::Separate;
      } else if (key == kHideItemShadowKey) {
        parsed = parseMenuBool(value, config.hideItemShadow);
        config.hideItemShadow = parsed;
        changed = Changed::Shadow;
      } else {
        return;
      }
      normalize(config);
      if (!mConfigFile->save())
        mSelf.getLogger().error("Failed to save Item Physics setting: {}",
                                key);
    }

    switch (changed) {
    case Changed::Single:
      mRuntime.setSingleModel(parsed);
      break;
    case Changed::Separate:
      mRuntime.setSeparateDropVisuals(parsed);
      break;
    case Changed::Shadow:
      mRuntime.setHideItemShadow(parsed);
      break;
    case Changed::None:
      break;
    }
  }

  void unregisterMenu() {
    if (!mModuleRegistered)
      return;
    pl::modmenu::unregisterModule(kModuleId);
    mModuleRegistered = false;
  }

  ll::mod::NativeMod &mSelf;
  ItemPhysicsRuntime mRuntime;
  std::mutex mConfigMutex;
  std::optional<pl::config::ConfigFile<ItemPhysicsConfig>> mConfigFile;
  bool mModuleRegistered{};
};

using RegisteredMod = LeviItemPhysicsMod;

} // namespace itemphysics

PL_REGISTER_MOD(itemphysics::RegisteredMod,
                itemphysics::RegisteredMod::instance())
