// Copyright 2026 MaxiMall. All Rights Reserved.

#include "ProceduralWallActor.h"
#include "RoomPlannerManager.h"
#include "PlannerOpeningStyles.h"
#include "Materials/Material.h"
#include "Materials/MaterialInterface.h"
#include "MaterialDomain.h"
#include "Engine/EngineTypes.h"
#include "Engine/World.h"

const FName AProceduralWallActor::LeafComponentTag(TEXT("PlannerOpeningLeaf"));

namespace PlannerOpeningDefaults
{
	constexpr float FloorTopZ = 1.f;             // top of the planner floor slab (RoomPlannerManager::RebuildRooms)
	constexpr float WalkThroughSillCm = 11.f;    // openings below this sill are walk-through (doors, archways)
	constexpr float LeafGap = 0.5f;              // clearance between a leaf and its lining / frame
	constexpr float ThresholdTopZ = 2.5f;        // 1.5 cm above the floor
	constexpr float DoorHandleHeightCm = 100.f;  // above the floor
	constexpr float DoorLeafThickness = 4.f;
	constexpr float WindowSashThickness = 6.f;

	constexpr float LiningThickness = 2.f;       // boards covering door / archway reveals
	constexpr float CasingWidth = 7.f;
	constexpr float CasingDepth = 1.6f;
	constexpr float CasingReveal = 0.5f;         // the casing's inner edge overlaps the lining by this much
	constexpr float MinCasingWidth = 3.f;        // less room than this (corner, neighbour, ceiling): no casing on that face

	constexpr float WindowFrameWidth = 5.5f;
	constexpr float WindowFrameDepth = 7.f;
	constexpr float StoolThickness = 2.5f;       // interior window sill board
	constexpr float StoolProjection = 3.5f;      // past the interior wall face
	constexpr float StoolEars = 3.f;             // past the opening on each side

	static const FLinearColor ThresholdOak(0.30f, 0.19f, 0.11f);
	static const FLinearColor FloorFillStone(0.55f, 0.53f, 0.50f);
}

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

	DressingMesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("DressingMesh"));
	DressingMesh->SetupAttachment(SceneRoot);
	DressingMesh->bUseAsyncCooking = false; // 2D: no collision, and the synchronous path does not allocate a body setup per update
	DressingMesh->bUseComplexAsSimpleCollision = true;
	DressingMesh->SetCastShadow(true);
	// 3D: only Visibility traces (click / hover) see the trim, so it can be picked for finishing; pawns and cameras pass through it.
	DressingMesh->SetCollisionObjectType(ECC_WorldDynamic);
	DressingMesh->SetCollisionResponseToAllChannels(ECR_Ignore);
	DressingMesh->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	DressingMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	// Plan symbols are drawing aids, not objects: no shadows, no ray-traced or GI presence.
	PlanSymbolMesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("PlanSymbolMesh"));
	PlanSymbolMesh->SetupAttachment(SceneRoot);
	PlanSymbolMesh->SetCastShadow(false);
	PlanSymbolMesh->bVisibleInRayTracing = false;
	PlanSymbolMesh->bAffectDynamicIndirectLighting = false;
	PlanSymbolMesh->bAffectDistanceFieldLighting = false;
	PlanSymbolMesh->bVisibleInReflectionCaptures = false;
	PlanSymbolMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
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
	if (DressingMesh) DressingMesh->ClearAllMeshSections();
	if (PlanSymbolMesh) PlanSymbolMesh->ClearAllMeshSections();
	for (UProceduralMeshComponent* Leaf : LeafMeshes)
	{
		if (Leaf)
		{
			Leaf->ClearAllMeshSections();
			Leaf->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		}
	}
	BaseWallMaterial = nullptr;
	WallSelectionMaterial = nullptr;
	OpeningSelectionMaterial = nullptr;
	Super::EndPlay(EndPlayReason);
}

void AProceduralWallActor::SetWallMaterial(UMaterialInterface* NewMaterial)
{
	BaseWallMaterial = NewMaterial;
	if (BaseWallMaterial)
	{
		ApplyWallSectionMaterials();
	}
}

void AProceduralWallActor::SetSelectedHighlight(bool bSelected, int32 StencilValue)
{
	SetSelectedFaceHighlight(bSelected, INDEX_NONE);
}

void AProceduralWallActor::SetSelectedFaceHighlight(bool bSelected, int32 Face)
{
	if (!WallProceduralMesh)
	{
		return;
	}

	// Disable Custom Depth Stencil pass
	WallProceduralMesh->SetRenderCustomDepth(false);

	bHighlightShown = bSelected;
	HighlightFace = (Face == LeftFaceSection || Face == RightFaceSection) ? Face : INDEX_NONE;
	ApplyWallSectionMaterials();
}

void AProceduralWallActor::SetFinishMaterial(UMaterialInterface* NewFinishMaterial)
{
	FinishMaterial = NewFinishMaterial;
	FinishMaterialRight = NewFinishMaterial;
	AppliedFinishRight = AppliedFinish;
	ApplyWallSectionMaterials(); // a highlighted section keeps the selection material until the highlight is cleared
}

void AProceduralWallActor::SetFaceFinish(bool bLeftFace, const FSurfaceFinish& Finish, UMaterialInterface* Material)
{
	if (bLeftFace)
	{
		AppliedFinish = Finish;
		FinishMaterial = Material;
	}
	else
	{
		AppliedFinishRight = Finish;
		FinishMaterialRight = Material;
	}
	ApplyWallSectionMaterials();
}

UMaterialInterface* AProceduralWallActor::ResolveBaseMaterial() const
{
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

UMaterialInterface* AProceduralWallActor::ResolveNormalMaterial() const
{
	return ResolveSectionMaterial(LeftFaceSection);
}

UMaterialInterface* AProceduralWallActor::ResolveSectionMaterial(int32 Section) const
{
	if (Section == LeftFaceSection && FinishMaterial) return FinishMaterial.Get();
	if (Section == RightFaceSection && FinishMaterialRight) return FinishMaterialRight.Get();
	return ResolveBaseMaterial();
}

UMaterialInterface* AProceduralWallActor::ResolveSelectionMaterial() const
{
	UMaterialInterface* SelMat = WallSelectionMaterial ? WallSelectionMaterial.Get() : nullptr;
	if (!SelMat)
	{
		SelMat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/RoomPlanner/Materials/M_WallSelection.M_WallSelection"));
	}
	if (!SelMat)
	{
		SelMat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Constructor/Materials/M_WallSelection.M_WallSelection"));
	}
	return SelMat;
}

void AProceduralWallActor::ApplyWallSectionMaterials()
{
	if (!WallProceduralMesh) return;
	UMaterialInterface* SelMat = bHighlightShown ? ResolveSelectionMaterial() : nullptr;
	const int32 NumSections = FMath::Min(WallProceduralMesh->GetNumSections(), NumWallSections);
	for (int32 Section = 0; Section < NumSections; ++Section)
	{
		const bool bHighlighted = SelMat && (HighlightFace == INDEX_NONE || HighlightFace == Section);
		if (UMaterialInterface* Mat = bHighlighted ? SelMat : ResolveSectionMaterial(Section))
		{
			WallProceduralMesh->SetMaterial(Section, Mat);
		}
	}
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

// ─────────────────────────────────────────────────────────────────────────────
// Presentation (2D plan / 3D view) and leaves
// ─────────────────────────────────────────────────────────────────────────────

void AProceduralWallActor::SetPresentation(bool bIn3D)
{
	bPresentation3D = bIn3D;
	if (PlanSymbolMesh)
	{
		PlanSymbolMesh->SetVisibility(!bIn3D);
	}
	if (DressingMesh)
	{
		const ECollisionEnabled::Type WantCollision = bIn3D ? ECollisionEnabled::QueryOnly : ECollisionEnabled::NoCollision;
		if (DressingMesh->GetCollisionEnabled() != WantCollision)
		{
			DressingMesh->SetCollisionEnabled(WantCollision);
		}
	}
	ApplyAllLeafPoses();
}

void AProceduralWallActor::SetDressingFaceLimits(float StartLeft, float StartRight, float EndLeft, float EndRight)
{
	DressingStartCover[0] = FMath::Max(0.f, StartLeft);
	DressingStartCover[1] = FMath::Max(0.f, StartRight);
	DressingEndCover[0] = FMath::Max(0.f, EndLeft);
	DressingEndCover[1] = FMath::Max(0.f, EndRight);
}

void AProceduralWallActor::SetLeafOpenFraction(int32 OpeningIndex, float Fraction)
{
	if (OpeningIndex < 0) return;
	if (LeafOpenFractions.Num() <= OpeningIndex)
	{
		LeafOpenFractions.SetNumZeroed(OpeningIndex + 1);
	}
	LeafOpenFractions[OpeningIndex] = Fraction;
	ApplyLeafPose(OpeningIndex);
}

int32 AProceduralWallActor::FindLeafIndex(const UPrimitiveComponent* Component) const
{
	if (!Component) return INDEX_NONE;
	for (int32 i = 0; i < LeafMeshes.Num(); ++i)
	{
		if (LeafMeshes[i] == Component)
		{
			return HasLeaf(i) ? i : INDEX_NONE;
		}
	}
	return INDEX_NONE;
}

bool AProceduralWallActor::HasLeaf(int32 OpeningIndex) const
{
	return LeafPoses.IsValidIndex(OpeningIndex) && LeafPoses[OpeningIndex].bValid
		&& LeafMeshes.IsValidIndex(OpeningIndex) && LeafMeshes[OpeningIndex] != nullptr;
}

void AProceduralWallActor::SyncLeafComponents(int32 Count)
{
	while (LeafMeshes.Num() > Count)
	{
		if (UProceduralMeshComponent* Leaf = LeafMeshes.Pop())
		{
			Leaf->DestroyComponent();
		}
	}
	while (LeafMeshes.Num() < Count)
	{
		LeafMeshes.Add(CreateLeafComponent());
	}
}

UProceduralMeshComponent* AProceduralWallActor::CreateLeafComponent()
{
	UProceduralMeshComponent* Leaf = NewObject<UProceduralMeshComponent>(this, NAME_None, RF_Transient);
	Leaf->CreationMethod = EComponentCreationMethod::Instance;
	Leaf->SetupAttachment(SceneRoot);
	Leaf->bUseAsyncCooking = true;
	Leaf->bUseComplexAsSimpleCollision = true;
	Leaf->SetCastShadow(true);
	// Only Visibility traces (3D click / hover) see a leaf; pawns and cameras pass through it.
	Leaf->SetCollisionObjectType(ECC_WorldDynamic);
	Leaf->SetCollisionResponseToAllChannels(ECR_Ignore);
	Leaf->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	Leaf->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Leaf->ComponentTags.Add(LeafComponentTag);
	Leaf->RegisterComponent();
	AddInstanceComponent(Leaf);
	return Leaf;
}

void AProceduralWallActor::ApplyLeafPose(int32 OpeningIndex)
{
	UProceduralMeshComponent* Leaf = LeafMeshes.IsValidIndex(OpeningIndex) ? LeafMeshes[OpeningIndex].Get() : nullptr;
	if (!Leaf) return;

	const bool bValid = LeafPoses.IsValidIndex(OpeningIndex) && LeafPoses[OpeningIndex].bValid;
	Leaf->SetVisibility(bValid);
	const ECollisionEnabled::Type WantCollision = (bValid && bPresentation3D) ? ECollisionEnabled::QueryOnly : ECollisionEnabled::NoCollision;
	if (Leaf->GetCollisionEnabled() != WantCollision)
	{
		Leaf->SetCollisionEnabled(WantCollision);
	}
	if (!bValid) return;

	const FLeafPose& Pose = LeafPoses[OpeningIndex];
	const float Fraction = LeafOpenFractions.IsValidIndex(OpeningIndex) ? LeafOpenFractions[OpeningIndex] : 0.f;
	const float Angle = bPresentation3D ? Pose.OpenAngle3DDeg * Fraction : Pose.PlanAngleDeg;
	Leaf->SetRelativeLocationAndRotation(Pose.Pivot, FRotator(0.f, Pose.ClosedYawDeg + Pose.SwingSign * Angle, 0.f));
}

void AProceduralWallActor::ApplyAllLeafPoses()
{
	for (int32 i = 0; i < LeafMeshes.Num(); ++i)
	{
		ApplyLeafPose(i);
	}
}

UMaterialInterface* AProceduralWallActor::ResolveOpeningMaterial(EPlannerOpeningMaterial Kind, const FLinearColor& Color) const
{
	if (ARoomPlannerManager* Manager = Cast<ARoomPlannerManager>(GetOwner()))
	{
		return Manager->GetOpeningMaterial(Kind, Color);
	}
	if (Kind == EPlannerOpeningMaterial::Glass || Kind == EPlannerOpeningMaterial::FrostedGlass)
	{
		return nullptr;
	}
	return LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
}

UMaterialInterface* AProceduralWallActor::ResolvePlanSymbolMaterial() const
{
	if (ARoomPlannerManager* Manager = Cast<ARoomPlannerManager>(GetOwner()))
	{
		return Manager->GetPlanSymbolMaterial();
	}
	return LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
}

void AProceduralWallActor::CommitSections(UProceduralMeshComponent* Component, const FPlannerSectionedMesh& Mesh, bool bCreateCollision)
{
	if (!Component) return;
	Component->ClearAllMeshSections();
	int32 SectionIndex = 0;
	for (const FPlannerSectionedMesh::FSection& Section : Mesh.Sections)
	{
		if (Section.Buffers.IsEmpty()) continue;
		UMaterialInterface* Material = nullptr;
		if (Section.Finish.IsSet())
		{
			if (ARoomPlannerManager* Manager = Cast<ARoomPlannerManager>(GetOwner()))
			{
				Material = Manager->GetFinishMaterial(Section.Finish); // trim finishing (REQ-13)
			}
		}
		if (!Material)
		{
			Material = ResolveOpeningMaterial(Section.Kind, Section.Color);
		}
		if (!Material) continue; // e.g. no glass material in this build
		const FPlannerMeshBuffers& B = Section.Buffers;
		Component->CreateMeshSection(SectionIndex, B.Vertices, B.Triangles, B.Normals, B.UVs, TArray<FColor>(), B.Tangents, bCreateCollision);
		Component->SetMaterial(SectionIndex, Material);
		++SectionIndex;
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// Wall mesh
// ─────────────────────────────────────────────────────────────────────────────

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
	if (DressingMesh)
	{
		// Trim is clickable only in 3D; 2D edits rebuild walls every frame, so they do not cook its collision there.
		DressingMesh->bUseAsyncCooking = bPresentation3D;
		DressingMesh->ClearAllMeshSections();
	}
	if (PlanSymbolMesh) PlanSymbolMesh->ClearAllMeshSections();

	for (UProceduralMeshComponent* Comp : OpeningHighlightMeshes)
	{
		if (Comp)
		{
			Comp->DestroyComponent();
		}
	}
	OpeningHighlightMeshes.Empty();

	// Leaves exist only where something renders (a dedicated server builds the wall body and its collision only).
	const bool bBuildVisuals = GetNetMode() != NM_DedicatedServer;
	const int32 NumOpenings = WallData.Openings.Num();
	LeafPoses.Reset();
	LeafPoses.SetNum(NumOpenings);
	if (LeafOpenFractions.Num() != NumOpenings)
	{
		LeafOpenFractions.SetNumZeroed(NumOpenings);
	}
	SyncLeafComponents(bBuildVisuals ? NumOpenings : 0);
	for (UProceduralMeshComponent* Leaf : LeafMeshes)
	{
		if (!Leaf) continue;
		// 2D leaves carry no collision and are rebuilt every frame of a drag: the synchronous path reuses one body setup
		// instead of allocating one and dispatching an async task per update.
		Leaf->bUseAsyncCooking = bPresentation3D;
		Leaf->ClearAllMeshSections();
	}

	FVector2D Dir2D = (EndPos - StartPos);
	const float NominalLength = Dir2D.Size();
	if (NominalLength < 1.0f)
	{
		ApplyAllLeafPoses();
		return;
	}

	Dir2D /= NominalLength;
	const FVector2D Normal2D(-Dir2D.Y, Dir2D.X);

	const float HalfThickness = WallData.Thickness * 0.5f;
	const float WallHeight = WallData.Height;

	// If corner vertices are not provided (e.g. preview wall), calculate standard rectangular corners
	const FVector2D SL2D = InSL2D.IsNearlyZero() ? (StartPos + Normal2D * HalfThickness) : InSL2D;
	const FVector2D SR2D = InSR2D.IsNearlyZero() ? (StartPos - Normal2D * HalfThickness) : InSR2D;
	const FVector2D EL2D = InEL2D.IsNearlyZero() ? (EndPos + Normal2D * HalfThickness) : InEL2D;
	const FVector2D ER2D = InER2D.IsNearlyZero() ? (EndPos - Normal2D * HalfThickness) : InER2D;

	const float TotalLength = NominalLength;

	const FVector LeftNormalVector(Normal2D.X, Normal2D.Y, 0.f);
	const FVector RightNormalVector(-Normal2D.X, -Normal2D.Y, 0.f);
	const FVector StartNormalVector(-Dir2D.X, -Dir2D.Y, 0.f);
	const FVector EndNormalVector(Dir2D.X, Dir2D.Y, 0.f);
	const FVector UpVector(0.f, 0.f, 1.f);

	// Point of the left / right face at a centreline distance D. Wall ends keep their exact (mitred) corners; points in
	// between lie square across the wall. Interpolating between the mitred end corners instead (the previous approach)
	// skewed every opening's jambs in plan by up to half the mitre offset of the wall's ends.
	auto AlongOf = [&](const FVector2D& P) { return FVector2D::DotProduct(P - StartPos, Dir2D); };
	const float LeftLo = FMath::Min(AlongOf(SL2D), AlongOf(EL2D));
	const float LeftHi = FMath::Max(AlongOf(SL2D), AlongOf(EL2D));
	const float RightLo = FMath::Min(AlongOf(SR2D), AlongOf(ER2D));
	const float RightHi = FMath::Max(AlongOf(SR2D), AlongOf(ER2D));
	auto FacePoint = [&](float D, bool bLeft) -> FVector2D
	{
		if (D <= 0.01f) return bLeft ? SL2D : SR2D;
		if (D >= TotalLength - 0.01f) return bLeft ? EL2D : ER2D;
		const float Clamped = bLeft ? FMath::Clamp(D, LeftLo, LeftHi) : FMath::Clamp(D, RightLo, RightHi);
		return StartPos + Dir2D * Clamped + Normal2D * (bLeft ? HalfThickness : -HalfThickness);
	};
	auto V3 = [](const FVector2D& P, float Z) { return FVector(P.X, P.Y, Z); };
	auto JambNormal = [](const FVector2D& A, const FVector2D& B, const FVector& Toward)
	{
		const FVector2D Edge = (B - A).GetSafeNormal();
		FVector N(-Edge.Y, Edge.X, 0.f);
		if (N.IsNearlyZero()) return Toward;
		return FVector::DotProduct(N, Toward) < 0.f ? -N : N;
	};
	// Holes are cut only where both faces run straight. An opening pushed into a mitred corner keeps square jambs (so its
	// lining, casing and leaf fit it) instead of following the mitre into the perpendicular wall. Openings away from
	// corners are unaffected (JointLo = 0 and JointHi = length on unmitred ends).
	const float JointLo = FMath::Clamp(FMath::Max(LeftLo, RightLo), 0.f, TotalLength);
	const float JointHi = FMath::Clamp(FMath::Min(LeftHi, RightHi), JointLo, TotalLength);
	auto ClampedStart = [&](const FWallOpening& Op) { return FMath::Clamp(Op.DistanceFromStart - Op.Width * 0.5f, JointLo, JointHi); };
	auto ClampedEnd = [&](const FWallOpening& Op) { return FMath::Clamp(Op.DistanceFromStart + Op.Width * 0.5f, JointLo, JointHi); };

	// Sort & filter out overlapping openings to prevent mesh corruption (indices keep the 1-to-1 match with WallData.Openings)
	TArray<int32> Order;
	Order.Reserve(NumOpenings);
	for (int32 i = 0; i < NumOpenings; ++i) Order.Add(i);
	Order.Sort([this](int32 A, int32 B) {
		return WallData.Openings[A].DistanceFromStart < WallData.Openings[B].DistanceFromStart;
	});

	TArray<int32> ValidIndices;
	float LastOpeningEnd = 0.f;
	for (int32 Index : Order)
	{
		const FWallOpening& Op = WallData.Openings[Index];
		const float OpStart = Op.DistanceFromStart - Op.Width * 0.5f;
		const float OpEnd = Op.DistanceFromStart + Op.Width * 0.5f;
		if (OpStart >= LastOpeningEnd - 1.f && OpStart < TotalLength && OpEnd <= TotalLength + 1.f)
		{
			ValidIndices.Add(Index);
			LastOpeningEnd = OpEnd;
		}
	}

	// Each face goes to its own section so it carries its own finish. Face UVs follow the manager's tile-grid frame: U runs along the
	// wall without restarting at openings and continues from wall to wall, V is the height; 1 UV = 1 m (REQ-13).
	FPlannerMeshBuffers FaceBuffers[2];
	auto FaceQuad = [&](int32 Face, const FVector& A, const FVector& B, const FVector& C, const FVector& D)
	{
		auto UV = [&](const FVector& P) { return FVector2D(FaceUV.U(Face, (float)AlongOf(FVector2D(P.X, P.Y))) / 100.f, P.Z / 100.f); };
		PlannerMeshBuilder::AddQuadUV(FaceBuffers[Face], A, B, C, D, Face == 0 ? LeftNormalVector : RightNormalVector, UV(A), UV(B), UV(C), UV(D));
	};
	// Strips across the wall depth (top, sill top, lintel soffit, reveal, end cap), given by their left-face edge (L0 -> L1) and
	// right-face edge (R0 -> R1): each half, up to the mid-plane, belongs to the face on its side, so a face finish covers it. Its UVs
	// unfold the face's grid over the edge: a horizontal strip keeps the face's columns and continues its rows into the depth; a
	// vertical strip keeps the rows and continues the columns. WrapSign (vertical strips): +1 where the strip turns off the face while
	// moving along +Dir (start reveal, end cap), -1 otherwise (end reveal, start cap).
	auto DepthStrip = [&](const FVector& L0, const FVector& L1, const FVector& R1, const FVector& R0, const FVector& Normal, float WrapSign)
	{
		const FVector M0 = (L0 + R0) * 0.5f;
		const FVector M1 = (L1 + R1) * 0.5f;
		const bool bHorizontal = FMath::Abs(Normal.Z) > 0.5f;
		auto AddHalf = [&](int32 Face, const FVector& A, const FVector& B, const FVector& C, const FVector& D, const FVector& FaceEdgePoint)
		{
			const float EdgeU = FaceUV.U(Face, (float)AlongOf(FVector2D(FaceEdgePoint.X, FaceEdgePoint.Y)));
			auto UV = [&](const FVector& P)
			{
				const float Side = (float)FVector2D::DotProduct(FVector2D(P.X, P.Y) - StartPos, Normal2D);
				const float DepthFromFace = FMath::Max(0.f, Face == 0 ? HalfThickness - Side : HalfThickness + Side);
				if (bHorizontal)
				{
					const float V = (float)P.Z + (Normal.Z > 0. ? DepthFromFace : -DepthFromFace);
					return FVector2D(FaceUV.U(Face, (float)AlongOf(FVector2D(P.X, P.Y))) / 100.f, V / 100.f);
				}
				return FVector2D((EdgeU + WrapSign * FaceUV.Sign[Face] * DepthFromFace) / 100.f, (float)P.Z / 100.f);
			};
			PlannerMeshBuilder::AddQuadUV(FaceBuffers[Face], A, B, C, D, Normal, UV(A), UV(B), UV(C), UV(D));
		};
		AddHalf(0, L0, L1, M1, M0, L0);
		AddHalf(1, M0, M1, R1, R0, R0);
	};
	float CurrentDist = 0.f;

	for (int32 Index : ValidIndices)
	{
		const FWallOpening& Opening = WallData.Openings[Index];
		const float OpenStart = ClampedStart(Opening);
		const float OpenEnd = ClampedEnd(Opening);

		if (OpenStart > CurrentDist)
		{
			// Solid wall section before opening
			const FVector2D SecSL = FacePoint(CurrentDist, true);
			const FVector2D SecEL = FacePoint(OpenStart, true);
			const FVector2D SecSR = FacePoint(CurrentDist, false);
			const FVector2D SecER = FacePoint(OpenStart, false);

			FaceQuad(0, V3(SecSL, 0.f), V3(SecEL, 0.f), V3(SecEL, WallHeight), V3(SecSL, WallHeight));
			FaceQuad(1, V3(SecER, 0.f), V3(SecSR, 0.f), V3(SecSR, WallHeight), V3(SecER, WallHeight));
			DepthStrip(V3(SecSL, WallHeight), V3(SecEL, WallHeight), V3(SecER, WallHeight), V3(SecSR, WallHeight), UpVector, 0.f);
		}

		// Opening section (Wall above/below opening)
		const FVector2D OpSL = FacePoint(OpenStart, true);
		const FVector2D OpEL = FacePoint(OpenEnd, true);
		const FVector2D OpSR = FacePoint(OpenStart, false);
		const FVector2D OpER = FacePoint(OpenEnd, false);

		const float SillZ = Opening.SillHeight;
		const float LintelZ = Opening.SillHeight + Opening.Height;

		// Sub-opening wall below sill (Windows)
		if (SillZ > 0.f)
		{
			FaceQuad(0, V3(OpSL, 0.f), V3(OpEL, 0.f), V3(OpEL, SillZ), V3(OpSL, SillZ));
			FaceQuad(1, V3(OpER, 0.f), V3(OpSR, 0.f), V3(OpSR, SillZ), V3(OpER, SillZ));
			// Sill top jamb
			DepthStrip(V3(OpSL, SillZ), V3(OpEL, SillZ), V3(OpER, SillZ), V3(OpSR, SillZ), UpVector, 0.f);
		}

		// Sub-opening wall above lintel
		if (LintelZ < WallHeight)
		{
			FaceQuad(0, V3(OpSL, LintelZ), V3(OpEL, LintelZ), V3(OpEL, WallHeight), V3(OpSL, WallHeight));
			FaceQuad(1, V3(OpER, LintelZ), V3(OpSR, LintelZ), V3(OpSR, WallHeight), V3(OpER, WallHeight));
			// Lintel bottom jamb
			DepthStrip(V3(OpEL, LintelZ), V3(OpSL, LintelZ), V3(OpSR, LintelZ), V3(OpER, LintelZ), -UpVector, 0.f);
			// Top Face above lintel
			DepthStrip(V3(OpSL, WallHeight), V3(OpEL, WallHeight), V3(OpER, WallHeight), V3(OpSR, WallHeight), UpVector, 0.f);
		}

		// Left & right reveals (jambs): each faces INTO the opening (the start jamb toward the wall's end and vice versa).
		DepthStrip(V3(OpSL, SillZ), V3(OpSL, LintelZ), V3(OpSR, LintelZ), V3(OpSR, SillZ), JambNormal(OpSL, OpSR, EndNormalVector), 1.f);
		DepthStrip(V3(OpEL, SillZ), V3(OpEL, LintelZ), V3(OpER, LintelZ), V3(OpER, SillZ), JambNormal(OpER, OpEL, StartNormalVector), -1.f);

		CurrentDist = OpenEnd;
	}

	// Final wall section after last opening
	if (CurrentDist < TotalLength)
	{
		const FVector2D SecSL = FacePoint(CurrentDist, true);
		const FVector2D SecEL = EL2D;
		const FVector2D SecSR = FacePoint(CurrentDist, false);
		const FVector2D SecER = ER2D;

		FaceQuad(0, V3(SecSL, 0.f), V3(SecEL, 0.f), V3(SecEL, WallHeight), V3(SecSL, WallHeight));
		FaceQuad(1, V3(SecER, 0.f), V3(SecSR, 0.f), V3(SecSR, WallHeight), V3(SecER, WallHeight));
		DepthStrip(V3(SecSL, WallHeight), V3(SecEL, WallHeight), V3(SecER, WallHeight), V3(SecSR, WallHeight), UpVector, 0.f);
	}

	// Start Cap Face (if open end)
	if (bStartCap)
	{
		DepthStrip(V3(SL2D, 0.f), V3(SL2D, WallHeight), V3(SR2D, WallHeight), V3(SR2D, 0.f), StartNormalVector, -1.f);
	}

	// End Cap Face (if open end)
	if (bEndCap)
	{
		DepthStrip(V3(EL2D, 0.f), V3(EL2D, WallHeight), V3(ER2D, WallHeight), V3(ER2D, 0.f), EndNormalVector, 1.f);
	}

	for (int32 Face = 0; Face < 2; ++Face)
	{
		const FPlannerMeshBuffers& B = FaceBuffers[Face];
		WallProceduralMesh->CreateMeshSection(Face == 0 ? LeftFaceSection : RightFaceSection, B.Vertices, B.Triangles, B.Normals, B.UVs,
			TArray<FColor>(), B.Tangents, bCreateCollision);
	}

	// The wall's normal materials (finish if any, else clean default white); the manager re-applies any selection highlight.
	bHighlightShown = false;
	HighlightFace = INDEX_NONE;
	ApplyWallSectionMaterials();

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

	for (int32 OpIdx = 0; OpIdx < NumOpenings; ++OpIdx)
	{
		const FWallOpening& Op = WallData.Openings[OpIdx];
		// The selection box keeps showing the opening's requested extent.
		const float H_OpenStart = FMath::Clamp(Op.DistanceFromStart - Op.Width * 0.5f, 0.f, TotalLength);
		const float H_OpenEnd = FMath::Clamp(Op.DistanceFromStart + Op.Width * 0.5f, 0.f, TotalLength);

		const FVector2D H_SL = FacePoint(H_OpenStart, true);
		const FVector2D H_EL = FacePoint(H_OpenEnd, true);
		const FVector2D H_SR = FacePoint(H_OpenStart, false);
		const FVector2D H_ER = FacePoint(H_OpenEnd, false);

		const float H_SillZ = FMath::Max(0.f, Op.SillHeight - 2.0f);
		const float H_LintelZ = Op.SillHeight + Op.Height + 2.0f;

		const FVector2D OutLeft = Normal2D * 2.0f;
		const FVector2D OutRight = -Normal2D * 2.0f;
		const FVector2D OutStart = -Dir2D * 2.0f;
		const FVector2D OutEnd = Dir2D * 2.0f;

		const FVector2D Box_SL = H_SL + OutLeft + OutStart;
		const FVector2D Box_EL = H_EL + OutLeft + OutEnd;
		const FVector2D Box_SR = H_SR + OutRight + OutStart;
		const FVector2D Box_ER = H_ER + OutRight + OutEnd;

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

		FPlannerMeshBuffers Box;
		PlannerMeshBuilder::AddQuad(Box, V3(Box_SL, H_SillZ), V3(Box_EL, H_SillZ), V3(Box_EL, H_LintelZ), V3(Box_SL, H_LintelZ), LeftNormalVector);
		PlannerMeshBuilder::AddQuad(Box, V3(Box_ER, H_SillZ), V3(Box_SR, H_SillZ), V3(Box_SR, H_LintelZ), V3(Box_ER, H_LintelZ), RightNormalVector);
		PlannerMeshBuilder::AddQuad(Box, V3(Box_SR, H_SillZ), V3(Box_SL, H_SillZ), V3(Box_SL, H_LintelZ), V3(Box_SR, H_LintelZ), StartNormalVector);
		PlannerMeshBuilder::AddQuad(Box, V3(Box_EL, H_SillZ), V3(Box_ER, H_SillZ), V3(Box_ER, H_LintelZ), V3(Box_EL, H_LintelZ), EndNormalVector);
		PlannerMeshBuilder::AddQuad(Box, V3(Box_SL, H_LintelZ), V3(Box_EL, H_LintelZ), V3(Box_ER, H_LintelZ), V3(Box_SR, H_LintelZ), UpVector);
		PlannerMeshBuilder::AddQuad(Box, V3(Box_EL, H_SillZ), V3(Box_SL, H_SillZ), V3(Box_SR, H_SillZ), V3(Box_ER, H_SillZ), -UpVector);

		HighlightMesh->CreateMeshSection(0, Box.Vertices, Box.Triangles, Box.Normals, Box.UVs, TArray<FColor>(), Box.Tangents, false);
		HighlightMesh->SetMaterial(0, OpeningMat ? OpeningMat : WallProceduralMesh->GetMaterial(0));
	}

	// Doors / windows / archways: dressing, leaves and plan symbols.
	if (bBuildVisuals)
	{
		FWallBuildFrame Frame;
		Frame.Start = StartPos;
		Frame.Dir = Dir2D;
		Frame.Normal = Normal2D;
		Frame.HalfThickness = HalfThickness;
		Frame.Length = TotalLength;
		Frame.Height = WallHeight;
		Frame.FaceLo[0] = LeftLo;
		Frame.FaceHi[0] = LeftHi;
		Frame.FaceLo[1] = RightLo;
		Frame.FaceHi[1] = RightHi;
		for (int32 FaceIdx = 0; FaceIdx < 2; ++FaceIdx)
		{
			// Branch walls at the ends (T-junctions) cover part of a face that the corner points do not describe. Only a real
			// branch limits the face: an outer mitred face keeps its extent beyond the node.
			if (DressingStartCover[FaceIdx] > 0.f)
			{
				Frame.FaceLo[FaceIdx] = FMath::Max(Frame.FaceLo[FaceIdx], DressingStartCover[FaceIdx]);
			}
			if (DressingEndCover[FaceIdx] > 0.f)
			{
				Frame.FaceHi[FaceIdx] = FMath::Min(Frame.FaceHi[FaceIdx], TotalLength - DressingEndCover[FaceIdx]);
			}
		}
		Frame.JointLo = JointLo;
		Frame.JointHi = JointHi;

		FPlannerSectionedMesh Dressing;
		FPlannerMeshBuffers Plan;
		for (int32 i = 0; i < ValidIndices.Num(); ++i)
		{
			const float PrevEnd = (i > 0) ? ClampedEnd(WallData.Openings[ValidIndices[i - 1]]) : -1.0e6f;
			const float NextStart = (i + 1 < ValidIndices.Num()) ? ClampedStart(WallData.Openings[ValidIndices[i + 1]]) : 1.0e6f;
			BuildOpeningVisuals(ValidIndices[i], Frame, PrevEnd, NextStart, Dressing, Plan);
		}
		CommitSections(DressingMesh, Dressing, bPresentation3D);
		if (PlanSymbolMesh && !Plan.IsEmpty())
		{
			PlanSymbolMesh->CreateMeshSection(0, Plan.Vertices, Plan.Triangles, Plan.Normals, Plan.UVs, TArray<FColor>(), Plan.Tangents, false);
			PlanSymbolMesh->SetMaterial(0, ResolvePlanSymbolMaterial());
		}
	}

	if (PlanSymbolMesh)
	{
		PlanSymbolMesh->SetVisibility(!bPresentation3D);
	}
	ApplyAllLeafPoses();
}

void AProceduralWallActor::BuildOpeningVisuals(int32 OpeningIndex, const FWallBuildFrame& F, float PrevOpeningEnd, float NextOpeningStart,
                                               FPlannerSectionedMesh& Dressing, FPlannerMeshBuffers& Plan)
{
	using namespace PlannerOpeningDefaults;

	const FWallOpening& Op = WallData.Openings[OpeningIndex];
	const FPlannerOpeningStyle& Style = PlannerOpeningStyles::Resolve(Op.Type, Op.Style);

	const float S0 = FMath::Clamp(Op.DistanceFromStart - Op.Width * 0.5f, F.JointLo, F.JointHi);
	const float S1 = FMath::Clamp(Op.DistanceFromStart + Op.Width * 0.5f, F.JointLo, F.JointHi);
	const float SillZ = FMath::Max(0.f, Op.SillHeight);
	const float TopZ = FMath::Min(Op.SillHeight + Op.Height, F.Height);
	if (S1 - S0 < 4.f || TopZ - SillZ < 4.f) return;

	const bool bDoor = Op.Type == EOpeningType::Door;
	const bool bWindow = Op.Type == EOpeningType::Window;
	const bool bWalkThrough = SillZ < WalkThroughSillCm;
	const float HT = F.HalfThickness;

	// Wall-aligned frame: X along the wall, Y toward the left face, Z up.
	const FVector Origin(F.Start.X, F.Start.Y, 0.f);
	const FVector AxisX(F.Dir.X, F.Dir.Y, 0.f);
	const FVector AxisY(F.Normal.X, F.Normal.Y, 0.f);
	const FVector AxisZ = FVector::UpVector;
	auto FrameBox = [&](const FVector& Min, const FVector& Max, uint8 Faces)
	{
		PlannerMeshBuilder::AddBox(Dressing.Get(EPlannerOpeningMaterial::Frame, Style.FrameColor, Op.TrimFinish), Origin, AxisX, AxisY, AxisZ, Min, Max, Faces);
	};
	const uint8 NoEnds = EPlannerBoxFace::All & ~(EPlannerBoxFace::NegX | EPlannerBoxFace::PosX);

	// Room along the wall that trim may use on each side: half the gap to a neighbouring opening, never past the face's extent
	// (a mitred inner corner ends the face early, so casings never run into the perpendicular wall).
	const float PrevLimit = PrevOpeningEnd > -1.0e5f ? (PrevOpeningEnd + S0) * 0.5f : -1.0e6f;
	const float NextLimit = NextOpeningStart < 1.0e5f ? (NextOpeningStart + S1) * 0.5f : 1.0e6f;
	auto RoomBefore = [&](int32 FaceIdx, float Edge) { return Edge - FMath::Max(F.FaceLo[FaceIdx], PrevLimit); };
	auto RoomAfter = [&](int32 FaceIdx, float Edge) { return FMath::Min(F.FaceHi[FaceIdx], NextLimit) - Edge; };

	// Floor: raised threshold for doors, a flush fill (just below the room floors, which meet at the centreline) otherwise.
	if (bWalkThrough && bDoor && Style.bThreshold)
	{
		PlannerMeshBuilder::AddBox(Dressing.Get(EPlannerOpeningMaterial::Threshold, ThresholdOak), Origin, AxisX, AxisY, AxisZ,
			FVector(S0, -HT - 1.f, FloorTopZ - 0.5f), FVector(S1, HT + 1.f, ThresholdTopZ), EPlannerBoxFace::All & ~EPlannerBoxFace::NegZ);
	}
	else if (bWalkThrough && !bWindow)
	{
		PlannerMeshBuilder::AddBox(Dressing.Get(EPlannerOpeningMaterial::Threshold, FloorFillStone), Origin, AxisX, AxisY, AxisZ,
			FVector(S0, -HT, FloorTopZ - 0.5f), FVector(S1, HT, FloorTopZ - 0.05f), EPlannerBoxFace::PosZ);
	}

	// Doors / archways: lining boards over the reveals and a mitred casing on both wall faces.
	float Lining = 0.f;
	if (!bWindow && Style.bLining)
	{
		Lining = FMath::Min(LiningThickness, (S1 - S0) * 0.1f);
		const float Base = bWalkThrough ? ((bDoor && Style.bThreshold) ? ThresholdTopZ : FloorTopZ) : SillZ;
		FrameBox(FVector(S0, -HT, Base), FVector(S0 + Lining, HT, TopZ), EPlannerBoxFace::All & ~EPlannerBoxFace::NegX);
		FrameBox(FVector(S1 - Lining, -HT, Base), FVector(S1, HT, TopZ), EPlannerBoxFace::All & ~EPlannerBoxFace::PosX);
		FrameBox(FVector(S0 + Lining, -HT, TopZ - Lining), FVector(S1 - Lining, HT, TopZ), NoEnds & ~EPlannerBoxFace::PosZ);

		if (Style.bCasing)
		{
			const float InnerStart = S0 + Lining - CasingReveal;
			const float InnerEnd = S1 - Lining + CasingReveal;
			const float InnerTop = TopZ - Lining + CasingReveal;
			const float CasingBottom = bWalkThrough ? FloorTopZ : SillZ;
			for (int32 FaceIdx = 0; FaceIdx < 2; ++FaceIdx)
			{
				float Width = CasingWidth;
				Width = FMath::Min(Width, RoomBefore(FaceIdx, InnerStart));
				Width = FMath::Min(Width, RoomAfter(FaceIdx, InnerEnd));
				Width = FMath::Min(Width, F.Height - 0.5f - InnerTop);
				if (Width < MinCasingWidth)
				{
					continue; // no room on this face (corner, neighbour, ceiling): a clipped casing looks worse than none
				}
				const float FaceSign = FaceIdx == 0 ? 1.f : -1.f;
				PlannerOpeningGeometry::AddMitredCasing(Dressing.Get(EPlannerOpeningMaterial::Frame, Style.FrameColor, Op.TrimFinish),
					Origin + AxisY * (FaceSign * HT), AxisX, AxisY * FaceSign, InnerStart, InnerEnd, CasingBottom, InnerTop, Width, CasingDepth);
			}
		}
	}

	// Windows: frame in the middle of the wall depth and a sill board on the interior face.
	float WindowFrame = 0.f;
	float WindowDepth = FMath::Max(1.f, 2.f * HT - 1.f);
	if (bWindow)
	{
		WindowFrame = FMath::Min(WindowFrameWidth, FMath::Min(S1 - S0, TopZ - SillZ) * 0.2f);
		WindowDepth = FMath::Min(WindowFrameDepth, WindowDepth);
		const float Y0 = -WindowDepth * 0.5f;
		const float Y1 = WindowDepth * 0.5f;
		FrameBox(FVector(S0, Y0, SillZ), FVector(S0 + WindowFrame, Y1, TopZ), EPlannerBoxFace::All & ~EPlannerBoxFace::NegX);
		FrameBox(FVector(S1 - WindowFrame, Y0, SillZ), FVector(S1, Y1, TopZ), EPlannerBoxFace::All & ~EPlannerBoxFace::PosX);
		FrameBox(FVector(S0 + WindowFrame, Y0, SillZ), FVector(S1 - WindowFrame, Y1, SillZ + WindowFrame), NoEnds & ~EPlannerBoxFace::NegZ);
		FrameBox(FVector(S0 + WindowFrame, Y0, TopZ - WindowFrame), FVector(S1 - WindowFrame, Y1, TopZ), NoEnds & ~EPlannerBoxFace::PosZ);

		if (!bWalkThrough)
		{
			const int32 InteriorFace = WallData.bLeftSideIsInterior ? 0 : 1;
			const float InteriorSign = WallData.bLeftSideIsInterior ? 1.f : -1.f;
			const float EarsBefore = FMath::Clamp(RoomBefore(InteriorFace, S0), 0.f, StoolEars);
			const float EarsAfter = FMath::Clamp(RoomAfter(InteriorFace, S1), 0.f, StoolEars);
			const float InnerY = InteriorSign * Y1;
			const float OuterY = InteriorSign * (HT + StoolProjection);
			FrameBox(FVector(S0 - EarsBefore, FMath::Min(InnerY, OuterY), SillZ), FVector(S1 + EarsAfter, FMath::Max(InnerY, OuterY), SillZ + StoolThickness),
				EPlannerBoxFace::All);
		}
	}

	if (Op.Type == EOpeningType::Archway || !LeafMeshes.IsValidIndex(OpeningIndex) || !LeafMeshes[OpeningIndex])
	{
		return;
	}

	// ── Leaf / sash ──
	const bool bHingeAtStart = IsHingeAtStart(Op, WallData.bLeftSideIsInterior);
	const FVector2D InteriorNormal = WallData.bLeftSideIsInterior ? F.Normal : -F.Normal;
	const FVector2D Swing = (Op.SwingDirection == EOpeningSwingDirection::Inward) ? InteriorNormal : -InteriorNormal;
	const FVector2D Along = bHingeAtStart ? F.Dir : -F.Dir;
	// +1 when the swing side is the leaf's local +Y at the closed yaw (UE yaw turns +X toward +Y).
	const float SwingSign = (Along.X * Swing.Y - Along.Y * Swing.X) >= 0.f ? 1.f : -1.f;
	const float SwingFace = FVector2D::DotProduct(Swing, F.Normal) >= 0.f ? 1.f : -1.f;

	const float Inset = bDoor ? Lining : WindowFrame;
	const float MaxThickness = FMath::Max(1.f, (bDoor ? 2.f * HT : WindowDepth) - 1.f);
	const float Thickness = FMath::Min(bDoor ? DoorLeafThickness : WindowSashThickness, MaxThickness);
	const float FloorBase = (bDoor && bWalkThrough) ? (Style.bThreshold ? ThresholdTopZ : FloorTopZ) : SillZ;
	const float LeafBottom = FloorBase + (bWindow ? WindowFrame : 0.f) + LeafGap;
	const float LeafTop = TopZ - Inset - LeafGap;
	const float LeafWidth = (S1 - S0) - 2.f * (Inset + LeafGap);
	const float LeafHeight = LeafTop - LeafBottom;
	if (LeafWidth < 4.f || LeafHeight < 4.f)
	{
		return;
	}

	// Hinge axis at the hinge-side lining / frame: on the swing-side wall face for doors (the leaf opens away from the wall),
	// on the swing face of the sash for windows (the sash sits in the middle of the wall depth).
	const float HingeAlong = bHingeAtStart ? S0 + Inset + LeafGap : S1 - Inset - LeafGap;
	const float Lateral = bDoor ? SwingFace * HT : SwingFace * Thickness * 0.5f;
	const FVector2D Pivot2D = F.Start + F.Dir * HingeAlong + F.Normal * Lateral;

	FLeafPose& Pose = LeafPoses[OpeningIndex];
	Pose.bValid = true;
	Pose.Pivot = FVector(Pivot2D.X, Pivot2D.Y, LeafBottom);
	Pose.ClosedYawDeg = FMath::RadiansToDegrees(FMath::Atan2(Along.Y, Along.X));
	Pose.SwingSign = SwingSign;
	Pose.PlanAngleDeg = bDoor ? 90.f : 25.f;
	Pose.OpenAngle3DDeg = bDoor ? 90.f : 45.f;

	FPlannerLeafParams Params;
	Params.Type = Op.Type;
	Params.Design = bDoor ? Style.LeafDesign : EPlannerLeafDesign::Flush;
	Params.GlassKind = Style.GlassKind;
	Params.Width = LeafWidth;
	Params.Height = LeafHeight;
	Params.Thickness = Thickness;
	Params.BodySign = -SwingSign; // the body lies behind the swing face, inside the wall depth
	Params.HandleZ = FloorTopZ + DoorHandleHeightCm - LeafBottom;
	Params.LeafColor = Style.LeafColor;
	Params.MetalColor = Style.MetalColor;
	Params.bSwingFaceIsInterior = (Op.SwingDirection == EOpeningSwingDirection::Inward);

	FPlannerSectionedMesh LeafMesh;
	PlannerOpeningGeometry::BuildLeaf(Params, LeafMesh);
	// Leaves are clickable only in 3D; 2D edits rebuild walls every frame, so they do not cook collision there
	// (the manager rebuilds the walls once when the view switches to 3D).
	CommitSections(LeafMeshes[OpeningIndex], LeafMesh, bPresentation3D);

	// Plan symbol: swing arc from the closed latch edge to the plan pose, just above the floor (doors) or the sill board (windows).
	const float ArcZ = bDoor ? FloorTopZ + 0.3f : SillZ + StoolThickness + 0.5f;
	PlannerMeshBuilder::AddArcStrip(Plan, Pivot2D, Along, Swing, FMath::DegreesToRadians(Pose.PlanAngleDeg),
		FMath::Max(0.f, LeafWidth - 1.5f), LeafWidth, ArcZ, bDoor ? 24 : 8);
}
