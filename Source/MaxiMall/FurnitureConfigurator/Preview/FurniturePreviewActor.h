// Copyright MaxiMall Project. All Rights Reserved.
// FurniturePreviewActor.h
//
// AFurniturePreviewActor — Client-local WorldInPlace preview actor.
//
// KEY GUARANTEE: bReplicates = false hard-coded. Spawned exclusively by
// AMaxiMallPreviewController on the owning client at the SourceBooth location.
//
// Architecture (WorldInPlace only):
//   - Mirrors the AShowroomBooth mesh layout driven from a local product snapshot.
//   - Camera ORBITS around the focused mesh via SpringArm.
//   - Component isolation: only the focused mesh group is visible during preview.
//   - Stencil-250 CustomDepth isolation dims the background via PP material.
//   - Wall occlusion: one-shot sphere overlap at SetFocusComponent time hides
//     all world geometry within the max orbit radius, restoring it on EndPlay.
//
// Compatible: UE 5.3 → UE 5.6+

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "FurnitureConfigurator/Data/FurnitureTypes.h"
#include "FurniturePreviewActor.generated.h"

class UStaticMeshComponent;
class USpringArmComponent;
class UCameraComponent;
class URectLightComponent;
class USkyLightComponent;
class UDirectionalLightComponent;
class ACharacter;
class AShowroomBooth;
class ARectLight;
class ADirectionalLight;

// ─────────────────────────────────────────────────────────────────────────────
// Per-Component Preview Configuration
// Assign one of these per furniture component type in BP_FurniturePreviewActor.
// ─────────────────────────────────────────────────────────────────────────────

USTRUCT(BlueprintType)
struct FPreviewComponentConfig
{
    GENERATED_BODY()

    /**
     * Minimum spring-arm length (cm).
     * Prevents the camera from clipping through the mesh on close zoom.
     * Tune per-component based on the mesh's physical depth.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Zoom",
              meta = (DisplayName = "Min Zoom Distance (cm)", ClampMin = "5.0", ClampMax = "300.0", UIMin = "5.0", UIMax = "300.0"))
    float MinZoomDistance = 40.f;

    /**
     * Maximum spring-arm length (cm).
     * Also defines the radius of the one-shot wall-hide sphere:
     * all world geometry within this distance + 100cm from the mesh pivot
     * will be hidden for the entire duration of the preview.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Zoom",
              meta = (DisplayName = "Max Zoom Distance (cm)", ClampMin = "50.0", ClampMax = "1000.0", UIMin = "50.0", UIMax = "1000.0"))
    float MaxZoomDistance = 400.f;

    /** Whether the focused mesh casts real-time shadows during the preview. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lighting",
              meta = (DisplayName = "Cast Shadow"))
    bool bCastShadow = true;

    // ── Preview Rect Light Rig ─────────────────────────────────────────────
    // One Key Light (at a fixed offset from the orbit pivot) + Fill + Rim.
    // All three default to 0 intensity — Lumen GI provides naturalistic
    // lighting without flattening AO or normal maps. Enable per component
    // only when a specific mesh genuinely needs a brightness boost.

    /**
     * Intensity of the Key Light (lux). Default is 0 (off).
     * Lumen already provides naturalistic GI; adding direct light at high
     * intensity washes out AO and flattens normal map depth. Use low values
     * (100–300 lux) only if a specific mesh is genuinely too dark.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lighting | Preview Rig",
              meta = (DisplayName = "Key Light Intensity (lux)", ClampMin = "0.0", ClampMax = "5000.0"))
    float KeyLightIntensity = 0.f;

    /** Color tint applied to all three preview lights. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lighting | Preview Rig",
              meta = (DisplayName = "Light Color"))
    FLinearColor LightColor = FLinearColor::White;

    /** Fill and Rim intensity as a fraction of KeyLightIntensity (0 = off, 1 = same as key). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lighting | Preview Rig",
              meta = (DisplayName = "Fill / Rim Multiplier", ClampMin = "0.0", ClampMax = "1.0"))
    float FillRimMultiplier = 0.4f;

    /** Width of the Rect Light source (cm). Larger values produce softer, more diffuse light. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lighting | Preview Rig",
              meta = (DisplayName = "Light Source Width (cm)", ClampMin = "5.0", ClampMax = "300.0"))
    float LightSourceWidth = 80.f;

    /** Height of the Rect Light source (cm). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lighting | Preview Rig",
              meta = (DisplayName = "Light Source Height (cm)", ClampMin = "5.0", ClampMax = "300.0"))
    float LightSourceHeight = 100.f;

    /**
     * Distance of the Key Light from the focus pivot along the orbit arm (cm).
     * The light sits at this fixed distance regardless of zoom level, which
     * prevents the attenuation boundary from crossing the mesh surface.
     * Increase if you see a hard cutoff line at close zoom distances.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lighting | Preview Rig",
              meta = (DisplayName = "Key Light Offset (cm)", ClampMin = "50.0", ClampMax = "800.0"))
    float KeyLightOffset = 200.f;

    /**
     * Key Light attenuation radius (cm).
     * Must always be greater than KeyLightOffset + the mesh bounds radius.
     * If you still see a hard attenuation line, increase this value.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lighting | Preview Rig",
              meta = (DisplayName = "Key Light Attenuation Radius (cm)", ClampMin = "100.0", ClampMax = "3000.0"))
    float KeyLightAttenuationRadius = 800.f;

    /** Allow the Key Light to cast real-time shadows onto the focused mesh. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lighting | Preview Rig",
              meta = (DisplayName = "Key Light Casts Shadows"))
    bool bPreviewLightCastShadows = false;

    // ── Camera Exposure ──────────────────────────────────────────────────────

    /**
     * EV100 offset applied to the Camera's AutoExposure during this component's preview.
     * This is the non-destructive alternative to adding Rect Lights:
     * it boosts the scene brightness as-seen-by the camera using only the
     * existing Lumen GI, preserving AO, normal map depth, and material response.
     *   0.0  = no change (auto-exposure as set by the level)
     *  +1.0  = one stop brighter (good default if mesh looks dark in preview)
     *  +2.0  = two stops brighter
     * Negative values darken the scene.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lighting | Exposure",
              meta = (DisplayName = "Exposure Compensation (EV)", ClampMin = "-8.0", ClampMax = "8.0"))
    float ExposureCompensation = 0.f;

    // ── Studio SkyLight ─────────────────────────────────────────────────────

    /**
     * Intensity of the studio SkyLight during this component's preview.
     * A single SkyLightComponent provides true 360° diffuse fill from all
     * directions simultaneously, eliminating the pitch-black back-side problem
     * caused by WIP_UpdateWallOcclusion removing Lumen's bounce surfaces.
     * Default 2.0 gives soft fill without flattening PBR material response.
     * Set to 0 to disable and rely purely on the world's existing lighting.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lighting | Studio SkyLight",
              meta = (DisplayName = "Studio SkyLight Intensity", ClampMin = "0.0", ClampMax = "20.0"))
    float SkyLightIntensity = 2.f;

    /** Color tint of the studio SkyLight. Neutral white preserves material accuracy. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lighting | Studio SkyLight",
              meta = (DisplayName = "SkyLight Color"))
    FLinearColor SkyLightColor = FLinearColor::White;

    // ── Directional Key Light ───────────────────────────────────────────────

    /**
     * If true (default), automatically inherits the level's main world sun Intensity and Color.
     * The light direction remains strictly relative to the Camera view direction.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lighting | Directional Key",
              meta = (DisplayName = "Use World Sun Defaults"))
    bool bUseWorldSunDefaults = true;

    /**
     * Intensity of the camera-headlight Directional Key Light (lux).
     * Used when Use World Sun Defaults is false.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lighting | Directional Key",
              meta = (DisplayName = "Key Light Intensity (lux)", ClampMin = "0.0", ClampMax = "100.0"))
    float DirectionalLightIntensity = 8.f;

    /**
     * Color tint of the directional key light.
     * Used when Use World Sun Defaults is false.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lighting | Directional Key",
              meta = (DisplayName = "Key Light Color"))
    FLinearColor DirectionalLightColor = FLinearColor(1.f, 0.95f, 0.85f);

    /**
     * Strict local rotation offset of the directional key light relative to the Camera view direction.
     * Pitch = -15° tilts light slightly down from the camera line of sight.
     * Yaw = +15° angles light slightly from the right to create natural 3D specular highlights.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lighting | Directional Key",
              meta = (DisplayName = "Key Light Relative Rotation"))
    FRotator DirectionalLightRelativeRotation = FRotator(-15.f, 15.f, 0.f);

    /** Whether the camera key light casts real-time shadows. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lighting | Directional Key",
              meta = (DisplayName = "Key Light Casts Shadows"))
    bool bDirectionalLightCastShadows = false;
};

// ─────────────────────────────────────────────────────────────────────────────
// AFurniturePreviewActor
// ─────────────────────────────────────────────────────────────────────────────

UCLASS(Blueprintable, BlueprintType, NotPlaceable,
       HideCategories = (Rendering, Physics, Collision, Lighting, HLOD, Navigation, Input,
                         ActorTick, ComponentTick, LOD, Cooking, Replication, Tags,
                         TextureStreaming, RayTracing, PathTracing, AssetUserData),
       meta = (DisplayName = "Furniture Preview Actor (World In-Place)"))
class MAXIMALL_API AFurniturePreviewActor : public AActor
{
    GENERATED_BODY()

public:

    AFurniturePreviewActor();

    // ── Lifecycle ──────────────────────────────────────────────────────────
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    // Tick is disabled (PrimaryActorTick.bCanEverTick = false).

    // ─────────────────────────────────────────────────────────────────────
    // VISUAL COMPONENTS
    // ─────────────────────────────────────────────────────────────────────

    UPROPERTY(BlueprintReadOnly, Category = "Components")
    TObjectPtr<USceneComponent> PreviewRoot;

    /** Pivot scene root parenting all furniture mesh components. */
    UPROPERTY(BlueprintReadOnly, Category = "Components")
    TObjectPtr<USceneComponent> MeshRoot;

    UPROPERTY(BlueprintReadOnly, Category = "Components")
    TObjectPtr<UStaticMeshComponent> CabinetMesh;

    UPROPERTY(BlueprintReadOnly, Category = "Components")
    TObjectPtr<UStaticMeshComponent> DoorMeshSlot0;

    UPROPERTY(BlueprintReadOnly, Category = "Components")
    TObjectPtr<UStaticMeshComponent> DoorMeshSlot1;

    UPROPERTY(BlueprintReadOnly, Category = "Components")
    TObjectPtr<UStaticMeshComponent> CountertopMesh;

    UPROPERTY(BlueprintReadOnly, Category = "Components")
    TObjectPtr<UStaticMeshComponent> SinkMesh;

    UPROPERTY(BlueprintReadOnly, Category = "Components")
    TObjectPtr<UStaticMeshComponent> FaucetMesh;

    UPROPERTY(BlueprintReadOnly, Category = "Components")
    TObjectPtr<UStaticMeshComponent> MirrorMesh;

    UPROPERTY(BlueprintReadOnly, Category = "Components")
    TObjectPtr<UStaticMeshComponent> ClosetMesh;

    UPROPERTY(BlueprintReadOnly, Category = "Components")
    TObjectPtr<UStaticMeshComponent> ClosetDoorMeshSlot0;

    UPROPERTY(BlueprintReadOnly, Category = "Components")
    TObjectPtr<UStaticMeshComponent> ClosetDoorMeshSlot1;

    /** SpringArm driving camera orbit around WIP_FocusPivotWorld. */
    UPROPERTY(BlueprintReadOnly, Category = "Components")
    TObjectPtr<USpringArmComponent> SpringArm;

    /** Camera attached to the SpringArm socket. */
    UPROPERTY(BlueprintReadOnly, Category = "Components")
    TObjectPtr<UCameraComponent> Camera;

    // ── Preview Lighting Rig ──────────────────────────────────────────────
    // Three URectLightComponents providing even illumination of the focused mesh.
    // Key orbits with the camera; Fill and Rim stay at the orbit pivot.

    /** Key light — at fixed orbit offset, always illuminates the mesh face the camera sees. */
    UPROPERTY(BlueprintReadOnly, Category = "Components | Preview Lighting")
    TObjectPtr<URectLightComponent> PreviewKeyLight;

    /** Fill light — at orbit pivot, angled below/behind, provides soft backfill. */
    UPROPERTY(BlueprintReadOnly, Category = "Components | Preview Lighting")
    TObjectPtr<URectLightComponent> PreviewFillLight;

    /** Rim / top light — at orbit pivot, angled from above, provides edge definition. */
    UPROPERTY(BlueprintReadOnly, Category = "Components | Preview Lighting")
    TObjectPtr<URectLightComponent> PreviewRimLight;

    /**
     * Studio SkyLight — provides 360° diffuse fill from all directions.
     * Activated once in LoadProductPreview (RecaptureSky) and configured
     * per-component in SetFocusComponent via SkyLightIntensity / SkyLightColor.
     * Eliminates pitch-black back-side artifacts caused by wall occlusion.
     */
    UPROPERTY(BlueprintReadOnly, Category = "Components | Preview Lighting")
    TObjectPtr<USkyLightComponent> PreviewSkyLight;

    /**
     * Camera-Headlight Directional Key Light.
     * Attached directly to CameraComponent with a strict local rotation offset.
     * Moves and rotates 1:1 with camera view, ensuring whichever face the camera
     * looks at (horizontal, from above, or from below) is always illuminated.
     */
    UPROPERTY(BlueprintReadOnly, Category = "Components | Preview Lighting")
    TObjectPtr<UDirectionalLightComponent> PreviewDirectionalLight;

    // ─────────────────────────────────────────────────────────────────────
    // PREVIEW CONFIG
    // ─────────────────────────────────────────────────────────────────────

    /** Post process material template for Stencil-250 background isolation.
     *  Assign MI_StencilIsolation in BP_FurniturePreviewActor. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Preview Config")
    TObjectPtr<UMaterialInterface> StencilIsolationMaterialParent;

    /** Runtime MID created from StencilIsolationMaterialParent. */
    UPROPERTY()
    TObjectPtr<UMaterialInstanceDynamic> StencilIsolationMID;

    // ── Per-Component Configuration ────────────────────────────────────────
    // Set MinZoomDistance / MaxZoomDistance / bCastShadow per component in
    // the BP_FurniturePreviewActor Details panel.

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Preview Config | Components",
              meta = (DisplayName = "Cabinet"))
    FPreviewComponentConfig CabinetConfig;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Preview Config | Components",
              meta = (DisplayName = "Closet"))
    FPreviewComponentConfig ClosetConfig;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Preview Config | Components",
              meta = (DisplayName = "Countertop"))
    FPreviewComponentConfig CountertopConfig;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Preview Config | Components",
              meta = (DisplayName = "Sink"))
    FPreviewComponentConfig SinkConfig;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Preview Config | Components",
              meta = (DisplayName = "Faucet"))
    FPreviewComponentConfig FaucetConfig;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Preview Config | Components",
              meta = (DisplayName = "Mirror"))
    FPreviewComponentConfig MirrorConfig;

    // ─────────────────────────────────────────────────────────────────────
    // PUBLIC API
    // ─────────────────────────────────────────────────────────────────────

    /** Applies a product snapshot locally. Rebuilds all mesh components. */
    UFUNCTION(BlueprintCallable, Category = "Preview | Control",
              meta = (DisplayName = "Load Product Preview"))
    void LoadProductPreview(const FFurnitureProductRow& ProductData,
                            const FShowroomBoothConfigState& ActiveState,
                            AShowroomBooth* SourceBooth);

    /**
     * Isolates and focuses on a single component group.
     * Handles:
     *   1. Visibility isolation (hides all other mesh groups).
     *   2. Stencil-250 CustomDepth on the focused group.
     *   3. SpringArm repositioned to the focused mesh's bounds center.
     *   4. Per-component zoom limits applied from the matching Config.
     *   5. One-shot sphere overlap to hide all world geometry in the orbit volume.
     */
    UFUNCTION(BlueprintCallable, Category = "Preview | Control",
              meta = (DisplayName = "Set Focus Component"))
    void SetFocusComponent(EFurnitureComponentType ComponentType);

    /** Orbits the camera around the focused mesh. */
    UFUNCTION(BlueprintCallable, Category = "Preview | Control",
              meta = (DisplayName = "Rotate Preview"))
    void RotatePreview(float DeltaYaw, float DeltaPitch);

    /** Resets camera orbit to the initial facing angle and zoom. */
    UFUNCTION(BlueprintCallable, Category = "Preview | Control",
              meta = (DisplayName = "Reset Preview Rotation"))
    void ResetRotation();

    /** Adjusts the SpringArm TargetArmLength, clamped by the active component's config. */
    UFUNCTION(BlueprintCallable, Category = "Preview | Control",
              meta = (DisplayName = "Zoom Preview"))
    void ZoomPreview(float DeltaZoom);

private:

    // ── Active zoom limits (set from the focused component's FPreviewComponentConfig) ──
    float ActiveMinZoom = 30.f;
    float ActiveMaxZoom = 400.f;

    // ── Orbit state ────────────────────────────────────────────────────────
    float CurrentZoomLength   = 180.f;
    float WorldInPlaceYaw     = 0.f;
    float WorldInPlacePitch   = 0.f;

    FVector  WIP_FocusPivotWorld  = FVector::ZeroVector;
    FRotator WIP_InitialOrbitRot  = FRotator::ZeroRotator;
    float    WIP_MeshBoundsRadius = 100.f;
    float    WIP_CurrentViewDist  = 180.f;
    float    WIP_InitialViewDist  = 180.f;

    // ── Scene restore ──────────────────────────────────────────────────────
    TWeakObjectPtr<ACharacter>      WIP_CachedCharacter;
    TWeakObjectPtr<AShowroomBooth>  WIP_CachedSourceBooth;
    TArray<TWeakObjectPtr<UPrimitiveComponent>> WIP_CachedHiddenWallComponents;

    /** World ARectLight actors that were visible before preview; restored on EndPlay. */
    TArray<TWeakObjectPtr<AActor>>  WIP_CachedWorldRectLights;

    /** World ADirectionalLight actors cached with their original intensities.
     *  Intensity is set to 0 during preview and restored on EndPlay. */
    TArray<TPair<TWeakObjectPtr<ADirectionalLight>, float>> WIP_CachedWorldDirLights;

    /** World properties of the primary ADirectionalLight captured in LoadProductPreview. */
    FRotator     WIP_CachedWorldSunRotation  = FRotator(-46.f, -46.f, 0.f);
    float        WIP_CachedWorldSunIntensity = 8.f;
    FLinearColor WIP_CachedWorldSunColor     = FLinearColor(1.f, 0.95f, 0.85f);

    UPROPERTY()
    TObjectPtr<UStaticMeshComponent> CurrentFocusedComponent;

    // ── Private helpers ────────────────────────────────────────────────────
    FVector WIP_GetFocusPivotWorld() const;
    void    WIP_ApplyStencilIsolation();
    void    WIP_UpdateWallOcclusion();   // One-shot sphere overlap. Called from SetFocusComponent.
    void    ConfigureMesh(UStaticMeshComponent* Comp) const;

    void ApplyComponentMeshAndMaterials(UStaticMeshComponent* Target,
                                        const FFurnitureComponentOptions& Options,
                                        int32 SizeIndex,
                                        int32 ColorIndex);

    void ApplyComponentMeshAndMaterials(UStaticMeshComponent* Target,
                                        const FFurnitureCabinetOptions& Options,
                                        int32 SizeIndex,
                                        int32 ColorIndex);

    void ApplyDoorMeshAndMaterials(UStaticMeshComponent* Target,
                                   const FFurnitureDoorGroup& DoorGroup,
                                   int32 SizeIndex,
                                   int32 ColorIndex,
                                   int32 SlotIndex);
};
