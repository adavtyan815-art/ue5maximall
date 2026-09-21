// Copyright 2026 MaxiMall. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Constructor/PlannerFinishLayout.h"
#include "Constructor/ProceduralWallActor.h"
#include "Constructor/RoomPlannerManager.h"
#include "Engine/DataTable.h"
#include "Engine/Engine.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "MaterialDomain.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
	const TCHAR* PaintMaterialPath = TEXT("/Game/RoomPlanner/Materials/M_PlannerPaint.M_PlannerPaint");
	const TCHAR* TileMaterialPath = TEXT("/Game/RoomPlanner/Materials/M_PlannerTile.M_PlannerTile");
	const TCHAR* TileCatalogPath = TEXT("/Game/DT/DT_PlannerTiles.DT_PlannerTiles");

	bool HasParameter(const UMaterialInterface* Material, EMaterialParameterType Type, FName Name)
	{
		if (!Material) return false;
		TArray<FMaterialParameterInfo> Infos;
		TArray<FGuid> Ids;
		switch (Type)
		{
		case EMaterialParameterType::Scalar:  Material->GetAllScalarParameterInfo(Infos, Ids); break;
		case EMaterialParameterType::Vector:  Material->GetAllVectorParameterInfo(Infos, Ids); break;
		case EMaterialParameterType::Texture: Material->GetAllTextureParameterInfo(Infos, Ids); break;
		default: return false;
		}
		return Infos.ContainsByPredicate([Name](const FMaterialParameterInfo& Info) { return Info.Name == Name; });
	}

	FSurfaceFinish MakePaint(const FLinearColor& Color, const TCHAR* Code)
	{
		return ARoomPlannerManager::MakePaintFinish(Code, Color);
	}

	/** Closed rectangular room of four walls with mitred corners, as the corner-joint pass produces them (half thickness H). */
	TArray<FPlannerWallFaceInput> MakeRectangleRoom(float W, float D, float H, bool bReverseSecondWall)
	{
		const FVector2D P[4] = { FVector2D(0., 0.), FVector2D(W, 0.), FVector2D(W, D), FVector2D(0., D) };
		// Left of a counter-clockwise outline is the interior: interior corners are inset by H, exterior corners outset by H.
		const FVector2D Inner[4] = { FVector2D(H, H), FVector2D(W - H, H), FVector2D(W - H, D - H), FVector2D(H, D - H) };
		const FVector2D Outer[4] = { FVector2D(-H, -H), FVector2D(W + H, -H), FVector2D(W + H, D + H), FVector2D(-H, D + H) };
		TArray<FPlannerWallFaceInput> Walls;
		for (int32 i = 0; i < 4; ++i)
		{
			const int32 j = (i + 1) % 4;
			FPlannerWallFaceInput In;
			In.SegmentID = i + 1;
			In.StartNodeID = i + 1;
			In.EndNodeID = j + 1;
			In.Start = P[i];
			In.End = P[j];
			In.StartCorner[0] = Inner[i];
			In.EndCorner[0] = Inner[j];
			In.StartCorner[1] = Outer[i];
			In.EndCorner[1] = Outer[j];
			if (bReverseSecondWall && i == 1)
			{
				Swap(In.Start, In.End);
				Swap(In.StartNodeID, In.EndNodeID);
				// Reversed direction: the interior is now on the right face.
				In.StartCorner[1] = Inner[j];
				In.EndCorner[1] = Inner[i];
				In.StartCorner[0] = Outer[j];
				In.EndCorner[0] = Outer[i];
			}
			Walls.Add(In);
		}
		return Walls;
	}

	float AlongOf(const FPlannerWallFaceInput& W, const FVector2D& P)
	{
		return (float)FVector2D::DotProduct(P - W.Start, (W.End - W.Start).GetSafeNormal());
	}

	/** U (cm) of a face corner point. */
	float CornerU(const TMap<int32, FPlannerWallFaceUV>& UVs, const FPlannerWallFaceInput& W, int32 Face, bool bStart)
	{
		const FVector2D& Corner = bStart ? W.StartCorner[Face] : W.EndCorner[Face];
		return UVs.FindChecked(W.SegmentID).U(Face, AlongOf(W, Corner));
	}

	struct FScopedTestWorld
	{
		UWorld* World = nullptr;
		FScopedTestWorld()
		{
			World = UWorld::CreateWorld(EWorldType::Game, false);
			if (World && GEngine)
			{
				GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
			}
		}
		~FScopedTestWorld()
		{
			if (World)
			{
				if (GEngine) GEngine->DestroyWorldContext(World);
				World->DestroyWorld(false);
				World->RemoveFromRoot();
			}
		}
	};
}

// ─────────────────────────────────────────────────────────────────────────────
// Items 1, 2, 4, 5: assets and the manager's finishing slots
// ─────────────────────────────────────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlannerFinishAssetsTest, "MaxiMall.Planner.Finish.Assets",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlannerFinishAssetsTest::RunTest(const FString& Parameters)
{
	UDataTable* Catalog = LoadObject<UDataTable>(nullptr, TileCatalogPath);
	if (!TestNotNull(TEXT("DT_PlannerTiles exists"), Catalog)) return false;
	TestTrue(TEXT("DT_PlannerTiles uses FPlannerTileRow"), Catalog->GetRowStruct() == FPlannerTileRow::StaticStruct());
	TestTrue(TEXT("DT_PlannerTiles has tiles"), Catalog->GetRowMap().Num() > 0);
	for (const TPair<FName, uint8*>& Pair : Catalog->GetRowMap())
	{
		const FPlannerTileRow* Row = reinterpret_cast<const FPlannerTileRow*>(Pair.Value);
		TestTrue(FString::Printf(TEXT("Tile %s has a size"), *Pair.Key.ToString()), Row && Row->TileSizeCm > 0.f);
		TestTrue(FString::Printf(TEXT("Tile %s has a name"), *Pair.Key.ToString()), Row && !Row->DisplayName.IsEmpty());
	}

	UMaterialInterface* TileMat = LoadObject<UMaterialInterface>(nullptr, TileMaterialPath);
	UMaterialInterface* PaintMat = LoadObject<UMaterialInterface>(nullptr, PaintMaterialPath);
	if (!TestNotNull(TEXT("M_PlannerTile exists"), TileMat) || !TestNotNull(TEXT("M_PlannerPaint exists"), PaintMat)) return false;

	// The parameters CreateFinishMaterialInstance sets.
	TestTrue(TEXT("M_PlannerTile: scalar TilesPerMeter"), HasParameter(TileMat, EMaterialParameterType::Scalar, TEXT("TilesPerMeter")));
	TestTrue(TEXT("M_PlannerTile: vector BaseColor"), HasParameter(TileMat, EMaterialParameterType::Vector, TEXT("BaseColor")));
	TestTrue(TEXT("M_PlannerTile: texture TileTexture"), HasParameter(TileMat, EMaterialParameterType::Texture, TEXT("TileTexture")));
	TestTrue(TEXT("M_PlannerPaint: vector BaseColor"), HasParameter(PaintMat, EMaterialParameterType::Vector, TEXT("BaseColor")));

	// Manager slots point at the assets.
	const ARoomPlannerManager* Defaults = GetDefault<ARoomPlannerManager>();
	TestTrue(TEXT("PaintMaterial slot = M_PlannerPaint"), Defaults->PaintMaterial == PaintMat);
	TestTrue(TEXT("TileMaterial slot = M_PlannerTile"), Defaults->TileMaterial == TileMat);
	TestTrue(TEXT("TileCatalog slot = DT_PlannerTiles"), Defaults->TileCatalog == Catalog);

	// Every catalog tile becomes a tile finish (the UI tile buttons list GetAvailableTiles).
	const TArray<FPlannerCatalogEntry> Tiles = Defaults->GetAvailableTiles();
	TestEqual(TEXT("Every tile row is offered"), Tiles.Num(), Catalog->GetRowMap().Num());
	for (const FPlannerCatalogEntry& Tile : Tiles)
	{
		FSurfaceFinish Finish;
		TestTrue(FString::Printf(TEXT("MakeTileFinish(%s)"), *Tile.ID), Defaults->MakeTileFinish(FName(*Tile.ID), Finish));
		TestTrue(TEXT("Tile finish type"), Finish.Type == ESurfaceFinishType::Tile);
		TestEqual(TEXT("Tile finish size"), Finish.TileSizeCm, Tile.TileSizeCm);
	}

	// Finish instances are built on the planner materials (paint no longer falls back to M_Change_Color).
	FScopedTestWorld TestWorld;
	if (!TestNotNull(TEXT("Test world"), TestWorld.World)) return false;
	ARoomPlannerManager* Manager = TestWorld.World->SpawnActor<ARoomPlannerManager>();
	if (!TestNotNull(TEXT("Manager"), Manager)) return false;

	const UMaterialInstanceDynamic* PaintMID = Cast<UMaterialInstanceDynamic>(Manager->GetFinishMaterial(MakePaint(FLinearColor(0.8f, 0.1f, 0.1f), TEXT("RAL 3020"))));
	TestTrue(TEXT("Paint finish uses M_PlannerPaint"), PaintMID && PaintMID->Parent == PaintMat);
	if (Tiles.Num() > 0)
	{
		FSurfaceFinish TileFinish;
		Manager->MakeTileFinish(FName(*Tiles[0].ID), TileFinish);
		UMaterialInstanceDynamic* TileMID = Cast<UMaterialInstanceDynamic>(Manager->GetFinishMaterial(TileFinish));
		TestTrue(TEXT("Tile finish uses M_PlannerTile"), TileMID && TileMID->Parent == TileMat);
		float TilesPerMeter = 0.f;
		if (TileMID) TilesPerMeter = TileMID->K2_GetScalarParameterValue(TEXT("TilesPerMeter"));
		TestNearlyEqual(TEXT("TilesPerMeter = 100 / tile size"), TilesPerMeter, 100.f / TileFinish.TileSizeCm, 0.001f);
		TestTrue(TEXT("One instance per finish"), Manager->GetFinishMaterial(TileFinish) == TileMID);
	}
	Manager->Destroy();
	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Item 7: wall tile grid continuity (corners, both faces)
// ─────────────────────────────────────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlannerFinishWallUVChainTest, "MaxiMall.Planner.Finish.WallUVChains",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlannerFinishWallUVChainTest::RunTest(const FString& Parameters)
{
	for (int32 Variant = 0; Variant < 2; ++Variant)
	{
		const bool bReversed = (Variant == 1);
		const TArray<FPlannerWallFaceInput> Walls = MakeRectangleRoom(500., 400., 10., bReversed);
		const TMap<int32, FPlannerWallFaceUV> UVs = PlannerFinishLayout::ComputeWallFaceUVs(Walls);
		TestEqual(TEXT("Every wall has a frame"), UVs.Num(), Walls.Num());

		// Around each corner the face that continues is on the same side of the room; its U must not jump.
		for (int32 i = 0; i < 4; ++i)
		{
			const FPlannerWallFaceInput& A = Walls[i];
			const FPlannerWallFaceInput& B = Walls[(i + 1) % 4];
			const int32 CornerNode = (A.EndNodeID == B.StartNodeID || A.EndNodeID == B.EndNodeID) ? A.EndNodeID : A.StartNodeID;
			for (int32 FaceA = 0; FaceA < 2; ++FaceA)
			{
				const bool bAStart = (A.StartNodeID == CornerNode);
				const FVector2D& PointA = bAStart ? A.StartCorner[FaceA] : A.EndCorner[FaceA];
				// The matching face of B shares the corner point.
				int32 FaceB = INDEX_NONE;
				const bool bBStart = (B.StartNodeID == CornerNode);
				for (int32 F = 0; F < 2; ++F)
				{
					const FVector2D& PointB = bBStart ? B.StartCorner[F] : B.EndCorner[F];
					if (PointB.Equals(PointA, 0.01)) FaceB = F;
				}
				if (!TestTrue(TEXT("Faces meet at the corner"), FaceB != INDEX_NONE)) continue;
				const float UA = CornerU(UVs, A, FaceA, bAStart);
				const float UB = CornerU(UVs, B, FaceB, bBStart);
				// A closed loop has exactly one seam, where the chain started (U = 0 on one side, the perimeter on the other).
				const bool bSeam = FMath::IsNearlyZero(FMath::Min(UA, UB), 0.01f) && FMath::Max(UA, UB) > 100.f;
				if (!bSeam)
				{
					TestNearlyEqual(FString::Printf(TEXT("Variant %d: U continues around corner %d face %d"), Variant, CornerNode, FaceA), UA, UB, 0.01f);
				}
			}
		}

		// Along a face U changes by exactly the distance travelled (metric, no stretching) and never restarts inside a wall.
		for (const FPlannerWallFaceInput& W : Walls)
		{
			for (int32 Face = 0; Face < 2; ++Face)
			{
				const float U0 = CornerU(UVs, W, Face, true);
				const float U1 = CornerU(UVs, W, Face, false);
				const float Len = FVector2D::Distance(W.StartCorner[Face], W.EndCorner[Face]);
				TestNearlyEqual(TEXT("Face U span = face length"), FMath::Abs(U1 - U0), (float)Len, 0.05f);
				const float Mid = (AlongOf(W, W.StartCorner[Face]) + AlongOf(W, W.EndCorner[Face])) * 0.5f;
				TestNearlyEqual(TEXT("U is linear along the face"), UVs.FindChecked(W.SegmentID).U(Face, Mid), (U0 + U1) * 0.5f, 0.05f);
			}
		}
	}

	// T-junction: a through wall split at a node, with a branch. Joints there are not mitred, so every face ends at its plain node
	// offset (as ComputeAllCornerJoints leaves them); the grid must still agree where the faces visibly meet.
	{
		const float H = 10.f;
		FPlannerWallFaceInput A; A.SegmentID = 1; A.StartNodeID = 1; A.EndNodeID = 2; A.Start = FVector2D(0., 0.); A.End = FVector2D(300., 0.);
		A.StartCorner[0] = FVector2D(0., H); A.StartCorner[1] = FVector2D(0., -H); A.EndCorner[0] = FVector2D(300., H); A.EndCorner[1] = FVector2D(300., -H);
		FPlannerWallFaceInput B; B.SegmentID = 2; B.StartNodeID = 2; B.EndNodeID = 3; B.Start = FVector2D(300., 0.); B.End = FVector2D(600., 0.);
		B.StartCorner[0] = FVector2D(300., H); B.StartCorner[1] = FVector2D(300., -H); B.EndCorner[0] = FVector2D(600., H); B.EndCorner[1] = FVector2D(600., -H);
		FPlannerWallFaceInput C; C.SegmentID = 3; C.StartNodeID = 2; C.EndNodeID = 4; C.Start = FVector2D(300., 0.); C.End = FVector2D(300., 250.);
		C.StartCorner[0] = FVector2D(300. - H, 0.); C.StartCorner[1] = FVector2D(300. + H, 0.); C.EndCorner[0] = FVector2D(300. - H, 250.); C.EndCorner[1] = FVector2D(300. + H, 250.);
		const TArray<FPlannerWallFaceInput> Walls = { A, B, C };
		const TMap<int32, FPlannerWallFaceUV> UVs = PlannerFinishLayout::ComputeWallFaceUVs(Walls);
		const FPlannerWallFaceUV& UA = UVs.FindChecked(1);
		const FPlannerWallFaceUV& UB = UVs.FindChecked(2);
		const FPlannerWallFaceUV& UC = UVs.FindChecked(3);
		// Far side (no branch): A and B right faces run straight through (300, -H).
		TestNearlyEqual(TEXT("Through face continues past a T-junction"), UA.U(1, 300.f), UB.U(1, 0.f), 0.01f);
		// Branch side: the visible inner corners are (290, 10) and (310, 10).
		TestNearlyEqual(TEXT("Through wall face continues onto the branch at the visible corner"), UA.U(0, 290.f), UC.U(0, 10.f), 0.01f);
		TestNearlyEqual(TEXT("Branch continues onto the next wall at the visible corner"), UC.U(1, 10.f), UB.U(0, 10.f), 0.01f);
	}
	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Items 6, 7, 9: wall mesh — per-face sections, UVs across openings, face / trim finish materials
// ─────────────────────────────────────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlannerFinishWallMeshTest, "MaxiMall.Planner.Finish.WallMesh",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlannerFinishWallMeshTest::RunTest(const FString& Parameters)
{
	FScopedTestWorld TestWorld;
	if (!TestNotNull(TEXT("Test world"), TestWorld.World)) return false;
	AProceduralWallActor* Wall = TestWorld.World->SpawnActor<AProceduralWallActor>();
	if (!TestNotNull(TEXT("Wall actor"), Wall)) return false;

	Wall->WallData.Thickness = 20.f;
	Wall->WallData.Height = 280.f;
	FWallOpening Door;
	Door.Type = EOpeningType::Door;
	Door.DistanceFromStart = 100.f;
	Door.Width = 90.f;
	Door.Height = 210.f;
	Door.SillHeight = 0.f;
	FWallOpening Window;
	Window.Type = EOpeningType::Window;
	Window.DistanceFromStart = 300.f;
	Window.Width = 120.f;
	Window.Height = 120.f;
	Window.SillHeight = 90.f;
	Wall->WallData.Openings = { Door, Window };

	FPlannerWallFaceUV FaceUV;
	FaceUV.Offset[0] = 37.f;  FaceUV.Sign[0] = 1.f;
	FaceUV.Offset[1] = 912.f; FaceUV.Sign[1] = -1.f;
	Wall->SetFaceUVFrame(FaceUV);
	Wall->SetPresentation(true);
	Wall->RebuildWallMesh(FVector2D(0., 0.), FVector2D(500., 0.), FVector2D::ZeroVector, FVector2D::ZeroVector, FVector2D::ZeroVector, FVector2D::ZeroVector,
		true, true, false);

	UProceduralMeshComponent* Mesh = Wall->WallProceduralMesh;
	TestEqual(TEXT("Wall body has one section per face"), Mesh->GetNumSections(), AProceduralWallActor::NumWallSections);

	const FVector FaceNormals[2] = { FVector(0., 1., 0.), FVector(0., -1., 0.) };
	for (int32 Face = 0; Face < 2; ++Face)
	{
		const FProcMeshSection* Section = Mesh->GetProcMeshSection(Face);
		if (!TestNotNull(TEXT("Face section"), Section)) continue;
		int32 FaceVertices = 0;
		int32 OtherFaceVertices = 0;
		int32 WrongSide = 0;
		int32 WrongUV = 0;
		int32 TopVertices = 0;
		int32 RevealVertices = 0;
		int32 StripColumnBreaks = 0;
		int32 StripRowBreaks = 0;
		for (const FProcMeshVertex& V : Section->ProcVertexBuffer)
		{
			// Tops, sill tops and soffits keep the face's columns; reveals and caps keep its rows, and start their columns at the face.
			const bool bOnFaceEdge = FMath::IsNearlyEqual(FMath::Abs(V.Position.Y), 10., 0.01);
			const float FaceU = FaceUV.U(Face, (float)V.Position.X) / 100.f;
			if (FMath::Abs(V.Normal.Z) > 0.5 && !FMath::IsNearlyEqual((float)V.UV0.X, FaceU, 0.0005f)) ++StripColumnBreaks;
			if (FMath::Abs(V.Normal.X) > 0.99)
			{
				if (!FMath::IsNearlyEqual((float)V.UV0.Y, (float)V.Position.Z / 100.f, 0.0005f)) ++StripRowBreaks;
				if (bOnFaceEdge && !FMath::IsNearlyEqual((float)V.UV0.X, FaceU, 0.0005f)) ++StripColumnBreaks;
			}
			if (bOnFaceEdge && FMath::Abs(V.Normal.Z) > 0.5 && !FMath::IsNearlyEqual((float)V.UV0.Y, (float)V.Position.Z / 100.f, 0.0005f)) ++StripRowBreaks;

			// Everything in a face section lies on that face's side of the wall's mid-plane (Y = 0 here).
			if ((Face == 0 && V.Position.Y < -0.01) || (Face == 1 && V.Position.Y > 0.01)) ++WrongSide;
			if (V.Normal.Equals(FaceNormals[1 - Face], 0.001)) ++OtherFaceVertices;
			if (V.Normal.Equals(FVector::UpVector, 0.001) && FMath::IsNearlyEqual(V.Position.Z, 280., 0.01)) ++TopVertices;
			if (FMath::Abs(V.Normal.X) > 0.99 && V.Position.X > 1. && V.Position.X < 499.) ++RevealVertices;
			if (!V.Normal.Equals(FaceNormals[Face], 0.001)) continue;
			++FaceVertices;
			// One mapping for the whole face: U follows the position along the wall, V the height (1 UV = 1 m).
			const float ExpectedU = FaceUV.U(Face, (float)V.Position.X) / 100.f;
			const float ExpectedV = (float)V.Position.Z / 100.f;
			if (!FMath::IsNearlyEqual((float)V.UV0.X, ExpectedU, 0.0005f) || !FMath::IsNearlyEqual((float)V.UV0.Y, ExpectedV, 0.0005f)) ++WrongUV;
		}
		// Two solid spans, the band under the window and the bands above both openings: at least 5 face quads.
		TestTrue(TEXT("Face split around the openings"), FaceVertices >= 20);
		TestEqual(TEXT("A face section never holds the other face"), OtherFaceVertices, 0);
		TestEqual(TEXT("A face section stays on its side of the mid-plane"), WrongSide, 0);
		TestTrue(TEXT("A face owns its half of the wall top"), TopVertices > 0);
		TestTrue(TEXT("A face owns its half of the reveals"), RevealVertices > 0);
		TestEqual(TEXT("Face UVs continue across openings (no restart)"), WrongUV, 0);
		TestEqual(TEXT("Tops, sills and soffits continue the face's tile columns; reveals and caps start from the face"), StripColumnBreaks, 0);
		TestEqual(TEXT("Reveals and caps continue the face's tile rows; tops, sills and soffits start from the face edge"), StripRowBreaks, 0);
	}

	// Per-face finish: one face never overwrites the other.
	UMaterialInterface* Base = UMaterial::GetDefaultMaterial(MD_Surface);
	UMaterialInstanceDynamic* MatA = UMaterialInstanceDynamic::Create(Base, Wall);
	UMaterialInstanceDynamic* MatB = UMaterialInstanceDynamic::Create(Base, Wall);
	const FSurfaceFinish FinishA = MakePaint(FLinearColor(0.7f, 0.2f, 0.2f), TEXT("RAL 3000"));
	const FSurfaceFinish FinishB = MakePaint(FLinearColor(0.2f, 0.2f, 0.7f), TEXT("RAL 5000"));
	Wall->SetFaceFinish(true, FinishA, MatA);
	Wall->SetFaceFinish(false, FinishB, MatB);
	TestTrue(TEXT("Left face shows its finish"), Mesh->GetMaterial(AProceduralWallActor::LeftFaceSection) == MatA);
	TestTrue(TEXT("Right face shows its own finish"), Mesh->GetMaterial(AProceduralWallActor::RightFaceSection) == MatB);
	Wall->SetFaceFinish(true, FSurfaceFinish(), nullptr);
	TestTrue(TEXT("Clearing the left face keeps the right face"), Mesh->GetMaterial(AProceduralWallActor::RightFaceSection) == MatB);

	// Per-face highlight.
	Wall->SetFaceFinish(true, FinishA, MatA);
	Wall->SetSelectedFaceHighlight(true, AProceduralWallActor::RightFaceSection);
	TestTrue(TEXT("Highlighting the right face leaves the left face's finish visible"), Mesh->GetMaterial(AProceduralWallActor::LeftFaceSection) == MatA);
	Wall->SetSelectedFaceHighlight(false, AProceduralWallActor::RightFaceSection);
	TestTrue(TEXT("Highlight cleared"), Mesh->GetMaterial(AProceduralWallActor::RightFaceSection) == MatB);

	// Trim finish: a finished door trim becomes its own dressing section.
	const int32 SectionsWithoutTrimFinish = Wall->DressingMesh->GetNumSections();
	Wall->WallData.Openings[0].TrimFinish = MakePaint(FLinearColor(0.1f, 0.4f, 0.1f), TEXT("RAL 6000"));
	Wall->RebuildWallMesh(FVector2D(0., 0.), FVector2D(500., 0.), FVector2D::ZeroVector, FVector2D::ZeroVector, FVector2D::ZeroVector, FVector2D::ZeroVector,
		true, true, false);
	TestEqual(TEXT("Door trim finish gets its own section"), Wall->DressingMesh->GetNumSections(), SectionsWithoutTrimFinish + 1);
	TestEqual(TEXT("UV frame survives rebuilds"), Wall->GetFaceUVFrame().Offset[1], 912.f);

	Wall->Destroy();
	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Items 8, 9: room surface frames and baseboards
// ─────────────────────────────────────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlannerFinishRoomSurfacesTest, "MaxiMall.Planner.Finish.RoomSurfaces",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlannerFinishRoomSurfacesTest::RunTest(const FString& Parameters)
{
	const TArray<float> Half = { 10.f, 10.f, 10.f, 10.f };

	// Axis-aligned room: frame at the interior corner of the longest wall.
	{
		const TArray<FVector2D> Room = { FVector2D(0., 0.), FVector2D(500., 0.), FVector2D(500., 400.), FVector2D(0., 400.) };
		const TArray<FVector2D> Inner = PlannerFinishLayout::OffsetInward(Room, Half);
		TestTrue(TEXT("Interior corner"), Inner[0].Equals(FVector2D(10., 10.), 0.01) && Inner[2].Equals(FVector2D(490., 390.), 0.01));

		FVector2D Origin, AxisU, AxisV;
		PlannerFinishLayout::ComputeRoomSurfaceFrame(Room, Half, Origin, AxisU, AxisV);
		TestTrue(TEXT("Origin at the interior wall-face corner"), Origin.Equals(FVector2D(10., 10.), 0.01));
		TestTrue(TEXT("U along the longest wall"), AxisU.Equals(FVector2D(1., 0.), 0.001));
		TestTrue(TEXT("V into the room"), AxisV.Equals(FVector2D(0., 1.), 0.001));
		const FVector2D UV = PlannerFinishLayout::RoomSurfaceUV(FVector2D(260., 60.), Origin, AxisU, AxisV);
		TestTrue(TEXT("Metric UVs (1 UV = 1 m)"), UV.Equals(FVector2D(2.5, 0.5), 0.0001));
	}

	// Rotated room, clockwise vertex order: the grid follows the room, not the world axes.
	{
		const FVector2D Offset(1234., -777.);
		const float Angle = FMath::DegreesToRadians(30.f);
		const FVector2D Ax(FMath::Cos(Angle), FMath::Sin(Angle));
		const FVector2D Ay(-Ax.Y, Ax.X);
		TArray<FVector2D> Room = { Offset, Offset + Ay * 300., Offset + Ax * 600. + Ay * 300., Offset + Ax * 600. }; // clockwise
		FVector2D Origin, AxisU, AxisV;
		PlannerFinishLayout::ComputeRoomSurfaceFrame(Room, Half, Origin, AxisU, AxisV);
		TestNearlyEqual(TEXT("U parallel to the longest wall"), (float)FMath::Abs(FVector2D::DotProduct(AxisU, Ax)), 1.f, 0.001f);
		const FVector2D Centre = Offset + Ax * 300. + Ay * 150.;
		TestTrue(TEXT("V points into the room"), FVector2D::DotProduct(Centre - Origin, AxisV) > 0.);
		TestTrue(TEXT("Origin is an interior corner"), FMath::IsNearlyEqual(FMath::Abs(FVector2D::DotProduct(Origin - Offset, Ay)), 290., 0.1) || FMath::IsNearlyEqual(FMath::Abs(FVector2D::DotProduct(Origin - Offset, Ay)), 10., 0.1));
		const FVector2D Along = PlannerFinishLayout::RoomSurfaceUV(Origin + AxisU * 125., Origin, AxisU, AxisV);
		TestTrue(TEXT("Rotated room keeps metric UVs"), Along.Equals(FVector2D(1.25, 0.), 0.0001));

		FVector2D Origin2, AxisU2, AxisV2;
		const TArray<FVector2D> Other = { FVector2D(0., 0.), FVector2D(300., 0.), FVector2D(300., 300.), FVector2D(0., 300.) };
		PlannerFinishLayout::ComputeRoomSurfaceFrame(Other, Half, Origin2, AxisU2, AxisV2);
		TestFalse(TEXT("Each room has its own frame"), AxisU2.Equals(AxisU, 0.01) && Origin2.Equals(Origin, 0.01));
	}

	// Baseboard: on the interior wall faces, interrupted by a doorway, facing the room.
	{
		const TArray<FVector2D> Room = { FVector2D(0., 0.), FVector2D(500., 0.), FVector2D(500., 400.), FVector2D(0., 400.) };
		TArray<TArray<FVector2D>> Cuts;
		Cuts.SetNum(4);
		Cuts[0].Add(FVector2D(200., 290.));
		FPlannerMeshBuffers Baseboard;
		PlannerFinishLayout::BuildBaseboard(Room, Half, Cuts, 1.f, 10.f, 1.5f, Baseboard);
		TestTrue(TEXT("Baseboard built"), Baseboard.Vertices.Num() > 0);
		TestEqual(TEXT("Baseboard winding matches its normals"), PlannerMeshBuilder::CountMisorientedTriangles(Baseboard), 0);

		// Quads (AddQuadUV appends four vertices each): facing, the gap at the doorway and its end caps.
		int32 Edge0FrontQuads = 0;
		int32 WrongFrontFacing = 0;
		int32 FrontAcrossDoorway = 0;
		int32 WrongTopFacing = 0;
		int32 DoorwayCaps = 0;
		for (int32 Q = 0; Q + 3 < Baseboard.Vertices.Num(); Q += 4)
		{
			FVector Min(1.e9), Max(-1.e9);
			for (int32 K = 0; K < 4; ++K)
			{
				Min = Min.ComponentMin(Baseboard.Vertices[Q + K]);
				Max = Max.ComponentMax(Baseboard.Vertices[Q + K]);
			}
			const FVector& N = Baseboard.Normals[Q];
			if (FMath::IsNearlyEqual(Min.Y, 11.5, 0.01) && FMath::IsNearlyEqual(Max.Y, 11.5, 0.01))
			{
				++Edge0FrontQuads;
				if (!N.Equals(FVector(0., 1., 0.), 0.001)) ++WrongFrontFacing; // toward the room
				if (Min.X < 245. && Max.X > 245.) ++FrontAcrossDoorway;
			}
			if (FMath::IsNearlyEqual(Min.Z, 11., 0.01) && FMath::IsNearlyEqual(Max.Z, 11., 0.01) && !N.Equals(FVector::UpVector, 0.001)) ++WrongTopFacing;
			if (FMath::IsNearlyEqual(Min.X, 200., 0.01) && FMath::IsNearlyEqual(Max.X, 200., 0.01) && N.Equals(FVector(1., 0., 0.), 0.001)) ++DoorwayCaps;
			if (FMath::IsNearlyEqual(Min.X, 290., 0.01) && FMath::IsNearlyEqual(Max.X, 290., 0.01) && N.Equals(FVector(-1., 0., 0.), 0.001)) ++DoorwayCaps;
		}
		TestEqual(TEXT("Baseboard runs on both sides of the doorway"), Edge0FrontQuads, 2);
		TestEqual(TEXT("Baseboard front faces the room"), WrongFrontFacing, 0);
		TestEqual(TEXT("Baseboard top faces up"), WrongTopFacing, 0);
		TestEqual(TEXT("Baseboard is interrupted at the doorway"), FrontAcrossDoorway, 0);
		TestEqual(TEXT("Both baseboard ends at the doorway are capped, facing the gap"), DoorwayCaps, 2);

		int32 InsideWall = 0;
		int32 InDoorway = 0;
		for (const FVector& V : Baseboard.Vertices)
		{
			const double DistToOutline = FMath::Min(FMath::Min((double)V.X, 500. - V.X), FMath::Min((double)V.Y, 400. - V.Y));
			if (DistToOutline < 10. - 0.01) ++InsideWall;
			if (V.Y < 12. && V.X > 200.5 && V.X < 289.5) ++InDoorway;
		}
		TestEqual(TEXT("Baseboard stands on the wall faces, not inside the walls"), InsideWall, 0);
		TestEqual(TEXT("No baseboard vertex inside the doorway"), InDoorway, 0);
	}

	// In-line walls of different thickness (a wall split at a node): the thicker wall's baseboard end is capped where it steps out.
	{
		const TArray<FVector2D> Room = { FVector2D(0., 0.), FVector2D(250., 0.), FVector2D(500., 0.), FVector2D(500., 400.), FVector2D(0., 400.) };
		const TArray<float> Halves = { 10.f, 15.f, 10.f, 10.f, 10.f };
		TArray<TArray<FVector2D>> Cuts;
		Cuts.SetNum(5);
		FPlannerMeshBuffers Baseboard;
		PlannerFinishLayout::BuildBaseboard(Room, Halves, Cuts, 1.f, 10.f, 1.5f, Baseboard);
		int32 StepCaps = 0;
		int32 StrayCaps = 0;
		for (int32 Q = 0; Q + 3 < Baseboard.Vertices.Num(); Q += 4)
		{
			FVector Min(1.e9), Max(-1.e9);
			for (int32 K = 0; K < 4; ++K)
			{
				Min = Min.ComponentMin(Baseboard.Vertices[Q + K]);
				Max = Max.ComponentMax(Baseboard.Vertices[Q + K]);
			}
			if (!FMath::IsNearlyEqual(Min.X, 250., 0.01) || !FMath::IsNearlyEqual(Max.X, 250., 0.01)) continue;
			if (Baseboard.Normals[Q].Equals(FVector(-1., 0., 0.), 0.001) && FMath::IsNearlyEqual(Min.Y, 15., 0.01) && FMath::IsNearlyEqual(Max.Y, 16.5, 0.01)) ++StepCaps;
			else ++StrayCaps;
		}
		TestEqual(TEXT("Thicker in-line wall's baseboard end is capped where it steps out"), StepCaps, 1);
		TestEqual(TEXT("No other cap at an in-line node"), StrayCaps, 0);
	}

	// Doorway reaching into a room corner: the neighbouring edge's mitred end there is closed along its mitre.
	{
		const TArray<FVector2D> Room = { FVector2D(0., 0.), FVector2D(500., 0.), FVector2D(500., 400.), FVector2D(0., 400.) };
		TArray<TArray<FVector2D>> Cuts;
		Cuts.SetNum(4);
		Cuts[0].Add(FVector2D(0., 90.));
		FPlannerMeshBuffers Baseboard;
		PlannerFinishLayout::BuildBaseboard(Room, Half, Cuts, 1.f, 10.f, 1.5f, Baseboard);
		int32 MitreCaps = 0;
		for (int32 Q = 0; Q + 3 < Baseboard.Vertices.Num(); Q += 4)
		{
			bool bOnMitre = true;
			for (int32 K = 0; K < 4; ++K)
			{
				const FVector& V = Baseboard.Vertices[Q + K];
				bOnMitre &= FMath::IsNearlyEqual(V.X, V.Y, 0.01) && V.X > 9.99 && V.X < 11.51;
			}
			if (bOnMitre && Baseboard.Normals[Q].Equals(FVector(UE_INV_SQRT_2, -UE_INV_SQRT_2, 0.), 0.001)) ++MitreCaps;
		}
		TestEqual(TEXT("Open mitre at a doorway corner is capped, facing the doorway"), MitreCaps, 1);
	}
	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Items 6, 9: storage, save / load and replication JSON
// ─────────────────────────────────────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlannerFinishPersistenceTest, "MaxiMall.Planner.Finish.Persistence",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlannerFinishPersistenceTest::RunTest(const FString& Parameters)
{
	// Layouts saved before per-face finishing: the one wall finish covers both faces.
	{
		TSharedPtr<FJsonObject> Wall;
		const FString Legacy = TEXT("{\"finish\":{\"type\":\"paint\",\"code\":\"RAL 3020\",\"r\":0.8,\"g\":0.1,\"b\":0.1,\"a\":1,\"tile\":\"\",\"tileSize\":30}}");
		FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Legacy), Wall);
		FSurfaceFinish Left, Right;
		ARoomPlannerManager::ReadWallFaceFinishes(Wall, Left, Right);
		TestTrue(TEXT("Legacy finish on the left face"), Left.Type == ESurfaceFinishType::Paint && Left.ColorCode == TEXT("RAL 3020"));
		TestTrue(TEXT("Legacy finish on the right face"), Right == Left);
	}

	FScopedTestWorld TestWorld;
	if (!TestNotNull(TEXT("Test world"), TestWorld.World)) return false;
	ARoomPlannerManager* Manager = TestWorld.World->SpawnActor<ARoomPlannerManager>();
	if (!TestNotNull(TEXT("Manager"), Manager)) return false;

	// A closed 5 x 4 m room with a door.
	const int32 N1 = Manager->AddNode(FVector2D(0., 0.));
	const int32 N2 = Manager->AddNode(FVector2D(500., 0.));
	const int32 N3 = Manager->AddNode(FVector2D(500., 400.));
	const int32 N4 = Manager->AddNode(FVector2D(0., 400.));
	const int32 W1 = Manager->AddWall(N1, N2);
	Manager->AddWall(N2, N3);
	Manager->AddWall(N3, N4);
	Manager->AddWall(N4, N1);
	TestTrue(TEXT("Door added"), Manager->AddOpeningToWall(W1, EOpeningType::Door, 250.f, 90.f, 210.f, 0.f));
	int32 RoomID = -1;
	Manager->RebuildRooms();
	for (int32 Id = 1; Id < 8 && RoomID == -1; ++Id)
	{
		FSurfaceFinish Probe;
		if (Manager->GetFloorFinish(Id, Probe)) RoomID = Id;
	}
	if (!TestTrue(TEXT("Room detected"), RoomID != -1)) { Manager->Destroy(); return false; }

	const FSurfaceFinish Red = MakePaint(FLinearColor(0.8f, 0.1f, 0.1f), TEXT("RAL 3020"));
	const FSurfaceFinish Blue = MakePaint(FLinearColor(0.1f, 0.2f, 0.7f), TEXT("RAL 5015"));
	FSurfaceFinish Tile;
	Manager->MakeTileFinish(TEXT("Tile_White30"), Tile);

	// Per-face storage.
	TestTrue(TEXT("Set left face"), Manager->SetWallFaceFinish(W1, true, Red));
	TestTrue(TEXT("Set right face"), Manager->SetWallFaceFinish(W1, false, Tile));
	FSurfaceFinish Out;
	Manager->GetWallFaceFinish(W1, true, Out);
	TestTrue(TEXT("Right face did not overwrite the left face"), Out == Red);
	Manager->GetWallFaceFinish(W1, false, Out);
	TestTrue(TEXT("Right face stored"), Out == Tile);

	// Ceiling, baseboard, trim.
	TestTrue(TEXT("Set ceiling"), Manager->SetCeilingFinish(RoomID, Blue));
	TestTrue(TEXT("Set baseboard"), Manager->SetBaseboardFinish(RoomID, Red));
	TestTrue(TEXT("Set door trim"), Manager->SetOpeningTrimFinish(W1, 0, Blue));

	// Round trip through the replicated / saved layout JSON.
	const FString Json = Manager->ExportLayoutToJSON();
	TestTrue(TEXT("Import"), Manager->ImportLayoutFromJSON(Json));

	int32 NewW1 = -1;
	for (int32 Id = 1; Id < 8 && NewW1 == -1; ++Id)
	{
		FSurfaceFinish Probe;
		if (Manager->GetWallFaceFinish(Id, false, Probe) && Probe == Tile) NewW1 = Id;
	}
	if (TestTrue(TEXT("Wall with the tiled right face survives the round trip"), NewW1 != -1))
	{
		Manager->GetWallFaceFinish(NewW1, true, Out);
		TestTrue(TEXT("Left face finish survives"), Out == Red);
		TestTrue(TEXT("Trim finish survives"), Manager->GetOpeningTrimFinish(NewW1, 0, Out) && Out == Blue);
	}
	int32 NewRoom = -1;
	for (int32 Id = 1; Id < 8 && NewRoom == -1; ++Id)
	{
		FSurfaceFinish Probe;
		if (Manager->GetCeilingFinish(Id, Probe)) NewRoom = Id;
	}
	if (TestTrue(TEXT("Room survives"), NewRoom != -1))
	{
		Manager->GetCeilingFinish(NewRoom, Out);
		TestTrue(TEXT("Ceiling finish survives"), Out == Blue);
		Manager->GetBaseboardFinish(NewRoom, Out);
		TestTrue(TEXT("Baseboard finish survives"), Out == Red);
		Manager->GetFloorFinish(NewRoom, Out);
		TestFalse(TEXT("Floor untouched"), Out.IsSet());
	}

	// Per-face areas (REQ-14 keeps counting every finished surface).
	float PaintArea = Manager->GetTotalFinishAreaM2(ESurfaceFinishType::Paint);
	float TileArea = Manager->GetTotalFinishAreaM2(ESurfaceFinishType::Tile);
	TestTrue(TEXT("Left face counted as paint"), PaintArea > 0.f);
	TestTrue(TEXT("Right face counted as tile"), TileArea > 0.f);

	Manager->Destroy();
	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Items 6, 7: manager-driven — T-junction grid from the real corner joints, 2D / 3D face picking
// ─────────────────────────────────────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlannerFinishManagerFacesTest, "MaxiMall.Planner.Finish.ManagerFaces",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlannerFinishManagerFacesTest::RunTest(const FString& Parameters)
{
	auto FindWallActor = [](UWorld* World, int32 SegmentID) -> AProceduralWallActor*
	{
		for (TActorIterator<AProceduralWallActor> It(World); It; ++It)
		{
			if (It->WallData.SegmentID == SegmentID) return *It;
		}
		return nullptr;
	};

	// T-junction built by the manager: the grid agrees at the visible inner corners (290, 10) and (310, 10).
	{
		FScopedTestWorld TestWorld;
		if (!TestNotNull(TEXT("Test world"), TestWorld.World)) return false;
		ARoomPlannerManager* Manager = TestWorld.World->SpawnActor<ARoomPlannerManager>();
		if (!TestNotNull(TEXT("Manager"), Manager)) return false;
		const int32 N1 = Manager->AddNode(FVector2D(0., 0.));
		const int32 N2 = Manager->AddNode(FVector2D(300., 0.));
		const int32 N3 = Manager->AddNode(FVector2D(600., 0.));
		const int32 N4 = Manager->AddNode(FVector2D(300., 250.));
		const int32 A = Manager->AddWall(N1, N2);
		const int32 B = Manager->AddWall(N2, N3);
		const int32 C = Manager->AddWall(N2, N4);
		Manager->RebuildAllWalls();
		const AProceduralWallActor* WA = FindWallActor(TestWorld.World, A);
		const AProceduralWallActor* WB = FindWallActor(TestWorld.World, B);
		const AProceduralWallActor* WC = FindWallActor(TestWorld.World, C);
		if (TestTrue(TEXT("T-junction walls exist"), WA && WB && WC))
		{
			TestNearlyEqual(TEXT("Manager T-junction: through wall onto branch"), WA->GetFaceUVFrame().U(0, 290.f), WC->GetFaceUVFrame().U(0, 10.f), 0.01f);
			TestNearlyEqual(TEXT("Manager T-junction: branch onto through wall"), WC->GetFaceUVFrame().U(1, 10.f), WB->GetFaceUVFrame().U(0, 10.f), 0.01f);
			TestNearlyEqual(TEXT("Manager T-junction: far face straight through"), WA->GetFaceUVFrame().U(1, 300.f), WB->GetFaceUVFrame().U(1, 0.f), 0.01f);
		}
		Manager->Destroy();
	}

	// Face picking on a closed room.
	{
		FScopedTestWorld TestWorld;
		if (!TestNotNull(TEXT("Test world"), TestWorld.World)) return false;
		ARoomPlannerManager* Manager = TestWorld.World->SpawnActor<ARoomPlannerManager>();
		if (!TestNotNull(TEXT("Manager"), Manager)) return false;
		const int32 N1 = Manager->AddNode(FVector2D(0., 0.));
		const int32 N2 = Manager->AddNode(FVector2D(500., 0.));
		const int32 N3 = Manager->AddNode(FVector2D(500., 400.));
		const int32 N4 = Manager->AddNode(FVector2D(0., 400.));
		const int32 South = Manager->AddWall(N1, N2);
		Manager->AddWall(N2, N3);
		Manager->AddWall(N3, N4);
		Manager->AddWall(N4, N1);
		Manager->RebuildRooms();
		Manager->ActiveToolMode = EPlannerToolMode::Select;

		// 2D: the top-down view puts the ground point under a clicked wall top outside the wall; the room side is still chosen.
		TestEqual(TEXT("2D pick selects the wall"), Manager->SelectWallAtWorldPos(FVector(250., -42., 0.)), South);
		TestTrue(TEXT("2D pick beside an outer wall selects its room-side face"), Manager->IsSelectedWallFaceInterior());
		Manager->ClearAllSelection();

		// 3D: the face that was hit.
		AProceduralWallActor* SouthActor = FindWallActor(TestWorld.World, South);
		if (TestNotNull(TEXT("South wall actor"), SouthActor))
		{
			FHitResult Outside(SouthActor, SouthActor->WallProceduralMesh, FVector(250., -10., 120.), FVector(0., -1., 0.));
			Outside.TraceStart = FVector(250., -800., 160.);
			Outside.TraceEnd = FVector(250., 800., 80.);
			TestTrue(TEXT("3D hit on the outer face selects a wall"), Manager->SelectSurfaceFromHit(Outside) == EPlannerSelectionKind::Wall);
			TestFalse(TEXT("3D hit on the outer face selects the outer face"), Manager->IsSelectedWallFaceInterior());

			FHitResult Inside(SouthActor, SouthActor->WallProceduralMesh, FVector(250., 10., 120.), FVector(0., 1., 0.));
			Inside.TraceStart = FVector(250., 300., 160.);
			Inside.TraceEnd = FVector(250., -300., 80.);
			Manager->SelectSurfaceFromHit(Inside);
			TestTrue(TEXT("3D hit on the room face selects the room face"), Manager->IsSelectedWallFaceInterior());

			// Finishing the selected face leaves the other one alone.
			const FSurfaceFinish Paint = MakePaint(FLinearColor(0.3f, 0.5f, 0.3f), TEXT("RAL 6019"));
			Manager->SetWallFaceFinish(South, Manager->bSelectedWallFaceLeft, Paint);
			FSurfaceFinish Other;
			Manager->GetWallFaceFinish(South, !Manager->bSelectedWallFaceLeft, Other);
			TestFalse(TEXT("The unselected face keeps its finish"), Other.IsSet());
		}
		Manager->Destroy();
	}

	// L-shaped room, door pushed against the inside corner: the wall cuts its hole only from where both faces run straight, and the
	// baseboard stops at that same jamb (no bare gap in front of the wall).
	{
		FScopedTestWorld TestWorld;
		if (!TestNotNull(TEXT("Test world"), TestWorld.World)) return false;
		ARoomPlannerManager* Manager = TestWorld.World->SpawnActor<ARoomPlannerManager>();
		if (!TestNotNull(TEXT("Manager"), Manager)) return false;
		const TArray<FVector2D> Outline = { FVector2D(0., 0.), FVector2D(400., 0.), FVector2D(400., 200.), FVector2D(200., 200.), FVector2D(200., 400.), FVector2D(0., 400.) };
		TArray<int32> NodeIDs;
		for (const FVector2D& P : Outline) NodeIDs.Add(Manager->AddNode(P));
		TArray<int32> WallIDs;
		for (int32 i = 0; i < NodeIDs.Num(); ++i) WallIDs.Add(Manager->AddWall(NodeIDs[i], NodeIDs[(i + 1) % NodeIDs.Num()]));
		// Wall (200, 200) -> (200, 400): its room-side face starts 10 cm before the corner node, its outer face 10 cm after it.
		Manager->AddOpeningToWall(WallIDs[3], EOpeningType::Door, 45.f, 90.f, 210.f, 0.f);
		Manager->RebuildRooms();

		int32 JambCaps = 0;
		int32 CapsBeforeJamb = 0;
		UProceduralMeshComponent* Baseboards = Manager->BaseboardProceduralMesh;
		for (int32 SectionIdx = 0; Baseboards && SectionIdx < Baseboards->GetNumSections(); ++SectionIdx)
		{
			const FProcMeshSection* Section = Baseboards->GetProcMeshSection(SectionIdx);
			if (!Section) continue;
			for (int32 Q = 0; Q + 3 < Section->ProcVertexBuffer.Num(); Q += 4)
			{
				FVector Min(1.e9), Max(-1.e9);
				for (int32 K = 0; K < 4; ++K)
				{
					Min = Min.ComponentMin(Section->ProcVertexBuffer[Q + K].Position);
					Max = Max.ComponentMax(Section->ProcVertexBuffer[Q + K].Position);
				}
				const bool bCapFacingDoor = FMath::IsNearlyEqual(Min.Y, Max.Y, 0.01) && Min.X > 188.4 && Max.X < 190.1
					&& Section->ProcVertexBuffer[Q].Normal.Equals(FVector(0., 1., 0.), 0.001);
				if (!bCapFacingDoor) continue;
				if (FMath::IsNearlyEqual(Min.Y, 210., 0.01)) ++JambCaps;
				else if (Min.Y < 209.9) ++CapsBeforeJamb;
			}
		}
		TestEqual(TEXT("Baseboard ends at the door jamb the wall actually cuts"), JambCaps, 1);
		TestEqual(TEXT("Baseboard does not stop short of the jamb"), CapsBeforeJamb, 0);
		Manager->Destroy();
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
