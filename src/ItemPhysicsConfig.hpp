#pragma once

#include <algorithm>
#include <optional>
#include <string_view>

#include <pl/Config.hpp>

namespace itemphysics {

struct ItemPhysicsConfig {

  int version = 6;

  bool enabled = true;
  bool singleModel = true;
  bool hideItemShadow = true;

  double rotationSpeed = 1.0;
  double settleSpeed = 3.0;

  double groundTilt = 90.0;

  double heightOffset = -0.38;

  double blockGroundHeight = -0.06;

  double thinBlockGroundHeight = -0.08;

  double torchGroundHeight = -0.08;

  double shapedBlockGroundHeight = -0.16;

  double skullGroundHeight = -0.22;

  double shieldGroundHeight = -0.08;

  double bannerGroundHeight = -0.075;
};

inline constexpr double
    kMinRotationSpeed = 0.25;

inline constexpr double
    kMaxRotationSpeed = 3.0;

inline constexpr double
    kMinSettleSpeed = 0.5;

inline constexpr double
    kMaxSettleSpeed = 8.0;

inline constexpr double
    kMinGroundTilt = 0.0;

inline constexpr double
    kMaxGroundTilt = 180.0;

inline constexpr double
    kMinHeightOffset = -0.60;

inline constexpr double
    kMaxHeightOffset = 0.20;

inline constexpr double
    kMinGroundHeight = -0.40;

inline constexpr double
    kMaxGroundHeight = 0.30;

inline void normalize(
    ItemPhysicsConfig &config) {

  const int oldVersion =
      config.version;

  if (oldVersion < 2) {

    config.groundTilt =
        90.0;
  }

  if (oldVersion < 4) {

    config.groundTilt =
        90.0;

    config.heightOffset =
        -0.38;
  }

  if (oldVersion < 5) {

    config.blockGroundHeight =
        -0.06;

    config.thinBlockGroundHeight =
        -0.08;

    config.torchGroundHeight =
        -0.08;

    config.shapedBlockGroundHeight =
        -0.16;

    config.skullGroundHeight =
        -0.22;

    config.shieldGroundHeight =
        -0.08;

    config.bannerGroundHeight =
        -0.075;
  }

  if (oldVersion < 6) {

    config.hideItemShadow =
        true;
  }

  config.version =
      6;

  config.rotationSpeed =
      std::clamp(
          config.rotationSpeed,
          kMinRotationSpeed,
          kMaxRotationSpeed);

  config.settleSpeed =
      std::clamp(
          config.settleSpeed,
          kMinSettleSpeed,
          kMaxSettleSpeed);

  config.groundTilt =
      std::clamp(
          config.groundTilt,
          kMinGroundTilt,
          kMaxGroundTilt);

  config.heightOffset =
      std::clamp(
          config.heightOffset,
          kMinHeightOffset,
          kMaxHeightOffset);

  config.blockGroundHeight =
      std::clamp(
          config.blockGroundHeight,
          kMinGroundHeight,
          kMaxGroundHeight);

  config.thinBlockGroundHeight =
      std::clamp(
          config.thinBlockGroundHeight,
          kMinGroundHeight,
          kMaxGroundHeight);

  config.torchGroundHeight =
      std::clamp(
          config.torchGroundHeight,
          kMinGroundHeight,
          kMaxGroundHeight);

  config.shapedBlockGroundHeight =
      std::clamp(
          config.shapedBlockGroundHeight,
          kMinGroundHeight,
          kMaxGroundHeight);

  config.skullGroundHeight =
      std::clamp(
          config.skullGroundHeight,
          kMinGroundHeight,
          kMaxGroundHeight);

  config.shieldGroundHeight =
      std::clamp(
          config.shieldGroundHeight,
          kMinGroundHeight,
          kMaxGroundHeight);

  config.bannerGroundHeight =
      std::clamp(
          config.bannerGroundHeight,
          kMinGroundHeight,
          kMaxGroundHeight);
}

} // namespace itemphysics

namespace pl::config {

template <>
struct Schema<
    itemphysics::ItemPhysicsConfig> {

  static constexpr std::string_view title =
      "Levi Item Physics";

  static constexpr std::string_view description =
      "Atlas-style dropped item physics with per-renderer ground height and "
      "optional dropped-item shadow suppression.";

  static constexpr FieldSchema field(
      std::string_view name) {

    using namespace itemphysics;

    if (name == "version") {

      return {
          "Version",
          "Configuration schema version.",
          std::nullopt,
          std::nullopt,
          true
      };
    }

    if (name == "enabled") {

      return {
          "Enabled",
          "Master Item Physics toggle.",
          std::nullopt,
          std::nullopt,
          false
      };
    }

    if (name == "singleModel") {

      return {
          "Single Model",
          "Render one physical model instead of vanilla stacked copies.",
          std::nullopt,
          std::nullopt,
          false
      };
    }

    if (name == "hideItemShadow") {

      return {
          "Hide Item Shadow",
          "Hide only the vanilla shadow under dropped ItemActor entities.",
          std::nullopt,
          std::nullopt,
          false
      };
    }

    if (name == "rotationSpeed") {

      return {
          "Tumble Speed",
          "Airborne angular-speed multiplier.",
          kMinRotationSpeed,
          kMaxRotationSpeed,
          false
      };
    }

    if (name == "settleSpeed") {

      return {
          "Settle Speed",
          "How quickly an item settles after touching the ground.",
          kMinSettleSpeed,
          kMaxSettleSpeed,
          false
      };
    }

    if (name == "groundTilt") {

      return {
          "Ground Angle",
          "Atlas grounded rotation target. Default: 90 degrees.",
          kMinGroundTilt,
          kMaxGroundTilt,
          false
      };
    }

    if (name == "heightOffset") {

      return {
          "Flat Item Height",
          "Base height for sword, pickaxe, tools and ordinary flat items. "
          "Negative lowers the model; positive raises it.",
          kMinHeightOffset,
          kMaxHeightOffset,
          false
      };
    }

    if (name == "blockGroundHeight") {

      return {
          "Block Ground Height",
          "Additional grounded Y correction for generic block-rendered items.",
          kMinGroundHeight,
          kMaxGroundHeight,
          false
      };
    }

    if (name == "thinBlockGroundHeight") {

      return {
          "Thin Block Height",
          "Additional grounded Y correction for slab, carpet, trapdoor and "
          "other thin horizontal block models.",
          kMinGroundHeight,
          kMaxGroundHeight,
          false
      };
    }

    if (name == "torchGroundHeight") {

      return {
          "Torch / Cross Height",
          "Additional grounded Y correction for torch and cross-style models.",
          kMinGroundHeight,
          kMaxGroundHeight,
          false
      };
    }

    if (name == "shapedBlockGroundHeight") {

      return {
          "Shaped Block Height",
          "Additional grounded Y correction for fence, lantern, lever, "
          "brewing stand, flower pot, chain and similar models.",
          kMinGroundHeight,
          kMaxGroundHeight,
          false
      };
    }

    if (name == "skullGroundHeight") {

      return {
          "Head / Skull Height",
          "Grounded Y correction used only for player heads and mob skulls.",
          kMinGroundHeight,
          kMaxGroundHeight,
          false
      };
    }

    if (name == "shieldGroundHeight") {

      return {
          "Shield Height",
          "Grounded Y correction used only for dropped shields.",
          kMinGroundHeight,
          kMaxGroundHeight,
          false
      };
    }

    if (name == "bannerGroundHeight") {

      return {
          "Banner Height",
          "Grounded Y correction used only for dropped banners.",
          kMinGroundHeight,
          kMaxGroundHeight,
          false
      };
    }

    return {};
  }
};

} // namespace pl::config
