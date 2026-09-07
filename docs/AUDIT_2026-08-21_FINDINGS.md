# awsTutorial / MaxiMall — Full Audit Findings

> **Audit date:** 2026-08-21
> **Scope:** entire repository — `Source/`, all 12 plugins, `Config/`, `Content/` (asset-reference
> level), `awsTemplate*.yaml`, and the two staged Linux builds under `LinuxClient/` / `LinuxServer/`.
> **Method:** static reading of every non-generated C++/C#/INI file, asset-reference scanning of all
> 2 269 `.uasset`/`.umap` files, and cross-checking against the **actual staged build manifests**
> produced by the last Linux Shipping cook (this is how several findings are marked CONFIRMED
> rather than PLAUSIBLE).
>
> **No production code was modified.** Only documentation files were created.

Read `docs/PROJECT_ARCHITECTURE.md` first — it explains the system this document critiques.

---

## Table of contents

1. [Severity legend and classification legend](#1-legends)
2. [Findings — Packaging & configuration](#2-packaging--configuration-pkg)
3. [Findings — Architecture mismatches](#3-architecture-mismatches-arch)
4. [Findings — Voice chat](#4-voice-chat-voice)
5. [Findings — Pixel Streaming](#5-pixel-streaming-ps)
6. [Findings — Multiplayer / replication / session](#6-multiplayer--replication--session-net)
7. [Findings — Linux / Dedicated-Server correctness & performance](#7-linux--dedicated-server-lnx)
8. [Findings — Security](#8-security-sec)
9. [Findings — Correctness bugs in project C++](#9-correctness-bugs-in-project-c-bug)
10. [Findings — Third-party plugin bugs](#10-third-party-plugin-bugs-plug)
11. [Cleanup candidates](#11-cleanup-candidates)
12. [Full remediation plan](#12-full-remediation-plan)
13. [External / manual checks required](#13-external--manual-checks-required)

---

## 1. Legends

**Severity**

| Level | Meaning |
|---|---|
| **S1 Critical** | Breaks a required production feature, or crashes / corrupts data |
| **S2 High** | Wrong behaviour or serious waste in production; user-visible |
| **S3 Medium** | Latent bug, or waste that does not yet hurt |
| **S4 Low** | Hygiene, maintainability, cosmetic |

**Confidence**

* **CONFIRMED** — proven from source plus a second artefact (build manifest, missing file, sibling implementation).
* **PLAUSIBLE** — proven from source reading alone; still needs one runtime check.

**Classification** (as requested)

`REQUIRED AND CORRECT` · `REQUIRED BUT NEEDS FIX` · `REQUIRED BUT SHOULD BE SIMPLIFIED` ·
`CORRECT CODE BUT NOT NEEDED FOR OUR ARCHITECTURE` · `UNUSED` · `LEGACY / OLD` · `DUPLICATED` ·
`POSSIBLY REMOVABLE — NEEDS VERIFICATION` · `SAFE TO REMOVE` · `PLATFORM-SPECIFIC` ·
`EXTERNAL DEPENDENCY` · `UNKNOWN — NEEDS MANUAL CONFIRMATION`

---

## 2. Packaging & configuration (PKG)

### PKG-01 — Colour catalogs are never staged; the RAL/NCS picker is empty in production
* **Severity:** S1 · **Confidence:** CONFIRMED · **Class:** REQUIRED BUT NEEDS FIX
* **Files:**
  * `Config/DefaultEngine.ini` (last two lines)
  * `Source/awsTutorial/ColorCatalog/ColorCatalogSubsystem.cpp` → `LoadColorJsonFile()`
  * `Content/Data/Colors/ral_classic.json`, `Content/Data/Colors/ncscolorguide_2052_ui_sorted.json`
* **What is wrong.** `DefaultEngine.ini` ends with:
  ```ini
  [/Script/UnrealEd.ProjectPackagingSettings]
  +DirectoriesToAlwaysStageAsUFS=(Path="Data/Colors")
  +DirectoriesToAlwaysStageAsNonUFS=(Path="Data/Colors"
  ```
  Two separate defects in three lines:
  1. `UProjectPackagingSettings` is `UCLASS(config=Game)`. A `[/Script/UnrealEd.ProjectPackagingSettings]`
     section placed in **DefaultEngine.ini is read from the wrong ini and silently ignored.** The
     equivalent entries for `Certificates` live in `DefaultGame.ini` and **do** work.
  2. The last line is **truncated** — the closing `)` is missing and there is no trailing newline.
* **Why it is wrong / proof.** The staged Linux client manifests were checked directly:
  ```
  grep -i "ral_classic|ncscolorguide|Data/Colors" LinuxClient/Manifest_UFSFiles_Linux.txt
                                                  LinuxClient/Manifest_NonUFSFiles_Linux.txt
  -> no matches
  find LinuxClient -iname "*.json" -path "*olor*"   -> no matches
  ```
  By contrast `awsTutorial/Content/Certificates/cacert.pem` *is* in `Manifest_NonUFSFiles_Linux.txt`,
  because its directive is in `DefaultGame.ini`.
* **Affects production?** Yes, today. `UColorCatalogSubsystem::Initialize` logs
  `Failed to locate 'ral_classic.json' in any known content or staging paths` and both
  `RALColors` and `NCSColors` stay empty.
* **Reproduce:** run the packaged Linux client, open the colour catalog UI → zero swatches.
* **Fix:** move both directives into `Config/DefaultGame.ini` under
  `[/Script/UnrealEd.ProjectPackagingSettings]`, repair the missing `)`, add a trailing newline,
  and mirror the `Certificates` pattern (both UFS and NonUFS).
* **Files to change:** `Config/DefaultGame.ini`, `Config/DefaultEngine.ini`.
* **Risk of fix:** very low. Adds ~1 MB to the stage.
* **How to test:** re-stage, then
  `grep -i colors LinuxClient/Manifest_NonUFSFiles_Linux.txt` must return two rows; launch the
  client and confirm `[ColorCatalogSubsystem] SUCCESS: Loaded color catalog from:` in the log.

### PKG-02 — `M_OpeningSelection` is referenced only by a runtime string, so it is never cooked
* **Severity:** S2 · **Confidence:** CONFIRMED · **Class:** REQUIRED BUT NEEDS FIX
* **Files:** `Source/awsTutorial/Constructor/ProceduralWallActor.cpp:288-291`
* **What is wrong.**
  ```cpp
  UMaterialInterface* OpeningMat = LoadObject<UMaterialInterface>(nullptr,
      TEXT("/Game/RoomPlanner/Materials/M_OpeningSelection.M_OpeningSelection"));
  if (!OpeningMat)
      OpeningMat = LoadObject<UMaterialInterface>(nullptr,
          TEXT("/Game/Constructor/Materials/M_OpeningSelection.M_OpeningSelection"));
  ```
  * `DefaultGame.ini` sets `bCookAll=False`, so only *referenced* assets are cooked.
  * A scan of all 2 269 assets shows **`M_OpeningSelection` is referenced by nothing except itself.**
  * `/Game/Constructor/Materials/` **does not exist** — the fallback path is dead.
* **Affects production?** Yes — in the packaged client the selected door/window highlight silently
  falls back to `WallProceduralMesh->GetMaterial(0)`, i.e. no visible highlight.
* **Fix:** add a `TSoftObjectPtr<UMaterialInterface>` `UPROPERTY(EditDefaultsOnly)` on
  `AProceduralWallActor` (or a `ConstructorHelpers::FObjectFinder`) so the cooker sees the
  reference; delete the dead `/Game/Constructor/...` fallback.
* **Risk of fix:** low. **Test:** confirm `M_OpeningSelection` appears in
  `LinuxClient/Manifest_UFSFiles_Linux.txt` after a re-cook.

### PKG-03 — Pixel Streaming is compiled into and staged with the dedicated server
* **Severity:** S3 · **Confidence:** CONFIRMED · **Class:** CORRECT CODE BUT NOT NEEDED FOR OUR ARCHITECTURE
* **Files:** `Source/awsTutorial/awsTutorial.Build.cs`
* **Proof:** `grep -ci pixelstreaming LinuxServer/Manifest_UFSFiles_Linux.txt` → **12**, including
  `Engine/Plugins/Media/PixelStreaming/PixelStreaming.uplugin` and `Config/DefaultPixelStreaming.ini`.
* **Why it is wrong.** A GameLift dedicated server never streams. It links WebRTC, AVEncoder and the
  PS input protocol for nothing, and `AAwsTutorial_PlayerController`'s constructor creates a
  `UPixelStreamingInput` default subobject for *every* connected player on the server.
* **Fix:** wrap the PS dependencies in `if (Target.Type != TargetType.Server)` and guard the PS code
  paths with a `WITH_PIXELSTREAMING`-style define, or move the PS code into the login/local
  controller only. Also add `"TargetDenyList": ["Server"]` to the bridge plugin descriptor.
* **Risk of fix:** medium — the PS includes are spread through `awsTutorial_PlayerController.cpp`
  and `RoomPlannerManager.cpp`. Do it as a dedicated task, not as a drive-by.

### PKG-04 — `EmbeddedVoiceChatPixelStreamingPlugin.uplugin` contains invalid JSON
* **Severity:** S3 · **Confidence:** CONFIRMED · **Class:** REQUIRED BUT NEEDS FIX
* **File:** `Plugins/EmbeddedVoiceChatPixelStreamingPlugin_UE5.6/EmbeddedVoiceChatPixelStreamingPlugin.uplugin`
* **What is wrong:** trailing comma in the array —
  ```json
  "PlatformAllowList": [ "Win64", "Linux", "LinuxArm64", "Mac", ]
  ```
* **Current impact:** none observed — the plugin *is* present in both staged manifests, so UE's
  lenient JSON reader accepts it today. It is a latent trap for any stricter tool
  (CI validators, `jq`, custom build scripts).
* **Fix:** delete the trailing comma. **Risk:** none.

### PKG-05 — `Certificates` is staged twice (UFS **and** NonUFS)
* **Severity:** S4 · **Confidence:** CONFIRMED · **Class:** REQUIRED AND CORRECT (intentional)
* **File:** `Config/DefaultGame.ini`
* Duplicating `cacert.pem` costs ~200 KB and is the documented workaround for libcurl needing a
  real on-disk CA bundle. **Keep it.** Mirror the same pattern for `Data/Colors` (PKG-01).

### PKG-06 — Legacy FPS-demo maps are cooked into both client and server
* **Severity:** S3 · **Confidence:** CONFIRMED · **Class:** LEGACY / OLD
* All 8 maps (`MatchingRoomCombatMap_*`, `LightMap_P00`, `WaitingRoomLobbyMap_P0*`) are staged into
  **both** Client and Server. `MatchingRoomCombatMap_*` is combat content from the original template,
  streamed by `BP_FirstPersonCharacter`. See the Cleanup section.

### PKG-08 — `HDRIBackdrop` plugin content is cooked into the client but nothing references it
* **Severity:** S4 · **Confidence:** CONFIRMED · **Class:** POSSIBLY REMOVABLE — NEEDS VERIFICATION
* `/Script/HDRIBackdrop` appears in **0** of 2 269 project assets, yet 18 rows of
  `Engine/Plugins/Runtime/HDRIBackdrop/Content/**` are in `LinuxClient/Manifest_UFSFiles_Linux.txt`
  (enabled plugins get their content staged regardless of references).
* **Fix:** confirm no level places an `HDRIBackdrop` actor, then set `"Enabled": false` in
  `awsTutorial.uproject`.

### PKG-09 — `GameplayAbilities` is enabled for one template asset
* **Severity:** S4 · **Confidence:** CONFIRMED · **Class:** POSSIBLY REMOVABLE — NEEDS VERIFICATION
* `/Script/GameplayAbilities` appears in exactly one asset:
  `Content/Characters/Heroes/Abilities/AN_Melee.uasset` — an animation notify from the template's
  hero content pack, unrelated to a furniture configurator.
* **Fix:** bundle this decision with the FPS-demo content cleanup (§11.3).

### PKG-10 — `DatasmithContent` is a genuine runtime dependency
* **Severity:** informational · **Confidence:** CONFIRMED · **Class:** REQUIRED AND CORRECT
* It would be easy to assume Datasmith is an editor-only import tool and disable it. **Do not.**
  `/Script/DatasmithContent` appears in **702** project assets — every imported mesh under
  `Content/AllModels/`, `Content/Models*` etc. carries `UDatasmithAssetUserData`. Disabling the plugin
  would break those assets on load.
* The two *importer* plugins (`DatasmithImporter`, `DatasmithFBXImporter`) are editor tools and could
  take `"TargetAllowList": ["Editor"]`, but the saving is only their descriptor and config files.

### PKG-07 — `IncludeOrderVersion = Unreal5_4` on a 5.6 engine
* **Severity:** S4 · **Class:** LEGACY / OLD · **Files:** all four `Source/*.Target.cs`
* Compatibility shim inherited from the 5.4 template. Harmless now, but it will block a future
  engine upgrade and silently hides missing includes.

---

## 3. Architecture mismatches (ARCH)

### ARCH-01 — The C++ module is a furniture configurator, not the multiplayer product
* **Severity:** informational (but the single most important fact in this audit)
* Of 14 767 lines in `Source/awsTutorial/`, roughly **60 lines** serve login / session / multiplayer:
  `GetRequestURL`, `GetRequestOptions`, `HasRequestOption`, `GetRequestOption`, `Kick`.
  Everything else is the MaxiMall showroom, room planner, colour catalog, save system and the
  Pixel Streaming bridge.
* **Do not delete the configurator** — it is the product. But be aware that
  `AAwsTutorial_PlayerController` is a **1825-line monolith** that is simultaneously
  (a) the GameLift session-validation surface the dedicated server depends on, and
  (b) the configurator's input/UI controller. Any refactor of (b) risks (a).

### ARCH-02 — Region-latency measurement is meaningless under Pixel Streaming
* **Severity:** S2 · **Confidence:** CONFIRMED (by architecture, not by a runtime test)
* **Files:** `Plugins/icmppingd30c78843dcfV2/*`, `Content/FirstPerson/Blueprints/BP_AwsTutorial_GI.uasset`
  (`SendIcmpRequest` → `BP_Latency_ST` → `StartGameSessionPlacement`)
* **What is wrong.** The Unreal client runs on an **AWS GPU instance**, not on the player's machine.
  `SendIcmpRequest` therefore measures *EC2 → GameLift region* latency. The player's own network path
  ends at the Pixel Streaming instance and is completely invisible to this measurement.
* **Second problem (Linux-specific).** `IcmpPosix.cpp:82` opens
  `socket(AF_INET, SOCK_DGRAM, IPPROTO_ICMP)`. On Linux this "unprivileged ICMP" socket only works if
  the process GID is inside `net.ipv4.ping_group_range`, which on stock Amazon Linux / Ubuntu is
  `1 0` (nobody). Expect **every ping to fail** with `InternalError` unless the instance is
  explicitly configured.
* **Reproduce:** run the Linux client on EC2 as a non-root user and watch every latency entry come
  back as an error / fallback value.
* **Proposed solution.** Stop measuring; place the game session in the **same region (ideally same AZ)
  as the GPU fleet**, which is a constant you already know. Pass a fixed
  `PlayerLatencies` array (or omit it and use `StartGameSessionPlacement` with a single-region queue).
* **Files to change:** `BP_AwsTutorial_GI`, `BP_Online_Offline_Selection_UI`; `Icmp` plugin then
  becomes removable.
* **Risk:** low–medium (Blueprint change on the room-join path — test both public and private rooms).

### ARCH-03 — The browser half of the Pixel Streaming protocol is not in this repository
* **Severity:** S2 · **Confidence:** CONFIRMED · **Class:** EXTERNAL DEPENDENCY
* `Samples/PixelStreaming/WebServers/**` is the **unmodified** Epic reference frontend — a grep for
  `MaxiMall`, `open_url`, `ClipboardPaste` across all of `Samples/` returns nothing.
* The C++ emits `MaxiMallCursor`, `MaxiMallClipboard`, `MaxiMallConstructor:`, `open_url:` and `DIAG:`
  and expects `MaxiMallPaste `, `{"Cmd":"ClipboardPaste"}` and the `add_wall`/`add_opening`/`clear`/
  `get_state` command family. **Whatever implements that contract must be version-locked with this
  repo, and it currently is not.**
* **Action:** document the protocol (done — see `PROJECT_ARCHITECTURE.md` §2.2) and either vendor the
  custom frontend into `Samples/` or pin its commit in a `README`.

### ARCH-04 — All AWS configuration is baked into a Blueprint
* **Severity:** S2 · **Confidence:** CONFIRMED · **Class:** REQUIRED BUT SHOULD BE SIMPLIFIED
* **File:** `Content/FirstPerson/Blueprints/BP_AwsTutorial_GI.uasset`
* Cognito user pool `ap-northeast-2_DQC2kURMX`, identity pool `ap-northeast-2:2ca85c1d-…`, four
  AppSync endpoints and the GameLift alias/fleet IDs are Blueprint **default values**. There is no
  dev/stage/prod switch and no way to change them without opening the editor and re-cooking.
* A stale GameLift **game-session ARN** from a past test run is also serialized into the asset.
* `BP_UserLogin_UI` still contains the template placeholder `us-east-1_123456789`.
* **Proposed solution:** add a `UDeveloperSettings`-derived config class (or a plain
  `Config/DefaultMaxiMall.ini` section) read at `GameInstance::Init`, with command-line overrides
  (`-CognitoUserPoolId=…`). Keep the Blueprint values only as editor defaults.
* **Risk:** medium (touches the login path) — but it is the single change that most improves
  operability.

### ARCH-05 — Room/session scope vs. configurator scope are not aligned
* **Severity:** S2 · **Confidence:** CONFIRMED · **Class:** REQUIRED BUT NEEDS FIX
* `USaveSystemWidget::HandleLoadSaveItem` collects **every** `AShowroomBooth` in the level and pushes
  each one through `AAwsTutorial_PlayerController::Server_LoadBoothState`, whose `_Validate` returns
  an unconditional `true`.
* On a shared dedicated server this means **any player pressing "Load" rewrites the whole room for
  everyone else**, with no ownership or permission check.
* Decide the intended semantics (per-player view vs. shared room state) before touching anything else
  in the save system.

### ARCH-06 — GameLift room state and configurator state live in two unrelated backends
* **Severity:** S3 · **Class:** UNKNOWN — NEEDS MANUAL CONFIRMATION
* Rooms/sessions live in AppSync + Lambda + GameLift (`awsTemplate_PrivateGame.yaml`).
* Configurator saves live in a separate Node service at `https://18-185-5-251.nip.io/api/saves`
  (hard-coded, unauthenticated).
* There is no shared identity or correlation between the two beyond the Cognito username string.
  Confirm this is intentional.

---

## 4. Voice chat (VOICE)

All findings are in `Plugins/EmbeddedVoiceChatPixelStreamingPlugin_UE5.6/` and
`Plugins/Ultimate969078bf2772V2/Source/EmbeddedVoiceChat/`. This is the **mandatory** path for
voice under Pixel Streaming: there is no microphone on a GPU instance, so the browser's mic must
arrive through the PS WebRTC audio sink.

### VOICE-01 — Loopback monitor plays the microphone **backwards** and grows without bound
* **Severity:** S1 · **Confidence:** CONFIRMED · **Class:** REQUIRED BUT NEEDS FIX
* **File:** `Plugins/EmbeddedVoiceChatPixelStreamingPlugin_UE5.6/Source/EmbeddedVoiceChatPixelStreamingBridge/Private/EmbeddedVoiceChatPixelStreamingAudioComponent.cpp`
* **Function:** `UEmbeddedVoiceChatPixelStreamingAudioComponent::OnGenerateAudio`
  ```cpp
  void AddAudio(const float* pcm, int n) { audioBuffer.Append(pcm, n); }   // appends at the END
  int32 OnGenerateAudio(float* Out, int32 N) {
      for (int i = 0; i < N; i++)
          Out[i] = audioBuffer.Num() > 0 ? audioBuffer.Pop() : 0.f;        // pops from the END
  }
  ```
* **What is wrong:** `TArray::Pop()` removes the **last** element. Producer appends at the tail,
  consumer drains from the tail, so playback is time-reversed. It is a LIFO used as a FIFO.
* **Second defect:** if the component is not playing (or the render callback is slower than the
  50–100 packets/s arriving from WebRTC) `audioBuffer` grows forever — an unbounded float array
  fed by every connected browser microphone.
* **Reproduce:** bind the capture to an audio component (`bindAudioComponent`) and speak; the monitor
  is unintelligible. Leave a session idle with mic on for minutes and watch RSS climb.
* **Fix:** replace with a ring buffer (`Audio::TCircularAudioBuffer<float>`) with a bounded capacity
  (e.g. 250 ms) and drop-oldest overflow policy.
* **Risk:** low, isolated file. **Test:** speak into the browser mic with the loopback component
  enabled; verify intelligibility and a flat memory graph.

### VOICE-02 — The resampler is never initialised, so browser audio is fed to the codec at the wrong rate
* **Severity:** S1 · **Confidence:** CONFIRMED (by comparison with the author's own sibling implementation)
* **File:** `.../Private/EmbeddedVoiceChatPixelStreamingAudioCapture.cpp` → `ConsumeRawPCM`
* `Audio::FResampler` requires `Init(EResamplingMethod, SampleRateRatio, NumChannels)` before
  `ProcessAudio`. The bridge constructs `new Audio::FResampler()` and **never calls `Init`**.
* The sibling class in the same product,
  `Plugins/Ultimate969078bf2772V2/Source/EmbeddedVoiceChat/Private/EmbeddedVoiceChatAudioCapture.cpp:169`,
  *does* call `Resampler->Init(...)` — so this is an omission in the newer bridge, not an API
  misunderstanding on my part.
* **Consequence:** when the browser delivers 48 kHz and `EmbeddedVoiceChat::sampleRate` is different,
  the "resampled" buffer is passed through with an unconfigured ratio → wrong pitch/speed, or
  garbage, on the receiving peer.
* **Fix:** call `Resampler->Init(Audio::EResamplingMethod::Linear,
  (float)EmbeddedVoiceChat::sampleRate / (float)InSampleRate, 1)` whenever `InSampleRate` changes,
  and cache the last rate.

### VOICE-03 — Integer division in the original module's resampler ratio
* **Severity:** S2 · **Confidence:** CONFIRMED · **Class:** REQUIRED BUT NEEDS FIX
* **File:** `Plugins/Ultimate969078bf2772V2/Source/EmbeddedVoiceChat/Private/EmbeddedVoiceChatAudioCapture.cpp:169`
  ```cpp
  Resampler->Init(Audio::EResamplingMethod::Linear,
                  EmbeddedVoiceChat::sampleRate / AudioCapture->GetSampleRate(), 1);
  ```
  Both operands are `int`. For the common down-sampling case (device 48 000 → target 16 000) the
  ratio evaluates to **0**. Cast one side to `float`.
* This path only runs when a *real* capture device is used, i.e. not on the GPU instance — but fix it
  anyway if desktop builds are ever used for testing.

### VOICE-04 — `Reset()` unsubscribes from the wrong delegates
* **Severity:** S2 · **Confidence:** CONFIRMED · **Class:** REQUIRED BUT NEEDS FIX
* **File:** `.../EmbeddedVoiceChatPixelStreamingAudioCapture.cpp`
* `init()` subscribes to `OnDataChannelOpenNative` / `OnDataChannelClosedNative`.
  `Reset()` removes the same handles from `OnNewConnectionNative` / `OnClosedConnectionNative`.
* Both `Remove` calls are no-ops; the real subscriptions survive. Because they were added with
  `AddUObject` they become stale weak bindings rather than dangling calls, but the entries accumulate
  across level travel and re-init.
* **Fix:** remove from the same two delegates that `init()` added to, and clear the handles.

### VOICE-05 — Null-pointer dereference inside the null check
* **Severity:** S2 · **Confidence:** CONFIRMED · **Class:** REQUIRED BUT NEEDS FIX
* **File:** `.../EmbeddedVoiceChatPixelStreamingAudioCapture.cpp`, inside `init()`'s `ForEachStreamer` lambda
  ```cpp
  if (!Streamer) {
      UE_LOG(..., TEXT("Streamer is not valid: %s"), *Streamer->GetId());   // <-- deref of null
      return;
  }
  ```
* If `ForEachStreamer` ever yields an invalid `TSharedPtr`, this crashes the client immediately.
* **Fix:** drop `*Streamer->GetId()` from that log line.

### VOICE-06 — Per-audio-packet logging and formatting in the hot path
* **Severity:** S2 · **Confidence:** CONFIRMED · **Class:** REQUIRED BUT NEEDS FIX
* **File:** `.../EmbeddedVoiceChatPixelStreamingAudioCapture.cpp` → first line of `ConsumeRawPCM`
  ```cpp
  LOG_EMBEDDEDVOICECHATPIXELSTREAMINGBRIDGE_NORMAL(
      FString::Printf(TEXT("captured %d frames in %d sample rate"), NFrames, InSampleRate));
  ```
* Three separate problems:
  1. The category is declared `DEFINE_LOG_CATEGORY_STATIC(LogCategory, All, All)` — compile-time
     verbosity `All`, so this is **not** stripped in Shipping.
  2. It runs on **every** WebRTC audio packet (typically 100/s per connected player) and builds
     three temporary `FString`s (`CURRENT_CLASS`, `CURRENT_FUNCTION`, the `Printf`) each time.
  3. `NFrames` is `size_t` (8 bytes) formatted with `%d` — a **varargs type mismatch**; the second
     `%d` reads adjacent stack garbage.
* **Consequence on AWS:** continuous log spam and measurable CPU burn on the audio thread; the log
  file grows unbounded on a long-lived GPU instance.
* **Fix:** delete the log (or demote to `VeryVerbose` under `UE_LOG(..., VeryVerbose, ...)` and change
  the category's compile-time verbosity), and use `%llu` with an explicit cast if kept.

### VOICE-07 — `malloc`/`free` per audio callback
* **Severity:** S3 · **Confidence:** CONFIRMED · **Class:** REQUIRED BUT SHOULD BE SIMPLIFIED
* Same file, resample branch: `malloc(MaxOutputFrames * sizeof(float))` … `free(resampled)` on every
  packet. Use a reusable `TArray<float>` member like `MonoBuffer` already is.

### VOICE-08 — Debug WAV writer is half-enabled by mismatched macro names
* **Severity:** S3 (privacy-relevant) · **Confidence:** CONFIRMED · **Class:** LEGACY / OLD
* `EmbeddedVoiceChatPixelStreamingBridge.Build.cs` defines `PixelStreaming_Allow_Voice_Debug=1`.
* The constructor and `BeginDestroy` guard the writer with `#if Allow_Voice_Debug` (**undefined**),
  so `writer` is never allocated. `ConsumeRawPCM` guards with `#if PixelStreaming_Allow_Voice_Debug`
  (**=1**), so it takes a `FScopeLock` on every packet for a writer that is always null.
* If anyone "fixes" the macro name, the plugin will start writing **every user's microphone audio to
  `Saved/`** on a shared GPU instance.
* **Fix:** set the define to `0` for Shipping (or remove it), and make both guards use the same name.

### VOICE-09 — `BeginDestroy` calls module APIs after `Super::BeginDestroy()`
* **Severity:** S3 · **Confidence:** PLAUSIBLE · **Class:** REQUIRED BUT NEEDS FIX
* `UEmbeddedVoiceChatPixelStreamingAudioCapture::BeginDestroy` calls `Super::BeginDestroy()` and
  *then* `Reset()`, which calls `IPixelStreamingModule::Get()`. During engine shutdown / GC after
  module unload this can access a destroyed module singleton.
* **Fix:** do all cleanup **before** `Super::BeginDestroy()`, and guard with
  `FModuleManager::Get().IsModuleLoaded("PixelStreaming")`.
* The same pattern exists in `GraphQL`'s `USubscriptionHandler::BeginDestroy` and
  `UCustomProtocolSubscriptionHandler::BeginDestroy`.

### VOICE-10 — `TWeakObjectPtr` validity checked inconsistently
* **Severity:** S4 · **Confidence:** CONFIRMED
* Same file: the resampled branch uses `if (this->audioComponent != nullptr)` while the non-resampled
  branch uses `if (this->audioComponent.IsValid())`. Use `IsValid()` in both.

### VOICE-11 — Voice media needs UDP + STUN/TURN between EC2 instances
* **Severity:** S2 · **Class:** EXTERNAL DEPENDENCY · **Confidence:** CONFIRMED (from
  `UEmbeddedVoiceChatGroup::setIceServers` and `UEmbeddedVoiceChatConnection::connect(iceServers, portRangeBegin, portRangeEnd)`)
* Because the Unreal clients run on EC2, the WebRTC voice mesh is **EC2 ↔ EC2**. Security groups must
  permit the configured UDP port range between GPU instances, and a TURN server is required if they
  sit behind restrictive NAT/SG rules. Verify this before blaming the code.
* `iceServers` / `portRangeBegin` / `portRangeEnd` are **`static` members of `UEmbeddedVoiceChatGroup`**
  set through the Blueprint-callable static `setIceServers`. If that call is missed (or made after the
  first `joinGroup`), every peer connection uses an empty ICE-server list and will fail on any NAT.

### VOICE-12 — Voice signalling depends on the dedicated server
* **Severity:** S2 · **Confidence:** CONFIRMED · **Class:** REQUIRED AND CORRECT (document, do not "fix")
* **File:** `Plugins/Ultimate969078bf2772V2/Source/EmbeddedVoiceChat/{Public,Private}/EmbeddedVoiceChatRPCGroup.*`
* `UEmbeddedVoiceChatRPCGroup` is a **replicated `UActorComponent`** whose SDP/ICE exchange uses plain
  Unreal RPCs: `gotLocalDescription` / `gotLocalCandidate` are `UFUNCTION(Server, Reliable)` and
  `gotRemoteDescription` / `gotRemoteCandidate` / `newPlayerJoin` / `oldPlayerLeave` are
  `UFUNCTION(Client, Reliable)`. The relay body
  (`gotLocalDescription_Implementation` -> `group->gotRemoteDescription(...)`) sits **outside** the
  `#if WITH_EMBEDDEDVOICECHAT` block, so it still compiles and runs on the Server target even though
  the codec is compiled out there.
* **Implication:** *"players in the same room can hear each other"* requires that they are on the
  **same dedicated server session**. Voice cannot work between two players who are not both connected
  to the same GameLift game session.
* An alternative `UEmbeddedVoiceChatCustomGroup` exists whose `sendLocalDescription` /
  `sendLocalCandidate` are `BlueprintNativeEvent`s, letting signalling be routed over the
  `WebSocket` or `GraphQL` plugin instead. `BP_FirstPersonCharacter` contains a variable named
  `EmbeddedVoiceChatWSGroup` **and** an `EmbeddedVoiceChatRPCGroup`, so **both** mechanisms may be
  wired up. **Confirm in the editor which one is actually used** — running both would create
  duplicate peer connections.

### VOICE-13 — Manual `AddToRoot` / `RemoveFromRoot` GC management on voice objects
* **Severity:** S3 · **Confidence:** PLAUSIBLE · **Class:** REQUIRED BUT NEEDS FIX
* `EmbeddedVoiceChatRPCGroup.cpp` roots `channel`, `connection` and `connectionHandler` with
  `AddToRoot()` and un-roots them on `oldPlayerLeave` / `EndPlay`. Two `AddToRoot` calls on
  `channelHandler` (lines 153, 378) are **commented out** while the matching `RemoveFromRoot` calls
  (lines 176, 319) remain — those are `IsRooted()`-guarded so they are safe, but the asymmetry means
  the ownership model is not obvious.
* Any path that destroys the component without reaching `EndPlay` (level travel, abrupt disconnect)
  leaks rooted `UObject`s that GC can never collect.
* **Test:** loop lobby -> session -> lobby 20 times with voice active and watch the UObject count
  (`obj list class=EmbeddedVoiceChatConnection`).

---

## 5. Pixel Streaming (PS)

### PS-01 — Dead "late-bind retry" logic; the comment contradicts the code
* **Severity:** S3 · **Confidence:** CONFIRMED · **Class:** LEGACY / OLD
* **File:** `Source/awsTutorial/awsTutorial_PlayerController.cpp`
* The constructor does:
  ```cpp
  PixelStreamingInput = CreateDefaultSubobject<UPixelStreamingInput>(TEXT("PixelStreamingInputComponent"));
  ```
  but `BeginPlay` is commented **"FIX 2 APPLIED: No CreateDefaultSubobject (removed from ctor)"** and
  then guards `if (!PixelStreamingInput) { PixelStreamingInput = GetComponentByClass<...>(); }`.
  Because the default subobject always exists, that branch — and the entire `TObjectIterator` retry
  block in `PlayerTick` — is **unreachable**.
* **Consequence:** ~40 lines of misleading dead code plus a stale comment that will send the next
  engineer down the wrong path.
* **Fix:** delete the unreachable branches and correct the comment.

### PS-02 — `ARoomPlannerManager::Tick` scans the whole UObject table every frame, forever
* **Severity:** S1 (on the dedicated server) · **Confidence:** CONFIRMED · **Class:** REQUIRED BUT NEEDS FIX
* **File:** `Source/awsTutorial/Constructor/RoomPlannerManager.cpp:88-99`
  ```cpp
  void ARoomPlannerManager::Tick(float DeltaTime) {
      ...
      if (!BoundPSInput.IsValid())
          for (TObjectIterator<UPixelStreamingInput> It; It; ++It) { ... break; }
  }
  ```
* On a **dedicated server** no `UPixelStreamingInput` component is ever registered on a *local*
  controller, so `BoundPSInput` stays invalid **permanently** and the manager performs a full
  `TObjectIterator` sweep (`GUObjectArray`, hundreds of thousands of entries) **every tick, forever**,
  once any client draws a wall.
* On the client it also binds to *whatever* `UPixelStreamingInput` it finds first, including one owned
  by another actor.
* **Reproduce:** start the dedicated server, have one client commit a wall (`Server_CommitWall`
  spawns the manager), then profile `stat game` — Tick cost climbs and never recovers.
* **Fix:** (a) do not tick the manager at all on the server: `SetActorTickEnabled(GetNetMode() != NM_DedicatedServer)`;
  (b) on the client, obtain the component from `GetWorld()->GetFirstPlayerController()` and stop
  retrying after a bounded number of attempts.
* **Risk:** low. **Test:** `stat unit` / `stat game` on the server before and after.

### PS-03 — Two independent Pixel Streaming input handlers process the same messages
* **Severity:** S3 · **Confidence:** CONFIRMED · **Class:** DUPLICATED
* `AAwsTutorial_PlayerController::OnPixelStreamingInput` and
  `ARoomPlannerManager::OnPixelStreamingInputReceived` are **near-identical**: both strip the
  `UIInteraction` prefix, both unwrap `descriptor`/`Descriptor`, both match the same
  `add_wall`/`add_opening`/`clear`/`get_state` command set, and both call
  `ProcessCommandJSON` + reply with `MaxiMallConstructor:`.
* If both are bound (the manager binds via `TObjectIterator`, the controller via its own component),
  **every constructor command is executed twice** and two replies are sent.
* Worse: on a client the manager's copy mutates *client-local* planner state that is not replicated,
  desynchronising it from `ReplicatedRoomJSON`.
* **Fix:** keep exactly one handler — the player controller's — and delete
  `ARoomPlannerManager::OnPixelStreamingInputReceived` plus the `BoundPSInput` machinery.

### PS-04 — `TObjectIterator` can bind to another player's PS input component
* **Severity:** S3 · **Confidence:** PLAUSIBLE · **Class:** REQUIRED BUT NEEDS FIX
* Same code as PS-02/PS-03. In any configuration with more than one local `UPixelStreamingInput`
  (multiple streamers, editor PIE with 2 players) the manager binds to an arbitrary one.

### PS-05 — Clipboard polling at 5 Hz on a headless Linux client
* **Severity:** S3 · **Confidence:** PLAUSIBLE · **Class:** POSSIBLY REMOVABLE — NEEDS VERIFICATION
* **File:** `awsTutorial_PlayerController.cpp` → `PlayerTick`
  ```cpp
  ClipboardCheckTimer += DeltaTime;
  if (ClipboardCheckTimer >= 0.2f) { FPlatformApplicationMisc::ClipboardPaste(CurrentClipboard); ... }
  ```
* On Linux `FPlatformApplicationMisc::ClipboardPaste` goes through SDL. A Pixel Streaming client runs
  with `-RenderOffscreen` and no X server, so SDL's clipboard is at best a no-op and at worst logs an
  error five times a second. The code *already* acknowledges this — the `ClipboardPaste` command
  handler bypasses the OS clipboard entirely and injects Slate character events.
* **Fix:** guard the poller with `#if PLATFORM_WINDOWS` or a console variable, or remove it and drive
  `MaxiMallClipboard` explicitly from the copy actions that need it.
* **Verify first:** confirm with the browser team that nothing depends on the `MaxiMallClipboard`
  message before removing it.

### PS-06 — `SendDiag_PC` diagnostics ship to production
* **Severity:** S4 · **Class:** LEGACY / OLD
* `DIAG: [PC] BeginPlay OK | FIX1: shader-pipeline Fast-batch removed …` is sent down the data channel
  and logged as `UE_LOG(LogTemp, Warning, …)` in Shipping. The text refers to fixes that are no longer
  identifiable. Remove or gate behind a cvar.

### PS-07 — Screenshot capture on the Pixel Streaming render path
* **Severity:** S2 · **Confidence:** PLAUSIBLE · **Class:** REQUIRED BUT NEEDS FIX
* **File:** `Source/awsTutorial/FurnitureConfigurator/UI/SaveSystemWidget.cpp` → `OnSaveClicked`
  ```cpp
  ViewportClient->OnScreenshotCaptured().AddUObject(this, &USaveSystemWidget::OnScreenshotCapturedHandler);
  FScreenshotRequest::RequestScreenshot(false);
  ```
* `FScreenshotRequest` forces a full-resolution GPU→CPU readback and, by default, also writes a PNG to
  `Saved/Screenshots/`. On a GPU instance encoding a live WebRTC stream this produces a visible hitch
  and slowly fills the disk.
* Additionally, the delegate is added on **every** Save click; two rapid clicks stack two bindings and
  `PendingSaveId`/`PendingSaveName` are overwritten, so the first save gets the second save's thumbnail.
* **Fix:** use `FViewport::ReadPixels` on a scaled render target, or add the delegate once in
  `NativeConstruct`; guard with a `bWaitingForScreenshot` flag (the member already exists but is never
  used).

---

## 6. Multiplayer / replication / session (NET)

### NET-01 — Almost every `Server_*` RPC has a stub `_Validate` that returns `true`
* **Severity:** S2 · **Confidence:** CONFIRMED · **Class:** REQUIRED BUT NEEDS FIX
* **Files:** `awsTutorial_PlayerController.cpp` (15 `_Validate` bodies), `ShowroomBooth.cpp` (4)
* **19 `_Validate` implementations exist; 17 are `{ return true; }`.** Only two do real work, both in
  `AShowroomBooth`:
  * `Server_ApplyProductChange_Validate` → `return IsValidProductID(NewProductID);`
  * `Server_ToggleDoor_Validate` → checks `DoorStates.IsValidIndex(SlotIndex)`

  Those two are the pattern to copy. The other 17 accept indices, lengths, wall thickness, opening
  dimensions and whole `FShowroomBoothConfigState` structs unchecked from clients.
* **Concrete abuse:** `Server_CommitWall(StartPos, EndPos, Thickness, Height)` accepts arbitrary
  floats. `NaN`/`inf` or a 10^9 cm wall reaches `AProceduralWallActor::RebuildWallMesh` and the
  procedural-mesh cooker on the **server**, affecting every player in the room.
  `Server_AddDoor/AddWindow` accept negative widths.
* **Fix:** implement real validation — clamp coordinates to a world bound, reject non-finite floats
  (`FMath::IsFinite`), clamp thickness/height/width to sane ranges, bound `SegmentID`/`OpeningIndex`
  to existing entries.
* **Risk of fix:** low. **Test:** a client harness that sends out-of-range values must be kicked or
  ignored, and legitimate values must still work.

### NET-02 — `Server_LoadBoothState` lets any client overwrite any booth
* **Severity:** S2 · **Confidence:** CONFIRMED · **Class:** REQUIRED BUT NEEDS FIX
* **File:** `awsTutorial_PlayerController.cpp` → `Server_LoadBoothState_Implementation`
  ```cpp
  TargetBooth->ActiveState = State;      // no validation, no ownership check
  TargetBooth->RebuildBoothVisuals();
  ```
* Combined with ARCH-05, one player's "Load" replaces the configuration of every booth in the shared
  room. Also, `ProductID` is an unchecked `FName` — a nonexistent product leaves the booth in a
  broken state.
* **Fix:** validate `ProductID` against the catalog, clamp all indices, and add an ownership/permission
  rule that matches the intended room semantics.

### NET-03 — Client-side mutation of a replicated actor's `bHidden`
* **Severity:** S3 · **Confidence:** PLAUSIBLE · **Class:** REQUIRED BUT NEEDS FIX
* **File:** `FurnitureConfigurator/Preview/FurniturePreviewActor.cpp` (≈ line 305) —
  `Booth->SetActorHiddenInGame(true)` on a **replicated** `AShowroomBooth` from a client.
* `AActor::bHidden` is a replicated property. Setting it locally works until the server next
  replicates its own value, at which point the booth can pop back or stay hidden inconsistently
  between clients.
* **Fix:** hide it per-player instead — add the booth to the local controller's `HiddenActors`, or
  toggle `SetActorHiddenInGame` on a client-only proxy.

### NET-04 — Duplicate `OnProductChanged` binding
* **Severity:** S3 · **Confidence:** CONFIRMED · **Class:** REQUIRED BUT NEEDS FIX
* **File:** `awsTutorial_PlayerController.cpp:755` uses plain `AddDynamic`, while lines 840 and 1263
  use `AddUniqueDynamic` on the **same** delegate/handler pair.
* `OpenFurniturePreview` calls `CloseFurniturePreview()` (which does `RemoveAll` then
  `AddUniqueDynamic`) and then adds a second binding at line 755. Steady state: **two bindings**, so
  `OnTargetBoothProductChanged` fires twice per product change and `LoadProductPreview` runs twice.
* **Fix:** change line 755 to `AddUniqueDynamic`.

### NET-05 — Door animation timer runs on the dedicated server for nothing
* **Severity:** S3 · **Confidence:** CONFIRMED · **Class:** REQUIRED BUT SHOULD BE SIMPLIFIED
* **File:** `FurnitureConfigurator/ShowroomBooth.cpp:1867-1869`
  ```cpp
  GetWorld()->GetTimerManager().SetTimer(DoorAnimationTimerHandle, this,
      &AShowroomBooth::UpdateDoorAnimation, 0.016f, true);
  ```
* Component relative rotations are **not** replicated — each client animates locally from the
  `DoorStates` RepNotify. The server's 60 Hz interpolation of invisible meshes is pure waste, and it
  scales with the number of booths in the level.
* **Fix:** `if (GetNetMode() == NM_DedicatedServer) return;` at the top of `UpdateDoorAnimation`, or
  do not start the timer on the server at all.

### NET-06 — `ARoomPlannerManager` replicates the entire layout as one `FString`
* **Severity:** S3 · **Confidence:** CONFIRMED · **Class:** REQUIRED BUT SHOULD BE SIMPLIFIED
* `ReplicatedRoomJSON` is re-serialised in full and re-sent after **every** wall/opening edit, and
  `OnRep_ReplicatedRoomJSON` calls `ImportLayoutFromJSON`, which tears down and rebuilds every
  `AProceduralWallActor` and the floor/ceiling/baseboard procedural meshes.
* For a 30-wall room that is a multi-kilobyte reliable property plus a full mesh rebuild per edit —
  visible hitching for every client while one player drags a wall.
* **Fix:** replicate a `TArray<FWallSegment>` with a `FFastArraySerializer`, or at minimum add a
  dirty-region rebuild. Not urgent unless the planner is in the shipping feature set.

### NET-07 — `ARoomPlannerManager::GetOrCreateInstance` is called from `PlayerTick`
* **Severity:** S3 · **Confidence:** CONFIRMED · **Class:** REQUIRED BUT SHOULD BE SIMPLIFIED
* Every frame the local controller runs a `TActorIterator<ARoomPlannerManager>` over all actors.
  Cache the result in a `TWeakObjectPtr`.

### NET-08 — Server RPCs are fired from `PlayerTick` on mouse-drag boundaries
* **Severity:** S4 · **Confidence:** CONFIRMED
* `Server_UpdateOpeningPosition` is sent on every LMB release during planner drags, and
  `Server_CommitWall` on every wall completion, with no rate limiting. Add a client-side cooldown if
  the planner ships.

### NET-09 — `Kick()` is a client-callable Server RPC with no authority check
* **Severity:** S4 · **Confidence:** CONFIRMED · **Class:** REQUIRED AND CORRECT (by design)
* `UFUNCTION(BlueprintCallable, Server, Reliable) void Kick()` closes **the caller's own**
  `UNetConnection` (`GetNetConnection()` of the invoking controller), so a client can only disconnect
  itself. Documented here so nobody "fixes" it into something dangerous.

---

## 7. Linux / Dedicated-Server (LNX)

### LNX-01 — Hard-coded developer absolute paths in the colour loader
* **Severity:** S3 · **Confidence:** CONFIRMED · **Class:** LEGACY / OLD
* **File:** `ColorCatalog/ColorCatalogSubsystem.cpp` → `LoadColorJsonFile`
  ```cpp
  SearchPaths.Add(TEXT("/home/ssm-user/client/awsTutorial/Content/Data/Colors/") + RelFileName);
  SearchPaths.Add(TEXT("/home/ubuntu/client/awsTutorial/Content/Data/Colors/")  + RelFileName);
  SearchPaths.Add(TEXT("/local/game/awsTutorial/Content/Data/Colors/")          + RelFileName);
  ```
* Thirteen fallback paths including three machine-specific absolutes. This is a symptom of PKG-01 —
  someone worked around the staging bug by hard-coding the deploy directory instead of fixing the ini.
* **Fix:** after PKG-01, reduce to a single `FPaths::ProjectContentDir() / TEXT("Data/Colors") / File`.

### LNX-02 — `UColorCatalogSubsystem` initialises on the dedicated server
* **Severity:** S3 · **Confidence:** CONFIRMED · **Class:** CORRECT CODE BUT NOT NEEDED FOR OUR ARCHITECTURE
* It is a `UGameInstanceSubsystem` with no `ShouldCreateSubsystem` override, so the dedicated server
  parses two colour JSON files (2 052 NCS entries) at boot and holds them in memory, for a UI it never
  renders.
* **Fix:** override `ShouldCreateSubsystem` to return `false` when
  `Cast<UGameInstance>(Outer)->IsDedicatedServerInstance()`.

### LNX-03 — Unchecked downcast of the log output device
* **Severity:** S2 · **Confidence:** PLAUSIBLE · **Class:** REQUIRED BUT NEEDS FIX
* **File:** `Plugins/Ultimate969078bf2772V2/Source/GameLiftServerSDK/Private/ServerHelper.cpp`
  ```cpp
  FOutputDevice* OutputDevice = FGenericPlatformOutputDevices::GetLog();
  FOutputDeviceFile* OutputDeviceFile = static_cast<FOutputDeviceFile*>(OutputDevice);  // unchecked
  logFilePath = OutputDeviceFile->GetFilename();
  ```
* `GetLog()` does not always return an `FOutputDeviceFile` — with `-NOLOGFILE`, or when logging is
  compiled out of Shipping, it returns a different device. The `static_cast` is undefined behaviour
  and `GetFilename()` will read a bogus vtable.
* This function is the one GameLift uses to report log paths for session log capture, so it *is* on
  the production path.
* **Fix:** the plugin is third-party; either patch it to test the device type, or stop calling
  `Get Log File Path` from Blueprint and pass a known path to `ProcessReady` instead.
* **Test:** launch the Linux server with `-NOLOGFILE` and call the node.

### LNX-04 — `UServerHelper::ServerIPAndPort` leaves outputs untouched off the dedicated server
* **Severity:** S4 · **Confidence:** CONFIRMED
* Also uses `EGetWorldErrorMode::Assert`, which will `check()`-fail if the world context is invalid.

### LNX-05 — Source files carry non-ASCII text without a BOM
* **Severity:** S3 · **Confidence:** CONFIRMED · **Class:** REQUIRED BUT NEEDS FIX
* Files with non-ASCII bytes and **no** UTF-8 BOM:
  `awsTutorial_PlayerController.{h,cpp}`, `ColorCatalogWidget.cpp`,
  `FurnitureConfigurator/{Data/FurnitureTypes.h, Preview/FurniturePreviewActor.{h,cpp}, ShowroomBooth.cpp,
  BoothInteractionInterface.h, UI/SaveSystemWidget.h, UI/SaveHistoryItemWidget.h}`.
* Several of these contain **Cyrillic UI strings** inside `TEXT(...)` literals
  (`ColorCatalogWidget.cpp:53,193`, `ConfiguratorMainWidget.cpp:651`, `RoomPlannerWidget.cpp:86-363`).
  MSVC (Windows editor build) and Clang (Linux build) can disagree about the source encoding of a
  BOM-less file, producing **different bytes in the shipped strings** on the two platforms.
* The comment blocks are already visibly corrupted — `awsTutorial_PlayerController.cpp` is full of
  `РІвЂќР‚` and `вЂ”`, which is UTF-8 that was decoded as CP1251 and re-encoded. Log strings such as
  `TEXT("[PC] Scalability set to Epic (3) вЂ” quality override applied.")` carry the mojibake into
  production logs.
* **Fix:** normalise every source file to UTF-8 **with BOM**, repair the mojibake, and move the
  user-facing Cyrillic text into the localisation system (`NSLOCTEXT`) instead of `FText::FromString`.

### LNX-06 — Hard-coded Cyrillic UI strings bypass localisation
* **Severity:** S3 · **Class:** REQUIRED BUT SHOULD BE SIMPLIFIED
* `InternationalizationPreset=English`, `CulturesToStage=en` in `DefaultGame.ini`, yet the
  configurator UI is Russian, hard-coded via `FText::FromString`. Nothing is translatable and the
  strings are invisible to the localisation dashboard.

### LNX-07 — Clipboard plugin depends on SDL on Linux
* **Severity:** S3 · **Confidence:** PLAUSIBLE · **Class:** PLATFORM-SPECIFIC
* `Plugins/Clipboar9fd0aa5b4ecdV2` is a one-line wrapper around
  `FPlatformApplicationMisc::ClipboardCopy/Paste`. On a headless Linux GPU instance with
  `-RenderOffscreen` there is no X11 clipboard, so both calls are unreliable. The one Blueprint that
  uses it (`BP_UserSettings_UI`) has a "copy" button that will silently do nothing in production.
* **Fix:** route copy through the Pixel Streaming data channel to `navigator.clipboard` in the browser
  (the same direction the paste workaround already goes).

---

## 8. Security (SEC)

### SEC-01 — The save API has no authentication and trusts a client-supplied username
* **Severity:** S1 · **Confidence:** CONFIRMED · **Class:** REQUIRED BUT NEEDS FIX
* **File:** `FurnitureConfigurator/UI/SaveSystemWidget.cpp`
  ```
  GET    {base}/api/saves/{UserName}
  POST   {base}/api/saves            body: { "username": UserName, ... }
  DELETE {base}/api/saves/{UserName}/{SaveId}
  ```
  No `Authorization` header, no Cognito ID/access token, no signature. `UserName` comes from a
  GameInstance property and falls back to the literal `"guest_tester"`.
* **Impact:** classic IDOR — anything that can reach the backend can read or delete any user's saves
  by changing the path segment. The Unreal client is not the security boundary here; the backend is.
* Additionally, `UserName` and `SaveId` are interpolated into the URL **without percent-encoding**
  (`FString::Printf(TEXT("%s/api/saves/%s/%s"), ...)`), so a username containing `/`, `?` or `..`
  changes the request path.
* **Fix (backend + client):** require the Cognito JWT as a bearer token; derive the username from the
  token server-side; ignore the client-supplied `username`. Client-side, URL-encode all path segments.
* **Test:** with a valid session, request another user's saves and confirm a 401/403.

### SEC-02 — Plaintext HTTP fallback for the save backend
* **Severity:** S2 · **Confidence:** CONFIRMED
* `GetBackendBaseURL()` derives `http://<host>:3000` from `-PixelStreamingURL=` (note: **http**, not
  https) whenever `-BackendURL=` is absent, and otherwise falls back to a hard-coded
  `https://18-185-5-251.nip.io`. Save payloads include a base64 screenshot of the user's design.
* **Fix:** require `-BackendURL=` with an https scheme; fail closed if absent.

### SEC-03 — Reflection lookup of `UserName` without a type check
* **Severity:** S2 · **Confidence:** CONFIRMED · **Class:** REQUIRED BUT NEEDS FIX
* **File:** `SaveSystemWidget.cpp` → `GetActiveUserName()`
  ```cpp
  FProperty* NameProp = GI->GetClass()->FindPropertyByName(TEXT("UserName"));
  FString* UserNamePtr = NameProp->ContainerPtrToValuePtr<FString>(GI);   // no CastField<FStrProperty>
  ```
* If the Blueprint variable is ever changed to `FText`, `FName` or an object reference, this
  reinterprets unrelated memory as an `FString` — a crash or an information leak, not a compile error.
* It is also brittle: renaming the Blueprint variable silently degrades every save to the shared
  `"guest_tester"` bucket.
* **Fix:** `if (FStrProperty* P = CastField<FStrProperty>(NameProp))`, and better, expose the username
  through a proper C++ interface or a `UGameInstanceSubsystem` instead of reflection.

### SEC-04 — AWS identifiers baked into a shipped asset
* **Severity:** S3 · **Confidence:** CONFIRMED · **Class:** REQUIRED BUT SHOULD BE SIMPLIFIED
* Cognito user-pool and identity-pool IDs and AppSync endpoints are inside the cooked
  `BP_AwsTutorial_GI`. These are not secrets, but they are also not rotatable without a re-cook, and a
  stale GameLift **game-session ARN** from a test run is serialized alongside them. See ARCH-04.
* **No static AWS access keys were found anywhere in the repository** — a scan for `AKIA…` patterns
  across `Content/`, `Config/` and `Source/` returned nothing. Credentials come from Cognito Identity
  at runtime, which is correct.

### SEC-05 — `content-length` computed from character count, not byte count
* **Severity:** S2 · **Confidence:** CONFIRMED · **Class:** REQUIRED BUT NEEDS FIX
* **File:** `Plugins/Ultimate969078bf2772V2/Source/GraphQL/Private/GraphQLFunctionLibrary.cpp:1416,1479`
  ```cpp
  Headers.Add("content-length", FString::FromInt(contentString.Len()));
  ```
* `FString::Len()` returns UTF-16 code units, not UTF-8 bytes. Any GraphQL variable containing
  non-ASCII (a Cyrillic room name, a display name, an emoji) makes `content-length` too small, which
  invalidates the SigV4 canonical request and gets the call rejected by AppSync.
* **Reproduce:** create a private room whose name contains Cyrillic characters while using IAM
  authorisation.
* **Fix:** compute the UTF-8 byte length (`FTCHARToUTF8(*contentString).Length()`).

### SEC-06 — Debug audio writer would capture user microphones
* See VOICE-08. Privacy-relevant, currently inert.

---

## 9. Correctness bugs in project C++ (BUG)

### BUG-01 — Out-of-bounds read in the screenshot thumbnail resampler
* **Severity:** S2 · **Confidence:** CONFIRMED · **Class:** REQUIRED BUT NEEDS FIX
* **File:** `SaveSystemWidget.cpp` → `OnScreenshotCapturedHandler`
  ```cpp
  int32 CropSize = FMath::Min(Width, Height);
  int32 SourceX = StartX + FMath::Clamp(x * CropSize / TargetWidth, 0, CropSize - 1);
  FColor Pixel = Colors[SourceY * Width + SourceX];
  ```
* Three missing guards:
  1. `Width` or `Height` == 0 ⇒ `CropSize == 0` ⇒ `FMath::Clamp(v, 0, -1)` (Min > Max) ⇒ **negative
     index** ⇒ out-of-bounds read.
  2. `Colors.Num()` is never compared against `Width * Height`.
  3. `CropSize / TargetWidth` is integer division; for a viewport narrower than 256 px the mapping
     collapses to column 0.
* **Reproduce:** a Pixel Streaming session that is minimised/backgrounded can deliver a 0×0 capture.
* **Fix:** early-out when `Width <= 0 || Height <= 0 || Colors.Num() < Width * Height`.

### BUG-02 — Every save writes two records
* **Severity:** S3 · **Confidence:** CONFIRMED
* `OnSaveClicked` calls `ExecuteSaveGame(id, name, "")` immediately and then again from
  `OnScreenshotCapturedHandler` with the thumbnail — two `POST /api/saves` with the same `saveId`.
  If the backend inserts rather than upserts, the history list shows duplicates.
* **Fix:** POST once with the thumbnail, or make the second call a `PUT`/`PATCH`.

### BUG-03 — Texture churn in the save-history grid
* **Severity:** S3 · **Confidence:** CONFIRMED
* `LoadTextureFromBase64` creates a fresh `UTexture2D` per history item on **every**
  `RefreshSaveHistory()`, and `RefreshSaveHistory()` is called from `NativeConstruct`,
  `OnPostSaveComplete` **and** `OnDeleteSaveComplete`. Old textures are only reclaimed by GC.
* **Fix:** cache decoded thumbnails by `saveId`.

### BUG-04 — Dead members and a dead function in the save widget
* **Severity:** S4 · **Class:** SAFE TO REMOVE (within the file)
* `ScreenshotTimeoutTimerHandle`, `bWaitingForScreenshot` are declared and never used;
  `OnScreenshotTimeout()` has an empty body with the comment `// Obsolete`.

### BUG-05 — `BoothInteractionInterface` is declared but never used anywhere
* **Severity:** S4 · **Confidence:** CONFIRMED · **Class:** UNUSED
* **File:** `Source/awsTutorial/FurnitureConfigurator/BoothInteractionInterface.h`
* `IBoothInteractionInterface` and its three events (`OnEnteredBoothRange`, `OnExitedBoothRange`,
  `OnBoothProductUpdated`) are:
  * not implemented by any C++ class (grep across `Source/`),
  * not implemented by any Blueprint (binary scan of all 2 269 assets — zero hits),
  * never invoked, despite the header claiming "AShowroomBooth and AAwsTutorial_PlayerController check
    for this interface".
* **Removable**, but confirm no in-flight branch depends on it first.

### BUG-06 — `OpenFurniturePreview` removes the main widget and then immediately re-adds it
* **Severity:** S3 · **Confidence:** CONFIRMED · **Class:** REQUIRED BUT SHOULD BE SIMPLIFIED
* `OpenFurniturePreview` calls `MainWidgetInstance->RemoveFromParent()` and then calls
  `CloseFurniturePreview()`, whose `if (PreviousBooth && MainWidgetInstance)` branch calls
  `AddToViewport()` on it again and re-sets the input mode. The net effect is a one-frame flicker plus
  redundant `SetupWidget` work.

### BUG-07 — `CloseFurniturePreview` empties the whole `HiddenActors` array
* **Severity:** S4 · **Confidence:** CONFIRMED
* `HiddenActors.Empty()` is `APlayerController::HiddenActors` — a shared engine list. If anything else
  ever hides actors per-player, this silently un-hides them.

### BUG-08 — `CloseFurniturePreview` can set a null view target
* **Severity:** S4 · **Confidence:** PLAUSIBLE
* `SetViewTargetWithBlend(GetPawn(), 0.0f)` — if the controller is unpossessed (travel, respawn) this
  passes `nullptr`.

### BUG-09 — Dead LoadObject fallback paths
* **Severity:** S4 · **Confidence:** CONFIRMED · **Class:** SAFE TO REMOVE
* `/Game/Constructor/Materials/M_OpeningSelection` — directory does not exist.
* `/Game/DT/DT_SharedFaucets`, `/Game/DT/DT_SharedMirrors` — assets do not exist; the real names are
  `AllowedFaucetIDs` / `AllowedMirrorIDs` and the primary paths already use them. Both fallbacks are
  commented as "legacy name fallback".

### BUG-10 — Synchronous `StaticLoadObject` on gameplay paths
* **Severity:** S3 · **Confidence:** CONFIRMED · **Class:** REQUIRED BUT SHOULD BE SIMPLIFIED
* `ShowroomBooth.cpp:588,596,604,608,616,620,865` call `StaticLoadObject` for the shared catalog
  DataTables at runtime instead of using the `ConstructorHelpers` references already established in
  the constructor. Each miss is a blocking package load.

---

## 10. Third-party plugin bugs (PLUG)

### PLUG-01 — `LoadingScreen` module is entirely dead
* **Severity:** S3 · **Confidence:** CONFIRMED · **Class:** UNUSED
* `Plugins/Ultimate969078bf2772V2/Source/LoadingScreen/` provides `UGameInstance_Modified` and
  `UGameViewportClient_Modified`. Nothing derives from either:
  * `Config/DefaultEngine.ini` sets `GameInstanceClass=/Game/.../BP_AwsTutorial_GI`, whose parent is
    engine `UGameInstance` (verified in the asset's import table).
  * No `GameViewportClientClass` override exists anywhere in `Config/`.
  * `/Script/LoadingScreen` appears in **zero** of the 2 269 content assets.
* It nevertheless drags `MoviePlayer` and `UMG` into every target, including the dedicated server.
* **Removal path:** delete the module entry from `awsSDK.uplugin` (do **not** delete the folder — the
  plugin is a purchased product and you may want to re-enable it after an upgrade).

### PLUG-02 — Raw owning pointers in the GraphQL subscription handlers
* **Severity:** S3 · **Confidence:** PLAUSIBLE · **Class:** REQUIRED BUT NEEDS FIX
* `USubscriptionHandler` holds `FConnectAction*` and
  `TMap<FString, FSendStartSubscriptionRequestAction*>` as raw pointers with no visible ownership or
  deletion. Latent action objects are owned by `FLatentActionManager`, so these are non-owning
  observers — but nothing clears them when the latent action completes, so they can dangle across a
  level travel. Voice-chat signalling and room subscriptions both go through this class.
* **Verify** with a level-travel stress test (lobby → session → back) before treating as a real leak.

### PLUG-03 — `GameLiftPlugin` (Editor) drags a Docker toolchain into the editor build
* **Severity:** S4 · **Class:** CORRECT CODE BUT NOT NEEDED FOR OUR ARCHITECTURE
* 91 files of Slate UI for building/testing Linux server containers locally. Editor-only
  (`"Type": "Editor"`), so it never ships. Harmless; only remove if editor start-up time matters.

### PLUG-04 — `RuntimeDataTable` + `EasyCsv` are completely unreferenced
* **Severity:** S3 · **Confidence:** CONFIRMED · **Class:** UNUSED
* `/Script/RuntimeDataTable` → 0 assets. `/Script/EasyCsv` → 0 assets.
  `RuntimeDataTable`, `EasyCsv`, `CsvToGoogleSheets` → 0 assets.
* It also vendors `jwt-cpp` and `picojson` headers, expanding the third-party surface for nothing.
* Note it is a **5.4** plugin whose `.uplugin` claims `EngineVersion 5.6.0` — an unverified upgrade.

### PLUG-05 — `UBIKSolver` is completely unreferenced
* **Severity:** S3 · **Confidence:** CONFIRMED · **Class:** UNUSED
* `/Script/UBIKRuntime` → 0 assets; `UBIKSolver`, `AnimNode_UBIKSolver` → 0 assets.
* VR upper-body IK — irrelevant to a Pixel-Streamed desktop configurator.
* Its `.uplugin` is UTF-16 encoded, has no `EngineVersion`, and its `SupportedTargetPlatforms` omits
  `Mac`.

### PLUG-06 — `Icmp` plugin ships an unused Windows implementation
* **Severity:** S4 · **Class:** PLATFORM-SPECIFIC
* `Private/Windows/IcmpWindows.cpp` is compiled only on Win64. Irrelevant for Linux-only production
  but harmless; it disappears if the plugin is removed per ARCH-02.

---

## 11. Cleanup candidates

### 11.1 SAFE TO REMOVE (strong evidence, low risk)

| Item | Evidence | What to check first |
|---|---|---|
| `AwsYamls/` (7 files) | byte-identical duplicates of the 7 root-level `awsTemplate_*.yaml` (`cmp` clean on all 7) | nothing — pick one location and keep it |
| `Source/awsTutorial/FurnitureConfigurator/BoothInteractionInterface.h` | zero C++ references, zero Blueprint implementers | no uncommitted branch uses it |
| Dead fallback paths in `ProceduralWallActor.cpp` (`/Game/Constructor/...`) and `ShowroomBooth.cpp` (`DT_SharedFaucets`, `DT_SharedMirrors`) | target assets do not exist | none |
| `USaveSystemWidget::OnScreenshotTimeout`, `ScreenshotTimeoutTimerHandle`, `bWaitingForScreenshot` | empty body, never referenced | none (but see PS-07 — you may want to *use* the flag instead) |
| Unreachable PS late-bind branches in `awsTutorial_PlayerController` (`BeginPlay` `if(!PixelStreamingInput)` + `PlayerTick` `TObjectIterator` block) | the constructor's `CreateDefaultSubobject` makes the pointer always valid | confirm no build config removes the CDO subobject |
| `Content/NewBlueprint.uasset`, `Content/NewMaterial.uasset` | not present in the staged Linux manifest ⇒ unreferenced | open once in the editor to confirm "Reference Viewer" is empty |
| `Intermediate/` (11 GB), `Saved/` (6.7 GB), `.vs/` (1.9 GB), `DerivedDataCache/` (1.6 MB), `awsTutorial.sln` (2.2 MB) | regenerated build artefacts | make sure they are in `.gitignore` before deleting |
| `Build/*/FileOpenOrder/*.log` | cooker profiling output | only if you are not using file-open-order optimisation |

### 11.2 PROBABLY REMOVABLE (good evidence, needs one confirmation)

| Item | Evidence | Must check before deleting |
|---|---|---|
| `Plugins/RuntimeDataTable_5.4/` | 0 asset references, 0 C++ references | search any external/CI scripts for `RuntimeDataTable`; do a full editor load and check for "missing class" warnings |
| `Plugins/UBIKSolver-main/` | 0 asset references, 0 C++ references | check animation Blueprints for a "UBIK Solver" anim graph node (binary scan found none, but anim-node names can be stored differently) |
| `LoadingScreen` module inside `awsSDK.uplugin` | GameInstance parent is engine `UGameInstance`; 0 asset references | confirm no plan to add movie loading screens |
| `Plugins/icmppingd30c78843dcfV2/` | used by exactly 1 asset, and that usage is architecturally wrong under Pixel Streaming (ARCH-02) | replace the latency logic in `BP_AwsTutorial_GI` first, then remove |
| `Plugins/Clipboar9fd0aa5b4ecdV2/` | used by exactly 1 asset; does not work on headless Linux (LNX-07) | replace the copy button with a PS data-channel message first |
| `LinuxClient/` and `LinuxServer/` staged builds (2.1 GB) | build output committed into the project tree | confirm the deploy pipeline does not read them from here |
| `Samples/PixelStreaming/WebServers/` | stock Epic reference, superseded by your own frontend | confirm the real frontend is versioned somewhere else (ARCH-03) |
| Unused AWS service modules in `awsSDK.uplugin` — `Lambda`, `DynamoDB`, `DynamoDBStreams`, `S3Control`, `S3Encryption`, `S3Outposts` | `/Script/<Module>` appears in **0** of 2 269 assets for each | see 11.4 — this is a build-time-only saving unless the ThirdParty archives are also pruned |

### 11.3 REQUIRES MANUAL VERIFICATION (do not touch without a product decision)

| Item | Why it is ambiguous |
|---|---|
| `Content/FirstPerson/Blueprints/FirstPersonMap/` weapon/projectile/AI family (`BP_Rifle_Component`, `BP_Pistol_Component`, `BP_Shotgun_Component`, `BP_PickUp_Rifle`, `BP_FirstPersonProjectile`, `BP_NullWeapon_Component`, `BP_Transparent_*`) | pure FPS-demo leftovers, but `BP_FirstPersonCharacter` — which also hosts **AcceptPlayerSession and the entire voice-chat component stack** — references them. Removing them means surgery on the most load-bearing Blueprint in the project. |
| `WS_MMO_AI/` (13 assets: behaviour tree, blackboard, AI controller, tasks, WS message structs) | belongs to the template's WebSocket-MMO sample. `BP_WSMMO_AI_PlayerCharacter` is placed in `WaitingRoomLobbyMap`. Confirm whether the "other players you see" in a room are these AI proxies or real replicated pawns — this changes the answer to "can players see each other". |
| `FriendSystem/` (26 structs) + `InvitationSystem/` (4 structs) + `BP_Friend_UI`, `BP_PendingFriend_UI`, `BP_UserSettings_UI` | drive the `GraphQL`, `S3`, `Transfer`, `QREncode`, `EnhancedScrollBox`, `Clipboard`, `CloudWatchLogs` plugins. If the product needs friends/invites/avatars, all of that stays. If not, six plugins become removable at once. **Decide this first — it is the biggest single cleanup lever.** |
| `Matchmaking/` (12 structs) + `awsTemplate_Matchmaking.yaml` | FlexMatch types exist but the room UI uses the PrivateGame Lambdas instead. Confirm matchmaking is definitively out of scope. |
| `MatchingRoomCombatMap_P00` / `_Server_P00`, `LightMap_P00` | streamed by `BP_FirstPersonCharacter`; cooked into both stages. Confirm they are not part of the showroom flow. |
| `Content/AdvancedPhotoMode`, `Content/FPWeapon`, `Content/Characters/Heroes`, `Content/ContextEffects` | large template content packs; check the reference viewer per pack |
| `Constructor/` room planner (2 800 lines C++ + `M_OpeningSelection` + `WBP_RoomPlannerWidget`) | a complete, non-trivial feature that is **not** in the target architecture you described. It also owns 9 of the 13 unvalidated Server RPCs and PS-02/PS-03. If it is not shipping, removing it eliminates a large share of this audit's findings. **Product decision required.** |
| `Content/Developers/`, `Content/Collections/` | not staged (confirmed), but they are editor working dirs |
| `Content/Milu1000x503`, `Content/SM_MERGED_*`, `Content/countertop_100`, `Content/SM_Tumba*` at the Content root | `SM_MERGED_*` **are** staged (referenced by the lobby map); the others need per-asset reference checks |

### 11.4 MUST KEEP

| Item | Why |
|---|---|
| `AAwsTutorial_PlayerController::{GetRequestURL, GetRequestOptions, HasRequestOption, GetRequestOption, Kick}` | `BP_FirstPersonGameMode` reads the GameLift `PlayerSessionId` through these. Removing them breaks player-session validation. |
| `awsSDK` modules `AWSCore`, `CognitoIdp`, `CognitoIdentity`, `GameLift`, `GameLiftServerSDK`, `EmbeddedVoiceChat`, `GraphQL`, `S3`, `Transfer`, `CloudWatchLogs` | all referenced by shipping Blueprints |
| `EmbeddedVoiceChatPixelStreamingBridge` | the only way a browser microphone reaches voice chat |
| `AutoDeSerialize` (14 assets), `WebSocket` (4), `Regexp` (3), `Restful` (2) | actively used on the login / room / friend paths |
| `Content/Certificates/cacert.pem` and both of its staging directives in `DefaultGame.ini` | TLS trust store for libcurl/AWS SDK on Linux |
| `Content/Data/Colors/*.json` | required by `UColorCatalogSubsystem` — currently broken **because** they are not staged, not because they are unused |
| `awsTemplate*.yaml` (one copy) | the only record of the backend contract |
| `Content/FirstPerson/Maps/UserLogin`, `WaitingRoomLobbyMap` (+ `_P00`, `_P01`, `_Server_P00`) | boot map and gameplay map |

---

## 12. Full remediation plan

Ordering rationale: fix what is **broken in production today** first, then what is **unsafe**, then
what **wastes money on AWS**, then cleanup. Nothing below has been implemented.

### Phase 0 — Product decisions (blocking, no code)

| # | Decision | Blocks |
|---|---|---|
| 0.1 | Is the **room planner** (`Constructor/`) shipping? | PS-02, PS-03, NET-01, NET-06..08, 11.3 cleanup |
| 0.2 | Are **friends / invitations / avatar upload** shipping? | removal of 6 plugins + 30 assets |
| 0.3 | Is the configurator state **per-player** or **shared per room**? | ARCH-05, NET-02 |
| 0.4 | Is **matchmaking** out of scope permanently? | Matchmaking asset removal |

### Phase 1 — Critical correctness (do first)

| # | Item | Priority | Risk | Files | Expected result | Verification |
|---|---|---|---|---|---|---|
| 1.1 | **PKG-01** move `Data/Colors` staging directives to `DefaultGame.ini`, repair truncated line | P0 | very low | `Config/DefaultGame.ini`, `Config/DefaultEngine.ini` | colour catalog populated in the packaged Linux client | `grep -i colors LinuxClient/Manifest_NonUFSFiles_Linux.txt` returns 2 rows; log shows `SUCCESS: Loaded color catalog` |
| 1.2 | **VOICE-01** ring buffer instead of `TArray::Pop` | P0 | low | `EmbeddedVoiceChatPixelStreamingAudioComponent.{h,cpp}` | loopback monitor intelligible; memory flat | 5-minute two-client voice session; watch RSS |
| 1.3 | **VOICE-02** initialise `Audio::FResampler` | P0 | low | `EmbeddedVoiceChatPixelStreamingAudioCapture.cpp` | correct pitch on the receiving peer | two clients at 48 kHz browser / non-48 kHz codec |
| 1.4 | **VOICE-05** remove null deref in the log line | P0 | none | same file | no crash path | code review + a run with 0 streamers |
| 1.5 | **BUG-01** bounds-guard the thumbnail resampler | P0 | none | `SaveSystemWidget.cpp` | no OOB on 0×0 captures | save while the browser tab is backgrounded |
| 1.6 | **PKG-02** make `M_OpeningSelection` a hard reference | P1 | low | `ProceduralWallActor.{h,cpp}` | opening highlight visible in packaged build | asset present in `Manifest_UFSFiles_Linux.txt` |
| 1.7 | **LNX-03** stop trusting the `FOutputDeviceFile` downcast | P1 | low | `ServerHelper.cpp` (plugin patch) or the calling Blueprint | no UB on the GameLift log path | run server with `-NOLOGFILE` and call the node |

### Phase 2 — Multiplayer / session / login

| # | Item | Priority | Risk | Files | Expected result | Verification |
|---|---|---|---|---|---|---|
| 2.1 | **NET-01** real `_Validate` for the 17 stub RPCs | P1 | low | `awsTutorial_PlayerController.cpp`, `ShowroomBooth.cpp` | malformed client input rejected, not executed | fuzz harness sends NaN/inf/huge/negative; server stays healthy |
| 2.2 | **NET-02** validate + authorise `Server_LoadBoothState` | P1 | medium | same + `SaveSystemWidget.cpp` | one player cannot rewrite the room for everyone (per decision 0.3) | 2-client test: A loads a save, B's view behaves as designed |
| 2.3 | **NET-04** `AddDynamic` → `AddUniqueDynamic` (line 755) | P2 | none | `awsTutorial_PlayerController.cpp` | `LoadProductPreview` runs once per change | breakpoint / log count |
| 2.4 | **NET-03** stop mutating replicated `bHidden` from clients | P2 | low | `FurniturePreviewActor.cpp` | booth visibility consistent across clients | 2-client test: A enters view mode, B still sees the booth |
| 2.5 | **ARCH-02** replace ICMP latency with a fixed region | P1 | medium | `BP_AwsTutorial_GI`, `BP_Online_Offline_Selection_UI` | deterministic placement in the GPU fleet's region; room join stops depending on a permission-gated syscall | join public + private rooms from a real GPU instance |
| 2.6 | **ARCH-04** externalise AWS config to ini + command line | P1 | medium | new `UDeveloperSettings` class, `BP_AwsTutorial_GI` | environment switch without a re-cook | launch with `-CognitoUserPoolId=` override and log in |
| 2.7 | Remove the stale GameLift session ARN and the `us-east-1_123456789` placeholder | P3 | none | `BP_AwsTutorial_GI`, `BP_UserLogin_UI` | no misleading defaults | open the assets in the editor |

### Phase 3 — Dedicated server

| # | Item | Priority | Risk | Files | Expected result | Verification |
|---|---|---|---|---|---|---|
| 3.1 | **PS-02** stop ticking `ARoomPlannerManager` on the dedicated server | P0 | low | `RoomPlannerManager.cpp` | server CPU no longer scans `GUObjectArray` every frame | `stat game` before/after with a wall committed |
| 3.2 | **PS-03** delete the duplicate PS handler on the manager | P1 | low | `RoomPlannerManager.{h,cpp}` | one handler, one reply, no client-side desync | send `get_state` from the browser, expect exactly one `MaxiMallConstructor:` |
| 3.3 | **NET-05** skip door animation on the dedicated server | P1 | very low | `ShowroomBooth.cpp` | no 60 Hz timer per booth on the server | `stat game` with N booths |
| 3.4 | **LNX-02** `ShouldCreateSubsystem` false on dedicated server | P2 | very low | `ColorCatalogSubsystem.{h,cpp}` | server does not parse colour JSON | server log has no ColorCatalog lines |
| 3.5 | **PKG-03** exclude PixelStreaming from the Server target | P2 | medium | `awsTutorial.Build.cs` + PS call sites + bridge `.uplugin` `TargetDenyList` | smaller server binary and stage; no PS component per player | rebuild server; `grep -ci pixelstreaming LinuxServer/Manifest_UFSFiles_Linux.txt` → 0 |
| 3.6 | **NET-07** cache the planner manager pointer | P3 | none | `awsTutorial_PlayerController.cpp` | no per-frame `TActorIterator` | profile |

### Phase 4 — Voice chat (beyond Phase 1)

| # | Item | Priority | Risk | Files |
|---|---|---|---|---|
| 4.1 | **VOICE-06** remove per-packet logging + fix the `%d`/`size_t` mismatch | P1 | none | `EmbeddedVoiceChatPixelStreamingAudioCapture.cpp` |
| 4.2 | **VOICE-04** unsubscribe from the delegates actually subscribed to | P1 | low | same |
| 4.3 | **VOICE-08** unify the debug macro name and disable it for Shipping | P1 | none | `EmbeddedVoiceChatPixelStreamingBridge.Build.cs` + same file |
| 4.4 | **VOICE-07** reuse a buffer instead of `malloc`/`free` per packet | P2 | low | same |
| 4.5 | **VOICE-09** clean up before `Super::BeginDestroy()`; guard module access | P2 | low | same + `GraphQLFunctionLibrary.h` |
| 4.6 | **VOICE-03** float division in the ratio | P2 | none | `EmbeddedVoiceChatAudioCapture.cpp` (plugin patch) |
| 4.7 | **VOICE-11** document + verify the EC2↔EC2 UDP/STUN/TURN requirement | P1 | n/a | infrastructure, not code |

Verification for all of Phase 4: a two-browser voice session between two GPU instances —
intelligible audio both ways, flat memory, no log spam, clean disconnect and reconnect.

### Phase 5 — Pixel Streaming

| # | Item | Priority | Risk | Files |
|---|---|---|---|---|
| 5.1 | **PS-01** delete unreachable late-bind code and the contradictory comments | P2 | none | `awsTutorial_PlayerController.{h,cpp}` |
| 5.2 | **PS-07** replace `FScreenshotRequest` with a render-target readback; bind the delegate once | P1 | medium | `SaveSystemWidget.cpp` |
| 5.3 | **PS-05** gate or remove the 5 Hz clipboard poll | P2 | low | `awsTutorial_PlayerController.cpp` |
| 5.4 | **PS-06** remove `SendDiag_PC` from Shipping | P3 | none | same |
| 5.5 | **ARCH-03** vendor or pin the custom browser frontend; add a protocol contract test | P1 | low | `Samples/` or a new `docs/PS_PROTOCOL.md` |

### Phase 6 — Security

| # | Item | Priority | Risk |
|---|---|---|---|
| 6.1 | **SEC-01** authenticate `/api/saves` with the Cognito JWT; derive the username server-side | P0 | medium (backend + client) |
| 6.2 | **SEC-01b** percent-encode every URL path segment | P1 | none |
| 6.3 | **SEC-02** require an https `-BackendURL=`; fail closed | P1 | low |
| 6.4 | **SEC-03** `CastField<FStrProperty>` (or drop the reflection lookup entirely) | P1 | none |
| 6.5 | **SEC-05** UTF-8 byte length for `content-length` | P1 | low |

### Phase 7 — Linux Shipping hygiene

| # | Item | Priority | Risk |
|---|---|---|---|
| 7.1 | **LNX-05** re-save every source file as UTF-8 **with BOM**; repair the mojibake | P2 | low, but touches many files — do it as one isolated commit with no logic changes |
| 7.2 | **LNX-01** collapse the 13 colour search paths to one (after 1.1) | P2 | none |
| 7.3 | **LNX-06** move Cyrillic UI strings to `NSLOCTEXT` and add a `ru` culture | P3 | low |
| 7.4 | **PKG-04** remove the trailing comma in the bridge `.uplugin` | P3 | none |
| 7.5 | **LNX-07** replace the Clipboard plugin call with a PS data-channel copy | P3 | low |

### Phase 8 — Performance

| # | Item | Priority | Risk |
|---|---|---|---|
| 8.1 | **BUG-10** use the constructor-cached DataTables instead of `StaticLoadObject` | P2 | low |
| 8.2 | **BUG-03** cache decoded thumbnails by `saveId` | P3 | none |
| 8.3 | **BUG-02** POST once per save | P3 | low |
| 8.4 | **NET-06** delta-replicate the room layout instead of a whole JSON string | P3 | medium (only if the planner ships) |

### Phase 9 — Safe cleanup (§11.1)

Delete duplicates and dead code. One commit per category, each independently revertible. Re-cook and
diff the staged manifests after each — the manifest diff is the regression test.

### Phase 10 — Cleanup requiring verification (§11.2, §11.3)

Gate every item on the Phase 0 decisions. Recommended order: unused plugins →
`LoadingScreen` module → unused AWS modules → template content packs → FPS-demo Blueprints last
(they are entangled with `BP_FirstPersonCharacter`).

### Phase 11 — Optional refactoring

| # | Item | Value |
|---|---|---|
| 11.1 | Split `AAwsTutorial_PlayerController` (1 825 lines) into a `UBoothInteractionComponent`, a `UPixelStreamingBridgeComponent` and a thin session controller | isolates the GameLift-critical 60 lines from the configurator churn — the single highest-value refactor |
| 11.2 | Move the AWS/session Blueprint logic out of `BP_FirstPersonCharacter` into a `PlayerState` or a component | the character currently owns `AcceptPlayerSession`, voice chat, level streaming, weapons and configurator interaction |
| 11.3 | Give the room planner its own module (or delete it per decision 0.1) | removes ~2 800 lines and 9 RPCs from the shipping path |
| 11.4 | Replace `IncludeOrderVersion.Unreal5_4` with `Unreal5_6` and fix the fallout | unblocks future engine upgrades |

---

## 13. External / manual checks required

Nothing below can be settled from source alone.

### In the Unreal Editor
1. Open `BP_AwsTutorial_GI` and confirm the parent class, the full AWS variable list, and whether the
   stale GameLift ARN is a variable default or debug data.
2. Reference Viewer on: `M_OpeningSelection`, `NewBlueprint`, `NewMaterial`, `Content/Milu1000x503`,
   `Content/SM_Tumba*`, `Content/countertop_100`.
3. Confirm no Animation Blueprint contains a **UBIK Solver** anim-graph node.
4. Confirm no Blueprint implements `BoothInteractionInterface` (Class Settings → Interfaces).
5. Confirm whether `BP_WSMMO_AI_PlayerCharacter` instances in `WaitingRoomLobbyMap` are how remote
   players are visualised, or leftover template AI.
6. Confirm which of the two voice-capture components on `BP_FirstPersonCharacter`
   (`EmbeddedVoiceChatAudio` vs `EmbeddedVoiceChatPixelStreamingAudio`) is actually initialised.

### Linux Shipping client on an AWS GPU instance
7. Confirm PKG-01 by grepping the log for `[ColorCatalogSubsystem]`.
8. Confirm whether `FPlatformApplicationMisc::ClipboardPaste` errors or is silent under
   `-RenderOffscreen` (PS-05, LNX-07).
9. Confirm `SendIcmpRequest` behaviour as the actual service user — check
   `sysctl net.ipv4.ping_group_range` (ARCH-02).
10. Measure the frame hitch caused by `FScreenshotRequest` during a save (PS-07).
11. Confirm the `-BackendURL=` / `-PixelStreamingURL=` launch arguments the deploy scripts actually pass.

### Linux Dedicated Server on GameLift
12. `stat game` / `stat unit` with one client that has committed a wall — quantify PS-02.
13. Launch with `-NOLOGFILE` and exercise the `Get Log File Path` node — LNX-03.
14. Confirm `libaws-cpp-sdk-gamelift-server.so` is present next to the executable in the fleet build.
15. Confirm `ProcessReady` → `ActivateGameSession` → `AcceptPlayerSession` all succeed, and that an
    invalid `PlayerSessionId` results in a `Kick()`.

### Pixel Streaming browser session
16. Verify the custom frontend implements every message in `PROJECT_ARCHITECTURE.md` §2.2, in both
    directions, and record which repository/commit it lives in (ARCH-03).
17. Verify clipboard paste into the login fields works end-to-end.
18. Verify `open_url:` opens the product URL.

### Voice chat, two or more clients
19. Two browsers → two GPU instances → intelligible two-way audio (VOICE-01/02).
20. Confirm the EC2 security groups permit the WebRTC UDP port range and that STUN/TURN is reachable
    (VOICE-11).
21. Watch the client log for `captured N frames in M sample rate` spam (VOICE-06).
22. Watch RSS over a 30-minute session (VOICE-01).

### Multiplayer, two or more clients
23. Two clients into the **same public** room — do they see each other's pawns?
24. Two clients into the **same private** room via room code — same question.
25. One client changes a booth product/colour — does the other see it? (`AShowroomBooth` RepNotify)
26. One client draws a wall — does the other see it? (`ReplicatedRoomJSON`)
27. One client presses "Load save" — what happens on the other client? (ARCH-05 / NET-02)
28. Disconnect one client mid-session — confirm `RemovePlayerSession` fires and GameLift's player
    count decrements.

### Backend
29. `GET /api/saves/<another-user>` without credentials — must be rejected (SEC-01).
30. Create a private room whose name contains Cyrillic characters — confirm the AppSync call succeeds
    (SEC-05).
