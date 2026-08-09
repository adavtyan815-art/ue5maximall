// Copyright 2026 MaxiMall. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "RoomPlannerWidget.generated.h"

class ARoomPlannerManager;
class UButton;
class UTextBlock;
class UEditableTextBox;
class UImage;

UENUM(BlueprintType)
enum class ERoomPlannerViewMode : uint8
{
	View2D UMETA(DisplayName = "2D Top-Down View"),
	View3D UMETA(DisplayName = "3D Orbit View")
};

UCLASS()
class MAXIMALL_API URoomPlannerWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	virtual void NativeConstruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

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

	/** Current View Mode. */
	UPROPERTY(BlueprintReadOnly, Category = "RoomPlanner")
	ERoomPlannerViewMode CurrentViewMode = ERoomPlannerViewMode::View3D;

	UPROPERTY(BlueprintReadOnly, Category = "RoomPlanner")
	float CurrentDragLengthMeters = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "RoomPlanner")
	FVector CurrentDragMidpointWorld = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "RoomPlanner")
	bool bIsAngleSnapped = false;

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
	TObjectPtr<UTextBlock> TxtLiveLength;

	UPROPERTY(meta = (BindWidgetOptional), BlueprintReadOnly, Category = "RoomPlanner|UI")
	TObjectPtr<UTextBlock> TxtFloorArea;

	UPROPERTY(meta = (BindWidgetOptional), BlueprintReadOnly, Category = "RoomPlanner|UI")
	TObjectPtr<UTextBlock> TxtPerimeter;

	UPROPERTY(meta = (BindWidgetOptional), BlueprintReadOnly, Category = "RoomPlanner|UI")
	TObjectPtr<UWidget> SnapIndicator;

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

protected:
	UPROPERTY(BlueprintReadOnly, Category = "RoomPlanner")
	TObjectPtr<ARoomPlannerManager> PlannerManager;

	UFUNCTION()
	void HandleWallDragProgress(float LengthMeters, FVector MidpointWorld, float AngleDeg, bool bIsSnapped);

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
};
