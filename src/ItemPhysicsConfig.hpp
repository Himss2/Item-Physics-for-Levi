#pragma once
#include <algorithm>
#include <optional>
#include <string_view>
#include <pl/Config.hpp>
namespace itemphysics {
struct ItemPhysicsConfig {
  int version = 7;
  bool enabled = true;
  bool singleModel = true;
  bool hideItemShadow = true;
  double rotationSpeed = 1.0;
  double settleSpeed = 3.0;
  double groundTilt = 86.40;
  double heightOffset = -0.09;
  double blockGroundHeight = 0.0;
  double thinBlockGroundHeight = -0.11;
  double torchGroundHeight = -0.11;
  double shapedBlockGroundHeight = -0.10;
  double skullGroundHeight = -0.25;
  double shieldGroundHeight = -0.10;
  double bannerGroundHeight = -0.10;
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
inline void normalize(ItemPhysicsConfig &config) {
  config.version = 7;
  config.rotationSpeed = std::clamp(config.rotationSpeed, kMinRotationSpeed, kMaxRotationSpeed);
  config.settleSpeed = std::clamp(config.settleSpeed, kMinSettleSpeed, kMaxSettleSpeed);
  config.groundTilt = 86.40;
  config.heightOffset = -0.09;
  config.blockGroundHeight = 0.0;
  config.thinBlockGroundHeight = -0.11;
  config.torchGroundHeight = -0.11;
  config.shapedBlockGroundHeight = -0.10;
  config.skullGroundHeight = -0.25;
  config.shieldGroundHeight = -0.10;
  config.bannerGroundHeight = -0.10;
}
}
namespace pl::config {
template <>
struct Schema<
    itemphysics::ItemPhysicsConfig> {
  static constexpr std::string_view title =
      "Levi Item Physics";
  static constexpr std::string_view description =
      "Atlas-style dropped item physics with fixed Java-style ground pose and "
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
          "Fixed internal grounded angle.",
          kMinGroundTilt,
          kMaxGroundTilt,
          true
      };
    }
    if (name == "heightOffset") {
      return {
          "Flat Item Height",
          "Fixed internal flat item height.",
          kMinHeightOffset,
          kMaxHeightOffset,
          true
      };
    }
    if (name == "blockGroundHeight") {
      return {
          "Block Ground Height",
          "Fixed internal generic block height.",
          kMinGroundHeight,
          kMaxGroundHeight,
          true
      };
    }
    if (name == "thinBlockGroundHeight") {
      return {
          "Thin Block Height",
          "Fixed internal thin block height.",
          kMinGroundHeight,
          kMaxGroundHeight,
          true
      };
    }
    if (name == "torchGroundHeight") {
      return {
          "Torch / Cross Height",
          "Fixed internal torch height.",
          kMinGroundHeight,
          kMaxGroundHeight,
          true
      };
    }
    if (name == "shapedBlockGroundHeight") {
      return {
          "Shaped Block Height",
          "Fixed internal shaped block height.",
          kMinGroundHeight,
          kMaxGroundHeight,
          true
      };
    }
    if (name == "skullGroundHeight") {
      return {
          "Head / Skull Height",
          "Fixed internal skull height.",
          kMinGroundHeight,
          kMaxGroundHeight,
          true
      };
    }
    if (name == "shieldGroundHeight") {
      return {
          "Shield Height",
          "Fixed internal shield height.",
          kMinGroundHeight,
          kMaxGroundHeight,
          true
      };
    }
    if (name == "bannerGroundHeight") {
      return {
          "Banner Height",
          "Fixed internal banner height.",
          kMinGroundHeight,
          kMaxGroundHeight,
          true
      };
    }
    return {};
  }
};
}
