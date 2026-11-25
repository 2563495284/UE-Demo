#if WITH_DEV_AUTOMATION_TESTS

#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Modules/ModuleManager.h"
#include "PartyDensityTerrain.h"
#include "PartyVoxelArena.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPartyArenaSupportTest, "ActDemo.Voxel.GroundedTerrain.Arena",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPartyArenaSupportTest::RunTest(const FString& Parameters)
{
    FModuleManager::Get().LoadModule(TEXT("PartyBeaconClimb"));
    UClass* ArenaType=FindObject<UClass>(nullptr,TEXT("/Script/PartyBeaconClimb.BeaconClimbSession"));
    if (!TestNotNull(TEXT("Beacon Climb rule class"),ArenaType)) return false;
    UWorld* World=UWorld::CreateWorld(EWorldType::Game,false);
    auto* Arena=World->SpawnActor<APartyVoxelSession>(ArenaType);
    if (!TestNotNull(TEXT("Beacon Climb arena"),Arena))
    {
        World->DestroyWorld(false);
        return false;
    }
    Arena->EnsureGrid();
    const FIntVector Deck(10,0,9);
    const auto HitsDeck = [&]()
    {
        FHitResult Hit;
        const FVector Center=Arena->CellCenter(Deck);
        return World->LineTraceSingleByChannel(Hit,Center+FVector(0,0,140.),Center-FVector(0,0,85.),ECC_Visibility)
            && Arena->Terrain->OwnsSurface(Hit.GetComponent());
    };
    TestTrue(TEXT("The two-pier deck has collision"),HitsDeck());
    const uint32 BeforeFill=Arena->Terrain->Checksum();
    const int32 BeforeEdits=Arena->GetAppliedEditCount();
    FPartyVoxelChange Brush;
    Brush.Center=FIntVector(-350,0,660);
    Brush.bFill=true;
    TestFalse(TEXT("Detached soil is rejected"),Arena->CommitBrush(Brush));
    Brush.ProtectedMin.Add(Brush.Center-FIntVector(500));
    Brush.ProtectedMax.Add(Brush.Center+FIntVector(500,500,40));
    TestFalse(TEXT("A clipped stroke that only changes air is rejected"),Arena->CommitBrush(Brush));
    TestEqual(TEXT("Rejected soil preserves the density and material digest"),Arena->Terrain->Checksum(),BeforeFill);
    TestEqual(TEXT("Rejected soil uses no replication sequence"),Arena->GetAppliedEditCount(),BeforeEdits);

    Brush=FPartyVoxelChange();
    Brush.Center=FIntVector(1050,-750,550);
    TestTrue(TEXT("Cut the first pillar"),Arena->CommitBrush(Brush));
    TestTrue(TEXT("Second pillar still supports the whole platform"),HitsDeck());
    Brush.Center.Y=850;
    TestTrue(TEXT("Cut the second pillar"),Arena->CommitBrush(Brush));
    TestTrue(TEXT("Remote platform material released"),Arena->BlockAt(Deck)==EPartyBlock::Air);
    TestFalse(TEXT("Remote platform collision released"),HitsDeck());
    TestEqual(TEXT("Both cuts used one replicated edit each"),Arena->GetAppliedEditCount(),BeforeEdits+2);

    for (const int32 Height : {100,60,25,0})
    {
        Brush.Center=FIntVector(-650,-650,Height);
        Arena->CommitBrush(Brush);
    }
    TestTrue(TEXT("Berth retains footing after its surface is excavated"),
        Arena->TerrainDensityAt(FVector(-650,-650,25))<0.);

    // A bridge course may have banks above recessed pier foundations.
    AActor* Owner=World->SpawnActor<AActor>();
    auto* Terrain=NewObject<UPartyDensityTerrain>(Owner);
    TMap<FIntVector,EPartyBlock> Cells;
    for (const FIntVector Cell : {FIntVector(0,0,0),FIntVector(2,0,-3),FIntVector(4,0,3)})
        Cells.Add(Cell,EPartyBlock::Stone);
    Terrain->Initialize(Cells,FColor::White,{});
    TestTrue(TEXT("Original bank remains solid"),Terrain->DensityAt(FVector(50,50,50))<0.);
    TestTrue(TEXT("Recessed foundation remains solid"),Terrain->DensityAt(FVector(250,50,-250))<0.);
    TestTrue(TEXT("An authored floating island is removed before play"),Terrain->DensityAt(FVector(450,50,350))>0.);
    World->DestroyWorld(false);
    return !HasAnyErrors();
}

#endif
