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

void APlannerPlacedObjectActor::ApplyData(const FPlacedFurnitureData& InData, UStaticMesh* ResolvedMesh, UMaterialInterface* ColorOverrideMaterial, const TArray<FPlannerMaterialOverride>& MaterialOverrides)
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
			// DT_PlannerObjects → Material Overrides: replace the listed slots on the freshly set mesh.
			// Bad slot indices and materials that fail to load are skipped so spawning never breaks.
			const int32 NumMats = MeshComponent->GetNumMaterials();
			for (const FPlannerMaterialOverride& Override : MaterialOverrides)
			{
				if (Override.SlotIndex < 0 || Override.SlotIndex >= NumMats)
				{
					UE_LOG(LogTemp, Warning, TEXT("[PlannerDrop] Object %s: material override slot %d ignored (mesh %s has %d slots)."),
						*Data.AssetID, Override.SlotIndex, *ResolvedMesh->GetName(), NumMats);
					continue;
				}
				if (Override.Material.IsNull()) continue;
				if (UMaterialInterface* Mat = Override.Material.LoadSynchronous())
				{
					MeshComponent->SetMaterial(Override.SlotIndex, Mat);
				}
				else
				{
					UE_LOG(LogTemp, Warning, TEXT("[PlannerDrop] Object %s: material override for slot %d could not load '%s'."),
						*Data.AssetID, Override.SlotIndex, *Override.Material.ToString());
				}
			}

			// Captured AFTER the overrides: clearing a finish colour restores the overridden materials.
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
