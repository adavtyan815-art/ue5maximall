// Copyright MaxiMall Project. All Rights Reserved.
// BoothProximityComponent.cpp

#include "FurnitureConfigurator/BoothProximityComponent.h"
#include "FurnitureConfigurator/BoothInteractionInterface.h"
#include "GameFramework/Pawn.h"

// ─────────────────────────────────────────────────────────────────────────────
// Constructor
// ─────────────────────────────────────────────────────────────────────────────

UBoothProximityComponent::UBoothProximityComponent(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer)
{
}

void UBoothProximityComponent::BeginPlay()
{
    Super::BeginPlay();

    // Extent can be resized in Editor — default to a generously sized booth footprint.
    SetBoxExtent(FVector(200.f, 200.f, 100.f));

    // Only detect pawns — ignore props, projectiles, etc.
    SetCollisionResponseToAllChannels(ECR_Ignore);
    SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
    SetCollisionEnabled(ECollisionEnabled::QueryOnly);

    // Overlap events on the server drive the authoritative proximity list.
    // On clients, the events still fire for local UI feedback.
    SetGenerateOverlapEvents(true);

    OnComponentBeginOverlap.AddDynamic(this, &UBoothProximityComponent::HandleBeginOverlap);
    OnComponentEndOverlap.AddDynamic(this, &UBoothProximityComponent::HandleEndOverlap);
}

// ─────────────────────────────────────────────────────────────────────────────
// Overlap Handlers
// ─────────────────────────────────────────────────────────────────────────────

void UBoothProximityComponent::HandleBeginOverlap(UPrimitiveComponent* /*OverlappedComp*/,
                                                   AActor* OtherActor,
                                                   UPrimitiveComponent* /*OtherComp*/,
                                                   int32 /*OtherBodyIndex*/,
                                                   bool /*bFromSweep*/,
                                                   const FHitResult& /*SweepResult*/)
{
    if (!OtherActor)
    {
        return;
    }

    // Only care about pawns (covers both player characters and NPC visitors).
    if (!OtherActor->IsA<APawn>())
    {
        return;
    }

    TrackedPawn = OtherActor;

    // Fire the interface event on the pawn — works regardless of the pawn's
    // concrete Blueprint or C++ class.
    if (OtherActor->GetClass()->ImplementsInterface(UBoothInteractionInterface::StaticClass()))
    {
        IBoothInteractionInterface::Execute_OnEnteredBoothRange(OtherActor, GetOwner());
    }
}

void UBoothProximityComponent::HandleEndOverlap(UPrimitiveComponent* /*OverlappedComp*/,
                                                 AActor* OtherActor,
                                                 UPrimitiveComponent* /*OtherComp*/,
                                                 int32 /*OtherBodyIndex*/)
{
    if (!OtherActor)
    {
        return;
    }

    if (OtherActor->GetClass()->ImplementsInterface(UBoothInteractionInterface::StaticClass()))
    {
        IBoothInteractionInterface::Execute_OnExitedBoothRange(OtherActor, GetOwner());
    }

    // Clear tracked pawn if it was the one leaving.
    if (TrackedPawn.Get() == OtherActor)
    {
        TrackedPawn.Reset();
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Product Change Notification
// ─────────────────────────────────────────────────────────────────────────────

void UBoothProximityComponent::NotifyProductChanged(FName NewProductID)
{
    AActor* Pawn = TrackedPawn.Get();
    if (!Pawn)
    {
        return;
    }

    if (Pawn->GetClass()->ImplementsInterface(UBoothInteractionInterface::StaticClass()))
    {
        IBoothInteractionInterface::Execute_OnBoothProductUpdated(Pawn, NewProductID);
    }
}
