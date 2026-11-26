#include "Tests/IslandProbe.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Island/ExpeditionAnimalComponent.h"
#include "Island/ExpeditionDock.h"
#include "Island/IslandGameMode.h"
#include "Island/IslandPlayerController.h"
#include "Island/IslandState.h"
#include "PartyVoxelArena.h"
#include "PartyCrewRig.h"
#include "Voxel/VoxelCharacter.h"
#include "Voxel/VoxelTerrain.h"

namespace
{
    enum ECheck : int32
    {
        Initial, BothFox, GuestReady, ReadyStillSet, InvalidAndTeamChange, BeforeCountdown,
        GuestCancelled, PickAfterCancel, Island, Arena, IslandAgain, NewLobby, PickInNewLobby, Disguised, Revealed
    };
    const FName StaticBodies[] = {TEXT("SM_Pig"), TEXT("SM_Monkey"), TEXT("SM_Fox"), TEXT("SM_Calf")};
    const FName Skins[] = {TEXT("SK_ExpeditionPig"), TEXT("SK_ExpeditionMonkey"), TEXT("SK_ExpeditionFox"), TEXT("SK_ExpeditionCalf")};

    bool OnOtherShore(const AIslandGameState& Round, const AIslandPlayerState* Observer, const AIslandPlayerState& Member)
    {
        if (Round.Phase != EIslandPhase::Waiting || !Observer || !Observer->GetPawn() || Member.GetPawn()
            || Observer->Team == Member.Team) return false;
        TArray<int32> Teams;
        for (const auto& PS : Round.PlayerArray)
            if (const auto* Crew = Cast<AIslandPlayerState>(PS)) Teams.AddUnique(Crew->Team);
        FTransform Berth;
        return AExpeditionDock::FindStart(Round.GetWorld(), Teams, Member.Team, 0, Berth)
            && FVector::DistSquared(Berth.GetLocation(), Observer->GetPawn()->GetActorLocation())
                > Observer->GetPawn()->GetNetCullDistanceSquared();
    }

    // Read the actual character's mesh, including which body is visible to a teammate.
    int32 ObservePawn(const AIslandPlayerState& Member, bool bParty, bool bLocal, bool bDisguised)
    {
        const APawn* Pawn = Member.GetPawn();
        if (!Pawn || Pawn->IsHidden() || (bParty ? !Pawn->IsA<APartyVoxelPawn>() : !Pawn->IsA<AVoxelCharacter>())) return INDEX_NONE;
#if !UE_BUILD_SHIPPING
        if (const auto* Hero = Cast<AVoxelCharacter>(Pawn); Hero && !Hero->IsInFirstPersonView())
        {
            const auto* Animal = Hero->FindComponentByClass<UExpeditionAnimalComponent>();
            if (!Animal || !Animal->ShowsOutfit(bDisguised)) return INDEX_NONE;
        }
#endif
        TInlineComponentArray<UStaticMeshComponent*> Components(Pawn);
        for (const auto* Part : Components)
        {
            if (!Part->GetStaticMesh() || !Part->IsVisible()) continue;
            for (FName Legacy : StaticBodies)
                if (Part->GetStaticMesh()->GetFName()==Legacy) return INDEX_NONE;
        }
        const auto* Rig=Pawn->FindComponentByClass<UPartyCrewRig>();
        const int32 Selected=Member.GetCharacterModel();
        if (!Rig || Selected<0 || Selected>=UE_ARRAY_COUNT(Skins)) return INDEX_NONE;
        if (bDisguised && !Rig->IsVisible() && bParty && !Rig->GetSkinnedAsset()) return Selected;
        if (!Rig->GetSkinnedAsset() || Rig->GetSkinnedAsset()->GetFName()!=Skins[Selected]) return INDEX_NONE;
        if (bDisguised ? Rig->IsVisible() : (!bLocal && !Rig->IsVisible())) return INDEX_NONE;
        return Selected;
    }
}

void AIslandProbePeer::ClientCharacterInput_Implementation(int32 Model, int32 Team, int32 Ready, bool bModelLast)
{
    auto* PC = CastChecked<AIslandPlayerController>(GetOwner());
    if (!bModelLast && Model != INDEX_NONE) PC->ServerChooseCharacter(Model);
    if (Team >= 0) PC->ServerChooseTeam(Team);
    if (Ready >= 0) PC->ServerSetRoomReady(Ready != 0);
    if (bModelLast && Model != INDEX_NONE) PC->ServerChooseCharacter(Model);
}

void AIslandProbePeer::ClientCharacterInspect_Implementation(int32 Stage, EIslandPhase Phase, FIntVector Host,
    FIntVector Guest, int32 Members)
{
    if (CharacterLocallySeen == Stage) return;
    const auto* Round = GetWorld()->GetGameState<AIslandGameState>();
    if (!Round || Round->Phase != Phase || Round->PlayerArray.Num() != Members) return;
    if (Round->IsIdentityDisguised() != (Stage == Disguised)) return;
    const auto* Observer = CastChecked<AIslandPlayerController>(GetOwner())->GetPlayerState<AIslandPlayerState>();
    int32 ObservedHost = INDEX_NONE, ObservedGuest = INDEX_NONE, LiveMeshes = 0;
    for (const auto& PlayerState : Round->PlayerArray)
    {
        const auto* Member = Cast<AIslandPlayerState>(PlayerState);
        if (!Member || Member->IsOnlyASpectator()) return;
        const FIntVector Expected = Member->bHost ? Host : Guest;
        const int32 ExpectedModel = Expected.X == INDEX_NONE ? uint32(Member->GetPlayerId()) % 4 : Expected.X;
        const int32 Mesh = ObservePawn(*Member, Phase == EIslandPhase::PartyPlaying, Member == Observer, Stage == Disguised);
        const bool bDistant = Mesh == INDEX_NONE && OnOtherShore(*Round, Observer, *Member);
        if (Mesh == ExpectedModel) ++LiveMeshes;
        if (Member->GetCharacterModel() != ExpectedModel || (!bDistant && Mesh != ExpectedModel) ||
            (Expected.Y >= 0 && Member->Team != Expected.Y) || (Expected.Z >= 0 && int32(Member->bRoomReady) != Expected.Z))
        {
            UE_LOG(LogTemp,Display,TEXT("CHARACTER_PROOF_WAIT stage=%d host=%d own=%d ps=%d mesh=%d pawn=%s hidden=%d team=%d ready=%d expected=%s"),
                Stage,Member->bHost,Member==Observer,Member->GetCharacterModel(),Mesh,*GetNameSafe(Member->GetPawn()),
                Member->GetPawn() && Member->GetPawn()->IsHidden(),Member->Team,Member->bRoomReady,*Expected.ToString());
            return;
        }
        (Member->bHost ? ObservedHost : ObservedGuest) = ExpectedModel;
    }
    if (ObservedHost == INDEX_NONE || (Members == 2 && ObservedGuest == INDEX_NONE)) return;
    CharacterLocallySeen = Stage;
    UE_LOG(LogTemp, Display, TEXT("CHARACTER_PROOF_VIEW stage=%d host=%d guest=%d phase=%d live_meshes=%d"),
        Stage, ObservedHost, ObservedGuest, int32(Phase), LiveMeshes);
    ServerCharacterObserved(Stage);
}

void AIslandProbePeer::ServerCharacterObserved_Implementation(int32 Stage)
{
    CharacterObserved = Stage;
}

void AIslandProbe::TickCharacterSelection(double Now)
{
    Mode = GetWorld()->GetAuthGameMode<AIslandGameMode>();
    if (!Mode || !Mode->GetTerrain() || !Mode->GetTerrain()->IsReady()) return;
    auto* Round = Mode->GetGameState<AIslandGameState>();
    const auto& Players = Mode->GetPlayers();
    const bool bPair = ExpeditionPlayers == 2;
    const int32 FinalHost = bPair ? 1 : 3;
    const int32 FinalGuest = 0;
    const auto Next = [&](int32 Stage) { CharacterStage = Stage; CharacterStageAt = Now; NextCharacterPoll = 0.; };

    if (CharacterStage == INDEX_NONE)
    {
        if (Players.Num() != ExpeditionPlayers || !Round || Round->Phase != EIslandPhase::Waiting || Round->bRoomBlocked) return;
        for (AVoxelPlayerController* P : Players)
        {
            const auto* State = P->GetPlayerState<AIslandPlayerState>();
            if (!State || !State->bMapReady || !P->GetHero()) return;
        }
        for (AVoxelPlayerController* P : Players)
        {
            auto* Peer = GetWorld()->SpawnActor<AIslandProbePeer>();
            Peer->SetOwner(P);
            Peers.Add(Peer);
        }
        const bool bReturning = GetWorld()->URL.HasOption(TEXT("RoomRound="));
        if (!bReturning && bPair) Peers[1]->ClientCharacterInput(INDEX_NONE,0,-1);
        Next(bReturning ? NewLobby : Initial);
    }
    if (Now - CharacterStageAt > 75.)
    {
        UE_LOG(LogTemp, Error, TEXT("CHARACTER_PROOF_TIMEOUT stage=%d phase=%d"), CharacterStage, int32(Round->Phase));
        Require(false, TEXT("all independent peers observed the authoritative choice and actual pawn mesh"));
        return;
    }

    // Excavate a genuine chart site through the existing walkable-surface shovel probe.
    // Once the chest exists and the opener is in range, use the player's usual open action.
    if (CharacterStage == Arena && Round->Phase == EIslandPhase::Hunting)
    {
        if (RoomCheck == ERoomCheck::Dig) TickExpedition(Now);
        else if (RoomCheck == ERoomCheck::Games && Now >= NextCharacterPoll)
        {
            NextCharacterPoll = Now + .45;
            Peers[ExpeditionOpener]->ClientOpenChest();
        }
        return;
    }
    if (CharacterStage == Arena && Round->Phase == EIslandPhase::PartyLoading)
        if (auto* Session = Cast<APartyVoxelSession>(Mode->PartySession)) Session->RoundSeconds = 9.f;
    if (Now < NextCharacterPoll) return;
    FIntVector Host(-1,0,0), Guest(-1,-1,0); // model, team, ready flag; -1 skips team/ready or checks the original default model.
    EIslandPhase Phase = EIslandPhase::Waiting;
    switch (CharacterStage)
    {
    case Initial: Guest.Y = 0; break;
    case BothFox: Host.X = Guest.X = 2; Guest.Y = 0; break;
    case GuestReady: Host.X = 0; Guest = {1,1,1}; break;
    case ReadyStillSet: Host.X = 3; Guest = {3,1,1}; break;
    case InvalidAndTeamChange: Host.X = 3; Guest = {3,0,0}; break;
    case BeforeCountdown:
    case GuestCancelled: Host.X = 3; Guest = {1,1,CharacterStage == BeforeCountdown ? 1 : 0}; break;
    case PickAfterCancel: Host.X = FinalHost; Guest = {FinalGuest,1,1}; break;
    case Island:
    case Arena:
    case IslandAgain:
    case Disguised:
    case Revealed:
        Phase = CharacterStage == Arena ? EIslandPhase::PartyPlaying : EIslandPhase::Hunting;
        Host = {FinalHost,0,-1}; Guest = {FinalGuest,1,-1}; break;
    case NewLobby: Host.X = FinalHost; Guest = {FinalGuest,1,0}; break;
    case PickInNewLobby: Host.X = 1; Guest = {1,0,0}; break;
    default: return;
    }
    if (Round->Phase != Phase) return;
    if (!NextCharacterPoll && bPair && (CharacterStage == Island || CharacterStage == IslandAgain))
    {
        // The expedition's ordinary character replication radius is smaller than the distance
        // between two opposing landings. Bring both players together for the island mesh audit.
        auto* HostHero = Players[0]->GetHero();
        auto* GuestHero = Players[1]->GetHero();
        if (!HostHero || !GuestHero) return;
        Place(1, HostHero->GetActorLocation() + FVector(0,150,0));
    }
    NextCharacterPoll = Now + .2;
    bool bAll = true;
    for (AIslandProbePeer* Peer : Peers)
    {
        if (Peer->CharacterObserved == CharacterStage) continue;
        bAll = false;
        Peer->ClientCharacterInspect(CharacterStage, Phase, Host, Guest, Players.Num());
    }
    if (!bAll) return;

    auto* HostPC = CastChecked<AIslandPlayerController>(Players[0]);
    auto* HostPeer = Peers[0].Get();
    auto* GuestPeer = bPair ? Peers[1].Get() : nullptr;
    switch (CharacterStage)
    {
    case Initial:
        for (AIslandProbePeer* Peer : Peers)
        {
            Peer->ClientCharacterInput(2, -1, -1);
            Peer->ClientCharacterInput(2, -1, -1); // Repeated confirmation in the same room.
        }
        Next(BothFox); break;
    case BothFox:
        HostPeer->ClientCharacterInput(0,-1,-1);
        if (GuestPeer) GuestPeer->ClientCharacterInput(1,1,1,true);
        Next(GuestReady); break;
    case GuestReady:
        for (AIslandProbePeer* Peer : Peers) Peer->ClientCharacterInput(3,-1,-1);
        Next(ReadyStillSet); break;
    case ReadyStillSet:
        for (AIslandProbePeer* Peer : Peers) Peer->ClientCharacterInput(-7,-1,-1);
        HostPeer->ClientCharacterInput(IslandRules::CharacterModelCount,-1,-1);
        if (GuestPeer) GuestPeer->ClientCharacterInput(IslandRules::CharacterModelCount,0,-1);
        Next(InvalidAndTeamChange); break;
    case InvalidAndTeamChange:
        if (GuestPeer) GuestPeer->ClientCharacterInput(1,1,1);
        Next(BeforeCountdown); break;
    case BeforeCountdown:
        HostPC->ServerStartExpedition();
        Require(Round->Phase == EIslandPhase::Starting, TEXT("character choice allows a normal countdown"));
        HostPeer->ClientCharacterInput(0,-1,-1);
        // Both calls use the same reliable player channel. The withdrawal proves the preceding
        // change really reached the server during its locked countdown.
        if (GuestPeer) GuestPeer->ClientCharacterInput(2,-1,0);
        Next(bPair ? GuestCancelled : Island); break;
    case GuestCancelled:
        HostPeer->ClientCharacterInput(FinalHost,-1,-1);
        GuestPeer->ClientCharacterInput(FinalGuest,-1,1);
        Next(PickAfterCancel); break;
    case PickAfterCancel:
        HostPC->ServerStartExpedition();
        Require(Round->Phase == EIslandPhase::Starting, TEXT("choosing again after the withdrawal allows departure"));
        Next(Island); break;
    case Island:
        for (AIslandProbePeer* Peer : Peers) Peer->ClientCharacterInput(1,-1,-1);
        RoomCheck = ERoomCheck::Dig;
        bGamesScenario = true;
        ExpeditionOpener = 0;
        PrepareDig(0);
        Next(Arena); break;
    case Arena:
        for (AIslandProbePeer* Peer : Peers) Peer->ClientCharacterInput(1,-1,-1);
        Next(IslandAgain); break;
    case IslandAgain:
        // Exercise the real field effect and the per-viewer outfit masking with a chosen monkey.
        Round->PartyDecoyEvent = EIslandDecoyEvent::Disguise;
        Round->BeginDecoyEffect();
        Next(Disguised); break;
    case Disguised:
        Round->ClearDecoyEffect();
        Next(Revealed); break;
    case Revealed:
        // Fast-forward the unrelated treasure hunt to the normal winning and server-travel paths.
        for (int32 I=0; I<IslandRules::ClueCount; ++I) Mode->GiveClue(0,I);
        Mode->PublishWinner(0);
        Require(Round->Phase == EIslandPhase::Finished, TEXT("the original expedition reached its result before returning"));
        HostPC->ServerReturnToRoom();
        Advance(EStep::Done); break;
    case NewLobby:
        HostPeer->ClientCharacterInput(1,-1,-1);
        if (GuestPeer) GuestPeer->ClientCharacterInput(1,0,-1);
        Next(PickInNewLobby); break;
    case PickInNewLobby:
        UE_LOG(LogTemp,Display,TEXT("CHARACTER_PROOF_DONE members=%d returned=1"), ExpeditionPlayers);
        Advance(EStep::Done); break;
    }
}
