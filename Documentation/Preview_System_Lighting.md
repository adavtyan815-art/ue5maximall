# Technical Reference: Preview System & Isolated Studio Lighting

This document explains the C++ and Blueprint configurations for the isolated local preview scene.

---

## 1. Client-Side Isolated Lighting Setup
To ensure the inspected furniture model is clearly lit in an otherwise pitch-black staging zone (10,000 units high), the `AFurniturePreviewActor` constructs its own isolated studio lights in C++:
* **Key Light (`USpotLightComponent`)**: 
  - Positioned at `(-300.f, -300.f, 300.f)` offset from the actor root.
  - Oriented to point directly down-forward at the center of the furniture model.
  - Configured with `InnerConeAngle = 30.f`, `OuterConeAngle = 50.f`, and `Intensity = 80000.f` Lumens.
  - `bCastShadows` is set to `true` to create realistic self-shadowing between components (e.g., faucet casting shadow on the sink).
* **Fill Light (`UPointLightComponent`)**:
  - Mounted directly onto the viewport `UCameraComponent` (moves with the camera orbit).
  - Acts as a constant headlight ensuring the visible side is never dark.
  - Intensity is dynamically scaled with zoom, defaulting to a baseline of `40000.f` Lumens with shadows disabled (`bCastShadows = false`) to soften key shadows.

---

## 2. Lighting Channel Isolation
To prevent the preview lights from illuminating the main multiplayer level (and vice versa):
* Both lights (`KeyLight`, `FillLight`) and all seven furniture mesh components have their **Lighting Channels** configured to use **Channel 1 only**, with **Channel 0 disabled**:
  ```cpp
  LightingChannels.bChannel0 = false;
  LightingChannels.bChannel1 = true;
  LightingChannels.bChannel2 = false;
  ```
Since all actors in the main level use the default Lighting Channel 0, the preview scene's lights and meshes have zero influence on or from the rest of the world.

---

## 3. Dynamic Zoom Headlight Intensity Compensator
Because point lights use inverse-squared distance falloff ($I \propto 1/d^2$), zooming the camera close makes the headlight extremely bright, while zooming far out makes it dark.

To solve this, a dynamic scaler math equation is applied in C++ whenever the camera zooms or resets:
$$\text{HeadlightIntensity} = \text{BaseFillIntensity} \times \left(\frac{\text{CurrentZoomLength}}{\text{ReferenceZoomDistance}}\right)^2$$

This cancels out the distance-attenuation falloff, maintaining a perfectly uniform headlight exposure level on the furniture surface across all camera distances.

---

## 4. Locked Camera Exposure (Anti-Flicker)
To prevent the visual adaptation delay (the 1-second dimming flash) when transitioning into preview mode, the preview camera locks the renderer's exposure.

Min and Max brightness post-process properties are set to the exact same value in the camera component's defaults:
```cpp
Camera->PostProcessSettings.bOverride_AutoExposureMinBrightness = true;
Camera->PostProcessSettings.bOverride_AutoExposureMaxBrightness = true;
Camera->PostProcessSettings.AutoExposureMinBrightness = 1.0f; // locked value
Camera->PostProcessSettings.AutoExposureMaxBrightness = 1.0f;
```
This forces the renderer to bypass exposure adaptation, resulting in a clean, instant transition into preview mode. Designers can adjust the overall brightness in the Blueprint details panel by changing the `Min EV100` and `Max EV100` properties of the `Camera` component.

---

## 5. WorldInPlace ViewMode — E-Commerce Camera Orbit System

This section covers the `WorldInPlace` mode, where the furniture model remains **100% static in its real-world room position** and the camera orbits around it.

### 5.1 Camera Orbit Architecture
- `SpringArm` is attached to `TargetComponent->Bounds.Origin` (geometric world-space center of the selected mesh).
- `SpringArm->bDoCollisionTest = false` for unrestricted 360° orbit.
- Initial spawn direction: `(FocusPivot - CharCamLoc).GetSafeNormal().Rotation()` — camera starts **in the room in front of the object**.
- `SpringArm->UpdateChildTransforms()` called immediately after setup for instant camera sync.
- Adaptive initial distance: `MeshBoundsRadius × 2.5` (clamped 40–500 cm).
- Zoom clamped between `MeshBoundsRadius × 0.8` and `MeshBoundsRadius × 5.0`.

### 5.2 Physical Depth of Field — DISABLED
All `bOverride_DepthOfField*` overrides (`FocalDistance`, `FocalRegion`, `Fstop`, `SensorWidth`, `NearBlurSize`, `FarBlurSize`) are explicitly set to `false`. The selected product is **100% crystal sharp** with zero lens blur at any zoom distance.

### 5.3 Custom Depth Stencil 250 — Target Product Isolation
`SetRenderCustomDepth(true)` and `SetCustomDepthStencilValue(250)` are set on the focused mesh and its sub-components on `SetFocusComponent`. All other meshes have custom depth disabled (`false`). Custom depth is fully restored in `EndPlay()`.

### 5.4 Dynamic Post-Process Material (MID)
- A `UMaterialInstanceDynamic` (`StencilIsolationMID`) is created at runtime from the designer-assigned `StencilIsolationMaterialParent` (exposed as `EditAnywhere` in the `Preview Config` category).
- Scalar parameters set via C++: `IsolationFade = 0.8`, `TargetStencil = 250.0`.
- The MID is bound to `Camera->PostProcessSettings.WeightedBlendables` on enter and removed in `EndPlay()`.
- **PPM Logic**: `CustomStencil == 250` → 100% sharp original color (selected mesh). `CustomStencil != 250` → multiply by 0.2 (80% dark/fade, background environment isolation).

### 5.5 Environment Vignette Fallback
`VignetteIntensity = 0.8f` applied as a secondary environment isolation mechanism when no PPM is assigned.

### 5.6 Wall Hiding (100 cm Sphere Sweep)
`SweepMultiByChannel` (100 cm sphere radius, `ECC_WorldStatic`) sweeps from `FocusPivotWorld` to `Camera->GetComponentLocation()`. Any non-furniture component hit is hidden (`SetVisibility(false)`) while `SetCastHiddenShadow(true)` is preserved so room lighting and shadows on the furniture remain intact. All hidden components are cached in `WIP_CachedHiddenWallComponents` and restored in `EndPlay()`.

### 5.7 Per-Frame Tick
`PrimaryActorTick.bCanEverTick = true` enabled in constructor. `Tick()` calls `WIP_ApplyDoF()` and `WIP_UpdateWallOcclusion()` every frame in `WorldInPlace` mode to keep focal distance and wall hiding current as the camera orbits.

### 5.8 Lumen Reflection Override
`LumenReflectionQuality = 2.0` overridden in PostProcess settings for high-quality Chrome/Gold/Brass metallic reflections.
