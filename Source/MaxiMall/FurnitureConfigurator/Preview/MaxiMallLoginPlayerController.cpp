// Copyright Epic Games, Inc. All Rights Reserved.

#include "FurnitureConfigurator/Preview/MaxiMallLoginPlayerController.h"
#include "Framework/Application/SlateApplication.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "HAL/PlatformApplicationMisc.h"
#include "UObject/UObjectIterator.h"

// ─────────────────────────────────────────────────────────────────────────────
// Constructor
// ─────────────────────────────────────────────────────────────────────────────

AMaxiMallLoginPlayerController::AMaxiMallLoginPlayerController()
{
	PrimaryActorTick.bCanEverTick = true;
}

// ─────────────────────────────────────────────────────────────────────────────
// BeginPlay
// ─────────────────────────────────────────────────────────────────────────────

void AMaxiMallLoginPlayerController::BeginPlay()
{
	Super::BeginPlay();

	UE_LOG(LogTemp, Warning, TEXT("[MaxiMall|Login] ===== BeginPlay START =====  IsLocalController: %s"),
		IsLocalController() ? TEXT("YES") : TEXT("NO"));

	if (IsLocalController())
	{
		if (!CachedPSInput)
		{
			CachedPSInput = GetComponentByClass<UPixelStreamingInput>();
		}

		if (CachedPSInput)
		{
			CachedPSInput->OnInputEvent.AddUniqueDynamic(
				this, &AMaxiMallLoginPlayerController::OnPixelStreamingInput);

			UE_LOG(LogTemp, Warning,
				TEXT("[MaxiMall|Login] PS Input component bound in BeginPlay."));

			SendDiag(TEXT("[Login] BeginPlay OK | PS component bound in BeginPlay | Awaiting user Login click."));
		}
		else
		{
			UE_LOG(LogTemp, Warning,
				TEXT("[MaxiMall|Login] PS Input NOT found in BeginPlay — will retry in PlayerTick via TObjectIterator."));
		}

		FInputModeGameAndUI InputMode;
		InputMode.SetHideCursorDuringCapture(true);
		InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		SetInputMode(InputMode);
		bShowMouseCursor = true;
		bEnableClickEvents = true;
		bEnableMouseOverEvents = true;

		UE_LOG(LogTemp, Warning, TEXT("[MaxiMall|Login] ===== BeginPlay END ====="));
	}
}

void AMaxiMallLoginPlayerController::PlayerTick(float DeltaTime)
{
	Super::PlayerTick(DeltaTime);

	if (!IsLocalController())
	{
		return;
	}

	// ── PS Input late-bind retry ───────────────────────────────────────────
	if (!CachedPSInput)
	{
		for (TObjectIterator<UPixelStreamingInput> It; It; ++It)
		{
			if (UPixelStreamingInput* PSInput = *It)
			{
				CachedPSInput = PSInput;
				CachedPSInput->OnInputEvent.AddUniqueDynamic(
					this, &AMaxiMallLoginPlayerController::OnPixelStreamingInput);

				UE_LOG(LogTemp, Warning,
					TEXT("[MaxiMall|Login] PS Input late-bound in PlayerTick via TObjectIterator."));
				SendDiag(TEXT("[Login] PS Input late-bound in PlayerTick via TObjectIterator — ready."));
				break;
			}
		}
	}

	// Throttled clipboard monitoring: checks 5 times a second
	ClipboardCheckTimer += DeltaTime;
	if (ClipboardCheckTimer >= ClipboardCheckInterval)
	{
		ClipboardCheckTimer = 0.0f;

		FString CurrentClipboard;
		FPlatformApplicationMisc::ClipboardPaste(CurrentClipboard);

		if (CurrentClipboard != LastKnownClipboardContent)
		{
			LastKnownClipboardContent = CurrentClipboard;

			if (CachedPSInput)
			{
				FString Payload = FString::Printf(TEXT("MaxiMallClipboard %s"), *CurrentClipboard);
				CachedPSInput->SendPixelStreamingResponse(Payload);
			}
		}
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// SendDiag — helper to send a diagnostic string to the browser via PS.
// ─────────────────────────────────────────────────────────────────────────────

void AMaxiMallLoginPlayerController::SendDiag(const FString& Message) const
{
	if (!CachedPSInput)
	{
		return;
	}

	const FString Payload = FString::Printf(TEXT("DIAG: %s"), *Message);
	CachedPSInput->SendPixelStreamingResponse(Payload);

	UE_LOG(LogTemp, Warning, TEXT("[MaxiMall|Login|DIAG] %s"), *Message);
}

// ─────────────────────────────────────────────────────────────────────────────
// OnPixelStreamingInput — handles data-channel messages from the browser
// ─────────────────────────────────────────────────────────────────────────────

void AMaxiMallLoginPlayerController::OnPixelStreamingInput(const FString& Descriptor)
{
	UE_LOG(LogTemp, Warning, TEXT("[MaxiMall|Login] OnPixelStreamingInput fired. Raw descriptor length: %d"), Descriptor.Len());

	// ── 0. Check legacy prefix (MaxiMallPaste <text>) ────────────────────
	static const FString LegacyPrefix = TEXT("MaxiMallPaste ");
	if (Descriptor.StartsWith(LegacyPrefix))
	{
		FString PasteText = Descriptor.Mid(LegacyPrefix.Len());
		PasteText.ReplaceInline(TEXT("\0"), TEXT(""));

		UE_LOG(LogTemp, Warning, TEXT("[MaxiMall|Login] [LEGACY] ClipboardPaste received. Chars: %d"), PasteText.Len());

		FPlatformApplicationMisc::ClipboardCopy(*PasteText);
		LastKnownClipboardContent = PasteText;

		if (FSlateApplication::IsInitialized())
		{
			FSlateApplication& SlateApp = FSlateApplication::Get();
			for (int32 i = 0; i < PasteText.Len(); ++i)
			{
				FCharacterEvent CharEvent(PasteText[i], FModifierKeysState(), 0, false);
				SlateApp.ProcessKeyCharEvent(CharEvent);
			}
			SendDiag(FString::Printf(TEXT("[Login] [LEGACY] ClipboardPaste OK — injected %d chars via Slate."), PasteText.Len()));
		}
		return;
	}

	// ── 1. Parse JSON descriptor ─────────────────────────────────────────
	FString CleanDescriptor = Descriptor;
	CleanDescriptor.ReplaceInline(TEXT("\0"), TEXT(""));
	if (CleanDescriptor.StartsWith(TEXT("UIInteraction:")))
	{
		CleanDescriptor = CleanDescriptor.Mid(14).TrimStart();
	}
	else if (CleanDescriptor.StartsWith(TEXT("UIInteraction")))
	{
		CleanDescriptor = CleanDescriptor.Mid(13).TrimStart();
	}

	TSharedPtr<FJsonObject> JsonObject;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(CleanDescriptor);

	if (!FJsonSerializer::Deserialize(Reader, JsonObject) || !JsonObject.IsValid())
	{
		UE_LOG(LogTemp, Warning, TEXT("[MaxiMall|Login] OnPixelStreamingInput: unrecognised message format. Descriptor: %s"), *Descriptor);
		return;
	}

	// Unwrap PixelStreaming UIInteraction descriptor field if present
	FString EffectiveJSON = CleanDescriptor;
	if (JsonObject->HasField(TEXT("descriptor")))
	{
		EffectiveJSON = JsonObject->GetStringField(TEXT("descriptor"));
		TSharedPtr<FJsonObject> InnerObject;
		TSharedRef<TJsonReader<>> InnerReader = TJsonReaderFactory<>::Create(EffectiveJSON);
		if (FJsonSerializer::Deserialize(InnerReader, InnerObject) && InnerObject.IsValid())
		{
			JsonObject = InnerObject;
		}
	}
	else if (JsonObject->HasField(TEXT("Descriptor")))
	{
		EffectiveJSON = JsonObject->GetStringField(TEXT("Descriptor"));
		TSharedPtr<FJsonObject> InnerObject;
		TSharedRef<TJsonReader<>> InnerReader = TJsonReaderFactory<>::Create(EffectiveJSON);
		if (FJsonSerializer::Deserialize(InnerReader, InnerObject) && InnerObject.IsValid())
		{
			JsonObject = InnerObject;
		}
	}

	// ── ClipboardPaste command ───────────────────────────────────────────
	if (JsonObject->HasField(TEXT("Cmd")) &&
		JsonObject->GetStringField(TEXT("Cmd")) == TEXT("ClipboardPaste"))
	{
		FString PasteText = JsonObject->GetStringField(TEXT("Text"));

		// Strip embedded null terminators
		PasteText.ReplaceInline(TEXT("\0"), TEXT(""));

		UE_LOG(LogTemp, Warning, TEXT("[MaxiMall|Login] ClipboardPaste received. Character count: %d"), PasteText.Len());

		// 1. Sync to OS-level clipboard as a fallback
		FPlatformApplicationMisc::ClipboardCopy(*PasteText);
		LastKnownClipboardContent = PasteText;

		// 2. Direct injection via Slate Character Events
		if (FSlateApplication::IsInitialized())
		{
			FSlateApplication& SlateApp = FSlateApplication::Get();
			for (int32 i = 0; i < PasteText.Len(); ++i)
			{
				FCharacterEvent CharEvent(PasteText[i], FModifierKeysState(), 0, false);
				SlateApp.ProcessKeyCharEvent(CharEvent);
			}

			UE_LOG(LogTemp, Warning, TEXT("[MaxiMall|Login] ClipboardPaste: injected %d characters via Slate CharacterEvent."), PasteText.Len());

			SendDiag(FString::Printf(
				TEXT("[Login] ClipboardPaste OK — injected %d chars via Slate."), PasteText.Len()));
		}
		else
		{
			UE_LOG(LogTemp, Error, TEXT("[MaxiMall|Login] ClipboardPaste FAILED: SlateApplication not initialized."));
			SendDiag(TEXT("[Login] ERROR: ClipboardPaste failed — Slate not initialized."));
		}
	}
}
