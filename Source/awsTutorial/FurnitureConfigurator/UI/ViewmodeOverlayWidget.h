// Copyright MaxiMall Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "ViewmodeOverlayWidget.generated.h"

class UButton;
class UImage;
class UTextBlock;
class AAwsTutorial_PlayerController;

/**
 * UViewmodeOverlayWidget
 * The overlay widget displayed during isolated Viewmode, providing a Back button.
 *
 * STUDIO INPUT LAYER: when the Studio Stage presentation is active, this widget
 * is also the mouse input surface for the ported StudioViewerTest interaction
 * (LMB orbit with inertia, RMB/MMB pan, wheel zoom, Btn_ResetView reset).
 * While panning, Img_PivotMarker (a designer-authored image in the WBP) is
 * shown pinned to the viewport center as a static pan indicator — it does NOT
 * track the 3D orbit pivot (which stays fixed at the product center under the
 * fixed-pivot pan scheme). Drags use Slate high-precision
 * mouse capture, so the cursor hides while dragging and reappears in place —
 * the editor-viewport pattern. Desktop and Pixel Streaming mouse input both
 * arrive here through Slate, so streaming behavior is identical. RMB is
 * dedicated to pan — there is deliberately no RMB cinematic-tour toggle in
 * studio mode (the 5 s idle turntable covers auto-rotation; the tour stays
 * a legacy-ViewMode/configurator-button feature).
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
    virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

    // Studio input layer (active only when bStudioInputActive).
    virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
    virtual FReply NativeOnMouseButtonUp(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
    virtual FReply NativeOnMouseMove(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
    virtual FReply NativeOnMouseWheel(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
    virtual FReply NativeOnMouseButtonDoubleClick(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;

    // Touch (Pixel Streaming tablets/phones): one finger orbits, two fingers
    // pinch-zoom and pan. Handling these natively also suppresses Slate's
    // synthesized mouse events, so the mouse path never double-fires.
    virtual FReply NativeOnTouchStarted(const FGeometry& InGeometry, const FPointerEvent& InTouchEvent) override;
    virtual FReply NativeOnTouchMoved(const FGeometry& InGeometry, const FPointerEvent& InTouchEvent) override;
    virtual FReply NativeOnTouchEnded(const FGeometry& InGeometry, const FPointerEvent& InTouchEvent) override;

    UPROPERTY(meta = (BindWidget))
    TObjectPtr<UButton> Btn_Back;

    /** Resets the studio/legacy preview view — replaces the old R-key reset. */
    UPROPERTY(meta = (BindWidgetOptional))
    TObjectPtr<UButton> Btn_ResetView;

    /** Designer-authored pan indicator, visible only while RMB/MMB panning.
        Pinned to the viewport center (re-anchored there when studio input
        activates); it does not track the 3D orbit pivot. */
    UPROPERTY(meta = (BindWidgetOptional))
    TObjectPtr<UImage> Img_PivotMarker;

    /** Optional in-WBP controls hint. When present, C++ fills its text and the
        stage's code-Slate hint bar is disabled — one UI system, stylable and
        localizable in the widget Blueprint. */
    UPROPERTY(meta = (BindWidgetOptional))
    TObjectPtr<UTextBlock> Txt_ControlsHint;

    /** Optional indicator (any widget) shown while the idle turntable is
        actually rotating the view, hidden the moment the user interacts. */
    UPROPERTY(meta = (BindWidgetOptional))
    TObjectPtr<UWidget> AutoRotateIndicator;

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

    // Active touches (pointer index -> last screen position) for the studio
    // touch interaction; pinch is active while two touches are tracked.
    TMap<int32, FVector2D> StudioTouches;
    bool bStudioTouchPinch = false;

    class AFurniturePreviewActor* GetStudioPreview() const;

    /** Sets the studio hover cursor on the owning PC (GrabHand when idle,
        GrabHandClosed while orbiting, CardinalCross while panning). */
    void SetStudioCursor(EMouseCursor::Type InCursor);

    /** Shows/hides Img_PivotMarker (no-op when the WBP has no such image). */
    void SetPivotMarkerShown(bool bShown);

public:
    void SetOwningPC(AAwsTutorial_PlayerController* InPC) { OwningPC = InPC; }

    /** Called by the PlayerController on preview open: true switches this widget
        into the studio input layer (and makes it hit-testable); false leaves
        the legacy Blueprint input path untouched. */
    void ConfigureForStudioInput(bool bEnable);

    /** True when the WBP contains Txt_ControlsHint — the PC then disables the
        stage's code-Slate hint bar in favor of the widget's own. */
    bool HandlesControlsHint() const { return Txt_ControlsHint != nullptr; }
};
