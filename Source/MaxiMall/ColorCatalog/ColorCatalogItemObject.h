// Copyright MaxiMall. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "ColorCatalogTypes.h"
#include "ColorCatalogItemObject.generated.h"

/**
 * UObject data wrapper passed to UTileView for virtualized scrolling.
 */
UCLASS(BlueprintType)
class MAXIMALL_API UColorCatalogItemObject : public UObject
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Color Catalog Item")
	FColorCatalogItem ColorItem;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Color Catalog Item")
	bool bIsSelected = false;

	UFUNCTION(BlueprintCallable, Category = "Color Catalog Item")
	void Init(const FColorCatalogItem& InColorItem, bool bInIsSelected = false)
	{
		ColorItem = InColorItem;
		bIsSelected = bInIsSelected;
	}
};

