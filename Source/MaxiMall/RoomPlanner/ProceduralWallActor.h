// Copyright 2026 MaxiMall. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ProceduralMeshComponent.h"
#include "RoomPlannerTypes.h"
#include "ProceduralWallActor.generated.h"

UCLASS()
class MAXIMALL_API AProceduralWallActor : public AActor
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

	/** Rebuilds the procedural 3D wall geometry taking mitered corner offsets and openings into account. */
	UFUNCTION(BlueprintCallable, Category = "Wall")
	void RebuildWallMesh(const FVector2D& StartPos, const FVector2D& EndPos,
	                     FVector2D StartLeftMiterOffset = FVector2D::ZeroVector,
	                     FVector2D StartRightMiterOffset = FVector2D::ZeroVector,
	                     FVector2D EndLeftMiterOffset = FVector2D::ZeroVector,
	                     FVector2D EndRightMiterOffset = FVector2D::ZeroVector,
	                     bool bCreateCollision = true);

	UFUNCTION(BlueprintCallable, Category = "Wall")
	void SetWallMaterial(UMaterialInterface* NewMaterial);

	UFUNCTION(BlueprintCallable, Category = "Wall")
	void SetSelectedHighlight(bool bSelected, int32 StencilValue = 2);

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Wall")
	TArray<TObjectPtr<UProceduralMeshComponent>> OpeningHighlightMeshes;

	UFUNCTION(BlueprintCallable, Category = "Wall")
	void SetOpeningSelectedHighlight(int32 OpeningIndex, bool bSelected, int32 StencilValue = 2);

	UFUNCTION(BlueprintCallable, Category = "Wall")
	void ClearAllOpeningHighlights();

private:
	void GenerateQuad(TArray<FVector>& Vertices, TArray<int32>& Triangles, TArray<FVector>& Normals, TArray<FVector2D>& UVs,
	                  const FVector& V0, const FVector& V1, const FVector& V2, const FVector& V3,
	                  const FVector& Normal, float UVScale = 100.f);
};
