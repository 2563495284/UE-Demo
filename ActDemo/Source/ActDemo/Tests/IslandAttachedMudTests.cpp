#if WITH_DEV_AUTOMATION_TESTS
#include "Island/IslandAttachedMud.h"

#include "Engine/World.h"
#include "EngineUtils.h"
#include "Island/IslandFallingScenery.h"
#include "Misc/AutomationTest.h"
#include "Voxel/IslandRouteVoxel.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FIslandAttachedMudTest, "ActDemo.Voxel.AttachedMud.SurfaceStackAndUndercut",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FIslandAttachedMudTest::RunTest(const FString&)
{
    const FName Package(*FString::Printf(TEXT("/Temp/AttachedMud_%s"), *FGuid::NewGuid().ToString()));
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("AttachedMud"), CreatePackage(*Package.ToString()));
    if (!TestNotNull(TEXT("Create a physics world"), World)) return false;
    AActor* Frame = World->SpawnActor<AActor>();
    UIslandRouteVoxelComponent* Deck = NewObject<UIslandRouteVoxelComponent>(Frame, TEXT("TestDeck"));
    Frame->SetRootComponent(Deck);
    Frame->AddInstanceComponent(Deck);
    Deck->RegisterComponent();
    Deck->SetWorldLocation({0., 0., 1000.});
    Deck->SetWorldScale3D({1., 1.25, 1.});
    Deck->InitializeField(FBox(FVector(-65,-350,-40), FVector(65,350,40)), 10., [](FVector P)
    {
        const FVector Box = P.GetAbs() - FVector(45, 320, 20);
        return FRouteVoxelSample{Box.GetMax(), FColor::White};
    });

    const auto Throw = [&](FVector Start, FVector End, bool bWide = false)
    {
        FHitResult Hit;
        if (!World->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility)) return static_cast<AIslandAttachedMud*>(nullptr);
        FVoxelEdit Edit;
        Edit.Action = EVoxelAction::Fill;
        Edit.BrushId = VoxelPlayground::MudPad;
        Edit.Center = VoxelPlayground::QuantizeCenter(Hit.ImpactPoint);
        Edit.SurfaceNormal = VoxelPlayground::QuantizeNormal(Hit.ImpactNormal);
        Edit.bWideFootprint = bWide;
        return AIslandAttachedMud::Place(Hit, Edit);
    };
    const FVector High(0., -220., 1300.);
    AIslandAttachedMud* Top = Throw(High, High - FVector(0.,0.,350.));
    AIslandAttachedMud* Upper = Top ? Throw(High, High - FVector(0.,0.,350.)) : nullptr;
    AIslandAttachedMud* Under = Throw({0., 220., 800.}, {0., 220., 1010.});
    AIslandAttachedMud* Side = Throw({300., 0., 1000.}, {0., 0., 1000.}, true);
    TestNotNull(TEXT("An elevated deck holds clay at the actual top contact"), Top);
    TestNotNull(TEXT("Clay can support a second throw above it"), Upper);
    TestNotNull(TEXT("Clay adheres under the deck"), Under);
    TestNotNull(TEXT("The vertical side holds a broad boosted ledge"), Side);
    if (Top && Upper && Under && Side)
    {
        TestTrue(TEXT("The nonuniform deck scale does not stretch the soil"), Upper->GetActorScale3D().Equals(FVector::OneVector, .001));
        FHitResult Floor;
        TestTrue(TEXT("Pawn collision sees the accumulated 80 cm of real treads"),
            World->LineTraceSingleByChannel(Floor, High, High - FVector(0.,0.,350.), ECC_Pawn)
            && Floor.GetActor() == Upper && FMath::IsNearlyEqual(Floor.ImpactPoint.Z, 1100., 3.));
        TestTrue(TEXT("The underside outside the deck is solid"), Under->Field()->IsSolidWorld({0.,220.,955.}));
        TestTrue(TEXT("The side exterior is solid with an empty far side"),
            Side->Field()->IsSolidWorld({120.,0.,1020.}) && !Side->Field()->IsSolidWorld({-70.,0.,1020.}));
        const FVector Before = Upper->GetActorLocation();
        Frame->AddActorWorldOffset(FVector(0.,20.,25.));
        TestTrue(TEXT("All courses follow their moving structural frame"), Upper->GetActorLocation().Equals(Before + FVector(0.,20.,25.), .05));
        AIslandAttachedMud::RefreshFor(*Frame);
        TestTrue(TEXT("A full support check carries clay through the first course"),
            Top->Field()->RemainingCount() > 0 && Upper->Field()->RemainingCount() > 0);
        Top->Field()->ApplyWorldCut(Top->GetActorLocation() + FVector(0.,0.,30.), FVector::UpVector, 124.);
        AIslandAttachedMud::RefreshFor(*Frame);
        TestTrue(TEXT("Digging the lower pad releases only the upper course"),
            !Top->Field()->RemainingCount() && !Upper->Field()->RemainingCount() && Under->Field()->RemainingCount() && Side->Field()->RemainingCount());
        Deck->CullUnsupported([](FVector){ return false; });
        AIslandAttachedMud::RefreshFor(*Frame);
        TestTrue(TEXT("Removing the frame frees the whole stack, including its second course"),
            !Top->Field()->RemainingCount() && !Upper->Field()->RemainingCount() && !Under->Field()->RemainingCount()
            && !Side->Field()->RemainingCount());
        FHitResult Cleared;
        TestFalse(TEXT("Detached dirt stops blocking a pawn"), World->LineTraceSingleByChannel(Cleared, High, High - FVector(0.,0.,350.), ECC_Pawn));
        TestTrue(TEXT("Unsupported clods have a separate falling visual"), bool(TActorIterator<AIslandFallingScenery>(World)));
    }
    World->DestroyWorld(false);
    return true;
}
#endif
