// Copyright 2026 MaxiMall. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/Button.h"
#include "PlannerStyleButton.generated.h"

DECLARE_DELEGATE_OneParam(FOnPlannerStyleButtonClicked, FName /*StyleID*/);

/** Button carrying a door / window style ID; the planner widget builds these at runtime inside its StyleRow. */
UCLASS()
class AWSTUTORIAL_API UPlannerStyleButton : public UButton
{
	GENERATED_BODY()

public:
	FName StyleID;

	FOnPlannerStyleButtonClicked OnStyleClicked;

	/** Routes the button's OnClicked to OnStyleClicked (call once after construction). */
	void BindClick();

private:
	UFUNCTION()
	void HandleClicked();
};
