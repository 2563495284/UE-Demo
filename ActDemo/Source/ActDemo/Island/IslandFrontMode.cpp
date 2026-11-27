#include "Island/IslandFrontMode.h"
#include "Camera/CameraActor.h"
#include "Engine/GameInstance.h"
#include "Engine/StaticMeshActor.h"
#include "Components/StaticMeshComponent.h"
#include "EngineUtils.h"
#include "Island/IslandHUD.h"
#include "Island/IslandRoomSession.h"

namespace
{
    ACameraActor* FindHarbourCamera(UWorld* World)
    {
        for (TActorIterator<ACameraActor> It(World); It; ++It)
            if (It->ActorHasTag(TEXT("HarbourView"))) return *It;
        return nullptr;
    }
}

AIslandFrontController::AIslandFrontController()
{
    bAutoManageActiveCameraTarget = false;
}

void AIslandFrontController::UpdateCameraManager(float DeltaSeconds)
{
    // The front end has no playable pawn. Bind the authored camera before its first POV update,
    // including when returning from a failed connection through a newly created local controller.
    if (!HarbourCamera.IsValid()) HarbourCamera = FindHarbourCamera(GetWorld());
    if (HarbourCamera.IsValid() && GetViewTarget() != HarbourCamera.Get()) SetViewTarget(HarbourCamera.Get());
    if (PlayerCameraManager && !bHarbourPresented)
    {
        if (PresentationStartedAt < 0.) PresentationStartedAt = FPlatformTime::Seconds();
        // On a cold Metal launch the sky can render before the dock's material pipelines.
        // Slate remains usable while the first visible authored meshes arrive.
        int32 VisibleProps = 0;
        for (TActorIterator<AStaticMeshActor> It(GetWorld()); It && VisibleProps < 3; ++It)
        {
            if (It->GetStaticMeshComponent()->GetLastRenderTimeOnScreen() > 0.f) ++VisibleProps;
        }
        if ((HarbourCamera.IsValid() && VisibleProps >= 3) || FPlatformTime::Seconds() - PresentationStartedAt >= 3.)
        {
            GetGameInstance()->GetSubsystem<UIslandRoomSession>()->RevealTravelScreen();
            bHarbourPresented = true;
        }
    }
    Super::UpdateCameraManager(DeltaSeconds);
}

AIslandFrontMode::AIslandFrontMode()
{
    DefaultPawnClass = nullptr;
    SpectatorClass = nullptr;
    bStartPlayersAsSpectators = true;
    PlayerControllerClass = AIslandFrontController::StaticClass();
    HUDClass = AIslandHUD::StaticClass();
}

AActor* AIslandFrontMode::ChoosePlayerStart_Implementation(AController* Player)
{
    if (auto* Camera = FindHarbourCamera(GetWorld())) return Camera;
    return Super::ChoosePlayerStart_Implementation(Player);
}
