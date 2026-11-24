#pragma once

#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerState.h"
#include "Island/IslandRules.h"
#include "Island/IslandRoomSettings.h"
#include "IslandState.generated.h"

class AIslandChest;

UENUM()
enum class EIslandLockRole : uint8
{
    Observer, Leader, Helper
};

USTRUCT()
struct FIslandLockView
{
    GENERATED_BODY()

    UPROPERTY() TObjectPtr<AIslandChest> Chest = nullptr;
    UPROPERTY() int32 Team = INDEX_NONE;
    UPROPERTY() EIslandLockRole Role = EIslandLockRole::Observer;
    UPROPERTY() bool bAssisted = false;
    UPROPERTY() bool bSeized = false;
    UPROPERTY() bool bEligible = false;
    UPROPERTY() double FinishAt = 0.0;

    double ProgressAt(double ServerTime) const
    {
        const double Duration = bAssisted ? IslandRules::LockSeconds * .5 : IslandRules::LockSeconds;
        return Chest ? FMath::Clamp(1.0 - (FinishAt - ServerTime) / Duration, 0.0, 1.0) : 0.0;
    }
};

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
    // Each player sees only a reachable, exposed lock and their own eligibility.
    UPROPERTY(ReplicatedUsing=OnRep_LockView) FIslandLockView LockView;
    void SetLockView(const FIslandLockView& NewView);
    FGuid MemberId;

private:
    UFUNCTION() void OnRep_LockView(const FIslandLockView& Previous);
};
