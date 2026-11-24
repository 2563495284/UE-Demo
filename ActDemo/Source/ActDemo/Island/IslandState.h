#pragma once

#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerState.h"
#include "Island/IslandRules.h"
#include "Island/IslandRoomSettings.h"
#include "IslandState.generated.h"

UCLASS()
class AIslandGameState : public AGameStateBase
{
    GENERATED_BODY()

public:
    void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

    UPROPERTY(Replicated) EIslandPhase Phase = EIslandPhase::Waiting;
    UPROPERTY(Replicated) int32 Winner = INDEX_NONE;
    UPROPERTY(Replicated) uint8 JoinedCount = 0;
    UPROPERTY(Replicated) FIslandRoomSettings RoomSettings;
    UPROPERTY(Replicated) bool bRoomLobby = false;
    UPROPERTY(Replicated) bool bRoomBlocked = false;
    UPROPERTY(Replicated) int32 ActiveTeams = 0;
    UPROPERTY(Replicated) TArray<int32> LaunchedTeams;
    UPROPERTY(Replicated) double StartAtServerTime = 0.0;
    UPROPERTY(Replicated) FString RoomNotice;

    bool CanLaunch() const;
};

UCLASS()
class AIslandPlayerState : public APlayerState
{
    GENERATED_BODY()

public:
    void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
    UPROPERTY(Replicated) int32 Team = INDEX_NONE;
    UPROPERTY(Replicated) bool bHost = false;
    UPROPERTY(Replicated) bool bRoomReady = false;
    UPROPERTY(Replicated) bool bMapReady = false;
    FGuid MemberId;
};
