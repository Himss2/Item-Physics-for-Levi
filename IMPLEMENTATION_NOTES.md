# Implementation notes / RE mapping

## Hook

Analyzed target:

- RTTI: `12ItemRenderer`
- vptr slot: `+0x18`
- analyzed `ItemRenderer::render` RVA: `0xA29F7A8`

The runtime does **not** hardcode that render RVA for resolution. It resolves RTTI/vtable at runtime and only uses the known RVA as a fingerprint/reference.

## Profile offsets currently guarded by fingerprint

- Actor registry: `Actor + 0x10`
- Actor entity id: `Actor + 0x18`
- Item stack count: `ItemActor + 0x3B2`
- `mIsInItemFrame`: `ItemActor + 0x440`
- `ActorRenderData::mActor`: `+0x00`
- ActorRenderData render position: `+0x10`
- BaseActorRenderContext::getWorldMatrix RVA: `0xA5C6868`
- MatrixStack::push(bool) RVA: `0x107CC67C`
- MatrixStackRef destructor RVA: `0x107CCD40`

## ECS

`OnGroundFlagComponent` entt/FNV hash:

```text
0xC29078A0
```

Ground state is read from the actor's ECS registry rather than raycasting every frame.

## Current expected tuning work

The first device test should focus on **pivot/orientation**, not hook discovery. The hook, vanilla-bob bypass, count bypass, ground flag, and MatrixStack path are implemented from RE. Shield/banner and block/3D model origin corrections still need visual validation.
