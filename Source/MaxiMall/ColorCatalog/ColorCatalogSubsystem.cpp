// Copyright MaxiMall. All Rights Reserved.

#include "ColorCatalog/ColorCatalogSubsystem.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "Dom/JsonObject.h"

void UColorCatalogSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	LoadRALCatalog();
	LoadNCSCatalog();
}

static bool LoadColorJsonFile(const FString& RelFileName, FString& OutJsonString)
{
	FString BaseDir = FPlatformProcess::BaseDir();
	FString ContentDir = FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir());
	FString ProjDir = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());

	TArray<FString> SearchPaths;
	SearchPaths.Add(FPaths::Combine(BaseDir, TEXT("../../Content/Data/Colors"), RelFileName));
	SearchPaths.Add(FPaths::Combine(BaseDir, TEXT("../../../Content/Data/Colors"), RelFileName));
	SearchPaths.Add(FPaths::Combine(BaseDir, TEXT("../../Content/data/colors"), RelFileName));
	SearchPaths.Add(FPaths::Combine(ContentDir, TEXT("Data/Colors"), RelFileName));
	SearchPaths.Add(FPaths::Combine(ContentDir, TEXT("data/colors"), RelFileName));
	SearchPaths.Add(FPaths::Combine(ProjDir, TEXT("Content/Data/Colors"), RelFileName));
	SearchPaths.Add(FPaths::Combine(ProjDir, TEXT("Data/Colors"), RelFileName));
	SearchPaths.Add(FPaths::ProjectContentDir() / TEXT("Data/Colors") / RelFileName);
	SearchPaths.Add(FPaths::ProjectDir() / TEXT("Content/Data/Colors") / RelFileName);
	SearchPaths.Add(FString(TEXT("/home/ssm-user/client/awsTutorial/Content/Data/Colors/")) + RelFileName);
	SearchPaths.Add(FString(TEXT("/home/ubuntu/client/awsTutorial/Content/Data/Colors/")) + RelFileName);
	SearchPaths.Add(FString(TEXT("/local/game/awsTutorial/Content/Data/Colors/")) + RelFileName);

	for (FString& Path : SearchPaths)
	{
		FPaths::NormalizeFilename(Path);
		FPaths::CollapseRelativeDirectories(Path);

		if (FFileHelper::LoadFileToString(OutJsonString, *Path) && !OutJsonString.IsEmpty())
		{
			UE_LOG(LogTemp, Warning, TEXT("[ColorCatalogSubsystem] SUCCESS: Loaded color catalog from: %s (Chars: %d)"), *Path, OutJsonString.Len());
			return true;
		}
		else
		{
			UE_LOG(LogTemp, Verbose, TEXT("[ColorCatalogSubsystem] Tried path: %s (not found)"), *Path);
		}
	}

	UE_LOG(LogTemp, Error, TEXT("[ColorCatalogSubsystem] Failed to locate '%s' in any known content or staging paths."), *RelFileName);
	return false;
}

void UColorCatalogSubsystem::LoadRALCatalog()
{
	RALColors.Reset();

	FString JsonString;
	if (!LoadColorJsonFile(TEXT("ral_classic.json"), JsonString))
	{
		return;
	}

	TSharedPtr<FJsonObject> RootObject;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);

	if (FJsonSerializer::Deserialize(Reader, RootObject) && RootObject.IsValid())
	{
		const TArray<TSharedPtr<FJsonValue>>* ColorsArray;
		if (RootObject->TryGetArrayField(TEXT("colors"), ColorsArray))
		{
			for (const TSharedPtr<FJsonValue>& Value : *ColorsArray)
			{
				TSharedPtr<FJsonObject> ColorObj = Value->AsObject();
				if (!ColorObj.IsValid()) continue;

				FColorCatalogItem Item;
				Item.Code = ColorObj->GetStringField(TEXT("code"));
				Item.Name = ColorObj->GetStringField(TEXT("name"));
				Item.HexCode = ColorObj->GetStringField(TEXT("hex"));
				Item.CatalogType = EColorCatalogType::RAL;

				const TSharedPtr<FJsonObject>* RgbObj;
				if (ColorObj->TryGetObjectField(TEXT("rgb"), RgbObj))
				{
					int32 R = FMath::Clamp((*RgbObj)->GetIntegerField(TEXT("r")), 0, 255);
					int32 G = FMath::Clamp((*RgbObj)->GetIntegerField(TEXT("g")), 0, 255);
					int32 B = FMath::Clamp((*RgbObj)->GetIntegerField(TEXT("b")), 0, 255);
					Item.Color = FLinearColor::FromSRGBColor(FColor(R, G, B, 255));
				}

				FString FamilyName = ColorObj->GetStringField(TEXT("family"));
				int32 ColorNum = ColorObj->GetIntegerField(TEXT("number"));
				float Brightness = (Item.Color.R + Item.Color.G + Item.Color.B) / 3.0f;

				Item.Category = MapRALFamilyToCategory(FamilyName, ColorNum, Brightness);
				RALColors.Add(Item);
			}
		}
	}

	UE_LOG(LogTemp, Log, TEXT("[ColorCatalogSubsystem] Successfully loaded %d RAL colors."), RALColors.Num());
}

void UColorCatalogSubsystem::LoadNCSCatalog()
{
	NCSColors.Reset();

	FString JsonString;
	if (!LoadColorJsonFile(TEXT("ncscolorguide_2052_ui_sorted.json"), JsonString))
	{
		return;
	}

	TSharedPtr<FJsonObject> RootObject;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);

	if (FJsonSerializer::Deserialize(Reader, RootObject) && RootObject.IsValid())
	{
		const TArray<TSharedPtr<FJsonValue>>* ColorsArray;
		if (RootObject->TryGetArrayField(TEXT("colors"), ColorsArray))
		{
			for (const TSharedPtr<FJsonValue>& Value : *ColorsArray)
			{
				TSharedPtr<FJsonObject> ColorObj = Value->AsObject();
				if (!ColorObj.IsValid()) continue;

				FColorCatalogItem Item;
				Item.Code = ColorObj->GetStringField(TEXT("ncs"));
				Item.Name = Item.Code;
				Item.HexCode = ColorObj->GetStringField(TEXT("hex"));
				Item.CatalogType = EColorCatalogType::NCS;

				const TArray<TSharedPtr<FJsonValue>>* RgbArr;
				if (ColorObj->TryGetArrayField(TEXT("rgb"), RgbArr) && RgbArr->Num() >= 3)
				{
					int32 R = FMath::Clamp(FMath::RoundToInt((*RgbArr)[0]->AsNumber()), 0, 255);
					int32 G = FMath::Clamp(FMath::RoundToInt((*RgbArr)[1]->AsNumber()), 0, 255);
					int32 B = FMath::Clamp(FMath::RoundToInt((*RgbArr)[2]->AsNumber()), 0, 255);
					Item.Color = FLinearColor::FromSRGBColor(FColor(R, G, B, 255));
				}

				FString FamilyName;
				if (ColorObj->TryGetStringField(TEXT("family"), FamilyName))
				{
					float Brightness = (Item.Color.R + Item.Color.G + Item.Color.B) / 3.0f;
					Item.Category = MapRALFamilyToCategory(FamilyName, 0, Brightness);
				}
				else
				{
					FString Hue = TEXT("N");
					int32 Blackness = 0;
					int32 Chromaticness = 0;

					const TSharedPtr<FJsonObject>* NcsSortObj;
					if (ColorObj->TryGetObjectField(TEXT("ncs_sort"), NcsSortObj))
					{
						Hue = (*NcsSortObj)->GetStringField(TEXT("hue"));
						Blackness = (*NcsSortObj)->GetIntegerField(TEXT("blackness"));
						Chromaticness = (*NcsSortObj)->GetIntegerField(TEXT("chromaticness"));
					}
					Item.Category = MapNCSHueToCategory(Hue, Blackness, Chromaticness);
				}

				NCSColors.Add(Item);
			}
		}
	}

	UE_LOG(LogTemp, Log, TEXT("[RoomPlannerColorSubsystem] Successfully loaded %d NCS colors."), NCSColors.Num());
}

EColorShadeCategory UColorCatalogSubsystem::MapRALFamilyToCategory(const FString& FamilyName, int32 ColorNumber, float Brightness)
{
	if (FamilyName.Equals(TEXT("Yellow"), ESearchCase::IgnoreCase)) return EColorShadeCategory::Yellow;
	if (FamilyName.Equals(TEXT("Orange"), ESearchCase::IgnoreCase)) return EColorShadeCategory::Orange;
	if (FamilyName.Equals(TEXT("Red"), ESearchCase::IgnoreCase)) return EColorShadeCategory::Red;
	if (FamilyName.Equals(TEXT("Violet"), ESearchCase::IgnoreCase) || FamilyName.Equals(TEXT("Purple"), ESearchCase::IgnoreCase)) return EColorShadeCategory::Violet;
	if (FamilyName.Equals(TEXT("Blue"), ESearchCase::IgnoreCase)) return EColorShadeCategory::Blue;
	if (FamilyName.Equals(TEXT("Green"), ESearchCase::IgnoreCase)) return EColorShadeCategory::Green;
	if (FamilyName.Equals(TEXT("Grey"), ESearchCase::IgnoreCase)) return EColorShadeCategory::Grey;
	if (FamilyName.Equals(TEXT("Brown"), ESearchCase::IgnoreCase)) return EColorShadeCategory::Brown;
	if (FamilyName.Equals(TEXT("Beige"), ESearchCase::IgnoreCase)) return EColorShadeCategory::Beige;
	if (FamilyName.Equals(TEXT("Neutral"), ESearchCase::IgnoreCase)) return EColorShadeCategory::Neutral;

	if (FamilyName.Contains(TEXT("White")) || FamilyName.Contains(TEXT("Black")))
	{
		if (Brightness > 0.65f) return EColorShadeCategory::White;
		if (Brightness < 0.35f) return EColorShadeCategory::Black;
		return EColorShadeCategory::Grey;
	}

	return EColorShadeCategory::All;
}

EColorShadeCategory UColorCatalogSubsystem::MapNCSHueToCategory(const FString& HueString, int32 Blackness, int32 Chromaticness)
{
	if (HueString.Equals(TEXT("N"), ESearchCase::IgnoreCase))
	{
		if (Blackness <= 15) return EColorShadeCategory::White;
		if (Blackness >= 75) return EColorShadeCategory::Black;
		return EColorShadeCategory::Grey;
	}

	if (HueString.StartsWith(TEXT("R"))) return EColorShadeCategory::Red;
	if (HueString.StartsWith(TEXT("B"))) return EColorShadeCategory::Blue;
	if (HueString.StartsWith(TEXT("G"))) return EColorShadeCategory::Green;
	if (HueString.StartsWith(TEXT("Y")))
	{
		if (Blackness >= 40 && Chromaticness <= 40) return EColorShadeCategory::Brown;
		return EColorShadeCategory::Yellow;
	}

	return EColorShadeCategory::All;
}

static FString NormalizeSearchString(const FString& InStr)
{
	FString Cleaned = InStr.ToLower();
	Cleaned.ReplaceInline(TEXT(" "), TEXT(""));
	Cleaned.ReplaceInline(TEXT("-"), TEXT(""));
	Cleaned.ReplaceInline(TEXT("_"), TEXT(""));
	return Cleaned;
}

TArray<FColorCatalogItem> UColorCatalogSubsystem::FilterColors(EColorCatalogType CatalogType, EColorShadeCategory Category, const FString& SearchQuery)
{
	const TArray<FColorCatalogItem>& SourceList = (CatalogType == EColorCatalogType::RAL) ? RALColors : NCSColors;

	FString NormalizedQuery = NormalizeSearchString(SearchQuery);

	TArray<FColorCatalogItem> Result;
	Result.Reserve(SourceList.Num());

	for (const FColorCatalogItem& Item : SourceList)
	{
		// 1. Category Filter
		if (Category != EColorShadeCategory::All && Item.Category != Category)
		{
			continue;
		}

		// 2. Ultra-Robust Fuzzy Search Filter
		if (!NormalizedQuery.IsEmpty())
		{
			FString NormCode = NormalizeSearchString(Item.Code);
			FString NormName = NormalizeSearchString(Item.Name);
			FString NormHex = NormalizeSearchString(Item.HexCode);

			bool bMatchesCode = NormCode.Contains(NormalizedQuery);
			bool bMatchesName = NormName.Contains(NormalizedQuery);
			bool bMatchesHex  = NormHex.Contains(NormalizedQuery);

			if (!bMatchesCode && !bMatchesName && !bMatchesHex)
			{
				continue;
			}
		}

		Result.Add(Item);
	}

	return Result;
}

