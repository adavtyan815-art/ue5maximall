// Copyright 2026 MaxiMall. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Styling/SlateBrush.h"
#include "MaxiUiDesignTools.generated.h"

class UFont;
class UFontFace;
class UUserWidget;

/**
 * Editor-only helpers for the scripted UMG restyle of the login and room-selection widgets (design handoff 2026-09-27).
 * Editor Python cannot reach these parts of the engine: the composite font data of a UFont and the widget GUID table of a
 * widget blueprint. Not used at runtime.
 */
UCLASS()
class AWSTUTORIAL_API UMaxiUiDesignTools : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	/**
	 * Makes Font a runtime composite font. The default typeface gets one entry per name from DefaultFaces; a sub-font named
	 * SubFontName gets the entries from SubFontFaces and serves the code point ranges SubFontRanges (X = first, Y = last).
	 * The three arrays of faces and names must have the same length.
	 */
	UFUNCTION(BlueprintCallable, Category = "MaxiUI|Editor")
	static bool SetupCompositeFont(UFont* Font, const TArray<FName>& TypefaceNames, const TArray<UFontFace*>& DefaultFaces,
		const TArray<UFontFace*>& SubFontFaces, const TArray<FIntPoint>& SubFontRanges, FName SubFontName);

	/**
	 * Gives every widget of the blueprint's widget tree an entry in its variable GUID table. The UMG designer does this when a
	 * widget is added by hand; widgets added by script have none, and the blueprint compiler reports each of them with an ensure.
	 * bMakeNewWidgetsNonVariable clears "Is Variable" on those widgets, so layout-only widgets add no Blueprint variables.
	 * Returns the number of entries added.
	 */
	UFUNCTION(BlueprintCallable, Category = "MaxiUI|Editor")
	static int32 RegisterMissingWidgetGuids(UObject* WidgetBlueprint, bool bMakeNewWidgetsNonVariable);

	/** Returns Brush with its image size set. Editor Python cannot write FSlateBrush::ImageSize directly. */
	UFUNCTION(BlueprintPure, Category = "MaxiUI|Editor")
	static FSlateBrush SetBrushImageSize(const FSlateBrush& Brush, FVector2D ImageSize);

	/**
	 * Creates WidgetClass in the world of WorldContextObject, draws it offscreen at Size over BackgroundColor and writes the
	 * result to a PNG file. Widgets named in ShowWidgets / HideWidgets are made visible / collapsed first, to pick a state.
	 */
	UFUNCTION(BlueprintCallable, Category = "MaxiUI|Editor", meta = (WorldContext = "WorldContextObject"))
	static bool RenderWidgetToPng(UObject* WorldContextObject, TSubclassOf<UUserWidget> WidgetClass, FIntPoint Size, const FString& FilePath,
		FLinearColor BackgroundColor, const TArray<FName>& ShowWidgets, const TArray<FName>& HideWidgets);
#endif
};
