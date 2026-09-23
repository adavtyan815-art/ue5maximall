// Copyright 2026 MaxiMall. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Constructor/PlannerRoomLightActor.h"
#include "Constructor/RoomPlannerTypes.h"
#include "Components/RectLightComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "ProceduralMeshComponent.h"

namespace
{
	struct FScopedLightTestWorld
	{
		UWorld* World = nullptr;
		FScopedLightTestWorld()
		{
			World = UWorld::CreateWorld(EWorldType::Game, false);
			if (World && GEngine)
			{
				GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
			}
		}
		~FScopedLightTestWorld()
		{
			if (World)
			{
				if (GEngine) GEngine->DestroyWorldContext(World);
				World->DestroyWorld(false);
				World->RemoveFromRoot();
			}
		}
	};

	/** A square room of side S cm, as RebuildRooms would hand it to the light. */
	FRoomData SquareRoom(double S)
	{
		FRoomData Room;
		Room.RoomID = 1;
		Room.FloorPolygon = { FVector2D(0., 0.), FVector2D(S, 0.), FVector2D(S, S), FVector2D(0., S) };
		Room.AreaM2 = (float)(S * S / 10000.);
		Room.CeilingHeightCm = 280.f;
		return Room;
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// The luminous panel is a visual extra: the room is lit with or without it
// ─────────────────────────────────────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlannerRoomLightPanelTest, "MaxiMall.Planner.RoomLight.PanelHidden",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlannerRoomLightPanelTest::RunTest(const FString& Parameters)
{
	FScopedLightTestWorld TestWorld;
	APlannerRoomLightActor* LightActor = TestWorld.World->SpawnActor<APlannerRoomLightActor>();
	if (!TestNotNull(TEXT("Light actor"), LightActor)) return false;

	const FRoomData Room = SquareRoom(400.);

	// Default settings: no visible panel on the ceiling, but the room is lit.
	FPlannerRoomLightSettings Settings;
	TestFalse(TEXT("The panel is off by default"), Settings.bShowSurface);
	LightActor->Build(Room, Settings);
	TestEqual(TEXT("No panel geometry is built"), LightActor->SurfaceMesh->GetNumSections(), 0);
	TestEqual(TEXT("The room still has its light"), LightActor->GetLightCount(), 1);
	TestTrue(FString::Printf(TEXT("The light emits (%.0f lm)"), LightActor->EmittedLumens), LightActor->EmittedLumens > 0.f);
	const float LumensWithoutPanel = LightActor->EmittedLumens;
	const FVector2D RectWithoutPanel = LightActor->LightRectSizeCm;

	// Turned on: the panel is built, and it changes nothing about the light itself.
	Settings.bShowSurface = true;
	LightActor->Build(Room, Settings);
	TestEqual(TEXT("The panel is built when asked for"), LightActor->SurfaceMesh->GetNumSections(), 1);
	TestTrue(TEXT("The panel glows"), LightActor->PanelLuminanceNits > 0.f);
	TestEqual(TEXT("Same emitted flux"), LightActor->EmittedLumens, LumensWithoutPanel);
	TestEqual(TEXT("Same light rectangle"), LightActor->LightRectSizeCm, RectWithoutPanel);

	// And off again: rebuilding drops the geometry.
	Settings.bShowSurface = false;
	LightActor->Build(Room, Settings);
	TestEqual(TEXT("The panel is dropped again"), LightActor->SurfaceMesh->GetNumSections(), 0);
	TestEqual(TEXT("The light is still there"), LightActor->GetLightCount(), 1);

	LightActor->Destroy();
	return true;
}

#endif
