# Levi Item Physics

ARM64 LeviLaunchroid native mod targeting Minecraft Bedrock `1.26.45.1`.
Version `0.15.1` preserves the device-approved airborne, landing, ground-height,
head and shadow behavior. Live fluid items now use a stable visual base like
retained drops. An optional guarded native tick hook adds physical buoyancy to
fire-resistant items in lava on the authoritative simulation.

## Implemented behavior

- One `ItemRenderer::render` hook covers every dropped `ItemActor`, regardless
  of whether it came from Q, a dead mob, a broken container, a block drop,
  player death, a dispenser, or a network spawn.
- Every model uses the same Java scalar `xRot` law in dry air. Classification
  selects only the block/flat pivot, contact pose, and ground correction; it
  never selects a different airborne animation.
- Dry-air rotation is `realtimeDeltaTicks * 0.25 * 2`. There is no landing
  spring, quaternion impulse, pre-contact alignment, or special shield/banner
  flight animation.
- Flat and structural items settle only after real contact. A conservative
  stable-Y fallback handles mob drops that omit the native on-ground flag.
  Once contact is latched, camera sneak cannot restart rotation.
- Full 3D blocks retain their final contact angle. Slabs, trapdoors, carpets,
  rails, pressure plates, and other naturally horizontal models retain their
  world-up axis on ground and in water.
- Heads retain the complete Java flip while airborne, then use a deterministic
  prone `xRot = 0` contact pose. Their native per-actor horizontal yaw remains
  untouched. Normal skull and Dragon Head have independent height corrections.
- Device-calibrated ground constants are compiled in: `-0.206` for 2D,
  `-0.205` for generic shaped, `-0.087` for full block, `-0.178` for slab/thin,
  `+0.165` for normal skull, `+0.203` for Dragon Head, `-0.147` for Shield,
  `-0.159` for Banner, `-0.171` for Fence/Gate, and `-0.081` for Scaffolding.
- Water and lava freeze the last roll angle and add only vertical surface
  movement. A class-independent `+0.125` lift places items at the visible line.
  The render-only wave has `+/-0.015` amplitude, an eight-tick lower hold, a
  smooth rise, a twenty-tick upper hold, and a mirrored fall. Its 91-sample
  table avoids an extra trigonometric call in the fluid path. Lava uses half
  the water waveform speed.
- Live water/lava models now approach a stored world-space base at the same
  rate as retained visuals. Small native buoyancy oscillations do not get added
  to the custom bob; bob is withheld while the model is catching up. Large real
  relocations or fluid exits reset the base. Ground height offsets are not
  applied while submerged, even if the native on-ground flag is also present.
- Fire-resistant lava items receive a post-tick upward velocity floor of `0.06`
  blocks/tick. This leaves `0.02` after the native `0.04` gravity subtraction
  when the small-item lava probe misses. Native collision and position updates
  still perform the movement, so the real pickup location moves too. The
  correction stops being applied as soon as native lava membership ends.
  Ordinary items retain native burning/removal, and water physics is unchanged.
- Java stack-copy thresholds remain `1 / 2 / 3 / 4 / 5` visible models at
  counts `1 / 2 / 17 / 33 / 49`. Every copy remains on one world-XZ plane.
- `Single Model` and `Hide Item Shadow` remain immediate Mod Menu toggles.

## Separate Drop Visuals

`Separate Drop Visuals` is optional and disabled by default. It implements the
requested visual behavior without changing Minecraft's stack rules:

1. Minecraft still performs its native count transfer, source removal, pickup,
   collision, save, and network behavior. The separate physical lava correction
   above is independent of this visual toggle.
2. When one complete source `ItemActor` is merged into another, the renderer
   transfers a small snapshot of the source's last visible world position and
   orientation to the surviving actor.
3. The survivor follows its native physics position with the stabilized fluid
   rendering described above. The transferred visual remains
   at the source position instead of jumping to the survivor. Chained merges
   retain the complete sequence of independent drop origins.
4. Each origin uses Java's normal 1-to-5 copy threshold for the count originally
   represented by that drop. `Single Model` reduces each retained origin to one
   model; it does not erase the independent origins.
5. A retained water or lava origin first rises toward the surviving actor's
   non-bob surface height while X/Z stay fixed. It moves at `0.04` block/tick
   in water or `0.02` block/tick in lava, never moves downward, and begins the
   approved vertical bob only after reaching that height. Its phase is rebased
   at transfer so neither ownership change nor bob start causes a jump.

Local-world merges use the exact source and destination UniqueIDs captured at
the analyzed `ItemActor::normalTick` removal call. A remote client does not
receive that source ID in event `0x45`, so its fallback accepts a transfer only
when exactly one recent removed actor matches registry, transferred count, item
type, block pointer, sequence window, and native merge distance. An ambiguous
case deliberately collapses to the surviving live group instead of showing a
ghost at the wrong position.

The tracker uses fixed arrays: no heap allocation, per-copy physics, entity
spawn, packet, block query, or world query is added. The anchor pool is capped at 256 visual
origins and stale states are reclaimed incrementally. With the toggle off, the
two merge observers perform only their disabled branch and the renderer stays
on the original maximum-five-copy path. With it on, geometry cost necessarily
scales with the number of retained independent drop origins.

Retained origins are absolute world-space positions. `ActorRenderData + 0x10`
is used only as the current frame's render origin; the stored position is
converted through the survivor's interpolated world/render pair every frame.
Moving the player, camera, or surviving actor therefore cannot drag a retained
source like a screen-space overlay.

The former `Real Item Models` option and its exact `1..64` compact grid have
been removed. Enabling the new option cannot reconstruct merges that happened
while it was disabled; it starts tracking subsequent merges. Turning it off
clears all retained origins and immediately returns to the normal renderer.

## Known compatibility exceptions

- Grounded heads intentionally do not retain an arbitrary final airborne roll.
  Bedrock's private skull and Dragon Head transforms made that pose clip or
  hover unpredictably, so only the grounded head pose differs from Java.
- A retained drop origin is visual only. Gameplay collision and pickup remain
  at the one native surviving `ItemActor`, by design.
- On a remote server, an ambiguous merge is not separated visually. This
  fail-closed rule prevents unrelated drops from being paired.
- Physical lava correction runs in an integrated world or LAN host simulation.
  A remote server owns its ItemActors: installing this mod only on the client
  cannot physically lift the server's items. Client fluid rendering still works.
- The fluid base rejects small native downward oscillations, rather than querying
  an exact surface mesh. Flowing fluids, unusual surface geometry, and small
  downward level changes still need device validation. It resets when native Y
  falls more than `0.25` block below its stored target.
- Version 0.15.1 is host-tested source, not yet validated in Android gameplay.

## Strict binary guard

The hooks activate only for the analyzed library:

- SHA-256: `444e77434bdd3789a0d90978d06336a99831e78e52955e528258cc375dfa0557`
- Build ID: `868e275cb295e9a275bb29d2258edc2f7dc48761`
- `ItemRenderer::render`: RTTI `12ItemRenderer`, vtable `+0x18`, RVA
  `0xA29F708`
- `ItemActor::handleEntityEvent`: RTTI `9ItemActor`, vtable `+0x228`, RVA
  `0xF12537C`
- `Actor::remove`: ItemActor vtable `+0x60`, RVA `0xEC8FC7C`
- Actor UniqueID accessor: RVA `0xEC8B12C`
- Native merge/removal sequence: RVA `0xF1245D8`, return site `0xF1245EC`

Runtime verifies the GNU Build ID, resolved vtable targets, and exact
instruction fingerprints before installing any hook. A mismatch leaves the mod
loaded in safe inactive mode. If only an optional merge observer cannot be
installed, the approved baseline renderer remains active and the log marks
`Separate Drop Visuals` unavailable.
The lava hook additionally verifies normalTick, client-side and fire-resistance
accessors, and removal-flag instructions. If it cannot be installed, the log
reports physical lava buoyancy unavailable and the visual core stays active.

Validate a local game library with:

```bash
python3 tools/validate_profile.py /path/to/libminecraftpe.so
```

The validator also accepts a ZIP containing `libminecraftpe.so`.

Host regression tests (C++20 compiler; test support is not packaged):

```bash
g++ -std=c++20 -O2 -Isrc tests/DropVisualStateTest.cpp -o /tmp/drop-state-test
/tmp/drop-state-test
g++ -std=c++20 -O2 -Itests/support -Isrc tests/FluidRuntimeTest.cpp src/ItemPhysicsRuntime.cpp src/RttiResolver.cpp -ldl -o /tmp/fluid-runtime-test
/tmp/fluid-runtime-test
```

## Build

Requirements: Android NDK r28c, CMake 3.22+, Ninja, Git, and `zip`.

```bash
export ANDROID_NDK_HOME=/path/to/android-ndk-r28c
scripts/build-android.sh
```

Windows:

```powershell
./build.ps1 -Ndk "C:\Android\Sdk\ndk\28.2.13676358"
```

Output: `dist/arm64-v8a/levi-item-physics.levipack`. The package contains only
`manifest.json` and `liblevi_item_physics.so`. Build scripts and CI reject a raw
`.so` larger than `614400` bytes.

## First device test

First verify the unchanged baseline with Q drops, mob death, chest destruction,
block drops, player death, dispenser drops, and network spawns. Exercise 2D,
tools, armor, full blocks, decorated pots, fence/gate, scaffolding, slabs,
trapdoors, normal heads, Dragon Head, Shield, and Banner on ground and in water.

Then enable `Separate Drop Visuals` and drop equal items one at a time at nearby
but visibly different positions. After native merge, every source model must
remain at its last position and orientation instead of moving to the first
item. Test chained `A -> B -> C` merges, separately dropped multi-item stacks,
partial transfers near stack limits, pickup/despawn of the survivor, water
and lava merges at several depths, `Single Model`, toggle-off cleanup, and
toggle-on restart. A submerged retained origin must keep X/Z fixed, rise to the
survivor's surface height, then bob without rotation; lava must rise and bob at
half water speed for retained visuals. In lava, compare a surviving fireproof
item with an ordinary burning item: the former must physically rise (also test
pickup location), and the latter and its retained visuals must disappear.
Check a deep source-lava pool, a shallow pool, a ceiling, exiting lava, and mod
disable. Compare live and retained fluid bob with Separate Drop Visuals on/off;
move and sneak the camera to check world anchoring. In multiplayer,
create two simultaneous same-item merges; an ambiguous pair may collapse to one
live group but must never create a visual at an unrelated source position.
