# Documentation index — awsTutorial / MaxiMall

Start here. Every document below was written or updated during the **2026-08-21 full project audit**.

## Read in this order

| # | Document | What it answers |
|---|---|---|
| 1 | [`PROJECT_ARCHITECTURE.md`](PROJECT_ARCHITECTURE.md) | What is this project? How does login → room → session → dedicated server → replication → voice → Pixel Streaming actually work, and which code implements each step? |
| 2 | [`PLUGIN_INDEX.md`](PLUGIN_INDEX.md) | Which of the 12 plugins matter, which are dead, and where is each one's detailed doc? |
| 3 | [`../Source/SOURCE_ARCHITECTURE.md`](../Source/SOURCE_ARCHITECTURE.md) | What is in the C++ module, what calls what, and what are the conventions and traps? |
| 4 | [`AUDIT_2026-08-21_FINDINGS.md`](AUDIT_2026-08-21_FINDINGS.md) | Every bug, security issue, architecture mismatch, cleanup candidate, and the prioritised remediation plan |

## Per-plugin documentation

Each plugin folder contains its own `PLUGIN_DOC.md`:

* [`Plugins/Ultimate969078bf2772V2/PLUGIN_DOC.md`](../Plugins/Ultimate969078bf2772V2/PLUGIN_DOC.md) — **awsSDK** (Cognito / GameLift / voice / GraphQL / S3) — the online backbone
* [`Plugins/EmbeddedVoiceChatPixelStreamingPlugin_UE5.6/PLUGIN_DOC.md`](../Plugins/EmbeddedVoiceChatPixelStreamingPlugin_UE5.6/PLUGIN_DOC.md) — browser mic → voice chat bridge
* [`Plugins/AutoDeSef64e1bfc0e22V2/PLUGIN_DOC.md`](../Plugins/AutoDeSef64e1bfc0e22V2/PLUGIN_DOC.md) — AutoDeSerialize
* [`Plugins/WebSocke3911c0965375V2/PLUGIN_DOC.md`](../Plugins/WebSocke3911c0965375V2/PLUGIN_DOC.md) — WebSocket
* [`Plugins/RegexpBl98e2507377d2V2/PLUGIN_DOC.md`](../Plugins/RegexpBl98e2507377d2V2/PLUGIN_DOC.md) — Regexp
* [`Plugins/Restfulh16cb1dcaf2b4V2/PLUGIN_DOC.md`](../Plugins/Restfulh16cb1dcaf2b4V2/PLUGIN_DOC.md) — Restful
* [`Plugins/QREncode783839f97b45V2/PLUGIN_DOC.md`](../Plugins/QREncode783839f97b45V2/PLUGIN_DOC.md) — QR Encode (MFA)
* [`Plugins/Enhanced2db8f2adfc28V2/PLUGIN_DOC.md`](../Plugins/Enhanced2db8f2adfc28V2/PLUGIN_DOC.md) — Enhanced Scroll Box
* [`Plugins/Clipboar9fd0aa5b4ecdV2/PLUGIN_DOC.md`](../Plugins/Clipboar9fd0aa5b4ecdV2/PLUGIN_DOC.md) — Clipboard *(removal candidate)*
* [`Plugins/icmppingd30c78843dcfV2/PLUGIN_DOC.md`](../Plugins/icmppingd30c78843dcfV2/PLUGIN_DOC.md) — Icmp *(removal candidate)*
* [`Plugins/RuntimeDataTable_5.4/PLUGIN_DOC.md`](../Plugins/RuntimeDataTable_5.4/PLUGIN_DOC.md) — Runtime DataTable *(unused)*
* [`Plugins/UBIKSolver-main/PLUGIN_DOC.md`](../Plugins/UBIKSolver-main/PLUGIN_DOC.md) — UBIK Solver *(unused)*

## Pre-existing documents (kept, not modified by the audit)

| Document | Note |
|---|---|
| [`AWSTUTORIAL_SOURCE_GUIDE.md`](AWSTUTORIAL_SOURCE_GUIDE.md) | Per-file C++ reference. Still accurate as a file listing; `SOURCE_ARCHITECTURE.md` adds runtime flow, replication and defects on top of it |
| [`AI_ACCESS_AND_PERMISSIONS.md`](AI_ACCESS_AND_PERMISSIONS.md) | Operational rules for AI agents in this repo |
| [`UE5_GIT_WORKFLOW.md`](UE5_GIT_WORKFLOW.md) | Git workflow for UE5 binary assets |
| [`../Source/awsTutorial/ColorCatalog/doc/ColorCatalogSystem.md`](../Source/awsTutorial/ColorCatalog/doc/ColorCatalogSystem.md) | Colour-catalog subsystem note. **Read `PKG-01` before trusting it** — the catalogs do not load in the packaged Linux client |

## The five things a new engineer most needs to know

1. **The C++ module is a furniture configurator.** The multiplayer/login flow lives in Blueprints on
   top of the `awsSDK` plugin. Only five C++ functions touch the GameLift session path.
2. **`Content/Data/Colors/*.json` is not staged**, so the colour catalog is empty in the packaged
   Linux client. Root cause is an ini section in the wrong file plus a truncated line (`PKG-01`).
3. **Voice chat signalling goes through the dedicated server**, while voice *media* is peer-to-peer
   between the two EC2 instances. Two S1 bugs in the PS↔voice bridge currently corrupt the audio path
   (`VOICE-01`, `VOICE-02`).
4. **17 of 19 Server RPC `_Validate` bodies are `return true`** (`NET-01`).
5. **The browser half of the Pixel Streaming protocol is not in this repository** (`ARCH-03`).
   The message contract is documented in `PROJECT_ARCHITECTURE.md` §2.2.
