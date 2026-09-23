// Copyright 2026 MaxiMall. All Rights Reserved.

#include "CodeLoadedAssets.h"

#if WITH_EDITOR
#include "UObject/ICookInfo.h"
#endif

namespace CodeLoadedAssets
{
	namespace
	{
		const TCHAR* const PackageNames[] =
		{
			// Room planner catalogs (RoomPlannerManager::Resolve*Catalog, FindCabinetSetLayoutRow). The cooker follows
			// the soft references in their rows (meshes, material overrides, thumbnails) once the tables are cooked.
			TEXT("/Game/DT/DT_PlannerObjects"),
			TEXT("/Game/DT/DT_CabinetSetLayouts"),
			TEXT("/Game/DT/DT_PlannerTiles"),
			TEXT("/Game/DT/DT_FurnitureCatalog"),

			// Room planner materials (RoomPlannerManager, ProceduralWallActor, PlannerRoomLightActor).
			TEXT("/Game/RoomPlanner/Materials/M_PlannerPaint"),
			TEXT("/Game/RoomPlanner/Materials/M_PlannerTile"),
			TEXT("/Game/RoomPlanner/Materials/M_WallSelection"),
			TEXT("/Game/RoomPlanner/Materials/M_OpeningSelection"),
			TEXT("/Game/ColorCatalog/Materials/M_Change_Color"),
			TEXT("/Game/NewDesign/scena/Materials/glass_2"),
			TEXT("/Game/NewDesign/scena/Materials/sid_glass_whitte"),
			TEXT("/Engine/EngineMaterials/EmissiveTexturedMaterial"),

			// Room planner classes and UI (RoomPlannerManager::ResolveCabinetSetActorClass, RoomPlannerWidget,
			// PlannerTileCatalogWidget).
			TEXT("/Game/BP_Booth"),
			TEXT("/Game/ColorCatalog/UI/WBP_ColorCatalog"),
			TEXT("/Game/RoomPlanner/Textures/T_TileCatalogIcon"),
			TEXT("/Game/FirstPerson/UI/CrowdTown/Online_Offline_Selection/Back"),

			// Furniture configurator tables (FurnitureTypes, ShowroomBooth).
			TEXT("/Game/DT/DT_SharedCountertops"),
			TEXT("/Game/DT/DT_SharedSinks"),
			TEXT("/Game/DT/AllowedFaucetIDs"),
			TEXT("/Game/DT/AllowedMirrorIDs"),
		};
	}

	TConstArrayView<const TCHAR*> GetPackageNames()
	{
		return PackageNames;
	}

#if WITH_EDITOR
	void AppendCookRules(TArray<UE::Cook::FPackageCookRule>& InOutRules)
	{
		for (const TCHAR* PackageName : PackageNames)
		{
			UE::Cook::FPackageCookRule& Rule = InOutRules.AddDefaulted_GetRef();
			Rule.PackageName = FName(PackageName);
			Rule.InstigatorName = FName(TEXT("CodeLoadedAssets"));
			Rule.CookRule = UE::Cook::EPackageCookRule::AddToCook;
		}
	}
#endif
}
