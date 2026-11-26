#include "ActIslandVoxelSourcesCommandlet.h"

#include "Engine/StaticMesh.h"
#include "HAL/FileManager.h"
#include "Island/IslandVoxelSourceArchive.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"

UActIslandVoxelSourcesCommandlet::UActIslandVoxelSourcesCommandlet()
{
    IsClient = IsServer = false;
    IsEditor = true;
    LogToConsole = true;
}

bool UActIslandVoxelSourcesCommandlet::RebuildSourceArchive()
{
    // Enumerate the same source families the art authoring script retains. The
    // archive must be complete before it replaces the known-good cooked input.
    struct FFamily
    {
        const TCHAR* Folder;
        const TCHAR* Prefix;
    };
    constexpr FFamily Families[] = {
        {TEXT("/Game/TreasureIsland/Art/Harbour"), TEXT("SM_")},
        {TEXT("/Game/TreasureIsland/Art/Neutral"), TEXT("SM_")},
        {TEXT("/Game/TreasureIsland/Art/Crew"), TEXT("SM_Coffer")}
    };
    TArray<UStaticMesh*> Meshes;
    int32 Counts[UE_ARRAY_COUNT(Families)] = {};
    for (int32 FamilyIndex = 0; FamilyIndex < UE_ARRAY_COUNT(Families); ++FamilyIndex)
    {
        const FFamily& Family = Families[FamilyIndex];
        const FString Directory = FPackageName::LongPackageNameToFilename(Family.Folder);
        const FString Glob = FPaths::Combine(Directory, FString(Family.Prefix) + TEXT("*.uasset"));
        TArray<FString> Filenames;
        IFileManager::Get().FindFiles(Filenames, *Glob, true, false);
        if (Filenames.IsEmpty())
        {
            UE_LOG(LogTemp,Error,TEXT("Voxel archive has no source meshes matching %s"),*Glob);
            return false;
        }
        Counts[FamilyIndex] = Filenames.Num();
        Filenames.Sort();
        for (const FString& Filename : Filenames)
        {
            const FString Name = FPaths::GetBaseFilename(Filename);
            const FString ObjectPath = FString::Printf(TEXT("%s/%s.%s"), Family.Folder, *Name, *Name);
            UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr,*ObjectPath);
            if (!Mesh)
            {
                UE_LOG(LogTemp,Error,TEXT("Cannot load voxel archive mesh %s"),*ObjectPath);
                return false;
            }
            Meshes.Add(Mesh);
        }
    }
    const int32 Total = Meshes.Num();
    if (!WriteIslandVoxelSourceArchive(MoveTemp(Meshes))) return false;
    UE_LOG(LogTemp,Display,TEXT("ISLAND_VOXEL_SOURCES_READY harbour=%d neutral=%d coffer=%d total=%d"),
        Counts[0],Counts[1],Counts[2],Total);
    return true;
}

int32 UActIslandVoxelSourcesCommandlet::Main(const FString& Params)
{
    return RebuildSourceArchive() ? 0 : 1;
}
