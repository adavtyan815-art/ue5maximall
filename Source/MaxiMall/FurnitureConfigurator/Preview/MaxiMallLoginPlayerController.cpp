// Copyright Epic Games, Inc. All Rights Reserved.

#include "FurnitureConfigurator/Preview/MaxiMallLoginPlayerController.h"
#include "Framework/Application/SlateApplication.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "HAL/PlatformApplicationMisc.h"

AMaxiMallLoginPlayerController::AMaxiMallLoginPlayerController()
{
	PrimaryActorTick.bCanEverTick = true;

	// Create default subobject for Pixel Streaming input
	PixelStreamingInput = CreateDefaultSubobject<UPixelStreamingInput>(TEXT("PixelStreamingInputComponent"));
}

void AMaxiMallLoginPlayerController::BeginPlay()
{
	Super::BeginPlay();

	if (IsLocalController())
	{
		if (PixelStreamingInput)
		{
			PixelStreamingInput->OnInputEvent.AddDynamic(this, &AMaxiMallLoginPlayerController::OnPixelStreamingInput);
		}
	}
}

void AMaxiMallLoginPlayerController::PlayerTick(float DeltaTime)
{
	Super::PlayerTick(DeltaTime);

	if (!IsLocalController())
	{
		return;
	}

	// Dynamically check for active Pixel Streaming Input components that are not our default subobject
	if (ActivePixelStreamingInput == nullptr)
	{
		TArray<UPixelStreamingInput*> InputComponents;
		GetComponents<UPixelStreamingInput>(InputComponents);
		for (UPixelStreamingInput* PSInput : InputComponents)
		{
			if (PSInput && PSInput != PixelStreamingInput)
			{
				ActivePixelStreamingInput = PSInput;
				ActivePixelStreamingInput->OnInputEvent.AddDynamic(this, &AMaxiMallLoginPlayerController::OnPixelStreamingInput);
				break;
			}
		}
	}
}

void AMaxiMallLoginPlayerController::OnPixelStreamingInput(const FString& Descriptor)
{
	TSharedPtr<FJsonObject> JsonObject;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Descriptor);

	if (FJsonSerializer::Deserialize(Reader, JsonObject) && JsonObject.IsValid())
	{
		if (JsonObject->HasField(TEXT("Cmd")) && JsonObject->GetStringField(TEXT("Cmd")) == TEXT("ClipboardPaste"))
		{
			FString PasteText = JsonObject->GetStringField(TEXT("Text"));
			
			// Remove null terminators
			PasteText.ReplaceInline(TEXT("\0"), TEXT(""));

			// Direct injection via Slate Character Events (bypasses headless OS clipboard limits)
			if (FSlateApplication::IsInitialized())
			{
				FSlateApplication& SlateApp = FSlateApplication::Get();
				for (int32 i = 0; i < PasteText.Len(); ++i)
				{
					FCharacterEvent CharEvent(PasteText[i], FModifierKeysState(), 0, false);
					SlateApp.ProcessKeyCharEvent(CharEvent);
				}
			}
		}
	}
}
