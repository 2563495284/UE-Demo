#pragma once

#include "GameFramework/GameModeBase.h"
#include "Island/IslandPlayerController.h"
#include "IslandFrontMode.generated.h"

class ACameraActor;

UCLASS()
class ACTDEMO_API AIslandFrontController : public AIslandPlayerController
{
    GENERATED_BODY()
public:
    AIslandFrontController();
    void UpdateCameraManager(float DeltaSeconds) override;

private:
    TWeakObjectPtr<ACameraActor> HarbourCamera;
    double PresentationStartedAt = -1.;
    bool bHarbourPresented = false;
};

UCLASS()
class ACTDEMO_API AIslandFrontMode : public AGameModeBase
{
    GENERATED_BODY()
public:
    AIslandFrontMode();
    AActor* ChoosePlayerStart_Implementation(AController* Player) override;
};
