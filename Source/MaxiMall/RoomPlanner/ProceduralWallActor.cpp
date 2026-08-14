// Copyright 2026 MaxiMall. All Rights Reserved.

#include "ProceduralWallActor.h"
#include "Materials/Material.h"
#include "Materials/MaterialInterface.h"
#include "MaterialDomain.h"
#include "Engine/EngineTypes.h"

AProceduralWallActor::AProceduralWallActor()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = false;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);

	WallProceduralMesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("WallProceduralMesh"));
	WallProceduralMesh->SetupAttachment(SceneRoot);
	WallProceduralMesh->bUseAsyncCooking = true;
	WallProceduralMesh->bUseComplexAsSimpleCollision = true;
	WallProceduralMesh->SetCastShadow(true);
	WallProceduralMesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	WallProceduralMesh->SetCollisionObjectType(ECC_WorldDynamic);
	WallProceduralMesh->SetCollisionResponseToAllChannels(ECR_Block);
}

void AProceduralWallActor::BeginPlay()
{
	Super::BeginPlay();
}

void AProceduralWallActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (WallProceduralMesh)
	{
		WallProceduralMesh->ClearAllMeshSections();
		WallProceduralMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}
	Super::EndPlay(EndPlayReason);
}

void AProceduralWallActor::SetWallMaterial(UMaterialInterface* NewMaterial)
{
	if (WallProceduralMesh && NewMaterial)
	{
		WallProceduralMesh->SetMaterial(0, NewMaterial);
	}
}

void AProceduralWallActor::SetSelectedHighlight(bool bSelected, int32 StencilValue)
{
	if (WallProceduralMesh)
	{
		WallProceduralMesh->SetRenderCustomDepth(bSelected);
		if (bSelected)
		{
			WallProceduralMesh->SetCustomDepthStencilValue(StencilValue);
		}
	}
}

void AProceduralWallActor::SetOpeningSelectedHighlight(int32 OpeningIndex, bool bSelected, int32 StencilValue)
{
	if (OpeningHighlightMeshes.IsValidIndex(OpeningIndex) && OpeningHighlightMeshes[OpeningIndex])
	{
		OpeningHighlightMeshes[OpeningIndex]->SetVisibility(bSelected);
	}
}

void AProceduralWallActor::ClearAllOpeningHighlights()
{
	for (UProceduralMeshComponent* Comp : OpeningHighlightMeshes)
	{
		if (Comp)
		{
			Comp->SetVisibility(false);
		}
	}
}

void AProceduralWallActor::GenerateQuad(TArray<FVector>& Vertices, TArray<int32>& Triangles, TArray<FVector>& Normals, TArray<FVector2D>& UVs,
                                         const FVector& V0, const FVector& V1, const FVector& V2, const FVector& V3,
                                         const FVector& Normal, float UVScale)
{
	int32 StartIdx = Vertices.Num();

	Vertices.Add(V0);
	Vertices.Add(V1);
	Vertices.Add(V2);
	Vertices.Add(V3);

	Normals.Add(Normal);
	Normals.Add(Normal);
	Normals.Add(Normal);
	Normals.Add(Normal);

	float Width = FVector::Distance(V0, V1);
	float Height = FVector::Distance(V0, V3);

	UVs.Add(FVector2D(0.f, 0.f));
	UVs.Add(FVector2D(Width / UVScale, 0.f));
	UVs.Add(FVector2D(Width / UVScale, Height / UVScale));
	UVs.Add(FVector2D(0.f, Height / UVScale));

	// First triangle (V0, V1, V2)
	Triangles.Add(StartIdx + 0);
	Triangles.Add(StartIdx + 1);
	Triangles.Add(StartIdx + 2);

	// Second triangle (V0, V2, V3)
	Triangles.Add(StartIdx + 0);
	Triangles.Add(StartIdx + 2);
	Triangles.Add(StartIdx + 3);
}

void AProceduralWallActor::RebuildWallMesh(const FVector2D& StartPos, const FVector2D& EndPos,
                                            FVector2D StartLeftMiterOffset, FVector2D StartRightMiterOffset,
                                            FVector2D EndLeftMiterOffset, FVector2D EndRightMiterOffset,
                                            bool bCreateCollision)
{
	if (!WallProceduralMesh)
	{
		return;
	}

	WallProceduralMesh->ClearAllMeshSections();

	for (UProceduralMeshComponent* Comp : OpeningHighlightMeshes)
	{
		if (Comp)
		{
			Comp->DestroyComponent();
		}
	}
	OpeningHighlightMeshes.Empty();

	FVector2D Dir2D = (EndPos - StartPos);
	float TotalLength = Dir2D.Size();
	if (TotalLength < 1.0f)
	{
		return;
	}

	Dir2D /= TotalLength;
	FVector2D Normal2D(-Dir2D.Y, Dir2D.X);

	float HalfThickness = WallData.Thickness * 0.5f;
	float WallHeight = WallData.Height;

	// Calculate 2D corner vertices
	FVector2D SL2D = StartPos + Normal2D * HalfThickness + StartLeftMiterOffset;
	FVector2D SR2D = StartPos - Normal2D * HalfThickness + StartRightMiterOffset;
	FVector2D EL2D = EndPos + Normal2D * HalfThickness + EndLeftMiterOffset;
	FVector2D ER2D = EndPos - Normal2D * HalfThickness + EndRightMiterOffset;

	TArray<FVector> Vertices;
	TArray<int32> Triangles;
	TArray<FVector> Normals;
	TArray<FVector2D> UVs;
	TArray<FProcMeshTangent> Tangents;

	FVector LeftNormalVector(Normal2D.X, Normal2D.Y, 0.f);
	FVector RightNormalVector(-Normal2D.X, -Normal2D.Y, 0.f);
	FVector StartNormalVector(-Dir2D.X, -Dir2D.Y, 0.f);
	FVector EndNormalVector(Dir2D.X, Dir2D.Y, 0.f);
	FVector UpVector(0.f, 0.f, 1.f);

	// Sort & filter out overlapping openings to prevent mesh corruption
	TArray<FWallOpening> SortedOpenings = WallData.Openings;
	SortedOpenings.Sort([](const FWallOpening& A, const FWallOpening& B) {
		return A.DistanceFromStart < B.DistanceFromStart;
	});

	TArray<FWallOpening> ValidOpenings;
	float LastOpeningEnd = 0.f;
	for (const FWallOpening& Op : SortedOpenings)
	{
		float OpStart = Op.DistanceFromStart - Op.Width * 0.5f;
		float OpEnd = Op.DistanceFromStart + Op.Width * 0.5f;
		if (OpStart >= LastOpeningEnd - 1.f && OpStart < TotalLength && OpEnd <= TotalLength + 1.f)
		{
			ValidOpenings.Add(Op);
			LastOpeningEnd = OpEnd;
		}
	}

	float CurrentDist = 0.f;

	for (const FWallOpening& Opening : ValidOpenings)
	{
		float OpenStart = FMath::Clamp(Opening.DistanceFromStart - Opening.Width * 0.5f, 0.f, TotalLength);
		float OpenEnd = FMath::Clamp(Opening.DistanceFromStart + Opening.Width * 0.5f, 0.f, TotalLength);

		if (OpenStart > CurrentDist)
		{
			// Solid wall section before opening
			float AlphaStart = CurrentDist / TotalLength;
			float AlphaEnd = OpenStart / TotalLength;

			FVector2D SecSL = FMath::Lerp(SL2D, EL2D, AlphaStart);
			FVector2D SecEL = FMath::Lerp(SL2D, EL2D, AlphaEnd);
			FVector2D SecSR = FMath::Lerp(SR2D, ER2D, AlphaStart);
			FVector2D SecER = FMath::Lerp(SR2D, ER2D, AlphaEnd);

			// Left Face
			GenerateQuad(Vertices, Triangles, Normals, UVs,
				FVector(SecSL.X, SecSL.Y, 0.f), FVector(SecEL.X, SecEL.Y, 0.f),
				FVector(SecEL.X, SecEL.Y, WallHeight), FVector(SecSL.X, SecSL.Y, WallHeight), LeftNormalVector);

			// Right Face
			GenerateQuad(Vertices, Triangles, Normals, UVs,
				FVector(SecER.X, SecER.Y, 0.f), FVector(SecSR.X, SecSR.Y, 0.f),
				FVector(SecSR.X, SecSR.Y, WallHeight), FVector(SecER.X, SecER.Y, WallHeight), RightNormalVector);

			// Top Face
			GenerateQuad(Vertices, Triangles, Normals, UVs,
				FVector(SecSL.X, SecSL.Y, WallHeight), FVector(SecEL.X, SecEL.Y, WallHeight),
				FVector(SecER.X, SecER.Y, WallHeight), FVector(SecSR.X, SecSR.Y, WallHeight), UpVector);
		}

		// Opening section (Wall above/below opening)
		float AlphaOpStart = OpenStart / TotalLength;
		float AlphaOpEnd = OpenEnd / TotalLength;

		FVector2D OpSL = FMath::Lerp(SL2D, EL2D, AlphaOpStart);
		FVector2D OpEL = FMath::Lerp(SL2D, EL2D, AlphaOpEnd);
		FVector2D OpSR = FMath::Lerp(SR2D, ER2D, AlphaOpStart);
		FVector2D OpER = FMath::Lerp(SR2D, ER2D, AlphaOpEnd);

		float SillZ = Opening.SillHeight;
		float LintelZ = Opening.SillHeight + Opening.Height;

		// Sub-opening wall below sill (Windows)
		if (SillZ > 0.f)
		{
			GenerateQuad(Vertices, Triangles, Normals, UVs,
				FVector(OpSL.X, OpSL.Y, 0.f), FVector(OpEL.X, OpEL.Y, 0.f),
				FVector(OpEL.X, OpEL.Y, SillZ), FVector(OpSL.X, OpSL.Y, SillZ), LeftNormalVector);

			GenerateQuad(Vertices, Triangles, Normals, UVs,
				FVector(OpER.X, OpER.Y, 0.f), FVector(OpSR.X, OpSR.Y, 0.f),
				FVector(OpSR.X, OpSR.Y, SillZ), FVector(OpER.X, OpER.Y, SillZ), RightNormalVector);

			// Sill top jamb
			GenerateQuad(Vertices, Triangles, Normals, UVs,
				FVector(OpSL.X, OpSL.Y, SillZ), FVector(OpEL.X, OpEL.Y, SillZ),
				FVector(OpER.X, OpER.Y, SillZ), FVector(OpSR.X, OpSR.Y, SillZ), UpVector);
		}

		// Sub-opening wall above lintel
		if (LintelZ < WallHeight)
		{
			GenerateQuad(Vertices, Triangles, Normals, UVs,
				FVector(OpSL.X, OpSL.Y, LintelZ), FVector(OpEL.X, OpEL.Y, LintelZ),
				FVector(OpEL.X, OpEL.Y, WallHeight), FVector(OpSL.X, OpSL.Y, WallHeight), LeftNormalVector);

			GenerateQuad(Vertices, Triangles, Normals, UVs,
				FVector(OpER.X, OpER.Y, LintelZ), FVector(OpSR.X, OpSR.Y, LintelZ),
				FVector(OpSR.X, OpSR.Y, WallHeight), FVector(OpER.X, OpER.Y, WallHeight), RightNormalVector);

			// Lintel bottom jamb
			GenerateQuad(Vertices, Triangles, Normals, UVs,
				FVector(OpEL.X, OpEL.Y, LintelZ), FVector(OpSL.X, OpSL.Y, LintelZ),
				FVector(OpSR.X, OpSR.Y, LintelZ), FVector(OpER.X, OpER.Y, LintelZ), -UpVector);

			// Top Face above lintel
			GenerateQuad(Vertices, Triangles, Normals, UVs,
				FVector(OpSL.X, OpSL.Y, WallHeight), FVector(OpEL.X, OpEL.Y, WallHeight),
				FVector(OpER.X, OpER.Y, WallHeight), FVector(OpSR.X, OpSR.Y, WallHeight), UpVector);
		}

		// Left & Right inner jamb faces
		GenerateQuad(Vertices, Triangles, Normals, UVs,
			FVector(OpSL.X, OpSL.Y, SillZ), FVector(OpSR.X, OpSR.Y, SillZ),
			FVector(OpSR.X, OpSR.Y, LintelZ), FVector(OpSL.X, OpSL.Y, LintelZ), StartNormalVector);

		GenerateQuad(Vertices, Triangles, Normals, UVs,
			FVector(OpER.X, OpER.Y, SillZ), FVector(OpEL.X, OpEL.Y, SillZ),
			FVector(OpEL.X, OpEL.Y, LintelZ), FVector(OpER.X, OpER.Y, LintelZ), EndNormalVector);

		CurrentDist = OpenEnd;

		// Generate visible highlight box for this opening
		UProceduralMeshComponent* HighlightMesh = NewObject<UProceduralMeshComponent>(this);
		HighlightMesh->CreationMethod = EComponentCreationMethod::Instance;
		HighlightMesh->RegisterComponent();
		HighlightMesh->AttachToComponent(SceneRoot, FAttachmentTransformRules::KeepRelativeTransform);
		AddInstanceComponent(HighlightMesh);
		HighlightMesh->bRenderInMainPass = true;
		HighlightMesh->bRenderCustomDepth = false;
		HighlightMesh->SetVisibility(false);
		HighlightMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		OpeningHighlightMeshes.Add(HighlightMesh);

		TArray<FVector> HVerts;
		TArray<int32> HTris;
		TArray<FVector> HNorms;
		TArray<FVector2D> HUVs;

		FVector2D OutLeft = FVector2D(LeftNormalVector.X, LeftNormalVector.Y) * 2.0f;
		FVector2D OutRight = FVector2D(RightNormalVector.X, RightNormalVector.Y) * 2.0f;
		FVector2D OutStart = FVector2D(StartNormalVector.X, StartNormalVector.Y) * 2.0f;
		FVector2D OutEnd = FVector2D(EndNormalVector.X, EndNormalVector.Y) * 2.0f;

		FVector2D H_OpSL = OpSL + OutLeft + OutStart;
		FVector2D H_OpEL = OpEL + OutLeft + OutEnd;
		FVector2D H_OpSR = OpSR + OutRight + OutStart;
		FVector2D H_OpER = OpER + OutRight + OutEnd;
		
		float H_SillZ = SillZ - 2.0f;
		float H_LintelZ = LintelZ + 2.0f;

		// Front Face
		GenerateQuad(HVerts, HTris, HNorms, HUVs,
			FVector(H_OpSL.X, H_OpSL.Y, H_SillZ), FVector(H_OpEL.X, H_OpEL.Y, H_SillZ),
			FVector(H_OpEL.X, H_OpEL.Y, H_LintelZ), FVector(H_OpSL.X, H_OpSL.Y, H_LintelZ), LeftNormalVector);
		// Back Face
		GenerateQuad(HVerts, HTris, HNorms, HUVs,
			FVector(H_OpER.X, H_OpER.Y, H_SillZ), FVector(H_OpSR.X, H_OpSR.Y, H_SillZ),
			FVector(H_OpSR.X, H_OpSR.Y, H_LintelZ), FVector(H_OpER.X, H_OpER.Y, H_LintelZ), RightNormalVector);
		// Left Face (Start Jamb)
		GenerateQuad(HVerts, HTris, HNorms, HUVs,
			FVector(H_OpSR.X, H_OpSR.Y, H_SillZ), FVector(H_OpSL.X, H_OpSL.Y, H_SillZ),
			FVector(H_OpSL.X, H_OpSL.Y, H_LintelZ), FVector(H_OpSR.X, H_OpSR.Y, H_LintelZ), StartNormalVector);
		// Right Face (End Jamb)
		GenerateQuad(HVerts, HTris, HNorms, HUVs,
			FVector(H_OpEL.X, H_OpEL.Y, H_SillZ), FVector(H_OpER.X, H_OpER.Y, H_SillZ),
			FVector(H_OpER.X, H_OpER.Y, H_LintelZ), FVector(H_OpEL.X, H_OpEL.Y, H_LintelZ), EndNormalVector);
		// Top Face
		GenerateQuad(HVerts, HTris, HNorms, HUVs,
			FVector(H_OpSL.X, H_OpSL.Y, H_LintelZ), FVector(H_OpEL.X, H_OpEL.Y, H_LintelZ),
			FVector(H_OpER.X, H_OpER.Y, H_LintelZ), FVector(H_OpSR.X, H_OpSR.Y, H_LintelZ), UpVector);
		// Bottom Face
		GenerateQuad(HVerts, HTris, HNorms, HUVs,
			FVector(H_OpEL.X, H_OpEL.Y, H_SillZ), FVector(H_OpSL.X, H_OpSL.Y, H_SillZ),
			FVector(H_OpSR.X, H_OpSR.Y, H_SillZ), FVector(H_OpER.X, H_OpER.Y, H_SillZ), -UpVector);

		HighlightMesh->CreateMeshSection(0, HVerts, HTris, HNorms, HUVs, TArray<FColor>(), TArray<FProcMeshTangent>(), false);
		
		UMaterialInterface* OpeningMat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Constructor/Materials/M_OpeningSelection.M_OpeningSelection"));
		if (OpeningMat)
		{
			HighlightMesh->SetMaterial(0, OpeningMat);
		}
		else
		{
			HighlightMesh->SetMaterial(0, WallProceduralMesh->GetMaterial(0));
		}
	}

	// Final wall section after last opening
	if (CurrentDist < TotalLength)
	{
		float AlphaStart = CurrentDist / TotalLength;
		float AlphaEnd = 1.0f;

		FVector2D SecSL = FMath::Lerp(SL2D, EL2D, AlphaStart);
		FVector2D SecEL = EL2D;
		FVector2D SecSR = FMath::Lerp(SR2D, ER2D, AlphaStart);
		FVector2D SecER = ER2D;

		// Left Face
		GenerateQuad(Vertices, Triangles, Normals, UVs,
			FVector(SecSL.X, SecSL.Y, 0.f), FVector(SecEL.X, SecEL.Y, 0.f),
			FVector(SecEL.X, SecEL.Y, WallHeight), FVector(SecSL.X, SecSL.Y, WallHeight), LeftNormalVector);

		// Right Face
		GenerateQuad(Vertices, Triangles, Normals, UVs,
			FVector(SecER.X, SecER.Y, 0.f), FVector(SecSR.X, SecSR.Y, 0.f),
			FVector(SecSR.X, SecSR.Y, WallHeight), FVector(SecER.X, SecER.Y, WallHeight), RightNormalVector);

		// Top Face
		GenerateQuad(Vertices, Triangles, Normals, UVs,
			FVector(SecSL.X, SecSL.Y, WallHeight), FVector(SecEL.X, SecEL.Y, WallHeight),
			FVector(SecER.X, SecER.Y, WallHeight), FVector(SecSR.X, SecSR.Y, WallHeight), UpVector);
	}

	// Start Cap Face (if no connected miter)
	if (StartLeftMiterOffset.IsZero() && StartRightMiterOffset.IsZero())
	{
		GenerateQuad(Vertices, Triangles, Normals, UVs,
			FVector(SR2D.X, SR2D.Y, 0.f), FVector(SL2D.X, SL2D.Y, 0.f),
			FVector(SL2D.X, SL2D.Y, WallHeight), FVector(SR2D.X, SR2D.Y, WallHeight), StartNormalVector);
	}

	// End Cap Face (if no connected miter)
	if (EndLeftMiterOffset.IsZero() && EndRightMiterOffset.IsZero())
	{
		GenerateQuad(Vertices, Triangles, Normals, UVs,
			FVector(EL2D.X, EL2D.Y, 0.f), FVector(ER2D.X, ER2D.Y, 0.f),
			FVector(ER2D.X, ER2D.Y, WallHeight), FVector(EL2D.X, EL2D.Y, WallHeight), EndNormalVector);
	}

	WallProceduralMesh->CreateMeshSection(0, Vertices, Triangles, Normals, UVs, TArray<FColor>(), Tangents, bCreateCollision);

	if (WallProceduralMesh->GetMaterial(0) == nullptr)
	{
		UMaterialInterface* BasicMat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
		if (BasicMat)
		{
			WallProceduralMesh->SetMaterial(0, BasicMat);
		}
		else
		{
			WallProceduralMesh->SetMaterial(0, UMaterial::GetDefaultMaterial(MD_Surface));
		}
	}
}
