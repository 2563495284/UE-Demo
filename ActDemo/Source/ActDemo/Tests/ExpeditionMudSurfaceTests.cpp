#if WITH_DEV_AUTOMATION_TESTS
#include "Island/ExpeditionMudBall.h"

#include "Components/BoxComponent.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Voxel/VoxelCharacter.h"
#include "Voxel/VoxelDensity.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FExpeditionMudSurfaceTest, "ActDemo.Voxel.MudSurface.ContactAndClearance",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FExpeditionMudSurfaceTest::RunTest(const FString&)
{
    const FString Package = FString::Printf(TEXT("/Temp/MudSurface_%s"), *FGuid::NewGuid().ToString());
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("MudSurface"), CreatePackage(*Package));
    if (!TestNotNull(TEXT("Create the contact physics scene"), World)) return false;
    AActor* Wall = World->SpawnActor<AActor>();
    auto* Face = NewObject<UBoxComponent>(Wall, TEXT("WallFace"));
    Wall->SetRootComponent(Face);
    Wall->AddInstanceComponent(Face);
    Face->InitBoxExtent({4., 200., 200.});
    Face->SetCollisionProfileName(TEXT("BlockAll"));
    Face->RegisterComponent();
    FActorSpawnParameters Spawn;
    Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    auto* Thrower = World->SpawnActor<AVoxelCharacter>(FVector(-350.,0.,90.), FRotator::ZeroRotator, Spawn);
    auto* Target = World->SpawnActor<AVoxelCharacter>(FVector(-44.,0.,90.), FRotator::ZeroRotator, Spawn);
    if (TestNotNull(TEXT("Create the thrower"), Thrower) && TestNotNull(TEXT("Create the target"), Target))
    {
        const FVector Start(-300.,0.,90.), End(180.,0.,90.);
        FHitResult Hit;
        for (const float Radius : {0.f, float(AExpeditionMudBall::Radius)})
        {
            Target->SetActorLocation({-44.,0.,90.});
            TestTrue(TEXT("A shoulder backed against the wall receives its own contact"),
                AExpeditionMudBall::TraceContact(*World,*Thrower,Start,End,Radius,Hit) && Hit.GetActor() == Target && Hit.Item == INDEX_NONE);
            Target->SetActorLocation({44.,0.,90.});
            TestTrue(TEXT("The same capsule on the far side of the wall is hidden"),
                AExpeditionMudBall::TraceContact(*World,*Thrower,Start,End,Radius,Hit) && Hit.GetActor() == Wall);
        }
        // Rotate the wall into a floor. A near-horizontal clod can meet the ankle and
        // floor together: the centre is 9 cm up and sqrt(36 * 40) cm from the capsule axis.
        Wall->SetActorLocationAndRotation({0.,0.,-4.},FRotator(90.,0.,0.));
        Target->SetActorLocation({0.,0.,90.});
        const FVector Joint(-FMath::Sqrt(36. * 40.),0.,9.);
        const FVector Approach = FVector(1.,0.,-.35).GetSafeNormal();
        TestTrue(TEXT("Simultaneous ankle and floor contact is assigned to the visible ankle"),
            AExpeditionMudBall::TraceContact(*World,*Thrower,Joint-Approach*30.,Joint+Approach*10.,9.f,Hit)
            && Hit.GetActor() == Target && Hit.ImpactPoint.Z < 20.);

        // An oversized Pawn-channel interaction proxy from another character can extend
        // into a pit, while its owner's actual capsule stands four metres away.
        Target->SetActorLocation({400.,0.,90.});
        Thrower->SetActorLocation({0.,0.,90.});
        auto* Proxy = NewObject<UBoxComponent>(Target,TEXT("DistantInteractionVolume"));
        Proxy->SetupAttachment(Target->GetRootComponent());
        Proxy->SetRelativeLocation({-400.,0.,-35.});
        Proxy->InitBoxExtent({30.,30.,10.});
        Proxy->SetCollisionProfileName(TEXT("BlockAllDynamic"));
        Proxy->SetCollisionObjectType(ECC_Pawn);
        Target->AddInstanceComponent(Proxy);
        Proxy->RegisterComponent();
        const FVector Eye = Thrower->GetAimOrigin(), Below = Eye - FVector(0.,0.,240.);
        const FCollisionQueryParams Query(SCENE_QUERY_STAT(PitFootProxy),false,Thrower);
        TestTrue(TEXT("The overlapping foreign proxy is a real Pawn query hit"),
            World->LineTraceSingleByObjectType(Hit,Eye,Below,FCollisionObjectQueryParams(ECC_Pawn),Query)
            && Hit.GetComponent() == Proxy);
        for (const float Radius : {0.f, float(AExpeditionMudBall::Radius)})
        {
            TestTrue(TEXT("The downward reticle and the launched clod both reach the pit floor through the proxy"),
                AExpeditionMudBall::TraceContact(*World,*Thrower,Eye,Below,Radius,Hit)
                && Hit.GetComponent() == Face && FMath::IsNearlyZero(Hit.ImpactPoint.Z,1.));
        }
    }
    World->DestroyWorld(false);

    FVoxelEdit Pad;
    Pad.BrushId = VoxelPlayground::MudPad;
    Pad.Action = EVoxelAction::Fill;
    Pad.SurfaceNormal = {1000,0,0};
    auto& Clearance = Pad.MudClearances.AddDefaulted_GetRef();
    Clearance.Center = {41,0,0};
    Clearance.Radius = 49;
    Clearance.AxisHalfLength = 50;
    TestTrue(TEXT("The wall hit leaves a void at the body"),
        VoxelPlayground::PartyBrushDistance(Pad,{20,0,20}) > 0);
    TestTrue(TEXT("The same throw retains solid mud on its exposed side"),
        VoxelPlayground::PartyBrushDistance(Pad,{20,60,20}) < 0);
    Pad.SurfaceNormal = {0,0,1000};
    Clearance.Center = {0,0,90};
    TestTrue(TEXT("A foot under a low ceiling can stay at its initial position"),
        VoxelPlayground::PartyBrushDistance(Pad,{0,0,10}) > 0);
    TestTrue(TEXT("The pad still grows next to this foot"),
        VoxelPlayground::PartyBrushDistance(Pad,{58,0,20}) < 0);
    return !HasAnyErrors();
}
#endif
