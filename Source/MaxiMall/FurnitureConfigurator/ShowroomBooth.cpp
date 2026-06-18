// Copyright MaxiMall Project. All Rights Reserved.
// ShowroomBooth.cpp

#include "FurnitureConfigurator/ShowroomBooth.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/DataTable.h"
#include "Net/UnrealNetwork.h"          // DOREPLIFETIME, DOREPLIFETIME_CONDITION
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "TimerManager.h"

// ─────────────────────────────────────────────────────────────────────────────
// Constructor
// ─────────────────────────────────────────────────────────────────────────────

AShowroomBooth::AShowroomBooth()
{
    // This actor replicates state to all clients.
    bReplicates = true;

    // We don't need Tick — all updates are driven by Timers and RepNotify.
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

    // Ensure baseline transforms are captured from the starting level editor state
    EnsureBaselineTransformsCaptured();

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

void AShowroomBooth::UpdateDoorAnimation()
{
    float DeltaTime = 0.016f;
    if (GetWorld())
    {
        DeltaTime = GetWorld()->GetDeltaSeconds();
    }

    bool bAnimating = false;

    // Interpolate door 0 if it is present
    if (DoorStates.IsValidIndex(0) && DoorMeshSlot0 && DoorStates[0] != EDoorSlotState::NotPresent)
    {
        bAnimating |= AnimateDoorSlot(DoorMeshSlot0, 0, DeltaTime);
    }

    // Interpolate door 1 if it is present
    if (DoorStates.IsValidIndex(1) && DoorMeshSlot1 && DoorStates[1] != EDoorSlotState::NotPresent)
    {
        bAnimating |= AnimateDoorSlot(DoorMeshSlot1, 1, DeltaTime);
    }

    // Stop ticking the timer once all active animations are complete
    if (!bAnimating)
    {
        if (GetWorld())
        {
            GetWorld()->GetTimerManager().ClearTimer(DoorAnimationTimerHandle);
        }
    }
}

bool AShowroomBooth::AnimateDoorSlot(UStaticMeshComponent* Slot, int32 SlotIndex, float DeltaTime)
{
    if (!Slot) return false;

    const FFurnitureProductRow* Row = FindProductRow(ActiveState.ProductID);
    if (!Row) return false;

    const FDoorSlotConfig SlotCfg = Row->DoorSlots.IsValidIndex(SlotIndex) ? Row->DoorSlots[SlotIndex] : FDoorSlotConfig();

    FTransform BaselineTransform = (SlotIndex == 0) ? BaselineDoor0Transform : BaselineDoor1Transform;
    if (BaselineTransform.GetScale3D().IsNearlyZero())
    {
        BaselineTransform.SetScale3D(FVector::OneVector);
    }

    // Target position (Open vs Closed, Translation/Sliding vs Hinge)
    FVector TargetLocation = BaselineTransform.GetLocation();
    if (!SlotCfg.bIsRotation && DoorStates[SlotIndex] == EDoorSlotState::Open)
    {
        TargetLocation = BaselineTransform.GetLocation() + SlotCfg.OpenTranslationOffset;
    }

    // Target rotation (Open vs Closed)
    float TargetYaw = BaselineTransform.Rotator().Yaw;
    if (SlotCfg.bIsRotation && DoorStates[SlotIndex] == EDoorSlotState::Open)
    {
        TargetYaw = BaselineTransform.Rotator().Yaw + SlotCfg.OpenYawDelta;
    }
    FRotator TargetRotation = BaselineTransform.Rotator();
    TargetRotation.Yaw = TargetYaw;

    FVector CurrentLocation = Slot->GetRelativeLocation();
    FRotator CurrentRotation = Slot->GetRelativeRotation();

    // Interp speed of 5.0f takes approximately 1 second to reach >99% of target
    const float InterpSpeed = 5.f;
    FVector NewLocation = FMath::VInterpTo(CurrentLocation, TargetLocation, DeltaTime, InterpSpeed);
    FRotator NewRotation = FMath::RInterpTo(CurrentRotation, TargetRotation, DeltaTime, InterpSpeed);

    Slot->SetRelativeLocationAndRotation(NewLocation, NewRotation);

    // Stop animating once we are close enough
    const bool bLocationAtTarget = NewLocation.Equals(TargetLocation, 0.1f);
    const bool bRotationAtTarget = NewRotation.Equals(TargetRotation, 0.1f);

    if (bLocationAtTarget && bRotationAtTarget)
    {
        Slot->SetRelativeLocationAndRotation(TargetLocation, TargetRotation);
        return false;
    }

    return true;
}

void AShowroomBooth::PostInitializeComponents()
{
    Super::PostInitializeComponents();

    // Ensure baseline transforms are captured from the starting level editor state at component initialization time
    EnsureBaselineTransformsCaptured();
}

void AShowroomBooth::EnsureBaselineTransformsCaptured()
{
    // Do not capture baseline transforms in the level editor, only in the game/PIE world.
    if (!GetWorld() || !GetWorld()->IsGameWorld())
    {
        return;
    }

    // In game world, if already captured, do not recapture
    if (bBaselineTransformsCaptured)
    {
        return;
    }

    if (SinkMesh)
    {
        BaselineSinkTransform = SinkMesh->GetRelativeTransform();
    }
    if (FaucetMesh)
    {
        BaselineFaucetTransform = FaucetMesh->GetRelativeTransform();
    }
    if (DoorMeshSlot0)
    {
        BaselineDoor0Transform = DoorMeshSlot0->GetRelativeTransform();
    }
    if (DoorMeshSlot1)
    {
        BaselineDoor1Transform = DoorMeshSlot1->GetRelativeTransform();
    }
    bBaselineTransformsCaptured = true;
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

    // Initialize ActiveState to default product options (Index 0 fallback) in the editor
    InitializeDefaultStateForProduct(ActiveState, InitialProductID, *ProductRow);

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

void AShowroomBooth::RequestComponentSelection(EFurnitureComponentType ComponentType, int32 SizeIndex, int32 ColorIndex)
{
    if (HasAuthority())
    {
        Server_ApplyComponentSelection_Implementation(ComponentType, SizeIndex, ColorIndex);
    }
    else
    {
        Server_ApplyComponentSelection(ComponentType, SizeIndex, ColorIndex);
    }
}

bool AShowroomBooth::Server_ApplyComponentSelection_Validate(EFurnitureComponentType ComponentType, int32 SizeIndex, int32 ColorIndex)
{
    return true;
}

void AShowroomBooth::Server_ApplyComponentSelection_Implementation(EFurnitureComponentType ComponentType, int32 SizeIndex, int32 ColorIndex)
{
    if (ComponentType == EFurnitureComponentType::Cabinet)
    {
        ActiveState.ActiveSizeIndex = SizeIndex;
        ActiveState.ActiveColorIndex = ColorIndex;
    }
    else if (ComponentType == EFurnitureComponentType::Countertop)
    {
        ActiveState.ActiveCountertopColorIndex = ColorIndex;
    }
    else if (ComponentType == EFurnitureComponentType::Closet)
    {
        ActiveState.ClosetSizeIndex = SizeIndex;
        ActiveState.ClosetColorIndex = ColorIndex;
    }
    else if (ComponentType == EFurnitureComponentType::Sink)
    {
        ActiveState.SinkSizeIndex = SizeIndex;
        ActiveState.SinkColorIndex = ColorIndex;
    }
    else if (ComponentType == EFurnitureComponentType::Faucet)
    {
        ActiveState.FaucetSizeIndex = SizeIndex;
        ActiveState.FaucetColorIndex = ColorIndex;
    }
    else if (ComponentType == EFurnitureComponentType::Mirror)
    {
        ActiveState.MirrorSizeIndex = SizeIndex;
        ActiveState.MirrorColorIndex = ColorIndex;
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
    EnsureBaselineTransformsCaptured();

    // ── 1. Cabinet ────────────────────────────────────────────────────────
    ApplyComponentMeshAndMaterials(MainCabinet.Get(), Data.CabinetOptions, ActiveState.ActiveSizeIndex, ActiveState.ActiveColorIndex);

    // ── 2. Closet ─────────────────────────────────────────────────────────
    ApplyComponentMeshAndMaterials(ClosetMesh.Get(), Data.ClosetOptions, ActiveState.ClosetSizeIndex, ActiveState.ClosetColorIndex);

    // ── 3. Doors ──────────────────────────────────────────────────────────
    ApplyDoorConfiguration(Data);

    // ── 4. Countertop ─────────────────────────────────────────────────────
    ApplyComponentMeshAndMaterials(CountertopMesh.Get(), Data.CountertopOptions, ActiveState.ActiveSizeIndex, ActiveState.ActiveCountertopColorIndex);

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
        ApplyComponentMeshAndMaterials(SinkMesh.Get(), Data.SinkOptions, ActiveState.SinkSizeIndex, ActiveState.SinkColorIndex);
    }

    // ── 6. Faucet ─────────────────────────────────────────────────────────
    ApplyComponentMeshAndMaterials(FaucetMesh.Get(), Data.FaucetOptions, ActiveState.FaucetSizeIndex, ActiveState.FaucetColorIndex);

    // ── 7. Mirror ─────────────────────────────────────────────────────────
    ApplyComponentMeshAndMaterials(MirrorMesh.Get(), Data.MirrorOptions, ActiveState.MirrorSizeIndex, ActiveState.MirrorColorIndex);

    // ── 8. Recalculate Sink + Faucet positions relative to Countertop ─────
    RecalculateDependentTransforms(Data);
}

void AShowroomBooth::ApplyComponentMeshAndMaterials(UStaticMeshComponent* Target,
                                                    const FFurnitureComponentOptions& Options,
                                                    int32 SizeIndex,
                                                    int32 ColorIndex)
{
    if (!Target)
    {
        return;
    }

    TSoftObjectPtr<UStaticMesh> TargetMeshPtr;
    if (Options.Sizes.IsValidIndex(SizeIndex))
    {
        TargetMeshPtr = Options.Sizes[SizeIndex];
    }
    else if (Options.Sizes.Num() > 0)
    {
        TargetMeshPtr = Options.Sizes[0];
    }

    if (TargetMeshPtr.IsNull() || TargetMeshPtr.ToSoftObjectPath().ToString().IsEmpty())
    {
        Target->SetStaticMesh(nullptr);
        Target->SetVisibility(false);
        Target->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        return;
    }

    UStaticMesh* LoadedMesh = TargetMeshPtr.LoadSynchronous();
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

        const FFurnitureColorOption* SelectedColor = nullptr;
        if (Options.Colors.IsValidIndex(ColorIndex))
        {
            SelectedColor = &Options.Colors[ColorIndex];
        }
        else if (Options.Colors.Num() > 0)
        {
            SelectedColor = &Options.Colors[0];
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

void AShowroomBooth::ApplyDoorMeshAndMaterials(UStaticMeshComponent* Target,
                                               const FFurnitureDoorOptions& Options,
                                               int32 SizeIndex,
                                               int32 ColorIndex,
                                               int32 SlotIndex)
{
    if (!Target)
    {
        return;
    }

    TSoftObjectPtr<UStaticMesh> TargetMeshPtr;
    if (Options.Sizes.IsValidIndex(SizeIndex))
    {
        const TArray<TSoftObjectPtr<UStaticMesh>>& Meshes = Options.Sizes[SizeIndex].DoorMeshes;
        if (Meshes.IsValidIndex(SlotIndex))
        {
            TargetMeshPtr = Meshes[SlotIndex];
        }
        else if (Meshes.Num() > 0)
        {
            TargetMeshPtr = Meshes[0]; // Fallback to first mesh if slot-specific mesh doesn't exist (e.g. symmetric layout)
        }
    }

    if (TargetMeshPtr.IsNull() || TargetMeshPtr.ToSoftObjectPath().ToString().IsEmpty())
    {
        Target->SetStaticMesh(nullptr);
        Target->SetVisibility(false);
        Target->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        return;
    }

    UStaticMesh* LoadedMesh = TargetMeshPtr.LoadSynchronous();
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

        const FFurnitureDoorColorOption* SelectedColor = nullptr;
        if (Options.Colors.IsValidIndex(ColorIndex))
        {
            SelectedColor = &Options.Colors[ColorIndex];
        }
        else if (Options.Colors.Num() > 0)
        {
            SelectedColor = &Options.Colors[0];
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
    ApplyDoorMeshAndMaterials(DoorMeshSlot0.Get(), Data.DoorOptions, ActiveState.ActiveSizeIndex, ActiveState.ActiveColorIndex, 0);
    ApplyDoorMeshAndMaterials(DoorMeshSlot1.Get(), Data.DoorOptions, ActiveState.ActiveSizeIndex, ActiveState.ActiveColorIndex, 1);

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

    // Apply visual state instantly without animation on full catalog/product change (runs on all machines).
    const FDoorSlotConfig Cfg0 = Data.DoorSlots.IsValidIndex(0) ? Data.DoorSlots[0] : FDoorSlotConfig();
    const FDoorSlotConfig Cfg1 = Data.DoorSlots.IsValidIndex(1) ? Data.DoorSlots[1] : FDoorSlotConfig();
    ApplyDoorSlotVisual(DoorMeshSlot0.Get(), DoorStates[0], Cfg0, false);
    ApplyDoorSlotVisual(DoorMeshSlot1.Get(), DoorStates[1], Cfg1, false);
}

void AShowroomBooth::ApplyDoorSlotVisual(UStaticMeshComponent* Slot,
                                         EDoorSlotState State,
                                         const FDoorSlotConfig& SlotCfg,
                                         bool bAnimate)
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

    // Determine the baseline transform for this door slot
    FTransform BaselineTransform = FTransform::Identity;
    if (Slot == DoorMeshSlot0.Get())
    {
        BaselineTransform = BaselineDoor0Transform;
    }
    else if (Slot == DoorMeshSlot1.Get())
    {
        BaselineTransform = BaselineDoor1Transform;
    }

    // Safety check: ensure scale is not zero
    if (BaselineTransform.GetScale3D().IsNearlyZero())
    {
        BaselineTransform.SetScale3D(FVector::OneVector);
    }

    if (GetWorld() && !GetWorld()->IsGameWorld())
    {
        // In the editor, do not proceduralize transforms so we do not break native editor overrides
        return;
    }

    if (bAnimate)
    {
        // Start the timer to smoothly interpolate the door meshes to their target state
        if (GetWorld())
        {
            if (!GetWorld()->GetTimerManager().IsTimerActive(DoorAnimationTimerHandle))
            {
                GetWorld()->GetTimerManager().SetTimer(DoorAnimationTimerHandle, this, &AShowroomBooth::UpdateDoorAnimation, 0.016f, true);
            }
        }
    }
    else
    {
        // Instantly snap to the target position
        FVector TargetLocation = BaselineTransform.GetLocation();
        if (!SlotCfg.bIsRotation && State == EDoorSlotState::Open)
        {
            TargetLocation = BaselineTransform.GetLocation() + SlotCfg.OpenTranslationOffset;
        }

        float TargetYaw = BaselineTransform.Rotator().Yaw;
        if (SlotCfg.bIsRotation && State == EDoorSlotState::Open)
        {
            TargetYaw = BaselineTransform.Rotator().Yaw + SlotCfg.OpenYawDelta;
        }
        FRotator TargetRotation = BaselineTransform.Rotator();
        TargetRotation.Yaw = TargetYaw;

        Slot->SetRelativeLocationAndRotation(TargetLocation, TargetRotation);
        Slot->SetRelativeScale3D(BaselineTransform.GetScale3D());
    }
}

void AShowroomBooth::RecalculateDependentTransforms(const FFurnitureProductRow& Data)
{
    // All transforms are expressed in CountertopMesh local space so they
    // stay correct regardless of how the designer positioned the countertop.

    if (GetWorld() && !GetWorld()->IsGameWorld())
    {
        // In the editor, do not proceduralize transforms so we do not break native editor overrides
        return;
    }

    // ── Sink ──────────────────────────────────────────────────────────────
    if (SinkMesh && Data.CountertopType == ECountertopType::SurfaceMounted)
    {
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

void AShowroomBooth::InitializeDefaultStateForProduct(FShowroomBoothConfigState& State, FName ProductID, const FFurnitureProductRow& Row)
{
    State.ProductID = ProductID;
    State.ActiveSizeIndex = 0;
    State.ActiveColorIndex = 0;
    State.ActiveCountertopColorIndex = 0;
    State.ClosetSizeIndex = 0;
    State.ClosetColorIndex = 0;
    State.SinkSizeIndex = 0;
    State.SinkColorIndex = 0;
    State.FaucetSizeIndex = 0;
    State.FaucetColorIndex = 0;
    State.MirrorSizeIndex = 0;
    State.MirrorColorIndex = 0;
}
