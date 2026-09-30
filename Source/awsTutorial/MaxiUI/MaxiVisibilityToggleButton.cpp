// Copyright 2026 MaxiMall. All Rights Reserved.

#include "MaxiUI/MaxiVisibilityToggleButton.h"

#include "Blueprint/UserWidget.h"

TSharedRef<SWidget> UMaxiVisibilityToggleButton::RebuildWidget()
{
	TSharedRef<SWidget> Widget = Super::RebuildWidget();
	if (!IsDesignTime())
	{
		OnClicked.AddUniqueDynamic(this, &UMaxiVisibilityToggleButton::HandleClicked);
	}
	return Widget;
}

void UMaxiVisibilityToggleButton::HandleClicked()
{
	ToggleTarget();
}

void UMaxiVisibilityToggleButton::ToggleTarget()
{
	UUserWidget* OwnerWidget = GetTypedOuter<UUserWidget>();
	UWidget* Target = OwnerWidget ? OwnerWidget->GetWidgetFromName(TargetWidgetName) : nullptr;
	if (!Target)
	{
		UE_LOG(LogTemp, Warning, TEXT("%s: widget '%s' not found in %s"), *GetName(), *TargetWidgetName.ToString(), *GetNameSafe(OwnerWidget));
		return;
	}
	Target->SetVisibility(Target->IsVisible() ? ESlateVisibility::Collapsed : ShownVisibility);
}
