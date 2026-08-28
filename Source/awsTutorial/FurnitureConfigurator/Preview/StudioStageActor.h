// Copyright MaxiMall Project. All Rights Reserved.
// StudioStageActor.h
//
// AStudioStageActor — the neutral "Studio Stage" environment for View Mode.
//
// A faithful port of the approved standalone StudioViewerTest environment: the
// backdrop, softbox rig, emissive capture panels and captured-scene SkyLight are
// spawned as separate actors exactly like the proven test implementation, with
// two production adaptations: the rect lights live on LIGHTING CHANNEL 1 (the
// preview meshes' channel) so the level never receives stage light, and the
// camera recipe is applied to the preview actor's existing camera.
//
// The stage also TICKS the studio interaction: every frame it pumps
// AFurniturePreviewActor::StudioTickUpdate on the driven preview (easing,
// inertia, idle turntable, pivot marker). Driving the per-frame update from
// this plain C++ actor keeps it independent of whatever tick settings the
// BP_FurniturePreviewActor subclass carries.
//
// Environment notes:
//   - The stage SkyLight is scene-global while registered (sky lights have no
//     lighting channels): it temporarily overrides the level's sky light on the
//     scene's stack and is restored automatically on destroy. The player only
//     ever sees the enclosed studio during this window.
//   - The emissive softbox panels exist only to be photographed into the sky
//     capture (the "HDRI"); they are hidden right after the capture completes.
//   - Client-local, never replicated.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "StudioStageActor.generated.h"

class AFurniturePreviewActor;
class USkyLightComponent;
class UCameraComponent;
class UMaterialInstanceDynamic;
class SWidget;

UCLASS(Blueprintable, NotPlaceable,
       HideCategories = (Collision, Physics, Replication, Networking, Actor, Cooking, Input),
       meta = (DisplayName = "Studio Stage Actor (View Mode)"))
class AWSTUTORIAL_API AStudioStageActor : public AActor
{
    GENERATED_BODY()

public:
    AStudioStageActor();

    // ── Calibrated look (defaults match the approved StudioViewerTest) ───────

    /** Background color, sRGB (the web viewer uses #3a3a3a). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Studio Stage|Backdrop")
    FColor BackdropColorSRGB = FColor(0x3A, 0x3A, 0x3A);

    /** Boost the backdrop emissive by the inverse of the camera exposure so it
        lands on-screen at exactly BackdropColorSRGB for any exposure value. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Studio Stage|Backdrop")
    bool bCompensateBackdropExposure = true;

    /** Locked exposure compensation in stops. Physical camera exposure is disabled,
        so scene color is multiplied by exactly 2^this. Default log2(0.85) ~= -0.23
        matches the web viewer's exposure="0.85". */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Studio Stage|Exposure", meta = (UIMin = "-4", UIMax = "6"))
    float LockedEV100 = -0.23f;

    /** 0 = fully neutral (linear->sRGB, color-true like the web viewer), 1 = UE filmic. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Studio Stage|Exposure", meta = (ClampMin = "0", ClampMax = "1"))
    float ToneCurveAmount = 0.0f;

    /** Illuminance the key softbox delivers at the subject; fills/rim are ratios.
        The stage runs at unit exposure (1 scene unit ~ display white), so useful
        values are single digits: ~4 puts a key-lit white diffuse near full white. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Studio Stage|Lighting", meta = (UIMin = "0.1", UIMax = "20"))
    float KeyIlluminanceLux = 4.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Studio Stage|Lighting", meta = (ClampMin = "0", ClampMax = "2"))
    float FillLeftRatio = 0.45f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Studio Stage|Lighting", meta = (ClampMin = "0", ClampMax = "2"))
    float FillRightRatio = 0.25f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Studio Stage|Lighting", meta = (ClampMin = "0", ClampMax = "2"))
    float RimRatio = 0.6f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Studio Stage|Lighting", meta = (ClampMin = "0", ClampMax = "2"))
    float TopRatio = 0.5f;

    /** Softbox size as a fraction of the subject's bounding-sphere radius. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Studio Stage|Lighting", meta = (UIMin = "0.5", UIMax = "6"))
    float SoftboxSizeFactor = 3.0f;

    /** Light distance as a multiple of the subject's bounding-sphere radius. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Studio Stage|Lighting", meta = (UIMin = "1.2", UIMax = "8"))
    float LightDistanceFactor = 2.6f;

    /** Emissive luminance of the key softbox panel as seen in reflections
        (others scale by their light ratio). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Studio Stage|Lighting", meta = (UIMin = "0", UIMax = "10"))
    float SoftboxPanelBrightness = 3.0f;

    /** Soft self-shadowing from the key & overhead softboxes — form and depth on
        diffuse materials (wood/fabric); disable for a fully shadowless match. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Studio Stage|Lighting")
    bool bKeyLightShadows = true;

    /** Screen-space ambient occlusion amount applied by the camera recipe. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Studio Stage|Lighting", meta = (ClampMin = "0", ClampMax = "1"))
    float AmbientOcclusionAmount = 0.5f;

    /** Bottom-center Russian controls hint (code-only Slate, no widget assets). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Studio Stage|UI")
    bool bShowHintOverlay = true;

    // ── Stage API (called by AAwsTutorial_PlayerController / preview actor) ──

    /** (Re)builds the environment around FocusPoint, sized for SubjectRadius.
        Safe to call again after a product reload — the previous build is torn down. */
    void BuildStage(const FVector& FocusPoint, float SubjectRadius);

    /** Applies the calibrated studio post recipe to a camera: manual exposure
        (LockedEV100 + ExtraExposureEV), neutral tone response, no lens effects,
        GI/reflections None (pure IBL from the stage's captured sky light). */
    void ApplyCameraRecipe(UCameraComponent* Camera, float ExtraExposureEV = 0.0f) const;

    /** The preview whose studio interaction this stage pumps every frame. */
    void SetDrivenPreview(AFurniturePreviewActor* InPreview);

    /** Destroys every stage actor. Also runs on EndPlay/Destroy. */
    void TearDownStage();

    virtual void Tick(float DeltaSeconds) override;

protected:
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
    UMaterialInstanceDynamic* MakeUnlitColorMID(const FLinearColor& LinearColor);
    void SpawnSoftbox(const FVector& FocusPoint, float SubjectRadius, float YawDeg, float PitchDeg, float Lux, bool bCastShadows);
    void DeferredEnvironmentCapture();
    void HideEnvironmentPanels();
    void ShowHintOverlay();
    void HideHintOverlay();

    /** [StudioDiag] one-shot visibility/render-state dump proving why (or whether)
        the backdrop reaches the renderer. Scheduled at +1 s and +3 s after build. */
    void DumpStageVisibilityDiagnostics();

    // ── Whole-level view isolation ────────────────────────────────────────
    // The studio must never show ANY level content, no matter what the level
    // contains now or later. While the stage exists, every level actor with
    // renderable primitives is added to the local PlayerController's per-view
    // HiddenActors list (whitelist: the stage, the preview product, the pivot
    // marker), and a world OnActorSpawned hook hides anything that appears
    // mid-session. Per-player only (multiplayer/Pixel Streaming safe), no
    // level state touched; removed exactly on teardown.
    void ApplyWorldIsolation();
    void RemoveWorldIsolation();
    void OnWorldActorSpawned(AActor* SpawnedActor);
    bool IsStageWhitelisted(const AActor* Actor) const;

    /** Scene-color multiplier the tonemapper applies under the stage's exposure. */
    float ComputeExposureMultiplier() const;

    /** Every actor spawned for the stage (backdrop, lights, panels, sky light). */
    UPROPERTY()
    TArray<TObjectPtr<AActor>> StageActors;

    /**
     * Level atmosphere actors (ExponentialHeightFog / VolumetricCloud /
     * SkyAtmosphere) hidden for the duration of the stage and restored on
     * teardown. PROVEN cause of the wrong background: height fog applies to
     * unlit materials at any distance, so at ~50 m it recolored the #3a3a3a
     * backdrop into the level's light blue-gray inscattering color (the
     * standalone test level had no fog, which is why it looked correct there).
     * Hiding is client-world-local and invisible to the user (the room cannot
     * be seen from inside the studio); legacy WorldInPlace mode never builds a
     * stage and is unaffected.
     */
    TArray<TWeakObjectPtr<AActor>> SuspendedEnvironmentActors;

    /** Level actors we added to the local player's HiddenActors view list. */
    TArray<TWeakObjectPtr<AActor>> HiddenWorldActors;
    FDelegateHandle ActorSpawnedHandle;
    /** Guards the spawn hook against hiding the stage's own (re)build spawns. */
    bool bBuildingStage = false;

    /** Softbox panels awaiting the environment capture; hidden once it completes. */
    TArray<TWeakObjectPtr<AActor>> PanelActors;

    TWeakObjectPtr<USkyLightComponent> StageSkyComp;
    TWeakObjectPtr<AFurniturePreviewActor> DrivenPreview;

    TSharedPtr<SWidget> HintWidget;

    FTimerHandle PanelHideTimerHandle;
    FTimerHandle DiagDumpTimerHandle;
    int32 DiagDumpCount = 0;
    float ActiveSubjectRadius = 100.0f;
};
