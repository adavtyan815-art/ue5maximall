// Copyright awsTutorial. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "ColorCatalogTypes.h"
#include "Blueprint/UserWidget.h"
#include "ColorCatalogWidget.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnColorSelectedEvent, FLinearColor, SelectedColor, UMaterialInterface*, OverrideMaterial);

class UTileView;
class UEditableTextBox;
class UButton;
class UTextBlock;
class AActor;
class UPrimitiveComponent;
class UColorCatalogSubsystem;
class UColorCatalogItemObject;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnColorCatalogClosedSignature);

/**
 * Main UI Controller for the RAL & NCS Color Palette Catalog.
 * Handles target mesh tinting, search filtering, and seamless navigation with WBP_PreviewWindow.
 */
UCLASS(Abstract)
class AWSTUTORIAL_API UColorCatalogWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	/**
	 * Helper method to open WBP_ColorCatalog from WBP_PreviewWindow.
	 * Collapses CallingWidget, sets target mesh, and automatically restores CallingWidget when Back is clicked.
	 */
	UFUNCTION(BlueprintCallable, Category = "Color Catalog Controller", meta = (DefaultToSelf = "CallingWidget"))
	static UColorCatalogWidget* OpenColorCatalogForWidget(
		UUserWidget* CallingWidget,
		TSubclassOf<UColorCatalogWidget> CatalogWidgetClass
	);

	/** Set parent widget to collapse on open and restore on Back button click */
	    /** Set the parent UI to collapse when this catalog is active */
    UFUNCTION(BlueprintCallable, Category = "Color Catalog Controller")
    void SetParentCallingWidget(UUserWidget* InParentWidget);

    /** Event fired when a color swatch is clicked for Live Preview. Bind to this for multiplayer! */
    UPROPERTY(BlueprintAssignable, Category = "Color Catalog Controller")
    FOnColorSelectedEvent OnColorSelected;

	/** Switch catalog tab (RAL vs NCS) */
	UFUNCTION(BlueprintCallable, Category = "Color Catalog Controller")
	void SetCatalogType(EColorCatalogType NewCatalogType);

	/** Set active shade category orb */
	UFUNCTION(BlueprintCallable, Category = "Color Catalog Controller")
	void SetActiveCategory(EColorShadeCategory NewCategory);

	/** Updates visual highlight for RAL and NCS buttons */
	UFUNCTION(BlueprintCallable, Category = "Color Catalog Controller")
	void UpdateTabButtonStyles();

	/** Updates visual highlight for color category buttons */
	UFUNCTION(BlueprintCallable, Category = "Color Catalog Controller")
	void UpdateCategoryButtonStyles();

	/** Refresh grid items */
	UFUNCTION(BlueprintCallable, Category = "Color Catalog Controller")
	void RefreshGrid();

	UPROPERTY(BlueprintAssignable, Category = "Color Catalog Controller")
	FOnColorCatalogClosedSignature OnCatalogClosed;

	/** Color applied to active RAL/NCS tab button (Editable in UMG Details panel or Blueprint) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Color Catalog Config|Styling")
	FLinearColor ActiveTabColor = FLinearColor(0.04f, 0.52f, 1.0f, 1.0f);

	/** Color applied to inactive RAL/NCS tab button (Editable in UMG Details panel or Blueprint) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Color Catalog Config|Styling")
	FLinearColor InactiveTabColor = FLinearColor(0.07f, 0.11f, 0.18f, 1.0f);

	/** Color for active 'All' category button */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Color Catalog Config|Styling")
	FLinearColor ActiveCategoryAllColor = FLinearColor(0.04f, 0.52f, 1.0f, 1.0f);

	/** Color for inactive 'All' category button */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Color Catalog Config|Styling")
	FLinearColor InactiveCategoryAllColor = FLinearColor(0.12f, 0.16f, 0.22f, 1.0f);

	/** Scale factor for the selected category orb (larger than inactive orbs) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Color Catalog Config|Styling")
	float ActiveCategoryScale = 1.25f;

	/** Scale factor for inactive category orbs */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Color Catalog Config|Styling")
	float InactiveCategoryScale = 1.0f;

	/** Scale factor for the selected shade in the grid (larger than inactive shades) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Color Catalog Config|Styling")
	float ActiveSwatchScale = 1.20f;

	/** Scale factor for inactive shades in the grid */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Color Catalog Config|Styling")
	float InactiveSwatchScale = 1.0f;

	/** Set active and inactive tab colors dynamically */
	UFUNCTION(BlueprintCallable, Category = "Color Catalog Controller")
	void SetTabColors(FLinearColor InActiveColor, FLinearColor InInactiveColor);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Color Catalog Config")
	EColorCatalogType DefaultCatalogType = EColorCatalogType::RAL;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Color Catalog Config")
	EColorShadeCategory DefaultCategory = EColorShadeCategory::Red;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Color Catalog Config")
	TSubclassOf<UColorCatalogItemObject> ItemObjectClass;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Color Catalog Config")
	FName ColorParameterName = FName("BaseColor");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Color Catalog Config")
	UMaterialInterface* OverrideMaterial = nullptr;

protected:

	// --- BIND WIDGET COMPONENTS ---

	UPROPERTY(meta = (BindWidgetOptional))
	UTileView* TileView_Swatches;

	UPROPERTY(meta = (BindWidgetOptional))
	UEditableTextBox* EditableText_Search;

	UPROPERTY(meta = (BindWidgetOptional))
	UButton* Button_RAL;

	UPROPERTY(meta = (BindWidgetOptional))
	UButton* Button_NCS;

	UPROPERTY(meta = (BindWidgetOptional))
	UTextBlock* Text_ActiveColor;

	UPROPERTY(meta = (BindWidgetOptional))
	UButton* Button_Back;

	// Category Orb Buttons (Optional Binds)
	UPROPERTY(meta = (BindWidgetOptional)) UButton* Button_CategoryAll;
	UPROPERTY(meta = (BindWidgetOptional)) UButton* Button_CategoryRed;
	UPROPERTY(meta = (BindWidgetOptional)) UButton* Button_CategoryOrange;
	UPROPERTY(meta = (BindWidgetOptional)) UButton* Button_CategoryYellow;
	UPROPERTY(meta = (BindWidgetOptional)) UButton* Button_CategoryGreen;
	UPROPERTY(meta = (BindWidgetOptional)) UButton* Button_CategoryBlue;
	UPROPERTY(meta = (BindWidgetOptional)) UButton* Button_CategoryViolet;
	UPROPERTY(meta = (BindWidgetOptional)) UButton* Button_CategoryGrey;
	UPROPERTY(meta = (BindWidgetOptional)) UButton* Button_CategoryWhite;
	UPROPERTY(meta = (BindWidgetOptional)) UButton* Button_CategoryBrown;
	UPROPERTY(meta = (BindWidgetOptional)) UButton* Button_CategoryBeige;
	UPROPERTY(meta = (BindWidgetOptional)) UButton* Button_CategoryBlack;
	UPROPERTY(meta = (BindWidgetOptional)) UButton* Button_CategoryNeutral;

private:
	UPROPERTY()
	UUserWidget* ParentCallingWidget = nullptr;

	UPROPERTY()
	AActor* TargetActor = nullptr;

	UPROPERTY()
	UPrimitiveComponent* TargetComponent = nullptr;

	UPROPERTY()
	TArray<UPrimitiveComponent*> TargetComponents;

	EColorCatalogType CurrentCatalogType = EColorCatalogType::RAL;
	EColorShadeCategory CurrentCategory = EColorShadeCategory::All;
	FString CurrentSearchQuery = TEXT("");

	FColorCatalogItem ActiveSelectedColorItem;
	bool bHasActiveSelection = false;

	UFUNCTION() void OnSearchTextChanged(const FText& NewText);
	UFUNCTION() void OnRALButtonClicked();
	UFUNCTION() void OnNCSButtonClicked();
	UFUNCTION() void OnBackButtonClicked();
	UFUNCTION() void OnTileViewEntryClicked(UObject* Item);

	// Category Click Handlers
	UFUNCTION() void OnCatAllClicked() { SetActiveCategory(EColorShadeCategory::All); }
	UFUNCTION() void OnCatRedClicked() { SetActiveCategory(EColorShadeCategory::Red); }
	UFUNCTION() void OnCatOrangeClicked() { SetActiveCategory(EColorShadeCategory::Orange); }
	UFUNCTION() void OnCatYellowClicked() { SetActiveCategory(EColorShadeCategory::Yellow); }
	UFUNCTION() void OnCatGreenClicked() { SetActiveCategory(EColorShadeCategory::Green); }
	UFUNCTION() void OnCatBlueClicked() { SetActiveCategory(EColorShadeCategory::Blue); }
	UFUNCTION() void OnCatVioletClicked() { SetActiveCategory(EColorShadeCategory::Violet); }
	UFUNCTION() void OnCatGreyClicked() { SetActiveCategory(EColorShadeCategory::Grey); }
	UFUNCTION() void OnCatWhiteClicked() { SetActiveCategory(EColorShadeCategory::White); }
	UFUNCTION() void OnCatBrownClicked() { SetActiveCategory(EColorShadeCategory::Brown); }
	UFUNCTION() void OnCatBeigeClicked() { SetActiveCategory(EColorShadeCategory::Beige); }
	UFUNCTION() void OnCatBlackClicked() { SetActiveCategory(EColorShadeCategory::Black); }
	UFUNCTION() void OnCatNeutralClicked() { SetActiveCategory(EColorShadeCategory::Neutral); }

	void BindCategoryButtons();
	void BroadcastColorSelected(const FLinearColor& LinearColor, UMaterialInterface* InOverrideMaterial);
};

