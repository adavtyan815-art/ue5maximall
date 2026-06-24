// Copyright MaxiMall Project. All Rights Reserved.

#include "FurnitureConfigurator/UI/ConfiguratorMainWidget.h"
#include "Components/TextBlock.h"
#include "Components/Button.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Image.h"
#include "Components/PanelWidget.h"
#include "Engine/Texture2D.h"
#include "FurnitureConfigurator/ShowroomBooth.h"
#include "FurnitureConfigurator/Preview/MaxiMallPreviewController.h"
#include "Engine/Engine.h"
#include "Components/ScrollBox.h"
#include "Components/UniformGridPanel.h"
#include "Components/UniformGridSlot.h"
#include "Components/SizeBox.h"
#include "Components/ScaleBox.h"
#include "Components/ButtonSlot.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/WrapBoxSlot.h"

void UFurnitureOptionListener::OnButtonClicked()
{
    if (OwnerWidget)
    {
        OwnerWidget->HandleOptionSelected(Component, Type, OptionIndex);
    }
}

void UFurnitureOptionListener::OnButtonHovered()
{
    if (OwnerWidget)
    {
        OwnerWidget->HandleOptionHovered(Component, Type, OptionIndex);
    }
}

void UFurnitureOptionListener::OnButtonUnhovered()
{
    if (OwnerWidget)
    {
        OwnerWidget->HandleOptionUnhovered(Component, Type, OptionIndex);
    }
}

void UConfiguratorMainWidget::NativeConstruct()
{
    Super::NativeConstruct();

    if (Btn_Viewmode)
    {
        Btn_Viewmode->OnClicked.RemoveAll(this);
        Btn_Viewmode->OnClicked.AddDynamic(this, &UConfiguratorMainWidget::OnViewmodeClicked);
    }
    if (Btn_CloseUI)
    {
        Btn_CloseUI->OnClicked.RemoveAll(this);
        Btn_CloseUI->OnClicked.AddDynamic(this, &UConfiguratorMainWidget::OnCloseUIClicked);
    }
    if (Txt_BtnURL)
    {
        Txt_BtnURL->OnClicked.RemoveAll(this);
        Txt_BtnURL->OnClicked.AddDynamic(this, &UConfiguratorMainWidget::OnURLButtonClicked);
    }
}

void UConfiguratorMainWidget::SetupWidget(AMaxiMallPreviewController* InPC, AShowroomBooth* InBooth, EFurnitureComponentType InComponent)
{
    OwningPC = InPC;
    TargetBooth = InBooth;
    ActiveComponent = InComponent;

    RefreshSelections();
}

void UConfiguratorMainWidget::RefreshSelections()
{
    AShowroomBooth* Booth = TargetBooth.Get();
    if (!Booth)
    {
        return;
    }

    FFurnitureProductRow ProductData;
    if (Booth->GetActiveProductData(ProductData))
    {
        // Gate optional component visibility — if the component's mesh is missing/null, collapse the UI selectors
        bool bIsValidMesh = IsComponentMeshValid(Booth, ActiveComponent);
        ESlateVisibility TargetVisibility = bIsValidMesh ? ESlateVisibility::Visible : ESlateVisibility::Collapsed;

        // Clear option listeners when regenerating layout
        OptionListeners.Empty();

        // Query active index for components to fetch nested colors correctly
        int32 ActiveSizeIdx = 0;
        if (Booth)
        {
            switch (ActiveComponent)
            {
            case EFurnitureComponentType::Cabinet:
            case EFurnitureComponentType::Doors:
                ActiveSizeIdx = Booth->ActiveState.ActiveSizeIndex;
                break;
            case EFurnitureComponentType::Countertop:
                ActiveSizeIdx = Booth->ActiveState.CountertopSizeIndex;
                break;
            case EFurnitureComponentType::Closet:
                ActiveSizeIdx = Booth->ActiveState.ClosetSizeIndex;
                break;
            case EFurnitureComponentType::Sink:
                ActiveSizeIdx = Booth->ActiveState.SinkSizeIndex;
                break;
            case EFurnitureComponentType::Faucet:
                ActiveSizeIdx = Booth->ActiveState.FaucetSizeIndex;
                break;
            case EFurnitureComponentType::Mirror:
                ActiveSizeIdx = Booth->ActiveState.MirrorSizeIndex;
                break;
            default:
                break;
            }
        }

        // Dynamically resolve component options if not Cabinet
        FFurnitureComponentOptions ResolvedOpts;
        const FFurnitureComponentOptions* ComponentOpts = nullptr;
        if (ActiveComponent != EFurnitureComponentType::Cabinet)
        {
            if (Booth->GetResolvedComponentOptions(ActiveComponent, ResolvedOpts))
            {
                ComponentOpts = &ResolvedOpts;
            }
        }

        // ── SIZE SELECTORS ──
        if (Size_Container)
        {
            if (ActiveComponent == EFurnitureComponentType::Cabinet)
            {
                const FFurnitureCabinetOptions& CabinetOpts = ProductData.CabinetOptions;
                if (CabinetOpts.Sizes.Num() <= 1)
                {
                    Size_Container->SetVisibility(ESlateVisibility::Collapsed);
                    Size_Container->ClearChildren();
                }
                else
                {
                    Size_Container->SetVisibility(TargetVisibility);
                    Size_Container->ClearChildren();

                    if (bIsValidMesh)
                    {
                        for (int32 i = 0; i < CabinetOpts.Sizes.Num(); ++i)
                        {
                            UButton* NewBtn = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass());
                            if (NewBtn)
                            {
                                // Apply Pill-Style with Glassmorphism Hover/Active effects
                                FButtonStyle CustomStyle = NewBtn->GetStyle();
                                if (i == ActiveSizeIdx)
                                {
                                    CustomStyle.Normal.TintColor = FSlateColor(FLinearColor(0.2f, 0.6f, 1.0f, 0.3f));
                                    CustomStyle.Hovered.TintColor = FSlateColor(FLinearColor(0.2f, 0.6f, 1.0f, 0.45f));
                                    CustomStyle.Pressed.TintColor = FSlateColor(FLinearColor(0.2f, 0.6f, 1.0f, 0.6f));
                                }
                                else
                                {
                                    CustomStyle.Normal.TintColor = FSlateColor(FLinearColor(1.f, 1.f, 1.f, 0.05f));
                                    CustomStyle.Hovered.TintColor = FSlateColor(FLinearColor(1.f, 1.f, 1.f, 0.15f));
                                    CustomStyle.Pressed.TintColor = FSlateColor(FLinearColor(1.f, 1.f, 1.f, 0.25f));
                                }
                                NewBtn->SetStyle(CustomStyle);

                                UTextBlock* BtnText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
                                if (BtnText)
                                {
                                    BtnText->SetVisibility(ESlateVisibility::HitTestInvisible);
                                    FText SizeText;
                                    if (CabinetOpts.SizeNames.IsValidIndex(i) && !CabinetOpts.SizeNames[i].IsEmpty())
                                    {
                                        SizeText = CabinetOpts.SizeNames[i];
                                    }
                                    else
                                    {
                                        SizeText = FText::Format(FText::FromString(TEXT("Size {0}")), FText::AsNumber(i + 1));
                                    }
                                    BtnText->SetText(SizeText);
                                    NewBtn->AddChild(BtnText);
                                }

                                UFurnitureOptionListener* Listener = NewObject<UFurnitureOptionListener>(this);
                                Listener->Init(this, ActiveComponent, EOptionType::Size, i);
                                OptionListeners.Add(Listener);

                                NewBtn->OnClicked.AddDynamic(Listener, &UFurnitureOptionListener::OnButtonClicked);
                                NewBtn->OnHovered.AddDynamic(Listener, &UFurnitureOptionListener::OnButtonHovered);
                                NewBtn->OnUnhovered.AddDynamic(Listener, &UFurnitureOptionListener::OnButtonUnhovered);

                                Size_Container->AddChild(NewBtn);

                                if (NewBtn->Slot)
                                {
                                    if (UHorizontalBoxSlot* HSlot = Cast<UHorizontalBoxSlot>(NewBtn->Slot))
                                    {
                                        HSlot->SetPadding(FMargin(5.f, 0.f, 5.f, 0.f));
                                    }
                                    else if (UWrapBoxSlot* WSlot = Cast<UWrapBoxSlot>(NewBtn->Slot))
                                    {
                                        WSlot->SetPadding(FMargin(5.f, 0.f, 5.f, 0.f));
                                    }
                                }
                            }
                        }
                    }
                }
            }
            else // General components options (Models list)
            {
                if (!ComponentOpts || ComponentOpts->Models.Num() <= 1)
                {
                    Size_Container->SetVisibility(ESlateVisibility::Collapsed);
                    Size_Container->ClearChildren();
                }
                else
                {
                    Size_Container->SetVisibility(TargetVisibility);

                    float SavedScrollOffset = 0.f;
                    bool bHasSavedOffset = false;
                    for (int32 ChildIdx = 0; ChildIdx < Size_Container->GetChildrenCount(); ++ChildIdx)
                    {
                        UWidget* ChildWidget = Size_Container->GetChildAt(ChildIdx);
                        if (UScrollBox* FoundScrollBox = Cast<UScrollBox>(ChildWidget))
                        {
                            SavedScrollOffset = FoundScrollBox->GetScrollOffset();
                            bHasSavedOffset = true;
                            break;
                        }
                        else if (USizeBox* SizeBoxWrapper = Cast<USizeBox>(ChildWidget))
                        {
                            if (UScrollBox* InnerScrollBox = Cast<UScrollBox>(SizeBoxWrapper->GetContent()))
                            {
                                SavedScrollOffset = InnerScrollBox->GetScrollOffset();
                                bHasSavedOffset = true;
                                break;
                            }
                        }
                    }

                    Size_Container->ClearChildren();

                    if (bIsValidMesh)
                    {
                        UScrollBox* ScrollBox = WidgetTree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass());
                        if (ScrollBox)
                        {
                            ScrollBox->SetVisibility(ESlateVisibility::Visible);
                            ScrollBox->SetScrollBarVisibility(ESlateVisibility::Visible);
                            ScrollBox->SetAnimateWheelScrolling(true);
                            if (bHasSavedOffset)
                            {
                                ScrollBox->SetScrollOffset(SavedScrollOffset);
                            }

                            UUniformGridPanel* GridPanel = WidgetTree->ConstructWidget<UUniformGridPanel>(UUniformGridPanel::StaticClass());
                            if (GridPanel)
                            {
                                GridPanel->SetVisibility(ESlateVisibility::Visible);
                                GridPanel->SetMinDesiredSlotWidth(0.f);
                                GridPanel->SetMinDesiredSlotHeight(0.f);
                                GridPanel->SetSlotPadding(FMargin(GridSlotPadding));

                                for (int32 i = 0; i < ComponentOpts->Models.Num(); ++i)
                                {
                                    const FFurnitureModelOption& ModelOpt = ComponentOpts->Models[i];

                                    UButton* NewBtn = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass());
                                    if (NewBtn)
                                    {
                                        PRAGMA_DISABLE_DEPRECATION_WARNINGS
                                        NewBtn->IsFocusable = false;
                                        PRAGMA_ENABLE_DEPRECATION_WARNINGS

                                        FButtonStyle CustomStyle = NewBtn->GetStyle();
                                        CustomStyle.NormalPadding = ButtonPadding;
                                        CustomStyle.PressedPadding = ButtonPadding;
                                        NewBtn->SetStyle(CustomStyle);

                                        UScaleBox* ScaleBox = WidgetTree->ConstructWidget<UScaleBox>(UScaleBox::StaticClass());
                                        UImage* BtnImg = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass());
                                        if (ScaleBox && BtnImg)
                                        {
                                            ScaleBox->SetStretch(ImageStretch);
                                            ScaleBox->SetVisibility(ESlateVisibility::HitTestInvisible);
                                            if (!ModelOpt.Thumbnail.IsNull())
                                            {
                                                UTexture2D* LoadedTex = ModelOpt.Thumbnail.LoadSynchronous();
                                                if (LoadedTex)
                                                {
                                                    BtnImg->SetBrushFromTexture(LoadedTex, true);
                                                }
                                            }
                                            ScaleBox->AddChild(BtnImg);
                                            NewBtn->AddChild(ScaleBox);

                                            if (UButtonSlot* BtnSlot = Cast<UButtonSlot>(ScaleBox->Slot))
                                            {
                                                BtnSlot->SetPadding(ButtonPadding);
                                                BtnSlot->SetHorizontalAlignment(EHorizontalAlignment::HAlign_Fill);
                                                BtnSlot->SetVerticalAlignment(EVerticalAlignment::VAlign_Fill);
                                            }
                                        }

                                        UFurnitureOptionListener* Listener = NewObject<UFurnitureOptionListener>(this);
                                        Listener->Init(this, ActiveComponent, EOptionType::Size, i);
                                        OptionListeners.Add(Listener);

                                        NewBtn->OnClicked.AddDynamic(Listener, &UFurnitureOptionListener::OnButtonClicked);
                                        NewBtn->OnHovered.AddDynamic(Listener, &UFurnitureOptionListener::OnButtonHovered);
                                        NewBtn->OnUnhovered.AddDynamic(Listener, &UFurnitureOptionListener::OnButtonUnhovered);

                                        USizeBox* SizeBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
                                        if (SizeBox)
                                        {
                                            SizeBox->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
                                            SizeBox->SetWidthOverride(ButtonWidth);
                                            SizeBox->SetHeightOverride(ButtonHeight);
                                            SizeBox->AddChild(NewBtn);

                                            int32 RowIdx = i / 2;
                                            int32 ColIdx = i % 2;
                                            UUniformGridSlot* GridSlot = GridPanel->AddChildToUniformGrid(SizeBox, RowIdx, ColIdx);
                                            if (GridSlot)
                                            {
                                                GridSlot->SetHorizontalAlignment(EHorizontalAlignment::HAlign_Fill);
                                                GridSlot->SetVerticalAlignment(EVerticalAlignment::VAlign_Fill);
                                            }
                                        }
                                    }
                                }
                                ScrollBox->AddChild(GridPanel);
                            }

                            // Wrap ScrollBox inside a SizeBox with a maximum height limit of 255.0f
                            USizeBox* ScrollLimitBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
                            if (ScrollLimitBox)
                            {
                                ScrollLimitBox->SetMaxDesiredHeight(255.f);
                                ScrollLimitBox->AddChild(ScrollBox);
                                Size_Container->AddChild(ScrollLimitBox);
                            }
                            else
                            {
                                Size_Container->AddChild(ScrollBox);
                            }
                        }
                    }
                }
            }
        }

        // ── COLOR SELECTORS ──
        TArray<FFurnitureColorOption> ActiveColors;
        if (ActiveComponent == EFurnitureComponentType::Cabinet)
        {
            ActiveColors = ProductData.CabinetOptions.Colors;
        }
        else
        {
            if (ComponentOpts && ComponentOpts->Models.IsValidIndex(ActiveSizeIdx))
            {
                ActiveColors = ComponentOpts->Models[ActiveSizeIdx].Colors;
            }
        }

        if (Color_Container)
        {
            if (ActiveColors.Num() <= 1)
            {
                Color_Container->SetVisibility(ESlateVisibility::Collapsed);
                Color_Container->ClearChildren();
            }
            else
            {
                Color_Container->SetVisibility(TargetVisibility);
                Color_Container->ClearChildren();

                if (bIsValidMesh)
                {
                    for (int32 i = 0; i < ActiveColors.Num(); ++i)
                    {
                        const FFurnitureColorOption& ColorOpt = ActiveColors[i];
                        UButton* NewBtn = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass());
                        if (NewBtn)
                        {
                            UImage* BtnImg = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass());
                            if (BtnImg)
                            {
                                BtnImg->SetVisibility(ESlateVisibility::HitTestInvisible);
                                if (!ColorOpt.Thumbnail.IsNull())
                                {
                                    UTexture2D* LoadedTex = ColorOpt.Thumbnail.LoadSynchronous();
                                    if (LoadedTex)
                                    {
                                        BtnImg->SetBrushFromTexture(LoadedTex);
                                    }
                                }
                                NewBtn->AddChild(BtnImg);
                            }

                            UFurnitureOptionListener* Listener = NewObject<UFurnitureOptionListener>(this);
                            Listener->Init(this, ActiveComponent, EOptionType::Color, i);
                            OptionListeners.Add(Listener);

                            NewBtn->OnClicked.AddDynamic(Listener, &UFurnitureOptionListener::OnButtonClicked);
                            NewBtn->OnHovered.AddDynamic(Listener, &UFurnitureOptionListener::OnButtonHovered);
                            NewBtn->OnUnhovered.AddDynamic(Listener, &UFurnitureOptionListener::OnButtonUnhovered);

                            Color_Container->AddChild(NewBtn);
                        }
                    }
                }
            }
        }

        // ── Update Metadata Text Blocks ──
        if (Txt_ProductName_1 || Txt_SKU)
        {
            FText ProductName;
            FString SKU;
            FString URL;
            if (OwningPC.Get() && OwningPC->GetActiveComponentMetadata(ActiveComponent, ProductName, SKU, URL))
            {
                if (Txt_ProductName_1)
                {
                    Txt_ProductName_1->SetText(ProductName);
                }
                if (Txt_SKU)
                {
                    Txt_SKU->SetText(FText::FromString(SKU));
                }
            }
            else
            {
                if (Txt_ProductName_1)
                {
                    Txt_ProductName_1->SetText(FText::GetEmpty());
                }
                if (Txt_SKU)
                {
                    Txt_SKU->SetText(FText::GetEmpty());
                }
            }
        }

        // ── Warning Popup ──
        if (Txt_Warning)
        {
            UE_LOG(LogTemp, Warning, TEXT("[Widget] RefreshSelections: Txt_Warning is valid. bCountertopSizeFallbackActive = %s"), 
                Booth->bCountertopSizeFallbackActive ? TEXT("True") : TEXT("False"));
            if (Booth->bCountertopSizeFallbackActive)
            {
                Txt_Warning->SetText(FText::FromString(TEXT("Для этой модели нет встроенной столешницы соответствующего размера")));
                Txt_Warning->SetVisibility(ESlateVisibility::Visible);
            }
            else
            {
                Txt_Warning->SetVisibility(ESlateVisibility::Collapsed);
            }
        }
        else
        {
            UE_LOG(LogTemp, Warning, TEXT("[Widget] RefreshSelections: Txt_Warning is NULL (unbound)! Check your WBP_PreviewWindow layout."));
        }
    }
}

void UConfiguratorMainWidget::OnViewmodeClicked()
{
    AMaxiMallPreviewController* PC = OwningPC.Get();
    AShowroomBooth* Booth = TargetBooth.Get();
    if (PC && Booth)
    {
        PC->OpenFurniturePreview(Booth, ActiveComponent);
    }
}

void UConfiguratorMainWidget::OnCloseUIClicked()
{
    AMaxiMallPreviewController* PC = OwningPC.Get();
    AShowroomBooth* Booth = TargetBooth.Get();
    if (PC && Booth)
    {
        PC->ToggleConfiguratorUI(Booth, ActiveComponent, false);
    }
}

void UConfiguratorMainWidget::OnURLButtonClicked()
{
    AShowroomBooth* Booth = TargetBooth.Get();
    if (!Booth || !OwningPC)
    {
        return;
    }

    FText ProductName;
    FString SKU;
    FString URL;
    if (OwningPC->GetActiveComponentMetadata(ActiveComponent, ProductName, SKU, URL))
    {
        if (GEngine)
        {
            GEngine->AddOnScreenDebugMessage(-1, 5.0f, FColor::Green, FString::Printf(TEXT("Product URL: %s"), *URL));
        }
        UE_LOG(LogTemp, Log, TEXT("Product URL: %s"), *URL);
    }
}

void UConfiguratorMainWidget::HandleOptionSelected(EFurnitureComponentType Component, EOptionType Type, int32 OptionIndex)
{
    AShowroomBooth* Booth = TargetBooth.Get();
    if (!Booth || !OwningPC) return;

    int32 SizeIndex = Booth->ActiveState.ActiveSizeIndex;
    int32 ColorIndex = Booth->ActiveState.ActiveColorIndex;

    if (Component == EFurnitureComponentType::Countertop)
    {
        SizeIndex = Booth->ActiveState.CountertopSizeIndex;
        ColorIndex = Booth->ActiveState.ActiveCountertopColorIndex;
    }
    else if (Component == EFurnitureComponentType::Closet)
    {
        SizeIndex = Booth->ActiveState.ClosetSizeIndex;
        ColorIndex = Booth->ActiveState.ClosetColorIndex;
    }
    else if (Component == EFurnitureComponentType::Sink)
    {
        SizeIndex = Booth->ActiveState.SinkSizeIndex;
        ColorIndex = Booth->ActiveState.SinkColorIndex;
    }
    else if (Component == EFurnitureComponentType::Faucet)
    {
        SizeIndex = Booth->ActiveState.FaucetSizeIndex;
        ColorIndex = Booth->ActiveState.FaucetColorIndex;
    }
    else if (Component == EFurnitureComponentType::Mirror)
    {
        SizeIndex = Booth->ActiveState.MirrorSizeIndex;
        ColorIndex = Booth->ActiveState.MirrorColorIndex;
    }

    if (Type == EOptionType::Size)
    {
        SizeIndex = OptionIndex;
        ColorIndex = 0; // Reset active color to 0 when changing the model size
    }
    else
    {
        ColorIndex = OptionIndex;
    }

    OwningPC->RequestBoothComponentSelection(Booth, Component, SizeIndex, ColorIndex);
}

void UConfiguratorMainWidget::HandleOptionHovered(EFurnitureComponentType Component, EOptionType Type, int32 OptionIndex)
{
    // No-op
}

void UConfiguratorMainWidget::HandleOptionUnhovered(EFurnitureComponentType Component, EOptionType Type, int32 OptionIndex)
{
    // No-op
}

bool UConfiguratorMainWidget::IsComponentMeshValid(AShowroomBooth* Booth, EFurnitureComponentType Component) const
{
    if (!Booth) return false;
    switch (Component)
    {
    case EFurnitureComponentType::Cabinet:
        return Booth->MainCabinet && Booth->MainCabinet->GetStaticMesh() != nullptr;
    case EFurnitureComponentType::Closet:
        return Booth->ClosetMesh && Booth->ClosetMesh->GetStaticMesh() != nullptr;
    case EFurnitureComponentType::Doors:
        return Booth->DoorMeshSlot0 && Booth->DoorMeshSlot0->GetStaticMesh() != nullptr;
    case EFurnitureComponentType::Countertop:
        return Booth->CountertopMesh && Booth->CountertopMesh->GetStaticMesh() != nullptr;
    case EFurnitureComponentType::Sink:
        return Booth->GetActiveCountertopType() == ECountertopType::SurfaceMounted && Booth->SinkMesh && Booth->SinkMesh->GetStaticMesh() != nullptr;
    case EFurnitureComponentType::Faucet:
        return Booth->FaucetMesh && Booth->FaucetMesh->GetStaticMesh() != nullptr;
    case EFurnitureComponentType::Mirror:
        return Booth->MirrorMesh && Booth->MirrorMesh->GetStaticMesh() != nullptr;
    default:
        break;
    }
    return false;
}
