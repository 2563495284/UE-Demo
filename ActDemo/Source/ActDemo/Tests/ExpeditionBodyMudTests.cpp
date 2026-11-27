#if WITH_DEV_AUTOMATION_TESTS
#include "Island/ExpeditionBodyMud.h"

#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "Island/IslandAttachedMud.h"
#include "Misc/AutomationTest.h"
#include "Voxel/IslandRouteVoxel.h"
#include "Voxel/VoxelCharacter.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FExpeditionBodyMudTest, "ActDemo.Voxel.BodyMud.SupportAndPeeling",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FExpeditionBodyMudTest::RunTest(const FString&)
{
    const FString Package = FString::Printf(TEXT("/Temp/BodyMud_%s"),*FGuid::NewGuid().ToString());
    UWorld* World = UWorld::CreateWorld(EWorldType::Game,false,TEXT("BodyMud"),CreatePackage(*Package));
    if (!TestNotNull(TEXT("Create a physics world"),World)) return false;
    AActor* Wall = World->SpawnActor<AActor>();
    auto* Surface = NewObject<UIslandRouteVoxelComponent>(Wall);
    Wall->SetRootComponent(Surface);
    Wall->AddInstanceComponent(Surface);
    Surface->RegisterComponent();
    Wall->SetActorLocation(FVector(500.,0.,1000.));
    Surface->InitializeField(FBox(FVector(-20,-150,-130),FVector(20,150,130)),5.,
        [](FVector P){ return FRouteVoxelSample{(P.GetAbs()-FVector(10.,140.,120.)).GetMax(),FColor::White}; });
    AVoxelCharacter* First = World->SpawnActor<AVoxelCharacter>(FVector(0.,0.,1000.),FRotator::ZeroRotator);
    AVoxelCharacter* Second = World->SpawnActor<AVoxelCharacter>(FVector(108.,0.,1000.),FRotator::ZeroRotator);
    auto* A = First ? First->FindComponentByClass<UExpeditionBodyMudComponent>() : nullptr;
    auto* B = Second ? Second->FindComponentByClass<UExpeditionBodyMudComponent>() : nullptr;
    if (!TestNotNull(TEXT("First body mud component"),A) || !TestNotNull(TEXT("Second body mud component"),B))
    {
        World->DestroyWorld(false);
        return false;
    }
    const auto Direct = [](AVoxelCharacter& Target, UExpeditionBodyMudComponent& Body, FVector Point, FVector Outward)
    {
        FHitResult Hit(&Target, Target.GetCapsuleComponent(),Target.GetActorLocation()+Point,Outward);
        Hit.Item = INDEX_NONE;
        return Body.AddHit(Hit);
    };
    const auto Grow = [&]()
    {
        FHitResult Hit;
        return A->Trace(FVector(200.,0.,1000.),First->GetActorLocation(),9.f,Hit) && A->AddHit(Hit);
    };
    const auto MoveWall = [&](double X)
    {
        Wall->SetActorLocation(FVector(X,0.,1000.));
        A->RefreshContacts();
    };

    Direct(*First,*A,{40.,0.,0.},FVector::ForwardVector);
    TestTrue(TEXT("Second and third throws actually hit and extend the outer surface"),Grow() && Grow() && A->Num()==3);
    TestFalse(TEXT("Three touching clods in open air leave the character mobile"),A->IsAnchored());
    MoveWall(126.); // last clod centre at 96, near wall face at 116
    TestFalse(TEXT("A four centimetre air gap breaks the chain"),A->IsAnchored());
    MoveWall(120.);
    TestTrue(TEXT("A chain from body through two outer clods reaches the wall"),A->IsAnchored());
    TestTrue(TEXT("Hitting its middle clod releases the body and keeps its original chest clay"),
        A->Strike(1) && A->Num()==1 && !A->IsAnchored());
    A->Clear();

    MoveWall(60.);
    Direct(*First,*A,{33.41,-22.,0.},FVector(33.41,-22.,0.).GetSafeNormal());
    Direct(*First,*A,{33.41,22.,0.},FVector(33.41,22.,0.).GetSafeNormal());
    TestTrue(TEXT("Two separated direct bonds hold the same pawn"),A->IsAnchored() && A->Num()==2);
    TestTrue(TEXT("Breaking one of two real bonds still holds the pawn"),A->Peel() && A->IsAnchored() && A->Num()==1);
    TestTrue(TEXT("Removing the last bond immediately releases the pawn"),A->Peel() && !A->IsAnchored());

    MoveWall(174.); // only the clod on Second's right flank touches the wall at X=164
    Direct(*First,*A,{40.,0.,0.},FVector::ForwardVector);
    Direct(*Second,*B,{-40.,0.,0.},FVector::BackwardVector);
    Direct(*Second,*B,{40.,0.,0.},FVector::ForwardVector);
    TestTrue(TEXT("Only the pawn whose right clod touches the wall is stuck"),B->IsAnchored() && !A->IsAnchored());
    MoveWall(90.); // left face at 80 reaches Second's left clod (X=66), but is 38 cm from First's clay
    TestTrue(TEXT("Actual mud contact transfers support across two characters"),A->IsAnchored() && B->IsAnchored());
    B->Strike(0);
    TestFalse(TEXT("Removing the shared clod severs the neighbour's last path"),A->IsAnchored());
    World->DestroyWorld(false);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FExpeditionMudRemoteSupportTest, "ActDemo.Voxel.BodyMud.RemoteBuildingSupport",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FExpeditionMudRemoteSupportTest::RunTest(const FString&)
{
    const FString Package = FString::Printf(TEXT("/Temp/MudFooting_%s"),*FGuid::NewGuid().ToString());
    UWorld* World = UWorld::CreateWorld(EWorldType::Game,false,TEXT("MudFooting"),CreatePackage(*Package));
    if (!TestNotNull(TEXT("Create the footing physics world"),World)) return false;

    AActor* Pier = World->SpawnActor<AActor>();
    auto* Deck = NewObject<UIslandRouteVoxelComponent>(Pier);
    Pier->SetRootComponent(Deck);
    Pier->AddInstanceComponent(Deck);
    Deck->RegisterComponent();
    Pier->SetActorLocation(FVector(0.,0.,1000.));
    Deck->InitializeField(FBox(FVector(-320,-60,-40),FVector(320,60,40)),10.,[](FVector P)
    {
        return FRouteVoxelSample{(P.GetAbs()-FVector(300.,40.,20.)).GetMax(),FColor::White};
    });

    const FVector From(220.,0.,1320.), To(220.,0.,960.);
    const auto Deposit = [&]() -> AIslandAttachedMud*
    {
        FHitResult Hit;
        if (!World->LineTraceSingleByChannel(Hit,From,To,ECC_Visibility)) return nullptr;
        FVoxelEdit Pad;
        Pad.Action = EVoxelAction::Fill;
        Pad.BrushId = VoxelPlayground::MudPad;
        Pad.Center = VoxelPlayground::QuantizeCenter(Hit.ImpactPoint);
        Pad.SurfaceNormal = VoxelPlayground::QuantizeNormal(Hit.ImpactNormal);
        return AIslandAttachedMud::Place(Hit,Pad);
    };
    AIslandAttachedMud* Lower = Deposit();
    AIslandAttachedMud* Upper = Deposit();
    FHitResult Tread;
    if (!TestTrue(TEXT("Two stacked world deposits form the remote walking surface"),
        Lower && Upper && World->LineTraceSingleByChannel(Tread,From,To,ECC_Pawn) && Tread.GetActor()==Upper))
    {
        World->DestroyWorld(false);
        return false;
    }
    const FVector Position = Tread.ImpactPoint + FVector(0.,0.,92.);
    AVoxelCharacter* Victim = World->SpawnActor<AVoxelCharacter>(Position,FRotator::ZeroRotator);
    auto* Body = Victim ? Victim->GetBodyMud() : nullptr;
    if (!TestNotNull(TEXT("Standing expedition character"),Body))
    {
        World->DestroyWorld(false);
        return false;
    }
    const double HalfHeight = Victim->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
    const FVector Ankle = Victim->GetActorLocation() - FVector(0.,0.,HalfHeight);
    FHitResult Shoe(Victim,Victim->GetCapsuleComponent(),Ankle,FVector::DownVector);
    TestFalse(TEXT("A clean character can stand on the upper world mud"),Body->IsAnchored());
    TestTrue(TEXT("Ankle clay connects through both pads to the distant foundation"),
        Body->AddHit(Shoe) && Body->IsAnchored() && Victim->IsMudAnchored());

    // The left end of the deck is the only fixed foundation; the victim stands 350 cm from the cut.
    const FVector Cut(-130.,0.,1020.);
    TestTrue(TEXT("An ordinary punch severs a section well outside the victim"),
        FVector::Dist2D(Cut,Ankle)>300. && Deck->ApplyWorldCut(Cut,FVector::UpVector,62.));
    TestTrue(TEXT("The far end has lost its foundation"),Deck->CullUnsupported([](FVector Local){return Local.X < -270.;}));
    TestTrue(TEXT("The supported stump survives and the remote end clears"),
        Deck->IsSolidWorld({-280.,0.,1000.}) && !Deck->IsSolidWorld({220.,0.,1000.}));
    AIslandAttachedMud::RefreshFor(*Pier);
    FHitResult Gone;
    TestTrue(TEXT("Both mud treads and their pawn collision disappear with the detached deck"),
        !Lower->Field()->RemainingCount() && !Upper->Field()->RemainingCount()
        && !World->LineTraceSingleByChannel(Gone,From,To,ECC_Pawn,FCollisionQueryParams(NAME_None,false,Victim)));

    // Give the component its first normal post-physics tick after the cut. The mock world's
    // clock does not advance, so an elapsed fallback interval cannot make this test pass.
    const uint64 CutFrame = GFrameCounter;
    ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand(([this,World,Body,Victim,CutFrame]()
    {
        if (GFrameCounter == CutFrame) return false;
        Body->TickComponent(1.f/60.f,LEVELTICK_All,&Body->PrimaryComponentTick);
        TestTrue(TEXT("A remote mesh edit frees the actual pawn but leaves its ankle clay"),
            !Body->IsAnchored() && !Victim->IsMudAnchored() && Body->Num()==1);
        World->DestroyWorld(false);
        return true;
    })));
    return true;
}
#endif
