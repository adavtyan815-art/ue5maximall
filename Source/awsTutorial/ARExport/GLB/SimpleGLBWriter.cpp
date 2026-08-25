// Copyright MaxiMall Project. All Rights Reserved.

#include "ARExport/GLB/SimpleGLBWriter.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"

bool FSimpleGLBWriter::SerializeToGLB(const TArray<FGLBPrimitive>& Primitives, TArray<uint8>& OutGLBData)
{
    if (Primitives.Num() == 0)
    {
        return false;
    }

    TArray<uint8> BinaryBuffer;
    TArray<TSharedPtr<FJsonValue>> AccessorsArray;
    TArray<TSharedPtr<FJsonValue>> BufferViewsArray;
    TArray<TSharedPtr<FJsonValue>> MaterialsArray;
    TArray<TSharedPtr<FJsonValue>> MeshesArray;
    TArray<TSharedPtr<FJsonValue>> NodesArray;
    TArray<TSharedPtr<FJsonValue>> SceneNodesArray;
    TArray<TSharedPtr<FJsonValue>> ImagesArray;
    TArray<TSharedPtr<FJsonValue>> TexturesArray;
    TMap<FString, int32> TextureIndexByKey;

    auto AlignBufferTo4Bytes = [&BinaryBuffer]()
    {
        while (BinaryBuffer.Num() % 4 != 0)
        {
            BinaryBuffer.Add(0);
        }
    };

    int32 MaterialIndexCounter = 0;
    int32 NodeIndexCounter = 0;

    for (int32 PrimIdx = 0; PrimIdx < Primitives.Num(); ++PrimIdx)
    {
        const FGLBPrimitive& Prim = Primitives[PrimIdx];
        if (Prim.Vertices.Num() == 0 || Prim.Indices.Num() == 0)
        {
            continue;
        }

        // ── 0. Embed Base Color Texture (deduplicated by key) ────────────────
        int32 BaseColorTextureIdx = INDEX_NONE;
        if (!Prim.BaseColorTextureKey.IsEmpty())
        {
            if (const int32* ExistingIdx = TextureIndexByKey.Find(Prim.BaseColorTextureKey))
            {
                BaseColorTextureIdx = *ExistingIdx;
            }
            else if (Prim.BaseColorTexturePNG.Num() > 0)
            {
                AlignBufferTo4Bytes();
                const int32 ImageByteOffset = BinaryBuffer.Num();
                BinaryBuffer.Append(Prim.BaseColorTexturePNG);

                const int32 ImageBufferViewIdx = BufferViewsArray.Num();
                TSharedPtr<FJsonObject> ImageBV = MakeShared<FJsonObject>();
                ImageBV->SetNumberField(TEXT("buffer"), 0);
                ImageBV->SetNumberField(TEXT("byteOffset"), ImageByteOffset);
                ImageBV->SetNumberField(TEXT("byteLength"), Prim.BaseColorTexturePNG.Num());
                BufferViewsArray.Add(MakeShared<FJsonValueObject>(ImageBV));

                const int32 ImageIdx = ImagesArray.Num();
                TSharedPtr<FJsonObject> ImageObj = MakeShared<FJsonObject>();
                ImageObj->SetStringField(TEXT("name"), Prim.BaseColorTextureKey);
                ImageObj->SetStringField(TEXT("mimeType"), TEXT("image/png"));
                ImageObj->SetNumberField(TEXT("bufferView"), ImageBufferViewIdx);
                ImagesArray.Add(MakeShared<FJsonValueObject>(ImageObj));

                BaseColorTextureIdx = TexturesArray.Num();
                TSharedPtr<FJsonObject> TexObj = MakeShared<FJsonObject>();
                TexObj->SetNumberField(TEXT("sampler"), 0);
                TexObj->SetNumberField(TEXT("source"), ImageIdx);
                TexturesArray.Add(MakeShared<FJsonValueObject>(TexObj));

                TextureIndexByKey.Add(Prim.BaseColorTextureKey, BaseColorTextureIdx);
            }
        }

        // ── 0b. Embed Normal Texture (deduplicated by key) ───────────────────
        int32 NormalTextureIdx = INDEX_NONE;
        if (!Prim.NormalTextureKey.IsEmpty())
        {
            if (const int32* ExistingNormalIdx = TextureIndexByKey.Find(Prim.NormalTextureKey))
            {
                NormalTextureIdx = *ExistingNormalIdx;
            }
            else if (Prim.NormalTexturePNG.Num() > 0)
            {
                AlignBufferTo4Bytes();
                const int32 NormalImageByteOffset = BinaryBuffer.Num();
                BinaryBuffer.Append(Prim.NormalTexturePNG);

                const int32 NormalImageBufferViewIdx = BufferViewsArray.Num();
                TSharedPtr<FJsonObject> NormalImageBV = MakeShared<FJsonObject>();
                NormalImageBV->SetNumberField(TEXT("buffer"), 0);
                NormalImageBV->SetNumberField(TEXT("byteOffset"), NormalImageByteOffset);
                NormalImageBV->SetNumberField(TEXT("byteLength"), Prim.NormalTexturePNG.Num());
                BufferViewsArray.Add(MakeShared<FJsonValueObject>(NormalImageBV));

                const int32 NormalImageIdx = ImagesArray.Num();
                TSharedPtr<FJsonObject> NormalImageObj = MakeShared<FJsonObject>();
                NormalImageObj->SetStringField(TEXT("name"), Prim.NormalTextureKey);
                NormalImageObj->SetStringField(TEXT("mimeType"), TEXT("image/png"));
                NormalImageObj->SetNumberField(TEXT("bufferView"), NormalImageBufferViewIdx);
                ImagesArray.Add(MakeShared<FJsonValueObject>(NormalImageObj));

                NormalTextureIdx = TexturesArray.Num();
                TSharedPtr<FJsonObject> NormalTexObj = MakeShared<FJsonObject>();
                NormalTexObj->SetNumberField(TEXT("sampler"), 0);
                NormalTexObj->SetNumberField(TEXT("source"), NormalImageIdx);
                TexturesArray.Add(MakeShared<FJsonValueObject>(NormalTexObj));

                TextureIndexByKey.Add(Prim.NormalTextureKey, NormalTextureIdx);
            }
        }

        // ── 1. Create Material ───────────────────────────────────────────────
        TSharedPtr<FJsonObject> MatObj = MakeShared<FJsonObject>();
        MatObj->SetStringField(TEXT("name"), FString::Printf(TEXT("Mat_%s"), *Prim.MeshName));

        TSharedPtr<FJsonObject> PbrObj = MakeShared<FJsonObject>();
        TArray<TSharedPtr<FJsonValue>> BaseColorArr;
        BaseColorArr.Add(MakeShared<FJsonValueNumber>(Prim.BaseColor.R));
        BaseColorArr.Add(MakeShared<FJsonValueNumber>(Prim.BaseColor.G));
        BaseColorArr.Add(MakeShared<FJsonValueNumber>(Prim.BaseColor.B));
        BaseColorArr.Add(MakeShared<FJsonValueNumber>(Prim.BaseColor.A));
        PbrObj->SetArrayField(TEXT("baseColorFactor"), BaseColorArr);
        PbrObj->SetNumberField(TEXT("metallicFactor"), Prim.Metallic);
        PbrObj->SetNumberField(TEXT("roughnessFactor"), Prim.Roughness);

        if (BaseColorTextureIdx != INDEX_NONE)
        {
            TSharedPtr<FJsonObject> BaseColorTexObj = MakeShared<FJsonObject>();
            BaseColorTexObj->SetNumberField(TEXT("index"), BaseColorTextureIdx);
            BaseColorTexObj->SetNumberField(TEXT("texCoord"), 0);
            PbrObj->SetObjectField(TEXT("baseColorTexture"), BaseColorTexObj);
        }

        if (NormalTextureIdx != INDEX_NONE)
        {
            TSharedPtr<FJsonObject> NormalTexObj = MakeShared<FJsonObject>();
            NormalTexObj->SetNumberField(TEXT("index"), NormalTextureIdx);
            NormalTexObj->SetNumberField(TEXT("texCoord"), 0);
            MatObj->SetObjectField(TEXT("normalTexture"), NormalTexObj);
        }

        MatObj->SetObjectField(TEXT("pbrMetallicRoughness"), PbrObj);
        if (Prim.bDoubleSided)
        {
            MatObj->SetBoolField(TEXT("doubleSided"), true);
        }
        MaterialsArray.Add(MakeShared<FJsonValueObject>(MatObj));

        const int32 CurrentMatIdx = MaterialIndexCounter++;

        // ── 2. Pack Indices into Binary Buffer ───────────────────────────────
        AlignBufferTo4Bytes();
        const int32 IndicesByteOffset = BinaryBuffer.Num();
        const int32 IndicesByteLength = Prim.Indices.Num() * sizeof(uint32);

        BinaryBuffer.Append(reinterpret_cast<const uint8*>(Prim.Indices.GetData()), IndicesByteLength);

        // BufferView for Indices
        const int32 IndicesBufferViewIdx = BufferViewsArray.Num();
        TSharedPtr<FJsonObject> IndicesBV = MakeShared<FJsonObject>();
        IndicesBV->SetNumberField(TEXT("buffer"), 0);
        IndicesBV->SetNumberField(TEXT("byteOffset"), IndicesByteOffset);
        IndicesBV->SetNumberField(TEXT("byteLength"), IndicesByteLength);
        IndicesBV->SetNumberField(TEXT("target"), 34963); // ELEMENT_ARRAY_BUFFER
        BufferViewsArray.Add(MakeShared<FJsonValueObject>(IndicesBV));

        // Accessor for Indices
        const int32 IndicesAccessorIdx = AccessorsArray.Num();
        TSharedPtr<FJsonObject> IndicesAcc = MakeShared<FJsonObject>();
        IndicesAcc->SetNumberField(TEXT("bufferView"), IndicesBufferViewIdx);
        IndicesAcc->SetNumberField(TEXT("byteOffset"), 0);
        IndicesAcc->SetNumberField(TEXT("componentType"), 5125); // UNSIGNED_INT
        IndicesAcc->SetNumberField(TEXT("count"), Prim.Indices.Num());
        IndicesAcc->SetStringField(TEXT("type"), TEXT("SCALAR"));
        AccessorsArray.Add(MakeShared<FJsonValueObject>(IndicesAcc));

        // ── 3. Pack Positions into Binary Buffer ─────────────────────────────
        AlignBufferTo4Bytes();
        const int32 PosByteOffset = BinaryBuffer.Num();
        TArray<FVector3f> Positions;
        Positions.Reserve(Prim.Vertices.Num());

        FVector3f MinPos(MAX_FLT, MAX_FLT, MAX_FLT);
        FVector3f MaxPos(-MAX_FLT, -MAX_FLT, -MAX_FLT);

        for (const FGLBVertex& V : Prim.Vertices)
        {
            Positions.Add(V.Position);
            MinPos.X = FMath::Min(MinPos.X, V.Position.X);
            MinPos.Y = FMath::Min(MinPos.Y, V.Position.Y);
            MinPos.Z = FMath::Min(MinPos.Z, V.Position.Z);
            MaxPos.X = FMath::Max(MaxPos.X, V.Position.X);
            MaxPos.Y = FMath::Max(MaxPos.Y, V.Position.Y);
            MaxPos.Z = FMath::Max(MaxPos.Z, V.Position.Z);
        }

        const int32 PosByteLength = Positions.Num() * sizeof(FVector3f);
        BinaryBuffer.Append(reinterpret_cast<const uint8*>(Positions.GetData()), PosByteLength);

        // BufferView for Positions
        const int32 PosBufferViewIdx = BufferViewsArray.Num();
        TSharedPtr<FJsonObject> PosBV = MakeShared<FJsonObject>();
        PosBV->SetNumberField(TEXT("buffer"), 0);
        PosBV->SetNumberField(TEXT("byteOffset"), PosByteOffset);
        PosBV->SetNumberField(TEXT("byteLength"), PosByteLength);
        PosBV->SetNumberField(TEXT("target"), 34962); // ARRAY_BUFFER
        BufferViewsArray.Add(MakeShared<FJsonValueObject>(PosBV));

        // Accessor for Positions (with min/max bounding box required by glTF standard)
        const int32 PosAccessorIdx = AccessorsArray.Num();
        TSharedPtr<FJsonObject> PosAcc = MakeShared<FJsonObject>();
        PosAcc->SetNumberField(TEXT("bufferView"), PosBufferViewIdx);
        PosAcc->SetNumberField(TEXT("byteOffset"), 0);
        PosAcc->SetNumberField(TEXT("componentType"), 5126); // FLOAT
        PosAcc->SetNumberField(TEXT("count"), Positions.Num());
        PosAcc->SetStringField(TEXT("type"), TEXT("VEC3"));

        TArray<TSharedPtr<FJsonValue>> MinArr, MaxArr;
        MinArr.Add(MakeShared<FJsonValueNumber>(MinPos.X)); MinArr.Add(MakeShared<FJsonValueNumber>(MinPos.Y)); MinArr.Add(MakeShared<FJsonValueNumber>(MinPos.Z));
        MaxArr.Add(MakeShared<FJsonValueNumber>(MaxPos.X)); MaxArr.Add(MakeShared<FJsonValueNumber>(MaxPos.Y)); MaxArr.Add(MakeShared<FJsonValueNumber>(MaxPos.Z));
        PosAcc->SetArrayField(TEXT("min"), MinArr);
        PosAcc->SetArrayField(TEXT("max"), MaxArr);
        AccessorsArray.Add(MakeShared<FJsonValueObject>(PosAcc));

        // ── 4. Pack Normals into Binary Buffer ───────────────────────────────
        AlignBufferTo4Bytes();
        const int32 NormByteOffset = BinaryBuffer.Num();
        TArray<FVector3f> Normals;
        Normals.Reserve(Prim.Vertices.Num());
        for (const FGLBVertex& V : Prim.Vertices)
        {
            Normals.Add(V.Normal);
        }

        const int32 NormByteLength = Normals.Num() * sizeof(FVector3f);
        BinaryBuffer.Append(reinterpret_cast<const uint8*>(Normals.GetData()), NormByteLength);

        // BufferView for Normals
        const int32 NormBufferViewIdx = BufferViewsArray.Num();
        TSharedPtr<FJsonObject> NormBV = MakeShared<FJsonObject>();
        NormBV->SetNumberField(TEXT("buffer"), 0);
        NormBV->SetNumberField(TEXT("byteOffset"), NormByteOffset);
        NormBV->SetNumberField(TEXT("byteLength"), NormByteLength);
        NormBV->SetNumberField(TEXT("target"), 34962); // ARRAY_BUFFER
        BufferViewsArray.Add(MakeShared<FJsonValueObject>(NormBV));

        // Accessor for Normals
        const int32 NormAccessorIdx = AccessorsArray.Num();
        TSharedPtr<FJsonObject> NormAcc = MakeShared<FJsonObject>();
        NormAcc->SetNumberField(TEXT("bufferView"), NormBufferViewIdx);
        NormAcc->SetNumberField(TEXT("byteOffset"), 0);
        NormAcc->SetNumberField(TEXT("componentType"), 5126); // FLOAT
        NormAcc->SetNumberField(TEXT("count"), Normals.Num());
        NormAcc->SetStringField(TEXT("type"), TEXT("VEC3"));
        AccessorsArray.Add(MakeShared<FJsonValueObject>(NormAcc));

        // ── 5. Pack UVs into Binary Buffer ───────────────────────────────────
        AlignBufferTo4Bytes();
        const int32 UVByteOffset = BinaryBuffer.Num();
        TArray<FVector2f> UVs;
        UVs.Reserve(Prim.Vertices.Num());
        for (const FGLBVertex& V : Prim.Vertices)
        {
            UVs.Add(V.UV);
        }

        const int32 UVByteLength = UVs.Num() * sizeof(FVector2f);
        BinaryBuffer.Append(reinterpret_cast<const uint8*>(UVs.GetData()), UVByteLength);

        // BufferView for UVs
        const int32 UVBufferViewIdx = BufferViewsArray.Num();
        TSharedPtr<FJsonObject> UVBV = MakeShared<FJsonObject>();
        UVBV->SetNumberField(TEXT("buffer"), 0);
        UVBV->SetNumberField(TEXT("byteOffset"), UVByteOffset);
        UVBV->SetNumberField(TEXT("byteLength"), UVByteLength);
        UVBV->SetNumberField(TEXT("target"), 34962); // ARRAY_BUFFER
        BufferViewsArray.Add(MakeShared<FJsonValueObject>(UVBV));

        // Accessor for UVs
        const int32 UVAccessorIdx = AccessorsArray.Num();
        TSharedPtr<FJsonObject> UVAcc = MakeShared<FJsonObject>();
        UVAcc->SetNumberField(TEXT("bufferView"), UVBufferViewIdx);
        UVAcc->SetNumberField(TEXT("byteOffset"), 0);
        UVAcc->SetNumberField(TEXT("componentType"), 5126); // FLOAT
        UVAcc->SetNumberField(TEXT("count"), UVs.Num());
        UVAcc->SetStringField(TEXT("type"), TEXT("VEC2"));
        AccessorsArray.Add(MakeShared<FJsonValueObject>(UVAcc));

        // ── 6. Create Mesh & Primitive ───────────────────────────────────────
        TSharedPtr<FJsonObject> MeshObj = MakeShared<FJsonObject>();
        MeshObj->SetStringField(TEXT("name"), Prim.MeshName);

        TArray<TSharedPtr<FJsonValue>> PrimsArray;
        TSharedPtr<FJsonObject> PrimEntry = MakeShared<FJsonObject>();

        TSharedPtr<FJsonObject> AttributesObj = MakeShared<FJsonObject>();
        AttributesObj->SetNumberField(TEXT("POSITION"), PosAccessorIdx);
        AttributesObj->SetNumberField(TEXT("NORMAL"), NormAccessorIdx);
        AttributesObj->SetNumberField(TEXT("TEXCOORD_0"), UVAccessorIdx);

        PrimEntry->SetObjectField(TEXT("attributes"), AttributesObj);
        PrimEntry->SetNumberField(TEXT("indices"), IndicesAccessorIdx);
        PrimEntry->SetNumberField(TEXT("material"), CurrentMatIdx);
        PrimEntry->SetNumberField(TEXT("mode"), 4); // TRIANGLES
        PrimsArray.Add(MakeShared<FJsonValueObject>(PrimEntry));

        MeshObj->SetArrayField(TEXT("primitives"), PrimsArray);
        const int32 MeshIdx = MeshesArray.Num();
        MeshesArray.Add(MakeShared<FJsonValueObject>(MeshObj));

        // ── 7. Create Scene Node ─────────────────────────────────────────────
        TSharedPtr<FJsonObject> NodeObj = MakeShared<FJsonObject>();
        NodeObj->SetStringField(TEXT("name"), Prim.MeshName);
        NodeObj->SetNumberField(TEXT("mesh"), MeshIdx);

        const int32 NodeIdx = NodesArray.Num();
        NodesArray.Add(MakeShared<FJsonValueObject>(NodeObj));
        SceneNodesArray.Add(MakeShared<FJsonValueNumber>(NodeIdx));
    }

    AlignBufferTo4Bytes();

    // ── 8. Assemble Root glTF JSON Object ────────────────────────────────────
    TSharedPtr<FJsonObject> RootObj = MakeShared<FJsonObject>();

    // Asset metadata
    TSharedPtr<FJsonObject> AssetObj = MakeShared<FJsonObject>();
    AssetObj->SetStringField(TEXT("version"), TEXT("2.0"));
    AssetObj->SetStringField(TEXT("generator"), TEXT("MaxiMall UE5 AR Exporter"));
    RootObj->SetObjectField(TEXT("asset"), AssetObj);

    // Scene & Nodes
    RootObj->SetNumberField(TEXT("scene"), 0);
    TArray<TSharedPtr<FJsonValue>> ScenesArray;
    TSharedPtr<FJsonObject> Scene0 = MakeShared<FJsonObject>();
    Scene0->SetStringField(TEXT("name"), TEXT("ShowroomScene"));
    Scene0->SetArrayField(TEXT("nodes"), SceneNodesArray);
    ScenesArray.Add(MakeShared<FJsonValueObject>(Scene0));
    RootObj->SetArrayField(TEXT("scenes"), ScenesArray);

    RootObj->SetArrayField(TEXT("nodes"), NodesArray);
    RootObj->SetArrayField(TEXT("meshes"), MeshesArray);
    RootObj->SetArrayField(TEXT("materials"), MaterialsArray);
    RootObj->SetArrayField(TEXT("accessors"), AccessorsArray);
    RootObj->SetArrayField(TEXT("bufferViews"), BufferViewsArray);

    // Texture resources (glTF forbids empty top-level arrays, so add them only when used)
    if (TexturesArray.Num() > 0)
    {
        TArray<TSharedPtr<FJsonValue>> SamplersArray;
        TSharedPtr<FJsonObject> Sampler0 = MakeShared<FJsonObject>();
        Sampler0->SetNumberField(TEXT("magFilter"), 9729);  // LINEAR
        Sampler0->SetNumberField(TEXT("minFilter"), 9987);  // LINEAR_MIPMAP_LINEAR
        Sampler0->SetNumberField(TEXT("wrapS"), 10497);     // REPEAT
        Sampler0->SetNumberField(TEXT("wrapT"), 10497);     // REPEAT
        SamplersArray.Add(MakeShared<FJsonValueObject>(Sampler0));
        RootObj->SetArrayField(TEXT("samplers"), SamplersArray);
        RootObj->SetArrayField(TEXT("images"), ImagesArray);
        RootObj->SetArrayField(TEXT("textures"), TexturesArray);
    }

    // Buffers array
    TArray<TSharedPtr<FJsonValue>> BuffersArray;
    TSharedPtr<FJsonObject> Buffer0 = MakeShared<FJsonObject>();
    Buffer0->SetNumberField(TEXT("byteLength"), BinaryBuffer.Num());
    BuffersArray.Add(MakeShared<FJsonValueObject>(Buffer0));
    RootObj->SetArrayField(TEXT("buffers"), BuffersArray);

    // Serialize JSON to String
    FString JsonString;
    TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
        TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&JsonString);
    FJsonSerializer::Serialize(RootObj.ToSharedRef(), Writer);

    // Convert JSON to UTF-8 Bytes
    FTCHARToUTF8 Utf8Json(*JsonString);
    TArray<uint8> JsonBytes;
    JsonBytes.Append(reinterpret_cast<const uint8*>(Utf8Json.Get()), Utf8Json.Length());

    // Pad JSON Chunk to 4-byte alignment with spaces (0x20)
    while (JsonBytes.Num() % 4 != 0)
    {
        JsonBytes.Add(0x20);
    }

    // ── 9. Write glTF 2.0 Binary (.glb) Header & Chunks ──────────────────────
    const uint32 Magic = 0x46546C67; // "glTF"
    const uint32 Version = 2;
    const uint32 HeaderSize = 12;
    const uint32 JsonChunkHeaderSize = 8;
    const uint32 BinChunkHeaderSize = 8;
    const uint32 TotalLength = HeaderSize + JsonChunkHeaderSize + JsonBytes.Num() + BinChunkHeaderSize + BinaryBuffer.Num();

    OutGLBData.Empty(TotalLength);

    // 12-byte File Header
    auto AppendUint32 = [&OutGLBData](uint32 Value)
    {
        OutGLBData.Append(reinterpret_cast<const uint8*>(&Value), sizeof(uint32));
    };

    AppendUint32(Magic);
    AppendUint32(Version);
    AppendUint32(TotalLength);

    // Chunk 0: JSON Chunk
    const uint32 JsonChunkType = 0x4E4F534A; // "JSON"
    AppendUint32(static_cast<uint32>(JsonBytes.Num()));
    AppendUint32(JsonChunkType);
    OutGLBData.Append(JsonBytes);

    // Chunk 1: BIN Chunk
    const uint32 BinChunkType = 0x004E4942; // "BIN\0"
    AppendUint32(static_cast<uint32>(BinaryBuffer.Num()));
    AppendUint32(BinChunkType);
    OutGLBData.Append(BinaryBuffer);

    return true;
}
