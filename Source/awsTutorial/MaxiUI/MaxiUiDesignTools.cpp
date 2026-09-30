// Copyright 2026 MaxiMall. All Rights Reserved.

#include "MaxiUI/MaxiUiDesignTools.h"

#if WITH_EDITOR

#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "Engine/FontFace.h"
#include "Engine/TextureRenderTarget2D.h"
#include "HAL/FileManager.h"
#include "ImageUtils.h"
#include "RenderingThread.h"
#include "ShaderCompiler.h"
#include "Slate/WidgetRenderer.h"
#include "Styling/CoreStyle.h"
#include "UObject/UnrealType.h"
#include "Widgets/Layout/SBorder.h"

bool UMaxiUiDesignTools::SetupCompositeFont(UFont* Font, const TArray<FName>& TypefaceNames, const TArray<UFontFace*>& DefaultFaces,
	const TArray<UFontFace*>& SubFontFaces, const TArray<FIntPoint>& SubFontRanges, FName SubFontName)
{
	if (!Font || TypefaceNames.Num() == 0 || TypefaceNames.Num() != DefaultFaces.Num() || TypefaceNames.Num() != SubFontFaces.Num())
	{
		return false;
	}

	Font->Modify();
	Font->FontCacheType = EFontCacheType::Runtime;

	FCompositeFont& Composite = Font->CompositeFont;
	Composite.DefaultTypeface.Fonts.Reset();
	Composite.SubTypefaces.Reset();

	FCompositeSubFont& SubFont = Composite.SubTypefaces.AddDefaulted_GetRef();
	SubFont.EditorName = SubFontName;
	for (const FIntPoint& Range : SubFontRanges)
	{
		SubFont.CharacterRanges.Add(FInt32Range(FInt32Range::BoundsType::Inclusive(Range.X), FInt32Range::BoundsType::Inclusive(Range.Y)));
	}

	for (int32 Index = 0; Index < TypefaceNames.Num(); ++Index)
	{
		if (!DefaultFaces[Index] || !SubFontFaces[Index])
		{
			return false;
		}
		Composite.DefaultTypeface.Fonts.Emplace_GetRef(TypefaceNames[Index]).Font = FFontData(DefaultFaces[Index]);
		SubFont.Typeface.Fonts.Emplace_GetRef(TypefaceNames[Index]).Font = FFontData(SubFontFaces[Index]);
	}

	Font->PostEditChange();
	Font->MarkPackageDirty();
	return true;
}

int32 UMaxiUiDesignTools::RegisterMissingWidgetGuids(UObject* WidgetBlueprint, bool bMakeNewWidgetsNonVariable)
{
	if (!WidgetBlueprint)
	{
		return 0;
	}

	// UWidgetBlueprint lives in an editor module; its two properties are reached through reflection.
	FMapProperty* GuidMapProperty = FindFProperty<FMapProperty>(WidgetBlueprint->GetClass(), TEXT("WidgetVariableNameToGuidMap"));
	FObjectProperty* TreeProperty = FindFProperty<FObjectProperty>(WidgetBlueprint->GetClass(), TEXT("WidgetTree"));
	if (!GuidMapProperty || !TreeProperty || !GuidMapProperty->KeyProp->IsA<FNameProperty>())
	{
		UE_LOG(LogTemp, Error, TEXT("RegisterMissingWidgetGuids: %s is not a widget blueprint"), *WidgetBlueprint->GetPathName());
		return 0;
	}

	UWidgetTree* WidgetTree = Cast<UWidgetTree>(TreeProperty->GetObjectPropertyValue_InContainer(WidgetBlueprint));
	if (!WidgetTree)
	{
		return 0;
	}

	FScriptMapHelper GuidMap(GuidMapProperty, GuidMapProperty->ContainerPtrToValuePtr<void>(WidgetBlueprint));
	TSet<FName> KnownNames;
	for (FScriptMapHelper::FIterator It = GuidMap.CreateIterator(); It; ++It)
	{
		KnownNames.Add(*reinterpret_cast<const FName*>(GuidMap.GetKeyPtr(It)));
	}

	TArray<FName> MissingNames;
	WidgetTree->ForEachWidget([&KnownNames, &MissingNames, bMakeNewWidgetsNonVariable](UWidget* Widget)
	{
		if (Widget && !KnownNames.Contains(Widget->GetFName()))
		{
			KnownNames.Add(Widget->GetFName());
			MissingNames.Add(Widget->GetFName());
			if (bMakeNewWidgetsNonVariable)
			{
				Widget->bIsVariable = false;
			}
		}
	});

	if (MissingNames.Num() > 0)
	{
		WidgetBlueprint->Modify();
		for (FName Name : MissingNames)
		{
			FGuid Guid = FGuid::NewGuid();
			GuidMap.AddPair(&Name, &Guid);
		}
	}
	return MissingNames.Num();
}

FSlateBrush UMaxiUiDesignTools::SetBrushImageSize(const FSlateBrush& Brush, FVector2D ImageSize)
{
	FSlateBrush Result = Brush;
	Result.ImageSize = ImageSize;
	return Result;
}

bool UMaxiUiDesignTools::RenderWidgetToPng(UObject* WorldContextObject, TSubclassOf<UUserWidget> WidgetClass, FIntPoint Size, const FString& FilePath,
	FLinearColor BackgroundColor, const TArray<FName>& ShowWidgets, const TArray<FName>& HideWidgets)
{
	UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull) : nullptr;
	if (!World || !*WidgetClass || Size.X <= 0 || Size.Y <= 0)
	{
		return false;
	}

	UUserWidget* Widget = CreateWidget<UUserWidget>(World, WidgetClass);
	if (!Widget)
	{
		return false;
	}
	for (FName Name : ShowWidgets)
	{
		if (UWidget* Target = Widget->GetWidgetFromName(Name))
		{
			Target->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
		}
	}
	for (FName Name : HideWidgets)
	{
		if (UWidget* Target = Widget->GetWidgetFromName(Name))
		{
			Target->SetVisibility(ESlateVisibility::Collapsed);
		}
	}

	// Slate draws linear colour here; the sRGB target encodes it once, so the PNG holds the colours a player sees.
	const FVector2D DrawSize(Size.X, Size.Y);
	UTextureRenderTarget2D* Target = FWidgetRenderer::CreateTargetFor(DrawSize, TF_Bilinear, true);
	FWidgetRenderer Renderer(false, true);
	const TSharedRef<SWidget> SlateWidget = SNew(SBorder)
		.Padding(0.f)
		.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
		.BorderBackgroundColor(BackgroundColor)
		[
			Widget->TakeWidget()
		];
	// The first passes lay out the widget, load font glyphs and queue the shaders of UI materials (icons); those shaders
	// are finished before the last pass, which is written out.
	for (int32 Pass = 0; Pass < 3; ++Pass)
	{
		Renderer.DrawWidget(Target, SlateWidget, DrawSize, 0.f);
		FlushRenderingCommands();
		if (GShaderCompilingManager)
		{
			GShaderCompilingManager->FinishAllCompilation();
		}
	}

	const TUniquePtr<FArchive> File(IFileManager::Get().CreateFileWriter(*FilePath));
	return File && FImageUtils::ExportRenderTarget2DAsPNG(Target, *File);
}

#endif // WITH_EDITOR
