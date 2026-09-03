# Levi Item Physics

ARM64 LeviLaunchroid native mod targeting Minecraft Bedrock `1.26.45.1`.
Version `0.8.1` keeps the universal Java ItemPhysic `1.8.15` animation and
restores Bedrock-model-specific ground-height correction.

## Implemented in 0.8.1

- One `ItemRenderer::render` hook covers every dropped `ItemActor`, regardless
  of whether it came from Q, a dead mob, a broken container, a block drop,
  player death, a dispenser, or a network spawn.
- Every model uses the same Java `xRot` law. Model classification selects only
  the Java block/flat pivot and a render-origin height correction; it never
  selects a different animation.
- There is no quaternion impulse, landing spring, shaped-block pre-alignment,
  head animation, or special shield/banner animation.
- Airborne rotation is `realtimeDeltaTicks * 0.25 * 2`.
- Flat items snap to their ground pose on native `OnGroundFlagComponent`.
  A conservative fallback handles Bedrock drops that omit that flag: vertical
  collision, near-zero vertical motion, and stable position must coexist on two
  distinct game ticks. It never predicts contact in mid-air.
- Block items freeze at the exact contact angle (`oldRotation=false`, Java
  default).
- Ground offsets are restored for flat, thin, shaped, head, special, and full
  block models. Full blocks are lowered slightly. Dragon Head keeps its exact
  frozen landing angle and receives tilt-dependent clearance for its jaw.
- Java stack-copy thresholds are used: `1 / 2 / 3 / 4 / 5` models at
  `1 / 2 / 17 / 33 / 49` items.
- The first tick remains vanilla, matching Java ItemPhysic's warm-up rule.
- Vanilla bob/spin is bypassed only while a custom copy is submitted.
- Item count is never modified. A scoped render-group hook forces one native
  model per custom submission.
- Vanilla item shadow is preserved.
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

Test each source (Q, mob death, chest destruction, block drop, player death,
dispenser, command/network spawn) with flat items, tools, armor, full blocks,
partial blocks, heads, shields, and banners. Watch the last airborne frame and
first ground frame closely: there must be no intentional pre-contact alignment.
Also test counts `1, 2, 16, 17, 32, 33, 48, 49, 64`, pause/unpause, and several
frame rates.
