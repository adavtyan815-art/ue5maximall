# Linux Packaged Client (Pixel Streaming) — Generic GLB Export Investigation (2026-08-26)

Analysis only. No production code changed. Evidence: UE 5.3.2 engine source at `D:\Programs\Epic Games\UE_5.3`
(GLTFExporter plugin v1.3.1, MaterialBaking module), project source/config, and the accepted PIE proof
(byte-identical antr/MR textures + blue-MID bake, see section 07 report and `Saved/AR_Exports/New folder/`).

Evidence classes used throughout:
- **[PROVEN]** — read directly from UE/project source or demonstrated by artifact.
- **[LIKELY]** — strongly implied by design/known engine behavior, not directly demonstrated.
- **[PROTOTYPE]** — requires the packaged-build prototype to confirm.

---

## 1. What exactly prevents the PIE baking path in a Linux packaged client

1. **[PROVEN]** The plugin links its baking dependencies only in editor builds:
   `GLTFExporter.Build.cs` adds `MaterialBaking`, `MaterialUtilities`, `MeshMergeUtilities`, `UnrealEd`, …
   under `if (Target.bBuildEditor)`. A Client target never gets those modules.
2. **[PROVEN]** `MaterialBaking.Build.cs` itself depends on **`UnrealEd`** — the module cannot exist in any
   packaged target on any platform. This is the hard blocker, not Linux specifically.
3. **[PROVEN]** All bake calls in `GLTFDelayedMaterialTasks.cpp` are inside `#if WITH_EDITOR`; the `#else`
   branch logs: *"Can't properly export material %s in a runtime environment without a glTF proxy … Create
   glTF Proxy Material"*. Epic's designed runtime answer is proxies.
4. **[LIKELY]** Underlying reason baking can never be runtime: it compiles temporary material-property proxy
   shaders, and shader compilation infrastructure is editor-only (no runtime shader compiler in packaged
   builds).
5. **Not a blocker** (unlike our custom exporter): editor-only texture Source data. **[PROVEN]** the plugin
   never touches `Texture->Source` at runtime — see §2.

## 2. GLTFExporter runtime-safety map (what works in a packaged client)

Runtime-safe (compiled and functional with `WITH_EDITOR=0`), all **[PROVEN]** from source:

| Capability | Mechanism |
|---|---|
| Entry point | `UGLTFExporter::ExportToGLTF(...)` — static, BlueprintCallable, Runtime module (`"Type": "Runtime"`, PlatformAllowList Win64/Mac/**Linux**) |
| Mesh geometry (cooked) | `FGLTFBufferAdapterGPU` reads index/position/tangent/texcoord buffers **from RHI via GPU readback** (`ReadRHIBuffer`) — no `bAllowCPUAccess` needed |
| Component/MID materials | `ResolveMaterials(Materials, StaticMeshComponent->GetMaterials())` — live per-slot component materials |
| Textures (cooked, compressed) | `FGLTFTextureUtilities::DrawTexture` renders any `UTexture2D` into a render target (GPU decompress) + `ReadPixels` readback; `GLTFDelayedTextureTasks.cpp` has **zero** `WITH_EDITOR` gates |
| Proxy resolution & reading | `UGLTFMaterialExportOptions::ResolveProxy` (AssetUserData walk) and `GetProxyParameters` (plain parameter reads) — ungated; `bExportProxyMaterials` defaults **true** |
| Alpha/blend, two-sided, cutoff | Read from the material interface API (`GetBlendMode`, `IsTwoSided`, `GetOpacityMaskClipValue`) — ungated |
| Texture transforms | Proxy texture params carry `UV Offset/Scale/Rotation/UVIndex` → exported as `KHR_texture_transform` (warning + strip if `bExportTextureTransforms` disabled) |
| GLB container/writer | Pure JSON + buffer code, ungated |

Editor-only (**[PROVEN]**): property baking (constant-folding analysis, `TryGetBakedMaterialProperty`,
`TryGetSourceTexture`'s expression analysis), `FGLTFMeshData`/MeshDescription (lightmap-UV bake, UV
degenerate/overlap checks), **proxy creation** (`GLTFMaterialProxyFactory.cpp` is entirely `#if WITH_EDITOR`;
`FGLTFProxyMaterialUtilities::CreateProxyMaterial`/`SetParameterValue` are `#if WITH_EDITOR`), the Content
Browser action. No `UE_BUILD_SHIPPING` gates anywhere in the exporter **[PROVEN]** → Shipping links it
(behavior in Shipping = same as Development client) **[LIKELY, PROTOTYPE for confirmation]**.

Conclusion: in a packaged client the exporter is a **proxy-parameter reader + GPU-readback serializer**.
Everything hinges on proxies existing for the materials.

## 3. Can glTF Proxy Materials reproduce our live runtime state?

**Mechanism [PROVEN]:** a proxy is a `UMaterialInstanceConstant` whose parent is one of three plugin base
materials (`M_GLTF_Default`, `M_GLTF_ClearCoat`, `M_GLTF_Unlit` in plugin Content), holding glTF-semantic
parameters: `Base Color` (texture), `Base Color Factor`, `Metallic Factor`, `Roughness Factor`,
`Metallic Roughness` (texture), `Normal`, `Occlusion`, `Emissive`, plus per-texture `UV Offset/Scale/
Rotation/UVIndex`. Blend mode / two-sided are copied onto the proxy as `BasePropertyOverrides`
(`SetBaseProperties`, factory line 67). At export, `ResolveProxy` substitutes the proxy via AssetUserData on
the original; **or** if the exported material's root base IS a proxy base material (e.g. a runtime MID whose
parent chain ends in a proxy), `GetProxyParameters` reads the parameters directly off that material —
inheritance means **live MID values flow through** [PROVEN from `IsProxyMaterial(BaseMaterial)` +
`GetVectorParameterValue` reads].

Per material family:

- **Material swaps (wood/stone/metal/glass per slot):** exact. Each configurator material asset gets its own
  proxy, baked in the editor by the *same* pipeline the accepted manual export uses. Swapping the slot swaps
  the resolved proxy. **[PROVEN mechanism]**
- **RAL/NCS runtime recolor (MIDs):** works with one critical rule. A proxy bakes with the material's
  parameter values *at creation time*. A runtime color must therefore multiply a **neutral** baked texture:
  the recolor bridge must use the proxy of the neutral master (white default), and set
  `Base Color Factor = chosen color`. That is mathematically identical to the currently proven RAL path
  (baked grayscale response × runtime color). Creating the proxy from an already-tinted MIC (e.g. `antr`,
  tint 0.098) and then applying a factor would double-modulate. **[PROVEN math/mechanism; neutral-master
  defaults need a one-time content check]**
- **Direct MIDs at export time:** a component's runtime MID has no AssetUserData, so `ResolveProxy(MID)`
  returns the MID itself and export errors **[PROVEN]**. A small generic bridge is required in our export
  layer (~30 lines): for each slot whose material is a MID, take
  `FGLTFProxyMaterialUtilities::GetProxyMaterial(ParentChainAssetWithUserData)` (public, runtime-safe
  **[PROVEN]**), create a transient MID from that proxy, set `Base Color Factor` from the **booth's own
  CustomColors state** (no material introspection, no name heuristics), temporarily assign, export, restore.
- **Glass:** proxy keeps Translucent blend mode via BasePropertyOverrides → exporter emits `alphaMode BLEND`
  with `Base Color Factor.A` opacity **[PROVEN mechanism]**. Same glTF limitation as everywhere: no
  refraction/transmission extension in the 5.3 exporter.
- **Metal / roughness / normals:** baked into proxy factors/textures at creation with full editor fidelity —
  including real `Metallic Roughness` textures (which the accepted manual GLB has and our custom exporter
  never produced).

**Fidelity limit to state honestly:** proxies capture a *snapshot* of each material; runtime parameter
changes are faithful only where the parameter enters glTF semantics directly (color factor multiply, scalar
metallic/roughness factors, UV transforms, opacity). For this configurator — swaps + tint recolors — that
covers the real use cases. Arbitrary graph-parameter animation (e.g. a runtime-varied procedural pattern)
would not be captured; that limitation is fundamental to any no-runtime-shader-compile approach.

## 4. Preparation required, and can it be automated?

- One proxy per unique material asset reachable by the configurator (masters and/or MICs; recolorable
  families use their neutral master).
- **Zero-code path:** Content Browser → select materials (bulk multi-select) → right-click →
  **Create glTF Proxy Material**. Attaches proxy via AssetUserData; originals still render normally in UE
  (visuals unchanged — proxies are used only by the exporter) **[PROVEN mechanism; bulk multi-select
  behavior LIKELY — standard asset-action pattern, verify with one right-click]**.
- **Scripted path:** `FGLTFMaterialProxyFactory` is private/unexported **[PROVEN]**, so full automation from
  project code means an editor-module utility recombining the public pieces (`FGLTFContainerBuilder`,
  `FGLTFProxyMaterialUtilities::CreateProxyMaterial/SetProxyMaterial` — public, editor-gated). Feasible
  (~1–2 days) but only worth it if the material set churns often.
- Cooking: proxies ride along automatically — AssetUserData on a cooked material references the proxy MIC →
  cooked into the client. **[LIKELY; PROTOTYPE confirms]**
- Re-run proxy creation when a material's look changes (same discipline as any derived asset).

## 5. Genericity across meshes/slots

Proxies attach to **materials**, not meshes or slots **[PROVEN]**. Any mesh, any slot count, any runtime
swap resolves per-slot through `StaticMeshComponent->GetMaterials()` → proxy. The MID bridge keys off booth
state, not names. No Wood/RAL/Glass/Metal branches, no name matching, no texture-color heuristics anywhere
in the export path.

## 6. If proxies were rejected: GBuffer/render-target fallback

Runtime-safe on Linux (desktop deferred renderer): `USceneCaptureComponent2D` with `SCS_BaseColor`/
`SCS_Normal`, plus 2–3 pre-authored post-process materials reading SceneTexture Metallic/Roughness, capturing
each slot in isolation; original textures extracted via the same DrawTexture/ReadPixels trick. Verdict:
**strictly worse than proxies** — screen-space (occlusion/coverage issues), per-slot *constants* only (no
per-pixel MR), needs slot-isolation machinery and a custom writer, and still can't capture procedural
patterns to texture. Keep only as a last-resort idea; not recommended.

## 7. Specific concerns

- **Tiled UVs:** preserved — proxies reference the original texture plus `UV Offset/Scale/Rotation`
  parameters exported as `KHR_texture_transform` **[PROVEN]**. Enable `bExportTextureTransforms`; viewer
  (`model-viewer`) supports the extension **[LIKELY]**.
- **Multiple UV channels:** exported (accepted PIE GLB already contains TEXCOORD_0/1) **[PROVEN]**; per-texture
  `UVIndex` selects the channel.
- **Texture size / GLB size:** proxies reference original textures at native size (a 4K wood → large PNG, the
  same ~16 MB scale seen before). Mitigations available in options: `TextureImageFormat=JPEG` (lossy, huge
  reduction, PNG kept automatically where alpha needed) **[PROVEN option exists]**; per-material bake-size
  overrides at proxy-creation time.
- **Transparency:** blend mode via BasePropertyOverrides → `alphaMode`; opacity via factor alpha. No
  transmission/refraction (5.3 exporter has no KHR_materials_transmission) **[PROVEN]**.
- **Metallic/Roughness/Normal:** real MR textures + factors from the editor bake; normal maps flipped to glTF
  convention by option `bAdjustNormalmaps` **[PROVEN]**.
- **Runtime MID changes:** color/scalar/UV-transform changes exact via the bridge (§3); graph-structural
  changes not capturable (fundamental).

## 8. Performance estimate (real booth under Pixel Streaming)

No baking happens at runtime — cost is GPU readbacks + PNG/JPEG encode + JSON:
- PIE proof measured 3.1 s / 1.0 s (with editor baking!) for a 2-slot mesh; packaged proxy path skips baking
  entirely.
- Booth (~10 components, ~20 primitives, ~6–10 unique textures): expect **~2–6 s** on the game thread,
  dominated by texture readback + PNG encode of large textures; JPEG option roughly halves it. **[LIKELY;
  PROTOTYPE measures]**
- Under Pixel Streaming this blocks the game thread → the video stream freezes for that duration on click.
  Acceptable for v1 (current custom exporter also blocks ~2 s); can later be mitigated by moving encode off
  the game thread (the readbacks must stay on game/render threads).
- Delivery note (separate problem, from the earlier audit): in Pixel Streaming production the GLB lands on
  the **cloud VM**. A QR pointing at that VM's private IP is unreachable from the user's phone, and AR launch
  requires HTTPS anyway — the export must be uploaded to a phone-reachable HTTPS endpoint (S3/CloudFront or
  similar). Plan this as part of production wiring regardless of the material solution.

## 9. Shipping Linux Client viability

- Module is Runtime type, Linux allowed, no Shipping-specific gates in the exporter **[PROVEN]**.
- Everything used at runtime (parameter reads, RHI readbacks, render-target draws) is ordinary game-runtime
  API **[PROVEN by inspection; PROTOTYPE confirms on Vulkan/Linux]**.
- Caveats: console is disabled in Shipping — the production trigger must be a direct function call from the
  AR button (it already would be); prototype/testing should use a Development client where the console works.

## 10. Architecture comparison and recommendation

| | A. Proxies + official exporter | B. GBuffer probe + custom writer | C. Editor-build bake service | D. Current heuristics |
|---|---|---|---|---|
| Material fidelity | Editor-bake quality snapshots; exact for swaps/tints | Constants only, screen-space risks | **Full live bake (PIE-proven path)** | Proven failure modes (white fallback, no MR) |
| Works in Shipping Linux client | Yes (proxy reads + readback) | Yes | N/A on client (runs on service) | Breaks (editor-only texture Source) |
| Heuristic-free | Yes | Mostly | Yes | No |
| Content prep | One-time proxy creation + neutral-master rule | None | None | None |
| Infra | None | None | Editor-build container + state handoff | None |
| Code | Small bridge (~30 lines) + trigger | Large custom system | Service + serialization wiring | Already exists |
| MR textures / KHR transforms / TEXCOORD_1 | Yes | No | Yes | No |

**Recommendation: Architecture A — glTF Proxy Materials + the official `UGLTFExporter` in the Linux client**,
with the neutral-master rule for recolorable families and the small MID bridge. It is Epic's designed runtime
path, reuses the exporter whose output you already accepted as the reference, removes every heuristic, and
needs no new infrastructure. Architecture C (a headless editor-build bake service fed by the existing saved
booth-state JSON) is the zero-content-prep alternative with perfect live fidelity — choose it only if adding
an infra component is acceptable and material churn makes proxy upkeep annoying. B and D are not recommended.

## 11. Smallest Linux prototype (design only — not implemented)

Goal: prove/disprove A end-to-end on a packaged client against the accepted manual GLB
(`Saved/AR_Exports/New folder/SM_MERGED_StaticMeshActor_16.glb`).

**Editor prep (10 min, Windows editor):**
1. Content Browser → select `antr` and `Wood_` → right-click → *Create glTF Proxy Material* (note where the
   proxies are created, e.g. same folder). Save.
2. (MID-recolor leg) also create a proxy for the RAL **master** (`Material__-2147483646`) — check its default
   color is neutral/white first; if not, note the value (the test will reveal double-tinting).
3. Project Settings → Packaging → *Additional Asset Directories to Cook*: add the folder(s) containing
   `SM_MERGED_StaticMeshActor_16`, the two materials, and the proxies (nothing references them from a map).

**Code (one small edit to the existing temp file `GLTFOfficialExportProto.cpp` — still zero production code):**
- Extend the MID-proof branch: instead of creating the MID from the slot material, create it from
  `FGLTFProxyMaterialUtilities::GetProxyMaterial(Slot0Material)` when available and set
  `"Base Color Factor"` to the proof blue. Everything else (spawn, export A/B, auto-run flag) already works
  and compiles in client targets.

**Build & run (stage 1 = Windows packaged client — catches ~90% of the risk with fast turnaround):**
```
RunUAT BuildCookRun -project=.../awsTutorial.uproject -platform=Win64 -clientconfig=Development
  -client -build -cook -stage -pak
```
Run the staged client with `-ARGLTFProto`; collect `awsTutorial/Saved/AR_Exports/New folder/proto_official_*.glb`
from the staged build's Saved directory.

**Stage 2 = same cook for Linux** (`-platform=Linux -client`), run on the streaming VM (or any Linux box with
Vulkan), same flags, same collection.

**Pass criteria (compare with the accepted manual GLB in the same viewer + the compare_glb.js structural dump):**
1. No `"Can't properly export material"` errors in the client log — proxies resolved.
2. As-is export: `antr` slot visually matches the manual GLB (proxy bake ≈ direct bake); wood slot shows the
   full-resolution tiled texture (KHR_texture_transform present if tiling params exist).
3. MID export: slot 0 is unmistakably blue → live MID values flow in a **packaged** build.
4. Structure: materials carry `baseColorTexture` + `metallicRoughnessTexture` like the manual export;
   TEXCOORD channels present; file loads in model-viewer.

**Fail signatures and what they mean:** missing/black textures → RHI readback issue on that platform;
`Can't properly export` → proxy user data lost in cook (then: reference proxies explicitly or fix cook list);
washed-out/double-dark colors on the MID leg → non-neutral master default (apply the neutral-master rule).

## 12. Alternative from-zero architecture (not a modification of anything current)

**Booth-state bake service:** the packaged Linux client sends the existing serialized booth configuration
(the save system already produces this JSON) to a small service running `UnrealEditor-Cmd <project> -game`
(editor binaries, uncooked content — exactly the environment of the accepted PIE proof). The service loads
the state onto a booth, calls `ExportToGLTF` with full `UseMeshData` baking, uploads the GLB (and an
iOS USDZ later) to HTTPS storage, and returns the URL the QR encodes. Perfect live-bake fidelity, zero
content preparation, zero heuristics, and it solves the Pixel-Streaming delivery problem (phone-reachable
HTTPS URL) in the same step. Costs: an infra component (~editor-sized container), state-handoff wiring, and
export latency including service round-trip. If the team is comfortable operating one more container, this is
the strongest long-term architecture; Architecture A is the strongest client-only one.
