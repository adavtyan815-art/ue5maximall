// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "PixelStreamingInputComponent.h"
#include "awsTutorial_LoginPlayerController.generated.h"

/**
 * Lightweight Player Controller for the Login screen to support Pixel Streaming Copy/Paste.
 */
UCLASS()
class AWSTUTORIAL_API AAwsTutorial_LoginPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	AAwsTutorial_LoginPlayerController();

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
