// Copyright 2026 MaxiMall. All Rights Reserved.

#include "PlannerOpeningStyles.h"

namespace
{
	const FLinearColor WhiteFrame(0.86f, 0.85f, 0.82f);
	const FLinearColor WhiteLeaf(0.82f, 0.81f, 0.78f);
	const FLinearColor OakFrame(0.34f, 0.21f, 0.11f);
	const FLinearColor OakLeaf(0.38f, 0.24f, 0.13f);
	const FLinearColor Graphite(0.045f, 0.047f, 0.05f);
	const FLinearColor MetalGraphite(0.035f, 0.035f, 0.037f);
	const FLinearColor MetalLight(0.55f, 0.55f, 0.56f);

	FPlannerOpeningStyle MakeStyle(const TCHAR* ID, const TCHAR* Name, EOpeningType Type, EPlannerLeafDesign Design,
	                               const FLinearColor& Frame, const FLinearColor& Leaf, const FLinearColor& Metal)
	{
		FPlannerOpeningStyle Style;
		Style.ID = FName(ID);
		Style.DisplayName = Name;
		Style.Type = Type;
		Style.LeafDesign = Design;
		Style.FrameColor = Frame;
		Style.LeafColor = Leaf;
		Style.MetalColor = Metal;
		Style.bThreshold = (Type == EOpeningType::Door);
		return Style;
	}

	TArray<FPlannerOpeningStyle> BuildCatalog()
	{
		TArray<FPlannerOpeningStyle> Styles;

		// Doors (first door entry = default)
		Styles.Add(MakeStyle(TEXT("Door_WhiteFlush"), TEXT("Белая гладкая"), EOpeningType::Door, EPlannerLeafDesign::Flush, WhiteFrame, WhiteLeaf, MetalGraphite));
		Styles.Add(MakeStyle(TEXT("Door_WhitePanel"), TEXT("Белая с филёнками"), EOpeningType::Door, EPlannerLeafDesign::TwoPanel, WhiteFrame, WhiteLeaf, MetalGraphite));
		Styles.Add(MakeStyle(TEXT("Door_OakPanel"), TEXT("Дуб с филёнками"), EOpeningType::Door, EPlannerLeafDesign::TwoPanel, OakFrame, OakLeaf, MetalGraphite));
		Styles.Add(MakeStyle(TEXT("Door_GlazedTop"), TEXT("Со стеклянной вставкой"), EOpeningType::Door, EPlannerLeafDesign::GlazedTop, WhiteFrame, WhiteLeaf, MetalGraphite));
		{
			FPlannerOpeningStyle Frosted = MakeStyle(TEXT("Door_GraphiteFrosted"), TEXT("Графит, матовое стекло"), EOpeningType::Door, EPlannerLeafDesign::FullGlass, Graphite, Graphite, MetalLight);
			Frosted.GlassKind = EPlannerOpeningMaterial::FrostedGlass;
			Styles.Add(Frosted);
		}

		// Windows (first window entry = default)
		Styles.Add(MakeStyle(TEXT("Window_White"), TEXT("Белое"), EOpeningType::Window, EPlannerLeafDesign::Flush, WhiteFrame, WhiteLeaf, MetalGraphite));
		Styles.Add(MakeStyle(TEXT("Window_Graphite"), TEXT("Графит"), EOpeningType::Window, EPlannerLeafDesign::Flush, Graphite, Graphite, MetalLight));
		Styles.Add(MakeStyle(TEXT("Window_Oak"), TEXT("Дуб"), EOpeningType::Window, EPlannerLeafDesign::Flush, OakFrame, OakLeaf, MetalGraphite));

		// Archways (first archway entry = default)
		Styles.Add(MakeStyle(TEXT("Archway_White"), TEXT("Портал белый"), EOpeningType::Archway, EPlannerLeafDesign::Flush, WhiteFrame, WhiteLeaf, MetalGraphite));
		Styles.Add(MakeStyle(TEXT("Archway_Oak"), TEXT("Портал дуб"), EOpeningType::Archway, EPlannerLeafDesign::Flush, OakFrame, OakLeaf, MetalGraphite));
		{
			FPlannerOpeningStyle Plain = MakeStyle(TEXT("Archway_Plain"), TEXT("Без наличников"), EOpeningType::Archway, EPlannerLeafDesign::Flush, WhiteFrame, WhiteLeaf, MetalGraphite);
			Plain.bLining = false;
			Plain.bCasing = false;
			Styles.Add(Plain);
		}
		return Styles;
	}
}

const TArray<FPlannerOpeningStyle>& PlannerOpeningStyles::All()
{
	static const TArray<FPlannerOpeningStyle> Catalog = BuildCatalog();
	return Catalog;
}

const FPlannerOpeningStyle* PlannerOpeningStyles::Find(FName StyleID)
{
	if (StyleID.IsNone()) return nullptr;
	for (const FPlannerOpeningStyle& Style : All())
	{
		if (Style.ID == StyleID) return &Style;
	}
	return nullptr;
}

const FPlannerOpeningStyle& PlannerOpeningStyles::GetDefault(EOpeningType Type)
{
	for (const FPlannerOpeningStyle& Style : All())
	{
		if (Style.Type == Type) return Style;
	}
	return All()[0];
}

const FPlannerOpeningStyle& PlannerOpeningStyles::Resolve(EOpeningType Type, FName StyleID)
{
	const FPlannerOpeningStyle* Style = Find(StyleID);
	return (Style && Style->Type == Type) ? *Style : GetDefault(Type);
}

bool PlannerOpeningStyles::IsValidFor(EOpeningType Type, FName StyleID)
{
	const FPlannerOpeningStyle* Style = Find(StyleID);
	return Style && Style->Type == Type;
}
