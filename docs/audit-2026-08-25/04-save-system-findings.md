# Save/Load & HTTP Backend — Findings (2026-08-25)

Severity legend: see [00-cross-cutting-findings.md](00-cross-cutting-findings.md).

---

## F-1 [LIKELY] Thumbnail race when saving twice quickly

`OnSaveClicked` overwrites `PendingSaveId`/`PendingSaveName` and adds **another**
`OnScreenshotCaptured` binding each time
([SaveSystemWidget.cpp:85-99](../../Source/awsTutorial/FurnitureConfigurator/UI/SaveSystemWidget.cpp)).
If Save is clicked twice before the first capture arrives: (a) the first save's thumbnail is
attached to the **second** save's ID; (b) two live bindings mean one broadcast can invoke the
handler twice (the `RemoveAll` inside the handler doesn't retroactively shrink the broadcast
in flight), producing duplicate crop/compress/POST work. Test: double-click Save rapidly and
inspect which record gets the thumbnail. **Fix**: disable the Save button until
`OnPostSaveComplete`, or queue `{SaveId, Name}` per capture instead of a single pending pair.

## F-2 [FRAGILE] Booth matching by actor name

Save and load key booths by `GetName()` (`SaveSystemWidget.cpp:187, 301-314`). Actor names are
not stable identity: renaming a booth in the level, recreating it (name gets an incremented
suffix), or PIE name mangling silently orphans previously saved state for that booth — the
loop just doesn't find a match and skips it, with no user-visible warning. **Fix direction**: a
designer-set stable ID on the booth (`BoothDisplayName` exists but is cosmetic; add an
`FName BoothSaveKey` UPROPERTY) and fall back to name matching for old saves.

## F-3 [FRAGILE] "Most recent save" = last array element

`OnLastSaveLoadClicked` and the Last Save panel use `LoadedSaves.Last()`
(`SaveSystemWidget.cpp:262, 539-543`) — ordering is whatever the backend returns. The `date`
field ("dd.MM.yyyy", no time) cannot break ties even client-side. Works because the current
backend appends; document that contract in the backend repo or sort by a server-side timestamp.

## F-4 [INFO] Identity and access model

- The Cognito username is fetched by **reflection** on a GameInstance BP property named
  `UserName` (`SaveSystemWidget.cpp:645-662`) — an invisible C++↔BP contract; a rename in the
  BP breaks saves silently (falls back to `guest_tester`).
- The `guest_tester` fallback means all not-logged-in users share one save namespace.
- The REST API carries no auth token; anyone who can reach the backend can read/delete any
  user's saves by URL. Acceptable for the closed test; must be revisited before public use
  (backend concern, but the client is where a token would be attached).

## F-5 [INFO] Hardcoded endpoints and ports

Fallback base URL `https://18-185-5-251.nip.io` pins an IP; the PixelStreamingURL-derived path
assumes the backend is plain HTTP on port 3000 on the same host
(`SaveSystemWidget.cpp:685-733`). Both are launch-configuration facts encoded in C++ — fine for
now, but they belong in a config (`DefaultGame.ini`) so ops changes don't require a client
build.

## F-6 [INFO] 5-second timeout vs. thumbnail upload

The thumbnail POST body is a few hundred KB of base64. On a slow uplink 5 s
(`Request->SetTimeout(5.0f)`) can abort the update POST; the save then permanently shows an
empty thumbnail (the state itself was already saved by the first POST — good design). Consider
10–15 s for the thumbnail POST only.

## F-7 [CLEANUP] Dead/obsolete members

- `OnScreenshotTimeout`, `ScreenshotTimeoutTimerHandle`, `bWaitingForScreenshot` — the comment
  itself says "Obsolete: timeout is no longer needed" (`SaveSystemWidget.cpp:106-109`). Delete.
- `MockNamesList` / `MockDatesList` — legacy "mock" naming for what is now real data; they
  duplicate fields already inside `LoadedSaves`. Fold into reading `LoadedSaves` directly.
- `LastSaveThumbnail` brush is set to `nullptr` when absent — fine, but the two
  "Retain user's custom layout definitions" comments refer to specific pixel sizes in the UMG
  and will rot; consider removing.

## F-8 [INFO] Screenshot capture assumptions

`OnScreenshotCapturedHandler` indexes `Colors[SourceY * Width + SourceX]` trusting
`Colors.Num() == Width*Height` (engine-guaranteed today) and runs the crop/PNG encode on the
game thread (~256×256 — negligible). `RequestScreenshot(false)` excludes UI — intended (the
thumbnail shows the scene, not the dialog). No action.

## F-9 [INFO] Delete has no confirmation

`HandleDeleteSaveItem` fires the HTTP DELETE on a single click. Product decision — flag only.

## F-10 [INFO] Loaded content is trusted

Thumbnails (`ImportBufferAsTexture2D` on arbitrary server bytes) and override-material paths
(`StaticLoadObject` of any `/Game/...` path from the save JSON) are applied without
validation. Fine while the backend is trusted infrastructure; would need allow-listing if saves
ever become user-shareable content.
