// Copyright 2026 MaxiMall. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ProceduralMeshComponent.h"
#include "RoomPlannerTypes.h"
#include "ProceduralWallActor.generated.h"

UCLASS()
class AWSTUTORIAL_API AProceduralWallActor : public AActor
{
	GENERATED_BODY()

public:
	AProceduralWallActor();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Wall")
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Wall")
	TObjectPtr<UProceduralMeshComponent> WallProceduralMesh;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wall")
	FWallSegment WallData;

	/** Rebuilds the procedural 3D wall geometry taking exact corner vertices and openings into account. */
	UFUNCTION(BlueprintCallable, Category = "Wall")
	void RebuildWallMesh(const FVector2D& StartPos, const FVector2D& EndPos,
	                     FVector2D InSL2D = FVector2D::ZeroVector,
	                     FVector2D InSR2D = FVector2D::ZeroVector,
	                     FVector2D InEL2D = FVector2D::ZeroVector,
	                     FVector2D InER2D = FVector2D::ZeroVector,
	                     bool bStartCap = true,
	                     bool bEndCap = true,
	                     bool bCreateCollision = true);

	UFUNCTION(BlueprintCallable, Category = "Wall")
	void SetWallMaterial(UMaterialInterface* NewMaterial);

	UFUNCTION(BlueprintCallable, Category = "Wall")
	void SetSelectedHighlight(bool bSelected, int32 StencilValue = 2);

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Wall")
	TArray<TObjectPtr<UProceduralMeshComponent>> OpeningHighlightMeshes;

	/** The normal base material for the wall (defaults to standard white surface material). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wall|Materials")
	TObjectPtr<UMaterialInterface> BaseWallMaterial;

	/** The material applied to the wall ONLY when selected (defaults to M_WallSelection). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wall|Materials")
	TObjectPtr<UMaterialInterface> WallSelectionMaterial;

	/** The material applied to the opening highlight box ONLY when selected (defaults to M_OpeningSelection). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wall|Materials")
	TObjectPtr<UMaterialInterface> OpeningSelectionMaterial;

	UFUNCTION(BlueprintCallable, Category = "Wall")
	void SetOpeningSelectedHighlight(int32 OpeningIndex, bool bSelected, int32 StencilValue = 2);

	UFUNCTION(BlueprintCallable, Category = "Wall")
	void ClearAllOpeningHighlights();

private:
	void GenerateQuad(TArray<FVector>& Vertices, TArray<int32>& Triangles, TArray<FVector>& Normals, TArray<FVector2D>& UVs,
	                  const FVector& V0, const FVector& V1, const FVector& V2, const FVector& V3,
	                  const FVector& Normal, float UVScale = 100.f);
};
