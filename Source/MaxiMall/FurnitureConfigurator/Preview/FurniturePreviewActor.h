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

USTRUCT(BlueprintType)
struct FFurniturePreviewLightingConfig
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lighting")
    float KeyLightIntensity = 80000.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lighting")
    float FillLightIntensity = 10000.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lighting")
    FVector KeyLightLocation = FVector(-300.f, -300.f, 300.f);

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lighting")
    float KeyLightInnerConeAngle = 30.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lighting")
    float KeyLightOuterConeAngle = 50.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lighting")
    float AttenuationRadius = 1000.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lighting")
    float ShadowBias = 0.02f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lighting")
    float ShadowSlopeBias = 0.02f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lighting")
    float ContactShadowLength = 0.05f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lighting")
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

    UPROPERTY(BlueprintReadOnly, Category = "Preview Config")
    TObjectPtr<USceneComponent> PreviewRoot;

    /** Dynamic pivot root component for rotating furniture meshes. */
    UPROPERTY(BlueprintReadOnly, Category = "Preview Config")
    TObjectPtr<USceneComponent> MeshRoot;

    UPROPERTY(BlueprintReadOnly, Category = "Preview Config")
    TObjectPtr<UStaticMeshComponent> CabinetMesh;

    UPROPERTY(BlueprintReadOnly, Category = "Preview Config")
    TObjectPtr<UStaticMeshComponent> DoorMeshSlot0;

    UPROPERTY(BlueprintReadOnly, Category = "Preview Config")
    TObjectPtr<UStaticMeshComponent> DoorMeshSlot1;

    UPROPERTY(BlueprintReadOnly, Category = "Preview Config")
    TObjectPtr<UStaticMeshComponent> CountertopMesh;

    UPROPERTY(BlueprintReadOnly, Category = "Preview Config")
    TObjectPtr<UStaticMeshComponent> SinkMesh;

    UPROPERTY(BlueprintReadOnly, Category = "Preview Config")
    TObjectPtr<UStaticMeshComponent> FaucetMesh;

    UPROPERTY(BlueprintReadOnly, Category = "Preview Config")
    TObjectPtr<UStaticMeshComponent> MirrorMesh;

    UPROPERTY(BlueprintReadOnly, Category = "Preview Config")
    TObjectPtr<UStaticMeshComponent> ClosetMesh;

    UPROPERTY(BlueprintReadOnly, Category = "Preview Config")
    TObjectPtr<UStaticMeshComponent> ClosetDoorMeshSlot0;

    UPROPERTY(BlueprintReadOnly, Category = "Preview Config")
    TObjectPtr<UStaticMeshComponent> ClosetDoorMeshSlot1;

    /** SpringArm component to handle orbit distance and rotation. */
    UPROPERTY(BlueprintReadOnly, Category = "Preview Config")
    TObjectPtr<USpringArmComponent> SpringArm;

    /** Camera component to render the high quality viewport. */
    UPROPERTY(BlueprintReadOnly, Category = "Preview Config")
    TObjectPtr<UCameraComponent> Camera;

    /** Backdrop mesh component for the clean studio background. */
    UPROPERTY(BlueprintReadOnly, Category = "Preview Config")
    TObjectPtr<UStaticMeshComponent> BackdropMesh;

    /** Optional Key Light spotlight (attached to SpringArm for consistent view-angle lighting). */
    UPROPERTY(BlueprintReadOnly, Category = "Preview Config")
    TObjectPtr<USpotLightComponent> KeyLight;

    /** Optional Camera-mounted Fill Light pointlight. */
    UPROPERTY(BlueprintReadOnly, Category = "Preview Config")
    TObjectPtr<UPointLightComponent> FillLight;

    /** Optional Rim / Back Light spotlight (placed behind model to separate from backdrop). */
    UPROPERTY(BlueprintReadOnly, Category = "Preview Config")
    TObjectPtr<USpotLightComponent> RimLight;

    /** Minimum pitch angle limit for camera orbit (in degrees). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Preview Config")
    float PitchMin = -80.f;

    /** Maximum pitch angle limit for camera orbit (in degrees). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Preview Config")
    float PitchMax = 80.f;

    /** Default distance of the camera from the actor (SpringArm target length). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Preview Config")
    float DefaultCameraDistance = 250.f;

    /** Field of view of the camera in degrees (65° studio catalog standard). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Preview Config")
    float CameraFOV = 65.f;

    /** Minimum distance the camera can zoom in. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Preview Config")
    float ZoomMin = 100.f;

    /** Maximum distance the camera can zoom out. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Preview Config")
    float ZoomMax = 500.f;

    /** Focus distance for Cabinet inspect mode. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Preview Config")
    float CabinetFocusDistance = 250.f;

    /** Focus distance for Closet inspect mode. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Preview Config")
    float ClosetFocusDistance = 250.f;

    /** Focus distance for Cabinet Doors inspect mode. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Preview Config")
    float DoorsFocusDistance = 200.f;

    /** Focus distance for Countertop inspect mode. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Preview Config")
    float CountertopFocusDistance = 200.f;

    /** Focus distance for Sink inspect mode. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Preview Config")
    float SinkFocusDistance = 150.f;

    /** Focus distance for Faucet inspect mode. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Preview Config")
    float FaucetFocusDistance = 100.f;

    /** Focus distance for Mirror inspect mode. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Preview Config")
    float MirrorFocusDistance = 150.f;

    /** Color of the key light (pure neutral white by default). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Preview Config")
    FLinearColor KeyLightColor = FLinearColor::White;

    /** Color of the fill light (pure neutral white by default). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Preview Config")
    FLinearColor FillLightColor = FLinearColor::White;

    /** Color of the rim light (pure neutral white by default). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Preview Config")
    FLinearColor RimLightColor = FLinearColor::White;

    /** Enable/disable shadow casting for Cabinet mesh. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Preview Config")
    bool bCabinetCastShadow = true;

    /** Enable/disable shadow casting for Closet mesh. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Preview Config")
    bool bClosetCastShadow = true;

    /** Enable/disable shadow casting for Countertop mesh. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Preview Config")
    bool bCountertopCastShadow = true;

    /** Enable/disable shadow casting for Sink mesh. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Preview Config")
    bool bSinkCastShadow = false;

    /** Enable/disable shadow casting for Faucet mesh. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Preview Config")
    bool bFaucetCastShadow = true;

    /** Enable/disable shadow casting for Mirror mesh. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Preview Config")
    bool bMirrorCastShadow = true;

    /** Toggle Key Light on/off independently. When true, simply adds Key Light to main scene lighting. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Preview Config", meta = (DisplayName = "Enable Key Light"))
    bool bEnableKeyLight = false;

    /** Toggle Fill Light on/off independently. When true, simply adds Fill Light to main scene lighting. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Preview Config", meta = (DisplayName = "Enable Fill Light"))
    bool bEnableFillLight = false;

    /** Toggle Rim Light on/off independently. When true, simply adds Rim Light to main scene lighting. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Preview Config", meta = (DisplayName = "Enable Rim Light"))
    bool bEnableRimLight = false;

    /** Intensity multiplier applied to the world Directional Light while the preview is active. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Preview Config", meta = (ClampMin = "0.0", ClampMax = "10.0"))
    float PreviewDirectionalLightIntensityScale = 1.0f;

    /** Master global intensity scale applied to all 3 preview lights simultaneously (1.0 = 100%, 0.5 = 50%, 2.0 = 200%). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Preview Config", meta = (ClampMin = "0.0", ClampMax = "10.0", DisplayName = "Master Light Intensity Scale"))
    float MasterLightIntensityScale = 1.0f;

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

    // ─────────────────────────────────────────────────────────────────────
    // PUBLIC API
    // ─────────────────────────────────────────────────────────────────────

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

    /** Dynamically toggles Key Light. */
    UFUNCTION(BlueprintCallable, Category = "Preview | Control", meta = (DisplayName = "Set Key Light Enabled"))
    void SetKeyLightEnabled(bool bEnable);

    /** Dynamically toggles Fill Light. */
    UFUNCTION(BlueprintCallable, Category = "Preview | Control", meta = (DisplayName = "Set Fill Light Enabled"))
    void SetFillLightEnabled(bool bEnable);

    /** Dynamically toggles Rim Light. */
    UFUNCTION(BlueprintCallable, Category = "Preview | Control", meta = (DisplayName = "Set Rim Light Enabled"))
    void SetRimLightEnabled(bool bEnable);

private:

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

    float SavedDirectionalLightIntensity = -1.f;

    UPROPERTY()
    TWeakObjectPtr<class ADirectionalLight> CachedDirectionalLight;

    UPROPERTY()
    TObjectPtr<UStaticMeshComponent> CurrentFocusedComponent;
};
