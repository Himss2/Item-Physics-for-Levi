#pragma once

#include <algorithm>
#include <cstdint>

namespace itemphysics {

inline constexpr std::uint32_t kMaxRealItemModels = 64u;

struct StackXZOffset {
  float x{};
  float z{};
};

[[nodiscard]] constexpr std::uint32_t
javaVisualCopyCount(std::uint32_t count) noexcept {
  if (count > 48u)
    return 5u;
  if (count > 32u)
    return 4u;
  if (count > 16u)
    return 3u;
  if (count > 1u)
    return 2u;
  return 1u;
}

[[nodiscard]] constexpr std::uint32_t
selectVisualCopyCount(std::uint32_t count, bool singleModel,
                      bool realItemModels) noexcept {
  if (singleModel)
    return 1u;
  if (realItemModels)
    return std::clamp(count, 1u, kMaxRealItemModels);
  return javaVisualCopyCount(count);
}

[[nodiscard]] constexpr StackXZOffset
centeredRowOffset(std::uint32_t copy, std::uint32_t copies,
                  float step) noexcept {
  const auto safeCopies = std::max(copies, 1u);
  const auto safeCopy = std::min(copy, safeCopies - 1u);
  return {(static_cast<float>(safeCopy) -
           static_cast<float>(safeCopies - 1u) * 0.5f) *
              step,
          0.0f};
}

[[nodiscard]] constexpr StackXZOffset
compactGridOffset(std::uint32_t copy, std::uint32_t copies,
                  float step) noexcept {
  const auto safeCopies = std::clamp(copies, 1u, kMaxRealItemModels);
  const auto safeCopy = std::min(copy, safeCopies - 1u);

  std::uint32_t columns = 1u;
  while (columns * columns < safeCopies)
    ++columns;
  const std::uint32_t rows =
      (safeCopies + columns - 1u) / columns;
  const std::uint32_t row = safeCopy / columns;
  const std::uint32_t firstInRow = row * columns;
  const std::uint32_t countInRow =
      std::min(columns, safeCopies - firstInRow);
  const std::uint32_t column = safeCopy - firstInRow;

  return {(static_cast<float>(column) -
           static_cast<float>(countInRow - 1u) * 0.5f) *
              step,
          (static_cast<float>(row) -
           static_cast<float>(rows - 1u) * 0.5f) *
              step};
}

[[nodiscard]] constexpr StackXZOffset
rotateStackOffset(StackXZOffset local, float sine, float cosine) noexcept {
  return {local.x * cosine - local.z * sine,
          local.x * sine + local.z * cosine};
}

} // namespace itemphysics
