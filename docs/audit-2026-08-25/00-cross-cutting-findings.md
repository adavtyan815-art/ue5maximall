# Cross-Cutting Findings — 2026-08-25

Issues that span multiple subsystems. Severity legend used across all findings docs:
**[BUG]** confirmed defect · **[LIKELY]** probable issue, needs a targeted test ·
**[FRAGILE]** works today but breaks easily · **[CLEANUP]** safe to remove/simplify ·
**[PERF]** performance · **[INFO]** worth knowing, no action required.

---

## X-1 [CLEANUP] Mojibake (corrupted UTF-8) in comments and one user-facing enum

Roughly ten files contain double-encoded UTF-8: the box-drawing separator comments read as
`РІвЂќР‚РІвЂќР‚…` (e.g. `ShowroomBooth.cpp`, `awsTutorial_PlayerController.cpp/h`,
`FurnitureTypes.h`). Harmless in comments, but in
[ColorCatalogTypes.h:15-27](../../Source/awsTutorial/ColorCatalog/ColorCatalogTypes.h) the
`EColorShadeCategory` **UMETA DisplayNames** are corrupted Russian (`Р’СЃРµ` instead of `Все`).
Anywhere those enum display names surface (editor dropdowns, `UEnum::GetDisplayValueAsText`)
shows garbage. The in-game category buttons are separate UMG widgets, which is why users have
not seen it. Fix by re-saving the affected files as UTF-8 (with BOM or with
`bForceUnicode`), restoring the intended characters once.

## X-2 [INFO] Warning-level logging used for routine flow

Nearly all informational logs use `UE_LOG(LogTemp, Warning, …)` — booth rebuilds, HTTP
dispatches, every Pixel Streaming descriptor (including full payload contents at
[awsTutorial_PlayerController.cpp:1735](../../Source/awsTutorial/awsTutorial_PlayerController.cpp)),
per-resolution catalog logs. In a shipping streaming server this produces significant log volume
and makes real warnings invisible. Recommend one custom log category (`LogMaxiMall`) with
`Log`/`Verbose` for routine flow.

## X-3 [INFO] Diagnostic messages are sent to the browser data channel

`SendDiag_PC` ([awsTutorial_PlayerController.cpp:66-75](../../Source/awsTutorial/awsTutorial_PlayerController.cpp))
pushes internal notes (including "FIX1/FIX2" changelog text) as `DIAG: …` messages to every
connected browser. Useful during bring-up; consider gating behind a launch flag before wider
release.

## X-4 [CLEANUP] Duplicated `ApplyComponentMeshAndMaterials` family

Three near-identical mesh+material application helpers exist **twice** — once in
`AShowroomBooth` ([ShowroomBooth.cpp:1617-1883](../../Source/awsTutorial/FurnitureConfigurator/ShowroomBooth.cpp))
and once in `AFurniturePreviewActor`
([FurniturePreviewActor.cpp:1380-1529](../../Source/awsTutorial/FurnitureConfigurator/Preview/FurniturePreviewActor.cpp)).
The preview variants differ only in collision/visibility handling and in skipping the
`SizeIndices` color filtering that the booth variant applies (a real, subtle behavioral
difference — see Findings 02, F-9). A shared free-function helper with a small options struct
would remove ~250 duplicated lines and eliminate the divergence risk.

## X-5 [INFO] All Server RPC `_Validate` functions return `true`

Every `WithValidation` RPC in the project validates nothing (indices, product IDs, wall lengths
are only sanity-checked in the implementation, if at all). `Server_ApplyProductChange_Validate`
is the sole exception (checks the product ID). For the current one-user-per-instance Pixel
Streaming deployment this is acceptable; if the multiplayer path is ever exposed to untrusted
clients, add real validation (a failed `_Validate` disconnects the sender — today a malicious
client can, e.g., call `Server_ClearLayout` or set absurd wall lengths freely).

## X-6 [INFO] The module name is `awsTutorial`

Project, docs, and log prefixes say MaxiMall; the module, folder, and `AWSTUTORIAL_API` macro
say `awsTutorial`. Renaming a UE module is invasive (redirects for Blueprint-referenced
classes) — not recommended now, but the mismatch already caused one real bug candidate: the
`GetOptions` meta path using a wrong module name (Findings 02, F-5).

## X-7 [INFO] Localization

All user-facing strings are hardcoded, mostly Russian, via `FText::FromString`
(`ConfiguratorMainWidget.cpp:681`, all of `RoomPlannerWidget.cpp`, `ColorCatalogWidget.cpp`).
If the product ever needs a second language this becomes a large mechanical change; using
`NSLOCTEXT` from the start would have been free. Note only.

## X-8 [INFO] No automated tests

There are no unit or functional tests in the module. The geometry code (room face tracing, wall
mesh generation, opening interval logic) is exactly the kind of pure logic that would benefit
from a handful of automation specs — it is currently protected only by manual testing.
