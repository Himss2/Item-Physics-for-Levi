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
constexpr std::string_view kModuleId="item_physics.main";
constexpr std::string_view kEnabledKey="enabled";
constexpr std::string_view kSingleModelKey="singleModel";
constexpr std::string_view kHideItemShadowKey="hideItemShadow";
constexpr std::string_view kOldRotationKey="oldRotation";
constexpr std::string_view kRotationSpeedKey="rotationSpeed";

bool parseBool(std::string_view v,bool fallback){
  if(v=="true"||v=="1"||v=="on"||v=="enabled")return true;
  if(v=="false"||v=="0"||v=="off"||v=="disabled")return false;
  return fallback;
}
double parseDouble(std::string_view v,double fallback){
  std::string s(v);char *end=nullptr;errno=0;const double x=std::strtod(s.c_str(),&end);
  if(end==s.c_str()||*end!='\0'||errno==ERANGE||!std::isfinite(x))return fallback;
  return x;
}
std::string boolText(bool v){return v?"true":"false";}
std::string numberText(double v){std::ostringstream s;s<<v;return s.str();}

class LeviItemPhysicsMod {
public:
  static LeviItemPhysicsMod &instance(){static LeviItemPhysicsMod value;return value;}
  LeviItemPhysicsMod():mSelf(*ll::mod::NativeMod::current()){}

  bool load(){
    std::lock_guard lock(mConfigMutex);mConfig.emplace();
    if(!mConfig->load()){mSelf.getLogger().error("Failed to load Item Physics config");mConfig.reset();return false;}
    normalize(mConfig->value());
    if(!mConfig->save())mSelf.getLogger().warn("Loaded config but failed to persist normalization");
    mRuntime.applyConfig(mConfig->value());
    mSelf.getLogger().info("Loaded Item Physics config from {}",mConfig->configPath().string());
    return true;
  }

  bool enable(){
    const auto c=snapshotConfig();mRuntime.applyConfig(c);const bool hookActive=mRuntime.install(mSelf);
    const bool registered=pl::modmenu::ModuleBuilder(std::string(kModuleId),"Item Physics")
      .modId(mSelf.getId())
      .description("Java ItemPhysic-style dropped-item physics adapted to Bedrock models.")
      .defaultEnabled(c.enabled)
      .onToggle(onToggle)
      .config(std::string(kSingleModelKey),"Single Model",pl::modmenu::ConfigType::Toggle,boolText(c.singleModel))
      .config(std::string(kHideItemShadowKey),"Hide Item Shadow",pl::modmenu::ConfigType::Toggle,boolText(c.hideItemShadow))
      .config(std::string(kRotationSpeedKey),"Tumble Speed",pl::modmenu::ConfigType::SliderFloat,numberText(c.rotationSpeed),numberText(kMinRotationSpeed),numberText(kMaxRotationSpeed))
      .config(std::string(kOldRotationKey),"Old Rotation",pl::modmenu::ConfigType::Toggle,boolText(c.oldRotation))
      .onConfigChanged(onConfigChanged)
      .registerModule();
    if(!registered){mSelf.getLogger().error("Failed to register Item Physics in Mod Menu");mRuntime.uninstall();return false;}
    mModuleRegistered=true;
    if(hookActive)mSelf.getLogger().info("Item Physics enabled: Java core + smooth Bedrock family landing v9");
    else mSelf.getLogger().warn("Item Physics registered in Mod Menu, but runtime hook is inactive for this Minecraft binary");
    return true;
  }

  bool disable(){unregisterMenu();mRuntime.uninstall();mSelf.getLogger().info("Item Physics disabled");return true;}
  bool unload(){unregisterMenu();mRuntime.uninstall();std::lock_guard lock(mConfigMutex);mConfig.reset();return true;}

private:
  ll::mod::NativeMod &mSelf;
  ItemPhysicsRuntime mRuntime;
  std::mutex mConfigMutex;
  std::optional<pl::config::ConfigFile<ItemPhysicsConfig>> mConfig;
  bool mModuleRegistered{};

  ItemPhysicsConfig snapshotConfig(){std::lock_guard lock(mConfigMutex);if(!mConfig)return {};auto c=mConfig->value();normalize(c);return c;}
  void persistAndApplyLocked(std::string_view reason){
    if(!mConfig)return;normalize(mConfig->value());mRuntime.applyConfig(mConfig->value());
    if(mConfig->save())mSelf.getLogger().info("Persisted Item Physics config after {}",reason);
    else mSelf.getLogger().warn("Failed to persist Item Physics config after {}",reason);
  }
  static void onToggle(std::string_view id,bool enabled){instance().handleToggle(id,enabled);}
  static void onConfigChanged(std::string_view id,std::string_view key,std::string_view value){instance().handleConfigChanged(id,key,value);}
  void handleToggle(std::string_view id,bool enabled){if(id!=kModuleId)return;std::lock_guard lock(mConfigMutex);if(!mConfig)return;mConfig->value().enabled=enabled;persistAndApplyLocked("module toggle");}
  void handleConfigChanged(std::string_view id,std::string_view key,std::string_view value){
    if(id!=kModuleId)return;std::lock_guard lock(mConfigMutex);if(!mConfig)return;auto &c=mConfig->value();
    if(key==kSingleModelKey)c.singleModel=parseBool(value,c.singleModel);
    else if(key==kHideItemShadowKey)c.hideItemShadow=parseBool(value,c.hideItemShadow);
    else if(key==kOldRotationKey)c.oldRotation=parseBool(value,c.oldRotation);
    else if(key==kRotationSpeedKey)c.rotationSpeed=parseDouble(value,c.rotationSpeed);
    else if(key==kEnabledKey)c.enabled=parseBool(value,c.enabled);
    else return;
    persistAndApplyLocked(key);
  }
  void unregisterMenu(){if(!mModuleRegistered)return;pl::modmenu::unregisterModule(kModuleId);mModuleRegistered=false;}
};
using RegisteredMod=LeviItemPhysicsMod;
}
PL_REGISTER_MOD(itemphysics::RegisteredMod,itemphysics::RegisteredMod::instance())
