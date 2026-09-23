// Copyright 2026 MaxiMall. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "CodeLoadedAssets.h"
#include "Misc/PackageName.h"
#include "UObject/ICookInfo.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlannerCodeLoadedAssetsTest, "MaxiMall.Planner.Packaging.CodeLoadedAssets",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlannerCodeLoadedAssetsTest::RunTest(const FString& Parameters)
{
	// Every package the game module adds to the cook must exist; a renamed or moved asset would silently drop out of
	// the packaged client and server again.
	TSet<FString> Seen;
	for (const TCHAR* PackageName : CodeLoadedAssets::GetPackageNames())
	{
		const FString Name(PackageName);
		TestTrue(FString::Printf(TEXT("%s is a long package name"), PackageName), FPackageName::IsValidLongPackageName(Name));
		TestTrue(FString::Printf(TEXT("%s exists"), PackageName), FPackageName::DoesPackageExist(Name));
		TestFalse(FString::Printf(TEXT("%s is listed once"), PackageName), Seen.Contains(Name));
		Seen.Add(Name);
	}

	// The assets behind the packaged-build bugs: the «Интерьер» catalog, cabinet-set layouts, the tile catalog icon.
	for (const TCHAR* Required : { TEXT("/Game/DT/DT_PlannerObjects"), TEXT("/Game/DT/DT_CabinetSetLayouts"),
		TEXT("/Game/RoomPlanner/Textures/T_TileCatalogIcon") })
	{
		TestTrue(FString::Printf(TEXT("%s is added to the cook"), Required), Seen.Contains(Required));
	}

	// The cook delegate gets one AddToCook rule per package.
	TArray<UE::Cook::FPackageCookRule> Rules;
	CodeLoadedAssets::AppendCookRules(Rules);
	TestEqual(TEXT("One cook rule per package"), Rules.Num(), CodeLoadedAssets::GetPackageNames().Num());
	for (const UE::Cook::FPackageCookRule& Rule : Rules)
	{
		TestTrue(FString::Printf(TEXT("%s: AddToCook"), *Rule.PackageName.ToString()), Rule.CookRule == UE::Cook::EPackageCookRule::AddToCook);
	}
	return true;
}

#endif
