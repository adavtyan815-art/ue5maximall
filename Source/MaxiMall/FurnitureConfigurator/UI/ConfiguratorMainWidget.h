// Copyright MaxiMall Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "FurnitureConfigurator/Data/FurnitureTypes.h"
#include "ConfiguratorMainWidget.generated.h"

class UTextBlock;
class UButton;
class UComboBoxString;
class AMaxiMallPreviewController;
class AShowroomBooth;

/**
 * UConfiguratorMainWidget
 * Direct C++ parent class for the main configurator UMG widget.
 * Uses strict BindWidget for core controls and BindWidgetOptional for selectors.
 */
UCLASS()
class MAXIMALL_API UConfiguratorMainWidget : public UUserWidget
{
    GENERATED_BODY()

protected:
    virtual void NativeConstruct() override;

    // ── STRICT WIDGET BINDINGS ───────────────────────────────────────────────
    
    UPROPERTY(meta = (BindWidget))
    TObjectPtr<UTextBlock> Txt_ProductName;

    UPROPERTY(meta = (BindWidget))
    TObjectPtr<UButton> Btn_Viewmode;

    UPROPERTY(meta = (BindWidget))
    TObjectPtr<UButton> Btn_CloseUI;

    // ── OPTIONAL WIDGET BINDINGS (allows designer visual flexibility) ─────────

    /** Selectors for the active right-clicked component */
    UPROPERTY(meta = (BindWidgetOptional))
    TObjectPtr<UComboBoxString> Combo_Size;

    UPROPERTY(meta = (BindWidgetOptional))
    TObjectPtr<UComboBoxString> Combo_Color;

    /** Specific individual component selectors (optional layout table) */
    UPROPERTY(meta = (BindWidgetOptional))
    TObjectPtr<UComboBoxString> Combo_Cabinet_Size;
    UPROPERTY(meta = (BindWidgetOptional))
    TObjectPtr<UComboBoxString> Combo_Cabinet_Color;

    UPROPERTY(meta = (BindWidgetOptional))
    TObjectPtr<UComboBoxString> Combo_Closet_Size;
    UPROPERTY(meta = (BindWidgetOptional))
    TObjectPtr<UComboBoxString> Combo_Closet_Color;

    UPROPERTY(meta = (BindWidgetOptional))
    TObjectPtr<UComboBoxString> Combo_Doors_Size;
    UPROPERTY(meta = (BindWidgetOptional))
    TObjectPtr<UComboBoxString> Combo_Doors_Color;

    UPROPERTY(meta = (BindWidgetOptional))
    TObjectPtr<UComboBoxString> Combo_Countertop_Size;
    UPROPERTY(meta = (BindWidgetOptional))
    TObjectPtr<UComboBoxString> Combo_Countertop_Color;

    UPROPERTY(meta = (BindWidgetOptional))
    TObjectPtr<UComboBoxString> Combo_Sink_Size;
    UPROPERTY(meta = (BindWidgetOptional))
    TObjectPtr<UComboBoxString> Combo_Sink_Color;

    UPROPERTY(meta = (BindWidgetOptional))
    TObjectPtr<UComboBoxString> Combo_Faucet_Size;
    UPROPERTY(meta = (BindWidgetOptional))
    TObjectPtr<UComboBoxString> Combo_Faucet_Color;

    UPROPERTY(meta = (BindWidgetOptional))
    TObjectPtr<UComboBoxString> Combo_Mirror_Size;
    UPROPERTY(meta = (BindWidgetOptional))
    TObjectPtr<UComboBoxString> Combo_Mirror_Color;

    // ── DELEGATE CALLBACKS ───────────────────────────────────────────────────

    UFUNCTION()
    void OnViewmodeClicked();

    UFUNCTION()
    void OnCloseUIClicked();

    UFUNCTION()
    void OnSizeSelected(FString SelectedItem, ESelectInfo::Type SelectionType);

    UFUNCTION()
    void OnColorSelected(FString SelectedItem, ESelectInfo::Type SelectionType);

    // Specific component selectors delegates
    UFUNCTION()
    void OnCabinetSizeChanged(FString SelectedItem, ESelectInfo::Type SelectionType);
    UFUNCTION()
    void OnCabinetColorChanged(FString SelectedItem, ESelectInfo::Type SelectionType);

    UFUNCTION()
    void OnClosetSizeChanged(FString SelectedItem, ESelectInfo::Type SelectionType);
    UFUNCTION()
    void OnClosetColorChanged(FString SelectedItem, ESelectInfo::Type SelectionType);

    UFUNCTION()
    void OnDoorsSizeChanged(FString SelectedItem, ESelectInfo::Type SelectionType);
    UFUNCTION()
    void OnDoorsColorChanged(FString SelectedItem, ESelectInfo::Type SelectionType);

    UFUNCTION()
    void OnCountertopSizeChanged(FString SelectedItem, ESelectInfo::Type SelectionType);
    UFUNCTION()
    void OnCountertopColorChanged(FString SelectedItem, ESelectInfo::Type SelectionType);

    UFUNCTION()
    void OnSinkSizeChanged(FString SelectedItem, ESelectInfo::Type SelectionType);
    UFUNCTION()
    void OnSinkColorChanged(FString SelectedItem, ESelectInfo::Type SelectionType);

    UFUNCTION()
    void OnFaucetSizeChanged(FString SelectedItem, ESelectInfo::Type SelectionType);
    UFUNCTION()
    void OnFaucetColorChanged(FString SelectedItem, ESelectInfo::Type SelectionType);

    UFUNCTION()
    void OnMirrorSizeChanged(FString SelectedItem, ESelectInfo::Type SelectionType);
    UFUNCTION()
    void OnMirrorColorChanged(FString SelectedItem, ESelectInfo::Type SelectionType);

public:
    /** Initialize and dynamically populate the widget. */
    UFUNCTION(BlueprintCallable, Category = "MaxiMall | UI")
    void SetupWidget(AMaxiMallPreviewController* InPC, AShowroomBooth* InBooth, EFurnitureComponentType InComponent);

    /** Refreshes current selections from the showroom booth state. */
    UFUNCTION(BlueprintCallable, Category = "MaxiMall | UI")
    void RefreshSelections();

private:
    UPROPERTY()
    TObjectPtr<AMaxiMallPreviewController> OwningPC;

    UPROPERTY()
    TObjectPtr<AShowroomBooth> TargetBooth;

    UPROPERTY()
    EFurnitureComponentType ActiveComponent;

    // Helper functions
    bool IsComponentMeshValid(AShowroomBooth* Booth, EFurnitureComponentType Component) const;
    void PopulateCombo(UComboBoxString* Combo, const TArray<FName>& OptionIDs, const TArray<FText>& DisplayNames, FName CurrentSelection);
    bool GetComponentOptionsAndState(const FFurnitureProductRow& Row, const FShowroomBoothConfigState& State, EFurnitureComponentType Component, FFurnitureComponentOptions& OutOptions, FFurnitureComponentState& OutState) const;
    void SetupIndividualComponentSelector(EFurnitureComponentType Component, UComboBoxString* SizeCombo, UComboBoxString* ColorCombo);
    void UpdateSelectionForComponent(EFurnitureComponentType Component, UComboBoxString* SizeCombo, UComboBoxString* ColorCombo);
};
