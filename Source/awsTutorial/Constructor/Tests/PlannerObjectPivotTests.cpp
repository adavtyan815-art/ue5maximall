// Copyright 2026 MaxiMall. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Components/StaticMeshComponent.h"
#include "Constructor/PlannerPlacedObjectActor.h"
#include "Constructor/RoomPlannerManager.h"
#include "Dom/JsonObject.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

namespace
{
	struct FScopedPivotTestWorld
	{
		UWorld* World = nullptr;
		FScopedPivotTestWorld()
		{
			World = UWorld::CreateWorld(EWorldType::Game, false);
			if (World && GEngine)
			{
				GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
			}
			// As a loaded game map: AActor::ProcessEvent drops every call (dynamic delegates included) until the world's actors are initialized.
			if (World) World->InitializeActorsForPlay(FURL());
		}
		~FScopedPivotTestWorld()
		{
			if (World)
			{
				if (GEngine) GEngine->DestroyWorldContext(World);
				World->DestroyWorld(false);
				World->RemoveFromRoot();
			}
		}
	};

	/** An engine mesh with its pivot at its centre: a 1 m cube, bounds ±50 cm. */
	const TCHAR* PivotCubePath = TEXT("/Engine/BasicShapes/Cube.Cube");

	/** The catalog's Blazer sofa (rows NewRow / NewRow_0 / NewRow_1), authored with its pivot at its bottom centre. */
	const TCHAR* PivotSofaPath = TEXT("/Game/MedaFurniturePack/Furniture/Sofas/SM_Sofa-Blazer-283.SM_Sofa-Blazer-283");

	/** The south wall's room-side face (20 cm walls from y = 0): y = 10, normal +Y. */
	constexpr double PivotFaceY = 10.;

	/** The four outer walls of a 5 x 4 m room from the origin; returns the south wall. */
	int32 DrawPivotTestRoom(ARoomPlannerManager* Manager)
	{
		const int32 South = Manager->AddWallBetweenPoints(FVector2D(0., 0.), FVector2D(500., 0.));
		Manager->AddWallBetweenPoints(FVector2D(500., 0.), FVector2D(500., 400.));
		Manager->AddWallBetweenPoints(FVector2D(500., 400.), FVector2D(0., 400.));
		Manager->AddWallBetweenPoints(FVector2D(0., 400.), FVector2D(0., 0.));
		return South;
	}

	/** Where an object's mesh really is: its local bounds through the mesh component's world transform. */
	struct FPivotMeshPlacement
	{
		FVector Centre = FVector::ZeroVector; // the bounds' centre
		FBox World = FBox(ForceInit);         // world box of the bounds' eight corners (the footprint itself at yaw multiples of 90°)

		double Bottom() const { return World.Min.Z; }
		double Top() const { return World.Max.Z; }
		FVector2D Footprint() const { return FVector2D(Centre.X, Centre.Y); }
	};

	bool GetPivotMeshPlacement(const ARoomPlannerManager* Manager, const FString& ID, FPivotMeshPlacement& Out)
	{
		const APlannerPlacedObjectActor* Actor = Manager->FindPlacedObjectActor(ID);
		const UStaticMeshComponent* Comp = Actor ? Actor->MeshComponent.Get() : nullptr;
		if (!Comp || !Comp->GetStaticMesh()) return false;
		const FBox Local = Comp->GetStaticMesh()->GetBoundingBox();
		const FTransform& TM = Comp->GetComponentTransform();
		Out.Centre = TM.TransformPosition(Local.GetCenter());
		Out.World = FBox(ForceInit);
		for (int32 Corner = 0; Corner < 8; ++Corner)
		{
			Out.World += TM.TransformPosition(FVector((Corner & 1) ? Local.Max.X : Local.Min.X, (Corner & 2) ? Local.Max.Y : Local.Min.Y,
				(Corner & 4) ? Local.Max.Z : Local.Min.Z));
		}
		return true;
	}

	const FPlannerDimensionLine* FindPivotLine(const TArray<FPlannerDimensionLine>& Lines, const TCHAR* Key)
	{
		return Lines.FindByPredicate([Key](const FPlannerDimensionLine& L) { return L.Key == Key; });
	}

	const FPlannerDimensionLabel* FindPivotLabel(const TArray<FPlannerDimensionLabel>& Labels, const TCHAR* Key)
	{
		return Labels.FindByPredicate([Key](const FPlannerDimensionLabel& L) { return L.Key == Key; });
	}

	/**
	 * A mesh placed as the Datasmith scene meshes are: its 120 x 60 x 180 cm box 29 m from its pivot and 10 cm above it (LONG_PLANT is
	 * 29 m off and 10 cm up). Bounds only: every planner consumer (placement, picking, labels, gap lines, wall depth) reads the mesh's
	 * local bounds and never its triangles, and a mesh without render data just draws nothing.
	 */
	UStaticMesh* NewPivotOffsetMesh()
	{
		UPackage* Transient = GetTransientPackage();
		UStaticMesh* Mesh = NewObject<UStaticMesh>(Transient, MakeUniqueObjectName(Transient, UStaticMesh::StaticClass(), TEXT("PlannerPivotTestMesh")), RF_Transient);
		Mesh->SetExtendedBounds(FBoxSphereBounds(FBox(FVector(2900., -2850., 10.), FVector(3020., -2790., 190.))));
		return Mesh;
	}

	/**
	 * A wall object on the south wall is flush: its rearmost point on the face (y = 10), its bottom at the attachment height, centred
	 * at its distance along the wall, and the actor pushed off the face by the stored stand-off (half its depth: the pivot is now the
	 * footprint centre).
	 */
	void ExpectPivotWallFlush(FAutomationTestBase& Test, const ARoomPlannerManager* M, const FString& ID, double AlongX, double Bottom, double HalfDepth, const FString& What)
	{
		FPlacedFurnitureData D;
		FPivotMeshPlacement P;
		if (!Test.TestTrue(What + TEXT(": placed and shown"), M->GetPlacedObject(ID, D) && GetPivotMeshPlacement(M, ID, P))) return;
		Test.TestTrue(FString::Printf(TEXT("%s: back on the wall face (y = %.3f)"), *What, P.World.Min.Y), FMath::IsNearlyEqual(P.World.Min.Y, PivotFaceY, 0.05));
		Test.TestTrue(FString::Printf(TEXT("%s: bottom at %.0f cm (%.3f)"), *What, Bottom, P.Bottom()), FMath::IsNearlyEqual(P.Bottom(), Bottom, 0.05));
		Test.TestTrue(FString::Printf(TEXT("%s: centred at %.0f along the wall (%.3f)"), *What, AlongX, P.Centre.X), FMath::IsNearlyEqual(P.Centre.X, AlongX, 0.05));
		Test.TestTrue(FString::Printf(TEXT("%s: stand-off = half its depth (%.3f)"), *What, D.WallAttachment.DepthOffsetCm),
			FMath::IsNearlyEqual(D.WallAttachment.DepthOffsetCm, (float)HalfDepth, 0.05f) && FMath::IsNearlyEqual(D.Location.Y, PivotFaceY + HalfDepth, 0.05));
	}

	/** The layout's entry for object ID (objects[]), or null. */
	TSharedPtr<FJsonObject> FindPivotLayoutObject(const TSharedPtr<FJsonObject>& Root, const FString& ID)
	{
		const TArray<TSharedPtr<FJsonValue>>* Items = nullptr;
		if (!Root.IsValid() || !Root->TryGetArrayField(TEXT("objects"), Items) || !Items) return nullptr;
		for (const TSharedPtr<FJsonValue>& Val : *Items)
		{
			const TSharedPtr<FJsonObject> Obj = Val.IsValid() ? Val->AsObject() : TSharedPtr<FJsonObject>();
			if (Obj.IsValid() && Obj->GetStringField(TEXT("id")) == ID) return Obj;
		}
		return nullptr;
	}

	/** The wall stand-off a layout JSON carries for object ID (objects[id].wall.depth). */
	bool PivotLayoutDepth(const FString& Layout, const FString& ID, double& OutDepth)
	{
		TSharedPtr<FJsonObject> Root;
		if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Layout), Root)) return false;
		const TSharedPtr<FJsonObject> Obj = FindPivotLayoutObject(Root, ID);
		const TSharedPtr<FJsonObject>* Wall = nullptr;
		return Obj.IsValid() && Obj->TryGetObjectField(TEXT("wall"), Wall) && Wall && Wall->IsValid() && (*Wall)->TryGetNumberField(TEXT("depth"), OutDepth);
	}

	/** Layout with object ID's mesh (AssetID) replaced by NewAssetID, everything else (its stored stand-off and Location) as saved. */
	bool SwapPivotLayoutAsset(const FString& Layout, const FString& ID, const FString& NewAssetID, FString& OutLayout)
	{
		TSharedPtr<FJsonObject> Root;
		if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Layout), Root)) return false;
		const TSharedPtr<FJsonObject> Obj = FindPivotLayoutObject(Root, ID);
		if (!Obj.IsValid()) return false;
		Obj->SetStringField(TEXT("asset"), NewAssetID);
		OutLayout.Reset();
		return FJsonSerializer::Serialize(Root.ToSharedRef(), TJsonWriterFactory<>::Create(&OutLayout));
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// A centred pivot: the object stands on the floor, centred on the drop point
// ─────────────────────────────────────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlannerPivotCentredMeshTest, "MaxiMall.Planner.Objects.PivotCentredMesh",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlannerPivotCentredMeshTest::RunTest(const FString& Parameters)
{
	const UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, PivotCubePath);
	if (!TestNotNull(TEXT("Engine cube"), Cube)) return false;
	const FBox CubeBox = Cube->GetBoundingBox();
	if (!TestTrue(TEXT("The fixture: the cube's pivot is its centre (bounds ±50)"),
		CubeBox.Min.Equals(FVector(-50.), 0.01) && CubeBox.Max.Equals(FVector(50.), 0.01))) return false;

	FScopedPivotTestWorld TestWorld;
	if (!TestNotNull(TEXT("Test world"), TestWorld.World)) return false;
	ARoomPlannerManager* Manager = TestWorld.World->SpawnActor<ARoomPlannerManager>();
	if (!TestNotNull(TEXT("Manager"), Manager)) return false;
	DrawPivotTestRoom(Manager);
	Manager->SetViewMode(true);
	Manager->SetToolMode(EPlannerToolMode::Select);

	const FString Obj = Manager->AddPlacedObject(PivotCubePath, FVector(250., 200., 0.), FRotator::ZeroRotator, FVector::OneVector);
	if (!TestFalse(TEXT("Cube placed on the floor"), Obj.IsEmpty())) return false;
	const APlannerPlacedObjectActor* Actor = Manager->FindPlacedObjectActor(Obj);
	if (!TestNotNull(TEXT("Cube actor"), Actor) || !TestTrue(TEXT("… showing the cube"), Actor->MeshComponent->GetStaticMesh() == Cube)) return false;

	FPivotMeshPlacement P;
	if (!TestTrue(TEXT("Cube mesh placement"), GetPivotMeshPlacement(Manager, Obj, P))) return false;
	TestEqual(TEXT("The cube's bottom is on the floor (Z 0), not half below it"), P.Bottom(), 0., 0.01);
	TestEqual(TEXT("… its top 1 m up"), P.Top(), 100., 0.01);
	TestTrue(TEXT("… its footprint centred on the drop point"), P.Footprint().Equals(FVector2D(250., 200.), 0.01));

	// 2D picking works around Location, which is now the footprint centre: radius √(50² + 50²) ≈ 70.7.
	TestEqual(TEXT("A click inside the footprint picks it"), Manager->FindPlacedObjectAtWorldPos(FVector(290., 230., 0.)), Obj);
	TestTrue(TEXT("A click beyond its footprint circle does not"), Manager->FindPlacedObjectAtWorldPos(FVector(330., 250., 0.)).IsEmpty());

	// Selected: the outline is drawn by the (re-centred) mesh, the size label floats above its top, gap lines start at its sides.
	TestTrue(TEXT("Selection picks the cube"), Manager->SelectAtWorldPos2D(FVector(270., 190., 0.)) == EPlannerSelectionKind::Object && Manager->SelectedObjectID == Obj);
	TestTrue(TEXT("… and outlines its mesh"), Actor->MeshComponent->bRenderCustomDepth && Actor->MeshComponent->CustomDepthStencilValue == 2);
	const FPlannerDimensionLabel* Size = FindPivotLabel(Manager->GetSelectionDimensionLabels(), TEXT("size"));
	TestTrue(TEXT("Size label: 1 × 1 × 1 m, 10 cm above the top, over the centre"), Size && FMath::IsNearlyEqual(Size->Value, 100.f, 0.01f)
		&& Size->WorldLocation.Equals(FVector(250., 200., 110.), 0.01));
	const TArray<FPlannerDimensionLine> Lines = Manager->GetSelectionDimensionLines();
	const FPlannerDimensionLine* Front = FindPivotLine(Lines, TEXT("gapFront"));
	const FPlannerDimensionLine* Back = FindPivotLine(Lines, TEXT("gapBack"));
	TestTrue(TEXT("Gap lines from the cube's sides: front 300 → 490, back 200 → 10"), Front && Back
		&& FMath::IsNearlyEqual(Front->Start.X, 300., 0.05) && FMath::IsNearlyEqual(Front->End.X, 490., 0.05)
		&& FMath::IsNearlyEqual(Back->Start.X, 200., 0.05) && FMath::IsNearlyEqual(Back->End.X, 10., 0.05));

	// Scale lives on the actor (a row's DefaultScale, a Move with a scale): the offset is scaled with the mesh, the bottom stays down.
	FPlacedFurnitureData D;
	Manager->GetPlacedObject(Obj, D);
	TestTrue(TEXT("Scaled 2 x 1 x 0.5"), Manager->MovePlacedObject(Obj, D.Location, D.Rotation, FVector(2., 1., 0.5)));
	GetPivotMeshPlacement(Manager, Obj, P);
	TestEqual(TEXT("Scaled: bottom still on the floor"), P.Bottom(), 0., 0.01);
	TestEqual(TEXT("Scaled: top at 50 cm"), P.Top(), 50., 0.01);
	TestTrue(TEXT("Scaled: footprint still centred on its point"), P.Footprint().Equals(FVector2D(250., 200.), 0.01));
	TestTrue(TEXT("Scaled: half extents 100 x 50 x 25"), Actor->GetLocalHalfExtents().Equals(FVector(100., 50., 25.), 0.01));
	TestEqual(TEXT("Scaled: picking radius √(100² + 50²)"), Actor->GetFootprintRadiusCm(), FMath::Sqrt(100.f * 100.f + 50.f * 50.f), 0.01f);
	Size = FindPivotLabel(Manager->GetSelectionDimensionLabels(), TEXT("size"));
	TestTrue(TEXT("Scaled: size label 10 cm above the new top"), Size && Size->WorldLocation.Equals(FVector(250., 200., 60.), 0.01));

	// Rotation (buttons, wheel) turns it about its own centre: the footprint centre stays put, the bottom stays on the floor.
	FString ID;
	bool bSet = false;
	float Yaw = 0.f;
	for (int32 Notch = 0; Notch < 3; ++Notch) Manager->RotateSelectionLocal(15.f, ID, bSet, Yaw);
	Manager->GetPlacedObject(Obj, D);
	TestEqual(TEXT("Turned 45°"), D.Rotation.Yaw, 45., 0.01);
	GetPivotMeshPlacement(Manager, Obj, P);
	TestTrue(TEXT("Turned: the footprint centre did not move"), P.Footprint().Equals(FVector2D(250., 200.), 0.01));
	TestEqual(TEXT("Turned: bottom still on the floor"), P.Bottom(), 0., 0.01);

	// A colour only re-applies the data with the same mesh: the mesh stays where it is.
	const FVector Offset = Actor->MeshComponent->GetRelativeLocation();
	FSurfaceFinish Finish;
	Finish.Type = ESurfaceFinishType::Paint;
	Finish.ColorCode = TEXT("RAL 3020");
	Finish.Color = FLinearColor::Red;
	TestTrue(TEXT("Colour applied"), Manager->SetPlacedObjectFinish(Obj, Finish));
	TestTrue(TEXT("Coloured: the mesh offset is unchanged"), Actor->MeshComponent->GetRelativeLocation().Equals(Offset, 0.001));
	GetPivotMeshPlacement(Manager, Obj, P);
	TestTrue(TEXT("Coloured: still centred on its point, on the floor"), P.Footprint().Equals(FVector2D(250., 200.), 0.01) && FMath::IsNearlyEqual(P.Bottom(), 0., 0.01));

	Manager->Destroy();
	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// A pivot far from the object (Datasmith scene meshes): the same result
// ─────────────────────────────────────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlannerPivotOffsetMeshTest, "MaxiMall.Planner.Objects.PivotOffsetMesh",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlannerPivotOffsetMeshTest::RunTest(const FString& Parameters)
{
	const TStrongObjectPtr<UStaticMesh> Mesh(NewPivotOffsetMesh());
	const FString AssetID = Mesh->GetPathName(); // a mesh path is a valid AssetID, as a catalog row name is

	FScopedPivotTestWorld TestWorld;
	if (!TestNotNull(TEXT("Test world"), TestWorld.World)) return false;
	ARoomPlannerManager* Server = TestWorld.World->SpawnActor<ARoomPlannerManager>();
	ARoomPlannerManager* Client = TestWorld.World->SpawnActor<ARoomPlannerManager>();
	if (!TestNotNull(TEXT("Server"), Server) || !TestNotNull(TEXT("Client"), Client)) return false;
	DrawPivotTestRoom(Server);
	Server->SetViewMode(true);
	Server->SetToolMode(EPlannerToolMode::Select);

	const FString Obj = Server->AddPlacedObject(AssetID, FVector(250., 200., 0.), FRotator::ZeroRotator, FVector::OneVector);
	if (!TestFalse(TEXT("Offset-pivot mesh placed on the floor"), Obj.IsEmpty())) return false;
	const APlannerPlacedObjectActor* Actor = Server->FindPlacedObjectActor(Obj);
	if (!TestNotNull(TEXT("Object actor"), Actor) || !TestTrue(TEXT("… showing the offset-pivot mesh"), Actor->MeshComponent->GetStaticMesh() == Mesh.Get())) return false;

	FPivotMeshPlacement P;
	if (!TestTrue(TEXT("Mesh placement"), GetPivotMeshPlacement(Server, Obj, P))) return false;
	TestTrue(TEXT("Dropped: its footprint centre is the drop point, not 29 m away"), P.Footprint().Equals(FVector2D(250., 200.), 0.01));
	TestEqual(TEXT("… its bottom on the floor, not 10 cm above it"), P.Bottom(), 0., 0.01);
	TestEqual(TEXT("… its top 1.8 m up"), P.Top(), 180., 0.01);
	TestTrue(TEXT("… a 120 x 60 footprint around the drop point"),
		FVector2D(P.World.Min.X, P.World.Min.Y).Equals(FVector2D(190., 170.), 0.01) && FVector2D(P.World.Max.X, P.World.Max.Y).Equals(FVector2D(310., 230.), 0.01));
	TestTrue(TEXT("The mesh sits under the actor by minus its bounds' bottom centre"), Actor->MeshComponent->GetRelativeLocation().Equals(FVector(-2960., 2820., -10.), 0.01));
	TestTrue(TEXT("Half extents 60 x 30 x 90"), Actor->GetLocalHalfExtents().Equals(FVector(60., 30., 90.), 0.01));

	// A click on what the user sees selects it; labels and gap lines measure what the user sees.
	TestTrue(TEXT("A click inside the visible footprint selects it"), Server->SelectAtWorldPos2D(FVector(300., 220., 0.)) == EPlannerSelectionKind::Object && Server->SelectedObjectID == Obj);
	const FPlannerDimensionLabel* Size = FindPivotLabel(Server->GetSelectionDimensionLabels(), TEXT("size"));
	TestTrue(TEXT("Size label: 1.20 x 0.60 x 1.80 m, 10 cm above its top, over its centre"), Size && FMath::IsNearlyEqual(Size->Value, 120.f, 0.01f)
		&& Size->WorldLocation.Equals(FVector(250., 200., 190.), 0.01));
	const TArray<FPlannerDimensionLine> Lines = Server->GetSelectionDimensionLines();
	TestEqual(TEXT("In the middle of the room: four gaps"), Lines.Num(), 4);
	const FPlannerDimensionLine* Front = FindPivotLine(Lines, TEXT("gapFront"));
	const FPlannerDimensionLine* Back = FindPivotLine(Lines, TEXT("gapBack"));
	TestTrue(TEXT("Gap lines from its sides: front 310 → 490, back 190 → 10"), Front && Back
		&& FMath::IsNearlyEqual(Front->Start.X, 310., 0.05) && FMath::IsNearlyEqual(Front->End.X, 490., 0.05)
		&& FMath::IsNearlyEqual(Back->Start.X, 190., 0.05) && FMath::IsNearlyEqual(Back->End.X, 10., 0.05));

	// A drag (grab offset kept, as both drag paths do): the object follows the cursor by its footprint.
	FPlacedFurnitureData D;
	Server->GetPlacedObject(Obj, D);
	const FVector Grab(300., 220., 0.);
	const FVector GrabOffset = D.Location - Grab;
	Server->MovePlacedObjectLocal(Obj, FVector(380., 260., 0.) + GrabOffset, D.Rotation);
	GetPivotMeshPlacement(Server, Obj, P);
	TestTrue(TEXT("Dragged 80 x 40: the footprint centre moved with the cursor"), P.Footprint().Equals(FVector2D(330., 240.), 0.01));

	// Turned 90° (six wheel notches): about its own centre, which stays; the footprint is now 60 x 120.
	FString ID;
	bool bSet = false;
	float Yaw = 0.f;
	for (int32 Notch = 0; Notch < 6; ++Notch) Server->RotateSelectionLocal(15.f, ID, bSet, Yaw);
	GetPivotMeshPlacement(Server, Obj, P);
	TestTrue(TEXT("Turned 90°: the footprint centre stays"), P.Footprint().Equals(FVector2D(330., 240.), 0.01));
	TestTrue(TEXT("… the footprint turned with it (60 x 120)"), FMath::IsNearlyEqual(P.World.GetSize().X, 60., 0.01) && FMath::IsNearlyEqual(P.World.GetSize().Y, 120., 0.01));
	TestEqual(TEXT("… bottom still on the floor"), P.Bottom(), 0., 0.01);
	TestEqual(TEXT("A turned object is still picked at its centre"), Server->FindPlacedObjectAtWorldPos(FVector(330., 240., 0.)), Obj);

	// Committed (the drag release / the burst's single commit) at half scale: every machine rebuilds the same placement.
	Server->GetPlacedObject(Obj, D);
	TestTrue(TEXT("Committed at half scale"), Server->MovePlacedObject(Obj, D.Location, D.Rotation, FVector(0.5)));
	GetPivotMeshPlacement(Server, Obj, P);
	TestTrue(TEXT("Half scale: still centred on its point, on the floor, 90 cm tall"), P.Footprint().Equals(FVector2D(330., 240.), 0.01)
		&& FMath::IsNearlyEqual(P.Bottom(), 0., 0.01) && FMath::IsNearlyEqual(P.Top(), 90., 0.01));
	TestTrue(TEXT("Client imports the published layout"), Client->ImportLayoutFromJSON(Server->ReplicatedRoomJSON));
	FPivotMeshPlacement C;
	if (TestTrue(TEXT("Client object placement"), GetPivotMeshPlacement(Client, Obj, C)))
	{
		TestTrue(TEXT("Client: the same footprint centre and box as the server"), C.Centre.Equals(P.Centre, 0.01) && C.World.Min.Equals(P.World.Min, 0.01)
			&& C.World.Max.Equals(P.World.Max, 0.01));
	}

	Server->Destroy();
	Client->Destroy();
	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// On a wall: the back still touches the face, the bottom is at the drop height
// ─────────────────────────────────────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlannerPivotWallFlushTest, "MaxiMall.Planner.Objects.PivotWallFlush",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlannerPivotWallFlushTest::RunTest(const FString& Parameters)
{
	const TStrongObjectPtr<UStaticMesh> OffsetMesh(NewPivotOffsetMesh());
	FScopedPivotTestWorld TestWorld;
	if (!TestNotNull(TEXT("Test world"), TestWorld.World)) return false;
	ARoomPlannerManager* Manager = TestWorld.World->SpawnActor<ARoomPlannerManager>();
	ARoomPlannerManager* Client = TestWorld.World->SpawnActor<ARoomPlannerManager>();
	if (!TestNotNull(TEXT("Manager"), Manager) || !TestNotNull(TEXT("Client"), Client)) return false;
	const int32 South = DrawPivotTestRoom(Manager);
	Manager->SetViewMode(true);
	Manager->SetToolMode(EPlannerToolMode::Select);

	// Flush: rearmost point on the face (y = 10), bottom at the attachment height, centred at the distance along the wall, and the
	// actor pushed off the face by the stored offset (half the depth: the pivot is now the footprint centre).
	auto ExpectFlush = [this](const ARoomPlannerManager* M, const FString& ID, double AlongX, double Bottom, double HalfDepth, const FString& What)
	{
		ExpectPivotWallFlush(*this, M, ID, AlongX, Bottom, HalfDepth, What);
	};

	// The centred cube hung 1 m up: before, half of it was inside the wall and its centre (not its bottom) at 1 m.
	const FString Cube = Manager->AddPlacedObjectOnWall(PivotCubePath, South, 120.f, true, 100.f);
	if (!TestFalse(TEXT("Cube placed on the wall"), Cube.IsEmpty())) return false;
	ExpectFlush(Manager, Cube, 120., 100., 50., TEXT("Cube on the wall"));

	// Slid along its wall (a drag frame): it stays flush.
	FPlacedFurnitureData D;
	Manager->GetPlacedObject(Cube, D);
	Manager->MovePlacedObjectLocal(Cube, FVector(200., 150., 0.), D.Rotation);
	ExpectFlush(Manager, Cube, 200., 100., 50., TEXT("Cube slid along the wall"));
	Manager->GetPlacedObject(Cube, D);
	TestTrue(TEXT("Slide committed"), Manager->MovePlacedObject(Cube, D.Location, D.Rotation, D.Scale));

	// The 29 m offset mesh on the floor against the wall: its 120 cm depth (actor +X) now runs away from the wall.
	const FString Far = Manager->AddPlacedObjectOnWall(OffsetMesh->GetPathName(), South, 380.f, true, 0.f);
	if (!TestFalse(TEXT("Offset-pivot mesh placed on the wall"), Far.IsEmpty())) return false;
	ExpectFlush(Manager, Far, 380., 0., 60., TEXT("Offset-pivot mesh on the wall"));

	// Every machine rebuilds both from the published layout: still flush there.
	TestTrue(TEXT("Client imports the published layout"), Client->ImportLayoutFromJSON(Manager->ReplicatedRoomJSON));
	ExpectFlush(Client, Cube, 200., 100., 50., TEXT("Client: cube on the wall"));
	ExpectFlush(Client, Far, 380., 0., 60., TEXT("Client: offset-pivot mesh on the wall"));

	Manager->Destroy();
	Client->Destroy();
	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Another mesh (or scale) for a saved wall object: it is put back on the face
// ─────────────────────────────────────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlannerPivotWallMeshSwapTest, "MaxiMall.Planner.Objects.PivotWallMeshSwap",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlannerPivotWallMeshSwapTest::RunTest(const FString& Parameters)
{
	// 120 cm deep along its X (the wall normal once it hangs on a wall), where the cube is 100: 10 cm more behind its centre.
	const TStrongObjectPtr<UStaticMesh> DeepMesh(NewPivotOffsetMesh());
	FScopedPivotTestWorld TestWorld;
	if (!TestNotNull(TEXT("Test world"), TestWorld.World)) return false;
	ARoomPlannerManager* Manager = TestWorld.World->SpawnActor<ARoomPlannerManager>();
	ARoomPlannerManager* Client = TestWorld.World->SpawnActor<ARoomPlannerManager>();
	if (!TestNotNull(TEXT("Manager"), Manager) || !TestNotNull(TEXT("Client"), Client)) return false;
	const int32 South = DrawPivotTestRoom(Manager);
	Manager->SetViewMode(true);
	Manager->SetToolMode(EPlannerToolMode::Select);

	const FString Obj = Manager->AddPlacedObjectOnWall(PivotCubePath, South, 250.f, true, 0.f);
	if (!TestFalse(TEXT("Cube placed on the wall"), Obj.IsEmpty())) return false;
	ExpectPivotWallFlush(*this, Manager, Obj, 250., 0., 50., TEXT("Cube on the wall"));

	// Saved with the cube; loaded after the instance's mesh became the deeper one (its AssetID path edited, or its row's mesh swapped).
	// The layout still carries the cube's 50 cm stand-off and the Location it gave.
	FString Swapped;
	if (!TestTrue(TEXT("Layout exported, the instance's mesh swapped"), SwapPivotLayoutAsset(Manager->ExportLayoutToJSON(), Obj, DeepMesh->GetPathName(), Swapped))) return false;
	double Depth = 0.;
	TestTrue(TEXT("… it still says 50 cm off the face"), PivotLayoutDepth(Swapped, Obj, Depth) && FMath::IsNearlyEqual(Depth, 50., 0.01));

	// The authority loads it: the deeper mesh is not left 10 cm inside the wall; its back goes onto the face, and the corrected
	// stand-off is published for the clients.
	Manager->ReplicatedRoomJSON.Reset();
	TestTrue(TEXT("Authority imports the swapped layout"), Manager->ImportLayoutFromJSON(Swapped));
	const APlannerPlacedObjectActor* Actor = Manager->FindPlacedObjectActor(Obj);
	TestTrue(TEXT("… showing the deeper mesh"), Actor && Actor->MeshComponent->GetStaticMesh() == DeepMesh.Get());
	ExpectPivotWallFlush(*this, Manager, Obj, 250., 0., 60., TEXT("Authority, deeper mesh"));
	TestTrue(TEXT("… the 60 cm stand-off is published"), PivotLayoutDepth(Manager->ReplicatedRoomJSON, Obj, Depth) && FMath::IsNearlyEqual(Depth, 60., 0.01));

	// No loop: what it published is flush already, so importing that again corrects and publishes nothing.
	const FString Corrected = Manager->ReplicatedRoomJSON;
	Manager->ReplicatedRoomJSON.Reset();
	TestTrue(TEXT("Authority re-imports the corrected layout"), Manager->ImportLayoutFromJSON(Corrected));
	TestTrue(TEXT("… and publishes nothing"), Manager->ReplicatedRoomJSON.IsEmpty());
	ExpectPivotWallFlush(*this, Manager, Obj, 250., 0., 60., TEXT("Authority, corrected layout re-imported"));

	// A client that still gets the stale layout puts its own copy back on the face and never publishes (it has no say).
	Client->SetRole(ROLE_SimulatedProxy);
	Client->ReplicatedRoomJSON = Swapped;
	TestTrue(TEXT("Client imports the stale layout"), Client->ImportLayoutFromJSON(Swapped));
	ExpectPivotWallFlush(*this, Client, Obj, 250., 0., 60., TEXT("Client, stale layout"));
	TestTrue(TEXT("… and publishes nothing"), Client->ReplicatedRoomJSON == Swapped);
	TestTrue(TEXT("Client imports the corrected layout"), Client->ImportLayoutFromJSON(Corrected));
	ExpectPivotWallFlush(*this, Client, Obj, 250., 0., 60., TEXT("Client, corrected layout"));
	Client->SetRole(ROLE_Authority);

	// A new scale is an apply too: at half scale the stand-off halves and the back stays on the face.
	FPlacedFurnitureData D;
	Manager->GetPlacedObject(Obj, D);
	TestTrue(TEXT("Wall object scaled to half"), Manager->MovePlacedObject(Obj, D.Location, D.Rotation, FVector(0.5)));
	ExpectPivotWallFlush(*this, Manager, Obj, 250., 0., 30., TEXT("Half scale"));
	TestTrue(TEXT("… published at 30 cm"), PivotLayoutDepth(Manager->ReplicatedRoomJSON, Obj, Depth) && FMath::IsNearlyEqual(Depth, 30., 0.01));

	Manager->Destroy();
	Client->Destroy();
	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// A mesh authored bottom-centred (the catalog sofa) does not move
// ─────────────────────────────────────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlannerPivotAuthoredBottomCentreTest, "MaxiMall.Planner.Objects.PivotAuthoredBottomCentre",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlannerPivotAuthoredBottomCentreTest::RunTest(const FString& Parameters)
{
	const UStaticMesh* Sofa = LoadObject<UStaticMesh>(nullptr, PivotSofaPath, nullptr, LOAD_NoWarn | LOAD_Quiet);
	if (!Sofa)
	{
		AddInfo(FString::Printf(TEXT("%s is not in the project: the authored-pivot case is not checked."), PivotSofaPath));
		return true;
	}
	const FBox SofaBox = Sofa->GetBoundingBox();

	FScopedPivotTestWorld TestWorld;
	if (!TestNotNull(TEXT("Test world"), TestWorld.World)) return false;
	ARoomPlannerManager* Manager = TestWorld.World->SpawnActor<ARoomPlannerManager>();
	if (!TestNotNull(TEXT("Manager"), Manager)) return false;
	DrawPivotTestRoom(Manager);

	const FString Obj = Manager->AddPlacedObject(PivotSofaPath, FVector(250., 200., 0.), FRotator(0., 30., 0.), FVector::OneVector);
	const APlannerPlacedObjectActor* Actor = Manager->FindPlacedObjectActor(Obj);
	if (!TestFalse(TEXT("Sofa placed"), Obj.IsEmpty()) || !TestNotNull(TEXT("Sofa actor"), Actor)) return false;

	// Its bounds' bottom centre is within 0.01 cm of its pivot: it moves by that, visually identical to the unshifted placement.
	const FVector Offset = Actor->MeshComponent->GetRelativeLocation();
	TestTrue(FString::Printf(TEXT("The sofa moves by under half a millimetre (%.4f, %.4f, %.4f)"), Offset.X, Offset.Y, Offset.Z), Offset.Size() < 0.05);
	const FVector ExpectedOffset(-SofaBox.GetCenter().X, -SofaBox.GetCenter().Y, -SofaBox.Min.Z);
	TestTrue(TEXT("… exactly minus its bounds' bottom centre"), Offset.Equals(ExpectedOffset, 0.0001));
	FPivotMeshPlacement P;
	if (TestTrue(TEXT("Sofa mesh placement"), GetPivotMeshPlacement(Manager, Obj, P)))
	{
		TestTrue(TEXT("Sofa: footprint centred on its point, bottom on the floor"), P.Footprint().Equals(FVector2D(250., 200.), 0.01) && FMath::IsNearlyEqual(P.Bottom(), 0., 0.01));
		TestTrue(TEXT("Sofa: its size is the mesh's (289 x 113 x 66 cm)"), Actor->GetLocalHalfExtents().Equals(SofaBox.GetExtent(), 0.01));
	}

	Manager->Destroy();
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
