// Copyright MaxiMall Project. All Rights Reserved.

#include "FurnitureConfigurator/UI/ViewmodeOverlayWidget.h"
#include "Components/Button.h"
#include "Components/Image.h"
#include "Components/CanvasPanelSlot.h"
#include "awsTutorial_PlayerController.h"
#include "FurnitureConfigurator/Preview/FurniturePreviewActor.h"
#include "FurnitureConfigurator/ShowroomBooth.h"
#include "ARExport/UI/ARExportModalWidget.h"

void UViewmodeOverlayWidget::NativeConstruct()
{
    Super::NativeConstruct();

    if (Btn_Back)
    {
        Btn_Back->OnClicked.RemoveAll(this);
        Btn_Back->OnClicked.AddDynamic(this, &UViewmodeOverlayWidget::OnBackClicked);
    }

    if (Btn_ARExport)
    {
        Btn_ARExport->OnClicked.RemoveAll(this);
        Btn_ARExport->OnClicked.AddDynamic(this, &UViewmodeOverlayWidget::OnARExportClicked);
    }
    if (BtnARExport)
    {
        BtnARExport->OnClicked.RemoveAll(this);
        BtnARExport->OnClicked.AddDynamic(this, &UViewmodeOverlayWidget::OnARExportClicked);
    }

    if (Btn_ResetView)
    {
        Btn_ResetView->OnClicked.RemoveAll(this);
        Btn_ResetView->OnClicked.AddDynamic(this, &UViewmodeOverlayWidget::OnResetViewClicked);
    }

    // The pivot marker only appears during an RMB/MMB pan drag.
    SetPivotMarkerShown(false);
}

void UViewmodeOverlayWidget::ConfigureForStudioInput(bool bEnable)
{
    UE_LOG(LogTemp, Warning, TEXT("[StudioDiag] Overlay ConfigureForStudioInput(%d) visibility(before)=%d focusable=%d"),
        bEnable ? 1 : 0, static_cast<int32>(GetVisibility()), IsFocusable() ? 1 : 0);

    bStudioInputActive = bEnable;
    bStudioOrbitDrag = false;
    bStudioPanDrag = false;
    SetPivotMarkerShown(false);

    if (bEnable)
    {
        // The widget itself must be hit-testable across the whole screen so empty
        // areas receive drags/wheel (buttons are deeper in the tree and still win
        // their own clicks).
        SetVisibility(ESlateVisibility::Visible);

        // The studio orbit/pan pivot always projects to the exact viewport
        // center (the camera sits on the spring-arm boresight looking at the
        // pivot, all offsets zeroed), so pin the marker there regardless of
        // where it was left in the WBP layout.
        if (Img_PivotMarker)
        {
            if (UCanvasPanelSlot* CanvasSlot = Cast<UCanvasPanelSlot>(Img_PivotMarker->Slot))
            {
                CanvasSlot->SetAnchors(FAnchors(0.5f, 0.5f));
                CanvasSlot->SetAlignment(FVector2D(0.5f, 0.5f));
                CanvasSlot->SetPosition(FVector2D::ZeroVector);
                CanvasSlot->SetAutoSize(true);
            }
        }
    }
}

void UViewmodeOverlayWidget::SetPivotMarkerShown(bool bShown)
{
    if (Img_PivotMarker)
    {
        // HitTestInvisible: the marker must never swallow the drag it indicates.
        Img_PivotMarker->SetVisibility(bShown ? ESlateVisibility::HitTestInvisible
                                              : ESlateVisibility::Collapsed);
    }
}

AFurniturePreviewActor* UViewmodeOverlayWidget::GetStudioPreview() const
{
    if (!bStudioInputActive || !OwningPC)
    {
        return nullptr;
    }
    AFurniturePreviewActor* Preview = OwningPC->GetActivePreviewActor();
    return (Preview && Preview->IsStudioStageMode()) ? Preview : nullptr;
}

FReply UViewmodeOverlayWidget::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
    AFurniturePreviewActor* Preview = GetStudioPreview();
    if (!Preview)
    {
        return Super::NativeOnMouseButtonDown(InGeometry, InMouseEvent);
    }

    const FKey Button = InMouseEvent.GetEffectingButton();
    if (Button == EKeys::LeftMouseButton)
    {
        if (!bDiagLoggedLMB)
        {
            bDiagLoggedLMB = true;
            UE_LOG(LogTemp, Warning, TEXT("[StudioDiag] Overlay LMB down -> orbit start (studio input layer active)"));
        }
        bStudioOrbitDrag = true;
        Preview->StudioSetOrbiting(true);
        // High-precision capture: cursor hides during the drag and reappears in
        // place on release (editor-viewport pattern), deltas keep flowing.
        return FReply::Handled().UseHighPrecisionMouseMovement(TakeWidget());
    }
    if (Button == EKeys::RightMouseButton || Button == EKeys::MiddleMouseButton)
    {
        if (!bDiagLoggedRMB)
        {
            bDiagLoggedRMB = true;
            UE_LOG(LogTemp, Warning, TEXT("[StudioDiag] Overlay RMB/MMB down -> pan start (studio input layer active)"));
        }
        bStudioPanDrag = true;
        StudioRMBPressTime = GetWorld() ? GetWorld()->GetRealTimeSeconds() : 0.f;
        StudioRMBDragDistance = 0.f;
        Preview->StudioSetPanning(true);
        SetPivotMarkerShown(true);
        return FReply::Handled().UseHighPrecisionMouseMovement(TakeWidget());
    }

    return Super::NativeOnMouseButtonDown(InGeometry, InMouseEvent);
}

FReply UViewmodeOverlayWidget::NativeOnMouseButtonDoubleClick(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
    // In studio mode a double-click is just the start of another drag.
    if (GetStudioPreview())
    {
        return NativeOnMouseButtonDown(InGeometry, InMouseEvent);
    }
    return Super::NativeOnMouseButtonDoubleClick(InGeometry, InMouseEvent);
}

FReply UViewmodeOverlayWidget::NativeOnMouseMove(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
    AFurniturePreviewActor* Preview = GetStudioPreview();
    if (!Preview || (!bStudioOrbitDrag && !bStudioPanDrag))
    {
        return Super::NativeOnMouseMove(InGeometry, InMouseEvent);
    }

    const FVector2D Delta = InMouseEvent.GetCursorDelta();
    if (!Delta.IsNearlyZero())
    {
        // Slate Y grows downward; the studio API expects up-positive.
        if (bStudioOrbitDrag)
        {
            if (!bDiagLoggedOrbitMove)
            {
                bDiagLoggedOrbitMove = true;
                UE_LOG(LogTemp, Warning, TEXT("[StudioDiag] Overlay orbit move delta=%s"), *Delta.ToString());
            }
            Preview->StudioOrbitDrag(static_cast<float>(Delta.X), static_cast<float>(-Delta.Y));
        }
        else
        {
            if (!bDiagLoggedPanMove)
            {
                bDiagLoggedPanMove = true;
                UE_LOG(LogTemp, Warning, TEXT("[StudioDiag] Overlay pan move delta=%s"), *Delta.ToString());
            }
            Preview->StudioPanDrag(static_cast<float>(Delta.X), static_cast<float>(-Delta.Y));
            StudioRMBDragDistance += static_cast<float>(Delta.Size());
        }
    }
    return FReply::Handled();
}

FReply UViewmodeOverlayWidget::NativeOnMouseButtonUp(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
    AFurniturePreviewActor* Preview = GetStudioPreview();
    if (!Preview)
    {
        return Super::NativeOnMouseButtonUp(InGeometry, InMouseEvent);
    }

    const FKey Button = InMouseEvent.GetEffectingButton();
    if (Button == EKeys::LeftMouseButton && bStudioOrbitDrag)
    {
        bStudioOrbitDrag = false;
        Preview->StudioSetOrbiting(false); // fling continues from here
        return FReply::Handled().ReleaseMouseCapture();
    }
    if ((Button == EKeys::RightMouseButton || Button == EKeys::MiddleMouseButton) && bStudioPanDrag)
    {
        bStudioPanDrag = false;
        Preview->StudioSetPanning(false);
        SetPivotMarkerShown(false);

        // Preserve the existing feature: a short RMB *click* (no real drag)
        // toggles the cinematic auto-tour, same thresholds as the legacy path.
        if (Button == EKeys::RightMouseButton && OwningPC)
        {
            const float Held = (GetWorld() ? GetWorld()->GetRealTimeSeconds() : 0.f) - StudioRMBPressTime;
            if (Held <= 0.35f && StudioRMBDragDistance <= 10.f)
            {
                OwningPC->ToggleCinematicTour(OwningPC->GetCurrentTargetBooth());
            }
        }
        return FReply::Handled().ReleaseMouseCapture();
    }

    return Super::NativeOnMouseButtonUp(InGeometry, InMouseEvent);
}

FReply UViewmodeOverlayWidget::NativeOnMouseWheel(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
    if (AFurniturePreviewActor* Preview = GetStudioPreview())
    {
        if (!bDiagLoggedWheel)
        {
            bDiagLoggedWheel = true;
            UE_LOG(LogTemp, Warning, TEXT("[StudioDiag] Overlay wheel delta=%.2f -> StudioZoom"), InMouseEvent.GetWheelDelta());
        }
        Preview->StudioZoom(InMouseEvent.GetWheelDelta());
        return FReply::Handled();
    }
    // [StudioDiag] wheel reached the overlay but the studio path is not active —
    // proves a routing/mode problem rather than a zoom-math problem.
    if (bStudioInputActive && !bDiagLoggedWheel)
    {
        bDiagLoggedWheel = true;
        AFurniturePreviewActor* RawPreview = OwningPC ? OwningPC->GetActivePreviewActor() : nullptr;
        UE_LOG(LogTemp, Warning, TEXT("[StudioDiag] Overlay wheel arrived but NO studio preview: pc=%d preview=%d studioMode=%d"),
            OwningPC ? 1 : 0, RawPreview ? 1 : 0, RawPreview ? (RawPreview->IsStudioStageMode() ? 1 : 0) : -1);
    }
    return Super::NativeOnMouseWheel(InGeometry, InMouseEvent);
}

void UViewmodeOverlayWidget::OnResetViewClicked()
{
    // Same reset the R key used to trigger: PC -> preview ResetRotation()
    // (studio path re-fits and glides back; legacy path snaps mesh/arm back).
    if (OwningPC)
    {
        OwningPC->ResetPreviewRotation();
    }
}

void UViewmodeOverlayWidget::OnBackClicked()
{
    AAwsTutorial_PlayerController* PC = OwningPC.Get();
    if (PC)
    {
        // Close the isolated preview viewport
        PC->CloseFurniturePreview();
    }
}

void UViewmodeOverlayWidget::OnARExportClicked()
{
    AAwsTutorial_PlayerController* PC = OwningPC.Get();
    if (!PC)
    {
        return;
    }

    // ViewMode exports the mesh currently displayed: the preview actor. The booth
    // is hidden while ViewMode is active, so it is only a fallback target.
    AActor* ExportTarget = PC->GetActivePreviewActor();
    if (!ExportTarget)
    {
        ExportTarget = PC->GetCurrentTargetBooth();
    }
    if (!ExportTarget)
    {
        return;
    }

    UClass* ModalClass = ARExportModalClass ? ARExportModalClass.Get() : UARExportModalWidget::StaticClass();
    UARExportModalWidget* Modal = CreateWidget<UARExportModalWidget>(PC, ModalClass);
    if (Modal)
    {
        Modal->AddToViewport(100);
        Modal->StartExportForActor(ExportTarget);
    }
}
