#include "ItemPhysicsRuntime.hpp"

#include <string>
#include <string_view>

#include <pl/Mod.hpp>
#include <pl/ModMenu.hpp>

namespace itemphysics {

constexpr std::string_view kModuleId = "item_physics.main";

class LeviItemPhysicsMod {
public:
  static LeviItemPhysicsMod &instance() {
    static LeviItemPhysicsMod value;
    return value;
  }

  LeviItemPhysicsMod() : mSelf(*ll::mod::NativeMod::current()) {}

  bool load() {
    mSelf.getLogger().info(
        "Levi Item Physics 0.8.1: height/contact correction profile loaded");
    return true;
  }

  bool enable() {
    // A profile mismatch is intentionally non-fatal: the mod remains loaded and
    // performs no writes or hooks against an unknown Minecraft binary.
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
