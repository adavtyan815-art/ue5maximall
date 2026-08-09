// Copyright Epic Games, Inc. All Rights Reserved.

#include "MaxiMallLoginPlayerController.h"
#include "Framework/Application/SlateApplication.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "HAL/PlatformApplicationMisc.h"
// NOTE: #include "ShaderPipelineCache.h" has been intentionally REMOVED.
//       FShaderPipelineCache::SetBatchMode(Fast) was the cause of the ~2-minute
//       game-thread freeze on Login level load (FIX 1). Background PSO
//       compilation is now driven exclusively by DefaultEngine.ini settings:
//         r.ShaderPipelineCache.Enabled=1
//         r.ShaderPipelineCache.BackgroundBatchSize=20
//         r.ShaderPipelineCache.PrecompileBatchTime=0.01

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

	if (!IsLocalController())
	{
		UE_LOG(LogTemp, Warning, TEXT("[MaxiMall|Login] Not a local controller — skipping PS binding."));
		return;
	}

	CachedPSInput = GetComponentByClass<UPixelStreamingInput>();

	if (CachedPSInput)
	{
		CachedPSInput->OnInputEvent.AddUniqueDynamic(
			this, &AMaxiMallLoginPlayerController::OnPixelStreamingInput);

		UE_LOG(LogTemp, Warning,
			TEXT("[MaxiMall|Login] PS Input component found in BeginPlay and bound."));

		SendDiag(TEXT("[Login] BeginPlay OK | FIX1: no shader-pipeline stall | FIX2: PS component bound in BeginPlay | Awaiting user Login click."));
	}
	else
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[MaxiMall|Login] PS Input NOT found in BeginPlay — will retry in PlayerTick."));
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

void AMaxiMallLoginPlayerController::PlayerTick(float DeltaTime)
{
	Super::PlayerTick(DeltaTime);

	if (!IsLocalController())
	{
		return;
	}

	if (!CachedPSInput)
	{
		CachedPSInput = GetComponentByClass<UPixelStreamingInput>();
		if (CachedPSInput)
		{
			CachedPSInput->OnInputEvent.AddUniqueDynamic(
				this, &AMaxiMallLoginPlayerController::OnPixelStreamingInput);
			UE_LOG(LogTemp, Warning,
				TEXT("[MaxiMall|Login] PS Input component found in PlayerTick and bound (late-bind)."));
			SendDiag(TEXT("[Login] PS Input late-bound in PlayerTick — input and clipboard active."));
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
// Works in Shipping builds (no UE_LOG dependency for browser visibility).
// ─────────────────────────────────────────────────────────────────────────────

void AMaxiMallLoginPlayerController::SendDiag(const FString& Message) const
{
	if (!CachedPSInput)
	{
		return; // PS not active — silently skip.
	}

	// Prefix with "DIAG:" so the browser interceptor snippet can filter it.
	const FString Payload = FString::Printf(TEXT("DIAG: %s"), *Message);
	CachedPSInput->SendPixelStreamingResponse(Payload);

	// Also log to file in Development/Debug builds (stripped in Shipping).
	UE_LOG(LogTemp, Warning, TEXT("[MaxiMall|Login|DIAG] %s"), *Message);
}

// ─────────────────────────────────────────────────────────────────────────────
// OnPixelStreamingInput — handles data-channel messages from the browser
// ─────────────────────────────────────────────────────────────────────────────

void AMaxiMallLoginPlayerController::OnPixelStreamingInput(const FString& Descriptor)
{
	UE_LOG(LogTemp, Warning, TEXT("[MaxiMall|Login] OnPixelStreamingInput fired. Raw descriptor length: %d"), Descriptor.Len());

	// Legacy format: MaxiMallPaste <text>
	static const FString LegacyPrefix = TEXT("MaxiMallPaste ");
	if (Descriptor.StartsWith(LegacyPrefix))
	{
		FString PasteText = Descriptor.Mid(LegacyPrefix.Len());
		PasteText.ReplaceInline(TEXT("\0"), TEXT(""));

		UE_LOG(LogTemp, Warning, TEXT("[MaxiMall|Login] [LEGACY] ClipboardPaste received. Chars: %d"), PasteText.Len());

		FPlatformApplicationMisc::ClipboardCopy(*PasteText);

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

	TSharedPtr<FJsonObject> JsonObject;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Descriptor);

	if (!FJsonSerializer::Deserialize(Reader, JsonObject) || !JsonObject.IsValid())
	{
		UE_LOG(LogTemp, Warning, TEXT("[MaxiMall|Login] OnPixelStreamingInput: unrecognised message format. Descriptor: %s"), *Descriptor);
		return;
	}

	// ── ClipboardPaste command ───────────────────────────────────────────
	if (JsonObject->HasField(TEXT("Cmd")) &&
		JsonObject->GetStringField(TEXT("Cmd")) == TEXT("ClipboardPaste"))
	{
		FString PasteText = JsonObject->GetStringField(TEXT("Text"));

		// Strip embedded null terminators (can appear in some browsers)
		PasteText.ReplaceInline(TEXT("\0"), TEXT(""));

		UE_LOG(LogTemp, Warning, TEXT("[MaxiMall|Login] ClipboardPaste received. Character count: %d"), PasteText.Len());

		FPlatformApplicationMisc::ClipboardCopy(*PasteText);

		if (!FSlateApplication::IsInitialized())
		{
			UE_LOG(LogTemp, Error, TEXT("[MaxiMall|Login] ClipboardPaste FAILED: SlateApplication not initialized."));
			SendDiag(TEXT("[Login] ERROR: ClipboardPaste failed — Slate not initialized."));
			return;
		}

		// Inject each character as a Slate CharacterEvent.
		// This bypasses the headless-OS clipboard limitation on Linux EC2.
		FSlateApplication& SlateApp = FSlateApplication::Get();
		for (int32 i = 0; i < PasteText.Len(); ++i)
		{
			FCharacterEvent CharEvent(PasteText[i], FModifierKeysState(), 0, false);
			SlateApp.ProcessKeyCharEvent(CharEvent);
		}

		UE_LOG(LogTemp, Warning, TEXT("[MaxiMall|Login] ClipboardPaste: injected %d characters via Slate CharacterEvent."), PasteText.Len());

		// ── BROWSER DIAGNOSTIC ──────────────────────────────────────────
		// Tells user in DevTools that paste arrived AND was injected successfully.
		SendDiag(FString::Printf(
			TEXT("[Login] ClipboardPaste OK — injected %d chars via Slate."), PasteText.Len()));
	}
}
