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
 *  - Sends diagnostic messages back to the browser over the PS data channel.
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
