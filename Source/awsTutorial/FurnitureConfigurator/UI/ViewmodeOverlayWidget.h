// Copyright MaxiMall Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "ViewmodeOverlayWidget.generated.h"

class UButton;
class UImage;
class AAwsTutorial_PlayerController;

/**
 * UViewmodeOverlayWidget
 * The overlay widget displayed during isolated Viewmode, providing a Back button.
 *
 * STUDIO INPUT LAYER: when the Studio Stage presentation is active, this widget
 * is also the mouse input surface for the ported StudioViewerTest interaction
 * (LMB orbit with inertia, RMB/MMB pan, wheel zoom, Btn_ResetView reset).
 * While panning, Img_PivotMarker (a designer-authored image in the WBP) is
 * shown at the viewport center — the studio camera always looks straight down
 * the spring-arm boresight at the orbit pivot, so the pivot's screen position
 * is exactly the viewport center at all times. Drags use Slate high-precision
 * mouse capture, so the cursor hides while dragging and reappears in place —
 * the editor-viewport pattern. Desktop and Pixel Streaming mouse input both
 * arrive here through Slate, so streaming behavior is identical. An RMB
 * *click* (short, no drag) still toggles the cinematic tour exactly as before.
 *
 * In legacy (WorldInPlace) mode every handler defers to Super:: — the Blueprint
 * graph's own input events keep firing unchanged.
 */
UCLASS()
class AWSTUTORIAL_API UViewmodeOverlayWidget : public UUserWidget
{
    GENERATED_BODY()

protected:
    virtual void NativeConstruct() override;

    // Studio input layer (active only when bStudioInputActive).
    virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
    virtual FReply NativeOnMouseButtonUp(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
    virtual FReply NativeOnMouseMove(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
    virtual FReply NativeOnMouseWheel(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
    virtual FReply NativeOnMouseButtonDoubleClick(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;

    UPROPERTY(meta = (BindWidget))
    TObjectPtr<UButton> Btn_Back;

    /** Resets the studio/legacy preview view — replaces the old R-key reset. */
    UPROPERTY(meta = (BindWidgetOptional))
    TObjectPtr<UButton> Btn_ResetView;

    /** Designer-authored pivot marker, visible only while RMB/MMB panning.
        Re-anchored to the viewport center when studio input activates, because
        the orbit/pan pivot always projects to the exact screen center. */
    UPROPERTY(meta = (BindWidgetOptional))
    TObjectPtr<UImage> Img_PivotMarker;

    UPROPERTY(meta = (BindWidgetOptional))
    TObjectPtr<UButton> Btn_ARExport;

    UPROPERTY(meta = (BindWidgetOptional))
    TObjectPtr<UButton> BtnARExport;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MaxiMall | UI Classes")
    TSubclassOf<UUserWidget> ARExportModalClass;

    UFUNCTION()
    void OnBackClicked();

    UFUNCTION()
    void OnARExportClicked();

    UFUNCTION()
    void OnResetViewClicked();

private:
    UPROPERTY()
    TObjectPtr<AAwsTutorial_PlayerController> OwningPC;

    /** True while the Studio Stage presentation drives this preview session. */
    bool bStudioInputActive = false;

    bool bStudioOrbitDrag = false;
    bool bStudioPanDrag = false;

    // RMB click-vs-drag discrimination for the cinematic-tour toggle (high-
    // precision capture freezes the cursor, so distance is accumulated deltas).
    float StudioRMBPressTime = 0.f;
    float StudioRMBDragDistance = 0.f;

    // [StudioDiag] one-shot logging flags — diagnostics only, no behavior.
    bool bDiagLoggedWheel = false;
    bool bDiagLoggedLMB = false;
    bool bDiagLoggedRMB = false;
    bool bDiagLoggedOrbitMove = false;
    bool bDiagLoggedPanMove = false;

    class AFurniturePreviewActor* GetStudioPreview() const;

    /** Shows/hides Img_PivotMarker (no-op when the WBP has no such image). */
    void SetPivotMarkerShown(bool bShown);

public:
    void SetOwningPC(AAwsTutorial_PlayerController* InPC) { OwningPC = InPC; }

    /** Called by the PlayerController on preview open: true switches this widget
        into the studio input layer (and makes it hit-testable); false leaves
        the legacy Blueprint input path untouched. */
    void ConfigureForStudioInput(bool bEnable);
};
