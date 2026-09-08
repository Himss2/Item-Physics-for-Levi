# Implementation notes: universal visual core 0.13.0

## Source behavior reproduced

Java ItemPhysic `1.8.15` injects at the dropped-item renderer, so spawn origin
does not select an animation. It has one scalar `xRot` and one `yRot` for all
items. `usesBlockLight()` changes only the transform/pivot and copy layout.

The Bedrock implementation follows the same boundary:

1. Hook `ItemRenderer::render` once.
2. Read `OnGroundFlagComponent` from the actor ECS registry. If a mob-drop path
   omits it, require stable world Y on four ItemActor age ticks, or two when
   `VerticalCollisionFlagComponent` confirms contact. Once latched, only actual
   ItemActor vertical velocity can release it; camera sneak cannot. A live or
   four-tick-grace `WasInWaterFlagComponent` explicitly excludes stable surface
   Y from this ground fallback.
3. Advance one scalar rotation from `age + partialTick` only while airborne and
   outside water. In water, retain the last scalar angle and compose Bedrock's
   ItemActor position with a small render-only sampled vertical wave.
4. Choose block or flat pivot; never choose a separate airborne motion family.
   Classification controls ground height and whether a thin structural model
   takes its corrected contact pose after—not before—contact. Horizontal block
   models preserve their native world-up thin axis. No visual-AABB centre
   subtraction is applied because Bedrock's rendered block-item origin is not
   the AABB centre; applying it pushed slabs and trapdoors below the floor. The
   same world-up pose is selected while `WasInWaterFlagComponent` is active.
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

Java's bytecode applies `realtimeDeltaTicks * 0.25 * rotateSpeed`, doubles that
step while airborne, and divides it by `1 + viscosity` in fluid. CreativeCore's
Fabric implementation returns `fluid.getTickDelay(level) / 5`, which is one for
water. The Bedrock adaptation keeps the exact doubled air step but intentionally
freezes custom roll while `WasInWaterFlagComponent` is active. The vertical
render wave uses `+/-0.015` extrema, an eight-tick lower hold, approximately
`0.10` radians/tick while moving, and a twenty-tick upper hold. Ground-only Y
corrections are never applied while airborne or floating.

Java bytecode applies no skull- or Dragon-Head-specific pivot: every
`usesBlockLight()` model follows the same block transform. The former Bedrock
adapter added a local-Z pivot of `-0.035` for ordinary heads and `-0.12` for a
Dragon Head. After the Java X+90 basis, that pivot produced a horizontal
translation proportional to `pivotZ * sin(xRot)`, so the model visibly orbited
the ItemActor and could move over a hole or into adjacent terrain. Version
0.8.7 removed that divergent pivot and routed heads through the universal Java
block matrix, making the XZ centre invariant for the complete airborne flip and
every frozen landing angle. Its remaining absolute-value ground formula still
assumed that Dragon Head was symmetric along model Z, however, so opposite
final angles received the same clearance even though the elongated jaw/snout
exists only on the negative-Z side.

Version 0.8.8 kept the centred Java matrix unchanged and introduced a
sign-aware support function, but approximated the negative-Z jaw as only twice
the positive-Z depth. That was still too symmetric. The canonical Dragon Head
geometry spans `X = [-8,+8]` and `Z = [-24,+6]`: side radius 8, positive-Z
cranium depth 6, and negative-Z jaw/snout depth 24.

Version 0.8.9 therefore uses the complete `8:6:24` support ratio. After the
Java X+90 basis, a point's vertical projection remains
`sin(xRot) * X - cos(xRot) * Z`. The world-space scale is solved from the
previously approved positive-Z 45-degree clearance, so that angle remains
exactly `+0.070` while the deficient jaw-facing half of the rotation receives
the missing support. The correction is continuous, applied only after
native/fallback ground contact, contains no XZ translation, and adds no
trigonometric work because it reuses the actor's precomputed sine and cosine.

Device testing showed that even the complete external support profile could
not make arbitrary grounded head angles reliable. `SkullBlockRenderer` applies
private model transforms after the ItemActor matrix, and Dragon Head adds an
internally animated jaw. Those internal transforms are not represented by the
block visual AABB available at the current hook boundary. Raising every angle
enough for the worst case would merely replace clipping with visible hovering.

Version 0.9.0 therefore kept the exact Java scalar flip for every airborne head
but stopped preserving an arbitrary roll after contact. It initially selected
`+pi/2` or `-pi/2` from ItemActor entity-ID parity. Device testing then showed
that the private Bedrock skull transform exposes those two outer-matrix signs
as undesirable opposite-facing results rather than a useful visual variation.

Version 0.9.1 kept the same contact boundary but replaced that parity branch
with one fixed grounded `xRot = 0` prone pose. The actor's native `yRot` remains
unchanged, preserving varied horizontal direction. Device testing of that new
pose exposed two different private renderer origins: the normal skull was fully
below the surface while part of DragonHeadModel remained visible.

Version 0.9.2 therefore keeps the exact same pose and separates only their
ground Y calibration. Normal skulls move from `-0.105` to `+0.015` (a `+0.120`
lift), while Dragon Head moves from `-0.0791` to `-0.015` (a `+0.0641` lift).
The exact `minecraft:dragon_head` identifier selects the Dragon value; all
other block-shape-83 skulls use the normal value. There is no pre-contact
change, interpolation through unsafe angles, local pivot, horizontal orbit, or
per-frame bounds work. If the actor genuinely becomes airborne again, normal
Java rotation resumes from the selected prone angle.

Version 0.10.0 turns the seven class-specific ground constants into independent
relaxed atomics, preserving their 0.9.2 values as defaults. Levi Mod Menu uses
integer milliblocks (`-300..+300`) so Android displays and reports exact values
without float-slider rounding. A config callback parses the complete integer
with `from_chars`, rejects malformed/overflow input, converts by `0.001`, and
updates only the selected family. Normal skull and Dragon Head stay separate.
No config change clears visual states or touches rotation, contact, stack,
classification, or water detection.

Version 0.11.0 bakes the device-measured final values for the completed
families: FlatItem `-0.206`, generic ShapedBlock `-0.205`, FullBlock `-0.087`,
HorizontalThin `-0.178`, normal Head `+0.165`, and Dragon Head `+0.203`.
Only Shield, Banner, Fence/Gate, and Scaffolding retain relaxed-atomic live
calibration, initially `+0.001`, `+0.001`, `-0.205`, and `-0.205`.

Those four cases are selected by a `GroundCalibration` subtype stored beside
the existing `HeightClass`. The subtype is consulted only by `heightOffset`;
it does not participate in ground-pose selection, Java airborne rotation,
contact detection, stack layout, water logic, or shadow handling. Shield and
Banner are exact/suffix item identifiers, fence-family matching includes both
`_fence` and `_fence_gate`, and Scaffolding is exact. Consequently the new
sliders cannot regress generic shaped items or alter their animation.

The ineffective water-speed setting is removed. The render-only wave is fixed
at `sin((age + partialTick) * 0.08 + phase) * 0.025`, 20 percent slower than
0.10. Bedrock's native ItemActor Y remains unmodified because replacing or
filtering it would change the already-approved surface rise and water height.

Version 0.12.0 bakes the remaining device measurements: Shield `-0.147`,
Banner `-0.159`, Fence/Gate `-0.171`, and Scaffolding `-0.081`. Their setters,
four float atomics, integer parser, and Mod Menu sliders are deleted. The
`GroundCalibration` subtype remains because it routes each identifier to its
native constant without changing any pose or motion decision.

The water transition returns to the former motion rate, but the timing is no
longer a continuously slowed sine. The approved cycle holds its lower endpoint
for about eight ticks, follows a half-sine transition, holds the upper endpoint
for about twenty ticks, and mirrors the transition downward. Its vertical range
shrinks from `+/-0.025` to `+/-0.015` block. Ninety-one precomputed one-tick
samples and linear partial-tick interpolation preserve a smooth curve without
calling `sin` or `cos` in the water render path. An entity-hash phase is stored
once in `VisualState`, so per-render work is bounded to integer indexing, two
table reads, and a lerp. Native ItemActor Y and the approved `+0.125` surface
lift remain untouched.

This is an intentional Bedrock-only compatibility exception: full blocks still
freeze at their exact contact angle, while only `HeightClass::Head` receives a
deterministic rest pose. The isolated 0.9.1 calibration also lowers
`HeightClass::FlatItem` to `-0.150` and `HeightClass::ShapedBlock` to `-0.165`.
The final 0.9.2 step is only another `-0.010` for each class. No other height
class, water correction, classification rule, or animation path changed.

The render hot path also reuses component-storage addresses for the current ECS
registry. The cache is discarded whenever the registry or any of its bucket,
node, or sentinel pointers changes, and missing storages are retried rather
than cached as permanent misses. Each actor's X/Y rotation sine and cosine are
computed once outside the model-copy loop. This removes repeated registry-chain
walks and up to several redundant trigonometric evaluations without changing
state timing, copy count, or transforms. Release compilation uses `-O2`; LTO,
dead-section collection, identical-code folding, external `c++_shared`, and
full stripping continue to keep the native library well under the build cap.

Bedrock's water-state position leaves the custom item model approximately half
an ItemActor height below the desired visible line. A uniform `+0.125` render-Y
correction is therefore applied to every item class while the native water flag
is live. The bounded `+/-0.015` sampled wave is added after that lift. Its
entity-derived phase keeps nearby items out of lockstep. Dry actors do not
evaluate it, and wet actors use table interpolation with no trigonometric call.
Ground-height selection uses compile-time constants with no float atomics or
string parsing in the render loop. These visual offsets change neither actor
position nor velocity. Horizontal-thin
items select the same world-up basis for `(grounded || inWater)`, while their
complete dry-air transform remains unchanged.

Version 0.13.0 adds the disabled-by-default `Real Item Models` Mod Menu toggle.
The normal path keeps Java's `1/2/3/4/5` thresholds exactly. Exact mode instead
selects the clamped ItemStack count `1..64`, while `Single Model` retains highest
priority and always selects one. This changes only the number of model
submissions; the ItemStack count and ItemActor remain untouched.

The existing render-group detour continues to force one native model per custom
submission. This avoids passing out-of-contract counts above five into
Bedrock's private group helper and prevents its internal three-dimensional copy
layout from reintroducing floating models. Exact copies use a deterministic
square grid with at most eight columns and eight rows. Row X and grid Z are
centred independently, then rotated into world XZ using the ItemActor's one
precomputed yaw sine/cosine pair. There is no Y term in the layout.

Exact mode allocates no per-copy state and performs no extra ECS, block-shape,
fluid, ground, or shadow query. Traits, contact, water phase, world Y, scale,
and all four rotation sine/cosine values are still resolved once per ItemActor
render and shared by up to 64 copies. Geometry emission necessarily remains
linear in the visible count, so disabling the opt-in toggle restores the former
five-copy maximum immediately. The helper and tests are header-only and add no
runtime dependency; the stripped binary remains subject to the same 600-KiB
hard limit.

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
- Was-in-water component hash: `0x78E89F39`
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
