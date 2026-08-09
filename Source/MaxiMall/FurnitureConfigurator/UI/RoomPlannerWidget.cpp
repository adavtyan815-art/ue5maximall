// Copyright 2026 MaxiMall. All Rights Reserved.

#include "FurnitureConfigurator/UI/RoomPlannerWidget.h"
#include "Constructor/RoomPlannerManager.h"
#include "FurnitureConfigurator/Preview/MaxiMallPreviewController.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Components/EditableTextBox.h"
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

	if (EditableTxtWallLength) { EditableTxtWallLength->OnTextCommitted.AddUniqueDynamic(this, &URoomPlannerWidget::OnWallLengthCommitted); }
	if (TxtSelectedWallLength) { TxtSelectedWallLength->OnTextCommitted.AddUniqueDynamic(this, &URoomPlannerWidget::OnWallLengthCommitted); }
	if (EditableTxtOpeningWidth) { EditableTxtOpeningWidth->OnTextCommitted.AddUniqueDynamic(this, &URoomPlannerWidget::OnOpeningWidthCommitted); }
	if (EditableTxtOpeningHeight) { EditableTxtOpeningHeight->OnTextCommitted.AddUniqueDynamic(this, &URoomPlannerWidget::OnOpeningHeightCommitted); }
	if (EditableTxtOpeningSillHeight) { EditableTxtOpeningSillHeight->OnTextCommitted.AddUniqueDynamic(this, &URoomPlannerWidget::OnOpeningSillHeightCommitted); }

	if (UWorld* World = GetWorld())
	{
		PlannerManager = ARoomPlannerManager::GetOrCreateInstance(World);
		if (PlannerManager)
		{
			PlannerManager->OnInteractiveWallDragProgress.AddUniqueDynamic(this, &URoomPlannerWidget::HandleWallDragProgress);
			PlannerManager->OnWallSelected.AddUniqueDynamic(this, &URoomPlannerWidget::HandleWallSelected);
		}
	}

	UpdateSummaryStatsUI();
	UpdateToolModeButtonStyles();
	UpdateViewModeButtonStyles();
}

void URoomPlannerWidget::SetViewMode(ERoomPlannerViewMode NewMode)
{
	CurrentViewMode = NewMode;
	if (PlannerManager)
	{
		PlannerManager->SetViewMode(NewMode == ERoomPlannerViewMode::View2D);
	}
	UpdateViewModeButtonStyles();
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

	if (BtnSelectTool) BtnSelectTool->SetBackgroundColor(CurrentToolMode == EPlannerToolMode::Select ? ActiveColor : InactiveColor);
	if (BtnDrawWallTool) BtnDrawWallTool->SetBackgroundColor(CurrentToolMode == EPlannerToolMode::DrawWall ? ActiveColor : InactiveColor);
	if (BtnDeleteTool) BtnDeleteTool->SetBackgroundColor(CurrentToolMode == EPlannerToolMode::Erase ? ActiveColor : InactiveColor);
}

void URoomPlannerWidget::UpdateViewModeButtonStyles()
{
	FLinearColor ActiveColor(0.2f, 0.6f, 1.0f, 1.0f);
	FLinearColor InactiveColor(0.17f, 0.17f, 0.18f, 1.0f);

	bool bIs2D = (CurrentViewMode == ERoomPlannerViewMode::View2D);

	if (Btn2DView) Btn2DView->SetBackgroundColor(bIs2D ? ActiveColor : InactiveColor);
	if (Btn_2DView) Btn_2DView->SetBackgroundColor(bIs2D ? ActiveColor : InactiveColor);
	if (Btn3DView) Btn3DView->SetBackgroundColor(!bIs2D ? ActiveColor : InactiveColor);
	if (Btn_3DView) Btn_3DView->SetBackgroundColor(!bIs2D ? ActiveColor : InactiveColor);

	bool bCeilingOn = PlannerManager && PlannerManager->bCeilingVisible;
	if (BtnToggleCeiling) BtnToggleCeiling->SetBackgroundColor(bCeilingOn ? ActiveColor : InactiveColor);
	if (Btn_ToggleCeiling) Btn_ToggleCeiling->SetBackgroundColor(bCeilingOn ? ActiveColor : InactiveColor);
	if (BtnCeiling) BtnCeiling->SetBackgroundColor(bCeilingOn ? ActiveColor : InactiveColor);
}

void URoomPlannerWidget::On2DViewClicked() { SetViewMode(ERoomPlannerViewMode::View2D); }
void URoomPlannerWidget::On3DViewClicked() { SetViewMode(ERoomPlannerViewMode::View3D); }
void URoomPlannerWidget::OnSelectToolClicked() { SetToolMode(EPlannerToolMode::Select); }
void URoomPlannerWidget::OnDrawWallToolClicked() { SetToolMode(EPlannerToolMode::DrawWall); }
void URoomPlannerWidget::OnDeleteToolClicked()
{
	if (PlannerManager)
	{
		if (PlannerManager->SelectedSegmentID != -1)
		{
			PlannerManager->DeleteSelectedWall();
			UpdateSummaryStatsUI();
		}
		else
		{
			SetToolMode(EPlannerToolMode::Erase);
		}
	}
}

void URoomPlannerWidget::OnAddDoorClicked() { if (PlannerManager) PlannerManager->AddDoorToSelectedWall(); }
void URoomPlannerWidget::OnAddWindowClicked() { if (PlannerManager) PlannerManager->AddWindowToSelectedWall(); }
void URoomPlannerWidget::OnPresetRoomClicked() { BuildPreset4x4mRoom(); UpdateSummaryStatsUI(); }
void URoomPlannerWidget::OnClearLayoutClicked() { ClearLayout(); UpdateSummaryStatsUI(); }
void URoomPlannerWidget::OnToggleCeilingClicked() { if (PlannerManager) PlannerManager->ToggleCeilingVisibility(); UpdateViewModeButtonStyles(); }

void URoomPlannerWidget::HandleWallSelected(int32 SegmentID, float LengthMeters)
{
	float LengthCm = LengthMeters * 100.f;
	FString FormattedText = FString::Printf(TEXT("%.0f cm"), LengthCm);
	if (EditableTxtWallLength)
	{
		EditableTxtWallLength->SetText(FText::FromString(FormattedText));
	}
	if (TxtSelectedWallLength)
	{
		TxtSelectedWallLength->SetText(FText::FromString(FormattedText));
	}

	if (PlannerManager)
	{
		float WidthM = 0.f, HeightM = 0.f, SillM = 0.f;
		if (PlannerManager->GetOpeningDetails(SegmentID, PlannerManager->SelectedOpeningIndex, WidthM, HeightM, SillM))
		{
			if (EditableTxtOpeningWidth) EditableTxtOpeningWidth->SetText(FText::FromString(FString::Printf(TEXT("%.0f cm"), WidthM * 100.f)));
			if (EditableTxtOpeningHeight) EditableTxtOpeningHeight->SetText(FText::FromString(FString::Printf(TEXT("%.0f cm"), HeightM * 100.f)));
			if (EditableTxtOpeningSillHeight) EditableTxtOpeningSillHeight->SetText(FText::FromString(FString::Printf(TEXT("%.0f cm"), SillM * 100.f)));
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
