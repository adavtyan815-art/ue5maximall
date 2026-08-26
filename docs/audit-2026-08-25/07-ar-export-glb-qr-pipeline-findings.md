# AR Export / GLB / Material / QR-Phone Pipeline — Full Audit (2026-08-25)

Investigation only. No code, assets, or Git state were modified. One new file (this report) was added.

Evidence sources: current source under `Source/awsTutorial/ARExport/`, `ShowroomBooth.cpp`, UI widgets,
`Saved/Logs/awsTutorial.log` (84 `[ARExport-Diag]` lines from real exports today), parsed JSON chunks of
generated GLBs in `Saved/AR_Exports/`, `run_local_ar_server.bat`, `server.js`, `index.html`, live system
state (netstat, firewall rules, NIC list), and Git history of `Source/awsTutorial/ARExport`.

---

## Architecture map (as actually implemented)

```
"View in AR" button
  ConfiguratorMainWidget::OnARExportClicked (ConfiguratorMainWidget.cpp:912)
  ViewmodeOverlayWidget::OnARExportClicked  (ViewmodeOverlayWidget.cpp:40)
        → ARExportModalWidget::StartExport(Booth)          (modal UI, progress/QR display)
        → UARExportSubsystem::ExportBoothToAR(Booth, cb)   (ARExportSubsystem.cpp:761)
              1. TargetBooth->GetComponents<UStaticMeshComponent>()   [booth actor only]
              2. per component: LOD0 sections → per section:
                   slot material = Comp->GetMaterial(Section.MaterialIndex)
                     (fallback: StaticMesh.StaticMaterials → Mesh->GetMaterial)
                   ResolveMaterialPBRState(SlotMat)         [name-based parameter parsing]
                   optional BaseColor texture bake (BakeTextureWithTintPNG + UV coverage mask)
                   optional normal texture extract (green-channel flip)
                   vertex copy: pos(Y,Z,X)*0.01, normals, UV0 (+MIC UV transform)
                   → FGLBPrimitive (one per section)
              3. FSimpleGLBWriter::SerializeToGLB on thread pool → Saved/AR_Exports/export_<Product>_<ts>.glb
              4. URL = "http://<GetLocalHostAddr>:8080/index.html?model=<file>"
              5. FQRCodeTextureHelper (Nayuki qrcodegen port) → QR texture → modal

SERVING (fully external, manual):
  run_local_ar_server.bat  → node server.js (0.0.0.0:8080, correct MIME, CORS, /api/latest-model)
                             fallbacks: python -m http.server / PowerShell HttpListener
  index.html               → <model-viewer 3.4.0 from Google CDN> loads ?model=<file>,
                             ar-modes="webxr scene-viewer quick-look", env "neutral", exposure 1.0
```

Material decision tree per slot (ResolveMaterialPBRState, ARExportSubsystem.cpp:479):

1. **Vector params** (name-matched, case-insensitive): UV tiling/offset/pivots; `Reflection`/`Reflection (4)`/`refl_color` captured;
   BaseColor from exact names `basecolor|base_color|albedo|maincolor|diffuse|diffuse (1)|color|tint`, then contains-match fallback.
2. **Scalar params**: `metallic|metalness|metal` (explicit), `roughness|rough` (explicit), glossiness
   (reflection-gloss preferred, refraction-gloss excluded), UV scalars.
3. **Textures**: recursive graph walk (editor) / GetUsedTextures+sRGB/TC_Normalmap (cooked).
   First non-"normal|nrm|norm|bump"-named texture → BaseColorTexture. First normal-named → NormalTexture.
4. **Roughness priority**: explicit > (has BaseColorTexture → 0.60) > (1 − gloss, clamp ≥0.04) > default 0.5.
5. **Metal branch** (only when NO explicit metallic AND a Reflection color exists):
   - colored reflection (max>0.35 ∧ max−min>0.08) ∧ dark/absent diffuse (<0.08 lum) → Metallic 0.85, BaseColor=Reflection, texture discarded
   - mirror-like (reflMax≥0.5 ∧ gloss≥0.65 ∧ dark/absent diffuse) → Metallic 0.95
6. **Alpha**: explicit `opacity|alpha|*transparen*|refract(≠ior)` scalar; "genuine translucent" =
   BlendMode ∈ {Translucent, Additive, AlphaComposite} ∧ (explicit alpha ∨ no solid diffuse) → BLEND
   (A = parsed or 0.25, Roughness 0.05 if not explicit); Masked → MASK + cutoff; else OPAQUE.
7. **Bake** (only if BaseColorTexture): tiled-UV test (any UV outside [−0.05, 1.05]); non-tiled → per-section UV
   coverage mask; texture classified by sampled avg chromatic diff >12 → full-color (tint multiply, never masked)
   vs grayscale (Luminance² × tint, masked). White tint + no mask → raw extract fast path.
8. **Fallback when nothing matches** (parameter-less `UMaterial`): silent White / Metallic 0 / Roughness 0.5 / OPAQUE.

---

## A. Runtime scene collection

**A1. Collection is booth-only, per visible component, per section — works as designed.**
Evidence: ARExportSubsystem.cpp:773–806; diag logs enumerate MainCabinet, DoorMeshSlot0 (2 slots),
CountertopMesh, SinkMesh, FaucetMesh, MirrorMesh (3 slots), ClosetMesh (3 slots), ClosetDoorMeshSlot0 (2 slots);
hidden `DoorMeshSlot1`/`ClosetDoorMeshSlot1` correctly absent (filtered by `Comp->IsVisible()`).
Status: **PROVEN**. Impact: correct for "product in AR"; room/walls are intentionally not exported.
Note: the export always targets the **booth**, not `AFurniturePreviewActor`; the preview mirrors booth state, so
this is consistent, but any future preview-only state would not export.

**A2. Only LOD0, UV0, no vertex colors.** Evidence: `LODResources[0]`, `GetVertexUV(...,0)`.
Status: PROVEN. Impact: fine for AR; vertex-color-driven materials would lose data (none observed in logs).

**A3. Geometry transform: relative-to-booth, cm→m (×0.01), axis map glTF(X,Y,Z) = UE(Y,Z,X); winding NOT
reversed; every material forced `doubleSided=true`** (ARExportSubsystem.cpp:855–857, 954–963).
Status: PROVEN (code + GLB shows `doubleSided:true` on all 14 materials).
Impact: correct scale (1:1 AR). But a pure axis permutation does not convert LH→RH; the model is very likely
**mirrored left↔right relative to UE**, hidden so far by near-symmetric furniture, and back-face culling issues are
masked by doubleSided. **SUSPECTED** (needs Test T3). Non-uniform component scale would also skew normals
(TransformVector without inverse-transpose) — minor, SUSPECTED, no observed instance.

**A4. Export blocks the game thread during texture baking.** Evidence: log timestamps 18:41:30.127→18:41:31.929
(~1.8 s) all before the async GLB serialization; only serialization is on the thread pool.
Status: PROVEN. Impact: UI hitch of seconds per export (scales with texture size). Cosmetic/perf only.

## B. Per-slot material processing

**B1. Per-slot independence of resolution: works.** Each section resolves `Comp->GetMaterial(MaterialIndex)`
independently; the same mesh exports different materials per slot.
Evidence: diag log — `DoorMeshSlot0 Slot=0 Mat=antr` (dark RAL) vs `Slot=1 Mat=Wood_` (textured wood), each with
its own baked texture and PBR values; GLB contains matching independent materials.
Status: **PROVEN**.

**B2. Runtime MIDs are respected and re-evaluated.** Evidence: 18:38 export shows `SinkMesh Slot=0
Mat=M_Change_Color RGB=(1,1,1)`; after a color change, 18:41 export shows `Mat=MaterialInstanceDynamic_0
(Parent=M_Change_Color) RGB=(0.497,0.014,0.006)` and the GLB carries `baseColorFactor [0.497,0.014,0.006,1]`.
`FPBRParsedState` is a fresh stack value per section — no carry-over between slots.
Status: **PROVEN**.

**B3. Texture cache key is wrong in BOTH directions.**
Key = `<TexturePath>_Baked_<TintString>_Sec<SectionIdx>` (ARExportSubsystem.cpp:891).
- *Too strong*: the same texture+tint used by different section indices is baked and embedded **twice**.
  Evidence: GLB of 22:41 contains two byte-identical 16,055,539-byte wood PNGs (images 0 and 2 — `..._Sec0` and
  `..._Sec1`); log shows both bakes. File is 37 MB where ~21 MB would suffice. **PROVEN.**
- *Too weak*: the key contains **no mesh/component identity**, but for non-tiled sections the baked PNG embeds a
  **mesh-specific UV coverage mask**. Two different meshes using the same texture+tint at the same section index
  would reuse the first mesh's masked bake — a genuine cross-slot state leak. Mechanism **PROVEN by code**;
  no collision occurred in today's scene (**SUSPECTED** as a live bug, guaranteed by construction if the asset
  combination arises).
Impact: 37 MB GLBs (slow phone loads, memory), and a latent wrong-texture bug.
Required verification: none for the duplicate (artifact proves it); T5 for the leak if desired.

**B4. MetallicRoughness textures are never exported.** `FGLBPrimitive.MetallicRoughnessTexture*` exists and the
writer supports it, but the subsystem never fills it (0 references in ARExportSubsystem.cpp).
Status: PROVEN. Impact: every material has constant metallic/roughness — a permanent visual gap vs UE for any
material with roughness maps.

**B5. Arbitrary slot counts supported.** Loop is over `LOD.Sections` with `Section.MaterialIndex`; 1, 2, 3-slot
meshes all appear in logs. No slot-index assumptions anywhere in the export path.
Status: **PROVEN**.

## C. RAL / NCS path

**C1. Mechanism (reconstructed, generic in principle).** Configurator applies color via
`AShowroomBooth::OnRep_CustomColors` (ShowroomBooth.cpp:1415): creates/reuses a MID per slot and sets vector
params `BaseColor` **and** `Color` (lines 1504–1505). The exporter reads those exact names back through the
name-list in the resolver. Solid-color path (no texture): BaseColor → `baseColorFactor`. MIC "RF color" variants
(e.g. `antr`, tint 0.098 + grayscale `RFcolor_-1_000000_Tex`) go through the Luminance²×Tint bake with a
per-section UV coverage mask.
Status: mechanism **PROVEN** (code + logs + GLB values round-trip exactly).

**C2. Is it generic?** It works for any mesh/slot whose material (or MID parent) exposes a parameter named in the
list (`color`, `basecolor`, `tint`, `diffuse`, …). It does NOT depend on cabinet assets, mesh names, or slot
indices — no such checks exist in the export path (verified by search). Two real constraints:
- `OnRep_CustomColors` sets the color on **every** slot of the target component; slots whose materials happen to
  have a `Color`-like parameter but should not be recolored would be affected (configurator-side behavior, not
  exporter).
- The Luminance²×Tint bake evaluates **one specific graph shape** (Desaturation → Power 2 → Multiply, per commit
  16a39713). Any RAL-style material with a different graph will bake a different response curve.
Status: reusable-in-principle **PROVEN**, universality **NOT PROVEN** (only the tested graph family is covered).

**C3. Can a non-RAL grayscale texture fall into this branch?** Yes: any material with a non-white tint and a
texture whose sampled average chromatic difference ≤12 gets Luminance² applied — a deliberately monochrome fabric
or concrete texture with a tint would be darkened quadratically. Status: SUSPECTED (no instance in logs); risky
heuristic by construction.

## D. Full-color textured materials

**D1. Classification `ChromaticDiff > 12` (avg of |R−G|+|G−B|+|R−B| over ≤200 samples).**
Works for the tested wood (full-color preserved, white tint → raw extract fast path). Borderline failures are
possible in both directions: a strongly desaturated wood/ash texture (≤12) would be Luminance²-crushed; sampling
is uniform-stride so small colored regions in a mostly-gray texture can flip the result.
Status: mechanism PROVEN; safety for arbitrary textures **NOT PROVEN** — risky heuristic.

**D2. Tiled-UV detection (`any UV outside [−0.05, 1.05]` → no coverage mask) and REPEAT sampler.**
Correct for the wood case (continuous tiling preserved — matches the controlled result you validated). Sampler is
always REPEAT/REPEAT with linear mipmap filtering; UV transform from MIC params is applied to exported vertex
UVs once (not to the texture) — consistent. Edge case: a mesh that is *mostly* inside [0,1] but has a few stray
UVs gets no mask and keeps the full texture — benign. Reverse edge case: non-tiled mesh + full-color texture —
mask is built but **full-color textures are never masked** (`bUseMask = !bIsFullColorTexture && …`), so
full-color always keeps the whole texture. That is the "no destructive UV masking" fix, PROVEN in code.

**D3. `Roughness = 0.60` fallback for any textured material without an explicit roughness parameter.**
Evidence: code (line 644); log/GLB — all wood and even the mirror's stencil texture slot get 0.60.
Impact: glossy/lacquered wood, polished stone, glossy plastic laminate will all look uniformly semi-matte unless
they carry an explicit roughness/gloss parameter. The earlier 18:37 export used 0.20 — the value is a style
choice, not derived from the material. Status: **PROVEN behavior; known approximation**, project-specific but
acceptable only while all textured dielectrics really are semi-matte.

**D4. Alpha/format handling in bake**: BGRA8 source assumed; alpha forced to 255 on baked pixels; RGB→sRGB round
trip via FLinearColor is correct; PNG re-encode lossless. Textures with source formats ≠ BGRA8 silently fall back
to raw extract (tint ignored!) — SUSPECTED silent-wrong-color path for e.g. HDR/16-bit sources; none observed.

**D5. `FImageUtils::GetTexture2DSourceImage` uses editor-only Source data.** In a **packaged** build it fails and
the fallback only handles `PF_B8G8R8A8` platform data — DXT-compressed textures (the normal case) produce **no
texture at all**. The `#if WITH_EDITORONLY_DATA` in FindTexturesInMaterial shows cooked builds were considered,
but the extraction path was not. Status: SUSPECTED (code-structural, not yet observed because all testing is
in-editor). Impact: the whole texture pipeline silently degrades to flat colors in a shipped build.

## E. Metals

**E1. Explicit `Metallic` parameter path**: generic and safe (name-matched, value-clamped). PROVEN mechanism; no
instance in today's logs used it.

**E2. V-Ray reflection heuristic** (colored reflection over dark diffuse → metal 0.85; strong neutral reflection +
gloss ≥0.65 + dark diffuse → 0.95): this is what turned `white_Inst` (faucet) into gold
`bc=[0.723,0.617,0.292], metal 0.85, rough 0.08` — the controlled result you accepted. It reads **actual compiled
parameter values** (GetVectorParameterValue on the interface, so MIC/MID overrides are respected), not just names.
The "unused master diffuse texture is discarded for metals" rule (line 668) prevents texture-based
misclassification of metals.
Status: PROVEN working on the tested faucet + mirror; the thresholds (0.35 / 0.08 / 0.08 lum / 0.5 / 0.65) are
**project-tuned constants** — a dark glossy dielectric (e.g. black piano-lacquer with bright neutral V-Ray
reflection ≥0.5 and gloss ≥0.65) **would** be classified as mirror-metal 0.95. Risky heuristic; no
counter-example in the current scene.

**E3. Metallic BaseColor = raw Reflection color.** For gold-like materials this matched your test. For materials
where V-Ray reflection color is not the intended tint (e.g. tinted fresnel), it will diverge. SUSPECTED.

## F. Glass / transparency

**F1. Blend-mode reading is real (`GetBlendMode()`), with a ceramic guard**: translucent blend only becomes glass
when there's an explicit opacity-like scalar OR no solid diffuse (`bHasSolidDiffuse` = texture ∨ luminance>0.25).
This is the protection that keeps porcelain sinks opaque. Defaults when translucent without explicit alpha:
A=0.25, Roughness 0.05. `alphaMode`/`alphaCutoff` serialization is spec-correct (writer lines 201–208).
Status: mechanism PROVEN; the constants 0.25/0.05 are hardcoded style choices.

**F2. Current scene's closet "glass" does NOT export as glass.** Diag logs show `Material__-2147483634_2` and
`cеrniy_mеtаl` resolving to White/Opaque/0.5 — they are parameter-less `UMaterial`s and (per the resolver log)
opaque blend mode. The hand-validated reference `export_FullScene_Transparency_Verified.glb` (generator
"MaxiMall Full-Scene Runtime AR Export (Validated)", material names like `Mat_ClosetMesh_Slot1_Glass`,
`bc [0.85,0.9,0.95,0.25] BLEND rough 0.05`, `MetalFrame bc [0.15,...] rough 0.25`) proves the *desired* output —
and that an earlier, non-generic code version produced it. The current generic pipeline lost these materials.
Status: **PROVEN divergence** between current output and the validated target for parameter-less materials.

**F3. glTF core alpha blending cannot express refraction/transmission.** The writer emits no
`KHR_materials_transmission`/`ior`/`volume`. Alpha-BLEND glass is a flat see-through tint — no refraction, weak
reflections under model-viewer's neutral environment. This is a real limitation, separate from any bug.
Status: PROVEN (writer emits core-only materials).

## G. Mirror

**G1. Current representation**: mirror surface (`white` material — a V-Ray import whose graph exposes a strong
neutral Reflection) → Metallic 0.95 / Roughness 0.35, no texture. In model-viewer/AR this renders as blurry
metal reflecting the `environment-image="neutral"` studio HDR — **not** the room.
Status: PROVEN from logs/GLB.
**G2. Fundamental limit**: glTF/WebAR has no planar reflections or live environment capture (Scene Viewer/Quick
Look add their own camera-based lighting estimation at best). UE-style mirror parity is **not achievable** in this
target format; a polished-metal approximation (roughness ≈0.05–0.1, metal 1.0) is the realistic ceiling. Current
0.35 roughness makes it duller than even that ceiling — small improvement available, parity impossible.

## H. UE viewport vs Manual UE GLB vs Runtime GLB

Ranked causes of remaining visual difference, with what the project actually proves:

1. **Parameter-less `UMaterial`s export as White/0.5/Opaque** (аttik, cеrniy_mеtаl, Material__18,
   Material__-2147483634_2, M_Change_Color-before-MID). The resolver can only see *parameters*; graphs built from
   constant nodes yield nothing, and the fallback is silent white. **PROVEN** — this is the single largest source
   of "GLB ≠ UE" in today's exports (a black metal frame renders white).
2. **Constant roughness/metallic per material; no MR/AO textures** — PROVEN (B4).
3. **Environment/lighting/tone mapping**: model-viewer neutral HDRI, exposure 1.0, its own tone mapper vs UE's
   scene lighting + filmic tonemapper. PROVEN different by configuration; the *magnitude* per material is
   SUSPECTED/unmeasured. This affects manual UE GLB exactly the same way — which matches your observation that
   Manual ≈ Runtime but both ≠ viewport.
4. **Luminance²×Tint bake approximates one graph family** — other graphs (different power, overlays, detail
   normals, fresnel tints) will not match. PROVEN limited by construction.
5. **Possible mirrored geometry** (A3) — SUSPECTED.
6. Missing transmission for glass (F3), mirror ceiling (G2) — PROVEN limits.

## I. QR / local HTTP / phone / WebAR — why the phone flow fails

**I1. The application never starts, checks, or manages any HTTP server.** No server code exists in Source (only
`LocalServerPort`/URL formatting); serving depends entirely on someone manually running
`run_local_ar_server.bat`. At audit time **nothing was listening on port 8080** (netstat). The export UI shows a
QR immediately regardless — the QR can (and today does) point at a dead URL.
Status: **PROVEN**. This alone guarantees intermittent "QR does nothing" failures.

**I2. The batch file's server selection works on this machine but is fragile.** `where node` succeeds
(`C:\Program Files\nodejs\node.exe`) → `server.js` runs: binds 0.0.0.0:8080, correct `model/gltf-binary` MIME,
CORS `*`, implements `/api/latest-model` (which `index.html` uses when no `?model=`), path-traversal guard.
PROVEN good. But the python fallback is a trap on this machine: `python` resolves to the WindowsApps **Store
alias stub** — `where python` succeeds, running it prints the Store message and exits (verified live). Only
relevant if node is ever missing. The `192.168.10.138` in server.js's banner is cosmetic (log text only).

**I3. IP/URL/QR generation is correct on this machine.** Single active NIC 192.168.10.138 (Ethernet);
`GetLocalHostAddr` non-127 filter; QR encodes exactly
`http://192.168.10.138:8080/index.html?model=export_….glb` (81 chars, matches log). Nayuki QR encoder, ECC M,
version 5, 8-module quiet zone, nearest filtering — scannable. PROVEN. (Risk only if a VPN/virtual adapter is
added later — the first local addr may be wrong then.)

**I4. Firewall/network profile is NOT the blocker.** Network is on the **Public** profile, but enabled inbound
Allow rules for node.exe cover Public (verified via Get-NetFirewallRule). PROVEN for node; the PowerShell
fallback server (`http://+:8080` HttpListener) would additionally require admin URL-ACL rights and has no
firewall rule — fragile, but unused while node exists.

**I5. Even with the server running, the AR *launch* step cannot succeed over plain HTTP.**
- `webxr`: WebXR requires a **secure context**; `http://192.168.10.138` is insecure → `navigator.xr` absent →
  mode unavailable.
- `scene-viewer` (Android): Scene Viewer's documented requirement is an **HTTPS** model URL; an http LAN URL
  fails to load in the Scene Viewer app.
- `quick-look` (iOS): requires **USDZ** via `ios-src`; the pipeline never generates USDZ → no AR offer on
  iPhone at all. iOS Safari has no WebXR.
Net effect: the phone shows the 3D orbit viewer (that part works over http, CDN script URL verified live), but
"Place in Your Room" is hidden or fails on both platforms.
Status: mechanism **PROVEN** from code + platform requirements; the exact on-phone symptom needs one
confirmation test (T1/T2). **This — plus I1 — is the root cause of the failed QR→AR flow. It is not fixable by
another IP/port/QR tweak; it requires HTTPS (tunnel/cloud) for Android and a USDZ path for iOS.**

**I6. 37 MB GLB (B3) makes the phone step slow even when everything else works** — PROVEN file size; borderline
for cellular/weak Wi-Fi and for Scene Viewer timeouts.

---

## 1. 100% PROVEN problems

| # | Problem | Evidence |
|---|---------|----------|
| P1 | No server lifecycle: QR generated even when nothing serves port 8080; app never starts/checks the server | Source has zero server code; netstat empty; ARExportSubsystem.cpp:1010 |
| P2 | AR launch requirements unmet by design: no HTTPS (WebXR+Scene Viewer), no USDZ (iOS Quick Look) | index.html:141; no USDZ code; platform requirements |
| P3 | Duplicate texture embedding: cache key includes SectionIdx → identical 16 MB PNG twice in one GLB (37 MB total) | ARExportSubsystem.cpp:891; GLB 22:41 images 0 & 2 identical size; log |
| P4 | Parameter-less UMaterials silently export as White/0.5/Opaque (black metal → white; closet glass → opaque white) | Diag logs; GLB materials; contrast with validated reference GLB |
| P5 | Metallic/Roughness maps never exported (writer supports, subsystem never fills) | 0 refs in ARExportSubsystem.cpp |
| P6 | Game-thread texture baking blocks UI ~2 s per export | Log timestamps |
| P7 | Textured-dielectric roughness is a constant 0.60 regardless of actual material | ARExportSubsystem.cpp:644; GLB |

## 2. SUSPECTED problems (not yet proven)

| # | Suspicion | Promote-to-proven via |
|---|-----------|----------------------|
| S1 | Geometry mirrored L↔R vs UE (axis permutation without handedness fix; winding never reversed, masked by forced doubleSided) | Test T3 |
| S2 | Cross-mesh masked-bake leak: cache key lacks mesh identity (same texture+tint+section index on two meshes → wrong mask reuse) | Test T5 / code inspection is conclusive on mechanism |
| S3 | Packaged builds lose all textures (editor-only Source data; fallback handles only PF_B8G8R8A8) | Package & export once |
| S4 | ChromaticDiff≤12 misclassification: desaturated full-color textures get Luminance²-crushed; dark glossy dielectrics with strong neutral V-Ray reflection become mirror-metal | Targeted material tests |
| S5 | Multi-texture materials: "first non-normal texture wins" can pick an ORM/mask texture as BaseColor (editor path has no sRGB filter) | Inspect any such material |
| S6 | VPN/second NIC would break IP selection (currently single NIC, so fine today) | Only relevant if topology changes |

## 3. Working behavior — do NOT change without new evidence

- Per-slot, per-section independent material resolution incl. component overrides and runtime MIDs (B1/B2 — log-proven).
- RAL/NCS round trip: `OnRep_CustomColors` MID params ↔ resolver name list; Luminance²×Tint bake with per-section
  UV mask for the RF-color family (C1).
- Full-color tiled texture preservation (no mask, REPEAT sampler, vertex-level UV transform) — the validated wood result (D2).
- V-Ray metal/mirror heuristic for the tested faucet/mirror assets (E2).
- Ceramic guard preventing sinks from becoming glass (F1).
- GLB container format: header/chunk layout, alignment, accessor min/max, per-material alphaMode — validates and parses cleanly (writer).
- `server.js` itself (MIME/CORS/latest-model/traversal guard) and QR generation (Nayuki, correct content, scannable).
- Local IP resolution and firewall rules on this machine.

## 4. Minimal tests for you to perform

**T1 — Baseline phone test (5 min).**
ACTION: Run `run_local_ar_server.bat`, export from UE, scan QR with an Android phone on the same Wi-Fi.
EXPECTED A (hypothesis I5 correct): 3D model loads and orbits in browser; AR button missing, or Scene Viewer
opens and fails to load the model.
EXPECTED B: AR placement works end-to-end.
PROOF: A promotes I5 from mechanism-proven to symptom-proven. (Also confirms I1: without the bat, nothing loads.)

**T2 — HTTPS hypothesis test (10 min, no code changes).**
ACTION: Expose the same folder via an HTTPS tunnel (e.g. `cloudflared tunnel --url http://localhost:8080` or
ngrok — one-off run, no install into the project), open the generated https URL on the phone, tap AR.
EXPECTED A: Scene Viewer/WebXR AR now launches on Android → HTTPS is the missing requirement (P2 confirmed as
the actionable fix).
EXPECTED B: AR still fails → something else (report the on-screen error).
PROOF: A makes "serve over HTTPS" the proven fix for Android; iOS still needs USDZ regardless.

**T3 — Mirroring test (5 min).**
ACTION: Configure an asymmetric booth state (e.g. door handle / hinge clearly on one side), export, view the GLB
side-by-side with the UE viewport.
EXPECTED A: handle appears on the opposite side in the GLB → S1 proven (fix = negate one axis + reverse winding).
EXPECTED B: same side → S1 refuted.

**T4 — (Only if you ship packaged builds) Packaged texture test.**
ACTION: Package the game, run an export, open the GLB.
EXPECTED A: textures missing/flat colors → S3 proven. EXPECTED B: textures intact → S3 refuted.

## Verdict

**Does the current system satisfy:** *any supported runtime mesh → any number of slots → current runtime
Material/MID per slot → independent evaluation → one configured GLB → QR → working phone AR?*

# **PARTIALLY**

- The geometry/slot/MID half is genuinely working and log-proven: arbitrary slot counts, component overrides,
  runtime MIDs, independent per-slot PBR resolution, valid GLB output (B1, B2, B5).
- The material half is generic **only for materials that expose parameters**; parameter-less UMaterials silently
  export as white (P4), roughness/metallic are constants (P5, P7), and several classification heuristics are
  tuned to the tested assets (S4, S5).
- The QR→phone→AR half **cannot currently complete**: the server is a manually-started external process the app
  neither launches nor verifies (P1), and even when it runs, plain-HTTP LAN serving fails every AR launch mode —
  WebXR (secure context), Scene Viewer (https requirement), Quick Look (no USDZ) (P2). The phone gets a 3D
  preview at best, never reliable AR placement.

---

## OPTIONAL: If I were designing this AR export system from zero

*(Separate from the audit. No changes made or proposed to current files.)*

**Would I build it differently? Yes — in two specific places (material acquisition and delivery). The
geometry/scene-collection layer and the writer I would keep essentially as-is.**

**1. Material evaluation: render, don't parse.** The current resolver reverse-engineers material graphs from
parameter *names* — that is the root of every heuristic (ChromaticDiff, reflection-metal thresholds, LumSq graph
assumption, 0.60 fallback) and of the parameter-less-UMaterial blind spot. The robust alternative used by UE's
own glTF exporter and Datasmith is **GPU material baking**: draw each material slot into small render targets
(BaseColor / Roughness / Metallic / Normal / Opacity as separate bake passes using the actual compiled shader,
unwrapped in UV space of the actual mesh section). That evaluates *any* graph — constants, functions, fresnel,
whatever — with zero classification, works identically in packaged builds, and produces a real
metallicRoughness texture (fixing P5) instead of one constant. Runtime MIDs are handled for free because the
bake uses the material instance the component actually renders with. Classification disappears almost entirely;
the only remaining decision is blend mode → alphaMode, read from the compiled material (as today). Bakes run on
the render thread, not the game thread (fixes P6). Cache key: (MaterialInterface pointer + parameter-state hash +
mesh section UV hash) — correct by construction (fixes P3/S2).
*Cost:* render-target plumbing and a UV-unwrap pass; tiled materials need the "bake in tile space, keep REPEAT"
special case you already discovered. This is the one genuinely hard part and your current tiled-UV insight carries
over directly.

**2. Delivery: stop depending on LAN topology.** LAN HTTP can never launch AR (P2). Two workable shapes:
- **Cloud relay (recommended for clients):** POST the GLB to any HTTPS endpoint you already operate (the project
  already has AWS tooling) → S3/CloudFront URL → QR encodes the https viewer URL. Works on any network, no
  firewall/IP logic at all, Scene Viewer + WebXR satisfied. Add server-side or on-demand GLB→USDZ conversion
  (e.g. Apple's usdzconvert on a small worker) for iOS Quick Look.
- **Fully-offline variant (if cloud is forbidden):** embed a tiny HTTPS server in the app (self-signed certs are
  rejected by Scene Viewer, so realistically this still degrades to 3D-preview-only on Android — which is why the
  cloud relay is the honest answer for *AR*).
Either way the app should own the server lifecycle: start/verify before showing the QR, and show an error state
when the URL isn't reachable (fixes P1).

**3. Keep:** booth-scoped `GetComponents` collection, per-section primitives, the SimpleGLBWriter container
(spec-clean), Nayuki QR, model-viewer page (add `ios-src` + a loading/progress bar; pin the CDN or self-host the
script for internet-less Wi-Fi).

| Aspect | Current | From-zero | Advantage | Cost/Risk |
|---|---|---|---|---|
| Material state | Name-based parameter parsing + heuristics | GPU bake of compiled shader per slot | Any graph, any build config, MR maps, no heuristic collisions | Render-target infra; tile-space bake path |
| Caching | Path+tint+sectionIdx string key | Material+param-hash+UV-hash key | No dup embeds, no cross-mesh leaks | none |
| Delivery | Manual bat + LAN http | App-managed upload → HTTPS + USDZ | AR actually launches on both platforms | Needs an endpoint; ~1 s upload |
| Geometry | Section→primitive, cm→m, forced doubleSided | Same + handedness fix + winding reversal | Correct culling/mirroring | trivial |

If the requirement had stayed "RAL cabinets on one LAN with Android developer phones", the current architecture
would be defensible. For the stated requirement — arbitrary materials, arbitrary clients' phones, reliable AR —
the parse-based material layer and the LAN-http delivery are both structurally at their ceiling, and I would
replace exactly those two layers while keeping the rest.
