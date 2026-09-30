// Copyright 2026 MaxiMall. All Rights Reserved.

#include "MaxiUI/MaxiStyleOverrides.h"

#include "Components/Border.h"
#include "UObject/UnrealType.h"

void UMaxiStyleOverrides::NativeConstruct()
{
	Super::NativeConstruct();

	if (DefaultsClassPath.IsEmpty() || DefaultColors.Num() == 0)
	{
		return;
	}
	UClass* Class = FindObject<UClass>(nullptr, *DefaultsClassPath);
	UObject* Defaults = Class ? Class->GetDefaultObject() : nullptr;
	if (!Defaults)
	{
		UE_LOG(LogTemp, Warning, TEXT("%s: class %s not found"), *GetName(), *DefaultsClassPath);
		return;
	}
	for (const TPair<FName, FLinearColor>& Pair : DefaultColors)
	{
		FStructProperty* Property = FindFProperty<FStructProperty>(Class, Pair.Key);
		if (Property && Property->Struct == TBaseStructure<FLinearColor>::Get())
		{
			*Property->ContainerPtrToValuePtr<FLinearColor>(Defaults) = Pair.Value;
		}
	}
}

void UMaxiStyleOverrides::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	UUserWidget* OwnerWidget = GetTypedOuter<UUserWidget>();
	if (!OwnerWidget)
	{
		return;
	}
	for (const FName& Name : FrameNames)
	{
		UBorder* Frame = Cast<UBorder>(OwnerWidget->GetWidgetFromName(Name));
		if (Frame && !(Frame->Background == FrameBrush))
		{
			Frame->SetBrush(FrameBrush);
			Frame->SetPadding(FramePadding);
		}
	}
}
