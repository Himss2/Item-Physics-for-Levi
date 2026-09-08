# Levi Item Physics

ARM64 LeviLaunchroid native mod targeting Minecraft Bedrock `1.26.45.1`.
Version `0.13.0` keeps the device-approved airborne/full-block/decorated-pot,
water-contact, ground-height, and sampled-wave paths intact, and adds an
optional exact visual model for every item contained by a merged stack.

## Implemented in 0.13.0

- One `ItemRenderer::render` hook covers every dropped `ItemActor`, regardless
  of whether it came from Q, a dead mob, a broken container, a block drop,
  player death, a dispenser, or a network spawn.
- Every model uses the same Java `xRot` law in dry air. Model classification
  selects only the Java block/flat pivot and a render-origin height correction;
  it never selects a different airborne animation. Heads are the only contact
  exception: after actual ground contact they use one deterministic prone pose.
- There is no quaternion impulse, landing spring, pre-contact alignment, or
  special shield/banner animation.
- Dry-air rotation is `realtimeDeltaTicks * 0.25 * 2`; water freezes the last
  angle and permits only vertical native/render movement.
- Flat items snap to their ground pose on native `OnGroundFlagComponent`.
  A conservative fallback handles mob drops that omit that flag: stable world
  Y across distinct ItemActor age ticks is required, while vertical collision
  shortens confirmation from four samples to two. It never predicts contact.
- Full 3D blocks except heads keep the exact contact angle. Thin structural
  models such as fences, ladders, bars, panes, slabs, trapdoors, carpets,
  pressure plates, and rails use a corrected contact pose only after landing.
  Slabs, trapdoors, carpets, and other naturally horizontal block models keep
  their native world-up axis at contact; their complete airborne transform is
  unchanged.
- Ground offsets are independent for flat, thin, shaped, head, special, and
  full-block models. The device-approved fixed values are `-0.206` for 2D,
  `-0.205` for generic shaped, `-0.087` for full block, `-0.178` for slab/thin,
  `+0.165` for normal skull, `+0.203` for Dragon Head, `-0.147` for Shield,
  `-0.159` for Banner, `-0.171` for Fence/Gate, and `-0.081` for Scaffolding.
  All heads keep the centred Java block transform and
  complete Java flip while airborne. On the first confirmed ground frame they
  switch to fixed `xRot = 0`. Their native per-actor yaw remains untouched, so
  they can point in different horizontal directions without selecting an
  up/down-facing rest alternative. Normal skulls and Dragon Head deliberately
  use separate render-origin corrections.
- All ground heights are native constants. The four calibration sliders,
  setters, parser, and relaxed float atomics have been removed. Fence detection
  still covers fence gates and Nether Brick Fence; Scaffolding remains isolated
  from all other shaped items. These subtypes change only ground height, never
  pose, classification, airborne transform, or landing behavior.
- `WasInWaterFlagComponent` is sampled once per ItemActor tick. A floating item
  remains airborne, never enters the stable-Y ground fallback, freezes its last
  roll angle, and receives a class-independent `+0.125` visual surface lift.
  The render-only wave now travels only `+/-0.015` block. It rests about eight
  ticks at the bottom, rises with the former approximately `0.10`-radian/tick
  half-sine motion, rests about twenty ticks at the top, then falls with the
  mirrored motion. A 91-sample table plus partial-tick interpolation replaces
  the extra per-render sine call, and an entity-derived phase prevents nearby
  drops from moving in lockstep. The mod never filters or overrides native Y,
  velocity, buoyancy, pickup, or networking. Naturally horizontal block items
  also use their world-up pose in water, so slabs, trapdoors, carpets, rails,
  and pressure plates cannot stand upright at the surface.
- With `Real Item Models` off, Java stack-copy thresholds remain unchanged:
  `1 / 2 / 3 / 4 / 5` models at `1 / 2 / 17 / 33 / 49` items.
- `Real Item Models` is an optional Mod Menu toggle, disabled by default. When
  enabled it renders exactly `1..64` models from the merged stack's actual
  count. It is visual-only: there is still one ItemActor, one cached visual
  state, and unchanged item count, merge, pickup, collision, buoyancy, and
  networking behavior.
- Exact models use a compact deterministic grid in world XZ. Every copy shares
  the actor's approved pose and ground/water Y; no copy receives a local or
  world Y displacement. The original Java 1..5-copy horizontal row is retained
  when exact mode is disabled.
- `Single Model` has explicit priority over `Real Item Models`: if both toggles
  are enabled, only one model is submitted.
- The first tick remains vanilla, matching Java ItemPhysic's warm-up rule.
- Vanilla bob/spin is bypassed only while a custom copy is submitted.
- Item count is never modified. A scoped render-group hook forces one native
  model per custom submission.
- Stack copies always remain in world XZ, so merging items cannot create a copy
  above the base model.
- `Single Model`, `Real Item Models`, and `Hide Item Shadow` remain in the same
  Mod Menu entry and apply immediately.
- Per-entity traits are cached, state lookup probes at most eight slots, contact
  ECS queries run once per game tick, and shadow storage is touched only when
  its requested state changes. Known ECS storage pointers are reused while the
  registry signature remains valid. Rotation sine/cosine pairs are evaluated
  once per ItemActor render and shared by all stack copies instead of being
  recomputed inside the copy loop. Ground height now requires no float atomic
  load or render-time parsing. Water bob performs two table reads and one
  linear interpolation instead of an additional trigonometric call. Release
  builds use `-O2` plus LTO, section GC,
  ICF, and stripping: runtime speed is prioritized while the existing 600-KiB
  hard package limit remains enforced.
- Exact rendering necessarily emits more model geometry. No per-copy physics,
  ECS lookup, classification, water query, or trigonometry is added, but a
  64-item stack still submits 64 visual models. Keeping the option disabled
  preserves the previous maximum of five submissions and its performance.
- The lightweight `item_physics.main` entry is registered in Levi Mod Menu;
  toggling it changes only the render path and does not reinstall hooks.

The calibration stage is complete: all measured values are compiled into the
native runtime, and Mod Menu no longer exposes temporary ground-height sliders.

This stage changes rendering only. Java gameplay physics, fluids, charged-Q,
damage/fire rules, and pickup behavior belong to later device-tested stages.

## Known compatibility exception

- Grounded heads intentionally no longer retain an arbitrary final airborne
  roll angle. This is a documented Bedrock-only compatibility exception to
  Java ItemPhysic, chosen to eliminate the severe renderer-origin clipping.

## Strict binary guard

The hook activates only for the analyzed binary:

- SHA-256: `444e77434bdd3789a0d90978d06336a99831e78e52955e528258cc375dfa0557`
- Build ID: `868e275cb295e9a275bb29d2258edc2f7dc48761`
- `ItemRenderer::render`: RTTI `12ItemRenderer`, vtable `+0x18`, expected RVA
  `0xA29F708`

Runtime checks the GNU Build ID, exact render RVA, and fingerprints for every
helper it calls. On mismatch the mod loads in safe inactive mode.

Validate a local game library with:

```bash
python3 tools/validate_profile.py /path/to/libminecraftpe.so
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

Output: `dist/arm64-v8a/levi-item-physics.levipack`.
The package contains only `manifest.json` and `liblevi_item_physics.so`.
Both build scripts and CI reject a raw `.so` larger than `614400` bytes.

## First device test

Test each source (Q, sheep/other mob death, chest destruction, block drop,
player death, dispenser, command/network spawn) with 2D items, tools, armor,
full blocks, decorated pots, fences, ladders, bars, heads, shields, and every
banner color. Watch the last airborne and first ground frames: there must be no
pre-contact alignment. Merge equal drops and test counts `1, 2, 16, 17, 32,
33, 48, 49, 64`; every visible copy must stay on one horizontal plane. Test
those counts once with `Real Item Models` disabled and once enabled. Exact mode
must produce the actual count in a compact XZ grid; enabling `Single Model` at
the same time must reduce it to one. Finally, toggle `Hide Item Shadow` both
ways while items are visible, then sneak/stand repeatedly while already-grounded
items remain in view. Drop
flat, block, head, slab, and trapdoor items into still and flowing water: they
must keep their last roll angle, show only vertical surface motion, and sit
visibly higher by the same amount. Confirm the wave pauses longer at its upper
point than its lower point and adds no rotation. Slabs/trapdoors/carpets must
remain horizontal. They must
not acquire a ground offset until they touch a solid floor. Test normal,
Creeper, Wither Skeleton, Piglin, Player, and Dragon heads.
Their airborne flip must remain unchanged; on contact each must use the same
prone roll pose, retain its own horizontal yaw, and remain fully above the
floor. Verify Shield, Banner, Fence/Gate, and Scaffolding against their compiled
heights at eye level.
