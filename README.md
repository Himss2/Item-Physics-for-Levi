# Levi Item Physics

ARM64 LeviLaunchroid native mod targeting Minecraft Bedrock `1.26.45.1`.
Version `0.8.8` keeps the device-approved airborne/full-block/decorated-pot,
water, contact, and fast render paths intact while making Dragon Head ground
contact correct for every frozen flip angle.

## Implemented in 0.8.8

- One `ItemRenderer::render` hook covers every dropped `ItemActor`, regardless
  of whether it came from Q, a dead mob, a broken container, a block drop,
  player death, a dispenser, or a network spawn.
- Every model uses the same Java `xRot` law in dry air. Model classification
  selects only the Java block/flat pivot and a render-origin height correction;
  it never selects a different airborne animation.
- There is no quaternion impulse, landing spring, shaped-block pre-alignment,
  head animation, or special shield/banner animation.
- Dry-air rotation is `realtimeDeltaTicks * 0.25 * 2`; water freezes the last
  angle and retains only native vertical movement.
- Flat items snap to their ground pose on native `OnGroundFlagComponent`.
  A conservative fallback handles mob drops that omit that flag: stable world
  Y across distinct ItemActor age ticks is required, while vertical collision
  shortens confirmation from four samples to two. It never predicts contact.
- Full 3D blocks keep the exact contact angle. Thin structural models such as
  fences, ladders, bars, panes, slabs, trapdoors, carpets, pressure plates, and
  rails use a corrected contact pose only after landing. Slabs, trapdoors,
  carpets, and other naturally horizontal block models keep their native
  world-up axis at contact; their complete airborne transform is unchanged.
- Ground offsets are tuned independently for flat, thin, shaped, head, special,
  and full-block models. Heads retain their frozen angle and now use the exact
  same centered block transform as Java ItemPhysic. The former head-only
  local-Z pivot was removed because it translated a Dragon Head horizontally
  by up to `0.12 * sin(xRot)`, producing an orbit around the ItemActor. The
  ground baselines remain `-0.095` for normal heads and `-0.105` for Dragon
  Head. Ordinary symmetric heads retain the periodic diagonal-clearance term.
  Dragon Head instead uses a sign-aware support projection for its asymmetric
  shape: the negative-Z jaw/snout is represented as twice the positive-Z head
  depth. This raises only final angles whose elongated jaw points toward the
  floor, while keeping the flip centre fixed and preserving the previously
  correct angles.
- `WasInWaterFlagComponent` is sampled once per ItemActor tick. A floating item
  remains airborne, never enters the stable-Y ground fallback, freezes its last
  roll angle, and receives a class-independent `+0.125` visual surface lift.
  Only Bedrock's native vertical float/bob remains; there is no custom water
  spin. Naturally horizontal block items also use their world-up pose in water,
  so slabs, trapdoors, carpets, rails, and pressure plates cannot stand upright
  at the surface.
- Java stack-copy thresholds are used: `1 / 2 / 3 / 4 / 5` models at
  `1 / 2 / 17 / 33 / 49` items.
- The first tick remains vanilla, matching Java ItemPhysic's warm-up rule.
- Vanilla bob/spin is bypassed only while a custom copy is submitted.
- Item count is never modified. A scoped render-group hook forces one native
  model per custom submission.
- Stack copies are centered in a deterministic horizontal row in world XZ, so
  merging items cannot create a copy above the base model.
- `Single Model` and `Hide Item Shadow` are available as Mod Menu toggles.
- Per-entity traits are cached, state lookup probes at most eight slots, contact
  ECS queries run once per game tick, and shadow storage is touched only when
  its requested state changes. Known ECS storage pointers are reused while the
  registry signature remains valid. Rotation sine/cosine pairs are evaluated
  once per ItemActor render and shared by all stack copies instead of being
  recomputed inside the copy loop. Release builds use `-O2` plus LTO, section
  GC, ICF, and stripping: runtime speed is prioritized while the existing
  600-KiB hard package limit remains enforced.
- The lightweight `item_physics.main` entry is registered in Levi Mod Menu;
  toggling it changes only the render path and does not reinstall hooks.

This stage changes rendering only. Java gameplay physics, fluids, charged-Q,
damage/fire rules, and pickup behavior belong to later device-tested stages.

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
33, 48, 49, 64`; every visible copy must stay on one horizontal plane. Finally,
toggle `Single Model` and `Hide Item Shadow` both ways while items are visible,
then sneak/stand repeatedly while already-grounded items remain in view. Drop
flat, block, head, slab, and trapdoor items into still and flowing water: they
must keep their last roll angle, show only vertical surface motion, and sit
visibly higher by the same amount. Slabs/trapdoors/carpets must remain
horizontal. They must not acquire a ground offset until they touch a solid
floor. Test normal, Creeper, Wither Skeleton, Piglin, Player, and Dragon heads
at axis and diagonal landing angles; none may enter the floor or hover.
