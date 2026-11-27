#pragma once

#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerState.h"
#include "Island/IslandRules.h"
#include "PartyCrewAppearance.h"
#include "Island/IslandRoomSettings.h"
#include "IslandState.generated.h"

class AIslandChest;

USTRUCT()
struct FIslandPartyBriefingMember
{
    GENERATED_BODY()

    UPROPERTY() int32 PlayerId = INDEX_NONE;
    UPROPERTY() FString Name;
    UPROPERTY() bool bHost = false;
    UPROPERTY() bool bAgreed = false;
    UPROPERTY() bool bAssetsReady = false;
    UPROPERTY() bool bGameReady = false;
};

UENUM()
enum class EIslandChestOutcome : uint8 { Unknown, Genuine, Scatter, Disguise, Decoy };

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
    UPROPERTY(Replicated) uint32 PartyBriefingRound = 0;
    UPROPERTY(Replicated) FName PartyBriefingFeature;
    UPROPERTY(Replicated) TArray<FIslandPartyBriefingMember> PartyBriefingMembers;
    UPROPERTY(Replicated) bool bPartyForceStartRequested = false;
    UPROPERTY(Replicated) FString PartyWinnerName;
    UPROPERTY(Replicated) int32 PartyWinnerTeam = INDEX_NONE;
    UPROPERTY(Replicated) int32 PartyWinnerMask = 0;
    bool IsPartyWinner(int32 Team) const { return Team >= 0 && Team < 8 && (PartyWinnerMask & (1 << Team)); }
    // Keep the previous effect through loading. Only a successful start or the end of the hunt clears it.
    UPROPERTY(Replicated) int32 DecoyToolBoostMask = 0;
    bool HasDecoyToolBoost(int32 Team) const { return Team >= 0 && Team < 8 && (DecoyToolBoostMask & (1 << Team)); }
    UPROPERTY(Replicated) EIslandDecoyEvent ActiveDecoyEvent = EIslandDecoyEvent::None;
    bool IsDecoyEventActive(EIslandDecoyEvent Event) const { return Event != EIslandDecoyEvent::None && ActiveDecoyEvent == Event; }
    void BeginDecoyEffect();
    void ClearDecoyEffect();
    UPROPERTY(Replicated) double PartyWinnerUntil = 0.;
    // Only publish the site and reward after the arena has finished.
    UPROPERTY(Replicated) EIslandChestOutcome PartyOutcome = EIslandChestOutcome::Unknown;
    UPROPERTY(Replicated) EIslandDecoyEvent PartyDecoyEvent = EIslandDecoyEvent::None;
    UPROPERTY(Replicated) int32 PartyOpeningTeam = INDEX_NONE;
    UPROPERTY(Replicated) int32 PartySite = INDEX_NONE;
    UPROPERTY(Replicated) int32 PartyRewardMask = 0;
    UPROPERTY(Replicated) TArray<FVector2D> SpentFalsePoints;
    bool IsIdentityDisguised() const
    {
        // Peers uncover their island pawn before the last player's return handshake completes.
        return (Phase == EIslandPhase::PartyReturning && PartyDecoyEvent == EIslandDecoyEvent::Disguise)
            || IsDecoyEventActive(EIslandDecoyEvent::Disguise);
    }

    bool CanLaunch() const;
};

UCLASS()
class AIslandPlayerState : public APlayerState, public IPartyCrewAppearance
{
    GENERATED_BODY()

public:
    void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
    UPROPERTY(Replicated) int32 Team = INDEX_NONE;
    int32 GetCharacterModel() const;
    UStaticMesh* GetPartyFirstPersonFist() const override;
    FVector GetCrewBadgeAnchor() const override;
    USkeletalMesh* GetPartyCharacterRig() const override;
    UPROPERTY(Replicated) bool bPrying = false;
    UStaticMesh* GetPartyCharacterMesh(bool& bLongPaws) const override;
    void SetCharacterModel(int32 ModelIndex);
    UPROPERTY(Replicated) bool bHost = false;
    UPROPERTY(Replicated) bool bRoomReady = false;
    UPROPERTY(Replicated) bool bMapReady = false;
    // Each player sees only a reachable, exposed lock and their own eligibility.
    UPROPERTY(ReplicatedUsing=OnRep_LockView) FIslandLockView LockView;
    void SetLockView(const FIslandLockView& NewView);
    FGuid MemberId;

private:
    UPROPERTY(Replicated) int32 SelectedCharacter = INDEX_NONE;
    UFUNCTION() void OnRep_LockView(const FIslandLockView& Previous);
};
