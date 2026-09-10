#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <utility>

namespace itemphysics {

inline constexpr std::uint16_t kNoDropVisualAnchor =
    std::numeric_limits<std::uint16_t>::max();

enum class DropFluidKind : std::uint8_t {
  None,
  Water,
  Lava,
};

[[nodiscard]] constexpr bool isFluid(DropFluidKind fluid) noexcept {
  return fluid != DropFluidKind::None;
}

[[nodiscard]] constexpr float
fluidMotionScale(DropFluidKind fluid) noexcept {
  return fluid == DropFluidKind::Lava ? 0.5f : 1.0f;
}

// Render-only surface latch. Minecraft owns the complete sink and buoyant-rise
// path: before the actor has stayed vertically stable for three distinct game
// ticks this returns the exact native interpolated Y. Only then is the base Y
// frozen so our small Java-style waveform cannot double with native jitter.
class FluidVisualBase {
public:
  float update(float nativeRenderY, float nativeTickY, float verticalSpeed,
               std::int32_t age, DropFluidKind fluid,
               bool nativeGrounded) noexcept {
    if (!isFluid(fluid) || !std::isfinite(nativeRenderY) ||
        !std::isfinite(nativeTickY) || !std::isfinite(verticalSpeed)) {
      *this = {};
      return nativeRenderY;
    }

    if (!mInitialized || fluid != mFluid || age < mLastAge ||
        age - mLastAge > 10) {
      initialize(nativeTickY, age, fluid);
      return nativeRenderY;
    }

    if (age != mLastAge) {
      const float tickDelta = nativeTickY - mLastTickY;
      // Once a real surface has been confirmed, ordinary native buoyancy
      // jitter or a slow client correction must not release the render base
      // and make the item sink again. Reset only for a genuine fast vertical
      // relocation; fluid exit/type changes are handled above.
      if (mBobbing && std::abs(nativeTickY - mBase) > 0.75f &&
          std::abs(verticalSpeed) > 0.20f) {
        initialize(nativeTickY, age, fluid);
        return nativeRenderY;
      }

      if (!mBobbing) {
        mLowestY = std::min(mLowestY, nativeTickY);
        const bool rising = tickDelta > 0.006f || verticalSpeed > 0.012f;
        mObservedAscent = mObservedAscent || rising;
        const bool stable = std::abs(tickDelta) <= 0.012f &&
                            std::abs(verticalSpeed) <= 0.025f &&
                            !nativeGrounded && mObservedAscent &&
                            nativeTickY - mLowestY >= 0.08f;
        mStableTicks = stable
                           ? static_cast<std::uint8_t>(
                                 std::min<unsigned>(mStableTicks + 1u, 3u))
                           : 0u;
        if (mStableTicks >= 3u) {
          mBase = nativeTickY;
          mBobbing = true;
        }
      }
      mLastTickY = nativeTickY;
      mLastAge = age;
    }

    return mBobbing ? mBase : nativeRenderY;
  }

  [[nodiscard]] bool bobbing() const noexcept { return mBobbing; }

private:
  void initialize(float nativeTickY, std::int32_t age,
                  DropFluidKind fluid) noexcept {
    mBase = nativeTickY;
    mLastTickY = nativeTickY;
    mLowestY = nativeTickY;
    mLastAge = age;
    mFluid = fluid;
    mStableTicks = 0;
    mInitialized = true;
    mBobbing = false;
    mObservedAscent = false;
  }

  float mBase{};
  float mLastTickY{};
  float mLowestY{};
  std::int32_t mLastAge{};
  DropFluidKind mFluid{DropFluidKind::None};
  std::uint8_t mStableTicks{};
  bool mInitialized{};
  bool mBobbing{};
  bool mObservedAscent{};
};

// A narrowly bounded repair for the analyzed Bedrock build: fire-resistant
// ItemActors can finish their native lava sink on a collision surface and then
// remain there indefinitely. This detector does not touch the entry path. It
// waits for real descent followed by three distinct stable collision ticks,
// then requests a small upward velocity only until the recorded entry height
// is reached. Water, burning items and remote clients never enter this state.
class LavaBottomRecovery {
public:
  [[nodiscard]] std::optional<float>
  update(float worldY, float verticalSpeed, bool nativeGrounded,
         bool verticalCollision, std::int32_t age, bool inLava,
         bool fireResistant = true, bool authoritative = true,
         bool enabled = true) noexcept {
    if (!enabled || !authoritative || !fireResistant || !inLava ||
        !std::isfinite(worldY) || !std::isfinite(verticalSpeed)) {
      *this = {};
      return std::nullopt;
    }

    if (!mInitialized || age < mLastAge || age - mLastAge > 10) {
      mEntryY = worldY;
      mLastY = worldY;
      mLastAge = age;
      mInitialized = true;
      return std::nullopt;
    }
    if (age == mLastAge)
      return mRecovering ? correction(worldY) : std::nullopt;

    const float deltaY = worldY - mLastY;
    mObservedDescent = mObservedDescent ||
                       deltaY < -0.01f || verticalSpeed < -0.03f ||
                       mEntryY - worldY >= 0.10f;

    if (mRecovering) {
      mLastY = worldY;
      mLastAge = age;
      return correction(worldY);
    }
    if (mCompleted) {
      mLastY = worldY;
      mLastAge = age;
      return std::nullopt;
    }

    const bool stableBottom =
        mObservedDescent && (nativeGrounded || verticalCollision) &&
        std::abs(verticalSpeed) <= 0.05f &&
        (mStableBottomTicks == 0u || std::abs(deltaY) <= 0.01f);
    mStableBottomTicks =
        stableBottom
            ? static_cast<std::uint8_t>(
                  std::min<unsigned>(mStableBottomTicks + 1u, 3u))
            : 0u;
    if (mStableBottomTicks >= 3u)
      mRecovering = true;

    mLastY = worldY;
    mLastAge = age;
    return mRecovering ? correction(worldY) : std::nullopt;
  }

  [[nodiscard]] bool recovering() const noexcept { return mRecovering; }

private:
  [[nodiscard]] std::optional<float> correction(float worldY) noexcept {
    if (worldY >= mEntryY - 0.03f) {
      mRecovering = false;
      mCompleted = true;
      return std::nullopt;
    }
    return 0.06f;
  }

  float mEntryY{};
  float mLastY{};
  std::int32_t mLastAge{};
  std::uint8_t mStableBottomTicks{};
  bool mInitialized{};
  bool mObservedDescent{};
  bool mRecovering{};
  bool mCompleted{};
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

struct StackXZOffset {
  float x{};
  float z{};
};

struct RenderSpacePoint {
  float x{};
  float y{};
  float z{};
};

// ActorRenderData::position is relative to the renderer's current origin.
// Convert a persistent world-space anchor through the live owner's matching
// world/render pair so camera-origin shifts cannot move the stored visual.
[[nodiscard]] constexpr RenderSpacePoint renderOriginForWorldAnchor(
    RenderSpacePoint anchorWorld, RenderSpacePoint ownerWorld,
    RenderSpacePoint ownerRender) noexcept {
  return {ownerRender.x + anchorWorld.x - ownerWorld.x,
          ownerRender.y + anchorWorld.y - ownerWorld.y,
          ownerRender.z + anchorWorld.z - ownerWorld.z};
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
  // Exact per-route dry-ground support. A source can land and merge between
  // two render frames; retaining the previous airborne delta would otherwise
  // freeze that model above the block surface.
  float groundOffsetY{};
  float waterSampleBias{};
  // Retained liquid visuals remember the survivor's last non-bob base so
  // they can copy native sink/rise displacement immediately after a merge.
  float fluidFollowBaseY{};
  float fluidTransitSample{};
  DropFluidKind fluid{DropFluidKind::None};
  bool grounded{};
  bool fluidBobbing{};
  bool fluidFollowSampled{};
  bool fluidTransitSampled{};
};

// Immutable native state captured immediately before Actor::remove. It is
// deliberately small enough to travel through the existing fixed signal ring
// and prevents a delayed merge from trusting a stale render-space position.
struct DropRemovalSnapshot {
  float worldX{};
  float worldY{};
  float worldZ{};
  float verticalSpeed{};
  std::int32_t age{};
  DropFluidKind fluid{DropFluidKind::None};
  bool nativeGrounded{};
  bool verticalCollision{};
  bool valid{};
};

[[nodiscard]] constexpr bool canRetainDropPose(
    const DropVisualPose &source,
    const DropVisualPose &destination) noexcept {
  if (!isFluid(source.fluid))
    return source.grounded;
  return source.fluid == destination.fluid;
}

// Rebase the last render orientation/model correction onto the actor's final
// native world position. Native final contact is authoritative on dry land;
// same-fluid sources can continue to the surface through advanceFluidAnchor.
[[nodiscard]] inline bool poseAtRemoval(
    const DropVisualPose &lastRendered, float lastActorWorldY,
    const DropRemovalSnapshot &removal, const DropVisualPose &destination,
    DropVisualPose &result,
    const DropVisualPose *previousDestination = nullptr) noexcept {
  if (!removal.valid || !std::isfinite(lastActorWorldY) ||
      !std::isfinite(removal.worldX) || !std::isfinite(removal.worldY) ||
      !std::isfinite(removal.worldZ) ||
      !std::isfinite(removal.verticalSpeed))
    return false;

  result = lastRendered;
  result.worldX = removal.worldX;
  result.worldZ = removal.worldZ;
  result.fluid = removal.fluid;
  // A rapid re-throw/merge can expose an OnGround flag for one stale tick.
  // Require the independent vertical-collision component before freezing a
  // dry source, otherwise that stale flag becomes a permanent hovering anchor.
  const bool dryStableContact =
      !isFluid(removal.fluid) && removal.nativeGrounded &&
      removal.verticalCollision && removal.verticalSpeed <= 0.025f &&
      std::abs(removal.verticalSpeed) <= 0.085f &&
      removal.worldY <= lastActorWorldY + 0.025f;
  result.grounded = dryStableContact;
  result.baseWorldY = dryStableContact
                          ? removal.worldY + lastRendered.groundOffsetY
                          : removal.worldY +
                                (lastRendered.baseWorldY - lastActorWorldY);
  result.fluidBobbing = isFluid(removal.fluid) &&
                        lastRendered.fluid == removal.fluid &&
                        lastRendered.fluidBobbing;
  const bool reusableDestinationBase =
      previousDestination &&
      previousDestination->fluid == destination.fluid &&
      std::isfinite(previousDestination->baseWorldY);
  result.fluidFollowBaseY =
      reusableDestinationBase ? previousDestination->baseWorldY
                              : destination.baseWorldY;
  result.fluidFollowSampled = isFluid(removal.fluid) &&
                              removal.fluid == destination.fluid;
  result.fluidTransitSampled = false;

  return canRetainDropPose(result, destination);
}

// A complete source lineage can change live owners before any intervening
// render (A -> B -> C). Rebase every same-fluid node to C's preceding base and
// sample; otherwise inherited anchors continue following B and either pause or
// jump by an unrelated native delta on their first visible frame.
inline void rebaseFluidAnchorOwner(
    DropVisualPose &pose, const DropVisualPose &destination,
    const DropVisualPose *previousDestination,
    float previousDestinationSample) noexcept {
  if (!isFluid(pose.fluid) || pose.fluid != destination.fluid)
    return;

  const bool reusableDestinationBase =
      previousDestination &&
      previousDestination->fluid == destination.fluid &&
      std::isfinite(previousDestination->baseWorldY);
  pose.fluidFollowBaseY =
      reusableDestinationBase ? previousDestination->baseWorldY
                              : destination.baseWorldY;
  pose.fluidFollowSampled = true;

  if (reusableDestinationBase &&
      std::isfinite(previousDestinationSample)) {
    pose.fluidTransitSample = previousDestinationSample;
    pose.fluidTransitSampled = true;
  } else {
    pose.fluidTransitSampled = false;
  }

  if (!destination.fluidBobbing ||
      std::abs(pose.baseWorldY - destination.baseWorldY) > 0.001f)
    pose.fluidBobbing = false;
}

// Keep a removed liquid source visually coupled to the surviving ItemActor.
// While Minecraft is still sinking/rising the survivor, copy its exact non-bob
// Y displacement so the retained origin reacts on the same frame. Once that
// target becomes stationary, close any remaining vertical separation at the
// bounded legacy water/lava rate. Returns true only when the retained base has
// reached the survivor base and may share its bob phase.
[[nodiscard]] inline bool advanceFluidAnchor(
    DropVisualPose &pose, float targetBaseY, float sample) noexcept {
  if (!isFluid(pose.fluid) || !std::isfinite(pose.baseWorldY) ||
      !std::isfinite(targetBaseY) || !std::isfinite(sample))
    return false;

  if (pose.fluidBobbing)
    return true;

  float targetDelta = 0.0f;
  if (pose.fluidFollowSampled) {
    targetDelta = targetBaseY - pose.fluidFollowBaseY;
  } else {
    pose.fluidFollowSampled = true;
  }
  pose.fluidFollowBaseY = targetBaseY;

  // A teleport/chunk discontinuity must rebase the reference rather than drag
  // a frozen visual several blocks through the world. Ordinary liquid motion
  // is far below this threshold.
  constexpr float kMaxNativeFollowDelta = 0.75f;
  if (!std::isfinite(targetDelta) ||
      std::abs(targetDelta) > kMaxNativeFollowDelta) {
    pose.fluidTransitSample = sample;
    pose.fluidTransitSampled = true;
    return false;
  }
  pose.baseWorldY += targetDelta;

  float delta = 0.0f;
  if (pose.fluidTransitSampled)
    delta = sample - pose.fluidTransitSample;
  pose.fluidTransitSample = sample;
  pose.fluidTransitSampled = true;

  const float distance = targetBaseY - pose.baseWorldY;
  if (std::abs(distance) <= 0.001f) {
    pose.baseWorldY = targetBaseY;
    pose.fluidBobbing = true;
    return true;
  }

  // Do not add a second ascent while the native survivor itself is moving.
  // This is the key difference from 0.16.0's delayed fake-anchor path.
  if (std::abs(targetDelta) > 0.001f || !std::isfinite(delta) ||
      delta <= 0.0f || delta > 10.0f)
    return false;

  const float speed = pose.fluid == DropFluidKind::Lava ? 0.02f : 0.04f;
  const float step = speed * delta;
  pose.baseWorldY += std::clamp(distance, -step, step);
  if (std::abs(targetBaseY - pose.baseWorldY) <= 0.001f) {
    pose.baseWorldY = targetBaseY;
    pose.fluidBobbing = true;
    return true;
  }
  return false;
}

[[nodiscard]] constexpr bool withinDropAnchorBudget(
    std::size_t destinationAnchors, std::size_t sourceAnchors,
    std::size_t maximumAnchors) noexcept {
  return destinationAnchors < maximumAnchors &&
         sourceAnchors < maximumAnchors - destinationAnchors;
}

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
    rootAnchor.pose.waterSampleBias +=
        waterSampleDelta * fluidMotionScale(rootAnchor.pose.fluid);
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
        node.pose.waterSampleBias +=
            waterSampleDelta * fluidMotionScale(node.pose.fluid);
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

  template <typename Visitor>
  void forEachMutable(DropVisualLineage &lineage, Visitor &&visitor)
      noexcept(noexcept(visitor(std::declval<DropVisualAnchor &>()))) {
    auto index = lineage.head;
    for (std::size_t guard = 0;
         index != kNoDropVisualAnchor && guard < Capacity; ++guard) {
      if (index >= Capacity)
        return;
      auto &anchor = mAnchors[index];
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
