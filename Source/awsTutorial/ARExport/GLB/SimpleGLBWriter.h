// Copyright MaxiMall Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

struct FGLBVertex
{
    FVector3f Position = FVector3f::ZeroVector;
    FVector3f Normal   = FVector3f::UpVector;
    FVector2f UV       = FVector2f::ZeroVector;
};

struct FGLBPrimitive
{
    FString MeshName;
    TArray<FGLBVertex> Vertices;
    TArray<uint32> Indices;
    FLinearColor BaseColor = FLinearColor::White;
    float Metallic = 0.0f;
    float Roughness = 0.5f;
};

class AWSTUTORIAL_API FSimpleGLBWriter
{
public:
    /**
     * Builds a self-contained glTF 2.0 Binary (.glb) buffer from primitive geometry data.
     * @param Primitives List of sub-meshes with vertices, indices and material parameters.
     * @param OutGLBData Destination byte array receiving the serialized .glb binary.
     * @return True if serialized successfully.
     */
    static bool SerializeToGLB(const TArray<FGLBPrimitive>& Primitives, TArray<uint8>& OutGLBData);
};
