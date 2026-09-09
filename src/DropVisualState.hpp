#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <utility>

namespace itemphysics {

inline constexpr std::uint16_t kNoDropVisualAnchor =
    std::numeric_limits<std::uint16_t>::max();

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

struct StackXZOffset {
  float x{};
  float z{};
};

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
rotateStackOffset(StackXZOffset local, float sine, float cosine) noexcept {
  return {local.x * cosine - local.z * sine,
          local.x * sine + local.z * cosine};
}

// Complete render-only snapshot of one independently spawned ItemActor. The
// live destination ItemActor remains authoritative for gameplay and item data;
// this pose only preserves the source's last visible origin after native merge.
struct DropVisualPose {
  float worldX{};
  float baseWorldY{};
  float worldZ{};
  float xRotSine{};
  float xRotCosine{1.0f};
  float yRotSine{};
  float yRotCosine{1.0f};
  float routeScale{};
  float bobOffset{};
  float waterSampleBias{};
  bool grounded{};
  bool inWater{};
};

struct DropVisualLineage {
  std::uint16_t head{kNoDropVisualAnchor};
  std::uint16_t rootCount{};
  std::uint16_t trackedCount{};
  bool initialized{};
};

struct DropVisualAnchor {
  DropVisualPose pose{};
  std::uint16_t next{kNoDropVisualAnchor};
  std::uint16_t count{};
  bool used{};
};

// Fixed-capacity, allocation-free ownership graph. One lineage belongs to the
// surviving live ItemActor. Every node is one older, independently dropped
// actor whose last visual position must remain visible after gameplay merge.
template <std::size_t Capacity> class DropVisualAnchorPool {
  static_assert(Capacity > 0);
  static_assert(Capacity < kNoDropVisualAnchor);

public:
  void reset() noexcept { mAnchors = {}; }

  void observe(DropVisualLineage &lineage,
               std::uint32_t currentCount) noexcept {
    const auto count = clampCount(currentCount);
    if (!lineage.initialized) {
      lineage = {};
      lineage.head = kNoDropVisualAnchor;
      lineage.rootCount = count;
      lineage.trackedCount = count;
      lineage.initialized = true;
      return;
    }
    reconcile(lineage, count);
  }

  [[nodiscard]] bool merge(DropVisualLineage &source,
                           const DropVisualPose &sourcePose,
                           std::uint32_t sourceCount,
                           DropVisualLineage &destination,
                           std::uint32_t destinationOldCount,
                           std::uint32_t destinationNewCount,
                           float waterSampleDelta = 0.0f) noexcept {
    const auto sourceTotal = clampCount(sourceCount);
    const auto oldTotal = clampCount(destinationOldCount);
    const auto newTotal = clampCount(destinationNewCount);
    if (&source == &destination || !source.initialized ||
        !destination.initialized || sourceTotal == 0 ||
        source.trackedCount != sourceTotal ||
        destination.trackedCount != oldTotal ||
        static_cast<std::uint32_t>(oldTotal) + sourceTotal != newTotal ||
        source.rootCount == 0)
      return false;

    const auto freeIndex = findFree();
    if (freeIndex == kNoDropVisualAnchor)
      return false;

    auto &rootAnchor = mAnchors[freeIndex];
    rootAnchor = {};
    rootAnchor.pose = sourcePose;
    rootAnchor.pose.waterSampleBias += waterSampleDelta;
    rootAnchor.count = source.rootCount;
    rootAnchor.used = true;

    // Put the source root before its already frozen ancestors, then splice the
    // complete source chain in front of the destination's existing chain.
    rootAnchor.next = source.head;
    if (source.head == kNoDropVisualAnchor) {
      rootAnchor.next = destination.head;
    } else {
      auto tail = source.head;
      for (std::size_t guard = 0; guard < Capacity; ++guard) {
        auto &node = mAnchors[tail];
        node.pose.waterSampleBias += waterSampleDelta;
        if (node.next == kNoDropVisualAnchor) {
          node.next = destination.head;
          break;
        }
        tail = node.next;
      }
    }

    destination.head = freeIndex;
    destination.trackedCount = newTotal;
    source = {};
    source.head = kNoDropVisualAnchor;
    return true;
  }

  void reconcile(DropVisualLineage &lineage,
                 std::uint32_t currentCount) noexcept {
    const auto count = clampCount(currentCount);
    if (!lineage.initialized) {
      observe(lineage, count);
      return;
    }
    if (count == lineage.trackedCount)
      return;
    if (count == 0) {
      release(lineage);
      return;
    }

    if (count > lineage.trackedCount) {
      const auto increase = count - lineage.trackedCount;
      lineage.rootCount = static_cast<std::uint16_t>(
          std::min<std::uint32_t>(lineage.rootCount + increase, 255u));
      lineage.trackedCount = count;
      return;
    }

    std::uint16_t decrease = lineage.trackedCount - count;
    while (decrease && lineage.head != kNoDropVisualAnchor) {
      auto &anchor = mAnchors[lineage.head];
      if (!anchor.used) {
        lineage.head = kNoDropVisualAnchor;
        break;
      }
      if (anchor.count > decrease) {
        anchor.count -= decrease;
        decrease = 0;
      } else {
        decrease -= anchor.count;
        const auto next = anchor.next;
        anchor = {};
        lineage.head = next;
      }
    }
    if (decrease) {
      lineage.rootCount = decrease >= lineage.rootCount
                              ? 0
                              : lineage.rootCount - decrease;
    }
    lineage.trackedCount = count;
    if (lineage.rootCount == 0) {
      // A live actor must always have a visible root. Corrupt or incomplete
      // history degrades to one authoritative group instead of stale ghosts.
      releaseAnchors(lineage);
      lineage.rootCount = count;
    }
  }

  void release(DropVisualLineage &lineage) noexcept {
    releaseAnchors(lineage);
    lineage = {};
    lineage.head = kNoDropVisualAnchor;
  }

  [[nodiscard]] std::size_t
  anchorCount(const DropVisualLineage &lineage) const noexcept {
    std::size_t count = 0;
    forEach(lineage, [&](const DropVisualAnchor &) { ++count; });
    return count;
  }

  template <typename Visitor>
  void forEach(const DropVisualLineage &lineage, Visitor &&visitor) const
      noexcept(noexcept(visitor(std::declval<const DropVisualAnchor &>()))) {
    auto index = lineage.head;
    for (std::size_t guard = 0;
         index != kNoDropVisualAnchor && guard < Capacity; ++guard) {
      if (index >= Capacity)
        return;
      const auto &anchor = mAnchors[index];
      if (!anchor.used)
        return;
      visitor(anchor);
      index = anchor.next;
    }
  }

private:
  [[nodiscard]] static constexpr std::uint16_t
  clampCount(std::uint32_t count) noexcept {
    return static_cast<std::uint16_t>(std::min(count, 255u));
  }

  [[nodiscard]] std::uint16_t findFree() const noexcept {
    for (std::uint16_t index = 0; index < Capacity; ++index) {
      if (!mAnchors[index].used)
        return index;
    }
    return kNoDropVisualAnchor;
  }

  void releaseAnchors(DropVisualLineage &lineage) noexcept {
    auto index = lineage.head;
    for (std::size_t guard = 0;
         index != kNoDropVisualAnchor && guard < Capacity; ++guard) {
      if (index >= Capacity)
        break;
      auto &anchor = mAnchors[index];
      if (!anchor.used)
        break;
      const auto next = anchor.next;
      anchor = {};
      index = next;
    }
    lineage.head = kNoDropVisualAnchor;
  }

  std::array<DropVisualAnchor, Capacity> mAnchors{};
};

} // namespace itemphysics
