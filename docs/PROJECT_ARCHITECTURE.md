# awsTutorial / MaxiMall — Full Project Architecture

> **Audit date:** 2026-08-21
> **Engine:** Unreal Engine 5.6.1 (BuildId `6d260af4-bc90-462b-9188-59be629997b2`)
> **Project file:** `awsTutorial.uproject`
> **Primary game module:** `awsTutorial` (`Source/awsTutorial/`)
> **Targets built for production:** `awsTutorialClient-Linux-Shipping`, `awsTutorialServer-Linux-Shipping`
>
> Companion documents:
> * `docs/AUDIT_2026-08-21_FINDINGS.md` — bugs, risks, cleanup candidates, remediation plan
> * `docs/PLUGIN_INDEX.md` — one-line index of every plugin + link to its own doc
> * `docs/AWSTUTORIAL_SOURCE_GUIDE.md` — pre-existing per-file C++ reference (kept as-is)
> * `<PluginDir>/PLUGIN_DOC.md` — detailed documentation inside every plugin folder

---

## 0. TL;DR — what this project actually is

This repository is **two projects fused into one**:

| Layer | Origin | What it does |
|---|---|---|
| **A. Online backbone** | Fork of the marketplace template *"Cognito / DynamoDB / GameLift / Lambda Client + GameLift Server SDK"* by Siqi Wu (`Plugins/Ultimate969078bf2772V2`, `awsSDK.uplugin`) plus its FPS demo content under `Content/FirstPerson/` | Login (Cognito), public/private rooms (AppSync GraphQL + Lambda), GameLift game-session placement, dedicated-server connection, replication, P2P voice chat |
| **B. Product configurator** | Custom code written for this project ("MaxiMall") — **all of `Source/awsTutorial/`** | Furniture showroom booths, procedural room planner, RAL/NCS colour catalog, save/load against a private REST backend, Pixel Streaming browser bridge |

**The entire login -> room -> session -> dedicated-server flow lives in Blueprints, not in C++.**
The C++ module contributes exactly **three** functions to that flow
(`GetRequestOption`, `HasRequestOption`, `Kick` on `AAwsTutorial_PlayerController`) plus the
Pixel Streaming data-channel plumbing. Everything else in `Source/` is the configurator.

---

## 1. Build targets and what is compiled where

`Source/*.Target.cs` declares four targets. All four are identical except for `Type`.

| Target | Type | Used in production | Notes |
|---|---|---|---|
| `awsTutorial` | Game | No | Monolithic client+server. Not used; the Client/Server split is used instead. |
| `awsTutorialClient` | Client | **Yes** — Linux Shipping, runs on AWS GPU instances | |
| `awsTutorialServer` | Server | **Yes** — Linux Shipping, runs on the GameLift fleet | |
| `awsTutorialEditor` | Editor | dev only | |

All targets set `IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_4` on a 5.6 engine
(legacy include ordering — it works, but it is one engine generation behind).

### 1.1 `awsTutorial.Build.cs` dependencies

```
Public : Core CoreUObject Engine InputCore EnhancedInput NetCore UMG HTTP Json
         JsonUtilities ApplicationCore RenderCore ProceduralMeshComponent
Private: Slate SlateCore PixelStreaming PixelStreamingInput ImageWrapper
```

* The game module has **no** dependency on any AWS SDK module. The AWS layer is reached
  exclusively from Blueprints through the plugin's Blueprint-exposed classes.
* `PixelStreaming` / `PixelStreamingInput` are added **unconditionally**, so they are also
  linked into and staged with the dedicated-server build (finding `PKG-03`).

### 1.2 Server-target compile gating inside `awsSDK`

Every AWS **client** module is compiled out for `TargetType.Server`:

```
AWSCore            -> WITH_CORE=0                      on Server
GameLift (client)  -> WITH_GAMELIFTCLIENTSDK=0         on Server
CognitoIdp         -> WITH_COGNITOIDPCLIENTSDK=0       on Server
CognitoIdentity    -> WITH_COGNITOIDENTITYCLIENTSDK=0  on Server
S3 / S3Control / S3Encryption / S3Outposts / Transfer  -> ...=0 on Server
Lambda / DynamoDB / DynamoDBStreams / CloudWatchLogs   -> ...=0 on Server
EmbeddedVoiceChat  -> WITH_EMBEDDEDVOICECHAT=0         on Server
GameLiftServerSDK  -> WITH_GAMELIFT=1  ONLY on Server (Linux / Mac / Win64)
```

Confirmed empirically from the staged manifests:

* `LinuxServer/Manifest_NonUFSFiles_Linux.txt` contains **only**
  `libaws-cpp-sdk-gamelift-server.so` from the plugin.
* `LinuxClient/Manifest_NonUFSFiles_Linux.txt` contains **no** AWS shared object at all —
  the Linux client links the AWS C++ SDK **statically** (`.a` archives).

**Consequence:** the dedicated server can never call Cognito / GameLift-client /
S3 / AppSync-signing APIs. All of that is client-side only. This is correct for the
target architecture and must be preserved.

---

## 2. End-to-end runtime flow

```
                      +---------------------------------------------+
  Browser (user) -----| Pixel Streaming frontend (NOT in this repo)  |
        ^  ^          +---------------------------------------------+
        |  | WebRTC video/audio + data channel
        |  |
  +-----+--+----------------------------------------------------------+
  |  AWS GPU instance : awsTutorialClient-Linux-Shipping               |
  |                                                                    |
  |  Map: UserLogin  (GameDefaultMap)                                  |
  |    GameMode  BP_UserLogin_GM     PC  BP_UserLogin_PC               |
  |    UI        BP_UserLogin_UI  --> CognitoIdp InitiateAuth          |
  |                               --> CognitoIdentity GetId /          |
  |                                   GetCredentialsForIdentity        |
  |    C++       AAwsTutorial_LoginPlayerController                    |
  |                  (Pixel-Streaming clipboard paste injection)       |
  |           v                                                        |
  |  GameInstance BP_AwsTutorial_GI  (holds ALL AWS config + tokens)   |
  |           v                                                        |
  |  Map: WaitingRoomLobbyMap  (ServerDefaultMap + lobby)              |
  |    UI  BP_Online_Offline_Selection_UI                              |
  |        +- Public  room : CreateRegularGame / JoinRegularGame       |
  |        +- Private room : CreatePrivateGame / JoinPrivateGame       |
  |             (AppSync GraphQL mutations, Cognito-JWT authorised)    |
  |        +- latency probe via Icmp plugin  --> BP_Latency_ST         |
  |           v                                                        |
  |    GameLiftClientObject::DescribeGameSessionPlacement (polled)     |
  |           v  IpAddress / Port / PlayerSessionId                    |
  |    ClientTravel  "IP:PORT?PlayerSessionId=psess-..."               |
  +------------------------------+-------------------------------------+
                                 | UE NetDriver (UDP 7777)
  +------------------------------v-------------------------------------+
  |  GameLift fleet : awsTutorialServer-Linux-Shipping                 |
  |    BP_FirstPersonGameMode                                          |
  |      InitSDK5(BP_ProcessParameters_PP) -> ProcessReady             |
  |      OnStartGameSession -> ActivateGameSession                     |
  |      PostLogin: HasRequestOption("PlayerSessionId")  <- C++        |
  |                 GetRequestOption("PlayerSessionId")  <- C++        |
  |                 DescribePlayerSessions -> validate                 |
  |                 (invalid => Kick())                  <- C++        |
  |    BP_FirstPersonCharacter                                         |
  |      AcceptPlayerSession / RemovePlayerSession                     |
  |    Replicated actors: Character, PlayerState, GameState,           |
  |                       AShowroomBooth, ARoomPlannerManager          |
  +--------------------------------------------------------------------+

  Voice chat MEDIA is peer-to-peer between the two UE clients on EC2:
     browser mic --WebRTC--> PS AudioSink
        +-> UEmbeddedVoiceChatPixelStreamingAudioCapture  (bridge plugin)
              +-> EmbeddedVoiceChat::AbstractCaptureDevice
                    +-> libdatachannel P2P (ICE/STUN/TURN, UDP)
                          +-> peer UE client -> USynthComponent -> game audio
                                +-> PS video/audio stream -> peer's browser

  Voice chat SIGNALLING goes through the DEDICATED SERVER, over ordinary UE RPCs:
     UEmbeddedVoiceChatRPCGroup (replicated UActorComponent on the character)
        client --Server RPC--> gotLocalDescription / gotLocalCandidate
        server --Client RPC--> gotRemoteDescription / gotRemoteCandidate
        server --Client RPC--> newPlayerJoin / oldPlayerLeave
     (An alternative UEmbeddedVoiceChatCustomGroup exists whose sendLocalDescription /
      sendLocalCandidate are BlueprintNativeEvents, so signalling can instead be routed over
      the WebSocket or GraphQL plugin. BP_FirstPersonCharacter carries a variable named
      EmbeddedVoiceChatWSGroup, so both paths may be present - confirm which one is live.)
```

> **Consequence:** voice chat depends on the dedicated server for signalling, even though
> `WITH_EMBEDDEDVOICECHAT=0` on the Server target. That still works because
> `gotLocalDescription_Implementation` (the relay) is outside the `#if WITH_EMBEDDEDVOICECHAT`
> block — only the media/codec internals are compiled out. **If a player is not connected to the
> dedicated server, they cannot establish voice.**

### 2.1 Stage-by-stage detail

| Stage | Where implemented | Key classes / assets |
|---|---|---|
| **Login** | Blueprint | `BP_UserLogin_UI`, `BP_UserLogin_GM`, `BP_UserLogin_PC`, `BP_AwsTutorial_GI`. Uses `UCognitoIdentityProviderClientObject` (`InitiateAuth`, `RespondToAuthChallenge`, `GetUser`, `AssociateSoftwareToken`) and `UCognitoIdentityClientObject` (`GetId`, `GetCredentialsForIdentity`). |
| **Login + Pixel Streaming** | C++ | `AAwsTutorial_LoginPlayerController` — receives `{"Cmd":"ClipboardPaste","Text":...}` on the PS data channel and injects the characters through `FSlateApplication::ProcessKeyCharEvent`, because the OS clipboard is unusable on a headless Linux box. |
| **Room create/join** | Blueprint | `BP_Online_Offline_Selection_UI` plus the `PrivateRoom/` struct family. Calls AppSync mutations `createRegularGame`, `joinRegularGame`, `createPrivateGame`, `joinPrivateGame` (see `awsTemplate_PrivateGame.yaml`). |
| **Region latency** | Plugin | `Icmp` plugin `SendIcmpRequest` -> `BP_Latency_ST[]` passed to placement. **See finding `ARCH-02` — this is measured from the EC2 instance, not from the end user.** |
| **Placement poll** | Blueprint | `UGameLiftClientObject::DescribeGameSessionPlacement` until `FULFILLED`, then read `GameSessionConnectionInfo` (IP / Port / `PlacedPlayerSessions`). |
| **Connect** | Engine | `ClientTravel` with a `?PlayerSessionId=` option. |
| **Server bootstrap** | Blueprint + plugin | `BP_FirstPersonGameMode` -> `UGameLiftServerFunctionLibrary::InitSDK5` / `ProcessReady(BP_ProcessParameters_PP)` / `ActivateGameSession` / `ProcessEnding`. |
| **Player-session validation** | **C++ + Blueprint** | `AAwsTutorial_PlayerController::HasRequestOption / GetRequestOption / Kick` — **the only production-critical multiplayer C++ in the project.** |
| **Replication** | C++ + Blueprint | `AShowroomBooth` (`ActiveState`, `CustomColors`, `DoorStates`, `bCountertopSizeFallbackActive`), `ARoomPlannerManager` (`ReplicatedRoomJSON`), plus the Blueprint character / player-state / game-state classes. |
| **Voice chat** | Plugin | `EmbeddedVoiceChat` (awsSDK) + `EmbeddedVoiceChatPixelStreamingBridge`. Components live on `BP_FirstPersonCharacter`. |
| **Pixel Streaming** | Engine plugin + C++ | Engine `PixelStreaming`; project side is `AAwsTutorial_PlayerController::OnPixelStreamingInput` / `BroadcastCursorState` / `SendOpenURLToBrowser` and `ARoomPlannerManager::OnPixelStreamingInputReceived`. |
| **Configurator save/load** | C++ | `USaveSystemWidget` -> private REST backend (`/api/saves`). |

### 2.2 Pixel Streaming data-channel protocol (project-specific)

**Browser -> Unreal** (arrives in `OnPixelStreamingInput`, both player controllers):

| Payload | Handler |
|---|---|
| `MaxiMallPaste <text>` (legacy plain text) | `AAwsTutorial_PlayerController` — Slate char injection |
| `{"Cmd":"ClipboardPaste","Text":"..."}` | both controllers — Slate char injection |
| `{"cmd":"add_wall" \| "add_opening" \| "clear" \| "get_state", ...}` | `ARoomPlannerManager::ProcessCommandJSON` |

A `UIInteraction:` prefix and a nested `descriptor` / `Descriptor` string field are both unwrapped.

**Unreal -> Browser** (`SendPixelStreamingResponse` / `Streamer->SendPlayerMessage`):

| Payload | Producer |
|---|---|
| `MaxiMallCursor pointer` / `MaxiMallCursor default` | `BroadcastCursorState` |
| `MaxiMallClipboard <text>` | `PlayerTick` clipboard poller |
| `MaxiMallConstructor:<json>` | room-planner command reply |
| `open_url: <url>` | `SendOpenURLToBrowser` |
| `DIAG: <text>` | `SendDiag_PC` diagnostics |

**The JavaScript that consumes and produces these messages is not in this repository.**
`Samples/PixelStreaming/WebServers` is the unmodified Epic reference frontend.

---

## 3. Map and GameMode wiring (`Config/DefaultEngine.ini`)

```
EditorStartupMap            = /Game/FirstPerson/Maps/WaitingRoomLobbyMap
GameDefaultMap              = /Game/FirstPerson/Maps/UserLogin
ServerDefaultMap            = /Game/FirstPerson/Maps/WaitingRoomLobbyMap
TransitionMap               = None
GameInstanceClass           = /Game/FirstPerson/Blueprints/BP_AwsTutorial_GI
GlobalDefaultGameMode       = /Game/FirstPerson/Blueprints/UserLogin/BP_UserLogin_GM
GlobalDefaultServerGameMode = None
```

Maps present (all 8 are cooked into both the Client and the Server stage):

| Map | Role |
|---|---|
| `UserLogin` | Client boot map. Login UI only. |
| `WaitingRoomLobbyMap` | Main gameplay / showroom map. Contains `BP_Booth`, `BP_FirstPersonCharacter`, `BP_FirstPersonGameMode`, WS-MMO AI pawns. 752 KB — the only large map. |
| `WaitingRoomLobbyMap_P00`, `_P01`, `_Server_P00` | Streaming sub-levels of the lobby (player-customization actors, transfer actors). |
| `MatchingRoomCombatMap_P00`, `_Server_P00` | Combat sub-levels streamed by `BP_FirstPersonCharacter`. Legacy FPS-demo content. |
| `LightMap_P00` | Lighting-only sub-level. |

`GameInstanceClass` derives from engine `UGameInstance`, **not** from the plugin's
`UGameInstance_Modified` — which is why the plugin's `LoadingScreen` module is dead code.

---

## 4. Where the AWS configuration lives

**All** AWS endpoints and pool identifiers are **hard-coded as Blueprint default values inside
`Content/FirstPerson/Blueprints/BP_AwsTutorial_GI.uasset`**:

* Cognito User Pool ID `ap-northeast-2_DQC2kURMX`
* Cognito Identity Pool ID `ap-northeast-2:2ca85c1d-...`
* Four AppSync GraphQL endpoints (`*.appsync-api.ap-northeast-2.amazonaws.com/graphql`)
  and their `appsync-realtime-api` WebSocket counterparts
* GameLift region / alias / fleet identifiers
* A GameLift game-session ARN left over from a test run

`Content/FirstPerson/UI/UserLogin/BP_UserLogin_UI.uasset` still carries the template placeholder
`us-east-1_123456789`.

There is **no** `.ini` or environment-variable path for any of this. Changing environment
requires a re-cook. See finding `ARCH-04`.

The configurator's own backend URL is hard-coded in C++:
`Source/awsTutorial/FurnitureConfigurator/UI/SaveSystemWidget.cpp` -> `https://18-185-5-251.nip.io`.

**Infrastructure-as-code** for the backend is checked in at the repo root
(`awsTemplate*.yaml`) and duplicated byte-for-byte in `AwsYamls/`.

Relevant CloudFormation stacks:

| File | Provides |
|---|---|
| `awsTemplate.yaml` | Cognito user pool, identity pool, base IAM |
| `awsTemplate_GameLift.yaml` | GameLift Build / Fleet / Alias / GameSessionQueue + fleet instance role |
| `awsTemplate_PrivateGame.yaml` | **The room system** — Lambdas `CreateRegularGame`, `JoinRegularGame`, `OpenRegularGame`, `CloseRegularGame`, `CreatePrivateGame`, `JoinPrivateGame`, plus the AppSync API, schema and resolvers |
| `awsTemplate_Matchmaking.yaml` | FlexMatch matchmaking (not wired into the current room UI) |
| `awsTemplate_FriendSystem.yaml`, `_InvitationSystem.yaml`, `_UGC.yaml`, `_WebSocketMMO.yaml` | Template extras carried over |

---

## 5. Source module map (`Source/awsTutorial/`)

```
awsTutorial.{h,cpp}                     IMPLEMENT_PRIMARY_GAME_MODULE - empty shell
awsTutorial_LoginPlayerController.*     Login-map PC; PS clipboard-paste injection
awsTutorial_PlayerController.*          THE monolith (1825 lines .cpp / 270 .h):
                                          - GameLift URL options + Kick  <- production critical
                                          - Pixel Streaming input/output data channel
                                          - Furniture preview open/close/orbit/zoom
                                          - Booth interaction + 4 Server RPCs
                                          - Room-planner 2D drawing + 9 Server RPCs
                                          - OS-clipboard polling at 5 Hz
ColorCatalog/                           RAL + NCS colour catalogs
  ColorCatalogSubsystem.*                 UGameInstanceSubsystem, loads 2 JSON files from disk
  ColorCatalogTypes.h                     FColorCatalogItem, EColorCatalogType, EColorShadeCategory
  ColorCatalogItemObject.h                UObject wrapper for UMG ListView
  ColorCatalogWidget.*                    catalog panel (Cyrillic UI strings hard-coded)
  ColorCatalogSwatchWidget.*              one swatch
Constructor/                            Procedural room planner ("MaxiMallConstructor")
  RoomPlannerTypes.h                      FWallNode / FWallSegment / FWallOpening / FRoomData
  RoomPlannerManager.*                    2326-line authority actor; graph of nodes+walls,
                                          JSON export/import, replicated via ReplicatedRoomJSON
  ProceduralWallActor.*                   per-wall UProceduralMeshComponent generator
FurnitureConfigurator/
  BoothInteractionInterface.h             *** declared, never implemented, never called ***
  Data/FurnitureTypes.*                   FFurnitureProductRow + FShowroomBoothConfigState
  ShowroomBooth.*                         2090-line replicated showroom actor
  Preview/FurniturePreviewActor.*         1325-line client-local "view mode" studio actor
  UI/ConfiguratorMainWidget.*             main configurator panel
  UI/FurnitureGridItemWidget.*            catalog tile
  UI/FurnitureSizePillWidget.*            size pill
  UI/RoomPlannerWidget.*                  planner tool panel (Cyrillic UI strings)
  UI/SaveHistoryItemWidget.*              save card
  UI/SaveSystemWidget.*                   REST save/load + screenshot thumbnail
  UI/ViewmodeOverlayWidget.*              "back" overlay during view mode
```

### 5.1 Call graph — who calls what

```
BP_FirstPersonGameMode      --> AAwsTutorial_PlayerController::HasRequestOption / GetRequestOption
BP_FirstPersonCharacter     --> AAwsTutorial_PlayerController::TraceFurnitureComponent
BP_Booth (Blueprint)         --- parent ---> AShowroomBooth
BP_FurniturePreviewActorNew  --- parent ---> AFurniturePreviewActor
WBP_ViewmodeOverlay          --- parent ---> UViewmodeOverlayWidget
WBP_ColorCatalog             --- parent ---> UColorCatalogWidget
WBP_RoomPlannerWidget        --- parent ---> URoomPlannerWidget

AAwsTutorial_PlayerController
  +-> AShowroomBooth::Request{ProductChange,DoorToggle,ComponentSelection,CustomColorChange}
  +-> AFurniturePreviewActor::{LoadProductPreview,SetFocusComponent,RotatePreview,ZoomPreview}
  +-> ARoomPlannerManager::GetOrCreateInstance + ~15 mutators
  +-> UConfiguratorMainWidget::{SetupWidget,RefreshSelections}
  +-> UViewmodeOverlayWidget::SetOwningPC
  +-> IPixelStreamingModule / UPixelStreamingInput

USaveSystemWidget
  +-> AShowroomBooth::ActiveState  (read, all booths in level)
  +-> AAwsTutorial_PlayerController::Server_LoadBoothState
  +-> FHttpModule  ->  https://18-185-5-251.nip.io/api/saves

UColorCatalogWidget --> UColorCatalogSubsystem::FilterColors
```

### 5.2 Replication contract

| Actor | `bReplicates` | Replicated properties | Authority mutators |
|---|---|---|---|
| `AShowroomBooth` | true | `ActiveState` (RepNotify), `CustomColors` (RepNotify), `DoorStates` (RepNotify), `bCountertopSizeFallbackActive` | `Server_ApplyProductChange`, `Server_ToggleDoor`, `Server_ApplyComponentSelection`, `Server_ApplyCustomColor` |
| `ARoomPlannerManager` | true | `ReplicatedRoomJSON` (RepNotify -> `ImportLayoutFromJSON`) | 9 `Server_*` RPCs on the player controller |
| `AProceduralWallActor` | **false** | — | spawned locally by the manager on each machine |
| `AFurniturePreviewActor` | **false** | — | client-local only, spawned by the owning PC |

`ARoomPlannerManager::GetOrCreateInstance` returns `nullptr` on a client when the replicated
instance has not arrived yet; it only spawns when `GetNetMode() != NM_Client`.

---

## 6. Plugin map

| Plugin folder | Module(s) | Used? | Role in *our* architecture |
|---|---|---|---|
| `Ultimate969078bf2772V2` (`awsSDK`) | 18 modules | **Yes — core** | Login, rooms, GameLift, voice chat, S3 avatars, AppSync |
| `EmbeddedVoiceChatPixelStreamingPlugin_UE5.6` | `EmbeddedVoiceChatPixelStreamingBridge` | **Yes — core** | Routes the browser microphone (PS WebRTC) into voice chat — mandatory for PS + voice |
| `AutoDeSef64e1bfc0e22V2` | `AutoDeSerialize`, `AutoDeSerializeNodes` | Yes (14 assets) | Struct <-> JSON / MessagePack for every GraphQL payload |
| `WebSocke3911c0965375V2` | `WebSocket` | Yes (4 assets) | Raw WS for the WS-MMO and voice signalling groups |
| `RegexpBl98e2507377d2V2` | `Regexp` | Yes (3 assets) | Login / registration field validation |
| `Restfulh16cb1dcaf2b4V2` | `Restful` | Yes (2 assets) | Generic REST from the GameMode and friend UI |
| `icmppingd30c78843dcfV2` | `IcmpBlueprint` | Marginal (1 asset) | GameLift region latency probe — **architecturally wrong under Pixel Streaming** |
| `Clipboar9fd0aa5b4ecdV2` | `Clipboard` | Marginal (1 asset) | Copy button in UserSettings; unreliable on headless Linux |
| `QREncode783839f97b45V2` | `QREncode` | Marginal (1 asset) | TOTP MFA QR code in UserSettings |
| `Enhanced2db8f2adfc28V2` | `EnhancedScrollBox` | Marginal (1 asset) | Scroll box in the UserSettings UI |
| `RuntimeDataTable_5.4` | `RuntimeDataTable`, `EasyCsv` | **No — 0 references** | Runtime CSV / Google Sheets import |
| `UBIKSolver-main` | `UBIKRuntime`, `UBIKEditor` | **No — 0 references** | VR upper-body IK |

Every plugin has its own `PLUGIN_DOC.md` with the full breakdown.

### 6.1 Plugin dependency graph

```
awsTutorial (game module)
   +-> PixelStreaming, PixelStreamingInput  (engine)

awsSDK plugin
   AWSCore  <- GameLift, CognitoIdp, CognitoIdentity, Lambda, DynamoDB,
               DynamoDBStreams, CloudWatchLogs, S3, S3Control,
               S3Encryption, S3Outposts, Transfer
   S3       <- S3Encryption, Transfer
   GameLiftServerSDK  (independent; Server target only)
   EmbeddedVoiceChat  (independent; needs AudioMixer + AudioCapture backends)
   GraphQL            (needs HTTP + WebSockets + OpenSSL)
   LoadingScreen      (needs MoviePlayer)     <-- DEAD, nothing derives from it
   GameLiftPlugin     (Editor only; Slate UI + Docker)

EmbeddedVoiceChatPixelStreamingBridge
   +-> PixelStreaming (engine)
   +-> EmbeddedVoiceChat + EmbeddedVoiceChatLibrary (awsSDK)
   +-> AudioMixer, AudioPlatformConfiguration
```

---

## 7. Deployment notes that follow from the code

1. **Client on GPU instance.** `awsTutorialClient-Linux-Shipping` is launched with Pixel
   Streaming arguments. `USaveSystemWidget::GetBackendBaseURL()` parses `-BackendURL=` first,
   then derives `http://<PS-signalling-host>:3000` from `-PixelStreamingURL=`, then falls back
   to the hard-coded `https://18-185-5-251.nip.io`.
2. **Static AWS SDK on the client.** No `.so` files to ship; about 3.5 GB of `.a` / `.lib`
   archives live in `Plugins/Ultimate969078bf2772V2/Source/ThirdParty/` but are build-time only.
3. **Server needs `libaws-cpp-sdk-gamelift-server.so`** next to the executable — it is the only
   plugin runtime dependency of the server target.
4. **Voice chat needs UDP egress plus STUN/TURN** between GPU instances. Media is *peer-to-peer
   between the Unreal clients running on EC2*, not between browsers. Security groups must allow
   the port range set by `UEmbeddedVoiceChatGroup::setIceServers(iceServers, portRangeBegin, portRangeEnd)`
   (a static, so it must be called once before any `joinGroup`). Signalling rides the normal
   dedicated-server connection and needs no extra ports.
5. **`Content/Certificates/cacert.pem`** is staged both UFS and NonUFS (declared in
   `DefaultGame.ini`) and is what the AWS SDK / libcurl uses for TLS on Linux. **Do not remove.**
6. **`Content/Data/Colors/*.json` is NOT staged** — see finding `PKG-01`.
7. **Scalability is force-overridden to Epic (3)** in `AAwsTutorial_PlayerController::BeginPlay`,
   overriding `DefaultGameUserSettings.ini`. Relevant for GPU-instance sizing.

---

## 8. Known architecture mismatches vs. the stated target

| # | Mismatch | Detail |
|---|---|---|
| 1 | About 85 % of the C++ module is the furniture configurator, not the multiplayer flow | `Source/awsTutorial/` is 14 767 lines; roughly 60 of those lines serve login / session / multiplayer |
| 2 | Region latency measured from the wrong machine | `Icmp` pings from the EC2 GPU instance, not from the user's browser |
| 3 | Pixel Streaming compiled and staged into the dedicated server | `awsTutorial.Build.cs` adds `PixelStreaming` unconditionally |
| 4 | The browser-side counterpart of the PS data-channel protocol is not in this repo | `Samples/PixelStreaming/WebServers` is stock Epic; no `MaxiMall*` handlers |
| 5 | Large amounts of FPS-demo content (weapons, projectiles, AI, friend system, matchmaking, UGC) remain from the original template | see `docs/AUDIT_2026-08-21_FINDINGS.md`, Cleanup section |
| 6 | Configurator state is world-global, not per-room | `USaveSystemWidget` saves/loads **all** booths in the level and pushes them through a validation-free Server RPC, so one player's "Load" rewrites the room for everyone |

---

## 9. Repository size snapshot (2026-08-21)

| Directory | Size | Committed? |
|---|---|---|
| `Plugins/` | 30 GB | yes (3.5 GB of it is `awsSDK/Source/ThirdParty`) |
| `Intermediate/` | 11 GB | should not be |
| `Saved/` | 6.7 GB | should not be |
| `Content/` | 4.8 GB | yes |
| `Binaries/` | 2.7 GB | partially |
| `.vs/` | 1.9 GB | should not be |
| `LinuxClient/` | 1.7 GB | staged build output |
| `LinuxServer/` | 423 MB | staged build output |
| `Samples/` | 3.2 MB | stock Epic PS web servers |
| `Source/` | 715 KB | yes |
