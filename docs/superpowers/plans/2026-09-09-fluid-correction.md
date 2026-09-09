# Fluid correction implementation plan

**Goal:** Match live-item bob to retained visual anchors and lift fire-resistant
ItemActors physically out of lava without changing ground calibration or merging.

**Architecture:** One post-normalTick velocity correction on the authoritative
simulation, guarded by native lava membership, native fire resistance, lifecycle,
enable state, and exact binary fingerprints. Render state remains render-owned.
A bounded world-space visual base follows ascent and rejects small native bob
oscillations; live and retained poses use the existing waveform once.

**Tech stack:** C++20, Levi preloader HookHandle, supplied ARM64 1.26.45.1 binary.
**Spec:** User-approved design in conversation (physical ItemActor in lava,
water motion unchanged, bob +/-0.015 with 20/8 holds and lava phase scale 0.5).

## Constraints

- Preserve all ground heights, rotations, toggles and native merge/despawn.
- No world scan, new entity, renderer write to physics, or extra dependency.
- Keep distributed mod under 500–600 KB; Android release size requires NDK build.
- Remote-server simulation remains server-owned; do not fake physical movement
  by changing only a remote client velocity.
- More than three changed files: deliver every file separately.

## Task 1 — verify native boundary

- [x] Verify normalTick, getPosDelta, client-side predicate, fireproof predicate,
  and remove lifecycle against the exact SHA-256 profile.
- [x] Record the observed call sequence and fingerprints in TargetProfile.hpp
  and the profile validation script, including removal flag setter evidence.

## Task 2 — test then implement fluid state

- [x] Add failing tests in tests/DropVisualStateTest.cpp for stationary lava
  motion becoming positive, water/non-fireproof/remote/disabled passthrough,
  small native Y oscillations not doubling bob, large relocation and fluid exit.
- [x] Run `g++ -std=c++20 -O2 -Isrc tests/DropVisualStateTest.cpp -o /tmp/fluid-test`
  and execute it; establish the missing-behavior failure.
- [x] Add minimal helpers in src/DropVisualState.hpp; share anchor advancement
  with the live world-space base and retain the existing bob lookup table.
- [x] Integrate src/ItemPhysicsRuntime.hpp and .cpp: optional guarded tick hook,
  no post-removal access, independent component lookup, live base conversion.
- [x] Run host state tests and runtime boundary fixtures, including original
  tick call-through, destruction during tick and module-off behavior.

## Task 3 — verification and handoff

- [x] Run native-boundary fingerprint validation on supplied archive bytes.
- [x] Compile all production units with C++20 warnings as errors; run regression
  and sanitizer tests. Keep Android/device validation explicitly outstanding.
- [x] Update manifest.json and src/LeviItemPhysics.cpp to 0.15.1; document
  behavior, binary evidence and device checklist in README.md and
  IMPLEMENTATION_NOTES.md. CMake source list remains unchanged.
- [x] Preserve previous files. Deliver changed files individually after upload.

Validation limitations and the device test checklist are recorded in README.md
and IMPLEMENTATION_NOTES.md. Android build size and gameplay remain unmeasured.
