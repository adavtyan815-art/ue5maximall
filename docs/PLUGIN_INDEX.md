# Plugin Index

> **Audit date:** 2026-08-21
> Every plugin has a detailed `PLUGIN_DOC.md` **inside its own folder**. This page is the map.
>
> See also: `docs/PROJECT_ARCHITECTURE.md` and `docs/AUDIT_2026-08-21_FINDINGS.md`.

---

## Quick verdict table

| Folder | Friendly name | Modules | Content refs | Verdict | Doc |
|---|---|---|---|---|---|
| `Ultimate969078bf2772V2` | **awsSDK** — Cognito / DynamoDB / GameLift / Lambda + GameLift Server SDK | 18 | many | **REQUIRED — core** | [doc](../Plugins/Ultimate969078bf2772V2/PLUGIN_DOC.md) |
| `EmbeddedVoiceChatPixelStreamingPlugin_UE5.6` | Embedded Voice Chat ↔ Pixel Streaming bridge | 1 | 2 | **REQUIRED — core, but has S1 bugs** | [doc](../Plugins/EmbeddedVoiceChatPixelStreamingPlugin_UE5.6/PLUGIN_DOC.md) |
| `AutoDeSef64e1bfc0e22V2` | AutoDeSerialize | 2 | **14** | **REQUIRED AND CORRECT** | [doc](../Plugins/AutoDeSef64e1bfc0e22V2/PLUGIN_DOC.md) |
| `WebSocke3911c0965375V2` | WebSocket | 1 | 4 | **REQUIRED** (pending voice-transport confirmation) | [doc](../Plugins/WebSocke3911c0965375V2/PLUGIN_DOC.md) |
| `RegexpBl98e2507377d2V2` | Regexp | 1 | 3 | **REQUIRED AND CORRECT** | [doc](../Plugins/RegexpBl98e2507377d2V2/PLUGIN_DOC.md) |
| `Restfulh16cb1dcaf2b4V2` | Restful | 1 | 2 | **REQUIRED AND CORRECT** (one open question) | [doc](../Plugins/Restfulh16cb1dcaf2b4V2/PLUGIN_DOC.md) |
| `QREncode783839f97b45V2` | QR Encode | 1 | 1 | **REQUIRED IF MFA SHIPS** | [doc](../Plugins/QREncode783839f97b45V2/PLUGIN_DOC.md) |
| `Enhanced2db8f2adfc28V2` | Enhanced Scroll Box | 1 | 1 | correct, cosmetic, marginally used | [doc](../Plugins/Enhanced2db8f2adfc28V2/PLUGIN_DOC.md) |
| `Clipboar9fd0aa5b4ecdV2` | Clipboard | 1 | 1 | **POSSIBLY REMOVABLE** — inert on headless Linux | [doc](../Plugins/Clipboar9fd0aa5b4ecdV2/PLUGIN_DOC.md) |
| `icmppingd30c78843dcfV2` | Icmp | 1 | 1 | **POSSIBLY REMOVABLE** — architecturally wrong under Pixel Streaming | [doc](../Plugins/icmppingd30c78843dcfV2/PLUGIN_DOC.md) |
| `RuntimeDataTable_5.4` | Runtime DataTable + EasyCsv | 2 | **0** | **UNUSED — probably removable** | [doc](../Plugins/RuntimeDataTable_5.4/PLUGIN_DOC.md) |
| `UBIKSolver-main` | UBIK Solver (VR upper-body IK) | 2 | **0** | **UNUSED — probably removable** | [doc](../Plugins/UBIKSolver-main/PLUGIN_DOC.md) |

"Content refs" = number of `.uasset`/`.umap` files (out of 2 269) whose import table names the
plugin's script package.

## Engine plugins enabled in `awsTutorial.uproject`

Content-reference counts are `/Script/<Module>` hits across all 2 269 project assets; "staged" counts
are rows in `LinuxClient/Manifest_UFSFiles_Linux.txt`.

| Plugin | Content refs | Staged rows | Verdict |
|---|---|---|---|
| `PixelStreaming` | 3 | 12 | **REQUIRED.** Also linked into the dedicated server unnecessarily — see `PKG-03` |
| `ProceduralMeshComponent` | 0 | 1 | **REQUIRED** — referenced from C++ (`ARoomPlannerManager`, `AProceduralWallActor`), so a 0 asset count is expected. Only removable if the room planner is cut |
| `AnimationLocomotionLibrary` | **185** | — | **REQUIRED** — character locomotion |
| `AnimationWarping` | 9 | 1 | **REQUIRED** — character animation |
| `DatasmithImporter` / `DatasmithFBXImporter` | — | 120 (shared with DatasmithContent) | **Editor-only importers**, but they auto-enable `DatasmithContent`, and **702 project assets carry `/Script/DatasmithContent` user data** (every mesh under `Content/AllModels/`, `Content/Models*`). So `DatasmithContent` is genuinely needed at runtime. The two *importers* could take `"TargetAllowList": ["Editor"]`; the saving is only their `.uplugin` + `.ini`, so this is low priority |
| `GameplayAbilities` | **1** (`Content/Characters/Heroes/Abilities/AN_Melee.uasset`) | 3 | **POSSIBLY REMOVABLE.** One animation notify from the template's hero content. If `AN_Melee` is cut with the rest of the FPS-demo content, this plugin goes too |
| `HDRIBackdrop` | **0** | 18 | **POSSIBLY REMOVABLE.** No project asset references it, yet its Blueprint and materials are cooked into the client because plugin content is staged for enabled plugins. Verify no level uses an HDRIBackdrop actor, then disable |
| `ModelingToolsEditorMode` | — | — | **CORRECT** — already `"TargetAllowList": ["Editor"]` |

> Note: plugins living in the project's `Plugins/` folder are enabled by default and do **not** need
> an entry in the `.uproject`. That is why the 12 folders above are absent from the `.uproject`
> plugin list yet all appear in the staged build manifests.

## Grouped by our architecture requirement

| Requirement | Plugins involved |
|---|---|
| **Login** | `awsSDK` (CognitoIdp, CognitoIdentity, AWSCore), `Regexp`, `AutoDeSerialize`, `QREncode` (MFA) |
| **Public / private rooms** | `awsSDK` (GraphQL, GameLift client), `AutoDeSerialize`, `Regexp`, `Icmp` (to be removed) |
| **Dedicated-server connectivity** | `awsSDK` (GameLiftServerSDK), `Restful` |
| **Players seeing each other** | engine replication + `WebSocket` (if the WS-MMO proxies are how remote players are shown — **confirm**) |
| **Voice chat** | `awsSDK` (EmbeddedVoiceChat), `EmbeddedVoiceChatPixelStreamingBridge`, possibly `WebSocket` |
| **Pixel Streaming** | engine `PixelStreaming`, `EmbeddedVoiceChatPixelStreamingBridge` |
| **Friend system / avatars (optional)** | `awsSDK` (GraphQL, S3, Transfer, CloudWatchLogs), `EnhancedScrollBox`, `Clipboard`, `QREncode` |
| **Nothing** | `RuntimeDataTable`, `UBIKSolver` |

## Recommended removal order

1. `UBIKSolver-main` — zero references, zero risk once the AnimGraph check passes.
2. `RuntimeDataTable_5.4` — zero references; confirm no Google-Sheets catalog plan.
3. The `LoadingScreen` module entry inside `awsSDK.uplugin` — dead, and it drags `MoviePlayer` in.
4. `icmppingd30c78843dcfV2` — **after** replacing the latency logic (`ARCH-02`).
5. `Clipboar9fd0aa5b4ecdV2` — **after** moving copy to the browser (`LNX-07`).
6. Unused `awsSDK` service modules (`Lambda`, `DynamoDB`, `DynamoDBStreams`, `S3Control`,
   `S3Encryption`, `S3Outposts`) — biggest disk win, needs a full Client **and** Server cook to verify.
7. Everything else only if the friend/MFA/avatar feature set is cut.

**After every removal:** re-cook and diff `LinuxClient/Manifest_UFSFiles_Linux.txt` and
`LinuxServer/Manifest_UFSFiles_Linux.txt` against the previous run.
