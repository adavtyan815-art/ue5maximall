# AI Access & Permissions Operational Guide

> **Purpose**: Definitive operational boundaries, autonomous permission scopes, human-approval requirements, and security governance for AI coding agents across the repository.  
> **Security Policy**: Zero plaintext credentials or secrets in Git. Describe credential mechanisms and locations, never secret values.  

---

## 1. Core Operational Directives for AI Agents

> [!IMPORTANT]
> ### STRICT TASK SCOPE ENFORCEMENT
> The AI agent must perform **only the operations explicitly requested by the current user task**.
> - It must **not** independently execute builds, tests, cleanups, synchronizations, migrations, deployments, Git operations, or refactors merely because they seem useful or standard.
> - If an operation is not part of the active user request, the AI must ask for explicit instructions before proceeding.

---

## 2. Permission Tier Model

```mermaid
flowchart TD
    subgraph Tier1 [Tier 1: Safe Read-Only (Autonomous / Unrestricted)]
        R1[Read source code, configs, & documentation]
        R2[Inspect Git status, branch list, & commit logs]
        R3[Inspect compiler diagnostic outputs & reports]
    end

    subgraph Tier2 [Tier 2: Autonomous Local Modifications]
        W1[Edit source and documentation files requested by task]
        W2[Create local commits on working/feature branches]
    end

    subgraph Tier3 [Tier 3: Operations Requiring Explicit Human Approval]
        A1[Delete files or directories from the repository]
        A2[Merge branches]
        A3[Push commits or branches to remote repositories]
        A4[Modify repository structure or top-level folders]
    end

    subgraph Tier4 [Tier 4: Sensitive Production Operations]
        P1[AWS EC2 instance lifecycle: start / stop / terminate]
        P2[Modify IAM roles, access policies, or Security Groups]
        P3[Deploy packages or modify live production environments]
    end
```

---

## 3. Detailed Operational Permissions

### 3.1 Tier 1: Safe Read-Only (Autonomous)
- **Filesystem & Code**: Read source files (`.cpp`, `.h`, `.cs`, `.ini`, `.json`, `.ts`, `.bat`, `.md`).
- **Git State**: Query `git status`, `git diff`, `git log`, `git branch -a`, `git remote -v`.
- **Diagnostics**: Read existing build logs, diagnostic summaries, and test reports.

### 3.2 Tier 2: Local Modifications (Autonomous)
- **Source Editing**: Modify only the source and documentation files specifically targeted by the task.
- **Local Commits**: Create local Git commits only on developer feature branches when requested.

### 3.3 Tier 3: Operations Requiring Human Confirmation
- **File Deletions**: Deleting any file or directory requires forensic verification of redundancy and explicit user confirmation.
- **Branch Merges**: Merging feature branches into shared branches.
- **Remote Operations**: Pushing local branches or tags to remote repositories (`git push`).
- **Structural Changes**: Adding, deleting, or renaming top-level project folders or module roots.

### 3.4 Tier 4: Sensitive Production Operations (Strict Confirmation)
- **Cloud Infrastructure**: Starting, stopping, or terminating AWS EC2 instances outside established automated pipelines.
- **Security & Network**: Altering AWS Security Groups, VPC configurations, or IAM permissions.
- **Production Deployments**: Publishing production binaries or modifying live host services.

---

## 4. File Deletion & Redundancy Rules

1. **Forensic Redundancy Requirement**:
   - A file may only be deleted if it is forensically proven to be already tracked in Git, an obsolete duplicate, or a regenerable temporary file.
2. **Unique Work Protection**:
   - If unique or uncommitted work is detected in an untracked file, the AI must preserve it and report it to the user.
3. **No Speculative Deletions**:
   - Never delete files "for cleanup" unless explicitly instructed by the current task.

---

## 5. Credential Management & Security Policies

> [!CAUTION]
> ### ZERO SECRETS POLICY
> 1. **No Embedded Credentials in Git**:
>    - Remote URLs must always use clean HTTPS URLs (`https://github.com/<org>/<repo>.git`) without embedded Personal Access Tokens (PATs) or basic authentication strings (`https://<token>@github.com/...`).
> 2. **No Hardcoded Secrets**:
>    - Never write passwords, AWS access keys, secret keys, SSH private keys, session tokens, or `.env` content into code, markdown files, or commit messages.
> 3. **Safe Authenticated Access**:
>    - **GitHub**: Rely on the system Git Credential Manager or local SSH agent. Never pass raw tokens via command-line arguments or log them.
>    - **AWS**: Rely on standard AWS CLI credential chains (system environment variables or `~/.aws/credentials`). Never print or echo secrets to transcripts.
>    - **SSH Keys**: Private keys (`.pem` / `.id_rsa`) must remain in secure local directories with restricted filesystem permissions.

---
*Document Version: 3.0.0 — AI Access & Permissions Operational Guide*
