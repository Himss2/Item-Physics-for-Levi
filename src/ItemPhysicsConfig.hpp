#pragma once
#include <algorithm>
#include <optional>
#include <string_view>
#include <pl/Config.hpp>

namespace itemphysics {

struct ItemPhysicsConfig {
  int version = 8;
  bool enabled = true;
  bool singleModel = false;
  bool hideItemShadow = true;
  bool oldRotation = false;
  double rotationSpeed = 1.0;
};

inline constexpr double kMinRotationSpeed = 0.25;
inline constexpr double kMaxRotationSpeed = 3.0;

inline void normalize(ItemPhysicsConfig &config) {
  config.version = 8;
  config.rotationSpeed = std::clamp(
      config.rotationSpeed,
      kMinRotationSpeed,
      kMaxRotationSpeed);
}

}

namespace pl::config {

template <>
struct Schema<itemphysics::ItemPhysicsConfig> {
  static constexpr std::string_view title =
      "Levi Item Physics";

  static constexpr std::string_view description =
      "Java ItemPhysic-style dropped item rotation with Bedrock renderer adaptation.";

  static constexpr FieldSchema field(std::string_view name) {
    using namespace itemphysics;

    if (name == "version")
      return {"Version","Configuration schema version.",
              std::nullopt,std::nullopt,true};

    if (name == "enabled")
      return {"Enabled","Master Item Physics toggle.",
              std::nullopt,std::nullopt,false};

    if (name == "singleModel")
      return {"Single Model",
              "Render one model instead of vanilla stacked copies.",
              std::nullopt,std::nullopt,false};

    if (name == "hideItemShadow")
      return {"Hide Item Shadow",
              "Hide the vanilla shadow under dropped items.",
              std::nullopt,std::nullopt,false};

    if (name == "oldRotation")
      return {"Old Rotation",
              "Settle 3D block models to the nearest 90-degree step, matching Java ItemPhysic's optional old rotation mode.",
              std::nullopt,std::nullopt,false};

    if (name == "rotationSpeed")
      return {"Tumble Speed",
              "Airborne rotation-speed multiplier.",
              kMinRotationSpeed,kMaxRotationSpeed,false};

    return {};
  }
};

}
