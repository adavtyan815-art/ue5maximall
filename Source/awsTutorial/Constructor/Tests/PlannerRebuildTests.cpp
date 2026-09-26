// Copyright 2026 MaxiMall. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Constructor/RoomPlannerManager.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "ProceduralMeshComponent.h"
#include "UObject/UnrealType.h"

namespace
{
	struct FScopedRebuildTestWorld
	{
		UWorld* World = nullptr;
		FScopedRebuildTestWorld()
		{
			World = UWorld::CreateWorld(EWorldType::Game, false);
			if (World && GEngine)
			{
				GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
			}
		}
		~FScopedRebuildTestWorld()
		{
			if (World)
			{
				if (GEngine) GEngine->DestroyWorldContext(World);
				World->DestroyWorld(false);
				World->RemoveFromRoot();
			}
		}
	};

	/** The four outer walls of a W x D room from the origin; returns the south wall (y = 0). */
	int32 DrawRoom(ARoomPlannerManager* Manager, double W, double D)
	{
		const int32 South = Manager->AddWallBetweenPoints(FVector2D(0., 0.), FVector2D(W, 0.));
		Manager->AddWallBetweenPoints(FVector2D(W, 0.), FVector2D(W, D));
		Manager->AddWallBetweenPoints(FVector2D(W, D), FVector2D(0., D));
		Manager->AddWallBetweenPoints(FVector2D(0., D), FVector2D(0., 0.));
		return South;
	}

	/** Distance along the wall of each opening, in array order. */
	TArray<float> OpeningDistances(const ARoomPlannerManager* Manager, int32 SegmentID)
	{
		TArray<float> Out;
		FWallSegment Segment;
		if (Manager->GetWallSegmentData(SegmentID, Segment))
		{
			for (const FWallOpening& Opening : Segment.Openings) Out.Add(Opening.DistanceFromStart);
		}
		return Out;
	}

	bool FloorSectionHasCollision(const ARoomPlannerManager* Manager)
	{
		const FProcMeshSection* Section = Manager->FloorProceduralMesh ? Manager->FloorProceduralMesh->GetProcMeshSection(0) : nullptr;
		return Section && Section->bEnableCollision;
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// A drag frame previews; the release publishes
// ─────────────────────────────────────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlannerOpeningDragPublishTest, "MaxiMall.Planner.Openings.DragPreviewDefersPublish",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlannerOpeningDragPublishTest::RunTest(const FString& Parameters)
{
	FScopedRebuildTestWorld TestWorld;
	ARoomPlannerManager* Manager = TestWorld.World->SpawnActor<ARoomPlannerManager>();
	if (!TestNotNull(TEXT("Manager"), Manager)) return false;
	const int32 Seg = DrawRoom(Manager, 800., 400.);
	if (!TestTrue(TEXT("Door added"), Manager->AddDoorToWall(Seg, 0.9f, 2.1f, 150.f))) return false;

	// A drag frame moves the door but keeps the layout to itself.
	Manager->ReplicatedRoomJSON.Reset();
	TestTrue(TEXT("Preview drag applied"), Manager->UpdateOpeningPosition(Seg, 0, 250.f, true));
	TestTrue(TEXT("A drag frame publishes nothing"), Manager->ReplicatedRoomJSON.IsEmpty());
	const TArray<float> Distances = OpeningDistances(Manager, Seg);
	if (TestEqual(TEXT("One opening"), Distances.Num(), 1))
	{
		TestEqual(TEXT("The door moved anyway"), Distances[0], 250.f, 1.f);
	}

	// The release repeats the distance the preview already applied: it must still publish.
	TestTrue(TEXT("Commit applied"), Manager->UpdateOpeningPosition(Seg, 0, 250.f));
	TestFalse(TEXT("The commit publishes the layout"), Manager->ReplicatedRoomJSON.IsEmpty());
	Manager->Destroy();
	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// An open door stays open when it is dragged, even past another opening
// ─────────────────────────────────────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlannerLeafStateFollowsOpeningTest, "MaxiMall.Planner.Openings.LeafStateFollowsOpening",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlannerLeafStateFollowsOpeningTest::RunTest(const FString& Parameters)
{
	FScopedRebuildTestWorld TestWorld;
	ARoomPlannerManager* Manager = TestWorld.World->SpawnActor<ARoomPlannerManager>();
	if (!TestNotNull(TEXT("Manager"), Manager)) return false;
	Manager->SetViewMode(false); // leaves exist in the 3D presentation only
	const int32 Seg = DrawRoom(Manager, 1000., 400.);
	if (!TestTrue(TEXT("Door A"), Manager->AddDoorToWall(Seg, 0.9f, 2.1f, 150.f))) return false;
	if (!TestTrue(TEXT("Door B"), Manager->AddDoorToWall(Seg, 0.9f, 2.1f, 500.f))) return false;

	Manager->ToggleOpeningLeaf(Seg, 0);
	TestEqual(TEXT("Door A is open"), Manager->GetLeafOpenTargetForDebug(Seg, 0), 1.f);
	TestEqual(TEXT("Door B is closed"), Manager->GetLeafOpenTargetForDebug(Seg, 1), 0.f);

	// Drag A past B. The openings are re-sorted by distance, so A is now the second entry.
	Manager->UpdateOpeningPosition(Seg, 0, 800.f);
	const TArray<float> Distances = OpeningDistances(Manager, Seg);
	if (!TestEqual(TEXT("Both doors are still there"), Distances.Num(), 2)) return false;
	if (!TestTrue(FString::Printf(TEXT("Door A passed door B (%.0f > %.0f)"), Distances[1], Distances[0]), Distances[1] > Distances[0])) return false;

	TestEqual(TEXT("The open state followed the door it belongs to"), Manager->GetLeafOpenTargetForDebug(Seg, 1), 1.f);
	TestEqual(TEXT("The door that was closed stays closed"), Manager->GetLeafOpenTargetForDebug(Seg, 0), 0.f);
	Manager->Destroy();
	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// A drag commit names the door that was dragged, on a machine that never saw the drag
// ─────────────────────────────────────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlannerOpeningCommitAddressesDraggedDoorTest, "MaxiMall.Planner.Openings.CommitAddressesDraggedDoor",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlannerOpeningCommitAddressesDraggedDoorTest::RunTest(const FString& Parameters)
{
	FScopedRebuildTestWorld TestWorld;
	// Two copies of one layout: the authority, and a client that has imported it. Only the client sees the drag frames.
	ARoomPlannerManager* Server = TestWorld.World->SpawnActor<ARoomPlannerManager>();
	ARoomPlannerManager* Client = TestWorld.World->SpawnActor<ARoomPlannerManager>();
	if (!TestNotNull(TEXT("Server"), Server) || !TestNotNull(TEXT("Client"), Client)) return false;

	const int32 Seg = DrawRoom(Server, 1000., 400.);
	if (!TestTrue(TEXT("Door A"), Server->AddDoorToWall(Seg, 0.9f, 2.1f, 150.f))) return false;
	if (!TestTrue(TEXT("Door B"), Server->AddDoorToWall(Seg, 0.9f, 2.1f, 500.f))) return false;
	if (!TestTrue(TEXT("Client has the layout"), Client->ImportLayoutFromJSON(Server->ExportLayoutToJSON()))) return false;

	const FString DoorA = Client->GetOpeningID(Seg, 0);
	const FString DoorB = Client->GetOpeningID(Seg, 1);
	if (!TestFalse(TEXT("Doors carry ids"), DoorA.IsEmpty() || DoorB.IsEmpty())) return false;

	// The client drags A past B. Its array is re-sorted; the server's is untouched, so an index would now name door B.
	Client->SelectedSegmentID = Seg;
	Client->SelectedOpeningIndex = 0;
	Client->UpdateOpeningPosition(Seg, 0, 800.f, true);
	const int32 DraggedIndexOnClient = Client->SelectedOpeningIndex;
	TestEqual(TEXT("The drag re-sorted the openings on the client"), DraggedIndexOnClient, 1);
	TestEqual(TEXT("...so that index names door B on the server"), Server->GetOpeningID(Seg, DraggedIndexOnClient), DoorB);

	// The commit travels as an id and is resolved on the server, which is what keeps door B where the user left it.
	const FString CommittedID = Client->GetOpeningID(Seg, DraggedIndexOnClient);
	TestEqual(TEXT("The commit names door A"), CommittedID, DoorA);
	const int32 IndexOnServer = Server->FindOpeningIndexByID(Seg, CommittedID);
	if (!TestTrue(TEXT("The server finds door A"), IndexOnServer != INDEX_NONE)) return false;
	Server->UpdateOpeningPosition(Seg, IndexOnServer, 800.f);

	float DistA = 0.f, DistB = 0.f;
	TestTrue(TEXT("Door A read back"), Server->GetOpeningDistance(Seg, Server->FindOpeningIndexByID(Seg, DoorA), DistA));
	TestTrue(TEXT("Door B read back"), Server->GetOpeningDistance(Seg, Server->FindOpeningIndexByID(Seg, DoorB), DistB));
	TestEqual(TEXT("Door A moved where it was dragged"), DistA, 800.f, 5.f);
	TestEqual(TEXT("Door B did not move"), DistB, 500.f, 0.5f);
	Server->Destroy();
	Client->Destroy();
	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// The selection, the wall ids and the spacing all survive the round trip
// ─────────────────────────────────────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlannerSelectionFollowsReimportTest, "MaxiMall.Planner.Openings.SelectionFollowsReimport",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlannerSelectionFollowsReimportTest::RunTest(const FString& Parameters)
{
	FScopedRebuildTestWorld TestWorld;
	ARoomPlannerManager* Server = TestWorld.World->SpawnActor<ARoomPlannerManager>();
	ARoomPlannerManager* Client = TestWorld.World->SpawnActor<ARoomPlannerManager>();
	if (!TestNotNull(TEXT("Server"), Server) || !TestNotNull(TEXT("Client"), Client)) return false;

	const int32 Seg = DrawRoom(Server, 1000., 400.);
	Server->AddDoorToWall(Seg, 0.9f, 2.1f, 150.f);
	Server->AddDoorToWall(Seg, 0.9f, 2.1f, 500.f);
	if (!TestTrue(TEXT("Client has the layout"), Client->ImportLayoutFromJSON(Server->ExportLayoutToJSON()))) return false;

	const FString DoorA = Client->GetOpeningID(Seg, 0); // the near door, at 150
	const FString DoorB = Client->GetOpeningID(Seg, 1); // the far door, at 500
	Client->SelectedSegmentID = Seg;
	Client->SelectedOpeningIndex = 0; // the user picked door A

	// Someone else moves door B in front of door A. The wall's openings are kept sorted, so the arriving layout has them the
	// other way round: index 0 now means door B, and a selection held as a number would quietly change which door it means.
	Server->UpdateOpeningPosition(Seg, Server->FindOpeningIndexByID(Seg, DoorB), 60.f);
	if (!TestTrue(TEXT("Client re-imports"), Client->ImportLayoutFromJSON(Server->ExportLayoutToJSON()))) return false;
	TestEqual(TEXT("Index 0 is now the other door"), Client->GetOpeningID(Seg, 0), DoorB);
	TestEqual(TEXT("The door the user picked is still the selected one"), Client->GetOpeningID(Seg, Client->SelectedOpeningIndex), DoorA);
	Server->Destroy();
	Client->Destroy();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlannerWallIdsSurviveImportTest, "MaxiMall.Planner.Rebuild.WallIdsSurviveImport",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlannerWallIdsSurviveImportTest::RunTest(const FString& Parameters)
{
	FScopedRebuildTestWorld TestWorld;
	ARoomPlannerManager* Server = TestWorld.World->SpawnActor<ARoomPlannerManager>();
	ARoomPlannerManager* Client = TestWorld.World->SpawnActor<ARoomPlannerManager>();
	if (!TestNotNull(TEXT("Server"), Server) || !TestNotNull(TEXT("Client"), Client)) return false;

	const int32 South = DrawRoom(Server, 600., 400.);
	Server->AddWallBetweenPoints(FVector2D(300., 0.), FVector2D(300., 400.));
	// A deleted wall leaves a gap in the numbering; renumbering on import is what made a client name another machine's wall.
	const int32 WallsBefore = Server->GetWallSegmentsForDebug().Num();
	Server->RemoveWall(South + 4);
	if (!TestEqual(TEXT("Partition removed"), Server->GetWallSegmentsForDebug().Num(), WallsBefore - 1)) return false;

	TArray<int32> ServerIDs;
	for (const auto& Pair : Server->GetWallSegmentsForDebug()) ServerIDs.Add(Pair.Key);
	ServerIDs.Sort();

	if (!TestTrue(TEXT("Client has the layout"), Client->ImportLayoutFromJSON(Server->ExportLayoutToJSON()))) return false;
	TArray<int32> ClientIDs;
	for (const auto& Pair : Client->GetWallSegmentsForDebug()) ClientIDs.Add(Pair.Key);
	ClientIDs.Sort();
	TestEqual(TEXT("Same wall ids on both machines"), ClientIDs, ServerIDs);

	FWallSegment ServerWall, ClientWall;
	if (ServerIDs.Num() > 0 && Server->GetWallSegmentData(ServerIDs[0], ServerWall) && Client->GetWallSegmentData(ServerIDs[0], ClientWall))
	{
		TestEqual(TEXT("And an id names the same wall"), ClientWall.WallGuid, ServerWall.WallGuid);
	}
	Server->Destroy();
	Client->Destroy();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlannerOpeningsKeepClearanceTest, "MaxiMall.Planner.Openings.DragKeepsClearance",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlannerOpeningsKeepClearanceTest::RunTest(const FString& Parameters)
{
	FScopedRebuildTestWorld TestWorld;
	ARoomPlannerManager* Manager = TestWorld.World->SpawnActor<ARoomPlannerManager>();
	if (!TestNotNull(TEXT("Manager"), Manager)) return false;
	const int32 Seg = DrawRoom(Manager, 800., 400.);
	Manager->AddDoorToWall(Seg, 0.9f, 2.1f, 150.f);
	Manager->AddDoorToWall(Seg, 0.9f, 2.1f, 400.f);
	Manager->AddDoorToWall(Seg, 0.9f, 2.1f, 650.f);

	// Drag the first door across the whole wall. Wherever it is let go, no two openings may overlap: one snap pass used to
	// push it clear of one neighbour and straight onto the next.
	for (float Target = 100.f; Target <= 700.f; Target += 37.f)
	{
		Manager->UpdateOpeningPosition(Seg, Manager->FindOpeningIndexByID(Seg, Manager->GetOpeningID(Seg, 0)), Target, true);
		FWallSegment Segment;
		if (!Manager->GetWallSegmentData(Seg, Segment)) continue;
		for (int32 i = 0; i < Segment.Openings.Num(); ++i)
		{
			for (int32 j = i + 1; j < Segment.Openings.Num(); ++j)
			{
				const float Gap = FMath::Abs(Segment.Openings[i].DistanceFromStart - Segment.Openings[j].DistanceFromStart);
				const float Needed = (Segment.Openings[i].Width + Segment.Openings[j].Width) * 0.5f;
				TestTrue(FString::Printf(TEXT("Target %.0f: openings %d and %d do not overlap (gap %.0f, needed %.0f)"), Target, i, j, Gap, Needed),
					Gap >= Needed - 0.5f);
			}
		}
	}
	Manager->Destroy();
	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Rebuild scope: no collision cooking while dragging, restored afterwards
// ─────────────────────────────────────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlannerDragCollisionTest, "MaxiMall.Planner.Rebuild.CollisionSkippedWhileDragging",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlannerDragCollisionTest::RunTest(const FString& Parameters)
{
	FScopedRebuildTestWorld TestWorld;
	ARoomPlannerManager* Manager = TestWorld.World->SpawnActor<ARoomPlannerManager>();
	if (!TestNotNull(TEXT("Manager"), Manager)) return false;
	Manager->SetViewMode(true);
	const int32 Seg = DrawRoom(Manager, 600., 400.);

	TestTrue(TEXT("Collision is cooked when nothing is being dragged"), Manager->ShouldCookCollision());
	TestTrue(TEXT("The floor starts with collision"), FloorSectionHasCollision(Manager));

	FWallSegment Segment;
	if (!TestTrue(TEXT("South wall"), Manager->GetWallSegmentData(Seg, Segment))) return false;
	const int32 NodeID = Segment.StartNodeID;
	if (!TestTrue(TEXT("Drag started"), Manager->StartNodeDrag(NodeID))) return false;
	TestFalse(TEXT("A drag frame skips the cook"), Manager->ShouldCookCollision());
	Manager->UpdateNodeDrag(FVector(40., 30., 0.));
	TestFalse(TEXT("The floor is rebuilt without collision"), FloorSectionHasCollision(Manager));

	int32 OutNode = -1;
	FVector2D OutPos = FVector2D::ZeroVector;
	Manager->EndNodeDrag(OutNode, OutPos);
	Manager->EnsureLayoutCollision(); // Tick does this in game
	TestTrue(TEXT("Collision is back after the drag"), FloorSectionHasCollision(Manager));
	Manager->Destroy();
	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Rooms without a finish share one cached material instead of minting one per rebuild
// ─────────────────────────────────────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlannerDefaultMaterialsSharedTest, "MaxiMall.Planner.Rebuild.DefaultMaterialsShared",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlannerDefaultMaterialsSharedTest::RunTest(const FString& Parameters)
{
	FScopedRebuildTestWorld TestWorld;
	ARoomPlannerManager* Manager = TestWorld.World->SpawnActor<ARoomPlannerManager>();
	if (!TestNotNull(TEXT("Manager"), Manager)) return false;
	DrawRoom(Manager, 600., 400.);
	Manager->AddWallBetweenPoints(FVector2D(300., 0.), FVector2D(300., 400.)); // two rooms, no finishes
	if (!TestEqual(TEXT("Two rooms"), Manager->GetRoomsForDebug().Num(), 2)) return false;

	UMaterialInterface* First = Manager->FloorProceduralMesh->GetMaterial(0);
	UMaterialInterface* Second = Manager->FloorProceduralMesh->GetMaterial(1);
	if (!TestNotNull(TEXT("Floor material"), First)) return false;
	TestTrue(TEXT("Both default floors share one instance"), First == Second);

	Manager->RebuildRooms();
	TestTrue(TEXT("A rebuild reuses it instead of creating another"), Manager->FloorProceduralMesh->GetMaterial(0) == First);
	Manager->Destroy();
	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// The replicated layout is condensed, and still round-trips
// ─────────────────────────────────────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlannerLayoutJsonCondensedTest, "MaxiMall.Planner.Rebuild.LayoutJsonIsCondensed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlannerLayoutJsonCondensedTest::RunTest(const FString& Parameters)
{
	FScopedRebuildTestWorld TestWorld;
	ARoomPlannerManager* Manager = TestWorld.World->SpawnActor<ARoomPlannerManager>();
	if (!TestNotNull(TEXT("Manager"), Manager)) return false;
	const int32 Seg = DrawRoom(Manager, 600., 400.);
	Manager->AddDoorToWall(Seg, 0.9f, 2.1f, 200.f);

	const FString Json = Manager->ExportLayoutToJSON();
	TestFalse(TEXT("No line breaks"), Json.Contains(TEXT("\n")));
	TestFalse(TEXT("No indentation"), Json.Contains(TEXT("\t")));
	TestFalse(TEXT("Coordinates are rounded, not 17 digits"), Json.Contains(TEXT("0000000000001")));

	const int32 RoomCount = Manager->GetRoomsForDebug().Num();
	TestTrue(TEXT("It imports again"), Manager->ImportLayoutFromJSON(Json));
	TestEqual(TEXT("Same rooms after the round trip"), Manager->GetRoomsForDebug().Num(), RoomCount);
	const TArray<float> Distances = OpeningDistances(Manager, Seg);
	if (TestEqual(TEXT("The door survived"), Distances.Num(), 1))
	{
		TestEqual(TEXT("At the same place"), Distances[0], 200.f, 0.5f);
	}
	Manager->Destroy();
	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// A node move leaves the same layout whether or not the passes were collapsed
// ─────────────────────────────────────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlannerNodeMoveIdempotentTest, "MaxiMall.Planner.Rebuild.NodeMoveIsIdempotent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlannerNodeMoveIdempotentTest::RunTest(const FString& Parameters)
{
	FScopedRebuildTestWorld TestWorld;
	ARoomPlannerManager* Manager = TestWorld.World->SpawnActor<ARoomPlannerManager>();
	if (!TestNotNull(TEXT("Manager"), Manager)) return false;
	const int32 Seg = DrawRoom(Manager, 600., 400.);
	Manager->AddDoorToWall(Seg, 0.9f, 2.1f, 200.f);

	FWallSegment Segment;
	if (!TestTrue(TEXT("South wall"), Manager->GetWallSegmentData(Seg, Segment))) return false;
	FVector2D Corner = FVector2D::ZeroVector;
	Manager->GetNodePosition(Segment.StartNodeID, Corner);
	TestTrue(TEXT("Corner moved"), Manager->MoveNode(Segment.StartNodeID, Corner + FVector2D(50., 30.)));

	// One collapsed rebuild must leave what a second, full rebuild would.
	const int32 RoomCount = Manager->GetRoomsForDebug().Num();
	TArray<float> AreasBefore;
	for (const auto& Pair : Manager->GetRoomsForDebug()) AreasBefore.Add(Pair.Value.AreaM2);
	AreasBefore.Sort();

	Manager->RebuildAllWalls();
	Manager->RebuildRooms();

	TestEqual(TEXT("Same room count"), Manager->GetRoomsForDebug().Num(), RoomCount);
	TArray<float> AreasAfter;
	for (const auto& Pair : Manager->GetRoomsForDebug()) AreasAfter.Add(Pair.Value.AreaM2);
	AreasAfter.Sort();
	if (TestEqual(TEXT("Same rooms"), AreasAfter.Num(), AreasBefore.Num()))
	{
		for (int32 i = 0; i < AreasAfter.Num(); ++i)
		{
			TestEqual(FString::Printf(TEXT("Room %d area unchanged"), i), AreasAfter[i], AreasBefore[i], 0.01f);
		}
	}
	Manager->Destroy();
	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// The collision repair keeps the selection, and the layout survives a re-export
// ─────────────────────────────────────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlannerCollisionRepairKeepsSelectionTest, "MaxiMall.Planner.Rebuild.CollisionRepairKeepsSelection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlannerCollisionRepairKeepsSelectionTest::RunTest(const FString& Parameters)
{
	FScopedRebuildTestWorld TestWorld;
	ARoomPlannerManager* Manager = TestWorld.World->SpawnActor<ARoomPlannerManager>();
	if (!TestNotNull(TEXT("Manager"), Manager)) return false;
	Manager->SetViewMode(true);
	const int32 Seg = DrawRoom(Manager, 600., 400.);
	Manager->SelectedSegmentID = Seg;

	FWallSegment Segment;
	if (!TestTrue(TEXT("South wall"), Manager->GetWallSegmentData(Seg, Segment))) return false;
	if (!TestTrue(TEXT("Drag started"), Manager->StartNodeDrag(Segment.StartNodeID))) return false;
	Manager->UpdateNodeDrag(FVector(30., 20., 0.));
	int32 OutNode = -1;
	FVector2D OutPos = FVector2D::ZeroVector;
	Manager->EndNodeDrag(OutNode, OutPos);

	Manager->EnsureLayoutCollision();
	TestEqual(TEXT("The wall is still selected after the repair"), Manager->SelectedSegmentID, Seg);
	TestTrue(TEXT("Collision is back"), FloorSectionHasCollision(Manager));

	// A layout that already cooks has nothing to repair: a second call must not rebuild anything.
	UMaterialInterface* FloorMaterial = Manager->FloorProceduralMesh->GetMaterial(0);
	Manager->EnsureLayoutCollision();
	TestTrue(TEXT("A cooked layout is left alone"), Manager->FloorProceduralMesh->GetMaterial(0) == FloorMaterial);
	Manager->Destroy();
	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Leaf state belongs to an opening and dies with it
// ─────────────────────────────────────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlannerLeafStateDroppedWithOpeningTest, "MaxiMall.Planner.Openings.LeafStateDroppedWithOpening",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlannerLeafStateDroppedWithOpeningTest::RunTest(const FString& Parameters)
{
	FScopedRebuildTestWorld TestWorld;
	ARoomPlannerManager* Manager = TestWorld.World->SpawnActor<ARoomPlannerManager>();
	if (!TestNotNull(TEXT("Manager"), Manager)) return false;
	Manager->SetViewMode(false);
	const int32 Seg = DrawRoom(Manager, 800., 400.);
	if (!TestTrue(TEXT("Door added"), Manager->AddDoorToWall(Seg, 0.9f, 2.1f, 200.f))) return false;

	Manager->ToggleOpeningLeaf(Seg, 0);
	TestEqual(TEXT("The door is open"), Manager->GetLeafOpenTargetForDebug(Seg, 0), 1.f);

	// Delete it and put an identical door back in the same place: it must start closed, not inherit the dead one's state.
	TestTrue(TEXT("Door deleted"), Manager->DeleteOpening(Seg, 0));
	TestTrue(TEXT("Door added again"), Manager->AddDoorToWall(Seg, 0.9f, 2.1f, 200.f));
	TestEqual(TEXT("The new door starts closed"), Manager->GetLeafOpenTargetForDebug(Seg, 0), 0.f);
	Manager->Destroy();
	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Rounding the layout is a fixed point: a round trip does not drift
// ─────────────────────────────────────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlannerLayoutRoundTripFixedPointTest, "MaxiMall.Planner.Rebuild.LayoutRoundTripIsFixedPoint",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlannerLayoutRoundTripFixedPointTest::RunTest(const FString& Parameters)
{
	FScopedRebuildTestWorld TestWorld;
	ARoomPlannerManager* Manager = TestWorld.World->SpawnActor<ARoomPlannerManager>();
	if (!TestNotNull(TEXT("Manager"), Manager)) return false;
	// Coordinates that do not land on the rounding grid on their own.
	Manager->AddWallBetweenPoints(FVector2D(0., 0.), FVector2D(633.333, 0.));
	Manager->AddWallBetweenPoints(FVector2D(633.333, 0.), FVector2D(633.333, 411.117));
	Manager->AddWallBetweenPoints(FVector2D(633.333, 411.117), FVector2D(0., 411.117));
	const int32 Seg = Manager->AddWallBetweenPoints(FVector2D(0., 411.117), FVector2D(0., 0.));
	Manager->AddDoorToWall(Seg, 0.913f, 2.107f, 137.777f);

	// The first export rounds the coordinates, so values derived from them (room areas) settle on the first import.
	// From there on the layout must be a fixed point: repeated round trips may not walk the geometry anywhere.
	const FString First = Manager->ExportLayoutToJSON();
	TestTrue(TEXT("Imports"), Manager->ImportLayoutFromJSON(First));
	const FString Second = Manager->ExportLayoutToJSON();
	TestTrue(TEXT("Imports again"), Manager->ImportLayoutFromJSON(Second));
	const FString Third = Manager->ExportLayoutToJSON();
	TestEqual(TEXT("Further round trips do not drift"), Third, Second);
	TestTrue(TEXT("Rounding moved nothing by more than a tenth of a millimetre"),
		FMath::Abs(FCString::Atod(*First.Mid(First.Find(TEXT("\"x\":633")) + 4, 10)) - 633.333) < 0.01);
	Manager->Destroy();
	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Actor containers stay visible to the garbage collector
// ─────────────────────────────────────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlannerActorContainersReflectedTest, "MaxiMall.Planner.Rebuild.ActorContainersReflected",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlannerActorContainersReflectedTest::RunTest(const FString& Parameters)
{
	// A container of spawned actors that is not a UPROPERTY hides those actors from the GC.
	for (const TCHAR* Name : { TEXT("WallActors"), TEXT("RoomLights"), TEXT("PlacedObjectActors") })
	{
		const FProperty* Property = ARoomPlannerManager::StaticClass()->FindPropertyByName(FName(Name));
		TestNotNull(FString::Printf(TEXT("%s is a UPROPERTY"), Name), Property);
	}
	return true;
}

#endif
