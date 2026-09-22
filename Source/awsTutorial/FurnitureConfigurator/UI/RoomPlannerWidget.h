// Copyright 2026 MaxiMall. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Constructor/RoomPlannerTypes.h"
#include "ColorCatalog/ColorCatalogTypes.h"
#include "Widgets/Layout/SScaleBox.h"
#include "RoomPlannerWidget.generated.h"

class ARoomPlannerManager;
class AAwsTutorial_PlayerController;
class UButton;
class UTextBlock;
class UEditableTextBox;
class UImage;
class UWidget;
class UPanelWidget;
class UColorCatalogWidget;
class UPlannerTileCatalogWidget;
class UPlannerCatalogItemWidget;
class UPlannerDimensionOverlay;
class UDragDropOperation;

UENUM(BlueprintType)
enum class ERoomPlannerViewMode : uint8
{
	View2D UMETA(DisplayName = "2D Top-Down View"),
	View3D UMETA(DisplayName = "3D Orbit View")
};

UCLASS()
class AWSTUTORIAL_API URoomPlannerWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	virtual void NativeOnInitialized() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	/**
	 * Parts of the layout built in code: the tile button next to "Отделка", the wrapping finish controls and the dimension line
	 * overlay. Runs from NativeOnInitialized, before the Slate tree exists; safe to call again.
	 */
	void BuildRuntimeLayout();

	/** Overlay the selected opening's dimension lines are drawn on (built by BuildRuntimeLayout). */
	UPlannerDimensionOverlay* GetDimensionOverlay() const { return DimensionOverlay; }

	/** The "Плитка" button built by BuildRuntimeLayout. */
	UButton* GetFinishTileButton() const { return BtnFinishTile; }

	/** The «Каталог» toolbar button built by BuildRuntimeLayout. */
	UButton* GetCatalogToggleButton() const { return BtnCatalogToggle; }

protected:
	virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnMouseButtonUp(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnMouseMove(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;

	/** Catalog cards dropped on this widget: resolved natively (wall / floor rules), no Blueprint nodes needed. */
	virtual bool NativeOnDragOver(const FGeometry& InGeometry, const FDragDropEvent& InDragDropEvent, UDragDropOperation* InOperation) override;
	virtual bool NativeOnDrop(const FGeometry& InGeometry, const FDragDropEvent& InDragDropEvent, UDragDropOperation* InOperation) override;

	bool bIsWidgetDrawingWall = false;

	/** Widget-path 2D drags (REQ-02 control points, REQ-17/18 objects). */
	bool bIsWidgetDraggingNode = false;
	FString WidgetDraggedObjectID;
	bool bWidgetDraggedIsCabinetSet = false;
	FVector WidgetDragOffset = FVector::ZeroVector;

public:

	/** Closes this room planner widget, restores 3D character view, and removes from parent. */
	UFUNCTION(BlueprintCallable, Category = "RoomPlanner")
	void ClosePlanner();

	/** Toggle between 2D Top-Down Drawing Mode and 3D Inspection Mode. */
	UFUNCTION(BlueprintCallable, Category = "RoomPlanner")
	void SetViewMode(ERoomPlannerViewMode NewMode);

	/** Switch active tool mode (Select, Draw Wall, Erase). */
	UFUNCTION(BlueprintCallable, Category = "RoomPlanner")
	void SetToolMode(EPlannerToolMode NewToolMode);

	UFUNCTION(BlueprintCallable, Category = "RoomPlanner")
	void UpdateToolModeButtonStyles();

	UFUNCTION(BlueprintCallable, Category = "RoomPlanner")
	void UpdateViewModeButtonStyles();

	/** Updates contextual guidance hint texts based on current tool, view mode, and selection. */
	UFUNCTION(BlueprintCallable, Category = "RoomPlanner")
	void UpdateGuidanceHintText();

	/** Build default 4x4m square room layout. */
	UFUNCTION(BlueprintCallable, Category = "RoomPlanner")
	void BuildPreset4x4mRoom();

	/** Clear all current walls and procedural geometry. */
	UFUNCTION(BlueprintCallable, Category = "RoomPlanner")
	void ClearLayout();

	/** Insert door on specified wall segment. */
	UFUNCTION(BlueprintCallable, Category = "RoomPlanner")
	void InsertDoor(int32 WallSegmentID, float DistanceAlongWallCm);

	/** Insert window on specified wall segment. */
	UFUNCTION(BlueprintCallable, Category = "RoomPlanner")
	void InsertWindow(int32 WallSegmentID, float DistanceAlongWallCm);

	/** Returns calculated floor area in square meters. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "RoomPlanner")
	float GetFloorAreaM2() const;

	/** Returns total wall perimeter length in meters. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "RoomPlanner")
	float GetPerimeterLengthM() const;

	/** Returns formatted live drag length text (e.g. "6.99 m"). */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "RoomPlanner")
	FString GetFormattedDragLengthText() const;

	/** Called every frame during interactive 2D wall drag for UMG overlays. */
	UFUNCTION(BlueprintImplementableEvent, Category = "RoomPlanner")
	void OnWallDragProgress(float LengthMeters, FVector MidpointWorld, float AngleDeg, bool bIsSnapped);

	// ── REQ-02 / REQ-04 / REQ-06: persistent dimension labels ─────────────

	/**
	 * Fired every frame while something is selected or a control point is dragged.
	 * Labels carry text, world position and a DPI-corrected screen position — place your
	 * text blocks at ScreenPosition to draw the values next to the selected object.
	 */
	UFUNCTION(BlueprintImplementableEvent, Category = "RoomPlanner|Labels")
	void OnSelectionLabelsUpdated(const TArray<FPlannerDimensionLabel>& Labels);

	/** Current labels (same data as OnSelectionLabelsUpdated). */
	UFUNCTION(BlueprintCallable, Category = "RoomPlanner|Labels")
	TArray<FPlannerDimensionLabel> GetSelectionLabels() const;

	/** Fired when the server refuses an operation (e.g. a wall shortened below its openings, REQ-09). */
	UFUNCTION(BlueprintImplementableEvent, Category = "RoomPlanner")
	void OnOperationRejectedMessage(const FString& Reason);

	/** Fired whenever the selected element kind changes (wall / opening / floor / object / cabinet set / none). */
	UFUNCTION(BlueprintImplementableEvent, Category = "RoomPlanner")
	void OnSelectionKindChanged(EPlannerSelectionKind Kind);

	UFUNCTION(BlueprintPure, Category = "RoomPlanner")
	EPlannerSelectionKind GetSelectionKind() const;

	// ── REQ-07: swing of the selected door / window ────────────────────────

	UFUNCTION(BlueprintCallable, Category = "RoomPlanner|Openings")
	void SetSelectedOpeningSwing(EOpeningSwingSide Side, EOpeningSwingDirection Direction);

	UFUNCTION(BlueprintCallable, Category = "RoomPlanner|Openings")
	void SetSelectedSwingSide(EOpeningSwingSide Side);

	UFUNCTION(BlueprintCallable, Category = "RoomPlanner|Openings")
	void SetSelectedSwingDirection(EOpeningSwingDirection Direction);

	UFUNCTION(BlueprintCallable, Category = "RoomPlanner|Openings")
	bool GetSelectedOpeningSwing(EOpeningSwingSide& OutSide, EOpeningSwingDirection& OutDirection) const;

	// ── REQ-13 / REQ-14: finishing ─────────────────────────────────────────

	/** Opens the existing RAL/NCS colour catalog (WBP_ColorCatalog) for the selected wall / floor / object. Each swatch click applies live. */
	UFUNCTION(BlueprintCallable, Category = "RoomPlanner|Finish")
	void OpenPaintCatalogForSelection();

	/** Opens the tile catalog (DT_PlannerTiles cards with preview pictures) for the selected wall face / floor / ceiling / baseboard. */
	UFUNCTION(BlueprintCallable, Category = "RoomPlanner|Finish")
	void OpenTileCatalogForSelection();

	/** Applies a tile (DT_PlannerTiles row name, or a material asset path) to the selected wall / floor. */
	UFUNCTION(BlueprintCallable, Category = "RoomPlanner|Finish")
	bool ApplyTileToSelection(FName TileID);

	/** Applies a ready finish struct to the current selection. */
	UFUNCTION(BlueprintCallable, Category = "RoomPlanner|Finish")
	bool ApplyFinishToSelection(const FSurfaceFinish& Finish);

	UFUNCTION(BlueprintCallable, Category = "RoomPlanner|Finish")
	void ClearFinishOnSelection();

	UFUNCTION(BlueprintCallable, Category = "RoomPlanner|Finish")
	TArray<FPlannerCatalogEntry> GetAvailableTiles() const;

	UFUNCTION(BlueprintCallable, Category = "RoomPlanner|Finish")
	bool GetSelectedSurfaceFinish(FSurfaceFinish& OutFinish) const;

	/** Human readable finish of the selection ("Краска RAL 3020", "Плитка 30×30 …", "—"). */
	UFUNCTION(BlueprintCallable, Category = "RoomPlanner|Finish")
	FString GetSelectedFinishText() const;

	/** Net finishing areas per finish (openings subtracted), REQ-14. */
	UFUNCTION(BlueprintCallable, Category = "RoomPlanner|Finish")
	TArray<FFinishAreaEntry> GetFinishAreas() const;

	UFUNCTION(BlueprintCallable, Category = "RoomPlanner|Finish")
	FString GetFinishAreaSummaryText() const;

	// ── REQ-17 / REQ-18: objects and cabinet sets ──────────────────────────

	UFUNCTION(BlueprintCallable, Category = "RoomPlanner|Objects")
	TArray<FPlannerCatalogEntry> GetAvailableObjects() const;

	UFUNCTION(BlueprintCallable, Category = "RoomPlanner|Objects")
	TArray<FPlannerCatalogEntry> GetAvailableCabinetSets() const;

	/** Arms click-to-place: the next LMB click on the plan (2D) or on the floor (3D) places the object. */
	UFUNCTION(BlueprintCallable, Category = "RoomPlanner|Objects")
	void BeginPlaceObject(const FString& AssetID);

	/** Arms click-to-place for a cabinet set (DT_FurnitureCatalog row name). */
	UFUNCTION(BlueprintCallable, Category = "RoomPlanner|Objects")
	void BeginPlaceCabinetSet(FName ProductID);

	UFUNCTION(BlueprintCallable, Category = "RoomPlanner|Objects")
	void CancelPlacement();

	/**
	 * Drag-and-drop entry point (call from your OnDrop): places the item under the cursor.
	 * Kind = Object (DT_PlannerObjects row name → wall or floor, decided by the drop target) or
	 * CabinetSet (DT_FurnitureCatalog row name → walls only). Returns false and shows a message when invalid.
	 */
	UFUNCTION(BlueprintCallable, Category = "RoomPlanner|Objects")
	bool DropCatalogItemUnderCursor(EPlannerPlacementKind Kind, const FString& ItemID);

	/** Drop at an explicit Slate screen-space position (drag/drop event position). */
	UFUNCTION(BlueprintCallable, Category = "RoomPlanner|Objects")
	bool DropCatalogItemAtScreenPosition(EPlannerPlacementKind Kind, const FString& ItemID, FVector2D ScreenSpacePosition);

	UFUNCTION(BlueprintCallable, Category = "RoomPlanner|Objects")
	bool DropObjectUnderCursor(const FString& AssetID) { return DropCatalogItemUnderCursor(EPlannerPlacementKind::Object, AssetID); }

	UFUNCTION(BlueprintCallable, Category = "RoomPlanner|Objects")
	bool DropCabinetSetUnderCursor(FName ProductID) { return DropCatalogItemUnderCursor(EPlannerPlacementKind::CabinetSet, ProductID.ToString()); }

	// ── Catalog: one content area, tabs «Интерьер» (DT_PlannerObjects) / «Тумбы» (DT_FurnitureCatalog) ──

	/** Rebuilds the catalog content area for the active tab (called on construct and on every tab switch). */
	UFUNCTION(BlueprintCallable, Category = "RoomPlanner|Objects")
	void RefreshCatalogPanels();

	/** Card class used for catalog items. Defaults to the C++ UPlannerCatalogItemWidget (self-drawing). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RoomPlanner|Objects")
	TSubclassOf<UPlannerCatalogItemWidget> CatalogItemWidgetClass;

	/** THE common catalog content area (same role as Size_Container in WBP_PreviewWindow). C++ builds a scrollable grid of cards inside it. */
	UPROPERTY(meta = (BindWidgetOptional), BlueprintReadOnly, Category = "RoomPlanner|UI")
	TObjectPtr<UPanelWidget> Catalog_Container;

	/** Optional container of the two tab buttons (hidden in 3D together with the content area). */
	UPROPERTY(meta = (BindWidgetOptional), BlueprintReadOnly, Category = "RoomPlanner|UI")
	TObjectPtr<UWidget> CatalogTabBar;

	UPROPERTY(meta = (BindWidgetOptional), BlueprintReadOnly, Category = "RoomPlanner|UI")
	TObjectPtr<UButton> BtnCatalogInterior;

	UPROPERTY(meta = (BindWidgetOptional), BlueprintReadOnly, Category = "RoomPlanner|UI")
	TObjectPtr<UButton> BtnCatalogCabinets;

	// ── Root-level styling, same pattern as WBP_PreviewWindow's "UI Sizing - Size" (ConfiguratorMainWidget) ──

	/** Tab shown first when bOpenCatalogTabOnStart is set. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MaxiMall | UI Sizing - Catalog")
	EPlannerPlacementKind DefaultCatalogTab = EPlannerPlacementKind::Object;

	/**
	 * Opens the catalog with DefaultCatalogTab when the planner opens. Off: the catalog (tabs and cards) stays folded until the
	 * «Каталог» toolbar button opens it, and no tab is active until one is clicked.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MaxiMall | UI Sizing - Catalog")
	bool bOpenCatalogTabOnStart = false;

	/** Tab button colours — same semantics and defaults as the RAL / NCS selector (UColorCatalogWidget). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MaxiMall | UI Sizing - Catalog")
	FLinearColor ActiveTabColor = FLinearColor(0.04f, 0.52f, 1.0f, 1.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MaxiMall | UI Sizing - Catalog")
	FLinearColor InactiveTabColor = FLinearColor(0.07f, 0.11f, 0.18f, 1.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MaxiMall | UI Sizing - Catalog")
	float CatalogButtonWidth = 100.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MaxiMall | UI Sizing - Catalog")
	float CatalogButtonHeight = 110.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MaxiMall | UI Sizing - Catalog")
	FMargin CatalogButtonPadding = FMargin(4.f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MaxiMall | UI Sizing - Catalog")
	TEnumAsByte<EStretch::Type> CatalogImageStretch = EStretch::ScaleToFit;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MaxiMall | UI Sizing - Catalog")
	float CatalogGridSlotPadding = 5.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MaxiMall | UI Sizing - Catalog")
	int32 CatalogColumns = 2;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MaxiMall | UI Sizing - Catalog")
	float CatalogContainerHeight = 255.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MaxiMall | UI Sizing - Catalog")
	FSlateColor CatalogButtonNormalColor = FSlateColor(FLinearColor(1.f, 1.f, 1.f, 0.05f));

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MaxiMall | UI Sizing - Catalog")
	FSlateColor CatalogButtonHoveredColor = FSlateColor(FLinearColor(1.f, 1.f, 1.f, 0.15f));

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MaxiMall | UI Sizing - Catalog")
	FSlateColor CatalogButtonPressedColor = FSlateColor(FLinearColor(1.f, 1.f, 1.f, 0.25f));

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MaxiMall | UI Sizing - Catalog")
	FSlateColor CatalogTextColor = FSlateColor(FLinearColor::White);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MaxiMall | UI Sizing - Catalog")
	FSlateFontInfo CatalogTextFont;

	// ── Dimension lines of the selected door / window (REQ-06) ──

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MaxiMall | UI Sizing - Dimensions")
	FLinearColor DimensionLineColor = FLinearColor(0.02f, 0.05f, 0.12f, 1.f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MaxiMall | UI Sizing - Dimensions")
	FLinearColor DimensionTextColor = FLinearColor(0.02f, 0.05f, 0.12f, 1.f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MaxiMall | UI Sizing - Dimensions")
	FLinearColor DimensionTextBackgroundColor = FLinearColor(1.f, 1.f, 1.f, 0.95f);

	/** Font of the values on the dimension lines. Empty: Roboto Bold 11. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MaxiMall | UI Sizing - Dimensions")
	FSlateFontInfo DimensionFont;

	/** Switches the catalog content (Object = «Интерьер», CabinetSet = «Тумбы») and the tab button styles; opens the card area. */
	UFUNCTION(BlueprintCallable, Category = "RoomPlanner|Objects")
	void SetActiveCatalogTab(EPlannerPlacementKind Tab);

	/** Tab of the card area; meaningful only while HasActiveCatalogTab() is true. */
	UFUNCTION(BlueprintPure, Category = "RoomPlanner|Objects")
	EPlannerPlacementKind GetActiveCatalogTab() const { return ActiveCatalogTab; }

	/** False until the user picks «Интерьер» or «Тумбы»: no tab is highlighted and the card area stays folded. */
	UFUNCTION(BlueprintPure, Category = "RoomPlanner|Objects")
	bool HasActiveCatalogTab() const { return bCatalogTabChosen; }

	/** Opens or folds the catalog section (the «Интерьер» / «Тумбы» tabs and the cards), as the «Каталог» toolbar button does. */
	UFUNCTION(BlueprintCallable, Category = "RoomPlanner|Objects")
	void SetCatalogOpen(bool bOpen);

	UFUNCTION(BlueprintPure, Category = "RoomPlanner|Objects")
	bool IsCatalogOpen() const { return bCatalogOpen; }

	/** Shows / hides the tab bar and content area for the current view mode (2D only), then updates the separators. */
	UFUNCTION(BlueprintCallable, Category = "RoomPlanner|Objects")
	void ApplyCatalogSectionVisibility();

	UFUNCTION(BlueprintCallable, Category = "RoomPlanner|Objects")
	void UpdateCatalogTabStyles();

	/**
	 * Global separator rule for every panel that contains Image_line* widgets. Every run of children between
	 * two consecutive lines (plus the run above the first and below the last line) is one section. A section
	 * is visible when any descendant leaf is effectively visible (Collapsed and Hidden both count as not
	 * visible). Every visible section requires the line directly above and below it; a required line whose
	 * nearest visible predecessor is another line is dropped, so there is never a doubled or dangling line.
	 * Runs every tick (cheap) so it also covers visibility changes made anywhere else.
	 */
	UFUNCTION(BlueprintCallable, Category = "RoomPlanner|UI")
	void UpdateSeparatorLines();

	/** Called by a card when its drag was released without any widget accepting it (plan area). */
	void HandleCatalogDragReleased(EPlannerPlacementKind Kind, const FString& ItemID, const FVector2D& ScreenSpacePosition);

	/** True when the screen-space point is over the catalog content area (a release there is not a placement). */
	bool IsScreenPositionOverCatalogPanels(const FVector2D& ScreenSpacePosition) const;

	/** Rotates the selected object / cabinet set around Z by DeltaYawDeg and commits it. */
	UFUNCTION(BlueprintCallable, Category = "RoomPlanner|Objects")
	void RotateSelected(float DeltaYawDeg);

	/** Deletes whatever is selected (opening, wall, object or cabinet set). */
	UFUNCTION(BlueprintCallable, Category = "RoomPlanner")
	void DeleteSelected();

	/** 3D mode: selects the planner element under the cursor. */
	UFUNCTION(BlueprintCallable, Category = "RoomPlanner")
	EPlannerSelectionKind PickSurfaceUnderCursor();

	/** Current View Mode. */
	UPROPERTY(BlueprintReadOnly, Category = "RoomPlanner")
	ERoomPlannerViewMode CurrentViewMode = ERoomPlannerViewMode::View3D;

	UPROPERTY(BlueprintReadOnly, Category = "RoomPlanner")
	float CurrentDragLengthMeters = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "RoomPlanner")
	FVector CurrentDragMidpointWorld = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "RoomPlanner")
	bool bIsAngleSnapped = false;

	/** Colour catalog widget class used for paint finishing. Falls back to /Game/ColorCatalog/UI/WBP_ColorCatalog. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RoomPlanner|Finish")
	TSubclassOf<UColorCatalogWidget> PlannerColorCatalogWidgetClass;

	/** Tile catalog widget class. Empty = UPlannerTileCatalogWidget (layout built in code). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RoomPlanner|Finish")
	TSubclassOf<UPlannerTileCatalogWidget> PlannerTileCatalogWidgetClass;

	// ── CONFIGURABLE CHARACTER RELOCATION ─────────────────────────────────
	/** Configurable spawn/relocation location when Room Planner opens (Default: -10000, 0, 0). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RoomPlanner | Relocation Config", meta = (DisplayName = "Planner Relocation Location"))
	FVector PlannerRelocationLocation = FVector(-10000.f, 0.f, 0.f);

	/** Cached location of the character before opening the planner. */
	UPROPERTY(BlueprintReadOnly, Category = "RoomPlanner | Relocation")
	FVector CachedOriginalPlayerLocation = FVector::ZeroVector;

	/** Cached rotation of the character before opening the planner. */
	UPROPERTY(BlueprintReadOnly, Category = "RoomPlanner | Relocation")
	FRotator CachedOriginalPlayerRotation = FRotator::ZeroRotator;

	/** Cached control rotation of the player controller before opening the planner. */
	UPROPERTY(BlueprintReadOnly, Category = "RoomPlanner | Relocation")
	FRotator CachedOriginalControlRotation = FRotator::ZeroRotator;

	/** True if original transform has been cached for restoration upon closing. */
	UPROPERTY(BlueprintReadOnly, Category = "RoomPlanner | Relocation")
	bool bHasCachedPlayerTransform = false;

	// ── AUTOMATIC BIND WIDGETS (Matching UMG Designer Names) ───────────
	UPROPERTY(meta = (BindWidgetOptional), BlueprintReadOnly, Category = "RoomPlanner|UI")
	TObjectPtr<UButton> Btn2DView;

	UPROPERTY(meta = (BindWidgetOptional), BlueprintReadOnly, Category = "RoomPlanner|UI")
	TObjectPtr<UButton> Btn_2DView;

	UPROPERTY(meta = (BindWidgetOptional), BlueprintReadOnly, Category = "RoomPlanner|UI")
	TObjectPtr<UButton> Btn3DView;

	UPROPERTY(meta = (BindWidgetOptional), BlueprintReadOnly, Category = "RoomPlanner|UI")
	TObjectPtr<UButton> Btn_3DView;

	UPROPERTY(meta = (BindWidgetOptional), BlueprintReadOnly, Category = "RoomPlanner|UI")
	TObjectPtr<UButton> BtnSelectTool;

	UPROPERTY(meta = (BindWidgetOptional), BlueprintReadOnly, Category = "RoomPlanner|UI")
	TObjectPtr<UButton> BtnDrawWallTool;

	UPROPERTY(meta = (BindWidgetOptional), BlueprintReadOnly, Category = "RoomPlanner|UI")
	TObjectPtr<UButton> BtnDeleteTool;

	UPROPERTY(meta = (BindWidgetOptional), BlueprintReadOnly, Category = "RoomPlanner|UI")
	TObjectPtr<UButton> BtnAddDoor;

	UPROPERTY(meta = (BindWidgetOptional), BlueprintReadOnly, Category = "RoomPlanner|UI")
	TObjectPtr<UButton> BtnAddWindow;

	UPROPERTY(meta = (BindWidgetOptional), BlueprintReadOnly, Category = "RoomPlanner|UI")
	TObjectPtr<UButton> BtnPresetRoom;

	UPROPERTY(meta = (BindWidgetOptional), BlueprintReadOnly, Category = "RoomPlanner|UI")
	TObjectPtr<UButton> BtnClearLayout;

	UPROPERTY(meta = (BindWidgetOptional), BlueprintReadOnly, Category = "RoomPlanner|UI")
	TObjectPtr<UButton> BtnToggleCeiling;

	UPROPERTY(meta = (BindWidgetOptional), BlueprintReadOnly, Category = "RoomPlanner|UI")
	TObjectPtr<UButton> Btn_ToggleCeiling;

	UPROPERTY(meta = (BindWidgetOptional), BlueprintReadOnly, Category = "RoomPlanner|UI")
	TObjectPtr<UButton> BtnCeiling;

	UPROPERTY(meta = (BindWidgetOptional), BlueprintReadOnly, Category = "RoomPlanner|UI")
	TObjectPtr<UButton> BtnClose;

	UPROPERTY(meta = (BindWidgetOptional), BlueprintReadOnly, Category = "RoomPlanner|UI")
	TObjectPtr<UButton> BtnBack;

	UPROPERTY(meta = (BindWidgetOptional), BlueprintReadOnly, Category = "RoomPlanner|UI")
	TObjectPtr<UTextBlock> TxtLiveLength;

	/** Optional frame around TxtLiveLength; shown only while a wall is being drawn. */
	UPROPERTY(meta = (BindWidgetOptional), BlueprintReadOnly, Category = "RoomPlanner|UI")
	TObjectPtr<UWidget> LiveLengthPanel;

	UPROPERTY(meta = (BindWidgetOptional), BlueprintReadOnly, Category = "RoomPlanner|UI")
	TObjectPtr<UTextBlock> TxtFloorArea;

	UPROPERTY(meta = (BindWidgetOptional), BlueprintReadOnly, Category = "RoomPlanner|UI")
	TObjectPtr<UTextBlock> TxtPerimeter;

	UPROPERTY(meta = (BindWidgetOptional), BlueprintReadOnly, Category = "RoomPlanner|UI")
	TObjectPtr<UTextBlock> TxtGuidanceHint;

	UPROPERTY(meta = (BindWidgetOptional), BlueprintReadOnly, Category = "RoomPlanner|UI")
	TObjectPtr<UWidget> SnapIndicator;

	UPROPERTY(meta = (BindWidgetOptional), BlueprintReadOnly, Category = "RoomPlanner|UI")
	TObjectPtr<UImage> mouse_cursor;

	void UpdateMouseCursorPosition();

	// --- Dynamic Properties Panel ---
	UPROPERTY(meta = (BindWidgetOptional), BlueprintReadOnly, Category = "RoomPlanner|UI")
	TObjectPtr<UEditableTextBox> EditableTxtProp1; // Wall Length OR Opening Width

	UPROPERTY(meta = (BindWidgetOptional), BlueprintReadOnly, Category = "RoomPlanner|UI")
	TObjectPtr<UEditableTextBox> EditableTxtProp2; // Opening Height

	UPROPERTY(meta = (BindWidgetOptional), BlueprintReadOnly, Category = "RoomPlanner|UI")
	TObjectPtr<UEditableTextBox> EditableTxtProp3; // Window Sill Height

	UPROPERTY(meta = (BindWidgetOptional), BlueprintReadOnly, Category = "RoomPlanner|UI")
	TObjectPtr<UButton> BtnApplyProperties; // Replaces 'wall_size' button

	UPROPERTY(meta = (BindWidgetOptional), BlueprintReadOnly, Category = "RoomPlanner|UI")
	TObjectPtr<UTextBlock> TxtApplyProperties; // Text inside BtnApplyProperties

	// --- Section containers of the selection block (collapsed together with their content so the section can empty) ---
	UPROPERTY(meta = (BindWidgetOptional), BlueprintReadOnly, Category = "RoomPlanner|UI")
	TObjectPtr<UWidget> Border_wall_size;

	/** Caption above EditableTxtProp1..3; text follows the selection (wall / door / window). */
	UPROPERTY(meta = (BindWidgetOptional), BlueprintReadOnly, Category = "RoomPlanner|UI")
	TObjectPtr<UTextBlock> LblWallSize;

	/** Container of the floor-area / perimeter rows; shown only in 2D and only when at least one wall exists. */
	UPROPERTY(meta = (BindWidgetOptional), BlueprintReadOnly, Category = "RoomPlanner|UI")
	TObjectPtr<UWidget> TotalsBox;

	UPROPERTY(meta = (BindWidgetOptional), BlueprintReadOnly, Category = "RoomPlanner|UI")
	TObjectPtr<UWidget> Border_AddDoor;

	UPROPERTY(meta = (BindWidgetOptional), BlueprintReadOnly, Category = "RoomPlanner|UI")
	TObjectPtr<UWidget> Border_AddWindow;

	// --- Creation Tools ---
	UPROPERTY(meta = (BindWidgetOptional), BlueprintReadOnly, Category = "RoomPlanner|UI")
	TObjectPtr<UEditableTextBox> EditableTxtOpeningWidth;

	UPROPERTY(meta = (BindWidgetOptional), BlueprintReadOnly, Category = "RoomPlanner|UI")
	TObjectPtr<UEditableTextBox> EditableTxtOpeningHeight;

	UPROPERTY(meta = (BindWidgetOptional), BlueprintReadOnly, Category = "RoomPlanner|UI")
	TObjectPtr<UEditableTextBox> EditableTxtOpeningWidth_1;

	UPROPERTY(meta = (BindWidgetOptional), BlueprintReadOnly, Category = "RoomPlanner|UI")
	TObjectPtr<UEditableTextBox> EditableTxtOpeningHeight_1;

	UPROPERTY(meta = (BindWidgetOptional), BlueprintReadOnly, Category = "RoomPlanner|UI")
	TObjectPtr<UEditableTextBox> EditableTxtOpeningSillHeight;

	UPROPERTY(meta = (BindWidgetOptional), BlueprintReadOnly, Category = "RoomPlanner|UI")
	TObjectPtr<UImage> Image_1;

	UPROPERTY(meta = (BindWidgetOptional), BlueprintReadOnly, Category = "RoomPlanner|UI")
	TObjectPtr<UImage> Image_2;

	// --- REQ-04 / REQ-06: selection value labels (optional; the BP event carries the same data) ---
	/** Container positioned at the selected object's screen position (e.g. a VerticalBox with the TxtSel* blocks). */
	UPROPERTY(meta = (BindWidgetOptional), BlueprintReadOnly, Category = "RoomPlanner|UI")
	TObjectPtr<UWidget> SelectionLabelPanel;

	/** First row of the selection section: TxtSelectionTitle (left) + BtnDeleteTool (right). Shown only while something is selected in 2D. */
	UPROPERTY(meta = (BindWidgetOptional), BlueprintReadOnly, Category = "RoomPlanner|UI")
	TObjectPtr<UWidget> SelectionHeaderRow;

	/** Type / name of the current selection (wall, door, window, floor, object or cabinet-set name). */
	UPROPERTY(meta = (BindWidgetOptional), BlueprintReadOnly, Category = "RoomPlanner|UI")
	TObjectPtr<UTextBlock> TxtSelectionTitle;

	/** Help bar (TxtGuidanceHint + BtnHideHelp). Hidden by BtnHideHelp, shown again by BtnHelp. */
	UPROPERTY(meta = (BindWidgetOptional), BlueprintReadOnly, Category = "RoomPlanner|UI")
	TObjectPtr<UWidget> HorizontalBox_3;

	UPROPERTY(meta = (BindWidgetOptional), BlueprintReadOnly, Category = "RoomPlanner|UI")
	TObjectPtr<UButton> BtnHelp;

	UPROPERTY(meta = (BindWidgetOptional), BlueprintReadOnly, Category = "RoomPlanner|UI")
	TObjectPtr<UButton> BtnHideHelp;

	UPROPERTY(meta = (BindWidgetOptional), BlueprintReadOnly, Category = "RoomPlanner|UI")
	TObjectPtr<UTextBlock> TxtSelectedDims;   // "2.45 м" wall length / "0.90 × 2.10 м" opening / area

	UPROPERTY(meta = (BindWidgetOptional), BlueprintReadOnly, Category = "RoomPlanner|UI")
	TObjectPtr<UTextBlock> TxtDistLeft;

	UPROPERTY(meta = (BindWidgetOptional), BlueprintReadOnly, Category = "RoomPlanner|UI")
	TObjectPtr<UTextBlock> TxtDistRight;

	UPROPERTY(meta = (BindWidgetOptional), BlueprintReadOnly, Category = "RoomPlanner|UI")
	TObjectPtr<UTextBlock> TxtDistFloor;

	UPROPERTY(meta = (BindWidgetOptional), BlueprintReadOnly, Category = "RoomPlanner|UI")
	TObjectPtr<UTextBlock> TxtDistNeighbor;

	// --- REQ-07: swing controls (visible only when a door / window is selected) ---
	UPROPERTY(meta = (BindWidgetOptional), BlueprintReadOnly, Category = "RoomPlanner|UI")
	TObjectPtr<UButton> BtnSwingLeft;

	UPROPERTY(meta = (BindWidgetOptional), BlueprintReadOnly, Category = "RoomPlanner|UI")
	TObjectPtr<UButton> BtnSwingRight;

	UPROPERTY(meta = (BindWidgetOptional), BlueprintReadOnly, Category = "RoomPlanner|UI")
	TObjectPtr<UButton> BtnSwingInward;

	UPROPERTY(meta = (BindWidgetOptional), BlueprintReadOnly, Category = "RoomPlanner|UI")
	TObjectPtr<UButton> BtnSwingOutward;

	/** Optional row (caption + swing buttons); follows the swing buttons' visibility so its caption folds with them. */
	UPROPERTY(meta = (BindWidgetOptional), BlueprintReadOnly, Category = "RoomPlanner|UI")
	TObjectPtr<UWidget> SwingRow;

	/**
	 * Optional container (e.g. a Wrap Box under SwingRow) for the door / window / archway style buttons. The buttons are created
	 * in code from the built-in style catalog; the row is visible only while an opening is selected in 2D.
	 */
	UPROPERTY(meta = (BindWidgetOptional), BlueprintReadOnly, Category = "RoomPlanner|UI")
	TObjectPtr<UPanelWidget> StyleRow;

	/** Builds (once per opening type) and highlights the style buttons of the selected opening. */
	void RefreshStyleRow();
	void SetSelectedOpeningStyle(FName StyleID);
	void OnStyleButtonClicked(FName StyleID);

	/** Opening type the StyleRow buttons were built for ("" = none). */
	FString StyleRowBuiltKey;

	// --- 3D view options (optional, visible only in 3D) ---
	/** Opens every door and window leaf (local view state, not saved). */
	UPROPERTY(meta = (BindWidgetOptional), BlueprintReadOnly, Category = "RoomPlanner|UI")
	TObjectPtr<UButton> BtnDoorsOpen;

	/** Closes every door and window leaf (local view state, not saved). */
	UPROPERTY(meta = (BindWidgetOptional), BlueprintReadOnly, Category = "RoomPlanner|UI")
	TObjectPtr<UButton> BtnDoorsClose;

	/** Optional row holding the 3D view option buttons; follows their visibility. */
	UPROPERTY(meta = (BindWidgetOptional), BlueprintReadOnly, Category = "RoomPlanner|UI")
	TObjectPtr<UWidget> ViewOptionsRow;

	UFUNCTION()
	void OnDoorsOpenClicked();

	UFUNCTION()
	void OnDoorsCloseClicked();

	/** Optional row (caption + rotate buttons); follows the rotate buttons' visibility. */
	UPROPERTY(meta = (BindWidgetOptional), BlueprintReadOnly, Category = "RoomPlanner|UI")
	TObjectPtr<UWidget> RotateRow;

	// --- REQ-13 / REQ-14: finishing controls ---
	UPROPERTY(meta = (BindWidgetOptional), BlueprintReadOnly, Category = "RoomPlanner|UI")
	TObjectPtr<UButton> BtnFinishPaint;     // opens the RAL/NCS catalog for the selection

	UPROPERTY(meta = (BindWidgetOptional), BlueprintReadOnly, Category = "RoomPlanner|UI")
	TObjectPtr<UButton> BtnClearFinish;

	UPROPERTY(meta = (BindWidgetOptional), BlueprintReadOnly, Category = "RoomPlanner|UI")
	TObjectPtr<UTextBlock> TxtFinishInfo;   // finish of the selection

	UPROPERTY(meta = (BindWidgetOptional), BlueprintReadOnly, Category = "RoomPlanner|UI")
	TObjectPtr<UTextBlock> TxtFinishAreas;  // REQ-14 summary

	// --- REQ-17 / REQ-18 ---
	UPROPERTY(meta = (BindWidgetOptional), BlueprintReadOnly, Category = "RoomPlanner|UI")
	TObjectPtr<UButton> BtnRotateLeft;

	UPROPERTY(meta = (BindWidgetOptional), BlueprintReadOnly, Category = "RoomPlanner|UI")
	TObjectPtr<UButton> BtnRotateRight;

	UPROPERTY(meta = (BindWidgetOptional), BlueprintReadOnly, Category = "RoomPlanner|UI")
	TObjectPtr<UButton> BtnCancelPlacement;

	/** Shows the last rejected-operation reason for a few seconds. */
	UPROPERTY(meta = (BindWidgetOptional), BlueprintReadOnly, Category = "RoomPlanner|UI")
	TObjectPtr<UTextBlock> TxtOperationMessage;

protected:
	UPROPERTY(BlueprintReadOnly, Category = "RoomPlanner")
	TObjectPtr<ARoomPlannerManager> PlannerManager;

	UFUNCTION()
	void HandleWallDragProgress(float LengthMeters, FVector MidpointWorld, float AngleDeg, bool bIsSnapped);

	UFUNCTION()
	void HandleRoomPlannerUpdated(const FString& JSONState);

	UFUNCTION()
	void HandleOperationRejected(const FString& Reason);

	UFUNCTION()
	void HandleSelectionChanged();

	/** A room floor picked or let go: the floor area line follows it. */
	UFUNCTION()
	void HandleFloorSelected(int32 RoomID, float AreaM2);

	UFUNCTION()
	void HandlePaintColorItemSelected(const FColorCatalogItem& Item);

	UFUNCTION()
	void HandlePaintCatalogClosed();

	AAwsTutorial_PlayerController* GetPreviewController() const;

	void BindManagerDelegates();
	void UnbindManagerDelegates();
	void UpdateSelectionLabelsUI();
	void UpdateFinishUI();
	bool DeprojectCursorToGround(FVector& OutGroundPos) const;

private:
	UFUNCTION()
	void On2DViewClicked();

	UFUNCTION()
	void On3DViewClicked();

	UFUNCTION()
	void OnSelectToolClicked();

	UFUNCTION()
	void OnDrawWallToolClicked();

	UFUNCTION()
	void OnDeleteToolClicked();

	UFUNCTION()
	void OnAddDoorClicked();

	UFUNCTION()
	void OnAddWindowClicked();

	UFUNCTION()
	void OnPresetRoomClicked();

	UFUNCTION() void OnClearLayoutClicked();
	UFUNCTION() void OnToggleCeilingClicked();
	UFUNCTION() void OnCloseClicked();

	UFUNCTION() void OnSwingLeftClicked();
	UFUNCTION() void OnSwingRightClicked();
	UFUNCTION() void OnSwingInwardClicked();
	UFUNCTION() void OnSwingOutwardClicked();
	/**
	 * "Плитка" button next to BtnFinishPaint ("Отделка") in its row, built in code with the same style when the widget initializes:
	 * opens the tile catalog. Shown while a surface that takes tiles is selected (REQ-13).
	 */
	UPROPERTY(Transient)
	TObjectPtr<UButton> BtnFinishTile;

	void CreateTileCatalogButton();

	/**
	 * The finish row was one line as wide as its content, so "Отделка", "Плитка", "Сбросить отделку" and the finish description
	 * ran past the side panel. The buttons go into a wrap box that breaks at the panel width; the description gets its own
	 * line below them and wraps.
	 */
	void WrapFinishControls();

	/**
	 * «Каталог» button at the end of the tool row ("Создать стену", "Выбрать", "4×4 м"), styled like "4×4 м": opens and folds the
	 * catalog section. The row becomes a wrap box so the buttons break onto a second line instead of running past the side panel.
	 */
	void CreateCatalogToggleButton();

	UFUNCTION() void OnCatalogToggleClicked();

	UPROPERTY(Transient)
	TObjectPtr<UButton> BtnCatalogToggle;

	/** Background of the «Каталог» button while the catalog is folded (the style it was copied from). */
	FLinearColor CatalogToggleIdleColor = FLinearColor(0.17f, 0.17f, 0.18f, 1.f);

	/** The caption before the floor area ("Площадь пола" in the WBP) and its designed text. */
	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> FloorAreaCaption;
	FText FloorAreaCaptionText;

	/** Full-screen, hit-test invisible overlay under the side panel (first child of the root canvas). */
	void CreateDimensionOverlay();

	/** Projects the selected opening's dimension lines to the screen and hands them to the overlay (cleared otherwise). */
	void UpdateDimensionOverlay();

	UPROPERTY(Transient)
	TObjectPtr<UPlannerDimensionOverlay> DimensionOverlay;

	/** True when the selection can take a tile: a wall face, floor, ceiling or baseboard. */
	bool CanTileSelection() const;
	/** Tile row of the selected surface's finish; None when it has no tile. */
	FName GetSelectedSurfaceTileID() const;

	UFUNCTION() void OnFinishTileClicked();
	UFUNCTION() void HandleTileChosen(FName TileID);
	UFUNCTION() void HandleTileCatalogClosed();

	UFUNCTION() void OnFinishPaintClicked();
	UFUNCTION() void OnClearFinishClicked();
	UFUNCTION() void OnRotateLeftClicked();
	UFUNCTION() void OnRotateRightClicked();
	UFUNCTION() void OnCancelPlacementClicked();
	UFUNCTION() void OnCatalogInteriorClicked();
	UFUNCTION() void OnCatalogCabinetsClicked();
	UFUNCTION() void OnHelpShowClicked();
	UFUNCTION() void OnHelpHideClicked();

	/** Wall / door / window / floor / object or cabinet-set display name; empty when nothing is selected. */
	FString GetSelectionTitleText() const;

	/** True after BtnHideHelp until BtnHelp is pressed again. */
	bool bHelpBarHidden = false;

	EPlannerPlacementKind ActiveCatalogTab = EPlannerPlacementKind::Object;

	/** A catalog tab has been picked since the planner opened (see HasActiveCatalogTab). */
	bool bCatalogTabChosen = false;

	/** The catalog section is open (see SetCatalogOpen). */
	bool bCatalogOpen = false;

	/** Image_line_* widgets found in the tree at construct, grouped by their parent panel. */
	TArray<TWeakObjectPtr<UWidget>> SeparatorLines;
	void CollectSeparatorLines();

	UFUNCTION()
	void OnApplyPropertiesClicked();

	UFUNCTION()
	void OnWallSelected(int32 SegmentID, float LengthMeters);

	void UpdateDynamicPropertiesPanel();

	UFUNCTION()
	void OnWallLengthCommitted(const FText& Text, ETextCommit::Type CommitMethod);

	UFUNCTION()
	void OnOpeningWidthCommitted(const FText& Text, ETextCommit::Type CommitMethod);

	UFUNCTION()
	void OnOpeningHeightCommitted(const FText& Text, ETextCommit::Type CommitMethod);

	UFUNCTION()
	void OnOpeningSillHeightCommitted(const FText& Text, ETextCommit::Type CommitMethod);

	void UpdateSummaryStatsUI();

	UPROPERTY()
	TObjectPtr<UColorCatalogWidget> ActivePlannerColorCatalog;

	/** Visibility of this widget before the RAL/NCS catalog collapsed it; restored on close instead of forcing Visible. */
	ESlateVisibility VisibilityBeforePaintCatalog = ESlateVisibility::Visible;

	UPROPERTY()
	TObjectPtr<UPlannerTileCatalogWidget> ActivePlannerTileCatalog;

	/** Visibility of this widget before the tile catalog collapsed it. */
	ESlateVisibility VisibilityBeforeTileCatalog = ESlateVisibility::Visible;

	float OperationMessageClearTime = 0.f;
	EPlannerSelectionKind LastNotifiedSelectionKind = EPlannerSelectionKind::None;
	bool bManagerDelegatesBound = false;
};
