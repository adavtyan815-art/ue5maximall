// Copyright MaxiMall Project. All Rights Reserved.

#include "FurnitureConfigurator/UI/ConfiguratorMainWidget.h"
#include "Components/TextBlock.h"
#include "Components/Button.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Image.h"
#include "Components/PanelWidget.h"
#include "Engine/Texture2D.h"
#include "FurnitureConfigurator/ShowroomBooth.h"
#include "awsTutorial_PlayerController.h"
#include "ARExport/UI/ARExportModalWidget.h"
#include "Engine/Engine.h"
#include "Components/ScrollBox.h"
#include "Components/UniformGridPanel.h"
#include "Components/UniformGridSlot.h"
#include "Components/SizeBox.h"
#include "Components/ScaleBox.h"
#include "Components/ButtonSlot.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/WrapBoxSlot.h"
#include "ColorCatalog/ColorCatalogWidget.h"

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
    if (Btn_ColorCatalog)
    {
        Btn_ColorCatalog->OnClicked.RemoveAll(this);
        Btn_ColorCatalog->OnClicked.AddDynamic(this, &UConfiguratorMainWidget::OnColorCatalogClicked);
    }
    if (Btn_CinematicTour)
    {
        Btn_CinematicTour->OnClicked.RemoveAll(this);
        Btn_CinematicTour->OnClicked.AddDynamic(this, &UConfiguratorMainWidget::OnCinematicTourButtonClicked);
    }
    if (BtnCinematicTour)
    {
        BtnCinematicTour->OnClicked.RemoveAll(this);
        BtnCinematicTour->OnClicked.AddDynamic(this, &UConfiguratorMainWidget::OnCinematicTourButtonClicked);
    }
    if (Btn_ARSelected)
    {
        Btn_ARSelected->OnClicked.RemoveAll(this);
        Btn_ARSelected->OnClicked.AddDynamic(this, &UConfiguratorMainWidget::OnARSelectedClicked);
    }
    if (Btn_ARFullScene)
    {
        Btn_ARFullScene->OnClicked.RemoveAll(this);
        Btn_ARFullScene->OnClicked.AddDynamic(this, &UConfiguratorMainWidget::OnARFullSceneClicked);
    }
}

void UConfiguratorMainWidget::SetupWidget(AAwsTutorial_PlayerController* InPC, AShowroomBooth* InBooth, EFurnitureComponentType InComponent)
{
    OwningPC = InPC;
    TargetBooth = InBooth;
    ActiveComponent = InComponent;

    RefreshSelections();
}

void UConfiguratorMainWidget::RefreshSelections()
{
    UpdateSelectedObjectNameUI();

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

        // Gate Color Catalog button visibility based on DataTable configuration for this component
        bool bIsColorCatalogAllowed = bIsValidMesh && Booth->IsColorCatalogAllowedForComponent(ActiveComponent);
        if (Btn_ColorCatalog)
        {
            Btn_ColorCatalog->SetVisibility(bIsColorCatalogAllowed ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
        }

        // If Color Catalog is currently open and active, but the newly selected component does not support it, close it cleanly!
        if (::IsValid(ActiveColorCatalogInstance) && ActiveColorCatalogInstance->IsInViewport())
        {
            if (!bIsColorCatalogAllowed)
            {
                ActiveColorCatalogInstance->CloseColorCatalog();
                ActiveColorCatalogInstance = nullptr;
            }
        }

        // Update Cinematic Tour button styling
        UpdateCinematicTourButtonStyle();

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

        // в”Ђв”Ђ SIZE SELECTORS в”Ђв”Ђ
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
                                    CustomStyle.Normal.TintColor = ActiveSizeButtonNormalColor;
                                    CustomStyle.Hovered.TintColor = ActiveSizeButtonHoveredColor;
                                    CustomStyle.Pressed.TintColor = ActiveSizeButtonPressedColor;
                                }
                                else
                                {
                                    CustomStyle.Normal.TintColor = SizeButtonNormalColor;
                                    CustomStyle.Hovered.TintColor = SizeButtonHoveredColor;
                                    CustomStyle.Pressed.TintColor = SizeButtonPressedColor;
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

                                    if (i == ActiveSizeIdx)
                                    {
                                        BtnText->SetColorAndOpacity(ActiveSizeTextColor);
                                    }
                                    else
                                    {
                                        BtnText->SetColorAndOpacity(SizeTextColor);
                                    }

                                    if (SizeTextFont.HasValidFont())
                                    {
                                        BtnText->SetFont(SizeTextFont);
                                    }

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
                                GridPanel->SetSlotPadding(FMargin(SizeGridSlotPadding));

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
                                        CustomStyle.NormalPadding = SizeButtonPadding;
                                        CustomStyle.PressedPadding = SizeButtonPadding;
                                        if (i == ActiveSizeIdx)
                                        {
                                            CustomStyle.Normal.TintColor = ActiveSizeButtonNormalColor;
                                            CustomStyle.Hovered.TintColor = ActiveSizeButtonHoveredColor;
                                            CustomStyle.Pressed.TintColor = ActiveSizeButtonPressedColor;
                                        }
                                        else
                                        {
                                            CustomStyle.Normal.TintColor = SizeButtonNormalColor;
                                            CustomStyle.Hovered.TintColor = SizeButtonHoveredColor;
                                            CustomStyle.Pressed.TintColor = SizeButtonPressedColor;
                                        }
                                        NewBtn->SetStyle(CustomStyle);

                                        UScaleBox* ScaleBox = WidgetTree->ConstructWidget<UScaleBox>(UScaleBox::StaticClass());
                                        UImage* BtnImg = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass());
                                        if (ScaleBox && BtnImg)
                                        {
                                            ScaleBox->SetStretch(SizeImageStretch);
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
                                                BtnSlot->SetPadding(SizeButtonPadding);
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
                                            SizeBox->SetWidthOverride(SizeButtonWidth);
                                            SizeBox->SetHeightOverride(SizeButtonHeight);
                                            SizeBox->AddChild(NewBtn);

                                            int32 Columns = SizeColumns > 0 ? SizeColumns : 2;
                                            int32 RowIdx = i / Columns;
                                            int32 ColIdx = i % Columns;
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

                            // Wrap ScrollBox inside a SizeBox with height limit controlled by SizeContainerHeight
                            USizeBox* ScrollLimitBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
                            if (ScrollLimitBox)
                            {
                                if (SizeContainerHeight > 0.f)
                                {
                                    ScrollLimitBox->SetMaxDesiredHeight(SizeContainerHeight);
                                }
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

        // в”Ђв”Ђ COLOR SELECTORS в”Ђв”Ђ
        TArray<FFurnitureColorOption> ActiveColors;
        if (ActiveComponent == EFurnitureComponentType::Cabinet)
        {
            for (const FFurnitureColorOption& ColorOpt : ProductData.CabinetOptions.Colors)
            {
                if (ColorOpt.SizeIndices.Num() == 0 || ColorOpt.SizeIndices.Contains(ActiveSizeIdx))
                {
                    ActiveColors.Add(ColorOpt);
                }
            }
        }
        else
        {
            if (ComponentOpts && ComponentOpts->Models.IsValidIndex(ActiveSizeIdx))
            {
                ActiveColors = ComponentOpts->Models[ActiveSizeIdx].Colors;
            }
        }

        // Query active color index for this component
        int32 ActiveColorIdx = 0;
        if (Booth)
        {
            switch (ActiveComponent)
            {
            case EFurnitureComponentType::Cabinet:
            case EFurnitureComponentType::Doors:
                ActiveColorIdx = Booth->ActiveState.ActiveColorIndex;
                break;
            case EFurnitureComponentType::Countertop:
                ActiveColorIdx = Booth->ActiveState.ActiveCountertopColorIndex;
                break;
            case EFurnitureComponentType::Closet:
                ActiveColorIdx = Booth->ActiveState.ClosetColorIndex;
                break;
            case EFurnitureComponentType::Sink:
                ActiveColorIdx = Booth->ActiveState.SinkColorIndex;
                break;
            case EFurnitureComponentType::Faucet:
                ActiveColorIdx = Booth->ActiveState.FaucetColorIndex;
                break;
            case EFurnitureComponentType::Mirror:
                ActiveColorIdx = Booth->ActiveState.MirrorColorIndex;
                break;
            default:
                break;
            }
        }

        if (!ActiveColors.IsValidIndex(ActiveColorIdx))
        {
            ActiveColorIdx = 0;
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

                float SavedColorScrollOffset = 0.f;
                bool bHasColorSavedOffset = false;
                for (int32 ChildIdx = 0; ChildIdx < Color_Container->GetChildrenCount(); ++ChildIdx)
                {
                    UWidget* ChildWidget = Color_Container->GetChildAt(ChildIdx);
                    if (UScrollBox* FoundScrollBox = Cast<UScrollBox>(ChildWidget))
                    {
                        SavedColorScrollOffset = FoundScrollBox->GetScrollOffset();
                        bHasColorSavedOffset = true;
                        break;
                    }
                    else if (USizeBox* SizeBoxWrapper = Cast<USizeBox>(ChildWidget))
                    {
                        if (UScrollBox* InnerScrollBox = Cast<UScrollBox>(SizeBoxWrapper->GetContent()))
                        {
                            SavedColorScrollOffset = InnerScrollBox->GetScrollOffset();
                            bHasColorSavedOffset = true;
                            break;
                        }
                    }
                }

                Color_Container->ClearChildren();

                if (bIsValidMesh)
                {
                    UScrollBox* ColorScrollBox = WidgetTree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass());
                    if (ColorScrollBox)
                    {
                        ColorScrollBox->SetVisibility(ESlateVisibility::Visible);
                        ColorScrollBox->SetScrollBarVisibility(ESlateVisibility::Visible);
                        ColorScrollBox->SetAnimateWheelScrolling(true);
                        if (bHasColorSavedOffset)
                        {
                            ColorScrollBox->SetScrollOffset(SavedColorScrollOffset);
                        }

                        UUniformGridPanel* ColorGridPanel = WidgetTree->ConstructWidget<UUniformGridPanel>(UUniformGridPanel::StaticClass());
                        if (ColorGridPanel)
                        {
                            ColorGridPanel->SetVisibility(ESlateVisibility::Visible);
                            ColorGridPanel->SetMinDesiredSlotWidth(0.f);
                            ColorGridPanel->SetMinDesiredSlotHeight(0.f);
                            ColorGridPanel->SetSlotPadding(FMargin(ColorGridSlotPadding));

                            for (int32 i = 0; i < ActiveColors.Num(); ++i)
                            {
                                const FFurnitureColorOption& ColorOpt = ActiveColors[i];
                                UButton* NewBtn = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass());
                                if (NewBtn)
                                {
                                    PRAGMA_DISABLE_DEPRECATION_WARNINGS
                                    NewBtn->IsFocusable = false;
                                    PRAGMA_ENABLE_DEPRECATION_WARNINGS

                                    // Apply Pill-Style with Glassmorphism Hover/Active effects
                                    FButtonStyle CustomStyle = NewBtn->GetStyle();
                                    CustomStyle.NormalPadding = ColorButtonPadding;
                                    CustomStyle.PressedPadding = ColorButtonPadding;
                                    if (i == ActiveColorIdx)
                                    {
                                        CustomStyle.Normal.TintColor = ActiveColorButtonNormalColor;
                                        CustomStyle.Hovered.TintColor = ActiveColorButtonHoveredColor;
                                        CustomStyle.Pressed.TintColor = ActiveColorButtonPressedColor;
                                    }
                                    else
                                    {
                                        CustomStyle.Normal.TintColor = ColorButtonNormalColor;
                                        CustomStyle.Hovered.TintColor = ColorButtonHoveredColor;
                                        CustomStyle.Pressed.TintColor = ColorButtonPressedColor;
                                    }
                                    NewBtn->SetStyle(CustomStyle);

                                    UScaleBox* ScaleBox = WidgetTree->ConstructWidget<UScaleBox>(UScaleBox::StaticClass());
                                    UImage* BtnImg = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass());
                                    if (ScaleBox && BtnImg)
                                    {
                                        ScaleBox->SetStretch(ColorImageStretch);
                                        ScaleBox->SetVisibility(ESlateVisibility::HitTestInvisible);
                                        if (!ColorOpt.Thumbnail.IsNull())
                                        {
                                            UTexture2D* LoadedTex = ColorOpt.Thumbnail.LoadSynchronous();
                                            if (LoadedTex)
                                            {
                                                BtnImg->SetBrushFromTexture(LoadedTex, true);
                                            }
                                        }
                                        ScaleBox->AddChild(BtnImg);
                                        NewBtn->AddChild(ScaleBox);

                                        if (UButtonSlot* BtnSlot = Cast<UButtonSlot>(ScaleBox->Slot))
                                        {
                                            BtnSlot->SetPadding(ColorButtonPadding);
                                            BtnSlot->SetHorizontalAlignment(EHorizontalAlignment::HAlign_Fill);
                                            BtnSlot->SetVerticalAlignment(EVerticalAlignment::VAlign_Fill);
                                        }
                                    }

                                    UFurnitureOptionListener* Listener = NewObject<UFurnitureOptionListener>(this);
                                    Listener->Init(this, ActiveComponent, EOptionType::Color, i);
                                    OptionListeners.Add(Listener);

                                    NewBtn->OnClicked.AddDynamic(Listener, &UFurnitureOptionListener::OnButtonClicked);
                                    NewBtn->OnHovered.AddDynamic(Listener, &UFurnitureOptionListener::OnButtonHovered);
                                    NewBtn->OnUnhovered.AddDynamic(Listener, &UFurnitureOptionListener::OnButtonUnhovered);

                                    USizeBox* SizeBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
                                    if (SizeBox)
                                    {
                                        SizeBox->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
                                        SizeBox->SetWidthOverride(ColorButtonWidth);
                                        SizeBox->SetHeightOverride(ColorButtonHeight);
                                        SizeBox->AddChild(NewBtn);

                                        int32 Columns = ColorColumns > 0 ? ColorColumns : 2;
                                        int32 RowIdx = i / Columns;
                                        int32 ColIdx = i % Columns;
                                        UUniformGridSlot* GridSlot = ColorGridPanel->AddChildToUniformGrid(SizeBox, RowIdx, ColIdx);
                                        if (GridSlot)
                                        {
                                            GridSlot->SetHorizontalAlignment(EHorizontalAlignment::HAlign_Fill);
                                            GridSlot->SetVerticalAlignment(EVerticalAlignment::VAlign_Fill);
                                        }
                                    }
                                }
                            }
                            ColorScrollBox->AddChild(ColorGridPanel);
                        }

                        // Wrap ScrollBox inside a SizeBox with height limit controlled by ColorContainerHeight
                        USizeBox* ColorScrollLimitBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
                        if (ColorScrollLimitBox)
                        {
                            if (ColorContainerHeight > 0.f)
                            {
                                ColorScrollLimitBox->SetMaxDesiredHeight(ColorContainerHeight);
                            }
                            ColorScrollLimitBox->AddChild(ColorScrollBox);
                            Color_Container->AddChild(ColorScrollLimitBox);
                        }
                        else
                        {
                            Color_Container->AddChild(ColorScrollBox);
                        }
                    }
                }
            }
        }

        // в”Ђв”Ђ Update Metadata Text Blocks в”Ђв”Ђ
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

        // в”Ђв”Ђ Warning Popup в”Ђв”Ђ
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
    AAwsTutorial_PlayerController* PC = OwningPC.Get();
    AShowroomBooth* Booth = TargetBooth.Get();
    if (PC && Booth)
    {
        PC->OpenFurniturePreview(Booth, ActiveComponent);
    }
}

void UConfiguratorMainWidget::OnCloseUIClicked()
{
    if (::IsValid(ActiveColorCatalogInstance) && ActiveColorCatalogInstance->IsInViewport())
    {
        ActiveColorCatalogInstance->CloseColorCatalog();
        ActiveColorCatalogInstance = nullptr;
    }

    AAwsTutorial_PlayerController* PC = OwningPC.Get();
    AShowroomBooth* Booth = TargetBooth.Get();
    if (PC && Booth)
    {
        PC->ToggleConfiguratorUI(Booth, ActiveComponent, false);
    }
}

void UConfiguratorMainWidget::OnColorCatalogClicked()
{
    AShowroomBooth* Booth = TargetBooth.Get();
    if (!Booth || !ColorCatalogWidgetClass || !Booth->IsColorCatalogAllowedForComponent(ActiveComponent))
    {
        return;
    }

    // Get the corresponding UPrimitiveComponent based on the ActiveComponent
    UPrimitiveComponent* TargetMesh = nullptr;
    switch (ActiveComponent)
    {
    case EFurnitureComponentType::Cabinet: TargetMesh = Booth->MainCabinet; break;
    case EFurnitureComponentType::Closet: TargetMesh = Booth->ClosetMesh; break;
    case EFurnitureComponentType::Doors: TargetMesh = Booth->DoorMeshSlot0; break;
    case EFurnitureComponentType::Countertop: TargetMesh = Booth->CountertopMesh; break;
    case EFurnitureComponentType::Sink: TargetMesh = Booth->SinkMesh; break;
    case EFurnitureComponentType::Faucet: TargetMesh = Booth->FaucetMesh; break;
    case EFurnitureComponentType::Mirror: TargetMesh = Booth->MirrorMesh; break;
    default: break;
    }

    // Open the catalog widget via the static C++ helper!
    UColorCatalogWidget* CatalogWidget = UColorCatalogWidget::OpenColorCatalogForWidget(this, ColorCatalogWidgetClass);
    if (CatalogWidget)
    {
        ActiveColorCatalogInstance = CatalogWidget;
        CatalogWidget->OnColorSelected.AddUniqueDynamic(this, &UConfiguratorMainWidget::HandleColorSelected);
    }
}

void UConfiguratorMainWidget::HandleColorSelected(FLinearColor SelectedColor, UMaterialInterface* OverrideMaterial)
{
    AShowroomBooth* Booth = TargetBooth.Get();
    if (!Booth || !Booth->IsColorCatalogAllowedForComponent(ActiveComponent))
    {
        return;
    }

    AAwsTutorial_PlayerController* PreviewPC = Cast<AAwsTutorial_PlayerController>(GetOwningPlayer());
    if (PreviewPC)
    {
        PreviewPC->RequestBoothCustomColorChange(Booth, ActiveComponent, SelectedColor, OverrideMaterial);
    }
    else
    {
        Booth->RequestCustomColorChange(ActiveComponent, SelectedColor, OverrideMaterial);
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

void UConfiguratorMainWidget::OnCinematicTourButtonClicked()
{
    if (OwningPC)
    {
        OwningPC->ToggleCinematicTour(TargetBooth.Get());
        UpdateCinematicTourButtonStyle();
    }
}

void UConfiguratorMainWidget::UpdateCinematicTourButtonStyle()
{
    bool bActive = OwningPC && OwningPC->bIsCinematicTourActive;

    FLinearColor ActiveColor(0.95f, 0.6f, 0.1f, 1.0f); // Warm gold/amber accent for tour
    FLinearColor InactiveColor(0.1f, 0.14f, 0.2f, 1.0f);

    if (Btn_CinematicTour)
    {
        Btn_CinematicTour->SetBackgroundColor(bActive ? ActiveColor : InactiveColor);
    }
    if (BtnCinematicTour)
    {
        BtnCinematicTour->SetBackgroundColor(bActive ? ActiveColor : InactiveColor);
    }
}

namespace
{
    /** Clean Russian logical name for the selected component category. */
    FText GetComponentDisplayNameRu(EFurnitureComponentType Type)
    {
        switch (Type)
        {
            case EFurnitureComponentType::Closet:     return FText::FromString(TEXT("Шкаф"));
            case EFurnitureComponentType::Cabinet:    return FText::FromString(TEXT("Тумба"));
            case EFurnitureComponentType::Doors:      return FText::FromString(TEXT("Тумба"));
            case EFurnitureComponentType::Countertop: return FText::FromString(TEXT("Столешница"));
            case EFurnitureComponentType::Faucet:     return FText::FromString(TEXT("Смеситель"));
            case EFurnitureComponentType::Sink:       return FText::FromString(TEXT("Раковина"));
            case EFurnitureComponentType::Mirror:     return FText::FromString(TEXT("Зеркало"));
            default:                                  return FText::GetEmpty();
        }
    }

    /** Booth components that make up the selected category (same grouping the recolor flow uses). */
    void GetComponentsForType(AShowroomBooth* Booth, EFurnitureComponentType Type, TArray<UStaticMeshComponent*>& OutComponents)
    {
        switch (Type)
        {
        case EFurnitureComponentType::Cabinet:
        case EFurnitureComponentType::Doors:
            OutComponents.Add(Booth->MainCabinet);
            OutComponents.Add(Booth->DoorMeshSlot0);
            OutComponents.Add(Booth->DoorMeshSlot1);
            break;
        case EFurnitureComponentType::Closet:
            OutComponents.Add(Booth->ClosetMesh);
            OutComponents.Add(Booth->ClosetDoorMeshSlot0);
            OutComponents.Add(Booth->ClosetDoorMeshSlot1);
            break;
        case EFurnitureComponentType::Countertop: OutComponents.Add(Booth->CountertopMesh); break;
        case EFurnitureComponentType::Sink:       OutComponents.Add(Booth->SinkMesh); break;
        case EFurnitureComponentType::Faucet:     OutComponents.Add(Booth->FaucetMesh); break;
        case EFurnitureComponentType::Mirror:     OutComponents.Add(Booth->MirrorMesh); break;
        default: break;
        }
        OutComponents.Remove(nullptr);
    }
}

UARExportModalWidget* UConfiguratorMainWidget::OpenARExportModal()
{
    if (!OwningPC)
    {
        return nullptr;
    }

    // The designed WBP must always be used: property first, then the known asset,
    // and only as a last resort the bare C++ class (undesigned).
    UClass* ModalClass = ARExportModalClass ? ARExportModalClass.Get() : nullptr;
    if (!ModalClass)
    {
        ModalClass = LoadClass<UUserWidget>(nullptr, TEXT("/Game/FurnitureConfigurator/UI/WBP_ARExportModal.WBP_ARExportModal_C"));
    }
    if (!ModalClass)
    {
        ModalClass = UARExportModalWidget::StaticClass();
    }

    UARExportModalWidget* Modal = CreateWidget<UARExportModalWidget>(OwningPC, ModalClass);
    if (Modal)
    {
        Modal->AddToViewport(100);
    }
    return Modal;
}

void UConfiguratorMainWidget::UpdateSelectedObjectNameUI()
{
    const FText DisplayName = GetComponentDisplayNameRu(ActiveComponent);
    if (Txt_SelectedMeshName)
    {
        Txt_SelectedMeshName->SetText(DisplayName);
    }
    if (Txt_ARSelectedMeshName)
    {
        Txt_ARSelectedMeshName->SetText(DisplayName);
    }
}

void UConfiguratorMainWidget::OnARSelectedClicked()
{
    AShowroomBooth* Booth = TargetBooth.Get();
    if (!Booth || !OwningPC)
    {
        return;
    }

    TArray<UStaticMeshComponent*> SelectedComponents;
    GetComponentsForType(Booth, ActiveComponent, SelectedComponents);
    if (SelectedComponents.Num() == 0)
    {
        return;
    }

    if (UARExportModalWidget* Modal = OpenARExportModal())
    {
        Modal->StartExportForComponents(Booth, SelectedComponents);
    }
}

void UConfiguratorMainWidget::OnARFullSceneClicked()
{
    AShowroomBooth* Booth = TargetBooth.Get();
    if (!Booth || !OwningPC)
    {
        return;
    }

    if (UARExportModalWidget* Modal = OpenARExportModal())
    {
        Modal->StartExportForActor(Booth);
    }
}


