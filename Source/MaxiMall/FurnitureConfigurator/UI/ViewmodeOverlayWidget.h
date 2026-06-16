// Copyright MaxiMall Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "ViewmodeOverlayWidget.generated.h"

class UButton;
class AMaxiMallPreviewController;

/**
 * UViewmodeOverlayWidget
 * The overlay widget displayed during isolated Viewmode, providing a Back button.
 */
UCLASS()
class MAXIMALL_API UViewmodeOverlayWidget : public UUserWidget
{
    GENERATED_BODY()

protected:
    virtual void NativeConstruct() override;

    UPROPERTY(meta = (BindWidget))
    TObjectPtr<UButton> Btn_Back;

    UFUNCTION()
    void OnBackClicked();

private:
    UPROPERTY()
    TObjectPtr<AMaxiMallPreviewController> OwningPC;

public:
    void SetOwningPC(AMaxiMallPreviewController* InPC) { OwningPC = InPC; }
};
