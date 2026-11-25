#include "Tests/ExpeditionIdentityProof.h"

#if !UE_BUILD_SHIPPING
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "InputKeyEventArgs.h"
#include "Island/ExpeditionAnimalComponent.h"
#include "Island/ExpeditionDock.h"
#include "Island/IslandGameMode.h"
#include "Island/IslandHUD.h"
#include "Island/IslandPlayerController.h"
#include "Island/IslandState.h"
#include "UnrealClient.h"
#include "Voxel/VoxelCharacter.h"

namespace
{
enum class EView { Setup, Fog, Chart, Expiry, Occlusion, Restore, Done };
struct FProof
{
    TWeakObjectPtr<UWorld> World;
    TWeakObjectPtr<ACameraActor> Camera;
    TWeakObjectPtr<AStaticMeshActor> Block;
    TWeakObjectPtr<AActor> PreviousView;
    TArray<FTransform> Positions;
    EView View = EView::Setup;
    double Next = 0.;
    double OriginalDeadline = 0.;
    FIntPoint Expected = FIntPoint::ZeroValue;
};

bool Fail(const TCHAR* Message)
{
    UE_LOG(LogTemp, Error, TEXT("ISLAND_QA_CHECK passed=0 identity: %s"), Message);
    FPlatformMisc::RequestExitWithStatus(false,1);
    return false;
}

void Capture(const TCHAR* Name)
{
    FString Dir;
    FParse::Value(FCommandLine::Get(),TEXT("FieldShotsDir="),Dir);
    FScreenshotRequest::RequestScreenshot(FPaths::Combine(Dir,Name),false,false);
}
}
#endif

bool CaptureExpeditionIdentity(AIslandGameMode& Mode, const TArray<TObjectPtr<AVoxelPlayerController>>& Players)
{
#if !UE_BUILD_SHIPPING
    static FProof Proof;
    UWorld* World=Mode.GetWorld();
    if (Proof.World.Get()!=World)
    {
        Proof=FProof{};
        Proof.World=World;
    }
    auto* Round=Mode.GetGameState<AIslandGameState>();
    if (Proof.View==EView::Done || Round->PartyOutcome!=EIslandChestOutcome::Disguise) return true;
    if (Players.Num()!=3) return Fail(TEXT("requires three participants"));
    auto* Viewer=CastChecked<AIslandPlayerController>(Players[0]);
    const auto* HUD=Cast<AIslandHUD>(Viewer->GetHUD());
    if (!HUD) return Fail(TEXT("rendered host HUD unavailable"));
    const double Time=Round->GetServerWorldTimeSeconds();
    if (Time<Proof.Next) return false;
    Proof.Next=Time+1.;
    auto Step=[&](EView View){ Proof.View=View; return false; };
    if (Proof.View==EView::Setup)
    {
        if (!Round->IsIdentityDisguised() || Round->DisguiseUntil-Time<100.) return Fail(TEXT("two-minute disguise did not begin"));
        const auto* Dock=AExpeditionDock::FindForTeam(World,Round->LaunchedTeams,Viewer->GetTeam());
        if (!Dock) return Fail(TEXT("host dock missing"));
        const FTransform DockPose=Dock->GetActorTransform();
        const FVector Stage[]={{-235,-260,123},{15,-116,123},{15,116,123}};
        for (int32 Seat=0;Seat<3;++Seat)
        {
            auto* Pawn=CastChecked<AVoxelCharacter>(Players[Seat]->GetPawn());
            Proof.Positions.Add(Pawn->GetActorTransform());
            Pawn->GetCharacterMovement()->StopMovementImmediately();
            Pawn->SetActorLocationAndRotation(DockPose.TransformPositionNoScale(Stage[Seat]),
                Dock->GetActorRotation()+FRotator(0,180,0),false,nullptr,ETeleportType::TeleportPhysics);
            if (Seat>0)
                ++(Players[Seat]->GetPlayerState<AIslandPlayerState>()->Team==Viewer->GetTeam() ? Proof.Expected.X : Proof.Expected.Y);
        }
        const FVector Eye=DockPose.TransformPositionNoScale(FVector(-500,0,213));
        const FVector Aim=DockPose.TransformPositionNoScale(FVector(15,0,112));
        Proof.Camera=World->SpawnActor<ACameraActor>(Eye,(Aim-Eye).Rotation());
        if (!Proof.Camera.IsValid()) return Fail(TEXT("unable to place dock camera"));
        Proof.Camera->GetCameraComponent()->SetFieldOfView(58.f);
        Proof.Camera->GetCameraComponent()->SetConstraintAspectRatio(false);
        Proof.PreviousView=Viewer->GetViewTarget();
        Viewer->SetViewTarget(Proof.Camera.Get());
        Proof.OriginalDeadline=Round->DisguiseUntil;
        return Step(EView::Fog);
    }
    const bool bFog=Round->IsIdentityDisguised();
    for (int32 Seat=1;Seat<3;++Seat)
    {
        const auto* Outfit=Players[Seat]->GetPawn()->FindComponentByClass<UExpeditionAnimalComponent>();
        if (!Outfit || !Outfit->ShowsOutfit(bFog)) return Fail(TEXT("visible outfit disagrees with replicated event"));
    }
    const FIntPoint Labels=HUD->GetDrawnCrewCounts();
    UE_LOG(LogTemp,Display,TEXT("EXPEDITION_IDENTITY_VIEW stage=%d fog=%d labels=%d,%d expected=%d,%d"),
        int32(Proof.View),bFog,Labels.X,Labels.Y,Proof.Expected.X,Proof.Expected.Y);
    if (Proof.View==EView::Fog)
    {
        if (!bFog || Labels!=FIntPoint::ZeroValue) return Fail(TEXT("identity exposed during fog"));
        Capture(TEXT("IdentityFog.png"));
        return Step(EView::Chart);
    }
    if (Proof.View==EView::Chart)
    {
        if (!Viewer->IsMapOpen())
        {
            Viewer->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::Tab,IE_Pressed,1.f));
            return false;
        }
        Capture(TEXT("IdentityFogChart.png"));
        return Step(EView::Expiry);
    }
    if (Proof.View==EView::Expiry)
    {
        if (Viewer->IsMapOpen())
        {
            Viewer->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::Tab,IE_Released,0.f));
            // Shorten only this QA countdown, preserving the real expiry and restoration paths.
            Round->DisguiseUntil=Time+.5;
            Round->ForceNetUpdate();
            // In a normal match the short chest announcement is already gone when two minutes elapse.
            Proof.Next=FMath::Max(Proof.Next,Round->PartyWinnerUntil+.5);
            return false;
        }
        if (bFog || Labels!=Proof.Expected) return Fail(TEXT("visible names did not return on expiry"));
        Capture(TEXT("IdentityRevealed.png"));
        return Step(EView::Occlusion);
    }
    if (Proof.View==EView::Occlusion)
    {
        if (!Proof.Block.IsValid())
        {
            const FVector Eye=Proof.Camera->GetActorLocation();
            const FVector Head=Players[2]->GetPawn()->GetActorLocation()+FVector(0,0,65);
            auto* Block=World->SpawnActor<AStaticMeshActor>(FMath::Lerp(Eye,Head,.68),FRotator::ZeroRotator);
            if (!Block) return Fail(TEXT("occluder unavailable"));
            auto* Mesh=Block->GetStaticMeshComponent();
            Mesh->SetMobility(EComponentMobility::Movable);
            Mesh->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube")));
            Block->SetActorScale3D(FVector(.88,.88,1.6));
            Mesh->SetCollisionResponseToChannel(ECC_Visibility,ECR_Block);
            Proof.Block=Block;
            return false;
        }
        FIntPoint Uncovered=Proof.Expected;
        --(Players[2]->GetPlayerState<AIslandPlayerState>()->Team==Viewer->GetTeam() ? Uncovered.X : Uncovered.Y);
        if (Labels!=Uncovered) return Fail(TEXT("occluded player's name showed through geometry"));
        Capture(TEXT("IdentityOccluded.png"));
        return Step(EView::Restore);
    }
    Proof.Block->Destroy();
    Viewer->SetViewTarget(Proof.PreviousView.IsValid() ? Proof.PreviousView.Get() : Viewer->GetPawn());
    Proof.Camera->Destroy();
    for (int32 Seat=0;Seat<3;++Seat)
        Players[Seat]->GetPawn()->SetActorTransform(Proof.Positions[Seat],false,nullptr,ETeleportType::TeleportPhysics);
    Round->DisguiseUntil=Proof.OriginalDeadline;
    Round->ForceNetUpdate();
    Proof.View=EView::Done;
    UE_LOG(LogTemp,Display,TEXT("EXPEDITION_IDENTITY_DONE allies=%d rivals=%d fog_names=0 occlusion=1"),
        Proof.Expected.X,Proof.Expected.Y);
#endif
    return true;
}
