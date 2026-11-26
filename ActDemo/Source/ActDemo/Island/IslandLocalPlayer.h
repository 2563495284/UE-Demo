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
    void RememberCharacterModel(int32 Index) { PreferredCharacter = Index; }
private:
    mutable FGuid MemberId;
    int32 PreferredCharacter = INDEX_NONE;
};

UCLASS()
class UIslandRoomRoster : public UGameInstanceSubsystem
{
    GENERATED_BODY()
public:
    TMap<FGuid, int32> ReturningTeams;
    FGuid ReturnRound;
};
