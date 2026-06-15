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
