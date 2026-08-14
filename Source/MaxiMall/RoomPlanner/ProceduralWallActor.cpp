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
		OpeningHighlightMeshes[OpeningIndex]->SetRenderCustomDepth(bSelected);
		if (bSelected)
		{
			OpeningHighlightMeshes[OpeningIndex]->SetCustomDepthStencilValue(StencilValue);
		}
	}
}

void AProceduralWallActor::ClearAllOpeningHighlights()
{
	for (UProceduralMeshComponent* Comp : OpeningHighlightMeshes)
	{
		if (Comp)
		{
			Comp->SetRenderCustomDepth(false);
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

void AProceduralWallActor::GenerateQuadWithColor(TArray<FVector>& Vertices, TArray<int32>& Triangles,
                                                 TArray<FVector>& Normals, TArray<FVector2D>& UVs,
                                                 TArray<FColor>& VertexColors,
                                                 const FVector& V0, const FVector& V1,
                                                 const FVector& V2, const FVector& V3,
                                                 const FVector& Normal, const FColor& Color)
{
	int32 BaseIndex = Vertices.Num();

	Vertices.Add(V0);
	Vertices.Add(V1);
	Vertices.Add(V2);
	Vertices.Add(V3);

	Normals.Add(Normal);
	Normals.Add(Normal);
	Normals.Add(Normal);
	Normals.Add(Normal);

	UVs.Add(FVector2D(0.f, 0.f));
	UVs.Add(FVector2D(1.f, 0.f));
	UVs.Add(FVector2D(1.f, 1.f));
	UVs.Add(FVector2D(0.f, 1.f));

	VertexColors.Add(Color);
	VertexColors.Add(Color);
	VertexColors.Add(Color);
	VertexColors.Add(Color);

	Triangles.Add(BaseIndex);
	Triangles.Add(BaseIndex + 1);
	Triangles.Add(BaseIndex + 2);

	Triangles.Add(BaseIndex);
	Triangles.Add(BaseIndex + 2);
	Triangles.Add(BaseIndex + 3);
}

void AProceduralWallActor::SetOpeningCADSymbolsVisibility(bool bVisible)
{
	for (UProceduralMeshComponent* Comp : OpeningHighlightMeshes)
	{
		if (Comp)
		{
			Comp->SetVisibility(bVisible);
		}
	}
}

void AProceduralWallActor::BuildOpeningCADVisuals(UProceduralMeshComponent* MeshComp,
                                                  const FWallOpening& Opening,
                                                  const FVector2D& OpSL, const FVector2D& OpEL,
                                                  const FVector2D& OpSR, const FVector2D& OpER,
                                                  const FVector2D& Dir2D, const FVector2D& Normal2D,
                                                  float WallHeight)
{
	if (!MeshComp) return;

	MeshComp->SetCastShadow(false);
	MeshComp->CastShadow = false;
	MeshComp->bCastDynamicShadow = false;

	TArray<FVector> Verts;
	TArray<int32> Tris;
	TArray<FVector> Norms;
	TArray<FVector2D> UVs;
	TArray<FColor> Colors;
	TArray<FProcMeshTangent> Tangents;

	float TopZ = WallHeight + 1.0f;
	float ExtZ = WallHeight + 1.8f;
	FVector Up(0.f, 0.f, 1.f);

	FLinearColor DynamicMatColor;

	if (Opening.Type == EOpeningType::Door)
	{
		FColor DoorColor(245, 160, 45, 255); // Vibrant Amber CAD Door Tone
		DynamicMatColor = FLinearColor(0.96f, 0.63f, 0.18f, 1.0f);

		// 1. Door Jamb Edge Lines (Start & End Jambs at Wall Top)
		float LineThick = 3.0f;
		FVector2D HalfThickDir = Dir2D * (LineThick * 0.5f);

		// Start Jamb Line
		GenerateQuadWithColor(Verts, Tris, Norms, UVs, Colors,
			FVector(OpSL.X - HalfThickDir.X, OpSL.Y - HalfThickDir.Y, TopZ),
			FVector(OpSL.X + HalfThickDir.X, OpSL.Y + HalfThickDir.Y, TopZ),
			FVector(OpSR.X + HalfThickDir.X, OpSR.Y + HalfThickDir.Y, TopZ),
			FVector(OpSR.X - HalfThickDir.X, OpSR.Y - HalfThickDir.Y, TopZ),
			Up, DoorColor);

		// End Jamb Line
		GenerateQuadWithColor(Verts, Tris, Norms, UVs, Colors,
			FVector(OpEL.X - HalfThickDir.X, OpEL.Y - HalfThickDir.Y, TopZ),
			FVector(OpEL.X + HalfThickDir.X, OpEL.Y + HalfThickDir.Y, TopZ),
			FVector(OpER.X + HalfThickDir.X, OpER.Y + HalfThickDir.Y, TopZ),
			FVector(OpER.X - HalfThickDir.X, OpER.Y - HalfThickDir.Y, TopZ),
			Up, DoorColor);

		// 2. 90° Door Leaf (Thick rectangular strip swung into the room from OpSL along +Normal2D)
		float LeafThick = 4.5f;
		float LeafLen = Opening.Width;
		FVector2D LeafHinge = OpSL;
		FVector2D LeafTip = LeafHinge + Normal2D * LeafLen;
		FVector2D LeafOffset = Dir2D * LeafThick;

		GenerateQuadWithColor(Verts, Tris, Norms, UVs, Colors,
			FVector(LeafHinge.X, LeafHinge.Y, TopZ),
			FVector(LeafHinge.X + LeafOffset.X, LeafHinge.Y + LeafOffset.Y, TopZ),
			FVector(LeafTip.X + LeafOffset.X, LeafTip.Y + LeafOffset.Y, TopZ),
			FVector(LeafTip.X, LeafTip.Y, TopZ),
			Up, DoorColor);

		// 3. 90° Door Swing Arc (Connecting OpEL to LeafTip around LeafHinge)
		const int32 NumArcSteps = 24;
		float ArcRadius = Opening.Width;
		float ArcThickness = 3.0f;

		FVector2D PrevArcInner = OpEL;
		FVector2D PrevArcOuter = OpEL + (OpEL - LeafHinge).GetSafeNormal() * ArcThickness;

		for (int32 Step = 1; Step <= NumArcSteps; ++Step)
		{
			float AngleRad = (PI * 0.5f) * ((float)Step / (float)NumArcSteps);
			FVector2D ArcDir = Dir2D * FMath::Cos(AngleRad) + Normal2D * FMath::Sin(AngleRad);
			FVector2D CurrArcInner = LeafHinge + ArcDir * ArcRadius;
			FVector2D CurrArcOuter = LeafHinge + ArcDir * (ArcRadius + ArcThickness);

			GenerateQuadWithColor(Verts, Tris, Norms, UVs, Colors,
				FVector(PrevArcInner.X, PrevArcInner.Y, TopZ),
				FVector(CurrArcInner.X, CurrArcInner.Y, TopZ),
				FVector(CurrArcOuter.X, CurrArcOuter.Y, TopZ),
				FVector(PrevArcOuter.X, PrevArcOuter.Y, TopZ),
				Up, DoorColor);

			PrevArcInner = CurrArcInner;
			PrevArcOuter = CurrArcOuter;
		}
	}
	else // Window
	{
		FColor FrameColor(80, 80, 85, 255);       // Dark Slate Outline Frame
		FColor GlassColor(0, 235, 255, 255);      // Brilliant Electric Cyan Glass
		DynamicMatColor = FLinearColor(0.0f, 0.92f, 1.0f, 1.0f);

		float FrameThick = 2.5f;

		// 1. Left & Right Jamb Lines
		FVector2D HalfThickDir = Dir2D * (FrameThick * 0.5f);
		GenerateQuadWithColor(Verts, Tris, Norms, UVs, Colors,
			FVector(OpSL.X - HalfThickDir.X, OpSL.Y - HalfThickDir.Y, TopZ),
			FVector(OpSL.X + HalfThickDir.X, OpSL.Y + HalfThickDir.Y, TopZ),
			FVector(OpSR.X + HalfThickDir.X, OpSR.Y + HalfThickDir.Y, TopZ),
			FVector(OpSR.X - HalfThickDir.X, OpSR.Y - HalfThickDir.Y, TopZ),
			Up, FrameColor);

		GenerateQuadWithColor(Verts, Tris, Norms, UVs, Colors,
			FVector(OpEL.X - HalfThickDir.X, OpEL.Y - HalfThickDir.Y, TopZ),
			FVector(OpEL.X + HalfThickDir.X, OpEL.Y + HalfThickDir.Y, TopZ),
			FVector(OpER.X + HalfThickDir.X, OpER.Y + HalfThickDir.Y, TopZ),
			FVector(OpER.X - HalfThickDir.X, OpER.Y - HalfThickDir.Y, TopZ),
			Up, FrameColor);

		// 2. Outer & Inner Border Lines
		FVector2D HalfNorm = Normal2D * (FrameThick * 0.5f);
		GenerateQuadWithColor(Verts, Tris, Norms, UVs, Colors,
			FVector(OpSL.X - HalfNorm.X, OpSL.Y - HalfNorm.Y, TopZ),
			FVector(OpEL.X - HalfNorm.X, OpEL.Y - HalfNorm.Y, TopZ),
			FVector(OpEL.X + HalfNorm.X, OpEL.Y + HalfNorm.Y, TopZ),
			FVector(OpSL.X + HalfNorm.X, OpSL.Y + HalfNorm.Y, TopZ),
			Up, FrameColor);

		GenerateQuadWithColor(Verts, Tris, Norms, UVs, Colors,
			FVector(OpSR.X - HalfNorm.X, OpSR.Y - HalfNorm.Y, TopZ),
			FVector(OpER.X - HalfNorm.X, OpER.Y - HalfNorm.Y, TopZ),
			FVector(OpER.X + HalfNorm.X, OpER.Y + HalfNorm.Y, TopZ),
			FVector(OpSR.X + HalfNorm.X, OpSR.Y + HalfNorm.Y, TopZ),
			Up, FrameColor);

		// 3. Thick Cyan Glass Block Pane across the entire window cutout
		float GlassThick = 12.0f; // Thick, prominent glass block
		FVector2D CenterS = (OpSL + OpSR) * 0.5f;
		FVector2D CenterE = (OpEL + OpER) * 0.5f;
		FVector2D GlassOffset = Normal2D * (GlassThick * 0.5f);

		GenerateQuadWithColor(Verts, Tris, Norms, UVs, Colors,
			FVector(CenterS.X - GlassOffset.X, CenterS.Y - GlassOffset.Y, ExtZ),
			FVector(CenterE.X - GlassOffset.X, CenterE.Y - GlassOffset.Y, ExtZ),
			FVector(CenterE.X + GlassOffset.X, CenterE.Y + GlassOffset.Y, ExtZ),
			FVector(CenterS.X + GlassOffset.X, CenterS.Y + GlassOffset.Y, ExtZ),
			Up, GlassColor);
	}

	MeshComp->CreateMeshSection(0, Verts, Tris, Norms, UVs, Colors, Tangents, false);

	// Apply Dynamic Material with Unlit Emissive Color
	UMaterialInterface* BaseMat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/LevelPrototyping/Materials/M_Solid.M_Solid"));
	if (!BaseMat)
	{
		BaseMat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/RoomPlanner/Materials/M_OpeningSelection.M_OpeningSelection"));
	}
	if (!BaseMat)
	{
		BaseMat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	}

	if (BaseMat)
	{
		UMaterialInstanceDynamic* DynMat = UMaterialInstanceDynamic::Create(BaseMat, MeshComp);
		if (DynMat)
		{
			DynMat->SetVectorParameterValue(TEXT("Color"), DynamicMatColor);
			DynMat->SetVectorParameterValue(TEXT("BaseColor"), DynamicMatColor);
			DynMat->SetVectorParameterValue(TEXT("EmissiveColor"), DynamicMatColor * 1.5f);
			MeshComp->SetMaterial(0, DynMat);
		}
		else
		{
			MeshComp->SetMaterial(0, BaseMat);
		}
	}
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
		}

		// Left & Right inner jamb faces
		GenerateQuad(Vertices, Triangles, Normals, UVs,
			FVector(OpSL.X, OpSL.Y, SillZ), FVector(OpSR.X, OpSR.Y, SillZ),
			FVector(OpSR.X, OpSR.Y, LintelZ), FVector(OpSL.X, OpSL.Y, LintelZ), StartNormalVector);

		GenerateQuad(Vertices, Triangles, Normals, UVs,
			FVector(OpER.X, OpER.Y, SillZ), FVector(OpEL.X, OpEL.Y, SillZ),
			FVector(OpEL.X, OpEL.Y, LintelZ), FVector(OpER.X, OpER.Y, LintelZ), EndNormalVector);

		CurrentDist = OpenEnd;

		// Generate dedicated architectural CAD blueprint overlay for this opening
		UProceduralMeshComponent* HighlightMesh = NewObject<UProceduralMeshComponent>(this);
		HighlightMesh->CreationMethod = EComponentCreationMethod::Instance;
		HighlightMesh->RegisterComponent();
		HighlightMesh->AttachToComponent(SceneRoot, FAttachmentTransformRules::KeepRelativeTransform);
		AddInstanceComponent(HighlightMesh);
		HighlightMesh->bRenderInMainPass = true;
		HighlightMesh->bRenderCustomDepth = false;
		HighlightMesh->SetVisibility(true);
		HighlightMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		OpeningHighlightMeshes.Add(HighlightMesh);

		BuildOpeningCADVisuals(HighlightMesh, Opening, OpSL, OpEL, OpSR, OpER, Dir2D, Normal2D, WallHeight);
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
