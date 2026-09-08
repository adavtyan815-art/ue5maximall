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
	BaseWallMaterial = nullptr;
	WallSelectionMaterial = nullptr;
	OpeningSelectionMaterial = nullptr;
	Super::EndPlay(EndPlayReason);
}

void AProceduralWallActor::SetWallMaterial(UMaterialInterface* NewMaterial)
{
	BaseWallMaterial = NewMaterial;
	if (WallProceduralMesh && BaseWallMaterial)
	{
		WallProceduralMesh->SetMaterial(0, BaseWallMaterial);
	}
}

void AProceduralWallActor::SetSelectedHighlight(bool bSelected, int32 StencilValue)
{
	if (!WallProceduralMesh)
	{
		return;
	}

	// Disable Custom Depth Stencil pass
	WallProceduralMesh->SetRenderCustomDepth(false);

	if (bSelected)
	{
		// Apply M_WallSelection ONLY on selection
		UMaterialInterface* SelMat = WallSelectionMaterial ? WallSelectionMaterial.Get() : nullptr;
		if (!SelMat)
		{
			SelMat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/RoomPlanner/Materials/M_WallSelection.M_WallSelection"));
		}
		if (!SelMat)
		{
			SelMat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Constructor/Materials/M_WallSelection.M_WallSelection"));
		}

		if (SelMat)
		{
			WallProceduralMesh->SetMaterial(0, SelMat);
		}
	}
	else
	{
		// Restore the wall's own material: applied finish (paint / tile) if any, else the default white.
		if (UMaterialInterface* NormalMat = ResolveNormalMaterial())
		{
			WallProceduralMesh->SetMaterial(0, NormalMat);
		}
	}
}

void AProceduralWallActor::SetFinishMaterial(UMaterialInterface* NewFinishMaterial)
{
	FinishMaterial = NewFinishMaterial;
	if (WallProceduralMesh && WallProceduralMesh->GetNumSections() > 0)
	{
		// Only touch the live material when the wall is not currently showing the selection material.
		UMaterialInterface* Current = WallProceduralMesh->GetMaterial(0);
		const bool bShowingSelection = (Current != nullptr && Current == WallSelectionMaterial.Get());
		if (!bShowingSelection)
		{
			if (UMaterialInterface* NormalMat = ResolveNormalMaterial())
			{
				WallProceduralMesh->SetMaterial(0, NormalMat);
			}
		}
	}
}

UMaterialInterface* AProceduralWallActor::ResolveNormalMaterial() const
{
	if (FinishMaterial)
	{
		return FinishMaterial.Get();
	}
	UMaterialInterface* NormalMat = BaseWallMaterial ? BaseWallMaterial.Get() : nullptr;
	if (!NormalMat)
	{
		NormalMat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	}
	if (!NormalMat)
	{
		NormalMat = UMaterial::GetDefaultMaterial(MD_Surface);
	}
	return NormalMat;
}

bool AProceduralWallActor::IsHingeAtStart(const FWallOpening& Opening, bool bLeftSideIsInterior)
{
	// Observer stands inside the room facing the wall.
	//  - interior on the LEFT face  -> observer's right hand points toward the START node
	//  - interior on the RIGHT face -> observer's right hand points toward the END node
	const bool bRightIsStart = bLeftSideIsInterior;
	const bool bHingeRight = (Opening.SwingSide == EOpeningSwingSide::Right);
	return bHingeRight ? bRightIsStart : !bRightIsStart;
}

void AProceduralWallActor::AppendOpeningLeaf(TArray<FVector>& Vertices, TArray<int32>& Triangles, TArray<FVector>& Normals, TArray<FVector2D>& UVs,
                                             const FWallOpening& Opening, const FVector2D& StartPos, const FVector2D& Dir2D, const FVector2D& Normal2D,
                                             float TotalLength)
{
	if (Opening.Type == EOpeningType::Archway)
	{
		return; // Archways have no leaf.
	}

	const float OpenStart = FMath::Clamp(Opening.DistanceFromStart - Opening.Width * 0.5f, 0.f, TotalLength);
	const float OpenEnd = FMath::Clamp(Opening.DistanceFromStart + Opening.Width * 0.5f, 0.f, TotalLength);
	const float LeafWidth = FMath::Max(2.f, (OpenEnd - OpenStart) - 2.f);
	const float LeafThickness = (Opening.Type == EOpeningType::Door) ? 4.f : 3.f;
	const float SillZ = FMath::Max(0.f, Opening.SillHeight);
	const float TopZ = FMath::Max(SillZ + 2.f, Opening.SillHeight + Opening.Height - 1.f);

	const bool bHingeAtStart = IsHingeAtStart(Opening, WallData.bLeftSideIsInterior);
	const FVector2D InteriorNormal = WallData.bLeftSideIsInterior ? Normal2D : -Normal2D;
	const FVector2D SwingNormal = (Opening.SwingDirection == EOpeningSwingDirection::Inward) ? InteriorNormal : -InteriorNormal;

	const FVector2D Hinge2D = bHingeAtStart ? (StartPos + Dir2D * (OpenStart + 1.f)) : (StartPos + Dir2D * (OpenEnd - 1.f));
	const FVector2D AlongOpening = bHingeAtStart ? Dir2D : -Dir2D;

	const float OpenAngleDeg = (Opening.Type == EOpeningType::Door) ? 90.f : 25.f;
	const float OpenAngleRad = FMath::DegreesToRadians(OpenAngleDeg);

	// Leaf direction rotated from the wall plane toward the swing side.
	const FVector2D LeafDir = (AlongOpening * FMath::Cos(OpenAngleRad) + SwingNormal * FMath::Sin(OpenAngleRad)).GetSafeNormal();
	const FVector2D LeafPerp = FVector2D(-LeafDir.Y, LeafDir.X) * (LeafThickness * 0.5f);

	const FVector2D A2 = Hinge2D - LeafPerp;                       // hinge, side 1
	const FVector2D B2 = Hinge2D + LeafDir * LeafWidth - LeafPerp; // far edge, side 1
	const FVector2D C2 = Hinge2D + LeafDir * LeafWidth + LeafPerp; // far edge, side 2
	const FVector2D D2 = Hinge2D + LeafPerp;                       // hinge, side 2

	const FVector A0(A2.X, A2.Y, SillZ), A1(A2.X, A2.Y, TopZ);
	const FVector B0(B2.X, B2.Y, SillZ), B1(B2.X, B2.Y, TopZ);
	const FVector C0(C2.X, C2.Y, SillZ), C1(C2.X, C2.Y, TopZ);
	const FVector D0(D2.X, D2.Y, SillZ), D1(D2.X, D2.Y, TopZ);

	const FVector NSide1(-LeafPerp.X, -LeafPerp.Y, 0.f);
	const FVector NSide2(LeafPerp.X, LeafPerp.Y, 0.f);
	const FVector NFar(LeafDir.X, LeafDir.Y, 0.f);
	const FVector NHinge(-LeafDir.X, -LeafDir.Y, 0.f);

	// Side faces
	GenerateQuad(Vertices, Triangles, Normals, UVs, A0, B0, B1, A1, NSide1.GetSafeNormal());
	GenerateQuad(Vertices, Triangles, Normals, UVs, C0, D0, D1, C1, NSide2.GetSafeNormal());
	// Far edge and hinge edge
	GenerateQuad(Vertices, Triangles, Normals, UVs, B0, C0, C1, B1, NFar);
	GenerateQuad(Vertices, Triangles, Normals, UVs, D0, A0, A1, D1, NHinge);
	// Top and bottom
	GenerateQuad(Vertices, Triangles, Normals, UVs, A1, B1, C1, D1, FVector::UpVector);
	GenerateQuad(Vertices, Triangles, Normals, UVs, D0, C0, B0, A0, -FVector::UpVector);

	// Floor swing arc (doors) / sill swing arc (windows): a thin annulus strip from the wall plane to the open leaf.
	const float ArcZ = SillZ + 1.5f;
	const float OuterR = LeafWidth;
	const float InnerR = FMath::Max(1.f, LeafWidth - 3.f);
	const int32 ArcSegments = (Opening.Type == EOpeningType::Door) ? 10 : 4;
	for (int32 i = 0; i < ArcSegments; ++i)
	{
		const float T0 = OpenAngleRad * (float)i / (float)ArcSegments;
		const float T1 = OpenAngleRad * (float)(i + 1) / (float)ArcSegments;
		const FVector2D R0 = AlongOpening * FMath::Cos(T0) + SwingNormal * FMath::Sin(T0);
		const FVector2D R1 = AlongOpening * FMath::Cos(T1) + SwingNormal * FMath::Sin(T1);
		const FVector2D P0 = Hinge2D + R0 * InnerR;
		const FVector2D P1 = Hinge2D + R0 * OuterR;
		const FVector2D P2 = Hinge2D + R1 * OuterR;
		const FVector2D P3 = Hinge2D + R1 * InnerR;

		// Ensure the quad winds so its normal points up regardless of swing orientation.
		const float Cross = (P1 - P0).X * (P3 - P0).Y - (P1 - P0).Y * (P3 - P0).X;
		if (Cross >= 0.f)
		{
			GenerateQuad(Vertices, Triangles, Normals, UVs, FVector(P0.X, P0.Y, ArcZ), FVector(P1.X, P1.Y, ArcZ), FVector(P2.X, P2.Y, ArcZ), FVector(P3.X, P3.Y, ArcZ), FVector::UpVector);
		}
		else
		{
			GenerateQuad(Vertices, Triangles, Normals, UVs, FVector(P0.X, P0.Y, ArcZ), FVector(P3.X, P3.Y, ArcZ), FVector(P2.X, P2.Y, ArcZ), FVector(P1.X, P1.Y, ArcZ), FVector::UpVector);
		}
	}
}

void AProceduralWallActor::SetOpeningSelectedHighlight(int32 OpeningIndex, bool bSelected, int32 StencilValue)
{
	if (OpeningHighlightMeshes.IsValidIndex(OpeningIndex) && OpeningHighlightMeshes[OpeningIndex])
	{
		OpeningHighlightMeshes[OpeningIndex]->SetRenderCustomDepth(false);
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
                                            FVector2D InSL2D, FVector2D InSR2D,
                                            FVector2D InEL2D, FVector2D InER2D,
                                            bool bStartCap, bool bEndCap,
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
	float NominalLength = Dir2D.Size();
	if (NominalLength < 1.0f)
	{
		return;
	}

	Dir2D /= NominalLength;
	FVector2D Normal2D(-Dir2D.Y, Dir2D.X);

	float HalfThickness = WallData.Thickness * 0.5f;
	float WallHeight = WallData.Height;

	// If corner vertices are not provided (e.g. preview wall), calculate standard rectangular corners
	FVector2D SL2D = InSL2D.IsNearlyZero() ? (StartPos + Normal2D * HalfThickness) : InSL2D;
	FVector2D SR2D = InSR2D.IsNearlyZero() ? (StartPos - Normal2D * HalfThickness) : InSR2D;
	FVector2D EL2D = InEL2D.IsNearlyZero() ? (EndPos + Normal2D * HalfThickness) : InEL2D;
	FVector2D ER2D = InER2D.IsNearlyZero() ? (EndPos - Normal2D * HalfThickness) : InER2D;

	float TotalLength = NominalLength;

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
	}

	// Generate 3D red translucent selection boxes with guaranteed 1-to-1 index match to WallData.Openings
	UMaterialInterface* OpeningMat = OpeningSelectionMaterial ? OpeningSelectionMaterial.Get() : nullptr;
	if (!OpeningMat)
	{
		OpeningMat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/RoomPlanner/Materials/M_OpeningSelection.M_OpeningSelection"));
	}
	if (!OpeningMat)
	{
		OpeningMat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Constructor/Materials/M_OpeningSelection.M_OpeningSelection"));
	}

	for (int32 OpIdx = 0; OpIdx < WallData.Openings.Num(); ++OpIdx)
	{
		const FWallOpening& Op = WallData.Openings[OpIdx];
		float H_OpenStart = FMath::Clamp(Op.DistanceFromStart - Op.Width * 0.5f, 0.f, TotalLength);
		float H_OpenEnd = FMath::Clamp(Op.DistanceFromStart + Op.Width * 0.5f, 0.f, TotalLength);
		float H_AlphaStart = H_OpenStart / TotalLength;
		float H_AlphaEnd = H_OpenEnd / TotalLength;

		FVector2D H_SL = FMath::Lerp(SL2D, EL2D, H_AlphaStart);
		FVector2D H_EL = FMath::Lerp(SL2D, EL2D, H_AlphaEnd);
		FVector2D H_SR = FMath::Lerp(SR2D, ER2D, H_AlphaStart);
		FVector2D H_ER = FMath::Lerp(SR2D, ER2D, H_AlphaEnd);

		float H_SillZ = FMath::Max(0.f, Op.SillHeight - 2.0f);
		float H_LintelZ = Op.SillHeight + Op.Height + 2.0f;

		FVector2D OutLeft = FVector2D(LeftNormalVector.X, LeftNormalVector.Y) * 2.0f;
		FVector2D OutRight = FVector2D(RightNormalVector.X, RightNormalVector.Y) * 2.0f;
		FVector2D OutStart = FVector2D(StartNormalVector.X, StartNormalVector.Y) * 2.0f;
		FVector2D OutEnd = FVector2D(EndNormalVector.X, EndNormalVector.Y) * 2.0f;

		FVector2D Box_SL = H_SL + OutLeft + OutStart;
		FVector2D Box_EL = H_EL + OutLeft + OutEnd;
		FVector2D Box_SR = H_SR + OutRight + OutStart;
		FVector2D Box_ER = H_ER + OutRight + OutEnd;

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

		// Front Face
		GenerateQuad(HVerts, HTris, HNorms, HUVs,
			FVector(Box_SL.X, Box_SL.Y, H_SillZ), FVector(Box_EL.X, Box_EL.Y, H_SillZ),
			FVector(Box_EL.X, Box_EL.Y, H_LintelZ), FVector(Box_SL.X, Box_SL.Y, H_LintelZ), LeftNormalVector);
		// Back Face
		GenerateQuad(HVerts, HTris, HNorms, HUVs,
			FVector(Box_ER.X, Box_ER.Y, H_SillZ), FVector(Box_SR.X, Box_SR.Y, H_SillZ),
			FVector(Box_SR.X, Box_SR.Y, H_LintelZ), FVector(Box_ER.X, Box_ER.Y, H_LintelZ), RightNormalVector);
		// Left Face (Start Jamb)
		GenerateQuad(HVerts, HTris, HNorms, HUVs,
			FVector(Box_SR.X, Box_SR.Y, H_SillZ), FVector(Box_SL.X, Box_SL.Y, H_SillZ),
			FVector(Box_SL.X, Box_SL.Y, H_LintelZ), FVector(Box_SR.X, Box_SR.Y, H_LintelZ), StartNormalVector);
		// Right Face (End Jamb)
		GenerateQuad(HVerts, HTris, HNorms, HUVs,
			FVector(Box_EL.X, Box_EL.Y, H_SillZ), FVector(Box_ER.X, Box_ER.Y, H_SillZ),
			FVector(Box_ER.X, Box_ER.Y, H_LintelZ), FVector(Box_EL.X, Box_EL.Y, H_LintelZ), EndNormalVector);
		// Top Face
		GenerateQuad(HVerts, HTris, HNorms, HUVs,
			FVector(Box_SL.X, Box_SL.Y, H_LintelZ), FVector(Box_EL.X, Box_EL.Y, H_LintelZ),
			FVector(Box_ER.X, Box_ER.Y, H_LintelZ), FVector(Box_SR.X, Box_SR.Y, H_LintelZ), UpVector);
		// Bottom Face
		GenerateQuad(HVerts, HTris, HNorms, HUVs,
			FVector(Box_EL.X, Box_EL.Y, H_SillZ), FVector(Box_SL.X, Box_SL.Y, H_SillZ),
			FVector(Box_SR.X, Box_SR.Y, H_SillZ), FVector(Box_ER.X, Box_ER.Y, H_SillZ), -UpVector);

		HighlightMesh->CreateMeshSection(0, HVerts, HTris, HNorms, HUVs, TArray<FColor>(), TArray<FProcMeshTangent>(), false);
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

	// Start Cap Face (if open end)
	if (bStartCap)
	{
		GenerateQuad(Vertices, Triangles, Normals, UVs,
			FVector(SR2D.X, SR2D.Y, 0.f), FVector(SL2D.X, SL2D.Y, 0.f),
			FVector(SL2D.X, SL2D.Y, WallHeight), FVector(SR2D.X, SR2D.Y, WallHeight), StartNormalVector);
	}

	// End Cap Face (if open end)
	if (bEndCap)
	{
		GenerateQuad(Vertices, Triangles, Normals, UVs,
			FVector(EL2D.X, EL2D.Y, 0.f), FVector(ER2D.X, ER2D.Y, 0.f),
			FVector(ER2D.X, ER2D.Y, WallHeight), FVector(EL2D.X, EL2D.Y, WallHeight), EndNormalVector);
	}

	WallProceduralMesh->CreateMeshSection(0, Vertices, Triangles, Normals, UVs, TArray<FColor>(), Tangents, bCreateCollision);

	// Section 1: door / window leaves in their open position + swing arcs (REQ-07).
	{
		TArray<FVector> LeafVerts;
		TArray<int32> LeafTris;
		TArray<FVector> LeafNorms;
		TArray<FVector2D> LeafUVs;
		for (const FWallOpening& Opening : ValidOpenings)
		{
			AppendOpeningLeaf(LeafVerts, LeafTris, LeafNorms, LeafUVs, Opening, StartPos, Dir2D, Normal2D, TotalLength);
		}
		if (LeafVerts.Num() > 0)
		{
			WallProceduralMesh->CreateMeshSection(1, LeafVerts, LeafTris, LeafNorms, LeafUVs, TArray<FColor>(), TArray<FProcMeshTangent>(), false);
		}
	}

	// Apply the wall's normal material (finish if any, else clean default white) and the leaf material.
	UMaterialInterface* NormalMat = ResolveNormalMaterial();
	if (NormalMat)
	{
		WallProceduralMesh->SetMaterial(0, NormalMat);
	}

	UMaterialInterface* LeafMat = LeafMaterial ? LeafMaterial.Get() : nullptr;
	if (!LeafMat)
	{
		LeafMat = BaseWallMaterial ? BaseWallMaterial.Get() : nullptr;
	}
	if (!LeafMat)
	{
		LeafMat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	}
	if (LeafMat && WallProceduralMesh->GetNumSections() > 1)
	{
		WallProceduralMesh->SetMaterial(1, LeafMat);
	}
}
