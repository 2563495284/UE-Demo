#pragma once

#include "Engine/LocalPlayer.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "IslandLocalPlayer.generated.h"

// Persists across a map reload, including independent clients running on the same machine.
UCLASS()
class UIslandLocalPlayer : public ULocalPlayer
{
    GENERATED_BODY()
public:
    FString GetGameLoginOptions() const override;
private:
    mutable FGuid MemberId;
};

UCLASS()
class UIslandRoomRoster : public UGameInstanceSubsystem
{
    GENERATED_BODY()
public:
    TMap<FGuid, int32> ReturningTeams;
    FGuid ReturnRound;
};
