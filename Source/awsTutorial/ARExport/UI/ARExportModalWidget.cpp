// Copyright MaxiMall Project. All Rights Reserved.

#include "ARExport/UI/ARExportModalWidget.h"
#include "ARExport/ARExportSubsystem.h"
#include "FurnitureConfigurator/ShowroomBooth.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "Components/Image.h"
#include "Components/ProgressBar.h"
#include "Components/TextBlock.h"
#include "Components/Button.h"
#include "Components/EditableTextBox.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
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

    if (Txt_DirectURL)
    {
        Txt_DirectURL->SetIsReadOnly(true);
        Txt_DirectURL->SetSelectAllTextWhenFocused(true);
    }
}

void UARExportModalWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
    Super::NativeTick(MyGeometry, InDeltaTime);

    if (!Txt_DirectURL || CachedWebARURL.IsEmpty() || Txt_DirectURL->GetVisibility() != ESlateVisibility::Visible)
    {
        return;
    }

    // Focused: show the full URL so a manual select/copy grabs the real link.
    // Unfocused: show the ellipsized version that fits the fixed width.
    const bool bFocused = Txt_DirectURL->HasKeyboardFocus();
    const float Width = Txt_DirectURL->GetCachedGeometry().GetLocalSize().X;
    const bool bWidthChanged = !FMath::IsNearlyEqual(Width, LastURLFieldWidth, 1.0f);

    if (bFocused != bURLFieldFocused || (bWidthChanged && !bFocused))
    {
        bURLFieldFocused = bFocused;
        LastURLFieldWidth = Width;
        RefreshDirectURLField(bFocused);
    }
}

void UARExportModalWidget::RefreshDirectURLField(bool bFocused)
{
    if (!Txt_DirectURL)
    {
        return;
    }

    if (bFocused || CachedWebARURL.IsEmpty())
    {
        Txt_DirectURL->SetText(FText::FromString(CachedWebARURL));
        return;
    }

    FString Display = CachedWebARURL;
    const float AvailableWidth = Txt_DirectURL->GetCachedGeometry().GetLocalSize().X - 16.0f; // inner padding margin

    if (AvailableWidth > 0.0f && FSlateApplication::IsInitialized())
    {
        const TSharedRef<FSlateFontMeasure> FontMeasure = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
        const FSlateFontInfo& Font = Txt_DirectURL->WidgetStyle.TextStyle.Font;

        if (FontMeasure->Measure(Display, Font).X > AvailableWidth)
        {
            const FString Ellipsis = TEXT("...");
            int32 Len = Display.Len();
            while (Len > 8 && FontMeasure->Measure(Display.Left(Len) + Ellipsis, Font).X > AvailableWidth)
            {
                --Len;
            }
            Display = Display.Left(Len) + Ellipsis;
        }
    }

    Txt_DirectURL->SetText(FText::FromString(Display));
}

void UARExportModalWidget::StartExport(AShowroomBooth* TargetBooth)
{
    StartExportForActor(TargetBooth);
}

void UARExportModalWidget::StartExportForActor(AActor* TargetActor)
{
    PendingTarget = TargetActor;
    PendingComponents.Reset();
    BeginDeferredExport();
}

void UARExportModalWidget::StartExportForComponents(AActor* TargetActor, const TArray<UStaticMeshComponent*>& OnlyComponents)
{
    PendingTarget = TargetActor;
    PendingComponents.Reset();
    for (UStaticMeshComponent* Comp : OnlyComponents)
    {
        PendingComponents.Add(Comp);
    }
    BeginDeferredExport();
}

void UARExportModalWidget::BeginDeferredExport()
{
    CachedWebARURL = TEXT("");

    if (ProgressBar_Export)
    {
        ProgressBar_Export->SetVisibility(ESlateVisibility::Visible);
        ProgressBar_Export->SetPercent(0.15f);
    }

    if (Img_QRCode)
    {
        Img_QRCode->SetVisibility(ESlateVisibility::Hidden);
    }

    if (Txt_Status)
    {
        Txt_Status->SetText(NSLOCTEXT("MaxiMall", "ARExportPreparing", "Создание 3D-модели и подготовка материалов…"));
    }

    if (Txt_DirectURL)
    {
        Txt_DirectURL->SetText(FText::GetEmpty());
        Txt_DirectURL->SetVisibility(ESlateVisibility::Collapsed);
    }

    // The official export runs synchronously on the game thread; defer it by one
    // tick so the modal and its progress bar are painted on screen first.
    if (UWorld* World = GetWorld())
    {
        World->GetTimerManager().SetTimerForNextTick(FTimerDelegate::CreateUObject(this, &UARExportModalWidget::RunPendingExport));
    }
    else
    {
        RunPendingExport();
    }
}

void UARExportModalWidget::RunPendingExport()
{
    if (ProgressBar_Export)
    {
        ProgressBar_Export->SetPercent(0.45f);
    }

    AActor* TargetActor = PendingTarget.Get();
    UGameInstance* GI = GetGameInstance();
    UARExportSubsystem* Subsystem = GI ? GI->GetSubsystem<UARExportSubsystem>() : nullptr;
    if (!Subsystem || !TargetActor)
    {
        HandleExportFinished(false, TEXT(""), TEXT(""), nullptr);
        return;
    }

    FOnARExportFinished Callback;
    Callback.BindDynamic(this, &UARExportModalWidget::HandleExportFinished);

    if (PendingComponents.Num() > 0)
    {
        TArray<UStaticMeshComponent*> Components;
        for (const TObjectPtr<UStaticMeshComponent>& Comp : PendingComponents)
        {
            if (Comp)
            {
                Components.Add(Comp);
            }
        }
        Subsystem->ExportActorComponentsToAR(TargetActor, Components, Callback);
    }
    else
    {
        Subsystem->ExportActorToAR(TargetActor, Callback);
    }
}

void UARExportModalWidget::HandleExportFinished(bool bSuccess, const FString& ExportedFilePath, const FString& WebARURL, UTexture2D* QRCodeTexture)
{
    CachedWebARURL = WebARURL;

    if (ProgressBar_Export)
    {
        ProgressBar_Export->SetPercent(1.0f);
        ProgressBar_Export->SetVisibility(ESlateVisibility::Collapsed);
    }

    if (bSuccess && QRCodeTexture)
    {
        if (Img_QRCode)
        {
            Img_QRCode->SetColorAndOpacity(FLinearColor::White);
            Img_QRCode->SetBrushFromTexture(QRCodeTexture, false);
            Img_QRCode->SetDesiredSizeOverride(FVector2D(320.0f, 320.0f));
            Img_QRCode->SetVisibility(ESlateVisibility::Visible);
        }

        if (Txt_Status)
        {
            Txt_Status->SetText(NSLOCTEXT("MaxiMall", "ARExportSuccess", "Наведите камеру смартфона на QR-код, чтобы посмотреть модель у себя в комнате"));
        }

        if (Txt_DirectURL)
        {
            Txt_DirectURL->SetVisibility(ESlateVisibility::Visible);
            bURLFieldFocused = false;
            LastURLFieldWidth = 0.0f;      // force the tick to re-measure and ellipsize
            RefreshDirectURLField(false);
        }
    }
    else
    {
        if (Txt_Status)
        {
            Txt_Status->SetText(NSLOCTEXT("MaxiMall", "ARExportError", "Не удалось создать AR-модель. Попробуйте ещё раз."));
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
            Txt_Status->SetText(NSLOCTEXT("MaxiMall", "ARURLCopied", "Ссылка скопирована в буфер обмена!"));
        }
    }
}
