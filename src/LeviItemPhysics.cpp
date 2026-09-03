#include "ItemPhysicsRuntime.hpp"

#include <pl/Mod.hpp>

namespace itemphysics {

class LeviItemPhysicsMod {
public:
  static LeviItemPhysicsMod &instance() {
    static LeviItemPhysicsMod value;
    return value;
  }

  LeviItemPhysicsMod() : mSelf(*ll::mod::NativeMod::current()) {}

  bool load() {
    mSelf.getLogger().info(
        "Levi Item Physics 0.8.0: strict visual profile loaded");
    return true;
  }

  bool enable() {
    // A profile mismatch is intentionally non-fatal: the mod remains loaded and
    // performs no writes or hooks against an unknown Minecraft binary.
    if (!mRuntime.install(mSelf))
      mSelf.getLogger().warn(
          "Levi Item Physics is loaded but inactive on this Minecraft binary");
    return true;
  }

  bool disable() {
    mRuntime.uninstall();
    mSelf.getLogger().info("Levi Item Physics disabled");
    return true;
  }

  bool unload() {
    mRuntime.uninstall();
    return true;
  }

private:
  ll::mod::NativeMod &mSelf;
  ItemPhysicsRuntime mRuntime;
};

using RegisteredMod = LeviItemPhysicsMod;

} // namespace itemphysics

PL_REGISTER_MOD(itemphysics::RegisteredMod,
                itemphysics::RegisteredMod::instance())
