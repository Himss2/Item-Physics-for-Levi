# Implementation notes: universal visual core 0.8.3

## Source behavior reproduced

Java ItemPhysic `1.8.15` injects at the dropped-item renderer, so spawn origin
does not select an animation. It has one scalar `xRot` and one `yRot` for all
items. `usesBlockLight()` changes only the transform/pivot and copy layout.

The Bedrock implementation follows the same boundary:

1. Hook `ItemRenderer::render` once.
2. Read `OnGroundFlagComponent` from the actor ECS registry. If a mob-drop path
   omits it, require stable world Y on four ItemActor age ticks, or two when
   `VerticalCollisionFlagComponent` confirms contact. Once latched, only actual
   ItemActor vertical velocity can release it; camera sneak cannot.
3. Advance one scalar rotation from `age + partialTick`.
4. Choose block or flat pivot; never choose a separate airborne motion family.
   Classification controls ground height and whether a thin structural model
   takes the flat contact pose after—not before—contact.
5. Push the world matrix, apply the Java pose around render position, submit one
   native model, and pop the matrix.
6. Repeat submission using Java's model-count thresholds, but place copies in a
   centered world-XZ row so frozen model rotation cannot turn a side offset into
   a vertical offset.

Classification is cached for the lifetime of each ItemActor visual state.
Ground ECS/motion sampling occurs once per ItemActor age tick, state lookup uses
an eight-slot bounded hash probe, and the relative-shadow component is written
only when its requested hidden/ground state changes. These constraints remove
the render-thread spikes caused by per-frame registry and block-shape work.

Using `age + partialTick` prevents double updates during multiple render passes,
stops naturally while game time is paused, and avoids wall-clock catch-up after
world changes. A discontinuity above ten ticks resets the baseline.

## Analyzed target

- Minecraft: `1.26.45.1`, ARM64
- SHA-256: `444e77434bdd3789a0d90978d06336a99831e78e52955e528258cc375dfa0557`
- Build ID: `868e275cb295e9a275bb29d2258edc2f7dc48761`
- ItemActor constructor: `0xF123A24`, allocation size `0x450`
- ItemRenderer render: RTTI slot `+0x18`, RVA `0xA29F708`
- Render-group helper: `0xA29F338`
- Context world matrix: `0xA5C67C8`
- Context partial tick: `0xA5C678C` (`ldr s0, [x0, #0xB0]; ret`)
- Matrix push/destructor: `0x107CBFFC` / `0x107CC6C0`
- ItemStackBase block-render query: `0xF642ADC`
- On-ground component hash: `0xC29078A0`
- Vertical-collision component hash: `0xC6A02A9A`
- Actor position delta: `0xEC82A68`
- BlockGraphics helpers: `0xA2189DC`, `0xA2189F0`, `0xA219718`, `0xA280E68`
- Relative-shadow storage/emplace: `0xE96A7E4` / `0xE96B68C`

Relevant ItemActor fields are guarded indirectly by the constructor/render
profile and are centralized in `TargetProfile.hpp`: age `+0x428`, bob offset
`+0x434`, item-frame render bypass `+0x440`, stack `+0x390`, count `+0x3B2`.
The loaded ELF GNU Build ID is checked before any hook is installed.

## Intentional stage boundary

This build does not hook `ItemActor::postNormalTick`, `Actor::move`, inventory
drop, or fluid queries. Those hooks should be added only after this visual ABI
and pivot are confirmed on-device. The next stage can then add Java trajectory
physics without changing the renderer architecture.
