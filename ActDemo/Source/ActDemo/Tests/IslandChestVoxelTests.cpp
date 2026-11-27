#if WITH_DEV_AUTOMATION_TESTS
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Island/IslandSceneryVoxels.h"
#include "Island/IslandPlayerController.h"
#include "Island/IslandRules.h"
#include "Island/IslandWorld.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Voxel/VoxelCharacter.h"
#include "Voxel/VoxelDensity.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FIslandLidExposureTest, "ActDemo.Voxel.RouteScenery.ChestLidExposure",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FIslandLidExposureTest::RunTest(const FString&)
{
    FVoxelDensity Field(EVoxelLayout::Expedition);
    const FVector2D XY(6600.,0.);
    const FVector Center(XY, Field.SurfaceAt(XY).GetValue() - 150.);
    const FBox ChestBounds = FBox::BuildAABB(Center,IslandRules::ChestExtent);
    TestFalse(TEXT("Undisturbed ground conceals the lid"),AIslandChest::IsLidExposed(Field,Center));
    FVoxelDensity Side(Field);
    FVoxelEdit Punch;
    Punch.BrushId = VoxelPlayground::ShallowCut;
    for (int32 Strike=0; Strike<7 && !AIslandChest::IsLidExposed(Field,Center); ++Strike)
    {
        Punch.Center = VoxelPlayground::QuantizeCenter(FVector(XY,Field.SurfaceAt(XY).GetValue()));
        Field.Apply(Field.PrepareEdit(Punch));
    }
    TestTrue(TEXT("Punches from the surface uncover the closed lid"),AIslandChest::IsLidExposed(Field,Center));
    TestTrue(TEXT("The box still has earth under its lid on discovery"),Field.SignedDistanceAt(Center-FVector(0,0,30))<0.);
    TestFalse(TEXT("A full volume clear is not required"),Field.IsVolumeClear(ChestBounds));

    // Dig a shaft just outside the front of the coffer, then punch its near wall.
    const FVector2D Shaft = XY + FVector2D(0.,110.);
    for (int32 Strike=0; Strike<8 && Side.SurfaceAt(Shaft).GetValue()>Center.Z-25.; ++Strike)
    {
        Punch.Center = VoxelPlayground::QuantizeCenter(FVector(Shaft,Side.SurfaceAt(Shaft).GetValue()));
        Side.Apply(Side.PrepareEdit(Punch));
    }
    TestFalse(TEXT("A fully dug neighbouring shaft still leaves the lid concealed"),AIslandChest::IsLidExposed(Side,Center));
    Punch.SurfaceNormal = {0,1000,0};
    Punch.Center = VoxelPlayground::QuantizeCenter(Center + FVector(0,55,24));
    Side.Apply(Side.PrepareEdit(Punch));
    TestTrue(TEXT("A sideways punch exposes the near lip beneath the roof"),AIslandChest::IsLidExposed(Side,Center));
    TestTrue(TEXT("The centre of the lid remains covered after the side cut"),Side.SignedDistanceAt(Center+FVector(0,0,32))<0.);
    return !HasAnyErrors();
}

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
    auto* Player = World->SpawnActor<AIslandPlayerController>();
    Player->Possess(Hero);
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
    // Cover the body and its centre line, leaving a narrow view of the top from the side.
    auto* Bank = World->SpawnActor<AActor>();
    auto* Earth = NewObject<UBoxComponent>(Bank);
    Bank->SetRootComponent(Earth);
    Earth->SetBoxExtent({8.,55.,35.});
    Earth->SetCollisionResponseToChannel(ECC_Visibility,ECR_Block);
    Earth->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    Earth->SetWorldLocation(Origin+FVector(72.,0.,-26.));
    Earth->RegisterComponent();
    TestFalse(TEXT("The remaining bank covers the chest's centre"),Trace() && Hit.GetActor()==Chest);
    const FVector LipAim=(Center+FVector(-44.,0.,27.)-Origin).GetSafeNormal();
    TestTrue(TEXT("A visible lip beside the bank accepts an aimed click"),Player->GetChestAlongAim(LipAim)==Chest);
    TestTrue(TEXT("A partly exposed lid remains reachable for a timed lock"),Chest->IsReachableFrom(*Hero));
    Earth->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    auto* Nearer = World->SpawnActor<AIslandChest>(Origin + FVector(0,115,0),FRotator::ZeroRotator);
    IslandSceneryVoxels::Register(*Nearer);
    TestTrue(TEXT("A fresh click selects the aimed lid over a nearer neighbour"),Player->GetChestAlongAim(FVector::ForwardVector)==Chest);
    TestTrue(TEXT("Turning toward the other lid selects that chest"),Player->GetChestAlongAim(FVector::RightVector)==Nearer);
    TestNull(TEXT("Looking at empty space beside both lids leaves the shovel active"),Player->GetChestAlongAim(-FVector::ForwardVector));
    for (auto* Surface : Shell)
    {
        TestTrue(TEXT("No intact source collision remains behind the carved surface"),!Surface->SourcePart()->IsQueryCollisionEnabled());
        if (Surface->SourcePart()->GetFName()==TEXT("Body"))
        {
            const int32 Before = Surface->RemainingCount();
            FVector Impact,Normal;
            TestTrue(TEXT("A scoop excavates only part of the hull"),Surface->GetSurfaceTarget(Impact,Normal)
                && Surface->Cut(Impact,Normal,1.f) && Surface->RemainingCount()>0 && Surface->RemainingCount()<Before);
        }
        TArray<int32> Cells;
        Surface->ForEachSolid([&](int32 I,FVector) { Cells.Add(I); });
        Surface->RemoveCells(Cells);
    }
    TestFalse(TEXT("Cleared chest leaves no invisible shovel blocker"),Trace());
    TestTrue(TEXT("The original find still grants a reachable clue after the shell is gone"),Chest->IsReachableFrom(*Hero));
    TestTrue(TEXT("The discovery point is targetable after the last board disappears"),Player->GetChestAlongAim(FVector::ForwardVector)==Chest);
    auto* Wall = World->SpawnActor<AActor>();
    auto* Blocker = NewObject<UBoxComponent>(Wall);
    Wall->SetRootComponent(Blocker);
    Blocker->SetBoxExtent({8,30,70});
    Blocker->SetCollisionResponseToChannel(ECC_Visibility,ECR_Block);
    Blocker->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    Blocker->SetWorldLocation((Origin+Center)*.5);
    Blocker->RegisterComponent();
    TestFalse(TEXT("Intervening rock still blocks access to the clue"),Chest->IsReachableFrom(*Hero));
    TestNull(TEXT("Clicking a wall in front of the discovery point cannot reopen it"),Player->GetChestAlongAim(FVector::ForwardVector));

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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FIslandPunchSectionsTest, "ActDemo.Voxel.ChargedPunch.TouchingSections",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FIslandPunchSectionsTest::RunTest(const FString&)
{
    const FString Package = FString::Printf(TEXT("/Temp/Punch_%s_L_ExpeditionIsland"),*FGuid::NewGuid().ToString());
    UWorld* World=UWorld::CreateWorld(EWorldType::Game,false,TEXT("L_ExpeditionIsland"),CreatePackage(*Package));
    if (!TestNotNull(TEXT("Test world"),World)) return false;
    ON_SCOPE_EXIT { World->DestroyWorld(false); };
    UStaticMesh* Cube=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube"));
    if (!TestNotNull(TEXT("100 cm cube"),Cube)) return false;
    auto* Boat=World->SpawnActor<AActor>();
    auto* Other=World->SpawnActor<AActor>();
    const auto AddPlank=[Cube](AActor& Owner,const TCHAR* Name,FVector Location)
    {
        auto* Plank=NewObject<UStaticMeshComponent>(&Owner,FName(Name));
        Plank->SetStaticMesh(Cube);
        Plank->SetCollisionProfileName(TEXT("BlockAll"));
        if (!Owner.GetRootComponent()) Owner.SetRootComponent(Plank);
        else Plank->SetupAttachment(Owner.GetRootComponent());
        Owner.AddInstanceComponent(Plank);
        Plank->RegisterComponent();
        Plank->SetWorldScale3D(FVector(2.,1.,1.));
        Plank->SetWorldLocation(Location);
        return Plank;
    };
    auto* Center=AddPlank(*Boat,TEXT("CenterBoard"),FVector(0,-50,0));
    auto* Side=AddPlank(*Boat,TEXT("SideBoard"),FVector(0,50,0));
    // An unrelated foreground piece lies inside the same lateral reach, behind the first face.
    auto* Separate=AddPlank(*Other,TEXT("DistinctBoard"),FVector(10,-150,0));
    IslandSceneryVoxels::Register(*Boat);
    IslandSceneryVoxels::Register(*Other);
    auto* A=Cast<UIslandMeshVoxels>(IslandSceneryVoxels::SurfaceFor(Center));
    auto* B=Cast<UIslandMeshVoxels>(IslandSceneryVoxels::SurfaceFor(Side));
    auto* C=Cast<UIslandMeshVoxels>(IslandSceneryVoxels::SurfaceFor(Separate));
    if (!A || !B || !C) { AddError(TEXT("One of the scene pieces did not acquire its collision field")); return false; }
    FHitResult Hit;
    if (!TestTrue(TEXT("The fist selects the first board"),World->LineTraceSingleByChannel(Hit,FVector(-190,-55,0),
            FVector(60,-55,0),ECC_Visibility) && Hit.GetComponent()==A)) return false;
    TestTrue(TEXT("A fully charged punch cuts the seam"),IslandSceneryVoxels::Strike(Hit,2.f,2.f));
    TestTrue(TEXT("The two touching boards both lose a part"),A->RemainingCount()>0 && A->RemainingCount()<A->InitialCount()
        && B->RemainingCount()>0 && B->RemainingCount()<B->InitialCount());
    TestEqual(TEXT("The other owner did not lose any material"),C->RemainingCount(),C->InitialCount());
    const FVector OtherContact=Hit.ImpactPoint+Separate->GetComponentLocation()-Center->GetComponentLocation();
    TestTrue(TEXT("An equally wide light punch on a fresh board stops before 76 cm"),
        C->Cut(OtherContact,Hit.ImpactNormal,2.f) && C->IsSolidWorld(OtherContact-Hit.ImpactNormal*76.));
    TestFalse(TEXT("A full punch reaches 76 cm inside the original board"),
        A->IsSolidWorld(Hit.ImpactPoint-Hit.ImpactNormal*76.));
    return !HasAnyErrors();
}
#endif
