// Copyright MaxiMall Project. All Rights Reserved.
// FurniturePreviewActor.h
//
// AFurniturePreviewActor — Client-local WorldInPlace preview actor.
//
// KEY GUARANTEE: bReplicates = false hard-coded. Spawned exclusively by
// AAwsTutorial_PlayerController on the owning client at the SourceBooth location.
//
// Architecture ("Real room, neutral subject"):
//   - Mirrors the AShowroomBooth mesh layout driven from a local product snapshot,
//     spawned at the booth's real world position so Lumen GI and reflections come
//     from the ACTUAL room. The level's lights, PostProcessVolumes and SkyLights
//     are never touched.
//   - The MESH rotates; the camera stays fixed. Viewing distance therefore never
//     changes during rotation - only explicit zoom moves the camera.
//   - Even illumination: preview meshes live on LIGHTING CHANNEL 1, so the world's
//     directional/rect lights (channel 0) do not light them directly - no bright
//     side/dark side, no shadows sweeping across the mesh while it turns. A small
//     shadowless two-light rig on channel 1 lights only the subject, while Lumen
//     GI and reflections from the intact room keep materials looking real.
//   - Swept clearance: before rotating, the pivot is relocated to the nearest free
//     spot (forward/up, "picked off the shelf") so a wall-backed cabinet cannot
//     clip through walls or floor during 360-degree rotation. Only if no free spot
//     exists are the few intersecting components temporarily hidden (restored on
//     EndPlay).
//   - Component isolation: only the focused mesh group is visible during preview.
//   - Stencil-250 CustomDepth isolation dims the background via PP material (post
//     only - reflections of the room stay undimmed).
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

    /**
     * Yaw offset (degrees) added to the entry view direction for THIS component.
     * The base entry view faces the booth's front (its forward axis). If a mesh is
     * authored or mounted facing a different local direction and enters Viewmode
     * showing its side, correct it here once — the offset is booth-relative, so it
     * holds for every placement and rotation of the booth in any bathroom.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera & Zoom",
              meta = (DisplayName = "Entry View Yaw Offset (deg)", ClampMin = "-180.0", ClampMax = "180.0"))
    float EntryYawOffsetDegrees = 0.f;

    // ── Subject Fill Lighting ────────────────────────────────────────────────
    // A shadowless two-light rig on LIGHTING CHANNEL 1 that lights ONLY the preview
    // meshes. The room's own lights never touch the subject (channel 0 vs 1), so the
    // subject stays evenly lit through a full rotation; Lumen GI and reflections from
    // the intact room still apply and keep materials looking like they do in the level.

    /**
     * Match the rig brightness and color to the level's real lights (recommended).
     * The subject is on channel 1, so it receives NO direct light from the room;
     * with a fixed rig intensity it reads darker/greyer than in the level whenever
     * the room's lights are brighter than the rig. When enabled, the direct
     * illuminance (lux) that the room's visible, unoccluded lights deliver at the
     * mesh's original booth position is measured at focus time, and the rig is
     * sized to deliver the same illuminance (and the lights' combined color) to
     * the subject - level-accurate brightness in every room, no hand-tuning.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Subject Fill Lighting",
              meta = (DisplayName = "Match Level Lighting (Auto)"))
    bool bMatchLevelLighting = true;

    /** Fine-tune multiplier on the matched level brightness (1 = exact match). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Subject Fill Lighting",
              meta = (DisplayName = "Level Match Intensity Scale", ClampMin = "0.1", ClampMax = "4.0", UIMin = "0.25", UIMax = "2.0", EditCondition = "bMatchLevelLighting"))
    float LevelMatchIntensityScale = 1.f;

    /**
     * Manual intensity (CANDELAS) of the soft key light (camera side, above-left).
     * Used only when "Match Level Lighting" is off, or when it measures no light
     * at the booth (e.g. purely emissive- or sky-lit rooms).
     * NOTE: intentionally renamed from the old "KeyLightIntensity" so that stale
     * Blueprint overrides saved for the previous studio rig (which defaulted this
     * to 0) do not silently switch the new rig off.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Subject Fill Lighting",
              meta = (DisplayName = "Key Light Intensity (Manual, cd)", ClampMin = "0.0", ClampMax = "5000.0", UIMin = "0.0", UIMax = "2000.0"))
    float PreviewKeyIntensity = 800.f;

    /** Color tint applied to both rig lights. Neutral white preserves PBR material color. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Subject Fill Lighting",
              meta = (DisplayName = "Light Color Tint"))
    FLinearColor LightColor = FLinearColor::White;

    /** Wrap-fill intensity as a fraction of the key (lights the opposite side; 0 = off). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Subject Fill Lighting",
              meta = (DisplayName = "Fill Intensity Ratio", ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "1.0"))
    float FillRimMultiplier = 0.4f;

    /** Key light source width (cm). Larger = softer, broader speculars on glossy surfaces. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Subject Fill Lighting",
              meta = (DisplayName = "Source Width (cm)", ClampMin = "5.0", ClampMax = "500.0", UIMin = "10.0", UIMax = "300.0"))
    float LightSourceWidth = 100.f;

    /** Key light source height (cm). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Subject Fill Lighting",
              meta = (DisplayName = "Source Height (cm)", ClampMin = "5.0", ClampMax = "500.0", UIMin = "10.0", UIMax = "300.0"))
    float LightSourceHeight = 120.f;

    /** Distance of the rig lights from the pivot (cm). Constant regardless of zoom. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Subject Fill Lighting",
              meta = (DisplayName = "Light Distance From Pivot (cm)", ClampMin = "50.0", ClampMax = "1000.0", UIMin = "100.0", UIMax = "500.0"))
    float KeyLightOffset = 200.f;

    /** Falloff attenuation radius (cm) for the rig lights. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Subject Fill Lighting",
              meta = (DisplayName = "Attenuation Radius (cm)", ClampMin = "100.0", ClampMax = "5000.0", UIMin = "200.0", UIMax = "2000.0"))
    float KeyLightAttenuationRadius = 800.f;
};

// ─────────────────────────────────────────────────────────────────────────────
// AFurniturePreviewActor
// ─────────────────────────────────────────────────────────────────────────────

UCLASS(Blueprintable, BlueprintType, NotPlaceable,
       HideCategories = (Rendering, Physics, Collision, Lighting, HLOD, Navigation, Input,
                         ActorTick, ComponentTick, LOD, Cooking, Replication, Tags,
                         TextureStreaming, RayTracing, PathTracing, AssetUserData),
       meta = (DisplayName = "Furniture Preview Actor (World In-Place)"))
class AWSTUTORIAL_API AFurniturePreviewActor : public AActor
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

    // ── Subject Fill Rig (LIGHTING CHANNEL 1, shadowless) ─────────────────
    // Lights ONLY the preview meshes (also channel 1). The room and its lights
    // (channel 0) are untouched, so the level's look is never modified.
    // Both lights are children of the SpringArm root at the pivot: their distance
    // to the subject is constant regardless of zoom, and because the camera never
    // rotates during mesh rotation, illumination is identical from every angle.
    // Shadowless lights ignore occluders, so a light "inside" a nearby wall is fine.

    /** Soft key — camera side, above and to the left of the view axis. */
    UPROPERTY(BlueprintReadOnly, Category = "Components | Preview Lighting")
    TObjectPtr<URectLightComponent> PreviewKeyLight;

    /** Wrap fill — opposite side, below-right, prevents a dark back side. */
    UPROPERTY(BlueprintReadOnly, Category = "Components | Preview Lighting")
    TObjectPtr<URectLightComponent> PreviewFillLight;

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

    /**
     * Camera vignette while the preview is active (0 = none, use the level's own).
     * Applied on the preview camera only; the level's PostProcessVolumes stay enabled.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Preview Config",
              meta = (DisplayName = "Preview Vignette Intensity", ClampMin = "0.0", ClampMax = "1.0"))
    float PreviewVignetteIntensity = 0.4f;

    /**
     * Downward tilt (degrees, negative = camera above looking down) of the entry
     * view for every component — the classic product-shot three-quarter angle.
     * 0 = perfectly level entry view.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Preview Config",
              meta = (DisplayName = "Entry View Pitch (deg)", ClampMin = "-60.0", ClampMax = "10.0"))
    float EntryPitchDegrees = -15.f;

    /**
     * Post-process MATERIALS (not volumes) to suspend while the preview is open,
     * restored exactly on exit. Needed for stencil-keyed materials like
     * M_PostProcessOutline: the Room Planner drives it via custom-depth stencil
     * values, but the preview subject also renders custom depth (stencil 250) for
     * its isolation dim, so the outline material would tint the previewed mesh.
     * Only the listed materials are pulled from the volumes' blendable arrays —
     * every other volume setting (exposure, bloom, grading) keeps applying, which
     * is required for level-accurate material appearance.
     * As a safety net for renamed assets, any volume blendable whose object name
     * contains "PostProcessOutline" is suspended as well.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Preview Config",
              meta = (DisplayName = "Post Process Materials To Suspend"))
    TArray<TSoftObjectPtr<UMaterialInterface>> PostProcessMaterialsToSuspend;

    // ── Swept-Clearance Pivot Relocation ──────────────────────────────────
    // Booths normally stand against bathroom walls, so a rotating mesh would sweep
    // through the wall (and, when pitched, the floor). Before rotation starts, the
    // pivot is moved to the nearest position where a sphere of the mesh's swept
    // radius fits - visually, the product is "picked off the shelf" to be inspected.

    /** Extra clearance (cm) added around the mesh's swept radius. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Preview Config | Clearance",
              meta = (DisplayName = "Clearance Margin (cm)", ClampMin = "0.0", ClampMax = "100.0"))
    float PivotClearanceMarginCm = 15.f;

    /** How far (cm) the pivot may be moved from the booth to find free space. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Preview Config | Clearance",
              meta = (DisplayName = "Max Pivot Search Distance (cm)", ClampMin = "0.0", ClampMax = "1000.0"))
    float MaxPivotSearchDistanceCm = 300.f;

    /**
     * If no fully free pivot exists within the search distance (very small bathrooms),
     * temporarily hide just the world components intersecting the swept sphere
     * (restored automatically when the preview closes). Off = the mesh may visibly
     * clip through nearby geometry in cramped layouts.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Preview Config | Clearance",
              meta = (DisplayName = "Hide Blocking Geometry As Fallback"))
    bool bAllowGeometryHideFallback = true;

    // ── Per-Component Configuration ────────────────────────────────────────
    // Zoom limits, exposure nudge and subject fill lighting per component type,
    // set in the BP_FurniturePreviewActor Details panel.

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
     *   3. Swept-clearance pivot relocation so 360-degree rotation cannot clip
     *      through walls/floor (see ResolveClearPivot).
     *   4. SpringArm placed at the pivot, entry view along the booth's forward
     *      axis; max zoom clamped by the wall behind the camera.
     *   5. Per-component zoom limits and subject fill rig from the matching Config.
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

    /**
     * World components temporarily hidden by the swept-clearance FALLBACK only
     * (cramped layouts where no free pivot exists). Restored to visible on EndPlay
     * and before every re-focus. This is the ONLY world state the preview may touch
     * besides hiding the source booth and the player character mesh.
     */
    TArray<TWeakObjectPtr<UPrimitiveComponent>> WIP_CachedHiddenWallComponents;

    UPROPERTY()
    TObjectPtr<UStaticMeshComponent> CurrentFocusedComponent;

    /** WorldInPlace pivot reference state for exact mesh-centered rotation. */
    FVector WIP_MeshPivotWorld      = FVector::ZeroVector;
    FVector WIP_MeshRootLocAtReset  = FVector::ZeroVector;
    FQuat   WIP_InitialMeshRootQuat = FQuat::Identity;

    // ── Private helpers ────────────────────────────────────────────────────
    FVector WIP_GetFocusPivotWorld() const;
    void    WIP_ApplyStencilIsolation();
    void    ConfigureMesh(UStaticMeshComponent* Comp) const;

    /**
     * Finds the nearest pivot position where a sphere of SweptRadius (+ margin) is
     * free of world geometry: lifts above the floor if needed, then walks forward /
     * right / left from the desired pivot. Returns the chosen position; when no free
     * spot exists it returns the best candidate and, if allowed, hides the few
     * components blocking it (cached in WIP_CachedHiddenWallComponents).
     */
    FVector ResolveClearPivot(const FVector& DesiredPivot, float SweptRadius);

    /** Restores any components hidden by the clearance fallback. */
    void RestoreClearanceHiddenComponents();

    /**
     * Measures the direct illuminance (lux) the world's channel-0 lights deliver
     * at WorldPoint, i.e. the direct lighting the mesh received there in the level:
     *   - Directional lights count only when unoccluded (indoor booths normally
     *     exclude the sun), at their intensity in lux.
     *   - Local lights (point/spot/rect) count when within attenuation range and
     *     with line of sight, converted to candelas via their configured units,
     *     with inverse-square + radial-window falloff, spot cone falloff and rect
     *     hemisphere/cosine emission respected.
     *   - The hidden source booth's own display lights (if any) still count: they
     *     lit the product in the level and the rig must reproduce them.
     * Returns the max RGB channel of the accumulated lux; OutLightColor receives
     * the lux-weighted combined light color (normalized, alpha 1). Approximation:
     * IES profiles and barn doors are ignored.
     */
    float MeasureWorldIlluminanceAt(const FVector& WorldPoint, FLinearColor& OutLightColor) const;

    // ── Suspended post-process blendables ─────────────────────────────────
    // One removed volume-blendable entry, with everything needed to put it back.
    struct FSuspendedPPBlendable
    {
        TWeakObjectPtr<class APostProcessVolume> Volume;
        TWeakObjectPtr<UObject>                  BlendableObject;
        float                                    Weight = 1.f;
    };

    TArray<FSuspendedPPBlendable> SuspendedPostProcessBlendables;

    /**
     * Removes the configured conflicting materials (plus the "PostProcessOutline"
     * name fallback) from every PostProcessVolume's blendable array, recording each
     * removal for exact restoration. Idempotent: already-removed entries are not
     * found again on a second call.
     */
    void SuspendConflictingPostProcessMaterials();

    /** Re-adds every suspended blendable with its original weight. */
    void RestoreSuspendedPostProcessMaterials();

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
