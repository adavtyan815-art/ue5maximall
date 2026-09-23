// Copyright 2026 MaxiMall. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Components/StaticMeshComponent.h"
#include "Constructor/RoomPlannerManager.h"
#include "Dom/JsonObject.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "FurnitureConfigurator/ShowroomBooth.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
	struct FScopedCabinetSetTestWorld
	{
		UWorld* World = nullptr;
		FScopedCabinetSetTestWorld()
		{
			World = UWorld::CreateWorld(EWorldType::Game, false);
			if (World && GEngine)
			{
				GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
			}
			// As a loaded game map: AActor::ProcessEvent drops every call, dynamic delegates included (the booth's
			// OnProductChanged reaches the manager through one), until the world's actors are initialized.
			if (World) World->InitializeActorsForPlay(FURL());
		}
		~FScopedCabinetSetTestWorld()
		{
			if (World)
			{
				if (GEngine) GEngine->DestroyWorldContext(World);
				World->DestroyWorld(false);
				World->RemoveFromRoot();
			}
		}
	};

	/** Rearmost visible point of a booth (or of one of its parts) toward -Y: each mesh's local box through its own transform. */
	double RearY(const AShowroomBooth* Booth, const UStaticMeshComponent* OnlyPart = nullptr)
	{
		double Rear = TNumericLimits<double>::Max();
		TArray<UStaticMeshComponent*> MeshComps;
		Booth->GetComponents(MeshComps);
		for (const UStaticMeshComponent* Comp : MeshComps)
		{
			if (!Comp || (OnlyPart && Comp != OnlyPart) || !Comp->GetStaticMesh() || !Comp->IsVisible()) continue;
			const FBox MeshBox = Comp->GetStaticMesh()->GetBoundingBox();
			for (int32 Corner = 0; Corner < 8; ++Corner)
			{
				Rear = FMath::Min(Rear, Comp->GetComponentTransform().TransformPosition(FVector((Corner & 1) ? MeshBox.Max.X : MeshBox.Min.X,
					(Corner & 2) ? MeshBox.Max.Y : MeshBox.Min.Y, (Corner & 4) ? MeshBox.Max.Z : MeshBox.Min.Z)).Y);
			}
		}
		return Rear;
	}

	/** A cabinet set's wall stand-off as the layout publishes it to clients (ReplicatedRoomJSON → cabinetSets[id].wall.depth). */
	bool PublishedDepth(const ARoomPlannerManager* Manager, const FString& SetID, double& OutDepth)
	{
		TSharedPtr<FJsonObject> Root;
		if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Manager->ReplicatedRoomJSON), Root) || !Root.IsValid()) return false;
		const TArray<TSharedPtr<FJsonValue>>* Sets = nullptr;
		if (!Root->TryGetArrayField(TEXT("cabinetSets"), Sets) || !Sets) return false;
		for (const TSharedPtr<FJsonValue>& Val : *Sets)
		{
			const TSharedPtr<FJsonObject> Obj = Val.IsValid() ? Val->AsObject() : TSharedPtr<FJsonObject>();
			const TSharedPtr<FJsonObject>* Wall = nullptr;
			if (Obj.IsValid() && Obj->GetStringField(TEXT("id")) == SetID && Obj->TryGetObjectField(TEXT("wall"), Wall) && Wall && Wall->IsValid())
			{
				return (*Wall)->TryGetNumberField(TEXT("depth"), OutDepth);
			}
		}
		return false;
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// A wall-attached cabinet set stays flush with its wall when it is reconfigured
// ─────────────────────────────────────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlannerCabinetSetReconfigureTest, "MaxiMall.Planner.Objects.CabinetSetReconfigure",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlannerCabinetSetReconfigureTest::RunTest(const FString& Parameters)
{
	FScopedCabinetSetTestWorld TestWorld;
	if (!TestNotNull(TEXT("Test world"), TestWorld.World)) return false;
	ARoomPlannerManager* Manager = TestWorld.World->SpawnActor<ARoomPlannerManager>();
	if (!TestNotNull(TEXT("Manager"), Manager)) return false;
	// The 5 x 4 m room with 20 cm walls: the south wall's room-side face is y = 10, its normal +Y.
	const int32 N1 = Manager->AddNode(FVector2D(0., 0.));
	const int32 N2 = Manager->AddNode(FVector2D(500., 0.));
	const int32 N3 = Manager->AddNode(FVector2D(500., 400.));
	const int32 N4 = Manager->AddNode(FVector2D(0., 400.));
	const int32 South = Manager->AddWall(N1, N2);
	Manager->AddWall(N2, N3);
	Manager->AddWall(N3, N4);
	Manager->AddWall(N4, N1);
	Manager->RebuildRooms();
	const double FaceY = 10.;

	const TArray<FPlannerCatalogEntry> Sets = Manager->GetAvailableCabinetSets();
	if (Sets.Num() == 0)
	{
		AddError(TEXT("DT_FurnitureCatalog offers no cabinet sets: reconfiguring one cannot be checked."));
		Manager->Destroy();
		return false;
	}

	const FString SetID = Manager->AddCabinetSetOnWall(FName(*Sets[0].ID), South, 250.f, true);
	AShowroomBooth* Booth = Manager->FindCabinetSetActor(SetID);
	if (!TestFalse(TEXT("Cabinet set placed"), SetID.IsEmpty()) || !TestNotNull(TEXT("Cabinet set booth"), Booth)) return false;
	// The game dresses a booth (its meshes) in BeginPlay, during the spawn; this bare test world never begins play. Dressed now,
	// with no configuration event, the offset measured at spawn may no longer match its meshes.
	if (!Booth->HasActorBegunPlay()) Booth->DispatchBeginPlay();
	if (!TestTrue(TEXT("Cabinet set has visible parts"), RearY(Booth) < TNumericLimits<double>::Max())) return false;

	// Flush: rearmost point on the face, booth where the layout puts it, and the layout published with the measured offset.
	auto ExpectFlush = [&](const FString& ID, const AShowroomBooth* SetBooth, const FString& What)
	{
		FPlacedCabinetSetData Data;
		if (!TestTrue(What + TEXT(": set in the layout"), Manager->GetCabinetSet(ID, Data))) return;
		const double Rear = RearY(SetBooth);
		TestTrue(FString::Printf(TEXT("%s: rearmost point on the wall face (y = %.2f)"), *What, Rear), FMath::IsNearlyEqual(Rear, FaceY, 0.1));
		TestTrue(What + TEXT(": booth where the layout puts it"),
			SetBooth->GetActorLocation().Equals(Data.Location, 0.1) && SetBooth->GetActorRotation().Equals(Data.Rotation, 0.1f));
		TestTrue(What + TEXT(": pivot pushed off the face by the stored offset"),
			FMath::IsNearlyEqual(Data.Location.Y, FaceY + Data.WallAttachment.DepthOffsetCm, 0.1));
		double Published = 0.;
		TestTrue(What + TEXT(": clients get the same offset"),
			PublishedDepth(Manager, ID, Published) && FMath::IsNearlyEqual(Published, (double)Data.WallAttachment.DepthOffsetCm, 0.01));
	};

	// 1. The configurator's path: Server_ApplyComponentSelection → ApplyProductData → OnProductChanged.
	Booth->RequestComponentSelection(EFurnitureComponentType::Cabinet, Booth->ActiveState.ActiveSizeIndex, Booth->ActiveState.ActiveColorIndex);
	ExpectFlush(SetID, Booth, TEXT("Cabinet re-selected"));

	// 2. Every catalog set, through every size / model it offers for the parts that decide how far a set reaches back.
	{
		struct FPartAxis { EFurnitureComponentType Type; const TCHAR* Name; };
		const FPartAxis Axes[] = {
			{ EFurnitureComponentType::Cabinet, TEXT("cabinet size") },
			{ EFurnitureComponentType::Countertop, TEXT("countertop model") },
			{ EFurnitureComponentType::Closet, TEXT("closet model") },
			{ EFurnitureComponentType::Mirror, TEXT("mirror model") },
		};
		int32 Checked = 0;
		TArray<FString> RearMoved;
		for (const FPlannerCatalogEntry& Entry : Sets)
		{
			const FString ID = Manager->AddCabinetSetOnWall(FName(*Entry.ID), South, 250.f, true);
			AShowroomBooth* SetBooth = Manager->FindCabinetSetActor(ID);
			if (!TestNotNull(*FString::Printf(TEXT("%s: booth"), *Entry.ID), SetBooth)) continue;
			if (!SetBooth->HasActorBegunPlay()) SetBooth->DispatchBeginPlay();
			FFurnitureProductRow Product;
			if (RearY(SetBooth) == TNumericLimits<double>::Max() || !SetBooth->GetActiveProductData(Product))
			{
				AddInfo(FString::Printf(TEXT("%s: no visible parts or product row, skipped."), *Entry.ID));
				Manager->RemoveCabinetSet(ID);
				continue;
			}
			// Settle the offset measured before the booth was dressed, so only the options below count as moves.
			SetBooth->RequestComponentSelection(EFurnitureComponentType::Cabinet, SetBooth->ActiveState.ActiveSizeIndex, SetBooth->ActiveState.ActiveColorIndex);
			int32 Moves = 0;
			for (const FPartAxis& Axis : Axes)
			{
				int32 Count = 0;
				if (Axis.Type == EFurnitureComponentType::Cabinet)
				{
					Count = Product.CabinetOptions.Sizes.Num();
				}
				else
				{
					FFurnitureComponentOptions Options;
					if (SetBooth->GetResolvedComponentOptions(Axis.Type, Options)) Count = Options.Models.Num();
				}
				for (int32 Index = 0; Index < Count; ++Index)
				{
					FPlacedCabinetSetData Before;
					Manager->GetCabinetSet(ID, Before);
					SetBooth->RequestComponentSelection(Axis.Type, Index, 0);
					ExpectFlush(ID, SetBooth, FString::Printf(TEXT("%s, %s %d"), *Entry.ID, Axis.Name, Index));
					FPlacedCabinetSetData After;
					Manager->GetCabinetSet(ID, After);
					++Checked;
					if (!FMath::IsNearlyEqual(Before.WallAttachment.DepthOffsetCm, After.WallAttachment.DepthOffsetCm, 0.01f)) ++Moves;
				}
			}
			if (Moves > 0) RearMoved.Add(FString::Printf(TEXT("%s (%d)"), *Entry.ID, Moves));
			Manager->RemoveCabinetSet(ID);
		}
		AddInfo(FString::Printf(TEXT("%d configurations of %d cabinet sets stayed flush. Rear moved and was re-measured in: %s"),
			Checked, Sets.Num(), RearMoved.Num() > 0 ? *FString::Join(RearMoved, TEXT(", ")) : TEXT("none")));
	}

	// 3. A reconfiguration that moves the rear 25 cm into the wall (catalog-independent): one part is shifted back, then the
	// booth reports the change as it does after every configuration change.
	{
		UStaticMeshComponent* Part = nullptr;
		TArray<UStaticMeshComponent*> MeshComps;
		Booth->GetComponents(MeshComps);
		for (UStaticMeshComponent* Comp : MeshComps)
		{
			if (Comp && Comp->GetStaticMesh() && Comp->IsVisible() && (!Part || Comp == Booth->MainCabinet)) Part = Comp;
		}
		FPlacedCabinetSetData Before;
		if (TestNotNull(TEXT("A visible part to move"), Part) && TestTrue(TEXT("Set in the layout"), Manager->GetCabinetSet(SetID, Before)))
		{
			Part->AddWorldOffset(FVector(0., (RearY(Booth) - 25.) - RearY(Booth, Part), 0.));
			TestTrue(TEXT("Moved part reaches 25 cm into the wall"), RearY(Booth) <= FaceY - 25. + 0.1);
			Booth->OnProductChanged.Broadcast(Booth, Booth->ActiveState.ProductID);
			ExpectFlush(SetID, Booth, TEXT("Rear moved 25 cm back"));
			FPlacedCabinetSetData After;
			Manager->GetCabinetSet(SetID, After);
			TestTrue(FString::Printf(TEXT("Stand-off grew by 25 cm (%.1f → %.1f)"), Before.WallAttachment.DepthOffsetCm, After.WallAttachment.DepthOffsetCm),
				FMath::IsNearlyEqual(After.WallAttachment.DepthOffsetCm - Before.WallAttachment.DepthOffsetCm, 25.f, 0.1f));
		}
	}

	// 4. A free-standing set has no wall to keep to: a reconfiguration leaves it where it is.
	{
		const FVector FreeLocation(250., 250., 0.);
		const FString FreeID = Manager->AddCabinetSet(FName(*Sets[0].ID), FreeLocation, FRotator::ZeroRotator);
		AShowroomBooth* FreeBooth = Manager->FindCabinetSetActor(FreeID);
		if (TestNotNull(TEXT("Free-standing booth"), FreeBooth))
		{
			if (!FreeBooth->HasActorBegunPlay()) FreeBooth->DispatchBeginPlay();
			FreeBooth->RequestComponentSelection(EFurnitureComponentType::Cabinet, FreeBooth->ActiveState.ActiveSizeIndex, FreeBooth->ActiveState.ActiveColorIndex);
			TestTrue(TEXT("Free-standing set: not moved by a reconfiguration"), FreeBooth->GetActorLocation().Equals(FreeLocation, 0.01));
			TestFalse(TEXT("Free-standing set: nothing to re-measure"), Manager->RemeasureCabinetSetWallDepth(FreeID));
		}
	}

	Manager->Destroy();
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
