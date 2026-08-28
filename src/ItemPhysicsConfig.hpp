#pragma once

#include <algorithm>
#include <optional>
#include <string_view>

#include <pl/Config.hpp>

namespace itemphysics {

struct ItemPhysicsConfig {
  int version = 1;
  bool enabled = true;
  bool singleModel = true;
  double rotationSpeed = 1.0;
  double settleSpeed = 3.0;
  double groundTilt = 1.0;
  double heightOffset = -0.38;
};

inline constexpr double kMinRotationSpeed = 0.25;
inline constexpr double kMaxRotationSpeed = 3.0;
inline constexpr double kMinSettleSpeed = 0.5;
inline constexpr double kMaxSettleSpeed = 8.0;
inline constexpr double kMinGroundTilt = 0.0;
inline constexpr double kMaxGroundTilt = 15.0;
inline constexpr double kMinHeightOffset = -0.60;
inline constexpr double kMaxHeightOffset = 0.20;

inline void normalize(ItemPhysicsConfig &config) {
  config.version = 1;
  config.rotationSpeed =
      std::clamp(config.rotationSpeed, kMinRotationSpeed, kMaxRotationSpeed);
  config.settleSpeed =
      std::clamp(config.settleSpeed, kMinSettleSpeed, kMaxSettleSpeed);
  config.groundTilt =
      std::clamp(config.groundTilt, kMinGroundTilt, kMaxGroundTilt);
  config.heightOffset =
      std::clamp(config.heightOffset, kMinHeightOffset, kMaxHeightOffset);
}

} // namespace itemphysics

namespace pl::config {

template <> struct Schema<itemphysics::ItemPhysicsConfig> {
  static constexpr std::string_view title = "Levi Item Physics";
  static constexpr std::string_view description =
      "Java-style dropped item physics. Rendering changes are client-side only.";

  static constexpr FieldSchema field(std::string_view name) {
    using namespace itemphysics;
    if (name == "version") {
      return {"Version", "Configuration schema version.", std::nullopt,
              std::nullopt, true};
    }
    if (name == "enabled") {
      return {"Enabled", "Master Item Physics toggle.", std::nullopt,
              std::nullopt, false};
    }
    if (name == "singleModel") {
      return {"Single Model",
              "Render one physical model instead of vanilla stack copies.",
              std::nullopt, std::nullopt, false};
    }
    if (name == "rotationSpeed") {
      return {"Tumble Speed", "Airborne angular speed multiplier.",
              kMinRotationSpeed, kMaxRotationSpeed, false};
    }
    if (name == "settleSpeed") {
      return {"Settle Speed", "How quickly an item settles after landing.",
              kMinSettleSpeed, kMaxSettleSpeed, false};
    }
    if (name == "groundTilt") {
      return {"Ground Tilt", "Target landing tilt in degrees.", kMinGroundTilt,
              kMaxGroundTilt, false};
    }
    if (name == "heightOffset") {
      return {"Height Offset", "Vertical correction for the dropped item model.",
              kMinHeightOffset, kMaxHeightOffset, false};
    }
    return {};
  }
};

} // namespace pl::config
