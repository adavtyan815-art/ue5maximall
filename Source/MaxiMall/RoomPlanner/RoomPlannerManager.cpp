// Copyright 2026 MaxiMall. All Rights Reserved.

#include "RoomPlanner/RoomPlannerManager.h"
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

ARoomPlannerManager::ARoomPlannerManager()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;

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
	BaseboardProceduralMesh->bUseAsyncCooking = false;
	BaseboardProceduralMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	BaseboardProceduralMesh->SetCastShadow(true);
	BaseboardProceduralMesh->SetAbsolute(true, true, true);

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

	if (UWorld* World = GetWorld())
	{
		if (APlayerController* PC = World->GetFirstPlayerController())
		{
			if (ActiveToolMode == EPlannerToolMode::Select)
			{
				// We no longer need to track drag state here since openings stay selected on click!
				bWasDraggingOpening = false;
			}

			if (b2DViewMode && TopDownCameraActor)
			{
				if (APawn* Pawn = PC->GetPawn())
				{
					FVector PawnLoc = Pawn->GetActorLocation();
					TopDownCameraActor->SetActorLocation(FVector(PawnLoc.X, PawnLoc.Y, 1500.f));
					TopDownCameraActor->SetActorRotation(FRotator(-90.f, 0.f, 0.f));
				}
			}
		}
	}

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
	// Check for snapping to existing node (50cm tolerance)
	const float SnapToleranceSq = 2500.f; // 50cm * 50cm
	for (const TPair<int32, FWallNode>& Pair : Nodes)
	{
		if (FVector2D::DistSquared(Pair.Value.Position, Position) <= SnapToleranceSq)
		{
			return Pair.Key;
		}
	}

	int32 NewID = NextNodeID++;
	FWallNode Node;
	Node.NodeID = NewID;
	Node.Position = Position;
	Nodes.Add(NewID, Node);
	return NewID;
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

	if (FWallNode* StartNode = Nodes.Find(Seg.StartNodeID))
	{
		StartNode->ConnectedSegmentIDs.Remove(SegmentID);
	}
	if (FWallNode* EndNode = Nodes.Find(Seg.EndNodeID))
	{
		EndNode->ConnectedSegmentIDs.Remove(SegmentID);
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
}

void ARoomPlannerManager::ClearLayout()
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
	NextNodeID = 1;
	NextSegmentID = 1;

	if (FloorProceduralMesh) FloorProceduralMesh->ClearAllMeshSections();
	if (CeilingProceduralMesh) CeilingProceduralMesh->ClearAllMeshSections();
	if (BaseboardProceduralMesh) BaseboardProceduralMesh->ClearAllMeshSections();
}

void ARoomPlannerManager::ComputeMiterOffsetsAtNode(int32 NodeID, TMap<int32, FVector2D>& OutStartLeftOffsets,
                                                     TMap<int32, FVector2D>& OutStartRightOffsets,
                                                     TMap<int32, FVector2D>& OutEndLeftOffsets,
                                                     TMap<int32, FVector2D>& OutEndRightOffsets)
{
	// Centerline extension logic is handled in RebuildAllWalls to ensure clean corner closing.
}

void ARoomPlannerManager::RebuildAllWalls()
{
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

			// --- Calculate Start Bisector Corner ---
			if (const FWallNode* StartNode = Nodes.Find(Seg->StartNodeID))
			{
				if (StartNode->ConnectedSegmentIDs.Num() == 2)
				{
					int32 OtherSegID = (StartNode->ConnectedSegmentIDs[0] == SegID) ? StartNode->ConnectedSegmentIDs[1] : StartNode->ConnectedSegmentIDs[0];
					if (const FWallSegment* OtherSeg = WallSegments.Find(OtherSegID))
					{
						FVector2D OtherEnd = (OtherSeg->StartNodeID == Seg->StartNodeID) ? Nodes[OtherSeg->EndNodeID].Position : Nodes[OtherSeg->StartNodeID].Position;
						FVector2D OtherDir = (OtherEnd - StartPos).GetSafeNormal();

						float Cross = Dir.X * OtherDir.Y - Dir.Y * OtherDir.X;
						float Dot = FVector2D::DotProduct(Dir, OtherDir);

						if (FMath::Abs(Cross) > 0.02f)
						{
							float AngleRad = FMath::Acos(FMath::Clamp(Dot, -1.f, 1.f));
							float SinHalf = FMath::Sin(AngleRad * 0.5f);
							float BisectorDist = (SinHalf > 0.05f) ? (HalfThick / SinHalf) : HalfThick;
							BisectorDist = FMath::Clamp(BisectorDist, HalfThick, Seg->Thickness * 2.5f);

							FVector2D Bisector = (Dir + OtherDir).GetSafeNormal();

							if (Cross > 0.f)
							{
								// Turn Left: Left side is inside corner (+Bisector), Right side is outside apex (-Bisector)
								SL2D = StartPos + Bisector * BisectorDist;
								SR2D = StartPos - Bisector * BisectorDist;
							}
							else
							{
								// Turn Right: Right side is inside corner (+Bisector), Left side is outside apex (-Bisector)
								SL2D = StartPos - Bisector * BisectorDist;
								SR2D = StartPos + Bisector * BisectorDist;
							}

							bStartCap = false; // Seamless bisector seam with connecting wall
						}
					}
				}
			}

			// --- Calculate End Bisector Corner ---
			if (const FWallNode* EndNode = Nodes.Find(Seg->EndNodeID))
			{
				if (EndNode->ConnectedSegmentIDs.Num() == 2)
				{
					int32 OtherSegID = (EndNode->ConnectedSegmentIDs[0] == SegID) ? EndNode->ConnectedSegmentIDs[1] : EndNode->ConnectedSegmentIDs[0];
					if (const FWallSegment* OtherSeg = WallSegments.Find(OtherSegID))
					{
						FVector2D OtherEnd = (OtherSeg->StartNodeID == Seg->EndNodeID) ? Nodes[OtherSeg->EndNodeID].Position : Nodes[OtherSeg->StartNodeID].Position;
						FVector2D OtherDir = (OtherEnd - EndPos).GetSafeNormal();
						FVector2D AwayDir = -Dir;

						float Cross = AwayDir.X * OtherDir.Y - AwayDir.Y * OtherDir.X;
						float Dot = FVector2D::DotProduct(AwayDir, OtherDir);

						if (FMath::Abs(Cross) > 0.02f)
						{
							float AngleRad = FMath::Acos(FMath::Clamp(Dot, -1.f, 1.f));
							float SinHalf = FMath::Sin(AngleRad * 0.5f);
							float BisectorDist = (SinHalf > 0.05f) ? (HalfThick / SinHalf) : HalfThick;
							BisectorDist = FMath::Clamp(BisectorDist, HalfThick, Seg->Thickness * 2.5f);

							FVector2D Bisector = (AwayDir + OtherDir).GetSafeNormal();

							if (Cross > 0.f)
							{
								// Turn Left relative to AwayDir (which is segment's Right side: ER2D)
								ER2D = EndPos + Bisector * BisectorDist;
								EL2D = EndPos - Bisector * BisectorDist;
							}
							else
							{
								// Turn Right relative to AwayDir (which is segment's Left side: EL2D)
								EL2D = EndPos + Bisector * BisectorDist;
								ER2D = EndPos - Bisector * BisectorDist;
							}

							bEndCap = false; // Seamless bisector seam with connecting wall
						}
					}
				}
			}

			WallActor->WallData = *Seg;
			WallActor->RebuildWallMesh(StartPos, EndPos, SL2D, SR2D, EL2D, ER2D, bStartCap, bEndCap, true);
			WallActor->SetOpeningCADSymbolsVisibility(b2DViewMode);
		}
	}
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
		return;
	}

	TMap<int32, TArray<int32>> Adj;
	for (const auto& Pair : WallSegments)
	{
		const FWallSegment& Seg = Pair.Value;
		if (Nodes.Contains(Seg.StartNodeID) && Nodes.Contains(Seg.EndNodeID))
		{
			Adj.FindOrAdd(Seg.StartNodeID).AddUnique(Seg.EndNodeID);
			Adj.FindOrAdd(Seg.EndNodeID).AddUnique(Seg.StartNodeID);
		}
	}

	TArray<int32> Path;
	TArray<TArray<int32>> FoundCycles;
	TSet<int32> VisitedGlobal;

	TFunction<void(int32, int32)> DFS = [&](int32 Curr, int32 Parent)
	{
		VisitedGlobal.Add(Curr);
		Path.Add(Curr);

		if (Adj.Contains(Curr))
		{
			for (int32 Neighbor : Adj[Curr])
			{
				if (Neighbor == Parent) continue;

				int32 CycleStartIdx = Path.IndexOfByKey(Neighbor);
				if (CycleStartIdx != INDEX_NONE)
				{
					TArray<int32> Cycle;
					for (int32 k = CycleStartIdx; k < Path.Num(); ++k)
					{
						Cycle.Add(Path[k]);
					}
					if (Cycle.Num() >= 3)
					{
						FoundCycles.Add(Cycle);
					}
				}
				else if (!VisitedGlobal.Contains(Neighbor))
				{
					DFS(Neighbor, Curr);
				}
			}
		}
		Path.Pop();
	};

	for (const auto& Pair : Nodes)
	{
		if (!VisitedGlobal.Contains(Pair.Key))
		{
			DFS(Pair.Key, -1);
		}
	}

	if (FoundCycles.Num() == 0)
	{
		return;
	}

	const TArray<int32>& PrimaryCycle = FoundCycles[0];
	FRoomData Room;
	Room.RoomID = 1;
	for (int32 NID : PrimaryCycle)
	{
		if (Nodes.Contains(NID))
		{
			Room.FloorPolygon.Add(Nodes[NID].Position);
		}
	}

	int32 VertCount = Room.FloorPolygon.Num();
	if (VertCount < 3) return;

	float TwiceArea = 0.f;
	for (int32 i = 0; i < VertCount; ++i)
	{
		const FVector2D& P1 = Room.FloorPolygon[i];
		const FVector2D& P2 = Room.FloorPolygon[(i + 1) % VertCount];
		TwiceArea += (P1.X * P2.Y - P2.X * P1.Y);
	}
	
	// Ensure counter-clockwise winding
	if (TwiceArea < 0.f)
	{
		Algo::Reverse(Room.FloorPolygon);
		TwiceArea = -TwiceArea;
	}

	Room.AreaM2 = TwiceArea * 0.5f / 10000.f;
	Rooms.Add(Room.RoomID, Room);

	UMaterialInterface* BaseMat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/EngineMaterials/DefaultMaterial.DefaultMaterial"));
	if (!BaseMat)
	{
		BaseMat = UMaterial::GetDefaultMaterial(MD_Surface);
	}

	UMaterialInstanceDynamic* FloorMatInst = UMaterialInstanceDynamic::Create(BaseMat, this);
	if (FloorMatInst)
	{
		FloorMatInst->SetVectorParameterValue(TEXT("Color"), FLinearColor(1.0f, 1.0f, 1.0f, 1.0f));
		FloorMatInst->SetVectorParameterValue(TEXT("BaseColor"), FLinearColor(1.0f, 1.0f, 1.0f, 1.0f));
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

	// Triangulate Room Polygon (Ear Clipping)
	TArray<int32> Indices;
	for (int32 i = 0; i < VertCount; ++i) Indices.Add(i);

	auto IsPointInTriangle = [](const FVector2D& P, const FVector2D& A, const FVector2D& B, const FVector2D& C) {
		auto Sign = [](const FVector2D& P1, const FVector2D& P2, const FVector2D& P3) {
			return (P1.X - P3.X) * (P2.Y - P3.Y) - (P2.X - P3.X) * (P1.Y - P3.Y);
		};
		bool b1 = Sign(P, A, B) < 0.0f;
		bool b2 = Sign(P, B, C) < 0.0f;
		bool b3 = Sign(P, C, A) < 0.0f;
		return ((b1 == b2) && (b2 == b3));
	};

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

			const FVector2D& P0 = Room.FloorPolygon[V0];
			const FVector2D& P1 = Room.FloorPolygon[V1];
			const FVector2D& P2 = Room.FloorPolygon[V2];

			// Cross > 0 indicates convex corner (since winding is CCW)
			float Cross = (P1.X - P0.X) * (P2.Y - P1.Y) - (P1.Y - P0.Y) * (P2.X - P1.X);
			if (Cross >= -0.01f)
			{
				bool bValid = true;
				for (int32 j = 0; j < Indices.Num(); ++j)
				{
					if (j == PrevIdx || j == i || j == NextIdx) continue;
					if (IsPointInTriangle(Room.FloorPolygon[Indices[j]], P0, P1, P2))
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
			// Fallback to guarantee triangulation never fully aborts
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

	// Reverse to make it Clockwise for Unreal's default front-face culling
	Algo::Reverse(TriangulatedIndices);

	// 1. Generate Procedural Floor Mesh
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
			Vertices.Add(FVector(Room.FloorPolygon[i].X, Room.FloorPolygon[i].Y, 1.f));
			Normals.Add(FVector::UpVector);
			UVs.Add(Room.FloorPolygon[i] / 100.f);
			FloorColors.Add(FColor(255, 255, 255, 255));
		}
		Triangles = TriangulatedIndices;

		// Bottom face (Z=0.f)
		int32 StartIdx = Vertices.Num();
		for (int32 i = 0; i < VertCount; ++i)
		{
			Vertices.Add(FVector(Room.FloorPolygon[i].X, Room.FloorPolygon[i].Y, 0.f));
			Normals.Add(-FVector::UpVector);
			UVs.Add(Room.FloorPolygon[i] / 100.f);
			FloorColors.Add(FColor(255, 255, 255, 255));
		}
		for (int32 i = 0; i < TriangulatedIndices.Num(); i+=3)
		{
			Triangles.Add(StartIdx + TriangulatedIndices[i]);
			Triangles.Add(StartIdx + TriangulatedIndices[i+2]);
			Triangles.Add(StartIdx + TriangulatedIndices[i+1]);
		}

		// Side faces for 1cm thickness
		for (int32 i = 0; i < VertCount; ++i)
		{
			FVector2D P1 = Room.FloorPolygon[i];
			FVector2D P2 = Room.FloorPolygon[(i + 1) % VertCount];
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

		FloorProceduralMesh->CreateMeshSection(0, Vertices, Triangles, Normals, UVs, FloorColors, TArray<FProcMeshTangent>(), true);
		FloorProceduralMesh->SetMaterial(0, FloorMatInst ? FloorMatInst : BaseMat);
		FloorProceduralMesh->SetVisibility(true);
	}

	// 2. Generate Procedural Ceiling Mesh
	if (CeilingProceduralMesh)
	{
		TArray<FVector> CeilVerts;
		TArray<int32> CeilTris;
		TArray<FVector> CeilNorms;
		TArray<FVector2D> CeilUVs;
		TArray<FColor> CeilColors;

		float CeilZ = 280.f;
		for (int32 i = 0; i < VertCount; ++i)
		{
			CeilVerts.Add(FVector(Room.FloorPolygon[i].X, Room.FloorPolygon[i].Y, CeilZ));
			CeilNorms.Add(-FVector::UpVector);
			CeilUVs.Add(Room.FloorPolygon[i] / 100.f);
			CeilColors.Add(FColor(240, 240, 240, 255));
		}

		for (int32 i = 0; i < TriangulatedIndices.Num(); i += 3)
		{
			CeilTris.Add(TriangulatedIndices[i]);
			CeilTris.Add(TriangulatedIndices[i + 2]);
			CeilTris.Add(TriangulatedIndices[i + 1]);
		}

		CeilingProceduralMesh->CreateMeshSection(0, CeilVerts, CeilTris, CeilNorms, CeilUVs, CeilColors, TArray<FProcMeshTangent>(), true);
		CeilingProceduralMesh->SetMaterial(0, CeilMatInst ? CeilMatInst : BaseMat);
		bool bShowCeil = bCeilingVisible && !b2DViewMode;
		CeilingProceduralMesh->SetVisibility(bShowCeil);
	}

	// 3. Generate Procedural 3D Baseboards
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
			FVector2D P1 = Room.FloorPolygon[i];
			FVector2D P2 = Room.FloorPolygon[(i + 1) % VertCount];
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

		BaseboardProceduralMesh->CreateMeshSection(0, BbVerts, BbTris, BbNorms, BbUVs, BbColors, TArray<FProcMeshTangent>(), false);
		BaseboardProceduralMesh->SetMaterial(0, BbMatInst ? BbMatInst : BaseMat);
		BaseboardProceduralMesh->SetVisibility(true);
	}
}


FString ARoomPlannerManager::ExportLayoutToJSON() const
{
	TSharedPtr<FJsonObject> RootObject = MakeShareable(new FJsonObject());

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

	// Serializing Wall Segments
	TArray<TSharedPtr<FJsonValue>> WallsArray;
	for (const TPair<int32, FWallSegment>& Pair : WallSegments)
	{
		TSharedPtr<FJsonObject> WallObj = MakeShareable(new FJsonObject());
		WallObj->SetNumberField(TEXT("id"), Pair.Key);
		WallObj->SetNumberField(TEXT("start"), Pair.Value.StartNodeID);
		WallObj->SetNumberField(TEXT("end"), Pair.Value.EndNodeID);
		WallObj->SetNumberField(TEXT("thickness"), Pair.Value.Thickness);
		WallObj->SetNumberField(TEXT("height"), Pair.Value.Height);

		TArray<TSharedPtr<FJsonValue>> OpeningsArray;
		for (const FWallOpening& Op : Pair.Value.Openings)
		{
			TSharedPtr<FJsonObject> OpObj = MakeShareable(new FJsonObject());
			OpObj->SetStringField(TEXT("id"), Op.OpeningID);
			OpObj->SetStringField(TEXT("type"), Op.Type == EOpeningType::Door ? TEXT("door") : TEXT("window"));
			OpObj->SetNumberField(TEXT("dist"), Op.DistanceFromStart);
			OpObj->SetNumberField(TEXT("width"), Op.Width);
			OpObj->SetNumberField(TEXT("height"), Op.Height);
			OpObj->SetNumberField(TEXT("sill"), Op.SillHeight);
			OpeningsArray.Add(MakeShareable(new FJsonValueObject(OpObj)));
		}
		WallObj->SetArrayField(TEXT("openings"), OpeningsArray);
		WallsArray.Add(MakeShareable(new FJsonValueObject(WallObj)));
	}
	RootObject->SetArrayField(TEXT("walls"), WallsArray);

	// Serializing Rooms Stats
	TArray<TSharedPtr<FJsonValue>> RoomsArray;
	for (const TPair<int32, FRoomData>& Pair : Rooms)
	{
		TSharedPtr<FJsonObject> RoomObj = MakeShareable(new FJsonObject());
		RoomObj->SetNumberField(TEXT("id"), Pair.Key);
		RoomObj->SetNumberField(TEXT("area_m2"), Pair.Value.AreaM2);
		RoomsArray.Add(MakeShareable(new FJsonValueObject(RoomObj)));
	}
	RootObject->SetArrayField(TEXT("rooms"), RoomsArray);

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

	ClearLayout();

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

				const TArray<TSharedPtr<FJsonValue>>* OpeningsArray = nullptr;
				if (SegID != -1 && WallObj->TryGetArrayField(TEXT("openings"), OpeningsArray) && OpeningsArray)
				{
					for (const TSharedPtr<FJsonValue>& OpVal : *OpeningsArray)
					{
						TSharedPtr<FJsonObject> OpObj = OpVal->AsObject();
						if (OpObj.IsValid())
						{
							FString TypeStr = OpObj->GetStringField(TEXT("type"));
							EOpeningType Type = TypeStr.Equals(TEXT("window"), ESearchCase::IgnoreCase) ? EOpeningType::Window : EOpeningType::Door;
							float Dist = OpObj->GetNumberField(TEXT("dist"));
							float Width = OpObj->GetNumberField(TEXT("width"));
							float OpHeight = OpObj->GetNumberField(TEXT("height"));
							float Sill = OpObj->GetNumberField(TEXT("sill"));

							AddOpeningToWall(SegID, Type, Dist, Width, OpHeight, Sill);
						}
					}
				}
			}
		}
	}

	RebuildAllWalls();
	RebuildRooms();

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
	UpdateSelectionVisuals();
	OnRoomPlannerUpdated.Broadcast(JSONString);
	if (SelectedSegmentID != -1)
	{
		OnWallSelected.Broadcast(SelectedSegmentID, GetWallLength(SelectedSegmentID));
	}

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
	}

	if (CeilingProceduralMesh)
	{
		CeilingProceduralMesh->SetVisibility(bCeilingVisible && !bIn2DMode);
	}

	for (auto& Pair : WallActors)
	{
		if (Pair.Value)
		{
			Pair.Value->SetOpeningCADSymbolsVisibility(bIn2DMode);
		}
	}

	if (UWorld* World = GetWorld())
	{
		if (APlayerController* PC = World->GetFirstPlayerController())
		{
			APawn* Pawn = PC->GetPawn();

			if (bIn2DMode)
			{
				SavedControlRotation = PC->GetControlRotation();
				PC->SetControlRotation(FRotator(0.f, 0.f, 0.f));
				PC->SetIgnoreLookInput(true);
				if (!TopDownCameraActor)
				{
					FActorSpawnParameters SpawnParams;
					SpawnParams.Owner = this;
					SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
					FVector CamLoc(0.f, 0.f, 2000.f);
					if (Pawn)
					{
						CamLoc.X = Pawn->GetActorLocation().X;
						CamLoc.Y = Pawn->GetActorLocation().Y;
					}
					FRotator CamRot(-90.f, 0.f, 0.f);
					TopDownCameraActor = World->SpawnActor<ACameraActor>(ACameraActor::StaticClass(), CamLoc, CamRot, SpawnParams);
					if (TopDownCameraActor)
					{
						if (UCameraComponent* CamComp = TopDownCameraActor->GetCameraComponent())
						{
							CamComp->ProjectionMode = ECameraProjectionMode::Orthographic;
							CamComp->OrthoWidth = 2500.f;
							CamComp->bConstrainAspectRatio = false;
							CamComp->OrthoNearClipPlane = -5000.f;
							CamComp->OrthoFarClipPlane = 20000.f;
						}
					}
				}
				else
				{
					if (UCameraComponent* CamComp = TopDownCameraActor->GetCameraComponent())
					{
						CamComp->ProjectionMode = ECameraProjectionMode::Orthographic;
						CamComp->OrthoWidth = 2500.f;
						CamComp->bConstrainAspectRatio = false;
					}
				}

				if (TopDownCameraActor)
				{
					PC->SetViewTargetWithBlend(TopDownCameraActor, 0.25f);
				}

				FInputModeGameAndUI InputMode;
				InputMode.SetHideCursorDuringCapture(false);
				InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
				PC->SetInputMode(InputMode);
				PC->bShowMouseCursor = true;
			}
			else
			{
				ClearWallSelection();
				if (ACharacter* CharPawn = Cast<ACharacter>(PC->GetPawn()))
				{
					PC->ResetIgnoreInputFlags();
					PC->SetControlRotation(SavedControlRotation);
					PC->SetIgnoreLookInput(false);
					PC->SetViewTargetWithBlend(CharPawn, 0.3f);
					
					FInputModeGameAndUI InputMode;
					InputMode.SetHideCursorDuringCapture(true);
					InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
					PC->SetInputMode(InputMode);
				}
			}
		}
	}
}

bool ARoomPlannerManager::FindWallEndpointSnap2D(const FVector2D& Point2D, float SnapRadiusCm, FVector2D& OutSnapPos) const
{
	float BestDistSq = SnapRadiusCm * SnapRadiusCm;
	bool bFoundSnap = false;

	for (const auto& Pair : WallSegments)
	{
		const FWallSegment& Seg = Pair.Value;
		if (Nodes.Contains(Seg.StartNodeID) && Nodes.Contains(Seg.EndNodeID))
		{
			FVector2D P1 = Nodes[Seg.StartNodeID].Position;
			FVector2D P2 = Nodes[Seg.EndNodeID].Position;
			FVector2D Dir = (P2 - P1);
			float TotalLen = Dir.Size();
			if (TotalLen < 0.1f) continue;
			Dir /= TotalLen;
			FVector2D Norm(-Dir.Y, Dir.X);
			float H = Seg.Thickness * 0.5f;

			// 6 Endpoint & Corner Vertices (Start/End Center & 4 Outer Cap Corners)
			FVector2D Vertices[6];
			Vertices[0] = P1 + Norm * H; // Start Left Corner
			Vertices[1] = P1 - Norm * H; // Start Right Corner
			Vertices[2] = P2 + Norm * H; // End Left Corner
			Vertices[3] = P2 - Norm * H; // End Right Corner
			Vertices[4] = P1;            // Center Start Node
			Vertices[5] = P2;            // Center End Node

			for (int32 i = 0; i < 6; ++i)
			{
				float DistSq = FVector2D::DistSquared(Point2D, Vertices[i]);
				if (DistSq <= BestDistSq)
				{
					BestDistSq = DistSq;
					OutSnapPos = Vertices[i];
					bFoundSnap = true;
				}
			}
		}
	}

	return bFoundSnap;
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

	// Only attempt to snap P2 to other walls if P2 has moved far enough from P1 (> 50cm)
	if (CurrentLengthCm > 50.f)
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
			int32 N1 = AddNode(P1);
			int32 N2 = AddNode(P2);
			AddWall(N1, N2, 20.f, 280.f);
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
	ActiveToolMode = NewToolMode;
	if (ActiveToolMode != EPlannerToolMode::Select)
	{
		ClearWallSelection();
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
		}
	}
	OnWallSelected.Broadcast(-1, 0.f);
}

int32 ARoomPlannerManager::SelectWallAtWorldPos(const FVector& WorldPos)
{
	if (ActiveToolMode != EPlannerToolMode::Select)
	{
		ClearWallSelection();
		return -1;
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

	FWallSegment& Seg = WallSegments[SegmentID];
	if (Nodes.Contains(Seg.StartNodeID) && Nodes.Contains(Seg.EndNodeID))
	{
		FVector2D P1 = Nodes[Seg.StartNodeID].Position;
		FVector2D P2 = Nodes[Seg.EndNodeID].Position;
		FVector2D Dir = (P2 - P1).GetSafeNormal();
		float NewLengthCm = NewLengthMeters * 100.f;

		Nodes[Seg.EndNodeID].Position = P1 + Dir * NewLengthCm;

		RebuildAllWalls();
		RebuildRooms();
		ReplicatedRoomJSON = ExportLayoutToJSON();
		OnRoomPlannerUpdated.Broadcast(ReplicatedRoomJSON);
		return true;
	}
	return false;
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
				DistFromStartCm = FMath::Max(10.f, (WallLengthCm - WidthCm) * 0.5f);
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
				DistFromStartCm = FMath::Max(10.f, (WallLengthCm - WidthCm) * 0.5f);
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

			float ClampedDist = FMath::Clamp(NewDistFromStartCm, HalfW + 5.f, WallLen - HalfW - 5.f);
			Seg.Openings[OpeningIndex].DistanceFromStart = ClampedDist;

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
	if (Seg.Openings.IsValidIndex(OpeningIndex))
	{
		FWallOpening& Op = Seg.Openings[OpeningIndex];
		Op.Width = WidthMeters * 100.f;
		Op.Height = HeightMeters * 100.f;
		Op.SillHeight = SillHeightMeters * 100.f;

		if (TObjectPtr<AProceduralWallActor>* ActorPtr = WallActors.Find(SegmentID))
		{
			if (*ActorPtr)
			{
				(*ActorPtr)->WallData = Seg;
				FVector2D P1 = Nodes[Seg.StartNodeID].Position;
				FVector2D P2 = Nodes[Seg.EndNodeID].Position;
				(*ActorPtr)->RebuildWallMesh(P1, P2);
			}
		}
		RebuildAllWalls();
		ReplicatedRoomJSON = ExportLayoutToJSON();
		OnRoomPlannerUpdated.Broadcast(ReplicatedRoomJSON);
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

