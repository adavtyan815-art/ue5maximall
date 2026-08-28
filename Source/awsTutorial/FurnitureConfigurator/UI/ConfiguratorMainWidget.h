// Copyright MaxiMall Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "FurnitureConfigurator/Data/FurnitureTypes.h"
#include "Widgets/Layout/SScaleBox.h"
#include "ConfiguratorMainWidget.generated.h"

class UTextBlock;
class UButton;
class UPanelWidget;
class AAwsTutorial_PlayerController;
class AShowroomBooth;
class UColorCatalogWidget;

UCLASS()
class AWSTUTORIAL_API UFurnitureOptionListener : public UObject
{
    GENERATED_BODY()

public:
    void Init(class UConfiguratorMainWidget* InOwnerWidget, EFurnitureComponentType InComponent, EOptionType InType, int32 InOptionIndex)
    {
        OwnerWidget = InOwnerWidget;
        Component = InComponent;
        Type = InType;
        OptionIndex = InOptionIndex;
    }

    UFUNCTION()
    void OnButtonClicked();

    UFUNCTION()
    void OnButtonHovered();

    UFUNCTION()
    void OnButtonUnhovered();

    UPROPERTY()
    TObjectPtr<class UConfiguratorMainWidget> OwnerWidget;

    EFurnitureComponentType Component;
    EOptionType Type;
    int32 OptionIndex;
};

/**
 * UConfiguratorMainWidget
 * Direct C++ parent class for the main configurator UMG widget.
 * Uses strict BindWidget for core controls.
 */
UCLASS()
class AWSTUTORIAL_API UConfiguratorMainWidget : public UUserWidget
{
    GENERATED_BODY()

protected:
    virtual void NativeConstruct() override;

    // ── STRICT WIDGET BINDINGS ─────────────────────────────────────────────
    
    UPROPERTY(meta = (BindWidget))
    TObjectPtr<UButton> Btn_Viewmode;

    UPROPERTY(meta = (BindWidget))
    TObjectPtr<UButton> Btn_CloseUI;

    UPROPERTY(meta = (BindWidgetOptional))
    TObjectPtr<UButton> Btn_ColorCatalog;

    // ── AR export UI (Выбрано / Посмотреть в AR / Вся сцена в AR) ──────────
    UPROPERTY(meta = (BindWidgetOptional))
    TObjectPtr<UTextBlock> Txt_SelectedMeshName;

    UPROPERTY(meta = (BindWidgetOptional))
    TObjectPtr<UTextBlock> Txt_ARSelectedMeshName;

    UPROPERTY(meta = (BindWidgetOptional))
    TObjectPtr<UButton> Btn_ARSelected;

    UPROPERTY(meta = (BindWidgetOptional))
    TObjectPtr<UButton> Btn_ARFullScene;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MaxiMall | UI Classes")
    TSubclassOf<UUserWidget> ARExportModalClass;

    UPROPERTY(meta = (BindWidget))
    TObjectPtr<UButton> Txt_BtnURL;

    UPROPERTY(meta = (BindWidget))
    TObjectPtr<UPanelWidget> Size_Container;

    UPROPERTY(meta = (BindWidget))
    TObjectPtr<UPanelWidget> Color_Container;

    UPROPERTY(meta = (BindWidget))
    TObjectPtr<UTextBlock> Txt_SKU;

    UPROPERTY(meta = (BindWidget))
    TObjectPtr<UTextBlock> Txt_ProductName_1;

    UPROPERTY(meta = (BindWidgetOptional))
    TObjectPtr<UTextBlock> Txt_Warning;

    // ── DELEGATE CALLBACKS ─────────────────────────────────────────────────

    UFUNCTION()
    void OnViewmodeClicked();

    UFUNCTION()
    void OnCloseUIClicked();

    UFUNCTION()
    void OnColorCatalogClicked();

    UFUNCTION()
    void OnARSelectedClicked();

    UFUNCTION()
    void OnARFullSceneClicked();

    /** Refreshes Txt_SelectedMeshName / Txt_ARSelectedMeshName with the Russian logical name of ActiveComponent. */
    void UpdateSelectedObjectNameUI();

    /** Creates and shows WBP_ARExportModal (property, then asset fallback) and returns it. */
    class UARExportModalWidget* OpenARExportModal();

    UFUNCTION()
    void HandleColorSelected(FLinearColor SelectedColor, UMaterialInterface* OverrideMaterial);

    UFUNCTION()
    void OnURLButtonClicked();

public:
    /** Initialize and dynamically populate the widget. */
    UFUNCTION(BlueprintCallable, Category = "MaxiMall | UI")
    void SetupWidget(AAwsTutorial_PlayerController* InPC, AShowroomBooth* InBooth, EFurnitureComponentType InComponent);

    /** Refreshes current selections from the showroom booth state. */
    UFUNCTION(BlueprintCallable, Category = "MaxiMall | UI")
    void RefreshSelections();

    void HandleOptionSelected(EFurnitureComponentType Component, EOptionType Type, int32 OptionIndex);
    void HandleOptionHovered(EFurnitureComponentType Component, EOptionType Type, int32 OptionIndex);
    void HandleOptionUnhovered(EFurnitureComponentType Component, EOptionType Type, int32 OptionIndex);

    // ── SIZE CONTAINER CONFIGURATION ───────────────────────────────────────
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MaxiMall | UI Sizing - Size")
    float SizeButtonWidth = 100.f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MaxiMall | UI Sizing - Size")
    float SizeButtonHeight = 100.f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MaxiMall | UI Sizing - Size")
    FMargin SizeButtonPadding = FMargin(0.f);

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MaxiMall | UI Sizing - Size")
    TEnumAsByte<EStretch::Type> SizeImageStretch = EStretch::ScaleToFit;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MaxiMall | UI Sizing - Size")
    float SizeGridSlotPadding = 5.f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MaxiMall | UI Sizing - Size")
    int32 SizeColumns = 2;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MaxiMall | UI Sizing - Size")
    float SizeContainerHeight = 255.f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MaxiMall | UI Sizing - Size")
    FSlateColor SizeButtonNormalColor = FSlateColor(FLinearColor(1.f, 1.f, 1.f, 0.05f));

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MaxiMall | UI Sizing - Size")
    FSlateColor SizeButtonHoveredColor = FSlateColor(FLinearColor(1.f, 1.f, 1.f, 0.15f));

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MaxiMall | UI Sizing - Size")
    FSlateColor SizeButtonPressedColor = FSlateColor(FLinearColor(1.f, 1.f, 1.f, 0.25f));

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MaxiMall | UI Sizing - Size")
    FSlateColor ActiveSizeButtonNormalColor = FSlateColor(FLinearColor(0.2f, 0.6f, 1.0f, 0.3f));

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MaxiMall | UI Sizing - Size")
    FSlateColor ActiveSizeButtonHoveredColor = FSlateColor(FLinearColor(0.2f, 0.6f, 1.0f, 0.45f));

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MaxiMall | UI Sizing - Size")
    FSlateColor ActiveSizeButtonPressedColor = FSlateColor(FLinearColor(0.2f, 0.6f, 1.0f, 0.6f));

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MaxiMall | UI Sizing - Size")
    FSlateColor SizeTextColor = FSlateColor(FLinearColor::White);

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MaxiMall | UI Sizing - Size")
    FSlateColor ActiveSizeTextColor = FSlateColor(FLinearColor(0.2f, 0.6f, 1.0f, 1.0f));

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MaxiMall | UI Sizing - Size")
    FSlateFontInfo SizeTextFont;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MaxiMall | UI Classes")
    TSubclassOf<UColorCatalogWidget> ColorCatalogWidgetClass;

    // ── COLOR CONTAINER CONFIGURATION ──────────────────────────────────────
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MaxiMall | UI Sizing - Color")
    float ColorButtonWidth = 100.f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MaxiMall | UI Sizing - Color")
    float ColorButtonHeight = 100.f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MaxiMall | UI Sizing - Color")
    FMargin ColorButtonPadding = FMargin(0.f);

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MaxiMall | UI Sizing - Color")
    TEnumAsByte<EStretch::Type> ColorImageStretch = EStretch::ScaleToFit;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MaxiMall | UI Sizing - Color")
    float ColorGridSlotPadding = 5.f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MaxiMall | UI Sizing - Color")
    int32 ColorColumns = 2;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MaxiMall | UI Sizing - Color")
    float ColorContainerHeight = 255.f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MaxiMall | UI Sizing - Color")
    FSlateColor ColorButtonNormalColor = FSlateColor(FLinearColor(1.f, 1.f, 1.f, 0.05f));

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MaxiMall | UI Sizing - Color")
    FSlateColor ColorButtonHoveredColor = FSlateColor(FLinearColor(1.f, 1.f, 1.f, 0.15f));

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MaxiMall | UI Sizing - Color")
    FSlateColor ColorButtonPressedColor = FSlateColor(FLinearColor(1.f, 1.f, 1.f, 0.25f));

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MaxiMall | UI Sizing - Color")
    FSlateColor ActiveColorButtonNormalColor = FSlateColor(FLinearColor(0.2f, 0.6f, 1.0f, 0.3f));

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MaxiMall | UI Sizing - Color")
    FSlateColor ActiveColorButtonHoveredColor = FSlateColor(FLinearColor(0.2f, 0.6f, 1.0f, 0.45f));

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MaxiMall | UI Sizing - Color")
    FSlateColor ActiveColorButtonPressedColor = FSlateColor(FLinearColor(0.2f, 0.6f, 1.0f, 0.6f));

private:
    UPROPERTY()
    TArray<TObjectPtr<UFurnitureOptionListener>> OptionListeners;

    UPROPERTY()
    TObjectPtr<AAwsTutorial_PlayerController> OwningPC;

    UPROPERTY()
    TObjectPtr<AShowroomBooth> TargetBooth;

    UPROPERTY()
    EFurnitureComponentType ActiveComponent;

    UPROPERTY()
    TObjectPtr<UColorCatalogWidget> ActiveColorCatalogInstance;

    // Helper functions
    bool IsComponentMeshValid(AShowroomBooth* Booth, EFurnitureComponentType Component) const;
};
