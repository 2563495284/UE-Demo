#pragma once

#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerState.h"
#include "Island/IslandRules.h"
#include "IslandState.generated.h"

UCLASS()
class AIslandGameState : public AGameStateBase
{
    GENERATED_BODY()

public:
    void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

    UPROPERTY(Replicated) EIslandPhase Phase = EIslandPhase::Waiting;
    UPROPERTY(Replicated) int8 Winner = -1;
    UPROPERTY(Replicated) uint8 JoinedCount = 0;
};

UCLASS()
class AIslandPlayerState : public APlayerState
{
    GENERATED_BODY()

public:
    void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
    UPROPERTY(Replicated) int8 Team = -1;
};
