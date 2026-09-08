// Copyright 2026 MaxiMall. All Rights Reserved.

#include "Constructor/RoomPlannerManager.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Net/UnrealNetwork.h"
#include "UObject/UObjectIterator.h"
#include "Materials/Material.h"
#include "Materials/MaterialInterface.h"
#include "MaterialDomain.h"
#include "Engine/EngineTypes.h"
#include "EngineUtils.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "JsonObjectConverter.h"
#include "PixelStreamingInputComponent.h"
#include "Constructor/PlannerPlacedObjectActor.h"
#include "FurnitureConfigurator/ShowroomBooth.h"
#include "Engine/DataTable.h"
#include "Engine/StaticMesh.h"
#include "Engine/HitResult.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/Guid.h"
#include "Framework/Application/SlateApplication.h"
#include "awsTutorial_PlayerController.h"

namespace PlannerJsonKeys
{
	static const TCHAR* FinishTypeToString(ESurfaceFinishType Type)
	{
		switch (Type)
		{
		case ESurfaceFinishType::Paint: return TEXT("paint");
		case ESurfaceFinishType::Tile:  return TEXT("tile");
		default: return TEXT("none");
		}
	}
	static ESurfaceFinishType FinishTypeFromString(const FString& S)
	{
		if (S.Equals(TEXT("paint"), ESearchCase::IgnoreCase)) return ESurfaceFinishType::Paint;
		if (S.Equals(TEXT("tile"), ESearchCase::IgnoreCase)) return ESurfaceFinishType::Tile;
		return ESurfaceFinishType::None;
	}
	static FString NewInstanceID()
	{
		return FGuid::NewGuid().ToString(EGuidFormats::Short);
	}
	static FString FormatMeters(float Cm)
	{
		return FString::Printf(TEXT("%.2f м"), Cm / 100.f);
	}
}

ARoomPlannerManager::ARoomPlannerManager()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	bAlwaysRelevant = true;
	AActor::SetReplicateMovement(false);
	NetCullDistanceSquared = 0.0f;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);

	FloorProceduralMesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("FloorProceduralMesh"));
	FloorProceduralMesh->SetupAttachment(SceneRoot);
	FloorProceduralMesh->bUseAsyncCooking = true;
	FloorProceduralMesh->bUseComplexAsSimpleCollision = true;
	FloorProceduralMesh->SetCastShadow(false);
	FloorProceduralMesh->SetAbsolute(true, true, true);

	CeilingProceduralMesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("CeilingProceduralMesh"));
	CeilingProceduralMesh->SetupAttachment(SceneRoot);
	CeilingProceduralMesh->bUseAsyncCooking = true;
	CeilingProceduralMesh->bUseComplexAsSimpleCollision = true;
	CeilingProceduralMesh->SetCastShadow(true);
	CeilingProceduralMesh->SetAbsolute(true, true, true);

	BaseboardProceduralMesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("BaseboardProceduralMesh"));
	BaseboardProceduralMesh->SetupAttachment(SceneRoot);
	BaseboardProceduralMesh->bUseAsyncCooking = true;
	BaseboardProceduralMesh->bUseComplexAsSimpleCollision = false;
	BaseboardProceduralMesh->SetCastShadow(false);
	BaseboardProceduralMesh->SetAbsolute(true, true, true);

	NodeHandleMesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("NodeHandleMesh"));
	NodeHandleMesh->SetupAttachment(SceneRoot);
	NodeHandleMesh->bUseAsyncCooking = false;
	NodeHandleMesh->SetCastShadow(false);
	NodeHandleMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	NodeHandleMesh->SetAbsolute(true, true, true);
	NodeHandleMesh->SetVisibility(false);

	PlacedObjectActorClass = APlannerPlacedObjectActor::StaticClass();

	WallSelectionMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/RoomPlanner/Materials/M_WallSelection.M_WallSelection"));
	if (!WallSelectionMaterial)
	{
		WallSelectionMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Constructor/Materials/M_WallSelection.M_WallSelection"));
	}

	OpeningSelectionMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/RoomPlanner/Materials/M_OpeningSelection.M_OpeningSelection"));
	if (!OpeningSelectionMaterial)
	{
		OpeningSelectionMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Constructor/Materials/M_OpeningSelection.M_OpeningSelection"));
	}

	bCeilingVisible = true;
}

void ARoomPlannerManager::BeginPlay()
{
	Super::BeginPlay();

	SetActorLocationAndRotation(FVector::ZeroVector, FRotator::ZeroRotator);
	SetActorScale3D(FVector(1.f, 1.f, 1.f));

	UE_LOG(LogTemp, Warning, TEXT("[MaxiMallConstructor] ARoomPlannerManager BeginPlay"));
}

void ARoomPlannerManager::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (ActiveToolMode == EPlannerToolMode::Select)
	{
		// We no longer need to track drag state here since openings stay selected on click!
		bWasDraggingOpening = false;
	}

	TickLocalNodeDrag();

	if (!BoundPSInput.IsValid())
	{
		for (TObjectIterator<UPixelStreamingInput> It; It; ++It)
		{
			if (UPixelStreamingInput* PSInput = *It)
			{
				PSInput->OnInputEvent.AddUniqueDynamic(this, &ARoomPlannerManager::OnPixelStreamingInputReceived);
				BoundPSInput = PSInput;
				UE_LOG(LogTemp, Warning, TEXT("[MaxiMallConstructor] ARoomPlannerManager bound directly to UPixelStreamingInput %s via TObjectIterator!"), *PSInput->GetName());
				break;
			}
		}
	}
}

void ARoomPlannerManager::OnPixelStreamingInputReceived(const FString& Descriptor)
{
	UE_LOG(LogTemp, Warning, TEXT("[MaxiMallConstructor] OnPixelStreamingInputReceived raw Descriptor: %s"), *Descriptor);

	FString CleanDescriptor = Descriptor;
	CleanDescriptor.ReplaceInline(TEXT("\0"), TEXT(""));
	if (CleanDescriptor.StartsWith(TEXT("UIInteraction:")))
	{
		CleanDescriptor = CleanDescriptor.Mid(14).TrimStart();
	}
	else if (CleanDescriptor.StartsWith(TEXT("UIInteraction")))
	{
		CleanDescriptor = CleanDescriptor.Mid(13).TrimStart();
	}

	TSharedPtr<FJsonObject> JsonObject;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(CleanDescriptor);

	if (!FJsonSerializer::Deserialize(Reader, JsonObject) || !JsonObject.IsValid())
	{
		return;
	}

	FString EffectiveJSON = Descriptor;
	if (JsonObject->HasField(TEXT("descriptor")))
	{
		EffectiveJSON = JsonObject->GetStringField(TEXT("descriptor"));
		TSharedPtr<FJsonObject> InnerObject;
		TSharedRef<TJsonReader<>> InnerReader = TJsonReaderFactory<>::Create(EffectiveJSON);
		if (FJsonSerializer::Deserialize(InnerReader, InnerObject) && InnerObject.IsValid())
		{
			JsonObject = InnerObject;
		}
	}
	else if (JsonObject->HasField(TEXT("Descriptor")))
	{
		EffectiveJSON = JsonObject->GetStringField(TEXT("Descriptor"));
		TSharedPtr<FJsonObject> InnerObject;
		TSharedRef<TJsonReader<>> InnerReader = TJsonReaderFactory<>::Create(EffectiveJSON);
		if (FJsonSerializer::Deserialize(InnerReader, InnerObject) && InnerObject.IsValid())
		{
			JsonObject = InnerObject;
		}
	}

	if (JsonObject->HasField(TEXT("cmd")) || JsonObject->HasField(TEXT("Cmd")))
	{
		FString CmdVal = JsonObject->HasField(TEXT("cmd")) ? JsonObject->GetStringField(TEXT("cmd")) : JsonObject->GetStringField(TEXT("Cmd"));
		if (CmdVal.StartsWith(TEXT("add_wall")) || CmdVal.StartsWith(TEXT("add_opening")) || CmdVal.StartsWith(TEXT("clear")) || CmdVal.StartsWith(TEXT("get_state")))
		{
			UE_LOG(LogTemp, Warning, TEXT("[MaxiMallConstructor] OnPixelStreamingInputReceived matched cmd: %s"), *CmdVal);
			FString Response = ProcessCommandJSON(EffectiveJSON);
			if (BoundPSInput.IsValid())
			{
				if (UPixelStreamingInput* PSInput = Cast<UPixelStreamingInput>(BoundPSInput.Get()))
				{
					PSInput->SendPixelStreamingResponse(FString::Printf(TEXT("MaxiMallConstructor:%s"), *Response));
				}
			}
		}
	}
}

ARoomPlannerManager* ARoomPlannerManager::GetOrCreateInstance(UWorld* World)
{
	if (!World)
	{
		return nullptr;
	}

	for (TActorIterator<ARoomPlannerManager> It(World); It; ++It)
	{
		return *It;
	}

	if (World->GetNetMode() != NM_Client)
	{
		FActorSpawnParameters SpawnParams;
		SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		return World->SpawnActor<ARoomPlannerManager>(ARoomPlannerManager::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams);
	}

	return nullptr;
}

int32 ARoomPlannerManager::AddNode(const FVector2D& Position)
{
	return GetOrCreateNodeAtPosition(Position, 25.f, 20.f);
}

int32 ARoomPlannerManager::GetOrCreateNodeAtPosition(const FVector2D& Position, float NodeSnapRadiusCm, float WallSnapRadiusCm)
{
	// 1. Check for snapping to existing node
	float BestNodeDistSq = NodeSnapRadiusCm * NodeSnapRadiusCm;
	int32 ClosestNodeID = INDEX_NONE;
	for (const TPair<int32, FWallNode>& Pair : Nodes)
	{
		float DistSq = FVector2D::DistSquared(Pair.Value.Position, Position);
		if (DistSq <= BestNodeDistSq)
		{
			BestNodeDistSq = DistSq;
			ClosestNodeID = Pair.Key;
		}
	}
	if (ClosestNodeID != INDEX_NONE)
	{
		return ClosestNodeID;
	}

	// 2. Check for mid-wall T-junction intersection along existing wall segments
	float BestWallDist = WallSnapRadiusCm;
	int32 TargetSegID = INDEX_NONE;
	FVector2D TargetSplitPoint = Position;

	for (const TPair<int32, FWallSegment>& Pair : WallSegments)
	{
		const FWallSegment& Seg = Pair.Value;
		if (Nodes.Contains(Seg.StartNodeID) && Nodes.Contains(Seg.EndNodeID))
		{
			FVector2D P1 = Nodes[Seg.StartNodeID].Position;
			FVector2D P2 = Nodes[Seg.EndNodeID].Position;
			FVector2D SegVec = P2 - P1;
			float SegLen = SegVec.Size();
			if (SegLen < 1.0f) continue;

			FVector2D SegDir = SegVec / SegLen;
			float t = FVector2D::DotProduct(Position - P1, SegDir);

			// Keep at least 20cm away from endpoints to avoid microscopic slivers
			if (t >= 20.f && t <= (SegLen - 20.f))
			{
				FVector2D ProjPoint = P1 + SegDir * t;
				float DistToCenterline = FVector2D::Distance(Position, ProjPoint);
				float MaxSnapDist = (Seg.Thickness * 0.5f) + WallSnapRadiusCm;

				if (DistToCenterline <= MaxSnapDist && DistToCenterline < BestWallDist)
				{
					BestWallDist = DistToCenterline;
					TargetSegID = Pair.Key;
					TargetSplitPoint = ProjPoint;
				}
			}
		}
	}

	if (TargetSegID != INDEX_NONE)
	{
		return SplitWallSegment(TargetSegID, TargetSplitPoint);
	}

	// 3. Create brand new standalone node
	int32 NewID = NextNodeID++;
	FWallNode Node;
	Node.NodeID = NewID;
	Node.Position = Position;
	Nodes.Add(NewID, Node);
	return NewID;
}

int32 ARoomPlannerManager::SplitWallSegment(int32 SegmentID, const FVector2D& SplitPos)
{
	if (!WallSegments.Contains(SegmentID))
	{
		return INDEX_NONE;
	}

	FWallSegment OldSeg = WallSegments[SegmentID];
	if (!Nodes.Contains(OldSeg.StartNodeID) || !Nodes.Contains(OldSeg.EndNodeID))
	{
		return INDEX_NONE;
	}

	int32 NodeA = OldSeg.StartNodeID;
	int32 NodeB = OldSeg.EndNodeID;
	FVector2D PosA = Nodes[NodeA].Position;
	float SplitDist = FVector2D::Distance(PosA, SplitPos);

	// 1. Create the Junction Node J
	int32 JunctionNodeID = NextNodeID++;
	FWallNode JNode;
	JNode.NodeID = JunctionNodeID;
	JNode.Position = SplitPos;
	Nodes.Add(JunctionNodeID, JNode);

	// 2. Separate existing openings between Segment 1 (A -> J) and Segment 2 (J -> B)
	TArray<FWallOpening> Openings1;
	TArray<FWallOpening> Openings2;

	for (const FWallOpening& Op : OldSeg.Openings)
	{
		if (Op.DistanceFromStart < SplitDist)
		{
			Openings1.Add(Op);
		}
		else
		{
			FWallOpening ShiftedOp = Op;
			ShiftedOp.DistanceFromStart = FMath::Max(10.f, Op.DistanceFromStart - SplitDist);
			Openings2.Add(ShiftedOp);
		}
	}

	// 3. Modify existing segment to become (NodeA -> JunctionNodeID)
	FWallSegment& Seg1 = WallSegments[SegmentID];
	Seg1.EndNodeID = JunctionNodeID;
	Seg1.Openings = Openings1;

	// Update node connectivity
	Nodes[NodeB].ConnectedSegmentIDs.Remove(SegmentID);
	Nodes[JunctionNodeID].ConnectedSegmentIDs.AddUnique(SegmentID);

	// Update actor for Seg1
	if (TObjectPtr<AProceduralWallActor>* Actor1Ptr = WallActors.Find(SegmentID))
	{
		if (*Actor1Ptr)
		{
			(*Actor1Ptr)->WallData = Seg1;
		}
	}

	// 4. Create new segment (JunctionNodeID -> NodeB)
	int32 Seg2ID = NextSegmentID++;
	FWallSegment Seg2;
	Seg2.SegmentID = Seg2ID;
	Seg2.StartNodeID = JunctionNodeID;
	Seg2.EndNodeID = NodeB;
	Seg2.Thickness = OldSeg.Thickness;
	Seg2.Height = OldSeg.Height;
	Seg2.Openings = Openings2;
	Seg2.Finish = OldSeg.Finish;              // a split wall keeps its finish on both halves (REQ-13)
	Seg2.bLeftSideIsInterior = OldSeg.bLeftSideIsInterior;
	Seg2.WallGuid = PlannerJsonKeys::NewInstanceID();

	WallSegments.Add(Seg2ID, Seg2);
	Nodes[JunctionNodeID].ConnectedSegmentIDs.AddUnique(Seg2ID);
	Nodes[NodeB].ConnectedSegmentIDs.AddUnique(Seg2ID);

	// Spawn actor for Seg2
	if (GetWorld())
	{
		FActorSpawnParameters SpawnParams;
		SpawnParams.Owner = this;
		AProceduralWallActor* WallActor2 = GetWorld()->SpawnActor<AProceduralWallActor>(AProceduralWallActor::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams);
		if (WallActor2)
		{
			WallActor2->WallData = Seg2;
			WallActor2->BaseWallMaterial = DefaultWallMaterial;
			WallActor2->WallSelectionMaterial = WallSelectionMaterial;
			WallActor2->OpeningSelectionMaterial = OpeningSelectionMaterial;
			WallActor2->LeafMaterial = LeafMaterial;
			WallActors.Add(Seg2ID, WallActor2);
		}
	}

	RebuildAllWalls();
	RebuildRooms();

	return JunctionNodeID;
}

int32 ARoomPlannerManager::AddWall(int32 StartNodeID, int32 EndNodeID, float Thickness, float Height)
{
	if (StartNodeID == EndNodeID || !Nodes.Contains(StartNodeID) || !Nodes.Contains(EndNodeID))
	{
		return -1;
	}

	// Check if wall segment between these nodes already exists
	for (const TPair<int32, FWallSegment>& Pair : WallSegments)
	{
		if ((Pair.Value.StartNodeID == StartNodeID && Pair.Value.EndNodeID == EndNodeID) ||
		    (Pair.Value.StartNodeID == EndNodeID && Pair.Value.EndNodeID == StartNodeID))
		{
			return Pair.Key;
		}
	}

	int32 SegID = NextSegmentID++;
	FWallSegment Segment;
	Segment.SegmentID = SegID;
	Segment.WallGuid = PlannerJsonKeys::NewInstanceID();
	Segment.StartNodeID = StartNodeID;
	Segment.EndNodeID = EndNodeID;
	Segment.Thickness = Thickness;
	Segment.Height = Height;

	WallSegments.Add(SegID, Segment);

	Nodes[StartNodeID].ConnectedSegmentIDs.AddUnique(SegID);
	Nodes[EndNodeID].ConnectedSegmentIDs.AddUnique(SegID);

	// Spawn 3D Wall Actor
	if (GetWorld())
	{
		FActorSpawnParameters SpawnParams;
		SpawnParams.Owner = this;
		AProceduralWallActor* WallActor = GetWorld()->SpawnActor<AProceduralWallActor>(AProceduralWallActor::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams);
		if (WallActor)
		{
			WallActor->WallData = Segment;
			WallActor->BaseWallMaterial = DefaultWallMaterial;
			WallActor->WallSelectionMaterial = WallSelectionMaterial;
			WallActor->OpeningSelectionMaterial = OpeningSelectionMaterial;
			WallActor->LeafMaterial = LeafMaterial;
			WallActors.Add(SegID, WallActor);
		}
	}

	RebuildAllWalls();
	RebuildRooms();

	return SegID;
}

bool ARoomPlannerManager::AddOpeningToWall(int32 SegmentID, EOpeningType Type, float DistFromStart, float Width, float Height, float SillHeight)
{
	FWallSegment* Seg = WallSegments.Find(SegmentID);
	if (!Seg)
	{
		return false;
	}

	FWallOpening Opening;
	Opening.OpeningID = FString::Printf(TEXT("Op_%d_%d"), SegmentID, Seg->Openings.Num() + 1);
	Opening.Type = Type;
	Opening.DistanceFromStart = DistFromStart;
	Opening.Width = Width;
	Opening.Height = Height;
	Opening.SillHeight = SillHeight;

	Seg->Openings.Add(Opening);

	if (TObjectPtr<AProceduralWallActor>* ActorPtr = WallActors.Find(SegmentID))
	{
		if (*ActorPtr)
		{
			(*ActorPtr)->WallData = *Seg;
		}
	}

	RebuildAllWalls();
	return true;
}

void ARoomPlannerManager::RemoveWall(int32 SegmentID)
{
	FWallSegment Seg;
	if (!WallSegments.RemoveAndCopyValue(SegmentID, Seg))
	{
		return;
	}

	if (SelectedSegmentID == SegmentID)
	{
		ClearWallSelection();
	}

	if (FWallNode* StartNode = Nodes.Find(Seg.StartNodeID))
	{
		StartNode->ConnectedSegmentIDs.Remove(SegmentID);
		if (StartNode->ConnectedSegmentIDs.Num() == 0)
		{
			Nodes.Remove(Seg.StartNodeID);
		}
	}
	if (FWallNode* EndNode = Nodes.Find(Seg.EndNodeID))
	{
		EndNode->ConnectedSegmentIDs.Remove(SegmentID);
		if (EndNode->ConnectedSegmentIDs.Num() == 0)
		{
			Nodes.Remove(Seg.EndNodeID);
		}
	}

	if (TObjectPtr<AProceduralWallActor>* ActorPtr = WallActors.Find(SegmentID))
	{
		if (*ActorPtr && (*ActorPtr)->IsValidLowLevel())
		{
			if ((*ActorPtr)->WallProceduralMesh)
			{
				(*ActorPtr)->WallProceduralMesh->ClearAllMeshSections();
				(*ActorPtr)->WallProceduralMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			}
			(*ActorPtr)->SetActorEnableCollision(false);
			(*ActorPtr)->Destroy();
		}
		WallActors.Remove(SegmentID);
	}

	RebuildAllWalls();
	RebuildRooms();
	ReplicatedRoomJSON = ExportLayoutToJSON();
	OnRoomPlannerUpdated.Broadcast(ReplicatedRoomJSON);
	OnWallSelected.Broadcast(-1, 0.f);
}

void ARoomPlannerManager::ClearWallsAndRooms()
{
	for (auto& Pair : WallActors)
	{
		if (Pair.Value && Pair.Value->IsValidLowLevel())
		{
			Pair.Value->Destroy();
		}
	}
	WallActors.Empty();
	WallSegments.Empty();
	Nodes.Empty();
	Rooms.Empty();
	FloorSectionMaterials.Empty();
	NextNodeID = 1;
	NextSegmentID = 1;
	DraggingNodeID = -1;

	if (FloorProceduralMesh) FloorProceduralMesh->ClearAllMeshSections();
	if (CeilingProceduralMesh) CeilingProceduralMesh->ClearAllMeshSections();
	if (BaseboardProceduralMesh) BaseboardProceduralMesh->ClearAllMeshSections();
	if (NodeHandleMesh) NodeHandleMesh->ClearAllMeshSections();
}

void ARoomPlannerManager::ClearLayout()
{
	// Walls / nodes / rooms
	ClearWallsAndRooms();

	// Finishes
	FloorFinishes.Empty();

	// Placed interior objects (local actors on every machine)
	for (auto& Pair : PlacedObjectActors)
	{
		if (Pair.Value && Pair.Value->IsValidLowLevel())
		{
			Pair.Value->Destroy();
		}
	}
	PlacedObjectActors.Empty();
	PlacedObjects.Empty();

	// Cabinet sets (replicated booths — only the server destroys them)
	CabinetSets.Empty();
	if (HasAuthority())
	{
		DestroyAllPlannerCabinetSets();
	}
	CabinetSetActorCache.Empty();

	SelectedSegmentID = -1;
	SelectedOpeningIndex = -1;
	SelectedRoomID = -1;
	SelectedObjectID.Empty();
	SelectedCabinetSetID.Empty();
	PendingPlacementKind = EPlannerPlacementKind::None;
	PendingPlacementAssetID.Empty();
}

void ARoomPlannerManager::ComputeMiterOffsetsAtNode(int32 NodeID, TMap<int32, FVector2D>& OutStartLeftOffsets,
                                                     TMap<int32, FVector2D>& OutStartRightOffsets,
                                                     TMap<int32, FVector2D>& OutEndLeftOffsets,
                                                     TMap<int32, FVector2D>& OutEndRightOffsets)
{
	// Centerline extension logic is handled in RebuildAllWalls / ComputeMitreCorner to ensure clean corner closing.
}

void ARoomPlannerManager::ComputeAllCornerJoints()
{
	CornerJoints.Reset();

	struct FEntry
	{
		int32 SegID = -1;
		FVector2D A = FVector2D::ZeroVector;     // outgoing direction from the node into the wall
		FVector2D NLeft = FVector2D::ZeroVector; // the wall's own LEFT face normal (start->end terms, matches the mesh builder)
		bool bCCWIsLeft = true;                  // true when the counter-clockwise face (w.r.t. A) is the wall's LEFT face
		float Half = 10.f;
		float Thickness = 20.f;
		float Len = 0.f;
		float Angle = 0.f;
	};

	auto Cross2 = [](const FVector2D& U, const FVector2D& V) { return U.X * V.Y - U.Y * V.X; };

	for (const auto& NodePair : Nodes)
	{
		const int32 NodeID = NodePair.Key;
		const FVector2D N = NodePair.Value.Position;

		// Gather the walls leaving this node.
		TArray<FEntry> Entries;
		for (int32 SegID : NodePair.Value.ConnectedSegmentIDs)
		{
			const FWallSegment* Seg = WallSegments.Find(SegID);
			if (!Seg || (Seg->StartNodeID != NodeID && Seg->EndNodeID != NodeID)) continue;
			const FWallNode* S = Nodes.Find(Seg->StartNodeID);
			const FWallNode* E = Nodes.Find(Seg->EndNodeID);
			if (!S || !E) continue;
			const float Len = FVector2D::Distance(S->Position, E->Position);
			if (Len < 1.f) continue;

			FEntry En;
			En.SegID = SegID;
			const FVector2D Dir = (E->Position - S->Position) / Len;
			En.NLeft = FVector2D(-Dir.Y, Dir.X);
			const bool bAtStart = (Seg->StartNodeID == NodeID);
			En.A = bAtStart ? Dir : -Dir;
			En.bCCWIsLeft = bAtStart; // CCW normal of A is (-A.Y, A.X): equals NLeft when A == Dir, -NLeft when A == -Dir
			En.Half = Seg->Thickness * 0.5f;
			En.Thickness = Seg->Thickness;
			En.Len = Len;
			En.Angle = FMath::Atan2(En.A.Y, En.A.X);
			Entries.Add(En);
		}
		if (Entries.Num() == 0) continue;

		// Per-wall face state at this node (indexed like Entries): CCW face / CW face w.r.t. the wall's outgoing direction.
		const int32 NumWalls = Entries.Num();
		TArray<bool> bCCWMitred, bCWMitred;
		TArray<FVector2D> CCWPoint, CWPoint;
		TArray<int32> CCWPartner, CWPartner;
		bCCWMitred.Init(false, NumWalls);
		bCWMitred.Init(false, NumWalls);
		CCWPoint.Init(FVector2D::ZeroVector, NumWalls);
		CWPoint.Init(FVector2D::ZeroVector, NumWalls);
		CCWPartner.Init(-1, NumWalls);
		CWPartner.Init(-1, NumWalls);

		if (NumWalls >= 2)
		{
			// Counter-clockwise order around the node.
			TArray<int32> Order;
			for (int32 i = 0; i < NumWalls; ++i) Order.Add(i);
			Order.Sort([&Entries](int32 L, int32 R) { return Entries[L].Angle < Entries[R].Angle; });

			const int32 Count = Order.Num();
			for (int32 k = 0; k < Count; ++k)
			{
				const int32 i = Order[k];
				const int32 j = Order[(k + 1) % Count]; // next wall counter-clockwise
				if (i == j) continue;

				const FEntry& Wi = Entries[i];
				const FEntry& Wj = Entries[j];

				const float Cross = Cross2(Wi.A, Wj.A);
				if (FMath::Abs(Cross) < 0.02f) continue; // parallel / collinear: flush caps

				// Wedge between wall i and wall j is bounded by i's CCW face and j's CW face.
				const FVector2D NCCW_i(-Wi.A.Y, Wi.A.X);
				const FVector2D NCW_j(Wj.A.Y, -Wj.A.X);
				const FVector2D P1 = N + NCCW_i * Wi.Half;
				const FVector2D P2 = N + NCW_j * Wj.Half;
				const FVector2D D = P2 - P1;

				// P1 + t*Ai = P2 + s*Aj
				const float T = Cross2(D, Wj.A) / Cross;
				const float Sv = Cross2(D, Wi.A) / Cross;

				// One shared decision for the pair.
				const float Limit = FMath::Min(3.f * FMath::Max(Wi.Thickness, Wj.Thickness), 0.5f * FMath::Min(Wi.Len, Wj.Len));
				if (FMath::Abs(T) > Limit || FMath::Abs(Sv) > Limit) continue;

				const FVector2D Point = P1 + Wi.A * T;

				bCCWMitred[i] = true; CCWPoint[i] = Point; CCWPartner[i] = j;
				bCWMitred[j] = true;  CWPoint[j] = Point;  CWPartner[j] = i;
			}

			// A wall end may stay mitred only when BOTH of its faces are mitred. A half-mitred end would be
			// drawn with a diagonal cap across the wall body (holes at T-junctions, spikes at acute branches).
			// Reverting a wall also un-mitres the partner faces that were paired with it, until stable.
			bool bChanged = true;
			while (bChanged)
			{
				bChanged = false;
				for (int32 i = 0; i < NumWalls; ++i)
				{
					if (bCCWMitred[i] == bCWMitred[i]) continue; // both or none: consistent

					if (bCCWMitred[i])
					{
						bCCWMitred[i] = false;
						const int32 j = CCWPartner[i];
						if (j >= 0 && bCWMitred[j] && CWPartner[j] == i)
						{
							bCWMitred[j] = false;
						}
					}
					if (bCWMitred[i])
					{
						bCWMitred[i] = false;
						const int32 k = CWPartner[i];
						if (k >= 0 && bCCWMitred[k] && CCWPartner[k] == i)
						{
							bCCWMitred[k] = false;
						}
					}
					bChanged = true;
				}
			}
		}

		// Build the joints: plain node offsets by default, mitre points where the pair survived.
		for (int32 i = 0; i < NumWalls; ++i)
		{
			const FEntry& W = Entries[i];
			FWallCornerJoint J;
			J.Left = N + W.NLeft * W.Half;
			J.Right = N - W.NLeft * W.Half;

			if (bCCWMitred[i])
			{
				if (W.bCCWIsLeft) { J.Left = CCWPoint[i]; J.bLeftMitred = true; }
				else              { J.Right = CCWPoint[i]; J.bRightMitred = true; }
			}
			if (bCWMitred[i])
			{
				if (W.bCCWIsLeft) { J.Right = CWPoint[i]; J.bRightMitred = true; }
				else              { J.Left = CWPoint[i]; J.bLeftMitred = true; }
			}
			CornerJoints.Add(MakeJointKey(W.SegID, NodeID), J);
		}
	}
}

void ARoomPlannerManager::RebuildAllWalls()
{
	ComputeAllCornerJoints();

	for (auto& Pair : WallActors)
	{
		int32 SegID = Pair.Key;
		AProceduralWallActor* WallActor = Pair.Value;
		const FWallSegment* Seg = WallSegments.Find(SegID);

		if (WallActor && Seg && Nodes.Contains(Seg->StartNodeID) && Nodes.Contains(Seg->EndNodeID))
		{
			FVector2D StartPos = Nodes[Seg->StartNodeID].Position;
			FVector2D EndPos = Nodes[Seg->EndNodeID].Position;

			FVector2D Dir = (EndPos - StartPos).GetSafeNormal();
			FVector2D Normal(-Dir.Y, Dir.X);
			float HalfThick = Seg->Thickness * 0.5f;

			FVector2D SL2D = StartPos + Normal * HalfThick;
			FVector2D SR2D = StartPos - Normal * HalfThick;
			FVector2D EL2D = EndPos + Normal * HalfThick;
			FVector2D ER2D = EndPos - Normal * HalfThick;

			bool bStartCap = true;
			bool bEndCap = true;

			// Corner joints from the per-node pre-pass (shared decision per face pair, 2..N walls per node).
			// A cap is drawn whenever at least one face of this end is not mitred.
			if (const FWallCornerJoint* J = CornerJoints.Find(MakeJointKey(SegID, Seg->StartNodeID)))
			{
				SL2D = J->Left;
				SR2D = J->Right;
				bStartCap = !(J->bLeftMitred && J->bRightMitred);
			}
			if (const FWallCornerJoint* J = CornerJoints.Find(MakeJointKey(SegID, Seg->EndNodeID)))
			{
				EL2D = J->Left;
				ER2D = J->Right;
				bEndCap = !(J->bLeftMitred && J->bRightMitred);
			}

			WallActor->WallData = *Seg;

			// Keep the wall's finish material in sync with its data (REQ-13); only rebuild the instance when the finish changed.
			if (Seg->Finish != WallActor->AppliedFinish || (Seg->Finish.IsSet() && !WallActor->FinishMaterial))
			{
				WallActor->SetFinishMaterial(Seg->Finish.IsSet() ? CreateFinishMaterialInstance(Seg->Finish, WallActor) : nullptr);
				WallActor->AppliedFinish = Seg->Finish;
			}

			WallActor->RebuildWallMesh(StartPos, EndPos, SL2D, SR2D, EL2D, ER2D, bStartCap, bEndCap, true);
		}
	}

	RefreshNodeHandles();
}

void ARoomPlannerManager::ToggleCeilingVisibility()
{
	SetCeilingVisibility(!bCeilingVisible);
}

void ARoomPlannerManager::SetCeilingVisibility(bool bVisible)
{
	bCeilingVisible = bVisible;
	if (CeilingProceduralMesh)
	{
		CeilingProceduralMesh->SetVisibility(bCeilingVisible && !b2DViewMode);
	}
}

void ARoomPlannerManager::RebuildRooms()
{
	Rooms.Empty();
	if (FloorProceduralMesh) FloorProceduralMesh->ClearAllMeshSections();
	if (CeilingProceduralMesh) CeilingProceduralMesh->ClearAllMeshSections();
	if (BaseboardProceduralMesh) BaseboardProceduralMesh->ClearAllMeshSections();

	if (Nodes.Num() < 3 || WallSegments.Num() < 3)
	{
		ComputeWallInteriorSides(TArray<TArray<FVector2D>>{});
		return;
	}

	// 1. Build adjacency map for 2-core degree pruning
	TMap<int32, TSet<int32>> NodeNeighbors;
	for (const auto& Pair : WallSegments)
	{
		const FWallSegment& Seg = Pair.Value;
		if (Nodes.Contains(Seg.StartNodeID) && Nodes.Contains(Seg.EndNodeID))
		{
			int32 u = Seg.StartNodeID;
			int32 v = Seg.EndNodeID;
			if (u != v)
			{
				NodeNeighbors.FindOrAdd(u).Add(v);
				NodeNeighbors.FindOrAdd(v).Add(u);
			}
		}
	}

	// 2. Iteratively prune leaves and dead-end spurs (degree < 2)
	// Open walls, standalone segments, and unclosed U-shapes are eliminated!
	bool bPruned = true;
	while (bPruned)
	{
		bPruned = false;
		TArray<int32> ToPrune;
		for (const auto& Pair : NodeNeighbors)
		{
			if (Pair.Value.Num() < 2)
			{
				ToPrune.Add(Pair.Key);
			}
		}
		for (int32 NodeID : ToPrune)
		{
			for (int32 Neighbor : NodeNeighbors[NodeID])
			{
				NodeNeighbors[Neighbor].Remove(NodeID);
			}
			NodeNeighbors.Remove(NodeID);
			bPruned = true;
		}
	}

	// If no closed 2-core cycles exist in the graph, abort immediately (zero floors/ceilings for open walls)
	if (NodeNeighbors.Num() < 3)
	{
		ComputeWallInteriorSides(TArray<TArray<FVector2D>>{});
		return;
	}

	// 3. Build Directed Half-Edges exclusively on the pruned closed 2-core graph
	struct FDirectedHalfEdge
	{
		int32 FromNode;
		int32 ToNode;
		float PolarAngle;
	};

	TMap<int32, TArray<FDirectedHalfEdge>> OutgoingEdges;

	for (const auto& Pair : WallSegments)
	{
		const FWallSegment& Seg = Pair.Value;
		int32 u = Seg.StartNodeID;
		int32 v = Seg.EndNodeID;
		if (NodeNeighbors.Contains(u) && NodeNeighbors.Contains(v) && NodeNeighbors[u].Contains(v))
		{
			FVector2D PosU = Nodes[u].Position;
			FVector2D PosV = Nodes[v].Position;

			FVector2D VecUV = PosV - PosU;
			FVector2D VecVU = PosU - PosV;

			if (VecUV.SizeSquared() < 1.0f) continue;

			float AngleUV = FMath::Atan2(VecUV.Y, VecUV.X);
			float AngleVU = FMath::Atan2(VecVU.Y, VecVU.X);

			OutgoingEdges.FindOrAdd(u).Add({ u, v, AngleUV });
			OutgoingEdges.FindOrAdd(v).Add({ v, u, AngleVU });
		}
	}

	// 4. Sort outgoing edges around each node counter-clockwise by polar angle
	for (auto& NodeEdgesPair : OutgoingEdges)
	{
		NodeEdgesPair.Value.Sort([](const FDirectedHalfEdge& A, const FDirectedHalfEdge& B) {
			return A.PolarAngle < B.PolarAngle;
		});
	}

	// 5. Directed Edge Visited Tracking
	TSet<uint64> VisitedDirectedEdges;
	auto MakeEdgeKey = [](int32 From, int32 To) -> uint64 {
		return ((uint64)(uint32)From << 32) | (uint64)(uint32)To;
	};

	TArray<TArray<FVector2D>> DetectedRoomPolygons;

	// 6. Trace all closed faces using the Left-Turn rule
	for (const auto& NodePair : OutgoingEdges)
	{
		for (const FDirectedHalfEdge& StartEdge : NodePair.Value)
		{
			uint64 StartKey = MakeEdgeKey(StartEdge.FromNode, StartEdge.ToNode);
			if (VisitedDirectedEdges.Contains(StartKey))
			{
				continue;
			}

			TArray<int32> FaceCycle;
			int32 CurrU = StartEdge.FromNode;
			int32 CurrV = StartEdge.ToNode;
			bool bValidFace = false;

			int32 MaxSteps = WallSegments.Num() * 4 + 16;
			int32 StepCount = 0;

			while (StepCount++ < MaxSteps)
			{
				uint64 CurrKey = MakeEdgeKey(CurrU, CurrV);
				if (VisitedDirectedEdges.Contains(CurrKey))
				{
					break;
				}
				VisitedDirectedEdges.Add(CurrKey);
				FaceCycle.Add(CurrU);

				if (!OutgoingEdges.Contains(CurrV))
				{
					break;
				}

				const TArray<FDirectedHalfEdge>& OutFromV = OutgoingEdges[CurrV];
				int32 NumOut = OutFromV.Num();
				if (NumOut == 0) break;

				int32 IncomingIdx = INDEX_NONE;
				for (int32 i = 0; i < NumOut; ++i)
				{
					if (OutFromV[i].ToNode == CurrU)
					{
						IncomingIdx = i;
						break;
					}
				}

				if (IncomingIdx == INDEX_NONE)
				{
					break;
				}

				int32 NextIdx = (IncomingIdx + 1) % NumOut;
				const FDirectedHalfEdge& NextEdge = OutFromV[NextIdx];

				CurrU = NextEdge.FromNode;
				CurrV = NextEdge.ToNode;

				if (CurrU == StartEdge.FromNode && CurrV == StartEdge.ToNode)
				{
					bValidFace = true;
					break;
				}
			}

			// Validate simple cycle (no duplicate vertices in face circuit)
			if (bValidFace && FaceCycle.Num() >= 3)
			{
				TSet<int32> UniqueNodes(FaceCycle);
				if (UniqueNodes.Num() != FaceCycle.Num())
				{
					continue; // Discard non-simple / self-intersecting loops
				}

				TArray<FVector2D> Poly;
				for (int32 NodeID : FaceCycle)
				{
					if (Nodes.Contains(NodeID))
					{
						Poly.Add(Nodes[NodeID].Position);
					}
				}

				// Clean consecutive duplicate vertices
				TArray<FVector2D> CleanPoly;
				for (int32 i = 0; i < Poly.Num(); ++i)
				{
					const FVector2D& P = Poly[i];
					if (CleanPoly.Num() == 0 || FVector2D::DistSquared(P, CleanPoly.Last()) > 1.0f)
					{
						CleanPoly.Add(P);
					}
				}
				if (CleanPoly.Num() >= 3 && FVector2D::DistSquared(CleanPoly[0], CleanPoly.Last()) < 1.0f)
				{
					CleanPoly.Pop();
				}

				if (CleanPoly.Num() >= 3)
				{
					// Shoelace Formula for signed area
					float TwiceArea = 0.f;
					int32 N = CleanPoly.Num();
					for (int32 i = 0; i < N; ++i)
					{
						const FVector2D& P1 = CleanPoly[i];
						const FVector2D& P2 = CleanPoly[(i + 1) % N];
						TwiceArea += (P1.X * P2.Y - P2.X * P1.Y);
					}

					// Bounding box size validation
					FVector2D MinP = CleanPoly[0];
					FVector2D MaxP = CleanPoly[0];
					for (const FVector2D& Pt : CleanPoly)
					{
						MinP.X = FMath::Min(MinP.X, Pt.X);
						MinP.Y = FMath::Min(MinP.Y, Pt.Y);
						MaxP.X = FMath::Max(MaxP.X, Pt.X);
						MaxP.Y = FMath::Max(MaxP.Y, Pt.Y);
					}

					// TwiceArea > 0 means CCW interior room face (TwiceArea < 0 is outer perimeter)
					if (TwiceArea > 500.0f && (MaxP.X - MinP.X >= 25.f) && (MaxP.Y - MinP.Y >= 25.f))
					{
						DetectedRoomPolygons.Add(CleanPoly);
					}
				}
			}
		}
	}

	if (DetectedRoomPolygons.Num() == 0)
	{
		ComputeWallInteriorSides(TArray<TArray<FVector2D>>{});
		return;
	}

	// 5. Materials Setup
	// Ceiling / baseboard use the parameterised paint material (BaseColor); floors get a per-room
	// material from ResolveFloorMaterialForRoom (finish if any, else white paint) — REQ-13.
	UMaterialInterface* BaseMat = ResolvePaintBaseMaterial();
	if (!BaseMat)
	{
		BaseMat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/EngineMaterials/DefaultMaterial.DefaultMaterial"));
	}
	if (!BaseMat)
	{
		BaseMat = UMaterial::GetDefaultMaterial(MD_Surface);
	}

	UMaterialInstanceDynamic* CeilMatInst = UMaterialInstanceDynamic::Create(BaseMat, this);
	if (CeilMatInst)
	{
		CeilMatInst->SetVectorParameterValue(TEXT("Color"), FLinearColor(0.95f, 0.95f, 0.95f, 1.0f));
		CeilMatInst->SetVectorParameterValue(TEXT("BaseColor"), FLinearColor(0.95f, 0.95f, 0.95f, 1.0f));
	}

	UMaterialInstanceDynamic* BbMatInst = UMaterialInstanceDynamic::Create(BaseMat, this);
	if (BbMatInst)
	{
		BbMatInst->SetVectorParameterValue(TEXT("Color"), FLinearColor(0.4f, 0.3f, 0.2f, 1.0f));
		BbMatInst->SetVectorParameterValue(TEXT("BaseColor"), FLinearColor(0.4f, 0.3f, 0.2f, 1.0f));
	}

	auto IsPointInTriangle = [](const FVector2D& P, const FVector2D& A, const FVector2D& B, const FVector2D& C) {
		auto Sign = [](const FVector2D& P1, const FVector2D& P2, const FVector2D& P3) {
			return (P1.X - P3.X) * (P2.Y - P3.Y) - (P2.X - P3.X) * (P1.Y - P3.Y);
		};
		bool b1 = Sign(P, A, B) < 0.0f;
		bool b2 = Sign(P, B, C) < 0.0f;
		bool b3 = Sign(P, C, A) < 0.0f;
		return ((b1 == b2) && (b2 == b3));
	};

	// 6. Generate Procedural Meshes for every detected room
	for (int32 RoomIdx = 0; RoomIdx < DetectedRoomPolygons.Num(); ++RoomIdx)
	{
		const TArray<FVector2D>& FloorPolygon = DetectedRoomPolygons[RoomIdx];
		int32 VertCount = FloorPolygon.Num();
		if (VertCount < 3) continue;

		// Calculate room area
		float TwiceArea = 0.f;
		for (int32 i = 0; i < VertCount; ++i)
		{
			const FVector2D& P1 = FloorPolygon[i];
			const FVector2D& P2 = FloorPolygon[(i + 1) % VertCount];
			TwiceArea += (P1.X * P2.Y - P2.X * P1.Y);
		}

		FRoomData Room;
		Room.RoomID = RoomIdx + 1;
		Room.FloorPolygon = FloorPolygon;
		Room.AreaM2 = TwiceArea * 0.5f / 10000.f;

		// Polygon centroid (area weighted) — the stable key for floor finishes across rebuilds.
		{
			double Cx = 0.0, Cy = 0.0;
			for (int32 i = 0; i < VertCount; ++i)
			{
				const FVector2D& P1 = FloorPolygon[i];
				const FVector2D& P2 = FloorPolygon[(i + 1) % VertCount];
				const double Cross = (double)P1.X * P2.Y - (double)P2.X * P1.Y;
				Cx += (P1.X + P2.X) * Cross;
				Cy += (P1.Y + P2.Y) * Cross;
			}
			if (FMath::Abs(TwiceArea) > KINDA_SMALL_NUMBER)
			{
				Room.Centroid = FVector2D((float)(Cx / (3.0 * TwiceArea)), (float)(Cy / (3.0 * TwiceArea)));
			}
			else
			{
				FVector2D Sum = FVector2D::ZeroVector;
				for (const FVector2D& P : FloorPolygon) Sum += P;
				Room.Centroid = Sum / (float)VertCount;
			}
		}
		if (const FFloorFinishRecord* Rec = FindFloorFinishRecord(Room.Centroid))
		{
			Room.FloorFinish = Rec->Finish;
		}
		Rooms.Add(Room.RoomID, Room);

		// Ear Clipping Triangulation
		TArray<int32> Indices;
		for (int32 i = 0; i < VertCount; ++i) Indices.Add(i);

		TArray<int32> TriangulatedIndices;
		int32 IterationCount = 0;
		while (Indices.Num() > 3 && IterationCount < 1000)
		{
			IterationCount++;
			bool bEarFound = false;
			for (int32 i = 0; i < Indices.Num(); ++i)
			{
				int32 PrevIdx = (i == 0) ? Indices.Num() - 1 : i - 1;
				int32 NextIdx = (i == Indices.Num() - 1) ? 0 : i + 1;

				int32 V0 = Indices[PrevIdx];
				int32 V1 = Indices[i];
				int32 V2 = Indices[NextIdx];

				const FVector2D& P0 = FloorPolygon[V0];
				const FVector2D& P1 = FloorPolygon[V1];
				const FVector2D& P2 = FloorPolygon[V2];

				float Cross = (P1.X - P0.X) * (P2.Y - P1.Y) - (P1.Y - P0.Y) * (P2.X - P1.X);
				if (Cross >= -0.01f)
				{
					bool bValid = true;
					for (int32 j = 0; j < Indices.Num(); ++j)
					{
						if (j == PrevIdx || j == i || j == NextIdx) continue;
						if (IsPointInTriangle(FloorPolygon[Indices[j]], P0, P1, P2))
						{
							bValid = false;
							break;
						}
					}

					if (bValid)
					{
						TriangulatedIndices.Add(V0);
						TriangulatedIndices.Add(V1);
						TriangulatedIndices.Add(V2);
						Indices.RemoveAt(i);
						bEarFound = true;
						break;
					}
				}
			}
			if (!bEarFound) 
			{
				TriangulatedIndices.Add(Indices[0]);
				TriangulatedIndices.Add(Indices[1]);
				TriangulatedIndices.Add(Indices[2]);
				Indices.RemoveAt(1);
			}
		}
		if (Indices.Num() == 3)
		{
			TriangulatedIndices.Add(Indices[0]);
			TriangulatedIndices.Add(Indices[1]);
			TriangulatedIndices.Add(Indices[2]);
		}

		// Reverse for Clockwise front-face rendering in Unreal
		Algo::Reverse(TriangulatedIndices);

		// 6.1. Generate Floor Mesh Section
		if (FloorProceduralMesh)
		{
			TArray<FVector> Vertices;
			TArray<int32> Triangles;
			TArray<FVector> Normals;
			TArray<FVector2D> UVs;
			TArray<FColor> FloorColors;

			// Top face (Z=1.f)
			for (int32 i = 0; i < VertCount; ++i)
			{
				Vertices.Add(FVector(FloorPolygon[i].X, FloorPolygon[i].Y, 1.f));
				Normals.Add(FVector::UpVector);
				UVs.Add(FloorPolygon[i] / 100.f);
				FloorColors.Add(FColor(255, 255, 255, 255));
			}
			Triangles = TriangulatedIndices;

			// Bottom face (Z=0.f)
			int32 StartIdx = Vertices.Num();
			for (int32 i = 0; i < VertCount; ++i)
			{
				Vertices.Add(FVector(FloorPolygon[i].X, FloorPolygon[i].Y, 0.f));
				Normals.Add(-FVector::UpVector);
				UVs.Add(FloorPolygon[i] / 100.f);
				FloorColors.Add(FColor(255, 255, 255, 255));
			}
			for (int32 i = 0; i < TriangulatedIndices.Num(); i += 3)
			{
				Triangles.Add(StartIdx + TriangulatedIndices[i]);
				Triangles.Add(StartIdx + TriangulatedIndices[i + 2]);
				Triangles.Add(StartIdx + TriangulatedIndices[i + 1]);
			}

			// Side faces for 1cm thickness
			for (int32 i = 0; i < VertCount; ++i)
			{
				FVector2D P1 = FloorPolygon[i];
				FVector2D P2 = FloorPolygon[(i + 1) % VertCount];
				FVector2D EdgeDir = (P2 - P1).GetSafeNormal();
				FVector2D EdgeNorm(EdgeDir.Y, -EdgeDir.X);
				FVector OutNormal(EdgeNorm.X, EdgeNorm.Y, 0.f);

				int32 SIdx = Vertices.Num();
				Vertices.Add(FVector(P2.X, P2.Y, 0.f));
				Vertices.Add(FVector(P1.X, P1.Y, 0.f));
				Vertices.Add(FVector(P1.X, P1.Y, 1.f));
				Vertices.Add(FVector(P2.X, P2.Y, 1.f));
				
				for(int k=0; k<4; k++) { Normals.Add(OutNormal); UVs.Add(FVector2D::ZeroVector); FloorColors.Add(FColor(255, 255, 255, 255)); }
				
				Triangles.Add(SIdx + 0); Triangles.Add(SIdx + 1); Triangles.Add(SIdx + 2);
				Triangles.Add(SIdx + 0); Triangles.Add(SIdx + 2); Triangles.Add(SIdx + 3);
			}

			FloorProceduralMesh->CreateMeshSection(RoomIdx, Vertices, Triangles, Normals, UVs, FloorColors, TArray<FProcMeshTangent>(), true);
			UMaterialInterface* FloorMat = ResolveFloorMaterialForRoom(Rooms[RoomIdx + 1]);
			FloorProceduralMesh->SetMaterial(RoomIdx, FloorMat ? FloorMat : BaseMat);
		}

		// 6.2. Generate Ceiling Mesh Section
		if (CeilingProceduralMesh)
		{
			TArray<FVector> CeilVerts;
			TArray<int32> CeilTris;
			TArray<FVector> CeilNorms;
			TArray<FVector2D> CeilUVs;
			TArray<FColor> CeilColors;

			// Ceiling sits on the tallest wall of this room (walls whose both corners are polygon vertices).
			float CeilZ = 0.f;
			for (const auto& SegPair : WallSegments)
			{
				const FWallNode* SNode = Nodes.Find(SegPair.Value.StartNodeID);
				const FWallNode* ENode = Nodes.Find(SegPair.Value.EndNodeID);
				if (!SNode || !ENode) continue;
				bool bStartOnPoly = false, bEndOnPoly = false;
				for (const FVector2D& PV : FloorPolygon)
				{
					if (FVector2D::DistSquared(PV, SNode->Position) < 4.f) bStartOnPoly = true;
					if (FVector2D::DistSquared(PV, ENode->Position) < 4.f) bEndOnPoly = true;
				}
				if (bStartOnPoly && bEndOnPoly)
				{
					CeilZ = FMath::Max(CeilZ, SegPair.Value.Height);
				}
			}
			if (CeilZ <= 0.f) CeilZ = 280.f;
			for (int32 i = 0; i < VertCount; ++i)
			{
				CeilVerts.Add(FVector(FloorPolygon[i].X, FloorPolygon[i].Y, CeilZ));
				CeilNorms.Add(-FVector::UpVector);
				CeilUVs.Add(FloorPolygon[i] / 100.f);
				CeilColors.Add(FColor(240, 240, 240, 255));
			}

			for (int32 i = 0; i < TriangulatedIndices.Num(); i += 3)
			{
				CeilTris.Add(TriangulatedIndices[i]);
				CeilTris.Add(TriangulatedIndices[i + 2]);
				CeilTris.Add(TriangulatedIndices[i + 1]);
			}

			CeilingProceduralMesh->CreateMeshSection(RoomIdx, CeilVerts, CeilTris, CeilNorms, CeilUVs, CeilColors, TArray<FProcMeshTangent>(), true);
			CeilingProceduralMesh->SetMaterial(RoomIdx, CeilMatInst ? CeilMatInst : BaseMat);
		}

		// 6.3. Generate Baseboard Mesh Section
		if (BaseboardProceduralMesh)
		{
			TArray<FVector> BbVerts;
			TArray<int32> BbTris;
			TArray<FVector> BbNorms;
			TArray<FVector2D> BbUVs;
			TArray<FColor> BbColors;

			float BbHeight = 10.f;
			for (int32 i = 0; i < VertCount; ++i)
			{
				FVector2D P1 = FloorPolygon[i];
				FVector2D P2 = FloorPolygon[(i + 1) % VertCount];
				FVector2D EdgeDir = (P2 - P1).GetSafeNormal();
				FVector2D EdgeNorm(-EdgeDir.Y, EdgeDir.X);
				FVector OutNormal(EdgeNorm.X, EdgeNorm.Y, 0.f);

				FVector V0(P1.X, P1.Y, 1.f);
				FVector V1(P2.X, P2.Y, 1.f);
				FVector V2(P2.X, P2.Y, 1.f + BbHeight);
				FVector V3(P1.X, P1.Y, 1.f + BbHeight);

				int32 StartIdx = BbVerts.Num();
				BbVerts.Add(V0); BbVerts.Add(V1); BbVerts.Add(V2); BbVerts.Add(V3);
				BbNorms.Add(OutNormal); BbNorms.Add(OutNormal); BbNorms.Add(OutNormal); BbNorms.Add(OutNormal);
				BbUVs.Add(FVector2D(0.f, 0.f)); BbUVs.Add(FVector2D(1.f, 0.f)); BbUVs.Add(FVector2D(1.f, 1.f)); BbUVs.Add(FVector2D(0.f, 1.f));

				FColor BbColor(100, 75, 50, 255);
				BbColors.Add(BbColor); BbColors.Add(BbColor); BbColors.Add(BbColor); BbColors.Add(BbColor);

				BbTris.Add(StartIdx + 0); BbTris.Add(StartIdx + 1); BbTris.Add(StartIdx + 2);
				BbTris.Add(StartIdx + 0); BbTris.Add(StartIdx + 2); BbTris.Add(StartIdx + 3);
				BbTris.Add(StartIdx + 0); BbTris.Add(StartIdx + 2); BbTris.Add(StartIdx + 1);
				BbTris.Add(StartIdx + 0); BbTris.Add(StartIdx + 3); BbTris.Add(StartIdx + 2);
			}

			BaseboardProceduralMesh->CreateMeshSection(RoomIdx, BbVerts, BbTris, BbNorms, BbUVs, BbColors, TArray<FProcMeshTangent>(), false);
			BaseboardProceduralMesh->SetMaterial(RoomIdx, BbMatInst ? BbMatInst : BaseMat);
		}
	}

	if (FloorProceduralMesh) FloorProceduralMesh->SetVisibility(true);
	if (CeilingProceduralMesh) CeilingProceduralMesh->SetVisibility(bCeilingVisible && !b2DViewMode);
	if (BaseboardProceduralMesh) BaseboardProceduralMesh->SetVisibility(true);

	// Which face of every wall looks into a room (drives door/window swing direction, REQ-07).
	ComputeWallInteriorSides(DetectedRoomPolygons);
}


TSharedPtr<FJsonObject> ARoomPlannerManager::FinishToJson(const FSurfaceFinish& Finish)
{
	TSharedPtr<FJsonObject> Obj = MakeShareable(new FJsonObject());
	Obj->SetStringField(TEXT("type"), PlannerJsonKeys::FinishTypeToString(Finish.Type));
	Obj->SetStringField(TEXT("code"), Finish.ColorCode);
	Obj->SetNumberField(TEXT("r"), Finish.Color.R);
	Obj->SetNumberField(TEXT("g"), Finish.Color.G);
	Obj->SetNumberField(TEXT("b"), Finish.Color.B);
	Obj->SetNumberField(TEXT("a"), Finish.Color.A);
	Obj->SetStringField(TEXT("tile"), Finish.TileAssetID);
	Obj->SetNumberField(TEXT("tileSize"), Finish.TileSizeCm);
	return Obj;
}

FSurfaceFinish ARoomPlannerManager::FinishFromJson(const TSharedPtr<FJsonObject>& Obj)
{
	FSurfaceFinish Finish;
	if (!Obj.IsValid())
	{
		return Finish;
	}
	FString TypeStr;
	if (Obj->TryGetStringField(TEXT("type"), TypeStr))
	{
		Finish.Type = PlannerJsonKeys::FinishTypeFromString(TypeStr);
	}
	Obj->TryGetStringField(TEXT("code"), Finish.ColorCode);
	double R = 1.0, G = 1.0, B = 1.0, A = 1.0;
	Obj->TryGetNumberField(TEXT("r"), R);
	Obj->TryGetNumberField(TEXT("g"), G);
	Obj->TryGetNumberField(TEXT("b"), B);
	Obj->TryGetNumberField(TEXT("a"), A);
	Finish.Color = FLinearColor((float)R, (float)G, (float)B, (float)A);
	Obj->TryGetStringField(TEXT("tile"), Finish.TileAssetID);
	double TileSize = 30.0;
	if (Obj->TryGetNumberField(TEXT("tileSize"), TileSize))
	{
		Finish.TileSizeCm = (float)TileSize;
	}
	return Finish;
}

FString ARoomPlannerManager::ExportLayoutToJSON() const
{
	TSharedPtr<FJsonObject> RootObject = MakeShareable(new FJsonObject());
	RootObject->SetNumberField(TEXT("version"), 2);

	// Serializing Nodes
	TArray<TSharedPtr<FJsonValue>> NodesArray;
	for (const TPair<int32, FWallNode>& Pair : Nodes)
	{
		TSharedPtr<FJsonObject> NodeObj = MakeShareable(new FJsonObject());
		NodeObj->SetNumberField(TEXT("id"), Pair.Key);
		NodeObj->SetNumberField(TEXT("x"), Pair.Value.Position.X);
		NodeObj->SetNumberField(TEXT("y"), Pair.Value.Position.Y);
		NodesArray.Add(MakeShareable(new FJsonValueObject(NodeObj)));
	}
	RootObject->SetArrayField(TEXT("nodes"), NodesArray);

	// Serializing Wall Segments (+ guid, finish, opening swing)
	TArray<TSharedPtr<FJsonValue>> WallsArray;
	for (const TPair<int32, FWallSegment>& Pair : WallSegments)
	{
		TSharedPtr<FJsonObject> WallObj = MakeShareable(new FJsonObject());
		WallObj->SetNumberField(TEXT("id"), Pair.Key);
		WallObj->SetStringField(TEXT("guid"), Pair.Value.WallGuid);
		WallObj->SetNumberField(TEXT("start"), Pair.Value.StartNodeID);
		WallObj->SetNumberField(TEXT("end"), Pair.Value.EndNodeID);
		WallObj->SetNumberField(TEXT("thickness"), Pair.Value.Thickness);
		WallObj->SetNumberField(TEXT("height"), Pair.Value.Height);
		WallObj->SetObjectField(TEXT("finish"), FinishToJson(Pair.Value.Finish));

		TArray<TSharedPtr<FJsonValue>> OpeningsArray;
		for (const FWallOpening& Op : Pair.Value.Openings)
		{
			TSharedPtr<FJsonObject> OpObj = MakeShareable(new FJsonObject());
			OpObj->SetStringField(TEXT("id"), Op.OpeningID);
			const TCHAR* TypeStr = TEXT("door");
			if (Op.Type == EOpeningType::Window) TypeStr = TEXT("window");
			else if (Op.Type == EOpeningType::Archway) TypeStr = TEXT("archway");
			OpObj->SetStringField(TEXT("type"), TypeStr);
			OpObj->SetNumberField(TEXT("dist"), Op.DistanceFromStart);
			OpObj->SetNumberField(TEXT("width"), Op.Width);
			OpObj->SetNumberField(TEXT("height"), Op.Height);
			OpObj->SetNumberField(TEXT("sill"), Op.SillHeight);
			OpObj->SetStringField(TEXT("swingSide"), Op.SwingSide == EOpeningSwingSide::Right ? TEXT("right") : TEXT("left"));
			OpObj->SetStringField(TEXT("swingDir"), Op.SwingDirection == EOpeningSwingDirection::Outward ? TEXT("out") : TEXT("in"));
			OpeningsArray.Add(MakeShareable(new FJsonValueObject(OpObj)));
		}
		WallObj->SetArrayField(TEXT("openings"), OpeningsArray);
		WallsArray.Add(MakeShareable(new FJsonValueObject(WallObj)));
	}
	RootObject->SetArrayField(TEXT("walls"), WallsArray);

	// Serializing Rooms Stats (derived; informational for the browser / save record)
	TArray<TSharedPtr<FJsonValue>> RoomsArray;
	for (const TPair<int32, FRoomData>& Pair : Rooms)
	{
		TSharedPtr<FJsonObject> RoomObj = MakeShareable(new FJsonObject());
		RoomObj->SetNumberField(TEXT("id"), Pair.Key);
		RoomObj->SetNumberField(TEXT("area_m2"), Pair.Value.AreaM2);
		RoomObj->SetNumberField(TEXT("cx"), Pair.Value.Centroid.X);
		RoomObj->SetNumberField(TEXT("cy"), Pair.Value.Centroid.Y);
		RoomObj->SetObjectField(TEXT("finish"), FinishToJson(Pair.Value.FloorFinish));
		RoomsArray.Add(MakeShareable(new FJsonValueObject(RoomObj)));
	}
	RootObject->SetArrayField(TEXT("rooms"), RoomsArray);

	// Floor finishes (authoritative, keyed by room centroid)
	TArray<TSharedPtr<FJsonValue>> FloorFinishArray;
	for (const FFloorFinishRecord& Rec : FloorFinishes)
	{
		TSharedPtr<FJsonObject> Obj = MakeShareable(new FJsonObject());
		Obj->SetNumberField(TEXT("x"), Rec.Anchor.X);
		Obj->SetNumberField(TEXT("y"), Rec.Anchor.Y);
		Obj->SetObjectField(TEXT("finish"), FinishToJson(Rec.Finish));
		FloorFinishArray.Add(MakeShareable(new FJsonValueObject(Obj)));
	}
	RootObject->SetArrayField(TEXT("floorFinishes"), FloorFinishArray);

	// Placed interior objects (REQ-17)
	TArray<TSharedPtr<FJsonValue>> ObjectsArray;
	for (const TPair<FString, FPlacedFurnitureData>& Pair : PlacedObjects)
	{
		const FPlacedFurnitureData& D = Pair.Value;
		TSharedPtr<FJsonObject> Obj = MakeShareable(new FJsonObject());
		Obj->SetStringField(TEXT("id"), D.InstanceID);
		Obj->SetStringField(TEXT("asset"), D.AssetID);
		Obj->SetNumberField(TEXT("x"), D.Location.X);
		Obj->SetNumberField(TEXT("y"), D.Location.Y);
		Obj->SetNumberField(TEXT("z"), D.Location.Z);
		Obj->SetNumberField(TEXT("pitch"), D.Rotation.Pitch);
		Obj->SetNumberField(TEXT("yaw"), D.Rotation.Yaw);
		Obj->SetNumberField(TEXT("roll"), D.Rotation.Roll);
		Obj->SetNumberField(TEXT("sx"), D.Scale.X);
		Obj->SetNumberField(TEXT("sy"), D.Scale.Y);
		Obj->SetNumberField(TEXT("sz"), D.Scale.Z);
		Obj->SetStringField(TEXT("material"), D.CustomMaterialID);
		Obj->SetObjectField(TEXT("finish"), FinishToJson(D.Finish));
		ObjectsArray.Add(MakeShareable(new FJsonValueObject(Obj)));
	}
	RootObject->SetArrayField(TEXT("objects"), ObjectsArray);

	// Placed cabinet sets (REQ-18)
	TArray<TSharedPtr<FJsonValue>> SetsArray;
	for (const TPair<FString, FPlacedCabinetSetData>& Pair : CabinetSets)
	{
		const FPlacedCabinetSetData& D = Pair.Value;
		TSharedPtr<FJsonObject> Obj = MakeShareable(new FJsonObject());
		Obj->SetStringField(TEXT("id"), D.InstanceID);
		Obj->SetStringField(TEXT("product"), D.ProductID.ToString());
		Obj->SetNumberField(TEXT("x"), D.Location.X);
		Obj->SetNumberField(TEXT("y"), D.Location.Y);
		Obj->SetNumberField(TEXT("z"), D.Location.Z);
		Obj->SetNumberField(TEXT("pitch"), D.Rotation.Pitch);
		Obj->SetNumberField(TEXT("yaw"), D.Rotation.Yaw);
		Obj->SetNumberField(TEXT("roll"), D.Rotation.Roll);
		SetsArray.Add(MakeShareable(new FJsonValueObject(Obj)));
	}
	RootObject->SetArrayField(TEXT("cabinetSets"), SetsArray);

	FString OutputString;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutputString);
	FJsonSerializer::Serialize(RootObject.ToSharedRef(), Writer);

	return OutputString;
}

bool ARoomPlannerManager::ImportLayoutFromJSON(const FString& JSONString)
{
	TSharedPtr<FJsonObject> RootObject;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JSONString);

	if (!FJsonSerializer::Deserialize(Reader, RootObject) || !RootObject.IsValid())
	{
		return false;
	}

	// Walls / nodes / rooms are rebuilt from scratch (established full-reimport model);
	// placed objects and cabinet sets are RECONCILED by InstanceID so they do not flicker.
	ClearWallsAndRooms();

	// Read Nodes
	const TArray<TSharedPtr<FJsonValue>>* NodesArray = nullptr;
	if (RootObject->TryGetArrayField(TEXT("nodes"), NodesArray) && NodesArray)
	{
		for (const TSharedPtr<FJsonValue>& Val : *NodesArray)
		{
			TSharedPtr<FJsonObject> NodeObj = Val->AsObject();
			if (NodeObj.IsValid())
			{
				int32 ID = NodeObj->GetIntegerField(TEXT("id"));
				float X = NodeObj->GetNumberField(TEXT("x"));
				float Y = NodeObj->GetNumberField(TEXT("y"));

				FWallNode Node;
				Node.NodeID = ID;
				Node.Position = FVector2D(X, Y);
				Nodes.Add(ID, Node);

				if (ID >= NextNodeID) NextNodeID = ID + 1;
			}
		}
	}

	// Read Floor finishes BEFORE walls so RebuildRooms (triggered by AddWall) can associate them.
	FloorFinishes.Empty();
	const TArray<TSharedPtr<FJsonValue>>* FloorFinishArray = nullptr;
	if (RootObject->TryGetArrayField(TEXT("floorFinishes"), FloorFinishArray) && FloorFinishArray)
	{
		for (const TSharedPtr<FJsonValue>& Val : *FloorFinishArray)
		{
			TSharedPtr<FJsonObject> Obj = Val->AsObject();
			if (Obj.IsValid())
			{
				FFloorFinishRecord Rec;
				Rec.Anchor = FVector2D(Obj->GetNumberField(TEXT("x")), Obj->GetNumberField(TEXT("y")));
				const TSharedPtr<FJsonObject>* FinishObj = nullptr;
				if (Obj->TryGetObjectField(TEXT("finish"), FinishObj) && FinishObj)
				{
					Rec.Finish = FinishFromJson(*FinishObj);
				}
				if (Rec.Finish.IsSet())
				{
					FloorFinishes.Add(Rec);
				}
			}
		}
	}

	// Read Walls
	const TArray<TSharedPtr<FJsonValue>>* WallsArray = nullptr;
	if (RootObject->TryGetArrayField(TEXT("walls"), WallsArray) && WallsArray)
	{
		for (const TSharedPtr<FJsonValue>& Val : *WallsArray)
		{
			TSharedPtr<FJsonObject> WallObj = Val->AsObject();
			if (WallObj.IsValid())
			{
				int32 StartID = WallObj->GetIntegerField(TEXT("start"));
				int32 EndID = WallObj->GetIntegerField(TEXT("end"));
				float Thickness = WallObj->GetNumberField(TEXT("thickness"));
				float Height = WallObj->GetNumberField(TEXT("height"));

				int32 SegID = AddWall(StartID, EndID, Thickness > 0.f ? Thickness : 20.f, Height > 0.f ? Height : 280.f);

				if (SegID != -1)
				{
					if (FWallSegment* Seg = WallSegments.Find(SegID))
					{
						FString Guid;
						if (WallObj->TryGetStringField(TEXT("guid"), Guid) && !Guid.IsEmpty())
						{
							Seg->WallGuid = Guid;
						}
						const TSharedPtr<FJsonObject>* FinishObj = nullptr;
						if (WallObj->TryGetObjectField(TEXT("finish"), FinishObj) && FinishObj)
						{
							Seg->Finish = FinishFromJson(*FinishObj);
						}
					}
				}

				const TArray<TSharedPtr<FJsonValue>>* OpeningsArray = nullptr;
				if (SegID != -1 && WallObj->TryGetArrayField(TEXT("openings"), OpeningsArray) && OpeningsArray)
				{
					for (const TSharedPtr<FJsonValue>& OpVal : *OpeningsArray)
					{
						TSharedPtr<FJsonObject> OpObj = OpVal->AsObject();
						if (OpObj.IsValid())
						{
							FString TypeStr = OpObj->GetStringField(TEXT("type"));
							EOpeningType Type = EOpeningType::Door;
							if (TypeStr.Equals(TEXT("window"), ESearchCase::IgnoreCase)) Type = EOpeningType::Window;
							else if (TypeStr.Equals(TEXT("archway"), ESearchCase::IgnoreCase)) Type = EOpeningType::Archway;
							float Dist = OpObj->GetNumberField(TEXT("dist"));
							float Width = OpObj->GetNumberField(TEXT("width"));
							float OpHeight = OpObj->GetNumberField(TEXT("height"));
							float Sill = OpObj->GetNumberField(TEXT("sill"));

							if (AddOpeningToWall(SegID, Type, Dist, Width, OpHeight, Sill))
							{
								if (FWallSegment* Seg = WallSegments.Find(SegID))
								{
									if (Seg->Openings.Num() > 0)
									{
										FWallOpening& NewOp = Seg->Openings.Last();
										FString SideStr, DirStr;
										if (OpObj->TryGetStringField(TEXT("swingSide"), SideStr))
										{
											NewOp.SwingSide = SideStr.Equals(TEXT("right"), ESearchCase::IgnoreCase) ? EOpeningSwingSide::Right : EOpeningSwingSide::Left;
										}
										if (OpObj->TryGetStringField(TEXT("swingDir"), DirStr))
										{
											NewOp.SwingDirection = DirStr.Equals(TEXT("out"), ESearchCase::IgnoreCase) ? EOpeningSwingDirection::Outward : EOpeningSwingDirection::Inward;
										}
									}
								}
							}
						}
					}
				}
			}
		}
	}

	// Read placed objects (REQ-17)
	PlacedObjects.Empty();
	const TArray<TSharedPtr<FJsonValue>>* ObjectsArray = nullptr;
	if (RootObject->TryGetArrayField(TEXT("objects"), ObjectsArray) && ObjectsArray)
	{
		for (const TSharedPtr<FJsonValue>& Val : *ObjectsArray)
		{
			TSharedPtr<FJsonObject> Obj = Val->AsObject();
			if (!Obj.IsValid()) continue;

			FPlacedFurnitureData D;
			D.InstanceID = Obj->GetStringField(TEXT("id"));
			D.AssetID = Obj->GetStringField(TEXT("asset"));
			if (D.InstanceID.IsEmpty()) continue;
			D.Location = FVector(Obj->GetNumberField(TEXT("x")), Obj->GetNumberField(TEXT("y")), Obj->GetNumberField(TEXT("z")));
			D.Rotation = FRotator(Obj->GetNumberField(TEXT("pitch")), Obj->GetNumberField(TEXT("yaw")), Obj->GetNumberField(TEXT("roll")));
			double Sx = 1.0, Sy = 1.0, Sz = 1.0;
			Obj->TryGetNumberField(TEXT("sx"), Sx); Obj->TryGetNumberField(TEXT("sy"), Sy); Obj->TryGetNumberField(TEXT("sz"), Sz);
			D.Scale = FVector((float)Sx, (float)Sy, (float)Sz);
			Obj->TryGetStringField(TEXT("material"), D.CustomMaterialID);
			const TSharedPtr<FJsonObject>* FinishObj = nullptr;
			if (Obj->TryGetObjectField(TEXT("finish"), FinishObj) && FinishObj)
			{
				D.Finish = FinishFromJson(*FinishObj);
			}
			PlacedObjects.Add(D.InstanceID, D);
		}
	}

	// Read cabinet sets (REQ-18)
	CabinetSets.Empty();
	const TArray<TSharedPtr<FJsonValue>>* SetsArray = nullptr;
	if (RootObject->TryGetArrayField(TEXT("cabinetSets"), SetsArray) && SetsArray)
	{
		for (const TSharedPtr<FJsonValue>& Val : *SetsArray)
		{
			TSharedPtr<FJsonObject> Obj = Val->AsObject();
			if (!Obj.IsValid()) continue;

			FPlacedCabinetSetData D;
			D.InstanceID = Obj->GetStringField(TEXT("id"));
			D.ProductID = FName(*Obj->GetStringField(TEXT("product")));
			if (D.InstanceID.IsEmpty()) continue;
			D.Location = FVector(Obj->GetNumberField(TEXT("x")), Obj->GetNumberField(TEXT("y")), Obj->GetNumberField(TEXT("z")));
			D.Rotation = FRotator(Obj->GetNumberField(TEXT("pitch")), Obj->GetNumberField(TEXT("yaw")), Obj->GetNumberField(TEXT("roll")));
			CabinetSets.Add(D.InstanceID, D);
		}
	}

	RebuildAllWalls();
	RebuildRooms();
	RebuildPlacedObjectActors();
	ReconcileCabinetSetActors();

	if (SelectedSegmentID != -1 && !WallSegments.Contains(SelectedSegmentID))
	{
		SelectedSegmentID = -1;
		SelectedOpeningIndex = -1;
	}
	else if (SelectedSegmentID != -1 && SelectedOpeningIndex != -1)
	{
		if (!WallSegments[SelectedSegmentID].Openings.IsValidIndex(SelectedOpeningIndex))
		{
			SelectedOpeningIndex = -1;
		}
	}
	if (SelectedRoomID != -1 && !Rooms.Contains(SelectedRoomID))
	{
		SelectedRoomID = -1;
	}
	if (!SelectedObjectID.IsEmpty() && !PlacedObjects.Contains(SelectedObjectID))
	{
		SelectedObjectID.Empty();
	}
	if (!SelectedCabinetSetID.IsEmpty() && !CabinetSets.Contains(SelectedCabinetSetID))
	{
		SelectedCabinetSetID.Empty();
	}

	UpdateSelectionVisuals();
	RefreshNodeHandles();
	OnRoomPlannerUpdated.Broadcast(JSONString);
	if (SelectedSegmentID != -1)
	{
		OnWallSelected.Broadcast(SelectedSegmentID, GetWallLength(SelectedSegmentID));
	}
	NotifySelectionChanged();

	return true;
}

FString ARoomPlannerManager::ProcessCommandJSON(const FString& JSONString)
{
	UE_LOG(LogTemp, Warning, TEXT("[MaxiMallConstructor] ProcessCommandJSON received: %s"), *JSONString);

	TSharedPtr<FJsonObject> CommandObject;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JSONString);

	if (!FJsonSerializer::Deserialize(Reader, CommandObject) || !CommandObject.IsValid())
	{
		return TEXT("{\"status\":\"error\",\"message\":\"Invalid JSON\"}");
	}

	FString Cmd = CommandObject->GetStringField(TEXT("cmd"));

	if (Cmd.Equals(TEXT("add_wall"), ESearchCase::IgnoreCase))
	{
		float X1 = CommandObject->GetNumberField(TEXT("x1"));
		float Y1 = CommandObject->GetNumberField(TEXT("y1"));
		float X2 = CommandObject->GetNumberField(TEXT("x2"));
		float Y2 = CommandObject->GetNumberField(TEXT("y2"));

		int32 N1 = AddNode(FVector2D(X1, Y1));
		int32 N2 = AddNode(FVector2D(X2, Y2));
		int32 SegID = AddWall(N1, N2);
		ReplicatedRoomJSON = ExportLayoutToJSON();

		FString ResponseJSON = FString::Printf(TEXT("{\"status\":\"ok\",\"cmd\":\"add_wall\",\"segment_id\":%d,\"state\":%s}"), SegID, *ReplicatedRoomJSON);
		OnRoomPlannerUpdated.Broadcast(ResponseJSON);
		return ResponseJSON;
	}
	else if (Cmd.Equals(TEXT("add_opening"), ESearchCase::IgnoreCase))
	{
		int32 SegID = CommandObject->GetIntegerField(TEXT("segment_id"));
		FString TypeStr = CommandObject->GetStringField(TEXT("type"));
		EOpeningType Type = TypeStr.Equals(TEXT("window"), ESearchCase::IgnoreCase) ? EOpeningType::Window : EOpeningType::Door;
		float Dist = CommandObject->GetNumberField(TEXT("dist"));
		float Width = CommandObject->HasField(TEXT("width")) ? CommandObject->GetNumberField(TEXT("width")) : 90.f;
		float Height = CommandObject->HasField(TEXT("height")) ? CommandObject->GetNumberField(TEXT("height")) : 210.f;
		float Sill = CommandObject->HasField(TEXT("sill")) ? CommandObject->GetNumberField(TEXT("sill")) : (Type == EOpeningType::Window ? 90.f : 0.f);

		bool bSuccess = AddOpeningToWall(SegID, Type, Dist, Width, Height, Sill);
		ReplicatedRoomJSON = ExportLayoutToJSON();
		FString ResponseJSON = FString::Printf(TEXT("{\"status\":\"%s\",\"cmd\":\"add_opening\",\"state\":%s}"), bSuccess ? TEXT("ok") : TEXT("error"), *ReplicatedRoomJSON);
		OnRoomPlannerUpdated.Broadcast(ResponseJSON);
		return ResponseJSON;
	}
	else if (Cmd.Equals(TEXT("clear"), ESearchCase::IgnoreCase))
	{
		ClearLayout();
		ReplicatedRoomJSON = ExportLayoutToJSON();
		FString ResponseJSON = TEXT("{\"status\":\"ok\",\"cmd\":\"clear\",\"state\":{\"nodes\":[],\"walls\":[],\"rooms\":[]}}");
		OnRoomPlannerUpdated.Broadcast(ResponseJSON);
		return ResponseJSON;
	}
	else if (Cmd.Equals(TEXT("get_state"), ESearchCase::IgnoreCase))
	{
		return FString::Printf(TEXT("{\"status\":\"ok\",\"cmd\":\"get_state\",\"state\":%s}"), *ExportLayoutToJSON());
	}

	return TEXT("{\"status\":\"error\",\"message\":\"unknown_command\"}");
}

float ARoomPlannerManager::CalculateFloorAreaM2() const
{
	float TotalArea = 0.f;
	for (const auto& Pair : Rooms)
	{
		TotalArea += Pair.Value.AreaM2;
	}
	return TotalArea;
}

float ARoomPlannerManager::CalculatePerimeterM() const
{
	float TotalPerimeterCm = 0.f;
	for (const auto& Pair : WallSegments)
	{
		const FWallSegment& Seg = Pair.Value;
		if (Nodes.Contains(Seg.StartNodeID) && Nodes.Contains(Seg.EndNodeID))
		{
			FVector2D P1 = Nodes[Seg.StartNodeID].Position;
			FVector2D P2 = Nodes[Seg.EndNodeID].Position;
			TotalPerimeterCm += FVector2D::Distance(P1, P2);
		}
	}
	return TotalPerimeterCm / 100.f;
}

void ARoomPlannerManager::SetViewMode(bool bIn2DMode)
{
	b2DViewMode = bIn2DMode;
	ActiveViewMode = bIn2DMode ? EPlannerViewMode::View2D_TopDown : EPlannerViewMode::View3D_Perspective;

	if (!bIn2DMode)
	{
		bCeilingVisible = true;
		DraggingNodeID = -1; // corner dragging is a 2D-only interaction
		ClearAllSelection();
	}

	if (CeilingProceduralMesh)
	{
		CeilingProceduralMesh->SetVisibility(bCeilingVisible && !bIn2DMode);
	}

	RefreshNodeHandles();
}

bool ARoomPlannerManager::FindWallSnapPoint2D(const FVector2D& Point2D, float SnapRadiusCm, FVector2D& OutSnapPos, int32& OutSnappedSegmentID, bool& bOutIsEndpoint) const
{
	bOutIsEndpoint = false;
	OutSnappedSegmentID = INDEX_NONE;
	float BestDistSq = SnapRadiusCm * SnapRadiusCm;
	bool bFoundSnap = false;

	// 1. First, check if Point2D is near ANY corner endpoint node (within SnapRadiusCm)
	for (const auto& Pair : Nodes)
	{
		float DistSq = FVector2D::DistSquared(Point2D, Pair.Value.Position);
		if (DistSq <= BestDistSq)
		{
			BestDistSq = DistSq;
			OutSnapPos = Pair.Value.Position;
			bOutIsEndpoint = true;
			bFoundSnap = true;
		}
	}

	if (bFoundSnap)
	{
		return true;
	}

	// 2. Check if Point2D is hovering along any wall centerline
	float BestWallDist = SnapRadiusCm + 15.f;
	for (const auto& Pair : WallSegments)
	{
		const FWallSegment& Seg = Pair.Value;
		if (Nodes.Contains(Seg.StartNodeID) && Nodes.Contains(Seg.EndNodeID))
		{
			FVector2D P1 = Nodes[Seg.StartNodeID].Position;
			FVector2D P2 = Nodes[Seg.EndNodeID].Position;
			FVector2D SegVec = P2 - P1;
			float SegLen = SegVec.Size();
			if (SegLen < 1.0f) continue;

			FVector2D SegDir = SegVec / SegLen;
			float t = FVector2D::DotProduct(Point2D - P1, SegDir);

			float ClampedT = FMath::Clamp(t, 0.f, SegLen);
			FVector2D ProjPoint = P1 + SegDir * ClampedT;
			float DistToCenterline = FVector2D::Distance(Point2D, ProjPoint);

			float HoverTolerance = (Seg.Thickness * 0.5f) + SnapRadiusCm;
			if (DistToCenterline <= HoverTolerance && DistToCenterline < BestWallDist)
			{
				BestWallDist = DistToCenterline;
				OutSnappedSegmentID = Pair.Key;

				// Magnetic snapping threshold to corner endpoints (25cm)
				if (ClampedT <= 25.f)
				{
					OutSnapPos = P1;
					bOutIsEndpoint = true;
				}
				else if (ClampedT >= (SegLen - 25.f))
				{
					OutSnapPos = P2;
					bOutIsEndpoint = true;
				}
				else
				{
					OutSnapPos = ProjPoint; // Always on Centerline!
					bOutIsEndpoint = false;
				}
				bFoundSnap = true;
			}
		}
	}

	return bFoundSnap;
}

bool ARoomPlannerManager::FindWallEndpointSnap2D(const FVector2D& Point2D, float SnapRadiusCm, FVector2D& OutSnapPos) const
{
	int32 DummySegID = INDEX_NONE;
	bool bDummyIsEndpoint = false;
	return FindWallSnapPoint2D(Point2D, SnapRadiusCm, OutSnapPos, DummySegID, bDummyIsEndpoint);
}

void ARoomPlannerManager::CheckHoverSnapHint(const FVector& WorldPos)
{
	FVector2D Pos2D(WorldPos.X, WorldPos.Y);
	FVector2D SnapPos2D;
	bool bSnapped = FindWallEndpointSnap2D(Pos2D, 30.f, SnapPos2D);

	FVector SnapPointWorld = bSnapped ? FVector(SnapPos2D.X, SnapPos2D.Y, 0.f) : WorldPos;

	bHasActiveHoverSnap = bSnapped;
	CurrentHoverSnapWorldPos = SnapPointWorld;
	OnInteractiveWallDragProgress.Broadcast(0.f, SnapPointWorld, 0.f, bSnapped);
}

void ARoomPlannerManager::StartInteractiveWallDraw(const FVector& WorldPos)
{
	bIsDrawingWall = true;

	if (bHasActiveHoverSnap)
	{
		DragStartPoint = CurrentHoverSnapWorldPos;
	}
	else
	{
		DragStartPoint = WorldPos;
		FVector2D Start2D(WorldPos.X, WorldPos.Y);
		FVector2D SnapStart2D;
		if (FindWallEndpointSnap2D(Start2D, 30.f, SnapStart2D))
		{
			DragStartPoint.X = SnapStart2D.X;
			DragStartPoint.Y = SnapStart2D.Y;
		}
	}

	DragCurrentPoint = DragStartPoint;

	if (!PreviewWallActor && GetWorld())
	{
		FActorSpawnParameters SpawnParams;
		SpawnParams.Owner = this;
		SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		PreviewWallActor = GetWorld()->SpawnActor<AProceduralWallActor>(AProceduralWallActor::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams);
		if (PreviewWallActor)
		{
			PreviewWallActor->SetActorEnableCollision(false);
			PreviewWallActor->BaseWallMaterial = DefaultWallMaterial;
			PreviewWallActor->WallSelectionMaterial = WallSelectionMaterial;
			PreviewWallActor->OpeningSelectionMaterial = OpeningSelectionMaterial;
			if (PreviewWallActor->WallProceduralMesh)
			{
				PreviewWallActor->WallProceduralMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			}
		}
	}

	if (PreviewWallActor)
	{
		PreviewWallActor->SetActorHiddenInGame(false);
	}
}

void ARoomPlannerManager::UpdateInteractiveWallDraw(const FVector& WorldPos)
{
	if (!bIsDrawingWall) return;

	DragCurrentPoint = WorldPos;

	FVector2D P1(DragStartPoint.X, DragStartPoint.Y);
	FVector2D P2(DragCurrentPoint.X, DragCurrentPoint.Y);

	float CurrentLengthCm = FVector2D::Distance(P1, P2);

	FVector2D SnapP2;
	bool bSnappedToOtherWall = false;

	// Only attempt to snap P2 to other walls if P2 has moved far enough from P1 (> 30cm)
	if (CurrentLengthCm > 30.f)
	{
		bSnappedToOtherWall = FindWallEndpointSnap2D(P2, 30.f, SnapP2);
		// Don't snap P2 back to P1's position
		if (bSnappedToOtherWall && FVector2D::Distance(SnapP2, P1) > 20.f)
		{
			P2 = SnapP2;
			DragCurrentPoint.X = P2.X;
			DragCurrentPoint.Y = P2.Y;
		}
		else
		{
			bSnappedToOtherWall = false;
		}
	}

	float LengthCm = FVector2D::Distance(P1, P2);

	if (LengthCm > 1.f)
	{
		float AngleRad = FMath::Atan2(P2.Y - P1.Y, P2.X - P1.X);
		float AngleDeg = FMath::RadiansToDegrees(AngleRad);
		float NormAngle = FMath::UnwindDegrees(AngleDeg);

		static const float SnapAngles[] = { 0.f, 45.f, 90.f, 135.f, 180.f, -45.f, -90.f, -135.f, -180.f };
		float ThresholdDeg = 8.0f;
		bool bSnappedToAngle = false;
		float TargetSnapDeg = NormAngle;

		if (!bSnappedToOtherWall)
		{
			for (float SnapAngle : SnapAngles)
			{
				if (FMath::Abs(FMath::UnwindDegrees(NormAngle - SnapAngle)) <= ThresholdDeg)
				{
					TargetSnapDeg = SnapAngle;
					bSnappedToAngle = true;
					break;
				}
			}

			if (bSnappedToAngle)
			{
				float SnapRad = FMath::DegreesToRadians(TargetSnapDeg);
				P2.X = P1.X + LengthCm * FMath::Cos(SnapRad);
				P2.Y = P1.Y + LengthCm * FMath::Sin(SnapRad);
				DragCurrentPoint.X = P2.X;
				DragCurrentPoint.Y = P2.Y;
			}
		}

		if (PreviewWallActor)
		{
			PreviewWallActor->RebuildWallMesh(P1, P2, FVector2D::ZeroVector, FVector2D::ZeroVector, FVector2D::ZeroVector, FVector2D::ZeroVector, true, true, false);
		}

		float LengthMeters = LengthCm / 100.f;
		FVector MidpointWorld = (DragStartPoint + DragCurrentPoint) * 0.5f;
		OnInteractiveWallDragProgress.Broadcast(LengthMeters, MidpointWorld, TargetSnapDeg, (bSnappedToOtherWall || bSnappedToAngle));
	}
}

void ARoomPlannerManager::CommitInteractiveWallDraw()
{
	if (!bIsDrawingWall) return;

	bIsDrawingWall = false;
	if (PreviewWallActor)
	{
		PreviewWallActor->SetActorHiddenInGame(true);
	}

	FVector2D P1(DragStartPoint.X, DragStartPoint.Y);
	FVector2D P2(DragCurrentPoint.X, DragCurrentPoint.Y);

	if (FVector2D::Distance(P1, P2) >= 10.f)
	{
		if (HasAuthority())
		{
			int32 N1 = GetOrCreateNodeAtPosition(P1);
			int32 N2 = GetOrCreateNodeAtPosition(P2);
			if (N1 != INDEX_NONE && N2 != INDEX_NONE && N1 != N2)
			{
				AddWall(N1, N2, 20.f, 280.f);
			}
			ReplicatedRoomJSON = ExportLayoutToJSON();
			OnRoomPlannerUpdated.Broadcast(ReplicatedRoomJSON);
		}
	}
}

void ARoomPlannerManager::CancelInteractiveWallDraw()
{
	bIsDrawingWall = false;
	if (PreviewWallActor)
	{
		PreviewWallActor->SetActorHiddenInGame(true);
	}
}

void ARoomPlannerManager::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(ARoomPlannerManager, ReplicatedRoomJSON);
}

void ARoomPlannerManager::OnRep_ReplicatedRoomJSON()
{
	ImportLayoutFromJSON(ReplicatedRoomJSON);
}


void ARoomPlannerManager::SetToolMode(EPlannerToolMode NewToolMode)
{
	const bool bChanged = (ActiveToolMode != NewToolMode);
	ActiveToolMode = NewToolMode;
	if (ActiveToolMode != EPlannerToolMode::Select)
	{
		ClearAllSelection();
	}
	if (ActiveToolMode != EPlannerToolMode::PlaceFurniture)
	{
		PendingPlacementKind = EPlannerPlacementKind::None;
		PendingPlacementAssetID.Empty();
	}
	// Only an actual tool change cancels a corner drag; re-asserting Select on every click must not.
	if (bChanged && DraggingNodeID != -1)
	{
		DraggingNodeID = -1;
	}
	RefreshNodeHandles();
}

void ARoomPlannerManager::TickLocalNodeDrag()
{
	if (!b2DViewMode || ActiveToolMode != EPlannerToolMode::Select || !bPlannerUIOpen || !GetWorld())
	{
		bPrevLMBDownForNodeDrag = false;
		return;
	}

	// Local player controller only (never the dedicated server / remote controllers).
	AAwsTutorial_PlayerController* LocalPC = nullptr;
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		if (AAwsTutorial_PlayerController* PC = Cast<AAwsTutorial_PlayerController>(It->Get()))
		{
			if (PC->IsLocalController()) { LocalPC = PC; break; }
		}
	}
	if (!LocalPC)
	{
		bPrevLMBDownForNodeDrag = false;
		return;
	}

	// Slate's own pressed-button set: valid even when a widget consumed the press before PlayerInput saw it.
	const bool bLMBDown = FSlateApplication::IsInitialized() && FSlateApplication::Get().GetPressedMouseButtons().Contains(EKeys::LeftMouseButton);
	const bool bJustPressed = bLMBDown && !bPrevLMBDownForNodeDrag;
	const bool bJustReleased = !bLMBDown && bPrevLMBDownForNodeDrag;
	bPrevLMBDownForNodeDrag = bLMBDown;

	FVector GroundPos = FVector::ZeroVector;
	bool bHasGround = false;
	{
		FVector Origin, Dir;
		if (LocalPC->DeprojectMousePositionToWorld(Origin, Dir) && !FMath::IsNearlyZero(Dir.Z))
		{
			const float T = -Origin.Z / Dir.Z;
			if (T >= 0.f)
			{
				GroundPos = Origin + T * Dir;
				GroundPos.Z = 0.f;
				bHasGround = true;
			}
		}
	}

	if (DraggingNodeID == -1)
	{
		// Start: LMB pressed this frame over a corner handle (whatever click path is live).
		if (bJustPressed && bHasGround && !bIsDrawingWall)
		{
			const int32 NodeID = FindNodeAtWorldPos(GroundPos, 25.f);
			if (NodeID != -1)
			{
				StartNodeDrag(NodeID);
			}
		}
		return;
	}

	if (bLMBDown)
	{
		if (bHasGround)
		{
			UpdateNodeDrag(GroundPos);
		}
	}
	else if (bJustReleased || !bLMBDown)
	{
		int32 NodeID = -1;
		FVector2D FinalPos;
		if (EndNodeDrag(NodeID, FinalPos))
		{
			LocalPC->Server_MoveNode(NodeID, FinalPos);
		}
	}
}

void ARoomPlannerManager::ClearWallSelection()
{
	SelectedSegmentID = -1;
	SelectedOpeningIndex = -1;
	for (auto& Pair : WallActors)
	{
		if (Pair.Value)
		{
			Pair.Value->SetSelectedHighlight(false);
			Pair.Value->ClearAllOpeningHighlights();
		}
	}
	OnWallSelected.Broadcast(-1, 0.f);
	NotifySelectionChanged();
}

int32 ARoomPlannerManager::SelectWallAtWorldPos(const FVector& WorldPos)
{
	if (ActiveToolMode != EPlannerToolMode::Select)
	{
		ClearWallSelection();
		return -1;
	}

	// REQ-02: a press on a corner handle (2D only) is a drag, not a wall pick — whichever
	// click path (native, tick, or Blueprint) called us. The current selection is left untouched.
	if (b2DViewMode)
	{
		if (DraggingNodeID != -1)
		{
			return SelectedSegmentID;
		}
		const int32 HandleNodeID = FindNodeAtWorldPos(WorldPos, 25.f);
		if (HandleNodeID != -1)
		{
			StartNodeDrag(HandleNodeID);
			return SelectedSegmentID;
		}
	}

	FVector2D Click2D(WorldPos.X, WorldPos.Y);
	int32 ClosestSegID = -1;
	float MinDistSq = 22500.f;

	for (const auto& Pair : WallSegments)
	{
		const FWallSegment& Seg = Pair.Value;
		if (Nodes.Contains(Seg.StartNodeID) && Nodes.Contains(Seg.EndNodeID))
		{
			FVector2D P1 = Nodes[Seg.StartNodeID].Position;
			FVector2D P2 = Nodes[Seg.EndNodeID].Position;

			FVector2D Dir = P2 - P1;
			float LengthSq = Dir.SizeSquared();
			if (LengthSq > 0.001f)
			{
				float t = FMath::Clamp(FVector2D::DotProduct(Click2D - P1, Dir) / LengthSq, 0.f, 1.f);
				FVector2D ClosestPoint = P1 + t * Dir;
				float DistSq = FVector2D::DistSquared(Click2D, ClosestPoint);
				if (DistSq < MinDistSq)
				{
					MinDistSq = DistSq;
					ClosestSegID = Pair.Key;
				}
			}
		}
	}

	// A wall / opening pick replaces any floor / object / cabinet-set selection.
	if (ClosestSegID != -1)
	{
		SelectedRoomID = -1;
		SelectedObjectID.Empty();
		SelectedCabinetSetID.Empty();
	}

	SelectedSegmentID = ClosestSegID;
	SelectedOpeningIndex = -1;
	float LengthMeters = 0.f;

	if (SelectedSegmentID != -1 && WallSegments.Contains(SelectedSegmentID))
	{
		const FWallSegment& Seg = WallSegments[SelectedSegmentID];
		FVector2D P1 = Nodes[Seg.StartNodeID].Position;
		FVector2D P2 = Nodes[Seg.EndNodeID].Position;
		float SegLen = FVector2D::Distance(P1, P2);
		LengthMeters = SegLen / 100.f;

		if (SegLen > 0.1f)
		{
			FVector2D SegDir = (P2 - P1) / SegLen;
			float ClickDistAlongWall = FVector2D::DotProduct(Click2D - P1, SegDir);

			for (int32 OpIdx = 0; OpIdx < Seg.Openings.Num(); ++OpIdx)
			{
				const FWallOpening& Op = Seg.Openings[OpIdx];
				if (FMath::Abs(ClickDistAlongWall - Op.DistanceFromStart) <= (Op.Width * 0.5f + 25.f))
				{
					SelectedOpeningIndex = OpIdx;
					break;
				}
			}
		}
	}

	UpdateSelectionVisuals();

	OnWallSelected.Broadcast(SelectedSegmentID, LengthMeters);
	NotifySelectionChanged();
	return SelectedSegmentID;
}

void ARoomPlannerManager::UpdateSelectionVisuals()
{
	for (auto& Pair : WallActors)
	{
		if (Pair.Value)
		{
			bool bIsWallSelected = (Pair.Key == SelectedSegmentID);
			bool bHighlightWall = bIsWallSelected;
			bool bHighlightOpening = false;

			if (bIsWallSelected && SelectedOpeningIndex != -1)
			{
				bHighlightWall = false;
				bHighlightOpening = true;
			}

			Pair.Value->SetSelectedHighlight(bHighlightWall, 2);
			Pair.Value->ClearAllOpeningHighlights();

			if (bHighlightOpening)
			{
				Pair.Value->SetOpeningSelectedHighlight(SelectedOpeningIndex, true, 2);
			}
		}
	}

	// Floors: selected room shows the selection material, every other room its finish / default material.
	if (FloorProceduralMesh)
	{
		const int32 NumSections = FloorProceduralMesh->GetNumSections();
		for (const auto& Pair : Rooms)
		{
			const int32 SectionIdx = Pair.Key - 1;
			if (SectionIdx < 0 || SectionIdx >= NumSections) continue;

			UMaterialInterface* Mat = nullptr;
			if (Pair.Key == SelectedRoomID && WallSelectionMaterial)
			{
				Mat = WallSelectionMaterial;
			}
			else if (const TObjectPtr<UMaterialInterface>* Found = FloorSectionMaterials.Find(Pair.Key))
			{
				Mat = Found->Get();
			}
			if (Mat)
			{
				FloorProceduralMesh->SetMaterial(SectionIdx, Mat);
			}
		}
	}

	// Placed objects
	for (auto& Pair : PlacedObjectActors)
	{
		if (Pair.Value)
		{
			Pair.Value->SetSelectedHighlight(Pair.Key == SelectedObjectID);
		}
	}

	// Cabinet sets (custom depth outline on every mesh of the booth)
	for (const auto& Pair : CabinetSets)
	{
		if (AShowroomBooth* Booth = FindCabinetSetActor(Pair.Key))
		{
			const bool bSel = (Pair.Key == SelectedCabinetSetID);
			TArray<UPrimitiveComponent*> Prims;
			Booth->GetComponents<UPrimitiveComponent>(Prims);
			for (UPrimitiveComponent* Prim : Prims)
			{
				if (Prim)
				{
					Prim->SetRenderCustomDepth(bSel);
					if (bSel) Prim->SetCustomDepthStencilValue(2);
				}
			}
		}
	}
}

float ARoomPlannerManager::GetWallLength(int32 SegmentID) const
{
	if (!WallSegments.Contains(SegmentID)) return 0.f;

	const FWallSegment& Seg = WallSegments[SegmentID];
	if (Nodes.Contains(Seg.StartNodeID) && Nodes.Contains(Seg.EndNodeID))
	{
		FVector2D P1 = Nodes[Seg.StartNodeID].Position;
		FVector2D P2 = Nodes[Seg.EndNodeID].Position;
		return FVector2D::Distance(P1, P2) / 100.f; // Return in meters
	}
	return 0.f;
}

bool ARoomPlannerManager::SetWallLength(int32 SegmentID, float NewLengthMeters)
{
	if (NewLengthMeters <= 0.1f || !WallSegments.Contains(SegmentID)) return false;

	const FWallSegment& Seg = WallSegments[SegmentID];
	if (Nodes.Contains(Seg.StartNodeID) && Nodes.Contains(Seg.EndNodeID))
	{
		FVector2D P1 = Nodes[Seg.StartNodeID].Position;
		FVector2D P2 = Nodes[Seg.EndNodeID].Position;
		FVector2D Dir = (P2 - P1).GetSafeNormal();
		float NewLengthCm = NewLengthMeters * 100.f;

		// Shared-node model (intended): the END node moves and every connected wall follows.
		// ApplyNodeMove keeps openings attached and refuses the edit if one no longer fits (REQ-09).
		return ApplyNodeMove(Seg.EndNodeID, P1 + Dir * NewLengthCm, false);
	}
	return false;
}

bool ARoomPlannerManager::SetWallDimensions(int32 SegmentID, float HeightCm, float ThicknessCm)
{
	FWallSegment* Seg = WallSegments.Find(SegmentID);
	if (!Seg) return false;

	if (HeightCm < 10.f || HeightCm > 1000.f)
	{
		BroadcastRejected(TEXT("Высота стены должна быть от 10 до 1000 см"));
		return false;
	}
	if (ThicknessCm < 1.f || ThicknessCm > 200.f)
	{
		BroadcastRejected(TEXT("Толщина стены должна быть от 1 до 200 см"));
		return false;
	}

	// Existing openings must still fit under the new height (same rule as opening sizing).
	for (const FWallOpening& Op : Seg->Openings)
	{
		if (Op.SillHeight + Op.Height > HeightCm + 0.5f)
		{
			BroadcastRejected(FString::Printf(TEXT("Проём (%.0f см от пола) выше новой высоты стены %.0f см"), Op.SillHeight + Op.Height, HeightCm));
			return false;
		}
	}

	if (FMath::IsNearlyEqual(Seg->Height, HeightCm, 0.01f) && FMath::IsNearlyEqual(Seg->Thickness, ThicknessCm, 0.01f))
	{
		return true; // nothing to do
	}

	Seg->Height = HeightCm;
	Seg->Thickness = ThicknessCm;

	RebuildAllWalls();   // recomputes bisector corner joints with the new thickness
	RebuildRooms();      // ceiling follows the room's wall height
	CommitStateAfterMutation();
	UpdateSelectionVisuals();
	return true;
}

bool ARoomPlannerManager::DeleteWallAtWorldPos(const FVector& WorldPos)
{
	int32 TargetSeg = SelectWallAtWorldPos(WorldPos);
	if (TargetSeg != -1)
	{
		RemoveWall(TargetSeg);
		return true;
	}
	return false;
}

static float FindNonOverlappingOpeningDist(const FWallSegment& Seg, float NewWidthCm, float WallLengthCm)
{
	float HalfW = NewWidthCm * 0.5f;
	float DefaultCenter = WallLengthCm * 0.5f;

	if (Seg.Openings.Num() == 0)
	{
		return FMath::Clamp(DefaultCenter, HalfW + 5.f, FMath::Max(HalfW + 5.f, WallLengthCm - HalfW - 5.f));
	}

	TArray<TPair<float, float>> Intervals;
	for (const FWallOpening& Op : Seg.Openings)
	{
		Intervals.Add(TPair<float, float>(Op.DistanceFromStart - Op.Width * 0.5f, Op.DistanceFromStart + Op.Width * 0.5f));
	}
	Intervals.Sort([](const TPair<float, float>& A, const TPair<float, float>& B) {
		return A.Key < B.Key;
	});

	float RightEdge = Intervals.Last().Value;
	float CandidateRight = RightEdge + 15.f + HalfW;
	if (CandidateRight + HalfW <= WallLengthCm - 5.f)
	{
		return CandidateRight;
	}

	float LeftEdge = Intervals[0].Key;
	float CandidateLeft = LeftEdge - 15.f - HalfW;
	if (CandidateLeft - HalfW >= 5.f)
	{
		return CandidateLeft;
	}

	for (int32 i = 0; i < Intervals.Num() - 1; ++i)
	{
		float GapStart = Intervals[i].Value;
		float GapEnd = Intervals[i + 1].Key;
		if (GapEnd - GapStart >= NewWidthCm + 10.f)
		{
			return GapStart + 5.f + HalfW;
		}
	}

	// Cannot find any space
	return -1.0f;
}

bool ARoomPlannerManager::AddDoorToWall(int32 SegmentID, float WidthMeters, float HeightMeters, float DistFromStartCm)
{
	if (!WallSegments.Contains(SegmentID)) return false;

	const FWallSegment& Seg = WallSegments[SegmentID];
	if (Nodes.Contains(Seg.StartNodeID) && Nodes.Contains(Seg.EndNodeID))
	{
		FVector2D P1 = Nodes[Seg.StartNodeID].Position;
		FVector2D P2 = Nodes[Seg.EndNodeID].Position;
		float WallLengthCm = FVector2D::Distance(P1, P2);

		float WidthCm = WidthMeters * 100.f;
		float HeightCm = HeightMeters * 100.f;

		if (DistFromStartCm < 0.f)
		{
			DistFromStartCm = FindNonOverlappingOpeningDist(Seg, WidthCm, WallLengthCm);
			if (DistFromStartCm < 0.f)
			{
				return false;
			}
		}

		AddOpeningToWall(SegmentID, EOpeningType::Door, DistFromStartCm, WidthCm, HeightCm, 0.f);
		if (SelectedSegmentID == SegmentID)
		{
			SelectedOpeningIndex = WallSegments[SegmentID].Openings.Num() - 1;
			UpdateSelectionVisuals();
		}
		ReplicatedRoomJSON = ExportLayoutToJSON();
		OnRoomPlannerUpdated.Broadcast(ReplicatedRoomJSON);
		return true;
	}
	return false;
}

bool ARoomPlannerManager::AddDoorToSelectedWall(float WidthMeters, float HeightMeters)
{
	if (SelectedSegmentID == -1) return false;
	return AddDoorToWall(SelectedSegmentID, WidthMeters, HeightMeters, -1.f);
}

bool ARoomPlannerManager::AddWindowToWall(int32 SegmentID, float WidthMeters, float HeightMeters, float SillHeightMeters, float DistFromStartCm)
{
	if (!WallSegments.Contains(SegmentID)) return false;

	const FWallSegment& Seg = WallSegments[SegmentID];
	if (Nodes.Contains(Seg.StartNodeID) && Nodes.Contains(Seg.EndNodeID))
	{
		FVector2D P1 = Nodes[Seg.StartNodeID].Position;
		FVector2D P2 = Nodes[Seg.EndNodeID].Position;
		float WallLengthCm = FVector2D::Distance(P1, P2);

		float WidthCm = WidthMeters * 100.f;
		float HeightCm = HeightMeters * 100.f;
		float SillCm = SillHeightMeters * 100.f;

		if (DistFromStartCm < 0.f)
		{
			DistFromStartCm = FindNonOverlappingOpeningDist(Seg, WidthCm, WallLengthCm);
			if (DistFromStartCm < 0.f)
			{
				return false;
			}
		}

		AddOpeningToWall(SegmentID, EOpeningType::Window, DistFromStartCm, WidthCm, HeightCm, SillCm);
		if (SelectedSegmentID == SegmentID)
		{
			SelectedOpeningIndex = WallSegments[SegmentID].Openings.Num() - 1;
			UpdateSelectionVisuals();
		}
		ReplicatedRoomJSON = ExportLayoutToJSON();
		OnRoomPlannerUpdated.Broadcast(ReplicatedRoomJSON);
		return true;
	}
	return false;
}

bool ARoomPlannerManager::AddWindowToSelectedWall(float WidthMeters, float HeightMeters, float SillHeightMeters)
{
	if (SelectedSegmentID == -1) return false;
	return AddWindowToWall(SelectedSegmentID, WidthMeters, HeightMeters, SillHeightMeters, -1.f);
}

void ARoomPlannerManager::BuildPreset4x4mRoom()
{
	ClearLayout();

	int32 N1 = AddNode(FVector2D(-200.f, -200.f));
	int32 N2 = AddNode(FVector2D(200.f, -200.f));
	int32 N3 = AddNode(FVector2D(200.f, 200.f));
	int32 N4 = AddNode(FVector2D(-200.f, 200.f));

	AddWall(N1, N2);
	AddWall(N2, N3);
	AddWall(N3, N4);
	AddWall(N4, N1);

	RebuildRooms();
	ReplicatedRoomJSON = ExportLayoutToJSON();
	OnRoomPlannerUpdated.Broadcast(ReplicatedRoomJSON);
}

bool ARoomPlannerManager::GetOpeningDetails(int32 SegmentID, int32 OpeningIndex, float& OutWidthMeters, float& OutHeightMeters, float& OutSillHeightMeters) const
{
	if (WallSegments.Contains(SegmentID))
	{
		const FWallSegment& Seg = WallSegments[SegmentID];
		int32 OpIdx = (OpeningIndex == -1) ? 0 : OpeningIndex;
		if (Seg.Openings.IsValidIndex(OpIdx))
		{
			const FWallOpening& Op = Seg.Openings[OpIdx];
			OutWidthMeters = Op.Width / 100.f;
			OutHeightMeters = Op.Height / 100.f;
			OutSillHeightMeters = Op.SillHeight / 100.f;
			return true;
		}
	}
	return false;
}

bool ARoomPlannerManager::GetOpeningDistance(int32 SegmentID, int32 OpeningIndex, float& OutDistFromStartCm) const
{
	if (WallSegments.Contains(SegmentID))
	{
		const FWallSegment& Seg = WallSegments[SegmentID];
		int32 OpIdx = (OpeningIndex == -1) ? 0 : OpeningIndex;
		if (Seg.Openings.IsValidIndex(OpIdx))
		{
			OutDistFromStartCm = Seg.Openings[OpIdx].DistanceFromStart;
			return true;
		}
	}
	return false;
}

bool ARoomPlannerManager::UpdateOpeningPosition(int32 SegmentID, int32 OpeningIndex, float NewDistFromStartCm)
{
	if (!WallSegments.Contains(SegmentID)) return false;

	FWallSegment& Seg = WallSegments[SegmentID];
	if (Seg.Openings.IsValidIndex(OpeningIndex))
	{
		if (Nodes.Contains(Seg.StartNodeID) && Nodes.Contains(Seg.EndNodeID))
		{
			FVector2D P1 = Nodes[Seg.StartNodeID].Position;
			FVector2D P2 = Nodes[Seg.EndNodeID].Position;
			float WallLen = FVector2D::Distance(P1, P2);
			float HalfW = Seg.Openings[OpeningIndex].Width * 0.5f;

			float WallMinDist = HalfW + 5.f;
			float WallMaxDist = WallLen - HalfW - 5.f;
			if (WallMinDist > WallMaxDist)
			{
				WallMinDist = WallLen * 0.5f;
				WallMaxDist = WallMinDist;
			}

			float CandidateDist = FMath::Clamp(NewDistFromStartCm, WallMinDist, WallMaxDist);
			FString DraggedOpID = Seg.Openings[OpeningIndex].OpeningID;

			// Handle snap-through collision against other openings to allow swapping sides
			for (int32 i = 0; i < Seg.Openings.Num(); ++i)
			{
				if (i == OpeningIndex) continue;
				const FWallOpening& Other = Seg.Openings[i];
				float OtherHalfW = Other.Width * 0.5f;
				float MinClearance = HalfW + OtherHalfW + 5.f;

				if (FMath::Abs(CandidateDist - Other.DistanceFromStart) < MinClearance)
				{
					if (CandidateDist >= Other.DistanceFromStart)
					{
						// Snap to the right side of Other
						CandidateDist = FMath::Min(WallMaxDist, Other.DistanceFromStart + MinClearance);
					}
					else
					{
						// Snap to the left side of Other
						CandidateDist = FMath::Max(WallMinDist, Other.DistanceFromStart - MinClearance);
					}
				}
			}

			Seg.Openings[OpeningIndex].DistanceFromStart = CandidateDist;

			// Sort openings by distance from start to maintain topological order
			Seg.Openings.Sort([](const FWallOpening& A, const FWallOpening& B) {
				return A.DistanceFromStart < B.DistanceFromStart;
			});

			// Re-acquire index of the dragged opening after sorting
			int32 NewDraggedIndex = Seg.Openings.IndexOfByPredicate([&](const FWallOpening& Op) {
				return Op.OpeningID == DraggedOpID;
			});

			if (NewDraggedIndex != INDEX_NONE && SelectedSegmentID == SegmentID)
			{
				SelectedOpeningIndex = NewDraggedIndex;
			}

			if (TObjectPtr<AProceduralWallActor>* ActorPtr = WallActors.Find(SegmentID))
			{
				if (*ActorPtr)
				{
					(*ActorPtr)->WallData = Seg;
				}
			}

			RebuildAllWalls();
			ReplicatedRoomJSON = ExportLayoutToJSON();
			UpdateSelectionVisuals();
			OnRoomPlannerUpdated.Broadcast(ReplicatedRoomJSON);
			return true;
		}
	}
	return false;
}

bool ARoomPlannerManager::DragSelectedOpeningToWorldPos(const FVector& WorldPos)
{
	if (SelectedSegmentID == -1 || SelectedOpeningIndex == -1 || !WallSegments.Contains(SelectedSegmentID))
	{
		return false;
	}

	const FWallSegment& Seg = WallSegments[SelectedSegmentID];
	if (Nodes.Contains(Seg.StartNodeID) && Nodes.Contains(Seg.EndNodeID))
	{
		FVector2D P1 = Nodes[Seg.StartNodeID].Position;
		FVector2D P2 = Nodes[Seg.EndNodeID].Position;
		FVector2D Dir = (P2 - P1).GetSafeNormal();
		float NewDistCm = FVector2D::DotProduct(FVector2D(WorldPos.X, WorldPos.Y) - P1, Dir);
		return UpdateOpeningPosition(SelectedSegmentID, SelectedOpeningIndex, NewDistCm);
	}
	return false;
}

bool ARoomPlannerManager::UpdateOpeningDimensions(int32 SegmentID, int32 OpeningIndex, float WidthMeters, float HeightMeters, float SillHeightMeters)
{
	if (!WallSegments.Contains(SegmentID)) return false;

	FWallSegment& Seg = WallSegments[SegmentID];
	if (Seg.Openings.IsValidIndex(OpeningIndex) && Nodes.Contains(Seg.StartNodeID) && Nodes.Contains(Seg.EndNodeID))
	{
		const float WallLen = FVector2D::Distance(Nodes[Seg.StartNodeID].Position, Nodes[Seg.EndNodeID].Position);

		// Free numeric sizing (REQ-08), validated against the wall (REQ-09).
		FWallOpening Candidate = Seg.Openings[OpeningIndex];
		Candidate.Width = WidthMeters * 100.f;
		Candidate.Height = HeightMeters * 100.f;
		Candidate.SillHeight = SillHeightMeters * 100.f;

		// If the new width no longer fits at the current position, slide the opening inside the wall first.
		const float HalfW = Candidate.Width * 0.5f;
		if (Candidate.Width <= WallLen)
		{
			Candidate.DistanceFromStart = FMath::Clamp(Candidate.DistanceFromStart, HalfW, WallLen - HalfW);
		}

		FString Reason;
		if (!ValidateOpeningFits(Seg, WallLen, Candidate, OpeningIndex, Reason))
		{
			BroadcastRejected(Reason);
			return false;
		}

		Seg.Openings[OpeningIndex] = Candidate;

		RebuildAllWalls();
		CommitStateAfterMutation();
		UpdateSelectionVisuals();
		return true;
	}
	return false;
}

bool ARoomPlannerManager::DeleteSelectedWall()
{
	if (SelectedSegmentID == -1) return false;

	int32 TargetSeg = SelectedSegmentID;
	SelectedSegmentID = -1;
	SelectedOpeningIndex = -1;
	RemoveWall(TargetSeg);
	return true;
}

bool ARoomPlannerManager::DeleteOpening(int32 SegmentID, int32 OpeningIndex)
{
	if (!WallSegments.Contains(SegmentID)) return false;
	if (!WallSegments[SegmentID].Openings.IsValidIndex(OpeningIndex)) return false;

	WallSegments[SegmentID].Openings.RemoveAt(OpeningIndex);
	RebuildAllWalls();
	if (SelectedSegmentID == SegmentID && SelectedOpeningIndex == OpeningIndex)
	{
		SelectedOpeningIndex = -1;
	}
	else if (SelectedSegmentID == SegmentID && SelectedOpeningIndex > OpeningIndex)
	{
		SelectedOpeningIndex--;
	}
	UpdateSelectionVisuals();
	ReplicatedRoomJSON = ExportLayoutToJSON();
	OnRoomPlannerUpdated.Broadcast(ReplicatedRoomJSON);
	OnWallSelected.Broadcast(SelectedSegmentID, GetWallLength(SelectedSegmentID));
	return true;
}

bool ARoomPlannerManager::DeleteSelectedOpening()
{
	if (SelectedSegmentID == -1 || SelectedOpeningIndex == -1) return false;
	return DeleteOpening(SelectedSegmentID, SelectedOpeningIndex);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Internal helpers
// ═══════════════════════════════════════════════════════════════════════════════

void ARoomPlannerManager::CommitStateAfterMutation()
{
	ReplicatedRoomJSON = ExportLayoutToJSON();
	OnRoomPlannerUpdated.Broadcast(ReplicatedRoomJSON);
}

void ARoomPlannerManager::NotifySelectionChanged()
{
	OnSelectionChanged.Broadcast();
}

void ARoomPlannerManager::BroadcastRejected(const FString& Reason)
{
	const float Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
	if (Now - LastRejectBroadcastTime >= 0.25f)
	{
		LastRejectBroadcastTime = Now;
		UE_LOG(LogTemp, Warning, TEXT("[MaxiMallConstructor] Operation rejected: %s"), *Reason);
		OnOperationRejected.Broadcast(Reason);
	}
}

bool ARoomPlannerManager::ValidateOpeningFits(const FWallSegment& Seg, float WallLengthCm, const FWallOpening& Candidate, int32 IgnoreOpeningIndex, FString& OutReason) const
{
	if (Candidate.Width < 10.f || Candidate.Height < 10.f)
	{
		OutReason = TEXT("Минимальный размер проёма — 10 см");
		return false;
	}
	if (Candidate.SillHeight < 0.f)
	{
		OutReason = TEXT("Высота от пола не может быть отрицательной");
		return false;
	}
	if (Candidate.SillHeight + Candidate.Height > Seg.Height + 0.5f)
	{
		OutReason = FString::Printf(TEXT("Проём выше стены (макс. %.0f см)"), Seg.Height);
		return false;
	}

	const float Start = Candidate.DistanceFromStart - Candidate.Width * 0.5f;
	const float End = Candidate.DistanceFromStart + Candidate.Width * 0.5f;
	if (Start < -0.5f || End > WallLengthCm + 0.5f)
	{
		OutReason = FString::Printf(TEXT("Проём (%.0f см) не помещается на стене (%.0f см)"), Candidate.Width, WallLengthCm);
		return false;
	}

	for (int32 i = 0; i < Seg.Openings.Num(); ++i)
	{
		if (i == IgnoreOpeningIndex) continue;
		const FWallOpening& Other = Seg.Openings[i];
		const float OStart = Other.DistanceFromStart - Other.Width * 0.5f;
		const float OEnd = Other.DistanceFromStart + Other.Width * 0.5f;
		if (Start < OEnd - 0.5f && End > OStart + 0.5f)
		{
			OutReason = TEXT("Проём пересекается с соседним проёмом");
			return false;
		}
	}
	return true;
}

bool ARoomPlannerManager::CanMoveNode(int32 NodeID, const FVector2D& NewPosition, FString& OutReason) const
{
	const FWallNode* Node = Nodes.Find(NodeID);
	if (!Node)
	{
		OutReason = TEXT("Узел не найден");
		return false;
	}

	for (int32 SegID : Node->ConnectedSegmentIDs)
	{
		const FWallSegment* Seg = WallSegments.Find(SegID);
		if (!Seg) continue;
		const int32 OtherNodeID = (Seg->StartNodeID == NodeID) ? Seg->EndNodeID : Seg->StartNodeID;
		const FWallNode* Other = Nodes.Find(OtherNodeID);
		if (!Other) continue;

		const float OldLen = FVector2D::Distance(Node->Position, Other->Position);
		const float NewLen = FVector2D::Distance(NewPosition, Other->Position);
		if (NewLen < 10.f)
		{
			OutReason = TEXT("Стена не может быть короче 10 см");
			return false;
		}

		// Openings keep their distance from the UNMOVED corner. When the start node moves,
		// DistanceFromStart therefore shifts by the length delta.
		const float Shift = (Seg->StartNodeID == NodeID) ? (NewLen - OldLen) : 0.f;

		FWallSegment Shifted = *Seg;
		for (FWallOpening& Op : Shifted.Openings)
		{
			Op.DistanceFromStart += Shift;
		}
		for (int32 i = 0; i < Shifted.Openings.Num(); ++i)
		{
			if (!ValidateOpeningFits(Shifted, NewLen, Shifted.Openings[i], i, OutReason))
			{
				OutReason = FString::Printf(TEXT("Стена не может быть изменена: %s"), *OutReason);
				return false;
			}
		}
	}
	return true;
}

bool ARoomPlannerManager::ApplyNodeMove(int32 NodeID, const FVector2D& NewPosition, bool bLocalPreviewOnly)
{
	FString Reason;
	if (!CanMoveNode(NodeID, NewPosition, Reason))
	{
		BroadcastRejected(Reason);
		return false;
	}

	FWallNode& Node = Nodes[NodeID];
	for (int32 SegID : Node.ConnectedSegmentIDs)
	{
		FWallSegment* Seg = WallSegments.Find(SegID);
		if (!Seg || Seg->StartNodeID != NodeID) continue;
		const FWallNode* Other = Nodes.Find(Seg->EndNodeID);
		if (!Other) continue;
		const float OldLen = FVector2D::Distance(Node.Position, Other->Position);
		const float NewLen = FVector2D::Distance(NewPosition, Other->Position);
		const float Shift = NewLen - OldLen;
		for (FWallOpening& Op : Seg->Openings)
		{
			Op.DistanceFromStart += Shift;
		}
	}
	Node.Position = NewPosition;

	RebuildAllWalls();
	RebuildRooms();
	if (!bLocalPreviewOnly)
	{
		CommitStateAfterMutation();
	}
	UpdateSelectionVisuals();
	return true;
}

bool ARoomPlannerManager::MoveNode(int32 NodeID, const FVector2D& NewPosition)
{
	// Server-side: snap exactly like the client preview, validate/apply, then join the node to
	// whatever it landed on (another corner, or the middle of a wall) so a real junction exists.
	const FVector2D Snapped = SnapNodeDragPosition(NodeID, NewPosition);
	if (!ApplyNodeMove(NodeID, Snapped, false))
	{
		return false;
	}
	TryConnectMovedNode(NodeID);
	return true;
}

bool ARoomPlannerManager::MergeNodeInto(int32 NodeID, int32 TargetNodeID)
{
	if (NodeID == TargetNodeID || !Nodes.Contains(NodeID) || !Nodes.Contains(TargetNodeID)) return false;

	const TArray<int32> SrcSegs = Nodes[NodeID].ConnectedSegmentIDs;
	const TArray<int32> TgtSegs = Nodes[TargetNodeID].ConnectedSegmentIDs;

	// Validation: no wall may collapse (both ends on the target) and no duplicate wall may appear.
	for (int32 SegID : SrcSegs)
	{
		const FWallSegment* Seg = WallSegments.Find(SegID);
		if (!Seg) continue;
		const int32 OtherEnd = (Seg->StartNodeID == NodeID) ? Seg->EndNodeID : Seg->StartNodeID;
		if (OtherEnd == TargetNodeID)
		{
			BroadcastRejected(TEXT("Стена не может быть соединена сама с собой"));
			return false;
		}
		for (int32 TSegID : TgtSegs)
		{
			const FWallSegment* TSeg = WallSegments.Find(TSegID);
			if (!TSeg) continue;
			const int32 TOther = (TSeg->StartNodeID == TargetNodeID) ? TSeg->EndNodeID : TSeg->StartNodeID;
			if (TOther == OtherEnd)
			{
				BroadcastRejected(TEXT("Между этими углами уже есть стена"));
				return false;
			}
		}
	}

	// Re-point every wall of the moved node to the target node.
	for (int32 SegID : SrcSegs)
	{
		if (FWallSegment* Seg = WallSegments.Find(SegID))
		{
			if (Seg->StartNodeID == NodeID) Seg->StartNodeID = TargetNodeID;
			if (Seg->EndNodeID == NodeID) Seg->EndNodeID = TargetNodeID;
			Nodes[TargetNodeID].ConnectedSegmentIDs.AddUnique(SegID);
		}
	}
	Nodes.Remove(NodeID);
	if (DraggingNodeID == NodeID) DraggingNodeID = -1;
	return true;
}

bool ARoomPlannerManager::TryConnectMovedNode(int32 NodeID)
{
	const FWallNode* Node = Nodes.Find(NodeID);
	if (!Node) return false;
	const FVector2D Pos = Node->Position;
	const TArray<int32> MySegs = Node->ConnectedSegmentIDs;

	auto IsNeighbourNode = [&](int32 OtherNodeID) -> bool
	{
		for (int32 SegID : MySegs)
		{
			const FWallSegment* Seg = WallSegments.Find(SegID);
			if (Seg && (Seg->StartNodeID == OtherNodeID || Seg->EndNodeID == OtherNodeID)) return true;
		}
		return false;
	};

	// 1. Coincident corner → merge.
	int32 TargetNodeID = -1;
	float BestDistSq = 25.f * 25.f;
	for (const auto& Pair : Nodes)
	{
		if (Pair.Key == NodeID || IsNeighbourNode(Pair.Key)) continue;
		const float D = FVector2D::DistSquared(Pair.Value.Position, Pos);
		if (D < BestDistSq) { BestDistSq = D; TargetNodeID = Pair.Key; }
	}
	if (TargetNodeID != -1)
	{
		if (!MergeNodeInto(NodeID, TargetNodeID)) return false;
		RebuildAllWalls();
		RebuildRooms();
		CommitStateAfterMutation();
		UpdateSelectionVisuals();
		return true;
	}

	// 2. Landed on the middle of a wall that is not one of its own → split it (T-junction) and merge with the junction.
	int32 TargetSegID = -1;
	FVector2D SplitPoint = Pos;
	float BestWallDist = TNumericLimits<float>::Max();
	for (const auto& Pair : WallSegments)
	{
		if (MySegs.Contains(Pair.Key)) continue;
		const FWallSegment& Seg = Pair.Value;
		if (!Nodes.Contains(Seg.StartNodeID) || !Nodes.Contains(Seg.EndNodeID)) continue;
		const FVector2D P1 = Nodes[Seg.StartNodeID].Position;
		const FVector2D P2 = Nodes[Seg.EndNodeID].Position;
		const float SegLen = FVector2D::Distance(P1, P2);
		if (SegLen < 40.f) continue;
		const FVector2D SegDir = (P2 - P1) / SegLen;
		const float T = FVector2D::DotProduct(Pos - P1, SegDir);
		if (T < 20.f || T > SegLen - 20.f) continue;
		const FVector2D Proj = P1 + SegDir * T;
		const float Dist = FVector2D::Distance(Pos, Proj);
		if (Dist <= Seg.Thickness * 0.5f + 20.f && Dist < BestWallDist)
		{
			BestWallDist = Dist;
			TargetSegID = Pair.Key;
			SplitPoint = Proj;
		}
	}
	if (TargetSegID != -1)
	{
		const int32 JunctionID = SplitWallSegment(TargetSegID, SplitPoint);
		if (JunctionID == INDEX_NONE || !Nodes.Contains(NodeID)) return false;
		if (!MergeNodeInto(NodeID, JunctionID)) return false;
		RebuildAllWalls();
		RebuildRooms();
		CommitStateAfterMutation();
		UpdateSelectionVisuals();
		return true;
	}

	return false;
}

FVector2D ARoomPlannerManager::SnapNodeDragPosition(int32 NodeID, const FVector2D& RawPos) const
{
	const FWallNode* Node = Nodes.Find(NodeID);
	const TArray<int32> MySegs = Node ? Node->ConnectedSegmentIDs : TArray<int32>();

	auto IsNeighbourNode = [&](int32 OtherNodeID) -> bool
	{
		for (int32 SegID : MySegs)
		{
			const FWallSegment* Seg = WallSegments.Find(SegID);
			if (Seg && (Seg->StartNodeID == OtherNodeID || Seg->EndNodeID == OtherNodeID)) return true;
		}
		return false;
	};

	// 1. Magnet to another corner (not a direct neighbour, which would collapse a wall) — 25 cm, same as wall drawing.
	int32 BestNode = -1;
	float BestDistSq = 25.f * 25.f;
	for (const auto& Pair : Nodes)
	{
		if (Pair.Key == NodeID || IsNeighbourNode(Pair.Key)) continue;
		const float D = FVector2D::DistSquared(Pair.Value.Position, RawPos);
		if (D < BestDistSq) { BestDistSq = D; BestNode = Pair.Key; }
	}
	if (BestNode != -1)
	{
		return Nodes[BestNode].Position;
	}

	// 2. Magnet to the centreline of a wall that is not one of its own (T-junction target).
	float BestWallDist = TNumericLimits<float>::Max();
	FVector2D WallSnap = RawPos;
	bool bWallSnap = false;
	for (const auto& Pair : WallSegments)
	{
		if (MySegs.Contains(Pair.Key)) continue;
		const FWallSegment& Seg = Pair.Value;
		if (!Nodes.Contains(Seg.StartNodeID) || !Nodes.Contains(Seg.EndNodeID)) continue;
		const FVector2D P1 = Nodes[Seg.StartNodeID].Position;
		const FVector2D P2 = Nodes[Seg.EndNodeID].Position;
		const float SegLen = FVector2D::Distance(P1, P2);
		if (SegLen < 40.f) continue;
		const FVector2D SegDir = (P2 - P1) / SegLen;
		const float T = FVector2D::DotProduct(RawPos - P1, SegDir);
		if (T < 20.f || T > SegLen - 20.f) continue;
		const FVector2D Proj = P1 + SegDir * T;
		const float Dist = FVector2D::Distance(RawPos, Proj);
		if (Dist <= Seg.Thickness * 0.5f + 20.f && Dist < BestWallDist)
		{
			BestWallDist = Dist;
			WallSnap = Proj;
			bWallSnap = true;
		}
	}
	if (bWallSnap)
	{
		return WallSnap;
	}

	// 3. Axis alignment with any other node (keeps walls orthogonal while dragging a corner).
	const float SnapCm = 15.f;
	FVector2D Snapped = RawPos;
	float BestDX = SnapCm, BestDY = SnapCm;
	for (const auto& Pair : Nodes)
	{
		if (Pair.Key == NodeID) continue;
		const float DX = FMath::Abs(Pair.Value.Position.X - RawPos.X);
		const float DY = FMath::Abs(Pair.Value.Position.Y - RawPos.Y);
		if (DX < BestDX) { BestDX = DX; Snapped.X = Pair.Value.Position.X; }
		if (DY < BestDY) { BestDY = DY; Snapped.Y = Pair.Value.Position.Y; }
	}
	return Snapped;
}

int32 ARoomPlannerManager::FindNodeAtWorldPos(const FVector& WorldPos, float RadiusCm) const
{
	const FVector2D P(WorldPos.X, WorldPos.Y);
	int32 Best = -1;
	float BestDistSq = RadiusCm * RadiusCm;
	for (const auto& Pair : Nodes)
	{
		const float D = FVector2D::DistSquared(P, Pair.Value.Position);
		if (D <= BestDistSq)
		{
			BestDistSq = D;
			Best = Pair.Key;
		}
	}
	return Best;
}

bool ARoomPlannerManager::GetNodePosition(int32 NodeID, FVector2D& OutPosition) const
{
	if (const FWallNode* Node = Nodes.Find(NodeID))
	{
		OutPosition = Node->Position;
		return true;
	}
	return false;
}

TArray<FVector> ARoomPlannerManager::GetNodeHandleWorldPositions() const
{
	TArray<FVector> Out;
	for (const auto& Pair : Nodes)
	{
		Out.Add(FVector(Pair.Value.Position.X, Pair.Value.Position.Y, 0.f));
	}
	return Out;
}

bool ARoomPlannerManager::StartNodeDrag(int32 NodeID)
{
	const FWallNode* Node = Nodes.Find(NodeID);
	if (!Node) return false;
	DraggingNodeID = NodeID;
	NodeDragOriginalPos = Node->Position;
	RefreshNodeHandles();
	return true;
}

void ARoomPlannerManager::UpdateNodeDrag(const FVector& WorldPos)
{
	if (DraggingNodeID == -1 || !Nodes.Contains(DraggingNodeID)) return;

	const FVector2D Raw(WorldPos.X, WorldPos.Y);
	const FVector2D Target = SnapNodeDragPosition(DraggingNodeID, Raw);
	const bool bSnapped = !Target.Equals(Raw, 0.01f);

	if (!Target.Equals(Nodes[DraggingNodeID].Position, 0.01f))
	{
		ApplyNodeMove(DraggingNodeID, Target, true); // refused positions keep the last valid one (REQ-09)
	}

	const FVector2D Cur = Nodes[DraggingNodeID].Position;
	OnNodeDragProgress.Broadcast(DraggingNodeID, FVector(Cur.X, Cur.Y, 0.f));

	// Live wall dimension for the 3D scene label (REQ-02): prefer the selected wall if it touches this node.
	const FWallNode& Node = Nodes[DraggingNodeID];
	int32 PrimarySeg = -1;
	if (SelectedSegmentID != -1 && Node.ConnectedSegmentIDs.Contains(SelectedSegmentID))
	{
		PrimarySeg = SelectedSegmentID;
	}
	else if (Node.ConnectedSegmentIDs.Num() > 0)
	{
		PrimarySeg = Node.ConnectedSegmentIDs[0];
	}
	if (const FWallSegment* Seg = (PrimarySeg != -1) ? WallSegments.Find(PrimarySeg) : nullptr)
	{
		if (Nodes.Contains(Seg->StartNodeID) && Nodes.Contains(Seg->EndNodeID))
		{
			const FVector2D P1 = Nodes[Seg->StartNodeID].Position;
			const FVector2D P2 = Nodes[Seg->EndNodeID].Position;
			const float LenM = FVector2D::Distance(P1, P2) / 100.f;
			const FVector2D Mid = (P1 + P2) * 0.5f;
			const float AngleDeg = FMath::RadiansToDegrees(FMath::Atan2(P2.Y - P1.Y, P2.X - P1.X));
			OnInteractiveWallDragProgress.Broadcast(LenM, FVector(Mid.X, Mid.Y, Seg->Height), AngleDeg, bSnapped);
		}
	}
}

bool ARoomPlannerManager::EndNodeDrag(int32& OutNodeID, FVector2D& OutFinalPosition)
{
	if (DraggingNodeID == -1 || !Nodes.Contains(DraggingNodeID))
	{
		DraggingNodeID = -1;
		return false;
	}
	OutNodeID = DraggingNodeID;
	OutFinalPosition = Nodes[DraggingNodeID].Position;
	DraggingNodeID = -1;
	RefreshNodeHandles();
	return true;
}

void ARoomPlannerManager::RefreshNodeHandles()
{
	if (!NodeHandleMesh) return;

	NodeHandleMesh->ClearAllMeshSections();
	const bool bShow = b2DViewMode && ActiveToolMode == EPlannerToolMode::Select && Nodes.Num() > 0;
	NodeHandleMesh->SetVisibility(bShow);
	if (!bShow) return;

	int32 Section = 0;
	for (const auto& Pair : Nodes)
	{
		const bool bActive = (Pair.Key == DraggingNodeID);
		// Floor-level handle: drawn just above the floor slab and wider than the thickest wall at the node,
		// so a ring stays visible around the wall footprint from the top-down camera.
		float MaxHalfThickness = 10.f;
		for (int32 SegID : Pair.Value.ConnectedSegmentIDs)
		{
			if (const FWallSegment* Seg = WallSegments.Find(SegID)) MaxHalfThickness = FMath::Max(MaxHalfThickness, Seg->Thickness * 0.5f);
		}
		const float Radius = FMath::Max(bActive ? 16.f : 12.f, MaxHalfThickness + (bActive ? 10.f : 6.f));
		const float Z = 2.f;

		TArray<FVector> V; TArray<int32> T; TArray<FVector> N; TArray<FVector2D> UV;
		const FVector2D C = Pair.Value.Position;
		V.Add(FVector(C.X, C.Y, Z)); N.Add(FVector::UpVector); UV.Add(FVector2D(0.5f, 0.5f));
		const int32 Steps = 12;
		for (int32 i = 0; i < Steps; ++i)
		{
			const float A = 2.f * PI * (float)i / (float)Steps;
			V.Add(FVector(C.X + FMath::Cos(A) * Radius, C.Y + FMath::Sin(A) * Radius, Z));
			N.Add(FVector::UpVector);
			UV.Add(FVector2D(0.5f + 0.5f * FMath::Cos(A), 0.5f + 0.5f * FMath::Sin(A)));
		}
		for (int32 i = 0; i < Steps; ++i)
		{
			const int32 A = 1 + i;
			const int32 B = 1 + ((i + 1) % Steps);
			// Both windings so the disc is visible from above and below in every camera mode.
			T.Add(0); T.Add(B); T.Add(A);
			T.Add(0); T.Add(A); T.Add(B);
		}
		NodeHandleMesh->CreateMeshSection(Section, V, T, N, UV, TArray<FColor>(), TArray<FProcMeshTangent>(), false);
		UMaterialInterface* Mat = bActive ? WallSelectionMaterial.Get() : OpeningSelectionMaterial.Get();
		if (!Mat) Mat = WallSelectionMaterial.Get();
		if (Mat) NodeHandleMesh->SetMaterial(Section, Mat);
		++Section;
	}
}

void ARoomPlannerManager::ComputeWallInteriorSides(const TArray<TArray<FVector2D>>& RoomPolygons)
{
	bool bAnyChanged = false;

	TMap<int32, bool> NewFlags;
	for (const auto& Pair : WallSegments)
	{
		NewFlags.Add(Pair.Key, true);
	}
	for (auto& RoomPair : Rooms)
	{
		RoomPair.Value.WallSegmentIDs.Reset();
	}

	auto NearlySame = [](const FVector2D& A, const FVector2D& B) { return FVector2D::DistSquared(A, B) < 4.f; };

	for (int32 PolyIdx = 0; PolyIdx < RoomPolygons.Num(); ++PolyIdx)
	{
		const TArray<FVector2D>& Poly = RoomPolygons[PolyIdx];
		const int32 N = Poly.Num();
		FRoomData* Room = Rooms.Find(PolyIdx + 1);
		for (int32 i = 0; i < N; ++i)
		{
			const FVector2D& Pi = Poly[i];
			const FVector2D& Pj = Poly[(i + 1) % N];
			for (const auto& Pair : WallSegments)
			{
				const FWallSegment& Seg = Pair.Value;
				const FWallNode* S = Nodes.Find(Seg.StartNodeID);
				const FWallNode* E = Nodes.Find(Seg.EndNodeID);
				if (!S || !E) continue;
				if (NearlySame(S->Position, Pi) && NearlySame(E->Position, Pj))
				{
					NewFlags[Pair.Key] = true;   // polygon is CCW: interior on the left of Start->End
					if (Room) Room->WallSegmentIDs.AddUnique(Pair.Key);
				}
				else if (NearlySame(S->Position, Pj) && NearlySame(E->Position, Pi))
				{
					NewFlags[Pair.Key] = false;  // traversed End->Start: interior on the right
					if (Room) Room->WallSegmentIDs.AddUnique(Pair.Key);
				}
			}
		}
	}

	for (auto& Pair : WallSegments)
	{
		const bool NewVal = NewFlags.FindRef(Pair.Key);
		if (Pair.Value.bLeftSideIsInterior != NewVal)
		{
			Pair.Value.bLeftSideIsInterior = NewVal;
			bAnyChanged = true;
		}
	}

	if (bAnyChanged)
	{
		RebuildAllWalls(); // leaves depend on the interior side
	}
}

bool ARoomPlannerManager::IsWallLeftSideInterior(int32 SegmentID) const
{
	if (const FWallSegment* Seg = WallSegments.Find(SegmentID))
	{
		return Seg->bLeftSideIsInterior;
	}
	return true;
}

bool ARoomPlannerManager::GetWallSegmentData(int32 SegmentID, FWallSegment& OutSegment) const
{
	if (const FWallSegment* Seg = WallSegments.Find(SegmentID))
	{
		OutSegment = *Seg;
		return true;
	}
	return false;
}

bool ARoomPlannerManager::GetRoomData(int32 RoomID, FRoomData& OutRoom) const
{
	if (const FRoomData* Room = Rooms.Find(RoomID))
	{
		OutRoom = *Room;
		return true;
	}
	return false;
}

// ═══════════════════════════════════════════════════════════════════════════════
// REQ-07: opening swing
// ═══════════════════════════════════════════════════════════════════════════════

bool ARoomPlannerManager::SetOpeningSwing(int32 SegmentID, int32 OpeningIndex, EOpeningSwingSide Side, EOpeningSwingDirection Direction)
{
	FWallSegment* Seg = WallSegments.Find(SegmentID);
	if (!Seg || !Seg->Openings.IsValidIndex(OpeningIndex)) return false;

	Seg->Openings[OpeningIndex].SwingSide = Side;
	Seg->Openings[OpeningIndex].SwingDirection = Direction;

	RebuildAllWalls();
	CommitStateAfterMutation();
	UpdateSelectionVisuals();
	return true;
}

bool ARoomPlannerManager::GetOpeningSwing(int32 SegmentID, int32 OpeningIndex, EOpeningSwingSide& OutSide, EOpeningSwingDirection& OutDirection) const
{
	const FWallSegment* Seg = WallSegments.Find(SegmentID);
	if (!Seg || !Seg->Openings.IsValidIndex(OpeningIndex)) return false;
	OutSide = Seg->Openings[OpeningIndex].SwingSide;
	OutDirection = Seg->Openings[OpeningIndex].SwingDirection;
	return true;
}

// ═══════════════════════════════════════════════════════════════════════════════
// REQ-06: exact distances
// ═══════════════════════════════════════════════════════════════════════════════

bool ARoomPlannerManager::GetOpeningDistances(int32 SegmentID, int32 OpeningIndex, float& OutLeftCornerCm, float& OutRightCornerCm, float& OutFloorCm, float& OutNeighborCm, bool& bOutHasNeighbor) const
{
	OutLeftCornerCm = OutRightCornerCm = OutFloorCm = OutNeighborCm = 0.f;
	bOutHasNeighbor = false;

	const FWallSegment* Seg = WallSegments.Find(SegmentID);
	if (!Seg || !Seg->Openings.IsValidIndex(OpeningIndex)) return false;
	if (!Nodes.Contains(Seg->StartNodeID) || !Nodes.Contains(Seg->EndNodeID)) return false;

	const float WallLen = FVector2D::Distance(Nodes[Seg->StartNodeID].Position, Nodes[Seg->EndNodeID].Position);
	const FWallOpening& Op = Seg->Openings[OpeningIndex];
	const float Start = Op.DistanceFromStart - Op.Width * 0.5f;
	const float End = Op.DistanceFromStart + Op.Width * 0.5f;

	const float ToStartCorner = FMath::Max(0.f, Start);
	const float ToEndCorner = FMath::Max(0.f, WallLen - End);

	// Seen from inside the room: with the interior on the wall's LEFT face, the observer's
	// right hand points to the START node (see AProceduralWallActor::IsHingeAtStart).
	if (Seg->bLeftSideIsInterior)
	{
		OutRightCornerCm = ToStartCorner;
		OutLeftCornerCm = ToEndCorner;
	}
	else
	{
		OutLeftCornerCm = ToStartCorner;
		OutRightCornerCm = ToEndCorner;
	}
	OutFloorCm = FMath::Max(0.f, Op.SillHeight);

	float Best = TNumericLimits<float>::Max();
	for (int32 i = 0; i < Seg->Openings.Num(); ++i)
	{
		if (i == OpeningIndex) continue;
		const FWallOpening& Other = Seg->Openings[i];
		const float OStart = Other.DistanceFromStart - Other.Width * 0.5f;
		const float OEnd = Other.DistanceFromStart + Other.Width * 0.5f;
		float Gap;
		if (OStart >= End) Gap = OStart - End;
		else if (OEnd <= Start) Gap = Start - OEnd;
		else Gap = 0.f; // overlapping
		if (Gap < Best) Best = Gap;
	}
	if (Best < TNumericLimits<float>::Max())
	{
		bOutHasNeighbor = true;
		OutNeighborCm = Best;
	}
	return true;
}

// ═══════════════════════════════════════════════════════════════════════════════
// REQ-02 / 04 / 06: dimension labels
// ═══════════════════════════════════════════════════════════════════════════════

TArray<FPlannerDimensionLabel> ARoomPlannerManager::GetSelectionDimensionLabels() const
{
	TArray<FPlannerDimensionLabel> Labels;

	auto AddLabel = [&Labels](const FString& Key, const FString& Text, float Value, const FVector& World)
	{
		FPlannerDimensionLabel L;
		L.Key = Key; L.Text = Text; L.Value = Value; L.WorldLocation = World;
		Labels.Add(L);
	};

	auto WallLengthLabel = [&](int32 SegID)
	{
		const FWallSegment* Seg = WallSegments.Find(SegID);
		if (!Seg || !Nodes.Contains(Seg->StartNodeID) || !Nodes.Contains(Seg->EndNodeID)) return;
		const FVector2D P1 = Nodes[Seg->StartNodeID].Position;
		const FVector2D P2 = Nodes[Seg->EndNodeID].Position;
		const float LenCm = FVector2D::Distance(P1, P2);
		const FVector2D Mid = (P1 + P2) * 0.5f;
		AddLabel(TEXT("length"), PlannerJsonKeys::FormatMeters(LenCm), LenCm, FVector(Mid.X, Mid.Y, Seg->Height + 15.f));
	};

	// While a control point is dragged: live length of every wall attached to it (REQ-02).
	if (DraggingNodeID != -1)
	{
		if (const FWallNode* Node = Nodes.Find(DraggingNodeID))
		{
			for (int32 SegID : Node->ConnectedSegmentIDs)
			{
				WallLengthLabel(SegID);
			}
		}
		return Labels;
	}

	switch (GetSelectionKind())
	{
	case EPlannerSelectionKind::Wall:
	{
		WallLengthLabel(SelectedSegmentID);
		if (const FWallSegment* Seg = WallSegments.Find(SelectedSegmentID))
		{
			if (Nodes.Contains(Seg->EndNodeID))
			{
				const FVector2D P = Nodes[Seg->EndNodeID].Position;
				AddLabel(TEXT("height"), PlannerJsonKeys::FormatMeters(Seg->Height), Seg->Height, FVector(P.X, P.Y, Seg->Height * 0.5f));
			}
		}
		break;
	}
	case EPlannerSelectionKind::Opening:
	{
		const FWallSegment* Seg = WallSegments.Find(SelectedSegmentID);
		if (!Seg || !Seg->Openings.IsValidIndex(SelectedOpeningIndex)) break;
		if (!Nodes.Contains(Seg->StartNodeID) || !Nodes.Contains(Seg->EndNodeID)) break;

		const FWallOpening& Op = Seg->Openings[SelectedOpeningIndex];
		const FVector2D P1 = Nodes[Seg->StartNodeID].Position;
		const FVector2D P2 = Nodes[Seg->EndNodeID].Position;
		const float WallLen = FVector2D::Distance(P1, P2);
		const FVector2D Dir = (P2 - P1).GetSafeNormal();
		const FVector2D Normal(-Dir.Y, Dir.X);
		const FVector2D Interior = Seg->bLeftSideIsInterior ? Normal : -Normal;
		const FVector2D Off = Interior * (Seg->Thickness * 0.5f + 5.f);

		auto AlongWall = [&](float DistCm, float Z) { const FVector2D P = P1 + Dir * DistCm + Off; return FVector(P.X, P.Y, Z); };

		const float Start = Op.DistanceFromStart - Op.Width * 0.5f;
		const float End = Op.DistanceFromStart + Op.Width * 0.5f;
		const float TopZ = Op.SillHeight + Op.Height;

		AddLabel(TEXT("width"), PlannerJsonKeys::FormatMeters(Op.Width), Op.Width, AlongWall(Op.DistanceFromStart, TopZ + 12.f));
		AddLabel(TEXT("height"), PlannerJsonKeys::FormatMeters(Op.Height), Op.Height, AlongWall(End + 8.f, Op.SillHeight + Op.Height * 0.5f));
		if (Op.SillHeight > 0.5f)
		{
			AddLabel(TEXT("sill"), PlannerJsonKeys::FormatMeters(Op.SillHeight), Op.SillHeight, AlongWall(Start - 8.f, Op.SillHeight * 0.5f));
		}

		float L, R, Fl, Nb; bool bHasNb;
		if (GetOpeningDistances(SelectedSegmentID, SelectedOpeningIndex, L, R, Fl, Nb, bHasNb))
		{
			// Physical positions along the wall for each gap (start-corner gap and end-corner gap).
			const float ToStart = FMath::Max(0.f, Start);
			const float ToEnd = FMath::Max(0.f, WallLen - End);
			const FVector StartGapPos = AlongWall(Start * 0.5f, 25.f);
			const FVector EndGapPos = AlongWall(End + ToEnd * 0.5f, 25.f);
			if (Seg->bLeftSideIsInterior)
			{
				AddLabel(TEXT("distRight"), PlannerJsonKeys::FormatMeters(ToStart), ToStart, StartGapPos);
				AddLabel(TEXT("distLeft"), PlannerJsonKeys::FormatMeters(ToEnd), ToEnd, EndGapPos);
			}
			else
			{
				AddLabel(TEXT("distLeft"), PlannerJsonKeys::FormatMeters(ToStart), ToStart, StartGapPos);
				AddLabel(TEXT("distRight"), PlannerJsonKeys::FormatMeters(ToEnd), ToEnd, EndGapPos);
			}
			AddLabel(TEXT("distFloor"), PlannerJsonKeys::FormatMeters(Fl), Fl, AlongWall(Op.DistanceFromStart, FMath::Max(8.f, Fl * 0.5f)));
			if (bHasNb)
			{
				// Place at the middle of the gap to the nearest neighbour.
				float GapCenter = Op.DistanceFromStart;
				float BestGap = TNumericLimits<float>::Max();
				for (int32 i = 0; i < Seg->Openings.Num(); ++i)
				{
					if (i == SelectedOpeningIndex) continue;
					const FWallOpening& Other = Seg->Openings[i];
					const float OStart = Other.DistanceFromStart - Other.Width * 0.5f;
					const float OEnd = Other.DistanceFromStart + Other.Width * 0.5f;
					if (OStart >= End && OStart - End < BestGap) { BestGap = OStart - End; GapCenter = (End + OStart) * 0.5f; }
					else if (OEnd <= Start && Start - OEnd < BestGap) { BestGap = Start - OEnd; GapCenter = (OEnd + Start) * 0.5f; }
				}
				AddLabel(TEXT("distNeighbor"), PlannerJsonKeys::FormatMeters(Nb), Nb, AlongWall(GapCenter, TopZ * 0.5f));
			}
		}
		break;
	}
	case EPlannerSelectionKind::Floor:
	{
		if (const FRoomData* Room = Rooms.Find(SelectedRoomID))
		{
			AddLabel(TEXT("area"), FString::Printf(TEXT("%.2f м²"), Room->AreaM2), Room->AreaM2, FVector(Room->Centroid.X, Room->Centroid.Y, 5.f));
		}
		break;
	}
	case EPlannerSelectionKind::Object:
	{
		if (const FPlacedFurnitureData* D = PlacedObjects.Find(SelectedObjectID))
		{
			FVector Half(50.f, 50.f, 50.f);
			if (APlannerPlacedObjectActor* A = FindPlacedObjectActor(SelectedObjectID)) Half = A->GetLocalHalfExtents();
			const FString SizeText = FString::Printf(TEXT("%.2f × %.2f × %.2f м"), Half.X * 2.f / 100.f, Half.Y * 2.f / 100.f, Half.Z * 2.f / 100.f);
			AddLabel(TEXT("size"), SizeText, Half.X * 2.f, D->Location + FVector(0.f, 0.f, Half.Z * 2.f + 10.f));
		}
		break;
	}
	case EPlannerSelectionKind::CabinetSet:
	{
		if (const FPlacedCabinetSetData* D = CabinetSets.Find(SelectedCabinetSetID))
		{
			FVector Origin = D->Location, Extent(50.f, 50.f, 50.f);
			if (AShowroomBooth* Booth = FindCabinetSetActor(SelectedCabinetSetID)) Booth->GetActorBounds(false, Origin, Extent);
			const FString SizeText = FString::Printf(TEXT("%s  %.2f × %.2f м"), *D->ProductID.ToString(), Extent.X * 2.f / 100.f, Extent.Y * 2.f / 100.f);
			AddLabel(TEXT("size"), SizeText, Extent.X * 2.f, Origin + FVector(0.f, 0.f, Extent.Z + 10.f));
		}
		break;
	}
	default:
		break;
	}

	return Labels;
}

// ═══════════════════════════════════════════════════════════════════════════════
// Selection
// ═══════════════════════════════════════════════════════════════════════════════

EPlannerSelectionKind ARoomPlannerManager::GetSelectionKind() const
{
	if (SelectedSegmentID != -1)
	{
		return (SelectedOpeningIndex != -1) ? EPlannerSelectionKind::Opening : EPlannerSelectionKind::Wall;
	}
	if (SelectedRoomID != -1) return EPlannerSelectionKind::Floor;
	if (!SelectedObjectID.IsEmpty()) return EPlannerSelectionKind::Object;
	if (!SelectedCabinetSetID.IsEmpty()) return EPlannerSelectionKind::CabinetSet;
	return EPlannerSelectionKind::None;
}

void ARoomPlannerManager::ClearAllSelection()
{
	const bool bHadFloor = (SelectedRoomID != -1);
	const bool bHadObject = !SelectedObjectID.IsEmpty() || !SelectedCabinetSetID.IsEmpty();

	SelectedRoomID = -1;
	SelectedObjectID.Empty();
	SelectedCabinetSetID.Empty();

	ClearWallSelection(); // broadcasts OnWallSelected(-1) + OnSelectionChanged
	UpdateSelectionVisuals();

	if (bHadFloor) OnFloorSelected.Broadcast(-1, 0.f);
	if (bHadObject) OnPlannerObjectSelected.Broadcast(FString(), EPlannerSelectionKind::None);
}

int32 ARoomPlannerManager::FindRoomAtWorldPos(const FVector& WorldPos) const
{
	const FVector2D P(WorldPos.X, WorldPos.Y);
	for (const auto& Pair : Rooms)
	{
		const TArray<FVector2D>& Poly = Pair.Value.FloorPolygon;
		const int32 N = Poly.Num();
		if (N < 3) continue;
		bool bInside = false;
		for (int32 i = 0, j = N - 1; i < N; j = i++)
		{
			const FVector2D& A = Poly[i];
			const FVector2D& B = Poly[j];
			if (((A.Y > P.Y) != (B.Y > P.Y)) && (P.X < (B.X - A.X) * (P.Y - A.Y) / (B.Y - A.Y) + A.X))
			{
				bInside = !bInside;
			}
		}
		if (bInside) return Pair.Key;
	}
	return -1;
}

int32 ARoomPlannerManager::SelectFloorAtWorldPos(const FVector& WorldPos)
{
	const int32 RoomID = FindRoomAtWorldPos(WorldPos);

	SelectedSegmentID = -1;
	SelectedOpeningIndex = -1;
	SelectedObjectID.Empty();
	SelectedCabinetSetID.Empty();
	SelectedRoomID = RoomID;

	UpdateSelectionVisuals();
	OnWallSelected.Broadcast(-1, 0.f);
	OnFloorSelected.Broadcast(RoomID, (RoomID != -1 && Rooms.Contains(RoomID)) ? Rooms[RoomID].AreaM2 : 0.f);
	NotifySelectionChanged();
	return RoomID;
}

bool ARoomPlannerManager::SelectPlacedObject(const FString& InstanceID)
{
	if (!PlacedObjects.Contains(InstanceID)) return false;
	SelectedSegmentID = -1;
	SelectedOpeningIndex = -1;
	SelectedRoomID = -1;
	SelectedCabinetSetID.Empty();
	SelectedObjectID = InstanceID;
	UpdateSelectionVisuals();
	OnWallSelected.Broadcast(-1, 0.f);
	OnPlannerObjectSelected.Broadcast(InstanceID, EPlannerSelectionKind::Object);
	NotifySelectionChanged();
	return true;
}

bool ARoomPlannerManager::SelectCabinetSet(const FString& InstanceID)
{
	if (!CabinetSets.Contains(InstanceID)) return false;
	SelectedSegmentID = -1;
	SelectedOpeningIndex = -1;
	SelectedRoomID = -1;
	SelectedObjectID.Empty();
	SelectedCabinetSetID = InstanceID;
	UpdateSelectionVisuals();
	OnWallSelected.Broadcast(-1, 0.f);
	OnPlannerObjectSelected.Broadcast(InstanceID, EPlannerSelectionKind::CabinetSet);
	NotifySelectionChanged();
	return true;
}

EPlannerSelectionKind ARoomPlannerManager::SelectAtWorldPos2D(const FVector& WorldPos)
{
	if (ActiveToolMode != EPlannerToolMode::Select)
	{
		ClearAllSelection();
		return EPlannerSelectionKind::None;
	}

	const FString ObjID = FindPlacedObjectAtWorldPos(WorldPos);
	if (!ObjID.IsEmpty() && SelectPlacedObject(ObjID))
	{
		return EPlannerSelectionKind::Object;
	}

	const FString SetID = FindCabinetSetAtWorldPos(WorldPos);
	if (!SetID.IsEmpty() && SelectCabinetSet(SetID))
	{
		return EPlannerSelectionKind::CabinetSet;
	}

	if (SelectWallAtWorldPos(WorldPos) != -1)
	{
		return (SelectedOpeningIndex != -1) ? EPlannerSelectionKind::Opening : EPlannerSelectionKind::Wall;
	}

	if (SelectFloorAtWorldPos(WorldPos) != -1)
	{
		return EPlannerSelectionKind::Floor;
	}

	ClearAllSelection();
	return EPlannerSelectionKind::None;
}

EPlannerSelectionKind ARoomPlannerManager::SelectSurfaceFromHit(const FHitResult& Hit)
{
	AActor* HitActor = Hit.GetActor();
	UPrimitiveComponent* HitComp = Hit.GetComponent();

	if (AProceduralWallActor* Wall = Cast<AProceduralWallActor>(HitActor))
	{
		int32 SegID = -1;
		for (const auto& Pair : WallActors)
		{
			if (Pair.Value == Wall) { SegID = Pair.Key; break; }
		}
		const FWallSegment* Seg = (SegID != -1) ? WallSegments.Find(SegID) : nullptr;
		if (Seg && Nodes.Contains(Seg->StartNodeID) && Nodes.Contains(Seg->EndNodeID))
		{
			const FVector2D P1 = Nodes[Seg->StartNodeID].Position;
			const FVector2D P2 = Nodes[Seg->EndNodeID].Position;
			const FVector2D Dir = (P2 - P1).GetSafeNormal();
			const float Along = FVector2D::DotProduct(FVector2D(Hit.ImpactPoint.X, Hit.ImpactPoint.Y) - P1, Dir);
			const float Z = Hit.ImpactPoint.Z;

			int32 OpIdx = -1;
			for (int32 i = 0; i < Seg->Openings.Num(); ++i)
			{
				const FWallOpening& Op = Seg->Openings[i];
				if (FMath::Abs(Along - Op.DistanceFromStart) <= Op.Width * 0.5f + 6.f && Z >= Op.SillHeight - 6.f && Z <= Op.SillHeight + Op.Height + 6.f)
				{
					OpIdx = i;
					break;
				}
			}

			SelectedRoomID = -1;
			SelectedObjectID.Empty();
			SelectedCabinetSetID.Empty();
			SelectedSegmentID = SegID;
			SelectedOpeningIndex = OpIdx;
			UpdateSelectionVisuals();
			OnWallSelected.Broadcast(SegID, GetWallLength(SegID));
			NotifySelectionChanged();
			return (OpIdx != -1) ? EPlannerSelectionKind::Opening : EPlannerSelectionKind::Wall;
		}
	}

	if (HitComp && FloorProceduralMesh && HitComp == static_cast<UPrimitiveComponent*>(FloorProceduralMesh.Get()))
	{
		if (SelectFloorAtWorldPos(Hit.ImpactPoint) != -1)
		{
			return EPlannerSelectionKind::Floor;
		}
	}

	if (APlannerPlacedObjectActor* Obj = Cast<APlannerPlacedObjectActor>(HitActor))
	{
		if (SelectPlacedObject(Obj->Data.InstanceID))
		{
			return EPlannerSelectionKind::Object;
		}
	}

	if (AShowroomBooth* Booth = Cast<AShowroomBooth>(HitActor))
	{
		if (!Booth->PlannerInstanceID.IsEmpty() && SelectCabinetSet(Booth->PlannerInstanceID))
		{
			return EPlannerSelectionKind::CabinetSet;
		}
	}

	ClearAllSelection();
	return EPlannerSelectionKind::None;
}

// ═══════════════════════════════════════════════════════════════════════════════
// REQ-13: finishing
// ═══════════════════════════════════════════════════════════════════════════════

UMaterialInterface* ARoomPlannerManager::ResolvePaintBaseMaterial()
{
	if (PaintMaterial) return PaintMaterial;
	if (CachedPaintBaseMaterial) return CachedPaintBaseMaterial;

	CachedPaintBaseMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/RoomPlanner/Materials/M_PlannerPaint.M_PlannerPaint"));
	if (!CachedPaintBaseMaterial)
	{
		CachedPaintBaseMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/ColorCatalog/Materials/M_Change_Color.M_Change_Color"));
	}
	if (!CachedPaintBaseMaterial)
	{
		CachedPaintBaseMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	}
	if (!CachedPaintBaseMaterial)
	{
		CachedPaintBaseMaterial = UMaterial::GetDefaultMaterial(MD_Surface);
	}
	return CachedPaintBaseMaterial;
}

UMaterialInterface* ARoomPlannerManager::ResolveTileBaseMaterial(const FSurfaceFinish& Finish)
{
	// 1. Tile catalog row material
	if (UDataTable* Catalog = ResolveTileCatalog())
	{
		if (!Finish.TileAssetID.IsEmpty())
		{
			if (const FPlannerTileRow* Row = Catalog->FindRow<FPlannerTileRow>(FName(*Finish.TileAssetID), TEXT("ResolveTileBaseMaterial"), false))
			{
				if (UMaterialInterface* RowMat = Row->Material.LoadSynchronous())
				{
					return RowMat;
				}
			}
		}
	}
	// 2. Asset path stored directly in TileAssetID
	if (Finish.TileAssetID.StartsWith(TEXT("/")))
	{
		if (UMaterialInterface* PathMat = LoadObject<UMaterialInterface>(nullptr, *Finish.TileAssetID))
		{
			return PathMat;
		}
	}
	// 3. Generic tile material
	if (TileMaterial) return TileMaterial;
	if (!CachedTileBaseMaterial)
	{
		CachedTileBaseMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/RoomPlanner/Materials/M_PlannerTile.M_PlannerTile"));
	}
	if (CachedTileBaseMaterial) return CachedTileBaseMaterial;
	// 4. Paint fallback (tinted with the tile's average colour)
	return ResolvePaintBaseMaterial();
}

UMaterialInstanceDynamic* ARoomPlannerManager::CreateFinishMaterialInstance(const FSurfaceFinish& Finish, UObject* Outer)
{
	UMaterialInterface* Base = (Finish.Type == ESurfaceFinishType::Tile) ? ResolveTileBaseMaterial(Finish) : ResolvePaintBaseMaterial();
	if (!Base) return nullptr;

	UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(Base, Outer ? Outer : this);
	if (!MID) return nullptr;

	MID->SetVectorParameterValue(FName("BaseColor"), Finish.Color);
	MID->SetVectorParameterValue(FName("Color"), Finish.Color);
	if (Finish.Type == ESurfaceFinishType::Tile)
	{
		const float TileSize = FMath::Max(1.f, Finish.TileSizeCm);
		MID->SetScalarParameterValue(FName("TilesPerMeter"), 100.f / TileSize); // UVs are metric (1 UV = 1 m)
		MID->SetScalarParameterValue(FName("TileSize"), TileSize / 100.f);
	}
	return MID;
}

const FFloorFinishRecord* ARoomPlannerManager::FindFloorFinishRecord(const FVector2D& Centroid) const
{
	const FFloorFinishRecord* Best = nullptr;
	float BestDist = 100.f * 100.f; // 1 m association radius
	for (const FFloorFinishRecord& Rec : FloorFinishes)
	{
		const float D = FVector2D::DistSquared(Rec.Anchor, Centroid);
		if (D < BestDist)
		{
			BestDist = D;
			Best = &Rec;
		}
	}
	return Best;
}

UMaterialInterface* ARoomPlannerManager::ResolveFloorMaterialForRoom(const FRoomData& Room)
{
	UMaterialInterface* Mat = nullptr;
	if (Room.FloorFinish.IsSet())
	{
		Mat = CreateFinishMaterialInstance(Room.FloorFinish, this);
	}
	if (!Mat)
	{
		if (UMaterialInterface* Base = ResolvePaintBaseMaterial())
		{
			if (UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(Base, this))
			{
				MID->SetVectorParameterValue(FName("BaseColor"), FLinearColor(0.92f, 0.92f, 0.92f, 1.f));
				MID->SetVectorParameterValue(FName("Color"), FLinearColor(0.92f, 0.92f, 0.92f, 1.f));
				Mat = MID;
			}
		}
	}
	FloorSectionMaterials.Add(Room.RoomID, Mat);
	return Mat;
}

void ARoomPlannerManager::ApplyWallFinishMaterials()
{
	for (auto& Pair : WallActors)
	{
		AProceduralWallActor* Actor = Pair.Value;
		const FWallSegment* Seg = WallSegments.Find(Pair.Key);
		if (!Actor || !Seg) continue;
		if (Seg->Finish != Actor->AppliedFinish || (Seg->Finish.IsSet() && !Actor->FinishMaterial))
		{
			Actor->SetFinishMaterial(Seg->Finish.IsSet() ? CreateFinishMaterialInstance(Seg->Finish, Actor) : nullptr);
			Actor->AppliedFinish = Seg->Finish;
		}
	}
}

bool ARoomPlannerManager::SetWallFinish(int32 SegmentID, const FSurfaceFinish& Finish)
{
	FWallSegment* Seg = WallSegments.Find(SegmentID);
	if (!Seg) return false;
	Seg->Finish = Finish;
	ApplyWallFinishMaterials();
	CommitStateAfterMutation();
	UpdateSelectionVisuals();
	return true;
}

bool ARoomPlannerManager::SetFloorFinish(int32 RoomID, const FSurfaceFinish& Finish)
{
	FRoomData* Room = Rooms.Find(RoomID);
	if (!Room) return false;

	// Upsert the centroid-keyed record
	bool bFound = false;
	for (FFloorFinishRecord& Rec : FloorFinishes)
	{
		if (FVector2D::DistSquared(Rec.Anchor, Room->Centroid) < 100.f * 100.f)
		{
			Rec.Anchor = Room->Centroid;
			Rec.Finish = Finish;
			bFound = true;
			break;
		}
	}
	if (!bFound && Finish.IsSet())
	{
		FFloorFinishRecord Rec;
		Rec.Anchor = Room->Centroid;
		Rec.Finish = Finish;
		FloorFinishes.Add(Rec);
	}
	if (!Finish.IsSet())
	{
		FloorFinishes.RemoveAll([Room](const FFloorFinishRecord& R) { return FVector2D::DistSquared(R.Anchor, Room->Centroid) < 100.f * 100.f; });
	}

	Room->FloorFinish = Finish;
	ResolveFloorMaterialForRoom(*Room);
	CommitStateAfterMutation();
	UpdateSelectionVisuals();
	return true;
}

bool ARoomPlannerManager::GetWallFinish(int32 SegmentID, FSurfaceFinish& OutFinish) const
{
	if (const FWallSegment* Seg = WallSegments.Find(SegmentID))
	{
		OutFinish = Seg->Finish;
		return true;
	}
	return false;
}

bool ARoomPlannerManager::GetFloorFinish(int32 RoomID, FSurfaceFinish& OutFinish) const
{
	if (const FRoomData* Room = Rooms.Find(RoomID))
	{
		OutFinish = Room->FloorFinish;
		return true;
	}
	return false;
}

bool ARoomPlannerManager::GetSelectedSurfaceFinish(FSurfaceFinish& OutFinish) const
{
	switch (GetSelectionKind())
	{
	case EPlannerSelectionKind::Wall:   return GetWallFinish(SelectedSegmentID, OutFinish);
	case EPlannerSelectionKind::Floor:  return GetFloorFinish(SelectedRoomID, OutFinish);
	case EPlannerSelectionKind::Object:
		if (const FPlacedFurnitureData* D = PlacedObjects.Find(SelectedObjectID)) { OutFinish = D->Finish; return true; }
		return false;
	default: return false;
	}
}

bool ARoomPlannerManager::CanApplyFinishToSelection() const
{
	const EPlannerSelectionKind Kind = GetSelectionKind();
	return Kind == EPlannerSelectionKind::Wall || Kind == EPlannerSelectionKind::Floor || Kind == EPlannerSelectionKind::Object;
}

FSurfaceFinish ARoomPlannerManager::MakePaintFinish(const FString& ColorCode, FLinearColor Color)
{
	FSurfaceFinish F;
	F.Type = ESurfaceFinishType::Paint;
	F.ColorCode = ColorCode;
	F.Color = Color;
	return F;
}

UDataTable* ARoomPlannerManager::ResolveTileCatalog() const
{
	if (TileCatalog) return TileCatalog;
	return LoadObject<UDataTable>(nullptr, TEXT("/Game/DT/DT_PlannerTiles.DT_PlannerTiles"));
}

UDataTable* ARoomPlannerManager::ResolveObjectCatalog() const
{
	if (ObjectCatalog) return ObjectCatalog;
	return LoadObject<UDataTable>(nullptr, TEXT("/Game/DT/DT_PlannerObjects.DT_PlannerObjects"));
}

UDataTable* ARoomPlannerManager::ResolveCabinetSetCatalog() const
{
	if (CabinetSetCatalog) return CabinetSetCatalog;
	return LoadObject<UDataTable>(nullptr, TEXT("/Game/DT/DT_FurnitureCatalog.DT_FurnitureCatalog"));
}

UClass* ARoomPlannerManager::ResolveCabinetSetActorClass() const
{
	if (CabinetSetActorClass) return CabinetSetActorClass;
	if (UClass* BP = LoadClass<AShowroomBooth>(nullptr, TEXT("/Game/BP_Booth.BP_Booth_C")))
	{
		return BP;
	}
	return AShowroomBooth::StaticClass();
}

bool ARoomPlannerManager::MakeTileFinish(FName TileID, FSurfaceFinish& OutFinish) const
{
	UDataTable* Catalog = ResolveTileCatalog();
	const FPlannerTileRow* Row = Catalog ? Catalog->FindRow<FPlannerTileRow>(TileID, TEXT("MakeTileFinish"), false) : nullptr;
	if (!Row)
	{
		// Allow a raw material path as tile id (no catalog needed).
		const FString IdStr = TileID.ToString();
		if (!IdStr.StartsWith(TEXT("/"))) return false;
		OutFinish = FSurfaceFinish();
		OutFinish.Type = ESurfaceFinishType::Tile;
		OutFinish.TileAssetID = IdStr;
		OutFinish.TileSizeCm = 30.f;
		OutFinish.Color = FLinearColor::White;
		return true;
	}
	OutFinish = FSurfaceFinish();
	OutFinish.Type = ESurfaceFinishType::Tile;
	OutFinish.TileAssetID = TileID.ToString();
	OutFinish.TileSizeCm = Row->TileSizeCm > 0.f ? Row->TileSizeCm : 30.f;
	OutFinish.Color = Row->AverageColor;
	return true;
}

TArray<FPlannerCatalogEntry> ARoomPlannerManager::GetAvailableTiles() const
{
	TArray<FPlannerCatalogEntry> Out;
	UDataTable* Catalog = ResolveTileCatalog();
	if (!Catalog) return Out;
	for (const auto& Pair : Catalog->GetRowMap())
	{
		const FPlannerTileRow* Row = reinterpret_cast<const FPlannerTileRow*>(Pair.Value);
		if (!Row) continue;
		FPlannerCatalogEntry E;
		E.ID = Pair.Key.ToString();
		E.DisplayName = Row->DisplayName.IsEmpty() ? FText::FromName(Pair.Key) : Row->DisplayName;
		E.Thumbnail = Row->Thumbnail;
		E.TileSizeCm = Row->TileSizeCm;
		E.Color = Row->AverageColor;
		Out.Add(E);
	}
	return Out;
}

// ═══════════════════════════════════════════════════════════════════════════════
// REQ-14: finishing areas
// ═══════════════════════════════════════════════════════════════════════════════

float ARoomPlannerManager::GetWallNetAreaM2(int32 SegmentID) const
{
	const FWallSegment* Seg = WallSegments.Find(SegmentID);
	if (!Seg || !Nodes.Contains(Seg->StartNodeID) || !Nodes.Contains(Seg->EndNodeID)) return 0.f;
	const float Len = FVector2D::Distance(Nodes[Seg->StartNodeID].Position, Nodes[Seg->EndNodeID].Position);
	float AreaCm2 = Len * Seg->Height;
	for (const FWallOpening& Op : Seg->Openings)
	{
		AreaCm2 -= Op.Width * Op.Height;
	}
	return FMath::Max(0.f, AreaCm2) / 10000.f;
}

TArray<FFinishAreaEntry> ARoomPlannerManager::CalculateFinishAreas() const
{
	TArray<FFinishAreaEntry> Out;
	auto Accumulate = [&Out](const FSurfaceFinish& F, float AreaM2)
	{
		if (!F.IsSet() || AreaM2 <= 0.f) return;
		const FString Key = F.GetKey();
		for (FFinishAreaEntry& E : Out)
		{
			if (E.Type == F.Type && E.FinishKey == Key)
			{
				E.AreaM2 += AreaM2;
				E.SurfaceCount++;
				return;
			}
		}
		FFinishAreaEntry NewE;
		NewE.Type = F.Type;
		NewE.FinishKey = Key;
		NewE.AreaM2 = AreaM2;
		NewE.SurfaceCount = 1;
		Out.Add(NewE);
	};

	for (const auto& Pair : WallSegments)
	{
		Accumulate(Pair.Value.Finish, GetWallNetAreaM2(Pair.Key));
	}
	for (const auto& Pair : Rooms)
	{
		Accumulate(Pair.Value.FloorFinish, Pair.Value.AreaM2);
	}
	Out.Sort([](const FFinishAreaEntry& A, const FFinishAreaEntry& B)
	{
		if (A.Type != B.Type) return (uint8)A.Type < (uint8)B.Type;
		return A.AreaM2 > B.AreaM2;
	});
	return Out;
}

float ARoomPlannerManager::GetTotalFinishAreaM2(ESurfaceFinishType Type) const
{
	float Total = 0.f;
	for (const FFinishAreaEntry& E : CalculateFinishAreas())
	{
		if (E.Type == Type) Total += E.AreaM2;
	}
	return Total;
}

FString ARoomPlannerManager::GetFinishAreaSummaryText() const
{
	const TArray<FFinishAreaEntry> Entries = CalculateFinishAreas();
	if (Entries.Num() == 0)
	{
		return TEXT("Отделка не назначена");
	}
	FString Text;
	for (int32 Pass = 0; Pass < 2; ++Pass)
	{
		const ESurfaceFinishType Type = (Pass == 0) ? ESurfaceFinishType::Paint : ESurfaceFinishType::Tile;
		float Total = 0.f;
		FString Lines;
		for (const FFinishAreaEntry& E : Entries)
		{
			if (E.Type != Type) continue;
			Total += E.AreaM2;
			Lines += FString::Printf(TEXT("  %s: %.2f м² (%d)\n"), *E.FinishKey, E.AreaM2, E.SurfaceCount);
		}
		if (Total > 0.f)
		{
			Text += FString::Printf(TEXT("%s: %.2f м²\n%s"), (Type == ESurfaceFinishType::Paint) ? TEXT("Краска") : TEXT("Плитка"), Total, *Lines);
		}
	}
	return Text.TrimEnd();
}

// ═══════════════════════════════════════════════════════════════════════════════
// Placement (REQ-17 / REQ-18)
// ═══════════════════════════════════════════════════════════════════════════════

void ARoomPlannerManager::BeginPlaceObject(const FString& AssetID)
{
	PendingPlacementKind = EPlannerPlacementKind::Object;
	PendingPlacementAssetID = AssetID;
	ActiveToolMode = EPlannerToolMode::PlaceFurniture;
	ClearAllSelection();
	RefreshNodeHandles();
}

void ARoomPlannerManager::BeginPlaceCabinetSet(FName ProductID)
{
	PendingPlacementKind = EPlannerPlacementKind::CabinetSet;
	PendingPlacementAssetID = ProductID.ToString();
	ActiveToolMode = EPlannerToolMode::PlaceFurniture;
	ClearAllSelection();
	RefreshNodeHandles();
}

void ARoomPlannerManager::CancelPendingPlacement()
{
	PendingPlacementKind = EPlannerPlacementKind::None;
	PendingPlacementAssetID.Empty();
}

UStaticMesh* ARoomPlannerManager::ResolveObjectMesh(const FString& AssetID) const
{
	if (AssetID.IsEmpty()) return nullptr;
	if (UDataTable* Catalog = ResolveObjectCatalog())
	{
		if (const FPlannerObjectRow* Row = Catalog->FindRow<FPlannerObjectRow>(FName(*AssetID), TEXT("ResolveObjectMesh"), false))
		{
			if (UStaticMesh* Mesh = Row->Mesh.LoadSynchronous())
			{
				return Mesh;
			}
		}
	}
	if (AssetID.StartsWith(TEXT("/")))
	{
		return LoadObject<UStaticMesh>(nullptr, *AssetID);
	}
	UE_LOG(LogTemp, Warning, TEXT("[MaxiMallConstructor] Object asset '%s' not found in DT_PlannerObjects and is not an asset path."), *AssetID);
	return nullptr;
}

TArray<FPlannerCatalogEntry> ARoomPlannerManager::GetAvailableObjects() const
{
	TArray<FPlannerCatalogEntry> Out;
	UDataTable* Catalog = ResolveObjectCatalog();
	if (!Catalog) return Out;
	for (const auto& Pair : Catalog->GetRowMap())
	{
		const FPlannerObjectRow* Row = reinterpret_cast<const FPlannerObjectRow*>(Pair.Value);
		if (!Row) continue;
		FPlannerCatalogEntry E;
		E.ID = Pair.Key.ToString();
		E.DisplayName = Row->DisplayName.IsEmpty() ? FText::FromName(Pair.Key) : Row->DisplayName;
		E.Thumbnail = Row->Thumbnail;
		E.Category = Row->Category;
		Out.Add(E);
	}
	return Out;
}

FString ARoomPlannerManager::AddPlacedObject(const FString& AssetID, const FVector& Location, const FRotator& Rotation, const FVector& Scale)
{
	if (AssetID.IsEmpty()) return FString();

	FPlacedFurnitureData D;
	D.InstanceID = PlannerJsonKeys::NewInstanceID();
	D.AssetID = AssetID;
	D.Location = Location;
	D.Rotation = Rotation;
	D.Scale = Scale.IsNearlyZero() ? FVector::OneVector : Scale;
	if (UDataTable* Catalog = ResolveObjectCatalog())
	{
		if (const FPlannerObjectRow* Row = Catalog->FindRow<FPlannerObjectRow>(FName(*AssetID), TEXT("AddPlacedObject"), false))
		{
			if (Scale.IsNearlyZero() || Scale.Equals(FVector::OneVector))
			{
				D.Scale = Row->DefaultScale.IsNearlyZero() ? FVector::OneVector : Row->DefaultScale;
			}
		}
	}

	PlacedObjects.Add(D.InstanceID, D);
	ApplyPlacedObjectActor(D);
	CommitStateAfterMutation();
	return D.InstanceID;
}

bool ARoomPlannerManager::MovePlacedObject(const FString& InstanceID, const FVector& Location, const FRotator& Rotation, const FVector& Scale)
{
	FPlacedFurnitureData* D = PlacedObjects.Find(InstanceID);
	if (!D) return false;
	D->Location = Location;
	D->Rotation = Rotation;
	if (!Scale.IsNearlyZero()) D->Scale = Scale;
	ApplyPlacedObjectActor(*D);
	CommitStateAfterMutation();
	return true;
}

bool ARoomPlannerManager::RemovePlacedObject(const FString& InstanceID)
{
	if (!PlacedObjects.Remove(InstanceID)) return false;
	if (TObjectPtr<APlannerPlacedObjectActor>* ActorPtr = PlacedObjectActors.Find(InstanceID))
	{
		if (*ActorPtr) (*ActorPtr)->Destroy();
		PlacedObjectActors.Remove(InstanceID);
	}
	if (SelectedObjectID == InstanceID)
	{
		SelectedObjectID.Empty();
		OnPlannerObjectSelected.Broadcast(FString(), EPlannerSelectionKind::None);
		NotifySelectionChanged();
	}
	CommitStateAfterMutation();
	return true;
}

bool ARoomPlannerManager::SetPlacedObjectFinish(const FString& InstanceID, const FSurfaceFinish& Finish)
{
	FPlacedFurnitureData* D = PlacedObjects.Find(InstanceID);
	if (!D) return false;
	D->Finish = Finish;
	ApplyPlacedObjectActor(*D);
	CommitStateAfterMutation();
	return true;
}

TArray<FPlacedFurnitureData> ARoomPlannerManager::GetPlacedObjects() const
{
	TArray<FPlacedFurnitureData> Out;
	PlacedObjects.GenerateValueArray(Out);
	return Out;
}

bool ARoomPlannerManager::GetPlacedObject(const FString& InstanceID, FPlacedFurnitureData& OutData) const
{
	if (const FPlacedFurnitureData* D = PlacedObjects.Find(InstanceID))
	{
		OutData = *D;
		return true;
	}
	return false;
}

APlannerPlacedObjectActor* ARoomPlannerManager::FindPlacedObjectActor(const FString& InstanceID) const
{
	if (const TObjectPtr<APlannerPlacedObjectActor>* Found = PlacedObjectActors.Find(InstanceID))
	{
		return Found->Get();
	}
	return nullptr;
}

FString ARoomPlannerManager::FindPlacedObjectAtWorldPos(const FVector& WorldPos) const
{
	const FVector2D P(WorldPos.X, WorldPos.Y);
	FString Best;
	float BestDist = TNumericLimits<float>::Max();
	for (const auto& Pair : PlacedObjects)
	{
		float Radius = 50.f;
		if (APlannerPlacedObjectActor* A = FindPlacedObjectActor(Pair.Key)) Radius = A->GetFootprintRadiusCm();
		const float D = FVector2D::Distance(P, FVector2D(Pair.Value.Location.X, Pair.Value.Location.Y));
		if (D <= Radius && D < BestDist)
		{
			BestDist = D;
			Best = Pair.Key;
		}
	}
	return Best;
}

void ARoomPlannerManager::MovePlacedObjectLocal(const FString& InstanceID, const FVector& Location, const FRotator& Rotation)
{
	FPlacedFurnitureData* D = PlacedObjects.Find(InstanceID);
	if (!D) return;
	D->Location = Location;
	D->Rotation = Rotation;
	if (APlannerPlacedObjectActor* A = FindPlacedObjectActor(InstanceID))
	{
		A->SetActorLocationAndRotation(Location, Rotation);
		A->Data.Location = Location;
		A->Data.Rotation = Rotation;
	}
}

void ARoomPlannerManager::ApplyPlacedObjectActor(const FPlacedFurnitureData& Data)
{
	APlannerPlacedObjectActor* Actor = FindPlacedObjectActor(Data.InstanceID);
	if (!Actor && GetWorld())
	{
		FActorSpawnParameters SpawnParams;
		SpawnParams.Owner = this;
		SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		UClass* Cls = PlacedObjectActorClass ? PlacedObjectActorClass.Get() : APlannerPlacedObjectActor::StaticClass();
		Actor = GetWorld()->SpawnActor<APlannerPlacedObjectActor>(Cls, Data.Location, Data.Rotation, SpawnParams);
		if (Actor)
		{
			PlacedObjectActors.Add(Data.InstanceID, Actor);
		}
	}
	if (Actor)
	{
		UStaticMesh* Mesh = ResolveObjectMesh(Data.AssetID);
		UMaterialInterface* ColorMat = Data.Finish.IsSet() ? ResolvePaintBaseMaterial() : nullptr;
		Actor->ApplyData(Data, Mesh, ColorMat);
		Actor->SetSelectedHighlight(Data.InstanceID == SelectedObjectID);
	}
}

void ARoomPlannerManager::RebuildPlacedObjectActors()
{
	// Remove actors whose data is gone
	TArray<FString> ToRemove;
	for (const auto& Pair : PlacedObjectActors)
	{
		if (!PlacedObjects.Contains(Pair.Key))
		{
			if (Pair.Value) Pair.Value->Destroy();
			ToRemove.Add(Pair.Key);
		}
	}
	for (const FString& Key : ToRemove) PlacedObjectActors.Remove(Key);

	// Spawn / update the rest
	for (const auto& Pair : PlacedObjects)
	{
		ApplyPlacedObjectActor(Pair.Value);
	}
}

TArray<FPlannerCatalogEntry> ARoomPlannerManager::GetAvailableCabinetSets() const
{
	TArray<FPlannerCatalogEntry> Out;
	UDataTable* Catalog = ResolveCabinetSetCatalog();
	if (!Catalog) return Out;
	for (const auto& Pair : Catalog->GetRowMap())
	{
		const FFurnitureProductRow* Row = reinterpret_cast<const FFurnitureProductRow*>(Pair.Value);
		if (!Row) continue;
		FPlannerCatalogEntry E;
		E.ID = Pair.Key.ToString();
		E.DisplayName = FText::FromName(Pair.Key);
		if (Row->CabinetOptions.Colors.Num() > 0)
		{
			const FFurnitureColorOption& First = Row->CabinetOptions.Colors[0];
			if (!First.ProductName.IsEmpty()) E.DisplayName = First.ProductName;
			E.Thumbnail = First.Thumbnail;
		}
		E.Category = TEXT("CabinetSet");
		Out.Add(E);
	}
	return Out;
}

AShowroomBooth* ARoomPlannerManager::SpawnCabinetSetActor(const FPlacedCabinetSetData& Data)
{
	UWorld* World = GetWorld();
	if (!World || !HasAuthority()) return nullptr;

	UClass* Cls = ResolveCabinetSetActorClass();
	const FTransform SpawnTM(Data.Rotation, Data.Location);
	AShowroomBooth* Booth = World->SpawnActorDeferred<AShowroomBooth>(Cls, SpawnTM, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!Booth) return nullptr;

	Booth->InitialProductID = Data.ProductID;
	Booth->PlannerInstanceID = Data.InstanceID;
	Booth->SetReplicates(true);
	Booth->SetReplicateMovement(true);
	Booth->FinishSpawning(SpawnTM);

	CabinetSetActorCache.Add(Data.InstanceID, Booth);
	return Booth;
}

AShowroomBooth* ARoomPlannerManager::FindCabinetSetActor(const FString& InstanceID) const
{
	if (InstanceID.IsEmpty()) return nullptr;
	if (const TWeakObjectPtr<AShowroomBooth>* Cached = CabinetSetActorCache.Find(InstanceID))
	{
		if (Cached->IsValid() && (*Cached)->PlannerInstanceID == InstanceID)
		{
			return Cached->Get();
		}
	}
	if (UWorld* World = GetWorld())
	{
		for (TActorIterator<AShowroomBooth> It(World); It; ++It)
		{
			if (It->PlannerInstanceID == InstanceID)
			{
				CabinetSetActorCache.Add(InstanceID, *It);
				return *It;
			}
		}
	}
	return nullptr;
}

FString ARoomPlannerManager::AddCabinetSet(FName ProductID, const FVector& Location, const FRotator& Rotation)
{
	if (!HasAuthority() || ProductID.IsNone()) return FString();

	FPlacedCabinetSetData D;
	D.InstanceID = PlannerJsonKeys::NewInstanceID();
	D.ProductID = ProductID;
	D.Location = Location;
	D.Rotation = Rotation;

	if (!SpawnCabinetSetActor(D))
	{
		return FString();
	}
	CabinetSets.Add(D.InstanceID, D);
	CommitStateAfterMutation();
	return D.InstanceID;
}

bool ARoomPlannerManager::MoveCabinetSet(const FString& InstanceID, const FVector& Location, const FRotator& Rotation)
{
	FPlacedCabinetSetData* D = CabinetSets.Find(InstanceID);
	if (!D) return false;
	D->Location = Location;
	D->Rotation = Rotation;
	if (AShowroomBooth* Booth = FindCabinetSetActor(InstanceID))
	{
		Booth->SetActorLocationAndRotation(Location, Rotation);
	}
	CommitStateAfterMutation();
	return true;
}

bool ARoomPlannerManager::RemoveCabinetSet(const FString& InstanceID)
{
	if (!CabinetSets.Remove(InstanceID)) return false;
	if (HasAuthority())
	{
		if (AShowroomBooth* Booth = FindCabinetSetActor(InstanceID))
		{
			Booth->Destroy();
		}
	}
	CabinetSetActorCache.Remove(InstanceID);
	if (SelectedCabinetSetID == InstanceID)
	{
		SelectedCabinetSetID.Empty();
		OnPlannerObjectSelected.Broadcast(FString(), EPlannerSelectionKind::None);
		NotifySelectionChanged();
	}
	CommitStateAfterMutation();
	return true;
}

TArray<FPlacedCabinetSetData> ARoomPlannerManager::GetCabinetSets() const
{
	TArray<FPlacedCabinetSetData> Out;
	CabinetSets.GenerateValueArray(Out);
	return Out;
}

bool ARoomPlannerManager::GetCabinetSet(const FString& InstanceID, FPlacedCabinetSetData& OutData) const
{
	if (const FPlacedCabinetSetData* D = CabinetSets.Find(InstanceID))
	{
		OutData = *D;
		return true;
	}
	return false;
}

FString ARoomPlannerManager::FindCabinetSetAtWorldPos(const FVector& WorldPos) const
{
	FString Best;
	float BestDist = TNumericLimits<float>::Max();
	for (const auto& Pair : CabinetSets)
	{
		FVector Origin = Pair.Value.Location;
		FVector Extent(60.f, 60.f, 60.f);
		if (AShowroomBooth* Booth = FindCabinetSetActor(Pair.Key))
		{
			Booth->GetActorBounds(false, Origin, Extent);
		}
		const float DX = FMath::Abs(WorldPos.X - Origin.X);
		const float DY = FMath::Abs(WorldPos.Y - Origin.Y);
		if (DX <= Extent.X + 10.f && DY <= Extent.Y + 10.f)
		{
			const float D = DX + DY;
			if (D < BestDist)
			{
				BestDist = D;
				Best = Pair.Key;
			}
		}
	}
	return Best;
}

void ARoomPlannerManager::MoveCabinetSetLocal(const FString& InstanceID, const FVector& Location, const FRotator& Rotation)
{
	FPlacedCabinetSetData* D = CabinetSets.Find(InstanceID);
	if (!D) return;
	D->Location = Location;
	D->Rotation = Rotation;
	if (AShowroomBooth* Booth = FindCabinetSetActor(InstanceID))
	{
		Booth->SetActorLocationAndRotation(Location, Rotation);
	}
}

void ARoomPlannerManager::ReconcileCabinetSetActors()
{
	if (HasAuthority())
	{
		// Destroy planner booths that are no longer in the layout
		if (UWorld* World = GetWorld())
		{
			TArray<AShowroomBooth*> Stale;
			for (TActorIterator<AShowroomBooth> It(World); It; ++It)
			{
				if (!It->PlannerInstanceID.IsEmpty() && !CabinetSets.Contains(It->PlannerInstanceID))
				{
					Stale.Add(*It);
				}
			}
			for (AShowroomBooth* Booth : Stale)
			{
				CabinetSetActorCache.Remove(Booth->PlannerInstanceID);
				Booth->Destroy();
			}
		}
		// Spawn missing / move existing
		for (const auto& Pair : CabinetSets)
		{
			AShowroomBooth* Booth = FindCabinetSetActor(Pair.Key);
			if (!Booth)
			{
				Booth = SpawnCabinetSetActor(Pair.Value);
			}
			else if (!Booth->GetActorLocation().Equals(Pair.Value.Location, 0.5f) || !Booth->GetActorRotation().Equals(Pair.Value.Rotation, 0.1f))
			{
				Booth->SetActorLocationAndRotation(Pair.Value.Location, Pair.Value.Rotation);
			}
		}
	}
	else
	{
		// Clients: booths replicate; just keep the transforms aligned with the layout for immediate feedback.
		for (const auto& Pair : CabinetSets)
		{
			if (AShowroomBooth* Booth = FindCabinetSetActor(Pair.Key))
			{
				if (!Booth->GetActorLocation().Equals(Pair.Value.Location, 0.5f) || !Booth->GetActorRotation().Equals(Pair.Value.Rotation, 0.1f))
				{
					Booth->SetActorLocationAndRotation(Pair.Value.Location, Pair.Value.Rotation);
				}
			}
		}
	}
}

void ARoomPlannerManager::DestroyAllPlannerCabinetSets()
{
	if (!HasAuthority()) return;
	if (UWorld* World = GetWorld())
	{
		TArray<AShowroomBooth*> ToDestroy;
		for (TActorIterator<AShowroomBooth> It(World); It; ++It)
		{
			if (!It->PlannerInstanceID.IsEmpty())
			{
				ToDestroy.Add(*It);
			}
		}
		for (AShowroomBooth* Booth : ToDestroy)
		{
			Booth->Destroy();
		}
	}
	CabinetSetActorCache.Empty();
}

// ═══════════════════════════════════════════════════════════════════════════════
// REQ-16: project restore
// ═══════════════════════════════════════════════════════════════════════════════

bool ARoomPlannerManager::ImportProjectFromSaveJSON(const FString& SaveRecordJSON)
{
	if (!HasAuthority())
	{
		UE_LOG(LogTemp, Warning, TEXT("[MaxiMallConstructor] ImportProjectFromSaveJSON must run on the server."));
		return false;
	}

	TSharedPtr<FJsonObject> Root;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(SaveRecordJSON);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		UE_LOG(LogTemp, Error, TEXT("[MaxiMallConstructor] ImportProjectFromSaveJSON: invalid JSON."));
		return false;
	}

	// 1. Planner layout (walls, openings, finishes, objects, cabinet sets)
	const TSharedPtr<FJsonObject>* PlannerObj = nullptr;
	if (Root->TryGetObjectField(TEXT("planner"), PlannerObj) && PlannerObj && PlannerObj->IsValid())
	{
		FString PlannerString;
		TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&PlannerString);
		FJsonSerializer::Serialize(PlannerObj->ToSharedRef(), Writer);
		ClearLayout();
		ImportLayoutFromJSON(PlannerString);
	}
	else if (Root->HasField(TEXT("nodes")) && Root->HasField(TEXT("walls")))
	{
		// A bare planner document was passed.
		ClearLayout();
		ImportLayoutFromJSON(SaveRecordJSON);
	}
	ReplicatedRoomJSON = ExportLayoutToJSON();

	// 2. Booth configuration states (planner-spawned sets by plannerInstanceId, level booths by name)
	const TArray<TSharedPtr<FJsonValue>>* BoothStates = nullptr;
	if (Root->TryGetArrayField(TEXT("boothStates"), BoothStates) && BoothStates)
	{
		TArray<AActor*> LevelBooths;
		if (GetWorld())
		{
			UGameplayStatics::GetAllActorsOfClass(GetWorld(), AShowroomBooth::StaticClass(), LevelBooths);
		}

		for (const TSharedPtr<FJsonValue>& Val : *BoothStates)
		{
			TSharedPtr<FJsonObject> BoothObj = Val->AsObject();
			if (!BoothObj.IsValid()) continue;

			AShowroomBooth* Target = nullptr;
			FString PlannerID;
			if (BoothObj->TryGetStringField(TEXT("plannerInstanceId"), PlannerID) && !PlannerID.IsEmpty())
			{
				Target = FindCabinetSetActor(PlannerID);
			}
			if (!Target)
			{
				const FString BoothName = BoothObj->GetStringField(TEXT("boothName"));
				for (AActor* A : LevelBooths)
				{
					if (A && A->GetName() == BoothName)
					{
						Target = Cast<AShowroomBooth>(A);
						break;
					}
				}
			}
			if (!Target) continue;

			const TSharedPtr<FJsonObject>* StateObjPtr = nullptr;
			if (!BoothObj->TryGetObjectField(TEXT("state"), StateObjPtr) || !StateObjPtr || !StateObjPtr->IsValid()) continue;
			const TSharedPtr<FJsonObject>& StateObj = *StateObjPtr;

			FShowroomBoothConfigState State;
			State.ProductID = FName(*StateObj->GetStringField(TEXT("productID")));
			State.ActiveSizeIndex = StateObj->GetIntegerField(TEXT("activeSizeIndex"));
			State.ActiveColorIndex = StateObj->GetIntegerField(TEXT("activeColorIndex"));
			State.CountertopSizeIndex = StateObj->GetIntegerField(TEXT("countertopSizeIndex"));
			State.ActiveCountertopColorIndex = StateObj->GetIntegerField(TEXT("activeCountertopColorIndex"));
			State.ClosetSizeIndex = StateObj->GetIntegerField(TEXT("closetSizeIndex"));
			State.ClosetColorIndex = StateObj->GetIntegerField(TEXT("closetColorIndex"));
			State.SinkSizeIndex = StateObj->GetIntegerField(TEXT("sinkSizeIndex"));
			State.SinkColorIndex = StateObj->GetIntegerField(TEXT("sinkColorIndex"));
			State.FaucetSizeIndex = StateObj->GetIntegerField(TEXT("faucetSizeIndex"));
			State.FaucetColorIndex = StateObj->GetIntegerField(TEXT("faucetColorIndex"));
			State.MirrorSizeIndex = StateObj->GetIntegerField(TEXT("mirrorSizeIndex"));
			State.MirrorColorIndex = StateObj->GetIntegerField(TEXT("mirrorColorIndex"));

			TArray<FCustomColorOverride> CustomColors;
			const TArray<TSharedPtr<FJsonValue>>* ColorsArray = nullptr;
			if (BoothObj->TryGetArrayField(TEXT("customColors"), ColorsArray) && ColorsArray)
			{
				for (const TSharedPtr<FJsonValue>& CVal : *ColorsArray)
				{
					TSharedPtr<FJsonObject> CObj = CVal->AsObject();
					if (!CObj.IsValid()) continue;
					const EFurnitureComponentType CompType = static_cast<EFurnitureComponentType>(CObj->GetIntegerField(TEXT("componentType")));
					FLinearColor Color = FLinearColor::White;
					const TSharedPtr<FJsonObject>* RGBA = nullptr;
					if (CObj->TryGetObjectField(TEXT("color"), RGBA) && RGBA && RGBA->IsValid())
					{
						Color.R = (*RGBA)->GetNumberField(TEXT("r"));
						Color.G = (*RGBA)->GetNumberField(TEXT("g"));
						Color.B = (*RGBA)->GetNumberField(TEXT("b"));
						Color.A = (*RGBA)->GetNumberField(TEXT("a"));
					}
					UMaterialInterface* OverrideMat = nullptr;
					FString MatPath;
					if (CObj->TryGetStringField(TEXT("overrideMaterial"), MatPath) && !MatPath.IsEmpty())
					{
						OverrideMat = Cast<UMaterialInterface>(StaticLoadObject(UMaterialInterface::StaticClass(), nullptr, *MatPath));
					}
					CustomColors.Add(FCustomColorOverride(CompType, Color, OverrideMat));
				}
			}

			TArray<EDoorSlotState> DoorStates;
			const TArray<TSharedPtr<FJsonValue>>* DoorsArray = nullptr;
			if (BoothObj->TryGetArrayField(TEXT("doorStates"), DoorsArray) && DoorsArray)
			{
				for (const TSharedPtr<FJsonValue>& DVal : *DoorsArray)
				{
					DoorStates.Add(static_cast<EDoorSlotState>(static_cast<int32>(DVal->AsNumber())));
				}
			}

			Target->LoadBoothFullState(State, CustomColors, DoorStates);
		}
	}

	return true;
}

