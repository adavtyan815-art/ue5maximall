# Viewmode / Furniture Preview — How It Works (2026-08-25)

> Supersedes `archive/VIEWMODE_SYSTEM.md`. Most of that document's lighting/rotation design is
> still accurate and worth reading for rationale; its central premise — "the preview happens at
> the real booth's world position inside the real room" — is **no longer true** (see below and
> Findings F-1).

## Files

| File | Role |
|---|---|
| `FurnitureConfigurator/Preview/FurniturePreviewActor.h/.cpp` (492 + 1529) | The client-local preview actor: mesh mirror, isolation, lighting rig, calibration, clearance, orbit/zoom |
| `awsTutorial_PlayerController.cpp` (`OpenFurniturePreview` / `CloseFurniturePreview`) | Lifecycle, booth relocation, view target, overlay, auto-tour |
| `FurnitureConfigurator/UI/ViewmodeOverlayWidget.h/.cpp` | Back-button overlay |

## Big picture

Viewmode isolates one component of a booth (cabinet, closet, countertop, sink, faucet, mirror)
for 360° inspection. The preview actor is strictly client-local (`bReplicates = false`,
tick disabled — fully event-driven).

**Current flow** (the 2026-08-24 change): before the preview spawns, the player controller
teleports the **source booth itself** to `ViewModeRelocationLocation` (default (-10000, 0, 0),
rotation (0, 90, 0)). The preview actor then spawns at the *relocated* booth's position, so the
whole session happens in a "studio" area far from the showroom. On close, the booth's original
transform is restored exactly. This replaced the earlier design where the preview lived at the
booth's real showroom position.

## Preview construction

`AFurniturePreviewActor` mirrors the booth's component tree (`MeshRoot` → cabinet → doors /
countertop / sink / faucet; mirror + closet on the root). `ConfigureMesh` puts every mesh on
**lighting channel 1 only**, movable, no collision, no shadow casting. A SpringArm
(`bDoCollisionTest = false`, deliberate) carries the camera; two shadowless `URectLightComponent`s
(key + fill, channel 1, candela units, `IndirectLightingIntensity = 0`) hang off the SpringArm
root so their distance to the subject never changes with zoom.

`LoadProductPreview(ProductData, ActiveState, SourceBooth)`:
1. Hides the source booth (`SetActorHiddenInGame`), records it for restore.
2. `SuspendConflictingPostProcessMaterials()` — removes only the stencil-keyed outline
   material(s) (`PostProcessMaterialsToSuspend`, default `M_PostProcessOutline`, plus a
   name-contains fallback) from every `APostProcessVolume`'s blendables, recording
   volume/object/weight for exact restoration. All other volume settings (exposure, bloom,
   grading) stay live.
3. Copies each booth component's relative transform onto the matching preview component, then
   applies meshes/materials from the product snapshot (using the booth's resolved options and
   baseline transforms for shared components, including the BuiltIn-countertop special cases).
4. Finally copies the booth's **live materials** slot-by-slot over the preview meshes so
   custom RAL/NCS MIDs carry over exactly.

## `SetFocusComponent(Type)`

1. Resolves the per-component `FPreviewComponentConfig` (zoom limits, exposure offset, entry
   yaw offset, fill-rig settings) and restores any previous clearance-hidden geometry.
2. **Isolation**: hides all preview meshes, shows only the focus group (cabinet ⇒ cabinet +
   both doors; closet ⇒ closet + closet doors), sets CustomDepth stencil **250** on the focused
   group only.
3. Resets orbit accumulators and `MeshRoot`, computes the focus pivot from the focused
   component's bounds center.
4. **Swept clearance** (`ResolveClearPivot`): lifts the pivot above floor level by the swept
   radius, then walks outward (forward, forward-diagonals, sideways; 20 cm steps up to
   `MaxPivotSearchDistanceCm`) doing `ECC_WorldStatic` sphere overlap tests until a position is
   free. If none is found and `bAllowGeometryHideFallback` is set, it hides exactly the
   components overlapping the best-effort position (cached; restored on refocus/EndPlay).
5. **Entry view**: yaw = booth yaw + 180° + per-component offset; pitch = `EntryPitchDegrees`
   (default −15°). Max zoom is clamped **once** by a line trace from the pivot backwards
   (30 cm wall margin); initial distance = bounds radius × 2.5 clamped to config limits.
6. **Level-match light calibration** (when `bMatchLevelLighting`, default on):
   `MeasureWorldIlluminanceAt` analytically sums the direct illuminance (per RGB channel) that
   all channel-0 world lights deliver at the *pre-clearance* pivot — directional lights count
   only when a sky trace is unoccluded; point/spot/rect lights within attenuation range and
   with line of sight contribute candelas with inverse-square, UE's radial window, cone/cosine
   falloff (IES/barn doors ignored; hidden actors skipped except the source booth's own
   lights). The key intensity is solved so key + fill deliver the measured lux at the pivot
   (`Key/dK² + Key·FillRatio/dF² = TargetLux`, clamped 20 000 lux / 100 000 cd), and the rig
   color is the lux-weighted light color × the config tint. The measurement is honored
   **unconditionally, including ~0 lux** (in GI-only rooms the correct rig is off — this was a
   deliberate bug fix; see archived doc §4.2b). With matching off, the manual
   `PreviewKeyIntensity` (candelas) applies. Every calibration logs `[PreviewCalib]` lines.
7. Per-component `ExposureCompensation` (EV) on the camera; `WIP_ApplyStencilIsolation` binds
   the stencil-isolation MID (`IsolationFade` 0.8, `TargetStencil` 250) as a camera blendable,
   disables DoF overrides, applies the optional preview vignette.

## Rotation / zoom / reset

`RotatePreview` rotates **the mesh, not the camera**: yaw about world Z, pitch about the
camera's flattened right vector (clamped ±80°), composed as a delta quat applied to the initial
`MeshRoot` orientation, with the mesh root orbited around the cached bounds-center pivot.
Viewing distance is therefore constant by construction. `ZoomPreview` just clamps
`TargetArmLength` between the active min and the entry-clamped max. `ResetRotation` restores
initial orientation, pivot, and distance. The cinematic tour (controller-side timer) simply
calls `RotatePreview` continuously.

## Teardown (`EndPlay` — the guaranteed restore path)

Removes the stencil blendable from the camera, restores the character mesh visibility, unhides
the source booth, restores clearance-hidden components, re-adds all suspended post-process
blendables with their original weights, hides the rig lights. The controller separately
restores the booth transform, view target, control rotation, and input mode.

## What is intentional (do not "fix")

- `SpringArm->bDoCollisionTest = false` — a live collision test would change viewing distance
  during rotation; camera safety is resolved once at entry instead.
- Honoring a ~0 lux calibration result instead of falling back to the manual intensity — the
  fallback was the cause of the Countertop-vs-Cabinet lighting split documented in the archived
  doc (§12).
- Preview meshes never cast shadows; the rig is shadowless — evenness requirement.
- Lumen GI/reflections are not channel-filtered — that is the mechanism that keeps materials
  looking real, not a leak.
