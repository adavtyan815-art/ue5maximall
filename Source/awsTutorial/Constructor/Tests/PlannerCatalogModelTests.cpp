// Copyright 2026 MaxiMall. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Image.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextBlock.h"
#include "Constructor/PlannerPlacedObjectActor.h"
#include "Constructor/RoomPlannerManager.h"
#include "ContentStreaming.h"
#include "Engine/DataTable.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "FurnitureConfigurator/UI/PlannerCatalogItemWidget.h"
#include "FurnitureConfigurator/UI/RoomPlannerWidget.h"
#include "ImageUtils.h"
#include "Materials/Material.h"
#include "MaterialDomain.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "RenderingThread.h"
#include "Slate/WidgetRenderer.h"
#if WITH_EDITOR
#include "TextureCompiler.h"
#endif

// ─────────────────────────────────────────────────────────────────────────────
// The interior catalog (DT_PlannerObjects) with the imported models: every row loads and places correctly
// ─────────────────────────────────────────────────────────────────────────────
//
// The catalog had three rows (NewRow «Stul», NewRow_0 «Stol», NewRow_1 «Divan», all the Blazer sofa mesh); seven CC0 Poly Haven
// models were imported after them (meshes under /Game/RoomPlanner/Furniture/<asset>/, 512 × 512 thumbnails
// /Game/RoomPlanner/PlannerObjectsImages/T_Obj_<Row>). Rows marked bHideInCatalog are not offered as cards but still place and
// resolve by their AssetID; each row's FrontYawDeg says which way its model's front points. The expectations below come from the
// rows themselves (which are hidden, which way each faces). These tests only read Content.

namespace
{
	const TCHAR* CatalogModelTablePath = TEXT("/Game/DT/DT_PlannerObjects.DT_PlannerObjects");
	const TCHAR* CatalogModelWidgetClassPath = TEXT("/Game/RoomPlanner/WBP_RoomPlannerWidget.WBP_RoomPlannerWidget_C");
	const TCHAR* CatalogModelSofaMesh = TEXT("/Game/MedaFurniturePack/Furniture/Sofas/SM_Sofa-Blazer-283.SM_Sofa-Blazer-283");

	/** One of the three rows the catalog had before the import, as it was. */
	struct FCatalogModelOriginalRow
	{
		const TCHAR* ID;
		const TCHAR* Name;
		const TCHAR* Thumbnail;
		const TCHAR* Override; // slot 0 material override, or null for none
	};

	const FCatalogModelOriginalRow CatalogModelOriginalRows[] = {
		{ TEXT("NewRow"), TEXT("Stul"), TEXT("/Game/RoomPlanner/PlannerObjectsImages/Frame_565.Frame_565"),
			TEXT("/Game/MedaFurniturePack/Materials/Fabric/Sofa-Blazer/MI_Fabric-PlainWeave-Slubbed_Blazer-283.MI_Fabric-PlainWeave-Slubbed_Blazer-283") },
		{ TEXT("NewRow_0"), TEXT("Stol"), TEXT("/Game/RoomPlanner/PlannerObjectsImages/Frame_565__2_.Frame_565__2_"),
			TEXT("/Game/MedaFurniturePack/Materials/Fabric/Sofa-Blazer/MI_Fabric-BasketWeave-SemiSolid_Blazer-283.MI_Fabric-BasketWeave-SemiSolid_Blazer-283") },
		{ TEXT("NewRow_1"), TEXT("Divan"), TEXT("/Game/RoomPlanner/PlannerObjectsImages/Frame_565__1_.Frame_565__1_"), nullptr },
	};

	/** One imported row: its row name and the Poly Haven asset its mesh was imported from. */
	struct FCatalogModelImportedRow
	{
		const TCHAR* ID;
		const TCHAR* Asset;
	};

	const FCatalogModelImportedRow CatalogModelImportedRows[] = {
		{ TEXT("Chair_Dining02"), TEXT("dining_chair_02") },
		{ TEXT("Armchair_Modern01"), TEXT("modern_arm_chair_01") },
		{ TEXT("Table_Coffee01"), TEXT("modern_coffee_table_01") },
		{ TEXT("Table_Side01"), TEXT("side_table_01") },
		{ TEXT("Desk_Office01"), TEXT("metal_office_desk") },
		{ TEXT("Shelf_Display01"), TEXT("wooden_display_shelves_01") },
		{ TEXT("Cabinet_Drawer01"), TEXT("drawer_cabinet") },
	};

	struct FScopedCatalogModelWorld
	{
		UWorld* World = nullptr;
		explicit FScopedCatalogModelWorld(bool bInitializeActors)
		{
			World = UWorld::CreateWorld(EWorldType::Game, false);
			if (World && GEngine)
			{
				GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
			}
			// As a loaded game map: AActor::ProcessEvent drops every call (dynamic delegates included) until the world's actors are initialized.
			if (World && bInitializeActors) World->InitializeActorsForPlay(FURL());
		}
		~FScopedCatalogModelWorld()
		{
			if (World)
			{
				if (GEngine) GEngine->DestroyWorldContext(World);
				World->DestroyWorld(false);
				World->RemoveFromRoot();
			}
		}
	};

	/** Where an object's mesh really is: its local bounds through the mesh component's world transform. */
	struct FCatalogModelPlacement
	{
		FVector Centre = FVector::ZeroVector;
		FBox World = FBox(ForceInit);
	};

	bool GetCatalogModelPlacement(const ARoomPlannerManager* Manager, const FString& ID, FCatalogModelPlacement& Out)
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

	/** DT_PlannerObjects's rows in the table's order, and split by bHideInCatalog (row names). */
	struct FCatalogModelTableRows
	{
		TArray<FName> All;
		TArray<FString> Visible;
		TArray<FString> Hidden;
	};

	FCatalogModelTableRows ReadCatalogModelTableRows(const UDataTable* Table)
	{
		FCatalogModelTableRows Out;
		if (!Table) return Out;
		for (const auto& Pair : Table->GetRowMap())
		{
			const FPlannerObjectRow* Row = reinterpret_cast<const FPlannerObjectRow*>(Pair.Value);
			if (!Row) continue;
			Out.All.Add(Pair.Key);
			(Row->bHideInCatalog ? Out.Hidden : Out.Visible).Add(Pair.Key.ToString());
		}
		return Out;
	}

	FString JoinCatalogModelIDs(const TArray<FPlannerCatalogEntry>& Entries)
	{
		TArray<FString> IDs;
		for (const FPlannerCatalogEntry& Entry : Entries) IDs.Add(Entry.ID);
		return FString::Join(IDs, TEXT(", "));
	}

	/** Makes a thumbnail fully resident (all mips), as a card on screen would be once streamed. */
	void MakeCatalogThumbnailResident(UTexture2D* Texture)
	{
		if (!Texture) return;
#if WITH_EDITOR
		FTextureCompilingManager::Get().FinishCompilation({ Texture });
#endif
		Texture->SetForceMipLevelsToBeResident(60.f);
		Texture->WaitForStreaming();
	}

	/** The target holds linear colour: encoded to sRGB as the screen does, made opaque (text is drawn with partial alpha). */
	bool RenderCatalogWidget(const TSharedRef<SWidget>& Content, const FVector2D& Size, TArray<FColor>& OutPixels)
	{
		UTextureRenderTarget2D* Target = FWidgetRenderer::CreateTargetFor(Size, TF_Bilinear, false);
		if (!Target) return false;
		{
			FWidgetRenderer Renderer(false, true);
			for (int32 Pass = 0; Pass < 3; ++Pass)
			{
				Renderer.DrawWidget(Target, Content, Size, 0.016f);
			}
			FlushRenderingCommands();
		}
		FTextureRenderTargetResource* Resource = Target->GameThread_GetRenderTargetResource();
		if (!Resource || !Resource->ReadPixels(OutPixels)) return false;
		for (FColor& Pixel : OutPixels)
		{
			Pixel = FLinearColor(Pixel.R / 255.f, Pixel.G / 255.f, Pixel.B / 255.f, 1.f).ToFColor(true);
		}
		return OutPixels.Num() == (int32)Size.X * (int32)Size.Y;
	}

	bool SaveCatalogPng(int32 Width, int32 Height, const TArray<FColor>& Pixels, const FString& File)
	{
		TArray64<uint8> Png;
		FImageUtils::PNGCompressImageArray(Width, Height, TArrayView64<const FColor>(Pixels.GetData(), Pixels.Num()), Png);
		return FFileHelper::SaveArrayToFile(Png, *File);
	}

	/** A widget's painted rectangle in render-target pixels (the renderer paints at scale 1 from the origin). */
	FIntRect CatalogWidgetPixels(const UWidget* Widget)
	{
		const FGeometry& G = Widget->GetCachedGeometry();
		const FVector2D Pos = G.GetAbsolutePosition();
		const FVector2D Size = G.GetAbsoluteSize();
		return FIntRect(FMath::RoundToInt(Pos.X), FMath::RoundToInt(Pos.Y), FMath::RoundToInt(Pos.X + Size.X), FMath::RoundToInt(Pos.Y + Size.Y));
	}

	/** Mean and standard deviation of the sRGB-decoded luminance inside Rect (clipped to the image). */
	void CatalogPixelStats(const TArray<FColor>& Pixels, int32 Width, int32 Height, FIntRect Rect, double& OutMean, double& OutStdDev)
	{
		Rect.Clip(FIntRect(0, 0, Width, Height));
		double Sum = 0., SumSq = 0.;
		int32 Count = 0;
		for (int32 Y = Rect.Min.Y; Y < Rect.Max.Y; ++Y)
		{
			for (int32 X = Rect.Min.X; X < Rect.Max.X; ++X)
			{
				const FLinearColor L(Pixels[Y * Width + X]);
				const double Lum = 0.2126 * L.R + 0.7152 * L.G + 0.0722 * L.B;
				Sum += Lum;
				SumSq += Lum * Lum;
				++Count;
			}
		}
		OutMean = Count > 0 ? Sum / Count : 0.;
		OutStdDev = Count > 0 ? FMath::Sqrt(FMath::Max(0., SumSq / Count - OutMean * OutMean)) : 0.;
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// Every catalog row (hidden ones too) loads, stands on the floor centred on its drop point, turns about its centre, and hangs flush on a
// wall with its front to the room; the catalog offers exactly the rows not hidden
// ─────────────────────────────────────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlannerCatalogRowsPlaceableTest, "MaxiMall.Planner.Objects.CatalogRowsPlaceable",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlannerCatalogRowsPlaceableTest::RunTest(const FString& Parameters)
{
	FScopedCatalogModelWorld TestWorld(true);
	if (!TestNotNull(TEXT("Test world"), TestWorld.World)) return false;
	ARoomPlannerManager* Manager = TestWorld.World->SpawnActor<ARoomPlannerManager>();
	if (!TestNotNull(TEXT("Manager"), Manager)) return false;

	// The table in its own order, and the catalog as the planner offers it (the «Интерьер» cards come from exactly this list, in this
	// order): every row not marked bHideInCatalog.
	const UDataTable* Table = LoadObject<UDataTable>(nullptr, CatalogModelTablePath);
	if (!TestNotNull(TEXT("DT_PlannerObjects"), Table)) return false;
	const FCatalogModelTableRows TableRows = ReadCatalogModelTableRows(Table);
	const TArray<FPlannerCatalogEntry> Entries = Manager->GetAvailableObjects();
	AddInfo(FString::Printf(TEXT("Catalog order: %s; hidden (bHideInCatalog): %s"), *JoinCatalogModelIDs(Entries),
		TableRows.Hidden.Num() > 0 ? *FString::Join(TableRows.Hidden, TEXT(", ")) : TEXT("none")));
	TestEqual(TEXT("Ten rows in DT_PlannerObjects"), TableRows.All.Num(), 10);
	TestEqual(TEXT("The catalog offers exactly the rows not hidden, in table order"), JoinCatalogModelIDs(Entries), FString::Join(TableRows.Visible, TEXT(", ")));

	// The three original rows come first in the table, their data unchanged (whether the catalog offers them or not).
	for (int32 I = 0; I < UE_ARRAY_COUNT(CatalogModelOriginalRows); ++I)
	{
		const FCatalogModelOriginalRow& Expected = CatalogModelOriginalRows[I];
		TestTrue(FString::Printf(TEXT("Table row %d is %s"), I, Expected.ID), TableRows.All.IsValidIndex(I) && TableRows.All[I] == FName(Expected.ID));
		const FPlannerObjectRow* Row = Table->FindRow<FPlannerObjectRow>(FName(Expected.ID), TEXT("CatalogRowsPlaceable"), false);
		if (!TestNotNull(FString::Printf(TEXT("%s row"), Expected.ID), Row)) continue;
		TestEqual(FString::Printf(TEXT("%s: name unchanged"), Expected.ID), Row->DisplayName.ToString(), FString(Expected.Name));
		TestEqual(FString::Printf(TEXT("%s: thumbnail unchanged"), Expected.ID), Row->Thumbnail.ToSoftObjectPath().ToString(), FString(Expected.Thumbnail));
		TestEqual(FString::Printf(TEXT("%s: mesh unchanged (the Blazer sofa)"), Expected.ID), Row->Mesh.ToSoftObjectPath().ToString(), FString(CatalogModelSofaMesh));
		TestEqual(FString::Printf(TEXT("%s: category unchanged"), Expected.ID), Row->Category, FString());
		TestTrue(FString::Printf(TEXT("%s: default scale unchanged"), Expected.ID), Row->DefaultScale.Equals(FVector::OneVector));
		TestTrue(FString::Printf(TEXT("%s: colour catalog allowed, as before"), Expected.ID), Row->bAllowColorCatalog);
		if (Expected.Override)
		{
			TestTrue(FString::Printf(TEXT("%s: its one material override unchanged"), Expected.ID), Row->MaterialOverrides.Num() == 1
				&& Row->MaterialOverrides[0].SlotIndex == 0 && Row->MaterialOverrides[0].Material.ToSoftObjectPath().ToString() == Expected.Override);
		}
		else
		{
			TestEqual(FString::Printf(TEXT("%s: still no material override"), Expected.ID), Row->MaterialOverrides.Num(), 0);
		}
	}

	// The seven imported rows are all there, after the original ones, each with its own mesh and thumbnail.
	for (const FCatalogModelImportedRow& Expected : CatalogModelImportedRows)
	{
		const int32 Index = TableRows.All.IndexOfByKey(FName(Expected.ID));
		if (!TestTrue(FString::Printf(TEXT("%s is in DT_PlannerObjects"), Expected.ID), Index != INDEX_NONE)) continue;
		TestTrue(FString::Printf(TEXT("%s comes after the original rows (at %d)"), Expected.ID, Index), Index >= UE_ARRAY_COUNT(CatalogModelOriginalRows));
		const FPlannerObjectRow* Row = Table->FindRow<FPlannerObjectRow>(FName(Expected.ID), TEXT("CatalogRowsPlaceable"), false);
		if (!TestNotNull(FString::Printf(TEXT("%s row"), Expected.ID), Row)) continue;
		const FString MeshPath = Row->Mesh.ToSoftObjectPath().ToString();
		const FString ThumbName = FString::Printf(TEXT("T_Obj_%s"), Expected.ID);
		TestTrue(FString::Printf(TEXT("%s: mesh imported from %s (%s)"), Expected.ID, Expected.Asset, *MeshPath),
			MeshPath.StartsWith(FString::Printf(TEXT("/Game/RoomPlanner/Furniture/%s/"), Expected.Asset)));
		TestEqual(FString::Printf(TEXT("%s: its own thumbnail"), Expected.ID), Row->Thumbnail.ToSoftObjectPath().ToString(),
			FString::Printf(TEXT("/Game/RoomPlanner/PlannerObjectsImages/%s.%s"), *ThumbName, *ThumbName));
		TestFalse(FString::Printf(TEXT("%s: a name for the card"), Expected.ID), Row->DisplayName.IsEmpty());
	}

	// Every row, hidden ones included: mesh and thumbnail load, the thumbnail is 512 × 512, the mesh has real materials (usable with
	// Nanite when the mesh is Nanite: a material without that usage renders as the default material in a cooked build).
	struct FRowInfo
	{
		FString ID;
		UStaticMesh* Mesh = nullptr;
		float FrontYaw = 0.f;
	};
	TArray<FRowInfo> Rows;
	for (const FName& RowName : TableRows.All)
	{
		const FString ID = RowName.ToString();
		const FPlannerObjectRow* Row = Table->FindRow<FPlannerObjectRow>(RowName, TEXT("CatalogRowsPlaceable"), false);
		if (!TestNotNull(FString::Printf(TEXT("%s row"), *ID), Row)) continue;
		const bool bOffered = Entries.ContainsByPredicate([&ID](const FPlannerCatalogEntry& E) { return E.ID == ID; });
		TestTrue(FString::Printf(TEXT("%s: %s"), *ID, Row->bHideInCatalog ? TEXT("hidden, not offered") : TEXT("offered in the catalog")), bOffered != Row->bHideInCatalog);
		FPlannerCatalogEntry Resolved;
		TestTrue(FString::Printf(TEXT("%s: resolves by its AssetID%s"), *ID, Row->bHideInCatalog ? TEXT(" although hidden") : TEXT("")),
			Manager->FindObjectCatalogEntry(ID, Resolved) && Resolved.ID == ID && !Resolved.DisplayName.IsEmpty());
		UStaticMesh* Mesh = Row->Mesh.LoadSynchronous();
		UTexture2D* Thumbnail = Row->Thumbnail.LoadSynchronous();
		TestNotNull(FString::Printf(TEXT("%s: mesh loads (%s)"), *ID, *Row->Mesh.ToString()), Mesh);
		if (TestNotNull(FString::Printf(TEXT("%s: thumbnail loads (%s)"), *ID, *Row->Thumbnail.ToString()), Thumbnail))
		{
#if WITH_EDITOR
			FTextureCompilingManager::Get().FinishCompilation({ Thumbnail });
#endif
			int32 SourceX = 0, SourceY = 0;
#if WITH_EDITORONLY_DATA
			SourceX = (int32)Thumbnail->Source.GetSizeX();
			SourceY = (int32)Thumbnail->Source.GetSizeY();
			TestTrue(FString::Printf(TEXT("%s: thumbnail source is 512 x 512 (%d x %d)"), *ID, SourceX, SourceY), SourceX == 512 && SourceY == 512);
#endif
			// The built (platform) size exists only where textures are built for rendering: not under -nullrhi, where it reads 0.
			const int32 SizeX = Thumbnail->GetSizeX();
			const int32 SizeY = Thumbnail->GetSizeY();
			if (SizeX > 0 || SizeY > 0 || SourceX == 0)
			{
				TestTrue(FString::Printf(TEXT("%s: thumbnail is 512 x 512 as built (%d x %d)"), *ID, SizeX, SizeY), SizeX == 512 && SizeY == 512);
			}
			AddInfo(FString::Printf(TEXT("[Catalog] %s thumbnail %s: %d x %d (source %d x %d), %d mips, group %s, sRGB %d, never stream %d"),
				*ID, *Thumbnail->GetName(), SizeX, SizeY, SourceX, SourceY, Thumbnail->GetNumMips(),
				*UEnum::GetValueAsString(Thumbnail->LODGroup.GetValue()), Thumbnail->SRGB ? 1 : 0, Thumbnail->NeverStream ? 1 : 0));
		}
		if (Mesh)
		{
			const TArray<FStaticMaterial>& Materials = Mesh->GetStaticMaterials();
			TestTrue(FString::Printf(TEXT("%s: the mesh has material slots"), *ID), Materials.Num() > 0);
			const UMaterialInterface* Default = UMaterial::GetDefaultMaterial(MD_Surface);
			TArray<FString> MaterialNames;
			for (int32 Slot = 0; Slot < Materials.Num(); ++Slot)
			{
				const UMaterialInterface* Material = Materials[Slot].MaterialInterface;
				TestTrue(FString::Printf(TEXT("%s: slot %d has its own material, not the engine default"), *ID, Slot), Material && Material != Default);
				MaterialNames.Add(Material ? Material->GetName() : TEXT("<none>"));
				if (Mesh->IsNaniteEnabled())
				{
					const UMaterial* Base = Material ? Material->GetMaterial() : nullptr;
					TestTrue(FString::Printf(TEXT("%s: slot %d material usable with Nanite (%s, base %s)"), *ID, Slot, Material ? *Material->GetName() : TEXT("<none>"),
						Base ? *Base->GetPathName() : TEXT("<none>")), Base && Base->GetUsageByFlag(MATUSAGE_Nanite));
				}
			}
			AddInfo(FString::Printf(TEXT("[Catalog] %s mesh %s: %d LOD(s), Nanite %d, materials [%s], %d row override(s)"), *ID, *Mesh->GetName(),
				Mesh->GetNumLODs(), Mesh->IsNaniteEnabled() ? 1 : 0, *FString::Join(MaterialNames, TEXT(", ")), Row->MaterialOverrides.Num()));
			Rows.Add({ ID, Mesh, Row->FrontYawDeg });
		}
	}

	// A 5 × 4 m room; its south wall's room-side face is y = 10 (20 cm walls from y = 0).
	const int32 South = Manager->AddWallBetweenPoints(FVector2D(0., 0.), FVector2D(500., 0.));
	Manager->AddWallBetweenPoints(FVector2D(500., 0.), FVector2D(500., 400.));
	Manager->AddWallBetweenPoints(FVector2D(500., 400.), FVector2D(0., 400.));
	Manager->AddWallBetweenPoints(FVector2D(0., 400.), FVector2D(0., 0.));
	Manager->RebuildRooms();
	Manager->SetViewMode(true);
	Manager->SetToolMode(EPlannerToolMode::Select);
	constexpr double FaceY = 10.;
	const FVector Drop(230., 170., 0.);

	TArray<FString> Footprints;
	for (const FRowInfo& Info : Rows)
	{
		const FString& ID = Info.ID;

		// Floor drop, exactly as Server_PlaceCatalogItem places a floor drop (yaw 0).
		const FString Obj = Manager->AddPlacedObject(ID, FVector(Drop.X, Drop.Y, 0.f), FRotator(0.f, FRotator::NormalizeAxis(0.f), 0.f), FVector::OneVector);
		if (!TestFalse(FString::Printf(TEXT("%s: placed on the floor"), *ID), Obj.IsEmpty())) continue;
		const APlannerPlacedObjectActor* Actor = Manager->FindPlacedObjectActor(Obj);
		if (!TestTrue(FString::Printf(TEXT("%s: shown with its own mesh, not the placeholder"), *ID),
			Actor && Actor->MeshComponent && Actor->MeshComponent->GetStaticMesh() == Info.Mesh)) continue;
		FCatalogModelPlacement P;
		if (!TestTrue(FString::Printf(TEXT("%s: mesh placement"), *ID), GetCatalogModelPlacement(Manager, Obj, P))) continue;
		const FVector Size = P.World.GetSize();
		const FVector Pivot = Actor->MeshComponent->GetRelativeLocation();
		Footprints.Add(FString::Printf(TEXT("%s %.1f x %.1f x %.1f"), *ID, Size.X, Size.Y, Size.Z));
		AddInfo(FString::Printf(TEXT("[Catalog] %s footprint %.1f x %.1f cm (X x Y), height %.1f cm; pivot offset (%.2f, %.2f, %.2f)"),
			*ID, Size.X, Size.Y, Size.Z, Pivot.X, Pivot.Y, Pivot.Z));
		TestTrue(FString::Printf(TEXT("%s: bottom on the floor (min Z %.3f)"), *ID, P.World.Min.Z), FMath::Abs(P.World.Min.Z) < 1.);
		TestTrue(FString::Printf(TEXT("%s: footprint centred on the drop point (%.3f, %.3f)"), *ID, P.Centre.X, P.Centre.Y),
			FVector2D(P.Centre.X, P.Centre.Y).Equals(FVector2D(Drop.X, Drop.Y), 0.1));
		TestTrue(FString::Printf(TEXT("%s: a furniture size, not a unit mix-up (%.1f x %.1f x %.1f cm)"), *ID, Size.X, Size.Y, Size.Z),
			Size.X >= 15. && Size.Y >= 15. && Size.Z >= 15. && Size.X <= 320. && Size.Y <= 320. && Size.Z <= 260.);

		// Turned 90° with the wheel (six 15° notches, the local 2D path), then committed as the burst's commit does: the centre stays.
		TestTrue(FString::Printf(TEXT("%s: selected"), *ID), Manager->SelectPlacedObject(Obj));
		FString TurnedID;
		bool bSet = false;
		float Yaw = 0.f;
		for (int32 Notch = 0; Notch < 6; ++Notch) Manager->RotateSelectionLocal(15.f, TurnedID, bSet, Yaw);
		TestTrue(FString::Printf(TEXT("%s: turned to 90° (%.1f)"), *ID, Yaw), TurnedID == Obj && FMath::IsNearlyEqual(Yaw, 90.f, 0.01f));
		FCatalogModelPlacement Turned;
		if (GetCatalogModelPlacement(Manager, Obj, Turned))
		{
			TestTrue(FString::Printf(TEXT("%s: turned, the centre stays (%.3f, %.3f)"), *ID, Turned.Centre.X, Turned.Centre.Y),
				FVector2D(Turned.Centre.X, Turned.Centre.Y).Equals(FVector2D(Drop.X, Drop.Y), 0.1));
			TestTrue(FString::Printf(TEXT("%s: turned, the footprint turned with it (%.1f x %.1f)"), *ID, Turned.World.GetSize().X, Turned.World.GetSize().Y),
				FMath::IsNearlyEqual(Turned.World.GetSize().X, Size.Y, 0.1) && FMath::IsNearlyEqual(Turned.World.GetSize().Y, Size.X, 0.1));
			TestTrue(FString::Printf(TEXT("%s: turned, still on the floor (%.3f)"), *ID, Turned.World.Min.Z), FMath::Abs(Turned.World.Min.Z) < 1.);
		}
		FPlacedFurnitureData D;
		if (Manager->GetPlacedObject(Obj, D))
		{
			TestTrue(FString::Printf(TEXT("%s: turn committed"), *ID), Manager->MovePlacedObject(Obj, D.Location, D.Rotation, D.Scale));
			FCatalogModelPlacement Committed;
			TestTrue(FString::Printf(TEXT("%s: committed, the centre stays"), *ID), GetCatalogModelPlacement(Manager, Obj, Committed)
				&& FVector2D(Committed.Centre.X, Committed.Centre.Y).Equals(FVector2D(Drop.X, Drop.Y), 0.1) && FMath::Abs(Committed.World.Min.Z) < 1.);
		}
		Manager->RemovePlacedObject(Obj);

		// A floor-standing wall drop on the south wall (room side +Y: yaw 90 points away from it), by its AssetID as the drop sends it:
		// turned so the row's front (FrontYawDeg) faces the room, its rearmost point on the room-side face, centred where dropped. Its
		// depth off the wall is then the model's front-to-back extent (its Y size at yaw 0 when the front is ±Y, else its X size).
		constexpr double WallAwayYaw = 90.;
		const bool bFrontAlongY = FMath::IsNearlyEqual(FMath::Abs(Info.FrontYaw), 90.f, 0.01f);
		const double FrontToBack = bFrontAlongY ? Size.Y : Size.X;
		const double SideToSide = bFrontAlongY ? Size.X : Size.Y;
		const double ExpectedYaw = FRotator::NormalizeAxis(WallAwayYaw - Info.FrontYaw);
		const FString WallObj = Manager->AddPlacedObjectOnWall(ID, South, 250.f, true, 0.f);
		if (!TestFalse(FString::Printf(TEXT("%s: placed on the wall"), *ID), WallObj.IsEmpty())) continue;
		FCatalogModelPlacement W;
		if (TestTrue(FString::Printf(TEXT("%s: wall placement"), *ID), GetCatalogModelPlacement(Manager, WallObj, W)))
		{
			TestTrue(FString::Printf(TEXT("%s: back on the wall face (y %.3f)"), *ID, W.World.Min.Y), FMath::IsNearlyEqual(W.World.Min.Y, FaceY, 0.1));
			TestTrue(FString::Printf(TEXT("%s: on the wall, bottom on the floor (%.3f)"), *ID, W.World.Min.Z), FMath::Abs(W.World.Min.Z) < 1.);
			TestTrue(FString::Printf(TEXT("%s: centred at 250 along the wall (%.3f)"), *ID, W.Centre.X), FMath::IsNearlyEqual(W.Centre.X, 250., 0.1));
			TestTrue(FString::Printf(TEXT("%s: its depth off the wall is its front-to-back extent (%.1f / %.1f, front yaw %.0f)"), *ID, W.World.GetSize().Y,
				FrontToBack, Info.FrontYaw), FMath::IsNearlyEqual(W.World.GetSize().Y, FrontToBack, 0.1));
			TestTrue(FString::Printf(TEXT("%s: along the wall, its side-to-side extent (%.1f / %.1f)"), *ID, W.World.GetSize().X, SideToSide),
				FMath::IsNearlyEqual(W.World.GetSize().X, SideToSide, 0.1));
		}
		FPlacedFurnitureData WD;
		const bool bGotWallData = Manager->GetPlacedObject(WallObj, WD);
		TestTrue(FString::Printf(TEXT("%s: on the wall, its front to the room (yaw %.2f, expected %.2f = %.0f − front yaw %.0f)"), *ID, bGotWallData ? WD.Rotation.Yaw : 0.,
			ExpectedYaw, WallAwayYaw, Info.FrontYaw), bGotWallData && FMath::Abs(FRotator::NormalizeAxis(WD.Rotation.Yaw - ExpectedYaw)) < 0.01);
		AddInfo(FString::Printf(TEXT("[Catalog] %s wall drop: front yaw %.0f, actor yaw %.1f, %.1f cm deep off the wall, %.1f cm along it"), *ID, Info.FrontYaw,
			bGotWallData ? WD.Rotation.Yaw : 0., W.World.GetSize().Y, W.World.GetSize().X));
		Manager->RemovePlacedObject(WallObj);
	}
	AddInfo(FString::Printf(TEXT("Footprints (cm, X x Y x Z at yaw 0): %s"), *FString::Join(Footprints, TEXT("; "))));
	TestEqual(TEXT("Every row was placed, hidden ones included"), Footprints.Num(), TableRows.All.Num());
	Manager->Destroy();
	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// The «Интерьер» catalog rendered off-screen (needs a GPU and -PlannerSnapshotDir=<folder>)
// ─────────────────────────────────────────────────────────────────────────────
//
// Writes into the snapshot folder:
//   InteriorCatalog_panel.png  the planner at 1920 × 1080 with «Каталог» → «Интерьер» open, as the WBP configures it (scrolling area)
//   InteriorCatalog_all.png    the same with the scroll limit lifted, so every card is on screen
//   InteriorCatalog_cards.png  the cards of the second picture, cropped and doubled
// and checks that every card shows its row's thumbnail, drawn (not flat) and square.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlannerInteriorCatalogSnapshotTest, "MaxiMall.Planner.UI.InteriorCatalogSnapshot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlannerInteriorCatalogSnapshotTest::RunTest(const FString& Parameters)
{
	FString OutDir;
	if (!FParse::Value(FCommandLine::Get(), TEXT("PlannerSnapshotDir="), OutDir) || !FApp::CanEverRender() || !FSlateApplication::IsInitialized())
	{
		AddInfo(TEXT("Snapshot skipped (needs a GPU and -PlannerSnapshotDir=<folder>)."));
		return true;
	}
	FScopedCatalogModelWorld TestWorld(false);
	if (!TestNotNull(TEXT("Test world"), TestWorld.World)) return false;
	UClass* WidgetClass = LoadClass<URoomPlannerWidget>(nullptr, CatalogModelWidgetClassPath);
	if (!TestNotNull(TEXT("WBP_RoomPlannerWidget"), WidgetClass)) return false;
	URoomPlannerWidget* Widget = CreateWidget<URoomPlannerWidget>(TestWorld.World, WidgetClass);
	if (!TestNotNull(TEXT("Planner widget"), Widget)) return false;
	Widget->BuildRuntimeLayout(); // without a player NativeOnInitialized does not run; the layout built in code is (idempotent)
	const TSharedRef<SWidget> Slate = Widget->TakeWidget();

	// «Каталог» → «Интерьер», as the user opens it.
	Widget->SetCatalogOpen(true);
	Widget->SetActiveCatalogTab(EPlannerPlacementKind::Object);
	const TArray<FPlannerCatalogEntry> Entries = Widget->GetAvailableObjects();
	const FCatalogModelTableRows TableRows = ReadCatalogModelTableRows(LoadObject<UDataTable>(nullptr, CatalogModelTablePath));
	TestTrue(TEXT("DT_PlannerObjects has rows"), TableRows.All.Num() > 0);
	TestEqual(TEXT("The interior cards are the rows not hidden, in table order"), JoinCatalogModelIDs(Entries), FString::Join(TableRows.Visible, TEXT(", ")));
	AddInfo(FString::Printf(TEXT("Interior cards: %d of %d rows (%s); hidden: %s"), Entries.Num(), TableRows.All.Num(), *JoinCatalogModelIDs(Entries),
		TableRows.Hidden.Num() > 0 ? *FString::Join(TableRows.Hidden, TEXT(", ")) : TEXT("none")));

	auto CollectCards = [Widget]()
	{
		TArray<UPlannerCatalogItemWidget*> Cards;
		Widget->WidgetTree->ForEachWidget([&Cards](UWidget* W)
		{
			if (UPlannerCatalogItemWidget* Card = Cast<UPlannerCatalogItemWidget>(W)) Cards.Add(Card);
		});
		return Cards;
	};
	TArray<UPlannerCatalogItemWidget*> Cards = CollectCards();
	if (!TestEqual(TEXT("One card per row"), Cards.Num(), Entries.Num())) return false;
	for (int32 I = 0; I < Cards.Num(); ++I)
	{
		UTexture2D* Thumbnail = Entries[I].Thumbnail.LoadSynchronous();
		MakeCatalogThumbnailResident(Thumbnail);
		const UImage* Picture = Cards[I]->GetThumbnailImage();
		TestTrue(FString::Printf(TEXT("Card %d is %s"), I, *Entries[I].ID), Cards[I]->ItemID == Entries[I].ID);
		TestTrue(FString::Printf(TEXT("Card %s shows its thumbnail"), *Entries[I].ID), Picture && Thumbnail && Picture->GetBrush().GetResourceObject() == Thumbnail);
		TestTrue(FString::Printf(TEXT("Card %s: thumbnail built at 512 x 512 (%d x %d, %d mips)"), *Entries[I].ID, Thumbnail ? Thumbnail->GetSizeX() : 0,
			Thumbnail ? Thumbnail->GetSizeY() : 0, Thumbnail ? Thumbnail->GetNumMips() : 0), Thumbnail && Thumbnail->GetSizeX() == 512 && Thumbnail->GetSizeY() == 512);
	}
	IStreamingManager::Get().BlockTillAllRequestsFinished(5.f);

	const FVector2D Screen(1920.f, 1080.f);
	const int32 W = (int32)Screen.X, H = (int32)Screen.Y;
	TArray<FColor> Pixels;

	// As configured: the card area scrolls inside its height limit.
	if (TestTrue(TEXT("Panel rendered"), RenderCatalogWidget(Slate, Screen, Pixels)))
	{
		const FString File = FPaths::Combine(OutDir, TEXT("InteriorCatalog_panel.png"));
		TestTrue(TEXT("Panel snapshot written"), SaveCatalogPng(W, H, Pixels, File));
		int32 Visible = 0;
		FIntRect Area = CatalogWidgetPixels(Widget->Catalog_Container);
		for (UPlannerCatalogItemWidget* Card : Cards)
		{
			const FIntRect R = CatalogWidgetPixels(Card);
			if (R.Area() > 0 && R.Min.Y >= Area.Min.Y - 1 && R.Max.Y <= Area.Max.Y + 1) ++Visible;
		}
		AddInfo(FString::Printf(TEXT("Panel snapshot: %s (card area %d x %d at %d,%d; %d of %d cards fully in view without scrolling, limit %.0f px)"),
			*File, Area.Width(), Area.Height(), Area.Min.X, Area.Min.Y, Visible, Cards.Num(), Widget->CatalogContainerHeight));
	}

	// Every card on screen: the scroll limit lifted (this instance only), the cards rebuilt.
	Widget->CatalogContainerHeight = 0.f;
	Widget->RefreshCatalogPanels();
	Cards = CollectCards();
	if (!TestEqual(TEXT("One card per row after the rebuild"), Cards.Num(), Entries.Num())) return false;
	if (!TestTrue(TEXT("All cards rendered"), RenderCatalogWidget(Slate, Screen, Pixels))) return false;
	const FString AllFile = FPaths::Combine(OutDir, TEXT("InteriorCatalog_all.png"));
	TestTrue(TEXT("All-cards snapshot written"), SaveCatalogPng(W, H, Pixels, AllFile));

	FIntRect CardsArea;
	bool bFirst = true;
	int32 ReadableCaptions = 0;
	FIntRect PreviousCardRect;
	for (int32 I = 0; I < Cards.Num(); ++I)
	{
		const FIntRect CardRect = CatalogWidgetPixels(Cards[I]);
		const UImage* Picture = Cards[I]->GetThumbnailImage();
		const FIntRect ImageRect = Picture ? CatalogWidgetPixels(Picture) : FIntRect();

		// The cards measured here are the rebuilt ones: each is still its row's, shows that row's thumbnail, in catalog order on screen
		// (reading order: a card is right of the one before it in the same row, or starts a lower row).
		TestTrue(FString::Printf(TEXT("Rebuilt card %d is %s"), I, *Entries[I].ID), Cards[I]->ItemID == Entries[I].ID);
		TestTrue(FString::Printf(TEXT("Rebuilt card %s shows its own thumbnail"), *Entries[I].ID),
			Picture && Entries[I].Thumbnail.Get() && Picture->GetBrush().GetResourceObject() == Entries[I].Thumbnail.Get());
		if (I > 0)
		{
			const bool bSameRow = FMath::Abs(CardRect.Min.Y - PreviousCardRect.Min.Y) <= 1;
			TestTrue(FString::Printf(TEXT("Card %s follows %s in reading order (%d,%d after %d,%d)"), *Entries[I].ID, *Entries[I - 1].ID, CardRect.Min.X, CardRect.Min.Y,
				PreviousCardRect.Min.X, PreviousCardRect.Min.Y), bSameRow ? CardRect.Min.X > PreviousCardRect.Min.X : CardRect.Min.Y > PreviousCardRect.Min.Y);
		}
		PreviousCardRect = CardRect;

		const bool bOnScreen = CardRect.Area() > 0 && CardRect.Min.X >= 0 && CardRect.Min.Y >= 0 && CardRect.Max.X <= W && CardRect.Max.Y <= H;
		TestTrue(FString::Printf(TEXT("Card %s on screen (%d,%d – %d,%d)"), *Entries[I].ID, CardRect.Min.X, CardRect.Min.Y, CardRect.Max.X, CardRect.Max.Y), bOnScreen);
		if (!bOnScreen) continue;
		CardsArea = bFirst ? CardRect : FIntRect(CardsArea.Min.ComponentMin(CardRect.Min), CardsArea.Max.ComponentMax(CardRect.Max));
		bFirst = false;

		// The picture: drawn with detail (not a flat block), and as square as the 512 × 512 texture (not stretched).
		double Mean = 0., StdDev = 0.;
		CatalogPixelStats(Pixels, W, H, ImageRect, Mean, StdDev);
		const double Aspect = ImageRect.Height() > 0 ? (double)ImageRect.Width() / ImageRect.Height() : 0.;
		const UTexture2D* Thumbnail = Entries[I].Thumbnail.Get();
		const double TextureAspect = Thumbnail && Thumbnail->GetSizeY() > 0 ? (double)Thumbnail->GetSizeX() / Thumbnail->GetSizeY() : 1.;
		AddInfo(FString::Printf(TEXT("[Card] %s «%s»: picture %d x %d px at %d,%d, luminance mean %.3f, spread %.3f"), *Entries[I].ID,
			*Entries[I].DisplayName.ToString(), ImageRect.Width(), ImageRect.Height(), ImageRect.Min.X, ImageRect.Min.Y, Mean, StdDev));
		TestTrue(FString::Printf(TEXT("Card %s: the picture is drawn (spread %.3f)"), *Entries[I].ID, StdDev), ImageRect.Area() > 400 && StdDev > 0.02);
		TestTrue(FString::Printf(TEXT("Card %s: the picture keeps the texture's shape (%.3f / %.3f)"), *Entries[I].ID, Aspect, TextureAspect),
			FMath::IsNearlyEqual(Aspect, TextureAspect, 0.05));

		// The caption under the picture: its colour and whether anything shows against the panel.
		if (const UTextBlock* Caption = Cards[I]->GetNameText())
		{
			const FIntRect CaptionRect = CatalogWidgetPixels(Caption);
			double CaptionMean = 0., CaptionSpread = 0.;
			CatalogPixelStats(Pixels, W, H, CaptionRect, CaptionMean, CaptionSpread);
			const FLinearColor TextColor = Caption->GetColorAndOpacity().GetSpecifiedColor();
			if (CaptionRect.Area() > 0 && CaptionSpread > 0.02) ++ReadableCaptions;
			AddInfo(FString::Printf(TEXT("[Card] %s caption «%s»: text colour (%.2f, %.2f, %.2f, %.2f), %d x %d px, luminance mean %.3f, spread %.3f"),
				*Entries[I].ID, *Caption->GetText().ToString(), TextColor.R, TextColor.G, TextColor.B, TextColor.A, CaptionRect.Width(), CaptionRect.Height(),
				CaptionMean, CaptionSpread));
		}
	}
	if (ReadableCaptions < Cards.Num())
	{
		// Reported, not failed: the caption colour is a planner style setting (CatalogTextColor), not part of the catalog rows.
		AddWarning(FString::Printf(TEXT("Only %d of %d card captions show against the panel behind them (see the [Card] caption lines)."), ReadableCaptions, Cards.Num()));
	}
	AddInfo(FString::Printf(TEXT("All-cards snapshot: %s"), *AllFile));

	// The cards alone, doubled (nearest neighbour) for a close look.
	if (!bFirst)
	{
		FIntRect Crop(CardsArea.Min - FIntPoint(12, 12), CardsArea.Max + FIntPoint(12, 12));
		Crop.Clip(FIntRect(0, 0, W, H));
		const int32 CW = Crop.Width() * 2, CH = Crop.Height() * 2;
		TArray<FColor> Doubled;
		Doubled.SetNumUninitialized(CW * CH);
		for (int32 Y = 0; Y < CH; ++Y)
		{
			for (int32 X = 0; X < CW; ++X)
			{
				Doubled[Y * CW + X] = Pixels[(Crop.Min.Y + Y / 2) * W + (Crop.Min.X + X / 2)];
			}
		}
		const FString CardsFile = FPaths::Combine(OutDir, TEXT("InteriorCatalog_cards.png"));
		TestTrue(TEXT("Cards close-up written"), SaveCatalogPng(CW, CH, Doubled, CardsFile));
		AddInfo(FString::Printf(TEXT("Cards close-up: %s (%d x %d px of the screen, doubled)"), *CardsFile, Crop.Width(), Crop.Height()));
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
