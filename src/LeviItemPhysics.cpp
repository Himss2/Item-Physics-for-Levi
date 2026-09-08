#include "ItemPhysicsRuntime.hpp"

#include <charconv>
#include <string>
#include <string_view>

#include <pl/Mod.hpp>
#include <pl/ModMenu.hpp>

namespace itemphysics {

constexpr std::string_view kModuleId = "item_physics.main";
constexpr std::string_view kSingleModelKey = "singleModel";
constexpr std::string_view kHideItemShadowKey = "hideItemShadow";
constexpr std::string_view kFlatGroundYKey = "flatGroundY";
constexpr std::string_view kShapedGroundYKey = "shapedGroundY";
constexpr std::string_view kFullBlockGroundYKey = "fullBlockGroundY";
constexpr std::string_view kThinGroundYKey = "thinGroundY";
constexpr std::string_view kSpecialGroundYKey = "specialGroundY";
constexpr std::string_view kNormalHeadGroundYKey = "normalHeadGroundY";
constexpr std::string_view kDragonHeadGroundYKey = "dragonHeadGroundY";
constexpr std::string_view kWaterBobSpeedKey = "waterBobSpeed";

constexpr float kGroundSliderUnit = 0.001f;
constexpr float kWaterSpeedSliderUnit = 0.01f;

bool parseMenuBool(std::string_view value, bool fallback) noexcept {
  if (value == "true" || value == "1" || value == "on" ||
      value == "enabled")
    return true;
  if (value == "false" || value == "0" || value == "off" ||
      value == "disabled")
    return false;
  return fallback;
}

bool parseMenuInt(std::string_view value, int &parsed) noexcept {
  if (value.empty())
    return false;
  int result{};
  const char *const end = value.data() + value.size();
  const auto conversion = std::from_chars(value.data(), end, result);
  if (conversion.ec != std::errc{} || conversion.ptr != end)
    return false;
  parsed = result;
  return true;
}

bool applySliderConfig(ItemPhysicsRuntime &runtime, std::string_view key,
                       std::string_view value) noexcept {
  int sliderValue{};
  if (!parseMenuInt(value, sliderValue))
    return false;

  if (key == kFlatGroundYKey)
    runtime.setFlatGroundHeight(sliderValue * kGroundSliderUnit);
  else if (key == kShapedGroundYKey)
    runtime.setShapedGroundHeight(sliderValue * kGroundSliderUnit);
  else if (key == kFullBlockGroundYKey)
    runtime.setFullBlockGroundHeight(sliderValue * kGroundSliderUnit);
  else if (key == kThinGroundYKey)
    runtime.setHorizontalThinGroundHeight(sliderValue * kGroundSliderUnit);
  else if (key == kSpecialGroundYKey)
    runtime.setSpecialGroundHeight(sliderValue * kGroundSliderUnit);
  else if (key == kNormalHeadGroundYKey)
    runtime.setNormalHeadGroundHeight(sliderValue * kGroundSliderUnit);
  else if (key == kDragonHeadGroundYKey)
    runtime.setDragonHeadGroundHeight(sliderValue * kGroundSliderUnit);
  else if (key == kWaterBobSpeedKey)
    runtime.setWaterBobSpeed(sliderValue * kWaterSpeedSliderUnit);
  else
    return false;
  return true;
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
        "Levi Item Physics 0.10.0: live render calibration loaded");
    return true;
  }

  bool enable() {
    // A profile mismatch is intentionally non-fatal: the mod remains loaded and
    // performs no writes or hooks against an unknown Minecraft binary.
    mRuntime.setEnabled(true);
    mRuntime.setSingleModel(false);
    mRuntime.setHideItemShadow(true);
    mRuntime.setFlatGroundHeight(-0.150f);
    mRuntime.setShapedGroundHeight(-0.165f);
    mRuntime.setFullBlockGroundHeight(-0.035f);
    mRuntime.setHorizontalThinGroundHeight(-0.145f);
    mRuntime.setSpecialGroundHeight(-0.140f);
    mRuntime.setNormalHeadGroundHeight(0.015f);
    mRuntime.setDragonHeadGroundHeight(-0.015f);
    mRuntime.setWaterBobSpeed(1.0f);
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
            .config(std::string(kFlatGroundYKey), "2D Item Ground Y",
                    pl::modmenu::ConfigType::SliderInt, "-150", "-300",
                    "300")
            .config(std::string(kShapedGroundYKey), "Shaped Item Ground Y",
                    pl::modmenu::ConfigType::SliderInt, "-165", "-300",
                    "300")
            .config(std::string(kFullBlockGroundYKey), "Full Block Ground Y",
                    pl::modmenu::ConfigType::SliderInt, "-35", "-300",
                    "300")
            .config(std::string(kThinGroundYKey), "Slab/Thin Ground Y",
                    pl::modmenu::ConfigType::SliderInt, "-145", "-300",
                    "300")
            .config(std::string(kSpecialGroundYKey),
                    "Shield/Banner Ground Y",
                    pl::modmenu::ConfigType::SliderInt, "-140", "-300",
                    "300")
            .config(std::string(kNormalHeadGroundYKey),
                    "Normal Skull Ground Y",
                    pl::modmenu::ConfigType::SliderInt, "15", "-300",
                    "300")
            .config(std::string(kDragonHeadGroundYKey),
                    "Dragon Head Ground Y",
                    pl::modmenu::ConfigType::SliderInt, "-15", "-300",
                    "300")
            .config(std::string(kWaterBobSpeedKey), "Water Bob Speed (%)",
                    pl::modmenu::ConfigType::SliderInt, "100", "0", "300")
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
    else
      applySliderConfig(runtime, key, value);
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
