// Copyright 2026 MaxiMall. All Rights Reserved.

#include "MaxiUI/MaxiPanelGroup.h"

#include "Components/Button.h"

void UMaxiPanelGroup::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	UUserWidget* OwnerWidget = GetTypedOuter<UUserWidget>();
	if (!OwnerWidget)
	{
		return;
	}

	LastVisible.SetNumZeroed(PanelNames.Num());
	TArray<UWidget*> Panels;
	TArray<bool> Visible;
	int32 NewlyShown = INDEX_NONE;
	for (int32 Index = 0; Index < PanelNames.Num(); ++Index)
	{
		UWidget* Panel = OwnerWidget->GetWidgetFromName(PanelNames[Index]);
		const bool bVisible = Panel && Panel->IsVisible();
		Panels.Add(Panel);
		Visible.Add(bVisible);
		if (bVisible && !LastVisible[Index] && NewlyShown == INDEX_NONE)
		{
			NewlyShown = Index;
		}
	}

	// The panel opened last wins; the others close.
	if (NewlyShown != INDEX_NONE)
	{
		for (int32 Index = 0; Index < Panels.Num(); ++Index)
		{
			if (Index != NewlyShown && Visible[Index])
			{
				Panels[Index]->SetVisibility(ESlateVisibility::Collapsed);
				Visible[Index] = false;
			}
		}
	}

	for (int32 Index = 0; Index < Visible.Num(); ++Index)
	{
		if (Visible[Index] != LastVisible[Index])
		{
			SetHighlighted(Index, Visible[Index]);
		}
	}
	LastVisible = Visible;
}

void UMaxiPanelGroup::SetHighlighted(int32 PanelIndex, bool bHighlighted)
{
	if (!HighlightButtonNames.IsValidIndex(PanelIndex) || HighlightButtonNames[PanelIndex].IsNone())
	{
		return;
	}
	UUserWidget* OwnerWidget = GetTypedOuter<UUserWidget>();
	UButton* Button = OwnerWidget ? Cast<UButton>(OwnerWidget->GetWidgetFromName(HighlightButtonNames[PanelIndex])) : nullptr;
	if (!Button)
	{
		return;
	}

	const FName Name = HighlightButtonNames[PanelIndex];
	if (!OriginalStyles.Contains(Name))
	{
		OriginalStyles.Add(Name, Button->GetStyle());
	}
	FButtonStyle Style = OriginalStyles[Name];
	if (bHighlighted)
	{
		Style.Normal = Style.Hovered;
	}
	Button->SetStyle(Style);
}
