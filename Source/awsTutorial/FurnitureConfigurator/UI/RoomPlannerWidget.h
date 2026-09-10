// Copyright 2026 MaxiMall. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Constructor/RoomPlannerTypes.h"
#include "ColorCatalog/ColorCatalogTypes.h"
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
class UPlannerCatalogItemWidget;
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
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

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

	// ── Catalog sections (DT_PlannerObjects / DT_FurnitureCatalog) ─────────

	/** Rebuilds both catalog panels from the DataTables (called automatically on construct). */
	UFUNCTION(BlueprintCallable, Category = "RoomPlanner|Objects")
	void RefreshCatalogPanels();

	/** Card class used for catalog items. Defaults to the C++ UPlannerCatalogItemWidget (self-drawing). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RoomPlanner|Objects")
	TSubclassOf<UPlannerCatalogItemWidget> CatalogItemWidgetClass;

	/** Container filled with DT_PlannerObjects cards (WrapBox recommended). */
	UPROPERTY(meta = (BindWidgetOptional), BlueprintReadOnly, Category = "RoomPlanner|UI")
	TObjectPtr<UPanelWidget> PanelPlannerObjects;

	/** Container filled with DT_FurnitureCatalog cards (WrapBox recommended). */
	UPROPERTY(meta = (BindWidgetOptional), BlueprintReadOnly, Category = "RoomPlanner|UI")
	TObjectPtr<UPanelWidget> PanelCabinetSets;

	/** Called by a card when its drag was released without any widget accepting it (plan area). */
	void HandleCatalogDragReleased(EPlannerPlacementKind Kind, const FString& ItemID, const FVector2D& ScreenSpacePosition);

	/** True when the screen-space point is over one of the catalog panels (a release there is not a placement). */
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
	UFUNCTION() void OnFinishPaintClicked();
	UFUNCTION() void OnClearFinishClicked();
	UFUNCTION() void OnRotateLeftClicked();
	UFUNCTION() void OnRotateRightClicked();
	UFUNCTION() void OnCancelPlacementClicked();

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

	float OperationMessageClearTime = 0.f;
	EPlannerSelectionKind LastNotifiedSelectionKind = EPlannerSelectionKind::None;
	bool bManagerDelegatesBound = false;
};
