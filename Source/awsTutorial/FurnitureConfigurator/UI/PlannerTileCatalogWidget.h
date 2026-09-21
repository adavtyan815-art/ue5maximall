// Copyright 2026 MaxiMall. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Constructor/RoomPlannerTypes.h"
#include "PlannerTileCatalogWidget.generated.h"

class UImage;
class UTextBlock;
class UWrapBox;
class UPlannerStyleButton;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnPlannerTileChosen, FName, TileID);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnPlannerTileChosenNative, FName /*TileID*/);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnPlannerTileCatalogClosed);

/**
 * Tile catalog of the Room Planner finishing (REQ-13): a left-side panel laid out like WBP_ColorCatalog, with one card per
 * DT_PlannerTiles row showing its preview picture (FPlannerTileRow::Thumbnail), name and size. Clicking a card chooses that tile.
 * The layout is built in code when the widget initializes; the colours below restyle it.
 */
UCLASS()
class AWSTUTORIAL_API UPlannerTileCatalogWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/**
	 * Opens the catalog the way the colour catalog opens: collapses CallingWidget, shows the catalog in the viewport (Z order 99)
	 * and makes CallingWidget visible again when the catalog closes. Null class = this class.
	 */
	static UPlannerTileCatalogWidget* OpenForWidget(UUserWidget* CallingWidget, TSubclassOf<UPlannerTileCatalogWidget> CatalogClass);

	virtual bool Initialize() override;

	/** Clicks, double-clicks and wheel turns on the panel stay in the catalog instead of reaching the scene behind it. */
	virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnMouseButtonDoubleClick(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnMouseWheel(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;

	/** Replaces the listed tiles (one card each). */
	void SetTiles(const TArray<FPlannerCatalogEntry>& InTiles);

	/** Marks the tile the selected surface carries (None = no tile finish). The owner sets it; a card click alone does not. */
	void SetActiveTile(FName TileID);
	FName GetActiveTile() const { return ActiveTileID; }

	/** False while the selection cannot take a tile: the cards are disabled and the catalog asks for a wall, floor, ceiling or baseboard. */
	void SetCardsEnabled(bool bEnabled);
	bool AreCardsEnabled() const { return bCardsEnabled; }

	/** Text of the line under the title ("Активная плитка: …" or the selection hint). */
	FText GetActiveTileLabel() const;

	UFUNCTION(BlueprintCallable, Category = "RoomPlanner|Tile Catalog")
	void CloseCatalog();

	/** A tile card was clicked. */
	UPROPERTY(BlueprintAssignable, Category = "RoomPlanner|Tile Catalog")
	FOnPlannerTileChosen OnTileChosen;

	/** Same moment as OnTileChosen, for native listeners. */
	FOnPlannerTileChosenNative OnTileChosenNative;

	UPROPERTY(BlueprintAssignable, Category = "RoomPlanner|Tile Catalog")
	FOnPlannerTileCatalogClosed OnCatalogClosed;

	int32 GetNumCards() const { return CardButtons.Num(); }
	UPlannerStyleButton* GetCardButton(int32 Index) const;
	UImage* GetCardImage(int32 Index) const;

	// ── Styling (defaults follow WBP_ColorCatalog) ─────────────────────────────

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RoomPlanner|Tile Catalog|Styling")
	FLinearColor PanelColor = FLinearColor::White;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RoomPlanner|Tile Catalog|Styling")
	FLinearColor TextColor = FLinearColor(0.048f, 0.072f, 0.112f, 1.f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RoomPlanner|Tile Catalog|Styling")
	FLinearColor SecondaryTextColor = FLinearColor(0.22f, 0.25f, 0.30f, 1.f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RoomPlanner|Tile Catalog|Styling")
	FLinearColor DividerColor = FLinearColor(0.0545f, 0.0625f, 0.0742f, 1.f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RoomPlanner|Tile Catalog|Styling")
	FLinearColor ButtonColor = FLinearColor(0.048f, 0.063f, 0.0865f, 1.f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RoomPlanner|Tile Catalog|Styling")
	FLinearColor CardOutlineColor = FLinearColor(0.70f, 0.72f, 0.75f, 1.f);

	/** Outline of the card of the active tile (the colour catalog's active tab colour). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RoomPlanner|Tile Catalog|Styling")
	FLinearColor ActiveCardColor = FLinearColor(0.04f, 0.52f, 1.0f, 1.f);

	/** Edge length of the preview picture on a card, in slate units. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RoomPlanner|Tile Catalog|Styling")
	float PreviewSize = 160.f;

protected:
	void BuildLayout();
	void RebuildCards();
	void RefreshActiveState();
	void HandleCardClicked(FName TileID);

	UFUNCTION()
	void OnBackClicked();

private:
	UPROPERTY(Transient)
	TObjectPtr<UWrapBox> CardBox;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> ActiveTileText;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UPlannerStyleButton>> CardButtons;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UImage>> CardImages;

	UPROPERTY(Transient)
	TObjectPtr<UUserWidget> ParentCallingWidget;

	TArray<FPlannerCatalogEntry> Tiles;
	FName ActiveTileID;
	bool bCardsEnabled = true;
};
