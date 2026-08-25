// Copyright MaxiMall Project. All Rights Reserved.

#include "FurnitureConfigurator/UI/ViewmodeOverlayWidget.h"
#include "Components/Button.h"
#include "awsTutorial_PlayerController.h"
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

    AShowroomBooth* Booth = PC->GetCurrentTargetBooth();
    if (!Booth)
    {
        return;
    }

    UClass* ModalClass = ARExportModalClass ? ARExportModalClass.Get() : UARExportModalWidget::StaticClass();
    UARExportModalWidget* Modal = CreateWidget<UARExportModalWidget>(PC, ModalClass);
    if (Modal)
    {
        Modal->AddToViewport(100);
        Modal->StartExport(Booth);
    }
}
