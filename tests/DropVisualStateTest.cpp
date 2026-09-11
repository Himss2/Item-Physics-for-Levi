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
  using itemphysics::LavaBottomRecovery;
  using itemphysics::DropRemovalSnapshot;
  using itemphysics::RenderSpacePoint;
  using itemphysics::advanceFluidAnchor;
  using itemphysics::javaVisualCopyCount;
  using itemphysics::poseAtRemoval;
  using itemphysics::rebaseFluidAnchorOwner;
  using itemphysics::renderOriginForWorldAnchor;

  // Fire-resistant items must complete Minecraft's native lava sink before a
  // bounded recovery begins. Repeated render/tick calls at the same age must
  // not advance the detector, and the correction stops at the recorded entry
  // surface instead of becoming a permanent velocity floor.
  LavaBottomRecovery lavaRecovery{};
  if (lavaRecovery.update(64.0f, -0.10f, false, false, 1, true) ||
      lavaRecovery.update(63.7f, -0.08f, false, false, 2, true) ||
      lavaRecovery.update(63.2f, -0.04f, true, true, 3, true) ||
      lavaRecovery.update(63.2f, -0.04f, true, true, 3, true) ||
      lavaRecovery.update(63.2f, -0.04f, true, true, 4, true))
    return 30;
  const auto recoveryStart =
      lavaRecovery.update(63.2f, -0.04f, true, true, 5, true);
  if (!recoveryStart || !closeEnough(*recoveryStart, 0.06f))
    return 31;
  const auto recoveryMiddle =
      lavaRecovery.update(63.6f, -0.01f, false, false, 6, true);
  if (!recoveryMiddle || !closeEnough(*recoveryMiddle, 0.06f))
    return 32;
  if (lavaRecovery.update(63.98f, 0.03f, false, false, 7, true))
    return 33;

  // Disabled, non-fire-resistant and non-authoritative paths are exact
  // passthrough and clear any prior recovery state.
  if (lavaRecovery.update(60.0f, 0.0f, true, true, 8, false) ||
      lavaRecovery.update(60.0f, 0.0f, true, true, 9, true,
                          false) ||
      lavaRecovery.update(60.0f, 0.0f, true, true, 10, true,
                          true, false))
    return 34;

  const float nan = std::numeric_limits<float>::quiet_NaN();

  // Regression: the mod must not fight Minecraft while an item sinks and then
  // rises. Every pre-surface frame is the exact native interpolated Y; the
  // custom bob starts only after three distinct stable game ticks.
  itemphysics::FluidVisualBase liveBase{};
  if (!closeEnough(liveBase.update(64.0f, 64.0f, -0.08f, 10,
                                   DropFluidKind::Water, false), 64.0f))
    return fail("fluid entry changed the initial world height");
  if (!closeEnough(liveBase.update(63.97f, 64.0f, -0.08f, 10,
                                   DropFluidKind::Water, false), 63.97f))
    return fail("repeat render was not native during liquid entry");
  if (!closeEnough(liveBase.update(63.90f, 63.80f, -0.10f, 11,
                                   DropFluidKind::Water, false), 63.90f) ||
      !closeEnough(liveBase.update(63.85f, 63.90f, 0.10f, 12,
                                   DropFluidKind::Water, false), 63.85f) ||
      !closeEnough(liveBase.update(63.95f, 64.00f, 0.10f, 13,
                                   DropFluidKind::Water, false), 63.95f))
    return fail("native sink/rise was replaced by a visual ascent");
  if (liveBase.bobbing())
    return fail("bob started while native buoyancy was still moving");
  if (!closeEnough(liveBase.update(64.00f, 64.00f, 0.0f, 14,
                                   DropFluidKind::Water, false), 64.00f) ||
      !closeEnough(liveBase.update(63.995f, 64.00f, 0.0f, 14,
                                   DropFluidKind::Water, false), 63.995f) ||
      !closeEnough(liveBase.update(64.00f, 64.00f, 0.0f, 14,
                                   DropFluidKind::Water, false), 64.00f) ||
      !closeEnough(liveBase.update(64.00f, 64.00f, 0.0f, 15,
                                   DropFluidKind::Water, false), 64.00f) ||
      liveBase.bobbing())
    return fail("surface latch counted frames instead of game ticks");
  if (!closeEnough(liveBase.update(64.00f, 64.00f, 0.0f, 16,
                                   DropFluidKind::Water, false), 64.00f) ||
      !liveBase.bobbing())
    return fail("stable native surface never enabled bobbing");
  if (!closeEnough(liveBase.update(63.99f, 63.99f, -0.01f, 17,
                                   DropFluidKind::Water, false), 64.00f))
    return fail("native surface jitter leaked into custom bobbing");
  // A slow native drift after latching must not pull the rendered item back
  // under water. This was the delayed potion/fish/bone sinking regression.
  if (!closeEnough(liveBase.update(63.45f, 63.45f, -0.01f, 18,
                                   DropFluidKind::Water, false), 64.00f) ||
      !liveBase.bobbing())
    return fail("small native drift released the confirmed surface");
  // A genuine fast relocation remains a reset boundary.
  if (!closeEnough(liveBase.update(62.0f, 62.0f, -0.4f, 19,
                                   DropFluidKind::Water, false), 62.0f) ||
      liveBase.bobbing())
    return fail("large real relocation left a floating ghost behind");
  if (!closeEnough(liveBase.update(60.0f, 60.0f, 0.0f, 20,
                                   DropFluidKind::None, false), 60.0f) ||
      liveBase.bobbing())
    return fail("leaving fluid retained the old surface anchor");
  if (!std::isnan(liveBase.update(nan, 60.0f, 0.0f, 21,
                                  DropFluidKind::Lava, false)))
    return fail("invalid visual position was not rejected");

  // Stable liquid at a pool bottom or mid-column is not a surface. It cannot
  // acquire bobbing until a real upward phase has been observed first.
  itemphysics::FluidVisualBase bottomBase{};
  for (int age = 30; age < 40; ++age) {
    const bool onBottom = age < 35;
    const float tickY = age < 35 ? 50.0f : 50.02f;
    const float speed = age == 35 ? 0.02f : 0.0f;
    const float result = bottomBase.update(tickY, tickY, speed, age,
                                           DropFluidKind::Lava, onBottom);
    if (!closeEnough(result, tickY) || bottomBase.bobbing())
      return fail("stationary underwater item was mistaken for a surface");
  }

  // Removed actors have no native physics of their own. Both water and lava
  // anchors therefore adopt the surviving real actor's absolute native Y on
  // every render, with no independent catch-up speed or initialization delay.
  DropVisualPose retainedWater{};
  retainedWater.worldX = 4.0f;
  retainedWater.baseWorldY = 60.0f;
  retainedWater.worldZ = 5.0f;
  retainedWater.xRotSine = 0.25f;
  retainedWater.fluid = DropFluidKind::Water;
  retainedWater.fluidFollowBaseY = 64.0f;
  retainedWater.fluidFollowSampled = true;
  if (!advanceFluidAnchor(retainedWater, 64.0f, 10.0f) ||
      !closeEnough(retainedWater.baseWorldY, 64.0f))
    return fail("retained water anchor did not adopt native owner Y");
  if (!advanceFluidAnchor(retainedWater, 64.15f, 11.0f) ||
      !closeEnough(retainedWater.baseWorldY, 64.15f))
    return fail("retained water anchor did not follow native owner ascent");
  if (!closeEnough(retainedWater.worldX, 4.0f) ||
      !closeEnough(retainedWater.worldZ, 5.0f) ||
      !closeEnough(retainedWater.xRotSine, 0.25f))
    return fail("retained fluid transit changed XZ or orientation");
  if (!advanceFluidAnchor(retainedWater, 64.15f, 12.0f) ||
      !closeEnough(retainedWater.baseWorldY, 64.15f))
    return fail("stationary native water owner moved its anchor");

  DropVisualPose retainedLava{};
  retainedLava.baseWorldY = 40.0f;
  retainedLava.fluid = DropFluidKind::Lava;
  retainedLava.fluidFollowBaseY = 41.0f;
  retainedLava.fluidFollowSampled = true;
  if (!advanceFluidAnchor(retainedLava, 41.0f, 20.0f) ||
      !closeEnough(retainedLava.baseWorldY, 41.0f))
    return fail("retained lava anchor did not adopt native owner Y");
  if (!advanceFluidAnchor(retainedLava, 41.10f, 21.0f) ||
      !closeEnough(retainedLava.baseWorldY, 41.10f))
    return fail("retained lava anchor did not follow native owner ascent");
  if (!advanceFluidAnchor(retainedLava, 41.10f, 22.0f) ||
      !closeEnough(retainedLava.baseWorldY, 41.10f))
    return fail("stationary native lava owner moved its anchor");

  // Removal admission uses native final state, not the last render's ground
  // latch. Airborne dry sources fail closed; a source that lands between two
  // frames receives its route's real ground support instead of retaining the
  // previous airborne zero-offset and hovering above the block.
  DropVisualPose lastRendered = pose(2.0f, 65.2f, 3.0f);
  lastRendered.grounded = true; // deliberately stale/incorrect render latch
  lastRendered.groundOffsetY = -0.206f;
  DropRemovalSnapshot removal{};
  removal.valid = true;
  removal.worldX = 2.2f;
  removal.worldY = 64.0f;
  removal.worldZ = 3.2f;
  removal.fluid = DropFluidKind::None;
  DropVisualPose retained{};
  DropVisualPose dryDestination = pose(2.3f, 64.0f, 3.3f);
  if (poseAtRemoval(lastRendered, 65.0f, removal, dryDestination, retained))
    return fail("airborne removal trusted a stale grounded render pose");
  removal.nativeGrounded = true;
  if (poseAtRemoval(lastRendered, 65.0f, removal, dryDestination, retained))
    return fail("dry removal trusted a stale on-ground flag without collision");
  removal.verticalCollision = true;
  removal.verticalSpeed = 0.20f;
  if (poseAtRemoval(lastRendered, 65.0f, removal, dryDestination, retained))
    return fail("dry removal trusted stale contact while rising");
  removal.verticalSpeed = 0.0f;
  removal.worldY = 65.1f;
  if (poseAtRemoval(lastRendered, 65.0f, removal, dryDestination, retained))
    return fail("dry removal trusted stale contact above its last actor pose");
  removal.worldY = 64.0f;
  if (!poseAtRemoval(lastRendered, 65.0f, removal, dryDestination, retained) ||
      !closeEnough(retained.worldX, 2.2f) ||
      !closeEnough(retained.baseWorldY, 63.794f) ||
      !closeEnough(retained.worldZ, 3.2f))
    return fail("grounded removal did not apply its calibrated final support");

  // Keep the device-approved rapid ground-spam path: a source already
  // rendered at rest for two game ticks may be retained after the transient
  // collision component disappears. One stale ground tick, movement, rising
  // and fast falling must still fail closed instead of creating a hover.
  DropVisualPose stableRendered = pose(2.0f, 63.794f, 3.0f);
  stableRendered.grounded = true;
  stableRendered.groundOffsetY = -0.206f;
  removal.verticalCollision = false;
  removal.verticalSpeed = 0.0f;
  removal.worldY = 64.02f;
  if (poseAtRemoval(stableRendered, 64.0f, removal, dryDestination,
                    retained))
    return fail("single-tick dry pose created a hovering retained origin");
  stableRendered.stableGroundContact = true;
  if (!poseAtRemoval(stableRendered, 64.0f, removal, dryDestination,
                     retained) ||
      !closeEnough(retained.baseWorldY, 63.814f))
    return fail("confirmed dry anchor required a transient collision flag");
  removal.verticalSpeed = 0.10f;
  if (poseAtRemoval(stableRendered, 64.0f, removal, dryDestination, retained))
    return fail("rising dry removal trusted an old grounded pose");
  removal.verticalSpeed = -0.08f;
  if (poseAtRemoval(stableRendered, 64.0f, removal, dryDestination, retained))
    return fail("fast-falling dry removal trusted an old grounded pose");
  removal.verticalSpeed = 0.0f;
  removal.worldY = 64.20f;
  if (poseAtRemoval(stableRendered, 64.0f, removal, dryDestination, retained))
    return fail("displaced dry removal trusted an old grounded pose");

  removal.worldY = 64.0f;
  removal.nativeGrounded = false;
  removal.fluid = DropFluidKind::Water;
  DropVisualPose waterDestination = dryDestination;
  waterDestination.fluid = DropFluidKind::Water;
  if (!poseAtRemoval(lastRendered, 65.0f, removal, waterDestination, retained) ||
      retained.fluidBobbing)
    return fail("rising fluid removal could not enter retained transit");

  // The first retained frame must copy native survivor motion that happened
  // since the preceding destination render. Initializing against the current
  // base would discard this delta and create the reported one-frame pause.
  DropVisualPose lastRenderedFluid = pose(2.0f, 60.125f, 3.0f);
  lastRenderedFluid.fluid = DropFluidKind::Water;
  lastRenderedFluid.grounded = false;
  DropRemovalSnapshot fluidRemoval = removal;
  fluidRemoval.worldY = 60.0f;
  fluidRemoval.fluid = DropFluidKind::Water;
  fluidRemoval.nativeGrounded = false;
  fluidRemoval.verticalCollision = false;
  DropVisualPose currentWaterDestination = pose(2.3f, 64.15f, 3.3f);
  currentWaterDestination.fluid = DropFluidKind::Water;
  DropVisualPose previousWaterDestination = currentWaterDestination;
  previousWaterDestination.baseWorldY = 64.0f;
  if (!poseAtRemoval(lastRenderedFluid, 60.0f, fluidRemoval,
                     currentWaterDestination, retained,
                     &previousWaterDestination))
    return fail("same-fluid removal rejected the previous survivor base");
  if (!advanceFluidAnchor(retained, currentWaterDestination.baseWorldY,
                          30.0f) ||
      !closeEnough(retained.baseWorldY,
                   currentWaterDestination.baseWorldY))
    return fail("retained fluid origin did not use the native owner Y");

  // An inherited A -> B -> C anchor must immediately switch to C's native Y.
  // There is no independent render-side buoyancy or delayed catch-up path.
  retained.baseWorldY = 60.0f;
  retained.fluidBobbing = true;
  previousWaterDestination.baseWorldY = currentWaterDestination.baseWorldY;
  rebaseFluidAnchorOwner(retained, currentWaterDestination,
                         &previousWaterDestination, 30.0f);
  if (retained.fluidBobbing)
    return fail("new fluid owner retained an unrelated settled latch");
  currentWaterDestination.baseWorldY = 64.35f;
  if (!advanceFluidAnchor(retained, currentWaterDestination.baseWorldY,
                          31.0f) ||
      !closeEnough(retained.baseWorldY,
                   currentWaterDestination.baseWorldY))
    return fail("retained origin did not follow the new native owner");

  // A removed actor has no physics of its own. Dry sources require final
  // ground contact. Same-fluid sources may be retained before the surface
  // because advanceFluidAnchor supplies their missing visual transit.
  DropVisualPose destinationPose = pose(0.0f, 64.0f, 0.0f);
  DropVisualPose airbornePose = pose(0.0f, 65.0f, 0.0f);
  airbornePose.grounded = false;
  if (itemphysics::canRetainDropPose(airbornePose, destinationPose))
    return fail("airborne source was accepted as a permanent anchor");
  DropVisualPose risingWaterPose = airbornePose;
  risingWaterPose.fluid = DropFluidKind::Water;
  destinationPose.fluid = DropFluidKind::Water;
  destinationPose.grounded = false;
  if (!itemphysics::canRetainDropPose(risingWaterPose, destinationPose))
    return fail("rising water source could not enter retained transit");
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
