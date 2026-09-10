#pragma once

#include <string_view>

#if __has_include(<pl/Config.hpp>)
#include <optional>
#include <pl/Config.hpp>
#define ITEMPHYSICS_HAS_TYPED_CONFIG 1
#else
#define ITEMPHYSICS_HAS_TYPED_CONFIG 0
#endif

namespace itemphysics {

struct ItemPhysicsConfig {
  int version = 10;
  bool singleModel = false;
  bool separateDropVisuals = false;
  bool hideItemShadow = true;
};

inline void normalize(ItemPhysicsConfig &config) noexcept {
  config.version = 10;
}

} // namespace itemphysics

#if ITEMPHYSICS_HAS_TYPED_CONFIG
namespace pl::config {

template <> struct Schema<itemphysics::ItemPhysicsConfig> {
  static constexpr std::string_view title = "Levi Item Physics";
  static constexpr std::string_view description =
      "Persistent rendering options for Java-style dropped-item physics.";

  static constexpr FieldSchema field(std::string_view name) {
    if (name == "version")
      return {"Version", "Configuration schema version.", std::nullopt,
              std::nullopt, true};
    if (name == "singleModel")
      return {"Single Model", "Render one model for each drop origin.",
              std::nullopt, std::nullopt, false};
    if (name == "separateDropVisuals")
      return {"Separate Drop Visuals",
              "Keep merged drop origins visually separate.", std::nullopt,
              std::nullopt, false};
    if (name == "hideItemShadow")
      return {"Hide Item Shadow", "Hide the vanilla dropped-item shadow.",
              std::nullopt, std::nullopt, false};
    return {};
  }
};

} // namespace pl::config
#endif
