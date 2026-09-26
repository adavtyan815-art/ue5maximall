// Copyright 2026 MaxiMall. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Constructor/RoomPlannerTypes.h"
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

/** What a mouse-wheel notch over the 2D plan turns (PlannerPanelRules::ResolveWheelTarget). */
enum class EPlannerWheelTarget : uint8
{
	/** Nothing: the wheel keeps its usual behaviour (camera zoom, scroll boxes, Blueprint bindings). */
	None,
	/** The interior object card being dragged from the catalog: it is placed with the yaw it was given. */
	CatalogDrag,
	/** The armed click-to-place interior object. */
	Pending,
	/** The selected object / cabinet set; also the one being dragged (a drag always drags the selection). */
	Selection
};

/** What the wheel rule depends on. */
struct FPlannerWheelInputs
{
	bool bPlannerOpen = false;
	bool bIs2D = false;
	/** False while a full-screen catalog hides the planner (bFinishCatalogBesidePanel off): the wheel is that catalog's. */
	bool bPlannerUIShown = true;
	/** Over the side panel, the status strip, a catalog beside the panel or the floating bar. */
	bool bCursorOverPlannerUI = false;
	/** Kind of the catalog card being dragged (None: no catalog drag). */
	EPlannerPlacementKind CatalogDragKind = EPlannerPlacementKind::None;
	EPlannerPlacementKind PendingPlacementKind = EPlannerPlacementKind::None;
	/** An object or a cabinet set is selected. */
	bool bHasObjectOrSetSelected = false;
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

	/**
	 * What a wheel notch turns. Editing is 2D-only, so 3D never turns anything; the planner's own UI keeps the wheel (its scroll
	 * boxes, or nothing). Then the item being placed comes first — a catalog drag, else an armed click-to-place — and the selection
	 * last. Cabinet sets are wall-only, so a cabinet-set drag or placement has nothing to turn (and does not fall back to the
	 * selection meanwhile).
	 */
	inline EPlannerWheelTarget ResolveWheelTarget(const FPlannerWheelInputs& In)
	{
		if (!In.bPlannerOpen || !In.bIs2D) return EPlannerWheelTarget::None;
		if (!In.bPlannerUIShown || In.bCursorOverPlannerUI) return EPlannerWheelTarget::None;
		if (In.CatalogDragKind != EPlannerPlacementKind::None)
		{
			return In.CatalogDragKind == EPlannerPlacementKind::Object ? EPlannerWheelTarget::CatalogDrag : EPlannerWheelTarget::None;
		}
		if (In.PendingPlacementKind != EPlannerPlacementKind::None)
		{
			return In.PendingPlacementKind == EPlannerPlacementKind::Object ? EPlannerWheelTarget::Pending : EPlannerWheelTarget::None;
		}
		return In.bHasObjectOrSetSelected ? EPlannerWheelTarget::Selection : EPlannerWheelTarget::None;
	}

	/**
	 * Yaw to add for a wheel delta. Precision wheels, touchpads and Pixel Streaming send fractions: they add up in Accum and every
	 * whole notch turns one step (the rest stays in Accum; a change of direction starts counting afresh). Scroll up (Delta > 0)
	 * turns counter-clockwise on the plan (−Step), like «↺ 15°»; bInvert swaps the directions.
	 */
	inline float ConsumeWheelSteps(float& Accum, float Delta, float StepDeg, bool bInvert = false)
	{
		if (Accum * Delta < 0.f) Accum = 0.f;
		Accum += Delta;
		constexpr float Tolerance = 1e-3f; // 0.1 x 10 is one notch, not 0.99999994 of one
		const int32 Notches = FMath::TruncToInt32(Accum + (Accum >= 0.f ? Tolerance : -Tolerance));
		Accum -= (float)Notches;
		if (FMath::Abs(Accum) < Tolerance) Accum = 0.f;
		return (bInvert ? 1.f : -1.f) * StepDeg * (float)Notches;
	}

	/** An angle on the plan as the rotate buttons name it: «↻ 30°» clockwise (positive yaw), «↺ 15°» counter-clockwise, «0°». */
	inline FString FormatPlanAngle(float YawDeg)
	{
		const float Yaw = (float)FRotator::NormalizeAxis(YawDeg);
		const float Abs = FMath::Abs(Yaw);
		const bool bWhole = FMath::IsNearlyEqual(Abs, FMath::RoundToFloat(Abs), 0.05f);
		const FString Number = bWhole ? FString::FromInt(FMath::RoundToInt(Abs)) : FString::Printf(TEXT("%.1f"), Abs);
		if (Number == TEXT("0")) return TEXT("0°");
		return FString::Printf(TEXT("%s %s°"), Yaw > 0.f ? TEXT("↻") : TEXT("↺"), *Number);
	}
}
