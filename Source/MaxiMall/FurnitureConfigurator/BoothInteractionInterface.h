// Copyright MaxiMall Project. All Rights Reserved.
// BoothInteractionInterface.h
//
// UBoothInteractionInterface — Unreal Interface for loose-coupling.
//
// PURPOSE:
//   Any Blueprint actor (including the existing monolithic Blueprint Character
//   in the UE 5.6 destination project) can implement this interface WITHOUT
//   any C++ changes to the character class.
//
//   AShowroomBooth and AMaxiMallPreviewController check for this interface
//   when firing feedback events back to an interacting pawn, so they never
//   need to know the concrete type of the Character.
//
// HOW TO USE IN BLUEPRINTS:
//   1. Open the Blueprint Character.
//   2. In Class Settings → Implemented Interfaces, add "BoothInteractionInterface".
//   3. The Blueprint receives the interface events as standard BP events
//      that can be wired into any UI or animation logic.
//
// Compatible: UE 5.3 → UE 5.6+

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "FurnitureConfigurator/Data/FurnitureTypes.h"
#include "BoothInteractionInterface.generated.h"

// Standard UE interface boilerplate — UBoothInteractionInterface provides
// the UObject reflection metadata; IBoothInteractionInterface is what
// actors implement.
UINTERFACE(MinimalAPI, BlueprintType,
           meta = (DisplayName = "Booth Interaction Interface"))
class UBoothInteractionInterface : public UInterface
{
    GENERATED_BODY()
};

class MAXIMALL_API IBoothInteractionInterface
{
    GENERATED_BODY()

public:

    /**
     * Called on the interacting pawn when they enter the trigger volume
     * of an AShowroomBooth. Implement in Blueprint to show a prompt UI,
     * highlight the booth, etc.
     *
     * @param Booth  The booth the pawn is now near. Cast in Blueprint as needed.
     */
    UFUNCTION(BlueprintImplementableEvent, BlueprintCallable,
              Category = "Booth Interaction",
              meta = (DisplayName = "On Entered Booth Range"))
    void OnEnteredBoothRange(AActor* Booth);

    /**
     * Called when the pawn leaves the booth's trigger volume.
     */
    UFUNCTION(BlueprintImplementableEvent, BlueprintCallable,
              Category = "Booth Interaction",
              meta = (DisplayName = "On Exited Booth Range"))
    void OnExitedBoothRange(AActor* Booth);

    /**
     * Called after a product change on the booth this pawn last interacted with.
     * Implement in Blueprint to update the character's UI panel, play animations, etc.
     *
     * @param NewProductID  The RowName of the product now displayed.
     */
    UFUNCTION(BlueprintImplementableEvent, BlueprintCallable,
              Category = "Booth Interaction",
              meta = (DisplayName = "On Booth Product Updated"))
    void OnBoothProductUpdated(FName NewProductID);
};
