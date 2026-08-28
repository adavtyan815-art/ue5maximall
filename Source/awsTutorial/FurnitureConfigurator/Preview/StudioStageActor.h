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
// inertia, idle turntable). Driving the per-frame update from
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
class URectLightComponent;
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

    /** 1 = UE's standard filmic tonemapper — the SAME response the normal level
        view renders with, so materials keep the exact tonal character the user
        already sees in the room (proper highlight shoulder, midtone contrast,
        no washed-out whites). 0 = web-viewer-neutral linear->sRGB, which
        measures color-true but reads visibly flatter and paler than the level. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Studio Stage|Exposure", meta = (ClampMin = "0", ClampMax = "1"))
    float ToneCurveAmount = 1.0f;

    /** ONE global multiplier on the whole studio lighting: scales every softbox
        (key/fill/rim/top keep their exact ratios) AND their emissive panels in
        the captured IBL, so direct light, ambient and reflections all move
        together — material fidelity and light balance are preserved, only the
        overall strength changes. 1 = the calibrated white-point budget; stay
        roughly within 0.5..1.5 or whites will clip / the scene will dim. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Studio Stage|Lighting", meta = (DisplayName = "Light Intensity Scale (Global)", ClampMin = "0.1", ClampMax = "4", UIMin = "0.25", UIMax = "2"))
    float StudioLightIntensityScale = 1.0f;

    /** Illuminance the key softbox delivers at the subject; fills/rim are ratios.
        CALIBRATION (universal white-point budget — material-agnostic): the stage
        runs at locked exposure M = 2^LockedEV100, and the subject is lit TWICE —
        directly by the rect lights AND by their emissive panel images in the
        captured IBL. A white Lambert surface shows on screen at roughly
            M * albedo * (E_direct/pi + L_ibl)
        where E_direct ~ 1.7x this value for an upward-facing surface (worst
        case: key + top + fills + rim cosines) and L_ibl ~ 0.3 at the default
        panel brightness. The default 1.7 lands peak white at ~0.95-1.0 before
        the tonemapper — just under display white, so ANY white material keeps
        visible form instead of compressing into the filmic shoulder. Raising
        this above ~2.5 blows whites out again for every product. (The old 4.0
        overexposed the subject ~2x — the proven washed-out/flat cause.) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Studio Stage|Lighting", meta = (UIMin = "0.1", UIMax = "20"))
    float KeyIlluminanceLux = 1.7f;

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
        (others scale by their light ratio). Part of the white-point budget on
        KeyIlluminanceLux: the panels light the subject a second time through
        the captured IBL, and metals multiply this value directly — 1.3 puts a
        mirror-metal highlight just above display white (bright but detailed,
        hue preserved), while the old 3.0 clipped metal reflections to pale,
        desaturated white on every metallic material. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Studio Stage|Lighting", meta = (UIMin = "0", UIMax = "10"))
    float SoftboxPanelBrightness = 1.3f;

    /** Soft self-shadowing from the key & overhead softboxes — form and depth on
        diffuse materials (wood/fabric); disable for a fully shadowless match. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Studio Stage|Lighting")
    bool bKeyLightShadows = true;

    /** Screen-space ambient occlusion amount applied by the camera recipe.
        Kept mild: the web reference has none at all, and strong SSAO darkens
        crevices beyond what the authored materials intend. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Studio Stage|Lighting", meta = (ClampMin = "0", ClampMax = "1"))
    float AmbientOcclusionAmount = 0.3f;

    /** Faint product-shot vignette applied by the camera recipe (0 = none, the
        exact web-viewer look). Presentation only — it does not touch material
        response, so keep it subtle or off for maximum color fidelity. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Studio Stage|Camera", meta = (ClampMin = "0", ClampMax = "1"))
    float StudioVignetteIntensity = 0.15f;

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

    /** Per-component subject light scale (applied on focus): multiplies every
        rig light's baked intensity and the stage SkyLight's intensity — direct
        light, IBL ambient and reflections all scale together, backdrop and
        exposure untouched. A few SetIntensity calls; NO sky recapture. */
    void SetSubjectLightScale(float Scale);

    /** Destroys every stage actor. Also runs on EndPlay/Destroy. */
    void TearDownStage();

    /** Permanently disables (and removes, if shown) the code-Slate hint overlay
        for this stage — called when the WBP overlay renders the hint itself. */
    void DisableHintOverlay();

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

    // ── Whole-level view isolation ────────────────────────────────────────
    // The studio must never show ANY level content, no matter what the level
    // contains now or later. While the stage exists, every level actor with
    // renderable primitives is added to the local PlayerController's per-view
    // HiddenActors list (whitelist: the stage and the preview product),
    // and a world OnActorSpawned hook hides anything that appears
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

    /** Rig rect lights + their baked base intensities (global scale included),
        so SetSubjectLightScale can rescale without respawning or recapturing. */
    TArray<TWeakObjectPtr<URectLightComponent>> RigLightComponents;
    TArray<float> RigLightBaseIntensities;

    /**
     * Level atmosphere actors (ExponentialHeightFog / VolumetricCloud /
     * SkyAtmosphere) hidden for the duration of the stage and restored on
     * teardown. PROVEN cause of the wrong background: height fog applies to
     * unlit materials at any distance, so at ~50 m it recolored the #3a3a3a
     * backdrop into the level's light blue-gray inscattering color (the
     * standalone test level had no fog, which is why it looked correct there).
     * Hiding is client-world-local and invisible to the user (the room cannot
     * be seen from inside the studio).
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
    float ActiveSubjectRadius = 100.0f;
};
