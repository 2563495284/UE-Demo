#include "ExpeditionSceneryArt.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "EngineUtils.h"
#include "Island/ExpeditionDock.h"
#include "Materials/MaterialInterface.h"
#include "ProceduralMeshComponent.h"
#include "Voxel/VoxelTerrain.h"

namespace
{
    bool Dress(UStaticMeshComponent& Part, const TCHAR* AssetName)
    {
        const FString Path = FString::Printf(TEXT("/Game/TreasureIsland/Art/Harbour/SM_%s.SM_%s"), AssetName, AssetName);
        UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, *Path);
        if (!Mesh)
        {
            UE_LOG(LogTemp, Error, TEXT("Missing harbour scenery mesh %s"), *Path);
            return false;
        }
        Part.Modify();
        Part.SetStaticMesh(Mesh);
        Part.EmptyOverrideMaterials();
        Part.SetReceivesDecals(false);
        return true;
    }

    const TCHAR* LandmarkMesh(const UStaticMeshComponent& Part)
    {
        const FString Name = Part.GetName();
        if (Name.StartsWith(TEXT("WestPier")) || Name.StartsWith(TEXT("EastPier"))) return TEXT("ArchStone");
        if (Name.StartsWith(TEXT("Lintel"))) return TEXT("ArchLintel");
        if (Name.StartsWith(TEXT("Heel"))) return TEXT("FossilHeel");
        if (Name.StartsWith(TEXT("TallNeedle"))) return TEXT("SignalNeedle");
        if (Name.StartsWith(TEXT("ShortNeedle"))) return TEXT("SignalMast");
        if (Name.StartsWith(TEXT("Crown"))) return TEXT("SignalDish");
        if (Name.StartsWith(TEXT("Bench")))
            return Part.GetRelativeScale3D().Z > 5.0 ? TEXT("QuarryDerrick") : TEXT("QuarryStep");
        if (Name.StartsWith(TEXT("Blade"))) return TEXT("Fin");
        return nullptr;
    }

    const TCHAR* HarbourMesh(const FString& Label)
    {
        if (Label.StartsWith(TEXT("Jetty | board"))) return TEXT("JettyBoard");
        if (Label.StartsWith(TEXT("Jetty | mooring pile")) || Label == TEXT("Dock crane | hoist")) return TEXT("Mooring");
        if (Label == TEXT("Dock crane | arm")) return TEXT("Plank");
        if (Label == TEXT("Dock crane | mast")) return TEXT("CraneMast");
        if (Label == TEXT("Dock crane | cargo") || Label.StartsWith(TEXT("Stacked cargo"))) return TEXT("Crate");
        if (Label.StartsWith(TEXT("Far island | stratum"))) return TEXT("Islet");
        if (Label == TEXT("Far island | arch lintel")) return TEXT("ArchLintel");
        if (Label.StartsWith(TEXT("Far island | arch"))) return TEXT("ArchStone");
        return nullptr;
    }
}

bool ExpeditionSceneryArt::DressIsland(UWorld& World)
{
    int32 Landings = 0, Landmarks = 0, Markers = 0;
    for (TActorIterator<AExpeditionDock> It(&World); It; ++It)
    {
        if (!Dress(It->GetMesh(), TEXT("Landing"))) return false;
        ++Landings;
    }
    for (TActorIterator<AExpeditionLandmark> It(&World); It; ++It)
    {
        TInlineComponentArray<UStaticMeshComponent*> Parts(*It);
        for (UStaticMeshComponent* Part : Parts)
        {
            if (const TCHAR* Art = LandmarkMesh(*Part))
            {
                if (!Dress(*Part, Art)) return false;
                ++Landmarks;
            }
        }
    }
    for (TActorIterator<AStaticMeshActor> It(&World); It; ++It)
    {
        const FString Label = It->GetActorLabel();
        const TCHAR* Art = nullptr;
        if (Label.StartsWith(TEXT("Coast trail cairn"))) Art = TEXT("Cairn");
        if (Label.StartsWith(TEXT("Inner trail beacon"))) Art = TEXT("TrailBeacon");
        if (Art)
        {
            if (!Dress(*It->GetStaticMeshComponent(), Art)) return false;
            ++Markers;
        }
    }
    // Upgrade only the stock override on saved tiles. Terrain authors can still paint their own tiles.
    auto* Ground = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/TreasureIsland/FX/M_IslandGround.M_IslandGround"));
    if (!Ground) return false;
    for (TActorIterator<AVoxelTerrain> It(&World); It; ++It)
    {
        TInlineComponentArray<UProceduralMeshComponent*> Tiles(*It);
        for (auto* Tile : Tiles)
        {
            const auto* Current = Tile->GetMaterial(0);
            if (Current && Current->GetPathName() == TEXT("/Game/LevelPrototyping/Materials/MI_PrototypeGrid_Gray.MI_PrototypeGrid_Gray"))
            {
                Tile->Modify();
                Tile->SetMaterial(0, Ground);
            }
        }
    }
    UE_LOG(LogTemp, Display, TEXT("EXPEDITION_SCENERY_APPLIED landings=%d landmark_parts=%d trail_markers=%d"), Landings, Landmarks, Markers);
    return true;
}

bool ExpeditionSceneryArt::DressHarbour(UWorld& World)
{
    int32 Pieces = 0;
    for (TActorIterator<AStaticMeshActor> It(&World); It; ++It)
    {
        const FString Label = It->GetActorLabel();
        if (Label == TEXT("Harbour water"))
        {
            auto* Water = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/TreasureIsland/Art/M_Lagoon.M_Lagoon"));
            if (!Water) return false;
            auto* Surface = It->GetStaticMeshComponent();
            Surface->Modify();
            Surface->SetMaterial(0, Water);
        }
        else if (const TCHAR* Art = HarbourMesh(Label))
        {
            if (!Dress(*It->GetStaticMeshComponent(), Art)) return false;
            ++Pieces;
        }
    }
    UE_LOG(LogTemp, Display, TEXT("EXPEDITION_HARBOUR_APPLIED pieces=%d"), Pieces);
    return true;
}
