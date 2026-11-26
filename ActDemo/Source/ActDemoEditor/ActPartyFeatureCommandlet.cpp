#include "ActPartyFeatureCommandlet.h"
#include "IslandPassageMaterial.h"
#include "PartyVenueScenery.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Level.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "FileHelpers.h"
#include "HAL/FileManager.h"
#include "Interfaces/IPluginManager.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Misc/PackageName.h"
#include "Modules/ModuleManager.h"
#include "PartyMinigame.h"
#include "UObject/SavePackage.h"

namespace PartyMapAuthoring
{
    struct FVenue
    {
        const TCHAR* Name;
        FColor Color;
    };

    // Restyle the original scenery in place so authored transforms and demolition groups survive.
    bool DressVenue(UWorld& World)
    {
        struct FPieceStyle { const TCHAR* Label; const TCHAR* Mesh; bool bUseMeshPaint; };
        static const FPieceStyle Styles[] = {
            {TEXT("Island shelf"), TEXT("Harbour/SM_ReefShelf"), true},
            {TEXT("Coral spur"), TEXT("Neutral/SM_CrystalSpire"), false},
            {TEXT("Coral arch"), TEXT("Harbour/SM_ArchStone"), true},
            {TEXT("Lighthouse shaft"), TEXT("Neutral/SM_TimberPost"), false},
            {TEXT("Lighthouse band"), TEXT("Neutral/SM_TimberPost"), false},
            {TEXT("Lantern room"), TEXT("Neutral/SM_TimberPost"), false},
            {TEXT("Lantern cap"), TEXT("Neutral/SM_CrystalSpire"), false},
            {TEXT("Crystal spire"), TEXT("Neutral/SM_CrystalSpire"), false},
            {TEXT("Viaduct pier"), TEXT("Harbour/SM_ArchStone"), true},
            {TEXT("Broken viaduct"), TEXT("Harbour/SM_Plank"), true},
            {TEXT("Sluice tower"), TEXT("Harbour/SM_QuarryStep"), true},
            {TEXT("Tide gate"), TEXT("Harbour/SM_Plank"), true},
            {TEXT("Watchtower"), TEXT("Neutral/SM_TimberPost"), false},
            {TEXT("Lookout"), TEXT("Harbour/SM_Mooring"), true},
            {TEXT("Flagpole"), TEXT("Harbour/SM_Mooring"), true},
            {TEXT("Pennant"), TEXT("Neutral/SM_HewnBlock"), false},
            {TEXT("Gate pier"), TEXT("Harbour/SM_ArchStone"), true},
            {TEXT("Gate lintel"), TEXT("Harbour/SM_Plank"), true}
        };
        auto* Sea = LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/TreasureIsland/Art/M_Lagoon"));
        if (!Sea) return false;
        for (AActor* Actor : World.PersistentLevel->Actors)
        {
            auto* Scenery = Cast<AStaticMeshActor>(Actor);
            if (!Scenery) continue;
            auto* Part = Scenery->GetStaticMeshComponent();
            if (!Part->GetStaticMesh() || !Part->GetStaticMesh()->GetPathName().StartsWith(TEXT("/Engine/BasicShapes/"))) continue;
            const FString Label = Actor->GetActorLabel();
            if (Label == TEXT("Lagoon"))
            {
                Part->SetMaterial(0,Sea);
                continue;
            }
            for (const FPieceStyle& Style : Styles)
            {
                if (Label != Style.Label) continue;
                auto* Mesh = LoadObject<UStaticMesh>(nullptr,*(FString(TEXT("/Game/TreasureIsland/Art/")) + Style.Mesh));
                if (!Mesh) return false;
                Part->SetStaticMesh(Mesh);
                if (Style.bUseMeshPaint) Part->SetMaterial(0, Mesh->GetMaterial(0));
                if (Label == TEXT("Island shelf")) Actor->Tags.AddUnique(PartyVenueScenery::ShelfTag);
                break;
            }
        }
        return true;
    }

    // Everything inside the playable board, including all goal markers, comes
    // from the feature rules. The map owns the scenery and its demolition groups.
    bool SaveVenue(const FString& MapPath, const FVenue& Venue, int32 Style)
    {
        UWorld* World = UEditorLoadingAndSavingUtils::NewBlankMap(false);
        if (!World) return false;
        auto* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube"));
        auto* Cylinder = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cylinder"));
        auto* Cone = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cone"));
        auto* Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial"));
        if (!Cube || !Cylinder || !Cone || !Base) return false;
        const auto Paint = [&](const TCHAR* Name, FLinearColor Color)
        {
            auto* Mat = NewObject<UMaterialInstanceConstant>(World->PersistentLevel, Name, RF_Transactional);
            Mat->SetParentEditorOnly(Base);
            Mat->SetVectorParameterValueEditorOnly(TEXT("Color"), Color);
            Mat->PostEditChange();
            return Mat;
        };
        auto* Accent = Paint(TEXT("VenueColor"), FLinearColor(Venue.Color));
        auto* Stone = Paint(TEXT("Basalt"), FLinearColor(FColor(61,80,90)));
        auto* Pale = Paint(TEXT("Foam"), FLinearColor(FColor(210,222,210)));
        int32 CurrentIsland = INDEX_NONE;
        const auto Shape = [&](const TCHAR* Name, UStaticMesh* Mesh, UMaterialInterface* Mat, FVector Center, FVector Size, FRotator Rot = FRotator::ZeroRotator)
        {
            auto* Prop=World->SpawnActor<AStaticMeshActor>(Center,Rot);
            Prop->SetActorLabel(Name);
            if (CurrentIsland == INDEX_NONE)
                Prop->Tags.Add(PartyVenueScenery::WaterTag);
            else
            {
                Prop->Tags.Add(PartyVenueScenery::GroupTag(CurrentIsland));
                if (FCString::Strcmp(Name, TEXT("Island shelf")) == 0)
                    Prop->Tags.Add(PartyVenueScenery::ShelfTag);
            }
            auto* Part=Prop->GetStaticMeshComponent();
            Part->SetStaticMesh(Mesh);
            Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
            Part->SetMaterial(0, Mat);
            Prop->SetActorScale3D(Size/100.);
        };
        Shape(TEXT("Lagoon"),Cylinder,Accent,{0,0,-1950},{24500,24500,180});
        for (int32 I=0; I<10; ++I)
        {
            CurrentIsland = I;
            const double Yaw=I*36.+9.*Style;
            const FVector Out=FRotator(0,Yaw,0).Vector();
            const FVector Side=FVector::CrossProduct(FVector::UpVector,Out);
            const FVector P=Out*(6600.+(I%3)*350.);
            const FRotator Face(0,Yaw,0);
            Shape(TEXT("Island shelf"),Cylinder,Stone,P-FVector(0,0,840),{1750,1420,1320},Face);
            switch (Style)
            {
            case 0: // An open reef arch
                for (double Sign : {-1.,1.}) Shape(TEXT("Coral spur"),Cube,Accent,P+Side*370.*Sign+FVector(0,0,290),{220,215,1180},FRotator(0,Yaw,Sign*12.));
                Shape(TEXT("Coral arch"),Cube,Pale,P+FVector(0,0,820),{190,730,160},Face);
                break;
            case 1: // A striped lighthouse and lantern
                Shape(TEXT("Lighthouse shaft"),Cylinder,Pale,P+FVector(0,0,550),{530,530,1560});
                Shape(TEXT("Lighthouse band"),Cylinder,Accent,P+FVector(0,0,975),{542,542,220});
                Shape(TEXT("Lantern room"),Cylinder,Accent,P+FVector(0,0,1480),{390,390,300});
                Shape(TEXT("Lantern cap"),Cone,Stone,P+FVector(0,0,1790),{630,630,340});
                break;
            case 2:
                for (int32 J=-1;J<=1;++J)
                    Shape(TEXT("Crystal spire"),Cone,J==0?Pale:Accent,P+Side*J*290.+FVector(0,0,405),{410,450,1450.-260.*FMath::Abs(J)},FRotator(0,Yaw,J*22.));
                break;
            case 3:
                for (int32 J=-1;J<=1;++J) Shape(TEXT("Viaduct pier"),Cube,Stone,P+Side*(J*420.)+FVector(0,0,220),{190,180,1250},Face);
                Shape(TEXT("Broken viaduct"),Cube,Accent,P+FVector(0,0,860),{490,1580,180},FRotator(0,Yaw,6.));
                break;
            case 4:
                for (double Sign : {-1.,1.}) Shape(TEXT("Sluice tower"),Cube,Pale,P+Side*440.*Sign+FVector(0,0,325),{410,210,1200},Face);
                Shape(TEXT("Tide gate"),Cube,Accent,P+FVector(0,0,210),{150,800,790},Face);
                break;
            case 5:
                Shape(TEXT("Watchtower"),Cylinder,Stone,P+FVector(0,0,420),{570,570,1350});
                Shape(TEXT("Lookout"),Cylinder,Pale,P+FVector(0,0,1120),{720,720,120});
                Shape(TEXT("Flagpole"),Cylinder,Stone,P+FVector(0,0,1450),{45,45,650});
                Shape(TEXT("Pennant"),Cube,Accent,P+Side*185.+FVector(0,0,1660),{32,370,180},Face);
                break;
            case 6:
                // Broken gatehouses echo the maze without revealing its passages.
                for (double Sign : {-1.,1.})
                    Shape(TEXT("Gate pier"),Cube,Stone,P+Side*340.*Sign+FVector(0,0,300),{260,250,1150},Face);
                Shape(TEXT("Gate lintel"),Cube,Accent,P+FVector(0,0,900),{320,990,140},Face);
                break;
            }
        }
        return DressVenue(*World) && UEditorLoadingAndSavingUtils::SaveMap(World, MapPath);
    }
}

int32 UActPartyFeatureCommandlet::Main(const FString& Params)
{
    if (!IslandPassageMaterial::Save(FParse::Param(*Params, TEXT("RebuildPassageMaterial"))))
    {
        UE_LOG(LogTemp, Error, TEXT("Could not save island passage UI material"));
        return 1;
    }
    if (FParse::Param(*Params, TEXT("PassageOnly")))
    {
        UE_LOG(LogTemp, Display, TEXT("PARTY_FEATURE_ASSET_READY /Game/TreasureIsland/FX/M_IslandPassage"));
        return 0;
    }
    using namespace PartyMapAuthoring;
    const FVenue Venues[] = {
        {TEXT("ReefRun"),{219,111,82}}, {TEXT("BeaconClimb"),{245,173,70}},
        {TEXT("CrystalHunt"),{167,109,201}}, {TEXT("BridgeDash"),{105,174,129}},
        {TEXT("FloodGates"),{74,165,198}}, {TEXT("CrownHold"),{210,105,150}},
        {TEXT("MazeChase"),{100,157,137}}
    };
    for (int32 Index=-1; Index<static_cast<int32>(UE_ARRAY_COUNT(Venues)); ++Index)
    {
        const bool bArena=Index>=0;
        const FString Short=bArena ? Venues[Index].Name : TEXT("Button");
        const FString Plugin=bArena ? TEXT("Party")+Short : TEXT("PartyButtonChallenge");
        IPluginManager::Get().MountExplicitlyLoadedPlugin(Plugin);
        const FString DataPath=FString::Printf(TEXT("/%s/%s"),*Plugin,*Plugin);
        const FString MapPath=FString::Printf(TEXT("/%s/Maps/L_%s"),*Plugin,*Short);
        const FString MapFile=FPackageName::LongPackageNameToFilename(MapPath,FPackageName::GetMapPackageExtension());
        const FString DataFile=FPackageName::LongPackageNameToFilename(DataPath,FPackageName::GetAssetPackageExtension());
        IFileManager::Get().MakeDirectory(*FPaths::GetPath(DataFile),true);
        if (bArena && FPaths::FileExists(MapFile) && FParse::Param(*Params,TEXT("RestyleVenues")))
        {
            UWorld* World = UEditorLoadingAndSavingUtils::LoadMap(MapFile);
            if (!World || !DressVenue(*World) || !UEditorLoadingAndSavingUtils::SaveMap(World,MapPath)) return 1;
        }
        else if (bArena && (!FPaths::FileExists(MapFile) || FParse::Param(*Params,TEXT("RebuildVenues"))))
        {
            IFileManager::Get().MakeDirectory(*FPaths::GetPath(MapFile),true);
            if (!SaveVenue(MapPath,Venues[Index],Index)) return 1;
        }
        if (!FPaths::FileExists(DataFile))
        {
            FModuleManager::Get().LoadModule(*Plugin);
            const FString ClassName=bArena ? Short+TEXT("Session") : TEXT("PartyButtonSession");
            TSoftClassPtr<APartySession> Session(FSoftObjectPath(FString::Printf(TEXT("/Script/%s.%s"),*Plugin,*ClassName)));
            if (!Session.LoadSynchronous()) { UE_LOG(LogTemp,Error,TEXT("Missing session %s"),*Session.ToString()); return 1; }
            auto* Package=CreatePackage(*DataPath);
            auto* Data=NewObject<UPartyMinigameFeatureData>(Package,*Plugin,RF_Public|RF_Standalone);
            Data->SessionClass=Session;
            if (bArena) Data->ArenaMap=TSoftObjectPtr<UWorld>(FSoftObjectPath(MapPath+TEXT(".L_")+Short));
            FAssetRegistryModule::AssetCreated(Data);
            FSavePackageArgs Args;
            Args.TopLevelFlags=RF_Public|RF_Standalone;
            if (!UPackage::SavePackage(Package,Data,*DataFile,Args)) return 1;
        }
        UE_LOG(LogTemp,Display,TEXT("PARTY_FEATURE_ASSET_READY %s"),*DataPath);
    }
    return 0;
}
