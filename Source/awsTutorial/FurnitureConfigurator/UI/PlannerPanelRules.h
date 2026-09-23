// Copyright 2026 MaxiMall. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "PlannerPanelRules.generated.h"

/**
 * The planner side panel's categories, like Planner 5D's left menu: one is open at a time and each opens its own page.
 * Editing is 2D-only, so 3D shows «Отделка» only.
 */
UENUM(BlueprintType)
enum class EPlannerPanelCategory : uint8
{
	Layout  UMETA(DisplayName = "Планировка"),
	Catalog UMETA(DisplayName = "Каталог"),
	Finish  UMETA(DisplayName = "Отделка")
};

/** Names of the WBP_RoomPlannerWidget widgets the code finds by name: a renamed designer widget breaks in one place. */
namespace PlannerPanelNames
{
	constexpr const TCHAR* PanelBackground = TEXT("Image_0");
	constexpr const TCHAR* LeftPanel = TEXT("LeftPanel");
	constexpr const TCHAR* PanelSections = TEXT("PanelSections");
	constexpr const TCHAR* ViewRow = TEXT("ViewRow");
	constexpr const TCHAR* ViewButtons = TEXT("HorizontalBox_0");
	constexpr const TCHAR* ViewSpacer = TEXT("Spacer_220");
	constexpr const TCHAR* ToolsRow = TEXT("ToolsRow");
	constexpr const TCHAR* SelectionBlock = TEXT("SelectionBlock");
	constexpr const TCHAR* FinishRow = TEXT("FinishRow");
	constexpr const TCHAR* InfoBlock = TEXT("InfoBlock");
	constexpr const TCHAR* SaveButton = TEXT("BtnSave");
	constexpr const TCHAR* BackButton = TEXT("BackButton");
	constexpr const TCHAR* FloorAreaCaption = TEXT("TOTALFLOORAREA");
	constexpr const TCHAR* FirstSeparator = TEXT("Image_line_1");
	constexpr const TCHAR* LineAboveTools = TEXT("Image_line_2");
	constexpr const TCHAR* SeparatorPrefix = TEXT("Image_line");
}

/** What the category rules depend on. */
struct FPlannerPanelInputs
{
	bool bIs2D = true;
	/** The 2D category; remembered while in 3D. */
	EPlannerPanelCategory Category = EPlannerPanelCategory::Layout;
	bool bHasWalls = false;
};

/** What the panel shows for FPlannerPanelInputs. */
struct FPlannerPanelVisibility
{
	/** The highlighted tab: the 2D category, «Отделка» in 3D. */
	EPlannerPanelCategory Shown = EPlannerPanelCategory::Layout;
	bool bToolsRow = false;
	bool bLayoutPage = false;
	bool bCatalogPage = false;
	bool bFinishPage = false;
	/** «Сначала постройте комнату…» on «Каталог» / «Отделка» while the plan has no walls. */
	bool bEmptyPlanNotice = false;
	bool bLayoutTabEnabled = true;
	bool bCatalogTabEnabled = true;
	bool bFinishTabEnabled = true;
};

namespace PlannerPanelRules
{
	inline FPlannerPanelVisibility Compute(const FPlannerPanelInputs& In)
	{
		FPlannerPanelVisibility Out;
		Out.Shown = In.bIs2D ? In.Category : EPlannerPanelCategory::Finish;
		Out.bToolsRow = In.bIs2D && Out.Shown == EPlannerPanelCategory::Layout;
		Out.bLayoutPage = Out.bToolsRow;
		Out.bCatalogPage = In.bIs2D && Out.Shown == EPlannerPanelCategory::Catalog;
		Out.bFinishPage = Out.Shown == EPlannerPanelCategory::Finish;
		Out.bEmptyPlanNotice = In.bIs2D && !In.bHasWalls && Out.Shown != EPlannerPanelCategory::Layout;
		// 3D is picking and finishing only: «Планировка» and «Каталог» stay visible, disabled.
		Out.bLayoutTabEnabled = In.bIs2D;
		Out.bCatalogTabEnabled = In.bIs2D;
		Out.bFinishTabEnabled = true;
		return Out;
	}

	/** «Каталог» is the one tab that folds: clicked while open, the panel goes back to the tab that was open before it. */
	inline EPlannerPanelCategory AfterCatalogToggle(EPlannerPanelCategory Current, EPlannerPanelCategory BeforeCatalog)
	{
		if (Current != EPlannerPanelCategory::Catalog) return EPlannerPanelCategory::Catalog;
		return BeforeCatalog == EPlannerPanelCategory::Catalog ? EPlannerPanelCategory::Layout : BeforeCatalog;
	}

	/** Entering «Каталог» or «Отделка» on a plan with walls puts the Draw tool away: clicks select, and a drop never lands mid-draw. */
	inline bool SwitchesDrawToSelect(EPlannerPanelCategory Category, bool bHasWalls, bool bDrawToolActive)
	{
		return bHasWalls && bDrawToolActive && Category != EPlannerPanelCategory::Layout;
	}

	/** Outside «Планировка» a wall draw can only start on a plan without walls (Draw stays on); the panel then goes back to «Планировка». */
	inline bool ReturnsToLayout(EPlannerPanelCategory Category, bool bIs2D, bool bDrawing)
	{
		return bIs2D && bDrawing && Category != EPlannerPanelCategory::Layout;
	}

	/** Offset from the viewport's centre that centres the status strip over the free plan area right of the panel (and a catalog beside it). */
	inline float StatusStripOffset(float PanelWidth, float SideCatalogWidth)
	{
		return 0.5f * (PanelWidth + SideCatalogWidth);
	}

	/**
	 * Top-left of the floating context bar: centred under the selection's anchor label, kept on the free plan area (right of the UI,
	 * inside the viewport).
	 */
	inline FVector2D PlaceContextBar(const FVector2D& Anchor, const FVector2D& BarSize, const FVector2D& Viewport, float LeftLimit)
	{
		constexpr float Margin = 8.f;
		constexpr float BelowLabel = 24.f;
		const float MinX = LeftLimit + Margin;
		const float MaxX = FMath::Max(MinX, Viewport.X - BarSize.X - Margin);
		const float MaxY = FMath::Max(Margin, Viewport.Y - BarSize.Y - Margin);
		return FVector2D(FMath::Clamp(Anchor.X - 0.5f * BarSize.X, MinX, MaxX), FMath::Clamp(Anchor.Y + BelowLabel, Margin, MaxY));
	}

	/** The column a finish catalog beside the panel covers, in root-canvas X. */
	inline bool IsOverSideCatalog(float LocalX, float PanelWidth, float SideCatalogWidth)
	{
		return SideCatalogWidth > 0.f && LocalX >= PanelWidth && LocalX <= PanelWidth + SideCatalogWidth;
	}
}
