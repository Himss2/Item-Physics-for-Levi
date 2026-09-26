# Flat ground-height investigation — test branch

Base: `8f29f939befee1fdc36413bbbb5253a368ebfe12` (0.16.8).
User report: the same bucket/sword can render correctly or sink, with Separate
Drop Visuals both enabled and disabled. Full blocks, slabs and fences are not
reported affected.

## Binary evidence

Inspected the supplied ARM64 Minecraft 1.26.51.1 binary:

- SHA-256: `b8a6351503d330628335a80e8131acd45291fa9a747465f0f34a31b2346847b4`
- Build ID: `712509dc14ccc233e91f267937dfb46ecdcc4b68`
- All current profile checks match.

Relevant instruction paths:

| RVA | Observation |
| --- | --- |
| `0xFA28260–0xFA28288` | Constructor obtains a random float, multiplies by pi and 2, and stores the phase at actor +0x434. |
| `0xA71224C–0xA7122D4` | Renderer post-translates its matrix by ActorRenderData position. |
| `0xA7122D8–0xA7122DC` | Loads actor +0x440; bit 0 branches directly to 0xA7124A8. The mod sets this bit during rendering. |
| `0xA7122E0–0xA7124A4` | Skipped path computes age/partial-tick bobbing and native rotation. |
| `0xA7115E8`, `0xA711620` | Native bob helper reads actor +0x434 inside the skipped path. |
| `0xA7126A0–0xA7126DC` | Ordinary sprite route scales by 0.3 and calls group helper 0xA711D0C. |
| `0xA711D0C–0xA7120D8` | Group helper uses actor scale interpolation, then dispatches the item renderer. No bob-phase read in this helper. |
| `0xF5842EC–0xF584300` | Scale interpolation reads actor +0x164/+0x168, not +0x434. |
| `0xB2F1CA4–0xB2F1CCC` | Ordinary item path calls transform helper 0xB2F41F4 with stack/model information, not the actor's bob phase. |
| `0xB2F42C8`, `0xB2F4620–0xB2F4624` | Standard non-custom sprite route skips custom-model transforms and returns after scale. |

These observations establish that the outer native bobbing compensation is
inactive and that the standard sprite transform path does not cancel the
mod's phase-dependent translation. Custom resource-pack geometry is outside
this proof.

## Defect and bounded correction

The mod's non-block path post-translates local Z by
`-0.04 - bobPhase * 0.007957747154594767` after the quarter-turn X basis.
For a prone sprite this contributes world Y
`+0.04 + bobPhase * 0.007957747154594767`. Thus identical grounded sprites
receive up to 0.05 blocks of differing support solely from spawn phase.
The branch is shared by live drops and retained origins. Block paths do not
use this expression, matching the reported scope.

For `!traits.block && height == FlatItem && pose.grounded && !isFluid`, use
fixed support 0.05 instead of `phase * scale`. This selects the old support
range's upper endpoint. It eliminates the proven random height spread while
never exceeding the previous theoretical upper envelope. It does not derive
a new collision surface or claim that the endpoint is correct for every
terrain/model. Device testing must establish absolute contact and no hovering.

No change to the height table, native actor data, physics, grounded detection,
rotation, model classification, merge logic, or fluid/air/special/block pivot.
No new hook, scan, allocation or trigonometric call is introduced at runtime.

## Verification and remaining work

The host regression calls the real `onRender` path with a native-boundary
fixture that applies the verified render-position translation. It captures
submitted matrices, sweeps 17 phases, exercises SDV off/on and a retained
origin with a different phase, and checks all five stack copies. It also
checks unchanged air/water/lava and full/shaped/horizontal/special paths,
plus restoration of render position and the item-frame flag.

On the unmodified runtime, grounded FlatItem at phase zero produces
11.959 instead of the chosen common reference 12.009 (render origin 12.125).
The corrected runtime removes this 0.05 spread. This is matrix verification;
the fixture does not reproduce Minecraft's mesh or collision system.

Before merging, test repeated buckets (empty/water/lava) and swords on full
blocks, upper/lower slabs and stairs, SDV off/on, single and merged drops.
Compare 3D drops, airborne tumbling and fluid behavior. If fixed-height sprites
still sink or hover, capture the floor type and resource pack: do not increase
the constant again without investigating mesh/native actor contact.

Manifest stays 0.16.8 with both declared Minecraft versions. This RE verifies
only the supplied 1.26.51.1 binary, not a separate 1.26.52.3 binary.
