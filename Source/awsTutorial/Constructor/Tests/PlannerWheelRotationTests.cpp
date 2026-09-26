// Copyright 2026 MaxiMall. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "awsTutorial_PlayerController.h"
#include "Constructor/PlannerPlacedObjectActor.h"
#include "Constructor/RoomPlannerManager.h"
#include "Dom/JsonObject.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "FurnitureConfigurator/ShowroomBooth.h"
#include "InputKeyEventArgs.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
	struct FScopedWheelTestWorld
	{
		UWorld* World = nullptr;
		FScopedWheelTestWorld()
		{
			World = UWorld::CreateWorld(EWorldType::Game, false);
			if (World && GEngine)
			{
				GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
			}
			// As a loaded game map: AActor::ProcessEvent drops every call (RPCs and dynamic delegates included) until the world's
			// actors are initialized.
			if (World) World->InitializeActorsForPlay(FURL());
		}
		~FScopedWheelTestWorld()
		{
			if (World)
			{
				if (GEngine) GEngine->DestroyWorldContext(World);
				World->DestroyWorld(false);
				World->RemoveFromRoot();
			}
		}
	};

	/** The four outer walls of a W x D room from the origin; returns the south wall (y = 0, the room on its +Y side). */
	int32 DrawWheelTestRoom(ARoomPlannerManager* Manager, double W, double D)
	{
		const int32 South = Manager->AddWallBetweenPoints(FVector2D(0., 0.), FVector2D(W, 0.));
		Manager->AddWallBetweenPoints(FVector2D(W, 0.), FVector2D(W, D));
		Manager->AddWallBetweenPoints(FVector2D(W, D), FVector2D(0., D));
		Manager->AddWallBetweenPoints(FVector2D(0., D), FVector2D(0., 0.));
		return South;
	}

	/** An item's yaw as the layout publishes it to clients (ReplicatedRoomJSON → objects[id].yaw / cabinetSets[id].yaw). */
	bool WheelPublishedYaw(const ARoomPlannerManager* Manager, const TCHAR* ArrayName, const FString& ID, double& OutYaw)
	{
		TSharedPtr<FJsonObject> Root;
		if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Manager->ReplicatedRoomJSON), Root) || !Root.IsValid()) return false;
		const TArray<TSharedPtr<FJsonValue>>* Items = nullptr;
		if (!Root->TryGetArrayField(ArrayName, Items) || !Items) return false;
		for (const TSharedPtr<FJsonValue>& Val : *Items)
		{
			const TSharedPtr<FJsonObject> Obj = Val.IsValid() ? Val->AsObject() : TSharedPtr<FJsonObject>();
			if (Obj.IsValid() && Obj->GetStringField(TEXT("id")) == ID)
			{
				return Obj->TryGetNumberField(TEXT("yaw"), OutYaw);
			}
		}
		return false;
	}

	double WheelObjectYaw(const ARoomPlannerManager* Manager, const FString& ID)
	{
		FPlacedFurnitureData D;
		return Manager->GetPlacedObject(ID, D) ? D.Rotation.Yaw : TNumericLimits<double>::Max();
	}

	/** The one placed object that is not in Known (the placement just made), or empty. */
	FString WheelNewObjectID(const ARoomPlannerManager* Manager, const TSet<FString>& Known)
	{
		for (const FPlacedFurnitureData& Object : Manager->GetPlacedObjects())
		{
			if (!Known.Contains(Object.InstanceID)) return Object.InstanceID;
		}
		return FString();
	}

	TSet<FString> WheelObjectIDs(const ARoomPlannerManager* Manager)
	{
		TSet<FString> IDs;
		for (const FPlacedFurnitureData& Object : Manager->GetPlacedObjects()) IDs.Add(Object.InstanceID);
		return IDs;
	}

	const TCHAR* WheelRefusalLog = TEXT("Operation rejected: Объект закреплён на стене");
}

// ─────────────────────────────────────────────────────────────────────────────
// The shared rotate rule: 2D, local only, refused on a wall
// ─────────────────────────────────────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlannerWheelRotateSelectionTest, "MaxiMall.Planner.Objects.WheelRotateSelection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlannerWheelRotateSelectionTest::RunTest(const FString& Parameters)
{
	FScopedWheelTestWorld TestWorld;
	if (!TestNotNull(TEXT("Test world"), TestWorld.World)) return false;
	ARoomPlannerManager* Manager = TestWorld.World->SpawnActor<ARoomPlannerManager>();
	if (!TestNotNull(TEXT("Manager"), Manager)) return false;
	const TArray<FPlannerCatalogEntry> Objects = Manager->GetAvailableObjects();
	if (Objects.Num() == 0)
	{
		AddInfo(TEXT("DT_PlannerObjects has no rows: the rotate rule is not checked."));
		Manager->Destroy();
		return true;
	}
	const int32 South = DrawWheelTestRoom(Manager, 500., 400.);
	Manager->SetViewMode(true);
	Manager->SetToolMode(EPlannerToolMode::Select);

	FString ID;
	bool bSet = true;
	float Yaw = 0.f;
	TestFalse(TEXT("Nothing selected: nothing to turn"), Manager->RotateSelectionLocal(15.f, ID, bSet, Yaw));

	const FString Obj = Manager->AddPlacedObject(Objects[0].ID, FVector(250., 200., 0.), FRotator::ZeroRotator, FVector::OneVector);
	if (!TestFalse(TEXT("Floor object placed"), Obj.IsEmpty())) return false;
	Manager->SelectPlacedObject(Obj);

	// One notch: the data and the actor turn at once, and nothing is published (the commit comes once per burst).
	Manager->ReplicatedRoomJSON.Reset();
	TestTrue(TEXT("A floor object turns"), Manager->RotateSelectionLocal(-15.f, ID, bSet, Yaw));
	TestTrue(TEXT("… it reports the object and its new yaw"), ID == Obj && !bSet && FMath::IsNearlyEqual(Yaw, -15.f));
	TestEqual(TEXT("… the stored yaw"), WheelObjectYaw(Manager, Obj), -15., 0.01);
	const APlannerPlacedObjectActor* Actor = Manager->FindPlacedObjectActor(Obj);
	if (TestNotNull(TEXT("Object actor"), Actor))
	{
		TestEqual(TEXT("… the actor turned with it"), Actor->GetActorRotation().Yaw, -15., 0.01);
	}
	TestTrue(TEXT("A local turn publishes nothing"), Manager->ReplicatedRoomJSON.IsEmpty());

	// A drag frame re-reads the stored rotation: the turn survives the drag.
	FPlacedFurnitureData D;
	Manager->GetPlacedObject(Obj, D);
	Manager->MovePlacedObjectLocal(Obj, FVector(300., 220., 0.), D.Rotation);
	TestEqual(TEXT("A drag frame keeps the turn"), WheelObjectYaw(Manager, Obj), -15., 0.01);

	// The commit (the drag release or the burst's single RPC) publishes it.
	Manager->GetPlacedObject(Obj, D);
	TestTrue(TEXT("Commit applied"), Manager->MovePlacedObject(Obj, D.Location, D.Rotation, D.Scale));
	double Published = 0.;
	TestTrue(TEXT("The commit publishes yaw −15"), WheelPublishedYaw(Manager, TEXT("objects"), Obj, Published) && FMath::IsNearlyEqual(Published, -15., 0.01));

	// Additive and normalized, like the buttons: 175 + 15 is −170.
	Manager->MovePlacedObjectLocal(Obj, D.Location, FRotator(0., 175., 0.));
	Manager->RotateSelectionLocal(15.f, ID, bSet, Yaw);
	TestEqual(TEXT("175° + 15° wraps to −170°"), WheelObjectYaw(Manager, Obj), -170., 0.01);

	// On a wall: refused with the message, nothing turns (the wall decides its rotation).
	const FString WallObj = Manager->AddPlacedObjectOnWall(Objects[0].ID, South, 250.f, true, 100.f);
	if (TestFalse(TEXT("Wall object placed"), WallObj.IsEmpty()))
	{
		Manager->SelectPlacedObject(WallObj);
		const double Before = WheelObjectYaw(Manager, WallObj);
		AddExpectedMessagePlain(WheelRefusalLog, ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 0);
		TestFalse(TEXT("A wall object is refused"), Manager->RotateSelectionLocal(15.f, ID, bSet, Yaw));
		TestEqual(TEXT("… and keeps the wall's yaw"), WheelObjectYaw(Manager, WallObj), Before, 0.01);
	}

	// 3D never turns anything, even an object picked there for finishing.
	Manager->SetViewMode(false);
	Manager->SelectPlacedObject(Obj);
	const double Before3D = WheelObjectYaw(Manager, Obj);
	TestFalse(TEXT("3D: a picked object is not turned"), Manager->RotateSelectionLocal(15.f, ID, bSet, Yaw));
	TestEqual(TEXT("3D: yaw unchanged"), WheelObjectYaw(Manager, Obj), Before3D, 0.01);
	Manager->Destroy();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlannerWheelRotateCabinetSetTest, "MaxiMall.Planner.Objects.WheelRotateCabinetSet",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlannerWheelRotateCabinetSetTest::RunTest(const FString& Parameters)
{
	FScopedWheelTestWorld TestWorld;
	if (!TestNotNull(TEXT("Test world"), TestWorld.World)) return false;
	ARoomPlannerManager* Manager = TestWorld.World->SpawnActor<ARoomPlannerManager>();
	if (!TestNotNull(TEXT("Manager"), Manager)) return false;
	const TArray<FPlannerCatalogEntry> Sets = Manager->GetAvailableCabinetSets();
	if (Sets.Num() == 0)
	{
		AddInfo(TEXT("DT_FurnitureCatalog offers no cabinet sets: the cabinet-set rule is not checked."));
		Manager->Destroy();
		return true;
	}
	const int32 South = DrawWheelTestRoom(Manager, 500., 400.);
	Manager->SetViewMode(true);
	Manager->SetToolMode(EPlannerToolMode::Select);

	const FString SetID = Manager->AddCabinetSetOnWall(FName(*Sets[0].ID), South, 250.f, true);
	if (!TestFalse(TEXT("Cabinet set placed on the wall"), SetID.IsEmpty())) return false;
	Manager->SelectCabinetSet(SetID);
	FPlacedCabinetSetData Before;
	Manager->GetCabinetSet(SetID, Before);

	FString ID;
	bool bSet = false;
	float Yaw = 0.f;
	AddExpectedMessagePlain(WheelRefusalLog, ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 0);
	TestFalse(TEXT("A set on its wall is refused (as the buttons)"), Manager->RotateSelectionLocal(15.f, ID, bSet, Yaw));
	FPlacedCabinetSetData After;
	Manager->GetCabinetSet(SetID, After);
	TestEqual(TEXT("… and keeps the wall's yaw"), After.Rotation.Yaw, Before.Rotation.Yaw, 0.01);

	// Its wall removed: the set stays where it is, free — now it turns.
	Manager->RemoveWall(South);
	Manager->SelectCabinetSet(SetID);
	TestFalse(TEXT("Wall removed: the set is detached"), Manager->IsSelectionWallAttached());
	TestTrue(TEXT("A detached set turns"), Manager->RotateSelectionLocal(15.f, ID, bSet, Yaw));
	TestTrue(TEXT("… reported as a cabinet set"), ID == SetID && bSet);
	Manager->GetCabinetSet(SetID, After);
	TestEqual(TEXT("… by 15°"), After.Rotation.Yaw, FRotator::NormalizeAxis(Before.Rotation.Yaw + 15.), 0.01);
	if (const AShowroomBooth* Booth = Manager->FindCabinetSetActor(SetID))
	{
		TestEqual(TEXT("… and its booth turned with it"), Booth->GetActorRotation().Yaw, After.Rotation.Yaw, 0.01);
	}
	Manager->Destroy();
	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// The armed placement's yaw: kept while armed, gone whenever the placement is
// ─────────────────────────────────────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlannerPendingPlacementYawTest, "MaxiMall.Planner.Objects.PendingPlacementYaw",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlannerPendingPlacementYawTest::RunTest(const FString& Parameters)
{
	FScopedWheelTestWorld TestWorld;
	if (!TestNotNull(TEXT("Test world"), TestWorld.World)) return false;
	ARoomPlannerManager* Manager = TestWorld.World->SpawnActor<ARoomPlannerManager>();
	if (!TestNotNull(TEXT("Manager"), Manager)) return false;
	DrawWheelTestRoom(Manager, 500., 400.);
	Manager->SetViewMode(true);

	auto Arm = [Manager]()
	{
		Manager->BeginPlaceObject(TEXT("WheelTestObject"));
		Manager->PendingPlacementYawDeg = 30.f; // two notches of the wheel
	};
	Arm();
	TestEqual(TEXT("Armed: the yaw is kept"), Manager->PendingPlacementYawDeg, 30.f);
	Manager->SetToolMode(EPlannerToolMode::PlaceFurniture);
	TestEqual(TEXT("Re-asserting the tool keeps it"), Manager->PendingPlacementYawDeg, 30.f);
	Manager->BeginPlaceObject(TEXT("WheelTestObject"));
	TestEqual(TEXT("Armed again: back to 0"), Manager->PendingPlacementYawDeg, 0.f);
	Arm();
	Manager->CancelPendingPlacement();
	TestEqual(TEXT("Cancelled: 0"), Manager->PendingPlacementYawDeg, 0.f);
	Arm();
	Manager->SetToolMode(EPlannerToolMode::Select);
	TestEqual(TEXT("Another tool: 0"), Manager->PendingPlacementYawDeg, 0.f);
	Arm();
	Manager->BeginPlaceCabinetSet(FName(TEXT("WheelTestSet")));
	TestEqual(TEXT("A cabinet set armed instead: 0"), Manager->PendingPlacementYawDeg, 0.f);
	Arm();
	Manager->ClearLayout();
	TestEqual(TEXT("Plan cleared: 0"), Manager->PendingPlacementYawDeg, 0.f);
	Manager->Destroy();
	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// A committed turn reaches a client and stays through its re-imports
// ─────────────────────────────────────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlannerRotationSurvivesImportTest, "MaxiMall.Planner.Rebuild.RotationSurvivesImport",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlannerRotationSurvivesImportTest::RunTest(const FString& Parameters)
{
	FScopedWheelTestWorld TestWorld;
	if (!TestNotNull(TEXT("Test world"), TestWorld.World)) return false;
	ARoomPlannerManager* Server = TestWorld.World->SpawnActor<ARoomPlannerManager>();
	ARoomPlannerManager* Client = TestWorld.World->SpawnActor<ARoomPlannerManager>();
	if (!TestNotNull(TEXT("Server"), Server) || !TestNotNull(TEXT("Client"), Client)) return false;
	const TArray<FPlannerCatalogEntry> Objects = Server->GetAvailableObjects();
	if (Objects.Num() == 0)
	{
		AddInfo(TEXT("DT_PlannerObjects has no rows: the replicated yaw is not checked."));
		Server->Destroy();
		Client->Destroy();
		return true;
	}
	DrawWheelTestRoom(Server, 500., 400.);
	Server->SetViewMode(true);
	const FString Obj = Server->AddPlacedObject(Objects[0].ID, FVector(250., 200., 0.), FRotator::ZeroRotator, FVector::OneVector);
	if (!TestFalse(TEXT("Object placed"), Obj.IsEmpty())) return false;
	Server->SelectPlacedObject(Obj);

	// Three notches (a burst), then its single commit.
	FString ID;
	bool bSet = false;
	float Yaw = 0.f;
	for (int32 Notch = 0; Notch < 3; ++Notch) Server->RotateSelectionLocal(15.f, ID, bSet, Yaw);
	FPlacedFurnitureData D;
	Server->GetPlacedObject(Obj, D);
	Server->MovePlacedObject(Obj, D.Location, D.Rotation, D.Scale);

	TestTrue(TEXT("Client imports the published layout"), Client->ImportLayoutFromJSON(Server->ReplicatedRoomJSON));
	TestEqual(TEXT("Client: the stored yaw"), WheelObjectYaw(Client, Obj), 45., 0.01);
	const APlannerPlacedObjectActor* ClientActor = Client->FindPlacedObjectActor(Obj);
	if (TestNotNull(TEXT("Client object actor"), ClientActor))
	{
		TestEqual(TEXT("Client: the actor's yaw"), ClientActor->GetActorRotation().Yaw, 45., 0.01);
	}
	// Any later update (someone else's edit) re-imports the whole layout: the yaw stays.
	TestTrue(TEXT("Client re-imports its own copy"), Client->ImportLayoutFromJSON(Client->ExportLayoutToJSON()));
	TestEqual(TEXT("Client: the yaw survives a re-import"), WheelObjectYaw(Client, Obj), 45., 0.01);
	Server->Destroy();
	Client->Destroy();
	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// The controller's wheel: what it turns, what it leaves alone, one commit per burst
// ─────────────────────────────────────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlannerWheelInputTest, "MaxiMall.Planner.Objects.WheelInput",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlannerWheelInputTest::RunTest(const FString& Parameters)
{
	FScopedWheelTestWorld TestWorld;
	if (!TestNotNull(TEXT("Test world"), TestWorld.World)) return false;
	ARoomPlannerManager* Manager = TestWorld.World->SpawnActor<ARoomPlannerManager>();
	// Standalone and with authority: the controller's Server_ RPCs run here, as on a listen server.
	AAwsTutorial_PlayerController* PC = TestWorld.World->SpawnActor<AAwsTutorial_PlayerController>();
	if (!TestNotNull(TEXT("Manager"), Manager) || !TestNotNull(TEXT("Player controller"), PC)) return false;
	const TArray<FPlannerCatalogEntry> Objects = Manager->GetAvailableObjects();
	if (Objects.Num() == 0)
	{
		AddInfo(TEXT("DT_PlannerObjects has no rows: the controller's wheel is not checked."));
		PC->Destroy();
		Manager->Destroy();
		return true;
	}
	const int32 South = DrawWheelTestRoom(Manager, 500., 400.);
	Manager->bPlannerUIOpen = true;
	Manager->SetViewMode(true);
	Manager->SetToolMode(EPlannerToolMode::Select);
	const FString Obj = Manager->AddPlacedObject(Objects[0].ID, FVector(250., 200., 0.), FRotator::ZeroRotator, FVector::OneVector);
	if (!TestFalse(TEXT("Floor object placed"), Obj.IsEmpty())) return false;
	Manager->SelectPlacedObject(Obj);

	// One wheel notch as the game viewport sends it; true when the controller kept all of it.
	auto Notch = [PC](float Delta)
	{
		const FKey Scroll = Delta < 0.f ? EKeys::MouseScrollDown : EKeys::MouseScrollUp;
		bool bConsumed = PC->InputKey(FInputKeyEventArgs::CreateSimulated(Scroll, IE_Pressed, 1.f));
		bConsumed &= PC->InputKey(FInputKeyEventArgs::CreateSimulated(Scroll, IE_Released, 1.f));
		bConsumed &= PC->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::MouseWheelAxis, IE_Axis, Delta));
		return bConsumed;
	};
	float Pending = 0.f;
	double Published = 0.;

	// A burst: every notch turns at once, locally; nothing is published until its single commit.
	const FString LayoutBeforeBurst = Manager->ExportLayoutToJSON();
	Manager->ReplicatedRoomJSON.Reset();
	TestTrue(TEXT("Scroll up over the plan with a selection: consumed"), Notch(1.f));
	TestEqual(TEXT("… it turns the selection counter-clockwise (−15°)"), WheelObjectYaw(Manager, Obj), -15., 0.01);
	Notch(1.f);
	Notch(1.f);
	TestEqual(TEXT("Three notches: −45°"), WheelObjectYaw(Manager, Obj), -45., 0.01);
	TestTrue(TEXT("The notches publish nothing"), Manager->ReplicatedRoomJSON.IsEmpty());
	TestTrue(TEXT("The burst waits for its commit"), PC->GetPendingPlannerWheelYaw(Obj, Pending) && FMath::IsNearlyEqual(Pending, -45.f));

	// A replicated update in between resets the stored yaw: the next notch still turns on from the burst's −45.
	TestTrue(TEXT("A re-import mid-burst"), Manager->ImportLayoutFromJSON(LayoutBeforeBurst));
	Notch(-1.f);
	TestEqual(TEXT("Scroll down after the re-import: −45 + 15 = −30"), WheelObjectYaw(Manager, Obj), -30., 0.01);

	PC->FlushPlannerWheelCommit();
	TestTrue(TEXT("The burst's single commit publishes −30"), WheelPublishedYaw(Manager, TEXT("objects"), Obj, Published) && FMath::IsNearlyEqual(Published, -30., 0.01));
	TestFalse(TEXT("… and nothing waits any more"), PC->GetPendingPlannerWheelYaw(Obj, Pending));
	Manager->ReplicatedRoomJSON.Reset();
	PC->FlushPlannerWheelCommit();
	TestTrue(TEXT("A second flush sends nothing"), Manager->ReplicatedRoomJSON.IsEmpty());

	// Fractional deltas (precision wheels, touchpads, Pixel Streaming): 0.4 x 3 is one notch.
	Notch(0.4f);
	Notch(0.4f);
	TestEqual(TEXT("0.8 of a notch: no turn yet"), WheelObjectYaw(Manager, Obj), -30., 0.01);
	TestTrue(TEXT("… but the wheel is still consumed (no zoom meanwhile)"), Notch(0.4f));
	TestEqual(TEXT("1.2 notches: one step"), WheelObjectYaw(Manager, Obj), -45., 0.01);
	PC->FlushPlannerWheelCommit();

	// The step and the direction are the controller's properties.
	PC->PlannerWheelRotateStepDeg = 5.f;
	PC->bInvertPlannerWheelRotation = true;
	Notch(1.f);
	TestEqual(TEXT("Inverted, 5° step: scroll up turns +5°"), WheelObjectYaw(Manager, Obj), -40., 0.01);
	PC->PlannerWheelRotateStepDeg = 15.f;
	PC->bInvertPlannerWheelRotation = false;
	PC->FlushPlannerWheelCommit();

	// A drag (LMB held): drag frames keep a wheel turn across a re-import, and the release commit carries it.
	const FString LayoutBeforeDrag = Manager->ExportLayoutToJSON();
	Notch(1.f);
	const double DragYaw = WheelObjectYaw(Manager, Obj);
	TestEqual(TEXT("Turned during the drag"), DragYaw, -55., 0.01);
	Manager->ImportLayoutFromJSON(LayoutBeforeDrag); // a replicated update mid-drag
	FPlacedFurnitureData D;
	Manager->GetPlacedObject(Obj, D);
	Manager->MovePlacedObjectLocal(Obj, FVector(300., 220., 0.), PC->GetPlannerDragRotation(Obj, D.Rotation)); // a drag frame (both paths)
	TestEqual(TEXT("The drag frame keeps the wheel turn"), WheelObjectYaw(Manager, Obj), DragYaw, 0.01);
	Manager->ReplicatedRoomJSON.Reset();
	PC->PlannerCommitSelectedObjectTransform(); // the release
	TestTrue(TEXT("The release commit carries the turn"), WheelPublishedYaw(Manager, TEXT("objects"), Obj, Published) && FMath::IsNearlyEqual(Published, DragYaw, 0.01));
	TestFalse(TEXT("… no separate wheel commit is left"), PC->GetPendingPlannerWheelYaw(Obj, Pending));

	// Bursts belong to the item turned: a notch on another item first sends the previous item's burst with that item's yaw, and a
	// flush once the selection is gone (a click on empty floor, the switch to 3D) still sends the last burst by its own item.
	const FString ObjB = Manager->AddPlacedObject(Objects[0].ID, FVector(120., 300., 0.), FRotator(0., 90., 0.), FVector::OneVector);
	if (TestFalse(TEXT("Second floor object (B) placed"), ObjB.IsEmpty()))
	{
		Manager->SelectPlacedObject(Obj);
		Notch(1.f);
		const double YawA = WheelObjectYaw(Manager, Obj);
		TestEqual(TEXT("A turned once more (−15°)"), YawA, DragYaw - 15., 0.01);
		TestTrue(TEXT("… its burst waits"), PC->GetPendingPlannerWheelYaw(Obj, Pending) && FMath::IsNearlyEqual(Pending, (float)YawA, 0.01f));

		Manager->SelectPlacedObject(ObjB);
		Manager->ReplicatedRoomJSON.Reset();
		TestTrue(TEXT("B selected, scroll down: consumed"), Notch(-1.f));
		TestTrue(FString::Printf(TEXT("Notching B sent A's burst first, with A's yaw (%.1f)"), YawA),
			WheelPublishedYaw(Manager, TEXT("objects"), Obj, Published) && FMath::IsNearlyEqual(Published, YawA, 0.01));
		TestTrue(TEXT("… B's own turn is not in it (B still at 90°)"), WheelPublishedYaw(Manager, TEXT("objects"), ObjB, Published) && FMath::IsNearlyEqual(Published, 90., 0.01));
		TestFalse(TEXT("… A no longer waits"), PC->GetPendingPlannerWheelYaw(Obj, Pending));
		TestTrue(TEXT("B waits with its own yaw (90 + 15 = 105)"), PC->GetPendingPlannerWheelYaw(ObjB, Pending) && FMath::IsNearlyEqual(Pending, 105.f, 0.01f));
		TestEqual(TEXT("B turned locally"), WheelObjectYaw(Manager, ObjB), 105., 0.01);
		TestEqual(TEXT("A kept its yaw"), WheelObjectYaw(Manager, Obj), YawA, 0.01);

		// The selection is gone before the idle commit: the flush sends B's burst (not "the selection", which is empty now).
		Manager->ClearAllSelection();
		Manager->ReplicatedRoomJSON.Reset();
		PC->FlushPlannerWheelCommit();
		TestTrue(TEXT("Selection cleared, then the flush: B's burst is published (105°)"),
			WheelPublishedYaw(Manager, TEXT("objects"), ObjB, Published) && FMath::IsNearlyEqual(Published, 105., 0.01));
		TestTrue(TEXT("… A is published unchanged"), WheelPublishedYaw(Manager, TEXT("objects"), Obj, Published) && FMath::IsNearlyEqual(Published, YawA, 0.01));
		TestEqual(TEXT("… and A's own yaw untouched"), WheelObjectYaw(Manager, Obj), YawA, 0.01);
		TestFalse(TEXT("… nothing waits any more"), PC->GetPendingPlannerWheelYaw(ObjB, Pending) || PC->GetPendingPlannerWheelYaw(Obj, Pending));
		Manager->SelectPlacedObject(Obj);
	}

	// Over the planner's UI, or with the planner hidden behind a full-screen catalog: not consumed, nothing turns.
	const double YawBefore = WheelObjectYaw(Manager, Obj);
	Manager->SetPlannerUIHitTest([]() { return true; });
	TestFalse(TEXT("Over the planner UI: not consumed"), Notch(1.f));
	Manager->ClearPlannerUIHitTest();
	Manager->SetPlannerUIShownQuery([]() { return false; });
	TestFalse(TEXT("Planner hidden by a full-screen catalog: not consumed"), Notch(1.f));
	Manager->ClearPlannerUIShownQuery();
	TestEqual(TEXT("… and nothing turned"), WheelObjectYaw(Manager, Obj), YawBefore, 0.01);

	// A wall object: consumed (no zoom leak), refused with the message, nothing waits for a commit.
	const FString WallObj = Manager->AddPlacedObjectOnWall(Objects[0].ID, South, 120.f, true, 100.f);
	if (TestFalse(TEXT("Wall object placed"), WallObj.IsEmpty()))
	{
		Manager->SelectPlacedObject(WallObj);
		const double WallYaw = WheelObjectYaw(Manager, WallObj);
		AddExpectedMessagePlain(WheelRefusalLog, ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 0);
		TestTrue(TEXT("Wall object: the wheel is consumed"), Notch(1.f));
		TestEqual(TEXT("… but it does not turn"), WheelObjectYaw(Manager, WallObj), WallYaw, 0.01);
		TestFalse(TEXT("… and no commit waits"), PC->GetPendingPlannerWheelYaw(WallObj, Pending));
	}

	// Nothing to turn: the wheel behaves as before.
	Manager->ClearAllSelection();
	TestFalse(TEXT("Nothing selected: not consumed"), Notch(1.f));

	// Click-to-place: the armed object turns (nothing exists yet, nothing is sent); the click places it with that yaw.
	Manager->BeginPlaceObject(Objects[0].ID);
	TestTrue(TEXT("Armed object: consumed"), Notch(-1.f));
	Notch(-1.f);
	TestEqual(TEXT("Two notches down: +30°"), Manager->PendingPlacementYawDeg, 30.f);
	TSet<FString> Known = WheelObjectIDs(Manager);
	TestTrue(TEXT("The click places it"), PC->PlannerPlacePendingAt(FVector(150., 150., 0.))); // it reads the armed yaw itself
	const FString Placed = WheelNewObjectID(Manager, Known);
	TestTrue(TEXT("… on the floor, turned 30°"), !Placed.IsEmpty() && FMath::IsNearlyEqual(WheelObjectYaw(Manager, Placed), 30., 0.01));
	TestEqual(TEXT("… and the placement (with its yaw) is over"), Manager->PendingPlacementYawDeg, 0.f);

	// A cabinet set armed (wall-only): nothing to turn.
	Manager->BeginPlaceCabinetSet(FName(TEXT("WheelTestSet")));
	TestFalse(TEXT("Armed cabinet set: not consumed"), Notch(1.f));
	Manager->CancelPendingPlacement();
	Manager->SetToolMode(EPlannerToolMode::Select);

	// A catalog drop: the yaw goes to a floor placement; a wall placement follows the wall.
	const FPlannerDropInfo FloorDrop = Manager->ResolveDropAtWorldPos2D(FVector(350., 250., 0.));
	if (TestTrue(TEXT("Floor drop target"), FloorDrop.Target == EPlannerDropTarget::Floor))
	{
		Known = WheelObjectIDs(Manager);
		TestTrue(TEXT("Floor drop sent"), PC->PlannerPlaceResolved(EPlannerPlacementKind::Object, Objects[0].ID, FloorDrop, -45.f));
		const FString Dropped = WheelNewObjectID(Manager, Known);
		TestTrue(TEXT("Floor drop: placed with the drag's yaw (−45°)"), !Dropped.IsEmpty() && FMath::IsNearlyEqual(WheelObjectYaw(Manager, Dropped), -45., 0.01));
	}
	const FPlannerDropInfo WallDrop = Manager->ResolveDropAtWorldPos2D(FVector(380., 3., 0.));
	if (TestTrue(TEXT("Wall drop target"), WallDrop.Target == EPlannerDropTarget::Wall))
	{
		const FString Reference = Manager->AddPlacedObjectOnWall(Objects[0].ID, WallDrop.SegmentID, 300.f, WallDrop.bLeftSide, 0.f);
		Known = WheelObjectIDs(Manager);
		TestTrue(TEXT("Wall drop sent"), PC->PlannerPlaceResolved(EPlannerPlacementKind::Object, Objects[0].ID, WallDrop, 37.f));
		const FString Dropped = WheelNewObjectID(Manager, Known);
		TestTrue(TEXT("Wall drop: the wall's yaw, not the drag's"), !Dropped.IsEmpty() && !Reference.IsEmpty()
			&& FMath::IsNearlyEqual(WheelObjectYaw(Manager, Dropped), WheelObjectYaw(Manager, Reference), 0.01) && !FMath::IsNearlyEqual(WheelObjectYaw(Manager, Dropped), 37., 0.01));
	}

	// 3D: the wheel keeps its camera behaviour — never consumed, never turns, even an object picked there.
	Manager->SetViewMode(false);
	Manager->SelectPlacedObject(Obj);
	const double Yaw3D = WheelObjectYaw(Manager, Obj);
	TestFalse(TEXT("3D: not consumed"), Notch(1.f));
	TestEqual(TEXT("3D: nothing turned"), WheelObjectYaw(Manager, Obj), Yaw3D, 0.01);

	PC->Destroy();
	Manager->Destroy();
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
