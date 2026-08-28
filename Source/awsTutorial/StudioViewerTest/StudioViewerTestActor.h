// Copyright MaxiMall Project. All Rights Reserved.
//
// STANDALONE STUDIO VIEWER TEST — isolated proof-of-concept, safe to delete.
//
// ⚠ FROZEN REFERENCE — DO NOT UPDATE. The production Studio ViewMode
// (StudioStageActor + FurniturePreviewActor studio path + ViewmodeOverlayWidget)
// has superseded this sandbox and deliberately diverges from it (fixed-pivot
// pan, UI pivot marker, UI reset button, fades, touch). Kept only because
// BP_StudioViewerTest references the class; do not port changes in either
// direction or use it to judge current production behavior.
//
// Goal: prove that UE can reproduce the neutral product-presentation look AND the
// interaction model of the web viewer (viewer_test.html / model-viewer): flat dark
// gray background, shadowless soft studio lighting, neutral tone response, fixed
// exposure, no screen-space extras, plus orbit / zoom / pan around a centered model.
//
// Usage:
//   1. Create an EMPTY level.
//   2. Place any StaticMeshActor (the test mesh) anywhere in the level.
//   3. Place this actor (or a Blueprint child of it).
//   4. PIE -> F enters/exits the studio view.
//        Left-drag  = orbit (with fling inertia)   Wheel = zoom (toward cursor)
//        Right/Middle-drag = pan                   R     = reset framing
//      The cursor stays visible and is hidden only while a drag is held.
//      After a few idle seconds the model auto-rotates (turntable).
//
// Optional assets (both under /Game/StudioViewerTest/, both auto-detected):
//   M_PBRNeutral — post-process material (blendable) implementing the Khronos
//                  PBR Neutral tone mapper; picked up automatically or assign
//                  ToneMapperMaterial explicitly.
//   A UTextureCube HDRI of model-viewer's "neutral" environment — assign it to
//                  ReferenceCubemap and set EnvironmentMode to ReferenceCubemap.
//
// This file intentionally has ZERO dependencies on the project's AR / ViewMode /
// configurator / preview / player-controller / widget systems. Engine types only.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "StudioViewerTestActor.generated.h"

class AStaticMeshActor;
class ARectLight;
class ACameraActor;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UTextureCube;
class USkyLightComponent;
class SWidget;

/** Where the studio's ambient light + reflections come from. */
UENUM()
enum class EStudioEnvironmentMode : uint8
{
    /** Capture the emissive softbox rig itself (self-contained, no assets needed). */
    CapturedStudio,
    /** Use ReferenceCubemap (e.g. model-viewer's "neutral" HDRI imported as a TextureCube). */
    ReferenceCubemap,
};

UCLASS(HideCategories = (Collision, Physics, Replication, Networking, Actor, Cooking))
class AWSTUTORIAL_API AStudioViewerTestActor : public AActor
{
    GENERATED_BODY()

public:
    AStudioViewerTestActor();

    // ── Look calibration (all tunable on the placed instance) ────────────────

    /** Background color, sRGB (viewer_test.html uses #3a3a3a). */
    UPROPERTY(EditAnywhere, Category = "Studio|Backdrop")
    FColor BackdropColorSRGB = FColor(0x3A, 0x3A, 0x3A);

    /** Boost the backdrop emissive by the inverse of the camera exposure, so the
        backdrop lands on-screen at exactly BackdropColorSRGB regardless of LockedEV100. */
    UPROPERTY(EditAnywhere, Category = "Studio|Backdrop")
    bool bCompensateBackdropExposure = true;

    /** Locked exposure compensation in stops (auto-exposure is pinned off; physical
        camera exposure is disabled, so scene color is multiplied by exactly 2^this).
        Default log2(0.85) ~= -0.23 matches the web viewer's exposure="0.85". */
    UPROPERTY(EditAnywhere, Category = "Studio|Exposure", meta = (UIMin = "-4", UIMax = "6"))
    float LockedEV100 = -0.23f;

    /** 0 = fully neutral (linear->sRGB, closest to the viewer's color-true look), 1 = standard UE filmic. */
    UPROPERTY(EditAnywhere, Category = "Studio|Exposure", meta = (ClampMin = "0", ClampMax = "1"))
    float ToneCurveAmount = 0.0f;

    /** Optional post-process blendable replacing/adjusting the tone mapping (e.g. a
        Khronos PBR Neutral material). If unset, /Game/StudioViewerTest/M_PBRNeutral
        is loaded automatically when it exists. */
    UPROPERTY(EditAnywhere, Category = "Studio|Exposure")
    TObjectPtr<UMaterialInterface> ToneMapperMaterial;

    /** Illuminance the key softbox delivers at the subject; fills/rim are ratios of it.
        The studio runs at unit exposure (1 scene unit ~ display white), so useful
        values are single digits: ~4 puts a key-lit white diffuse near full white. */
    UPROPERTY(EditAnywhere, Category = "Studio|Lighting", meta = (UIMin = "0.1", UIMax = "20"))
    float KeyIlluminanceLux = 4.0f;

    UPROPERTY(EditAnywhere, Category = "Studio|Lighting", meta = (ClampMin = "0", ClampMax = "2"))
    float FillLeftRatio = 0.45f;

    UPROPERTY(EditAnywhere, Category = "Studio|Lighting", meta = (ClampMin = "0", ClampMax = "2"))
    float FillRightRatio = 0.25f;

    UPROPERTY(EditAnywhere, Category = "Studio|Lighting", meta = (ClampMin = "0", ClampMax = "2"))
    float RimRatio = 0.6f;

    /** Softbox size as a fraction of the subject's bounding-sphere radius. */
    UPROPERTY(EditAnywhere, Category = "Studio|Lighting", meta = (UIMin = "0.5", UIMax = "6"))
    float SoftboxSizeFactor = 3.0f;

    /** Emissive luminance of the key softbox panel as seen in reflections (others
        scale by their light ratio). Panels are invisible to the camera and exist
        only in the captured environment, like the web viewer's HDRI panels. */
    UPROPERTY(EditAnywhere, Category = "Studio|Lighting", meta = (UIMin = "0", UIMax = "10"))
    float SoftboxPanelBrightness = 3.0f;

    /** Light distance as a multiple of the subject's bounding-sphere radius. */
    UPROPERTY(EditAnywhere, Category = "Studio|Lighting", meta = (UIMin = "1.2", UIMax = "8"))
    float LightDistanceFactor = 2.6f;

    /** Soft self-shadowing from the key & overhead softboxes. Gives diffuse
        materials (wood, fabric) form and depth that the fully shadowless web
        viewer cannot produce; disable for an exact shadowless match. */
    UPROPERTY(EditAnywhere, Category = "Studio|Lighting")
    bool bKeyLightShadows = true;

    /** Screen-space ambient occlusion amount — grounds crevices and contact areas
        on diffuse materials (the web viewer has none; 0 disables). */
    UPROPERTY(EditAnywhere, Category = "Studio|Lighting", meta = (ClampMin = "0", ClampMax = "1"))
    float AmbientOcclusionAmount = 0.5f;

    /** Ambient/reflection source. ReferenceCubemap needs ReferenceCubemap set. */
    UPROPERTY(EditAnywhere, Category = "Studio|Lighting")
    EStudioEnvironmentMode EnvironmentMode = EStudioEnvironmentMode::CapturedStudio;

    /** HDRI cubemap used when EnvironmentMode == ReferenceCubemap. */
    UPROPERTY(EditAnywhere, Category = "Studio|Lighting")
    TObjectPtr<UTextureCube> ReferenceCubemap;

    /** SkyLight intensity applied in ReferenceCubemap mode. */
    UPROPERTY(EditAnywhere, Category = "Studio|Lighting", meta = (UIMin = "0", UIMax = "10"))
    float ReferenceCubemapIntensity = 1.0f;

    // ── Camera / interaction ────────────────────────────────────────────────

    /** Starting orbit angles (viewer_test.html opens at ~30 deg yaw, slightly above). */
    UPROPERTY(EditAnywhere, Category = "Studio|Camera")
    float StartYawDegrees = 30.0f;

    UPROPERTY(EditAnywhere, Category = "Studio|Camera", meta = (UIMin = "-85", UIMax = "85"))
    float StartPitchDegrees = -10.0f;

    /** Narrow product-viewer FOV (model-viewer uses ~30 deg). */
    UPROPERTY(EditAnywhere, Category = "Studio|Camera", meta = (UIMin = "20", UIMax = "90"))
    float CameraFOV = 32.0f;

    /** Framing padding around the subject at the default distance (1.0 = tight fit;
        the web viewer opens at ~105% of its tight-fit distance). */
    UPROPERTY(EditAnywhere, Category = "Studio|Camera", meta = (UIMin = "1.0", UIMax = "2.5"))
    float FramingMargin = 1.1f;

    /** Enter/exit camera blend duration, seconds (0 = instant cut). */
    UPROPERTY(EditAnywhere, Category = "Studio|Camera", meta = (UIMin = "0", UIMax = "2"))
    float EnterBlendSeconds = 0.5f;

    /** Orbit speed. UE pre-scales raw mouse-axis deltas by the input settings'
        MouseX/Y sensitivity (0.07 by default), so ~5 here is ~0.35 deg per pixel:
        a half-screen drag turns the model ~180 deg, like the web viewer. */
    UPROPERTY(EditAnywhere, Category = "Studio|Interaction", meta = (UIMin = "0.5", UIMax = "15.0"))
    float OrbitSensitivity = 5.0f;

    /** Fraction of distance changed per wheel notch. */
    UPROPERTY(EditAnywhere, Category = "Studio|Interaction", meta = (UIMin = "0.02", UIMax = "0.5"))
    float ZoomSensitivity = 0.25f;

    /** Pan speed as a fraction of the current distance per mouse unit (pre-scaled
        by the same 0.07 input sensitivity as orbit). */
    UPROPERTY(EditAnywhere, Category = "Studio|Interaction", meta = (UIMin = "0.005", UIMax = "0.1"))
    float PanSensitivity = 0.025f;

    /** Zoom range as multiples of the auto-fit distance. */
    UPROPERTY(EditAnywhere, Category = "Studio|Interaction", meta = (UIMin = "0.1", UIMax = "1.0"))
    float MinZoomFactor = 0.35f;

    UPROPERTY(EditAnywhere, Category = "Studio|Interaction", meta = (UIMin = "1.0", UIMax = "8.0"))
    float MaxZoomFactor = 3.0f;

    /** Orbit/pan smoothing (higher = snappier; 0 = instant). */
    UPROPERTY(EditAnywhere, Category = "Studio|Interaction", meta = (UIMin = "0", UIMax = "30"))
    float CameraSmoothingSpeed = 14.0f;

    /** Zoom easing, deliberately snappier than orbit. */
    UPROPERTY(EditAnywhere, Category = "Studio|Interaction", meta = (UIMin = "0", UIMax = "40"))
    float ZoomSmoothingSpeed = 25.0f;

    /** Carry orbit momentum after releasing the drag (fling). */
    UPROPERTY(EditAnywhere, Category = "Studio|Interaction")
    bool bOrbitInertia = true;

    /** How fast the fling dies out (per-second exponential decay). */
    UPROPERTY(EditAnywhere, Category = "Studio|Interaction", meta = (UIMin = "0.5", UIMax = "12"))
    float OrbitInertiaDamping = 4.0f;

    /** Pan is clamped to this many subject bounding-sphere radii from center. */
    UPROPERTY(EditAnywhere, Category = "Studio|Interaction", meta = (UIMin = "0.1", UIMax = "3"))
    float PanRangeFactor = 1.0f;

    /** Wheel zoom homes in on the point under the cursor. */
    UPROPERTY(EditAnywhere, Category = "Studio|Interaction")
    bool bZoomToCursor = true;

    /** Turntable auto-rotate after a few seconds without input. */
    UPROPERTY(EditAnywhere, Category = "Studio|Interaction")
    bool bAutoRotateWhenIdle = true;

    UPROPERTY(EditAnywhere, Category = "Studio|Interaction", meta = (UIMin = "0.5", UIMax = "30"))
    float AutoRotateIdleDelay = 5.0f;

    UPROPERTY(EditAnywhere, Category = "Studio|Interaction", meta = (UIMin = "1", UIMax = "60"))
    float AutoRotateSpeedDegPerSec = 10.0f;

    /** Bottom-center controls hint (code-only Slate, no widget assets). */
    UPROPERTY(EditAnywhere, Category = "Studio|Interaction")
    bool bShowHintOverlay = true;

    /** Console variables applied while the studio is active ("name value" per entry);
        previous values are restored on exit. Intended for specular-AA tuning. */
    UPROPERTY(EditAnywhere, Category = "Studio|Rendering")
    TArray<FString> StudioConsoleVariables = { TEXT("r.TemporalAACurrentFrameWeight 0.04") };

    /** Optional explicit subject; when unset, the first StaticMeshActor in the level is used. */
    UPROPERTY(EditAnywhere, Category = "Studio|Subject")
    TObjectPtr<AStaticMeshActor> ExplicitSubject;

    /** Enters/exits the studio view (bound to F; also callable via console "studio.Toggle"). */
    UFUNCTION(BlueprintCallable, Category = "Studio")
    void ToggleStudioView();

    /** Restores the default framing/orbit (bound to R). */
    UFUNCTION(BlueprintCallable, Category = "Studio")
    void ResetFraming();

protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    virtual void Tick(float DeltaSeconds) override;

private:
    void EnterStudioView();
    void ExitStudioView();
    void FinishExitCleanup();
    void DeferredEnvironmentCapture();
    void HideEnvironmentPanels();

    AStaticMeshActor* FindSubjectInLevel() const;
    UMaterialInstanceDynamic* MakeUnlitColorMID(const FLinearColor& LinearColor);
    ARectLight* SpawnSoftbox(const FVector& FocusPoint, float SubjectRadius, float YawDeg, float PitchDeg, float Lux, bool bCastShadows = false);
    void SetPivotMarkerVisible(bool bVisible);

    float ComputeFitDistance() const;
    /** Scene-color multiplier the tonemapper applies under the studio's manual exposure. */
    float ComputeExposureMultiplier() const;
    void ApplyCameraTransform(bool bInstant);

    void ApplyStudioCVars();
    void RestoreStudioCVars();
    void ShowHintOverlay();
    void HideHintOverlay();

    void NotifyInteraction() { IdleSeconds = 0.0f; }
    void ClampPan();

    // Input handlers (bound on this actor's own InputComponent only).
    void OnMouseX(float Value);
    void OnMouseY(float Value);
    void OnMouseWheel(float Value);
    void OnOrbitPressed();
    void OnOrbitReleased();
    void OnPanPressed();
    void OnPanReleased();

    bool bStudioActive = false;
    bool bOrbiting = false;
    bool bPanning = false;

    // Orbit targets (input writes these; the camera eases toward them).
    float OrbitYaw = 0.0f;
    float OrbitPitch = 0.0f;
    float OrbitDistance = 0.0f;
    FVector PanOffset = FVector::ZeroVector;

    // Smoothed values actually applied to the camera (per-channel easing).
    float CurrentYaw = 0.0f;
    float CurrentPitch = 0.0f;
    float CurrentDistance = 0.0f;
    FVector CurrentPan = FVector::ZeroVector;

    // Fling state (deg/s), fed while dragging, consumed after release.
    float YawVelocity = 0.0f;
    float PitchVelocity = 0.0f;
    float FrameYawInput = 0.0f;
    float FramePitchInput = 0.0f;

    float IdleSeconds = 0.0f;

    float FitDistance = 0.0f;
    FVector StudioFocus = FVector::ZeroVector;
    float SubjectBoundsRadius = 100.0f;
    FVector SubjectBoxExtent = FVector(100.0f);

    UPROPERTY()
    TArray<TObjectPtr<AActor>> StudioActors;

    /** Actors kept alive during the exit blend, destroyed when it finishes. */
    UPROPERTY()
    TArray<TObjectPtr<AActor>> DyingStudioActors;

    UPROPERTY()
    TObjectPtr<AStaticMeshActor> SubjectCopy;

    UPROPERTY()
    TObjectPtr<ACameraActor> StudioCamera;

    /** Small dot marking the orbit/pan pivot, shown while panning (RMB/MMB held). */
    UPROPERTY()
    TObjectPtr<AStaticMeshActor> PivotMarker;

    TWeakObjectPtr<USkyLightComponent> StudioSkyComp;

    /** Softbox panels awaiting the environment capture; hidden once it completes. */
    TArray<TWeakObjectPtr<AActor>> EnvironmentPanels;

    FTimerHandle ExitCleanupTimerHandle;
    FTimerHandle PanelHideTimerHandle;

    TSharedPtr<SWidget> HintWidget;
    TArray<TPair<FString, FString>> SavedCVarValues;

    TWeakObjectPtr<AActor> SavedViewTarget;
    bool bSavedShowMouseCursor = false;
    // EMouseCursor::Type values, stored raw to keep the header include-free.
    int32 SavedMouseCursor = 0;
    int32 SavedDefaultMouseCursor = 0;
};
