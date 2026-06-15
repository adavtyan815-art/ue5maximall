// Copyright MaxiMall Project. All Rights Reserved.
// BoothProximityComponent.h
//
// UBoothProximityComponent — A UBoxComponent subclass attached to AShowroomBooth.
//
// PURPOSE:
//   Detects when any pawn enters/exits the booth's interaction zone and fires
//   IBoothInteractionInterface events on the pawn WITHOUT knowing its concrete type.
//
//   The booth owning this component fires OnProductChanged → the component
//   notifies the last tracked pawn via IBoothInteractionInterface::OnBoothProductUpdated.
//
// Designer Setup:
//   This component is added to AShowroomBooth as a VisibleAnywhere subobject.
//   Resize the box in the Editor Details panel per-booth to match floor space.
//
// Compatible: UE 5.3 → UE 5.6+

#pragma once

#include "CoreMinimal.h"
#include "Components/BoxComponent.h"
#include "BoothProximityComponent.generated.h"

UCLASS(ClassGroup = (MaxiMall),
       meta = (BlueprintSpawnableComponent,
               DisplayName = "Booth Proximity Component"))
class MAXIMALL_API UBoothProximityComponent : public UBoxComponent
{
    GENERATED_BODY()

public:

    UBoothProximityComponent(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

    virtual void BeginPlay() override;

    /**
     * Notify the proximity component that the owning booth's product changed.
     * Called by AShowroomBooth after a successful product update.
     * The component relays this to any tracked pawn in range.
     *
     * @param NewProductID  The new active product RowName.
     */
    UFUNCTION(BlueprintCallable, Category = "Proximity")
    void NotifyProductChanged(FName NewProductID);

private:

    UFUNCTION()
    void HandleBeginOverlap(UPrimitiveComponent* OverlappedComp, AActor* OtherActor,
                             UPrimitiveComponent* OtherComp, int32 OtherBodyIndex,
                             bool bFromSweep, const FHitResult& SweepResult);

    UFUNCTION()
    void HandleEndOverlap(UPrimitiveComponent* OverlappedComp, AActor* OtherActor,
                           UPrimitiveComponent* OtherComp, int32 OtherBodyIndex);

    /**
     * Weak pointer to the pawn currently inside the trigger volume.
     * Weak reference — avoids GC interference if the pawn is destroyed mid-session.
     * Only one pawn tracked at a time; extend to TArray for multi-player awareness.
     */
    TWeakObjectPtr<AActor> TrackedPawn;
};
