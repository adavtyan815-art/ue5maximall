// Copyright 2026 MaxiMall. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/Button.h"
#include "MaxiVisibilityToggleButton.generated.h"

/**
 * Button that shows or hides another widget of the same user widget, found by name. It is the ≡ menu button of
 * BP_Online_Offline_Selection_UI (design handoff 2026-09-27): the menu panel opens and closes without Blueprint graph logic.
 */
UCLASS()
class AWSTUTORIAL_API UMaxiVisibilityToggleButton : public UButton
{
	GENERATED_BODY()

public:
	/** Name of the widget, in the same user widget, whose visibility the button toggles. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Toggle")
	FName TargetWidgetName;

	/** Visibility given to the target when the button shows it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Toggle")
	ESlateVisibility ShownVisibility = ESlateVisibility::SelfHitTestInvisible;

	/** Shows the target when it is hidden or collapsed, hides it otherwise. */
	UFUNCTION(BlueprintCallable, Category = "Toggle")
	void ToggleTarget();

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;

private:
	UFUNCTION()
	void HandleClicked();
};
