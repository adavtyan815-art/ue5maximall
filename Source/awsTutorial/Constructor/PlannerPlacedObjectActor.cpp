// Copyright 2026 MaxiMall. All Rights Reserved.

#include "Constructor/PlannerPlacedObjectActor.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"

APlannerPlacedObjectActor::APlannerPlacedObjectActor()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = false;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);

	MeshComponent = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("MeshComponent"));
	MeshComponent->SetupAttachment(SceneRoot);
	MeshComponent->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	MeshComponent->SetCollisionObjectType(ECC_WorldDynamic);
	MeshComponent->SetCollisionResponseToAllChannels(ECR_Block);
	MeshComponent->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
	MeshComponent->SetCastShadow(true);
}

void APlannerPlacedObjectActor::ApplyData(const FPlacedFurnitureData& InData, UStaticMesh* ResolvedMesh, UMaterialInterface* ColorOverrideMaterial)
{
	const bool bMeshChanged = (MeshComponent && MeshComponent->GetStaticMesh() != ResolvedMesh);
	Data = InData;

	SetActorLocationAndRotation(Data.Location, Data.Rotation);
	SetActorScale3D(Data.Scale.IsNearlyZero() ? FVector::OneVector : Data.Scale);

	if (MeshComponent && bMeshChanged)
	{
		MeshComponent->SetStaticMesh(ResolvedMesh);
		OriginalMaterials.Reset();
		if (ResolvedMesh)
		{
			const int32 NumMats = MeshComponent->GetNumMaterials();
			for (int32 i = 0; i < NumMats; ++i)
			{
				OriginalMaterials.Add(MeshComponent->GetMaterial(i));
			}
		}
	}

	ApplyColorOverride(ColorOverrideMaterial);
}

void APlannerPlacedObjectActor::ApplyColorOverride(UMaterialInterface* ColorOverrideMaterial)
{
	if (!MeshComponent || !MeshComponent->GetStaticMesh())
	{
		return;
	}

	const int32 NumMats = MeshComponent->GetNumMaterials();

	if (!Data.Finish.IsSet())
	{
		// Restore the mesh's original materials.
		for (int32 i = 0; i < NumMats; ++i)
		{
			if (OriginalMaterials.IsValidIndex(i))
			{
				MeshComponent->SetMaterial(i, OriginalMaterials[i]);
			}
		}
		return;
	}

	for (int32 i = 0; i < NumMats; ++i)
	{
		UMaterialInstanceDynamic* MID = nullptr;
		if (ColorOverrideMaterial)
		{
			UMaterialInstanceDynamic* Existing = Cast<UMaterialInstanceDynamic>(MeshComponent->GetMaterial(i));
			if (Existing && Existing->Parent == ColorOverrideMaterial)
			{
				MID = Existing;
			}
			else
			{
				MID = UMaterialInstanceDynamic::Create(ColorOverrideMaterial, this);
				MeshComponent->SetMaterial(i, MID);
			}
		}
		else
		{
			MID = Cast<UMaterialInstanceDynamic>(MeshComponent->GetMaterial(i));
			if (!MID)
			{
				MID = MeshComponent->CreateAndSetMaterialInstanceDynamic(i);
			}
		}

		if (MID)
		{
			MID->SetVectorParameterValue(FName("BaseColor"), Data.Finish.Color);
			MID->SetVectorParameterValue(FName("Color"), Data.Finish.Color);
		}
	}
}

void APlannerPlacedObjectActor::SetSelectedHighlight(bool bSelected)
{
	if (MeshComponent)
	{
		MeshComponent->SetRenderCustomDepth(bSelected);
		MeshComponent->SetCustomDepthStencilValue(bSelected ? 2 : 0);
	}
}

FVector APlannerPlacedObjectActor::GetLocalHalfExtents() const
{
	if (MeshComponent && MeshComponent->GetStaticMesh())
	{
		const FBoxSphereBounds B = MeshComponent->GetStaticMesh()->GetBounds();
		return B.BoxExtent * GetActorScale3D();
	}
	return FVector(50.f, 50.f, 50.f);
}

float APlannerPlacedObjectActor::GetFootprintRadiusCm() const
{
	const FVector Half = GetLocalHalfExtents();
	return FMath::Max(25.f, FMath::Sqrt(Half.X * Half.X + Half.Y * Half.Y));
}
