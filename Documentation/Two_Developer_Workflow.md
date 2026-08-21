# Two-Developer Git Workflow (Narek & Artur)

## 1. Roles & Engine Environments

| Developer | Machine | Unreal Engine | Primary Responsibility |
|---|---|---|---|
| **Narek** | **PC1** | **UE 5.3** | Functional C++ logic, UI/Widgets, Blueprints, local PIE testing |
| **Artur** | **PC2** | **UE 5.6** | Production Client/Dedicated Server builds, Linux packaging, release gatekeeper |

---

## 2. One-Time Setup on Each Machine

### On PC1 (Narek - UE 5.3):
After cloning or pulling the repository:
```cmd
cd Build
setup_ue53.bat
```
*(Instantiates the UE 5.3-compatible Target.cs files into `Source/`)*

### On PC2 (Artur - UE 5.6):
After cloning or pulling the repository:
```cmd
cd Build
setup_ue56.bat
```
*(Instantiates the proven UE 5.6 production Target.cs files into `Source/`)*

---

## 3. Branching & Pull Request Workflow

```
main (Production Release Branch - Locked)
  ^
  | (Promoted only after full PC2 UE 5.6 Linux build test)
dev (Shared Integration & Testing Branch)
  ^                    ^
  | (PR & Review)      | (PR & Review)
narek/feature-*     artur/feature-*
```

### Daily Development Flow:
1. **Always branch from latest `dev`**:
   ```bash
   git checkout dev
   git pull origin dev
   git checkout -b narek/my-new-feature
   ```
2. **Make C++ and Content changes**:
   - Add or edit files inside `Source/awsTutorial/`.
   - Add or edit assets in `Content/`.
3. **Local Testing**:
   - Compile locally in Visual Studio / UBT.
   - Test in Unreal Editor PIE.
4. **Commit & Push**:
   ```bash
   git add Source/awsTutorial/ Content/ Config/
   git commit -m "feat(module): description of feature"
   git push -u origin narek/my-new-feature
   ```
5. **Open Pull Request to `dev`**:
   - Peer review: Artur reviews Narek's PRs; Narek reviews Artur's PRs.
   - Merge into `dev`.

---

## 4. Promotion from `dev` to `main` (Production Release Gate)

1. **PC2 Build Verification**:
   Before merging `dev` into `main`, Artur compiles on PC2 (UE 5.6):
   - `awsTutorialServer Linux Development / Shipping`
   - `awsTutorialClient Linux / Win64 Development / Shipping`
2. **Sign-Off & Merge**:
   - Open PR: `dev` $\rightarrow$ `main`.
   - Both developers sign off $\rightarrow$ merge into `main`.
3. **Deployment**:
   - Official production builds and AWS deployments are created **only from `main` on PC2**.

---

## 5. Unreal Binary Asset (.uasset / .umap) Ownership Rule

- Unreal binary assets cannot be merged automatically by Git.
- **Rule**: Single-writer ownership per asset. Always communicate in the team chat before modifying shared Blueprints (e.g. `BP_ShowroomBooth.uasset`) or maps (`MaxiMall.umap`).
- Keep business logic in C++ (`Source/awsTutorial/`) to minimize binary `.uasset` edits.

---

## 6. Rollback Procedure

If a bad commit reaches `dev` or `main`:
1. Do **not** force-push (`git push --force`).
2. Create a clean revert commit:
   ```bash
   git checkout dev
   git pull origin dev
   git revert <commit-hash> -m 1
   git push origin dev
   ```
