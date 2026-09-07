// Copyright MaxiMall Project. All Rights Reserved.
// StudioStageActor.h
//
// AStudioStageActor — the neutral "Studio Stage" environment for View Mode.
//
// GROUND-TRUTH ENVIRONMENT ARCHITECTURE (see docs/VIEWMODE_INVESTIGATION_V2.md):
// the product is lit the way a good web product viewer lights it — by ONE
// explicit HDRI environment on a SkyLight (SLS_SpecifiedCubemap), with a fully
// specified camera recipe. Nothing is captured at runtime and nothing is
// inherited from the level's post processing.
//
// PRESENTATION MODES — a 1:1 port of the reference forma-webviewer modes:
//   NEUTRAL   procedural neutral studio (RoomEnvironment-like) as the light;
//             background = flat light warm-gray + soft radial vignette (the
//             viewer's CSS radial gradient, not a 3D environment).
//   STUDIO    brown_photostudio_02 HDRI as the light AND, blurred and dimmed,
//             as the visible background (viewer: blur 0.4, intensity 0.55).
//   LIFESTYLE lebombo_2k HDRI as the light and blurred background
//             (viewer: blur 0.25, intensity 0.85, env intensity 0.95).
// The blurred-HDRI background is a rough METALLIC two-sided dome lit only by
// the SkyLight: a metal reflects the environment cubemap, roughness = blur,
// tint = background intensity — the same specular path the products use.
// It needs one small project material (/Game/ViewMode/M_ViewModeBackdrop,
// see BackdropDomeMaterial); without it Studio/Lifestyle fall back to a flat
// surround color.
//
// GROUNDING — like the viewer (ShadowMaterial catcher + contact blob): two
// translucent blobs on the flat surround — a tight contact blob and a longer
// directional shadow ellipse stretched away from the key light. No floor
// geometry exists, so there is never a floor/wall seam or horizon line.
//
//   - The stage SkyLight is scene-global while registered (sky lights have no
//     lighting channels): it temporarily overrides the level's sky light on the
//     scene's stack and is restored automatically on destroy. The player only
//     ever sees the enclosed studio during this window.
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
class UMaterialInterface;
class UTextureCube;
class UTexture2D;
class SWidget;

/** ViewMode presentation, mirroring the reference web viewer's modes. */
UENUM(BlueprintType)
enum class EStudioPresentation : uint8
{
    /** Procedural neutral studio light + flat light-gray surround ("most faithful color view"). */
    Neutral   UMETA(DisplayName = "Neutral"),
    /** Photo-studio HDRI as light and blurred background ("photo studio lighting"). */
    Studio    UMETA(DisplayName = "Studio"),
    /** Warm interior HDRI as light and blurred background ("warm interior lighting"). */
    Lifestyle UMETA(DisplayName = "Lifestyle")
};

/** Everything that defines one presentation mode's light + background. */
USTRUCT(BlueprintType)
struct FStudioPresentationLook
{
    GENERATED_BODY()

    /** Project path of the HDRI TextureCube used as the LIGHT (and, if the dome
        is enabled, as the background). Empty = the procedural neutral studio. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Look")
    FString EnvironmentAssetPath;

    /** SkyLight intensity multiplier for this mode (viewer: environmentIntensity). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Look", meta = (UIMin = "0", UIMax = "4"))
    float EnvironmentIntensity = 1.0f;

    /** Show the environment itself as a blurred background dome (viewer:
        showBackground). Requires BackdropDomeMaterial; otherwise FlatColorSRGB. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Look")
    bool bEnvironmentDome = false;

    /** Background brightness of the dome (viewer: backgroundIntensity). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Look", meta = (UIMin = "0", UIMax = "2"))
    float DomeIntensity = 1.0f;

    /** Dome blur (viewer backgroundBlurriness, a PMREM roughness): mapped onto
        the smallest mips of the cubemap — 0 already ≈ 45% of the mip chain,
        1 = the 2px/face mip. Reference values: Lifestyle 0.40, Studio 0.55. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Look", meta = (ClampMin = "0", ClampMax = "1"))
    float DomeRoughness = 0.5f;

    /** The dome follows the camera's full orientation (yaw and pitch), so any
        orbit/tilt always shows the same region of the HDRI behind the product
        (a stable backdrop, as the reference reads). This picks WHICH region:
        yaw offset in degrees between the view direction and the HDRI. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Look", meta = (UIMin = "-180", UIMax = "180"))
    float DomeYawOffset = 0.0f;

    /** Flat surround color (sRGB) when no dome is shown / as dome fallback. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Look")
    FColor FlatColorSRGB = FColor(0xEC, 0xEC, 0xEA);

    /** Post-process vignette for this mode (Neutral uses it to reproduce the
        viewer's radial CSS gradient: light center, darker warm edges). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Look", meta = (ClampMin = "0", ClampMax = "1"))
    float Vignette = 0.0f;
};

UCLASS(Blueprintable, NotPlaceable,
       HideCategories = (Collision, Physics, Replication, Networking, Actor, Cooking, Input),
       meta = (DisplayName = "Studio Stage Actor (View Mode)"))
class AWSTUTORIAL_API AStudioStageActor : public AActor
{
    GENERATED_BODY()

public:
    AStudioStageActor();

    // ── Presentation modes (mirror the reference viewer 1:1) ─────────────────

    /** Active mode. Switch live in-game with `studio.Preset Neutral|Studio|Lifestyle`. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Studio Stage|Presentation")
    EStudioPresentation PresentationMode = EStudioPresentation::Neutral;

    /** NEUTRAL: procedural light, flat #ECEAE6 surround + vignette 0.35
        (= the viewer's radial gradient #fafaf8 -> #e9e6e1 -> #d8d4cd). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Studio Stage|Presentation")
    FStudioPresentationLook NeutralLook;

    /** STUDIO: /Game/ViewMode/StudioEnvHDRI (brown_photostudio_02) as light and
        blurred dome background (intensity 0.55, roughness 0.55). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Studio Stage|Presentation")
    FStudioPresentationLook StudioLook;

    /** LIFESTYLE: /Game/ViewMode/LifestyleEnvHDRI (lebombo_2k) as light (0.95)
        and blurred dome background (intensity 0.85, roughness 0.4). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Studio Stage|Presentation")
    FStudioPresentationLook LifestyleLook;

    /** Two-sided UNLIT skybox material for the blurred-HDRI background dome
        (= three.js scene.background + backgroundBlurriness). Parameters set by
        the stage: TextureCube "EnvCube", vector "Tint", scalar "BlurMip".
        Empty = auto-load /Game/ViewMode/M_ViewModeBackdrop. Graph: Unlit,
        Two Sided; TextureSampleParameterCube "EnvCube" with UVs = -CameraVector
        and MipValueMode = MipLevel driven by ScalarParameter "BlurMip";
        result RGB x VectorParameter "Tint" -> Emissive Color. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Studio Stage|Presentation")
    TObjectPtr<UMaterialInterface> BackdropDomeMaterial;

    // ── Backdrop (flat surround) ────────────────────────────────────────────
    // The flat surround is an enclosing box of UNLIT planes: constant color in
    // every direction, so its corners/edges are invisible — one seamless field
    // with no horizon. (A LIT floor plane was the proven source of the old
    // horizon band: grazing-angle Fresnel sheen where it met the wall.)

    /** Boost the flat backdrop emissive by the inverse of the camera exposure
        so it lands on-screen at exactly the chosen color for any exposure. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Studio Stage|Backdrop")
    bool bCompensateBackdropExposure = true;

    // ── Exposure & tone response ─────────────────────────────────────────────

    /** Locked exposure compensation in stops. Physical camera exposure is disabled,
        so scene color is multiplied by exactly 2^this. Default 0 = exposure 1.0,
        matching the reference viewer (toneMappingExposure = 1.0). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Studio Stage|Exposure", meta = (UIMin = "-4", UIMax = "6"))
    float LockedEV100 = 0.0f;

    /** 1 = UE's standard filmic response with ENGINE-DEFAULT grading values —
        the exact tone response of the Static Mesh Editor, applied with every
        input pinned so level PostProcessVolumes can never alter it. 0 = raw
        linear->sRGB (reads flat/hazy — measuring tool only). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Studio Stage|Exposure", meta = (ClampMin = "0", ClampMax = "1"))
    float ToneCurveAmount = 1.0f;

    /** Optional post-process blendable refining the tone response (e.g. a
        Khronos PBR Neutral material). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Studio Stage|Exposure")
    TObjectPtr<UMaterialInterface> ToneMapperMaterial;

    // ── Environment (IBL) ────────────────────────────────────────────────────

    /** Explicit override: when set, this cubemap is the light for EVERY mode
        (the mode's EnvironmentAssetPath is ignored). Normally leave empty. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Studio Stage|Environment")
    TObjectPtr<UTextureCube> EnvironmentCubemap;

    /** Yaw rotation (degrees) applied to the source cubemap. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Studio Stage|Environment", meta = (UIMin = "0", UIMax = "360"))
    float EnvironmentCubemapAngle = 0.0f;

    /** Global SkyLight intensity (multiplied by the mode's EnvironmentIntensity). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Studio Stage|Environment", meta = (UIMin = "0", UIMax = "10"))
    float SkyLightIntensity = 1.0f;

    /** Procedural environment: face size in pixels (used when a mode has no
        HDRI). 512 gives crisp softbox shapes in chrome. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Studio Stage|Environment", meta = (ClampMin = "32", ClampMax = "1024"))
    int32 ProceduralEnvSize = 512;

    /** Procedural environment: surround radiance at the zenith (linear). Bright
        surround — metals mirror the environment's average, and vertical product
        faces are lit by its walls; a dark surround starves both (proven). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Studio Stage|Environment", meta = (UIMin = "0", UIMax = "4"))
    float EnvBaseTopLuminance = 1.1f;

    /** Procedural environment: surround radiance at the nadir (linear). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Studio Stage|Environment", meta = (UIMin = "0", UIMax = "4"))
    float EnvBaseBottomLuminance = 0.50f;

    /** Procedural environment: peak radiance of the KEY light panel (others by
        the rig ratios). Small, very hot sources (RoomEnvironment-style) give
        metals sparkling highlights with a moderate diffuse share. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Studio Stage|Environment", meta = (UIMin = "0", UIMax = "40"))
    float EnvPanelLuminance = 6.0f;

    /** Shrinks the environment panels relative to the physical softbox size. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Studio Stage|Environment", meta = (ClampMin = "0.05", ClampMax = "1"))
    float EnvPanelAngularScale = 0.35f;

    // ── Direct lighting (shadows & form on top of the IBL) ───────────────────

    /** ONE global multiplier on the whole studio lighting (key/rim lights AND
        the procedural environment together). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Studio Stage|Lighting", meta = (DisplayName = "Light Intensity Scale (Global)", ClampMin = "0.1", ClampMax = "4", UIMin = "0.25", UIMax = "2"))
    float StudioLightIntensityScale = 1.0f;

    /** Illuminance the key softbox delivers at the subject, on top of the IBL
        (form + self-shadowing; ~20% of the total keeps it soft). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Studio Stage|Lighting", meta = (UIMin = "0", UIMax = "10"))
    float KeyIlluminanceLux = 1.5f;

    /** Optional shadowless rim as a fraction of the key. 0 = off. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Studio Stage|Lighting", meta = (ClampMin = "0", ClampMax = "2"))
    float RimRatio = 0.35f;

    /** Soft area self-shadowing on the product from the key light. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Studio Stage|Lighting")
    bool bKeyLightShadows = true;

    /** Softbox size as a fraction of the subject's bounding-sphere radius. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Studio Stage|Lighting", meta = (UIMin = "0.5", UIMax = "6"))
    float SoftboxSizeFactor = 3.0f;

    /** Light distance as a multiple of the subject's bounding-sphere radius. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Studio Stage|Lighting", meta = (UIMin = "1.2", UIMax = "8"))
    float LightDistanceFactor = 2.6f;

    // ── Grounding (viewer: contact blob + directional shadow catcher) ───────

    /** Draw the two shadow blobs under the focused component. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Studio Stage|Grounding")
    bool bContactShadow = true;

    /** Tight contact blob: peak darkness (viewer ≈ 0.24) and size vs footprint. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Studio Stage|Grounding", meta = (ClampMin = "0", ClampMax = "1"))
    float ContactShadowStrength = 0.30f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Studio Stage|Grounding", meta = (UIMin = "0.5", UIMax = "3"))
    float ContactShadowScale = 1.15f;

    /** Directional shadow ellipse stretched away from the key light (viewer's
        key-light shadow, opacity 0.32): peak darkness, length vs footprint,
        and how far its center is pushed along the shadow direction. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Studio Stage|Grounding", meta = (ClampMin = "0", ClampMax = "1"))
    float DirectionalShadowStrength = 0.32f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Studio Stage|Grounding", meta = (UIMin = "1", UIMax = "4"))
    float DirectionalShadowLength = 1.9f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Studio Stage|Grounding", meta = (UIMin = "0", UIMax = "1.5"))
    float DirectionalShadowOffset = 0.45f;

    /** Translucent UNLIT material for the blobs. Defaults to the engine's
        Widget3DPassThrough_Translucent (SlateUI texture, TintColorAndOpacity,
        OpacityFromTexture) which ships in every build. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Studio Stage|Grounding")
    TObjectPtr<UMaterialInterface> ContactShadowMaterial;

    // ── Camera extras ────────────────────────────────────────────────────────

    /** Screen-space ambient occlusion (crevice depth, as in the mesh editor). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Studio Stage|Camera", meta = (ClampMin = "0", ClampMax = "1"))
    float AmbientOcclusionAmount = 0.35f;

    /** Bloom on bright speculars (0.675 = engine default the mesh editor shows). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Studio Stage|Camera", meta = (ClampMin = "0", ClampMax = "8"))
    float StudioBloomIntensity = 0.675f;

    // ── Rendering stability ──────────────────────────────────────────────────

    /** Console variables applied while the stage exists; restored on teardown. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Studio Stage|Rendering")
    TArray<FString> StudioConsoleVariables = { TEXT("r.TemporalAACurrentFrameWeight 0.04") };

    // ── UI ───────────────────────────────────────────────────────────────────

    /** Bottom-center Russian controls hint (code-only Slate, no widget assets). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Studio Stage|UI")
    bool bShowHintOverlay = true;

    // ── Stage API (called by AAwsTutorial_PlayerController / preview actor) ──

    /** (Re)builds the environment around FocusPoint, sized for SubjectRadius.
        Safe to call again after a product reload — the previous build is torn down. */
    void BuildStage(const FVector& FocusPoint, float SubjectRadius);

    /** Applies the studio post recipe to a camera: manual exposure
        (LockedEV100 + ExtraExposureEV), pinned tone response, the mode's
        vignette, GI/reflections None (pure IBL from the stage's environment). */
    void ApplyCameraRecipe(UCameraComponent* Camera, float ExtraExposureEV = 0.0f) const;

    /** The preview whose studio interaction this stage pumps every frame. */
    void SetDrivenPreview(AFurniturePreviewActor* InPreview);

    /** Per-component subject light scale (applied on focus): multiplies every
        rig light's baked intensity and the stage SkyLight's intensity. */
    void SetSubjectLightScale(float Scale);

    /** Destroys every stage actor. Also runs on EndPlay/Destroy. */
    void TearDownStage();

    /** Switches the presentation live: rebuilds the stage (light + background)
        with the last build parameters and re-applies the camera recipe. The
        product, framing and interaction are untouched. */
    void SetPresentationMode(EStudioPresentation NewMode);

    /** Re-targets the shadow blobs to the currently visible product box
        (called by the preview actor after focus isolation). */
    void UpdateContactShadow(const FBox& VisibleProductBox);

    /** Permanently disables (and removes, if shown) the code-Slate hint overlay. */
    void DisableHintOverlay();

    virtual void Tick(float DeltaSeconds) override;

protected:
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
    UMaterialInstanceDynamic* MakeUnlitColorMID(const FLinearColor& LinearColor);

    /** Spawns one channel-1 rect light of the rig. */
    void SpawnRigLight(const FVector& FocusPoint, float SubjectRadius, float YawDeg, float PitchDeg, float Lux, bool bCastShadows);

    /** Flat unlit surround box (all modes' fallback; Neutral's background). */
    void SpawnFlatBackdrop(const FVector& FocusPoint, float SubjectRadius, const FColor& ColorSRGB);

    /** Blurred-HDRI background: unlit two-sided SKYBOX dome sampling EnvCube
        by view direction. Returns false if the material/cubemap is missing. */
    bool SpawnBackdropDome(const FVector& FocusPoint, float SubjectRadius, const FStudioPresentationLook& Look, UTextureCube* EnvCube);

    /** Spawns one translucent blob plane (returns the actor). */
    AActor* SpawnShadowBlob(const FBox& ProductBox, float Strength);

    /** Runtime radial-gradient texture (RGB black, alpha falloff) for the blobs. */
    UTexture2D* GetOrBuildContactShadowTexture();

    /** Generates the procedural neutral-studio HDR cubemap (cached). */
    UTextureCube* GetOrBuildProceduralEnvironment();

    const FStudioPresentationLook& GetActiveLook() const;

    void ApplyStudioCVars();
    void RestoreStudioCVars();

    void ShowHintOverlay();
    void HideHintOverlay();

    // ── Whole-level view isolation ────────────────────────────────────────
    void ApplyWorldIsolation();
    void RemoveWorldIsolation();
    void OnWorldActorSpawned(AActor* SpawnedActor);
    bool IsStageWhitelisted(const AActor* Actor) const;

    /** Scene-color multiplier the tonemapper applies under the stage's exposure. */
    float ComputeExposureMultiplier() const;

    /** Every actor spawned for the stage (backdrop, dome, blobs, lights, sky light). */
    UPROPERTY()
    TArray<TObjectPtr<AActor>> StageActors;

    /** Rig rect lights + their baked base intensities (global scale included). */
    TArray<TWeakObjectPtr<URectLightComponent>> RigLightComponents;
    TArray<float> RigLightBaseIntensities;

    /** The runtime-generated neutral-studio cubemap (kept referenced for GC). */
    UPROPERTY()
    TObjectPtr<UTextureCube> ProceduralEnvCube;

    /** Cosine-weighted up-facing irradiance of the procedural cubemap (RGB, logged). */
    FLinearColor ProceduralEnvIrradianceUp = FLinearColor::Black;

    /** Runtime-generated radial gradient for the shadow blobs. */
    UPROPERTY()
    TObjectPtr<UTexture2D> ContactShadowTexture;

    /** The two shadow blobs (re-targeted on focus changes). */
    TWeakObjectPtr<AActor> ContactBlobActor;
    TWeakObjectPtr<AActor> DirectionalBlobActor;

    /** The blurred-HDRI background dome (yaw-locked to the camera in Tick). */
    TWeakObjectPtr<AActor> DomeActor;

    /** Last BuildStage parameters, so a presentation switch can rebuild. */
    FVector LastBuildFocusPoint = FVector::ZeroVector;
    float LastBuildSubjectRadius = 100.0f;
    bool bHasBuiltOnce = false;

    /** Last per-component EV passed to ApplyCameraRecipe (re-applied on switch). */
    mutable float LastExtraExposureEV = 0.0f;

    /** Active mode's SkyLight intensity factor (kept for SetSubjectLightScale). */
    float ActiveEnvironmentIntensity = 1.0f;

    /** Level atmosphere actors hidden for the stage's lifetime (see .cpp). */
    TArray<TWeakObjectPtr<AActor>> SuspendedEnvironmentActors;

    /** Level actors we added to the local player's HiddenActors view list. */
    TArray<TWeakObjectPtr<AActor>> HiddenWorldActors;
    FDelegateHandle ActorSpawnedHandle;
    /** Guards the spawn hook against hiding the stage's own (re)build spawns. */
    bool bBuildingStage = false;

    TWeakObjectPtr<USkyLightComponent> StageSkyComp;
    TWeakObjectPtr<AFurniturePreviewActor> DrivenPreview;

    TSharedPtr<SWidget> HintWidget;

    /** cvar name -> previous value, restored on teardown. */
    TArray<TPair<FString, FString>> SavedCVarValues;

    float ActiveSubjectRadius = 100.0f;
};
