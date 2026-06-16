// Copyright MaxiMall Project. All Rights Reserved.
// ShowroomBooth.cpp

#include "FurnitureConfigurator/ShowroomBooth.h"

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

    // ── Closet ────────────────────────────────────────────────────────────
    ClosetMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ClosetMesh"));
    ClosetMesh->SetupAttachment(BoothRoot);

    // ── Default state ─────────────────────────────────────────────────────
    ActiveState.ProductID = NAME_None;
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
    //    Always capture these on BeginPlay to ensure we use the actual editor-placed transforms as baseline
    if (SinkMesh)
    {
        BaselineSinkTransform = SinkMesh->GetRelativeTransform();
    }
    if (FaucetMesh)
    {
        BaselineFaucetTransform = FaucetMesh->GetRelativeTransform();
    }
    bBaselineTransformsCaptured = true;

    // ── 2. Initialize the door state array (guarantee 2 entries) ──────────
    InitializeDoorStateArray();

    // ── 3. Apply the initial product ──────────────────────────────────────
    //    Only the server sets the authoritative replicated state.
    //    Clients receive ActiveState via initial replication and their OnRep_ActiveState fires.
    if (HasAuthority() && InitialProductID != NAME_None)
    {
        const FFurnitureProductRow* Row = FindProductRow(InitialProductID);
        if (Row)
        {
            InitializeDefaultStateForProduct(ActiveState, InitialProductID, *Row);

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

    // Only run editor-side visualization and baseline capturing in the editor viewport (not in game/PIE)
    if (GetWorld() && !GetWorld()->IsGameWorld())
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

        InitializeDefaultBooth();
    }
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
    if (!ClosetMesh)
    {
        UE_LOG(LogTemp, Warning, TEXT("[ShowroomBooth] '%s': ClosetMesh component is null!"), *GetName());
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

    // ActiveState — replicated to all connected clients.
    DOREPLIFETIME(AShowroomBooth, ActiveState);

    // DoorStates — replicated to all connected clients.
    DOREPLIFETIME(AShowroomBooth, DoorStates);
}

// ─────────────────────────────────────────────────────────────────────────────
// Public API
// ─────────────────────────────────────────────────────────────────────────────

void AShowroomBooth::RequestProductChange(FName NewProductID)
{
    // Prevent no-op updates that would generate unnecessary network traffic.
    if (NewProductID == ActiveState.ProductID)
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
    const FFurnitureProductRow* Row = FindProductRow(ActiveState.ProductID);
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
    return IsValidProductID(NewProductID);
}

void AShowroomBooth::Server_ApplyProductChange_Implementation(FName NewProductID)
{
    const FFurnitureProductRow* Row = FindProductRow(NewProductID);
    if (!Row)
    {
        return;
    }

    InitializeDefaultStateForProduct(ActiveState, NewProductID, *Row);

    // Apply visuals on the server immediately
    ApplyProductData(*Row);

    // Broadcast the delegate on the server
    OnProductChanged.Broadcast(this, ActiveState.ProductID);
}

// ── Component Selection ───────────────────────────────────────────────────────

void AShowroomBooth::RequestComponentSelection(EFurnitureComponentType ComponentType, FName SizeID, FName ColorID)
{
    if (HasAuthority())
    {
        Server_ApplyComponentSelection_Implementation(ComponentType, SizeID, ColorID);
    }
    else
    {
        Server_ApplyComponentSelection(ComponentType, SizeID, ColorID);
    }
}

bool AShowroomBooth::Server_ApplyComponentSelection_Validate(EFurnitureComponentType ComponentType, FName SizeID, FName ColorID)
{
    return true;
}

void AShowroomBooth::Server_ApplyComponentSelection_Implementation(EFurnitureComponentType ComponentType, FName SizeID, FName ColorID)
{
    switch (ComponentType)
    {
    case EFurnitureComponentType::Cabinet:
        ActiveState.CabinetState.SelectedSizeID = SizeID;
        ActiveState.CabinetState.SelectedColorID = ColorID;
        break;
    case EFurnitureComponentType::Closet:
        ActiveState.ClosetState.SelectedSizeID = SizeID;
        ActiveState.ClosetState.SelectedColorID = ColorID;
        break;
    case EFurnitureComponentType::Doors:
        ActiveState.DoorState.SelectedSizeID = SizeID;
        ActiveState.DoorState.SelectedColorID = ColorID;
        break;
    case EFurnitureComponentType::Countertop:
        ActiveState.CountertopState.SelectedSizeID = SizeID;
        ActiveState.CountertopState.SelectedColorID = ColorID;
        break;
    case EFurnitureComponentType::Sink:
        ActiveState.SinkState.SelectedSizeID = SizeID;
        ActiveState.SinkState.SelectedColorID = ColorID;
        break;
    case EFurnitureComponentType::Faucet:
        ActiveState.FaucetState.SelectedSizeID = SizeID;
        ActiveState.FaucetState.SelectedColorID = ColorID;
        break;
    case EFurnitureComponentType::Mirror:
        ActiveState.MirrorState.SelectedSizeID = SizeID;
        ActiveState.MirrorState.SelectedColorID = ColorID;
        break;
    default:
        break;
    }

    const FFurnitureProductRow* Row = FindProductRow(ActiveState.ProductID);
    if (Row)
    {
        ApplyProductData(*Row);
        OnProductChanged.Broadcast(this, ActiveState.ProductID);
    }
}

// ── Door Toggle ───────────────────────────────────────────────────────────────

bool AShowroomBooth::Server_ToggleDoor_Validate(int32 SlotIndex)
{
    return DoorStates.IsValidIndex(SlotIndex);
}

void AShowroomBooth::Server_ToggleDoor_Implementation(int32 SlotIndex)
{
    if (!DoorStates.IsValidIndex(SlotIndex))
    {
        return;
    }

    const EDoorSlotState Current = DoorStates[SlotIndex];

    if (Current == EDoorSlotState::NotPresent)
    {
        return;
    }

    DoorStates[SlotIndex] = (Current == EDoorSlotState::Closed)
        ? EDoorSlotState::Open
        : EDoorSlotState::Closed;

    const FFurnitureProductRow* Row = FindProductRow(ActiveState.ProductID);
    if (Row)
    {
        const FDoorSlotConfig SlotCfg = Row->DoorSlots.IsValidIndex(SlotIndex) ? Row->DoorSlots[SlotIndex] : FDoorSlotConfig();
        UStaticMeshComponent* Slot = (SlotIndex == 0) ? DoorMeshSlot0.Get() : DoorMeshSlot1.Get();
        ApplyDoorSlotVisual(Slot, DoorStates[SlotIndex], SlotCfg);
    }

    OnDoorStateChanged.Broadcast(this, SlotIndex, DoorStates[SlotIndex]);
}

// ─────────────────────────────────────────────────────────────────────────────
// RepNotify Callbacks (fire on ALL clients)
// ─────────────────────────────────────────────────────────────────────────────

void AShowroomBooth::OnRep_ActiveState()
{
    const FFurnitureProductRow* Row = FindProductRow(ActiveState.ProductID);
    if (Row)
    {
        ApplyProductData(*Row);
        OnProductChanged.Broadcast(this, ActiveState.ProductID);
    }
    else
    {
        UE_LOG(LogTemp, Warning,
            TEXT("[ShowroomBooth] OnRep: ProductID '%s' not found in catalog on client '%s'."),
            *ActiveState.ProductID.ToString(), *GetName());
    }
}

void AShowroomBooth::OnRep_DoorStates()
{
    const FFurnitureProductRow* Row = FindProductRow(ActiveState.ProductID);
    if (!Row)
    {
        return;
    }

    if (DoorStates.IsValidIndex(0))
    {
        const FDoorSlotConfig Cfg0 = Row->DoorSlots.IsValidIndex(0) ? Row->DoorSlots[0] : FDoorSlotConfig();
        ApplyDoorSlotVisual(DoorMeshSlot0.Get(), DoorStates[0], Cfg0);
        OnDoorStateChanged.Broadcast(this, 0, DoorStates[0]);
    }

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
    ApplyComponentMeshAndMaterials(MainCabinet.Get(), Data.CabinetOptions, ActiveState.CabinetState);

    // ── 2. Closet ─────────────────────────────────────────────────────────
    ApplyComponentMeshAndMaterials(ClosetMesh.Get(), Data.ClosetOptions, ActiveState.ClosetState);

    // ── 3. Doors ──────────────────────────────────────────────────────────
    ApplyDoorConfiguration(Data);

    // ── 4. Countertop ─────────────────────────────────────────────────────
    ApplyComponentMeshAndMaterials(CountertopMesh.Get(), Data.CountertopOptions, ActiveState.CountertopState);

    // ── 5. Sink (visibility + mesh driven by CountertopType) ──────────────
    if (Data.CountertopType == ECountertopType::BuiltIn)
    {
        if (SinkMesh)
        {
            SinkMesh->SetStaticMesh(nullptr);
            SinkMesh->SetVisibility(false);
            SinkMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        }
    }
    else // SurfaceMounted
    {
        ApplyComponentMeshAndMaterials(SinkMesh.Get(), Data.SinkOptions, ActiveState.SinkState);
    }

    // ── 6. Faucet ─────────────────────────────────────────────────────────
    ApplyComponentMeshAndMaterials(FaucetMesh.Get(), Data.FaucetOptions, ActiveState.FaucetState);

    // ── 7. Mirror ─────────────────────────────────────────────────────────
    ApplyComponentMeshAndMaterials(MirrorMesh.Get(), Data.MirrorOptions, ActiveState.MirrorState);

    // ── 8. Recalculate Sink + Faucet positions relative to Countertop ─────
    RecalculateDependentTransforms(Data);
}

void AShowroomBooth::ApplyComponentMeshAndMaterials(UStaticMeshComponent* Target,
                                                    const FFurnitureComponentOptions& Options,
                                                    const FFurnitureComponentState& State)
{
    if (!Target)
    {
        return;
    }

    const FFurnitureSizeOption* SelectedSize = nullptr;
    if (Options.Sizes.Num() > 0)
    {
        for (const FFurnitureSizeOption& SizeOpt : Options.Sizes)
        {
            if (SizeOpt.SizeID == State.SelectedSizeID)
            {
                SelectedSize = &SizeOpt;
                break;
            }
        }
        if (!SelectedSize)
        {
            SelectedSize = &Options.Sizes[0];
        }
    }

    const FFurnitureColorOption* SelectedColor = nullptr;
    if (Options.Colors.Num() > 0)
    {
        for (const FFurnitureColorOption& ColorOpt : Options.Colors)
        {
            if (ColorOpt.ColorID == State.SelectedColorID)
            {
                SelectedColor = &ColorOpt;
                break;
            }
        }
        if (!SelectedColor)
        {
            SelectedColor = &Options.Colors[0];
        }
    }

    if (!SelectedSize || SelectedSize->Mesh.IsNull() || SelectedSize->Mesh.ToSoftObjectPath().ToString().IsEmpty())
    {
        Target->SetStaticMesh(nullptr);
        Target->SetVisibility(false);
        Target->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        return;
    }

    UStaticMesh* LoadedMesh = SelectedSize->Mesh.LoadSynchronous();
    if (LoadedMesh)
    {
        Target->SetStaticMesh(LoadedMesh);
        Target->SetVisibility(true);
        Target->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);

        const int32 NumMaterials = Target->GetNumMaterials();
        for (int32 i = 0; i < NumMaterials; ++i)
        {
            Target->SetMaterial(i, nullptr);
        }

        if (SelectedColor)
        {
            for (const FFurnitureMaterialSlot& SlotOverride : SelectedColor->MaterialOverrides)
            {
                UMaterialInterface* LoadedMat = SlotOverride.Material.LoadSynchronous();
                if (LoadedMat && Target->GetNumMaterials() > SlotOverride.SlotIndex)
                {
                    Target->SetMaterial(SlotOverride.SlotIndex, LoadedMat);
                }
            }
        }
    }
    else
    {
        Target->SetStaticMesh(nullptr);
        Target->SetVisibility(false);
        Target->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    }
}

void AShowroomBooth::ApplyDoorConfiguration(const FFurnitureProductRow& Data)
{
    ApplyComponentMeshAndMaterials(DoorMeshSlot0.Get(), Data.DoorOptions, ActiveState.DoorState);
    ApplyComponentMeshAndMaterials(DoorMeshSlot1.Get(), Data.DoorOptions, ActiveState.DoorState);

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
    if (SinkMesh)
    {
        if (GetWorld() && !GetWorld()->IsGameWorld())
        {
            // In the editor, keep the mesh at its baseline (placed) transform
            SinkMesh->SetRelativeTransform(BaselineSinkTransform);
        }
        else if (Data.CountertopType == ECountertopType::SurfaceMounted)
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
    }

    // ── Faucet ────────────────────────────────────────────────────────────
    if (FaucetMesh)
    {
        if (GetWorld() && !GetWorld()->IsGameWorld())
        {
            // In the editor, keep the mesh at its baseline (placed) transform
            FaucetMesh->SetRelativeTransform(BaselineFaucetTransform);
        }
        else
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

void AShowroomBooth::InitializeDefaultStateForProduct(FShowroomBoothConfigState& State, FName ProductID, const FFurnitureProductRow& Row)
{
    State.ProductID = ProductID;

    auto GetDefaultSize = [](const FFurnitureComponentOptions& Opts) -> FName {
        return (Opts.Sizes.Num() > 0) ? Opts.Sizes[0].SizeID : NAME_None;
    };

    auto GetDefaultColor = [](const FFurnitureComponentOptions& Opts) -> FName {
        return (Opts.Colors.Num() > 0) ? Opts.Colors[0].ColorID : NAME_None;
    };

    State.CabinetState.SelectedSizeID = GetDefaultSize(Row.CabinetOptions);
    State.CabinetState.SelectedColorID = GetDefaultColor(Row.CabinetOptions);

    State.ClosetState.SelectedSizeID = GetDefaultSize(Row.ClosetOptions);
    State.ClosetState.SelectedColorID = GetDefaultColor(Row.ClosetOptions);

    State.DoorState.SelectedSizeID = GetDefaultSize(Row.DoorOptions);
    State.DoorState.SelectedColorID = GetDefaultColor(Row.DoorOptions);

    State.CountertopState.SelectedSizeID = GetDefaultSize(Row.CountertopOptions);
    State.CountertopState.SelectedColorID = GetDefaultColor(Row.CountertopOptions);

    State.SinkState.SelectedSizeID = GetDefaultSize(Row.SinkOptions);
    State.SinkState.SelectedColorID = GetDefaultColor(Row.SinkOptions);

    State.FaucetState.SelectedSizeID = GetDefaultSize(Row.FaucetOptions);
    State.FaucetState.SelectedColorID = GetDefaultColor(Row.FaucetOptions);

    State.MirrorState.SelectedSizeID = GetDefaultSize(Row.MirrorOptions);
    State.MirrorState.SelectedColorID = GetDefaultColor(Row.MirrorOptions);
}
