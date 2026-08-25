// Copyright MaxiMall Project. All Rights Reserved.

#include "ARExport/UI/ARExportModalWidget.h"
#include "ARExport/ARExportSubsystem.h"
#include "Components/Image.h"
#include "Components/ProgressBar.h"
#include "Components/TextBlock.h"
#include "Components/Button.h"
#include "HAL/PlatformApplicationMisc.h"
#include "Kismet/GameplayStatics.h"

void UARExportModalWidget::NativeConstruct()
{
    Super::NativeConstruct();

    if (Btn_CloseModal)
    {
        Btn_CloseModal->OnClicked.RemoveAll(this);
        Btn_CloseModal->OnClicked.AddDynamic(this, &UARExportModalWidget::OnCloseModalClicked);
    }

    if (Btn_CopyURL)
    {
        Btn_CopyURL->OnClicked.RemoveAll(this);
        Btn_CopyURL->OnClicked.AddDynamic(this, &UARExportModalWidget::OnCopyURLClicked);
    }
}

void UARExportModalWidget::StartExport(AShowroomBooth* TargetBooth)
{
    CachedWebARURL = TEXT("");

    if (ProgressBar_Export)
    {
        ProgressBar_Export->SetVisibility(ESlateVisibility::Visible);
        ProgressBar_Export->SetPercent(0.3f);
    }

    if (Img_QRCode)
    {
        Img_QRCode->SetVisibility(ESlateVisibility::Hidden);
    }

    if (Txt_Status)
    {
        Txt_Status->SetText(NSLOCTEXT("MaxiMall", "ARExportPreparing", "Generating 3D model & baking materials..."));
    }

    if (Txt_DirectURL)
    {
        Txt_DirectURL->SetText(FText::GetEmpty());
        Txt_DirectURL->SetVisibility(ESlateVisibility::Collapsed);
    }

    UGameInstance* GI = GetGameInstance();
    if (!GI)
    {
        HandleExportFinished(false, TEXT(""), TEXT(""), nullptr);
        return;
    }

    UARExportSubsystem* Subsystem = GI->GetSubsystem<UARExportSubsystem>();
    if (!Subsystem)
    {
        HandleExportFinished(false, TEXT(""), TEXT(""), nullptr);
        return;
    }

    FOnARExportFinished Callback;
    Callback.BindDynamic(this, &UARExportModalWidget::HandleExportFinished);

    Subsystem->ExportBoothToAR(TargetBooth, Callback);
}

void UARExportModalWidget::HandleExportFinished(bool bSuccess, const FString& ExportedFilePath, const FString& WebARURL, UTexture2D* QRCodeTexture)
{
    CachedWebARURL = WebARURL;

    if (ProgressBar_Export)
    {
        ProgressBar_Export->SetVisibility(ESlateVisibility::Collapsed);
    }

    if (bSuccess && QRCodeTexture)
    {
        if (Img_QRCode)
        {
            Img_QRCode->SetBrushFromTexture(QRCodeTexture, true);
            Img_QRCode->SetVisibility(ESlateVisibility::Visible);
        }

        if (Txt_Status)
        {
            Txt_Status->SetText(NSLOCTEXT("MaxiMall", "ARExportSuccess", "Point your smartphone camera at this code to view in your room"));
        }

        if (Txt_DirectURL)
        {
            Txt_DirectURL->SetText(FText::FromString(WebARURL));
            Txt_DirectURL->SetVisibility(ESlateVisibility::Visible);
        }
    }
    else
    {
        if (Txt_Status)
        {
            Txt_Status->SetText(NSLOCTEXT("MaxiMall", "ARExportError", "Failed to generate AR model. Please try again."));
        }
    }
}

void UARExportModalWidget::OnCloseModalClicked()
{
    RemoveFromParent();
}

void UARExportModalWidget::OnCopyURLClicked()
{
    if (!CachedWebARURL.IsEmpty())
    {
        FPlatformApplicationMisc::ClipboardCopy(*CachedWebARURL);
        if (Txt_Status)
        {
            Txt_Status->SetText(NSLOCTEXT("MaxiMall", "ARURLCopied", "Link copied to clipboard!"));
        }
    }
}
