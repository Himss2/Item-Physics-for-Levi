#include "DropVisualState.hpp"

#include <cmath>
#include <cstdint>
#include <iostream>
#include <utility>
#include <vector>

namespace {

int fail(const char *message) {
  std::cerr << message << '\n';
  return 1;
}

bool closeEnough(float actual, float expected) {
  return std::abs(actual - expected) < 0.00001f;
}

itemphysics::DropVisualPose pose(float x, float y, float z) {
  itemphysics::DropVisualPose value{};
  value.worldX = x;
  value.baseWorldY = y;
  value.worldZ = z;
  return value;
}

} // namespace

int main() {
  using itemphysics::DropVisualAnchorPool;
  using itemphysics::DropFluidKind;
  using itemphysics::DropVisualLineage;
  using itemphysics::DropVisualPose;
  using itemphysics::RenderSpacePoint;
  using itemphysics::javaVisualCopyCount;
  using itemphysics::renderOriginForWorldAnchor;

  // Regression: a frozen visual is a world-space anchor, not an old
  // ActorRenderData position. Camera-origin shifts and movement of the live
  // merge survivor must therefore never drag the frozen source with them.
  const RenderSpacePoint frozenWorld{100.0f, 64.0f, 100.0f};
  const RenderSpacePoint firstOwnerWorld{101.0f, 64.0f, 100.0f};
  const RenderSpacePoint firstOwnerRender{5.0f, 2.0f, -3.0f};
  const auto firstFrozenRender = renderOriginForWorldAnchor(
      frozenWorld, firstOwnerWorld, firstOwnerRender);
  if (!closeEnough(firstFrozenRender.x, 4.0f) ||
      !closeEnough(firstFrozenRender.y, 2.0f) ||
      !closeEnough(firstFrozenRender.z, -3.0f))
    return fail("world anchor was not converted into current render space");

  const RenderSpacePoint movedOwnerWorld{102.0f, 64.0f, 100.0f};
  const RenderSpacePoint movedOwnerRender{4.0f, 2.0f, -3.0f};
  const auto secondFrozenRender = renderOriginForWorldAnchor(
      frozenWorld, movedOwnerWorld, movedOwnerRender);
  if (!closeEnough(secondFrozenRender.x, 2.0f) ||
      !closeEnough(secondFrozenRender.y, 2.0f) ||
      !closeEnough(secondFrozenRender.z, -3.0f))
    return fail("camera or survivor movement dragged a frozen world anchor");

  // A stack dropped in one action remains one visual origin. It uses the
  // Java 1..5 copy rule instead of the removed exact-count grid.
  const std::pair<std::uint32_t, std::uint32_t> javaThresholds[] = {
      {1, 1},  {2, 2},  {16, 2}, {17, 3}, {32, 3},
      {33, 4}, {48, 4}, {49, 5}, {64, 5}, {255, 5},
  };
  for (const auto &[count, expected] : javaThresholds) {
    if (javaVisualCopyCount(count) != expected)
      return fail("an original drop group changed Java's copy thresholds");
  }

  DropVisualAnchorPool<8> pool;
  DropVisualLineage first{};
  DropVisualLineage second{};
  pool.observe(first, 1);
  pool.observe(second, 1);

  // Two independently dropped single items must retain two positions after
  // native gameplay merges them into one ItemActor.
  const DropVisualPose firstPose = pose(1.0f, 2.0f, 3.0f);
  if (!pool.merge(first, firstPose, 1, second, 1, 2))
    return fail("valid two-item merge was rejected");
  if (second.rootCount != 1 || second.trackedCount != 2 ||
      pool.anchorCount(second) != 1)
    return fail("two-item merge collapsed or duplicated a visual origin");

  std::vector<DropVisualPose> positions;
  pool.forEach(second, [&](const auto &anchor) {
    positions.push_back(anchor.pose);
  });
  if (positions.size() != 1 || !closeEnough(positions[0].worldX, 1.0f) ||
      !closeEnough(positions[0].baseWorldY, 2.0f) ||
      !closeEnough(positions[0].worldZ, 3.0f))
    return fail("source drop did not freeze at its last visual position");

  // A lineage that already owns a frozen source must carry the complete
  // chain when it later becomes the source of another native merge.
  DropVisualLineage third{};
  pool.observe(third, 1);
  const DropVisualPose secondPose = pose(4.0f, 5.0f, 6.0f);
  if (!pool.merge(second, secondPose, 2, third, 1, 3, 7.5f))
    return fail("chained merge was rejected");
  if (third.rootCount != 1 || third.trackedCount != 3 ||
      pool.anchorCount(third) != 2)
    return fail("chained merge lost an earlier visual origin");

  positions.clear();
  pool.forEach(third, [&](const auto &anchor) {
    positions.push_back(anchor.pose);
  });
  bool foundFirst = false;
  bool foundSecond = false;
  for (const auto &value : positions) {
    foundFirst |= closeEnough(value.worldX, 1.0f) &&
                  closeEnough(value.baseWorldY, 2.0f) &&
                  closeEnough(value.worldZ, 3.0f);
    foundSecond |= closeEnough(value.worldX, 4.0f) &&
                   closeEnough(value.baseWorldY, 5.0f) &&
                   closeEnough(value.worldZ, 6.0f);
  }
  if (!foundFirst || !foundSecond)
    return fail("chained merge changed a frozen source position");

  // When a surviving lineage later becomes a source, water phases must be
  // rebased from the old actor age to the new owner age without a vertical
  // jump. Every inherited anchor receives the same sample delta.
  bool waterBiasAdjusted = true;
  pool.forEach(third, [&](const auto &anchor) {
    waterBiasAdjusted &= closeEnough(anchor.pose.waterSampleBias, 7.5f);
  });
  if (!waterBiasAdjusted)
    return fail("water animation phase jumped during chained merge");

  // Counts belong to original drop groups. A 20-item destination plus a
  // separately dropped 10-item source renders their Java copies separately,
  // never thirty copies at the survivor position.
  pool.reset();
  DropVisualLineage stack20{};
  DropVisualLineage stack10{};
  pool.observe(stack20, 20);
  pool.observe(stack10, 10);
  if (!pool.merge(stack10, pose(-1.0f, 0.0f, 1.0f), 10, stack20, 20, 30))
    return fail("valid stack-group merge was rejected");
  std::uint32_t sourceCopies = 0;
  pool.forEach(stack20, [&](const auto &anchor) {
    sourceCopies += javaVisualCopyCount(anchor.count);
  });
  if (javaVisualCopyCount(stack20.rootCount) != 3 || sourceCopies != 2)
    return fail("merged drop groups did not retain independent Java copies");

  // A removed actor no longer has native buoyancy. Its retained water anchor
  // must therefore approach the live survivor's current surface Y using game
  // time. Five ticks at 0.04 blocks/tick move it exactly 0.20 blocks, and bob
  // starts only after the target has actually been reached.
  DropVisualAnchorPool<4> fluidPool;
  DropVisualLineage waterSource{};
  DropVisualLineage waterDestination{};
  fluidPool.observe(waterSource, 1);
  fluidPool.observe(waterDestination, 1);
  DropVisualPose waterPose = pose(0.0f, 60.0f, 0.0f);
  waterPose.fluid = DropFluidKind::Water;
  if (!fluidPool.merge(waterSource, waterPose, 1, waterDestination, 1, 2,
                       0.0f, 10.0f))
    return fail("water source merge was rejected");
  fluidPool.advanceFluidAnchors(waterDestination, 61.0f, 15.0f);
  DropVisualPose advancedWater{};
  fluidPool.forEach(waterDestination,
                    [&](const auto &anchor) { advancedWater = anchor.pose; });
  if (!closeEnough(advancedWater.baseWorldY, 60.20f) ||
      advancedWater.fluidBobbing)
    return fail("water anchor did not rise by tick time before bobbing");

  fluidPool.advanceFluidAnchors(waterDestination, 60.22f, 15.5f);
  fluidPool.forEach(waterDestination,
                    [&](const auto &anchor) { advancedWater = anchor.pose; });
  if (!closeEnough(advancedWater.baseWorldY, 60.22f) ||
      !advancedWater.fluidBobbing)
    return fail("water anchor overshot its surface or failed to begin bobbing");

  // Lava uses the approved half-speed path: the same five game ticks move
  // only 0.10 blocks. A lower target must never pull an anchor back down.
  fluidPool.reset();
  DropVisualLineage lavaSource{};
  DropVisualLineage lavaDestination{};
  fluidPool.observe(lavaSource, 1);
  fluidPool.observe(lavaDestination, 1);
  DropVisualPose lavaPose = pose(0.0f, 60.0f, 0.0f);
  lavaPose.fluid = DropFluidKind::Lava;
  if (!fluidPool.merge(lavaSource, lavaPose, 1, lavaDestination, 1, 2,
                       0.0f, 10.0f))
    return fail("lava source merge was rejected");
  fluidPool.advanceFluidAnchors(lavaDestination, 61.0f, 15.0f);
  DropVisualPose advancedLava{};
  fluidPool.forEach(lavaDestination,
                    [&](const auto &anchor) { advancedLava = anchor.pose; });
  if (!closeEnough(advancedLava.baseWorldY, 60.10f) ||
      advancedLava.fluidBobbing)
    return fail("lava anchor did not rise at half water speed");
  fluidPool.advanceFluidAnchors(lavaDestination, 59.0f, 16.0f);
  fluidPool.forEach(lavaDestination,
                    [&](const auto &anchor) { advancedLava = anchor.pose; });
  if (!closeEnough(advancedLava.baseWorldY, 60.10f))
    return fail("a lower fluid target pulled an anchor downward");

  // A non-merge count decrease (for example hopper/inventory transfer) must
  // remove visual source groups before leaving stale ghosts behind.
  pool.reconcile(stack20, 20);
  if (stack20.trackedCount != 20 || pool.anchorCount(stack20) != 0 ||
      stack20.rootCount != 20)
    return fail("count decrease left a stale frozen visual");

  // Capacity exhaustion must fail closed: do not partially move a chain or
  // create a duplicated visual. Runtime can then fall back to one live group.
  DropVisualAnchorPool<1> tinyPool;
  DropVisualLineage sourceA{};
  DropVisualLineage sourceB{};
  DropVisualLineage destination{};
  tinyPool.observe(sourceA, 1);
  tinyPool.observe(sourceB, 1);
  tinyPool.observe(destination, 1);
  if (!tinyPool.merge(sourceA, pose(1.0f, 0.0f, 0.0f), 1,
                      destination, 1, 2))
    return fail("first merge did not use the available anchor");
  if (tinyPool.merge(sourceB, pose(2.0f, 0.0f, 0.0f), 1,
                     destination, 2, 3))
    return fail("full anchor pool accepted a partial merge");
  if (destination.trackedCount != 2 ||
      tinyPool.anchorCount(destination) != 1 || sourceB.trackedCount != 1)
    return fail("failed merge mutated an existing lineage");

  tinyPool.release(destination);
  if (destination.initialized || destination.trackedCount != 0 ||
      tinyPool.anchorCount(destination) != 0)
    return fail("pickup/despawn release left visual anchors alive");

  return 0;
}
