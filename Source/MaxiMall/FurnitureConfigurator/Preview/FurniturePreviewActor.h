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
class ACharacter;
class AShowroomBooth;

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
