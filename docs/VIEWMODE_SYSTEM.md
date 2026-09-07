# Viewmode / Furniture Preview — System Documentation

> **Scope**: The complete Viewmode (furniture preview) system: architecture, lighting model, rotation, clearance, zoom, post-process handling, and configuration.
> **Status**: Implemented, manually tested and approved.
> **Module**: `Source/awsTutorial/` — shared C++ source, identical on both workstations.

---

## 1. Goal

Viewmode lets a user inspect one selected component of a `AShowroomBooth` (cabinet, closet, countertop, sink, faucet, mirror) in isolation. The requirements it must satisfy simultaneously:

1. **Material parity** — the selected mesh/material should look as close as possible to its real appearance in the level, *especially* shiny and reflective materials.
2. **Clean 360° viewing** — the mesh must be viewable from any angle.
3. **Even illumination** — no bright side / dark side from the world sun, no harsh moving shadows while rotating; the object stays clearly and evenly lit through the full rotation.
4. **Stable viewing distance** — rotating must not change the camera distance; nearby walls must not push the camera around.
5. **Predictable zoom** — intentional zoom in/out works normally, within sane limits.
6. **Clean, professional behaviour** — no camera clipping, no walls blocking the view, no lighting jumps, no distracting environmental changes.
7. **Level integrity** — entering and leaving Viewmode must not damage or alter level state.

Requirements 1 and 3 pull in opposite directions: the level's directional light is exactly what would create a bright/dark split. The architecture resolves this by separating **what the material reflects** from **what illuminates it** — two independent rendering channels.

---

## 2. Final Architecture — "Real room, neutral subject"

The preview is a client-local duplicate actor (`AFurniturePreviewActor`, `bReplicates = false`) spawned at the **real booth's world position and rotation**. The real booth is hidden while the preview is open.

| Aspect | Behaviour |
|---|---|
| **Room geometry** | **Untouched.** The bathroom stays fully intact — it is what Lumen reflects. |
| **World lights** | **Untouched.** DirectionalLight, RectLights and SkyLights are never hidden, modified or recaptured. |
| **PostProcessVolumes** | **Left enabled.** Only one specific blendable is temporarily suspended (see §7). |
| **Subject lighting** | Preview meshes are on **lighting channel 1**; world lights are channel 0, so they do not light the subject directly. A shadowless two-light rig on channel 1 lights only the subject. |
| **Reflections / GI** | Lumen GI and reflections are **not** channel-filtered, so the intact room still provides realistic ambient and reflections on the subject. |
| **Rotation model** | The **mesh rotates**; the camera stays fixed. Viewing distance can therefore only change via explicit zoom. |
| **Isolation** | Stencil-250 CustomDepth + a post-process material dims the surroundings. This is a *post* effect — it does not alter lighting, and reflections of the room stay undimmed. |

### Rendering context this was designed against

The level uses **Lumen GI + Lumen Reflections in software mode** (`r.DynamicGlobalIlluminationMethod=1`, `r.ReflectionMethod=1`, `r.Lumen.HardwareRayTracing=0`) and contains **no reflection capture actors**. Every reflection therefore comes from (a) the Lumen scene built from nearby geometry and (b) the SkyLight. Both are preserved by this design; both were destroyed by the previous one.

---

## 3. Files & Classes Changed

| File | Class / Scope | Nature of change |
|---|---|---|
| `Source/awsTutorial/FurnitureConfigurator/Preview/FurniturePreviewActor.h` | `AFurniturePreviewActor`, `FPreviewComponentConfig` | Config struct redesigned; obsolete components/members removed; clearance, entry-orientation and post-process-suspension settings added |
| `Source/awsTutorial/FurnitureConfigurator/Preview/FurniturePreviewActor.cpp` | `AFurniturePreviewActor` | Core rewrite of constructor, `EndPlay`, `LoadProductPreview`, `SetFocusComponent`, `ConfigureMesh`, `WIP_ApplyStencilIsolation`; new `ResolveClearPivot`, `RestoreClearanceHiddenComponents`, `SuspendConflictingPostProcessMaterials`, `RestoreSuspendedPostProcessMaterials` |
| `Source/awsTutorial/awsTutorial_PlayerController.cpp` | `AAwsTutorial_PlayerController::OpenFurniturePreview` / `CloseFurniturePreview` | Removed the two loops that disabled/re-enabled every `APostProcessVolume` |

No other classes or systems were modified. All changed functions belong to `AFurniturePreviewActor` except the two PlayerController functions above.

---

## 4. Lighting & Reflection Behaviour

### 4.1 Channel separation

- `ConfigureMesh()` puts every preview mesh on **lighting channel 1 only** (`bChannel0 = false`, `bChannel1 = true`).
- The rig lights (`PreviewKeyLight`, `PreviewFillLight`) are also channel 1 only.
- Result: world lights (channel 0) do not directly light the subject → no bright/dark split, no shadows sweeping across it during rotation. World lighting of the *room* is completely unaffected.

### 4.2 The subject fill rig

Two `URectLightComponent`s, both children of the **SpringArm root at the pivot** (not the camera socket):

- **Key** — camera side, raised and offset left of the view axis, aimed at the pivot. Large source area produces broad, gentle speculars rather than hard glints.
- **Fill** — opposite side, slightly below, so the far side never reads as a dark half.

Both are **shadowless by design** (evenness requirement). Because they hang off the SpringArm root, their distance to the subject is constant regardless of zoom, and because the camera does not move during mesh rotation, the on-screen illumination is identical at every rotation angle. Shadowless lights ignore occluders, so a rig light positioned inside nearby wall geometry still works correctly.

### 4.3 What preserves material parity

| Contribution | Source | Matches level? |
|---|---|---|
| Environment reflection | Lumen reflecting the **intact real room** | Yes — same mechanism, same scene |
| Ambient / bounce | Lumen GI from the real room | Yes |
| Direct speculars | Camera-relative soft rig | **Deliberately different** — even instead of directional |
| Exposure / bloom / grading | The level's own PostProcessVolume | Yes |

The single intentional departure is direct speculars — and that departure *is* requirement 3. As the mesh rotates, the room's reflection slides across glossy surfaces realistically, while no shadowed light ever crosses it.

Preview meshes have `SetCastShadow(false)`: they are lit only by the shadowless rig, so their own shadow casting would produce artifacts against channel-0 world lighting.

---

## 5. 360° Rotation Behaviour

- `RotatePreview(DeltaYaw, DeltaPitch)` rotates `MeshRoot` around the mesh's bounds centre (`WIP_MeshPivotWorld`). Yaw is applied around world Z; pitch around the camera's flattened right vector, clamped to **±80°** to prevent gimbal roll.
- The **camera never moves** during rotation. This is what guarantees requirement 4: viewing distance is constant by construction, and no wall can newly obstruct the view mid-rotation.
- `ResetRotation()` restores the initial mesh orientation, the entry camera rotation and the initial view distance.

---

## 6. Clearance / Relocation for Wall-Backed Meshes

Bathroom vanity units are normally placed against walls, so free space around them is limited. A rotating mesh sweeps a sphere of its bounds radius around the pivot — at the raw booth position that sphere would intersect the wall behind the booth and, once pitch is involved, the floor below. The mesh would visibly clip through them.

The previous implementation avoided this accidentally by hiding all geometry within 5 m — which is precisely what destroyed reflection parity. The new solution keeps the room and moves the pivot instead.

### `ResolveClearPivot()` — entry-time only

1. **Floor clearance** — the pivot is lifted so a pitched (tumbling) mesh cannot sweep into the floor.
2. **Horizontal search** — an overlap test (`ECC_WorldStatic`, sphere of `bounds radius + Clearance Margin`) walks outward from the pivot in 20 cm steps: forward along the booth's facing direction first (into the open room), then the two forward diagonals, then sideways — up to `Max Pivot Search Distance`.
3. **Fallback** — if no free position exists (very small bathrooms) and `Hide Blocking Geometry As Fallback` is enabled, the best candidate is used and **only the components actually intersecting the swept sphere** are hidden, cached and restored on exit or on re-focus. This is a targeted hide of one or two wall pieces, not a 5 m purge.

Visually the product is "picked off the shelf" to be inspected. This runs **once per `SetFocusComponent`** — nothing moves during rotation or zoom.

---

## 7. Zoom Behaviour

- `ZoomPreview(DeltaZoom)` adjusts `SpringArm->TargetArmLength`, clamped between the focused component's `Minimum Zoom Distance` and the effective maximum.
- **Initial distance** is adaptive: `mesh bounds radius × 2.5`, clamped to the component's configured limits.
- **`SpringArm->bDoCollisionTest` is deliberately `false`.** A live collision test would shrink the arm whenever geometry brushed it, changing the viewing distance during rotation — a direct violation of requirement 4.
- Instead, camera safety is resolved **once at entry**: a single line trace from the pivot backwards along the entry view direction finds the wall behind the camera and clamps `ActiveMaxZoom` to that distance minus a 30 cm margin. In a small bathroom the user simply cannot zoom out as far — the camera can never end up inside a wall.

---

## 8. `M_PostProcessOutline` Conflict

### The conflict

`M_PostProcessOutline` is a post-process material on the level's `PostProcessVolume`, used by the **Room Planner** for selection outlines. It is keyed on **custom-depth stencil values**.

The Viewmode isolation effect also requires custom depth: the focused mesh group renders with **stencil value 250** so the isolation material can dim everything else. Two stencil-keyed post-process materials share one stencil buffer, so the outline material reacted to stencil 250 and tinted the entire previewed mesh **blue**.

Disabling the whole volume (the previous behaviour) fixed the tint but destroyed exposure/bloom/grading parity — the very thing that makes shiny materials read correctly.

### The resolution — targeted blendable suspension

While the preview is open, **only the conflicting material is pulled out of the volumes' blendable arrays**; the volumes themselves stay enabled so every other setting (exposure, bloom, grading, Lumen quality) keeps applying.

- `SuspendConflictingPostProcessMaterials()` runs in `LoadProductPreview`. It iterates every `APostProcessVolume`, and for each blendable entry that matches, records `{Volume, BlendableObject, Weight}` and removes it.
- `RestoreSuspendedPostProcessMaterials()` runs in `EndPlay` — the guaranteed-restore path — re-adding each entry to the same volume with its original weight.

**Matching rules** (any one is sufficient):
- the object is one of the materials listed in `Post Process Materials To Suspend`;
- the object is an instance/MID whose base material matches a listed material;
- **name fallback** — the object's name contains `PostProcessOutline`. This safety net catches the material even if the asset was moved or renamed, which matters because `Content/` differs between the two workstations.

**Room Planner impact: none.** The outline material is present and functioning at all times except while Viewmode is actually on screen — and the Room Planner UI is not usable during Viewmode.

> **Note**: this is a deliberate, documented exception to the otherwise strict "never modify level state" rule. It is the minimal possible exception — a single blendable entry, exactly restored — and it exists because the two stencil-keyed materials cannot share the stencil buffer simultaneously without editing the material assets themselves (per-machine Content).

---

## 9. Entry / Front-View Orientation

Every component must enter Viewmode from a consistent, natural **front** view regardless of how the booth is placed or rotated in the bathroom.

- **Base direction**: the booth's facing axis (`GetActorRotation().Yaw + 180°`). The preview actor is spawned with the booth's yaw, and booths face into the open side of the bathroom — so the camera enters from where a person would naturally stand. Being booth-relative, this holds for any placement or rotation in any level.
- **`Entry View Yaw Offset (deg)`** — per component. Corrects meshes whose authored front does not align with the booth's forward axis (the "enters showing its side" case). Set once per component type; because the offset is booth-relative it then holds everywhere.
- **`Entry View Pitch (deg)`** — actor-wide, default **−15°**: the camera enters slightly above looking down, the classic three-quarter product-shot angle.

Because the fill rig is camera-relative, the subject is lit identically regardless of the chosen entry direction — changing entry orientation has no lighting side effects. Rotation after entry is unaffected.

---

## 10. `BP_FurniturePreviewActor` Settings Reference

### 10.1 Preview Config (actor-wide)

| Setting | Default | Purpose |
|---|---|---|
| **Stencil Isolation Material Parent** | `M_StencilIsolation4` | Post-process material that dims everything not marked stencil-250. Drives the isolation look. |
| **Preview Vignette Intensity** | `0.4` | Camera vignette during preview. **0 = use the level's own vignette unchanged.** |
| **Entry View Pitch (deg)** | `-15` | Downward tilt of the entry view (negative = camera above). `0` = perfectly level. |
| **Post Process Materials To Suspend** | `[M_PostProcessOutline]` | Materials pulled from the level volumes for the preview's duration and restored on exit (see §8). |

### 10.2 Preview Config | Clearance (actor-wide)

| Setting | Default | Purpose |
|---|---|---|
| **Clearance Margin (cm)** | `15` | Extra clearance added around the mesh's swept radius when searching for a free pivot. |
| **Max Pivot Search Distance (cm)** | `300` | How far the pivot may be moved from the booth to find free space. |
| **Hide Blocking Geometry As Fallback** | `true` | If no free pivot exists, temporarily hide only the components intersecting the sweep. Off = the mesh may visibly clip in very cramped layouts. |

### 10.3 Preview Config | Components — per component type

Available for **Cabinet, Closet, Countertop, Sink, Faucet, Mirror**.

**Camera & Zoom**

| Setting | Default | Purpose |
|---|---|---|
| **Minimum Zoom Distance (cm)** | per type (15–60) | Closest the camera may zoom before clamping. |
| **Maximum Zoom Distance (cm)** | per type (150–400) | Farthest zoom, further clamped at entry by the wall behind the camera. |
| **Camera Exposure Offset (EV)** | `0` | EV100 offset on the preview camera; brightens/darkens this component's preview without touching lighting. |
| **Entry View Yaw Offset (deg)** | `0` | Per-component correction so the component enters showing its front (see §9). |

**Subject Fill Lighting** (the channel-1 rig)

| Setting | Default | Purpose |
|---|---|---|
| **Key Light Intensity** | `800` | Main rig light intensity. `0` = subject lit by room GI only. |
| **Light Color Tint** | White | Tint for both rig lights. Neutral white preserves PBR material colour. |
| **Fill Intensity Ratio** | `0.4` | Wrap-fill intensity as a fraction of the key. `0` = key only. |
| **Source Width (cm)** | `100` | Key source width. Larger = softer, broader speculars. |
| **Source Height (cm)** | `120` | Key source height. |
| **Light Distance From Pivot (cm)** | `200` | Rig distance from the subject. Constant regardless of zoom. |
| **Attenuation Radius (cm)** | `800` | Rig falloff radius. |

Every listed setting is read by the current implementation. There are no inert options.

---

## 11. Removed / Obsolete

### Removed components

| Removed | Reason |
|---|---|
| `PreviewRimLight` (RectLight) | Part of the old studio rig; superseded by the two-light channel-1 rig. |
| `PreviewSkyLight` (SkyLight) | Ambient now comes from real room Lumen GI. Also removed a double-SkyLight ambient contribution. |
| `PreviewDirectionalLight` (camera-welded) | Its highlight followed the viewer — the most obvious "this isn't the level" cue on shiny surfaces. |

### Removed behaviour

| Removed | Reason |
|---|---|
| `WIP_UpdateWallOcclusion()` — 5 m sphere hiding all world geometry | Removed the Lumen scene the subject needed to reflect. Replaced by pivot relocation (§6). |
| Hiding world `ARectLight` / `ADirectionalLight` actors | Deleted the speculars that define shiny materials. Channel separation achieves evenness without it. |
| `DeferredHideWorldLights()` / `LockInSkyLightCubemap()` — the two/three-tick SkyLight recapture | Contradictory mechanism (`bRealTimeCapture` captures the sky, not local geometry). **It also never restored world SkyLight state — a single Viewmode session permanently altered the level's ambient for the rest of the play session.** Removing it fixes that bug. |
| Disabling all `APostProcessVolume`s | Destroyed exposure/bloom/grading parity. Replaced by targeted blendable suspension (§8). |
| Forced `ReflectionMethod` / `LumenReflectionQuality` overrides on the preview camera | Reflections must render exactly as the level renders them. Also, `bOverride_ReflectionMethod` was set without ever assigning a value. |
| Hardcoded `VignetteIntensity = 0.8` | Now the configurable `Preview Vignette Intensity`. |

### Removed settings

| Setting | Reason |
|---|---|
| `Inherit World Sun Settings`, `Sun Light Intensity Override`, `Sun Light Color Override`, `Sun Light Relative Rotation`, `Sun Light Casts Shadows` | The preview no longer has its own directional light. **These were already non-functional before this work** — nothing read them, so designers were tuning values that could never take effect. |
| `SkyLight Fill Intensity`, `SkyLight Ambient Color` | The preview SkyLight no longer exists. |
| `Enable Mesh Dynamic Shadows` | The subject is lit only by shadowless lights; the toggle could not do anything. |
| `Rect Key Casts Shadows` | Rig shadows would break the evenness requirement; hard-off by design. |

### Deliberate rename

`KeyLightIntensity` → **`PreviewKeyIntensity`**. The Blueprint had the old property saved as **0** (correct for the old rig, which defaulted it off). Keeping the name would have silently disabled the new rig on load. The rename discards the stale override so the new default (800) applies. All other previously-saved BP values carry over unchanged.

---

## 12. Testing Performed

Verified by build tooling:

- Editor and Game targets both build clean from scratch (`-Rebuild -DisableAdaptiveUnity`), zero errors, zero warnings from the changed files.
- UnrealHeaderTool passes with `-WarningsAsErrors`.
- No dangling references to any removed symbol anywhere in the module.

Verified manually in PIE (approved):

- Entry, framing and isolation for the previewed component.
- Full 360° rotation with even illumination and no lighting jumps.
- Stable viewing distance throughout rotation.
- Zoom in/out within limits, no camera clipping.
- Reflective/shiny materials visually consistent with the level.
- No wall/floor clipping for wall-backed bathroom cabinets.
- Entry/exit leaves level state intact.
- `M_PostProcessOutline` no longer tints the preview mesh, and Room Planner outlines still work after exiting Viewmode.
- Consistent front-view entry orientation.

---

## 13. Limitations & Configuration Notes

1. **Specular highlights do not match the level's light positions.** This is unavoidable and intentional: even illumination *means* replacing directional speculars. Environment reflections — the dominant cue for chrome, gloss and mirrors — do match.
2. **No self-shadowing on the subject.** Crevices read slightly flatter than in the level; Lumen GI contact darkening recovers some of it.
3. **No floor contact shadow.** The subject is lit by shadowless lights and is typically lifted by the clearance relocation, so it reads as "picked up for inspection".
4. **Clearance tests `ECC_WorldStatic` only.** Room Planner procedural walls are `WorldDynamic` and are not considered — booths are not placed inside planner-generated rooms, so this does not arise in practice.
5. **Very small bathrooms** may hit the clearance fallback (a wall piece briefly hidden) or a shorter maximum zoom-out. Both are tunable via the Clearance settings.
6. **A third stencil-keyed post-process material** added to the level volume in future would need adding to `Post Process Materials To Suspend`.
7. **`Content/` is per-machine.** `BP_FurniturePreviewActor` must be checked on the second workstation: the removed settings will be gone from the Details panel and the new ones will show defaults. Re-save the Blueprint there. `Entry View Yaw Offset` may need setting per component on that machine too.
8. **Lighting channels and Lumen**: channel filtering applies to *direct* lighting. Lumen GI/reflections intentionally still reach the subject — that is the mechanism providing material parity, not a leak.

---

*Document Version: 1.0.0 — Viewmode / Furniture Preview System, awsTutorial*
