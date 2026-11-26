#include "PartyVoxelSourceArchive.h"

#include "PartyMeshVoxelData.h"
#include "Engine/StaticMesh.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/MemoryReader.h"
#if WITH_EDITOR
#include "Serialization/MemoryWriter.h"
#endif

namespace
{
    constexpr uint32 Format = 0x49565401; // Island voxel triangles, version 1
    constexpr int32 MaxAssets = 4096;
    constexpr int32 MaxTriangles = 1 << 20;
    FString ArchivePath()
    {
        return FPaths::ProjectContentDir() / TEXT("TreasureIsland/VoxelSources/SceneryTriangles.bin");
    }

    void SerializeTriangle(FArchive& Ar, FIslandVoxelTri& Tri)
    {
        Ar << Tri.Material;
        for (auto& Vertex : Tri.V)
        {
            FVector3f P(Vertex.Position), N(Vertex.Normal), T(Vertex.Tangent.TangentX);
            FVector2f UV(Vertex.UV0);
            uint8 Flip = Vertex.Tangent.bFlipTangentY;
            Ar << P << N << T << UV << Vertex.Color << Flip;
            if (Ar.IsLoading())
            {
                Vertex.Position = FVector(P);
                Vertex.Normal = FVector(N);
                Vertex.Tangent = FProcMeshTangent(FVector(T), Flip != 0);
                Vertex.UV0 = FVector2D(UV);
            }
        }
    }

    using FSourceLibrary = TMap<FString, TArray<FIslandVoxelTri>>;

    FSourceLibrary LoadLibrary()
    {
        TArray<uint8> File;
        if (!FFileHelper::LoadFileToArray(File, *ArchivePath()))
        {
            UE_LOG(LogTemp, Error, TEXT("Unable to load expedition voxel triangles from %s"), *ArchivePath());
            return {};
        }
        FMemoryReader Reader(File, true);
        uint32 Version = 0;
        int32 Entries = 0;
        Reader << Version << Entries;
        if (Version != Format || Entries <= 0 || Entries > MaxAssets) return {};
        FSourceLibrary Library;
        for (int32 I = 0; I < Entries; ++I)
        {
            FString Key;
            int32 Num = 0;
            Reader << Key << Num;
            // Each vertex contains eleven floats and five bytes of color / tangent flags.
            constexpr int32 BytesPerTriangle = sizeof(int32) + 3 * (11 * sizeof(float) + 5);
            if (Reader.IsError() || Key.IsEmpty() || Library.Contains(Key) || Num <= 0 || Num > MaxTriangles ||
                int64(Num) * BytesPerTriangle > Reader.TotalSize() - Reader.Tell()) return {};
            auto& Triangles = Library.Add(Key);
            Triangles.SetNum(Num);
            for (auto& Tri : Triangles) SerializeTriangle(Reader, Tri);
        }
        if (Reader.IsError() || !Reader.AtEnd()) return {};
        UE_LOG(LogTemp, Display, TEXT("ISLAND_VOXEL_SOURCE_ARCHIVE meshes=%d"), Library.Num());
        return Library;
    }
}

bool ReadIslandVoxelArchive(const UStaticMesh& Asset, TArray<FIslandVoxelTri>& Out)
{
    static const FSourceLibrary Library = LoadLibrary();
    if (const auto* Triangles = Library.Find(Asset.GetPathName()))
    {
        Out = *Triangles;
        return true;
    }
    return false;
}

#if WITH_EDITOR
bool WriteIslandVoxelSourceArchive(TArray<UStaticMesh*> Assets)
{
    if (Assets.IsEmpty() || Assets.Num() > MaxAssets || Assets.Contains(nullptr)) return false;
    Assets.Sort([](const UStaticMesh& A, const UStaticMesh& B) { return A.GetPathName() < B.GetPathName(); });
    TArray<uint8> File;
    FMemoryWriter Writer(File, true);
    uint32 Version = Format;
    int32 Count = Assets.Num();
    Writer << Version << Count;
    for (const UStaticMesh* Mesh : Assets)
    {
        TArray<FIslandVoxelTri> Triangles;
        if (!ReadIslandVoxelRenderSource(*Mesh, Triangles) || Triangles.Num() > MaxTriangles)
        {
            UE_LOG(LogTemp, Error, TEXT("Missing LOD0 triangles on %s"), *Mesh->GetPathName());
            return false;
        }
        FString Key = Mesh->GetPathName();
        int32 Num = Triangles.Num();
        Writer << Key << Num;
        for (auto& Tri : Triangles) SerializeTriangle(Writer, Tri);
    }
    if (Writer.IsError()) return false;
    const FString Path = ArchivePath();
    TArray<uint8> Previous;
    if (FPaths::FileExists(Path) && FFileHelper::LoadFileToArray(Previous, *Path) && Previous == File) return true;
    if (!IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true)) return false;
    const FString Temp = FPaths::CreateTempFilename(*FPaths::GetPath(Path), TEXT("SceneryTriangles"), TEXT(".tmp"));
    if (!FFileHelper::SaveArrayToFile(File, *Temp) || !IFileManager::Get().Move(*Path, *Temp, true))
    {
        IFileManager::Get().Delete(*Temp);
        return false;
    }
    return true;
}
#endif
