// Copyright MaxiMall Project. All Rights Reserved.

#include "FurnitureConfigurator/UI/ViewmodeOverlayWidget.h"
#include "Components/Button.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
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
    bStudioInputActive = bEnable;
    bStudioOrbitDrag = false;
    bStudioPanDrag = false;
    StudioTouches.Reset();
    bStudioTouchPinch = false;
    SetPivotMarkerShown(false);

    if (AutoRotateIndicator)
    {
        AutoRotateIndicator->SetVisibility(ESlateVisibility::Collapsed);
    }

    if (bEnable)
    {
        // In-WBP controls hint (replaces the stage's code-Slate bar — the PC
        // disables that when HandlesControlsHint() is true).
        if (Txt_ControlsHint)
        {
            Txt_ControlsHint->SetText(FText::FromString(TEXT(
                "ЛКМ — вращение"
                "      ПКМ/СКМ — перемещение"
                "      Колесо — масштаб")));
        }

        // The widget itself must be hit-testable across the whole screen so empty
        // areas receive drags/wheel (buttons are deeper in the tree and still win
        // their own clicks).
        SetVisibility(ESlateVisibility::Visible);

        // Static pan indicator: pin the marker to the viewport center whatever
        // its WBP layout says. It deliberately does NOT track the 3D pivot.
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

void UViewmodeOverlayWidget::SetStudioCursor(EMouseCursor::Type InCursor)
{
    if (bStudioInputActive && OwningPC)
    {
        OwningPC->CurrentMouseCursor = InCursor;
    }
}

void UViewmodeOverlayWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
    Super::NativeTick(MyGeometry, InDeltaTime);

    // Optional auto-rotate indicator: visible exactly while the idle turntable
    // is turning the view. No-op unless the WBP provides the widget.
    if (AutoRotateIndicator && bStudioInputActive)
    {
        AFurniturePreviewActor* Preview = GetStudioPreview();
        const bool bRotating = Preview && Preview->IsStudioAutoRotating();
        const ESlateVisibility Wanted = bRotating ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed;
        if (AutoRotateIndicator->GetVisibility() != Wanted)
        {
            AutoRotateIndicator->SetVisibility(Wanted);
        }
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
        bStudioOrbitDrag = true;
        Preview->StudioSetOrbiting(true);
        SetStudioCursor(EMouseCursor::GrabHandClosed);
        // High-precision capture: cursor hides during the drag and reappears in
        // place on release (editor-viewport pattern), deltas keep flowing.
        return FReply::Handled().UseHighPrecisionMouseMovement(TakeWidget());
    }
    if (Button == EKeys::RightMouseButton || Button == EKeys::MiddleMouseButton)
    {
        bStudioPanDrag = true;
        Preview->StudioSetPanning(true);
        SetStudioCursor(EMouseCursor::CardinalCross);
        SetPivotMarkerShown(true);
        return FReply::Handled().UseHighPrecisionMouseMovement(TakeWidget());
    }

    return Super::NativeOnMouseButtonDown(InGeometry, InMouseEvent);
}

FReply UViewmodeOverlayWidget::NativeOnMouseButtonDoubleClick(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
    if (AFurniturePreviewActor* Preview = GetStudioPreview())
    {
        // LMB double-click: smart focus — zoom toward the clicked point on the
        // product, or glide back to the entry framing on empty background.
        if (InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
        {
            Preview->StudioSmartFocusAtCursor();
            return FReply::Handled();
        }
        // RMB/MMB double-click stays the start of another pan drag.
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
            Preview->StudioOrbitDrag(static_cast<float>(Delta.X), static_cast<float>(-Delta.Y));
        }
        else
        {
            Preview->StudioPanDrag(static_cast<float>(Delta.X), static_cast<float>(-Delta.Y));
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
        SetStudioCursor(EMouseCursor::GrabHand);
        return FReply::Handled().ReleaseMouseCapture();
    }
    if ((Button == EKeys::RightMouseButton || Button == EKeys::MiddleMouseButton) && bStudioPanDrag)
    {
        bStudioPanDrag = false;
        Preview->StudioSetPanning(false);
        SetStudioCursor(EMouseCursor::GrabHand);
        SetPivotMarkerShown(false);
        // NOTE: no RMB-click cinematic-tour toggle in studio mode — RMB is
        // dedicated to pan; the 5 s idle turntable covers auto-rotation.
        return FReply::Handled().ReleaseMouseCapture();
    }

    return Super::NativeOnMouseButtonUp(InGeometry, InMouseEvent);
}

FReply UViewmodeOverlayWidget::NativeOnMouseWheel(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
    if (AFurniturePreviewActor* Preview = GetStudioPreview())
    {
        Preview->StudioZoom(InMouseEvent.GetWheelDelta());
        return FReply::Handled();
    }
    return Super::NativeOnMouseWheel(InGeometry, InMouseEvent);
}

FReply UViewmodeOverlayWidget::NativeOnTouchStarted(const FGeometry& InGeometry, const FPointerEvent& InTouchEvent)
{
    AFurniturePreviewActor* Preview = GetStudioPreview();
    if (!Preview)
    {
        return Super::NativeOnTouchStarted(InGeometry, InTouchEvent);
    }

    StudioTouches.Add(InTouchEvent.GetPointerIndex(), InTouchEvent.GetScreenSpacePosition());

    if (StudioTouches.Num() == 1)
    {
        Preview->StudioSetOrbiting(true);
    }
    else if (StudioTouches.Num() == 2)
    {
        // Second finger: orbit ends, pinch-zoom + two-finger pan begins.
        Preview->StudioSetOrbiting(false);
        bStudioTouchPinch = true;
        Preview->StudioSetPanning(true);
        SetPivotMarkerShown(true);
    }
    return FReply::Handled();
}

FReply UViewmodeOverlayWidget::NativeOnTouchMoved(const FGeometry& InGeometry, const FPointerEvent& InTouchEvent)
{
    AFurniturePreviewActor* Preview = GetStudioPreview();
    const int32 PointerIndex = InTouchEvent.GetPointerIndex();
    if (!Preview || !StudioTouches.Contains(PointerIndex))
    {
        return Super::NativeOnTouchMoved(InGeometry, InTouchEvent);
    }

    const FVector2D NewPos = InTouchEvent.GetScreenSpacePosition();
    const FVector2D OldPos = StudioTouches[PointerIndex];

    if (bStudioTouchPinch && StudioTouches.Num() >= 2)
    {
        // The other finger's last known position anchors the pinch.
        FVector2D OtherPos = OldPos;
        for (const TPair<int32, FVector2D>& Touch : StudioTouches)
        {
            if (Touch.Key != PointerIndex)
            {
                OtherPos = Touch.Value;
                break;
            }
        }
        const float OldDist = static_cast<float>(FVector2D::Distance(OldPos, OtherPos));
        const float NewDist = static_cast<float>(FVector2D::Distance(NewPos, OtherPos));
        if (OldDist > 1.f && NewDist > 1.f)
        {
            Preview->StudioPinchZoom(NewDist / OldDist);
        }
        // Midpoint drag pans (Slate Y down -> up-positive for the studio API).
        const FVector2D MidDelta = (NewPos - OldPos) * 0.5f;
        Preview->StudioPanDrag(static_cast<float>(MidDelta.X), static_cast<float>(-MidDelta.Y));
    }
    else if (StudioTouches.Num() == 1)
    {
        const FVector2D Delta = NewPos - OldPos;
        Preview->StudioOrbitDrag(static_cast<float>(Delta.X), static_cast<float>(-Delta.Y));
    }

    StudioTouches[PointerIndex] = NewPos;
    return FReply::Handled();
}

FReply UViewmodeOverlayWidget::NativeOnTouchEnded(const FGeometry& InGeometry, const FPointerEvent& InTouchEvent)
{
    AFurniturePreviewActor* Preview = GetStudioPreview();
    const int32 PointerIndex = InTouchEvent.GetPointerIndex();
    if (!Preview || !StudioTouches.Contains(PointerIndex))
    {
        return Super::NativeOnTouchEnded(InGeometry, InTouchEvent);
    }

    StudioTouches.Remove(PointerIndex);

    if (bStudioTouchPinch && StudioTouches.Num() < 2)
    {
        bStudioTouchPinch = false;
        Preview->StudioSetPanning(false);
        SetPivotMarkerShown(false);
        if (StudioTouches.Num() == 1)
        {
            Preview->StudioSetOrbiting(true); // remaining finger continues as orbit
        }
    }
    else if (StudioTouches.Num() == 0)
    {
        Preview->StudioSetOrbiting(false); // fling continues from here
    }
    return FReply::Handled();
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
