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
//   - Exposes the same six visual mesh components as AShowroomBooth, but they
//     are driven directly from a local snapshot of FFurnitureProductRow data.
//   - The player controller passes the active product data to LoadProductPreview().
//   - No Server RPCs exist. All functions are local-only.
//   - Orbit/inspect rotation is driven by the player controller — the actor
//     simply exposes RotatePreview(DeltaYaw, DeltaPitch).
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
    virtual void PostInitializeComponents() override;
    virtual void OnConstruction(const FTransform& Transform) override;

    // ─────────────────────────────────────────────────────────────────────
    // VISUAL COMPONENTS (mirrors AShowroomBooth layout)
    // ─────────────────────────────────────────────────────────────────────

    UPROPERTY(BlueprintReadOnly, Category = "Preview Config")
    TObjectPtr<USceneComponent> PreviewRoot;

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

    /** Key light for the studio setup. */
    UPROPERTY(BlueprintReadOnly, Category = "Preview Config")
    TObjectPtr<USpotLightComponent> KeyLight;

    /** Camera-mounted fill light (headlight). */
    UPROPERTY(BlueprintReadOnly, Category = "Preview Config")
    TObjectPtr<UPointLightComponent> FillLight;

    /** Skylight component to provide ambient reflections for mirror/metallic surfaces. */
    UPROPERTY(BlueprintReadOnly, Category = "Preview Config")
    TObjectPtr<USkyLightComponent> PreviewSkyLight;

    /** Minimum pitch angle limit for camera orbit (in degrees). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Preview Config")
    float PitchMin = -80.f;

    /** Maximum pitch angle limit for camera orbit (in degrees). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Preview Config")
    float PitchMax = 80.f;

    /** Default distance of the camera from the actor (SpringArm target length). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Preview Config")
    float DefaultCameraDistance = 250.f;

    /** Field of view of the camera in degrees. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Preview Config")
    float CameraFOV = 90.f;

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

    /** Base intensity for the camera headlight (FillLight) in Lumens. */
    UPROPERTY()
    float BaseFillIntensity = 40000.f;

    /** Reference zoom distance corresponding to the BaseFillIntensity. */
    UPROPERTY()
    float ReferenceZoomDistance = 250.f;

    /** Base intensity for the spotlight (KeyLight) in Lumens. */
    UPROPERTY()
    float BaseKeyIntensity = 80000.f;

    /** Color of the key light. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Preview Config")
    FLinearColor KeyLightColor = FLinearColor::White;

    /** Color of the fill light. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Preview Config")
    FLinearColor FillLightColor = FLinearColor::White;

    /** Position of the spotlight relative to the preview studio center. C++ will auto-rotate the light to look at target. */
    UPROPERTY()
    FVector KeyLightRelativeLocation = FVector(-300.f, -300.f, 300.f);

    /** Inner spotlight cone angle in degrees. */
    UPROPERTY()
    float KeyLightInnerConeAngle = 30.f;

    /** Outer spotlight cone angle in degrees. */
    UPROPERTY()
    float KeyLightOuterConeAngle = 50.f;

    /** Max reach distance of the spotlight. */
    UPROPERTY()
    float KeyLightAttenuationRadius = 1000.f;

    /** Max reach distance of the headlight. */
    UPROPERTY()
    float FillLightAttenuationRadius = 1000.f;

    /** Shadow bias for the key light spotlight to resolve shadow acne. */
    UPROPERTY()
    float KeyLightShadowBias = 1.0f;

    /** Shadow slope bias for the key light spotlight. */
    UPROPERTY()
    float KeyLightShadowSlopeBias = 1.0f;

    /** Contact shadow length for the key light spotlight (0.0 = disabled). */
    UPROPERTY()
    float KeyLightContactShadowLength = 0.0f;

    /** Shadow bias for the fill light PointLight to resolve shadow acne. */
    UPROPERTY()
    float FillLightShadowBias = 1.0f;

    /** Shadow slope bias for the fill light PointLight. */
    UPROPERTY()
    float FillLightShadowSlopeBias = 1.0f;

    /** Contact shadow length for the fill light PointLight (0.0 = disabled). */
    UPROPERTY()
    float FillLightContactShadowLength = 0.0f;

    /** Toggle to isolate preview lighting using lighting channel 1. If false, uses default channel 0. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Preview Config")
    bool bUseLightingChannels = true;

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

    /**
     * Applies a product snapshot locally. Rebuilds all six mesh components.
     * Called by the player controller after it reads the active booth product.
     *
     * @param ProductData  A value copy of FFurnitureProductRow (no server reference).
     */
    UFUNCTION(BlueprintCallable, Category = "Preview | Control",
              meta = (DisplayName = "Load Product Preview"))
    void LoadProductPreview(const FFurnitureProductRow& ProductData, const FShowroomBoothConfigState& ActiveState, class AShowroomBooth* SourceBooth);

    /**
     * Inspect and focus the camera orbit around a specific model component.
     */
    UFUNCTION(BlueprintCallable, Category = "Preview | Control",
              meta = (DisplayName = "Set Focus Component"))
    void SetFocusComponent(EFurnitureComponentType ComponentType);

    /** Configures the static mesh and material for the studio background. */
    UFUNCTION(BlueprintCallable, Category = "Preview | Control")
    void SetupBackdrop(UStaticMesh* InMesh, UMaterialInterface* InMaterial);

    /**
     * Rotates the preview around its local up-axis (Yaw) and local right-axis (Pitch).
     * Intended to be called every frame from the player controller while the
     * user drags within the preview widget. Does NOT send any RPC.
     *
     * @param DeltaYaw    Horizontal drag delta in degrees.
     * @param DeltaPitch  Vertical drag delta in degrees (clamped internally).
     */
    UFUNCTION(BlueprintCallable, Category = "Preview | Control",
              meta = (DisplayName = "Rotate Preview"))
    void RotatePreview(float DeltaYaw, float DeltaPitch);

    /**
     * Resets the preview rotation to the default facing angle.
     */
    UFUNCTION(BlueprintCallable, Category = "Preview | Control",
              meta = (DisplayName = "Reset Preview Rotation"))
    void ResetRotation();

    /** Initializes the starting and default rotation for the preview camera. */
    UFUNCTION(BlueprintCallable, Category = "Preview | Control",
              meta = (DisplayName = "Set Initial Rotation"))
    void SetInitialRotation(float InYaw, float InPitch);

    /**
     * Zooms the camera by adjusting the SpringArm TargetArmLength.
     *
     * @param DeltaZoom  Distance to zoom (positive zooms out, negative zooms in).
     */
    UFUNCTION(BlueprintCallable, Category = "Preview | Control",
              meta = (DisplayName = "Zoom Preview"))
    void ZoomPreview(float DeltaZoom);

private:

    /** Current accumulated spring arm length. */
    float CurrentZoomLength = 250.f;



    /** Current accumulated yaw (horizontal) rotation in degrees. */
    float CurrentYaw   = 0.f;

    /** Current accumulated pitch (vertical) rotation in degrees. */
    float CurrentPitch = 0.f;

    /** Default Yaw (horizontal) camera angle in degrees. */
    float DefaultYaw = 0.f;

    /** Default Pitch (vertical) camera angle in degrees. */
    float DefaultPitch = -15.f;

    /** Helper — applies dynamic size and color selections to a target mesh component. */
    void ApplyComponentMeshAndMaterials(UStaticMeshComponent* Target,
                                        const FFurnitureComponentOptions& Options,
                                        int32 SizeIndex,
                                        int32 ColorIndex);

    void ApplyComponentMeshAndMaterials(UStaticMeshComponent* Target,
                                        const FFurnitureCabinetOptions& Options,
                                        int32 SizeIndex,
                                        int32 ColorIndex);

    /** Helper — applies dynamic size and color selections to a target door component using the dedicated door options struct. */
    void ApplyDoorMeshAndMaterials(UStaticMeshComponent* Target,
                                   const FFurnitureDoorGroup& DoorGroup,
                                   int32 SizeIndex,
                                   int32 ColorIndex,
                                   int32 SlotIndex);

    /** Dynamically adjusts the camera-mounted FillLight intensity to compensate for distance attenuation. */
    void UpdateLightIntensityForZoom();

    void EnforceLightingSettings();

    void ApplyLightingConfig(const FFurniturePreviewLightingConfig& Config);

    float ActiveBaseFillIntensity = 40000.f;
};
