// Copyright 2026 MaxiMall. All Rights Reserved.

#include "FurnitureConfigurator/UI/RoomPlannerWidget.h"
#include "Constructor/RoomPlannerManager.h"
#include "FurnitureConfigurator/Preview/MaxiMallPreviewController.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Components/EditableTextBox.h"
#include "Components/Image.h"
#include "Kismet/GameplayStatics.h"

void URoomPlannerWidget::NativeConstruct()
{
	Super::NativeConstruct();

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

	if (EditableTxtOpeningWidth) { EditableTxtOpeningWidth->OnTextCommitted.AddUniqueDynamic(this, &URoomPlannerWidget::OnOpeningWidthCommitted); }
	if (EditableTxtOpeningHeight) { EditableTxtOpeningHeight->OnTextCommitted.AddUniqueDynamic(this, &URoomPlannerWidget::OnOpeningHeightCommitted); }
	if (EditableTxtOpeningSillHeight) { EditableTxtOpeningSillHeight->OnTextCommitted.AddUniqueDynamic(this, &URoomPlannerWidget::OnOpeningSillHeightCommitted); }

	if (UWorld* World = GetWorld())
	{
		PlannerManager = ARoomPlannerManager::GetOrCreateInstance(World);
		if (PlannerManager)
		{
			PlannerManager->OnInteractiveWallDragProgress.AddUniqueDynamic(this, &URoomPlannerWidget::HandleWallDragProgress);
			PlannerManager->OnWallSelected.AddUniqueDynamic(this, &URoomPlannerWidget::OnWallSelected);
		}
	}

	if (BtnApplyProperties) { BtnApplyProperties->OnClicked.AddUniqueDynamic(this, &URoomPlannerWidget::OnApplyPropertiesClicked); }

	// Initialize UI state
	UpdateViewModeButtonStyles();
	UpdateDynamicPropertiesPanel();

	UpdateSummaryStatsUI();
	UpdateToolModeButtonStyles();
	UpdateViewModeButtonStyles();
}

void URoomPlannerWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

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
	}
}

void URoomPlannerWidget::SetViewMode(ERoomPlannerViewMode NewMode)
{
	if (CurrentViewMode == NewMode) return;
	CurrentViewMode = NewMode;
	if (PlannerManager)
	{
		PlannerManager->SetViewMode(NewMode == ERoomPlannerViewMode::View2D);
	}
	UpdateViewModeButtonStyles();
	UpdateDynamicPropertiesPanel();
}

void URoomPlannerWidget::SetToolMode(EPlannerToolMode NewToolMode)
{
	if (PlannerManager)
	{
		PlannerManager->SetToolMode(NewToolMode);
	}
	UpdateToolModeButtonStyles();
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
		}
		else
		{
			BtnSelectTool->SetBackgroundColor(CurrentToolMode == EPlannerToolMode::Select ? ActiveColor : InactiveColor);
		}
	}
	if (BtnDrawWallTool) BtnDrawWallTool->SetBackgroundColor(CurrentToolMode == EPlannerToolMode::DrawWall ? ActiveColor : InactiveColor);
	if (BtnDeleteTool) BtnDeleteTool->SetBackgroundColor(CurrentToolMode == EPlannerToolMode::Erase ? ActiveColor : InactiveColor);
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
	
	// BtnSelectTool is handled by NativeTick, but we can also force it here
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
		if (PlannerManager->SelectedSegmentID != -1)
		{
			if (PlannerManager->SelectedOpeningIndex != -1)
			{
				PlannerManager->DeleteSelectedOpening();
			}
			else
			{
				PlannerManager->DeleteSelectedWall();
			}
			UpdateSummaryStatsUI();
		}
		else
		{
			SetToolMode(EPlannerToolMode::Erase);
		}
	}
}

void URoomPlannerWidget::OnAddDoorClicked()
{
	if (!PlannerManager) return;
	float W = 0.9f;
	float H = 2.1f;
	if (EditableTxtOpeningWidth && !EditableTxtOpeningWidth->GetText().IsEmpty()) W = FCString::Atof(*EditableTxtOpeningWidth->GetText().ToString()) / 100.f;
	if (EditableTxtOpeningHeight && !EditableTxtOpeningHeight->GetText().IsEmpty()) H = FCString::Atof(*EditableTxtOpeningHeight->GetText().ToString()) / 100.f;
	PlannerManager->AddDoorToSelectedWall(W, H);
}

void URoomPlannerWidget::OnAddWindowClicked()
{
	if (!PlannerManager) return;
	float W = 1.2f;
	float H = 1.2f;
	float S = 0.9f;
	if (EditableTxtOpeningWidth_1 && !EditableTxtOpeningWidth_1->GetText().IsEmpty()) W = FCString::Atof(*EditableTxtOpeningWidth_1->GetText().ToString()) / 100.f;
	if (EditableTxtOpeningHeight_1 && !EditableTxtOpeningHeight_1->GetText().IsEmpty()) H = FCString::Atof(*EditableTxtOpeningHeight_1->GetText().ToString()) / 100.f;
	if (EditableTxtOpeningSillHeight && !EditableTxtOpeningSillHeight->GetText().IsEmpty()) S = FCString::Atof(*EditableTxtOpeningSillHeight->GetText().ToString()) / 100.f;
	PlannerManager->AddWindowToSelectedWall(W, H, S);
}

void URoomPlannerWidget::OnPresetRoomClicked() { BuildPreset4x4mRoom(); UpdateSummaryStatsUI(); }
void URoomPlannerWidget::OnClearLayoutClicked() { ClearLayout(); UpdateSummaryStatsUI(); }
void URoomPlannerWidget::OnToggleCeilingClicked() { if (PlannerManager) PlannerManager->ToggleCeilingVisibility(); UpdateViewModeButtonStyles(); }

void URoomPlannerWidget::OnWallSelected(int32 SegmentID, float LengthMeters)
{
	UpdateDynamicPropertiesPanel();

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
			if (EditableTxtProp2) EditableTxtProp2->SetVisibility(ESlateVisibility::Hidden);
			if (EditableTxtProp3) EditableTxtProp3->SetVisibility(ESlateVisibility::Hidden);
			if (TxtApplyProperties) TxtApplyProperties->SetText(FText::FromString(TEXT("Изменить размер стены")));
		}

		if (BtnApplyProperties) BtnApplyProperties->SetVisibility(ESlateVisibility::Visible);
		if (BtnDeleteTool) BtnDeleteTool->SetVisibility(ESlateVisibility::Visible);
		if (Image_2) Image_2->SetVisibility(ESlateVisibility::Visible);
	}
	else
	{
		// Nothing is selected
		if (EditableTxtProp1) EditableTxtProp1->SetVisibility(ESlateVisibility::Hidden);
		if (EditableTxtProp2) EditableTxtProp2->SetVisibility(ESlateVisibility::Hidden);
		if (EditableTxtProp3) EditableTxtProp3->SetVisibility(ESlateVisibility::Hidden);
		if (TxtApplyProperties) TxtApplyProperties->SetText(FText::FromString(TEXT("Размер стены")));

		if (BtnApplyProperties) BtnApplyProperties->SetVisibility(ESlateVisibility::Collapsed);
		if (BtnDeleteTool) BtnDeleteTool->SetVisibility(ESlateVisibility::Collapsed);
		if (Image_2) Image_2->SetVisibility(ESlateVisibility::Collapsed);
	}
}

void URoomPlannerWidget::OnApplyPropertiesClicked()
{
	if (!PlannerManager || PlannerManager->SelectedSegmentID == -1) return;

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

			PlannerManager->UpdateOpeningDimensions(PlannerManager->SelectedSegmentID, PlannerManager->SelectedOpeningIndex, W / 100.f, H / 100.f, S / 100.f);
		}
	}
	else
	{
		// Modify Wall
		if (EditableTxtProp1)
		{
			float NewLenMeters = FCString::Atof(*EditableTxtProp1->GetText().ToString()) / 100.f;
			PlannerManager->SetWallLength(PlannerManager->SelectedSegmentID, NewLenMeters);
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
	if ((CommitMethod == ETextCommit::OnEnter || CommitMethod != ETextCommit::Default) && PlannerManager)
	{
		float NewLengthMeters = ParseLengthDimensionInput(Text.ToString(), 4.0f);
		if (NewLengthMeters > 0.1f)
		{
			PlannerManager->SetWallLength(PlannerManager->SelectedSegmentID, NewLengthMeters);
			UpdateSummaryStatsUI();
		}
	}
}

void URoomPlannerWidget::OnOpeningWidthCommitted(const FText& Text, ETextCommit::Type CommitMethod)
{
	if ((CommitMethod == ETextCommit::OnEnter || CommitMethod != ETextCommit::Default) && PlannerManager)
	{
		int32 SegID = PlannerManager->SelectedSegmentID;
		int32 OpIdx = PlannerManager->SelectedOpeningIndex;
		if (OpIdx == -1) OpIdx = 0;

		float CurWidthM = 0.9f, CurHeightM = 2.1f, CurSillM = 0.f;
		PlannerManager->GetOpeningDetails(SegID, OpIdx, CurWidthM, CurHeightM, CurSillM);

		float NewWidthMeters = ParseLengthDimensionInput(Text.ToString(), CurWidthM);
		if (NewWidthMeters > 0.1f)
		{
			PlannerManager->UpdateOpeningDimensions(SegID, OpIdx, NewWidthMeters, CurHeightM, CurSillM);
		}
	}
}

void URoomPlannerWidget::OnOpeningHeightCommitted(const FText& Text, ETextCommit::Type CommitMethod)
{
	if ((CommitMethod == ETextCommit::OnEnter || CommitMethod != ETextCommit::Default) && PlannerManager)
	{
		int32 SegID = PlannerManager->SelectedSegmentID;
		int32 OpIdx = PlannerManager->SelectedOpeningIndex;
		if (OpIdx == -1) OpIdx = 0;

		float CurWidthM = 0.9f, CurHeightM = 2.1f, CurSillM = 0.f;
		PlannerManager->GetOpeningDetails(SegID, OpIdx, CurWidthM, CurHeightM, CurSillM);

		float NewHeightMeters = ParseLengthDimensionInput(Text.ToString(), CurHeightM);
		if (NewHeightMeters > 0.1f)
		{
			PlannerManager->UpdateOpeningDimensions(SegID, OpIdx, CurWidthM, NewHeightMeters, CurSillM);
		}
	}
}

void URoomPlannerWidget::OnOpeningSillHeightCommitted(const FText& Text, ETextCommit::Type CommitMethod)
{
	if ((CommitMethod == ETextCommit::OnEnter || CommitMethod != ETextCommit::Default) && PlannerManager)
	{
		int32 SegID = PlannerManager->SelectedSegmentID;
		int32 OpIdx = PlannerManager->SelectedOpeningIndex;
		if (OpIdx == -1) OpIdx = 0;

		float CurWidthM = 0.9f, CurHeightM = 2.1f, CurSillM = 0.f;
		PlannerManager->GetOpeningDetails(SegID, OpIdx, CurWidthM, CurHeightM, CurSillM);

		float NewSillMeters = ParseLengthDimensionInput(Text.ToString(), CurSillM);
		PlannerManager->UpdateOpeningDimensions(SegID, OpIdx, CurWidthM, CurHeightM, NewSillMeters);
	}
}

#include "Blueprint/WidgetLayoutLibrary.h"

void URoomPlannerWidget::HandleWallDragProgress(float LengthMeters, FVector MidpointWorld, float AngleDeg, bool bIsSnapped)
{
	CurrentDragLengthMeters = LengthMeters;
	CurrentDragMidpointWorld = MidpointWorld;
	bIsAngleSnapped = bIsSnapped;

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
	OnWallDragProgress(LengthMeters, MidpointWorld, AngleDeg, bIsSnapped);
}

void URoomPlannerWidget::UpdateSummaryStatsUI()
{
	if (TxtFloorArea)
	{
		TxtFloorArea->SetText(FText::FromString(FString::Printf(TEXT("%.2f m²"), GetFloorAreaM2())));
	}
	if (TxtPerimeter)
	{
		TxtPerimeter->SetText(FText::FromString(FString::Printf(TEXT("%.2f m"), GetPerimeterLengthM())));
	}
}

FString URoomPlannerWidget::GetFormattedDragLengthText() const
{
	return FString::Printf(TEXT("%.2f m"), CurrentDragLengthMeters);
}

void URoomPlannerWidget::BuildPreset4x4mRoom()
{
	if (!PlannerManager && GetWorld())
	{
		PlannerManager = ARoomPlannerManager::GetOrCreateInstance(GetWorld());
	}

	if (APlayerController* PC = GetOwningPlayer())
	{
		if (!PC->HasAuthority())
		{
			if (AMaxiMallPreviewController* MaxiPC = Cast<AMaxiMallPreviewController>(PC))
			{
				MaxiPC->Server_BuildPreset4x4mRoom();
				return;
			}
		}
	}

	if (PlannerManager)
	{
		PlannerManager->ClearLayout();
		int32 N1 = PlannerManager->AddNode(FVector2D(-200.f, -200.f));
		int32 N2 = PlannerManager->AddNode(FVector2D(200.f, -200.f));
		int32 N3 = PlannerManager->AddNode(FVector2D(200.f, 200.f));
		int32 N4 = PlannerManager->AddNode(FVector2D(-200.f, 200.f));
		PlannerManager->AddWall(N1, N2);
		PlannerManager->AddWall(N2, N3);
		PlannerManager->AddWall(N3, N4);
		PlannerManager->AddWall(N4, N1);
		PlannerManager->RebuildRooms();
		PlannerManager->ReplicatedRoomJSON = PlannerManager->ExportLayoutToJSON();
	}
}

void URoomPlannerWidget::ClearLayout()
{
	if (APlayerController* PC = GetOwningPlayer())
	{
		if (!PC->HasAuthority())
		{
			if (AMaxiMallPreviewController* MaxiPC = Cast<AMaxiMallPreviewController>(PC))
			{
				MaxiPC->Server_ClearLayout();
				return;
			}
		}
	}

	if (PlannerManager)
	{
		PlannerManager->ClearLayout();
		PlannerManager->ReplicatedRoomJSON = PlannerManager->ExportLayoutToJSON();
	}
}

void URoomPlannerWidget::InsertDoor(int32 WallSegmentID, float DistanceAlongWallCm)
{
	if (PlannerManager)
	{
		FString Cmd = FString::Printf(TEXT("{\"cmd\":\"add_opening\",\"segment_id\":%d,\"type\":\"door\",\"dist\":%.2f,\"width\":90,\"height\":210,\"sill\":0}"), WallSegmentID, DistanceAlongWallCm);
		PlannerManager->ProcessCommandJSON(Cmd);
	}
}

void URoomPlannerWidget::InsertWindow(int32 WallSegmentID, float DistanceAlongWallCm)
{
	if (PlannerManager)
	{
		FString Cmd = FString::Printf(TEXT("{\"cmd\":\"add_opening\",\"segment_id\":%d,\"type\":\"window\",\"dist\":%.2f,\"width\":120,\"height\":120,\"sill\":90}"), WallSegmentID, DistanceAlongWallCm);
		PlannerManager->ProcessCommandJSON(Cmd);
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
