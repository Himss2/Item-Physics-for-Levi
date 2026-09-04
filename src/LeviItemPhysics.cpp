#include "ItemPhysicsRuntime.hpp"

#include <string>
#include <string_view>

#include <pl/Mod.hpp>
#include <pl/ModMenu.hpp>

namespace itemphysics {

constexpr std::string_view kModuleId = "item_physics.main";
constexpr std::string_view kSingleModelKey = "singleModel";
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
    mSelf.getLogger().info(
        "Levi Item Physics 0.8.5: stable water/contact profile loaded");
    return true;
  }

  bool enable() {
    // A profile mismatch is intentionally non-fatal: the mod remains loaded and
    // performs no writes or hooks against an unknown Minecraft binary.
    mRuntime.setEnabled(true);
    mRuntime.setSingleModel(false);
    mRuntime.setHideItemShadow(true);
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
                    pl::modmenu::ConfigType::Toggle, "false")
            .config(std::string(kHideItemShadowKey), "Hide Item Shadow",
                    pl::modmenu::ConfigType::Toggle, "true")
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
    return true;
  }

private:
  static void onToggle(std::string_view moduleId, bool enabled) {
    if (moduleId == kModuleId)
      instance().mRuntime.setEnabled(enabled);
  }

  static void onConfigChanged(std::string_view moduleId, std::string_view key,
                              std::string_view value) {
    if (moduleId != kModuleId)
      return;
    auto &runtime = instance().mRuntime;
    if (key == kSingleModelKey)
      runtime.setSingleModel(parseMenuBool(value, false));
    else if (key == kHideItemShadowKey)
      runtime.setHideItemShadow(parseMenuBool(value, true));
  }

  void unregisterMenu() {
    if (!mModuleRegistered)
      return;
    pl::modmenu::unregisterModule(kModuleId);
    mModuleRegistered = false;
  }

  ll::mod::NativeMod &mSelf;
  ItemPhysicsRuntime mRuntime;
  bool mModuleRegistered{};
};

using RegisteredMod = LeviItemPhysicsMod;

} // namespace itemphysics

PL_REGISTER_MOD(itemphysics::RegisteredMod,
                itemphysics::RegisteredMod::instance())
