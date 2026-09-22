// Copyright 2026 MaxiMall. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Constructor/PlannerFinishLayout.h"
#include "Constructor/RoomPlannerManager.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "ProceduralMeshComponent.h"

namespace
{
	struct FScopedRoomTestWorld
	{
		UWorld* World = nullptr;
		FScopedRoomTestWorld()
		{
			World = UWorld::CreateWorld(EWorldType::Game, false);
			if (World && GEngine)
			{
				GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
			}
		}
		~FScopedRoomTestWorld()
		{
			if (World)
			{
				if (GEngine) GEngine->DestroyWorldContext(World);
				World->DestroyWorld(false);
				World->RemoveFromRoot();
			}
		}
	};

	/** The four outer walls of a W x D room from the origin, drawn as the wall tool does. */
	void DrawRectangle(ARoomPlannerManager* Manager, double W, double D)
	{
		Manager->AddWallBetweenPoints(FVector2D(0., 0.), FVector2D(W, 0.));
		Manager->AddWallBetweenPoints(FVector2D(W, 0.), FVector2D(W, D));
		Manager->AddWallBetweenPoints(FVector2D(W, D), FVector2D(0., D));
		Manager->AddWallBetweenPoints(FVector2D(0., D), FVector2D(0., 0.));
	}

	/** The room whose centre-line outline contains the point (-1 if none). */
	int32 RoomAt(const ARoomPlannerManager* Manager, double X, double Y)
	{
		return Manager->FindRoomAtWorldPos(FVector(X, Y, 0.));
	}

	/** Bounds of a room's floor section (top face only, Z = 1). */
	FBox2D FloorBounds(const ARoomPlannerManager* Manager, int32 RoomID)
	{
		FBox2D Box(ForceInit);
		const FProcMeshSection* Section = Manager->FloorProceduralMesh ? Manager->FloorProceduralMesh->GetProcMeshSection(RoomID - 1) : nullptr;
		if (!Section) return Box;
		for (const FProcMeshVertex& V : Section->ProcVertexBuffer)
		{
			if (FMath::IsNearlyEqual(V.Position.Z, 1., 0.01)) Box += FVector2D(V.Position.X, V.Position.Y);
		}
		return Box;
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// Rooms are divided however the partition was drawn
// ─────────────────────────────────────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlannerRoomDivisionTest, "MaxiMall.Planner.Rooms.Division",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlannerRoomDivisionTest::RunTest(const FString& Parameters)
{
	auto RoomCount = [](const ARoomPlannerManager* Manager) { return Manager->GetRoomsForDebug().Num(); };

	// Drawn across the room and through both walls (overshooting): the walls are joined where they cross.
	{
		FScopedRoomTestWorld TestWorld;
		ARoomPlannerManager* Manager = TestWorld.World->SpawnActor<ARoomPlannerManager>();
		if (!TestNotNull(TEXT("Manager"), Manager)) return false;
		DrawRectangle(Manager, 600., 400.);
		TestEqual(TEXT("One room before the partition"), RoomCount(Manager), 1);
		Manager->AddWallBetweenPoints(FVector2D(300., -60.), FVector2D(300., 460.));
		TestEqual(TEXT("Partition drawn through both walls: two rooms"), RoomCount(Manager), 2);
		TestTrue(TEXT("Two different rooms either side"), RoomAt(Manager, 150., 200.) != -1 && RoomAt(Manager, 450., 200.) != -1
			&& RoomAt(Manager, 150., 200.) != RoomAt(Manager, 450., 200.));
		Manager->Destroy();
	}

	// The partition drawn first, then the outer walls over its ends: joined at the corners they pass over.
	{
		FScopedRoomTestWorld TestWorld;
		ARoomPlannerManager* Manager = TestWorld.World->SpawnActor<ARoomPlannerManager>();
		if (!TestNotNull(TEXT("Manager"), Manager)) return false;
		Manager->AddWallBetweenPoints(FVector2D(300., 0.), FVector2D(300., 400.));
		DrawRectangle(Manager, 600., 400.);
		TestEqual(TEXT("Partition drawn before the outer walls: two rooms"), RoomCount(Manager), 2);
		Manager->Destroy();
	}

	// Two partitions crossing each other and the outer walls: four rooms.
	{
		FScopedRoomTestWorld TestWorld;
		ARoomPlannerManager* Manager = TestWorld.World->SpawnActor<ARoomPlannerManager>();
		if (!TestNotNull(TEXT("Manager"), Manager)) return false;
		DrawRectangle(Manager, 600., 400.);
		Manager->AddWallBetweenPoints(FVector2D(300., -40.), FVector2D(300., 440.));
		Manager->AddWallBetweenPoints(FVector2D(-40., 200.), FVector2D(640., 200.));
		TestEqual(TEXT("Crossing partitions: four rooms"), RoomCount(Manager), 4);
		Manager->Destroy();
	}

	// A partition ending 15 cm short of where the last wall is then drawn: its free end is drawn onto that wall.
	{
		FScopedRoomTestWorld TestWorld;
		ARoomPlannerManager* Manager = TestWorld.World->SpawnActor<ARoomPlannerManager>();
		if (!TestNotNull(TEXT("Manager"), Manager)) return false;
		Manager->AddWallBetweenPoints(FVector2D(0., 400.), FVector2D(0., 0.));
		Manager->AddWallBetweenPoints(FVector2D(0., 0.), FVector2D(600., 0.));
		Manager->AddWallBetweenPoints(FVector2D(600., 0.), FVector2D(600., 400.));
		Manager->AddWallBetweenPoints(FVector2D(300., 0.), FVector2D(300., 385.));
		TestEqual(TEXT("Open on one side: no room yet"), RoomCount(Manager), 0);
		Manager->AddWallBetweenPoints(FVector2D(600., 400.), FVector2D(0., 400.));
		TestEqual(TEXT("Closing wall drawn past the partition's free end: two rooms"), RoomCount(Manager), 2);
		Manager->Destroy();
	}
	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Each room its own floor: clear area, thresholds, selection, area
// ─────────────────────────────────────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlannerRoomFloorsTest, "MaxiMall.Planner.Rooms.Floors",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlannerRoomFloorsTest::RunTest(const FString& Parameters)
{
	FScopedRoomTestWorld TestWorld;
	ARoomPlannerManager* Manager = TestWorld.World->SpawnActor<ARoomPlannerManager>();
	if (!TestNotNull(TEXT("Manager"), Manager)) return false;
	DrawRectangle(Manager, 600., 400.);
	const int32 Partition = Manager->AddWallBetweenPoints(FVector2D(300., 0.), FVector2D(300., 400.));
	const int32 Left = RoomAt(Manager, 150., 200.);
	const int32 Right = RoomAt(Manager, 450., 200.);
	if (!TestTrue(TEXT("Two rooms"), Left != -1 && Right != -1 && Left != Right)) return false;

	// Clear floors: out to the inner faces of the 20 cm walls, not under them; 2.80 x 3.80 m each.
	{
		const FBox2D L = FloorBounds(Manager, Left);
		const FBox2D R = FloorBounds(Manager, Right);
		TestTrue(TEXT("Left floor between the inner faces"), L.Min.Equals(FVector2D(10., 10.), 0.01) && L.Max.Equals(FVector2D(290., 390.), 0.01));
		TestTrue(TEXT("Right floor between the inner faces"), R.Min.Equals(FVector2D(310., 10.), 0.01) && R.Max.Equals(FVector2D(590., 390.), 0.01));
		FRoomData Room;
		TestTrue(TEXT("Left room: net area 10.64 m²"), Manager->GetRoomData(Left, Room) && FMath::IsNearlyEqual(Room.AreaM2, 10.64f, 0.001f));
		TestTrue(TEXT("Total floor area: both rooms, net"), FMath::IsNearlyEqual(Manager->CalculateFloorAreaM2(), 21.28f, 0.001f));
	}

	// A door in the partition: each floor runs on through it to the middle of the wall, where the other room's floor begins.
	TestTrue(TEXT("Door added"), Manager->AddOpeningToWall(Partition, EOpeningType::Door, 200.f, 90.f, 210.f, 0.f));
	Manager->RebuildRooms();
	{
		const FBox2D L = FloorBounds(Manager, RoomAt(Manager, 150., 200.));
		const FBox2D R = FloorBounds(Manager, RoomAt(Manager, 450., 200.));
		TestTrue(TEXT("Left floor reaches the middle of the doorway"), FMath::IsNearlyEqual(L.Max.X, 300., 0.01));
		TestTrue(TEXT("Right floor starts at the middle of the doorway"), FMath::IsNearlyEqual(R.Min.X, 300., 0.01));
		// Only over the door (y 155..245): check the vertices at x = 300.
		const FProcMeshSection* Section = Manager->FloorProceduralMesh->GetProcMeshSection(RoomAt(Manager, 150., 200.) - 1);
		bool bOnlyInDoorway = Section != nullptr;
		for (const FProcMeshVertex& V : Section ? Section->ProcVertexBuffer : TArray<FProcMeshVertex>())
		{
			if (V.Position.X > 290.01 && (V.Position.Y < 154.99 || V.Position.Y > 245.01)) bOnlyInDoorway = false;
		}
		TestTrue(TEXT("The threshold covers the doorway only"), bOnlyInDoorway);
		FRoomData Room;
		TestTrue(TEXT("The threshold does not count in the room's area"), Manager->GetRoomData(RoomAt(Manager, 150., 200.), Room) && FMath::IsNearlyEqual(Room.AreaM2, 10.64f, 0.001f));
	}

	// Picking a floor picks that room only; the selection shows on its floor only.
	const int32 Picked = Manager->SelectFloorAtWorldPos(FVector(150., 200., 0.));
	TestEqual(TEXT("A click in the left room picks the left room"), Picked, RoomAt(Manager, 150., 200.));
	TestEqual(TEXT("Selected room"), Manager->SelectedRoomID, RoomAt(Manager, 150., 200.));
	if (Manager->WallSelectionMaterial)
	{
		TestTrue(TEXT("Left floor shows the selection"), Manager->FloorProceduralMesh->GetMaterial(RoomAt(Manager, 150., 200.) - 1) == Manager->WallSelectionMaterial);
		TestTrue(TEXT("Right floor does not"), Manager->FloorProceduralMesh->GetMaterial(RoomAt(Manager, 450., 200.) - 1) != Manager->WallSelectionMaterial);
	}
	TestEqual(TEXT("A click in the right room picks the right room"), Manager->SelectFloorAtWorldPos(FVector(450., 200., 0.)), RoomAt(Manager, 450., 200.));

	// The threshold follows the door when it is moved (no explicit rebuild), and goes with it when it is deleted.
	auto ThresholdSpan = [Manager](FVector2D& OutSpan)
	{
		const FProcMeshSection* Section = Manager->FloorProceduralMesh->GetProcMeshSection(RoomAt(Manager, 150., 200.) - 1);
		OutSpan = FVector2D(TNumericLimits<double>::Max(), -TNumericLimits<double>::Max());
		for (const FProcMeshVertex& V : Section ? Section->ProcVertexBuffer : TArray<FProcMeshVertex>())
		{
			if (V.Position.X > 290.01)
			{
				OutSpan.X = FMath::Min(OutSpan.X, V.Position.Y);
				OutSpan.Y = FMath::Max(OutSpan.Y, V.Position.Y);
			}
		}
		return OutSpan.X <= OutSpan.Y;
	};
	FVector2D Span;
	TestTrue(TEXT("Door moved to 1.00 m"), Manager->UpdateOpeningPosition(Partition, 0, 100.f));
	TestTrue(TEXT("Moved door: the threshold is under the new doorway only (y 55..145)"), ThresholdSpan(Span)
		&& FMath::IsNearlyEqual(Span.X, 55., 0.01) && FMath::IsNearlyEqual(Span.Y, 145., 0.01));
	TestTrue(TEXT("Door deleted"), Manager->DeleteOpening(Partition, 0));
	TestFalse(TEXT("Deleted door: no threshold left"), ThresholdSpan(Span));
	Manager->Destroy();
	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// A finish belongs to one room
// ─────────────────────────────────────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlannerRoomFinishesTest, "MaxiMall.Planner.Rooms.Finishes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlannerRoomFinishesTest::RunTest(const FString& Parameters)
{
	auto Paint = [](const FLinearColor& Color, const TCHAR* Code)
	{
		FSurfaceFinish Finish;
		Finish.Type = ESurfaceFinishType::Paint;
		Finish.Color = Color;
		Finish.ColorCode = Code;
		return Finish;
	};
	const FSurfaceFinish Red = Paint(FLinearColor(0.8f, 0.1f, 0.1f), TEXT("RAL 3020"));
	const FSurfaceFinish Blue = Paint(FLinearColor(0.1f, 0.2f, 0.7f), TEXT("RAL 5015"));

	// Two narrow rooms side by side (90 cm apart centre to centre): each keeps its own floor and ceiling.
	{
		FScopedRoomTestWorld TestWorld;
		ARoomPlannerManager* Manager = TestWorld.World->SpawnActor<ARoomPlannerManager>();
		if (!TestNotNull(TEXT("Manager"), Manager)) return false;
		DrawRectangle(Manager, 180., 300.);
		Manager->AddWallBetweenPoints(FVector2D(90., 0.), FVector2D(90., 300.));
		const int32 Left = RoomAt(Manager, 45., 150.);
		const int32 Right = RoomAt(Manager, 135., 150.);
		if (!TestTrue(TEXT("Two narrow rooms"), Left != -1 && Right != -1 && Left != Right)) return false;

		FSurfaceFinish Out;
		TestTrue(TEXT("Set the left floor"), Manager->SetFloorFinish(Left, Red));
		TestTrue(TEXT("The right floor stays bare"), Manager->GetFloorFinish(Right, Out) && !Out.IsSet());
		TestTrue(TEXT("Set the right floor"), Manager->SetFloorFinish(Right, Blue));
		TestTrue(TEXT("The left floor keeps its own finish"), Manager->GetFloorFinish(Left, Out) && Out == Red);
		TestTrue(TEXT("The right floor has its own"), Manager->GetFloorFinish(Right, Out) && Out == Blue);

		Manager->RebuildRooms();
		const int32 LeftAgain = RoomAt(Manager, 45., 150.);
		const int32 RightAgain = RoomAt(Manager, 135., 150.);
		TestTrue(TEXT("Rebuilt: still their own floors"), Manager->GetFloorFinish(LeftAgain, Out) && Out == Red && Manager->GetFloorFinish(RightAgain, Out) && Out == Blue);

		TestTrue(TEXT("Set the left ceiling"), Manager->SetCeilingFinish(LeftAgain, Blue));
		FRoomData Room;
		TestTrue(TEXT("The right ceiling stays bare"), Manager->GetRoomData(RightAgain, Room) && !Room.CeilingFinish.IsSet());
		TestTrue(TEXT("Clearing the right floor leaves the left one"), Manager->SetFloorFinish(RightAgain, FSurfaceFinish())
			&& Manager->GetFloorFinish(LeftAgain, Out) && Out == Red);
		Manager->Destroy();
	}

	// A room finished and then divided: the finish stays with the part that holds its anchor, the other part is bare.
	{
		FScopedRoomTestWorld TestWorld;
		ARoomPlannerManager* Manager = TestWorld.World->SpawnActor<ARoomPlannerManager>();
		if (!TestNotNull(TEXT("Manager"), Manager)) return false;
		DrawRectangle(Manager, 600., 400.);
		TestTrue(TEXT("Set the whole floor"), Manager->SetFloorFinish(RoomAt(Manager, 300., 200.), Red));
		Manager->AddWallBetweenPoints(FVector2D(250., 0.), FVector2D(250., 400.));
		FSurfaceFinish Out;
		TestTrue(TEXT("Divided: the part with the anchor keeps the finish"), Manager->GetFloorFinish(RoomAt(Manager, 400., 200.), Out) && Out == Red);
		TestTrue(TEXT("Divided: the other part is bare"), Manager->GetFloorFinish(RoomAt(Manager, 100., 200.), Out) && !Out.IsSet());
		Manager->Destroy();
	}

	// Records to rooms: exclusive, by containment first (pure).
	{
		const TArray<TArray<FVector2D>> Rooms = {
			{ FVector2D(0., 0.), FVector2D(90., 0.), FVector2D(90., 300.), FVector2D(0., 300.) },
			{ FVector2D(90., 0.), FVector2D(180., 0.), FVector2D(180., 300.), FVector2D(90., 300.) } };
		const TArray<FVector2D> Centroids = { FVector2D(45., 150.), FVector2D(135., 150.) };
		const TArray<int32> One = PlannerFinishLayout::AssignRecordsToRooms({ FVector2D(45., 150.) }, Rooms, Centroids, 100.f);
		TestTrue(TEXT("One record, one room: not shared with the neighbour 90 cm away"), One[0] == 0 && One[1] == INDEX_NONE);
		const TArray<int32> Outside = PlannerFinishLayout::AssignRecordsToRooms({ FVector2D(45., 330.) }, Rooms, Centroids, 200.f);
		TestTrue(TEXT("An anchor just outside goes to the nearest room"), Outside[0] == 0 && Outside[1] == INDEX_NONE);
	}
	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// A room standing inside another one
// ─────────────────────────────────────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlannerNestedRoomTest, "MaxiMall.Planner.Rooms.Nested",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlannerNestedRoomTest::RunTest(const FString& Parameters)
{
	FScopedRoomTestWorld TestWorld;
	ARoomPlannerManager* Manager = TestWorld.World->SpawnActor<ARoomPlannerManager>();
	if (!TestNotNull(TEXT("Manager"), Manager)) return false;
	DrawRectangle(Manager, 800., 600.);
	Manager->AddWallBetweenPoints(FVector2D(300., 200.), FVector2D(500., 200.));
	Manager->AddWallBetweenPoints(FVector2D(500., 200.), FVector2D(500., 400.));
	Manager->AddWallBetweenPoints(FVector2D(500., 400.), FVector2D(300., 400.));
	Manager->AddWallBetweenPoints(FVector2D(300., 400.), FVector2D(300., 200.));
	const int32 Inner = RoomAt(Manager, 400., 300.);
	const int32 Outer = RoomAt(Manager, 100., 100.);
	TestTrue(TEXT("A click inside the inner room picks the inner room, not the one around it"), Inner != -1 && Outer != -1 && Inner != Outer);
	FRoomData Room;
	TestTrue(TEXT("The picked room is the small one"), Manager->GetRoomData(Inner, Room) && Room.AreaM2 < 4.f);

	// A finish on the outer room (whose centre lies in the inner room) lands on the outer room.
	FSurfaceFinish Red;
	Red.Type = ESurfaceFinishType::Paint;
	Red.Color = FLinearColor(0.8f, 0.1f, 0.1f);
	Red.ColorCode = TEXT("RAL 3020");
	FSurfaceFinish Out;
	TestTrue(TEXT("Set the outer floor"), Manager->SetFloorFinish(Outer, Red));
	TestTrue(TEXT("The outer floor has it"), Manager->GetFloorFinish(Outer, Out) && Out == Red);
	TestTrue(TEXT("The inner floor stays bare"), Manager->GetFloorFinish(Inner, Out) && !Out.IsSet());
	Manager->RebuildRooms();
	TestTrue(TEXT("Rebuilt: still on the outer floor only"), Manager->GetFloorFinish(RoomAt(Manager, 100., 100.), Out) && Out == Red
		&& Manager->GetFloorFinish(RoomAt(Manager, 400., 300.), Out) && !Out.IsSet());
	TestTrue(TEXT("The outer room's label point lies in the outer room"), Manager->GetRoomData(RoomAt(Manager, 100., 100.), Room)
		&& RoomAt(Manager, Room.InteriorPoint.X, Room.InteriorPoint.Y) == RoomAt(Manager, 100., 100.));
	Manager->Destroy();
	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Rooms kept apart through edits
// ─────────────────────────────────────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlannerRoomEditsTest, "MaxiMall.Planner.Rooms.Edits",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlannerRoomEditsTest::RunTest(const FString& Parameters)
{
	auto RoomCount = [](const ARoomPlannerManager* Manager) { return Manager->GetRoomsForDebug().Num(); };
	auto Paint = [](const FLinearColor& Color, const TCHAR* Code)
	{
		FSurfaceFinish Finish;
		Finish.Type = ESurfaceFinishType::Paint;
		Finish.Color = Color;
		Finish.ColorCode = Code;
		return Finish;
	};
	const FSurfaceFinish Red = Paint(FLinearColor(0.8f, 0.1f, 0.1f), TEXT("RAL 3020"));
	const FSurfaceFinish Blue = Paint(FLinearColor(0.1f, 0.2f, 0.7f), TEXT("RAL 5015"));
	auto FloorIs = [](const ARoomPlannerManager* Manager, double X, double Y, const FSurfaceFinish& Expected)
	{
		FSurfaceFinish Out;
		return Manager->GetFloorFinish(RoomAt(Manager, X, Y), Out) && Out == Expected;
	};

	// A box inside a room, joined to its wall by one wall: the room around it keeps its floor.
	{
		FScopedRoomTestWorld TestWorld;
		ARoomPlannerManager* Manager = TestWorld.World->SpawnActor<ARoomPlannerManager>();
		if (!TestNotNull(TEXT("Manager"), Manager)) return false;
		DrawRectangle(Manager, 800., 600.);
		Manager->AddWallBetweenPoints(FVector2D(300., 200.), FVector2D(500., 200.));
		Manager->AddWallBetweenPoints(FVector2D(500., 200.), FVector2D(500., 400.));
		Manager->AddWallBetweenPoints(FVector2D(500., 400.), FVector2D(300., 400.));
		Manager->AddWallBetweenPoints(FVector2D(300., 400.), FVector2D(300., 200.));
		Manager->AddWallBetweenPoints(FVector2D(400., 0.), FVector2D(400., 200.));
		const int32 Outer = RoomAt(Manager, 100., 100.);
		const int32 Inner = RoomAt(Manager, 400., 300.);
		TestEqual(TEXT("Bridged box: two rooms"), RoomCount(Manager), 2);
		TestTrue(TEXT("Bridged box: the room around it and the box are different rooms"), Outer != -1 && Inner != -1 && Outer != Inner);
		const FBox2D Floor = FloorBounds(Manager, Outer);
		TestTrue(TEXT("Bridged box: the room around it has its floor, out to the inner faces"), Floor.bIsValid
			&& Floor.Min.Equals(FVector2D(10., 10.), 0.01) && Floor.Max.Equals(FVector2D(790., 590.), 0.01));
		FRoomData Room;
		TestTrue(TEXT("Bridged box: the room around it has an area"), Manager->GetRoomData(Outer, Room) && Room.AreaM2 > 40.f);
		Manager->Destroy();
	}

	// A partition has a room on both sides: its interior side is the left one, whichever way it was drawn.
	for (const bool bDrawnUp : { true, false })
	{
		FScopedRoomTestWorld TestWorld;
		ARoomPlannerManager* Manager = TestWorld.World->SpawnActor<ARoomPlannerManager>();
		if (!TestNotNull(TEXT("Manager"), Manager)) return false;
		DrawRectangle(Manager, 600., 400.);
		const int32 Partition = bDrawnUp ? Manager->AddWallBetweenPoints(FVector2D(300., 0.), FVector2D(300., 400.))
			: Manager->AddWallBetweenPoints(FVector2D(300., 400.), FVector2D(300., 0.));
		TestTrue(bDrawnUp ? TEXT("Partition drawn up: left side interior") : TEXT("Partition drawn down: left side interior"), Manager->IsWallLeftSideInterior(Partition));
		Manager->RebuildRooms();
		TestTrue(TEXT("Rebuilt: still the left side"), Manager->IsWallLeftSideInterior(Partition));
		Manager->Destroy();
	}

	// Two finished rooms merged by removing the partition, then cleared: the other room's finish does not come back.
	{
		FScopedRoomTestWorld TestWorld;
		ARoomPlannerManager* Manager = TestWorld.World->SpawnActor<ARoomPlannerManager>();
		if (!TestNotNull(TEXT("Manager"), Manager)) return false;
		DrawRectangle(Manager, 600., 400.);
		const int32 Partition = Manager->AddWallBetweenPoints(FVector2D(300., 0.), FVector2D(300., 400.));
		TestTrue(TEXT("Both floors set"), Manager->SetFloorFinish(RoomAt(Manager, 150., 200.), Red) && Manager->SetFloorFinish(RoomAt(Manager, 450., 200.), Blue));
		Manager->RemoveWall(Partition);
		TestEqual(TEXT("Partition removed: one room"), RoomCount(Manager), 1);
		TestTrue(TEXT("Merged room cleared"), Manager->SetFloorFinish(RoomAt(Manager, 300., 200.), FSurfaceFinish()));
		FSurfaceFinish Out;
		TestTrue(TEXT("Cleared: bare"), Manager->GetFloorFinish(RoomAt(Manager, 300., 200.), Out) && !Out.IsSet());
		Manager->RebuildRooms();
		TestTrue(TEXT("Cleared, rebuilt: still bare"), Manager->GetFloorFinish(RoomAt(Manager, 300., 200.), Out) && !Out.IsSet());
		Manager->AddWallBetweenPoints(FVector2D(300., 0.), FVector2D(300., 400.));
		TestTrue(TEXT("Divided again: both parts bare"), Manager->GetFloorFinish(RoomAt(Manager, 150., 200.), Out) && !Out.IsSet()
			&& Manager->GetFloorFinish(RoomAt(Manager, 450., 200.), Out) && !Out.IsSet());
		Manager->Destroy();
	}

	// A partition moved a long way, one end at a time: each room keeps its own finish.
	{
		FScopedRoomTestWorld TestWorld;
		ARoomPlannerManager* Manager = TestWorld.World->SpawnActor<ARoomPlannerManager>();
		if (!TestNotNull(TEXT("Manager"), Manager)) return false;
		DrawRectangle(Manager, 600., 400.);
		Manager->AddWallBetweenPoints(FVector2D(300., 0.), FVector2D(300., 400.));
		TestTrue(TEXT("Both floors set"), Manager->SetFloorFinish(RoomAt(Manager, 150., 200.), Red) && Manager->SetFloorFinish(RoomAt(Manager, 450., 200.), Blue));
		const int32 Bottom = Manager->FindNodeAtWorldPos(FVector(300., 0., 0.), 1.f);
		const int32 Top = Manager->FindNodeAtWorldPos(FVector(300., 400., 0.), 1.f);
		if (!TestTrue(TEXT("Partition ends found"), Bottom != INDEX_NONE && Top != INDEX_NONE)) return false;
		TestTrue(TEXT("Bottom end moved to x 150"), Manager->MoveNode(Bottom, FVector2D(150., 0.)));
		TestTrue(TEXT("Slanted: left Red, right Blue"), FloorIs(Manager, 50., 300., Red) && FloorIs(Manager, 500., 200., Blue));
		TestTrue(TEXT("Top end moved to x 150"), Manager->MoveNode(Top, FVector2D(150., 400.)));
		TestTrue(TEXT("Moved: left (0..150) Red, right (150..600) Blue"), FloorIs(Manager, 75., 200., Red) && FloorIs(Manager, 400., 200., Blue));
		Manager->Destroy();
	}

	// A layout saved before rooms were divided (version 2): the one room's finish spreads to every room it now splits into.
	{
		FScopedRoomTestWorld TestWorld;
		ARoomPlannerManager* Manager = TestWorld.World->SpawnActor<ARoomPlannerManager>();
		if (!TestNotNull(TEXT("Manager"), Manager)) return false;
		DrawRectangle(Manager, 600., 400.);
		TestTrue(TEXT("Whole floor set"), Manager->SetFloorFinish(RoomAt(Manager, 300., 200.), Red));
		Manager->AddWallBetweenPoints(FVector2D(250., 0.), FVector2D(250., 400.));
		const FString Current = Manager->ExportLayoutToJSON();
		FString Legacy = Current;
		const int32 Replaced = Legacy.ReplaceInline(TEXT("\"version\": 3"), TEXT("\"version\": 2")) + Legacy.ReplaceInline(TEXT("\"version\":3"), TEXT("\"version\":2"));
		TestEqual(TEXT("Saved as version 3"), Replaced, 1);
		FSurfaceFinish Out;
		TestTrue(TEXT("Version 3 loaded"), Manager->ImportLayoutFromJSON(Current));
		TestTrue(TEXT("Version 3: only the part with the finish has it"), FloorIs(Manager, 400., 200., Red)
			&& Manager->GetFloorFinish(RoomAt(Manager, 100., 200.), Out) && !Out.IsSet());
		TestTrue(TEXT("Version 2 loaded"), Manager->ImportLayoutFromJSON(Legacy));
		TestEqual(TEXT("Version 2: two rooms"), RoomCount(Manager), 2);
		TestTrue(TEXT("Version 2: both parts have the room's finish"), FloorIs(Manager, 400., 200., Red) && FloorIs(Manager, 100., 200., Red));
		Manager->Destroy();
	}

	// The same wall committed twice (the host's own draw, then its server call): no second copy.
	{
		FScopedRoomTestWorld TestWorld;
		ARoomPlannerManager* Manager = TestWorld.World->SpawnActor<ARoomPlannerManager>();
		if (!TestNotNull(TEXT("Manager"), Manager)) return false;
		DrawRectangle(Manager, 600., 400.);
		Manager->AddWallBetweenPoints(FVector2D(300., -60.), FVector2D(300., 460.));
		const int32 AfterFirst = Manager->GetWallCount();
		Manager->AddWallBetweenPoints(FVector2D(300., -60.), FVector2D(300., 460.));
		TestEqual(TEXT("Drawn twice: no new walls"), Manager->GetWallCount(), AfterFirst);
		TestEqual(TEXT("Drawn twice: two rooms"), RoomCount(Manager), 2);

		Manager->StartInteractiveWallDraw(FVector(-60., 200., 0.));
		Manager->UpdateInteractiveWallDraw(FVector(660., 200., 0.));
		Manager->CommitInteractiveWallDraw();
		const int32 AfterDraw = Manager->GetWallCount();
		TestEqual(TEXT("Drawn across: four rooms"), RoomCount(Manager), 4);
		Manager->AddWallBetweenPoints(FVector2D(-60., 200.), FVector2D(660., 200.));
		TestEqual(TEXT("Server commit after the host's draw: no new walls"), Manager->GetWallCount(), AfterDraw);
		TestEqual(TEXT("Server commit after the host's draw: four rooms"), RoomCount(Manager), 4);
		Manager->Destroy();
	}

	// A wall drawn alongside another one's free end: the other wall is not bent onto it.
	{
		FScopedRoomTestWorld TestWorld;
		ARoomPlannerManager* Manager = TestWorld.World->SpawnActor<ARoomPlannerManager>();
		if (!TestNotNull(TEXT("Manager"), Manager)) return false;
		Manager->AddWallBetweenPoints(FVector2D(100., 25.), FVector2D(400., 25.));
		Manager->AddWallBetweenPoints(FVector2D(0., 0.), FVector2D(600., 0.));
		FVector2D P;
		const int32 NearEnd = Manager->FindNodeAtWorldPos(FVector(100., 25., 0.), 1.f);
		TestTrue(TEXT("Parallel wall: its end stays where it was"), NearEnd != INDEX_NONE && Manager->GetNodePosition(NearEnd, P) && P.Equals(FVector2D(100., 25.), 0.01));
		TestEqual(TEXT("Parallel wall: two walls, no joins"), Manager->GetWallCount(), 2);
		Manager->Destroy();
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
