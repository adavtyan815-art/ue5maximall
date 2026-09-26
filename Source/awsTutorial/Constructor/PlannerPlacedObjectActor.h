// Copyright 2026 MaxiMall. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "RoomPlannerTypes.h"
#include "PlannerPlacedObjectActor.generated.h"

class UStaticMeshComponent;
class UStaticMesh;

/**
 * One interior object placed by the Room Planner (REQ-17).
 *
 * NOT replicated: exactly like AProceduralWallActor, every machine rebuilds these
 * from the manager's replicated planner JSON, so they stay in sync (REQ-15) and are
 * saved / loaded with the project (REQ-16) without any extra networking.
 *
 * Pivot: the actor origin (Data.Location) is the object's footprint centre on its base, whatever pivot the
 * catalog mesh was authored with. Meshes taken from Datasmith scenes keep the scene origin as their pivot
 * (metres away, sometimes above the bottom), so MeshComponent is offset under SceneRoot by minus the
 * bottom centre (XY centre, lowest Z) of the mesh's local bounds. A drop point is therefore where the
 * footprint centre lands on the floor, and every rotation (buttons, wheel) turns the object about its own
 * centre. The offset lives in SceneRoot's unscaled frame, so the actor scale (row DefaultScale) scales it
 * with the mesh and the bottom centre stays on the origin at any scale.
 */
UCLASS()
class AWSTUTORIAL_API APlannerPlacedObjectActor : public AActor
{
	GENERATED_BODY()

public:
	APlannerPlacedObjectActor();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "PlannerObject")
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "PlannerObject")
	TObjectPtr<UStaticMeshComponent> MeshComponent;

	/** Current data (mirrors the manager's entry for this InstanceID). */
	UPROPERTY(BlueprintReadOnly, Category = "PlannerObject")
	FPlacedFurnitureData Data;

	/**
	 * Applies mesh, transform, scale and colour override from Data. MaterialOverrides (DT_PlannerObjects row)
	 * are applied to the freshly set mesh, per slot index, before the "original" materials are captured, so a
	 * finish colour can still be applied over them and cleared back to them. The mesh is then re-centred under
	 * the actor origin (see the class comment); materials never move it.
	 */
	UFUNCTION(BlueprintCallable, Category = "PlannerObject")
	void ApplyData(const FPlacedFurnitureData& InData, UStaticMesh* ResolvedMesh, UMaterialInterface* ColorOverrideMaterial, const TArray<FPlannerMaterialOverride>& MaterialOverrides);

	/** Enables / disables the selection outline (custom depth stencil). */
	UFUNCTION(BlueprintCallable, Category = "PlannerObject")
	void SetSelectedHighlight(bool bSelected);

	/** XY footprint radius in cm (half of the bounds extent), used for 2D picking around the actor origin (the footprint centre). */
	UFUNCTION(BlueprintPure, Category = "PlannerObject")
	float GetFootprintRadiusCm() const;

	/** Half extents of the mesh bounds in the actor's own axes, times the actor scale (cm). The box spans ±X, ±Y and 0…2·Z from the origin. */
	UFUNCTION(BlueprintPure, Category = "PlannerObject")
	FVector GetLocalHalfExtents() const;

private:
	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInterface>> OriginalMaterials;

	void ApplyColorOverride(UMaterialInterface* ColorOverrideMaterial);

	/**
	 * The mesh's local bounds as seen from SceneRoot: through MeshComponent's relative rotation and scale (identity
	 * unless a Blueprint subclass sets them), without its relative location (the pivot offset) and without the actor
	 * scale. False when there is no mesh or its bounds are empty.
	 */
	bool GetMeshBoundsUnderRoot(FBox& OutBox) const;

	/** Moves MeshComponent under SceneRoot so the bottom centre of its bounds is the actor origin (zero offset without a mesh). */
	void CentreMeshOnOrigin();
};
