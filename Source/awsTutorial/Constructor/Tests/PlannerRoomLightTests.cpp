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

	// The slab never enters the ceiling: with the default flush offset (0.5 cm) it has no room for a rim and the panel is its
	// luminous face alone; 4 cm under the ceiling it gets the full SurfaceThicknessCm. Never a zero-area triangle.
	auto CheckPanelSlab = [this, LightActor, &Room](const TCHAR* State, float ExpectedHeightCm)
	{
		const FProcMeshSection* Section = LightActor->SurfaceMesh->GetProcMeshSection(0);
		if (!TestNotNull(*FString::Printf(TEXT("%s: panel section"), State), Section)) return;
		float MinZ = TNumericLimits<float>::Max(), MaxZ = TNumericLimits<float>::Lowest();
		for (const FProcMeshVertex& Vertex : Section->ProcVertexBuffer)
		{
			MinZ = FMath::Min(MinZ, (float)Vertex.Position.Z);
			MaxZ = FMath::Max(MaxZ, (float)Vertex.Position.Z);
		}
		int32 Degenerate = 0;
		const TArray<uint32>& Indices = Section->ProcIndexBuffer;
		for (int32 i = 0; i + 2 < Indices.Num(); i += 3)
		{
			const FVector A = Section->ProcVertexBuffer[Indices[i]].Position;
			const FVector B = Section->ProcVertexBuffer[Indices[i + 1]].Position;
			const FVector C = Section->ProcVertexBuffer[Indices[i + 2]].Position;
			Degenerate += ((B - A) ^ (C - A)).SizeSquared() < 1e-4 ? 1 : 0;
		}
		TestEqual(*FString::Printf(TEXT("%s: no degenerate triangles"), State), Degenerate, 0);
		TestTrue(*FString::Printf(TEXT("%s: slab height %.2f cm (expected %.2f)"), State, MaxZ - MinZ, ExpectedHeightCm),
			FMath::IsNearlyEqual(MaxZ - MinZ, ExpectedHeightCm, 0.01f));
		TestTrue(*FString::Printf(TEXT("%s: the slab stays under the ceiling (top %.2f)"), State, MaxZ), MaxZ <= Room.CeilingHeightCm - 0.5f + 0.01f);
	};
	TestEqual(TEXT("Default offset: flush with the ceiling"), Settings.CeilingOffsetCm, 0.5f);
	CheckPanelSlab(TEXT("Flush panel"), 0.f);
	Settings.CeilingOffsetCm = 4.f;
	LightActor->Build(Room, Settings);
	CheckPanelSlab(TEXT("Panel 4 cm down"), Settings.SurfaceThicknessCm);
	Settings.CeilingOffsetCm = FPlannerRoomLightSettings().CeilingOffsetCm;
	LightActor->Build(Room, Settings);

	// And off again: rebuilding drops the geometry.
	Settings.bShowSurface = false;
	LightActor->Build(Room, Settings);
	TestEqual(TEXT("The panel is dropped again"), LightActor->SurfaceMesh->GetNumSections(), 0);
	TestEqual(TEXT("The light is still there"), LightActor->GetLightCount(), 1);

	LightActor->Destroy();
	return true;
}

#endif
