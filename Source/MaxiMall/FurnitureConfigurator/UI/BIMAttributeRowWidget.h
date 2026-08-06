// Copyright MaxiMall Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "BIMAttributeRowWidget.generated.h"

class UTextBlock;

/**
 * UBIMAttributeRowWidget
 * C++ parent class for WBP_BIMAttributeRow.
 */
UCLASS()
class MAXIMALL_API UBIMAttributeRowWidget : public UUserWidget
{
    GENERATED_BODY()

public:
    UFUNCTION(BlueprintCallable, Category = "BIM UI")
    void SetRowData(const FString& InLabel, const FString& InValue);

protected:
    UPROPERTY(meta = (BindWidgetOptional), BlueprintReadOnly, Category = "BIM UI")
    TObjectPtr<UTextBlock> Txt_Label;

    UPROPERTY(meta = (BindWidgetOptional), BlueprintReadOnly, Category = "BIM UI")
    TObjectPtr<UTextBlock> Txt_Value;
};
