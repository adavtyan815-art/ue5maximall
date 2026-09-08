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
#include "Engine/HitResult.h"
#include "ColorCatalog/ColorCatalogWidget.h"

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
	if (BtnPresetRoom) { BtnPresetRoom->OnClicked.AddUniqueDynamic(this, &URoomPlannerWidget::OnPresetRoomClicked); }
	if (BtnClearLayout) { BtnClearLayout->OnClicked.AddUniqueDynamic(this, &URoomPlannerWidget::OnClearLayoutClicked); }
	if (BtnToggleCeiling) { BtnToggleCeiling->OnClicked.AddUniqueDynamic(this, &URoomPlannerWidget::OnToggleCeilingClicked); }
	if (Btn_ToggleCeiling) { Btn_ToggleCeiling->OnClicked.AddUniqueDynamic(this, &URoomPlannerWidget::OnToggleCeilingClicked); }
	if (BtnCeiling) { BtnCeiling->OnClicked.AddUniqueDynamic(this, &URoomPlannerWidget::OnToggleCeilingClicked); }
	if (BtnClose) { BtnClose->OnClicked.AddUniqueDynamic(this, &URoomPlannerWidget::OnCloseClicked); }
	if (BtnBack) { BtnBack->OnClicked.AddUniqueDynamic(this, &URoomPlannerWidget::OnCloseClicked); }

	// Set descriptive tooltips on tools and actions
	if (Btn2DView) Btn2DView->SetToolTipText(FText::FromString(TEXT("2D Вид: Режим черчения стен")));
	if (Btn_2DView) Btn_2DView->SetToolTipText(FText::FromString(TEXT("2D Вид: Режим черчения стен")));
	if (Btn3DView) Btn3DView->SetToolTipText(FText::FromString(TEXT("3D Вид: Осмотр созданной комнаты")));
	if (Btn_3DView) Btn_3DView->SetToolTipText(FText::FromString(TEXT("3D Вид: Осмотр созданной комнаты")));
	if (BtnPresetRoom) BtnPresetRoom->SetToolTipText(FText::FromString(TEXT("Построить готовую квадратную комнату 4х4 метра")));
	if (BtnClearLayout) BtnClearLayout->SetToolTipText(FText::FromString(TEXT("Очистить все стены и проёмы")));
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

	// UI Sanitization: Immediately collapse contextual/unselected buttons
	if (BtnSelectTool) BtnSelectTool->SetVisibility(ESlateVisibility::Collapsed);
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

	if (TxtGuidanceHint)
	{
		TxtGuidanceHint->SetText(FText::FromString(TEXT("Зажмите ЛКМ и потяните мышь, чтобы нарисовать первую стену, или выберите пресет 4х4 м")));
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

	if (BtnSwingLeft) BtnSwingLeft->SetToolTipText(FText::FromString(TEXT("Петли слева (вид изнутри комнаты)")));
	if (BtnSwingRight) BtnSwingRight->SetToolTipText(FText::FromString(TEXT("Петли справа (вид изнутри комнаты)")));
	if (BtnSwingInward) BtnSwingInward->SetToolTipText(FText::FromString(TEXT("Открывается внутрь комнаты")));
	if (BtnSwingOutward) BtnSwingOutward->SetToolTipText(FText::FromString(TEXT("Открывается наружу")));
	if (BtnFinishPaint) BtnFinishPaint->SetToolTipText(FText::FromString(TEXT("Покрасить выбранную стену / пол цветом RAL / NCS")));
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

	// Automatically enter 2D Top-Down Drawing Mode on open
	CurrentViewMode = ERoomPlannerViewMode::View3D;
	SetViewMode(ERoomPlannerViewMode::View2D);
	SetToolMode(EPlannerToolMode::DrawWall);

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

	if (PlannerManager)
	{
		UnbindManagerDelegates();
		PlannerManager->bPlannerUIOpen = false;
		PlannerManager->CancelPendingPlacement();
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
						// 1. Wall control point → corner drag (REQ-02)
						const int32 NodeID = PlannerManager->FindNodeAtWorldPos(GroundPos, 25.f);
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
						PC->PlannerPlacePendingAt(GroundPos);
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
						PlannerManager->UpdateNodeDrag(GroundPos);
						return FReply::Handled();
					}
					else if (!WidgetDraggedObjectID.IsEmpty() && InMouseEvent.IsMouseButtonDown(EKeys::LeftMouseButton))
					{
						const FVector NewLoc = FVector(GroundPos.X, GroundPos.Y, 0.f) + WidgetDragOffset;
						if (bWidgetDraggedIsCabinetSet)
						{
							FPlacedCabinetSetData D;
							if (PlannerManager->GetCabinetSet(WidgetDraggedObjectID, D)) PlannerManager->MoveCabinetSetLocal(WidgetDraggedObjectID, NewLoc, D.Rotation);
						}
						else
						{
							FPlacedFurnitureData D;
							if (PlannerManager->GetPlacedObject(WidgetDraggedObjectID, D)) PlannerManager->MovePlacedObjectLocal(WidgetDraggedObjectID, NewLoc, D.Rotation);
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

	if (bIsWidgetDrawingWall)
	{
		UpdateMouseCursorPosition();
	}

	if (!PlannerManager && GetWorld())
	{
		BindManagerDelegates();
		if (PlannerManager)
		{
			PlannerManager->SetViewMode(CurrentViewMode == ERoomPlannerViewMode::View2D);
			UpdateSummaryStatsUI();
			UpdateViewModeButtonStyles();
			UpdateDynamicPropertiesPanel();
			UpdateToolModeButtonStyles();
		}
	}

	// REQ-02 / REQ-04 / REQ-06: keep the dimension labels next to the selected object every frame.
	UpdateSelectionLabelsUI();

	if (TxtOperationMessage && OperationMessageClearTime > 0.f && GetWorld() && GetWorld()->GetTimeSeconds() > OperationMessageClearTime)
	{
		OperationMessageClearTime = 0.f;
		TxtOperationMessage->SetVisibility(ESlateVisibility::Collapsed);
	}

	if (PlannerManager)
	{
		// Logic for Select button
		bool bIs2D = (CurrentViewMode == ERoomPlannerViewMode::View2D);
		bool bHasWalls = PlannerManager->GetWallCount() > 0;
		if (BtnSelectTool)
		{
			BtnSelectTool->SetVisibility((bIs2D && bHasWalls) ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);

			if (!bHasWalls)
			{
				BtnSelectTool->SetToolTipText(FText::FromString(TEXT("Пока нет стен для работы")));
			}
			else
			{
				BtnSelectTool->SetToolTipText(FText::GetEmpty());
			}
		}
		
		// If Select tool is active but no walls exist, force revert to Draw Wall
		if (!bHasWalls && PlannerManager->ActiveToolMode == EPlannerToolMode::Select)
		{
			SetToolMode(EPlannerToolMode::DrawWall);
		}

		// Logic for Door/Window tools (visible only if a wall is selected)
		bool bHasSelection = PlannerManager->SelectedSegmentID != -1;
		ESlateVisibility DoorWinVis = bHasSelection ? ESlateVisibility::Visible : ESlateVisibility::Collapsed;

		if (BtnAddDoor) BtnAddDoor->SetVisibility(DoorWinVis);
		if (BtnAddWindow) BtnAddWindow->SetVisibility(DoorWinVis);
		if (EditableTxtOpeningWidth) EditableTxtOpeningWidth->SetVisibility(DoorWinVis);
		if (EditableTxtOpeningHeight) EditableTxtOpeningHeight->SetVisibility(DoorWinVis);
		
		if (EditableTxtOpeningWidth_1) EditableTxtOpeningWidth_1->SetVisibility(DoorWinVis);
		if (EditableTxtOpeningHeight_1) EditableTxtOpeningHeight_1->SetVisibility(DoorWinVis);
		if (EditableTxtOpeningSillHeight) EditableTxtOpeningSillHeight->SetVisibility(DoorWinVis);

		UpdateGuidanceHintText();
	}
}

void URoomPlannerWidget::SetViewMode(ERoomPlannerViewMode NewMode)
{
	if (CurrentViewMode == NewMode) return;
	CurrentViewMode = NewMode;
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
	FLinearColor ActiveColor(0.18f, 0.8f, 0.44f, 1.0f);
	FLinearColor InactiveColor(0.17f, 0.17f, 0.18f, 1.0f);

	EPlannerToolMode CurrentToolMode = EPlannerToolMode::DrawWall;
	if (PlannerManager)
	{
		CurrentToolMode = PlannerManager->ActiveToolMode;
	}

	if (BtnSelectTool) 
	{
		if (PlannerManager && PlannerManager->GetWallCount() == 0)
		{
			BtnSelectTool->SetBackgroundColor(FLinearColor(0.05f, 0.05f, 0.05f, 1.0f)); // Visually disabled
			BtnSelectTool->SetToolTipText(FText::FromString(TEXT("Инструмент недоступен: пока нет построенных стен")));
		}
		else
		{
			BtnSelectTool->SetBackgroundColor(CurrentToolMode == EPlannerToolMode::Select ? ActiveColor : InactiveColor);
			BtnSelectTool->SetToolTipText(FText::FromString(TEXT("Выделение: кликните стену или проём для редактирования")));
		}
	}
	if (BtnDrawWallTool)
	{
		BtnDrawWallTool->SetBackgroundColor(CurrentToolMode == EPlannerToolMode::DrawWall ? ActiveColor : InactiveColor);
		BtnDrawWallTool->SetToolTipText(FText::FromString(TEXT("Инструмент 'Стена': зажмите и тяните ЛКМ на плане")));
	}
	if (BtnDeleteTool)
	{
		BtnDeleteTool->SetBackgroundColor(CurrentToolMode == EPlannerToolMode::Erase ? ActiveColor : InactiveColor);
		BtnDeleteTool->SetToolTipText(FText::FromString(TEXT("Удалить выбранный элемент или войти в режим удаления")));
	}

	UpdateGuidanceHintText();
}

void URoomPlannerWidget::UpdateViewModeButtonStyles()
{
	FLinearColor ActiveColor(0.2f, 0.6f, 1.0f, 1.0f);
	FLinearColor InactiveColor(0.17f, 0.17f, 0.18f, 1.0f);

	bool bIs2D = (CurrentViewMode == ERoomPlannerViewMode::View2D);

	if (Btn2DView) { Btn2DView->SetBackgroundColor(bIs2D ? ActiveColor : InactiveColor); Btn2DView->SetIsEnabled(!bIs2D); }
	if (Btn_2DView) { Btn_2DView->SetBackgroundColor(bIs2D ? ActiveColor : InactiveColor); Btn_2DView->SetIsEnabled(!bIs2D); }
	if (Btn3DView) { Btn3DView->SetBackgroundColor(!bIs2D ? ActiveColor : InactiveColor); Btn3DView->SetIsEnabled(bIs2D); }
	if (Btn_3DView) { Btn_3DView->SetBackgroundColor(!bIs2D ? ActiveColor : InactiveColor); Btn_3DView->SetIsEnabled(bIs2D); }

	bool bCeilingOn = PlannerManager && PlannerManager->bCeilingVisible;
	if (BtnToggleCeiling) BtnToggleCeiling->SetBackgroundColor(bCeilingOn ? ActiveColor : InactiveColor);
	if (Btn_ToggleCeiling) Btn_ToggleCeiling->SetBackgroundColor(bCeilingOn ? ActiveColor : InactiveColor);
	if (BtnCeiling) BtnCeiling->SetBackgroundColor(bCeilingOn ? ActiveColor : InactiveColor);

	ESlateVisibility ToolsVis = bIs2D ? ESlateVisibility::Visible : ESlateVisibility::Collapsed;
	if (BtnDrawWallTool) BtnDrawWallTool->SetVisibility(ToolsVis);
	if (BtnClearLayout) BtnClearLayout->SetVisibility(ToolsVis);
	if (BtnPresetRoom) BtnPresetRoom->SetVisibility(ToolsVis);
	if (Image_1) Image_1->SetVisibility(ToolsVis);

	UpdateGuidanceHintText();
}

void URoomPlannerWidget::UpdateGuidanceHintText()
{
	if (!TxtGuidanceHint)
	{
		return;
	}

	if (CurrentViewMode == ERoomPlannerViewMode::View3D)
	{
		TxtGuidanceHint->SetText(FText::FromString(TEXT("3D Просмотр: Удерживайте ПКМ для вращения камеры • Колесо мыши для зума • Кликните '2D Вид' для редактирования")));
		return;
	}

	if (!PlannerManager)
	{
		TxtGuidanceHint->SetText(FText::FromString(TEXT("Зажмите ЛКМ и потяните мышь, чтобы нарисовать первую стену, или выберите пресет 4х4 м")));
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

	switch (PlannerManager->ActiveToolMode)
	{
	case EPlannerToolMode::DrawWall:
		if (PlannerManager->GetWallCount() == 0)
		{
			TxtGuidanceHint->SetText(FText::FromString(TEXT("Зажмите ЛКМ и потяните мышь, чтобы нарисовать первую стену, или выберите пресет 4х4 м")));
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
		else if (PlannerManager->SelectedSegmentID != -1 && PlannerManager->SelectedOpeningIndex != -1)
		{
			TxtGuidanceHint->SetText(FText::FromString(TEXT("Проём выбран: тяните вдоль стены, задайте размеры и сторону открывания справа")));
		}
		else if (PlannerManager->SelectedSegmentID != -1)
		{
			TxtGuidanceHint->SetText(FText::FromString(TEXT("Стена выбрана: тяните её углы, измените длину, добавьте дверь/окно или назначьте отделку")));
		}
		else if (PlannerManager->SelectedRoomID != -1)
		{
			TxtGuidanceHint->SetText(FText::FromString(TEXT("Пол выбран: назначьте краску или плитку")));
		}
		else if (!PlannerManager->SelectedObjectID.IsEmpty() || !PlannerManager->SelectedCabinetSetID.IsEmpty())
		{
			TxtGuidanceHint->SetText(FText::FromString(TEXT("Объект выбран: тяните для перемещения, поверните кнопками или удалите клавишей Delete")));
		}
		else
		{
			TxtGuidanceHint->SetText(FText::FromString(TEXT("Кликните по стене, двери/окну, полу или объекту, чтобы настроить их параметры. Углы стен можно тянуть")));
		}
		break;

	case EPlannerToolMode::PlaceFurniture:
		TxtGuidanceHint->SetText(FText::FromString(TEXT("Кликните на плане, чтобы разместить выбранный объект")));
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
	UpdateSummaryStatsUI();
	UpdateDynamicPropertiesPanel();
	UpdateToolModeButtonStyles();
	UpdateFinishUI();
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
		if (SegmentID != -1)
		{
			PC->UpdateRoomPlannerCameraToolMode(EPlannerToolMode::Select);
		}
	}

	UpdateDynamicPropertiesPanel();
	UpdateGuidanceHintText();

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

	if (PlannerManager->SelectedSegmentID != -1 && CurrentViewMode == ERoomPlannerViewMode::View2D)
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

				if (S_cm > 1.0f) // It's a window
				{
					if (EditableTxtProp3)
					{
						EditableTxtProp3->SetVisibility(ESlateVisibility::Visible);
						EditableTxtProp3->SetText(FText::FromString(FString::Printf(TEXT("%.0f"), S_cm)));
					}
					if (TxtApplyProperties) TxtApplyProperties->SetText(FText::FromString(TEXT("Изменить размер окна")));
				}
				else // It's a door
				{
					if (EditableTxtProp3) EditableTxtProp3->SetVisibility(ESlateVisibility::Hidden);
					if (TxtApplyProperties) TxtApplyProperties->SetText(FText::FromString(TEXT("Изменить размер двери")));
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
			if (TxtApplyProperties) TxtApplyProperties->SetText(FText::FromString(TEXT("Изменить размер стены")));
		}

		if (BtnApplyProperties) BtnApplyProperties->SetVisibility(ESlateVisibility::Visible);
		if (BtnDeleteTool) BtnDeleteTool->SetVisibility(ESlateVisibility::Visible);
		if (Image_2) Image_2->SetVisibility(ESlateVisibility::Visible);
	}
	else
	{
		// No wall / opening selected (nothing, or a floor / object / cabinet set)
		if (EditableTxtProp1) EditableTxtProp1->SetVisibility(ESlateVisibility::Hidden);
		if (EditableTxtProp2) EditableTxtProp2->SetVisibility(ESlateVisibility::Hidden);
		if (EditableTxtProp3) EditableTxtProp3->SetVisibility(ESlateVisibility::Hidden);
		if (TxtApplyProperties) TxtApplyProperties->SetText(FText::FromString(TEXT("Размер стены")));

		// Editing (delete / rotate / swing / placement) is a 2D-only workflow, matching the existing panel logic.
		const bool bOtherSelection = (CurrentViewMode == ERoomPlannerViewMode::View2D) && (PlannerManager->GetSelectionKind() != EPlannerSelectionKind::None);
		if (BtnApplyProperties) BtnApplyProperties->SetVisibility(ESlateVisibility::Collapsed);
		if (BtnDeleteTool) BtnDeleteTool->SetVisibility(bOtherSelection ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
		if (Image_2) Image_2->SetVisibility(bOtherSelection ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}

	const bool bIs2DPanel = (CurrentViewMode == ERoomPlannerViewMode::View2D);

	// REQ-07: swing controls only for a selected door / window, 2D only
	const EPlannerSelectionKind Kind = PlannerManager->GetSelectionKind();
	const ESlateVisibility SwingVis = (bIs2DPanel && Kind == EPlannerSelectionKind::Opening) ? ESlateVisibility::Visible : ESlateVisibility::Collapsed;
	if (BtnSwingLeft) BtnSwingLeft->SetVisibility(SwingVis);
	if (BtnSwingRight) BtnSwingRight->SetVisibility(SwingVis);
	if (BtnSwingInward) BtnSwingInward->SetVisibility(SwingVis);
	if (BtnSwingOutward) BtnSwingOutward->SetVisibility(SwingVis);
	if (Kind == EPlannerSelectionKind::Opening)
	{
		EOpeningSwingSide Side; EOpeningSwingDirection Dir;
		if (GetSelectedOpeningSwing(Side, Dir))
		{
			const FLinearColor Active(0.18f, 0.8f, 0.44f, 1.f), Inactive(0.17f, 0.17f, 0.18f, 1.f);
			if (BtnSwingLeft) BtnSwingLeft->SetBackgroundColor(Side == EOpeningSwingSide::Left ? Active : Inactive);
			if (BtnSwingRight) BtnSwingRight->SetBackgroundColor(Side == EOpeningSwingSide::Right ? Active : Inactive);
			if (BtnSwingInward) BtnSwingInward->SetBackgroundColor(Dir == EOpeningSwingDirection::Inward ? Active : Inactive);
			if (BtnSwingOutward) BtnSwingOutward->SetBackgroundColor(Dir == EOpeningSwingDirection::Outward ? Active : Inactive);
		}
	}

	// REQ-17 / REQ-18: rotate controls for objects / cabinet sets
	const ESlateVisibility RotVis = (bIs2DPanel && (Kind == EPlannerSelectionKind::Object || Kind == EPlannerSelectionKind::CabinetSet)) ? ESlateVisibility::Visible : ESlateVisibility::Collapsed;
	if (BtnRotateLeft) BtnRotateLeft->SetVisibility(RotVis);
	if (BtnRotateRight) BtnRotateRight->SetVisibility(RotVis);
	if (BtnCancelPlacement) BtnCancelPlacement->SetVisibility((bIs2DPanel && PlannerManager->HasPendingPlacement()) ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);

	UpdateFinishUI();
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

			PC->Server_UpdateOpeningDimensions(PlannerManager->SelectedSegmentID, PlannerManager->SelectedOpeningIndex, W / 100.f, H / 100.f, S / 100.f);
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
		int32 OpIdx = PlannerManager->SelectedOpeningIndex;
		if (OpIdx == -1) OpIdx = 0;

		float CurWidthM = 0.9f, CurHeightM = 2.1f, CurSillM = 0.f;
		PlannerManager->GetOpeningDetails(SegID, OpIdx, CurWidthM, CurHeightM, CurSillM);

		float NewWidthMeters = ParseLengthDimensionInput(Text.ToString(), CurWidthM);
		if (NewWidthMeters > 0.1f)
		{
			if (AAwsTutorial_PlayerController* PC = GetPreviewController())
			{
				PC->Server_UpdateOpeningDimensions(SegID, OpIdx, NewWidthMeters, CurHeightM, CurSillM);
			}
		}
	}
}

void URoomPlannerWidget::OnOpeningHeightCommitted(const FText& Text, ETextCommit::Type CommitMethod)
{
	if ((CommitMethod == ETextCommit::OnEnter || CommitMethod != ETextCommit::Default) && PlannerManager && PlannerManager->SelectedSegmentID != -1)
	{
		int32 SegID = PlannerManager->SelectedSegmentID;
		int32 OpIdx = PlannerManager->SelectedOpeningIndex;
		if (OpIdx == -1) OpIdx = 0;

		float CurWidthM = 0.9f, CurHeightM = 2.1f, CurSillM = 0.f;
		PlannerManager->GetOpeningDetails(SegID, OpIdx, CurWidthM, CurHeightM, CurSillM);

		float NewHeightMeters = ParseLengthDimensionInput(Text.ToString(), CurHeightM);
		if (NewHeightMeters > 0.1f)
		{
			if (AAwsTutorial_PlayerController* PC = GetPreviewController())
			{
				PC->Server_UpdateOpeningDimensions(SegID, OpIdx, CurWidthM, NewHeightMeters, CurSillM);
			}
		}
	}
}

void URoomPlannerWidget::OnOpeningSillHeightCommitted(const FText& Text, ETextCommit::Type CommitMethod)
{
	if ((CommitMethod == ETextCommit::OnEnter || CommitMethod != ETextCommit::Default) && PlannerManager && PlannerManager->SelectedSegmentID != -1)
	{
		int32 SegID = PlannerManager->SelectedSegmentID;
		int32 OpIdx = PlannerManager->SelectedOpeningIndex;
		if (OpIdx == -1) OpIdx = 0;

		float CurWidthM = 0.9f, CurHeightM = 2.1f, CurSillM = 0.f;
		PlannerManager->GetOpeningDetails(SegID, OpIdx, CurWidthM, CurHeightM, CurSillM);

		float NewSillMeters = ParseLengthDimensionInput(Text.ToString(), CurSillM);
		if (AAwsTutorial_PlayerController* PC = GetPreviewController())
		{
			PC->Server_UpdateOpeningDimensions(SegID, OpIdx, CurWidthM, CurHeightM, NewSillMeters);
		}
	}
}

void URoomPlannerWidget::UpdateMouseCursorPosition()
{
	if (!mouse_cursor)
	{
		mouse_cursor = Cast<UImage>(GetWidgetFromName(TEXT("mouse_cursor")));
	}
	if (!mouse_cursor)
	{
		return;
	}

	bool bIsDrawing = (bIsWidgetDrawingWall || (PlannerManager && PlannerManager->IsWallDrawingActive()));
	if (bIsDrawing && PlannerManager)
	{
		mouse_cursor->SetVisibility(ESlateVisibility::HitTestInvisible);

		APlayerController* PC = GetPreviewController();
		if (!PC) PC = GetOwningPlayer();

		if (PC)
		{
			FVector EndPointWorld = PlannerManager->GetDragCurrentPoint();
			FVector2D ScreenPos;
			if (PC->ProjectWorldLocationToScreen(EndPointWorld, ScreenPos))
			{
				float DPIScale = UWidgetLayoutLibrary::GetViewportScale(this);
				if (DPIScale > 0.001f)
				{
					ScreenPos /= DPIScale;
				}

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
	}
	else
	{
		mouse_cursor->SetVisibility(ESlateVisibility::Collapsed);
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
		bool bShowSnapIndicator = bIsSnapped && PlannerManager && !PlannerManager->IsWallDrawingActive();
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
		TxtFloorArea->SetText(FText::FromString(FString::Printf(TEXT("%.2f м²"), GetFloorAreaM2())));
	}
	if (TxtPerimeter)
	{
		TxtPerimeter->SetText(FText::FromString(FString::Printf(TEXT("%.2f м"), GetPerimeterLengthM())));
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
	PlannerManager->bPlannerUIOpen = true;
	bManagerDelegatesBound = true;
}

void URoomPlannerWidget::UnbindManagerDelegates()
{
	if (!PlannerManager) return;
	PlannerManager->OnInteractiveWallDragProgress.RemoveAll(this);
	PlannerManager->OnWallSelected.RemoveAll(this);
	PlannerManager->OnRoomPlannerUpdated.RemoveAll(this);
	PlannerManager->OnOperationRejected.RemoveAll(this);
	PlannerManager->OnSelectionChanged.RemoveAll(this);
	bManagerDelegatesBound = false;
}

void URoomPlannerWidget::HandleOperationRejected(const FString& Reason)
{
	if (TxtOperationMessage)
	{
		TxtOperationMessage->SetText(FText::FromString(Reason));
		TxtOperationMessage->SetVisibility(ESlateVisibility::HitTestInvisible);
		OperationMessageClearTime = GetWorld() ? GetWorld()->GetTimeSeconds() + 4.f : 0.f;
	}
	OnOperationRejectedMessage(Reason);
}

void URoomPlannerWidget::HandleSelectionChanged()
{
	UpdateDynamicPropertiesPanel();
	UpdateGuidanceHintText();

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
	if (!PlannerManager) return;

	const bool bActive = PlannerManager->IsNodeDragActive() || PlannerManager->GetSelectionKind() != EPlannerSelectionKind::None;
	if (!bActive)
	{
		if (SelectionLabelPanel && SelectionLabelPanel->GetVisibility() != ESlateVisibility::Collapsed)
		{
			SelectionLabelPanel->SetVisibility(ESlateVisibility::Collapsed);
			OnSelectionLabelsUpdated(TArray<FPlannerDimensionLabel>());
		}
		return;
	}

	const TArray<FPlannerDimensionLabel> Labels = GetSelectionLabels();

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

	// Anchor the optional panel at the primary label's screen position
	if (SelectionLabelPanel)
	{
		const FPlannerDimensionLabel* Anchor = FindLabel(TEXT("width"));
		if (!Anchor) Anchor = FindLabel(TEXT("length"));
		if (!Anchor) Anchor = FindLabel(TEXT("area"));
		if (!Anchor) Anchor = FindLabel(TEXT("size"));
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
		PC->Server_SetOpeningSwing(PlannerManager->SelectedSegmentID, PlannerManager->SelectedOpeningIndex, Side, Direction);
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
// REQ-13 / REQ-14: finishing
// ═══════════════════════════════════════════════════════════════════════════════

void URoomPlannerWidget::OpenPaintCatalogForSelection()
{
	if (!PlannerManager || !PlannerManager->CanApplyFinishToSelection())
	{
		HandleOperationRejected(TEXT("Сначала выберите стену, пол или объект"));
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

	UColorCatalogWidget* Catalog = UColorCatalogWidget::OpenColorCatalogForWidget(this, CatalogClass);
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
	ApplyFinishToSelection(ARoomPlannerManager::MakePaintFinish(Item.Code, Item.Color));
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
		PC->Server_SetWallFinish(PlannerManager->SelectedSegmentID, Finish);
		return true;
	case EPlannerSelectionKind::Floor:
		PC->Server_SetFloorFinish(PlannerManager->SelectedRoomID, Finish);
		return true;
	case EPlannerSelectionKind::Object:
		if (Finish.Type == ESurfaceFinishType::Tile)
		{
			HandleOperationRejected(TEXT("Плитку можно назначить только стене или полу"));
			return false;
		}
		PC->Server_SetPlacedObjectFinish(PlannerManager->SelectedObjectID, Finish);
		return true;
	default:
		HandleOperationRejected(TEXT("Сначала выберите стену, пол или объект"));
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
	FSurfaceFinish F;
	if (!GetSelectedSurfaceFinish(F) || !F.IsSet())
	{
		return TEXT("—");
	}
	if (F.Type == ESurfaceFinishType::Paint)
	{
		return FString::Printf(TEXT("Краска %s"), *F.GetKey());
	}
	return FString::Printf(TEXT("Плитка %s (%.0f см)"), *F.TileAssetID, F.TileSizeCm);
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
	if (TxtFinishAreas)
	{
		TxtFinishAreas->SetText(FText::FromString(GetFinishAreaSummaryText()));
	}
}

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

void URoomPlannerWidget::RotateSelected(float DeltaYawDeg)
{
	if (!PlannerManager) return;
	if (CurrentViewMode != ERoomPlannerViewMode::View2D) return; // moving / rotating is a 2D workflow
	AAwsTutorial_PlayerController* PC = GetPreviewController();
	if (!PC) return;

	if (!PlannerManager->SelectedObjectID.IsEmpty())
	{
		FPlacedFurnitureData D;
		if (PlannerManager->GetPlacedObject(PlannerManager->SelectedObjectID, D))
		{
			D.Rotation.Yaw = FRotator::NormalizeAxis(D.Rotation.Yaw + DeltaYawDeg);
			PlannerManager->MovePlacedObjectLocal(D.InstanceID, D.Location, D.Rotation);
			PC->Server_MovePlacedObject(D.InstanceID, D.Location, D.Rotation, D.Scale);
		}
	}
	else if (!PlannerManager->SelectedCabinetSetID.IsEmpty())
	{
		FPlacedCabinetSetData D;
		if (PlannerManager->GetCabinetSet(PlannerManager->SelectedCabinetSetID, D))
		{
			D.Rotation.Yaw = FRotator::NormalizeAxis(D.Rotation.Yaw + DeltaYawDeg);
			PlannerManager->MoveCabinetSetLocal(D.InstanceID, D.Location, D.Rotation);
			PC->Server_MoveCabinetSet(D.InstanceID, D.Location, D.Rotation);
		}
	}
}

void URoomPlannerWidget::DeleteSelected()
{
	if (!PlannerManager) return;
	if (CurrentViewMode != ERoomPlannerViewMode::View2D) return; // deleting is a 2D workflow (matches the Delete key handling)
	AAwsTutorial_PlayerController* PC = GetPreviewController();
	if (!PC) return;

	switch (PlannerManager->GetSelectionKind())
	{
	case EPlannerSelectionKind::Opening:
		PC->Server_DeleteOpening(PlannerManager->SelectedSegmentID, PlannerManager->SelectedOpeningIndex);
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

