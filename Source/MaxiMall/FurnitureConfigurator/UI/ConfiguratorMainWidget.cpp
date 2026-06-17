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
        OwnerWidget->HandleOptionSelected(Component, Type, OptionID);
    }
}

void UFurnitureOptionListener::OnButtonHovered()
{
    if (OwnerWidget)
    {
        OwnerWidget->HandleOptionHovered(Component, Type, OptionID);
    }
}

void UFurnitureOptionListener::OnButtonUnhovered()
{
    if (OwnerWidget)
    {
        OwnerWidget->HandleOptionUnhovered(Component, Type, OptionID);
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
        FFurnitureComponentState ActiveState;
        if (GetComponentOptionsAndState(ProductData, Booth->ActiveState, ActiveComponent, ActiveOpts, ActiveState))
        {
            // Gate optional component visibility — if the component's mesh is missing/null, collapse the UI selectors
            bool bIsValidMesh = IsComponentMeshValid(Booth, ActiveComponent);
            ESlateVisibility TargetVisibility = bIsValidMesh ? ESlateVisibility::Visible : ESlateVisibility::Collapsed;

            // Clear option listeners when regenerating layout
            OptionListeners.Empty();

            // Set default / active selection details first (strictly only SKU and Description of active size mesh)
            if (bIsValidMesh)
            {
                FText ActiveNameText;
                FText ActiveDescText;
                for (const FFurnitureSizeOption& SizeOpt : ActiveOpts.Sizes)
                {
                    if (SizeOpt.SizeID == ActiveState.SelectedSizeID)
                    {
                        ActiveNameText = SizeOpt.DisplayName;
                        ActiveDescText = SizeOpt.Description;
                        break;
                    }
                }

                if (Txt_SelectionName)
                {
                    Txt_SelectionName->SetText(ActiveNameText);
                    Txt_SelectionName->SetVisibility(ESlateVisibility::Visible);
                }
                if (Txt_SelectionDescription)
                {
                    Txt_SelectionDescription->SetText(ActiveDescText);
                    Txt_SelectionDescription->SetVisibility(ESlateVisibility::Visible);
                }
            }
            else
            {
                if (Txt_SelectionName)
                {
                    Txt_SelectionName->SetVisibility(ESlateVisibility::Collapsed);
                }
                if (Txt_SelectionDescription)
                {
                    Txt_SelectionDescription->SetVisibility(ESlateVisibility::Collapsed);
                }
            }

            // ── SIZE SELECTORS ──
            if (Size_Container)
            {
                Size_Container->SetVisibility(TargetVisibility);
                Size_Container->ClearChildren();
                if (Combo_Size)
                {
                    Combo_Size->SetVisibility(ESlateVisibility::Collapsed);
                }

                if (bIsValidMesh)
                {
                    for (const FFurnitureSizeOption& SizeOpt : ActiveOpts.Sizes)
                    {
                        UButton* NewBtn = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass());
                        if (NewBtn)
                        {
                            UTextBlock* BtnText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
                            if (BtnText)
                            {
                                BtnText->SetText(SizeOpt.DisplayName);
                                NewBtn->AddChild(BtnText);
                            }

                            UFurnitureOptionListener* Listener = NewObject<UFurnitureOptionListener>(this);
                            Listener->Init(this, ActiveComponent, EOptionType::Size, SizeOpt.SizeID);
                            OptionListeners.Add(Listener);

                            NewBtn->OnClicked.AddDynamic(Listener, &UFurnitureOptionListener::OnButtonClicked);
                            NewBtn->OnHovered.AddDynamic(Listener, &UFurnitureOptionListener::OnButtonHovered);
                            NewBtn->OnUnhovered.AddDynamic(Listener, &UFurnitureOptionListener::OnButtonUnhovered);

                            Size_Container->AddChild(NewBtn);
                        }
                    }
                }
            }
            else if (Combo_Size)
            {
                Combo_Size->SetVisibility(TargetVisibility);
                if (bIsValidMesh)
                {
                    TArray<FName> SizeIDs;
                    TArray<FText> SizeNames;
                    for (const FFurnitureSizeOption& SizeOpt : ActiveOpts.Sizes)
                    {
                        SizeIDs.Add(SizeOpt.SizeID);
                        SizeNames.Add(SizeOpt.DisplayName);
                    }
                    PopulateCombo(Combo_Size, SizeIDs, SizeNames, ActiveState.SelectedSizeID);
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
                    for (const FFurnitureColorOption& ColorOpt : ActiveOpts.Colors)
                    {
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
                            Listener->Init(this, ActiveComponent, EOptionType::Color, ColorOpt.ColorID);
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
                    TArray<FName> ColorIDs;
                    TArray<FText> ColorNames;
                    for (const FFurnitureColorOption& ColorOpt : ActiveOpts.Colors)
                    {
                        ColorIDs.Add(ColorOpt.ColorID);
                        ColorNames.Add(ColorOpt.DisplayName);
                    }
                    PopulateCombo(Combo_Color, ColorIDs, ColorNames, ActiveState.SelectedColorID);
                }
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
        FFurnitureComponentState State;
        if (GetComponentOptionsAndState(ProductData, Booth->ActiveState, Component, Opts, State))
        {
            if (SizeCombo)
            {
                SizeCombo->SetVisibility(TargetVisibility);
                if (bIsValidMesh)
                {
                    TArray<FName> SizeIDs;
                    TArray<FText> SizeNames;
                    for (const FFurnitureSizeOption& SizeOpt : Opts.Sizes)
                    {
                        SizeIDs.Add(SizeOpt.SizeID);
                        SizeNames.Add(SizeOpt.DisplayName);
                    }
                    PopulateCombo(SizeCombo, SizeIDs, SizeNames, State.SelectedSizeID);
                }
            }

            if (ColorCombo)
            {
                ColorCombo->SetVisibility(TargetVisibility);
                if (bIsValidMesh)
                {
                    TArray<FName> ColorIDs;
                    TArray<FText> ColorNames;
                    for (const FFurnitureColorOption& ColorOpt : Opts.Colors)
                    {
                        ColorIDs.Add(ColorOpt.ColorID);
                        ColorNames.Add(ColorOpt.DisplayName);
                    }
                    PopulateCombo(ColorCombo, ColorIDs, ColorNames, State.SelectedColorID);
                }
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

void UConfiguratorMainWidget::PopulateCombo(UComboBoxString* Combo, const TArray<FName>& OptionIDs, const TArray<FText>& DisplayNames, FName CurrentSelection)
{
    if (!Combo)
    {
        return;
    }

    // Temporarily unbind to prevent firing selection change callbacks during population
    Combo->OnSelectionChanged.RemoveAll(this);

    Combo->ClearOptions();
    
    int32 SelectIndex = 0;
    for (int32 i = 0; i < OptionIDs.Num(); ++i)
    {
        FString OptionStr = OptionIDs[i].ToString();
        if (DisplayNames.IsValidIndex(i) && !DisplayNames[i].IsEmpty())
        {
            OptionStr = FString::Printf(TEXT("%s (%s)"), *DisplayNames[i].ToString(), *OptionIDs[i].ToString());
        }
        Combo->AddOption(OptionStr);

        if (OptionIDs[i] == CurrentSelection)
        {
            SelectIndex = i;
        }
    }

    if (OptionIDs.Num() > 0)
    {
        Combo->SetSelectedIndex(SelectIndex);
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

bool UConfiguratorMainWidget::GetComponentOptionsAndState(const FFurnitureProductRow& Row, const FShowroomBoothConfigState& State, EFurnitureComponentType Component, FFurnitureComponentOptions& OutOptions, FFurnitureComponentState& OutState) const
{
    switch (Component)
    {
    case EFurnitureComponentType::Cabinet:
        OutOptions = Row.CabinetOptions;
        OutState = State.CabinetState;
        return true;
    case EFurnitureComponentType::Closet:
        OutOptions = Row.ClosetOptions;
        OutState = State.ClosetState;
        return true;
    case EFurnitureComponentType::Doors:
        OutOptions = Row.DoorOptions;
        OutState = State.DoorState;
        return true;
    case EFurnitureComponentType::Countertop:
        OutOptions = Row.CountertopOptions;
        OutState = State.CountertopState;
        return true;
    case EFurnitureComponentType::Sink:
        OutOptions = Row.SinkOptions;
        OutState = State.SinkState;
        return true;
    case EFurnitureComponentType::Faucet:
        OutOptions = Row.FaucetOptions;
        OutState = State.FaucetState;
        return true;
    case EFurnitureComponentType::Mirror:
        OutOptions = Row.MirrorOptions;
        OutState = State.MirrorState;
        return true;
    default:
        break;
    }
    return false;
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

    FFurnitureProductRow ProductData;
    if (Booth->GetActiveProductData(ProductData))
    {
        FFurnitureComponentOptions Opts;
        FFurnitureComponentState State;
        if (GetComponentOptionsAndState(ProductData, Booth->ActiveState, ActiveComponent, Opts, State))
        {
            int32 SelectedIdx = Combo_Size->GetSelectedIndex();
            if (Opts.Sizes.IsValidIndex(SelectedIdx))
            {
                if (OwningPC)
                {
                    OwningPC->RequestBoothComponentSelection(Booth, ActiveComponent, Opts.Sizes[SelectedIdx].SizeID, State.SelectedColorID);
                }
            }
        }
    }
}

void UConfiguratorMainWidget::OnColorSelected(FString SelectedItem, ESelectInfo::Type SelectionType)
{
    AShowroomBooth* Booth = TargetBooth.Get();
    if (!Booth || !Combo_Color) return;

    FFurnitureProductRow ProductData;
    if (Booth->GetActiveProductData(ProductData))
    {
        FFurnitureComponentOptions Opts;
        FFurnitureComponentState State;
        if (GetComponentOptionsAndState(ProductData, Booth->ActiveState, ActiveComponent, Opts, State))
        {
            int32 SelectedIdx = Combo_Color->GetSelectedIndex();
            if (Opts.Colors.IsValidIndex(SelectedIdx))
            {
                if (OwningPC)
                {
                    OwningPC->RequestBoothComponentSelection(Booth, ActiveComponent, State.SelectedSizeID, Opts.Colors[SelectedIdx].ColorID);
                }
            }
        }
    }
}

// ── Specific Component Selector Updates (Helper) ──────────────────────────────

void UConfiguratorMainWidget::UpdateSelectionForComponent(EFurnitureComponentType Component, UComboBoxString* SizeCombo, UComboBoxString* ColorCombo)
{
    AShowroomBooth* Booth = TargetBooth.Get();
    if (!Booth) return;

    FFurnitureProductRow ProductData;
    if (Booth->GetActiveProductData(ProductData))
    {
        FFurnitureComponentOptions Opts;
        FFurnitureComponentState State;
        if (GetComponentOptionsAndState(ProductData, Booth->ActiveState, Component, Opts, State))
        {
            FName TargetSize = State.SelectedSizeID;
            FName TargetColor = State.SelectedColorID;

            if (SizeCombo)
            {
                int32 SizeIdx = SizeCombo->GetSelectedIndex();
                if (Opts.Sizes.IsValidIndex(SizeIdx))
                {
                    TargetSize = Opts.Sizes[SizeIdx].SizeID;
                }
            }

            if (ColorCombo)
            {
                int32 ColorIdx = ColorCombo->GetSelectedIndex();
                if (Opts.Colors.IsValidIndex(ColorIdx))
                {
                    TargetColor = Opts.Colors[ColorIdx].ColorID;
                }
            }

            if (OwningPC)
            {
                OwningPC->RequestBoothComponentSelection(Booth, Component, TargetSize, TargetColor);
            }
        }
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

void UConfiguratorMainWidget::HandleOptionSelected(EFurnitureComponentType Component, EOptionType Type, FName OptionID)
{
    AShowroomBooth* Booth = TargetBooth.Get();
    if (!Booth || !OwningPC) return;

    FFurnitureProductRow ProductData;
    if (Booth->GetActiveProductData(ProductData))
    {
        FFurnitureComponentOptions Opts;
        FFurnitureComponentState State;
        if (GetComponentOptionsAndState(ProductData, Booth->ActiveState, Component, Opts, State))
        {
            if (Type == EOptionType::Size)
            {
                OwningPC->RequestBoothComponentSelection(Booth, Component, OptionID, State.SelectedColorID);
            }
            else
            {
                OwningPC->RequestBoothComponentSelection(Booth, Component, State.SelectedSizeID, OptionID);
            }
        }
    }
}

void UConfiguratorMainWidget::HandleOptionHovered(EFurnitureComponentType Component, EOptionType Type, FName OptionID)
{
    AShowroomBooth* Booth = TargetBooth.Get();
    if (!Booth) return;

    FFurnitureProductRow ProductData;
    if (Booth->GetActiveProductData(ProductData))
    {
        FFurnitureComponentOptions Opts;
        FFurnitureComponentState State;
        if (GetComponentOptionsAndState(ProductData, Booth->ActiveState, Component, Opts, State))
        {
            FText DisplayNameText;
            FText DescriptionText;

            if (Type == EOptionType::Size)
            {
                for (const FFurnitureSizeOption& SizeOpt : Opts.Sizes)
                {
                    if (SizeOpt.SizeID == OptionID)
                    {
                        DisplayNameText = SizeOpt.DisplayName;
                        DescriptionText = SizeOpt.Description;
                        break;
                    }
                }
            }
            else
            {
                for (const FFurnitureColorOption& ColorOpt : Opts.Colors)
                {
                    if (ColorOpt.ColorID == OptionID)
                    {
                        DisplayNameText = ColorOpt.DisplayName;
                        DescriptionText = ColorOpt.Description;
                        break;
                    }
                }
            }

            if (Txt_SelectionName)
            {
                Txt_SelectionName->SetText(DisplayNameText);
            }
            if (Txt_SelectionDescription)
            {
                Txt_SelectionDescription->SetText(DescriptionText);
            }
        }
    }
}

void UConfiguratorMainWidget::HandleOptionUnhovered(EFurnitureComponentType Component, EOptionType Type, FName OptionID)
{
    // Restore text to show active selection details
    RefreshSelections();
}
