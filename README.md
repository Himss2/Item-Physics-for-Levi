# Levi Item Physics

Native LeviLaunchroid mod that changes **dropped-item rendering** to a Java-style Item Physics look while keeping world/server physics vanilla.

## v0.1-dev architecture

- C++ lifecycle mod (`PL_REGISTER_MOD`).
- Mod Menu module id: `item_physics.main`.
- `ItemRenderer::render` is resolved through Itanium RTTI name `12ItemRenderer`, then hooked at vtable slot `+0x18`.
- The hook is activated only when the resolved function matches the analyzed `libminecraftpe.so` fingerprint.
- The analyzed binary SHA-256 is:
  `4492ce15ceda3bb4865788a50e8d35b1bbafd45b62ce441440240a693a97d749`.
- Vanilla item bob/spin is bypassed only during the draw call by temporarily setting the ItemActor item-frame flag.
- `OnGroundFlagComponent` drives airborne tumble versus landing settle.
- Matrix changes are isolated with `MatrixStack::push()` and the corresponding `MatrixStackRef` destructor.

## Mod Menu

The menu exposes:

- Item Physics toggle
- Single Model
- Tumble Speed
- Settle Speed
- Ground Tilt
- Height Offset

Settings are persisted in `config/config.json`.

## Safety profile

This first test build intentionally uses a strict fingerprint. If Minecraft's `libminecraftpe.so` differs from the analyzed build, the mod still loads and registers in Mod Menu, but the renderer hook stays inactive instead of writing profile-specific actor offsets.

## Build

Requirements:

- Android NDK r28c (`28.2.13676358` recommended)
- CMake >= 3.22
- Ninja
- Git / network access for CMake FetchContent
- `zip` on Linux/macOS

Linux / GitHub Actions:

```bash
export ANDROID_NDK_HOME=/path/to/android-ndk-r28c
scripts/build-android.sh
```

Windows PowerShell:

```powershell
./build.ps1 -Ndk "C:\Android\Sdk\ndk\28.2.13676358"
```

Output:

```text
dist/arm64-v8a/levi-item-physics.levipack
```

The `.levipack` contains:

```text
manifest.json
liblevi_item_physics.so
config/config.json
config/config.schema.json
```

## First test checklist

1. Import the `.levipack` in LeviLaunchroid.
2. Start the matching Minecraft build.
3. Confirm **Item Physics** appears in Mod Menu.
4. Drop one item and one stack of 64.
5. Toggle Item Physics off/on while looking at the dropped item.
6. Test a normal 2D item, a block item, a tool/sword, shield, and banner.
7. If there is a crash or wrong pivot, save the Levi log / tombstone before changing offsets.

This is intentionally `0.1.0-dev`: the hook/profile path is implemented, while model-class-specific pivot tuning (especially shield/banner and 3D blocks) is expected to need one or more device tests.
