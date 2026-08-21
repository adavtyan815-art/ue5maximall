// Copyright awsTutorial. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "ColorCatalogTypes.generated.h"

/**
 * Shade categories matching the 9 UI Category Orbs.
 */
UENUM(BlueprintType)
enum class EColorShadeCategory : uint8
{
	All       UMETA(DisplayName = "Р’СЃРµ"),
	Red       UMETA(DisplayName = "РљСЂР°СЃРЅС‹Рµ"),
	Orange    UMETA(DisplayName = "РћСЂР°РЅР¶РµРІС‹Рµ"),
	Yellow    UMETA(DisplayName = "Р–РµР»С‚С‹Рµ"),
	Green     UMETA(DisplayName = "Р—РµР»РµРЅС‹Рµ"),
	Blue      UMETA(DisplayName = "РЎРёРЅРёРµ"),
	Violet    UMETA(DisplayName = "Р¤РёРѕР»РµС‚РѕРІС‹Рµ"),
	Grey      UMETA(DisplayName = "РЎРµСЂС‹Рµ"),
	White     UMETA(DisplayName = "Р‘РµР»С‹Рµ"),
	Brown     UMETA(DisplayName = "РљРѕСЂРёС‡РЅРµРІС‹Рµ"),
	Beige     UMETA(DisplayName = "Р‘РµР¶РµРІС‹Рµ"),
	Black     UMETA(DisplayName = "Р§РµСЂРЅС‹Рµ"),
	Neutral   UMETA(DisplayName = "РќРµР№С‚СЂР°Р»СЊРЅС‹Рµ")
};

/**
 * Catalog Type (RAL vs NCS)
 */
UENUM(BlueprintType)
enum class EColorCatalogType : uint8
{
	RAL       UMETA(DisplayName = "RAL Classic"),
	NCS       UMETA(DisplayName = "NCS System")
};

/**
 * Struct representing a single color entry from JSON catalogs.
 */
USTRUCT(BlueprintType)
struct AWSTUTORIAL_API FColorCatalogItem
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Color Catalog")
	FString Code; // e.g. "RAL 6018" or "S 1080-Y90R"

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Color Catalog")
	FString Name; // e.g. "Yellow green"

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Color Catalog")
	FLinearColor Color = FLinearColor::White; // RGBA Normalized Linear Color

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Color Catalog")
	FString HexCode; // e.g. "#57A639"

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Color Catalog")
	EColorShadeCategory Category = EColorShadeCategory::All;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Color Catalog")
	EColorCatalogType CatalogType = EColorCatalogType::RAL;
};

