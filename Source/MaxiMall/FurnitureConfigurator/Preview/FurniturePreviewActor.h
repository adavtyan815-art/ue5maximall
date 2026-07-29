// Copyright MaxiMall Project. All Rights Reserved.
// FurniturePreviewActor.h
//
// AFurniturePreviewActor — The private, client-local actor used in the
// isolated 3D furniture preview viewport.
//
// KEY GUARANTEE: This actor is NEVER replicated to the server or other clients.
// It is spawned exclusively by AMaxiMallPreviewController on the owning client,
// with bReplicates = false hard-coded in the constructor.
//
// Architecture:
//   - Exposes the same visual mesh components as AShowroomBooth, driven directly
//     from a local snapshot of FFurnitureProductRow data.
//   - No Server RPCs exist. All functions are local-only.
//   - Orbit/inspect rotation is driven by the player controller — the actor
//     exposes RotatePreview(DeltaYaw, DeltaPitch).
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
class USpotLightComponent;
class UPointLightComponent;
class USkyLightComponent;

USTRUCT(BlueprintType)
struct FFurniturePreviewLightingConfig
{
    GENERATED_BODY()

    /** Focus distance of the camera for this component section. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera")
    float FocusDistance = 250.f;

    /** Field of view of the camera for this component section. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera")
    float CameraFOV = 65.f;

    /** Default pitch angle of camera for this component section (negative = looking down). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera")
    float Pitch = -15.f;

    /** Default yaw angle of camera for this component section. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera")
    float Yaw = 0.f;

    /** Minimum pitch angle limit for camera orbit (in degrees). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera")
    float PitchMin = -80.f;

    /** Maximum pitch angle limit for camera orbit (in degrees). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera")
    float PitchMax = 80.f;

    /** Minimum distance the camera can zoom in. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera")
    float ZoomMin = 100.f;

    /** Maximum distance the camera can zoom out. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera")
    float ZoomMax = 500.f;

    /** Enable/disable shadow casting for this component mesh. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shadows")
    bool bCastShadow = true;

    /** Toggle Key Light on/off for this component section. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lighting Toggles")
    bool bEnableKeyLight = true;

    /** Toggle Fill Light on/off for this component section. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lighting Toggles")
    bool bEnableFillLight = true;

    /** Toggle Rim Light on/off for this component section. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lighting Toggles")
    bool bEnableRimLight = true;

    /** Key light intensity for this component section. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lighting Intensities")
    float KeyLightIntensity = 80000.f;

    /** Fill light intensity for this component section. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lighting Intensities")
    float FillLightIntensity = 10000.f;

    /** Rim light intensity for this component section. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lighting Intensities")
    float RimLightIntensity = 30000.f;

    /** Sky light (ambient environment reflection) intensity for metals/gold/chrome. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lighting Intensities")
    float SkyLightIntensity = 1.0f;

    /** Sun light (Directional Light) scale for this component section. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lighting Intensities", meta = (ClampMin = "0.0", ClampMax = "10.0"))
    float DirectionalLightScale = 1.0f;

    /** Master global intensity scale applied to all preview lights simultaneously for this section. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lighting Intensities", meta = (ClampMin = "0.0", ClampMax = "10.0"))
    float MasterLightIntensityScale = 1.0f;

    /** Color of the key light for this component section. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lighting Colors")
    FLinearColor KeyLightColor = FLinearColor::White;

    /** Color of the fill light for this component section. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lighting Colors")
    FLinearColor FillLightColor = FLinearColor::White;

    /** Color of the rim light for this component section. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lighting Colors")
    FLinearColor RimLightColor = FLinearColor::White;

    /** Color of the sky light ambient reflection for this component section. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lighting Colors")
    FLinearColor SkyLightColor = FLinearColor::White;

    /** Optional HDRI Studio Cubemap texture to give metals (gold, chrome, brass) high-contrast reflection highlights. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lighting Advanced")
    TSoftObjectPtr<class UTextureCube> StudioCubemap;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lighting Advanced")
    FVector KeyLightLocation = FVector(-300.f, -300.f, 300.f);

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lighting Advanced")
    float KeyLightInnerConeAngle = 30.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lighting Advanced")
    float KeyLightOuterConeAngle = 50.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lighting Advanced")
    float AttenuationRadius = 1000.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lighting Advanced")
    float ShadowBias = 0.02f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lighting Advanced")
    float ShadowSlopeBias = 0.02f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lighting Advanced")
    float ContactShadowLength = 0.05f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lighting Advanced")
    float KeyLightSourceRadius = 15.f;
};

UCLASS(Blueprintable, BlueprintType, NotPlaceable,
       HideCategories = (Rendering, Physics, Collision, Lighting, HLOD, Navigation, Input, ActorTick, ComponentTick, LOD, Cooking, Replication, Tags, TextureStreaming, RayTracing, PathTracing, AssetUserData),
       meta = (DisplayName = "Furniture Preview Actor (Client Only)"))
class MAXIMALL_API AFurniturePreviewActor : public AActor
{
    GENERATED_BODY()

public:

    AFurniturePreviewActor();

    // ── Lifecycle ─────────────────────────────────────────────────────────
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    virtual void PostInitializeComponents() override;
    virtual void OnConstruction(const FTransform& Transform) override;

    // ─────────────────────────────────────────────────────────────────────
    // VISUAL COMPONENTS
    // ─────────────────────────────────────────────────────────────────────

    UPROPERTY(BlueprintReadOnly, Category = "Components")
    TObjectPtr<USceneComponent> PreviewRoot;

    /** Dynamic pivot root component for rotating furniture meshes. */
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

    /** SpringArm component to handle orbit distance and rotation. */
    UPROPERTY(BlueprintReadOnly, Category = "Components")
    TObjectPtr<USpringArmComponent> SpringArm;

    /** Camera component to render the high quality viewport. */
    UPROPERTY(BlueprintReadOnly, Category = "Components")
    TObjectPtr<UCameraComponent> Camera;

    /** Backdrop mesh component for the clean studio background. */
    UPROPERTY(BlueprintReadOnly, Category = "Components")
    TObjectPtr<UStaticMeshComponent> BackdropMesh;

    /** Key Light spotlight (attached to SpringArm for consistent view-angle lighting). */
    UPROPERTY(BlueprintReadOnly, Category = "Components")
    TObjectPtr<USpotLightComponent> KeyLight;

    /** Camera-mounted Fill Light pointlight. */
    UPROPERTY(BlueprintReadOnly, Category = "Components")
    TObjectPtr<UPointLightComponent> FillLight;

    /** Rim / Back Light spotlight (placed behind model to separate from backdrop). */
    UPROPERTY(BlueprintReadOnly, Category = "Components")
    TObjectPtr<USpotLightComponent> RimLight;

    /** Studio Sky Light component to provide rich HDRI ambient reflections for metals (gold, chrome, brass). */
    UPROPERTY(BlueprintReadOnly, Category = "Components")
    TObjectPtr<USkyLightComponent> SkyLight;

    // ─────────────────────────────────────────────────────────────────────
    // SECTION PROFILES (Fully encapsulated settings per model/section)
    // ─────────────────────────────────────────────────────────────────────

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Preview Config")
    FFurniturePreviewLightingConfig CabinetLighting;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Preview Config")
    FFurniturePreviewLightingConfig ClosetLighting;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Preview Config")
    FFurniturePreviewLightingConfig CountertopLighting;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Preview Config")
    FFurniturePreviewLightingConfig SinkLighting;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Preview Config")
    FFurniturePreviewLightingConfig FaucetLighting;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Preview Config")
    FFurniturePreviewLightingConfig MirrorLighting;

    /** Active viewmode mode strategy (Isolated Studio vs World In-Place). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Preview Config")
    EPreviewViewportMode ViewportMode = EPreviewViewportMode::IsolatedStudio;

    /** In WorldInPlace mode, how far forward (cm towards camera) the focused model shifts from the wall. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Preview Config | World In-Place")
    float WorldInPlaceForwardOffset = 25.0f;

    /** In WorldInPlace mode, Depth of Field F-Stop for background blur (lower = blurrier background, 1.4 is soft cinematic blur). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Preview Config | World In-Place")
    float WorldInPlaceBackgroundBlurFstop = 1.4f;

    // ─────────────────────────────────────────────────────────────────────
    // PUBLIC API
    // ─────────────────────────────────────────────────────────────────────

    /** Dynamically switch preview mode strategy at runtime. */
    UFUNCTION(BlueprintCallable, Category = "Preview | Control", meta = (DisplayName = "Set Viewport Mode"))
    void SetViewportMode(EPreviewViewportMode NewMode);

    /** Applies a product snapshot locally. Rebuilds all mesh components. */
    UFUNCTION(BlueprintCallable, Category = "Preview | Control",
              meta = (DisplayName = "Load Product Preview"))
    void LoadProductPreview(const FFurnitureProductRow& ProductData, const FShowroomBoothConfigState& ActiveState, class AShowroomBooth* SourceBooth);

    /** Inspect and focus the camera orbit around a specific model component. */
    UFUNCTION(BlueprintCallable, Category = "Preview | Control",
              meta = (DisplayName = "Set Focus Component"))
    void SetFocusComponent(EFurnitureComponentType ComponentType);

    /** Configures the static mesh and material for the studio background. */
    UFUNCTION(BlueprintCallable, Category = "Preview | Control")
    void SetupBackdrop(UStaticMesh* InMesh, UMaterialInterface* InMaterial);

    /** Rotates the preview camera around its target. */
    UFUNCTION(BlueprintCallable, Category = "Preview | Control",
              meta = (DisplayName = "Rotate Preview"))
    void RotatePreview(float DeltaYaw, float DeltaPitch);

    /** Resets the preview rotation to the default facing angle. */
    UFUNCTION(BlueprintCallable, Category = "Preview | Control",
              meta = (DisplayName = "Reset Preview Rotation"))
    void ResetRotation();

    /** Initializes the starting and default rotation for the preview camera. */
    UFUNCTION(BlueprintCallable, Category = "Preview | Control",
              meta = (DisplayName = "Set Initial Rotation"))
    void SetInitialRotation(float InYaw, float InPitch);

    /** Zooms the camera by adjusting the SpringArm TargetArmLength. */
    UFUNCTION(BlueprintCallable, Category = "Preview | Control",
              meta = (DisplayName = "Zoom Preview"))
    void ZoomPreview(float DeltaZoom);

    /** Dynamically toggles Key Light for current active profile. */
    UFUNCTION(BlueprintCallable, Category = "Preview | Control", meta = (DisplayName = "Set Key Light Enabled"))
    void SetKeyLightEnabled(bool bEnable);

    /** Dynamically toggles Fill Light for current active profile. */
    UFUNCTION(BlueprintCallable, Category = "Preview | Control", meta = (DisplayName = "Set Fill Light Enabled"))
    void SetFillLightEnabled(bool bEnable);

    /** Dynamically toggles Rim Light for current active profile. */
    UFUNCTION(BlueprintCallable, Category = "Preview | Control", meta = (DisplayName = "Set Rim Light Enabled"))
    void SetRimLightEnabled(bool bEnable);

private:

    FFurniturePreviewLightingConfig ActiveConfig;

    float CurrentZoomLength = 250.f;
    float CurrentYaw   = 0.f;
    float CurrentPitch = 0.f;
    float DefaultYaw = 0.f;
    float DefaultPitch = -15.f;

    float ActiveBaseFillIntensity = 10000.f;
    float ReferenceZoomDistance = 250.f;

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

    void UpdateLightIntensityForZoom();
    void EnforceLightingSettings();
    void ApplyLightingConfig(const FFurniturePreviewLightingConfig& Config);
    void ApplyDirectionalLightScale();
    void RestoreDirectionalLight();
    void ApplyWorldPostProcessSettings();

    float SavedDirectionalLightIntensity = -1.f;

    UPROPERTY()
    TWeakObjectPtr<class ADirectionalLight> CachedDirectionalLight;

    UPROPERTY()
    TObjectPtr<UStaticMeshComponent> CurrentFocusedComponent;
};
