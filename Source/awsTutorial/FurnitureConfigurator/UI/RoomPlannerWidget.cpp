// Copyright 2026 MaxiMall. All Rights Reserved.

#include "FurnitureConfigurator/UI/RoomPlannerWidget.h"
#include "Constructor/RoomPlannerManager.h"
#include "awsTutorial_PlayerController.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Components/EditableTextBox.h"
#include "Components/Image.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Kismet/GameplayStatics.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Components/Widget.h"
#include "Components/PanelWidget.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/UniformGridPanel.h"
#include "Components/UniformGridSlot.h"
#include "Engine/HitResult.h"
#include "Blueprint/DragDropOperation.h"
#include "ColorCatalog/ColorCatalogWidget.h"
#include "FurnitureConfigurator/UI/PlannerCatalogItemWidget.h"
#include "FurnitureConfigurator/UI/PlannerStyleButton.h"
#include "FurnitureConfigurator/UI/PlannerTileCatalogWidget.h"
#include "FurnitureConfigurator/UI/PlannerDimensionOverlay.h"
#include "Components/HorizontalBox.h"
#include "Components/VerticalBox.h"
#include "Components/WrapBox.h"
#include "Components/ButtonSlot.h"
#include "Constructor/PlannerOpeningStyles.h"
#include "Blueprint/WidgetTree.h"
#include "Components/WrapBoxSlot.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/VerticalBoxSlot.h"
#include "Misc/ScopeExit.h"
#include "Styling/CoreStyle.h"

void URoomPlannerWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	// Before the Slate tree exists: a panel built later takes its children in slot order, so the button lands where it is inserted.
	BuildRuntimeLayout();
}

void URoomPlannerWidget::BuildRuntimeLayout()
{
	if (!PanelBackground && WidgetTree)
	{
		PanelBackground = WidgetTree->FindWidget(PlannerPanelNames::PanelBackground);
	}
	CreateTileCatalogButton();
	WrapFinishControls();
	CreateCatalogToggleButton();
	// Planner 5D's structure in our panel: category tabs, the tool segment and one page per category. A WBP without the sections
	// it needs keeps the one-row toolbar.
	if (!BuildCategoryLayout())
	{
		InstallFallbackToolWrap();
	}
	BuildContextBlock();
	CreateMissingViewOptions();
	MakePanelScrollable();
	CreateStatusStrip();
	CreateFloatingContextBar();
	CreateDimensionOverlay();
	ClearStrayRootTooltip();
	if (!FloorAreaCaption && WidgetTree)
	{
		FloorAreaCaption = Cast<UTextBlock>(WidgetTree->FindWidget(PlannerPanelNames::FloorAreaCaption));
		if (FloorAreaCaption) FloorAreaCaptionText = FloorAreaCaption->GetText();
	}
}

void URoomPlannerWidget::NativeConstruct()
{
	Super::NativeConstruct();

	// ── RELOCATE CHARACTER TO PLANNER AREA VIA SERVER RPC ────────────────
	AAwsTutorial_PlayerController* PC = GetPreviewController();
	APawn* Pawn = nullptr;
	if (PC)
	{
		Pawn = PC->GetPawn();
	}
	else
	{
		Pawn = GetOwningPlayerPawn();
	}
	ACharacter* CharPawn = Cast<ACharacter>(Pawn);

	if (CharPawn)
	{
		CachedOriginalPlayerLocation = CharPawn->GetActorLocation();
		CachedOriginalPlayerRotation = CharPawn->GetActorRotation();
		CachedOriginalControlRotation = PC ? PC->GetControlRotation() : CharPawn->GetActorRotation();
		bHasCachedPlayerTransform = true;

		FVector SafeSpot = AAwsTutorial_PlayerController::FindNonOverlappingPlannerSpot(GetWorld(), CharPawn, PlannerRelocationLocation);

		UE_LOG(LogTemp, Warning, TEXT("[RoomPlanner] Requesting server relocation of player to: %s (Original was: %s)"),
			*SafeSpot.ToString(), *CachedOriginalPlayerLocation.ToString());

		// Immediate local client-side prediction
		CharPawn->TeleportTo(SafeSpot, FRotator::ZeroRotator, false, true);
		if (UCharacterMovementComponent* MoveComp = CharPawn->GetCharacterMovement())
		{
			MoveComp->StopMovementImmediately();
			MoveComp->SetMovementMode(EMovementMode::MOVE_Walking);
		}

		if (PC)
		{
			PC->Server_EnterRoomPlanner(PlannerRelocationLocation);
		}
	}

	if (Btn2DView) { Btn2DView->OnClicked.AddUniqueDynamic(this, &URoomPlannerWidget::On2DViewClicked); }
	if (Btn_2DView) { Btn_2DView->OnClicked.AddUniqueDynamic(this, &URoomPlannerWidget::On2DViewClicked); }
	if (Btn3DView) { Btn3DView->OnClicked.AddUniqueDynamic(this, &URoomPlannerWidget::On3DViewClicked); }
	if (Btn_3DView) { Btn_3DView->OnClicked.AddUniqueDynamic(this, &URoomPlannerWidget::On3DViewClicked); }
	if (BtnSelectTool) { BtnSelectTool->OnClicked.AddUniqueDynamic(this, &URoomPlannerWidget::OnSelectToolClicked); }
	if (BtnDrawWallTool) { BtnDrawWallTool->OnClicked.AddUniqueDynamic(this, &URoomPlannerWidget::OnDrawWallToolClicked); }
	if (BtnDeleteTool) { BtnDeleteTool->OnClicked.AddUniqueDynamic(this, &URoomPlannerWidget::OnDeleteToolClicked); }
	if (BtnAddDoor) { BtnAddDoor->OnClicked.AddUniqueDynamic(this, &URoomPlannerWidget::OnAddDoorClicked); }
	if (BtnAddWindow) { BtnAddWindow->OnClicked.AddUniqueDynamic(this, &URoomPlannerWidget::OnAddWindowClicked); }
	if (BtnPresetRoom) { BtnPresetRoom->OnClicked.AddUniqueDynamic(this, &URoomPlannerWidget::OnPresetRoomRequested); } // asks first on a plan that is not empty
	if (BtnClearLayout) { BtnClearLayout->OnClicked.AddUniqueDynamic(this, &URoomPlannerWidget::OnClearLayoutClicked); }
	if (BtnToggleCeiling) { BtnToggleCeiling->OnClicked.AddUniqueDynamic(this, &URoomPlannerWidget::OnToggleCeilingClicked); }
	if (Btn_ToggleCeiling) { Btn_ToggleCeiling->OnClicked.AddUniqueDynamic(this, &URoomPlannerWidget::OnToggleCeilingClicked); }
	if (BtnCeiling) { BtnCeiling->OnClicked.AddUniqueDynamic(this, &URoomPlannerWidget::OnToggleCeilingClicked); }
	if (BtnClose) { BtnClose->OnClicked.AddUniqueDynamic(this, &URoomPlannerWidget::OnCloseClicked); }
	if (BtnBack) { BtnBack->OnClicked.AddUniqueDynamic(this, &URoomPlannerWidget::OnCloseClicked); }

	if (BtnHelp) { BtnHelp->OnClicked.AddUniqueDynamic(this, &URoomPlannerWidget::OnHelpShowClicked); }
	if (BtnHideHelp) { BtnHideHelp->OnClicked.AddUniqueDynamic(this, &URoomPlannerWidget::OnHelpHideClicked); }
	if (HorizontalBox_3) HorizontalBox_3->SetVisibility(bHelpBarHidden ? ESlateVisibility::Collapsed : ESlateVisibility::SelfHitTestInvisible);
	if (SelectionHeaderRow) SelectionHeaderRow->SetVisibility(ESlateVisibility::Collapsed);

	// Set descriptive tooltips on tools and actions (every tooltip describes the button's current action)
	if (Btn2DView) Btn2DView->SetToolTipText(FText::FromString(TEXT("2D вид: черчение стен и редактирование плана")));
	if (Btn_2DView) Btn_2DView->SetToolTipText(FText::FromString(TEXT("2D вид: черчение стен и редактирование плана")));
	if (Btn3DView) Btn3DView->SetToolTipText(FText::FromString(TEXT("3D вид: осмотр комнаты и подбор отделки")));
	if (Btn_3DView) Btn_3DView->SetToolTipText(FText::FromString(TEXT("3D вид: осмотр комнаты и подбор отделки")));
	if (BtnPresetRoom) BtnPresetRoom->SetToolTipText(FText::FromString(TEXT("Построить готовую комнату 4×4 м")));
	if (BtnClearLayout) BtnClearLayout->SetToolTipText(FText::FromString(TEXT("Очистить весь план")));
	if (BtnApplyProperties) BtnApplyProperties->SetToolTipText(FText::FromString(TEXT("Применить введённые размеры (см) к выбранному элементу")));
	if (BtnRotateLeft) BtnRotateLeft->SetToolTipText(FText::FromString(TEXT("Повернуть объект на 15° против часовой стрелки")));
	if (BtnRotateRight) BtnRotateRight->SetToolTipText(FText::FromString(TEXT("Повернуть объект на 15° по часовой стрелке")));
	if (BtnCancelPlacement) BtnCancelPlacement->SetToolTipText(FText::FromString(TEXT("Отменить размещение объекта")));
	if (BtnHelp) BtnHelp->SetToolTipText(FText::FromString(TEXT("Показать подсказки")));
	if (BtnHideHelp) BtnHideHelp->SetToolTipText(FText::FromString(TEXT("Скрыть подсказки")));
	if (BtnToggleCeiling) BtnToggleCeiling->SetToolTipText(FText::FromString(TEXT("Включить / скрыть потолок в 3D виде")));
	if (Btn_ToggleCeiling) Btn_ToggleCeiling->SetToolTipText(FText::FromString(TEXT("Включить / скрыть потолок в 3D виде")));
	if (BtnCeiling) BtnCeiling->SetToolTipText(FText::FromString(TEXT("Включить / скрыть потолок в 3D виде")));
	if (BtnAddDoor) BtnAddDoor->SetToolTipText(FText::FromString(TEXT("Добавить дверь на выбранную стену")));
	if (BtnAddWindow) BtnAddWindow->SetToolTipText(FText::FromString(TEXT("Добавить окно на выбранную стену")));
	if (BtnClose) BtnClose->SetToolTipText(FText::FromString(TEXT("Закрыть планировщик")));
	if (BtnBack) BtnBack->SetToolTipText(FText::FromString(TEXT("Назад")));

	if (EditableTxtOpeningWidth) { EditableTxtOpeningWidth->OnTextCommitted.AddUniqueDynamic(this, &URoomPlannerWidget::OnOpeningWidthCommitted); }
	if (EditableTxtOpeningHeight) { EditableTxtOpeningHeight->OnTextCommitted.AddUniqueDynamic(this, &URoomPlannerWidget::OnOpeningHeightCommitted); }
	if (EditableTxtOpeningSillHeight) { EditableTxtOpeningSillHeight->OnTextCommitted.AddUniqueDynamic(this, &URoomPlannerWidget::OnOpeningSillHeightCommitted); }

	// UI Sanitization: Immediately collapse contextual/unselected buttons. «Выбрать» stays in the tool segment (disabled until the
	// first wall), so the segment never changes size.
	if (BtnDrawWallTool) BtnDrawWallTool->SetToolTipText(FText::FromString(TEXT("Инструмент «Стена»: зажмите и тяните ЛКМ на плане")));
	if (BtnAddDoor) BtnAddDoor->SetVisibility(ESlateVisibility::Collapsed);
	if (BtnAddWindow) BtnAddWindow->SetVisibility(ESlateVisibility::Collapsed);
	if (EditableTxtOpeningWidth) EditableTxtOpeningWidth->SetVisibility(ESlateVisibility::Collapsed);
	if (EditableTxtOpeningHeight) EditableTxtOpeningHeight->SetVisibility(ESlateVisibility::Collapsed);
	if (EditableTxtOpeningWidth_1) EditableTxtOpeningWidth_1->SetVisibility(ESlateVisibility::Collapsed);
	if (EditableTxtOpeningHeight_1) EditableTxtOpeningHeight_1->SetVisibility(ESlateVisibility::Collapsed);
	if (EditableTxtOpeningSillHeight) EditableTxtOpeningSillHeight->SetVisibility(ESlateVisibility::Collapsed);
	if (BtnApplyProperties) BtnApplyProperties->SetVisibility(ESlateVisibility::Collapsed);
	if (BtnDeleteTool) BtnDeleteTool->SetVisibility(ESlateVisibility::Collapsed);
	if (Image_2) Image_2->SetVisibility(ESlateVisibility::Collapsed);
	if (SnapIndicator) SnapIndicator->SetVisibility(ESlateVisibility::Collapsed);
	if (Border_wall_size) Border_wall_size->SetVisibility(ESlateVisibility::Collapsed);
	if (Border_AddDoor) Border_AddDoor->SetVisibility(ESlateVisibility::Collapsed);
	if (Border_AddWindow) Border_AddWindow->SetVisibility(ESlateVisibility::Collapsed);

	if (TxtGuidanceHint)
	{
		TxtGuidanceHint->SetText(FText::FromString(TEXT("Зажмите ЛКМ и потяните мышь, чтобы нарисовать первую стену, или нажмите «4×4 м»")));
	}

	BindManagerDelegates();

	if (BtnApplyProperties) { BtnApplyProperties->OnClicked.AddUniqueDynamic(this, &URoomPlannerWidget::OnApplyPropertiesClicked); }

	// REQ-07 swing, REQ-13 finishing, REQ-17/18 objects — optional controls of the existing WBP
	if (BtnSwingLeft) { BtnSwingLeft->OnClicked.AddUniqueDynamic(this, &URoomPlannerWidget::OnSwingLeftClicked); }
	if (BtnSwingRight) { BtnSwingRight->OnClicked.AddUniqueDynamic(this, &URoomPlannerWidget::OnSwingRightClicked); }
	if (BtnSwingInward) { BtnSwingInward->OnClicked.AddUniqueDynamic(this, &URoomPlannerWidget::OnSwingInwardClicked); }
	if (BtnSwingOutward) { BtnSwingOutward->OnClicked.AddUniqueDynamic(this, &URoomPlannerWidget::OnSwingOutwardClicked); }
	if (BtnFinishPaint) { BtnFinishPaint->OnClicked.AddUniqueDynamic(this, &URoomPlannerWidget::OnFinishPaintClicked); }
	if (BtnClearFinish) { BtnClearFinish->OnClicked.AddUniqueDynamic(this, &URoomPlannerWidget::OnClearFinishClicked); }
	if (BtnRotateLeft) { BtnRotateLeft->OnClicked.AddUniqueDynamic(this, &URoomPlannerWidget::OnRotateLeftClicked); }
	if (BtnRotateRight) { BtnRotateRight->OnClicked.AddUniqueDynamic(this, &URoomPlannerWidget::OnRotateRightClicked); }
	if (BtnCancelPlacement) { BtnCancelPlacement->OnClicked.AddUniqueDynamic(this, &URoomPlannerWidget::OnCancelPlacementClicked); }
	if (BtnDoorsOpen)
	{
		BtnDoorsOpen->OnClicked.AddUniqueDynamic(this, &URoomPlannerWidget::OnDoorsOpenClicked);
		BtnDoorsOpen->SetToolTipText(FText::FromString(TEXT("Открыть все двери и окна")));
	}
	if (BtnDoorsClose)
	{
		BtnDoorsClose->OnClicked.AddUniqueDynamic(this, &URoomPlannerWidget::OnDoorsCloseClicked);
		BtnDoorsClose->SetToolTipText(FText::FromString(TEXT("Закрыть все двери и окна")));
	}

	if (BtnSwingLeft) BtnSwingLeft->SetToolTipText(FText::FromString(TEXT("Петли слева (вид изнутри комнаты)")));
	if (BtnSwingRight) BtnSwingRight->SetToolTipText(FText::FromString(TEXT("Петли справа (вид изнутри комнаты)")));
	if (BtnSwingInward) BtnSwingInward->SetToolTipText(FText::FromString(TEXT("Открывается внутрь комнаты")));
	if (BtnSwingOutward) BtnSwingOutward->SetToolTipText(FText::FromString(TEXT("Открывается наружу")));
	if (BtnFinishPaint) BtnFinishPaint->SetToolTipText(FText::FromString(TEXT("Краска RAL / NCS для выбранной поверхности")));
	if (BtnClearFinish) BtnClearFinish->SetToolTipText(FText::FromString(TEXT("Убрать отделку с выбранной поверхности")));

	{
		UWidget* InitiallyHidden[] = {
			BtnSwingLeft.Get(), BtnSwingRight.Get(), BtnSwingInward.Get(), BtnSwingOutward.Get(),
			BtnFinishPaint.Get(), BtnClearFinish.Get(), BtnRotateLeft.Get(), BtnRotateRight.Get(),
			BtnCancelPlacement.Get(), SelectionLabelPanel.Get(), TxtOperationMessage.Get() };
		for (UWidget* W : InitiallyHidden)
		{
			if (W) W->SetVisibility(ESlateVisibility::Collapsed);
		}
	}

	// Catalog sections: DT_PlannerObjects and DT_FurnitureCatalog cards (drag & drop onto the plan)
	if (BtnCatalogInterior) { BtnCatalogInterior->OnClicked.AddUniqueDynamic(this, &URoomPlannerWidget::OnCatalogInteriorClicked); }
	if (BtnCatalogCabinets) { BtnCatalogCabinets->OnClicked.AddUniqueDynamic(this, &URoomPlannerWidget::OnCatalogCabinetsClicked); }
	if (BtnCatalogInterior) BtnCatalogInterior->SetToolTipText(FText::FromString(TEXT("Объекты интерьера: перетащите на пол или на стену")));
	if (BtnCatalogCabinets) BtnCatalogCabinets->SetToolTipText(FText::FromString(TEXT("Комплекты тумб: перетащите к стене")));
	ActiveCatalogTab = (DefaultCatalogTab == EPlannerPlacementKind::CabinetSet) ? EPlannerPlacementKind::CabinetSet : EPlannerPlacementKind::Object;
	// The catalog is folded when the planner opens (the «Каталог» button opens it), and neither «Интерьер» nor «Тумбы» is active:
	// the card area stays folded until a tab is clicked. bOpenCatalogTabOnStart opens on the «Каталог» tab with DefaultCatalogTab.
	bCatalogTabChosen = bOpenCatalogTabOnStart;
	bCatalogOpen = bOpenCatalogTabOnStart && !bCategoryLayoutBuilt;
	Active2DCategory = EPlannerPanelCategory::Layout; // bOpenCatalogTabOnStart opens «Каталог» below, through its tool rule
	CategoryBeforeCatalog = EPlannerPanelCategory::Layout;
	bPlanEmptyCached = IsPlanEmpty();
	CollectSeparatorLines();
	RefreshCatalogPanels();
	UpdateCatalogTabStyles();
	ApplyCatalogSectionVisibility();

	// Automatically enter 2D Top-Down Drawing Mode on open (not a user's view switch: nothing to reconcile, the start tab stays)
	CurrentViewMode = ERoomPlannerViewMode::View3D;
	bOpeningPlanner = true;
	SetViewMode(ERoomPlannerViewMode::View2D);
	SetToolMode(EPlannerToolMode::DrawWall);
	bOpeningPlanner = false;
	if (bOpenCatalogTabOnStart && bCategoryLayoutBuilt)
	{
		// After the forced Draw tool: the «Каталог» category applies its rule (a re-opened plan with walls gets «Выбрать»).
		SetActiveCatalogTab(DefaultCatalogTab);
	}

	// Initialize UI state
	UpdateViewModeButtonStyles();
	UpdateDynamicPropertiesPanel();
	UpdateSummaryStatsUI();
	UpdateToolModeButtonStyles();
}

void URoomPlannerWidget::NativeDestruct()
{
	AAwsTutorial_PlayerController* PC = GetPreviewController();
	if (PC)
	{
		PC->SendPixelStreamingResponse(TEXT("PlannerMode:Closed"));
	}

	if (::IsValid(ActivePlannerColorCatalog) && ActivePlannerColorCatalog->IsInViewport())
	{
		ActivePlannerColorCatalog->OnColorItemSelected.RemoveAll(this);
		ActivePlannerColorCatalog->OnCatalogClosed.RemoveAll(this);
		ActivePlannerColorCatalog->RemoveFromParent();
	}
	ActivePlannerColorCatalog = nullptr;

	if (::IsValid(ActivePlannerTileCatalog) && ActivePlannerTileCatalog->IsInViewport())
	{
		ActivePlannerTileCatalog->OnTileChosen.RemoveAll(this);
		ActivePlannerTileCatalog->OnCatalogClosed.RemoveAll(this);
		ActivePlannerTileCatalog->RemoveFromParent();
	}
	ActivePlannerTileCatalog = nullptr;

	if (PlannerManager)
	{
		UnbindManagerDelegates();
		PlannerManager->bPlannerUIOpen = false;
		PlannerManager->CancelPendingPlacement();
		PlannerManager->SetPlannerSessionActive(false); // release the exposure override before leaving 3D
		PlannerManager->SetViewMode(false);
	}

	// ── RESTORE CHARACTER TO ORIGINAL LOCATION VIA SERVER RPC ─────────────
	if (bHasCachedPlayerTransform)
	{
		if (PC)
		{
			UE_LOG(LogTemp, Warning, TEXT("[RoomPlanner] Restoring player to original location: %s (Rot: %s)"),
				*CachedOriginalPlayerLocation.ToString(), *CachedOriginalPlayerRotation.ToString());

			PC->Server_ExitRoomPlanner(CachedOriginalPlayerLocation, CachedOriginalPlayerRotation);
			PC->RestorePlayerCamera();
		}
		bHasCachedPlayerTransform = false;
	}

	Super::NativeDestruct();
}

FReply URoomPlannerWidget::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	// Any press first sends a mouse-wheel rotation still waiting for its idle commit (the press may select, drag or place another item).
	if (InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		if (AAwsTutorial_PlayerController* WheelPC = GetPreviewController())
		{
			WheelPC->FlushPlannerWheelCommit();
		}
	}

	// A press on the panel background, between its buttons, on the status strip or on a catalog beside the panel reaches this root
	// unhandled: it must not draw, select or pick on the plan hidden behind the UI.
	if (InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton && IsScreenPositionOverPlannerUI(InMouseEvent.GetScreenSpacePosition()))
	{
		return FReply::Handled();
	}

	if (CurrentViewMode == ERoomPlannerViewMode::View2D && InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		AAwsTutorial_PlayerController* PC = GetPreviewController();
		if (PlannerManager && PC)
		{
			FVector WorldOrigin, WorldDirection;
			if (PC->DeprojectMousePositionToWorld(WorldOrigin, WorldDirection) && !FMath::IsNearlyZero(WorldDirection.Z))
			{
				float t = -WorldOrigin.Z / WorldDirection.Z;
				if (t >= 0.f)
				{
					FVector GroundPos = WorldOrigin + t * WorldDirection;
					GroundPos.Z = 0.f;

					if (PlannerManager->ActiveToolMode == EPlannerToolMode::DrawWall)
					{
						bIsWidgetDrawingWall = true;
						PlannerManager->StartInteractiveWallDraw(GroundPos);
						UpdateMouseCursorPosition();
						return FReply::Handled().CaptureMouse(TakeWidget());
					}
					else if (PlannerManager->ActiveToolMode == EPlannerToolMode::Select)
					{
						// 1. Wall control point → corner drag (REQ-02). Ray test: the handle is on the wall top.
						const int32 NodeID = PlannerManager->FindNodeAtCursorRay(WorldOrigin, WorldDirection, 25.f);
						if (NodeID != -1 && PlannerManager->StartNodeDrag(NodeID))
						{
							bIsWidgetDraggingNode = true;
							UpdateDynamicPropertiesPanel();
							return FReply::Handled().CaptureMouse(TakeWidget());
						}

						// 2. Object / cabinet set / wall / opening / floor
						const EPlannerSelectionKind Kind = PlannerManager->SelectAtWorldPos2D(GroundPos);
						if (Kind == EPlannerSelectionKind::Object || Kind == EPlannerSelectionKind::CabinetSet)
						{
							bWidgetDraggedIsCabinetSet = (Kind == EPlannerSelectionKind::CabinetSet);
							WidgetDraggedObjectID = bWidgetDraggedIsCabinetSet ? PlannerManager->SelectedCabinetSetID : PlannerManager->SelectedObjectID;
							FVector ObjLoc = GroundPos;
							if (bWidgetDraggedIsCabinetSet) { FPlacedCabinetSetData D; if (PlannerManager->GetCabinetSet(WidgetDraggedObjectID, D)) ObjLoc = D.Location; }
							else { FPlacedFurnitureData D; if (PlannerManager->GetPlacedObject(WidgetDraggedObjectID, D)) ObjLoc = D.Location; }
							WidgetDragOffset = ObjLoc - FVector(GroundPos.X, GroundPos.Y, 0.f);
							UpdateDynamicPropertiesPanel();
							return FReply::Handled().CaptureMouse(TakeWidget());
						}
						WidgetDraggedObjectID.Empty();
						UpdateDynamicPropertiesPanel();
						return FReply::Handled();
					}
					else if (PlannerManager->ActiveToolMode == EPlannerToolMode::PlaceFurniture)
					{
						PC->PlannerPlacePendingAtCursorRay(WorldOrigin, WorldDirection);
						UpdateToolModeButtonStyles();
						return FReply::Handled();
					}
					else if (PlannerManager->ActiveToolMode == EPlannerToolMode::Erase)
					{
						int32 TargetSeg = PlannerManager->SelectWallAtWorldPos(GroundPos);
						if (TargetSeg != -1)
						{
							PC->Server_DeleteWall(TargetSeg);
						}
						return FReply::Handled();
					}
				}
			}
		}
	}
	else if (CurrentViewMode == ERoomPlannerViewMode::View3D && InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		// 3D mode: pick the surface / object under the cursor for finishing (REQ-13 in 3D). Not handled so the
		// controller's booth interaction still receives the click. Placement / editing stay 2D-only.
		PickSurfaceUnderCursor();
	}
	return Super::NativeOnMouseButtonDown(InGeometry, InMouseEvent);
}

FReply URoomPlannerWidget::NativeOnMouseMove(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (CurrentViewMode == ERoomPlannerViewMode::View2D)
	{
		AAwsTutorial_PlayerController* PC = GetPreviewController();
		if (PlannerManager && PC)
		{
			FVector WorldOrigin, WorldDirection;
			if (PC->DeprojectMousePositionToWorld(WorldOrigin, WorldDirection) && !FMath::IsNearlyZero(WorldDirection.Z))
			{
				float t = -WorldOrigin.Z / WorldDirection.Z;
				if (t >= 0.f)
				{
					FVector GroundPos = WorldOrigin + t * WorldDirection;
					GroundPos.Z = 0.f;

					if (bIsWidgetDrawingWall && PlannerManager->ActiveToolMode == EPlannerToolMode::DrawWall)
					{
						PlannerManager->UpdateInteractiveWallDraw(GroundPos);
						UpdateMouseCursorPosition();
						return FReply::Handled();
					}
					else if (!bIsWidgetDrawingWall && PlannerManager->ActiveToolMode == EPlannerToolMode::DrawWall)
					{
						PlannerManager->CheckHoverSnapHint(GroundPos);
					}
					else if (bIsWidgetDraggingNode)
					{
						FVector DragPos = GroundPos;
						if (AAwsTutorial_PlayerController* DragPC = GetPreviewController())
						{
							FVector O, D;
							if (DragPC->DeprojectMousePositionToWorld(O, D))
							{
								PlannerManager->ProjectCursorRayToNodeHandlePlane(PlannerManager->GetDraggingNodeID(), O, D, DragPos);
							}
						}
						PlannerManager->UpdateNodeDrag(DragPos);
						return FReply::Handled();
					}
					else if (!WidgetDraggedObjectID.IsEmpty() && InMouseEvent.IsMouseButtonDown(EKeys::LeftMouseButton))
					{
						// The rotation keeps a wheel turn made during the drag even if a replicated update reset the stored one meanwhile.
						const FVector NewLoc = FVector(GroundPos.X, GroundPos.Y, 0.f) + WidgetDragOffset;
						if (bWidgetDraggedIsCabinetSet)
						{
							FPlacedCabinetSetData D;
							if (PlannerManager->GetCabinetSet(WidgetDraggedObjectID, D)) PlannerManager->MoveCabinetSetLocal(WidgetDraggedObjectID, NewLoc, PC->GetPlannerDragRotation(WidgetDraggedObjectID, D.Rotation));
						}
						else
						{
							FPlacedFurnitureData D;
							if (PlannerManager->GetPlacedObject(WidgetDraggedObjectID, D)) PlannerManager->MovePlacedObjectLocal(WidgetDraggedObjectID, NewLoc, PC->GetPlannerDragRotation(WidgetDraggedObjectID, D.Rotation));
						}
						return FReply::Handled();
					}
				}
			}
		}
	}
	return Super::NativeOnMouseMove(InGeometry, InMouseEvent);
}

FReply URoomPlannerWidget::NativeOnMouseButtonUp(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton && bIsWidgetDraggingNode)
	{
		bIsWidgetDraggingNode = false;
		if (PlannerManager)
		{
			int32 NodeID = -1;
			FVector2D FinalPos;
			if (PlannerManager->EndNodeDrag(NodeID, FinalPos))
			{
				if (AAwsTutorial_PlayerController* PC = GetPreviewController())
				{
					PC->Server_MoveNode(NodeID, FinalPos);
				}
			}
		}
		return FReply::Handled().ReleaseMouseCapture();
	}
	if (InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton && !WidgetDraggedObjectID.IsEmpty())
	{
		WidgetDraggedObjectID.Empty();
		if (AAwsTutorial_PlayerController* PC = GetPreviewController())
		{
			PC->PlannerCommitSelectedObjectTransform();
		}
		return FReply::Handled().ReleaseMouseCapture();
	}
	if (InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton && bIsWidgetDrawingWall)
	{
		bIsWidgetDrawingWall = false;
		UpdateMouseCursorPosition();
		AAwsTutorial_PlayerController* PC = GetPreviewController();
		if (PlannerManager)
		{
			FVector2D P1(PlannerManager->GetDragStartPoint().X, PlannerManager->GetDragStartPoint().Y);
			FVector2D P2(PlannerManager->GetDragCurrentPoint().X, PlannerManager->GetDragCurrentPoint().Y);
			PlannerManager->CommitInteractiveWallDraw();

			if (FVector2D::Distance(P1, P2) >= 10.f && PC)
			{
				PC->Server_CommitWall(P1, P2);
			}
		}
		return FReply::Handled().ReleaseMouseCapture();
	}
	return Super::NativeOnMouseButtonUp(InGeometry, InMouseEvent);
}

void URoomPlannerWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	// Every tick: keeps the drawing cursor / live-length label in sync for both the widget-driven and the
	// manager-driven drawing paths (the label must fold the moment drawing ends).
	UpdateMouseCursorPosition();

	// The snap marker may only exist while the Draw Wall hover preview is being refreshed every frame. Any other
	// state (tool switched, cursor over the UI, drag released, drawing started) leaves no marker behind.
	if (SnapIndicator && SnapIndicator->GetVisibility() != ESlateVisibility::Collapsed
		&& (!PlannerManager || !PlannerManager->HasFreshHoverSnap() || PlannerManager->IsNodeDragActive() || PlannerManager->IsWallDrawingActive()))
	{
		SnapIndicator->SetVisibility(ESlateVisibility::Collapsed);
	}

	if (!PlannerManager && GetWorld())
	{
		BindManagerDelegates();
		if (PlannerManager)
		{
			PlannerManager->SetPlannerSessionActive(true); // enables the planner's bounded exposure while open in 3D
			PlannerManager->SetViewMode(CurrentViewMode == ERoomPlannerViewMode::View2D);
			UpdateSummaryStatsUI();
			UpdateViewModeButtonStyles();
			UpdateDynamicPropertiesPanel();
			UpdateToolModeButtonStyles();
		}
	}

	// REQ-02 / REQ-04 / REQ-06: keep the dimension labels next to the selected object every frame.
	UpdateSelectionLabelsUI();

	// Separator lines follow every section visibility change, wherever it was made.
	UpdateSeparatorLines();

	if (OperationMessageClearTime > 0.f && GetWorld() && GetWorld()->GetTimeSeconds() > OperationMessageClearTime)
	{
		OperationMessageClearTime = 0.f;
		if (TxtOperationMessage) TxtOperationMessage->SetVisibility(ESlateVisibility::Collapsed);
		if (MessageChip) MessageChip->SetVisibility(ESlateVisibility::Collapsed);
	}

	// The status strip stays centred over the free plan area and wraps at its width (a finish catalog beside the panel narrows it).
	UpdateStatusStripPlacement();

	if (PlannerManager)
	{
		bool bIs2D = (CurrentViewMode == ERoomPlannerViewMode::View2D);
		const int32 WallCount = PlannerManager->GetWallCount();
		bool bHasWalls = WallCount > 0;

		// If Select tool is active but no walls exist, force revert to Draw Wall
		if (!bHasWalls && PlannerManager->ActiveToolMode == EPlannerToolMode::Select)
		{
			SetToolMode(EPlannerToolMode::DrawWall);
			ShowStatusMessage(TEXT("Стен нет — включён инструмент «Создать стену»"));
		}

		// The category rules follow the plan: «Выбрать» enabled with walls, the empty-plan notice, and a wall draw started outside
		// «Планировка» (possible only on a plan without walls) brings the panel back to «Планировка».
		const bool bDrawing = bIsWidgetDrawingWall || PlannerManager->IsWallDrawingActive();
		if (bCategoryLayoutBuilt && PlannerPanelRules::ReturnsToLayout(Active2DCategory, bIs2D, bDrawing))
		{
			SetActiveCategory(EPlannerPanelCategory::Layout);
		}
		if (WallCount != LastTickWallCount)
		{
			LastTickWallCount = WallCount;
			EnforceCategoryTool(); // walls arrived while «Каталог» / «Отделка» is open (a load, a replicated edit): not the Draw tool there
			ApplyCategoryVisibility();
			UpdateToolModeButtonStyles();
		}

		// Door / window creation blocks: a selected wall only, 2D (openings are edited in 2D). Their Borders follow their content, so
		// the selection section becomes truly empty (and its separators fold) when nothing is selected.
		UpdateCreationBlocksVisibility();

		UpdateGuidanceHintText();
	}
}

void URoomPlannerWidget::SetViewMode(ERoomPlannerViewMode NewMode)
{
	if (CurrentViewMode == NewMode) return;
	const ERoomPlannerViewMode OldMode = CurrentViewMode;
	CurrentViewMode = NewMode;
	ON_SCOPE_EXIT { ReconcileAfterViewSwitch(OldMode); };
	AAwsTutorial_PlayerController* PC = GetPreviewController();
	if (PlannerManager)
	{
		PlannerManager->SetViewMode(NewMode == ERoomPlannerViewMode::View2D);
	}
	if (PC)
	{
		PC->SetRoomPlannerCamera2D(NewMode == ERoomPlannerViewMode::View2D, PlannerRelocationLocation);
		PC->SendPixelStreamingResponse(NewMode == ERoomPlannerViewMode::View2D ? TEXT("PlannerMode:2D") : TEXT("PlannerMode:3D"));
	}
	UpdateViewModeButtonStyles();
	UpdateDynamicPropertiesPanel();
	UpdateGuidanceHintText();
}

void URoomPlannerWidget::SetToolMode(EPlannerToolMode NewToolMode)
{
	if (PlannerManager)
	{
		PlannerManager->SetToolMode(NewToolMode);
	}
	if (AAwsTutorial_PlayerController* PC = GetPreviewController())
	{
		PC->UpdateRoomPlannerCameraToolMode(NewToolMode);
	}
	UpdateToolModeButtonStyles();
	UpdateGuidanceHintText();
}

void URoomPlannerWidget::UpdateToolModeButtonStyles()
{
	EPlannerToolMode CurrentToolMode = EPlannerToolMode::DrawWall;
	if (PlannerManager)
	{
		CurrentToolMode = PlannerManager->ActiveToolMode;
	}
	const bool bHasWalls = PlannerManager && PlannerManager->GetWallCount() > 0;

	if (BtnSelectTool)
	{
		// Always in the segment; disabled (the native disabled look) until the first wall, with the reason in the tooltip. (The
		// one-row fallback toolbar still hides its tools in 3D.)
		const ESlateVisibility SelectVis = (bCategoryLayoutBuilt || CurrentViewMode == ERoomPlannerViewMode::View2D) ? ESlateVisibility::Visible : ESlateVisibility::Collapsed;
		if (BtnSelectTool->GetVisibility() != SelectVis) BtnSelectTool->SetVisibility(SelectVis);
		if (BtnSelectTool->GetIsEnabled() != bHasWalls) BtnSelectTool->SetIsEnabled(bHasWalls);
		ApplyButtonState(BtnSelectTool, CurrentToolMode == EPlannerToolMode::Select ? EPanelButtonState::On : EPanelButtonState::Idle);
		const int8 TooltipState = bHasWalls ? 1 : 0;
		if (TooltipState != SelectToolTooltipState)
		{
			SelectToolTooltipState = TooltipState;
			BtnSelectTool->SetToolTipText(FText::FromString(bHasWalls
				? TEXT("Выделение: кликните стену, проём, пол или объект")
				: TEXT("Пока нет стен для работы")));
		}
	}
	if (BtnDrawWallTool)
	{
		ApplyButtonState(BtnDrawWallTool, CurrentToolMode == EPlannerToolMode::DrawWall ? EPanelButtonState::On : EPanelButtonState::Idle);
	}
	if (BtnDeleteTool)
	{
		// Deletes the selection: an action, never shown as a switched-on tool.
		ApplyButtonState(BtnDeleteTool, EPanelButtonState::Idle);
		BtnDeleteTool->SetToolTipText(FText::FromString(TEXT("Удалить выбранный элемент (клавиша Delete / Backspace)")));
	}

	UpdateGuidanceHintText();
}

void URoomPlannerWidget::UpdateViewModeButtonStyles()
{
	bool bIs2D = (CurrentViewMode == ERoomPlannerViewMode::View2D);

	// The selected view is navy and stays enabled (clicking it again changes nothing): it must not look unavailable.
	UButton* ViewButtons2D[] = { Btn2DView.Get(), Btn_2DView.Get() };
	UButton* ViewButtons3D[] = { Btn3DView.Get(), Btn_3DView.Get() };
	for (UButton* Button : ViewButtons2D)
	{
		if (!Button) continue;
		if (!Button->GetIsEnabled()) Button->SetIsEnabled(true);
		ApplyButtonState(Button, bIs2D ? EPanelButtonState::Selected : EPanelButtonState::Idle);
	}
	for (UButton* Button : ViewButtons3D)
	{
		if (!Button) continue;
		if (!Button->GetIsEnabled()) Button->SetIsEnabled(true);
		ApplyButtonState(Button, !bIs2D ? EPanelButtonState::Selected : EPanelButtonState::Idle);
	}

	// The ceiling toggle is a 3D view option (entering 3D shows the ceiling; the plan view never does).
	const bool bCeilingOn = PlannerManager && PlannerManager->bCeilingVisible;
	UButton* CeilingButtons[] = { BtnToggleCeiling.Get(), Btn_ToggleCeiling.Get(), BtnCeiling.Get() };
	for (UButton* Button : CeilingButtons)
	{
		if (!Button) continue;
		ApplyButtonState(Button, bCeilingOn ? EPanelButtonState::On : EPanelButtonState::Idle);
		Button->SetVisibility(bIs2D ? ESlateVisibility::Collapsed : ESlateVisibility::Visible);
	}

	ESlateVisibility ToolsVis = bIs2D ? ESlateVisibility::Visible : ESlateVisibility::Collapsed;
	if (bCategoryLayoutBuilt)
	{
		// The tool segment and the category pages decide; the buttons themselves stay visible. Delete-all is «Очистить план» on
		// the «Планировка» page now: the trash icon beside 2D/3D goes, the trash icon means "delete the selection" only.
		if (BtnDrawWallTool && BtnDrawWallTool->GetVisibility() != ESlateVisibility::Visible) BtnDrawWallTool->SetVisibility(ESlateVisibility::Visible);
		if (BtnPresetRoom && BtnPresetRoom->GetVisibility() != ESlateVisibility::Visible) BtnPresetRoom->SetVisibility(ESlateVisibility::Visible);
		if (BtnClearLayout) BtnClearLayout->SetVisibility(ESlateVisibility::Collapsed);
	}
	else
	{
		if (BtnDrawWallTool) BtnDrawWallTool->SetVisibility(ToolsVis);
		if (BtnClearLayout) BtnClearLayout->SetVisibility(ToolsVis);
		if (BtnPresetRoom) BtnPresetRoom->SetVisibility(ToolsVis);
		if (BtnSelectTool) BtnSelectTool->SetVisibility(ToolsVis);
		if (BtnCatalogToggle) BtnCatalogToggle->SetVisibility(ToolsVis); // placement is 2D-only, like the catalog it opens
	}
	if (Image_1) Image_1->SetVisibility(ToolsVis);

	// 3D view options: door / window leaves.
	const ESlateVisibility ViewOptionsVis = bIs2D ? ESlateVisibility::Collapsed : ESlateVisibility::Visible;
	if (BtnDoorsOpen) BtnDoorsOpen->SetVisibility(ViewOptionsVis);
	if (BtnDoorsClose) BtnDoorsClose->SetVisibility(ViewOptionsVis);
	if (ViewOptionsRow) ViewOptionsRow->SetVisibility(bIs2D ? ESlateVisibility::Collapsed : ESlateVisibility::SelfHitTestInvisible);
	{
		const bool bLeavesOpen = PlannerManager && PlannerManager->GetDefaultOpeningLeavesOpen();
		ApplyButtonState(BtnDoorsOpen, bLeavesOpen ? EPanelButtonState::On : EPanelButtonState::Idle);
		ApplyButtonState(BtnDoorsClose, !bLeavesOpen ? EPanelButtonState::On : EPanelButtonState::Idle);
	}

	// Category pages (and the catalog area inside «Каталог») follow the view: editing pages are 2D-only.
	ApplyCategoryVisibility();
	UpdateSummaryStatsUI();

	UpdateGuidanceHintText();
}

// ═══════════════════════════════════════════════════════════════════════════════
// Catalog tabs «Интерьер» / «Тумбы»
// ═══════════════════════════════════════════════════════════════════════════════

void URoomPlannerWidget::SetActiveCatalogTab(EPlannerPlacementKind Tab)
{
	const EPlannerPlacementKind NewTab = (Tab == EPlannerPlacementKind::CabinetSet) ? EPlannerPlacementKind::CabinetSet : EPlannerPlacementKind::Object;
	const bool bChanged = !bCatalogTabChosen || NewTab != ActiveCatalogTab;
	bCatalogTabChosen = true;
	ActiveCatalogTab = NewTab;
	if (bChanged)
	{
		RefreshCatalogPanels(); // the single content area is (re-)populated with this tab's cards
	}
	UpdateCatalogTabStyles();
	// A tab is clicked in an open catalog; picking one from code opens the «Каталог» category as well.
	if (bCategoryLayoutBuilt && Active2DCategory != EPlannerPanelCategory::Catalog)
	{
		SetActiveCategory(EPlannerPanelCategory::Catalog);
	}
	else
	{
		bCatalogOpen = true;
		ApplyCatalogSectionVisibility();
		UpdateGuidanceHintText(); // the «Каталог» hint depends on the tab (the wheel turns interior cards only)
	}
}

void URoomPlannerWidget::UpdateCatalogTabStyles()
{
	// Identical to UColorCatalogWidget::UpdateTabButtonStyles (RAL / NCS): active = ActiveTabColor, other = InactiveTabColor.
	// Until a tab is picked both are inactive.
	const bool bObjects = bCatalogTabChosen && ActiveCatalogTab == EPlannerPlacementKind::Object;
	const bool bCabinets = bCatalogTabChosen && ActiveCatalogTab == EPlannerPlacementKind::CabinetSet;
	if (BtnCatalogInterior) BtnCatalogInterior->SetBackgroundColor(bObjects ? ActiveTabColor : InactiveTabColor);
	if (BtnCatalogCabinets) BtnCatalogCabinets->SetBackgroundColor(bCabinets ? ActiveTabColor : InactiveTabColor);
}

void URoomPlannerWidget::SetCatalogOpen(bool bOpen)
{
	if (bCategoryLayoutBuilt)
	{
		// The catalog is the «Каталог» category: open = that tab; folded = back to the tab open before it.
		if (bOpen)
		{
			SetActiveCategory(EPlannerPanelCategory::Catalog);
		}
		else if (Active2DCategory == EPlannerPanelCategory::Catalog)
		{
			SetActiveCategory(PlannerPanelRules::AfterCatalogToggle(EPlannerPanelCategory::Catalog, CategoryBeforeCatalog));
		}
		return;
	}
	bCatalogOpen = bOpen;
	ApplyCatalogSectionVisibility();
}

void URoomPlannerWidget::OnCatalogToggleClicked()
{
	if (bCategoryLayoutBuilt)
	{
		SetActiveCategory(PlannerPanelRules::AfterCatalogToggle(Active2DCategory, CategoryBeforeCatalog));
		return;
	}
	SetCatalogOpen(!bCatalogOpen);
}

void URoomPlannerWidget::ApplyCatalogSectionVisibility()
{
	// The tabs and the cards show only while the catalog is open (the «Каталог» tab) and only in 2D.
	const bool bIs2D = (CurrentViewMode == ERoomPlannerViewMode::View2D);
	if (bCategoryLayoutBuilt)
	{
		bCatalogOpen = (Active2DCategory == EPlannerPanelCategory::Catalog);
	}
	const ESlateVisibility Vis = (bIs2D && bCatalogOpen) ? ESlateVisibility::Visible : ESlateVisibility::Collapsed;
	if (BtnCatalogToggle && !bCategoryLayoutBuilt)
	{
		BtnCatalogToggle->SetBackgroundColor(bCatalogOpen ? ActiveTabColor : CatalogToggleIdleColor);
	}
	if (CatalogTabBar) CatalogTabBar->SetVisibility(Vis);
	if (BtnCatalogInterior && !CatalogTabBar) BtnCatalogInterior->SetVisibility(Vis);
	if (BtnCatalogCabinets && !CatalogTabBar) BtnCatalogCabinets->SetVisibility(Vis);
	// The card area opens with the first tab click; until then a line says what to do.
	if (Catalog_Container) Catalog_Container->SetVisibility(bCatalogTabChosen ? Vis : ESlateVisibility::Collapsed);
	if (CatalogPrompt) CatalogPrompt->SetVisibility((bIs2D && bCatalogOpen && !bCatalogTabChosen) ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	if (CatalogDragHint) CatalogDragHint->SetVisibility((bIs2D && bCatalogOpen && bCatalogTabChosen) ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	UpdateSeparatorLines();
}

void URoomPlannerWidget::CollectSeparatorLines()
{
	SeparatorLines.Reset();
	if (!WidgetTree) return;
	WidgetTree->ForEachWidget([this](UWidget* W)
	{
		if (W && W->GetName().StartsWith(TEXT("Image_line"), ESearchCase::IgnoreCase))
		{
			SeparatorLines.Add(W);
		}
	});
}

namespace PlannerSeparatorRule
{
	static bool IsOwnVisibilityShown(const UWidget* W)
	{
		const ESlateVisibility V = W->GetVisibility();
		return V != ESlateVisibility::Collapsed && V != ESlateVisibility::Hidden;
	}

	/** A widget is effectively visible when it is shown itself and, for panels, at least one descendant leaf is shown. */
	static bool HasVisibleLeaf(const UWidget* W)
	{
		if (!W || !IsOwnVisibilityShown(W)) return false;
		const UPanelWidget* Panel = Cast<UPanelWidget>(W);
		if (!Panel)
		{
			// A TextBlock with no text draws nothing and must not keep a section (and its lines) alive.
			if (const UTextBlock* Txt = Cast<UTextBlock>(W)) return !Txt->GetText().IsEmpty();
			return true; // leaf (Image, Button, EditableTextBox, user widget ...)
		}
		const int32 N = Panel->GetChildrenCount();
		if (N == 0) return false; // empty container adds nothing to a section
		for (int32 i = 0; i < N; ++i)
		{
			if (HasVisibleLeaf(Panel->GetChildAt(i))) return true;
		}
		return false;
	}
}

void URoomPlannerWidget::UpdateSeparatorLines()
{
	using namespace PlannerSeparatorRule;
	if (SeparatorLines.Num() == 0) return;

	// Group the lines by their parent panel; each panel is handled on its own (so lines nested inside a
	// section, e.g. under SelectionHeaderRow, work with the same rule).
	TSet<UPanelWidget*> Parents;
	for (const TWeakObjectPtr<UWidget>& LinePtr : SeparatorLines)
	{
		if (UWidget* Line = LinePtr.Get())
		{
			if (UPanelWidget* Parent = Line->GetParent()) Parents.Add(Parent);
		}
	}

	for (UPanelWidget* Parent : Parents)
	{
		// Rule: walking the children in order, a line is shown only when a visible section precedes it and
		// another visible section follows it, and it is the first line between those two sections. Every
		// other line (edges of the stack, lines next to collapsed sections, consecutive lines) is collapsed.
		TArray<UWidget*> LinesToShow;
		UWidget* PendingLine = nullptr;      // first line seen since the last visible section
		bool bHaveVisibleAbove = false;      // a visible section has been seen already

		const int32 Count = Parent->GetChildrenCount();
		for (int32 i = 0; i < Count; ++i)
		{
			UWidget* Child = Parent->GetChildAt(i);
			if (!Child) continue;
			const bool bIsLine = SeparatorLines.ContainsByPredicate([Child](const TWeakObjectPtr<UWidget>& P) { return P.Get() == Child; });
			if (bIsLine)
			{
				if (bHaveVisibleAbove && !PendingLine) PendingLine = Child;
			}
			else if (HasVisibleLeaf(Child))
			{
				if (PendingLine) { LinesToShow.Add(PendingLine); PendingLine = nullptr; }
				bHaveVisibleAbove = true;
			}
		}

		for (int32 i = 0; i < Count; ++i)
		{
			UWidget* Child = Parent->GetChildAt(i);
			if (!Child) continue;
			const bool bIsLine = SeparatorLines.ContainsByPredicate([Child](const TWeakObjectPtr<UWidget>& P) { return P.Get() == Child; });
			if (!bIsLine) continue;
			const bool bShow = LinesToShow.Contains(Child);
			const bool bCurrentlyShown = IsOwnVisibilityShown(Child);
			if (bShow && !bCurrentlyShown) Child->SetVisibility(ESlateVisibility::HitTestInvisible);
			else if (!bShow && bCurrentlyShown) Child->SetVisibility(ESlateVisibility::Collapsed);
		}
	}
}

void URoomPlannerWidget::OnCatalogInteriorClicked() { SetActiveCatalogTab(EPlannerPlacementKind::Object); }
void URoomPlannerWidget::OnCatalogCabinetsClicked() { SetActiveCatalogTab(EPlannerPlacementKind::CabinetSet); }

void URoomPlannerWidget::UpdateGuidanceHintText()
{
	if (!TxtGuidanceHint)
	{
		return;
	}

	if (CurrentViewMode == ERoomPlannerViewMode::View3D)
	{
		TxtGuidanceHint->SetText(FText::FromString(TEXT("3D: кликните по стене, полу, потолку или объекту и назначьте «Краску» или «Плитку» • ПКМ — вращение камеры • колесо — зум • «2D» — редактирование плана")));
		return;
	}

	if (!PlannerManager)
	{
		TxtGuidanceHint->SetText(FText::FromString(TEXT("Зажмите ЛКМ и потяните мышь, чтобы нарисовать первую стену, или нажмите «4×4 м»")));
		return;
	}

	if (PlannerManager->IsWallDrawingActive())
	{
		if (bIsAngleSnapped)
		{
			TxtGuidanceHint->SetText(FText::FromString(TEXT("Привязка активна. Отпустите ЛКМ для фиксации/стыковки стены")));
		}
		else
		{
			TxtGuidanceHint->SetText(FText::FromString(TEXT("Тяните стену до нужной длины. Отпустите ЛКМ, чтобы зафиксировать")));
		}
		return;
	}

	// «Каталог» / «Отделка» on a plan without walls: the room comes first.
	if (bCategoryLayoutBuilt && Active2DCategory != EPlannerPanelCategory::Layout && PlannerManager->GetWallCount() == 0)
	{
		TxtGuidanceHint->SetText(FText::FromString(TEXT("Сначала постройте комнату: нарисуйте стену на плане или нажмите «4×4 м»")));
		return;
	}

	switch (PlannerManager->ActiveToolMode)
	{
	case EPlannerToolMode::DrawWall:
		if (PlannerManager->GetWallCount() == 0)
		{
			TxtGuidanceHint->SetText(FText::FromString(TEXT("Зажмите ЛКМ и потяните мышь, чтобы нарисовать первую стену, или нажмите «4×4 м»")));
		}
		else
		{
			TxtGuidanceHint->SetText(FText::FromString(TEXT("Зажмите и тяните ЛКМ для создания стены. Подведите к существующему углу для соединения")));
		}
		break;

	case EPlannerToolMode::Select:
		if (PlannerManager->IsNodeDragActive())
		{
			TxtGuidanceHint->SetText(FText::FromString(TEXT("Перетаскивание угла: длины стен обновляются в реальном времени. Отпустите ЛКМ, чтобы применить")));
		}
		else if (bCategoryLayoutBuilt && Active2DCategory == EPlannerPanelCategory::Finish && PlannerManager->GetSelectionKind() != EPlannerSelectionKind::None
			&& PlannerManager->SelectedRoomID == -1)
		{
			// «Отделка» shows the finishes only (no size fields, no creation blocks, no rotate): the hint says what this page does. The
			// wheel still turns a free object here; one on a wall is refused (its wall decides its rotation), so it is not offered.
			const EPlannerSelectionKind Kind = PlannerManager->GetSelectionKind();
			TxtGuidanceHint->SetText(FText::FromString(Kind == EPlannerSelectionKind::Wall ? TEXT("Стена выбрана: назначьте «Краску» или «Плитку» этой стороне стены")
				: (Kind == EPlannerSelectionKind::Opening ? TEXT("Проём выбран: назначьте краску наличнику / раме")
				: (Kind == EPlannerSelectionKind::Object ? (PlannerManager->IsSelectionWallAttached() ? TEXT("Объект выбран: назначьте «Краску»")
					: TEXT("Объект выбран: назначьте «Краску» • колесо мыши — поворот"))
				: TEXT("Отделка для тумб недоступна: выберите стену, пол, дверь / окно или объект")))));
		}
		else if (PlannerManager->SelectedSegmentID != -1 && PlannerManager->SelectedOpeningIndex != -1)
		{
			TxtGuidanceHint->SetText(FText::FromString(TEXT("Проём выбран: тяните его вдоль стены; размеры, открывание и стиль — в панели слева")));
		}
		else if (PlannerManager->SelectedSegmentID != -1)
		{
			TxtGuidanceHint->SetText(FText::FromString(TEXT("Стена выбрана: тяните её углы, измените длину, добавьте дверь/окно или назначьте отделку")));
		}
		else if (PlannerManager->SelectedRoomID != -1)
		{
			// The room surface that is actually selected (a baseboard or a ceiling carried over from 3D is not "the floor").
			const EPlannerSelectionKind Surface = PlannerManager->GetSelectionKind();
			TxtGuidanceHint->SetText(FText::FromString(Surface == EPlannerSelectionKind::Baseboard ? TEXT("Плинтус выбран: назначьте краску или плитку на вкладке «Отделка»")
				: (Surface == EPlannerSelectionKind::Ceiling ? TEXT("Потолок выбран: назначьте краску или плитку на вкладке «Отделка»")
				: TEXT("Пол выбран: назначьте краску или плитку на вкладке «Отделка»"))));
		}
		else if (!PlannerManager->SelectedObjectID.IsEmpty() || !PlannerManager->SelectedCabinetSetID.IsEmpty())
		{
			// A wall-attached object / cabinet set only slides along its wall: the wheel and the rotate buttons are refused for it.
			TxtGuidanceHint->SetText(FText::FromString(PlannerManager->IsSelectionWallAttached()
				? TEXT("Объект закреплён на стене: тяните вдоль стены, удалите клавишей Delete")
				: TEXT("Объект выбран: тяните для перемещения, поворачивайте колесом мыши или кнопками, удалите клавишей Delete")));
		}
		else if (bCategoryLayoutBuilt && Active2DCategory == EPlannerPanelCategory::Catalog)
		{
			// Cabinet sets are wall-only: the wheel has nothing to turn during their drag (PlannerPanelRules::ResolveWheelTarget), so
			// their tab does not offer it.
			const bool bCabinetTab = HasActiveCatalogTab() && GetActiveCatalogTab() == EPlannerPlacementKind::CabinetSet;
			TxtGuidanceHint->SetText(FText::FromString(bCabinetTab ? TEXT("Перетащите гарнитур из каталога к стене")
				: TEXT("Перетащите карточку из каталога на пол или на стену; колесо мыши при перетаскивании поворачивает объект")));
		}
		else if (bCategoryLayoutBuilt && Active2DCategory == EPlannerPanelCategory::Finish)
		{
			TxtGuidanceHint->SetText(FText::FromString(TEXT("Кликните по стене, полу, двери / окну или объекту, затем «Краска» или «Плитка»")));
		}
		else
		{
			TxtGuidanceHint->SetText(FText::FromString(TEXT("Кликните по стене, двери/окну, полу или объекту, чтобы настроить их параметры. Углы стен можно тянуть")));
		}
		break;

	case EPlannerToolMode::PlaceFurniture:
		if (PlannerManager->PendingPlacementKind == EPlannerPlacementKind::Object)
		{
			// The yaw the wheel has given the armed object (placed on the floor with it; on a wall it follows the wall).
			TxtGuidanceHint->SetText(FText::FromString(FString::Printf(TEXT("Кликните на плане, чтобы разместить выбранный объект • колесо мыши — поворот (%s)"),
				*PlannerPanelRules::FormatPlanAngle(PlannerManager->PendingPlacementYawDeg))));
		}
		else
		{
			TxtGuidanceHint->SetText(FText::FromString(TEXT("Кликните на плане, чтобы разместить выбранный объект")));
		}
		break;

	case EPlannerToolMode::Erase:
		TxtGuidanceHint->SetText(FText::FromString(TEXT("Режим удаления: Кликните по любой стене, чтобы удалить её")));
		break;

	default:
		TxtGuidanceHint->SetText(FText::FromString(TEXT("2D Режим: Нажмите и тяните ЛКМ для создания стены")));
		break;
	}
}

AAwsTutorial_PlayerController* URoomPlannerWidget::GetPreviewController() const
{
	if (APlayerController* PC = GetOwningPlayer())
	{
		return Cast<AAwsTutorial_PlayerController>(PC);
	}
	if (UWorld* World = GetWorld())
	{
		return Cast<AAwsTutorial_PlayerController>(UGameplayStatics::GetPlayerController(World, 0));
	}
	return nullptr;
}

void URoomPlannerWidget::HandleRoomPlannerUpdated(const FString& JSONState)
{
	bPlanEmptyCached = IsPlanEmpty();
	// «4×4 м» built: its walls are here, so the new room is ready to edit with «Выбрать» (a stale request expires).
	if (bSelectToolAfterPresetPending && PlannerManager)
	{
		const double Now = GetWorld() ? GetWorld()->GetRealTimeSeconds() : 0.0;
		if (Now - PresetRequestTime > 10.0)
		{
			bSelectToolAfterPresetPending = false;
		}
		else if (PlannerManager->GetWallCount() > 0)
		{
			bSelectToolAfterPresetPending = false;
			if (CurrentViewMode == ERoomPlannerViewMode::View2D && PlannerManager->ActiveToolMode == EPlannerToolMode::DrawWall)
			{
				SetToolMode(EPlannerToolMode::Select);
			}
		}
	}
	// A catalog drop has arrived: select the new item (the drawer stays open).
	TrySelectDroppedItem();
	EnforceCategoryTool();
	UpdateSummaryStatsUI();
	UpdateDynamicPropertiesPanel();
	UpdateToolModeButtonStyles();
	UpdateFinishUI();
	ApplyCategoryVisibility();
}

void URoomPlannerWidget::On2DViewClicked() { SetViewMode(ERoomPlannerViewMode::View2D); }
void URoomPlannerWidget::On3DViewClicked() { SetViewMode(ERoomPlannerViewMode::View3D); }
void URoomPlannerWidget::OnSelectToolClicked() 
{ 
	if (PlannerManager && PlannerManager->GetWallCount() > 0)
	{
		SetToolMode(EPlannerToolMode::Select); 
	}
}
void URoomPlannerWidget::OnDrawWallToolClicked() { SetToolMode(EPlannerToolMode::DrawWall); }
void URoomPlannerWidget::OnDeleteToolClicked()
{
	if (PlannerManager)
	{
		if (PlannerManager->GetSelectionKind() != EPlannerSelectionKind::None)
		{
			DeleteSelected();
		}
		else
		{
			SetToolMode(EPlannerToolMode::Erase);
		}
	}
}

void URoomPlannerWidget::OnAddDoorClicked()
{
	if (!PlannerManager || PlannerManager->SelectedSegmentID == -1) return;
	float W = 0.9f;
	float H = 2.1f;
	if (EditableTxtOpeningWidth && !EditableTxtOpeningWidth->GetText().IsEmpty()) W = FCString::Atof(*EditableTxtOpeningWidth->GetText().ToString()) / 100.f;
	if (EditableTxtOpeningHeight && !EditableTxtOpeningHeight->GetText().IsEmpty()) H = FCString::Atof(*EditableTxtOpeningHeight->GetText().ToString()) / 100.f;
	if (AAwsTutorial_PlayerController* PC = GetPreviewController())
	{
		PC->Server_AddDoor(PlannerManager->SelectedSegmentID, W, H);
	}
}

void URoomPlannerWidget::OnAddWindowClicked()
{
	if (!PlannerManager || PlannerManager->SelectedSegmentID == -1) return;
	float W = 1.2f;
	float H = 1.2f;
	float S = 0.9f;
	if (EditableTxtOpeningWidth_1 && !EditableTxtOpeningWidth_1->GetText().IsEmpty()) W = FCString::Atof(*EditableTxtOpeningWidth_1->GetText().ToString()) / 100.f;
	if (EditableTxtOpeningHeight_1 && !EditableTxtOpeningHeight_1->GetText().IsEmpty()) H = FCString::Atof(*EditableTxtOpeningHeight_1->GetText().ToString()) / 100.f;
	if (EditableTxtOpeningSillHeight && !EditableTxtOpeningSillHeight->GetText().IsEmpty()) S = FCString::Atof(*EditableTxtOpeningSillHeight->GetText().ToString()) / 100.f;
	if (AAwsTutorial_PlayerController* PC = GetPreviewController())
	{
		PC->Server_AddWindow(PlannerManager->SelectedSegmentID, W, H, S);
	}
}

void URoomPlannerWidget::OnPresetRoomClicked() { BuildPreset4x4mRoom(); }
void URoomPlannerWidget::OnClearLayoutClicked() { ClearLayout(); }
void URoomPlannerWidget::OnToggleCeilingClicked() { if (PlannerManager) PlannerManager->ToggleCeilingVisibility(); UpdateViewModeButtonStyles(); }

void URoomPlannerWidget::ClosePlanner()
{
	if (bHasCachedPlayerTransform)
	{
		AAwsTutorial_PlayerController* PC = GetPreviewController();
		if (PC)
		{
			UE_LOG(LogTemp, Warning, TEXT("[RoomPlanner] Restoring player to original location: %s (Rot: %s)"),
				*CachedOriginalPlayerLocation.ToString(), *CachedOriginalPlayerRotation.ToString());

			PC->Server_ExitRoomPlanner(CachedOriginalPlayerLocation, CachedOriginalPlayerRotation);
			PC->RestorePlayerCamera();
		}
		bHasCachedPlayerTransform = false;
	}

	RemoveFromParent();
}

void URoomPlannerWidget::OnCloseClicked()
{
	ClosePlanner();
}

void URoomPlannerWidget::OnWallSelected(int32 SegmentID, float LengthMeters)
{
	if (AAwsTutorial_PlayerController* PC = GetPreviewController())
	{
		// 2D only: a wall picked in 3D must not change the plan camera (the 3D → 2D switch sets it from the tool).
		if (SegmentID != -1 && CurrentViewMode == ERoomPlannerViewMode::View2D)
		{
			PC->UpdateRoomPlannerCameraToolMode(EPlannerToolMode::Select);
		}
	}

	UpdateDynamicPropertiesPanel();
	UpdateGuidanceHintText();
	UpdateSummaryStatsUI(); // a floor pick arrives here too (SegmentID -1): the floor area follows the selected room

	// Update creation tool fields with some defaults if they are empty
	if (EditableTxtOpeningWidth && EditableTxtOpeningWidth->GetText().IsEmpty()) EditableTxtOpeningWidth->SetText(FText::FromString(TEXT("90")));
	if (EditableTxtOpeningHeight && EditableTxtOpeningHeight->GetText().IsEmpty()) EditableTxtOpeningHeight->SetText(FText::FromString(TEXT("210")));
	if (EditableTxtOpeningWidth_1 && EditableTxtOpeningWidth_1->GetText().IsEmpty()) EditableTxtOpeningWidth_1->SetText(FText::FromString(TEXT("120")));
	if (EditableTxtOpeningHeight_1 && EditableTxtOpeningHeight_1->GetText().IsEmpty()) EditableTxtOpeningHeight_1->SetText(FText::FromString(TEXT("120")));
	if (EditableTxtOpeningSillHeight && EditableTxtOpeningSillHeight->GetText().IsEmpty()) EditableTxtOpeningSillHeight->SetText(FText::FromString(TEXT("90")));
}

#include "Components/Image.h"

void URoomPlannerWidget::UpdateDynamicPropertiesPanel()
{
	if (!PlannerManager) return;

	// The size fields, «Добавить на стену», swing, style and rotate are the Context's editor: 2D, not on «Отделка» (which shows the
	// title, a one-line size summary and the finishes instead).
	const bool bEditorVisible = IsContextEditorVisible();

	if (PlannerManager->SelectedSegmentID != -1 && bEditorVisible)
	{
		if (PlannerManager->SelectedOpeningIndex != -1)
		{
			// An opening is selected
			float W_m = 0.f, H_m = 0.f, Sill_m = 0.f;
			if (PlannerManager->GetOpeningDetails(PlannerManager->SelectedSegmentID, PlannerManager->SelectedOpeningIndex, W_m, H_m, Sill_m))
			{
				float W_cm = W_m * 100.f;
				float H_cm = H_m * 100.f;
				float S_cm = Sill_m * 100.f;

				if (EditableTxtProp1)
				{
					EditableTxtProp1->SetVisibility(ESlateVisibility::Visible);
					EditableTxtProp1->SetText(FText::FromString(FString::Printf(TEXT("%.0f"), W_cm)));
				}
				if (EditableTxtProp2)
				{
					EditableTxtProp2->SetVisibility(ESlateVisibility::Visible);
					EditableTxtProp2->SetText(FText::FromString(FString::Printf(TEXT("%.0f"), H_cm)));
				}

				EOpeningType SelectedOpeningType = (S_cm > 1.0f) ? EOpeningType::Window : EOpeningType::Door;
				PlannerManager->GetOpeningType(PlannerManager->SelectedSegmentID, PlannerManager->SelectedOpeningIndex, SelectedOpeningType);
				if (SelectedOpeningType == EOpeningType::Window || S_cm > 1.0f) // a window (or a raised opening): the sill height is editable
				{
					if (EditableTxtProp3)
					{
						EditableTxtProp3->SetVisibility(ESlateVisibility::Visible);
					EditableTxtProp3->SetText(FText::FromString(FString::Printf(TEXT("%.0f"), S_cm)));
				}
				if (LblWallSize) LblWallSize->SetText(FText::FromString(TEXT("Окно, см: ширина · высота · высота от пола")));
				if (TxtApplyProperties) TxtApplyProperties->SetText(FText::FromString(TEXT("Применить")));
				}
				else // It's a door
				{
					if (EditableTxtProp3) EditableTxtProp3->SetVisibility(ESlateVisibility::Collapsed); // no empty slot in the size row
					if (LblWallSize) LblWallSize->SetText(FText::FromString((SelectedOpeningType == EOpeningType::Archway ? TEXT("Проём, см: ширина · высота") : TEXT("Дверь, см: ширина · высота"))));
					if (TxtApplyProperties) TxtApplyProperties->SetText(FText::FromString(TEXT("Применить")));
				}
			}
		}
		else
		{
			// Only Wall is selected
			if (EditableTxtProp1)
			{
				EditableTxtProp1->SetVisibility(ESlateVisibility::Visible);
				
				float LenCm = PlannerManager->GetWallLength(PlannerManager->SelectedSegmentID) * 100.f;
				EditableTxtProp1->SetText(FText::FromString(FString::Printf(TEXT("%.0f"), LenCm)));
			}
			// REQ-01: Prop2 = wall height (cm), Prop3 = wall thickness (cm)
			FWallSegment SelSeg;
			const bool bHaveSeg = PlannerManager->GetWallSegmentData(PlannerManager->SelectedSegmentID, SelSeg);
			if (EditableTxtProp2)
			{
				EditableTxtProp2->SetVisibility(ESlateVisibility::Visible);
				EditableTxtProp2->SetText(FText::FromString(FString::Printf(TEXT("%.0f"), bHaveSeg ? SelSeg.Height : 280.f)));
			}
			if (EditableTxtProp3)
			{
				EditableTxtProp3->SetVisibility(ESlateVisibility::Visible);
				EditableTxtProp3->SetText(FText::FromString(FString::Printf(TEXT("%.0f"), bHaveSeg ? SelSeg.Thickness : 20.f)));
			}
			if (LblWallSize) LblWallSize->SetText(FText::FromString(TEXT("Стена, см: длина · высота · толщина")));
			if (TxtApplyProperties) TxtApplyProperties->SetText(FText::FromString(TEXT("Применить")));
		}

		if (BtnApplyProperties) BtnApplyProperties->SetVisibility(ESlateVisibility::Visible);
		if (BtnDeleteTool) BtnDeleteTool->SetVisibility(ESlateVisibility::Visible);
		if (Image_2) Image_2->SetVisibility(ESlateVisibility::Visible);
		if (Border_wall_size) Border_wall_size->SetVisibility(ESlateVisibility::Visible);
	}
	else
	{
		// No wall / opening selected (nothing, or a floor / object / cabinet set)
		if (Border_wall_size) Border_wall_size->SetVisibility(ESlateVisibility::Collapsed);
		if (EditableTxtProp1) EditableTxtProp1->SetVisibility(ESlateVisibility::Hidden);
		if (EditableTxtProp2) EditableTxtProp2->SetVisibility(ESlateVisibility::Hidden);
		if (EditableTxtProp3) EditableTxtProp3->SetVisibility(ESlateVisibility::Hidden);
		if (TxtApplyProperties) TxtApplyProperties->SetText(FText::FromString(TEXT("Применить")));

		// Editing (delete / rotate / swing / placement) is a 2D-only workflow, matching the existing panel logic.
		const bool bOtherSelection = (CurrentViewMode == ERoomPlannerViewMode::View2D) && (PlannerManager->GetSelectionKind() != EPlannerSelectionKind::None);
		if (BtnApplyProperties) BtnApplyProperties->SetVisibility(ESlateVisibility::Collapsed);
		if (BtnDeleteTool) BtnDeleteTool->SetVisibility(bOtherSelection ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
		if (Image_2) Image_2->SetVisibility(bOtherSelection ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}

	const bool bIs2DPanel = (CurrentViewMode == ERoomPlannerViewMode::View2D);

	// REQ-07: swing controls only for a selected door / window, 2D only
	const EPlannerSelectionKind Kind = PlannerManager->GetSelectionKind();
	const ESlateVisibility SwingVis = (bEditorVisible && Kind == EPlannerSelectionKind::Opening) ? ESlateVisibility::Visible : ESlateVisibility::Collapsed;
	if (BtnSwingLeft) BtnSwingLeft->SetVisibility(SwingVis);
	if (BtnSwingRight) BtnSwingRight->SetVisibility(SwingVis);
	if (BtnSwingInward) BtnSwingInward->SetVisibility(SwingVis);
	if (BtnSwingOutward) BtnSwingOutward->SetVisibility(SwingVis);
	if (SwingRow) SwingRow->SetVisibility(SwingVis == ESlateVisibility::Visible ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
	if (Kind == EPlannerSelectionKind::Opening)
	{
		EOpeningSwingSide Side; EOpeningSwingDirection Dir;
		if (GetSelectedOpeningSwing(Side, Dir))
		{
			ApplyButtonState(BtnSwingLeft, Side == EOpeningSwingSide::Left ? EPanelButtonState::On : EPanelButtonState::Idle);
			ApplyButtonState(BtnSwingRight, Side == EOpeningSwingSide::Right ? EPanelButtonState::On : EPanelButtonState::Idle);
			ApplyButtonState(BtnSwingInward, Dir == EOpeningSwingDirection::Inward ? EPanelButtonState::On : EPanelButtonState::Idle);
			ApplyButtonState(BtnSwingOutward, Dir == EOpeningSwingDirection::Outward ? EPanelButtonState::On : EPanelButtonState::Idle);
		}
	}

	// Door / window / archway style picker (built-in catalog): 2D only, like the swing controls.
	if (StyleRow)
	{
		const bool bShowStyles = bEditorVisible && Kind == EPlannerSelectionKind::Opening;
		StyleRow->SetVisibility(bShowStyles ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
		if (StyleCaption) StyleCaption->SetVisibility(bShowStyles ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
		if (bShowStyles)
		{
			RefreshStyleRow();
		}
	}

	// REQ-17 / REQ-18: rotate controls for objects / cabinet sets. An object hung on a wall takes its rotation from the wall: the
	// buttons stay (the row does not jump) but are disabled and say why.
	const ESlateVisibility RotVis = (bEditorVisible && (Kind == EPlannerSelectionKind::Object || Kind == EPlannerSelectionKind::CabinetSet)) ? ESlateVisibility::Visible : ESlateVisibility::Collapsed;
	if (BtnRotateLeft) BtnRotateLeft->SetVisibility(RotVis);
	if (BtnRotateRight) BtnRotateRight->SetVisibility(RotVis);
	if (RotateRow) RotateRow->SetVisibility(RotVis == ESlateVisibility::Visible ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
	if (RotVis == ESlateVisibility::Visible)
	{
		const bool bRotatable = !PlannerManager->IsSelectionWallAttached();
		UButton* RotateButtons[] = { BtnRotateLeft.Get(), BtnRotateRight.Get() };
		for (UButton* Button : RotateButtons)
		{
			if (!Button || Button->GetIsEnabled() == bRotatable) continue;
			Button->SetIsEnabled(bRotatable);
			Button->SetToolTipText(FText::FromString(!bRotatable ? ARoomPlannerManager::WallAttachedRotationMessage
				: (Button == BtnRotateLeft ? TEXT("Повернуть объект на 15° против часовой стрелки") : TEXT("Повернуть объект на 15° по часовой стрелке"))));
		}
	}

	// Selection header: "<type / name>   [Delete]", in 2D and 3D (in 3D it names the picked surface the finishes go to). Delete only
	// in 2D and only for what can be deleted (a wall, an opening, an object, a cabinet set; not a floor, ceiling or baseboard).
	{
		FString Title = GetSelectionTitleText();
		if (Kind == EPlannerSelectionKind::Wall && !bEditorVisible && !Title.IsEmpty())
		{
			Title += PlannerManager->IsSelectedWallFaceInterior() ? TEXT(" · сторона в комнату") : TEXT(" · наружная сторона");
		}
		const bool bShowHeader = !Title.IsEmpty();
		if (TxtSelectionTitle)
		{
			TxtSelectionTitle->SetText(FText::FromString(Title));
			TxtSelectionTitle->SetVisibility(bShowHeader ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
		}
		if (SelectionHeaderRow) SelectionHeaderRow->SetVisibility(bShowHeader ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
		const bool bDeletable = Kind == EPlannerSelectionKind::Wall || Kind == EPlannerSelectionKind::Opening
			|| Kind == EPlannerSelectionKind::Object || Kind == EPlannerSelectionKind::CabinetSet;
		if (BtnDeleteTool) BtnDeleteTool->SetVisibility((bIs2DPanel && bDeletable) ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
		if (Image_2) Image_2->SetVisibility((bIs2DPanel && bShowHeader) ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}
	if (BtnCancelPlacement) BtnCancelPlacement->SetVisibility((bIs2DPanel && PlannerManager->HasPendingPlacement()) ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);

	UpdateCreationBlocksVisibility();
	UpdateContextSummary(nullptr);
	UpdateFinishUI();
}

FString URoomPlannerWidget::GetSelectionTitleText() const
{
	if (!PlannerManager) return FString();
	switch (PlannerManager->GetSelectionKind())
	{
	case EPlannerSelectionKind::Wall:
		return TEXT("Стена");
	case EPlannerSelectionKind::Opening:
	{
		EOpeningType Type = EOpeningType::Door;
		if (PlannerManager->GetOpeningType(PlannerManager->SelectedSegmentID, PlannerManager->SelectedOpeningIndex, Type))
		{
			return Type == EOpeningType::Window ? TEXT("Окно") : (Type == EOpeningType::Archway ? TEXT("Проём") : TEXT("Дверь"));
		}
		return TEXT("Дверь");
	}
	case EPlannerSelectionKind::Floor:
		return TEXT("Пол");
	case EPlannerSelectionKind::Ceiling:
		return TEXT("Потолок");
	case EPlannerSelectionKind::Baseboard:
		return TEXT("Плинтус");
	case EPlannerSelectionKind::Object:
	{
		FString AssetID;
		for (const FPlacedFurnitureData& Obj : PlannerManager->GetPlacedObjects())
		{
			if (Obj.InstanceID == PlannerManager->SelectedObjectID) { AssetID = Obj.AssetID; break; }
		}
		FPlannerCatalogEntry Entry; // a row hidden from the catalog (bHideInCatalog) still names what was placed from it
		if (PlannerManager->FindObjectCatalogEntry(AssetID, Entry) && !Entry.DisplayName.IsEmpty()) return Entry.DisplayName.ToString();
		return AssetID.IsEmpty() ? FString(TEXT("Объект")) : AssetID;
	}
	case EPlannerSelectionKind::CabinetSet:
	{
		FString ProductID;
		for (const FPlacedCabinetSetData& Set : PlannerManager->GetCabinetSets())
		{
			if (Set.InstanceID == PlannerManager->SelectedCabinetSetID) { ProductID = Set.ProductID.ToString(); break; }
		}
		for (const FPlannerCatalogEntry& Entry : PlannerManager->GetAvailableCabinetSets())
		{
			if (Entry.ID == ProductID && !Entry.DisplayName.IsEmpty()) return Entry.DisplayName.ToString();
		}
		return ProductID.IsEmpty() ? FString(TEXT("Тумбы")) : ProductID;
	}
	default:
		return FString();
	}
}

void URoomPlannerWidget::OnHelpShowClicked()
{
	bHelpBarHidden = false;
	if (HorizontalBox_3) HorizontalBox_3->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
}

void URoomPlannerWidget::OnHelpHideClicked()
{
	bHelpBarHidden = true;
	if (HorizontalBox_3) HorizontalBox_3->SetVisibility(ESlateVisibility::Collapsed);
}

void URoomPlannerWidget::OnApplyPropertiesClicked()
{
	if (!PlannerManager || PlannerManager->SelectedSegmentID == -1) return;

	AAwsTutorial_PlayerController* PC = GetPreviewController();
	if (!PC) return;

	if (PlannerManager->SelectedOpeningIndex != -1)
	{
		// Modify Opening
		float W_m = 0.f, H_m = 0.f, Sill_m = 0.f;
		if (PlannerManager->GetOpeningDetails(PlannerManager->SelectedSegmentID, PlannerManager->SelectedOpeningIndex, W_m, H_m, Sill_m))
		{
			float W = W_m * 100.f;
			float H = H_m * 100.f;
			float S = Sill_m * 100.f;

			if (EditableTxtProp1) W = FCString::Atof(*EditableTxtProp1->GetText().ToString());
			if (EditableTxtProp2) H = FCString::Atof(*EditableTxtProp2->GetText().ToString());
			if (EditableTxtProp3 && EditableTxtProp3->GetVisibility() == ESlateVisibility::Visible)
			{
				S = FCString::Atof(*EditableTxtProp3->GetText().ToString());
			}

			PC->Server_UpdateOpeningDimensions(PlannerManager->SelectedSegmentID,
				PlannerManager->GetOpeningID(PlannerManager->SelectedSegmentID, PlannerManager->SelectedOpeningIndex), W / 100.f, H / 100.f, S / 100.f);
		}
	}
	else
	{
		// Modify Wall
		const int32 SegID = PlannerManager->SelectedSegmentID;

		// REQ-01: height (Prop2) and thickness (Prop3) in cm — sent only when either value actually changed.
		FWallSegment SelSeg;
		if (PlannerManager->GetWallSegmentData(SegID, SelSeg) && (EditableTxtProp2 || EditableTxtProp3))
		{
			float NewHeightCm = SelSeg.Height;
			float NewThicknessCm = SelSeg.Thickness;
			if (EditableTxtProp2 && !EditableTxtProp2->GetText().IsEmpty())
			{
				NewHeightCm = FCString::Atof(*EditableTxtProp2->GetText().ToString());
			}
			if (EditableTxtProp3 && !EditableTxtProp3->GetText().IsEmpty())
			{
				NewThicknessCm = FCString::Atof(*EditableTxtProp3->GetText().ToString());
			}
			if (!FMath::IsNearlyEqual(NewHeightCm, SelSeg.Height, 0.5f) || !FMath::IsNearlyEqual(NewThicknessCm, SelSeg.Thickness, 0.5f))
			{
				PC->Server_SetWallDimensions(SegID, NewHeightCm, NewThicknessCm);
			}
		}

		// Length (existing behaviour, unchanged)
		if (EditableTxtProp1)
		{
			float NewLenMeters = FCString::Atof(*EditableTxtProp1->GetText().ToString()) / 100.f;
			PC->Server_SetWallLength(SegID, NewLenMeters);
		}
	}
}

static float ParseLengthDimensionInput(const FString& InText, float DefaultIfInvalidMeters = 1.0f)
{
	FString Raw = InText;
	bool bHasCM = Raw.Contains(TEXT("cm"), ESearchCase::IgnoreCase);
	bool bHasM = !bHasCM && Raw.Contains(TEXT("m"), ESearchCase::IgnoreCase);

	Raw.ReplaceInline(TEXT("cm"), TEXT(""), ESearchCase::IgnoreCase);
	Raw.ReplaceInline(TEXT("m"), TEXT(""), ESearchCase::IgnoreCase);
	Raw.TrimStartAndEndInline();

	float Value = FCString::Atof(*Raw);
	if (Value <= 0.001f) return DefaultIfInvalidMeters;

	if (bHasCM) return Value / 100.f;
	if (bHasM) return Value;

	if (Value > 10.f)
	{
		return Value / 100.f;
	}
	return Value;
}

void URoomPlannerWidget::OnWallLengthCommitted(const FText& Text, ETextCommit::Type CommitMethod)
{
	if ((CommitMethod == ETextCommit::OnEnter || CommitMethod != ETextCommit::Default) && PlannerManager && PlannerManager->SelectedSegmentID != -1)
	{
		float NewLengthMeters = ParseLengthDimensionInput(Text.ToString(), 4.0f);
		if (NewLengthMeters > 0.1f)
		{
			if (AAwsTutorial_PlayerController* PC = GetPreviewController())
			{
				PC->Server_SetWallLength(PlannerManager->SelectedSegmentID, NewLengthMeters);
			}
		}
	}
}

void URoomPlannerWidget::OnOpeningWidthCommitted(const FText& Text, ETextCommit::Type CommitMethod)
{
	if ((CommitMethod == ETextCommit::OnEnter || CommitMethod != ETextCommit::Default) && PlannerManager && PlannerManager->SelectedSegmentID != -1)
	{
		int32 SegID = PlannerManager->SelectedSegmentID;
		// No fallback to the wall's first opening: a value typed with no opening selected must not resize an arbitrary door.
		const int32 OpIdx = PlannerManager->SelectedOpeningIndex;
		if (OpIdx == -1) return;

		float CurWidthM = 0.9f, CurHeightM = 2.1f, CurSillM = 0.f;
		PlannerManager->GetOpeningDetails(SegID, OpIdx, CurWidthM, CurHeightM, CurSillM);

		float NewWidthMeters = ParseLengthDimensionInput(Text.ToString(), CurWidthM);
		if (NewWidthMeters > 0.1f)
		{
			if (AAwsTutorial_PlayerController* PC = GetPreviewController())
			{
				PC->Server_UpdateOpeningDimensions(SegID, PlannerManager->GetOpeningID(SegID, OpIdx), NewWidthMeters, CurHeightM, CurSillM);
			}
		}
	}
}

void URoomPlannerWidget::OnOpeningHeightCommitted(const FText& Text, ETextCommit::Type CommitMethod)
{
	if ((CommitMethod == ETextCommit::OnEnter || CommitMethod != ETextCommit::Default) && PlannerManager && PlannerManager->SelectedSegmentID != -1)
	{
		int32 SegID = PlannerManager->SelectedSegmentID;
		// No fallback to the wall's first opening: a value typed with no opening selected must not resize an arbitrary door.
		const int32 OpIdx = PlannerManager->SelectedOpeningIndex;
		if (OpIdx == -1) return;

		float CurWidthM = 0.9f, CurHeightM = 2.1f, CurSillM = 0.f;
		PlannerManager->GetOpeningDetails(SegID, OpIdx, CurWidthM, CurHeightM, CurSillM);

		float NewHeightMeters = ParseLengthDimensionInput(Text.ToString(), CurHeightM);
		if (NewHeightMeters > 0.1f)
		{
			if (AAwsTutorial_PlayerController* PC = GetPreviewController())
			{
				PC->Server_UpdateOpeningDimensions(SegID, PlannerManager->GetOpeningID(SegID, OpIdx), CurWidthM, NewHeightMeters, CurSillM);
			}
		}
	}
}

void URoomPlannerWidget::OnOpeningSillHeightCommitted(const FText& Text, ETextCommit::Type CommitMethod)
{
	if ((CommitMethod == ETextCommit::OnEnter || CommitMethod != ETextCommit::Default) && PlannerManager && PlannerManager->SelectedSegmentID != -1)
	{
		int32 SegID = PlannerManager->SelectedSegmentID;
		// No fallback to the wall's first opening: a value typed with no opening selected must not resize an arbitrary door.
		const int32 OpIdx = PlannerManager->SelectedOpeningIndex;
		if (OpIdx == -1) return;

		float CurWidthM = 0.9f, CurHeightM = 2.1f, CurSillM = 0.f;
		PlannerManager->GetOpeningDetails(SegID, OpIdx, CurWidthM, CurHeightM, CurSillM);

		float NewSillMeters = ParseLengthDimensionInput(Text.ToString(), CurSillM);
		if (AAwsTutorial_PlayerController* PC = GetPreviewController())
		{
			PC->Server_UpdateOpeningDimensions(SegID, PlannerManager->GetOpeningID(SegID, OpIdx), CurWidthM, CurHeightM, NewSillMeters);
		}
	}
}

void URoomPlannerWidget::UpdateMouseCursorPosition()
{
	if (!mouse_cursor)
	{
		mouse_cursor = Cast<UImage>(GetWidgetFromName(TEXT("mouse_cursor")));
	}

	const bool bIsDrawing = (bIsWidgetDrawingWall || (PlannerManager && PlannerManager->IsWallDrawingActive()));

	// Project the current drag end point once; the drawing cursor and the live length label share it.
	FVector2D ScreenPos = FVector2D::ZeroVector;
	bool bHaveScreenPos = false;
	if (bIsDrawing && PlannerManager)
	{
		APlayerController* PC = GetPreviewController();
		if (!PC) PC = GetOwningPlayer();
		if (PC && PC->ProjectWorldLocationToScreen(PlannerManager->GetDragCurrentPoint(), ScreenPos))
		{
			const float DPIScale = UWidgetLayoutLibrary::GetViewportScale(this);
			if (DPIScale > 0.001f) ScreenPos /= DPIScale;
			bHaveScreenPos = true;
		}
	}

	if (mouse_cursor)
	{
		if (bIsDrawing && PlannerManager)
		{
			mouse_cursor->SetVisibility(ESlateVisibility::HitTestInvisible);
			if (bHaveScreenPos)
			{
				if (UCanvasPanelSlot* CanvasSlot = Cast<UCanvasPanelSlot>(mouse_cursor->Slot))
				{
					CanvasSlot->SetPosition(ScreenPos);
					mouse_cursor->SetRenderTranslation(FVector2D::ZeroVector);
				}
				else
				{
					mouse_cursor->SetRenderTranslation(ScreenPos);
				}
			}
		}
		else
		{
			mouse_cursor->SetVisibility(ESlateVisibility::Collapsed);
		}
	}

	// Live length label: same anchor point as the drawing cursor, small offset up-right; visible only while drawing.
	UWidget* LenWidget = LiveLengthPanel ? LiveLengthPanel.Get() : Cast<UWidget>(TxtLiveLength.Get());
	if (LenWidget)
	{
		const ESlateVisibility LenVis = (bIsDrawing && bHaveScreenPos) ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed;
		if (LenWidget->GetVisibility() != LenVis) LenWidget->SetVisibility(LenVis);
		if (LenVis != ESlateVisibility::Collapsed)
		{
			static const FVector2D LiveLengthOffset(18.f, -12.f);
			const FVector2D Pos = ScreenPos + LiveLengthOffset;
			if (UCanvasPanelSlot* CanvasSlot = Cast<UCanvasPanelSlot>(LenWidget->Slot))
			{
				CanvasSlot->SetAnchors(FAnchors(0.f, 0.f));
				CanvasSlot->SetAlignment(FVector2D(0.f, 1.f)); // bottom-left corner of the label sits at the offset point
				CanvasSlot->SetAutoSize(true);
				CanvasSlot->SetPosition(Pos);
				LenWidget->SetRenderTranslation(FVector2D::ZeroVector);
			}
			else
			{
				LenWidget->SetRenderTranslation(Pos);
			}
		}
	}
}

void URoomPlannerWidget::HandleWallDragProgress(float LengthMeters, FVector MidpointWorld, float AngleDeg, bool bIsSnapped)
{
	CurrentDragLengthMeters = LengthMeters;
	CurrentDragMidpointWorld = MidpointWorld;
	bIsAngleSnapped = bIsSnapped;

	UpdateMouseCursorPosition();

	if (TxtLiveLength)
	{
		TxtLiveLength->SetText(FText::FromString(GetFormattedDragLengthText()));
	}

	if (SnapIndicator)
	{
		// The snap marker is the Draw Wall hover preview (cursor snapped to a wall endpoint). It must never follow
		// a corner drag: UpdateNodeDrag reports the dragged WALL's midpoint with the node's snap flag, which used to
		// park this marker in the middle of the wall whenever the last drag update was snapped.
		bool bShowSnapIndicator = bIsSnapped && PlannerManager && !PlannerManager->IsWallDrawingActive() && !PlannerManager->IsNodeDragActive();
		SnapIndicator->SetVisibility(bShowSnapIndicator ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
		if (bShowSnapIndicator)
		{
			if (APlayerController* PC = GetOwningPlayer())
			{
				FVector2D ScreenPos;
				if (PC->ProjectWorldLocationToScreen(MidpointWorld, ScreenPos))
				{
					float DPIScale = UWidgetLayoutLibrary::GetViewportScale(this);
					if (DPIScale > 0.001f)
					{
						ScreenPos /= DPIScale;
					}
					SnapIndicator->SetRenderTranslation(ScreenPos);
				}
			}
		}
	}

	UpdateSummaryStatsUI();
	UpdateGuidanceHintText();
	OnWallDragProgress(LengthMeters, MidpointWorld, AngleDeg, bIsSnapped);
}

void URoomPlannerWidget::UpdateSummaryStatsUI()
{
	if (TxtFloorArea)
	{
		// A selected room floor: that room's net area; otherwise all rooms together.
		float AreaM2 = GetFloorAreaM2();
		bool bSelectedRoom = false;
		FRoomData Room;
		if (PlannerManager && PlannerManager->GetSelectionKind() == EPlannerSelectionKind::Floor && PlannerManager->GetRoomData(PlannerManager->SelectedRoomID, Room))
		{
			AreaM2 = Room.AreaM2;
			bSelectedRoom = true;
		}
		TxtFloorArea->SetText(FText::FromString(FString::Printf(TEXT("%.2f м²"), AreaM2)));
		if (FloorAreaCaption)
		{
			FloorAreaCaption->SetText(bSelectedRoom ? FText::FromString(TEXT("Площадь пола (комната)")) : FloorAreaCaptionText);
		}
	}
	if (TxtPerimeter)
	{
		TxtPerimeter->SetText(FText::FromString(FString::Printf(TEXT("%.2f м"), GetPerimeterLengthM())));
	}

	// Floor area / perimeter are plan-editing information: 2D only, and only once at least one wall exists.
	const bool bHasWalls = PlannerManager && PlannerManager->GetWallCount() > 0;
	const bool bShowTotals = bHasWalls && CurrentViewMode == ERoomPlannerViewMode::View2D;
	const ESlateVisibility TotalsVis = bShowTotals ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed;
	if (TotalsBox)
	{
		if (TotalsBox->GetVisibility() != TotalsVis) TotalsBox->SetVisibility(TotalsVis);
	}
	else
	{
		if (TxtFloorArea) TxtFloorArea->SetVisibility(bShowTotals ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
		if (TxtPerimeter) TxtPerimeter->SetVisibility(bShowTotals ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}
}

FString URoomPlannerWidget::GetFormattedDragLengthText() const
{
	return FString::Printf(TEXT("%.2f м"), CurrentDragLengthMeters);
}

void URoomPlannerWidget::BuildPreset4x4mRoom()
{
	if (AAwsTutorial_PlayerController* PC = GetPreviewController())
	{
		PC->Server_BuildPreset4x4mRoom();
	}
}

void URoomPlannerWidget::ClearLayout()
{
	if (AAwsTutorial_PlayerController* PC = GetPreviewController())
	{
		PC->Server_ClearLayout();
	}
}

void URoomPlannerWidget::InsertDoor(int32 WallSegmentID, float DistanceAlongWallCm)
{
	if (AAwsTutorial_PlayerController* PC = GetPreviewController())
	{
		PC->Server_AddDoor(WallSegmentID, 0.9f, 2.1f, DistanceAlongWallCm);
	}
}

void URoomPlannerWidget::InsertWindow(int32 WallSegmentID, float DistanceAlongWallCm)
{
	if (AAwsTutorial_PlayerController* PC = GetPreviewController())
	{
		PC->Server_AddWindow(WallSegmentID, 1.2f, 1.2f, 0.9f, DistanceAlongWallCm);
	}
}

float URoomPlannerWidget::GetFloorAreaM2() const
{
	if (PlannerManager)
	{
		return PlannerManager->CalculateFloorAreaM2();
	}
	return 0.0f;
}

float URoomPlannerWidget::GetPerimeterLengthM() const
{
	if (PlannerManager)
	{
		return PlannerManager->CalculatePerimeterM();
	}
	return 0.0f;
}

// ═══════════════════════════════════════════════════════════════════════════════
// Manager delegate wiring
// ═══════════════════════════════════════════════════════════════════════════════

void URoomPlannerWidget::BindManagerDelegates()
{
	if (!PlannerManager && GetWorld())
	{
		PlannerManager = ARoomPlannerManager::GetOrCreateInstance(GetWorld());
	}
	if (!PlannerManager || bManagerDelegatesBound) return;

	PlannerManager->OnInteractiveWallDragProgress.AddUniqueDynamic(this, &URoomPlannerWidget::HandleWallDragProgress);
	PlannerManager->OnWallSelected.AddUniqueDynamic(this, &URoomPlannerWidget::OnWallSelected);
	PlannerManager->OnRoomPlannerUpdated.AddUniqueDynamic(this, &URoomPlannerWidget::HandleRoomPlannerUpdated);
	PlannerManager->OnOperationRejected.AddUniqueDynamic(this, &URoomPlannerWidget::HandleOperationRejected);
	PlannerManager->OnSelectionChanged.AddUniqueDynamic(this, &URoomPlannerWidget::HandleSelectionChanged);
	PlannerManager->OnFloorSelected.AddUniqueDynamic(this, &URoomPlannerWidget::HandleFloorSelected);
	PlannerManager->bPlannerUIOpen = true;
	// Input paths that read raw mouse state (the controller tick, the manager's corner drag) ask the panel at the moment of the press.
	PlannerManager->SetPlannerUIHitTest([WeakThis = TWeakObjectPtr<URoomPlannerWidget>(this)]()
	{
		return WeakThis.IsValid() && WeakThis->IsCursorOverPlannerUI();
	});
	// The controller's mouse wheel leaves the plan alone while a full-screen catalog hides this widget.
	PlannerManager->SetPlannerUIShownQuery([WeakThis = TWeakObjectPtr<URoomPlannerWidget>(this)]()
	{
		return WeakThis.IsValid() && WeakThis->IsVisible();
	});
	// The manager binds here (NativeConstruct), not in the tick fallback, so the session flag that drives the
	// planner's bounded 3D exposure must be raised here as well.
	PlannerManager->SetPlannerSessionActive(true);
	if (AAwsTutorial_PlayerController* PC = GetPreviewController())
	{
		PC->ApplyPlannerRoomLightSettings(); // BP_MaxiMallPlayerController override of the room light settings, if enabled
	}
	bManagerDelegatesBound = true;
}

void URoomPlannerWidget::HandleFloorSelected(int32 RoomID, float AreaM2)
{
	UpdateSummaryStatsUI();
}

void URoomPlannerWidget::UnbindManagerDelegates()
{
	if (!PlannerManager) return;
	PlannerManager->OnInteractiveWallDragProgress.RemoveAll(this);
	PlannerManager->OnWallSelected.RemoveAll(this);
	PlannerManager->OnRoomPlannerUpdated.RemoveAll(this);
	PlannerManager->OnOperationRejected.RemoveAll(this);
	PlannerManager->OnSelectionChanged.RemoveAll(this);
	PlannerManager->OnFloorSelected.RemoveAll(this);
	PlannerManager->ClearPlannerUIHitTest();
	PlannerManager->ClearPlannerUIShownQuery();
	bManagerDelegatesBound = false;
}

void URoomPlannerWidget::HandleOperationRejected(const FString& Reason)
{
	ShowStatusMessage(Reason, 4.f);
	OnOperationRejectedMessage(Reason);
}

void URoomPlannerWidget::ShowStatusMessage(const FString& Message, float Seconds)
{
	if (TxtOperationMessage)
	{
		TxtOperationMessage->SetText(FText::FromString(Message));
		TxtOperationMessage->SetVisibility(ESlateVisibility::HitTestInvisible);
	}
	if (MessageChip)
	{
		MessageChip->SetVisibility(ESlateVisibility::HitTestInvisible);
	}
	OperationMessageClearTime = GetWorld() ? GetWorld()->GetTimeSeconds() + Seconds : 0.f;
}

void URoomPlannerWidget::HandleSelectionChanged()
{
	UpdateDynamicPropertiesPanel();
	UpdateGuidanceHintText();
	UpdateSummaryStatsUI(); // the floor area follows the selected room

	const EPlannerSelectionKind Kind = GetSelectionKind();
	if (Kind != LastNotifiedSelectionKind)
	{
		LastNotifiedSelectionKind = Kind;
		OnSelectionKindChanged(Kind);
	}
}

EPlannerSelectionKind URoomPlannerWidget::GetSelectionKind() const
{
	return PlannerManager ? PlannerManager->GetSelectionKind() : EPlannerSelectionKind::None;
}

bool URoomPlannerWidget::DeprojectCursorToGround(FVector& OutGroundPos) const
{
	AAwsTutorial_PlayerController* PC = GetPreviewController();
	if (!PC) return false;
	FVector WorldOrigin, WorldDirection;
	if (PC->DeprojectMousePositionToWorld(WorldOrigin, WorldDirection) && !FMath::IsNearlyZero(WorldDirection.Z))
	{
		const float T = -WorldOrigin.Z / WorldDirection.Z;
		if (T >= 0.f)
		{
			OutGroundPos = WorldOrigin + T * WorldDirection;
			OutGroundPos.Z = 0.f;
			return true;
		}
	}
	return false;
}

// ═══════════════════════════════════════════════════════════════════════════════
// REQ-02 / REQ-04 / REQ-06: labels
// ═══════════════════════════════════════════════════════════════════════════════

TArray<FPlannerDimensionLabel> URoomPlannerWidget::GetSelectionLabels() const
{
	TArray<FPlannerDimensionLabel> Labels;
	if (!PlannerManager) return Labels;

	Labels = PlannerManager->GetSelectionDimensionLabels();

	APlayerController* PC = GetOwningPlayer();
	const float DPIScale = UWidgetLayoutLibrary::GetViewportScale(this);
	for (FPlannerDimensionLabel& L : Labels)
	{
		FVector2D ScreenPos;
		if (PC && PC->ProjectWorldLocationToScreen(L.WorldLocation, ScreenPos))
		{
			if (DPIScale > 0.001f) ScreenPos /= DPIScale;
			L.ScreenPosition = ScreenPos;
			L.bOnScreen = true;
		}
	}
	return Labels;
}

void URoomPlannerWidget::UpdateSelectionLabelsUI()
{
	// Runs every frame: with nothing selected the overlay only has to be emptied once, not rebuilt from screen projections.
	const bool bActive = PlannerManager && (PlannerManager->IsNodeDragActive() || PlannerManager->GetSelectionKind() != EPlannerSelectionKind::None);
	if (bActive)
	{
		UpdateDimensionOverlay();
	}
	else if (DimensionOverlay)
	{
		DimensionOverlay->SetDimensions(TArray<FPlannerScreenDimension>());
	}
	if (!PlannerManager) return;

	if (!bActive)
	{
		if (SelectionLabelPanel && SelectionLabelPanel->GetVisibility() != ESlateVisibility::Collapsed)
		{
			SelectionLabelPanel->SetVisibility(ESlateVisibility::Collapsed);
			OnSelectionLabelsUpdated(TArray<FPlannerDimensionLabel>());
		}
		if (TxtContextSummary && TxtContextSummary->GetVisibility() != ESlateVisibility::Collapsed)
		{
			UpdateContextSummary(nullptr);
		}
		UpdateFloatingContextBar(nullptr);
		return;
	}

	const TArray<FPlannerDimensionLabel> Labels = GetSelectionLabels();
	UpdateContextSummary(&Labels); // live while a corner or the object is dragged
	UpdateFloatingContextBar(&Labels);

	auto FindLabel = [&Labels](const TCHAR* Key) -> const FPlannerDimensionLabel*
	{
		for (const FPlannerDimensionLabel& L : Labels) if (L.Key == Key) return &L;
		return nullptr;
	};
	auto SetText = [](UTextBlock* Block, const FPlannerDimensionLabel* L)
	{
		if (!Block) return;
		if (L) { Block->SetText(FText::FromString(L->Text)); Block->SetVisibility(ESlateVisibility::HitTestInvisible); }
		else { Block->SetVisibility(ESlateVisibility::Collapsed); }
	};

	// Summary line
	if (TxtSelectedDims)
	{
		FString Dims;
		if (const FPlannerDimensionLabel* W = FindLabel(TEXT("width")))
		{
			const FPlannerDimensionLabel* H = FindLabel(TEXT("height"));
			Dims = H ? FString::Printf(TEXT("%s × %s"), *W->Text, *H->Text) : W->Text;
		}
		else if (const FPlannerDimensionLabel* Len = FindLabel(TEXT("length")))
		{
			Dims = Len->Text;
		}
		else if (const FPlannerDimensionLabel* Area = FindLabel(TEXT("area")))
		{
			Dims = Area->Text;
		}
		else if (const FPlannerDimensionLabel* Size = FindLabel(TEXT("size")))
		{
			Dims = Size->Text;
		}
		TxtSelectedDims->SetText(FText::FromString(Dims));
		TxtSelectedDims->SetVisibility(Dims.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
	}
	SetText(TxtDistLeft, FindLabel(TEXT("distLeft")));
	SetText(TxtDistRight, FindLabel(TEXT("distRight")));
	SetText(TxtDistFloor, FindLabel(TEXT("distFloor")));
	SetText(TxtDistNeighbor, FindLabel(TEXT("distNeighbor")));

	// Anchor the optional panel at the primary label's screen position. Walls (also while a corner is dragged) and doors / windows
	// have their values on their dimension lines instead (drawn by the overlay), so the panel stays folded for them.
	if (SelectionLabelPanel)
	{
		const EPlannerSelectionKind Kind = PlannerManager->GetSelectionKind();
		const bool bValuesOnDimensionLines = DimensionOverlay
			&& (PlannerManager->IsNodeDragActive() || Kind == EPlannerSelectionKind::Wall || Kind == EPlannerSelectionKind::Opening);
		const FPlannerDimensionLabel* Anchor = nullptr;
		if (!bValuesOnDimensionLines)
		{
			Anchor = FindLabel(TEXT("width"));
			if (!Anchor) Anchor = FindLabel(TEXT("length"));
			if (!Anchor) Anchor = FindLabel(TEXT("area"));
			if (!Anchor) Anchor = FindLabel(TEXT("size"));
		}
		if (Anchor && Anchor->bOnScreen)
		{
			SelectionLabelPanel->SetVisibility(ESlateVisibility::HitTestInvisible);
			if (UCanvasPanelSlot* CanvasSlot = Cast<UCanvasPanelSlot>(SelectionLabelPanel->Slot))
			{
				CanvasSlot->SetPosition(Anchor->ScreenPosition);
			}
			else
			{
				SelectionLabelPanel->SetRenderTranslation(Anchor->ScreenPosition);
			}
		}
		else
		{
			SelectionLabelPanel->SetVisibility(ESlateVisibility::Collapsed);
		}
	}

	OnSelectionLabelsUpdated(Labels);
}

// ═══════════════════════════════════════════════════════════════════════════════
// REQ-07: swing
// ═══════════════════════════════════════════════════════════════════════════════

void URoomPlannerWidget::SetSelectedOpeningSwing(EOpeningSwingSide Side, EOpeningSwingDirection Direction)
{
	if (!PlannerManager || PlannerManager->SelectedSegmentID == -1 || PlannerManager->SelectedOpeningIndex == -1) return;
	if (CurrentViewMode != ERoomPlannerViewMode::View2D) return; // editing openings is a 2D workflow
	if (AAwsTutorial_PlayerController* PC = GetPreviewController())
	{
		PC->Server_SetOpeningSwing(PlannerManager->SelectedSegmentID,
			PlannerManager->GetOpeningID(PlannerManager->SelectedSegmentID, PlannerManager->SelectedOpeningIndex), Side, Direction);
	}
}

void URoomPlannerWidget::SetSelectedSwingSide(EOpeningSwingSide Side)
{
	EOpeningSwingSide CurSide; EOpeningSwingDirection CurDir;
	if (GetSelectedOpeningSwing(CurSide, CurDir))
	{
		SetSelectedOpeningSwing(Side, CurDir);
	}
}

void URoomPlannerWidget::SetSelectedSwingDirection(EOpeningSwingDirection Direction)
{
	EOpeningSwingSide CurSide; EOpeningSwingDirection CurDir;
	if (GetSelectedOpeningSwing(CurSide, CurDir))
	{
		SetSelectedOpeningSwing(CurSide, Direction);
	}
}

bool URoomPlannerWidget::GetSelectedOpeningSwing(EOpeningSwingSide& OutSide, EOpeningSwingDirection& OutDirection) const
{
	if (!PlannerManager) return false;
	return PlannerManager->GetOpeningSwing(PlannerManager->SelectedSegmentID, PlannerManager->SelectedOpeningIndex, OutSide, OutDirection);
}

void URoomPlannerWidget::OnSwingLeftClicked() { SetSelectedSwingSide(EOpeningSwingSide::Left); }
void URoomPlannerWidget::OnSwingRightClicked() { SetSelectedSwingSide(EOpeningSwingSide::Right); }
void URoomPlannerWidget::OnSwingInwardClicked() { SetSelectedSwingDirection(EOpeningSwingDirection::Inward); }
void URoomPlannerWidget::OnSwingOutwardClicked() { SetSelectedSwingDirection(EOpeningSwingDirection::Outward); }

// ═══════════════════════════════════════════════════════════════════════════════
// Door / window / archway styles
// ═══════════════════════════════════════════════════════════════════════════════

void URoomPlannerWidget::RefreshStyleRow()
{
	if (!StyleRow || !PlannerManager || !WidgetTree) return;

	EOpeningType Type = EOpeningType::Door;
	if (!PlannerManager->GetOpeningType(PlannerManager->SelectedSegmentID, PlannerManager->SelectedOpeningIndex, Type)) return;
	FName CurrentStyle;
	PlannerManager->GetOpeningStyle(PlannerManager->SelectedSegmentID, PlannerManager->SelectedOpeningIndex, CurrentStyle);

	const FString Key = FString::FromInt((int32)Type);
	if (Key != StyleRowBuiltKey)
	{
		StyleRow->ClearChildren();
		for (const FPlannerOpeningStyle& Style : PlannerOpeningStyles::All())
		{
			if (Style.Type != Type) continue;

			UPlannerStyleButton* Button = WidgetTree->ConstructWidget<UPlannerStyleButton>(UPlannerStyleButton::StaticClass());
			UTextBlock* Label = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
			if (!Button || !Label) continue;

			// Our buttons' look (the "Добавить дверь" style: slate, rounded, #D9D9D9 outline) with the captions' Roboto Bold 10.5.
			if (const UButton* StyleSource = BtnAddDoor ? BtnAddDoor.Get() : BtnApplyProperties.Get())
			{
				Button->SetStyle(StyleSource->GetStyle());
			}
			Label->SetText(FText::FromString(Style.DisplayName));
			Label->SetFont(LblWallSize ? LblWallSize->GetFont() : FCoreStyle::GetDefaultFontStyle("Bold", 10.5f));
			Label->SetColorAndOpacity(FSlateColor(FLinearColor::White));
			if (UButtonSlot* LabelSlot = Cast<UButtonSlot>(Button->AddChild(Label)))
			{
				LabelSlot->SetPadding(FMargin(2.f, 1.f));
			}
			Button->StyleID = Style.ID;
			Button->SetToolTipText(FText::FromString(FString::Printf(TEXT("Стиль: %s"), *Style.DisplayName)));
			Button->OnStyleClicked.BindUObject(this, &URoomPlannerWidget::OnStyleButtonClicked);
			Button->BindClick();

			const FMargin ButtonPadding(2.f);
			UPanelSlot* ButtonSlot = StyleRow->AddChild(Button);
			if (UWrapBoxSlot* WrapSlot = Cast<UWrapBoxSlot>(ButtonSlot)) WrapSlot->SetPadding(ButtonPadding);
			else if (UHorizontalBoxSlot* HorizontalSlot = Cast<UHorizontalBoxSlot>(ButtonSlot)) HorizontalSlot->SetPadding(ButtonPadding);
			else if (UVerticalBoxSlot* VerticalSlot = Cast<UVerticalBoxSlot>(ButtonSlot)) VerticalSlot->SetPadding(ButtonPadding);
		}
		StyleRowBuiltKey = Key;
	}

	for (UWidget* Child : StyleRow->GetAllChildren())
	{
		if (UPlannerStyleButton* Button = Cast<UPlannerStyleButton>(Child))
		{
			ApplyButtonState(Button, Button->StyleID == CurrentStyle ? EPanelButtonState::On : EPanelButtonState::Idle);
		}
	}
}

void URoomPlannerWidget::SetSelectedOpeningStyle(FName StyleID)
{
	if (!PlannerManager || PlannerManager->SelectedSegmentID == -1 || PlannerManager->SelectedOpeningIndex == -1) return;
	if (CurrentViewMode != ERoomPlannerViewMode::View2D) return; // editing openings is a 2D workflow
	if (AAwsTutorial_PlayerController* PC = GetPreviewController())
	{
		PC->Server_SetOpeningStyle(PlannerManager->SelectedSegmentID,
			PlannerManager->GetOpeningID(PlannerManager->SelectedSegmentID, PlannerManager->SelectedOpeningIndex), StyleID);
	}
}

void URoomPlannerWidget::OnStyleButtonClicked(FName StyleID)
{
	SetSelectedOpeningStyle(StyleID);
}

// ═══════════════════════════════════════════════════════════════════════════════
// 3D view options: door / window leaves, exterior look
// ═══════════════════════════════════════════════════════════════════════════════

void URoomPlannerWidget::OnDoorsOpenClicked()
{
	if (!PlannerManager) return;
	PlannerManager->SetAllOpeningLeavesOpen(true);
	UpdateViewModeButtonStyles();
}

void URoomPlannerWidget::OnDoorsCloseClicked()
{
	if (!PlannerManager) return;
	PlannerManager->SetAllOpeningLeavesOpen(false);
	UpdateViewModeButtonStyles();
}

// ═══════════════════════════════════════════════════════════════════════════════
// REQ-13 / REQ-14: finishing
// ═══════════════════════════════════════════════════════════════════════════════

void URoomPlannerWidget::OpenPaintCatalogForSelection()
{
	if (!PlannerManager || !PlannerManager->CanApplyFinishToSelection())
	{
		HandleOperationRejected(TEXT("Сначала выберите поверхность: стену, пол, потолок, плинтус, дверь / окно или объект"));
		return;
	}

	TSubclassOf<UColorCatalogWidget> CatalogClass = PlannerColorCatalogWidgetClass;
	if (!CatalogClass)
	{
		CatalogClass = LoadClass<UColorCatalogWidget>(nullptr, TEXT("/Game/ColorCatalog/UI/WBP_ColorCatalog.WBP_ColorCatalog_C"));
	}
	if (!CatalogClass)
	{
		HandleOperationRejected(TEXT("Каталог цветов не найден (WBP_ColorCatalog)"));
		return;
	}

	if (::IsValid(ActivePlannerColorCatalog) && ActivePlannerColorCatalog->IsInViewport())
	{
		return; // already open
	}

	// The catalog collapses this widget and later forces it back to Visible; remember the designed visibility.
	VisibilityBeforePaintCatalog = GetVisibility();

	UColorCatalogWidget* Catalog = nullptr;
	if (bFinishCatalogBesidePanel)
	{
		// Beside the panel: the planner stays on screen. Created without a calling widget, so the shared catalog neither collapses
		// the planner nor forces it visible on close (its other users open it through OpenColorCatalogForWidget as before).
		if (::IsValid(ActivePlannerTileCatalog) && ActivePlannerTileCatalog->IsInViewport())
		{
			ActivePlannerTileCatalog->CloseCatalog(); // one finish catalog at a time
		}
		APlayerController* CatalogOwner = GetOwningPlayer() ? GetOwningPlayer() : UGameplayStatics::GetPlayerController(GetWorld(), 0);
		Catalog = CatalogOwner ? CreateWidget<UColorCatalogWidget>(CatalogOwner, CatalogClass) : nullptr;
		OpenFinishFlyout(Catalog);
	}
	else
	{
		Catalog = UColorCatalogWidget::OpenColorCatalogForWidget(this, CatalogClass);
	}
	if (!Catalog)
	{
		return;
	}
	ActivePlannerColorCatalog = Catalog;
	Catalog->OnColorItemSelected.AddUniqueDynamic(this, &URoomPlannerWidget::HandlePaintColorItemSelected);
	Catalog->OnCatalogClosed.AddUniqueDynamic(this, &URoomPlannerWidget::HandlePaintCatalogClosed);
}

void URoomPlannerWidget::HandlePaintColorItemSelected(const FColorCatalogItem& Item)
{
	if (ApplyFinishToSelection(ARoomPlannerManager::MakePaintFinish(Item.Code, Item.Color)) && PlannerManager)
	{
		// Colour applied: hide the blue selection highlight so the new finish is visible. The selection itself
		// stays active (further swatch clicks, finish info, delete etc. keep working); the next pick restores it.
		PlannerManager->SetSelectionHighlightSuppressed(true);
	}
}

void URoomPlannerWidget::HandlePaintCatalogClosed()
{
	if (ActivePlannerColorCatalog)
	{
		ActivePlannerColorCatalog->OnColorItemSelected.RemoveAll(this);
		ActivePlannerColorCatalog->OnCatalogClosed.RemoveAll(this);
	}
	ActivePlannerColorCatalog = nullptr;

	// CloseColorCatalog has just set this widget to Visible; restore the designed visibility instead.
	if (VisibilityBeforePaintCatalog != ESlateVisibility::Collapsed && VisibilityBeforePaintCatalog != ESlateVisibility::Hidden)
	{
		SetVisibility(VisibilityBeforePaintCatalog);
	}

	// Re-establish the planner input mode for the current view (2D keeps the cursor free during capture),
	// so cursor-driven corner / opening / object drags keep working after the overlay closes.
	if (AAwsTutorial_PlayerController* PC = GetPreviewController())
	{
		PC->ApplyRoomPlannerInputMode(CurrentViewMode == ERoomPlannerViewMode::View2D);
	}

	UpdateFinishUI();
}

bool URoomPlannerWidget::ApplyTileToSelection(FName TileID)
{
	if (!PlannerManager) return false;
	FSurfaceFinish Finish;
	if (!PlannerManager->MakeTileFinish(TileID, Finish))
	{
		HandleOperationRejected(FString::Printf(TEXT("Плитка '%s' не найдена в DT_PlannerTiles"), *TileID.ToString()));
		return false;
	}
	return ApplyFinishToSelection(Finish);
}

bool URoomPlannerWidget::ApplyFinishToSelection(const FSurfaceFinish& Finish)
{
	if (!PlannerManager) return false;
	AAwsTutorial_PlayerController* PC = GetPreviewController();
	if (!PC) return false;

	switch (PlannerManager->GetSelectionKind())
	{
	case EPlannerSelectionKind::Wall:
		// Only the selected face; the other face of the wall keeps its own finish.
		PC->Server_SetWallFaceFinish(PlannerManager->SelectedSegmentID, PlannerManager->bSelectedWallFaceLeft, Finish);
		return true;
	case EPlannerSelectionKind::Opening:
		if (Finish.Type == ESurfaceFinishType::Tile)
		{
			HandleOperationRejected(TEXT("Плитку можно назначить только стене, полу, потолку или плинтусу"));
			return false;
		}
		PC->Server_SetOpeningTrimFinish(PlannerManager->SelectedSegmentID,
			PlannerManager->GetOpeningID(PlannerManager->SelectedSegmentID, PlannerManager->SelectedOpeningIndex), Finish);
		return true;
	case EPlannerSelectionKind::Floor:
		PC->Server_SetFloorFinish(PlannerManager->SelectedRoomID, Finish);
		return true;
	case EPlannerSelectionKind::Ceiling:
		PC->Server_SetCeilingFinish(PlannerManager->SelectedRoomID, Finish);
		return true;
	case EPlannerSelectionKind::Baseboard:
		PC->Server_SetBaseboardFinish(PlannerManager->SelectedRoomID, Finish);
		return true;
	case EPlannerSelectionKind::Object:
		if (Finish.Type == ESurfaceFinishType::Tile)
		{
			HandleOperationRejected(TEXT("Плитку можно назначить только стене, полу, потолку или плинтусу"));
			return false;
		}
		PC->Server_SetPlacedObjectFinish(PlannerManager->SelectedObjectID, Finish);
		return true;
	default:
		HandleOperationRejected(TEXT("Сначала выберите поверхность: стену, пол, потолок, плинтус, дверь / окно или объект"));
		return false;
	}
}

void URoomPlannerWidget::ClearFinishOnSelection()
{
	ApplyFinishToSelection(FSurfaceFinish());
}

TArray<FPlannerCatalogEntry> URoomPlannerWidget::GetAvailableTiles() const
{
	return PlannerManager ? PlannerManager->GetAvailableTiles() : TArray<FPlannerCatalogEntry>();
}

bool URoomPlannerWidget::GetSelectedSurfaceFinish(FSurfaceFinish& OutFinish) const
{
	return PlannerManager ? PlannerManager->GetSelectedSurfaceFinish(OutFinish) : false;
}

FString URoomPlannerWidget::GetSelectedFinishText() const
{
	// Which surface the finish goes to (a wall has two faces; a door / window finish goes to its trim).
	FString Surface;
	if (PlannerManager)
	{
		switch (PlannerManager->GetSelectionKind())
		{
		case EPlannerSelectionKind::Wall:      Surface = PlannerManager->IsSelectedWallFaceInterior() ? TEXT("Сторона в комнату: ") : TEXT("Наружная сторона: "); break;
		case EPlannerSelectionKind::Opening:   Surface = TEXT("Наличник / рама: "); break;
		case EPlannerSelectionKind::Floor:     Surface = TEXT("Пол: "); break;
		case EPlannerSelectionKind::Object:    Surface = TEXT("Объект: "); break;
		case EPlannerSelectionKind::Ceiling:   Surface = TEXT("Потолок: "); break;
		case EPlannerSelectionKind::Baseboard: Surface = TEXT("Плинтус: "); break;
		default: break;
		}
	}

	FSurfaceFinish F;
	if (!GetSelectedSurfaceFinish(F) || !F.IsSet())
	{
		return Surface + TEXT("—");
	}
	if (F.Type == ESurfaceFinishType::Paint)
	{
		return Surface + FString::Printf(TEXT("Краска %s"), *F.GetKey());
	}
	return Surface + FString::Printf(TEXT("Плитка %s (%.0f см)"), *F.TileAssetID, F.TileSizeCm);
}

TArray<FFinishAreaEntry> URoomPlannerWidget::GetFinishAreas() const
{
	return PlannerManager ? PlannerManager->CalculateFinishAreas() : TArray<FFinishAreaEntry>();
}

FString URoomPlannerWidget::GetFinishAreaSummaryText() const
{
	return PlannerManager ? PlannerManager->GetFinishAreaSummaryText() : FString();
}

void URoomPlannerWidget::UpdateFinishUI()
{
	if (!PlannerManager) return;
	const bool bCanFinish = PlannerManager->CanApplyFinishToSelection();
	const ESlateVisibility Vis = bCanFinish ? ESlateVisibility::Visible : ESlateVisibility::Collapsed;
	if (BtnFinishPaint) BtnFinishPaint->SetVisibility(Vis);
	if (BtnClearFinish)
	{
		FSurfaceFinish F;
		const bool bHasFinish = GetSelectedSurfaceFinish(F) && F.IsSet();
		BtnClearFinish->SetVisibility(bHasFinish ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}
	if (TxtFinishInfo)
	{
		TxtFinishInfo->SetText(FText::FromString(bCanFinish ? GetSelectedFinishText() : FString()));
		TxtFinishInfo->SetVisibility(bCanFinish ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}
	if (FinishPrompt)
	{
		// The «Отделка» page with nothing to finish selected: what to click (surface first, then the colour or tile).
		FinishPrompt->SetText(FText::FromString(CurrentViewMode == ERoomPlannerViewMode::View2D
			? TEXT("Выберите на плане стену, пол, дверь / окно или объект, затем «Краска» или «Плитка»")
			: TEXT("Кликните по стене, полу, потолку или объекту, затем «Краска» или «Плитка»")));
		FinishPrompt->SetVisibility(bCanFinish ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
	}
	// Tiles go on surfaces (wall faces, floors, ceilings, baseboards), not on door / window trim or objects.
	if (BtnFinishTile)
	{
		BtnFinishTile->SetVisibility(CanTileSelection() ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}
	if (ActivePlannerTileCatalog)
	{
		// The catalog stays open while another surface is picked in the scene; it waits while the selection cannot take a tile.
		ActivePlannerTileCatalog->SetCardsEnabled(CanTileSelection());
		ActivePlannerTileCatalog->SetActiveTile(GetSelectedSurfaceTileID());
	}
	if (TxtFinishAreas)
	{
		const bool bHasWalls = PlannerManager && PlannerManager->GetWallCount() > 0;
		TxtFinishAreas->SetText(FText::FromString(bHasWalls ? GetFinishAreaSummaryText() : FString()));
	}
	UpdateSurfaceChooser();
}

void URoomPlannerWidget::CreateTileCatalogButton()
{
	if (BtnFinishTile || !WidgetTree || !BtnFinishPaint) return;
	UPanelWidget* Row = BtnFinishPaint->GetParent();
	if (!Row || !Row->CanHaveMultipleChildren()) return;

	// Same look as the "Отделка" button beside it.
	BtnFinishTile = MakeRuntimeButton(BtnFinishPaint, TEXT("Плитка"), TEXT("BtnFinishTile"), TEXT("Выбрать плитку из каталога для выбранной поверхности"));
	if (!BtnFinishTile) return;

	UPanelSlot* RowSlot = Row->InsertChildAt(Row->GetChildIndex(BtnFinishPaint) + 1, BtnFinishTile);
	UHorizontalBoxSlot* TileSlot = Cast<UHorizontalBoxSlot>(RowSlot);
	const UHorizontalBoxSlot* PaintSlot = Cast<UHorizontalBoxSlot>(BtnFinishPaint->Slot);
	if (TileSlot && PaintSlot)
	{
		TileSlot->SetPadding(PaintSlot->GetPadding());
		TileSlot->SetSize(PaintSlot->GetSize());
		TileSlot->SetHorizontalAlignment(PaintSlot->GetHorizontalAlignment());
		TileSlot->SetVerticalAlignment(PaintSlot->GetVerticalAlignment());
	}

	BtnFinishTile->SetVisibility(ESlateVisibility::Collapsed);
	BtnFinishTile->OnClicked.AddUniqueDynamic(this, &URoomPlannerWidget::OnFinishTileClicked);
}

void URoomPlannerWidget::CreateCatalogToggleButton()
{
	if (BtnCatalogToggle || !WidgetTree) return;
	// The category tabs copy the 2D/3D switch (their hover stays slate); the fallback toolbar copies "4×4 м" beside it.
	UButton* StyleSource = Btn_3DView ? Btn_3DView.Get() : (BtnPresetRoom ? BtnPresetRoom.Get() : BtnDrawWallTool.Get());
	if (!StyleSource) return;

	BtnCatalogToggle = MakeRuntimeButton(StyleSource, TEXT("Каталог"), TEXT("BtnCatalogToggle"),
		TEXT("Каталог: интерьер и тумбы (перетащите карточку на план). Нажмите ещё раз, чтобы свернуть"), BtnDrawWallTool);
	if (!BtnCatalogToggle) return;
	CatalogToggleIdleColor = IdleControlColor;
	BtnCatalogToggle->SetBackgroundColor(CatalogToggleIdleColor);
	BtnCatalogToggle->OnClicked.AddUniqueDynamic(this, &URoomPlannerWidget::OnCatalogToggleClicked);
}

void URoomPlannerWidget::InstallFallbackToolWrap()
{
	if (!WidgetTree || !BtnCatalogToggle || BtnCatalogToggle->GetParent()) return;
	UButton* RowMember = BtnPresetRoom ? BtnPresetRoom.Get() : BtnDrawWallTool.Get();
	UHorizontalBox* Row = RowMember ? Cast<UHorizontalBox>(RowMember->GetParent()) : nullptr;
	if (!Row) return;
	// Styled like "4×4 м" beside it; while the catalog is open it takes the active tab colour.
	BtnCatalogToggle->SetStyle(RowMember->GetStyle());
	CatalogToggleIdleColor = RowMember->GetBackgroundColor();
	BtnCatalogToggle->SetBackgroundColor(CatalogToggleIdleColor);

	// The tool row as a wrap box (with the new button last): four buttons do not fit the side panel's width on one line.
	UWrapBox* Tools = WidgetTree->ConstructWidget<UWrapBox>(UWrapBox::StaticClass(), TEXT("ToolButtons"));
	if (!Tools) return;
	Tools->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	Tools->SetInnerSlotPadding(FVector2D(4.f, 4.f));
	TArray<UWidget*> Children = Row->GetAllChildren();
	Row->ClearChildren();
	Children.Add(BtnCatalogToggle);
	for (UWidget* Child : Children)
	{
		if (!Child) continue;
		if (UWrapBoxSlot* ToolSlot = Tools->AddChildToWrapBox(Child))
		{
			ToolSlot->SetVerticalAlignment(VAlign_Center);
			// The last button takes the rest of its line, as "4×4 м" took the rest of the row before.
			ToolSlot->SetFillEmptySpace(Child == BtnCatalogToggle);
		}
	}
	if (UHorizontalBoxSlot* ToolsSlot = Row->AddChildToHorizontalBox(Tools))
	{
		ToolsSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		ToolsSlot->SetHorizontalAlignment(HAlign_Fill);
	}
}

void URoomPlannerWidget::WrapFinishControls()
{
	if (!WidgetTree || !BtnFinishPaint) return;
	UHorizontalBox* Row = Cast<UHorizontalBox>(BtnFinishPaint->GetParent());
	if (!Row) return; // already wrapped, or a layout without the row

	UVerticalBox* Column = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("FinishColumn"));
	UWrapBox* Buttons = WidgetTree->ConstructWidget<UWrapBox>(UWrapBox::StaticClass(), TEXT("FinishButtons"));
	if (!Column || !Buttons) return;
	// Like the row they replace: clicks between the buttons reach the plan logic as before.
	Column->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	Buttons->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	Buttons->SetInnerSlotPadding(FVector2D(6.f, 6.f));

	const TArray<UWidget*> Children = Row->GetAllChildren();
	Row->ClearChildren();
	bool bInfoWasInRow = false;
	for (UWidget* Child : Children)
	{
		if (!Child) continue;
		if (Child == TxtFinishInfo)
		{
			bInfoWasInRow = true;
			continue;
		}
		if (UWrapBoxSlot* ButtonSlot = Buttons->AddChildToWrapBox(Child))
		{
			ButtonSlot->SetVerticalAlignment(VAlign_Center);
		}
	}
	if (UVerticalBoxSlot* ButtonsSlot = Column->AddChildToVerticalBox(Buttons))
	{
		ButtonsSlot->SetHorizontalAlignment(HAlign_Fill);
	}
	if (bInfoWasInRow)
	{
		TxtFinishInfo->SetAutoWrapText(true);
		// Designed white, it was only readable where it ran out over the viewport; inside the white side panel it takes the
		// panel's text colour (a designer's darker colour is kept).
		if (TxtFinishInfo->GetColorAndOpacity().GetSpecifiedColor().GetLuminance() > 0.6f)
		{
			TxtFinishInfo->SetColorAndOpacity(TxtFloorArea ? TxtFloorArea->GetColorAndOpacity() : FSlateColor(FLinearColor(0.048f, 0.072f, 0.112f, 1.f)));
		}
		if (UVerticalBoxSlot* InfoSlot = Column->AddChildToVerticalBox(TxtFinishInfo))
		{
			InfoSlot->SetHorizontalAlignment(HAlign_Fill);
			InfoSlot->SetPadding(FMargin(0.f, 6.f, 0.f, 0.f));
		}
	}
	if (UHorizontalBoxSlot* ColumnSlot = Row->AddChildToHorizontalBox(Column))
	{
		// The row's full width (the side panel's width minus the row padding) is where the buttons wrap and the text breaks.
		ColumnSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		ColumnSlot->SetHorizontalAlignment(HAlign_Fill);
	}
}

void URoomPlannerWidget::CreateDimensionOverlay()
{
	if (DimensionOverlay || !WidgetTree) return;
	UCanvasPanel* Root = Cast<UCanvasPanel>(WidgetTree->RootWidget);
	if (!Root) return;

	DimensionOverlay = WidgetTree->ConstructWidget<UPlannerDimensionOverlay>(UPlannerDimensionOverlay::StaticClass(), TEXT("DimensionOverlay"));
	if (!DimensionOverlay) return;
	DimensionOverlay->LineColor = DimensionLineColor;
	DimensionOverlay->TextColor = DimensionTextColor;
	DimensionOverlay->TextBackgroundColor = DimensionTextBackgroundColor;
	if (DimensionFont.HasValidFont())
	{
		DimensionOverlay->Font = DimensionFont;
	}
	// First child of the root canvas: the side panel and every other overlay draw over the lines.
	if (UCanvasPanelSlot* OverlaySlot = Cast<UCanvasPanelSlot>(Root->InsertChildAt(0, DimensionOverlay)))
	{
		OverlaySlot->SetAnchors(FAnchors(0.f, 0.f, 1.f, 1.f));
		OverlaySlot->SetOffsets(FMargin(0.f));
	}
}

void URoomPlannerWidget::UpdateDimensionOverlay()
{
	if (!DimensionOverlay) return;

	TArray<FPlannerScreenDimension> ScreenDimensions;
	APlayerController* PC = GetOwningPlayer();
	if (PlannerManager && PC)
	{
		// Same projection as the selection labels: viewport pixels over the DPI scale = the root canvas' coordinates.
		const float DPIScale = UWidgetLayoutLibrary::GetViewportScale(this);
		auto ToScreen = [PC, DPIScale](const FVector& World, FVector2D& OutScreen)
		{
			if (!PC->ProjectWorldLocationToScreen(World, OutScreen)) return false;
			if (DPIScale > 0.001f) OutScreen /= DPIScale;
			return true;
		};
		// A vertical dimension shorter than this on screen shows only its value.
		constexpr float MinVerticalLinePx = 40.f;
		const bool bPlanView = (CurrentViewMode == ERoomPlannerViewMode::View2D);
		for (const FPlannerDimensionLine& Line : PlannerManager->GetSelectionDimensionLines())
		{
			FPlannerScreenDimension Dimension;
			if (!ToScreen(Line.Start, Dimension.Start) || !ToScreen(Line.End, Dimension.End)) continue;
			// Seen from above the heights have no length: the plan shows them in one caption outside the wall; 3D draws the lines.
			if (!PlannerDimensionLayout::ShowInView(Line.bVertical, Line.bPlanOnly, bPlanView, FVector2D::Distance(Dimension.Start, Dimension.End),
				MinVerticalLinePx, Dimension.bTextOnly)) continue;
			if (!ToScreen(Line.StartRef, Dimension.StartRef)) Dimension.StartRef = Dimension.Start;
			if (!ToScreen(Line.EndRef, Dimension.EndRef)) Dimension.EndRef = Dimension.End;
			Dimension.Text = Line.Text;
			ScreenDimensions.Add(Dimension);
		}
	}
	DimensionOverlay->SetDimensions(ScreenDimensions);
}

bool URoomPlannerWidget::CanTileSelection() const
{
	if (!PlannerManager || !PlannerManager->CanApplyFinishToSelection()) return false;
	const EPlannerSelectionKind Kind = PlannerManager->GetSelectionKind();
	return Kind != EPlannerSelectionKind::Object && Kind != EPlannerSelectionKind::Opening;
}

FName URoomPlannerWidget::GetSelectedSurfaceTileID() const
{
	FSurfaceFinish Finish;
	if (GetSelectedSurfaceFinish(Finish) && Finish.Type == ESurfaceFinishType::Tile && !Finish.TileAssetID.IsEmpty())
	{
		return FName(*Finish.TileAssetID);
	}
	return NAME_None;
}

void URoomPlannerWidget::OpenTileCatalogForSelection()
{
	if (!CanTileSelection())
	{
		HandleOperationRejected(TEXT("Сначала выберите поверхность для плитки: стену, пол, потолок или плинтус"));
		return;
	}
	if (::IsValid(ActivePlannerTileCatalog) && ActivePlannerTileCatalog->IsInViewport())
	{
		return; // already open
	}

	// The catalog collapses this widget and later forces it back to Visible; remember the designed visibility.
	VisibilityBeforeTileCatalog = GetVisibility();

	UPlannerTileCatalogWidget* Catalog = nullptr;
	if (bFinishCatalogBesidePanel)
	{
		// Beside the panel, the planner stays on screen (no calling widget: nothing is collapsed or forced visible on close).
		if (::IsValid(ActivePlannerColorCatalog) && ActivePlannerColorCatalog->IsInViewport())
		{
			ActivePlannerColorCatalog->CloseColorCatalog(); // one finish catalog at a time
		}
		APlayerController* CatalogOwner = GetOwningPlayer() ? GetOwningPlayer() : UGameplayStatics::GetPlayerController(GetWorld(), 0);
		TSubclassOf<UPlannerTileCatalogWidget> TileClass = PlannerTileCatalogWidgetClass ? PlannerTileCatalogWidgetClass : TSubclassOf<UPlannerTileCatalogWidget>(UPlannerTileCatalogWidget::StaticClass());
		Catalog = CatalogOwner ? CreateWidget<UPlannerTileCatalogWidget>(CatalogOwner, TileClass) : nullptr;
		OpenFinishFlyout(Catalog);
	}
	else
	{
		Catalog = UPlannerTileCatalogWidget::OpenForWidget(this, PlannerTileCatalogWidgetClass);
	}
	if (!Catalog)
	{
		return;
	}
	ActivePlannerTileCatalog = Catalog;
	Catalog->SetTiles(PlannerManager->GetAvailableTiles());
	Catalog->SetActiveTile(GetSelectedSurfaceTileID());
	Catalog->OnTileChosen.AddUniqueDynamic(this, &URoomPlannerWidget::HandleTileChosen);
	Catalog->OnCatalogClosed.AddUniqueDynamic(this, &URoomPlannerWidget::HandleTileCatalogClosed);
}

void URoomPlannerWidget::HandleTileChosen(FName TileID)
{
	const bool bApplied = ApplyTileToSelection(TileID);
	if (bApplied && PlannerManager)
	{
		// As with paint: hide the selection highlight so the new finish is visible; the selection itself stays active.
		PlannerManager->SetSelectionHighlightSuppressed(true);
	}
	if (ActivePlannerTileCatalog)
	{
		ActivePlannerTileCatalog->SetActiveTile(bApplied ? TileID : GetSelectedSurfaceTileID());
	}
}

void URoomPlannerWidget::HandleTileCatalogClosed()
{
	if (ActivePlannerTileCatalog)
	{
		ActivePlannerTileCatalog->OnTileChosen.RemoveAll(this);
		ActivePlannerTileCatalog->OnCatalogClosed.RemoveAll(this);
	}
	ActivePlannerTileCatalog = nullptr;

	// CloseCatalog has just set this widget to Visible; restore the designed visibility instead.
	if (VisibilityBeforeTileCatalog != ESlateVisibility::Collapsed && VisibilityBeforeTileCatalog != ESlateVisibility::Hidden)
	{
		SetVisibility(VisibilityBeforeTileCatalog);
	}

	// Re-establish the planner input mode for the current view (as after the colour catalog).
	if (AAwsTutorial_PlayerController* PC = GetPreviewController())
	{
		PC->ApplyRoomPlannerInputMode(CurrentViewMode == ERoomPlannerViewMode::View2D);
	}

	UpdateFinishUI();
}

void URoomPlannerWidget::OnFinishTileClicked() { OpenTileCatalogForSelection(); }

void URoomPlannerWidget::OnFinishPaintClicked() { OpenPaintCatalogForSelection(); }
void URoomPlannerWidget::OnClearFinishClicked() { ClearFinishOnSelection(); }

// ═══════════════════════════════════════════════════════════════════════════════
// REQ-17 / REQ-18: objects & cabinet sets
// ═══════════════════════════════════════════════════════════════════════════════

TArray<FPlannerCatalogEntry> URoomPlannerWidget::GetAvailableObjects() const
{
	return PlannerManager ? PlannerManager->GetAvailableObjects() : TArray<FPlannerCatalogEntry>();
}

TArray<FPlannerCatalogEntry> URoomPlannerWidget::GetAvailableCabinetSets() const
{
	return PlannerManager ? PlannerManager->GetAvailableCabinetSets() : TArray<FPlannerCatalogEntry>();
}

void URoomPlannerWidget::BeginPlaceObject(const FString& AssetID)
{
	if (!PlannerManager) return;
	if (CurrentViewMode != ERoomPlannerViewMode::View2D)
	{
		HandleOperationRejected(TEXT("Размещение объектов доступно только в 2D режиме"));
		return;
	}
	if (bCategoryLayoutBuilt) SetActiveCategory(EPlannerPanelCategory::Catalog); // «Отменить размещение» is on that page (first: it may switch the tool)
	PlannerManager->BeginPlaceObject(AssetID);
	if (AAwsTutorial_PlayerController* PC = GetPreviewController())
	{
		PC->UpdateRoomPlannerCameraToolMode(EPlannerToolMode::PlaceFurniture);
	}
	UpdateToolModeButtonStyles();
	UpdateDynamicPropertiesPanel();
}

void URoomPlannerWidget::BeginPlaceCabinetSet(FName ProductID)
{
	if (!PlannerManager) return;
	if (CurrentViewMode != ERoomPlannerViewMode::View2D)
	{
		HandleOperationRejected(TEXT("Размещение гарнитуров доступно только в 2D режиме"));
		return;
	}
	if (bCategoryLayoutBuilt) SetActiveCategory(EPlannerPanelCategory::Catalog); // «Отменить размещение» is on that page (first: it may switch the tool)
	PlannerManager->BeginPlaceCabinetSet(ProductID);
	if (AAwsTutorial_PlayerController* PC = GetPreviewController())
	{
		PC->UpdateRoomPlannerCameraToolMode(EPlannerToolMode::PlaceFurniture);
	}
	UpdateToolModeButtonStyles();
	UpdateDynamicPropertiesPanel();
}

void URoomPlannerWidget::CancelPlacement()
{
	if (!PlannerManager) return;
	PlannerManager->CancelPendingPlacement();
	SetToolMode(EPlannerToolMode::Select);
	UpdateDynamicPropertiesPanel();
}

void URoomPlannerWidget::RefreshCatalogPanels()
{
	if (!PlannerManager)
	{
		BindManagerDelegates();
	}
	if (!Catalog_Container || !WidgetTree)
	{
		return;
	}

	// Same construction as WBP_PreviewWindow's Size_Container: ScrollBox (height-limited by a SizeBox)
	// → UniformGridPanel → one SizeBox-wrapped item per cell, all driven by the root "UI Sizing - Catalog" values.
	float SavedScrollOffset = 0.f;
	bool bHasSavedOffset = false;
	for (int32 ChildIdx = 0; ChildIdx < Catalog_Container->GetChildrenCount(); ++ChildIdx)
	{
		if (USizeBox* Wrapper = Cast<USizeBox>(Catalog_Container->GetChildAt(ChildIdx)))
		{
			if (UScrollBox* Inner = Cast<UScrollBox>(Wrapper->GetContent()))
			{
				SavedScrollOffset = Inner->GetScrollOffset();
				bHasSavedOffset = true;
			}
		}
	}
	Catalog_Container->ClearChildren();
	if (!bCatalogTabChosen)
	{
		return; // no tab picked yet: no cards
	}

	const EPlannerPlacementKind Kind = ActiveCatalogTab;
	const TArray<FPlannerCatalogEntry> Entries = (Kind == EPlannerPlacementKind::CabinetSet) ? GetAvailableCabinetSets() : GetAvailableObjects();
	TSubclassOf<UPlannerCatalogItemWidget> CardClass = CatalogItemWidgetClass ? CatalogItemWidgetClass : TSubclassOf<UPlannerCatalogItemWidget>(UPlannerCatalogItemWidget::StaticClass());

	UScrollBox* ScrollBox = WidgetTree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass());
	UUniformGridPanel* GridPanel = WidgetTree->ConstructWidget<UUniformGridPanel>(UUniformGridPanel::StaticClass());
	if (!ScrollBox || !GridPanel)
	{
		return;
	}
	ScrollBox->SetVisibility(ESlateVisibility::Visible);
	ScrollBox->SetScrollBarVisibility(ESlateVisibility::Visible);
	ScrollBox->SetAnimateWheelScrolling(true);
	GridPanel->SetVisibility(ESlateVisibility::Visible);
	GridPanel->SetMinDesiredSlotWidth(0.f);
	GridPanel->SetMinDesiredSlotHeight(0.f);
	GridPanel->SetSlotPadding(FMargin(CatalogGridSlotPadding));

	const int32 Columns = CatalogColumns > 0 ? CatalogColumns : 2;
	for (int32 i = 0; i < Entries.Num(); ++i)
	{
		UPlannerCatalogItemWidget* Card = CreateWidget<UPlannerCatalogItemWidget>(this, CardClass);
		if (!Card) continue;

		// Root-level "UI Sizing - Catalog" values forwarded to the card (self-drawing when no Blueprint design exists).
		Card->CardWidth = CatalogButtonWidth;
		Card->CardHeight = CatalogButtonHeight;
		Card->CardPadding = CatalogButtonPadding;
		Card->ImageStretch = CatalogImageStretch;
		Card->NormalColor = CatalogButtonNormalColor.GetSpecifiedColor();
		Card->HoveredColor = CatalogButtonHoveredColor.GetSpecifiedColor();
		Card->PressedColor = CatalogButtonPressedColor.GetSpecifiedColor();
		Card->TextColor = CatalogTextColor.GetSpecifiedColor();
		Card->TextFont = CatalogTextFont;
		Card->SetupCatalogItem(this, Kind, Entries[i]);

		USizeBox* SizeBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
		if (!SizeBox) continue;
		SizeBox->SetWidthOverride(CatalogButtonWidth);
		SizeBox->SetHeightOverride(CatalogButtonHeight);
		SizeBox->AddChild(Card);

		if (UUniformGridSlot* GridSlot = GridPanel->AddChildToUniformGrid(SizeBox, i / Columns, i % Columns))
		{
			GridSlot->SetHorizontalAlignment(HAlign_Fill);
			GridSlot->SetVerticalAlignment(VAlign_Fill);
		}
	}

	ScrollBox->AddChild(GridPanel);
	if (bHasSavedOffset)
	{
		ScrollBox->SetScrollOffset(SavedScrollOffset);
	}

	USizeBox* ScrollLimitBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
	if (ScrollLimitBox)
	{
		if (CatalogContainerHeight > 0.f)
		{
			ScrollLimitBox->SetMaxDesiredHeight(CatalogContainerHeight);
		}
		ScrollLimitBox->AddChild(ScrollBox);
		Catalog_Container->AddChild(ScrollLimitBox);
	}
	else
	{
		Catalog_Container->AddChild(ScrollBox);
	}
}

bool URoomPlannerWidget::IsScreenPositionOverCatalogPanels(const FVector2D& ScreenSpacePosition) const
{
	// A folded catalog keeps its last geometry: it is not under anything.
	return Catalog_Container && Catalog_Container->IsVisible() && Catalog_Container->GetCachedGeometry().IsUnderLocation(ScreenSpacePosition);
}

void URoomPlannerWidget::HandleCatalogDragReleased(EPlannerPlacementKind Kind, const FString& ItemID, const FVector2D& ScreenSpacePosition, float YawDeg)
{
	if (IsScreenPositionOverPlannerUI(ScreenSpacePosition))
	{
		UE_LOG(LogTemp, Warning, TEXT("[PlannerDrop] Drag of '%s' released over the planner UI — ignored."), *ItemID);
		return; // dropped back onto the panel (the cards, the tabs, the toolbar ...): not a placement on the plan hidden behind it
	}
	UE_LOG(LogTemp, Warning, TEXT("[PlannerDrop] Drag-cancel path: '%s' (kind %d) released at screen (%.0f, %.0f), yaw %.1f"), *ItemID, (int32)Kind, ScreenSpacePosition.X, ScreenSpacePosition.Y, YawDeg);
	DropCatalogItemAtScreenPosition(Kind, ItemID, ScreenSpacePosition, YawDeg);
}

bool URoomPlannerWidget::NativeOnDragOver(const FGeometry& InGeometry, const FDragDropEvent& InDragDropEvent, UDragDropOperation* InOperation)
{
	if (InOperation && Cast<UPlannerCatalogItemWidget>(InOperation->Payload))
	{
		return true;
	}
	return Super::NativeOnDragOver(InGeometry, InDragDropEvent, InOperation);
}

bool URoomPlannerWidget::NativeOnDrop(const FGeometry& InGeometry, const FDragDropEvent& InDragDropEvent, UDragDropOperation* InOperation)
{
	if (InOperation)
	{
		if (const UPlannerCatalogItemWidget* Card = Cast<UPlannerCatalogItemWidget>(InOperation->Payload))
		{
			UE_LOG(LogTemp, Warning, TEXT("[PlannerDrop] NativeOnDrop path: '%s' (kind %d) at screen (%.0f, %.0f)"), *Card->ItemID, (int32)Card->Kind, InDragDropEvent.GetScreenSpacePosition().X, InDragDropEvent.GetScreenSpacePosition().Y);
			if (!IsScreenPositionOverPlannerUI(InDragDropEvent.GetScreenSpacePosition()))
			{
				const UPlannerCatalogDragOperation* CatalogDrag = Cast<UPlannerCatalogDragOperation>(InOperation);
				DropCatalogItemAtScreenPosition(Card->Kind, Card->ItemID, InDragDropEvent.GetScreenSpacePosition(), CatalogDrag ? CatalogDrag->YawDeg : 0.f);
			}
			return true; // consumed either way: no cancel-path placement for this drop
		}
	}
	return Super::NativeOnDrop(InGeometry, InDragDropEvent, InOperation);
}

bool URoomPlannerWidget::DropCatalogItemUnderCursor(EPlannerPlacementKind Kind, const FString& ItemID)
{
	if (!PlannerManager)
	{
		UE_LOG(LogTemp, Warning, TEXT("[PlannerDrop] DropCatalogItemUnderCursor: no PlannerManager"));
		return false;
	}
	if (CurrentViewMode != ERoomPlannerViewMode::View2D)
	{
		HandleOperationRejected(TEXT("Размещение доступно только в 2D режиме"));
		return false;
	}
	AAwsTutorial_PlayerController* PC = GetPreviewController();
	if (!PC)
	{
		UE_LOG(LogTemp, Warning, TEXT("[PlannerDrop] DropCatalogItemUnderCursor: owning player is not AAwsTutorial_PlayerController"));
		return false;
	}
	BeginSelectDroppedItem(Kind, ItemID); // before the call: on a listen server the new item arrives inside it
	const bool bSent = PC->PlannerDropCatalogItemUnderCursor(Kind, ItemID);
	if (!bSent) PendingDrop = FPendingDropSelection();
	UpdateDynamicPropertiesPanel();
	return bSent;
}

bool URoomPlannerWidget::DropCatalogItemAtScreenPosition(EPlannerPlacementKind Kind, const FString& ItemID, FVector2D ScreenSpacePosition, float YawDeg)
{
	if (!PlannerManager)
	{
		UE_LOG(LogTemp, Warning, TEXT("[PlannerDrop] DropCatalogItemAtScreenPosition: no PlannerManager"));
		return false;
	}
	if (CurrentViewMode != ERoomPlannerViewMode::View2D)
	{
		HandleOperationRejected(TEXT("Размещение доступно только в 2D режиме"));
		return false;
	}
	AAwsTutorial_PlayerController* PC = GetPreviewController();
	if (!PC)
	{
		UE_LOG(LogTemp, Warning, TEXT("[PlannerDrop] DropCatalogItemAtScreenPosition: owning player is not AAwsTutorial_PlayerController"));
		return false;
	}
	BeginSelectDroppedItem(Kind, ItemID); // before the call: on a listen server the new item arrives inside it
	const bool bSent = PC->PlannerDropCatalogItemAtScreenPosition(Kind, ItemID, ScreenSpacePosition, YawDeg);
	if (!bSent) PendingDrop = FPendingDropSelection();
	UpdateDynamicPropertiesPanel();
	return bSent;
}

void URoomPlannerWidget::RotateSelected(float DeltaYawDeg)
{
	if (!PlannerManager) return;
	if (CurrentViewMode != ERoomPlannerViewMode::View2D) return; // moving / rotating is a 2D workflow
	AAwsTutorial_PlayerController* PC = GetPreviewController();
	if (PC)
	{
		PC->FlushPlannerWheelCommit(); // a wheel turn not yet sent goes first, so the two commits arrive in order
	}
	else if (!PlannerManager->IsSelectionWallAttached())
	{
		return; // no player: nothing could be committed (a refusal is still shown, below)
	}

	// The mouse wheel's rules: a wall-attached selection is refused with the message, anything else turns locally.
	FString InstanceID;
	bool bCabinetSet = false;
	float NewYawDeg = 0.f;
	if (!PlannerManager->RotateSelectionLocal(DeltaYawDeg, InstanceID, bCabinetSet, NewYawDeg) || !PC) return;

	if (bCabinetSet)
	{
		FPlacedCabinetSetData D;
		if (PlannerManager->GetCabinetSet(InstanceID, D))
		{
			PC->Server_MoveCabinetSet(D.InstanceID, D.Location, D.Rotation);
		}
	}
	else
	{
		FPlacedFurnitureData D;
		if (PlannerManager->GetPlacedObject(InstanceID, D))
		{
			PC->Server_MovePlacedObject(D.InstanceID, D.Location, D.Rotation, D.Scale);
		}
	}
}

void URoomPlannerWidget::DeleteSelected()
{
	if (!PlannerManager) return;
	if (CurrentViewMode != ERoomPlannerViewMode::View2D) return; // deleting is a 2D workflow (matches the Delete key handling)
	AAwsTutorial_PlayerController* PC = GetPreviewController();
	if (!PC) return;
	PC->FlushPlannerWheelCommit(); // as the Delete key does

	switch (PlannerManager->GetSelectionKind())
	{
	case EPlannerSelectionKind::Opening:
		PC->Server_DeleteOpening(PlannerManager->SelectedSegmentID,
			PlannerManager->GetOpeningID(PlannerManager->SelectedSegmentID, PlannerManager->SelectedOpeningIndex));
		break;
	case EPlannerSelectionKind::Wall:
		PC->Server_DeleteWall(PlannerManager->SelectedSegmentID);
		break;
	case EPlannerSelectionKind::Object:
		PC->Server_RemovePlacedObject(PlannerManager->SelectedObjectID);
		break;
	case EPlannerSelectionKind::CabinetSet:
		PC->Server_RemoveCabinetSet(PlannerManager->SelectedCabinetSetID);
		break;
	default:
		return;
	}
	PlannerManager->ClearAllSelection();
	UpdateDynamicPropertiesPanel();
}

EPlannerSelectionKind URoomPlannerWidget::PickSurfaceUnderCursor()
{
	if (AAwsTutorial_PlayerController* PC = GetPreviewController())
	{
		const EPlannerSelectionKind Kind = PC->PlannerPickUnderCursor();
		UpdateDynamicPropertiesPanel();
		return Kind;
	}
	return EPlannerSelectionKind::None;
}

void URoomPlannerWidget::OnRotateLeftClicked() { RotateSelected(-15.f); }
void URoomPlannerWidget::OnRotateRightClicked() { RotateSelected(15.f); }
void URoomPlannerWidget::OnCancelPlacementClicked() { CancelPlacement(); }

