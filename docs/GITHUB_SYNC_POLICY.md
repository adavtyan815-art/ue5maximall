# GitHub Sync Policy — awsTemplate_GameLift

> **Scope**: governs every interaction between the local Unreal project at
> `D:\awsTemplate_GameLift` and the GitHub repository
> `https://github.com/adavtyan815-art/ue5maximall`.
>
> **Precedence**: this document overrides ad-hoc task instructions. See §5.

---

## 1. The situation this protects

- `D:\awsTemplate_GameLift` is **Artur's working Unreal Engine project**. It contains
  large machine-specific state — `Content/`, `Plugins/`, `Config/`, `Binaries/`,
  `Intermediate/`, `Saved/`, `DerivedDataCache/`, `awsTutorial.uproject`,
  `awsTutorial.sln`, and local `Source/*.Target.cs` files.
- Narek develops on a **different machine with a different Unreal setup**. The two
  environments deliberately differ.
- The `dev` branch on GitHub is **source-only**. It tracks exactly two top-level paths:
  `Source/` and `docs/`. It does **not** contain `Content`, `Plugins`, `Config`,
  the `.uproject`, or the `Target.cs` files — by design, because those differ per machine.

**Therefore: `dev` is not a snapshot of the project. Making the project match `dev`
destroys the project.**

## 2. The rules

1. `D:\awsTemplate_GameLift` is a **working Unreal project, not a git checkout**. It must
   never be converted into a mirror of any branch.
2. It must **never contain a `.git` directory**. `git init` there is forbidden. If one
   appears, stop and report it.
3. All GitHub access happens in a **temporary clone outside the project tree**, discarded
   afterwards. Never copy `.git` into the project.
4. Only files under **`dev:Source/`** may be applied, and only into
   **`D:\awsTemplate_GameLift\Source\`**.
5. Apply means **per-file copy**: overwrite matching files in place, add new files.
   **Never delete or clear first.** Local-only files under `Source/` remain untouched.
6. **Nothing outside `Source/` is written** unless the user names the specific paths in
   that same request.
7. **Permanently forbidden**: `git reset` (any form), `git clean`, `git stash`, `git init`,
   full-tree `checkout`/`restore`/`switch`, branch switching, `robocopy /MIR` or `/PURGE`,
   `rsync --delete`, and recursive deletion of project directories.
8. **Mandatory pre-flight**: verify and state the `dev` commit hash; classify every file as
   *differs* / *new* / *identical* (batch hash comparison is sufficient); show the plan
   before writing.
9. **Mandatory post-flight**: list exactly which files were overwritten and added, and
   verify every copied file is byte-identical to `dev`. The guarantee that nothing outside
   `Source/` changed is **structural** in the approved procedure (it executes only
   per-file copies to paths enumerated from `dev:Source/`); the exhaustive whole-project
   timestamp/hash proof is **deep verification**, run only on explicit request or when
   something looks wrong — not on every routine sync.
10. **A clean working tree is not a safety argument.** "Nothing uncommitted will be lost"
    says nothing about committed or untracked files that exist only on this machine.
11. **Scope discipline.** If the goal is "update the shared source," the operation must
    touch only shared source. An operation whose blast radius exceeds the stated goal is
    wrong even if it produces the desired end state.

## 3. The approved procedure

Use the `sync-source-from-dev` skill
(`.claude/skills/sync-source-from-dev/SKILL.md`). It is the only sanctioned path.
The skill runs a single audited script, `sync.sh`, in one of three modes:

| Mode | Command | When | Runtime |
|---|---|---|---|
| normal | `bash sync.sh` | every routine "update Source from dev" | seconds |
| plan only | `bash sync.sh --dry-run` | "what would change?" | seconds |
| deep verify | `bash sync.sh --deep` | explicit request, or something looks wrong | +2–5 s |

Normal mode satisfies rules 8–9. Deep mode adds the exhaustive whole-project
modified-file scan and a SHA-256 snapshot diff of `Source/`.

## 4. Enforcement layers

| Layer | Mechanism | Binding? |
|---|---|---|
| 0 | Project contains no `.git`, so tree-wide git commands cannot target it | **structural** |
| 1 | PreToolUse hook `~/.claude/hooks/guard-awstemplate.sh` blocks the commands | **mechanical** |
| 2 | `permissions.deny` in `.claude/settings.json` | mechanical (coarse) |
| 3 | `CLAUDE.md`, auto-loaded every session | advisory |
| 4 | `sync-source-from-dev` skill + `sync.sh` (aborts on `.git`, refuses non-`Source/` paths, verifies copies) | advisory |
| 5 | Pre-sync backup of every file about to be overwritten (`~/.claude/sync-cache/backups/`, last 10 syncs) | recovery |

Only layers 0–2 are true enforcement. Layers 3–5 reduce the chance of reaching them.

## 5. Override protocol

A deviation requires the user to name **the exact operation and the exact paths in the
same message**. The agent must then restate the blast radius and obtain a second
confirmation. Vague authorization — "just make it match dev", "do whatever's needed",
"bring the project up to date" — is **explicitly not sufficient**.

## 6. Incident record — 2026-08-22

A `git reset --hard a3599b19` was run against the project after it had been
`git init`-ed and committed as `bfaa2799`. Because `dev` is source-only, the reset
deleted every tracked file present locally but absent from `dev`:

| Path | Files deleted |
|---|---|
| `Plugins/` | **6,247** (incl. all 12 `.uplugin` descriptors, 5,914 headers, 51 `Build.cs`) |
| `Config/` | 8 |
| `Source/*.Target.cs` + `SOURCE_ARCHITECTURE.md` | 5 |
| `docs/` | 4 |
| `Content/` (data files) | 3 |
| `awsTemplate*.yaml` | 8 |
| `awsTutorial.uproject`, `.gitignore`, `.gitattributes` | 3 |
| **Total** | **6,278** |

Consequences: the build failed (`awsTutorialEditorTarget` not found), and every Blueprint
whose parent class lived in a deleted plugin module failed to load with
`CreateExport: Failed to load Outer for resource` — `BP_WSMMO_AI_SH` (`/Script/WebSocket`),
`BP_PendingFriend_FriendRequest_SH` (`/Script/GraphQL`, `/Script/AutoDeSerialize`),
`BP_ProcessParameters_PP` (`/Script/GameLiftServerSDK`), and others.

A second-order effect: restoring the plugin files left them **untracked**, and
UnrealBuildTool uses `git status` to build its adaptive-unity working set. All 6,247
untracked files were treated as "being edited", excluding 34 modules from unity builds
and exposing a latent missing `#include` in the vendor AWS SDK plugin
(`AWSLambdaFunctionUrlConfig.h` uses `EAWSLambdaInvokeMode` without including
`Model/AWSLambdaInvokeMode.h`). Rule 2 — no `.git` in the project — also prevents this.

**Root cause of the process failure**: `docs/UE5_GIT_WORKFLOW.md` existed and had been
read. It forbade merge/push without approval but said nothing about `reset --hard`.
Documentation alone proved insufficient, which is why layers 0–2 exist.

## 7. Relationship to `UE5_GIT_WORKFLOW.md`

`docs/UE5_GIT_WORKFLOW.md` describes the `main` / `dev` / `artur/*` / `narek/*` branching
model **for the GitHub repository**. It does not describe the local Unreal project, which
is not a repository. Where the two documents appear to conflict regarding
`D:\awsTemplate_GameLift`, **this document wins**.

---
*Policy version 1.1.0 — created 2026-08-22 following the incident in §6; amended
2026-08-24: routine syncs run the single audited `sync.sh` (batch-hash classification,
targeted pre-overwrite backups, byte-level verification of copied files); exhaustive
whole-project forensics moved to opt-in deep verification. No safety rule was removed —
rules 1–7, 10–11 are unchanged.*
