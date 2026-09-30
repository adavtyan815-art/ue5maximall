// Copyright 2026 MaxiMall. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Styling/SlateTypes.h"
#include "MaxiPanelGroup.generated.h"

/**
 * Invisible helper placed in a user widget: keeps at most one of the named panels open and highlights the menu button of
 * the open panel. The menu of BP_Online_Offline_Selection_UI only makes its panels visible and never hides the others;
 * with glass panels that would stack them. No Blueprint graph logic is needed: the group watches the panels' visibility.
 */
UCLASS()
class AWSTUTORIAL_API UMaxiPanelGroup : public UUserWidget
{
	GENERATED_BODY()

public:
	/** Widgets of the owning user widget, found by name, of which only one may be visible. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Panel Group")
	TArray<FName> PanelNames;

	/** Menu button per panel (same order as PanelNames, None = no highlight) shown in its hovered look while the panel is open. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Panel Group")
	TArray<FName> HighlightButtonNames;

protected:
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

private:
	void SetHighlighted(int32 PanelIndex, bool bHighlighted);

	TArray<bool> LastVisible;
	TMap<FName, FButtonStyle> OriginalStyles;
};
