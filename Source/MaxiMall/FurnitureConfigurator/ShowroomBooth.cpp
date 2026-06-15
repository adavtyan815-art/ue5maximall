// Copyright MaxiMall Project. All Rights Reserved.
// ShowroomBooth.cpp

#include "FurnitureConfigurator/ShowroomBooth.h"

#include "FurnitureConfigurator/BoothProximityComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/DataTable.h"
#include "Net/UnrealNetwork.h"          // DOREPLIFETIME, DOREPLIFETIME_CONDITION
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"

// ─────────────────────────────────────────────────────────────────────────────
// Constructor
// ─────────────────────────────────────────────────────────────────────────────

AShowroomBooth::AShowroomBooth()
{
    // This actor replicates state to all clients.
    bReplicates = true;

    // We don't need Tick — all updates are event-driven via RepNotify.
    PrimaryActorTick.bCanEverTick = false;

    // ── Root ──────────────────────────────────────────────────────────────
    BoothRoot = CreateDefaultSubobject<USceneComponent>(TEXT("BoothRoot"));
    SetRootComponent(BoothRoot);

    // ── Main Cabinet ──────────────────────────────────────────────────────
    MainCabinet = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("MainCabinet"));
    MainCabinet->SetupAttachment(BoothRoot);

    // ── Door Slot 0 (Left / Single) ───────────────────────────────────────
    DoorMeshSlot0 = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("DoorMeshSlot0"));
    DoorMeshSlot0->SetupAttachment(MainCabinet);

    // ── Door Slot 1 (Right) ───────────────────────────────────────────────
    DoorMeshSlot1 = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("DoorMeshSlot1"));
    DoorMeshSlot1->SetupAttachment(MainCabinet);

    // ── Countertop ────────────────────────────────────────────────────────
    CountertopMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("CountertopMesh"));
    CountertopMesh->SetupAttachment(MainCabinet);

    // ── Sink (standalone — hidden for BuiltIn countertops) ────────────────
    SinkMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("SinkMesh"));
    SinkMesh->SetupAttachment(CountertopMesh);

    // ── Faucet ────────────────────────────────────────────────────────────
    FaucetMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("FaucetMesh"));
    FaucetMesh->SetupAttachment(CountertopMesh);

    // ── Mirror ────────────────────────────────────────────────────────────────
    MirrorMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("MirrorMesh"));
    MirrorMesh->SetupAttachment(BoothRoot);

    // ── Proximity Trigger ─────────────────────────────────────────────────
    ProximityVolume = CreateDefaultSubobject<UBoothProximityComponent>(TEXT("ProximityVolume"));
    ProximityVolume->SetupAttachment(BoothRoot);

    // ── Default state ─────────────────────────────────────────────────────
    ActiveProductID = NAME_None;
    bBaselineTransformsCaptured = false;

    // Pre-size to exactly 2 slots. Never grows beyond this.
    DoorStates.SetNumZeroed(2);
    DoorStates[0] = EDoorSlotState::NotPresent;
    DoorStates[1] = EDoorSlotState::NotPresent;
}

// ─────────────────────────────────────────────────────────────────────────────
// BeginPlay
// ─────────────────────────────────────────────────────────────────────────────

void AShowroomBooth::BeginPlay()
{
    Super::BeginPlay();

    // ── 1. Capture baseline transforms (editor-established layout) ─────────
    //    These are captured BEFORE any product is applied so that
    //    RecalculateDependentTransforms can use them as delta anchors.
    if (!bBaselineTransformsCaptured)
    {
        if (SinkMesh)
        {
            BaselineSinkTransform = SinkMesh->GetRelativeTransform();
        }
        if (FaucetMesh)
        {
            BaselineFaucetTransform = FaucetMesh->GetRelativeTransform();
        }
        bBaselineTransformsCaptured = true;
    }

    // ── 2. Initialize the door state array (guarantee 2 entries) ──────────
    InitializeDoorStateArray();

    // ── 3. Apply the initial product ──────────────────────────────────────
    //    Only the server sets the authoritative replicated state.
    //    Clients receive ActiveProductID via initial replication and their
    //    OnRep_ActiveProductID fires, rebuilding visuals from that value.
    if (HasAuthority() && InitialProductID != NAME_None)
    {
        const FFurnitureProductRow* Row = FindProductRow(InitialProductID);
        if (Row)
        {
            ActiveProductID = InitialProductID;

            // Apply visuals immediately on the server (and listen-server host).
            ApplyProductData(*Row);
        }
        else
        {
            UE_LOG(LogTemp, Warning,
                TEXT("[ShowroomBooth] '%s': InitialProductID '%s' not found in ProductCatalog."),
                *GetName(), *InitialProductID.ToString());
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// OnConstruction & InitializeDefaultBooth
// ─────────────────────────────────────────────────────────────────────────────

void AShowroomBooth::OnConstruction(const FTransform& Transform)
{
    Super::OnConstruction(Transform);

    // Capture baseline transforms if they haven't been captured yet.
    // In the editor, this locks in the designer's placed positions before we apply any product visuals.
    if (!bBaselineTransformsCaptured)
    {
        if (SinkMesh)
        {
            BaselineSinkTransform = SinkMesh->GetRelativeTransform();
        }
        if (FaucetMesh)
        {
            BaselineFaucetTransform = FaucetMesh->GetRelativeTransform();
        }
        bBaselineTransformsCaptured = true;
    }

    InitializeDefaultBooth();
}

void AShowroomBooth::InitializeDefaultBooth()
{
    // Safety guard for ProductCatalog DataTable reference
    if (!ProductCatalog)
    {
        UE_LOG(LogTemp, Warning, TEXT("[ShowroomBooth] '%s': ProductCatalog is null/unassigned inside InitializeDefaultBooth(). Please set it in the Details panel."), *GetName());
        return;
    }

    // Safety guard for InitialProductID
    if (InitialProductID == NAME_None)
    {
        UE_LOG(LogTemp, Warning, TEXT("[ShowroomBooth] '%s': InitialProductID is None inside InitializeDefaultBooth(). Please set a valid row name."), *GetName());
        return;
    }

    // Safety guard for ProductRow lookup
    const FFurnitureProductRow* ProductRow = FindProductRow(InitialProductID);
    if (!ProductRow)
    {
        UE_LOG(LogTemp, Warning, TEXT("[ShowroomBooth] '%s': ProductRow for ID '%s' is null/not found in ProductCatalog!"), *GetName(), *InitialProductID.ToString());
        return;
    }

    // Safety guards for component references
    if (!MainCabinet)
    {
        UE_LOG(LogTemp, Warning, TEXT("[ShowroomBooth] '%s': MainCabinet component is null!"), *GetName());
        return;
    }
    if (!DoorMeshSlot0)
    {
        UE_LOG(LogTemp, Warning, TEXT("[ShowroomBooth] '%s': DoorMeshSlot0 component is null!"), *GetName());
        return;
    }
    if (!DoorMeshSlot1)
    {
        UE_LOG(LogTemp, Warning, TEXT("[ShowroomBooth] '%s': DoorMeshSlot1 component is null!"), *GetName());
        return;
    }
    if (!CountertopMesh)
    {
        UE_LOG(LogTemp, Warning, TEXT("[ShowroomBooth] '%s': CountertopMesh component is null!"), *GetName());
        return;
    }
    if (!SinkMesh)
    {
        UE_LOG(LogTemp, Warning, TEXT("[ShowroomBooth] '%s': SinkMesh component is null!"), *GetName());
        return;
    }
    if (!FaucetMesh)
    {
        UE_LOG(LogTemp, Warning, TEXT("[ShowroomBooth] '%s': FaucetMesh component is null!"), *GetName());
        return;
    }
    if (!MirrorMesh)
    {
        UE_LOG(LogTemp, Warning, TEXT("[ShowroomBooth] '%s': MirrorMesh component is null!"), *GetName());
        return;
    }

    // Safely apply product visuals
    ApplyProductData(*ProductRow);
}

// ─────────────────────────────────────────────────────────────────────────────
// Replication
// ─────────────────────────────────────────────────────────────────────────────

void AShowroomBooth::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);

    // ActiveProductID — replicated to all connected clients.
    DOREPLIFETIME(AShowroomBooth, ActiveProductID);

    // DoorStates — replicated to all connected clients.
    DOREPLIFETIME(AShowroomBooth, DoorStates);
}

// ─────────────────────────────────────────────────────────────────────────────
// Public API
// ─────────────────────────────────────────────────────────────────────────────

void AShowroomBooth::RequestProductChange(FName NewProductID)
{
    // Prevent no-op updates that would generate unnecessary network traffic.
    if (NewProductID == ActiveProductID)
    {
        return;
    }

    if (HasAuthority())
    {
        // Server path: validate and apply immediately.
        Server_ApplyProductChange_Implementation(NewProductID);
    }
    else
    {
        // Client path: forward the request to the server.
        Server_ApplyProductChange(NewProductID);
    }
}

void AShowroomBooth::RequestDoorToggle(int32 SlotIndex)
{
    // Clamp slot index to valid range.
    const int32 ClampedIndex = FMath::Clamp(SlotIndex, 0, 1);

    if (HasAuthority())
    {
        Server_ToggleDoor_Implementation(ClampedIndex);
    }
    else
    {
        Server_ToggleDoor(ClampedIndex);
    }
}

bool AShowroomBooth::GetActiveProductData(FFurnitureProductRow& OutData) const
{
    const FFurnitureProductRow* Row = FindProductRow(ActiveProductID);
    if (Row)
    {
        OutData = *Row;
        return true;
    }
    return false;
}

EDoorSlotState AShowroomBooth::GetDoorSlotState(int32 SlotIndex) const
{
    if (!DoorStates.IsValidIndex(SlotIndex))
    {
        return EDoorSlotState::NotPresent;
    }
    return DoorStates[SlotIndex];
}

bool AShowroomBooth::IsValidProductID(FName ProductID) const
{
    return FindProductRow(ProductID) != nullptr;
}

// ─────────────────────────────────────────────────────────────────────────────
// Server RPC Implementations
// ─────────────────────────────────────────────────────────────────────────────

// ── Product Change ────────────────────────────────────────────────────────────

bool AShowroomBooth::Server_ApplyProductChange_Validate(FName NewProductID)
{
    // Reject requests referencing products that don't exist in the catalog.
    // This prevents clients from injecting arbitrary asset references.
    return IsValidProductID(NewProductID);
}

void AShowroomBooth::Server_ApplyProductChange_Implementation(FName NewProductID)
{
    const FFurnitureProductRow* Row = FindProductRow(NewProductID);
    if (!Row)
    {
        // Validate should have caught this, but guard anyway.
        return;
    }

    // Write the authoritative state. Replication engine propagates this to clients.
    ActiveProductID = NewProductID;

    // Apply visuals on the server immediately (required for listen-server correctness
    // and to prevent a one-frame visual gap on the server machine).
    ApplyProductData(*Row);

    // Notify any tracked pawn in the proximity zone.
    if (ProximityVolume)
    {
        ProximityVolume->NotifyProductChanged(ActiveProductID);
    }

    // Broadcast the delegate on the server so server-side listeners are notified.
    OnProductChanged.Broadcast(this, ActiveProductID);
}

// ── Door Toggle ───────────────────────────────────────────────────────────────

bool AShowroomBooth::Server_ToggleDoor_Validate(int32 SlotIndex)
{
    // Only permit interaction on valid slot indices.
    return DoorStates.IsValidIndex(SlotIndex);
}

void AShowroomBooth::Server_ToggleDoor_Implementation(int32 SlotIndex)
{
    if (!DoorStates.IsValidIndex(SlotIndex))
    {
        return;
    }

    const EDoorSlotState Current = DoorStates[SlotIndex];

    // Only toggle slots that are physically present.
    if (Current == EDoorSlotState::NotPresent)
    {
        return;
    }

    // Toggle: Closed ↔ Open.
    DoorStates[SlotIndex] = (Current == EDoorSlotState::Closed)
        ? EDoorSlotState::Open
        : EDoorSlotState::Closed;

    // Apply the new state visually on the server.
    const FFurnitureProductRow* Row = FindProductRow(ActiveProductID);
    if (Row)
    {
        const FDoorSlotConfig SlotCfg = Row->DoorSlots.IsValidIndex(SlotIndex) ? Row->DoorSlots[SlotIndex] : FDoorSlotConfig();
        UStaticMeshComponent* Slot = (SlotIndex == 0) ? DoorMeshSlot0.Get() : DoorMeshSlot1.Get();
        ApplyDoorSlotVisual(Slot, DoorStates[SlotIndex], SlotCfg);
    }

    // Broadcast on the server.
    OnDoorStateChanged.Broadcast(this, SlotIndex, DoorStates[SlotIndex]);
}

// ─────────────────────────────────────────────────────────────────────────────
// RepNotify Callbacks (fire on ALL clients including listen-server client)
// ─────────────────────────────────────────────────────────────────────────────

void AShowroomBooth::OnRep_ActiveProductID()
{
    // Look up the product row and rebuild all visuals.
    const FFurnitureProductRow* Row = FindProductRow(ActiveProductID);
    if (Row)
    {
        ApplyProductData(*Row);

        // Broadcast the event delegate so Blueprint listeners are notified.
        OnProductChanged.Broadcast(this, ActiveProductID);
    }
    else
    {
        UE_LOG(LogTemp, Warning,
            TEXT("[ShowroomBooth] OnRep: ProductID '%s' not found in catalog on client '%s'."),
            *ActiveProductID.ToString(), *GetName());
    }
}

void AShowroomBooth::OnRep_DoorStates()
{
    // Rebuild door visuals from the replicated state array.
    const FFurnitureProductRow* Row = FindProductRow(ActiveProductID);
    if (!Row)
    {
        return;
    }

    // Slot 0
    if (DoorStates.IsValidIndex(0))
    {
        const FDoorSlotConfig Cfg0 = Row->DoorSlots.IsValidIndex(0) ? Row->DoorSlots[0] : FDoorSlotConfig();
        ApplyDoorSlotVisual(DoorMeshSlot0.Get(), DoorStates[0], Cfg0);
        OnDoorStateChanged.Broadcast(this, 0, DoorStates[0]);
    }

    // Slot 1
    if (DoorStates.IsValidIndex(1))
    {
        const FDoorSlotConfig Cfg1 = Row->DoorSlots.IsValidIndex(1) ? Row->DoorSlots[1] : FDoorSlotConfig();
        ApplyDoorSlotVisual(DoorMeshSlot1.Get(), DoorStates[1], Cfg1);
        OnDoorStateChanged.Broadcast(this, 1, DoorStates[1]);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Internal Visual Application
// ─────────────────────────────────────────────────────────────────────────────

void AShowroomBooth::ApplyProductData(const FFurnitureProductRow& Data)
{
    // ── 1. Cabinet ────────────────────────────────────────────────────────
    ApplyMeshAndMaterials(MainCabinet.Get(), Data.CabinetBody);

    // ── 2. Doors ──────────────────────────────────────────────────────────
    ApplyDoorConfiguration(Data);

    // ── 3. Countertop ─────────────────────────────────────────────────────
    ApplyMeshAndMaterials(CountertopMesh.Get(), Data.CountertopMesh);

    // ── 4. Sink (visibility + mesh driven by CountertopType) ──────────────
    if (Data.CountertopType == ECountertopType::BuiltIn)
    {
        // Sink geometry is baked into the countertop mesh. Hide the standalone.
        if (SinkMesh)
        {
            SinkMesh->SetVisibility(false);
            SinkMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        }
    }
    else // SurfaceMounted
    {
        // Apply the standalone sink mesh and make it visible.
        ApplyMeshAndMaterials(SinkMesh.Get(), Data.SinkMesh);
        if (SinkMesh)
        {
            SinkMesh->SetVisibility(true);
            SinkMesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
        }
    }

    // ── 5. Faucet ─────────────────────────────────────────────────────────
    ApplyMeshAndMaterials(FaucetMesh.Get(), Data.FaucetMesh);
    if (FaucetMesh)
    {
        FaucetMesh->SetVisibility(true);
        FaucetMesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
    }

    // ── 6. Mirror ─────────────────────────────────────────────────────────
    ApplyMeshAndMaterials(MirrorMesh.Get(), Data.MirrorMesh);

    // ── 7. Recalculate Sink + Faucet positions relative to Countertop ─────
    RecalculateDependentTransforms(Data);
}

void AShowroomBooth::ApplyMeshAndMaterials(UStaticMeshComponent* Target,
                                            const FFurnitureMeshMaterials& Config)
{
    if (!Target)
    {
        return;
    }

    // Apply the mesh if a soft reference is set and the asset is loaded.
    // TSoftObjectPtr::Get() returns null if not loaded — safe to call.
    if (UStaticMesh* LoadedMesh = Config.Mesh.Get())
    {
        Target->SetStaticMesh(LoadedMesh);
    }

    // Apply per-slot material overrides.
    for (const FFurnitureMaterialSlot& SlotOverride : Config.MaterialOverrides)
    {
        UMaterialInterface* LoadedMat = SlotOverride.Material.Get();
        if (LoadedMat && Target->GetNumMaterials() > SlotOverride.SlotIndex)
        {
            Target->SetMaterial(SlotOverride.SlotIndex, LoadedMat);
        }
    }
}

void AShowroomBooth::ApplyDoorConfiguration(const FFurnitureProductRow& Data)
{
    // Apply the door mesh to both slots first (regardless of visibility).
    ApplyMeshAndMaterials(DoorMeshSlot0.Get(), Data.DoorMesh);
    ApplyMeshAndMaterials(DoorMeshSlot1.Get(), Data.DoorMesh);

    // Determine the initial door slot states from EDoorCount.
    EDoorSlotState Slot0State = EDoorSlotState::NotPresent;
    EDoorSlotState Slot1State = EDoorSlotState::NotPresent;

    switch (Data.DoorCount)
    {
    case EDoorCount::OneDoor:
        Slot0State = EDoorSlotState::Closed;
        Slot1State = EDoorSlotState::NotPresent;
        break;

    case EDoorCount::TwoDoors:
        Slot0State = EDoorSlotState::Closed;
        Slot1State = EDoorSlotState::Closed;
        break;

    case EDoorCount::NoDoors:
    default:
        Slot0State = EDoorSlotState::NotPresent;
        Slot1State = EDoorSlotState::NotPresent;
        break;
    }

    // Only the server writes to the replicated DoorStates array.
    // Clients receive the correct state via OnRep_DoorStates.
    if (HasAuthority())
    {
        DoorStates[0] = Slot0State;
        DoorStates[1] = Slot1State;
    }

    // Apply visual state (runs on all machines).
    const FDoorSlotConfig Cfg0 = Data.DoorSlots.IsValidIndex(0) ? Data.DoorSlots[0] : FDoorSlotConfig();
    const FDoorSlotConfig Cfg1 = Data.DoorSlots.IsValidIndex(1) ? Data.DoorSlots[1] : FDoorSlotConfig();
    ApplyDoorSlotVisual(DoorMeshSlot0.Get(), DoorStates[0], Cfg0);
    ApplyDoorSlotVisual(DoorMeshSlot1.Get(), DoorStates[1], Cfg1);
}

void AShowroomBooth::ApplyDoorSlotVisual(UStaticMeshComponent* Slot,
                                          EDoorSlotState State,
                                          const FDoorSlotConfig& SlotCfg)
{
    if (!Slot)
    {
        return;
    }

    if (State == EDoorSlotState::NotPresent)
    {
        // Door does not exist in this product model.
        Slot->SetVisibility(false);
        Slot->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        return;
    }

    // Door is present — ensure it's visible with collision.
    Slot->SetVisibility(true);
    Slot->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);

    // Set local position to the product-defined closed position offset.
    FVector TargetLocation = SlotCfg.ClosedPositionOffset;

    // Compute the open/closed rotation.
    // Closed = no yaw delta.  Open = add the product-defined yaw delta.
    const float YawDelta = (State == EDoorSlotState::Open) ? SlotCfg.OpenYawDelta : 0.f;
    FRotator TargetRotation = FRotator(0.f, YawDelta, 0.f);

    Slot->SetRelativeLocationAndRotation(TargetLocation, TargetRotation);
}

void AShowroomBooth::RecalculateDependentTransforms(const FFurnitureProductRow& Data)
{
    // All transforms are expressed in CountertopMesh local space so they
    // stay correct regardless of how the designer positioned the countertop.

    // ── Sink ──────────────────────────────────────────────────────────────
    if (SinkMesh && Data.CountertopType == ECountertopType::SurfaceMounted)
    {
        // Build the target relative transform from the product data offset.
        // The baseline transform is the editor-placed starting position;
        // the product offset is a DELTA on top of it for fitting variation.
        const FSinkPlacementOffset& SO = Data.SinkOffset;

        FTransform ProductDelta;
        ProductDelta.SetLocation(SO.RelativeLocation);
        ProductDelta.SetRotation(SO.RelativeRotation.Quaternion());
        ProductDelta.SetScale3D(SO.RelativeScale);

        // Combine: editor baseline + product-specific delta.
        const FTransform FinalSinkTransform = ProductDelta * BaselineSinkTransform;
        SinkMesh->SetRelativeTransform(FinalSinkTransform);
    }

    // ── Faucet ────────────────────────────────────────────────────────────
    if (FaucetMesh)
    {
        const FFaucetPlacementOffset& FO = Data.FaucetOffset;

        FTransform ProductDelta;
        ProductDelta.SetLocation(FO.RelativeLocation);
        ProductDelta.SetRotation(FO.RelativeRotation.Quaternion());
        ProductDelta.SetScale3D(FO.RelativeScale);

        const FTransform FinalFaucetTransform = ProductDelta * BaselineFaucetTransform;
        FaucetMesh->SetRelativeTransform(FinalFaucetTransform);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────

void AShowroomBooth::InitializeDoorStateArray()
{
    // Guarantee exactly 2 entries so DoorStates[0] / DoorStates[1] are always valid.
    if (DoorStates.Num() != 2)
    {
        DoorStates.SetNum(2);
        DoorStates[0] = EDoorSlotState::NotPresent;
        DoorStates[1] = EDoorSlotState::NotPresent;
    }
}

const FFurnitureProductRow* AShowroomBooth::FindProductRow(FName RowName) const
{
    if (!ProductCatalog || RowName == NAME_None)
    {
        return nullptr;
    }

    return ProductCatalog->FindRow<FFurnitureProductRow>(RowName, TEXT("ShowroomBooth"), /*bWarnIfNotFound=*/true);
}
