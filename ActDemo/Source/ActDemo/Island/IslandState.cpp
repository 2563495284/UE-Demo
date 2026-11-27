#include "Island/IslandState.h"
#include "Island/ExpeditionAnimalComponent.h"
#include "Island/IslandWorld.h"
#include "GameFramework/PlayerController.h"
#include "Net/UnrealNetwork.h"

void AIslandGameState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(AIslandGameState, Phase);
    DOREPLIFETIME(AIslandGameState, Winner);
    DOREPLIFETIME(AIslandGameState, JoinedCount);
    DOREPLIFETIME(AIslandGameState, RoomSettings);
    DOREPLIFETIME(AIslandGameState, bRoomLobby);
    DOREPLIFETIME(AIslandGameState, bRoomBlocked);
    DOREPLIFETIME(AIslandGameState, ActiveTeams);
    DOREPLIFETIME(AIslandGameState, LaunchedTeams);
    DOREPLIFETIME(AIslandGameState, StartAtServerTime);
    DOREPLIFETIME(AIslandGameState, RoomNotice);
    DOREPLIFETIME(AIslandGameState, PartyStartedAt);
    DOREPLIFETIME(AIslandGameState, PartyBriefingRound);
    DOREPLIFETIME(AIslandGameState, PartyBriefingFeature);
    DOREPLIFETIME(AIslandGameState, PartyBriefingMembers);
    DOREPLIFETIME(AIslandGameState, bPartyForceStartRequested);
    DOREPLIFETIME(AIslandGameState, PartyWinnerName);
    DOREPLIFETIME(AIslandGameState, PartyWinnerTeam);
    DOREPLIFETIME(AIslandGameState, PartyWinnerMask);
    DOREPLIFETIME(AIslandGameState, DecoyToolBoostMask);
    DOREPLIFETIME(AIslandGameState, ActiveDecoyEvent);
    DOREPLIFETIME(AIslandGameState, PartyWinnerUntil);
    DOREPLIFETIME(AIslandGameState, PartyOutcome);
    DOREPLIFETIME(AIslandGameState, PartyDecoyEvent);
    DOREPLIFETIME(AIslandGameState, PartyOpeningTeam);
    DOREPLIFETIME(AIslandGameState, PartySite);
    DOREPLIFETIME(AIslandGameState, PartyRewardMask);
    DOREPLIFETIME(AIslandGameState, SpentFalsePoints);
}

void AIslandGameState::BeginDecoyEffect()
{
    if (!IslandRules::IsPersistentDecoyEvent(PartyDecoyEvent)) return;
    ActiveDecoyEvent = PartyDecoyEvent;
    DecoyToolBoostMask = ActiveDecoyEvent == EIslandDecoyEvent::WideTools ? PartyWinnerMask : 0;
    ForceNetUpdate();
}

void AIslandGameState::ClearDecoyEffect()
{
    ActiveDecoyEvent = EIslandDecoyEvent::None;
    DecoyToolBoostMask = 0;
    ForceNetUpdate();
}

void AIslandPlayerState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(AIslandPlayerState, Team);
    DOREPLIFETIME(AIslandPlayerState, SelectedCharacter);
    DOREPLIFETIME(AIslandPlayerState, bHost);
    DOREPLIFETIME(AIslandPlayerState, bRoomReady);
    DOREPLIFETIME(AIslandPlayerState, bPrying);
    DOREPLIFETIME(AIslandPlayerState, bMapReady);
    DOREPLIFETIME_CONDITION(AIslandPlayerState, LockView, COND_OwnerOnly);
}

int32 AIslandPlayerState::GetCharacterModel() const
{
    return IslandRules::IsCharacterModel(SelectedCharacter) ? SelectedCharacter : uint32(GetPlayerId()) % IslandRules::CharacterModelCount;
}

UStaticMesh* AIslandPlayerState::GetPartyCharacterMesh(bool& bLongPaws) const
{
    const auto* Round = GetWorld()->GetGameState<AIslandGameState>();
    if (!Round || !Round->bRoomLobby || Round->IsIdentityDisguised()) return nullptr;
    const int32 Model = GetCharacterModel();
    bLongPaws = Model == 1;
    return GetDefault<UExpeditionAnimalComponent>()->GetCharacterMesh(Model);
}

FVector AIslandPlayerState::GetCrewBadgeAnchor() const
{
    // Back surface of each garment, in centimetres above the sole.
    // The fox's patch sits beside her diagonal chart case.
    static const FVector Anchors[]={{-24.,6.,85.},{-18.7,0.,90.},{-16.9,11.,78.},{-28.2,0.,88.}};
    static_assert(UE_ARRAY_COUNT(Anchors)==IslandRules::CharacterModelCount);
    return Anchors[GetCharacterModel()];
}

USkeletalMesh* AIslandPlayerState::GetPartyCharacterRig() const
{
    const auto* Round=GetWorld()->GetGameState<AIslandGameState>();
    if (!Round || !Round->bRoomLobby || Round->IsIdentityDisguised()) return nullptr;
    return GetDefault<UExpeditionAnimalComponent>()->GetCharacterRig(GetCharacterModel());
}

UStaticMesh* AIslandPlayerState::GetPartyFirstPersonFist() const
{
    const auto* Round=GetWorld()->GetGameState<AIslandGameState>();
    if (!Round || !Round->bRoomLobby || Round->IsIdentityDisguised()) return nullptr;
    return GetDefault<UExpeditionAnimalComponent>()->GetFirstPersonFist(GetCharacterModel());
}

void AIslandPlayerState::SetCharacterModel(int32 ModelIndex)
{
    if (!HasAuthority() || !IslandRules::IsCharacterModel(ModelIndex) || SelectedCharacter == ModelIndex) return;
    SelectedCharacter = ModelIndex;
    ForceNetUpdate();
}

void AIslandPlayerState::SetLockView(const FIslandLockView& NewView)
{
    const FIslandLockView Previous = LockView;
    LockView = NewView;
    bPrying=NewView.Chest && NewView.Role!=EIslandLockRole::Observer;
    if (const APlayerController* Player = GetPlayerController(); Player && Player->IsLocalController())
    {
        OnRep_LockView(Previous);
    }
    ForceNetUpdate();
}

void AIslandPlayerState::OnRep_LockView(const FIslandLockView& Previous)
{
    if (IsValid(Previous.Chest) && Previous.Chest != LockView.Chest)
    {
        Previous.Chest->AnimateLock(0.0, false);
    }
    if (IsValid(LockView.Chest))
    {
        LockView.Chest->AnimateLock(LockView.FinishAt, LockView.bAssisted);
    }
}

bool AIslandGameState::CanLaunch() const
{
    if (!bRoomLobby || bRoomBlocked || Phase != EIslandPhase::Waiting || JoinedCount == 0 || JoinedCount != PlayerArray.Num())
    {
        return false;
    }
    for (const APlayerState* Player : PlayerArray)
    {
        const auto* Member = Cast<AIslandPlayerState>(Player);
        if (!Member || Member->Team < 0 || !Member->bMapReady || (!Member->bHost && !Member->bRoomReady))
        {
            return false;
        }
    }
    return true;
}
