#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Voxel/IslandRouteVoxel.h"
#include "Island/IslandFallingScenery.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FIslandRouteObjectVolumeTest, "ActDemo.Voxel.RouteScenery.PartialCutAndReplay",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FIslandRouteObjectVolumeTest::RunTest(const FString&)
{
    const FBox Bounds(FVector(-45, -45, -20), FVector(45, 45, 190));
    auto Column = [](FVector P)
    {
        const double D = FMath::Max3(FVector2D(P).Size() - 32., -16.-P.Z, P.Z-175.);
        return FRouteVoxelSample{D, FColor(105,88,66)};
    };
    auto* Field = NewObject<UIslandRouteVoxelComponent>();
    auto* Replica = NewObject<UIslandRouteVoxelComponent>();
    TestTrue(TEXT("Create server field"), Field->InitializeField(Bounds, 10., Column));
    TestTrue(TEXT("Create independently reconstructed peer"), Replica->InitializeField(Bounds, 10., Column));
    const int32 Initial = Field->RemainingCount();
    const auto Root = [](FVector P){ return P.Z < 2.; };
    TestFalse(TEXT("Undamaged pillar stays anchored"), Field->CullUnsupported(Root));
    TestTrue(TEXT("A grazing cut excavates the side"), Field->ApplyWorldCut({32.,0.,133.}, FVector::ForwardVector, 18.));
    TestTrue(TEXT("Partial hit leaves most of the volume"), Field->RemainingCount() > Initial / 2 && Field->RemainingCount() < Initial);
    TestTrue(TEXT("The far side stays solid"), Field->IsSolidLocal({-21.,0.,133.}));
    TestTrue(TEXT("Connected head survives"), Field->IsSolidLocal({0.,0.,160.}));
    TestTrue(TEXT("Sever base with the ordinary shovel footprint"), Field->ApplyWorldCut({0.,0.,75.}, FVector::UpVector));
    TArray<FIslandLooseSamples> Loose;
    TestTrue(TEXT("Disconnected head clears after losing its roots"), Field->CullUnsupported(Root, &Loose));
    TestEqual(TEXT("The severed head falls as a single connected piece"), Loose.Num(), 1);
    if (Loose.Num() == 1)
    {
        FProcMeshSection Head;
        TestTrue(TEXT("A peer can reconstruct the painted head after the server clears it"), Replica->FallingSection(Loose[0].Shape, Head));
        TestTrue(TEXT("The falling body has a closed head shape and leaves the anchored stump behind"),
            Head.ProcIndexBuffer.Num()>0 && Head.SectionLocalBox.Min.Z>75. && Head.SectionLocalBox.Max.Z>160. && !Head.bEnableCollision);
    }
    TestFalse(TEXT("No floating head"), Field->IsSolidLocal({0.,0.,160.}));
    TestTrue(TEXT("Still grounded below the cut"), Field->IsSolidLocal({0.,0.,0.}));
    TArray<uint8> Snapshot;
    Field->WriteChanges(Snapshot);
    Replica->ReadChanges(Snapshot);
    TestTrue(TEXT("Late peer restores exact density digest and mesh"), Replica->Digest() == Field->Digest() &&
        Replica->GetProcMeshSection(0)->ProcIndexBuffer == Field->GetProcMeshSection(0)->ProcIndexBuffer);
    TestTrue(TEXT("Digging away the underlying terrain clears the remaining root"), Field->CullUnsupported([](FVector){ return false; }));
    Field->WriteChanges(Snapshot);
    Replica->ReadChanges(Snapshot);
    TestTrue(TEXT("Cleared collision and render sections agree across peers"), Field->RemainingCount() == 0 &&
        Replica->RemainingCount() == 0 && Field->GetNumSections() == 0 && Replica->GetNumSections() == 0);
    auto* Thick = NewObject<UIslandRouteVoxelComponent>();
    if (!TestTrue(TEXT("A solid object for measuring penetration"), Thick->InitializeField(Bounds, 10., Column))) return false;
    const FVector Top(0.,0.,175.);
    // Hold the contact and lateral radius constant to measure only the axial reach.
    Thick->ApplyWorldCut(Top, FVector::UpVector, 124., 1.f);
    TestTrue(TEXT("A broad light cut leaves material 70 cm behind the face"), Thick->IsSolidLocal(Top-FVector(0.,0.,70.)));
    Thick->ApplyWorldCut(Top, FVector::UpVector, 124., 1.5f);
    TestTrue(TEXT("Half charge reaches 70 cm and leaves material at 95 cm"),
        !Thick->IsSolidLocal(Top-FVector(0.,0.,70.)) && Thick->IsSolidLocal(Top-FVector(0.,0.,95.)));
    Thick->ApplyWorldCut(Top, FVector::UpVector, 124., 2.f);
    TestTrue(TEXT("Full charge reaches 95 cm and leaves material at 125 cm"),
        !Thick->IsSolidLocal(Top-FVector(0.,0.,95.)) && Thick->IsSolidLocal(Top-FVector(0.,0.,125.)));
    return true;
}
#endif

#if WITH_DEV_AUTOMATION_TESTS
#include "Components/StaticMeshComponent.h"
#include "Components/BoxComponent.h"
#include "EngineUtils.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "Island/IslandBreakable.h"
#include "Island/IslandNeutralProp.h"
#include "Island/IslandWorld.h"
#include "Voxel/VoxelCharacter.h"
#include "Island/IslandScenerySupport.h"
#include "Island/IslandSceneryVoxels.h"
#include "Voxel/VoxelDensity.h"

#include "Voxel/VoxelTerrain.h"

namespace
{
    class FUndercutObjects final : public IAutomationLatentCommand
    {
    public:
        FUndercutObjects(FAutomationTestBase& InTest, bool bWithTreasure) : Test(InTest), bTreasure(bWithTreasure) {}

        bool Update() override
        {
            if (!World)
            {
                const FString PackageName = FString::Printf(TEXT("/Temp/Undercut_%s_L_ExpeditionIsland"), *FGuid::NewGuid().ToString());
                World = UWorld::CreateWorld(EWorldType::Game,false,TEXT("L_ExpeditionIsland"),CreatePackage(*PackageName));
                Terrain = World->SpawnActor<AVoxelTerrain>();
                Terrain->Layout = EVoxelLayout::Expedition;
                Terrain->DispatchBeginPlay();
                Deadline = FPlatformTime::Seconds() + 30.;
                return false;
            }
            if (FPlatformTime::Seconds() > Deadline)
            {
                Test.AddError(TEXT("The authority did not finish committing the terrain edits"));
                World->DestroyWorld(false);
                return true;
            }
            if (!Terrain->IsReady() || Terrain->IsBusy()) return false;
            if (Centers.IsEmpty())
            {
                using IslandRouteFlora::EShape;
                const FVector2D Beach(6600., 0.);
                const FVector Base(Beach, Terrain->GetDensity()->SurfaceAt(Beach).GetValue());
                if (bTreasure) SpawnTreasure(Base);
                else
                {
                    // The same soil profile supports all four shapes. Only the terrain receives edits.
                    for (EShape Shape : {EShape::FanPalm, EShape::Saltbush, EShape::SplitReef, EShape::CoralSpur})
                    {
                        auto* Plant = AIslandRouteVoxel::Spawn(*World, Shape, Base, 29, 1);
                        Plant->DispatchBeginPlay();
                        Plants.Add(Plant);
                        Test.TestTrue(TEXT("The authority spawned a grounded solid"),
                            Plant->HasAuthority() && Plant->Field()->RemainingCount() > 0 && !Plant->RefreshGrounding(*Terrain));
                    }
                }
                Centers.Add(VoxelPlayground::QuantizeCenter(Base + FVector(110., 45., 0.)));
                for (double Z : {0., -50., -100.})
                for (double Y : {-50., 0., 50.})
                for (double X : {-100., -50., 0., 50., 100.})
                    Centers.Add(VoxelPlayground::QuantizeCenter(Base + FVector(X, Y, Z)));
            }
            if (NextStroke == 1)
            {
                Test.TestEqual(TEXT("The side excavation committed"), Terrain->GetAppliedSequence(), 1u);
                for (auto* Part : Timber)
                    Test.TestTrue(TEXT("Timber survives digging next to its base"), Part->RemainingCount() > 0);
                for (auto* Plant : Plants)
                    Test.TestTrue(TEXT("Soil at the far edge still supports the object"), Plant->Field()->RemainingCount() > 0);
            }
            if (NextStroke < Centers.Num())
            {
                FVoxelEdit Shovel;
                Shovel.Sequence = Terrain->GetAppliedSequence() + 1;
                Shovel.BrushId = VoxelPlayground::ShallowCut;
                Shovel.Center = Centers[NextStroke++];
                Terrain->BuildEdit(Shovel, {}, {});
                return false;
            }
            Test.TestTrue(TEXT("The undercut required several successful ordinary shovel strokes"), Terrain->GetAppliedSequence() > 1);
            for (auto* Plant : Plants)
            {
                // Commit must notify the plant. Explicitly refreshing here would mask a missing notification.
                Test.TestEqual(TEXT("No floating matter after the last soil contact was removed"), Plant->Field()->RemainingCount(), 0);
                Test.TestEqual(TEXT("The unsupported object has no render or collision sections"), Plant->Field()->GetNumSections(), 0);
            }
            if (bTreasure) CheckTreasure();
            World->DestroyWorld(false);
            return true;
        }

    private:
        void SpawnTreasure(FVector Base)
        {
            Discovery = Base + FVector(0.,0.,350.);
            Chest = World->SpawnActor<AIslandChest>(Discovery,FRotator::ZeroRotator);
            Chest->DispatchBeginPlay();
            Chest->SeatAtReveal(*Terrain);
            Test.TestTrue(TEXT("The exposed coffer lands on the cave floor"), Discovery.Z-Chest->GetActorLocation().Z > 250.);
            Chest->GetComponents(Timber);
            Test.TestEqual(TEXT("The closed coffer has all ten tangible parts"), Timber.Num(),10);
            Hero = World->SpawnActor<AVoxelCharacter>();
            CheckReach(Discovery);
            CheckReach(Chest->GetActorLocation());

            auto* Plank = World->SpawnActor<AIslandBreakable>(Base+FVector(-75.,0.,23.),FRotator::ZeroRotator);
            Plank->GetMesh().SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube")));
            Plank->GetMesh().SetWorldScale3D(FVector(.36,.36,.5));
            IslandSceneryVoxels::Register(*Plank);
            if (auto* Field = Plank->FindComponentByClass<UIslandMeshVoxels>()) Timber.Add(Field);
            else Test.AddError(TEXT("The placed breakable did not register a voxel field"));

            auto* Raft = World->SpawnActor<AIslandRescueRaft>(FVector(9800.,0.,65.),FRotator::ZeroRotator);
            Raft->DispatchBeginPlay();
            auto* Deck = Raft->FindComponentByClass<UIslandMeshVoxels>();
            IslandSceneryVoxels::CheckFoundations(*World,Raft->GetComponentsBoundingBox(true));
            if (Test.TestTrue(TEXT("The offshore rescue platform is supported by its flotation deck"),
                Deck && Deck->RemainingCount() == Deck->InitialCount() && Deck->RemainingCount() > 0))
                CheckRaftShovel(*Deck,Raft->GetActorLocation());
        }

        void CheckRaftShovel(UIslandMeshVoxels& Deck,FVector Origin)
        {
            FCollisionQueryParams Query(SCENE_QUERY_STAT(RescueDeckShovel),false);
            Query.bReturnFaceIndex=true;
            const auto Trace=[&](FVector Aim,FHitResult& Hit)
            {
                return AIslandNeutralProp::TraceShovel(*World,Hit,Aim+FVector(0,0,110),Aim-FVector(0,0,75),Query);
            };
            const FVector Left=Origin+FVector(-40,-18,0), Right=Origin+FVector(40,56,0);
            const int32 Initial=Deck.RemainingCount();
            FHitResult Hit;
            if (!Test.TestTrue(TEXT("The shovel hits the visible raft planks"),Deck.IsVisible() && Trace(Left,Hit) && Hit.GetComponent()==&Deck)) return;
            if (!Test.TestTrue(TEXT("One ordinary shovel stroke removes only the struck area"),IslandSceneryVoxels::Strike(Hit) &&
                Deck.RemainingCount()>0 && Deck.RemainingCount()<Initial)) return;
            const int32 Partial=Deck.RemainingCount();
            Test.TestFalse(TEXT("The shovel passes through the first opening"),Trace(Left,Hit));
            Test.TestTrue(TEXT("The opposite end of the raft still supports a hit"),Trace(Right,Hit) && Hit.GetComponent()==&Deck);

            // Aim down through the remaining planks. Every removal must come from a real physical hit.
            int32 Strikes=1;
            while (Deck.RemainingCount()>0)
            {
                bool bFound=false;
                Deck.ForEachSolid([&](int32,FVector Center)
                {
                    if (!bFound) bFound=Trace(Center,Hit) && Hit.GetComponent()==&Deck;
                });
                const int32 Before=Deck.RemainingCount();
                if (!Test.TestTrue(TEXT("A later shovel stroke removes remaining raft cells"),
                    bFound && IslandSceneryVoxels::Strike(Hit) && Deck.RemainingCount()<Before)) return;
                ++Strikes;
            }
            Test.TestFalse(TEXT("No intact source or voxel geometry blocks the last standing spot"),Trace(Right,Hit));
            Test.TestFalse(TEXT("A fully carved rescue raft has no visible deck or pawn collision"),Deck.IsVisible() ||
                Deck.IsQueryCollisionEnabled() || Deck.SourcePart()->IsQueryCollisionEnabled());
            Test.AddInfo(FString::Printf(TEXT("Rescue raft: %d initial cells, %d after the first opening, %d after %d shovel hits"),
                Initial,Partial,Deck.RemainingCount(),Strikes));
        }

        void CheckReach(FVector Target)
        {
            const FVector EyeOffset = Hero->GetAimOrigin()-Hero->GetActorLocation();
            Hero->SetActorLocation(Target + FVector(0.,90.,25.) - EyeOffset,false,nullptr,ETeleportType::TeleportPhysics);
            Test.TestTrue(TEXT("The clue can be reached at this end of the dig"),
                Chest->GetInteractionDistanceSquared(Hero->GetAimOrigin()) < FMath::Square(200.) && Chest->IsReachableFrom(*Hero));
        }

        void CheckTreasure()
        {
            for (auto* Part : Timber)
                Test.TestTrue(TEXT("Undercut timber loses its visible and physical geometry"),
                    Part->RemainingCount()==0 && !Part->IsQueryCollisionEnabled());
            CheckReach(Discovery);
            CheckReach(Chest->GetActorLocation());
            World->SetGameState(World->SpawnActor<AGameStateBase>());
            Chest->SetPrize(true);
            Chest->RevealPrize(0.);
            World->TimeSeconds = 2.;
            Chest->Tick(1.);
            const auto* Compass = FindObjectFast<USceneComponent>(Chest,TEXT("PrizeCompass"));
            Test.TestTrue(TEXT("The reward visibly rises after the empty shell is opened"),
                Compass && Compass->GetRelativeLocation().Z > 90.);
            IslandSceneryVoxels::CheckFoundations(*World,Chest->GetComponentsBoundingBox(true));
            TInlineComponentArray<UIslandMeshVoxels*> Parts(Chest);
            int32 NeedlesAndDials = 0;
            for (auto* Part : Parts)
            {
                if (!Part->SourcePart()->ComponentHasTag(TEXT("IslandPrizeCompass"))) continue;
                ++NeedlesAndDials;
                Test.TestTrue(TEXT("The floating prize remains intact on its spindle"),Part->RemainingCount()==Part->InitialCount());
            }
            Test.TestEqual(TEXT("The rim, needles and dials still exist after checking the raised prize"),NeedlesAndDials,11);
        }

        FAutomationTestBase& Test;
        bool bTreasure;
        AIslandChest* Chest = nullptr;
        AVoxelCharacter* Hero = nullptr;
        FVector Discovery = FVector::ZeroVector;
        TArray<UIslandMeshVoxels*> Timber;
        UWorld* World = nullptr;
        AVoxelTerrain* Terrain = nullptr;
        TArray<AIslandRouteVoxel*> Plants;
        TArray<FIntVector> Centers;
        int32 NextStroke = 0;
        double Deadline = 0.;
    };
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FIslandRouteUndercutTest, "ActDemo.Voxel.RouteScenery.UndercutPlantedShapes",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FIslandRouteUndercutTest::RunTest(const FString&)
{
    ADD_LATENT_AUTOMATION_COMMAND(FUndercutObjects(*this,false));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FIslandUndercutChestTest, "ActDemo.Voxel.RouteScenery.UndercutTreasureAndBreakables",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FIslandUndercutChestTest::RunTest(const FString&)
{
    ADD_LATENT_AUTOMATION_COMMAND(FUndercutObjects(*this,true));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FIslandCrossPartSupportTest, "ActDemo.Voxel.RouteScenery.ArchSupport",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FIslandCrossPartSupportTest::RunTest(const FString&)
{
    const FVoxelDensity Ground(EVoxelLayout::Expedition);
    UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube"));
    if (!TestNotNull(TEXT("Authored cube source"),Cube)) return false;
    UWorld* World = UWorld::CreateWorld(EWorldType::Game,false);
    AActor* Actor = World->SpawnActor<AActor>();
    const double Base = Ground.SurfaceAt(FVector2D(0.,0.)).GetValue();
    auto AddPiece = [&](FVector Center,FVector Dimensions)
    {
        auto* Source = NewObject<UStaticMeshComponent>(Actor);
        Source->SetStaticMesh(Cube);
        Source->SetWorldTransform(FTransform(FQuat::Identity,Center+FVector(0.,0.,Base),Dimensions/100.));
        Source->RegisterComponent();
        auto* Volume = NewObject<UIslandMeshVoxels>(Actor);
        if (!Volume->Initialize(*Source)) return static_cast<UIslandMeshVoxels*>(nullptr);
        return Volume;
    };
    auto* West = AddPiece({-89.,0.,65.},{50.,60.,170.});
    auto* East = AddPiece({89.,0.,65.},{50.,60.,170.});
    auto* Beam = AddPiece({0.,0.,163.},{240.,54.,32.});
    auto* Floating = AddPiece({0.,0.,216.},{240.,54.,18.});
    if (!West || !East || !Beam || !Floating)
    {
        AddError(TEXT("All four authored meshes must have occupied voxels"));
        World->DestroyWorld(false);
        return false;
    }
    TArray<UIslandMeshVoxels*> Parts{West,East,Beam,Floating};
    TMap<UIslandMeshVoxels*,TArray<int32>> Loss;
    IslandScenerySupport::FindUnsupported(Ground, Parts, Loss);
    TestTrue(TEXT("Two piers on real island soil carry the beam"),!Loss.Contains(West) && !Loss.Contains(East) && !Loss.Contains(Beam));
    TestTrue(TEXT("A separate slab above the beam is completely unsupported"),
        Loss.Contains(Floating) && Loss[Floating].Num()==Floating->RemainingCount());
    Parts.Remove(Floating);
    auto RemoveAll = [](UIslandMeshVoxels& Volume)
    {
        TArray<int32> Cells;
        Volume.ForEachSolid([&](int32 I,FVector){Cells.Add(I);});
        Volume.RemoveCells(Cells);
    };
    RemoveAll(*West);
    IslandScenerySupport::FindUnsupported(Ground, Parts, Loss);
    TestTrue(TEXT("Far pier still supports the entire lintel"),Loss.IsEmpty());
    RemoveAll(*East);
    Parts.Add(Floating);
    TArray<IslandScenerySupport::FCellGroup> LooseGroups;
    IslandScenerySupport::FindUnsupported(Ground, Parts, Loss, &LooseGroups);
    TestEqual(TEXT("Two disconnected slabs become two independently falling bodies"), LooseGroups.Num(), 2);
    TestTrue(TEXT("Removing both supports releases every lintel voxel"),
        Loss.Contains(Beam) && Loss[Beam].Num()==Beam->RemainingCount());
    if (const auto* Cells=Loss.Find(Beam))
    {
        TArray<uint8> Packed;
        AIslandFallingScenery::PackCells(*Cells, Packed);
        Beam->RemoveCells(*Cells);
        TArray<FProcMeshSection> Rebuilt;
        TestTrue(TEXT("Late journal application cannot erase the falling shape"), Beam->FallingSections(Packed, Rebuilt));
        TestTrue(TEXT("An entirely detached lintel keeps the original cube silhouette"),
            !Rebuilt.IsEmpty() && Rebuilt[0].ProcIndexBuffer.Num()==36 && Beam->RemainingCount()==0);

        auto Landing=[&](double Top)
        {
            auto* Floor=NewObject<UBoxComponent>(World->SpawnActor<AActor>());
            Floor->SetBoxExtent({400.,400.,8.});
            Floor->SetWorldLocation({0.,0.,Base+Top-8.});
            Floor->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
            Floor->SetCollisionResponseToAllChannels(ECR_Block);
            Floor->RegisterComponent();
            return Floor;
        };
        Landing(0.);
        auto* Shelf=Landing(82.);
        TArray<IslandScenerySupport::FCellGroup> FallingBeam;
        FallingBeam.AddDefaulted_GetRef().Add(Beam,*Cells);
        AIslandFallingScenery::Release(*Actor,FallingBeam);
        TActorIterator<AIslandFallingScenery> Effects(World);
        if (TestTrue(TEXT("A disconnected slab starts an authority fall"),bool(Effects)))
        {
            auto* FX=*Effects;
            TestTrue(TEXT("It initially falls towards the closest surviving shelf"),
                FX->Contact.bHasContact && FMath::Abs(FX->Contact.Point.Z-Base-82.)<2.);
            Shelf->SetCollisionEnabled(ECollisionEnabled::NoCollision);
            World->TimeSeconds=.20;
            FX->Tick(.20f);
            TestTrue(TEXT("Removing the predicted landing while the slab is airborne uncovers the lower floor"),
                FX->Contact.bHasContact && !FX->Contact.bConfirmed && FMath::Abs(FX->Contact.Point.Z-Base)<2.);
            World->TimeSeconds=1.1;
            FX->Tick(.9f);
            TestTrue(TEXT("Impact is confirmed at the remaining floor"),FX->Contact.bConfirmed && FMath::Abs(FX->Contact.Point.Z-Base)<2.);
        }
    }
    World->DestroyWorld(false);
    return !HasAnyErrors();
}
#endif
