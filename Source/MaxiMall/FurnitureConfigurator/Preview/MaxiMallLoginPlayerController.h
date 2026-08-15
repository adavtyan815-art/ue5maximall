// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "PixelStreamingInputComponent.h"
#include "MaxiMallLoginPlayerController.generated.h"

/**
 * Lightweight Player Controller for the Login screen to support Pixel Streaming Copy/Paste.
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
	UPROPERTY()
	TObjectPtr<UPixelStreamingInput> PixelStreamingInput;

	UPROPERTY()
	TObjectPtr<UPixelStreamingInput> ActivePixelStreamingInput;

	UFUNCTION()
	void OnPixelStreamingInput(const FString& Descriptor);
};
