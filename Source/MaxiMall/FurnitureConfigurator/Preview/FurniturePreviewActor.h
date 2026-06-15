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

UCLASS(Blueprintable, BlueprintType, NotPlaceable,
       meta = (DisplayName = "Furniture Preview Actor (Client Only)"))
class MAXIMALL_API AFurniturePreviewActor : public AActor
{
    GENERATED_BODY()

public:

    AFurniturePreviewActor();

    // ── Lifecycle ─────────────────────────────────────────────────────────
    virtual void BeginPlay() override;

    // ─────────────────────────────────────────────────────────────────────
    // VISUAL COMPONENTS (mirrors AShowroomBooth layout)
    // ─────────────────────────────────────────────────────────────────────

    UPROPERTY(VisibleAnywhere, Category = "Preview | Components")
    TObjectPtr<USceneComponent> PreviewRoot;

    UPROPERTY(VisibleAnywhere, Category = "Preview | Components")
    TObjectPtr<UStaticMeshComponent> CabinetMesh;

    UPROPERTY(VisibleAnywhere, Category = "Preview | Components")
    TObjectPtr<UStaticMeshComponent> DoorMeshSlot0;

    UPROPERTY(VisibleAnywhere, Category = "Preview | Components")
    TObjectPtr<UStaticMeshComponent> DoorMeshSlot1;

    UPROPERTY(VisibleAnywhere, Category = "Preview | Components")
    TObjectPtr<UStaticMeshComponent> CountertopMesh;

    UPROPERTY(VisibleAnywhere, Category = "Preview | Components")
    TObjectPtr<UStaticMeshComponent> SinkMesh;

    UPROPERTY(VisibleAnywhere, Category = "Preview | Components")
    TObjectPtr<UStaticMeshComponent> FaucetMesh;

    UPROPERTY(VisibleAnywhere, Category = "Preview | Components")
    TObjectPtr<UStaticMeshComponent> MirrorMesh;

    /** SpringArm component to handle orbit distance and rotation. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Preview | Camera")
    TObjectPtr<USpringArmComponent> SpringArm;

    /** Camera component to render the high quality viewport. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Preview | Camera")
    TObjectPtr<UCameraComponent> Camera;

    /** Backdrop mesh component for the clean studio background. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Preview | Components")
    TObjectPtr<UStaticMeshComponent> BackdropMesh;

    /** Key light for the studio setup. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Preview | Lighting")
    TObjectPtr<USpotLightComponent> KeyLight;

    /** Camera-mounted fill light (headlight). */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Preview | Lighting")
    TObjectPtr<UPointLightComponent> FillLight;

    /** Minimum pitch angle limit for camera orbit (in degrees). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Preview | Camera Clamping")
    float PitchMin = -80.f;

    /** Maximum pitch angle limit for camera orbit (in degrees). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Preview | Camera Clamping")
    float PitchMax = 80.f;

    /** Base intensity for the camera headlight (FillLight) in Lumens. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Preview | Lighting")
    float BaseFillIntensity = 40000.f;

    /** Reference zoom distance corresponding to the BaseFillIntensity. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Preview | Lighting")
    float ReferenceZoomDistance = 250.f;



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
    void LoadProductPreview(const FFurnitureProductRow& ProductData);

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

    /** Zoom limit settings. */
    static constexpr float ZoomMin = 100.f;
    static constexpr float ZoomMax = 500.f;

    /** Current accumulated yaw (horizontal) rotation in degrees. */
    float CurrentYaw   = 0.f;

    /** Current accumulated pitch (vertical) rotation in degrees. */
    float CurrentPitch = 0.f;

    /** Helper — applies mesh and material config to a target component. */
    void ApplyMeshAndMaterials(UStaticMeshComponent* Target,
                               const FFurnitureMeshMaterials& Config);

    /** Dynamically adjusts the camera-mounted FillLight intensity to compensate for distance attenuation. */
    void UpdateLightIntensityForZoom();
};
