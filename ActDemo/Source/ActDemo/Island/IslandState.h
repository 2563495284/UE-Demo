#pragma once

#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerState.h"
#include "Island/IslandRules.h"
#include "Island/IslandRoomSettings.h"
#include "IslandState.generated.h"

class AIslandChest;

UENUM()
enum class EIslandChestOutcome : uint8 { Unknown, Genuine, Scatter, Disguise };

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
    UPROPERTY(Replicated) double PartyStartedAt = 0.;
    UPROPERTY(Replicated) FString PartyWinnerName;
    UPROPERTY(Replicated) int32 PartyWinnerTeam = INDEX_NONE;
    UPROPERTY(Replicated) int32 PartyWinnerMask = 0;
    bool IsPartyWinner(int32 Team) const { return Team >= 0 && Team < 8 && (PartyWinnerMask & (1 << Team)); }
    UPROPERTY(Replicated) double PartyWinnerUntil = 0.;
    // Only publish the site and reward after the arena has finished.
    UPROPERTY(Replicated) EIslandChestOutcome PartyOutcome = EIslandChestOutcome::Unknown;
    UPROPERTY(Replicated) int32 PartyOpeningTeam = INDEX_NONE;
    UPROPERTY(Replicated) int32 PartySite = INDEX_NONE;
    UPROPERTY(Replicated) int32 PartyRewardMask = 0;
    UPROPERTY(Replicated) TArray<FVector2D> SpentFalsePoints;
    UPROPERTY(Replicated) double DisguiseUntil = 0.;
    bool IsIdentityDisguised() const { return GetServerWorldTimeSeconds() < DisguiseUntil; }

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
