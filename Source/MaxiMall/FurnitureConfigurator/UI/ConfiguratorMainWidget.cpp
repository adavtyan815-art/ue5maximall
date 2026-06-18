// Copyright MaxiMall Project. All Rights Reserved.

#include "FurnitureConfigurator/UI/ConfiguratorMainWidget.h"
#include "Components/TextBlock.h"
#include "Components/Button.h"
#include "Components/ComboBoxString.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Image.h"
#include "Components/PanelWidget.h"
#include "Engine/Texture2D.h"
#include "FurnitureConfigurator/ShowroomBooth.h"
#include "FurnitureConfigurator/Preview/MaxiMallPreviewController.h"

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

    // Bind strict buttons (clear existing bindings first to prevent double-binding ensures on reconstruction)
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

    // Bind active component combo selectors
    if (Combo_Size)
    {
        Combo_Size->OnSelectionChanged.RemoveAll(this);
        Combo_Size->OnSelectionChanged.AddDynamic(this, &UConfiguratorMainWidget::OnSizeSelected);
    }
    if (Combo_Color)
    {
        Combo_Color->OnSelectionChanged.RemoveAll(this);
        Combo_Color->OnSelectionChanged.AddDynamic(this, &UConfiguratorMainWidget::OnColorSelected);
    }

    // Bind specific component combo selectors (optional layout table)
    if (Combo_Cabinet_Size)
    {
        Combo_Cabinet_Size->OnSelectionChanged.RemoveAll(this);
        Combo_Cabinet_Size->OnSelectionChanged.AddDynamic(this, &UConfiguratorMainWidget::OnCabinetSizeChanged);
    }
    if (Combo_Cabinet_Color)
    {
        Combo_Cabinet_Color->OnSelectionChanged.RemoveAll(this);
        Combo_Cabinet_Color->OnSelectionChanged.AddDynamic(this, &UConfiguratorMainWidget::OnCabinetColorChanged);
    }

    if (Combo_Closet_Size)
    {
        Combo_Closet_Size->OnSelectionChanged.RemoveAll(this);
        Combo_Closet_Size->OnSelectionChanged.AddDynamic(this, &UConfiguratorMainWidget::OnClosetSizeChanged);
    }
    if (Combo_Closet_Color)
    {
        Combo_Closet_Color->OnSelectionChanged.RemoveAll(this);
        Combo_Closet_Color->OnSelectionChanged.AddDynamic(this, &UConfiguratorMainWidget::OnClosetColorChanged);
    }

    if (Combo_Doors_Size)
    {
        Combo_Doors_Size->OnSelectionChanged.RemoveAll(this);
        Combo_Doors_Size->OnSelectionChanged.AddDynamic(this, &UConfiguratorMainWidget::OnDoorsSizeChanged);
    }
    if (Combo_Doors_Color)
    {
        Combo_Doors_Color->OnSelectionChanged.RemoveAll(this);
        Combo_Doors_Color->OnSelectionChanged.AddDynamic(this, &UConfiguratorMainWidget::OnDoorsColorChanged);
    }

    if (Combo_Countertop_Size)
    {
        Combo_Countertop_Size->OnSelectionChanged.RemoveAll(this);
        Combo_Countertop_Size->OnSelectionChanged.AddDynamic(this, &UConfiguratorMainWidget::OnCountertopSizeChanged);
    }
    if (Combo_Countertop_Color)
    {
        Combo_Countertop_Color->OnSelectionChanged.RemoveAll(this);
        Combo_Countertop_Color->OnSelectionChanged.AddDynamic(this, &UConfiguratorMainWidget::OnCountertopColorChanged);
    }

    if (Combo_Sink_Size)
    {
        Combo_Sink_Size->OnSelectionChanged.RemoveAll(this);
        Combo_Sink_Size->OnSelectionChanged.AddDynamic(this, &UConfiguratorMainWidget::OnSinkSizeChanged);
    }
    if (Combo_Sink_Color)
    {
        Combo_Sink_Color->OnSelectionChanged.RemoveAll(this);
        Combo_Sink_Color->OnSelectionChanged.AddDynamic(this, &UConfiguratorMainWidget::OnSinkColorChanged);
    }

    if (Combo_Faucet_Size)
    {
        Combo_Faucet_Size->OnSelectionChanged.RemoveAll(this);
        Combo_Faucet_Size->OnSelectionChanged.AddDynamic(this, &UConfiguratorMainWidget::OnFaucetSizeChanged);
    }
    if (Combo_Faucet_Color)
    {
        Combo_Faucet_Color->OnSelectionChanged.RemoveAll(this);
        Combo_Faucet_Color->OnSelectionChanged.AddDynamic(this, &UConfiguratorMainWidget::OnFaucetColorChanged);
    }

    if (Combo_Mirror_Size)
    {
        Combo_Mirror_Size->OnSelectionChanged.RemoveAll(this);
        Combo_Mirror_Size->OnSelectionChanged.AddDynamic(this, &UConfiguratorMainWidget::OnMirrorSizeChanged);
    }
    if (Combo_Mirror_Color)
    {
        Combo_Mirror_Color->OnSelectionChanged.RemoveAll(this);
        Combo_Mirror_Color->OnSelectionChanged.AddDynamic(this, &UConfiguratorMainWidget::OnMirrorColorChanged);
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

    // Set product details title (Unnecessary titles / clutter collapsed)
    if (Txt_ProductName)
    {
        Txt_ProductName->SetVisibility(ESlateVisibility::Collapsed);
    }

    FFurnitureProductRow ProductData;
    if (Booth->GetActiveProductData(ProductData))
    {
        // ── Setup active right-clicked component selectors ────────────────────
        FFurnitureComponentOptions ActiveOpts;
        if (ActiveComponent == EFurnitureComponentType::Countertop)
        {
            ActiveOpts = ProductData.CountertopOptions;
        }
        else if (ActiveComponent == EFurnitureComponentType::Closet)
        {
            ActiveOpts = ProductData.ClosetOptions;
        }
        else if (ActiveComponent == EFurnitureComponentType::Sink)
        {
            ActiveOpts = ProductData.SinkOptions;
        }
        else if (ActiveComponent == EFurnitureComponentType::Faucet)
        {
            ActiveOpts = ProductData.FaucetOptions;
        }
        else if (ActiveComponent == EFurnitureComponentType::Mirror)
        {
            ActiveOpts = ProductData.MirrorOptions;
        }
        else
        {
            ActiveOpts = ProductData.CabinetOptions;
        }

        // Gate optional component visibility — if the component's mesh is missing/null, collapse the UI selectors
        bool bIsValidMesh = IsComponentMeshValid(Booth, ActiveComponent);
        ESlateVisibility TargetVisibility = bIsValidMesh ? ESlateVisibility::Visible : ESlateVisibility::Collapsed;

        // Clear option listeners when regenerating layout
        OptionListeners.Empty();

        // ── SIZE SELECTORS ──
        if (Size_Container)
        {
            if (ActiveComponent == EFurnitureComponentType::Countertop)
            {
                Size_Container->SetVisibility(ESlateVisibility::Collapsed);
            }
            else
            {
                Size_Container->SetVisibility(TargetVisibility);
                Size_Container->ClearChildren();
                if (Combo_Size)
                {
                    Combo_Size->SetVisibility(ESlateVisibility::Collapsed);
                }

                if (bIsValidMesh)
                {
                    for (int32 i = 0; i < ActiveOpts.Sizes.Num(); ++i)
                    {
                        UButton* NewBtn = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass());
                        if (NewBtn)
                        {
                            UTextBlock* BtnText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
                            if (BtnText)
                            {
                                BtnText->SetText(FText::Format(FText::FromString(TEXT("Size {0}")), FText::AsNumber(i + 1)));
                                NewBtn->AddChild(BtnText);
                            }

                            UFurnitureOptionListener* Listener = NewObject<UFurnitureOptionListener>(this);
                            Listener->Init(this, ActiveComponent, EOptionType::Size, i);
                            OptionListeners.Add(Listener);

                            NewBtn->OnClicked.AddDynamic(Listener, &UFurnitureOptionListener::OnButtonClicked);
                            NewBtn->OnHovered.AddDynamic(Listener, &UFurnitureOptionListener::OnButtonHovered);
                            NewBtn->OnUnhovered.AddDynamic(Listener, &UFurnitureOptionListener::OnButtonUnhovered);

                            Size_Container->AddChild(NewBtn);
                        }
                    }
                }
            }
        }
        else if (Combo_Size)
        {
            if (ActiveComponent == EFurnitureComponentType::Countertop)
            {
                Combo_Size->SetVisibility(ESlateVisibility::Collapsed);
            }
            else
            {
                Combo_Size->SetVisibility(TargetVisibility);
                if (bIsValidMesh)
                {
                    TArray<FText> SizeNames;
                    for (int32 i = 0; i < ActiveOpts.Sizes.Num(); ++i)
                    {
                        SizeNames.Add(FText::Format(FText::FromString(TEXT("Size {0}")), FText::AsNumber(i + 1)));
                    }
                    int32 CurrentSizeIdx = Booth->ActiveState.ActiveSizeIndex;
                    if (ActiveComponent == EFurnitureComponentType::Closet) CurrentSizeIdx = Booth->ActiveState.ClosetSizeIndex;
                    else if (ActiveComponent == EFurnitureComponentType::Sink) CurrentSizeIdx = Booth->ActiveState.SinkSizeIndex;
                    else if (ActiveComponent == EFurnitureComponentType::Faucet) CurrentSizeIdx = Booth->ActiveState.FaucetSizeIndex;
                    else if (ActiveComponent == EFurnitureComponentType::Mirror) CurrentSizeIdx = Booth->ActiveState.MirrorSizeIndex;
                    PopulateCombo(Combo_Size, SizeNames, CurrentSizeIdx);
                }
            }
        }

        // ── COLOR SELECTORS ──
        if (Color_Container)
        {
            Color_Container->SetVisibility(TargetVisibility);
            Color_Container->ClearChildren();
            if (Combo_Color)
            {
                Combo_Color->SetVisibility(ESlateVisibility::Collapsed);
            }

            if (bIsValidMesh)
            {
                for (int32 i = 0; i < ActiveOpts.Colors.Num(); ++i)
                {
                    const FFurnitureColorOption& ColorOpt = ActiveOpts.Colors[i];
                    UButton* NewBtn = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass());
                    if (NewBtn)
                    {
                        UImage* BtnImg = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass());
                        if (BtnImg)
                        {
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
        else if (Combo_Color)
        {
            Combo_Color->SetVisibility(TargetVisibility);
            if (bIsValidMesh)
            {
                TArray<FText> ColorNames;
                for (int32 i = 0; i < ActiveOpts.Colors.Num(); ++i)
                {
                    ColorNames.Add(FText::Format(FText::FromString(TEXT("Color {0}")), FText::AsNumber(i + 1)));
                }
                int32 CurrentColorIdx = Booth->ActiveState.ActiveColorIndex;
                if (ActiveComponent == EFurnitureComponentType::Countertop) CurrentColorIdx = Booth->ActiveState.ActiveCountertopColorIndex;
                else if (ActiveComponent == EFurnitureComponentType::Closet) CurrentColorIdx = Booth->ActiveState.ClosetColorIndex;
                else if (ActiveComponent == EFurnitureComponentType::Sink) CurrentColorIdx = Booth->ActiveState.SinkColorIndex;
                else if (ActiveComponent == EFurnitureComponentType::Faucet) CurrentColorIdx = Booth->ActiveState.FaucetColorIndex;
                else if (ActiveComponent == EFurnitureComponentType::Mirror) CurrentColorIdx = Booth->ActiveState.MirrorColorIndex;
                PopulateCombo(Combo_Color, ColorNames, CurrentColorIdx);
            }
        }

        // ── Setup individual specific component selectors (optional layout table — hidden/collapsed) ────
        if (Combo_Cabinet_Size) Combo_Cabinet_Size->SetVisibility(ESlateVisibility::Collapsed);
        if (Combo_Cabinet_Color) Combo_Cabinet_Color->SetVisibility(ESlateVisibility::Collapsed);
        if (Combo_Closet_Size) Combo_Closet_Size->SetVisibility(ESlateVisibility::Collapsed);
        if (Combo_Closet_Color) Combo_Closet_Color->SetVisibility(ESlateVisibility::Collapsed);
        if (Combo_Doors_Size) Combo_Doors_Size->SetVisibility(ESlateVisibility::Collapsed);
        if (Combo_Doors_Color) Combo_Doors_Color->SetVisibility(ESlateVisibility::Collapsed);
        if (Combo_Countertop_Size) Combo_Countertop_Size->SetVisibility(ESlateVisibility::Collapsed);
        if (Combo_Countertop_Color) Combo_Countertop_Color->SetVisibility(ESlateVisibility::Collapsed);
        if (Combo_Sink_Size) Combo_Sink_Size->SetVisibility(ESlateVisibility::Collapsed);
        if (Combo_Sink_Color) Combo_Sink_Color->SetVisibility(ESlateVisibility::Collapsed);
        if (Combo_Faucet_Size) Combo_Faucet_Size->SetVisibility(ESlateVisibility::Collapsed);
        if (Combo_Faucet_Color) Combo_Faucet_Color->SetVisibility(ESlateVisibility::Collapsed);
        if (Combo_Mirror_Size) Combo_Mirror_Size->SetVisibility(ESlateVisibility::Collapsed);
        if (Combo_Mirror_Color) Combo_Mirror_Color->SetVisibility(ESlateVisibility::Collapsed);
    }
}

void UConfiguratorMainWidget::SetupIndividualComponentSelector(EFurnitureComponentType Component, UComboBoxString* SizeCombo, UComboBoxString* ColorCombo)
{
    AShowroomBooth* Booth = TargetBooth.Get();
    if (!Booth || (!SizeCombo && !ColorCombo))
    {
        return;
    }

    bool bIsValidMesh = IsComponentMeshValid(Booth, Component);
    ESlateVisibility TargetVisibility = bIsValidMesh ? ESlateVisibility::Visible : ESlateVisibility::Collapsed;

    FFurnitureProductRow ProductData;
    if (Booth->GetActiveProductData(ProductData))
    {
        FFurnitureComponentOptions Opts;
        if (Component == EFurnitureComponentType::Doors)
        {
            Opts = ProductData.CabinetOptions; // Redirect/fallback to Cabinet since doors configuration is unified
        }
        else if (Component == EFurnitureComponentType::Countertop)
        {
            Opts = ProductData.CountertopOptions;
        }
        else if (Component == EFurnitureComponentType::Closet)
        {
            Opts = ProductData.ClosetOptions;
        }
        else if (Component == EFurnitureComponentType::Sink)
        {
            Opts = ProductData.SinkOptions;
        }
        else if (Component == EFurnitureComponentType::Faucet)
        {
            Opts = ProductData.FaucetOptions;
        }
        else if (Component == EFurnitureComponentType::Mirror)
        {
            Opts = ProductData.MirrorOptions;
        }
        else
        {
            Opts = ProductData.CabinetOptions;
        }

        if (SizeCombo)
        {
            SizeCombo->SetVisibility(TargetVisibility);
            if (bIsValidMesh)
            {
                TArray<FText> SizeNames;
                for (int32 i = 0; i < Opts.Sizes.Num(); ++i)
                {
                    SizeNames.Add(FText::Format(FText::FromString(TEXT("Size {0}")), FText::AsNumber(i + 1)));
                }
                int32 SizeIdx = Booth->ActiveState.ActiveSizeIndex;
                if (Component == EFurnitureComponentType::Closet) SizeIdx = Booth->ActiveState.ClosetSizeIndex;
                else if (Component == EFurnitureComponentType::Sink) SizeIdx = Booth->ActiveState.SinkSizeIndex;
                else if (Component == EFurnitureComponentType::Faucet) SizeIdx = Booth->ActiveState.FaucetSizeIndex;
                else if (Component == EFurnitureComponentType::Mirror) SizeIdx = Booth->ActiveState.MirrorSizeIndex;
                PopulateCombo(SizeCombo, SizeNames, SizeIdx);
            }
        }

        if (ColorCombo)
        {
            ColorCombo->SetVisibility(TargetVisibility);
            if (bIsValidMesh)
            {
                TArray<FText> ColorNames;
                for (int32 i = 0; i < Opts.Colors.Num(); ++i)
                {
                    ColorNames.Add(FText::Format(FText::FromString(TEXT("Color {0}")), FText::AsNumber(i + 1)));
                }
                int32 ColorIdx = Booth->ActiveState.ActiveColorIndex;
                if (Component == EFurnitureComponentType::Countertop) ColorIdx = Booth->ActiveState.ActiveCountertopColorIndex;
                else if (Component == EFurnitureComponentType::Closet) ColorIdx = Booth->ActiveState.ClosetColorIndex;
                else if (Component == EFurnitureComponentType::Sink) ColorIdx = Booth->ActiveState.SinkColorIndex;
                else if (Component == EFurnitureComponentType::Faucet) ColorIdx = Booth->ActiveState.FaucetColorIndex;
                else if (Component == EFurnitureComponentType::Mirror) ColorIdx = Booth->ActiveState.MirrorColorIndex;
                PopulateCombo(ColorCombo, ColorNames, ColorIdx);
            }
        }
    }
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
        {
            // Standalone sink mesh validity requires countertop type to be SurfaceMounted and sink to not be null
            FFurnitureProductRow Row;
            if (Booth->GetActiveProductData(Row))
            {
                return Row.CountertopType == ECountertopType::SurfaceMounted && Booth->SinkMesh && Booth->SinkMesh->GetStaticMesh() != nullptr;
            }
            return false;
        }
    case EFurnitureComponentType::Faucet:
        return Booth->FaucetMesh && Booth->FaucetMesh->GetStaticMesh() != nullptr;
    case EFurnitureComponentType::Mirror:
        return Booth->MirrorMesh && Booth->MirrorMesh->GetStaticMesh() != nullptr;
    default:
        break;
    }
    return false;
}

void UConfiguratorMainWidget::PopulateCombo(UComboBoxString* Combo, const TArray<FText>& DisplayNames, int32 CurrentSelectionIndex)
{
    if (!Combo)
    {
        return;
    }

    // Temporarily unbind to prevent firing selection change callbacks during population
    Combo->OnSelectionChanged.RemoveAll(this);

    Combo->ClearOptions();
    
    for (int32 i = 0; i < DisplayNames.Num(); ++i)
    {
        FString OptionStr = DisplayNames[i].ToString();
        Combo->AddOption(OptionStr);
    }

    if (DisplayNames.Num() > 0 && DisplayNames.IsValidIndex(CurrentSelectionIndex))
    {
        Combo->SetSelectedIndex(CurrentSelectionIndex);
    }

    // Re-bind the selection changed callbacks
    if (Combo == Combo_Size) Combo->OnSelectionChanged.AddDynamic(this, &UConfiguratorMainWidget::OnSizeSelected);
    else if (Combo == Combo_Color) Combo->OnSelectionChanged.AddDynamic(this, &UConfiguratorMainWidget::OnColorSelected);
    else if (Combo == Combo_Cabinet_Size) Combo->OnSelectionChanged.AddDynamic(this, &UConfiguratorMainWidget::OnCabinetSizeChanged);
    else if (Combo == Combo_Cabinet_Color) Combo->OnSelectionChanged.AddDynamic(this, &UConfiguratorMainWidget::OnCabinetColorChanged);
    else if (Combo == Combo_Closet_Size) Combo->OnSelectionChanged.AddDynamic(this, &UConfiguratorMainWidget::OnClosetSizeChanged);
    else if (Combo == Combo_Closet_Color) Combo->OnSelectionChanged.AddDynamic(this, &UConfiguratorMainWidget::OnClosetColorChanged);
    else if (Combo == Combo_Doors_Size) Combo->OnSelectionChanged.AddDynamic(this, &UConfiguratorMainWidget::OnDoorsSizeChanged);
    else if (Combo == Combo_Doors_Color) Combo->OnSelectionChanged.AddDynamic(this, &UConfiguratorMainWidget::OnDoorsColorChanged);
    else if (Combo == Combo_Countertop_Size) Combo->OnSelectionChanged.AddDynamic(this, &UConfiguratorMainWidget::OnCountertopSizeChanged);
    else if (Combo == Combo_Countertop_Color) Combo->OnSelectionChanged.AddDynamic(this, &UConfiguratorMainWidget::OnCountertopColorChanged);
    else if (Combo == Combo_Sink_Size) Combo->OnSelectionChanged.AddDynamic(this, &UConfiguratorMainWidget::OnSinkSizeChanged);
    else if (Combo == Combo_Sink_Color) Combo->OnSelectionChanged.AddDynamic(this, &UConfiguratorMainWidget::OnSinkColorChanged);
    else if (Combo == Combo_Faucet_Size) Combo->OnSelectionChanged.AddDynamic(this, &UConfiguratorMainWidget::OnFaucetSizeChanged);
    else if (Combo == Combo_Faucet_Color) Combo->OnSelectionChanged.AddDynamic(this, &UConfiguratorMainWidget::OnFaucetColorChanged);
    else if (Combo == Combo_Mirror_Size) Combo->OnSelectionChanged.AddDynamic(this, &UConfiguratorMainWidget::OnMirrorSizeChanged);
    else if (Combo == Combo_Mirror_Color) Combo->OnSelectionChanged.AddDynamic(this, &UConfiguratorMainWidget::OnMirrorColorChanged);
}



// ── Strict UI button event callbacks ──────────────────────────────────────────

void UConfiguratorMainWidget::OnViewmodeClicked()
{
    AMaxiMallPreviewController* PC = OwningPC.Get();
    AShowroomBooth* Booth = TargetBooth.Get();
    if (PC && Booth)
    {
        // Transition player to isolated Viewmode
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

// ── Selector Selection Changed Event Listeners ────────────────────────────────

void UConfiguratorMainWidget::OnSizeSelected(FString SelectedItem, ESelectInfo::Type SelectionType)
{
    AShowroomBooth* Booth = TargetBooth.Get();
    if (!Booth || !Combo_Size) return;

    int32 SelectedIdx = Combo_Size->GetSelectedIndex();
    if (OwningPC)
    {
        int32 ColorIndex = Booth->ActiveState.ActiveColorIndex;
        if (ActiveComponent == EFurnitureComponentType::Countertop)
        {
            ColorIndex = Booth->ActiveState.ActiveCountertopColorIndex;
        }
        else if (ActiveComponent == EFurnitureComponentType::Closet)
        {
            ColorIndex = Booth->ActiveState.ClosetColorIndex;
        }
        else if (ActiveComponent == EFurnitureComponentType::Sink)
        {
            ColorIndex = Booth->ActiveState.SinkColorIndex;
        }
        else if (ActiveComponent == EFurnitureComponentType::Faucet)
        {
            ColorIndex = Booth->ActiveState.FaucetColorIndex;
        }
        else if (ActiveComponent == EFurnitureComponentType::Mirror)
        {
            ColorIndex = Booth->ActiveState.MirrorColorIndex;
        }

        OwningPC->RequestBoothComponentSelection(Booth, ActiveComponent, SelectedIdx, ColorIndex);
    }
}

void UConfiguratorMainWidget::OnColorSelected(FString SelectedItem, ESelectInfo::Type SelectionType)
{
    AShowroomBooth* Booth = TargetBooth.Get();
    if (!Booth || !Combo_Color) return;

    int32 SelectedIdx = Combo_Color->GetSelectedIndex();
    if (OwningPC)
    {
        int32 SizeIndex = Booth->ActiveState.ActiveSizeIndex;
        if (ActiveComponent == EFurnitureComponentType::Closet)
        {
            SizeIndex = Booth->ActiveState.ClosetSizeIndex;
        }
        else if (ActiveComponent == EFurnitureComponentType::Sink)
        {
            SizeIndex = Booth->ActiveState.SinkSizeIndex;
        }
        else if (ActiveComponent == EFurnitureComponentType::Faucet)
        {
            SizeIndex = Booth->ActiveState.FaucetSizeIndex;
        }
        else if (ActiveComponent == EFurnitureComponentType::Mirror)
        {
            SizeIndex = Booth->ActiveState.MirrorSizeIndex;
        }

        OwningPC->RequestBoothComponentSelection(Booth, ActiveComponent, SizeIndex, SelectedIdx);
    }
}

// ── Specific Component Selector Updates (Helper) ──────────────────────────────

void UConfiguratorMainWidget::UpdateSelectionForComponent(EFurnitureComponentType Component, UComboBoxString* SizeCombo, UComboBoxString* ColorCombo)
{
    AShowroomBooth* Booth = TargetBooth.Get();
    if (!Booth) return;

    int32 TargetSize = Booth->ActiveState.ActiveSizeIndex;
    int32 TargetColor = Booth->ActiveState.ActiveColorIndex;

    if (Component == EFurnitureComponentType::Countertop)
    {
        TargetColor = Booth->ActiveState.ActiveCountertopColorIndex;
    }
    else if (Component == EFurnitureComponentType::Closet)
    {
        TargetSize = Booth->ActiveState.ClosetSizeIndex;
        TargetColor = Booth->ActiveState.ClosetColorIndex;
    }
    else if (Component == EFurnitureComponentType::Sink)
    {
        TargetSize = Booth->ActiveState.SinkSizeIndex;
        TargetColor = Booth->ActiveState.SinkColorIndex;
    }
    else if (Component == EFurnitureComponentType::Faucet)
    {
        TargetSize = Booth->ActiveState.FaucetSizeIndex;
        TargetColor = Booth->ActiveState.FaucetColorIndex;
    }
    else if (Component == EFurnitureComponentType::Mirror)
    {
        TargetSize = Booth->ActiveState.MirrorSizeIndex;
        TargetColor = Booth->ActiveState.MirrorColorIndex;
    }

    if (SizeCombo)
    {
        int32 SizeIdx = SizeCombo->GetSelectedIndex();
        if (SizeIdx != -1)
        {
            TargetSize = SizeIdx;
        }
    }

    if (ColorCombo)
    {
        int32 ColorIdx = ColorCombo->GetSelectedIndex();
        if (ColorIdx != -1)
        {
            TargetColor = ColorIdx;
        }
    }

    if (OwningPC)
    {
        OwningPC->RequestBoothComponentSelection(Booth, Component, TargetSize, TargetColor);
    }
}

// ── Individual Component Event Listeners ───────────────────────────────────────

void UConfiguratorMainWidget::OnCabinetSizeChanged(FString SelectedItem, ESelectInfo::Type SelectionType)
{
    UpdateSelectionForComponent(EFurnitureComponentType::Cabinet, Combo_Cabinet_Size, Combo_Cabinet_Color);
}
void UConfiguratorMainWidget::OnCabinetColorChanged(FString SelectedItem, ESelectInfo::Type SelectionType)
{
    UpdateSelectionForComponent(EFurnitureComponentType::Cabinet, Combo_Cabinet_Size, Combo_Cabinet_Color);
}

void UConfiguratorMainWidget::OnClosetSizeChanged(FString SelectedItem, ESelectInfo::Type SelectionType)
{
    UpdateSelectionForComponent(EFurnitureComponentType::Closet, Combo_Closet_Size, Combo_Closet_Color);
}
void UConfiguratorMainWidget::OnClosetColorChanged(FString SelectedItem, ESelectInfo::Type SelectionType)
{
    UpdateSelectionForComponent(EFurnitureComponentType::Closet, Combo_Closet_Size, Combo_Closet_Color);
}

void UConfiguratorMainWidget::OnDoorsSizeChanged(FString SelectedItem, ESelectInfo::Type SelectionType)
{
    UpdateSelectionForComponent(EFurnitureComponentType::Doors, Combo_Doors_Size, Combo_Doors_Color);
}
void UConfiguratorMainWidget::OnDoorsColorChanged(FString SelectedItem, ESelectInfo::Type SelectionType)
{
    UpdateSelectionForComponent(EFurnitureComponentType::Doors, Combo_Doors_Size, Combo_Doors_Color);
}

void UConfiguratorMainWidget::OnCountertopSizeChanged(FString SelectedItem, ESelectInfo::Type SelectionType)
{
    UpdateSelectionForComponent(EFurnitureComponentType::Countertop, Combo_Countertop_Size, Combo_Countertop_Color);
}
void UConfiguratorMainWidget::OnCountertopColorChanged(FString SelectedItem, ESelectInfo::Type SelectionType)
{
    UpdateSelectionForComponent(EFurnitureComponentType::Countertop, Combo_Countertop_Size, Combo_Countertop_Color);
}

void UConfiguratorMainWidget::OnSinkSizeChanged(FString SelectedItem, ESelectInfo::Type SelectionType)
{
    UpdateSelectionForComponent(EFurnitureComponentType::Sink, Combo_Sink_Size, Combo_Sink_Color);
}
void UConfiguratorMainWidget::OnSinkColorChanged(FString SelectedItem, ESelectInfo::Type SelectionType)
{
    UpdateSelectionForComponent(EFurnitureComponentType::Sink, Combo_Sink_Size, Combo_Sink_Color);
}

void UConfiguratorMainWidget::OnFaucetSizeChanged(FString SelectedItem, ESelectInfo::Type SelectionType)
{
    UpdateSelectionForComponent(EFurnitureComponentType::Faucet, Combo_Faucet_Size, Combo_Faucet_Color);
}
void UConfiguratorMainWidget::OnFaucetColorChanged(FString SelectedItem, ESelectInfo::Type SelectionType)
{
    UpdateSelectionForComponent(EFurnitureComponentType::Faucet, Combo_Faucet_Size, Combo_Faucet_Color);
}

void UConfiguratorMainWidget::OnMirrorSizeChanged(FString SelectedItem, ESelectInfo::Type SelectionType)
{
    UpdateSelectionForComponent(EFurnitureComponentType::Mirror, Combo_Mirror_Size, Combo_Mirror_Color);
}
void UConfiguratorMainWidget::OnMirrorColorChanged(FString SelectedItem, ESelectInfo::Type SelectionType)
{
    UpdateSelectionForComponent(EFurnitureComponentType::Mirror, Combo_Mirror_Size, Combo_Mirror_Color);
}

void UConfiguratorMainWidget::HandleOptionSelected(EFurnitureComponentType Component, EOptionType Type, int32 OptionIndex)
{
    AShowroomBooth* Booth = TargetBooth.Get();
    if (!Booth || !OwningPC) return;

    int32 SizeIndex = Booth->ActiveState.ActiveSizeIndex;
    int32 ColorIndex = Booth->ActiveState.ActiveColorIndex;

    if (Component == EFurnitureComponentType::Countertop)
    {
        SizeIndex = Booth->ActiveState.ActiveSizeIndex;
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
    }
    else
    {
        ColorIndex = OptionIndex;
    }

    OwningPC->RequestBoothComponentSelection(Booth, Component, SizeIndex, ColorIndex);
}

void UConfiguratorMainWidget::HandleOptionHovered(EFurnitureComponentType Component, EOptionType Type, int32 OptionIndex)
{
    // No-op for metadata-free phase
}

void UConfiguratorMainWidget::HandleOptionUnhovered(EFurnitureComponentType Component, EOptionType Type, int32 OptionIndex)
{
    // Restore text to show active selection details
    RefreshSelections();
}
