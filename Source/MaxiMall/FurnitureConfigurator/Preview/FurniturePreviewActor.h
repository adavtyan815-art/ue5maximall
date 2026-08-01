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

    // ── Camera & Zoom Controls ───────────────────────────────────────────────

    /** Minimum distance (cm) the camera can zoom in before clamping. Prevents mesh clipping. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera & Zoom",
              meta = (DisplayName = "Minimum Zoom Distance (cm)", ClampMin = "5.0", ClampMax = "500.0", UIMin = "10.0", UIMax = "300.0"))
    float MinZoomDistance = 40.f;

    /** Maximum distance (cm) the camera can zoom out. Also sets the wall-hiding sphere radius. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera & Zoom",
              meta = (DisplayName = "Maximum Zoom Distance (cm)", ClampMin = "50.0", ClampMax = "1500.0", UIMin = "100.0", UIMax = "1000.0"))
    float MaxZoomDistance = 400.f;

    /** EV100 offset applied to the Camera's AutoExposure (positive values brighten dark meshes). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera & Zoom",
              meta = (DisplayName = "Camera Exposure Offset (EV)", ClampMin = "-4.0", ClampMax = "4.0", UIMin = "-2.0", UIMax = "2.0"))
    float ExposureCompensation = 0.f;

    // ── Mesh Rendering ───────────────────────────────────────────────────────

    /** Enables or disables dynamic shadow casting for the focused mesh component during preview. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mesh Rendering",
              meta = (DisplayName = "Enable Mesh Dynamic Shadows"))
    bool bCastShadow = true;

    // ── Studio SkyLight Environment ──────────────────────────────────────────

    /** Intensity of the 360° studio SkyLight fill component (0 = rely purely on level ambient). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Studio Environment",
              meta = (DisplayName = "SkyLight Fill Intensity", ClampMin = "0.0", ClampMax = "20.0", UIMin = "0.0", UIMax = "10.0"))
    float SkyLightIntensity = 2.f;

    /** Color tint for the 360° studio SkyLight ambient fill. Neutral white preserves PBR materials. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Studio Environment",
              meta = (DisplayName = "SkyLight Ambient Color"))
    FLinearColor SkyLightColor = FLinearColor::White;

    // ── Directional Sun Light ────────────────────────────────────────────────

    /**
     * If true, inherits ALL lighting properties (intensity, color, temperature, indirect bounce)
     * directly from the level's main World Sun light, greyed out in UI.
     * If false, allows manual override values below.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Directional Sun Light",
              meta = (DisplayName = "Inherit World Sun Settings"))
    bool bUseWorldSunDefaults = true;

    /** Manual intensity override for the directional sun light (lux). Active when inherit = false. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Directional Sun Light",
              meta = (DisplayName = "Sun Light Intensity Override (lux)", ClampMin = "0.0", ClampMax = "100.0", UIMin = "0.0", UIMax = "30.0", EditCondition = "!bUseWorldSunDefaults"))
    float DirectionalLightIntensity = 8.f;

    /** Manual color tint override for the directional sun light. Active when inherit = false. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Directional Sun Light",
              meta = (DisplayName = "Sun Light Color Override", EditCondition = "!bUseWorldSunDefaults"))
    FLinearColor DirectionalLightColor = FLinearColor(1.f, 0.95f, 0.85f);

    /** Local rotation offset relative to camera view line. Active when inherit = false. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Directional Sun Light",
              meta = (DisplayName = "Sun Light Relative Rotation", EditCondition = "!bUseWorldSunDefaults"))
    FRotator DirectionalLightRelativeRotation = FRotator(-15.f, 15.f, 0.f);

    /** Toggles real-time shadow casting for directional sun light. Active when inherit = false. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Directional Sun Light",
              meta = (DisplayName = "Sun Light Casts Shadows", EditCondition = "!bUseWorldSunDefaults"))
    bool bDirectionalLightCastShadows = false;

    // ── Preview Rect Lights ──────────────────────────────────────────────────

    /** Intensity of key RectLight component (lux). Default 0 (relying on SkyLight / Sun). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Preview Rect Lights",
              meta = (DisplayName = "Key Rect Intensity (lux)", ClampMin = "0.0", ClampMax = "5000.0", UIMin = "0.0", UIMax = "1000.0"))
    float KeyLightIntensity = 0.f;

    /** Color tint applied to 3-light RectLight rig. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Preview Rect Lights",
              meta = (DisplayName = "Rect Light Color Tint"))
    FLinearColor LightColor = FLinearColor::White;

    /** Intensity fraction for Fill & Rim rect lights relative to Key (0 = off, 1 = same). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Preview Rect Lights",
              meta = (DisplayName = "Fill & Rim Intensity Ratio", ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "1.0"))
    float FillRimMultiplier = 0.4f;

    /** Source width (cm) for rect light soft shadows. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Preview Rect Lights",
              meta = (DisplayName = "Rect Source Width (cm)", ClampMin = "5.0", ClampMax = "500.0", UIMin = "10.0", UIMax = "300.0"))
    float LightSourceWidth = 80.f;

    /** Source height (cm) for rect light soft shadows. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Preview Rect Lights",
              meta = (DisplayName = "Rect Source Height (cm)", ClampMin = "5.0", ClampMax = "500.0", UIMin = "10.0", UIMax = "300.0"))
    float LightSourceHeight = 100.f;

    /** Distance offset of key rect light from pivot along orbit arm (cm). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Preview Rect Lights",
              meta = (DisplayName = "Rect Key Offset Distance (cm)", ClampMin = "50.0", ClampMax = "1000.0", UIMin = "100.0", UIMax = "500.0"))
    float KeyLightOffset = 200.f;

    /** Falloff attenuation radius (cm) for key rect light. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Preview Rect Lights",
              meta = (DisplayName = "Rect Key Attenuation Radius (cm)", ClampMin = "100.0", ClampMax = "5000.0", UIMin = "200.0", UIMax = "2000.0"))
    float KeyLightAttenuationRadius = 800.f;

    /** Toggles real-time shadow casting for key rect light. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Preview Rect Lights",
              meta = (DisplayName = "Rect Key Casts Shadows"))
    bool bPreviewLightCastShadows = false;
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
    bool         WIP_CachedWorldSunUseTemp   = false;
    float        WIP_CachedWorldSunTemp      = 6500.f;
    float        WIP_CachedWorldSunIndirect  = 1.0f;

    UPROPERTY()
    TObjectPtr<UStaticMeshComponent> CurrentFocusedComponent;

    /** WorldInPlace pivot reference state for exact mesh-centered rotation. */
    FVector WIP_MeshPivotWorld      = FVector::ZeroVector;
    FVector WIP_MeshRootLocAtReset  = FVector::ZeroVector;
    FQuat   WIP_InitialMeshRootQuat = FQuat::Identity;

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
