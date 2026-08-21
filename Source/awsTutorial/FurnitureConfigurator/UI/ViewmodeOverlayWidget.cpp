// Copyright MaxiMall Project. All Rights Reserved.

#include "FurnitureConfigurator/UI/ViewmodeOverlayWidget.h"
#include "Components/Button.h"
#include "awsTutorial_PlayerController.h"

void UViewmodeOverlayWidget::NativeConstruct()
{
    Super::NativeConstruct();

    if (Btn_Back)
    {
        Btn_Back->OnClicked.RemoveAll(this);
        Btn_Back->OnClicked.AddDynamic(this, &UViewmodeOverlayWidget::OnBackClicked);
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
