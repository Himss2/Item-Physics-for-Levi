#include "DropVisualState.hpp"

#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
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
  value.grounded = true;
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

  const float nan = std::numeric_limits<float>::quiet_NaN();

  // Regression: the mod must not fight Minecraft while an item sinks and then
  // rises. Every pre-surface frame is the exact native interpolated Y; the
  // custom bob starts only after three distinct stable game ticks.
  itemphysics::FluidVisualBase liveBase{};
  if (!closeEnough(liveBase.update(64.0f, 64.0f, -0.08f, 10,
                                   DropFluidKind::Water), 64.0f))
    return fail("fluid entry changed the initial world height");
  if (!closeEnough(liveBase.update(63.97f, 64.0f, -0.08f, 10,
                                   DropFluidKind::Water), 63.97f))
    return fail("repeat render was not native during liquid entry");
  if (!closeEnough(liveBase.update(63.90f, 63.80f, -0.10f, 11,
                                   DropFluidKind::Water), 63.90f) ||
      !closeEnough(liveBase.update(63.85f, 63.90f, 0.10f, 12,
                                   DropFluidKind::Water), 63.85f) ||
      !closeEnough(liveBase.update(63.95f, 64.00f, 0.10f, 13,
                                   DropFluidKind::Water), 63.95f))
    return fail("native sink/rise was replaced by a visual ascent");
  if (liveBase.bobbing())
    return fail("bob started while native buoyancy was still moving");
  if (!closeEnough(liveBase.update(64.00f, 64.00f, 0.0f, 14,
                                   DropFluidKind::Water), 64.00f) ||
      !closeEnough(liveBase.update(63.995f, 64.00f, 0.0f, 14,
                                   DropFluidKind::Water), 63.995f) ||
      !closeEnough(liveBase.update(64.00f, 64.00f, 0.0f, 14,
                                   DropFluidKind::Water), 64.00f) ||
      !closeEnough(liveBase.update(64.00f, 64.00f, 0.0f, 15,
                                   DropFluidKind::Water), 64.00f) ||
      liveBase.bobbing())
    return fail("surface latch counted frames instead of game ticks");
  if (!closeEnough(liveBase.update(64.00f, 64.00f, 0.0f, 16,
                                   DropFluidKind::Water), 64.00f) ||
      !liveBase.bobbing())
    return fail("stable native surface never enabled bobbing");
  if (!closeEnough(liveBase.update(63.99f, 63.99f, -0.01f, 17,
                                   DropFluidKind::Water), 64.00f))
    return fail("native surface jitter leaked into custom bobbing");
  if (!closeEnough(liveBase.update(62.0f, 62.0f, -0.4f, 18,
                                   DropFluidKind::Water), 62.0f) ||
      liveBase.bobbing())
    return fail("large real relocation left a floating ghost behind");
  if (!closeEnough(liveBase.update(60.0f, 60.0f, 0.0f, 19,
                                   DropFluidKind::None), 60.0f) ||
      liveBase.bobbing())
    return fail("leaving fluid retained the old surface anchor");
  if (!std::isnan(liveBase.update(nan, 60.0f, 0.0f, 20,
                                  DropFluidKind::Lava)))
    return fail("invalid visual position was not rejected");

  // A removed actor has no physics of its own. Preserve its origin only after
  // a dry item has landed or a liquid item has reached the custom bob phase;
  // otherwise the last mid-air frame becomes a permanent ghost.
  DropVisualPose destinationPose = pose(0.0f, 64.0f, 0.0f);
  DropVisualPose airbornePose = pose(0.0f, 65.0f, 0.0f);
  airbornePose.grounded = false;
  if (itemphysics::canRetainDropPose(airbornePose, destinationPose))
    return fail("airborne source was accepted as a permanent anchor");
  DropVisualPose risingWaterPose = airbornePose;
  risingWaterPose.fluid = DropFluidKind::Water;
  destinationPose.fluid = DropFluidKind::Water;
  destinationPose.grounded = false;
  if (itemphysics::canRetainDropPose(risingWaterPose, destinationPose))
    return fail("rising water source was accepted before the surface");
  risingWaterPose.fluidBobbing = true;
  if (!itemphysics::canRetainDropPose(risingWaterPose, destinationPose))
    return fail("settled water source could not retain its visual origin");
  destinationPose.fluid = DropFluidKind::Lava;
  if (itemphysics::canRetainDropPose(risingWaterPose, destinationPose))
    return fail("an anchor crossed between different fluids");

  // Rendering more independent origins is intentionally bounded. The 17th
  // source collapses into the live group instead of causing unbounded draw
  // calls and the severe frame drop reported on Android.
  if (!itemphysics::withinDropAnchorBudget(15, 0, 16) ||
      itemphysics::withinDropAnchorBudget(16, 0, 16) ||
      itemphysics::withinDropAnchorBudget(14, 2, 16))
    return fail("separate-drop anchor budget accepted the wrong boundary");

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
