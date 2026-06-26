# UI System Setup and Integration Guide

This document provides a detailed, step-by-step guide on how to build, configure, and troubleshoot the modular UI and 3D preview systems from scratch, as well as how to integrate them into new or existing projects.

---

## 1. Class Architecture & Blueprint Assignments

To maintain separation of concerns and designer freedom, the system uses a hybrid C++ and Blueprint architecture:

### 1.1 C++ Classes to Create
Create the following classes in your C++ source directory:

1. **`UFurnitureOptionListener`** (inherits from `UObject`):
   - **Purpose**: A lightweight, memory-managed listener object that dynamically binds to buttons generated at runtime (Size pills, color thumbnails). It translates raw button clicks, hovers, and unhovers into typed configurator callbacks.
2. **`UConfiguratorMainWidget`** (inherits from `UUserWidget`):
   - **Purpose**: The main backend controller for the UI panels.
   - **Key Bindings (meta = (BindWidget))**:
     - `UButton* Btn_Viewmode`: Enters the isolated 3D preview.
     - `UButton* Btn_CloseUI`: Exposes close/back button logic.
     - `UButton* Txt_BtnURL`: Displays metadata/checkout URL links.
     - `UPanelWidget* Size_Container`: Dynamic grid layout parent.
     - `UPanelWidget* Color_Container`: Dynamic color selector parent.
     - `UTextBlock* Txt_SKU` and `Txt_ProductName_1`: Dynamic product metadata displays.
     - `UTextBlock* Txt_Warning` (meta = (BindWidgetOptional)): Fallback warning text.
3. **`UAwsTutorial_PlayerController`** (inherits from `APlayerController`):
   - **Purpose**: Handles client-side local input, line traces, mouse capture mode transitions, and the lifecycle (spawn/teardown) of the isolated preview actor.
4. **`AFurniturePreviewActor`** (inherits from `AActor`):
   - **Purpose**: A local, non-replicated studio environment actor containing mesh components (Cabinet, Countertop, Sink, Faucet, Mirror), lighting components (KeyLight, FillLight), spring arm, camera, and studio backdrop.

### 1.2 Blueprint Assets to Create & Reparent
Create these assets in your project's Content Browser and configure their parents:

| Blueprint Name | Base C++ Class Parent | Settings to Assign (Class Defaults) |
| :--- | :--- | :--- |
| **`BP_awsTutorial_PlayerController`** | `UAwsTutorial_PlayerController` | - **Main Widget Class**: `WBP_PreviewWindow`<br>- **Viewmode Overlay Class**: `WBP_ViewmodeOverlay`<br>- **Preview Actor Class**: `BP_FurniturePreviewActor` |
| **`BP_FurniturePreviewActor`** | `AFurniturePreviewActor` | - **Base Fill Intensity**: `40000.f`<br>- **Key Light Intensity**: `80000.f`<br>- **Camera Exposure**: Locked to `1.0f` to prevent flashing. |
| **`WBP_PreviewWindow`** | `UConfiguratorMainWidget` | - Bind UMG button/text items to their matching C++ variable names.<br>- Expose `GridItemWidth` and `GridItemHeight` in Details. |
| **`WBP_ViewmodeOverlay`** | `UViewmodeOverlayWidget` | - Exposes "Exit Preview" / "Reset Camera" overlays during isolated view. |

---

## 2. From Absolute Zero: New Project Checklist

Follow this chronological checklist when setting up a brand new project:

- [ ] **Step 2.1: Configure Module Dependencies**:
  - Open `Source/[ProjectName]/[ProjectName].Build.cs` and add `"UMG"`, `"Slate"`, and `"SlateCore"` to your dependency lists.
  - Add `ModuleDirectory` to the public include paths so folder structures resolve cleanly.
- [ ] **Step 2.2: Implement Data Structures**:
  - Create `FurnitureTypes.h` containing all data models, enums (`EFurnitureComponentType`, `EOptionType`, `ECountertopType`), and DataTable structures (`FFurnitureProductRow`, `FFurnitureCountertopRow`).
- [ ] **Step 2.3: Implement the C++ Preview Actor & Player Controller**:
  - Write `FurniturePreviewActor.h/.cpp` (handles scene spawning and material application).
  - Write `awsTutorial_PlayerController.h/.cpp` (handles mouse clicks, double-click doors animation, and UI toggles).
- [ ] **Step 2.4: Implement C++ Widget Controllers**:
  - Write `ConfiguratorMainWidget.h/.cpp` implementing the dynamic grid building.
- [ ] **Step 2.5: Generate Project Files & Compile**:
  - Right-click `.uproject` $\rightarrow$ **Generate Visual Studio project files**, then build the project.
- [ ] **Step 2.6: Create and Reparent Blueprint Assets**:
  - Create the blueprints listed in Section 1.2 and reparent them to their new C++ classes.
- [ ] **Step 2.7: Set up collision on Meshes**:
  - Ensure all furniture static meshes have their collision settings configured to **Block the Visibility Channel**, enabling player mouse traces.
- [ ] **Step 2.8: Assign GameMode Defaults**:
  - In your GameMode settings, assign `BP_awsTutorial_PlayerController` as the active Player Controller class.

---

## 3. Integration & Troubleshooting for Existing Projects

Use these rules when importing files into existing projects or solving compilation blocks:

### 3.1 Unresolved External Symbol errors (Linker Failures)
- **Problem**: You see compiler errors referencing `UUserWidget`, `USizeBox`, `UButton`, or `FSlateApplication` as unresolved symbols.
- **Cause**: The compiler compiles the headers, but the linker cannot bind Slate or UMG library binaries.
- **Solution**: Open `[ModuleName].Build.cs` and add UI dependencies:
  ```csharp
  PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "InputCore", "UMG" });
  PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore" });
  ```

### 3.2 Cannot open include file: "FurnitureConfigurator/..." (C1083)
- **Problem**: The compiler complains it cannot find configuration headers.
- **Cause**: The target project's build settings do not include the module's own source folder in the include directories.
- **Solution**: Open `[ModuleName].Build.cs` and add the `ModuleDirectory` to public include paths:
  ```csharp
  PublicIncludePaths.AddRange(new string[]
  {
      ModuleDirectory
  });
  ```
  *(Make sure to delete `Intermediate/` and `Binaries/` folders and regenerate project files to clear the compiler cache afterwards).*

### 3.3 Found "classawsTutorial_API" when expecting class (Parser Error)
- **Problem**: Unreal Header Tool (UHT) crashes during compilation.
- **Cause**: Search-and-replace naming operations joined the `class` keyword and the API export macro together without a space.
- **Solution**: Search your files for the typo and add the missing space:
  ```cpp
  // WRONG: classawsTutorial_API UFurnitureEditorHelper
  // RIGHT: class AWSTUTORIAL_API UFurnitureEditorHelper
  ```

### 3.4 Right-Click Widget does not open
- **Problem**: Playing in Editor (PIE) and right-clicking furniture yields no response.
- **Cause**: Missing class default assignments, deactivated input routing, or missing collision profiles.
- **Solution**:
  1. Open your player controller blueprint defaults and verify **Main Widget Class** is not set to `None`.
  2. Verify that your player controller Blueprint Event Graph has the **Right Mouse Button** mapped to **Trace Furniture Component** $\rightarrow$ *(if True)* $\rightarrow$ **Toggle Configurator UI**.
  3. Inspect your furniture mesh assets and make sure their **Collision Presets** block the **Visibility** channel.

### 3.5 Preview Studio is too dark or bright
- **Problem**: The isolated 3D viewport preview is over-exposed or dark.
- **Cause**: The key spotlight intensity, camera-mounted pointlight, or camera exposure values require adjustments.
- **Solution**:
  1. Open `BP_FurniturePreviewActor` and select **KeyLight** (SpotLight component) $\rightarrow$ adjust its **Intensity** (defaults to `80000.f` lumens).
  2. Select the root component of `BP_FurniturePreviewActor` and adjust **Base Fill Intensity** (defaults to `40000.f` lumens).
  3. Select the **Camera** component and adjust **Exposure Bias** (Post Process Settings) to scale overall viewport brightness up/down.
