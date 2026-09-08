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

	/** Applies mesh, transform, scale and colour override from Data. */
	UFUNCTION(BlueprintCallable, Category = "PlannerObject")
	void ApplyData(const FPlacedFurnitureData& InData, UStaticMesh* ResolvedMesh, UMaterialInterface* ColorOverrideMaterial);

	/** Enables / disables the selection outline (custom depth stencil). */
	UFUNCTION(BlueprintCallable, Category = "PlannerObject")
	void SetSelectedHighlight(bool bSelected);

	/** XY footprint radius in cm (half of the bounds extent), used for 2D picking. */
	UFUNCTION(BlueprintPure, Category = "PlannerObject")
	float GetFootprintRadiusCm() const;

	/** Half extents of the mesh bounds in local space (cm). */
	UFUNCTION(BlueprintPure, Category = "PlannerObject")
	FVector GetLocalHalfExtents() const;

private:
	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInterface>> OriginalMaterials;

	void ApplyColorOverride(UMaterialInterface* ColorOverrideMaterial);
};
