// Copyright 2026 MaxiMall. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Components/PostProcessComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Constructor/PlannerPlacedObjectActor.h"
#include "Constructor/PlannerSelectionOverlay.h"
#include "Constructor/RoomPlannerManager.h"
#include "Engine/Engine.h"
#include "Engine/HitResult.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "FurnitureConfigurator/UI/PlannerPanelRules.h"
#include "ProceduralMeshComponent.h"

namespace
{
	struct FScopedSelection3DTestWorld
	{
		UWorld* World = nullptr;
		FScopedSelection3DTestWorld()
		{
			World = UWorld::CreateWorld(EWorldType::Game, false);
			if (World && GEngine)
			{
				GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
			}
			// As a loaded game map: AActor::ProcessEvent drops every call (dynamic delegates included) until the world's actors are initialized.
			if (World) World->InitializeActorsForPlay(FURL());
		}
		~FScopedSelection3DTestWorld()
		{
			if (World)
			{
				if (GEngine) GEngine->DestroyWorldContext(World);
				World->DestroyWorld(false);
				World->RemoveFromRoot();
			}
		}
	};

	/** 5 x 4 m room of 20 cm walls from the origin with a 90 cm door 200 cm along the south wall (edges 155 / 245); returns the south wall. */
	int32 DrawSelectionTestRoom(ARoomPlannerManager* Manager)
	{
		const int32 South = Manager->AddWallBetweenPoints(FVector2D(0., 0.), FVector2D(500., 0.));
		Manager->AddWallBetweenPoints(FVector2D(500., 0.), FVector2D(500., 400.));
		Manager->AddWallBetweenPoints(FVector2D(500., 400.), FVector2D(0., 400.));
		Manager->AddWallBetweenPoints(FVector2D(0., 400.), FVector2D(0., 0.));
		Manager->AddOpeningToWall(South, EOpeningType::Door, 200.f, 90.f, 210.f, 0.f);
		Manager->RebuildRooms();
		return South;
	}

	const FProcMeshSection* OverlaySection(const UProceduralMeshComponent* Mesh)
	{
		return (Mesh && Mesh->GetNumSections() > 0) ? const_cast<UProceduralMeshComponent*>(Mesh)->GetProcMeshSection(0) : nullptr;
	}

	/** A cursor hit on the south wall at (X, Y, Z), traced from the room (+Y) or from outside (−Y). */
	FHitResult SouthWallHit(ARoomPlannerManager* Manager, int32 South, double X, double Y, double Z, bool bFromRoom)
	{
		AProceduralWallActor* Wall = nullptr;
		for (TActorIterator<AProceduralWallActor> It(Manager->GetWorld()); It; ++It)
		{
			if (It->WallData.SegmentID == South) { Wall = *It; break; }
		}
		FHitResult Hit(Wall, Wall ? Wall->WallProceduralMesh.Get() : nullptr, FVector(X, Y, Z), FVector(0., bFromRoom ? 1. : -1., 0.));
		Hit.ImpactPoint = FVector(X, Y, Z);
		Hit.TraceStart = FVector(X, bFromRoom ? 300. : -300., 160.);
		Hit.TraceEnd = FVector(X, bFromRoom ? -300. : 300., 160.);
		return Hit;
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// Click rules of the 3D pointer
// ─────────────────────────────────────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlannerSelection3DClickRulesTest, "MaxiMall.Planner.Selection3D.ClickRules",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlannerSelection3DClickRulesTest::RunTest(const FString& Parameters)
{
	const FPlannerClickLimits Limits; // 6 px, 3 look input, 0.6 s
	TestTrue(TEXT("A still, quick right click selects"), PlannerPanelRules::IsPointerClick(0.f, 0.f, 0.15f, Limits));
	TestTrue(TEXT("A hand's jitter still clicks"), PlannerPanelRules::IsPointerClick(4.f, 1.f, 0.3f, Limits));
	TestFalse(TEXT("The cursor travelled: an orbit, not a click"), PlannerPanelRules::IsPointerClick(20.f, 0.f, 0.2f, Limits));
	TestFalse(TEXT("The camera turned (cursor locked in place): an orbit"), PlannerPanelRules::IsPointerClick(0.f, 12.f, 0.2f, Limits));
	TestFalse(TEXT("Held long: not a click"), PlannerPanelRules::IsPointerClick(0.f, 0.f, 1.5f, Limits));
	TestFalse(TEXT("A door press that has not moved is still a click"), PlannerPanelRules::StartsOpeningDrag(3.f, 5.f));
	TestTrue(TEXT("… and becomes a drag once it moved"), PlannerPanelRules::StartsOpeningDrag(8.f, 5.f));
	TestEqual(TEXT("Pulse starts at its maximum"), PlannerSelectionOverlay::PulseFraction(0.f, 1.6f, 0.05f, 0.14f), 0.14f, 1.e-4f);
	TestEqual(TEXT("… and reaches its minimum half a period later"), PlannerSelectionOverlay::PulseFraction(0.8f, 1.6f, 0.05f, 0.14f), 0.05f, 1.e-4f);
	TestEqual(TEXT("White at EV100 6.8 is about 111 cd/m²"), PlannerSelectionOverlay::LuminanceForFraction(6.8f, 1.f), 111.43f, 0.05f);

	// The wheel in 3D: camera zoom, except while the left button drags an object (then it turns it).
	FPlannerWheelInputs Wheel;
	Wheel.bPlannerOpen = true;
	Wheel.bIs2D = false;
	Wheel.bHasObjectOrSetSelected = true;
	TestTrue(TEXT("3D, an object selected but not dragged: the wheel zooms"), PlannerPanelRules::ResolveWheelTarget(Wheel) == EPlannerWheelTarget::None);
	Wheel.bDraggingObject3D = true;
	TestTrue(TEXT("3D, dragging it: the wheel turns it"), PlannerPanelRules::ResolveWheelTarget(Wheel) == EPlannerWheelTarget::Selection);
	Wheel.bPlannerOpen = false;
	TestTrue(TEXT("Planner closed: never"), PlannerPanelRules::ResolveWheelTarget(Wheel) == EPlannerWheelTarget::None);
	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// The 3D selection overlay: frame + tint, the finish stays; the plan keeps the selection material
// ─────────────────────────────────────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlannerSelection3DOverlayTest, "MaxiMall.Planner.Selection3D.Overlay",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlannerSelection3DOverlayTest::RunTest(const FString& Parameters)
{
	FScopedSelection3DTestWorld TestWorld;
	if (!TestNotNull(TEXT("Test world"), TestWorld.World)) return false;
	ARoomPlannerManager* Manager = TestWorld.World->SpawnActor<ARoomPlannerManager>();
	if (!TestNotNull(TEXT("Manager"), Manager)) return false;
	const int32 South = DrawSelectionTestRoom(Manager);
	Manager->SetViewMode(false);
	if (!TestEqual(TEXT("One room"), Manager->GetRoomsForDebug().Num(), 1)) return false;

	const float Band = Manager->SelectionFrameWidthCm;
	constexpr double FacePlaneY = 10.4; // the room-side face (y = 10) plus the overlay's lift

	// ── A wall face, from the room ──
	Manager->SelectedSegmentID = South;
	Manager->SelectedOpeningIndex = -1;
	Manager->bSelectedWallFaceLeft = true; // the south wall runs +X: its left face (+Y) is the room side
	Manager->UpdateSelectionVisuals();
	TestTrue(TEXT("3D wall: the overlay shows"), Manager->IsSelectionOverlayShown());
	const FProcMeshSection* Tint = OverlaySection(Manager->SelectionTintMesh);
	const FProcMeshSection* Frame = OverlaySection(Manager->SelectionFrameMesh);
	if (!TestNotNull(TEXT("Tint over the face"), Tint) || !TestNotNull(TEXT("Frame on the face"), Frame)) return false;
	{
		const FProcMeshSection* Face = nullptr;
		for (TActorIterator<AProceduralWallActor> It(TestWorld.World); It; ++It)
		{
			if (It->WallData.SegmentID == South)
			{
				Face = It->WallProceduralMesh->GetProcMeshSection(AProceduralWallActor::LeftFaceSection);
				TestTrue(TEXT("3D: the face keeps its own material (no selection material)"),
					!Manager->WallSelectionMaterial || It->WallProceduralMesh->GetMaterial(AProceduralWallActor::LeftFaceSection) != Manager->WallSelectionMaterial);
			}
		}
		TestTrue(TEXT("The tint copies the whole face section"), Face && Face->ProcIndexBuffer.Num() == Tint->ProcIndexBuffer.Num());
	}
	bool bFrameOnFace = true, bNoBandAcrossDoor = true, bBandAlongFloor = false, bBandAroundDoorTop = false;
	for (const FProcMeshVertex& V : Frame->ProcVertexBuffer)
	{
		bFrameOnFace &= FMath::IsNearlyEqual(V.Position.Y, FacePlaneY, 0.01);
		// The floor band's top edge (z = Band) stops at the door's jamb bands (151..155 and 245..249).
		if (V.Position.X > 156. && V.Position.X < 244. && FMath::IsNearlyEqual(V.Position.Z, Band, 0.01)) bNoBandAcrossDoor = false;
		if (V.Position.X > 300. && FMath::IsNearlyEqual(V.Position.Z, Band, 0.01)) bBandAlongFloor = true;
		// Clear of the casing: 9 cm out from the door's edges (155 → 146, 210 → 219).
		if (FMath::IsNearlyEqual(V.Position.X, 146., 0.01) && FMath::IsNearlyEqual(V.Position.Z, 219. + Band, 0.01)) bBandAroundDoorTop = true;
	}
	TestTrue(TEXT("The frame lies on the room-side face, 0.4 cm in front of it"), bFrameOnFace);
	TestTrue(TEXT("The frame runs along the floor beside the door"), bBandAlongFloor);
	TestTrue(TEXT("… not across the doorway"), bNoBandAcrossDoor);
	TestTrue(TEXT("… and around the door's head"), bBandAroundDoorTop);
	TestTrue(TEXT("The dimension line on the floor is plan-only now"), Manager->GetSelectionDimensionLines().IsEmpty());

	// Suppressed after a catalog colour: nothing drawn, the finish is seen as it is.
	Manager->SetSelectionHighlightSuppressed(true);
	TestFalse(TEXT("Suppressed: no overlay"), Manager->IsSelectionOverlayShown());
	Manager->SetSelectionHighlightSuppressed(false);
	TestTrue(TEXT("… back when the suppression ends"), Manager->IsSelectionOverlayShown());

	// The selection's identity (a right click on the selection deselects it): the same face again is the same, the other face is not.
	{
		const FString Face = Manager->GetSelectionSignature();
		TestFalse(TEXT("A selected face has a signature"), Face.IsEmpty());
		Manager->bSelectedWallFaceLeft = false;
		TestNotEqual(TEXT("The wall's other face is another selection"), Manager->GetSelectionSignature(), Face);
		Manager->bSelectedWallFaceLeft = true;
		TestEqual(TEXT("… the same face the same"), Manager->GetSelectionSignature(), Face);
		Manager->SelectOpening(South, 0, true);
		const FString Door = Manager->GetSelectionSignature();
		Manager->SelectOpening(South, 0, false);
		TestEqual(TEXT("A door is the same selection from either side"), Manager->GetSelectionSignature(), Door);
		Manager->ClearAllSelection();
		TestTrue(TEXT("Nothing selected: empty"), Manager->GetSelectionSignature().IsEmpty());
		Manager->SelectedSegmentID = South;
		Manager->SelectedOpeningIndex = -1;
		Manager->bSelectedWallFaceLeft = true;
		Manager->UpdateSelectionVisuals();
	}

	// ── The door: frame around it on both faces, tint over its hole on both faces ──
	TestTrue(TEXT("The door selected"), Manager->SelectOpening(South, 0, true));
	Tint = OverlaySection(Manager->SelectionTintMesh);
	Frame = OverlaySection(Manager->SelectionFrameMesh);
	if (!TestNotNull(TEXT("Door tint"), Tint) || !TestNotNull(TEXT("Door frame"), Frame)) return false;
	TestEqual(TEXT("Door tint: one quad per face"), Tint->ProcVertexBuffer.Num(), 8);
	bool bRoomSide = false, bOutside = false, bOnlyAroundDoor = true;
	for (const FProcMeshVertex& V : Frame->ProcVertexBuffer)
	{
		bRoomSide |= FMath::IsNearlyEqual(V.Position.Y, FacePlaneY, 0.01);
		bOutside |= FMath::IsNearlyEqual(V.Position.Y, -FacePlaneY, 0.01);
		bOnlyAroundDoor &= V.Position.X >= 146. - Band - 0.01 && V.Position.X <= 254. + Band + 0.01 && V.Position.Z <= 219. + Band + 0.01;
	}
	TestTrue(TEXT("Door frame on the room side"), bRoomSide);
	TestTrue(TEXT("… and outside"), bOutside);
	TestTrue(TEXT("… only around the door (no wall outline)"), bOnlyAroundDoor);

	// ── The floor: tint on its top only, frame along the clear outline ──
	TestTrue(TEXT("Floor picked"), Manager->SelectFloorAtWorldPos(FVector(250., 200., 0.)) != -1);
	Tint = OverlaySection(Manager->SelectionTintMesh);
	Frame = OverlaySection(Manager->SelectionFrameMesh);
	if (!TestNotNull(TEXT("Floor tint"), Tint) || !TestNotNull(TEXT("Floor frame"), Frame)) return false;
	bool bUp = true;
	for (const FProcMeshVertex& V : Tint->ProcVertexBuffer) bUp &= V.Normal.Z > 0.5f;
	TestTrue(TEXT("Floor tint: its top only"), bUp);
	bool bInside = true;
	for (const FProcMeshVertex& V : Frame->ProcVertexBuffer)
	{
		bInside &= V.Position.X >= 9.99 && V.Position.X <= 490.01 && V.Position.Y >= 9.99 && V.Position.Y <= 390.01;
	}
	TestTrue(TEXT("Floor frame inside the clear floor (inner faces at 10 / 490 / 390)"), bInside);
	TestTrue(TEXT("3D floor: its own material"), !Manager->WallSelectionMaterial
		|| Manager->FloorProceduralMesh->GetMaterial(Manager->SelectedRoomID - 1) != Manager->WallSelectionMaterial);

	// ── An object: the tint as its overlay material ──
	const FString Obj = Manager->AddPlacedObject(TEXT("/Engine/BasicShapes/Cube.Cube"), FVector(250., 200., 0.), FRotator::ZeroRotator, FVector::OneVector);
	if (TestFalse(TEXT("Cube placed"), Obj.IsEmpty()))
	{
		TestTrue(TEXT("Cube selected"), Manager->SelectPlacedObject(Obj));
		TestTrue(TEXT("Object: tint as overlay material"), Manager->GetSelectionOverlayMaterialCount() > 0);
		Manager->ClearAllSelection();
		TestEqual(TEXT("Deselected: overlay material removed"), Manager->GetSelectionOverlayMaterialCount(), 0);
		const APlannerPlacedObjectActor* Actor = Manager->FindPlacedObjectActor(Obj);
		TestTrue(TEXT("… from the mesh too"), Actor && Actor->MeshComponent->GetOverlayMaterial() == nullptr);
	}

	// ── The plan: the selection material as before, no overlay ──
	Manager->SetViewMode(true);
	Manager->SelectedSegmentID = South;
	Manager->SelectedOpeningIndex = -1;
	Manager->UpdateSelectionVisuals();
	TestFalse(TEXT("2D: no overlay"), Manager->IsSelectionOverlayShown());
	if (Manager->WallSelectionMaterial)
	{
		for (TActorIterator<AProceduralWallActor> It(TestWorld.World); It; ++It)
		{
			if (It->WallData.SegmentID == South)
			{
				TestTrue(TEXT("2D: the whole wall shows the selection material"),
					It->WallProceduralMesh->GetMaterial(AProceduralWallActor::LeftFaceSection) == Manager->WallSelectionMaterial
					&& It->WallProceduralMesh->GetMaterial(AProceduralWallActor::RightFaceSection) == Manager->WallSelectionMaterial);
			}
		}
	}
	TestEqual(TEXT("2D: the wall's length line on the plan"), Manager->GetSelectionDimensionLines().Num(), 1);
	Manager->Destroy();
	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// 3D doors: found under the cursor, dragged along the wall face
// ─────────────────────────────────────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlannerSelection3DOpeningDragTest, "MaxiMall.Planner.Selection3D.OpeningDrag",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlannerSelection3DOpeningDragTest::RunTest(const FString& Parameters)
{
	FScopedSelection3DTestWorld TestWorld;
	if (!TestNotNull(TEXT("Test world"), TestWorld.World)) return false;
	ARoomPlannerManager* Manager = TestWorld.World->SpawnActor<ARoomPlannerManager>();
	if (!TestNotNull(TEXT("Manager"), Manager)) return false;
	const int32 South = DrawSelectionTestRoom(Manager);
	Manager->SetViewMode(false);

	int32 Seg = -1, Op = -1;
	bool bFaceLeft = false;
	TestTrue(TEXT("A hit on the wall beside the door jamb finds the door"), Manager->FindOpeningAtHit(SouthWallHit(Manager, South, 158., 10., 120., true), Seg, Op, bFaceLeft)
		&& Seg == South && Op == 0 && bFaceLeft);
	TestTrue(TEXT("… from outside: the outer face"), Manager->FindOpeningAtHit(SouthWallHit(Manager, South, 240., -10., 60., false), Seg, Op, bFaceLeft)
		&& Op == 0 && !bFaceLeft);
	TestFalse(TEXT("A hit on the wall away from the door finds nothing"), Manager->FindOpeningAtHit(SouthWallHit(Manager, South, 400., 10., 120., true), Seg, Op, bFaceLeft));
	TestFalse(TEXT("… nor one above its head"), Manager->FindOpeningAtHit(SouthWallHit(Manager, South, 200., 10., 250., true), Seg, Op, bFaceLeft));

	// The cursor ray from eye height in the room, looking down at the wall.
	float Along = 0.f;
	const FVector Eye(300., 300., 160.);
	TestTrue(TEXT("A ray meets the room-side face plane"), Manager->ProjectRayOntoWallFace(South, true, Eye, (FVector(300., 10., 100.) - Eye).GetSafeNormal(), Along)
		&& FMath::IsNearlyEqual(Along, 300.f, 0.01f));
	TestFalse(TEXT("A ray away from the face does not"), Manager->ProjectRayOntoWallFace(South, true, Eye, FVector(0., 1., 0.), Along));

	// Grabbed 20 cm right of its centre and dragged so the cursor is at x = 330: the centre goes to 310.
	if (!TestTrue(TEXT("Door selected from the room"), Manager->SelectOpening(South, 0, true))) return false;
	TestTrue(TEXT("Drag frame"), Manager->DragSelectedOpeningAlongRay(Eye, (FVector(330., 10., 100.) - Eye).GetSafeNormal(), 20.f));
	float Dist = 0.f;
	TestTrue(TEXT("The door follows the cursor, where it was grabbed"), Manager->GetOpeningDistance(South, Manager->SelectedOpeningIndex, Dist)
		&& FMath::IsNearlyEqual(Dist, 310.f, 0.05f));
	// Past the end of the wall: it stops where it still fits (5 cm from the centre-line end, as the 2D drag).
	Manager->DragSelectedOpeningAlongRay(Eye, (FVector(900., 10., 100.) - Eye).GetSafeNormal(), 0.f);
	TestTrue(TEXT("Dragged past the corner: clamped on the wall"), Manager->GetOpeningDistance(South, Manager->SelectedOpeningIndex, Dist)
		&& FMath::IsNearlyEqual(Dist, 500.f - 45.f - 5.f, 0.05f));
	Manager->Destroy();
	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Lighting: one light scale — the level's auto exposure everywhere, the planner only quickens it
// ─────────────────────────────────────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlannerSelection3DOneLightScaleTest, "MaxiMall.Planner.Selection3D.OneLightScale",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlannerSelection3DOneLightScaleTest::RunTest(const FString& Parameters)
{
	FScopedSelection3DTestWorld TestWorld;
	if (!TestNotNull(TEXT("Test world"), TestWorld.World)) return false;
	ARoomPlannerManager* Manager = TestWorld.World->SpawnActor<ARoomPlannerManager>();
	if (!TestNotNull(TEXT("Manager"), Manager)) return false;
	Manager->SetViewMode(false);
	Manager->SetPlannerSessionActive(true);

	const FPostProcessSettings& PP = Manager->PlannerExposure->Settings;
	auto OnlyAdaptSpeed = [&]()
	{
		// The level's exposure (method, range, bias untouched), only adapting at the planner's speed.
		return Manager->PlannerExposure->bEnabled && !PP.bOverride_AutoExposureMinBrightness && !PP.bOverride_AutoExposureMaxBrightness
			&& !PP.bOverride_AutoExposureMethod && !PP.bOverride_AutoExposureBias && PP.bOverride_AutoExposureSpeedUp && PP.bOverride_AutoExposureSpeedDown
			&& FMath::IsNearlyEqual(PP.AutoExposureSpeedUp, Manager->PlannerExposureAdaptSpeed) && FMath::IsNearlyEqual(PP.AutoExposureSpeedDown, Manager->PlannerExposureAdaptSpeed);
	};

	Manager->AddWallBetweenPoints(FVector2D(0., 0.), FVector2D(500., 0.));
	Manager->UpdatePlannerExposure();
	TestTrue(TEXT("One wall: the level's exposure, only quicker"), OnlyAdaptSpeed());
	Manager->AddWallBetweenPoints(FVector2D(500., 0.), FVector2D(500., 400.));
	Manager->AddWallBetweenPoints(FVector2D(500., 400.), FVector2D(0., 400.));
	Manager->AddWallBetweenPoints(FVector2D(0., 400.), FVector2D(0., 0.));
	Manager->UpdatePlannerExposure();
	TestEqual(TEXT("Closed: one room"), Manager->GetRoomsForDebug().Num(), 1);
	TestTrue(TEXT("A closed room: still the level's exposure (no fixed interior exposure any more)"), OnlyAdaptSpeed());
	TestTrue(TEXT("The room light is on the level's scale (well under a lumen per m², not 280)"), Manager->RoomLightSettings.LumensPerM2 < 5.f);
	TestEqual(TEXT("… 280 lm/m² at EV100 6.8 brought to the level's exposure (2^8.8 less)"), Manager->RoomLightSettings.LumensPerM2, 280.f / FMath::Pow(2.f, 8.8f), 0.03f);

	Manager->SetPlannerSessionActive(false);
	TestFalse(TEXT("Planner closed: no override at all"), Manager->PlannerExposure->bEnabled);
	Manager->Destroy();
	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// 3D rotation (buttons on the 15° grid, fine wheel) and walls that stop a dragged item
// ─────────────────────────────────────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlannerSelection3DRotateAndWallsTest, "MaxiMall.Planner.Selection3D.RotateAndWalls",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlannerSelection3DRotateAndWallsTest::RunTest(const FString& Parameters)
{
	// The buttons: round to the 15° grid, then one step.
	TestEqual(TEXT("0 + 15 → 15"), PlannerPanelRules::SnapAndStepYaw(0.f, 15.f, 15.f), 15.f, 1.e-4f);
	TestEqual(TEXT("7 rounds to 0, + 15 → 15"), PlannerPanelRules::SnapAndStepYaw(7.f, 15.f, 15.f), 15.f, 1.e-4f);
	TestEqual(TEXT("8 rounds to 15, + 15 → 30"), PlannerPanelRules::SnapAndStepYaw(8.f, 15.f, 15.f), 30.f, 1.e-4f);
	TestEqual(TEXT("37 rounds to 30, − 15 → 15"), PlannerPanelRules::SnapAndStepYaw(37.f, -15.f, 15.f), 15.f, 1.e-4f);
	TestEqual(TEXT("−52 rounds to −45, − 15 → −60"), PlannerPanelRules::SnapAndStepYaw(-52.f, -15.f, 15.f), -60.f, 1.e-4f);
	TestEqual(TEXT("178 rounds to 180, + 15 → −165 (wrapped)"), PlannerPanelRules::SnapAndStepYaw(178.f, 15.f, 15.f), -165.f, 1.e-4f);

	FScopedSelection3DTestWorld TestWorld;
	if (!TestNotNull(TEXT("Test world"), TestWorld.World)) return false;
	ARoomPlannerManager* Manager = TestWorld.World->SpawnActor<ARoomPlannerManager>();
	if (!TestNotNull(TEXT("Manager"), Manager)) return false;
	const int32 South = DrawSelectionTestRoom(Manager); // door on the south wall, 155…245
	Manager->SetViewMode(false);

	// 3D turns a selected object (the buttons / the wheel while dragged); it used to refuse outside 2D.
	const FString Obj = Manager->AddPlacedObject(TEXT("/Engine/BasicShapes/Cube.Cube"), FVector(250., 200., 0.), FRotator::ZeroRotator, FVector(0.6, 0.6, 0.6));
	if (!TestFalse(TEXT("Cube placed (60 cm)"), Obj.IsEmpty())) return false;
	TestTrue(TEXT("Selected"), Manager->SelectPlacedObject(Obj));
	FString ID;
	bool bSet = false;
	float Yaw = 0.f;
	TestTrue(TEXT("3D: a 1° wheel turn"), Manager->RotateSelectionLocal(1.f, ID, bSet, Yaw) && FMath::IsNearlyEqual(Yaw, 1.f, 1.e-3f));

	// Walls stop a dragged item. The room's clear floor is x 10…490, y 10…390; the cube is 60 cm (30 cm half, 29 with the slack).
	const AActor* Actor = Manager->FindPlacedObjectActor(Obj);
	Manager->MovePlacedObjectLocal(Obj, FVector(250., 200., 0.), FRotator::ZeroRotator);
	const FVector Inside(250., 200., 0.);
	TestTrue(TEXT("A step inside the room is free"), Manager->ConstrainDragOutsideWalls(Actor, Inside, FVector(300., 220., 0.)).Equals(FVector(300., 220., 0.), 0.01));
	const FVector IntoEast = Manager->ConstrainDragOutsideWalls(Actor, Inside, FVector(495., 260., 0.));
	TestTrue(TEXT("Into the east wall diagonally: slides along it (keeps x, takes y)"), IntoEast.Equals(FVector(250., 260., 0.), 0.01));
	TestTrue(TEXT("Straight into the east wall: stays"), Manager->ConstrainDragOutsideWalls(Actor, Inside, FVector(480., 200., 0.)).Equals(Inside, 0.01));
	// Through the doorway (x 155…245) the south wall is open: the cube (60 wide) passes at x 200.
	Manager->MovePlacedObjectLocal(Obj, FVector(200., 60., 0.), FRotator::ZeroRotator);
	TestTrue(TEXT("Through the doorway: free"), Manager->ConstrainDragOutsideWalls(Actor, FVector(200., 60., 0.), FVector(200., 0., 0.)).Equals(FVector(200., 0., 0.), 0.01));
	TestFalse(TEXT("… but not through the wall beside it"), Manager->ConstrainDragOutsideWalls(Actor, FVector(200., 60., 0.), FVector(350., 0., 0.)).Equals(FVector(350., 0., 0.), 0.01));
	(void)South;
	Manager->Destroy();
	return true;
}


#endif // WITH_DEV_AUTOMATION_TESTS
