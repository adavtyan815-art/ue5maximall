# Viewmode / Furniture Preview — Findings (2026-08-25)

Severity legend: see [00-cross-cutting-findings.md](00-cross-cutting-findings.md).

---

## F-1 [FRAGILE — highest-priority finding of this audit] The studio relocation silently invalidates the "real room" design the code still implements

The 2026-08-24 change teleports the booth to `ViewModeRelocationLocation` (-10000, 0, 0)
**before** the preview spawns
([awsTutorial_PlayerController.cpp:751-764](../../Source/awsTutorial/awsTutorial_PlayerController.cpp)).
Everything in `AFurniturePreviewActor` still assumes it is running at the booth's showroom
position — the class header, `LoadProductPreview` comments, and the archived
`VIEWMODE_SYSTEM.md` all describe reflections/GI coming from "the ACTUAL room". Concretely, at
the studio location:

- **`MeasureWorldIlluminanceAt` measures the studio, not the showroom.** With the default
  `bMatchLevelLighting = true` and *no lights at the studio coordinates*, the measurement is
  ~0 lux → the rig switches off → the subject is lit only by whatever Lumen GI exists there
  (in an empty void: nothing). The preview looking correct today therefore **depends on level
  content at (-10000, 0, 0)** (a lit studio room) and/or Blueprint overrides
  (`bMatchLevelLighting = false` + manual candelas) that C++ cannot see or guarantee.
- **Lumen reflections on glossy materials show the studio surroundings**, not the showroom —
  the entire "material parity with the level" rationale (archived doc §2/§4.3) no longer holds
  as documented.
- **`ResolveClearPivot` and the zoom wall-clamp trace run against studio geometry.** In an
  empty void they find everything free (harmless but wasted overlap tests); in a small studio
  box they now do something meaningful again.
- The Room Planner's character relocation uses the **same default coordinates**
  (`PlannerRelocationLocation` = (-10000, 0, 0)) — if a user could ever have the planner open
  and enter Viewmode, the booth would teleport onto the relocated character. Today's UI flow
  prevents it; nothing structural does.

**This is not a bug today** (manually tested and approved per the changelog), but it is the
project's most fragile point: correctness now lives in un-versioned level content + BP config,
while the C++ and docs describe a different system. **Actions** (pick at least one):
1. Document the studio requirement prominently (what must exist at the relocation coords:
   lighting, floor, backdrop; whether `bMatchLevelLighting` should be off).
2. Update the `FurniturePreviewActor.h/.cpp` header comments — they are now misleading.
3. Consider measuring illuminance at the **cached original booth transform**
   (`CachedOriginalBoothTransform` is available on the controller) if level-match lighting at
   the showroom appearance is still the goal.
4. Change one of the two relocation defaults so planner and Viewmode areas can never collide.

## F-2 [CLEANUP] Header/class documentation is stale

`FurniturePreviewActor.h:4-28` ("spawned … at the SourceBooth location … so Lumen GI and
reflections come from the ACTUAL room") and the long comments in `LoadProductPreview` /
`SetFocusComponent` describe the pre-relocation architecture. Same for the calibration comment
"the mesh's original booth position" — it is now the relocated position. Rewrite after
resolving F-1.

## F-3 [INFO] `IsolationFade` is hardcoded

`WIP_ApplyStencilIsolation` sets `IsolationFade = 0.8f` and `TargetStencil = 250` on the MID
every time ([FurniturePreviewActor.cpp:1208-1209](../../Source/awsTutorial/FurnitureConfigurator/Preview/FurniturePreviewActor.cpp)),
overriding whatever the material instance was authored with. If designers ever tune the dim
strength in the material, it will be silently reset. Make it an `EditAnywhere` float next to
`PreviewVignetteIntensity`, or stop setting it when the parent already defines it.

## F-4 [INFO] Repeated resolution cost on preview load

`LoadProductPreview` calls `SourceBooth->GetResolvedComponentOptions` and the
`GetActive*Offset/Type` helpers, multiplying the cost documented in Findings 02/F-1 (each
offset helper re-resolves). One product change while a preview is open triggers the booth
rebuild *and* a full preview reload, each with its own resolution cascade. Same fix as 02/F-1.

## F-5 [INFO] Cabinet color indexing skips the `SizeIndices` filter

See Findings 02/F-9 — the preview's cabinet material helper indexes unfiltered colors; masked
by the subsequent live-material copy from the booth. Consolidate helpers to remove the trap.

## F-6 [INFO] Clearance fallback can hide the studio itself

If the studio at the relocation point is a small enclosed box, `ResolveClearPivot` may fail to
find a free swept sphere and (with `bAllowGeometryHideFallback = true`, the default) hide
studio wall pieces — which then also disappear from Lumen reflections for the session duration
of that focus. Restored correctly on refocus/close; just be aware when sizing the studio.

## F-7 [OK — leave alone] Deliberate behaviors that look like bugs

- `SpringArm->bDoCollisionTest = false` (stable-distance requirement).
- Honoring a ~0 lux measurement (the low-lux manual fallback was itself the bug; archived doc
  §12 documents the log-driven diagnosis).
- Preview meshes with `SetCastShadow(false)` and the shadowless rig.
- `SuspendConflictingPostProcessMaterials` name-contains("PostProcessOutline") fallback — it
  exists because `Content/` differs per machine; keep it until content is unified.
- The `EndPlay`-based restore path — it is the guaranteed-execution cleanup; do not move the
  restores into `CloseFurniturePreview` only.
