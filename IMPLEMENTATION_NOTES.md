# Implementation notes: universal visual core 0.16.5

## Source behavior reproduced

Java ItemPhysic `1.8.15` injects at the dropped-item renderer, so spawn origin
does not select an animation. It has one scalar `xRot` and one `yRot` for all
items. `usesBlockLight()` changes only the transform/pivot and copy layout.

The Bedrock implementation follows the same boundary:

1. Hook `ItemRenderer::render` once.
2. Read `OnGroundFlagComponent` from the actor ECS registry. If a mob-drop path
   omits it, require stable absolute world Y plus
   `VerticalCollisionFlagComponent` for two ItemActor age ticks. Once latched,
   only actual
   ItemActor vertical velocity can release it; camera sneak cannot. A live or
   four-tick-grace `WasInWaterFlagComponent` or `WasInLavaFlagComponent`
   explicitly excludes stable surface Y from this ground fallback.
3. Advance one scalar rotation from `age + partialTick` only while airborne and
   outside fluid. In water or lava, retain the last scalar angle and compose
   Bedrock's ItemActor position with a small render-only sampled vertical wave.
4. Choose block or flat pivot; never choose a separate airborne motion family.
   Classification controls ground height and whether a thin structural model
   takes its corrected contact pose after—not before—contact. Horizontal block
   models preserve their native world-up thin axis. No visual-AABB centre
   subtraction is applied because Bedrock's rendered block-item origin is not
   the AABB centre; applying it pushed slabs and trapdoors below the floor. The
   same world-up pose is selected while either fluid flag is active.
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
freezes custom roll while either native fluid flag is active. The vertical
render wave uses `+/-0.015` extrema, an eight-tick lower hold, approximately
`0.10` radians/tick while moving, and a twenty-tick upper hold. Lava evaluates
the same table at half water speed. Ground-only Y corrections are never applied
while airborne or floating.

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

Bedrock's fluid-state position leaves the custom item model approximately half
an ItemActor height below the desired visible line. A uniform `+0.125` render-Y
correction is therefore applied to every item class while a native water or
lava flag is live. The bounded `+/-0.015` sampled wave is added after that lift. Its
entity-derived phase keeps nearby items out of lockstep. Dry actors do not
evaluate it, and wet actors use table interpolation with no trigonometric call.
Ground-height selection uses compile-time constants with no float atomics or
string parsing in the render loop. These visual offsets change neither actor
position nor velocity. Horizontal-thin
items select the same world-up basis for `(grounded || inFluid)`, while their
complete dry-air transform remains unchanged.

The version 0.13.0 exact-count grid experiment is removed. Rendering every
contained unit at the surviving actor could not satisfy the intended behavior:
when native merging removed the second actor, all of its copies appeared at the
first actor and looked as if they had been attracted there.

Version 0.14.0 introduced disabled-by-default `Separate Drop Visuals`.
Native stack counts and actor lifetime remain authoritative. The new path only
records independently spawned visual origins. Each origin continues to use
Java's normal `1/2/3/4/5` copy thresholds for its original drop-group count;
there is no exact-count grid and no Y component in copy placement.

Version 0.14.1 corrects the coordinate lifetime of those origins. The render
position at `ActorRenderData + 0x10` is relative to the current render origin
and is never persisted. Actor current/previous position accessors at
`0xEC7A020` / `0xEC8EAAC` provide an interpolated absolute position. Each
stored world anchor is converted back through the live survivor's matching
world/render pair on every submission, cancelling both camera-origin shifts
and native survivor movement.

Three analyzed points support the transfer. ItemActor's event-vtable slot
`+0x228` reaches RVA `0xF12537C`, where event `0x45` updates the destination
stack count. Actor's remove slot `+0x60` reaches RVA `0xEC8FC7C`. In
`ItemActor::normalTick`, the merge removal returns at `0xF1245EC` with source
in `x25` and destination in `x24`; the naked ARM64 bridge preserves incoming
`x24` and LR before forwarding the original remove call. Actor UniqueIDs from
RVA `0xEC8B12C` provide a stable correlation key.

For an exact local observation, the render thread moves the source lineage to
the destination lineage. It creates one fixed-size anchor containing the
source's last fully rendered XYZ origin, frozen rotation sine/cosine pairs,
route scale, bob offset, contact/fluid state, and original source-root count.
Any anchors already owned by the source are spliced into the same chain, so an
`A -> B -> C` sequence retains all three independent origins. The live root
keeps following the native destination actor.

Event and remove hooks never allocate or edit renderer state. They append small
signals into a bounded 64-entry lock-protected ring. Rendering drains those
signals into 128 fixed pending slots. The anchor pool has 256 fixed nodes and a
one-slot-per-render stale-state sweep reclaims lineages whose survivor has
disappeared. Toggle-off requests a render-thread clear rather than racing the
hook callbacks.

A remote client may receive destination count event `0x45` and source removal
without the server's exact source pointer. The fallback therefore requires one
and only one source with matching client registry, count delta, item type,
block pointer, spatial merge range, and a 16-event sequence window. It retries
for eight destination renders to tolerate event ordering. Missing, ambiguous,
or capacity-exhausted history collapses into the live destination group. This
is deliberately fail-closed: an omitted separation is preferable to a ghost
copied from an unrelated item.

Version 0.15.0 makes retained fluid origins follow the missing native buoyancy
phase after their source `ItemActor` is removed. Each anchor records whether it
was in water or lava and stores the native-Y-derived base separately from the
bob. While the live survivor remains in fluid, that base approaches the
survivor's current non-bob surface base at `0.04` block/tick for water or
`0.02` block/tick for lava. The approach is driven by `age + partialTick`, is
independent of render FPS, clamps at the target, and never pulls an anchor
downward. Bobbing is withheld during ascent and enabled only after the target
is reached. Water then uses the established waveform; lava uses the same
waveform at half speed. XZ and roll remain frozen.

When a complete lineage changes owners, its waveform bias is rebased by source
sample minus destination sample, scaled for its fluid, preserving phase without
a vertical jump. The anchor pool performs one bounded traversal for ascent only
while `Separate Drop Visuals` is enabled and the survivor is in fluid. It adds
no actor, packet, block query, per-anchor ECS lookup, or per-anchor trigonometry.

The existing render-group detour still forces one native model per custom
submission. `Single Model` means one submission per independent origin, not one
origin for the complete native stack. With separate visuals disabled, the
renderer remains on its prior maximum-five-copy path. With it enabled, model
submission necessarily scales with retained origins, but it adds no entity,
per-copy physics, packet, ECS lookup, block classification, fluid query, or
per-copy trigonometric work. The stripped library remains subject to the same
600-KiB hard limit.

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
- Was-in-lava component hash: `0x832A2768`
- Actor position delta: `0xEC82A68`
- Actor current/previous position: `0xEC7A020` / `0xEC8EAAC`
- BlockGraphics helpers: `0xA2189DC`, `0xA2189F0`, `0xA219718`, `0xA280E68`
- Relative-shadow storage/emplace: `0xE96A7E4` / `0xE96B68C`
- ItemActor event handler: `0xF12537C` (vtable `+0x228`)
- Actor remove: `0xEC8FC7C` (ItemActor vtable `+0x60`)
- Actor UniqueID accessor: `0xEC8B12C`
- Native merge/remove sequence: `0xF1245D8`, return `0xF1245EC`

Relevant ItemActor fields are guarded indirectly by the constructor/render
profile and are centralized in `TargetProfile.hpp`: age `+0x428`, bob offset
`+0x434`, item-frame render bypass `+0x440`, stack `+0x390`, count `+0x3B2`.
The loaded ELF GNU Build ID is checked before any hook is installed.

## Version 0.15.1: live fluid base and physical lava correction

The previous live pose added the custom bob directly to the interpolated native
Y. Retained poses instead added it to their own stable base. `FluidVisualBase`
now rejects small downward native buoyancy oscillations and approaches upward
targets using the same `advanceFluidBase` helper as retained origins. It stores
absolute world Y; render-origin conversion uses the same current world/render
pair as retained visuals. The existing 91-sample wave is unchanged. Live and
retained poses both gate the wave on `fluidBobbing`; no ground constants or
dry-air rotation math changed. Native fluid takes precedence over on-ground
only while resolving fluid rendering, avoiding ground-height lowering at a
pool bottom.

Binary evidence from the supplied archive (full SHA matched):

- normalTick at `0xF124154` checks `Actor::isClientSide` at `0xEC8E9D8`; the
  client path skips the native movement branch.
- The native lava test calls `0xEC8E428` with material `6`, AABB and SubBBs.
  Its inset vector at `0x3146754` is `(0.1, 0.4, 0.1)`. This is a plausible
  reason small ItemActors fail the buoyancy branch despite WasInLava; exact
  in-game collision behavior still requires device confirmation.
- A missed lava test can enable native gravity; at `0xF1246CC` normalTick
  subtracts `0.04` from posDelta.y. Its movement request is `0xEC89668`.
- `0xF63FC00` follows stack +8 -> Item handle -> `0xF6699A4`, which reads
  bit 5 of the Item word at +0x112. The same predicate appears in the ItemActor
  damage path at `0xF124908` and the lava branch at `0xF1242EC`. Use this native
  fire-resistance predicate, not an item-name allowlist.
- Actor::remove sets byte +0x251 at `0xEC90124`. A thread-local nested removal
  watch observes calls through the existing remove hook before forwarding.

The new normalTick detour calls the original exactly once. Only surviving,
authoritative, native-fire-resistant items with current WasInLava membership
receive `posDelta.y = max(posDelta.y, 0.06)` afterwards. Native collision and
network simulation remain responsible for movement. There is no teleport,
extra move call, block/surface scan, new actor or packet. The correction is
disabled when the module is off; leaving lava gets no further velocity floor.
The floor compensates the following native gravity tick; it is not a guarantee
that physical ascent equals the retained visual's fixed `0.02` rate.

Physics lookup uses a local component cache, never the render cache/state.
The post-tick removal watch is active independently of Separate Drop Visuals;
removed actors are not dereferenced after the original returns. A missing
remove observer or optional physics fingerprint disables the physics hook.
uninstall removes normalTick before removing the lifecycle observer.

Remote-server ItemActors are intentionally not modified by a client-only mod.
The physical correction applies to integrated singleplayer and host-side
simulation. The visual base is not an exact fluid-surface solver: small level
drops can retain the old base until the 0.25-block reset threshold. Tests cover
the state math, gates, removed-actor lifetime, ground/fluid priority and camera
coordinate conversion; they do not establish on-device behavior or FPS.

No CMake source-list change is required: production changes remain in existing
translation units and headers. The two tests/support headers are host doubles
and are never included by the Android target. Ground calibration, native
merging, visual removal, and existing menu toggles remain intact.

Verification for this revision: DropVisualStateTest and FluidRuntimeTest pass
with GCC C++20; both also pass with AddressSanitizer + UndefinedBehaviorSanitizer
(leak checking disabled). Runtime fixtures exercise post-tick ordering,
fireproof/lava/authority/enable gates, removal during tick with actual fixture
deallocation, fluid/ground priority, all ten calibrated ground categories, and
waveform holds/amplitude. The existing Mod Menu toggle harness also passes.
Production translation units pass host syntax compilation with `-Wall -Wextra
-Wpedantic -Werror`. Full SHA and all 25 profile fingerprints match the supplied
ZIP. The older scratch visual harness targets a removed pre-0.15 header/API;
its ground/wave checks were covered by the current runtime fixture instead.
No Android NDK build, actual hook installation, gameplay run, release `.so`
size measurement, or device FPS benchmark was performed in this environment.

## Version 0.15.2: native liquid transit and bounded visual separation

Device testing showed that 0.15.1's render-side upward target and physical
`normalTick` lava velocity floor fought native Minecraft in two distinct ways:
water ascent appeared stepped before settling, while fire-resistant lava items
lost their normal sink phase. Both interventions are removed. Neither water nor
lava velocity or position is written by the mod.

`FluidVisualBase` is now a surface latch rather than an ascent simulator. It
returns the exact native interpolated world Y during entry, sinking and buoyant
ascent. It counts stability only once per `ItemActor::age`, requires three
consecutive ticks within `0.012` block and `0.025` vertical speed, then freezes
that base for the existing `+/-0.015` waveform. A fluid change, time rollback,
large tick gap or `0.35`-block relocation resets acquisition. Thus frame rate
cannot accelerate the transition and the custom wave cannot start underwater.

Flat/shaped/special liquid models now use a `+0.085` render lift; full blocks
keep `+0.125`. Non-angle-preserving models snap to their fully prone roll on
fluid entry, while full 3D blocks retain the approved final angle. All dry
ground constants and matrix paths are unchanged.

Separate Drop Visuals now retains a source only when it is grounded, or when a
liquid source is already surface-latched in the same fluid as its survivor.
This deliberately collapses mid-air and still-rising merges into the live
group instead of freezing an actor's final pre-removal frame forever. The
global fixed pool is reduced from 256 to 96 origins, one lineage is capped at
16, and merge application per render is reduced from eight to two. These caps
bound matrix pushes and native model submissions; overflow remains a visual
fallback only and never changes Minecraft item counts.

The optional `ItemActor::normalTick`, client-authority and fire-resistance
profile entries were removed with the physical hook. The remaining 20 binary
fingerprints still match the supplied `libminecraftpe.so` SHA exactly. Host
tests cover native-Y passthrough, tick-based surface acquisition, relocation
reset, liquid orientation, per-class surface height, anchor eligibility and
the 16-origin budget. Android gameplay and FPS remain the required final test.

## Version 0.16.0: five regression corrections

The live liquid tracker is now an ascent-gated state machine. Entry, sinking
and native rise use the current interpolated actor height and do not receive
the custom bob. Surface acquisition is sampled once per `ItemActor::age`,
requires at least `0.08` block of recovery from the lowest observed point, and
then requires three stable non-grounded ticks. A small bottom bounce therefore
cannot be mistaken for the surface. Once acquired, slow native correction no
longer drags potion, fish, bone or other flat models underwater; only a fast
relocation over `0.75` block at speed over `0.20`, a time discontinuity, or
fluid exit resets it. Non-full models use `+0.055` liquid support and full
blocks keep `+0.125`; all compiled dry-ground values are unchanged.

Removed same-fluid sources may now survive a merge before reaching the
surface. Their retained render-only pose advances upward toward the live
survivor's confirmed non-bob base at `0.04` block/tick in water or `0.02` in
lava. Sampling uses `age + partialTick`, clamps without overshoot, and does not
change XZ, orientation, Minecraft position, count, pickup or networking. The
wave begins only at the target. Traversal remains inside the existing fixed
96-node global pool and 16-node per-lineage limit.

Merge admission no longer trusts the last render frame. `Actor::remove`
captures an immutable native position, velocity, age, ground, collision and
fluid snapshot before forwarding the original. The last render's model-space
correction is rebased onto that final absolute world position. Dry roots
require native ground at removal; same-fluid roots enter the visual ascent;
missing, stale, non-finite, wrong-item, wrong-identity, ambiguous and out-of-
range evidence fails closed into the live group. ECS entity-slot reuse is
validated against UniqueID plus registry. The mob-drop ground fallback now
uses absolute `Actor::getPosition` Y and requires vertical collision for two
distinct game ticks, so camera motion and airborne apices cannot create a
permanent floating anchor.

The fire-resistant lava repair returns in a bounded form. Exact fingerprints
guard `ItemActor::normalTick` (`0xF124154`), `Actor::isClientSide`
(`0xEC8E9D8`) and the native stack fire-resistance predicate (`0xF63FC00`).
The original tick always runs exactly once. The state records natural descent,
waits for three position-stable bottom-collision ticks while velocity remains
inside the native `-0.04` gravity residual range, then
applies `posDelta.y = max(posDelta.y, 0.06)` only until the first recorded lava
height. It never affects water, burning items, disabled modules or remote
clients, and it is disabled if the remove-lifecycle observer is unavailable.
A thread-local removal watch prevents post-original access to an actor removed
inside its tick.

Mod Menu persistence uses `pl::config::ConfigFile<ItemPhysicsConfig>` schema
version 10. Only `singleModel`, `separateDropVisuals` and `hideItemShadow` are
owned by this file; Levi remains the single owner of the master module toggle.
Load supplies the menu's current values, each callback updates the renderer's
atomic flag and saves under a mutex, and Preloader's temporary-file/rename path
provides crash-safe replacement. Obsolete v9 fields are discarded during the
versioned rewrite.

Host regression tests cover surface gating, delayed native drift, bottom
bounce rejection, retained water/lava transit, removal-time admission,
absolute ground fallback, entity identity reuse, every compiled height class,
liquid prone orientation, bounded lava recovery, native tick call-through,
client gating and removal during tick. Production units compile with strict
warnings using host boundary doubles, and every fingerprint matches the exact
supplied SHA-256 library. Android hook installation, device gameplay, FPS and
the stripped `614400`-byte size gate still require the Android CI/device run.


## Version 0.16.1: liquid-height and retained-anchor synchronization

The `0.16.0` split liquid support (`+0.055` for non-full models versus
`+0.125` for full blocks) was the direct cause of the reported 2D/shaped
sinking and inconsistent lava height. Version `0.16.1` restores the
class-independent `+0.125` support used by the device-approved `0.15.x`
baseline. Dry-ground calibration constants and the current bob waveform are
unchanged.

Retained same-fluid origins no longer wait for the live survivor to enter its
bobbing state before they can move. Each anchor records the survivor's previous
non-bob base Y and copies the survivor's actual native sink/rise displacement
on every render. If the survivor stops while the retained origin still has a
vertical gap, the origin closes that remainder at the bounded legacy rate
(`0.04` block/tick water, `0.02` lava). A `0.75`-block discontinuity rebases
the follow reference rather than dragging a visual through a teleport/chunk
change. The retained bob flag is synchronized with the live surface latch, so
catching the survivor at the pool bottom cannot start the custom wave early.

Rapid dry merge admission is also stricter. `Actor::remove` already captured
both the native on-ground and vertical-collision components; `poseAtRemoval`
now requires both before a dry source can become a permanent anchor. This
prevents a transient/stale on-ground flag from freezing a just-thrown item in
mid-air while keeping ambiguous evidence fail-closed in the live group.

Host regressions now cover the restored common water/lava support, immediate
retained native-Y following, bounded water/lava catch-up, and rejection of a
dry removal whose on-ground flag is not corroborated by vertical collision.
Android gameplay remains the final validation for visual placement and FPS.

## Version 0.16.2: between-frame contact and first-render transit

Rapid drops exposed two remaining timing gaps. First, a source can complete its
native landing and merge after its last airborne render but before the next
grounded render. Version `0.16.1` accepted the final native contact but rebased
the last airborne render delta, which is zero. The retained model therefore
missed the route-specific dry support and appeared above the ground. Each live
pose now carries its exact compiled ground offset. A corroborated dry removal
uses `removal.worldY + groundOffsetY`, while a removal that is still rising,
exceeds the stable-motion bound, or moved above its last actor pose fails
closed. No ground constants or matrix transforms changed.

Second, retained liquid transit now receives both the survivor's preceding
non-bob base and its preceding render sample. Native survivor displacement is
copied on the first merge render; when that displacement is zero, the same
render can spend the real elapsed tick fraction on the bounded `0.04` water or
`0.02` lava catch-up. This removes the initialization-frame pause without
advancing by render count or changing the native ItemActor.

The pending-merge application bound is restored from two to sixteen, matching
the existing per-lineage origin cap. A rapid local `A -> ... -> P` burst can
therefore resolve before its first survivor draw instead of spreading visual
ownership across several frames. The ordinary no-pending-signal path still
performs one bounded fixed-array scan, and global/per-lineage memory limits are
unchanged.

## Version 0.16.5: v0.16.2 rollback with native-owner fluid anchors

This release is rebuilt directly from the `0.16.2` source after device testing
showed that the generalized `0.16.4` water/lava bottom-recovery path increased
load and worsened liquid motion. None of the `0.16.4` water velocity writes,
extra water component lookup, generalized fluid-recovery table, persistent
surface-candidate state, or lava count-preservation experiment is included.
Water ItemActors therefore use Minecraft's native `normalTick` physics without
any position or velocity write from this mod. The narrowly bounded, fire-proof
lava recovery already present in `0.16.2` is unchanged.

Fake anchors are render-only and cannot own a real Minecraft physics body.
Instead of simulating a second `0.04`/`0.02` block-per-tick ascent, every
same-fluid anchor now adopts the surviving real ItemActor's exact non-bob world
Y each render. This makes live and retained origins react together and reach
the same native surface while preserving each origin's independent X/Z and
orientation. The live surface latch remains the sole gate for the existing
`+/-0.015` bob waveform.

Water retains the class-independent `+0.125` visual support. Lava alone uses a
class-independent `+0.055` support, lowering every render route by `0.070`
block without touching gameplay position or velocity. Ground calibrations and
all item classification remain unchanged.

The only post-`0.16.2` dry behavior retained is the device-approved rapid-drop
anchor admission. A no-collision removal path requires two distinct grounded
render ticks, absolute vertical speed at most `0.025`, and no more than `0.075`
block drift. A source landing between renders still requires native collision;
rising, fast-falling, displaced, or one-tick stale poses collapse into the live
group rather than becoming hovering anchors.
