// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "PixelStreamingInputComponent.h"
#include "MaxiMallLoginPlayerController.generated.h"

/**
 * Lightweight Player Controller for the Login screen.
 *
 * Responsibilities:
 *  - Supports Pixel Streaming clipboard paste via Slate Character Event injection.
 *  - Sends DIAG diagnostic messages back to the browser over the PS data channel
 *    so that every step can be verified in the browser DevTools console even in
 *    Shipping builds (where UE_LOG is compiled out).
 *
 * Design principles (fixes applied):
 *  FIX 1 — SetBatchMode(Fast) has been REMOVED from BeginPlay().
 *           It was stalling the game thread for ~90-150 s on Login level load.
 *           Background PSO compilation is driven by DefaultEngine.ini settings.
 *
 *  FIX 2 — CreateDefaultSubobject<UPixelStreamingInput> has been REMOVED from
 *           the constructor. The PS plugin creates and owns exactly one
 *           UPixelStreamingInput component per controller. We discover it once
 *           in BeginPlay() via GetComponentByClass — no per-frame tick scan.
 *           This eliminates the duplicate-component PS handshake interference
 *           that caused intermittent login kickbacks.
 */
UCLASS()
class MAXIMALL_API AMaxiMallLoginPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	AMaxiMallLoginPlayerController();

protected:
	virtual void BeginPlay() override;
	virtual void PlayerTick(float DeltaTime) override;

private:
	/**
	 * Cached reference to the UPixelStreamingInput component created and owned
	 * by the Pixel Streaming plugin. Populated once in BeginPlay() via
	 * GetComponentByClass — NOT created with CreateDefaultSubobject.
	 */
	UPROPERTY()
	TObjectPtr<UPixelStreamingInput> CachedPSInput;

	FString LastKnownClipboardContent;
	float ClipboardCheckInterval = 0.2f;
	float ClipboardCheckTimer = 0.0f;

	/** Handles incoming PS data-channel messages (e.g. ClipboardPaste commands). */
	UFUNCTION()
	void OnPixelStreamingInput(const FString& Descriptor);

	/** Sends a DIAG diagnostic string to the browser via the PS data channel.
	 *  Visible in browser DevTools when the MaxiMall diagnostic interceptor
	 *  snippet is active. Works in Shipping builds (no UE_LOG dependency). */
	void SendDiag(const FString& Message) const;
};
