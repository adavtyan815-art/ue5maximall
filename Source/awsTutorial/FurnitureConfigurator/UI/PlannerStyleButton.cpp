// Copyright 2026 MaxiMall. All Rights Reserved.

#include "FurnitureConfigurator/UI/PlannerStyleButton.h"

void UPlannerStyleButton::BindClick()
{
	OnClicked.AddUniqueDynamic(this, &UPlannerStyleButton::HandleClicked);
}

void UPlannerStyleButton::HandleClicked()
{
	OnStyleClicked.ExecuteIfBound(StyleID);
}
