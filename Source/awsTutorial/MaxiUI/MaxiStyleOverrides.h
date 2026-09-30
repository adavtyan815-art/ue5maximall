// Copyright 2026 MaxiMall. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Styling/SlateBrush.h"
#include "MaxiStyleOverrides.generated.h"

/**
 * Invisible helper placed in a user widget whose code builds parts of its own layout with fixed brushes or opens widgets
 * of a native class (the room planner): applies the design-system look to those parts without touching that code.
 *  - Borders named in FrameNames (found by name, also ones created at runtime) get FrameBrush and FramePadding.
 *  - The class default object of DefaultsClassPath gets DefaultColors (FLinearColor properties by name) on construct,
 *    so widgets of that class created later use them.
 */
UCLASS()
class AWSTUTORIAL_API UMaxiStyleOverrides : public UUserWidget
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Style Overrides")
	TArray<FName> FrameNames;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Style Overrides")
	FSlateBrush FrameBrush;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Style Overrides")
	FMargin FramePadding;

	/** Script path of a native class, e.g. /Script/awsTutorial.PlannerTileCatalogWidget. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Style Overrides")
	FString DefaultsClassPath;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Style Overrides")
	TMap<FName, FLinearColor> DefaultColors;

protected:
	virtual void NativeConstruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
};
