// Copyright 2026 MaxiMall. All Rights Reserved.

// ARoomPlannerManager, 3D selection: the selection overlay (no custom depth in this project) and the 3D opening pick / drag.

#include "Constructor/RoomPlannerManager.h"
#include "Constructor/PlannerSelectionOverlay.h"
#include "Constructor/PlannerOpeningBuilder.h"
#include "Constructor/PlannerPlacedObjectActor.h"
#include "FurnitureConfigurator/ShowroomBooth.h"
#include "Components/MeshComponent.h"
#include "Components/PostProcessComponent.h"
#include "Interfaces/Interface_PostProcessVolume.h"
#include "Camera/PlayerCameraManager.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/HitResult.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "SceneView.h"
#include "SceneViewExtension.h"

namespace PlannerSelection3D
{
	constexpr float OverlayLiftCm = 0.4f;    // the overlay floats this far in front of the surface it marks
	constexpr float WalkThroughSillCm = 11.f; // as the wall builder: lower sills are doors / archways standing on the floor
	constexpr float HoleMarginCm = 9.f;      // frame around a door / window: clear of its casing (lining 2 + casing 7) and sill board
}

/**
 * Reads the exposure the renderer actually applied to this world's game view (auto exposure included), so the additive tint shows the
 * same share of white under the planner's fixed exposure and under a level's own, whatever the level's volumes do.
 */
class FPlannerExposureProbe : public FWorldSceneViewExtension
{
public:
	FPlannerExposureProbe(const FAutoRegister& AutoRegister, UWorld* InWorld) : FWorldSceneViewExtension(AutoRegister, InWorld) {}

	virtual void SetupViewFamily(FSceneViewFamily& InViewFamily) override {}
	virtual void BeginRenderViewFamily(FSceneViewFamily& InViewFamily) override {}
	virtual void SetupView(FSceneViewFamily& InViewFamily, FSceneView& InView) override
	{
		if (InView.bIsGameView && !InView.bIsSceneCapture)
		{
			const float Exposure = InView.GetLastEyeAdaptationExposure();
			if (Exposure > 0.f && FMath::IsFinite(Exposure)) LastExposure = Exposure;
		}
	}

	/** Scene luminance × this = display-linear value; 0 until a game view has been exposed. Game thread. */
	float LastExposure = 0.f;
};

// ─────────────────────────────────────────────────────────────────────────────
// 3D opening pick and drag
// ─────────────────────────────────────────────────────────────────────────────

bool ARoomPlannerManager::FindOpeningAtHit(const FHitResult& Hit, int32& OutSegmentID, int32& OutOpeningIndex, bool& bOutFaceLeft) const
{
	OutSegmentID = INDEX_NONE;
	OutOpeningIndex = INDEX_NONE;
	const AProceduralWallActor* Wall = Cast<AProceduralWallActor>(Hit.GetActor());
	if (!Wall) return false;
	int32 SegID = INDEX_NONE;
	for (const auto& Pair : WallActors)
	{
		if (Pair.Value == Wall) { SegID = Pair.Key; break; }
	}
	const FWallSegment* Seg = (SegID != INDEX_NONE) ? WallSegments.Find(SegID) : nullptr;
	if (!Seg || !Nodes.Contains(Seg->StartNodeID) || !Nodes.Contains(Seg->EndNodeID)) return false;

	const FVector2D P1 = Nodes[Seg->StartNodeID].Position;
	const FVector2D Dir = (Nodes[Seg->EndNodeID].Position - P1).GetSafeNormal();
	const FVector2D LeftNormal(-Dir.Y, Dir.X);
	const FVector2D Impact(Hit.ImpactPoint.X, Hit.ImpactPoint.Y);
	const float Along = FVector2D::DotProduct(Impact - P1, Dir);
	const float Side = FVector2D::DotProduct(Impact - P1, LeftNormal);

	int32 OpIdx = IsOpeningLeafComponent(Hit.GetComponent()) ? Wall->FindLeafIndex(Hit.GetComponent()) : INDEX_NONE;
	for (int32 i = 0; OpIdx == INDEX_NONE && i < Seg->Openings.Num(); ++i)
	{
		const FWallOpening& Op = Seg->Openings[i];
		if (FMath::Abs(Along - Op.DistanceFromStart) <= Op.Width * 0.5f + 6.f
			&& Hit.ImpactPoint.Z >= Op.SillHeight - 6.f && Hit.ImpactPoint.Z <= Op.SillHeight + Op.Height + 6.f)
		{
			OpIdx = i;
		}
	}
	if (OpIdx == INDEX_NONE) return false;

	// On a face: that face. Inside the thickness (a reveal, the frame) or on a leaf: the side the view comes from.
	if (FMath::Abs(Side) >= Seg->Thickness * 0.5f - 0.5f && !IsOpeningLeafComponent(Hit.GetComponent()))
	{
		bOutFaceLeft = Side >= 0.f;
	}
	else
	{
		bOutFaceLeft = FVector2D::DotProduct(FVector2D(Hit.TraceStart.X, Hit.TraceStart.Y) - P1, LeftNormal) >= 0.f;
	}
	OutSegmentID = SegID;
	OutOpeningIndex = OpIdx;
	return true;
}

FString ARoomPlannerManager::GetSelectionSignature() const
{
	switch (GetSelectionKind())
	{
	case EPlannerSelectionKind::Wall:
		return FString::Printf(TEXT("wall:%d:%s"), SelectedSegmentID, bSelectedWallFaceLeft ? TEXT("L") : TEXT("R"));
	case EPlannerSelectionKind::Opening:
		// The opening, whichever face it was picked from.
		return FString::Printf(TEXT("opening:%d:%s"), SelectedSegmentID, *GetOpeningID(SelectedSegmentID, SelectedOpeningIndex));
	case EPlannerSelectionKind::Floor:
	case EPlannerSelectionKind::Ceiling:
	case EPlannerSelectionKind::Baseboard:
		return FString::Printf(TEXT("room:%d:%d"), SelectedRoomID, (int32)GetSelectionKind());
	case EPlannerSelectionKind::Object:
		return TEXT("object:") + SelectedObjectID;
	case EPlannerSelectionKind::CabinetSet:
		return TEXT("set:") + SelectedCabinetSetID;
	default:
		return FString();
	}
}

bool ARoomPlannerManager::SelectOpening(int32 SegmentID, int32 OpeningIndex, bool bFaceLeft)
{
	const FWallSegment* Seg = WallSegments.Find(SegmentID);
	if (!Seg || !Seg->Openings.IsValidIndex(OpeningIndex)) return false;
	bSelectionHighlightSuppressed = false;
	SelectedRoomID = -1;
	SelectedObjectID.Empty();
	SelectedCabinetSetID.Empty();
	SelectedSegmentID = SegmentID;
	SelectedOpeningIndex = OpeningIndex;
	bSelectedWallFaceLeft = bFaceLeft;
	UpdateSelectionVisuals();
	OnWallSelected.Broadcast(SegmentID, GetWallLength(SegmentID));
	NotifySelectionChanged();
	return true;
}

bool ARoomPlannerManager::ProjectRayOntoWallFace(int32 SegmentID, bool bFaceLeft, const FVector& RayOrigin, const FVector& RayDirection, float& OutAlongCm) const
{
	const FWallSegment* Seg = WallSegments.Find(SegmentID);
	if (!Seg || !Nodes.Contains(Seg->StartNodeID) || !Nodes.Contains(Seg->EndNodeID)) return false;
	const FVector2D P1 = Nodes[Seg->StartNodeID].Position;
	const FVector2D Dir = (Nodes[Seg->EndNodeID].Position - P1).GetSafeNormal();
	if (Dir.IsNearlyZero()) return false;
	const FVector2D OutNormal = FVector2D(-Dir.Y, Dir.X) * (bFaceLeft ? 1.f : -1.f);

	// The face is a vertical plane: only the horizontal part of the ray moves toward it.
	const FVector PlanePoint(P1.X + OutNormal.X * Seg->Thickness * 0.5f, P1.Y + OutNormal.Y * Seg->Thickness * 0.5f, 0.f);
	const FVector PlaneNormal(OutNormal.X, OutNormal.Y, 0.f);
	const double Denominator = FVector::DotProduct(RayDirection, PlaneNormal);
	if (FMath::Abs(Denominator) < 1.e-4) return false;
	const double T = FVector::DotProduct(PlanePoint - RayOrigin, PlaneNormal) / Denominator;
	if (T <= 0.0) return false;
	const FVector Hit = RayOrigin + RayDirection * T;
	OutAlongCm = FVector2D::DotProduct(FVector2D(Hit.X, Hit.Y) - P1, Dir);
	return true;
}

bool ARoomPlannerManager::DragSelectedOpeningAlongRay(const FVector& RayOrigin, const FVector& RayDirection, float GrabOffsetCm)
{
	if (SelectedSegmentID == -1 || SelectedOpeningIndex == -1) return false;
	const FWallSegment* Seg = WallSegments.Find(SelectedSegmentID);
	if (!Seg || !Nodes.Contains(Seg->StartNodeID) || !Nodes.Contains(Seg->EndNodeID)) return false;
	float Along = 0.f;
	if (!ProjectRayOntoWallFace(SelectedSegmentID, bSelectedWallFaceLeft, RayOrigin, RayDirection, Along)) return false;
	const FVector2D P1 = Nodes[Seg->StartNodeID].Position;
	const FVector2D Dir = (Nodes[Seg->EndNodeID].Position - P1).GetSafeNormal();
	const FVector2D Centre = P1 + Dir * (Along - GrabOffsetCm);
	// The same step as the 2D drag: local preview frame, snap-through of neighbours, the release publishes.
	return DragSelectedOpeningToWorldPos(FVector(Centre.X, Centre.Y, 0.f));
}

namespace PlannerSelection3D
{
	/** Separating-axis test of two convex polygons (either winding); touching (within Tolerance) is not an overlap. */
	bool ConvexPolygonsOverlap(const TArray<FVector2D>& A, const TArray<FVector2D>& B, double Tolerance)
	{
		auto Separated = [Tolerance](const TArray<FVector2D>& P, const TArray<FVector2D>& Q)
		{
			for (int32 i = 0; i < P.Num(); ++i)
			{
				const FVector2D Edge = P[(i + 1) % P.Num()] - P[i];
				const FVector2D Axis = FVector2D(-Edge.Y, Edge.X).GetSafeNormal();
				if (Axis.IsNearlyZero()) continue;
				double MinP = TNumericLimits<double>::Max(), MaxP = -TNumericLimits<double>::Max();
				double MinQ = MinP, MaxQ = MaxP;
				for (const FVector2D& V : P) { const double D = FVector2D::DotProduct(V, Axis); MinP = FMath::Min(MinP, D); MaxP = FMath::Max(MaxP, D); }
				for (const FVector2D& V : Q) { const double D = FVector2D::DotProduct(V, Axis); MinQ = FMath::Min(MinQ, D); MaxQ = FMath::Max(MaxQ, D); }
				if (MaxP <= MinQ + Tolerance || MaxQ <= MinP + Tolerance) return true;
			}
			return false;
		};
		return A.Num() >= 3 && B.Num() >= 3 && !Separated(A, B) && !Separated(B, A);
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// Dragged items stay out of the walls
// ─────────────────────────────────────────────────────────────────────────────

bool ARoomPlannerManager::FootprintOverlapsWalls(const FVector2D& Center, const FVector2D& AxisX, const FVector2D& HalfSize) const
{
	const FVector2D X = AxisX.GetSafeNormal();
	const FVector2D Y(-X.Y, X.X);
	const TArray<FVector2D> Box = { Center - X * HalfSize.X - Y * HalfSize.Y, Center + X * HalfSize.X - Y * HalfSize.Y,
	                                Center + X * HalfSize.X + Y * HalfSize.Y, Center - X * HalfSize.X + Y * HalfSize.Y };

	TArray<FPlannerWallFaceInput> Inputs;
	TMap<int32, int32> IndexBySegment;
	TArray<FVector2D> FaceExtents;
	GatherWallFaces(Inputs, IndexBySegment, FaceExtents);
	for (const FPlannerWallFaceInput& In : Inputs)
	{
		const FWallSegment* Seg = WallSegments.Find(In.SegmentID);
		const double Length = FVector2D::Distance(In.Start, In.End);
		if (!Seg || Length < 1.) continue;
		const FVector2D Dir = (In.End - In.Start) / Length;
		const FVector2D Left(-Dir.Y, Dir.X);
		const double Half = Seg->Thickness * 0.5;
		auto FacePoint = [&](double Along, bool bLeft) { return In.Start + Dir * Along + Left * (bLeft ? Half : -Half); };

		// The wall's solid, cut open where a door or archway goes through it: pieces between the walk-through spans. The end pieces
		// keep the mitred corners.
		TArray<FVector2D> Spans;
		GetWalkThroughSpans(In.SegmentID, Spans);
		Spans.Sort([](const FVector2D& A, const FVector2D& B) { return A.X < B.X; });
		double From = 0.;
		for (int32 Piece = 0; Piece <= Spans.Num(); ++Piece)
		{
			const double To = (Piece < Spans.Num()) ? Spans[Piece].X : Length;
			if (To - From > 0.5)
			{
				const FVector2D StartL = (Piece == 0) ? In.StartCorner[0] : FacePoint(From, true);
				const FVector2D StartR = (Piece == 0) ? In.StartCorner[1] : FacePoint(From, false);
				const FVector2D EndL = (Piece == Spans.Num()) ? In.EndCorner[0] : FacePoint(To, true);
				const FVector2D EndR = (Piece == Spans.Num()) ? In.EndCorner[1] : FacePoint(To, false);
				if (PlannerSelection3D::ConvexPolygonsOverlap(Box, { StartL, EndL, EndR, StartR }, 0.1)) return true;
			}
			if (Piece < Spans.Num()) From = FMath::Max(From, (double)Spans[Piece].Y);
		}
	}
	return false;
}

FVector ARoomPlannerManager::ConstrainDragOutsideWalls(const AActor* Actor, const FVector& From, const FVector& To) const
{
	FVector2D Center, AxisX, HalfSize;
	float TopZ = 0.f;
	if (!Actor || !GetActorFootprint(Actor, Center, AxisX, HalfSize, TopZ)) return To;
	// The footprint moves with the item; 1 cm of slack so an item standing against a wall can slide along it.
	const FVector2D Offset = Center - FVector2D(From.X, From.Y);
	const FVector2D Half = FVector2D(FMath::Max(HalfSize.X - 1., 1.), FMath::Max(HalfSize.Y - 1., 1.));
	auto Blocked = [&](const FVector& Location) { return FootprintOverlapsWalls(FVector2D(Location.X, Location.Y) + Offset, AxisX, Half); };

	if (Blocked(From)) return To; // already in a wall (placed there, or a wall drawn through it): never trapped
	if (!Blocked(To)) return To;
	const FVector AlongX(To.X, From.Y, To.Z), AlongY(From.X, To.Y, To.Z);
	if (!Blocked(AlongX)) return AlongX;
	if (!Blocked(AlongY)) return AlongY;
	return From;
}

// ─────────────────────────────────────────────────────────────────────────────
// 3D selection overlay
// ─────────────────────────────────────────────────────────────────────────────

float ARoomPlannerManager::GetViewExposureEV100() const
{
	// Only until the exposure probe has measured the real exposure (see TickSelectionOverlay). The level's exposure: the renderer blends the volumes around the camera in ascending priority, the last to set a value wins
	// (the walk planner.Exposure prints). Auto exposure settles somewhere in the winner's range; its middle is close enough for a tint.
	const UWorld* World = GetWorld();
	if (!World) return OpenLayoutExposureEV100;
	FVector Camera = FVector::ZeroVector;
	if (const APlayerCameraManager* CameraManager = UGameplayStatics::GetPlayerCameraManager(World, 0))
	{
		Camera = CameraManager->GetCameraLocation();
	}
	bool bFound = false;
	float MinEV = OpenLayoutExposureEV100, MaxEV = OpenLayoutExposureEV100, Bias = 0.f;
	for (IInterface_PostProcessVolume* Volume : World->PostProcessVolumes)
	{
		if (!Volume) continue;
		const FPostProcessVolumeProperties Props = Volume->GetProperties();
		if (!Props.bIsEnabled || !Props.Settings || Props.BlendWeight <= 0.f) continue;
		if (!Props.bIsUnbound && !Volume->EncompassesPoint(Camera, 0.f, nullptr)) continue;
		const FPostProcessSettings& S = *Props.Settings;
		if (S.bOverride_AutoExposureMinBrightness) { MinEV = S.AutoExposureMinBrightness; bFound = true; }
		if (S.bOverride_AutoExposureMaxBrightness) { MaxEV = S.AutoExposureMaxBrightness; bFound = true; }
		if (S.bOverride_AutoExposureBias) Bias = S.AutoExposureBias;
	}
	return bFound ? 0.5f * (MinEV + MaxEV) + Bias : OpenLayoutExposureEV100;
}

UMaterialInterface* ARoomPlannerManager::GetSelectionTintMaterial()
{
	if (SelectionTintMID) return SelectionTintMID;
	// EmissiveMeshMaterial (unlit, additive, emissive = "Color" x texture "LinearColor") is an engine startup package: cooked in every
	// build, as the exterior backdrop relies on. Its default texture is a grid, so it gets a white one.
	UMaterialInterface* Parent = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/EngineMaterials/EmissiveMeshMaterial.EmissiveMeshMaterial"));
	if (!Parent) return nullptr;
	if (!SelectionTintTexture)
	{
		SelectionTintTexture = PlannerRuntimeTextures::CreateHdrColor(FLinearColor::White);
	}
	SelectionTintMID = UMaterialInstanceDynamic::Create(Parent, this);
	if (SelectionTintMID && SelectionTintTexture)
	{
		SelectionTintMID->SetTextureParameterValue(FName("LinearColor"), SelectionTintTexture);
	}
	return SelectionTintMID;
}

UMaterialInterface* ARoomPlannerManager::GetSelectionFrameMaterial()
{
	if (!SelectionFrameMID)
	{
		// Lit like paint (BasicShapeMaterial is cooked: the lobby map references it), so the frame keeps its colour at every exposure.
		if (UMaterialInterface* Parent = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial")))
		{
			SelectionFrameMID = UMaterialInstanceDynamic::Create(Parent, this);
		}
	}
	if (SelectionFrameMID)
	{
		SelectionFrameMID->SetVectorParameterValue(FName("Color"), SelectionFrameColor);
		SelectionFrameMID->SetScalarParameterValue(FName("Roughness"), 0.7f);
	}
	return SelectionFrameMID;
}

void ARoomPlannerManager::SetSelectionOverlayMaterial(UMeshComponent* Mesh)
{
	UMaterialInterface* Tint = GetSelectionTintMaterial();
	if (!Mesh || !Tint) return;
	Mesh->SetOverlayMaterial(Tint);
	SelectionOverlayMeshes.Add(Mesh);
}

void ARoomPlannerManager::ClearSelectionOverlayMaterials()
{
	for (const TWeakObjectPtr<UMeshComponent>& Mesh : SelectionOverlayMeshes)
	{
		if (Mesh.IsValid() && Mesh->GetOverlayMaterial() == SelectionTintMID)
		{
			Mesh->SetOverlayMaterial(nullptr);
		}
	}
	SelectionOverlayMeshes.Reset();
}

bool ARoomPlannerManager::IsSelectionOverlayShown() const
{
	const bool bMeshes = SelectionTintMesh && SelectionFrameMesh && SelectionTintMesh->GetVisibleFlag()
		&& (SelectionTintMesh->GetNumSections() > 0 || SelectionFrameMesh->GetNumSections() > 0);
	return bMeshes || GetSelectionOverlayMaterialCount() > 0;
}

float ARoomPlannerManager::GetMeasuredViewExposure() const
{
	return SelectionExposureProbe.IsValid() ? SelectionExposureProbe->LastExposure : 0.f;
}

int32 ARoomPlannerManager::GetSelectionOverlayMaterialCount() const
{
	int32 Count = 0;
	for (const TWeakObjectPtr<UMeshComponent>& Mesh : SelectionOverlayMeshes)
	{
		if (Mesh.IsValid() && Mesh->GetOverlayMaterial() != nullptr) ++Count;
	}
	return Count;
}

void ARoomPlannerManager::RebuildSelectionOverlay()
{
	using namespace PlannerSelection3D;
	if (!SelectionTintMesh || !SelectionFrameMesh) return;
	SelectionTintMesh->ClearAllMeshSections();
	SelectionFrameMesh->ClearAllMeshSections();
	ClearSelectionOverlayMaterials();

	const UWorld* World = GetWorld();
	const bool bActive = !b2DViewMode && !bSelectionHighlightSuppressed && World && World->GetNetMode() != NM_DedicatedServer;
	if (!bActive)
	{
		SelectionTintMesh->SetVisibility(false);
		SelectionFrameMesh->SetVisibility(false);
		return;
	}
	if (!SelectionExposureProbe.IsValid())
	{
		SelectionExposureProbe = FSceneViewExtensions::NewExtension<FPlannerExposureProbe>(GetWorld());
	}

	FPlannerMeshBuffers Tint;
	FPlannerMeshBuffers Frame;
	const float Band = SelectionFrameWidthCm;

	// Frame of one wall face: its outline and the holes on it (bFaceOutline), or only the bands around one opening.
	auto AddWallFrame = [&](int32 SegID, bool bFaceLeft, int32 OnlyOpening)
	{
		const FWallSegment* Seg = WallSegments.Find(SegID);
		if (!Seg) return;
		TArray<FPlannerWallFaceInput> Inputs;
		TMap<int32, int32> IndexBySegment;
		TArray<FVector2D> FaceExtents;
		GatherWallFaces(Inputs, IndexBySegment, FaceExtents);
		const int32* Index = IndexBySegment.Find(SegID);
		if (!Index) return;
		const FPlannerWallFaceInput& In = Inputs[*Index];
		const FVector2D Dir = (In.End - In.Start).GetSafeNormal();
		if (Dir.IsNearlyZero()) return;
		const FVector2D OutNormal = FVector2D(-Dir.Y, Dir.X) * (bFaceLeft ? 1.f : -1.f);
		const FVector2D Extent = FaceExtents[*Index * 2 + (bFaceLeft ? 0 : 1)];

		TArray<PlannerSelectionOverlay::FFaceHole> Holes;
		for (int32 i = 0; i < Seg->Openings.Num(); ++i)
		{
			if (OnlyOpening != INDEX_NONE && i != OnlyOpening) continue;
			const FWallOpening& Op = Seg->Openings[i];
			PlannerSelectionOverlay::FFaceHole Hole;
			Hole.Start = FMath::Clamp(Op.DistanceFromStart - Op.Width * 0.5f, Extent.X, Extent.Y);
			Hole.End = FMath::Clamp(Op.DistanceFromStart + Op.Width * 0.5f, Extent.X, Extent.Y);
			Hole.Bottom = FMath::Max(0.f, Op.SillHeight);
			Hole.Top = FMath::Min(Op.SillHeight + Op.Height, Seg->Height);
			Hole.bOnFloor = Op.SillHeight < WalkThroughSillCm;
			Holes.Add(Hole);
		}
		const FVector2D FaceOrigin = In.Start + OutNormal * Seg->Thickness * 0.5f;
		PlannerSelectionOverlay::AddWallFaceFrame(Frame, FaceOrigin, Dir, OutNormal, Extent.X, Extent.Y, Seg->Height, Holes, Band, OverlayLiftCm,
			OnlyOpening == INDEX_NONE, HoleMarginCm);
		// A selected opening: the tint spans its hole on this face (the leaf's procedural mesh cannot take an overlay material, and
		// a sheet in the face plane follows no leaf animation anyway).
		if (OnlyOpening != INDEX_NONE && Holes.Num() == 1 && Holes[0].End - Holes[0].Start > 1.f)
		{
			const PlannerSelectionOverlay::FFaceHole& Hole = Holes[0];
			auto At = [&](float Along, float Z)
			{
				const FVector2D P = FaceOrigin + Dir * Along + OutNormal * OverlayLiftCm;
				return FVector(P.X, P.Y, Z);
			};
			PlannerMeshBuilder::AddQuad(Tint, At(Hole.Start, Hole.Bottom), At(Hole.End, Hole.Bottom), At(Hole.End, Hole.Top), At(Hole.Start, Hole.Top),
				FVector(OutNormal.X, OutNormal.Y, 0.f));
		}
	};

	// The copied surface's extreme height (floor top / ceiling underside) for its frame.
	auto ExtremeZ = [](const FPlannerMeshBuffers& Buffers, bool bHighest)
	{
		float Z = bHighest ? -TNumericLimits<float>::Max() : TNumericLimits<float>::Max();
		for (const FVector& V : Buffers.Vertices)
		{
			Z = bHighest ? FMath::Max(Z, (float)V.Z) : FMath::Min(Z, (float)V.Z);
		}
		return Z;
	};

	switch (GetSelectionKind())
	{
	case EPlannerSelectionKind::Wall:
	{
		const TObjectPtr<AProceduralWallActor>* WallPtr = WallActors.Find(SelectedSegmentID);
		const AProceduralWallActor* Wall = WallPtr ? WallPtr->Get() : nullptr;
		const int32 Section = bSelectedWallFaceLeft ? AProceduralWallActor::LeftFaceSection : AProceduralWallActor::RightFaceSection;
		if (Wall && Wall->WallProceduralMesh && Section < Wall->WallProceduralMesh->GetNumSections())
		{
			if (const FProcMeshSection* Face = Wall->WallProceduralMesh->GetProcMeshSection(Section))
			{
				PlannerSelectionOverlay::AppendLiftedSection(*Face, OverlayLiftCm, FVector::ZeroVector, Tint);
			}
		}
		AddWallFrame(SelectedSegmentID, bSelectedWallFaceLeft, INDEX_NONE);
		break;
	}
	case EPlannerSelectionKind::Opening:
	{
		// Both faces: an opening is looked at from either room.
		AddWallFrame(SelectedSegmentID, true, SelectedOpeningIndex);
		AddWallFrame(SelectedSegmentID, false, SelectedOpeningIndex);
		break;
	}
	case EPlannerSelectionKind::Floor:
	case EPlannerSelectionKind::Ceiling:
	case EPlannerSelectionKind::Baseboard:
	{
		const EPlannerSelectionKind Kind = GetSelectionKind();
		UProceduralMeshComponent* Mesh = (Kind == EPlannerSelectionKind::Floor) ? FloorProceduralMesh.Get()
			: (Kind == EPlannerSelectionKind::Ceiling ? CeilingProceduralMesh.Get() : BaseboardProceduralMesh.Get());
		const int32 Section = SelectedRoomID - 1;
		if (!Mesh || !Mesh->IsVisible() || Section < 0 || Section >= Mesh->GetNumSections()) break;
		const FProcMeshSection* Surface = Mesh->GetProcMeshSection(Section);
		if (!Surface) break;
		// Floor: only its top; ceiling: only its underside (the slab's other faces are never seen from the room).
		const FVector Facing = (Kind == EPlannerSelectionKind::Floor) ? FVector::UpVector
			: (Kind == EPlannerSelectionKind::Ceiling ? FVector::DownVector : FVector::ZeroVector);
		PlannerSelectionOverlay::AppendLiftedSection(*Surface, OverlayLiftCm, Facing, Tint);
		const FRoomData* Room = Rooms.Find(SelectedRoomID);
		if (Room && Kind != EPlannerSelectionKind::Baseboard && !Tint.IsEmpty())
		{
			const bool bFloor = (Kind == EPlannerSelectionKind::Floor);
			const TArray<FVector2D>& Outline = Room->NetFloorPolygon.Num() >= 3 ? Room->NetFloorPolygon : Room->FloorPolygon;
			PlannerSelectionOverlay::AddPolygonFrame(Frame, Outline, ExtremeZ(Tint, bFloor) + (bFloor ? 0.1f : -0.1f), Band,
				bFloor ? FVector::UpVector : FVector::DownVector);
		}
		break;
	}
	case EPlannerSelectionKind::Object:
	case EPlannerSelectionKind::CabinetSet:
	{
		const AActor* Actor = (GetSelectionKind() == EPlannerSelectionKind::Object)
			? static_cast<const AActor*>(FindPlacedObjectActor(SelectedObjectID)) : static_cast<const AActor*>(FindCabinetSetActor(SelectedCabinetSetID));
		if (Actor)
		{
			TArray<UMeshComponent*> Meshes;
			Actor->GetComponents<UMeshComponent>(Meshes);
			for (UMeshComponent* Mesh : Meshes)
			{
				if (Mesh && Mesh->IsVisible()) SetSelectionOverlayMaterial(Mesh);
			}
		}
		break;
	}
	default:
		break;
	}

	if (!Tint.IsEmpty())
	{
		if (UMaterialInterface* TintMaterial = GetSelectionTintMaterial())
		{
			SelectionTintMesh->CreateMeshSection(0, Tint.Vertices, Tint.Triangles, Tint.Normals, Tint.UVs, TArray<FColor>(), Tint.Tangents, false);
			SelectionTintMesh->SetMaterial(0, TintMaterial);
		}
	}
	if (!Frame.IsEmpty())
	{
		SelectionFrameMesh->CreateMeshSection(0, Frame.Vertices, Frame.Triangles, Frame.Normals, Frame.UVs, TArray<FColor>(), Frame.Tangents, false);
		SelectionFrameMesh->SetMaterial(0, GetSelectionFrameMaterial());
	}
	SelectionTintMesh->SetVisibility(SelectionTintMesh->GetNumSections() > 0);
	SelectionFrameMesh->SetVisibility(SelectionFrameMesh->GetNumSections() > 0);
	TickSelectionOverlay(); // the tint's brightness right away, not one frame late
}

void ARoomPlannerManager::TickSelectionOverlay()
{
	if (!SelectionTintMID) return;
	const bool bTintShown = (SelectionTintMesh && SelectionTintMesh->GetVisibleFlag()) || SelectionOverlayMeshes.Num() > 0;
	if (!bTintShown) return;
	const UWorld* World = GetWorld();
	const float Now = World ? World->GetRealTimeSeconds() : 0.f;
	const float Fraction = PlannerSelectionOverlay::PulseFraction(Now, SelectionPulseSeconds,
		FMath::Min(SelectionTintMinFraction, SelectionTintMaxFraction), FMath::Max(SelectionTintMinFraction, SelectionTintMaxFraction));
	// Display white in scene luminance: from the exposure the view really had, else from the exposure settings.
	const float Luminance = (SelectionExposureProbe.IsValid() && SelectionExposureProbe->LastExposure > 0.f)
		? Fraction / SelectionExposureProbe->LastExposure
		: PlannerSelectionOverlay::LuminanceForFraction(GetViewExposureEV100(), Fraction);
	// The hue at unit luminance, scaled to the wanted luminance.
	const float HueLuminance = FMath::Max(SelectionTintColor.GetLuminance(), 1.e-3f);
	FLinearColor Color = SelectionTintColor * (Luminance / HueLuminance);
	Color.A = 1.f;
	SelectionTintMID->SetVectorParameterValue(FName("Color"), Color);
}
