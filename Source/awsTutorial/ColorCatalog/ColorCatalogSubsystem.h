// Copyright awsTutorial. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "ColorCatalogTypes.h"
#include "ColorCatalogSubsystem.generated.h"

/**
 * Subsystem responsible for loading, parsing, and caching RAL & NCS JSON color catalogs.
 */
UCLASS()
class AWSTUTORIAL_API UColorCatalogSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	/** Get all RAL Classic colors */
	UFUNCTION(BlueprintPure, Category = "Color Catalog Subsystem")
	const TArray<FColorCatalogItem>& GetRALColors() const { return RALColors; }

	/** Get all NCS System colors */
	UFUNCTION(BlueprintPure, Category = "Color Catalog Subsystem")
	const TArray<FColorCatalogItem>& GetNCSColors() const { return NCSColors; }

	/** Filter colors by catalog type, category orb, and optional search query */
	UFUNCTION(BlueprintCallable, Category = "Color Catalog Subsystem")
	TArray<FColorCatalogItem> FilterColors(EColorCatalogType CatalogType, EColorShadeCategory Category, const FString& SearchQuery);

private:
	TArray<FColorCatalogItem> RALColors;
	TArray<FColorCatalogItem> NCSColors;

	void LoadRALCatalog();
	void LoadNCSCatalog();

	EColorShadeCategory MapRALFamilyToCategory(const FString& FamilyName, int32 ColorNumber, float Brightness);
	EColorShadeCategory MapNCSHueToCategory(const FString& HueString, int32 Blackness, int32 Chromaticness);
};

