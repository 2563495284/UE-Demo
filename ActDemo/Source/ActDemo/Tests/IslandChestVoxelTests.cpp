#if WITH_DEV_AUTOMATION_TESTS
#include "Components/BoxComponent.h"
#include "Engine/World.h"
#include "Island/IslandSceneryVoxels.h"
#include "Island/IslandWorld.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Voxel/VoxelCharacter.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FIslandChestVoxelTest, "ActDemo.Voxel.RouteScenery.ChestAndCompass",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FIslandChestVoxelTest::RunTest(const FString&)
{
    // Use the live registration path in an isolated world whose map selects expedition rules.
    const FString PkgName = FString::Printf(TEXT("/Temp/Chest_%s_L_ExpeditionIsland"), *FGuid::NewGuid().ToString());
    UWorld* World = UWorld::CreateWorld(EWorldType::Game,false,TEXT("L_ExpeditionIsland"),CreatePackage(*PkgName));
    if (!TestNotNull(TEXT("Test world"),World)) return false;
    ON_SCOPE_EXIT { World->DestroyWorld(false); };
    auto* Hero = World->SpawnActor<AVoxelCharacter>();
    const FVector Origin = Hero->GetAimOrigin();
    const FVector Center = Origin + FVector(145,0,0);
    auto* Chest = World->SpawnActor<AIslandChest>(Center,FRotator::ZeroRotator);
    IslandSceneryVoxels::Register(*Chest);
    const FCollisionQueryParams Query(SCENE_QUERY_STAT(ChestVoxelProof), false, Hero);
    FHitResult Hit;
    const auto Trace = [&] { return World->LineTraceSingleByChannel(Hit,Origin,Center+FVector(55,0,0),ECC_Visibility,Query); };
    TInlineComponentArray<UIslandMeshVoxels*> Shell(Chest);
    if (!TestEqual(TEXT("All ten chest sections, clasp and ironwork included"),Shell.Num(),10)) return false;
    TestTrue(TEXT("The shovel reaches an actual board"),Trace() && Hit.GetActor()==Chest && Hit.GetComponent()->IsA<UIslandMeshVoxels>());
    TestTrue(TEXT("A nearby exposed clue can be opened"),Chest->IsReachableFrom(*Hero));
    for (auto* Surface : Shell)
    {
        TestTrue(TEXT("No intact source collision remains behind the carved surface"),!Surface->SourcePart()->IsQueryCollisionEnabled());
        if (Surface->SourcePart()->GetFName()==TEXT("Body"))
        {
            const int32 Before = Surface->RemainingCount();
            FVector Impact,Normal;
            TestTrue(TEXT("A scoop excavates only part of the hull"),Surface->GetSurfaceTarget(Impact,Normal)
                && Surface->Cut(Impact,Normal,false) && Surface->RemainingCount()>0 && Surface->RemainingCount()<Before);
        }
        TArray<int32> Cells;
        Surface->ForEachSolid([&](int32 I,FVector) { Cells.Add(I); });
        Surface->RemoveCells(Cells);
    }
    TestFalse(TEXT("Cleared chest leaves no invisible shovel blocker"),Trace());
    TestTrue(TEXT("The original find still grants a reachable clue after the shell is gone"),Chest->IsReachableFrom(*Hero));
    auto* Wall = World->SpawnActor<AActor>();
    auto* Blocker = NewObject<UBoxComponent>(Wall);
    Wall->SetRootComponent(Blocker);
    Blocker->SetBoxExtent({8,30,70});
    Blocker->SetCollisionResponseToChannel(ECC_Visibility,ECR_Block);
    Blocker->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    Blocker->SetWorldLocation((Origin+Center)*.5);
    Blocker->RegisterComponent();
    TestFalse(TEXT("Intervening rock still blocks access to the clue"),Chest->IsReachableFrom(*Hero));

    Chest->SetPrize(true);
    Chest->RevealPrize(0.);
    TInlineComponentArray<UIslandMeshVoxels*> Revealed(Chest);
    int32 CompassParts=0;
    for (auto* Surface : Revealed)
    {
        if (Shell.Contains(Surface)) continue;
        ++CompassParts;
        TestTrue(TEXT("Compass subdivisions use its full-size geometry during the reveal"),Surface->InitialCount()>1);
        TArray<int32> Cells;
        Surface->ForEachSolid([&](int32 I,FVector) { Cells.Add(I); });
        Surface->RemoveCells(Cells);
        TestFalse(TEXT("A removed compass part no longer intercepts the shovel"),Surface->IsQueryCollisionEnabled());
    }
    TestEqual(TEXT("The rim, both dials and eight compass points are diggable"),CompassParts,11);
    return !HasAnyErrors();
}
#endif
