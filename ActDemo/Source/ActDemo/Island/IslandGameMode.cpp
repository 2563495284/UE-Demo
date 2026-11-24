#include "Island/IslandGameMode.h"

#include "Algo/AllOf.h"
#include "Components/CapsuleComponent.h"
#include "EngineUtils.h"
#include "PartyMinigame.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerStart.h"
#include "Island/IslandHUD.h"
#include "Island/IslandPlayerController.h"
#include "Island/IslandState.h"
#include "Island/IslandWorld.h"
#include "Tests/IslandProbe.h"
#include "Voxel/VoxelCharacter.h"
#include "Voxel/VoxelTerrain.h"

AIslandGameMode::AIslandGameMode()
{
    PlayerControllerClass = AIslandPlayerController::StaticClass();
    GameStateClass = AIslandGameState::StaticClass();
    PlayerStateClass = AIslandPlayerState::StaticClass();
    HUDClass = AIslandHUD::StaticClass();
}

void AIslandGameMode::BeginPlay()
{
    Super::BeginPlay();
    const int32 Decoys = FMath::Clamp(DecoyChestCount, 0, IslandRules::MaxDecoys);
    if (Decoys != DecoyChestCount)
    {
        UE_LOG(LogTemp, Warning, TEXT("DecoyChestCount %d clamped to %d"), DecoyChestCount, Decoys);
    }
    HiddenLayout = IslandRules::GenerateLayout(static_cast<int32>(FPlatformTime::Cycles64()), Decoys);
    Exposed.SetNum(HiddenLayout.Num());
#if !UE_BUILD_SHIPPING
    if (FParse::Param(FCommandLine::Get(), TEXT("IslandQA")))
    {
        GetWorld()->SpawnActor<AIslandProbe>();
    }
#endif
}

void AIslandGameMode::PostLogin(APlayerController* NewPlayer)
{
    Super::PostLogin(NewPlayer);
    GetGameState<AIslandGameState>()->JoinedCount = GetPlayers().Num();
    CastChecked<AIslandPlayerController>(NewPlayer)->ClientSetRoundActive(false, FString());
}

void AIslandGameMode::Logout(AController* Exiting)
{
    if (AIslandPlayerController* Player = Cast<AIslandPlayerController>(Exiting))
    {
        RemovePartyPlayer(*Player);
        SeaStates.Remove(Player);
    }
    Super::Logout(Exiting);
    GetGameState<AIslandGameState>()->JoinedCount = GetPlayers().Num();
    if (IsPlaying())
    {
        UE_LOG(LogTemp, Display, TEXT("ISLAND_GUEST_LEFT players=%d active=1"), GetPlayers().Num());
    }
}

AActor* AIslandGameMode::FindDock(int32 Seat) const
{
    const FName Tag(*FString::Printf(TEXT("Seat%d"), Seat));
    for (TActorIterator<APlayerStart> It(GetWorld()); It; ++It)
    {
        if (It->PlayerStartTag == Tag)
        {
            return *It;
        }
    }
    return nullptr;
}

AActor* AIslandGameMode::ChoosePlayerStart_Implementation(AController* Player)
{
    const int32 ExistingSeat = GetPlayers().IndexOfByKey(Cast<AVoxelPlayerController>(Player));
    if (AActor* Dock = FindDock(ExistingSeat == INDEX_NONE ? GetPlayers().Num() : ExistingSeat))
    {
        CastChecked<AIslandPlayerController>(Player)->StartTransform = Dock->GetActorTransform();
        return Dock;
    }
    return Super::ChoosePlayerStart_Implementation(Player);
}

void AIslandGameMode::OnRoundStarted()
{
    GetGameState<AIslandGameState>()->Phase = EIslandPhase::Hunting;
    for (int32 Seat = 0; Seat < GetPlayers().Num(); ++Seat)
    {
        auto& Player = *GetPlayers()[Seat];
        Player.GetPlayerState<AIslandPlayerState>()->Team = Seat % IslandRules::TeamCount;
        if (const AActor* Dock = FindDock(Seat))
        {
            Player.StartTransform = Dock->GetActorTransform();
            RecoverPlayer(Player);
        }
    }
}

void AIslandGameMode::OnTerrainCommitted(const FVoxelEdit& Edit)
{
    if (Edit.Action == EVoxelAction::Fill)
    {
        return;
    }
    for (int32 Index = 0; Index < Exposed.Num(); ++Index)
    {
        const FVector Center = HiddenLayout.GetCenter(Index);
        const FBox Bounds = FBox::BuildAABB(Center, IslandRules::ChestExtent);
        if (!Exposed[Index] && !ConsumedDecoys.Contains(Index) && GetTerrain()->IsVolumeClear(Bounds))
        {
            if (AIslandChest* Chest = GetWorld()->SpawnActor<AIslandChest>(Center, FRotator::ZeroRotator))
            {
                Chest->SetPrize(Index == IslandRules::PrizeIndex);
                Exposed[Index] = Chest;
            }
        }
    }
}

bool AIslandGameMode::CanFillAt(const FVoxelEdit& Edit) const
{
    const FSphere Fill(FVector(Edit.Center), VoxelPlayground::RadiusFor(Edit.BrushId) + VoxelPlayground::Spacing * .5);
    for (const AIslandChest* Chest : Exposed)
    {
        if (Chest && FBox::BuildAABB(Chest->GetActorLocation(), IslandRules::ChestExtent).ComputeSquaredDistanceToPoint(Fill.Center)
            <= FMath::Square(Fill.W))
        {
            return false;
        }
    }
    return true;
}

bool AIslandGameMode::CanPlayerAct(const AVoxelPlayerController& Player) const
{
    const FSeaState* Sea = SeaStates.Find(&Player);
    return GetGameState<AIslandGameState>()->Phase == EIslandPhase::Hunting
        && Super::CanPlayerAct(Player) && (!Sea || Sea->RespawnAt == 0.0);
}

int32 AIslandGameMode::FindReachableChest(const AIslandPlayerController& Player) const
{
    const AVoxelCharacter* Hero = Player.GetHero();
    const FVector Start = Hero->GetAimOrigin();
    FCollisionQueryParams Params(SCENE_QUERY_STAT(IslandChest), false, Hero);
    double Nearest = FMath::Square(IslandRules::OpenReach);
    int32 Selected = INDEX_NONE;
    for (int32 Index = 0; Index < Exposed.Num(); ++Index)
    {
        const AIslandChest* Chest = Exposed[Index];
        if (!Chest)
        {
            continue;
        }
        const FVector Target = Chest->GetActorLocation();
        const double Distance = FVector::DistSquared(Start, Target);
        FHitResult Hit;
        if (Distance <= Nearest && GetWorld()->LineTraceSingleByChannel(Hit, Start, Target, ECC_Visibility, Params)
            && Hit.GetActor() == Chest)
        {
            Selected = Index;
            Nearest = Distance;
        }
    }
    return Selected;
}

void AIslandGameMode::TryOpen(AIslandPlayerController& Player)
{
    const int32 Team = Player.GetTeam();
    if (!CanPlayerAct(Player) || Team < 0 || Team >= IslandRules::TeamCount)
    {
        return;
    }
    const int32 Index = FindReachableChest(Player);
    if (Index == INDEX_NONE)
    {
        Player.ClientEditRejected(TEXT("Reach an exposed chest"));
    }
    else if (HiddenLayout.IsDecoy(Index))
    {
        if (bPartyUnavailable)
        {
            Player.ClientEditRejected(TEXT("This party challenge is unavailable"));
        }
        else
        {
            BeginParty(Index);
        }
    }
    else if (Index == IslandRules::PrizeIndex)
    {
        if (TeamClues[Team].Complete())
        {
            PublishWinner(Team);
        }
        else
        {
            Player.ClientEditRejected(TEXT("Collect all four clues to open the prize"));
        }
    }
    else if ((TeamClues[Team].Mask & (1u << Index)) != 0)
    {
        Player.ClientEditRejected(TEXT("Your crew already has this clue"));
#if !UE_BUILD_SHIPPING
        if (FParse::Param(FCommandLine::Get(), TEXT("IslandQA")))
        {
            UE_LOG(LogTemp, Display, TEXT("ISLAND_QA_DUPLICATE team=%d chest=%d"), Team, Index);
        }
#endif
    }
    else
    {
        GiveClue(Team, Index);
    }
}

void AIslandGameMode::GiveClue(int32 Team, int32 Index)
{
    FIslandClues& Snapshot = TeamClues[Team];
    Snapshot.Mask |= 1u << Index;
    Snapshot.Points[Index] = FVector2D(HiddenLayout.Centers[Index]);
    if (Snapshot.Complete())
    {
        Snapshot.Crossing = HiddenLayout.Crossing;
    }
    for (AVoxelPlayerController* Player : GetPlayers())
    {
        AIslandPlayerController* Member = CastChecked<AIslandPlayerController>(Player);
        if (Member->GetTeam() == Team)
        {
            Member->ClientUpdateClues(Snapshot);
        }
    }
}

void AIslandGameMode::PublishWinner(int32 Team)
{
    AIslandGameState* Round = GetGameState<AIslandGameState>();
    Round->Winner = Team;
    Round->Phase = EIslandPhase::Finished;
    Round->ForceNetUpdate();
    FinishRound(FString());
    UE_LOG(LogTemp, Display, TEXT("ISLAND_WINNER team=%d"), Team);
}

void AIslandGameMode::RestartIsland()
{
    if (!bRestarting && GetGameState<AIslandGameState>()->Phase == EIslandPhase::Finished)
    {
        bRestarting = true;
        GetWorld()->ServerTravel(TEXT("/Game/TreasureIsland/L_TreasureIsland?listen"));
    }
}

void AIslandGameMode::RecoverPlayer(AVoxelPlayerController& Player)
{
    const EIslandPhase Phase = GetGameState<AIslandGameState>()->Phase;
    if (Phase == EIslandPhase::PartyLoading || Phase == EIslandPhase::PartyPlaying || Phase == EIslandPhase::PartyReturning)
    {
        return;
    }
    if (const FSeaState* Sea = SeaStates.Find(&Player); !Sea || Sea->RespawnAt == 0.0)
    {
        SeaStates.Remove(&Player);
        CastChecked<AIslandPlayerController>(&Player)->ClientWaterState(0);
        Super::RecoverPlayer(Player);
    }
}

void AIslandGameMode::UpdateEnvironment()
{
    if (GetGameState<AIslandGameState>()->Phase != EIslandPhase::Hunting)
    {
        UpdateParty();
        return;
    }
    const double Now = FPlatformTime::Seconds();
    for (AVoxelPlayerController* Controller : GetPlayers())
    {
        AIslandPlayerController& Player = *CastChecked<AIslandPlayerController>(Controller);
        AVoxelCharacter* Hero = Player.GetHero();
        if (!Hero)
        {
            continue;
        }
        const FVector Position = Hero->GetActorLocation();
        if (FSeaState* Sea = SeaStates.Find(&Player); Sea && Sea->RespawnAt > 0.0)
        {
            if (Now >= Sea->RespawnAt)
            {
                SeaStates.Remove(&Player);
                RecoverPlayer(Player);
            }
            continue;
        }
        if (IslandRules::InSea(Position + FVector(0, 0, 80.0)))
        {
            if (FSeaState* Sea = SeaStates.Find(&Player))
            {
                if (Now - Sea->ImmersedAt >= IslandRules::DrownTime)
                {
                    Sea->RespawnAt = Now + IslandRules::RespawnTime;
                    Hero->SetWantsClimb(false);
                    Hero->GetCharacterMovement()->StopMovementImmediately();
                    Hero->GetCharacterMovement()->DisableMovement();
                    Player.ClientWaterState(2);
                }
            }
            else
            {
                SeaStates.Add(&Player, {Now, 0.0});
                Player.ClientWaterState(1);
            }
        }
        else
        {
            if (SeaStates.Remove(&Player) != 0)
            {
                Player.ClientWaterState(0);
            }
            if (Position.Z < -970.0)
            {
                RecoverPlayer(Player);
            }
        }
    }
}

void AIslandGameMode::SetPhase(EIslandPhase Phase)
{
    AIslandGameState* Round = GetGameState<AIslandGameState>();
    Round->Phase = Phase;
    Round->ForceNetUpdate();
}

void AIslandGameMode::BeginParty(int32 ChestIndex)
{
    constexpr double PrepareTimeout = 15.0;
    SetPhase(EIslandPhase::PartyLoading);
    ++PartyRoundId;
    PartyChestIndex = ChestIndex;
    PartyDeadline = FPlatformTime::Seconds() + PrepareTimeout;
    bPartyPrepareFailed = false;

    for (int32 Seat = 0; Seat < GetPlayers().Num(); ++Seat)
    {
        auto* Player = CastChecked<AIslandPlayerController>(GetPlayers()[Seat]);
        FPartyPlayer& Entry = PartyPlayers.Add(Player);
        Entry.Seat = Seat;
        Entry.View = Player->GetControlRotation();
        Entry.bReturnToDock = SeaStates.Contains(Player);
        Entry.Original = Player->GetHero();
        if (AVoxelCharacter* Hero = Entry.Original.Get())
        {
            Hero->SetWantsClimb(false);
            Hero->StopJumping();
            Hero->GetCharacterMovement()->StopMovementImmediately();
            Hero->GetCharacterMovement()->DisableMovement();
        }
        else
        {
            bPartyPrepareFailed = true;
        }
        Player->SetVoxelInputEnabled(false);
    }
    UE_LOG(LogTemp, Display, TEXT("PARTY_PREPARE round=%u chest=%d players=%d"), PartyRoundId, ChestIndex, PartyPlayers.Num());
    for (const auto& Pair : PartyPlayers)
    {
        Pair.Key->ClientPrepareParty(PartyRoundId, DecoyMinigameFeature);
    }
}

AIslandGameMode::FPartyPlayer* AIslandGameMode::FindPartyPlayer(AIslandPlayerController& Player, uint32 RoundId)
{
    return RoundId == PartyRoundId ? PartyPlayers.Find(&Player) : nullptr;
}

void AIslandGameMode::PartyAssetsReady(AIslandPlayerController& Player, uint32 RoundId, bool bSuccess)
{
    if (GetGameState<AIslandGameState>()->Phase == EIslandPhase::PartyLoading)
    {
        if (FPartyPlayer* Entry = FindPartyPlayer(Player, RoundId); Entry && !Entry->bAssetsReady)
        {
            Entry->bAssetsReady = bSuccess;
            bPartyPrepareFailed |= !bSuccess;
        }
    }
}

void AIslandGameMode::PartyPawnReady(AIslandPlayerController& Player, uint32 RoundId)
{
    if (GetGameState<AIslandGameState>()->Phase == EIslandPhase::PartyLoading)
    {
        if (FPartyPlayer* Entry = FindPartyPlayer(Player, RoundId); Entry && Entry->Temporary.IsValid()
            && Entry->Temporary.Get() == Player.GetPawn())
        {
            Entry->bPawnReady = true;
        }
    }
}

void AIslandGameMode::PartyUnloaded(AIslandPlayerController& Player, uint32 RoundId, bool bSuccess)
{
    if (GetGameState<AIslandGameState>()->Phase == EIslandPhase::PartyReturning)
    {
        if (FPartyPlayer* Entry = FindPartyPlayer(Player, RoundId); Entry && !Entry->bUnloaded)
        {
            Entry->bUnloaded = true;
            bPartyUnavailable |= !bSuccess;
        }
    }
}

void AIslandGameMode::UpdateParty()
{
    const EIslandPhase Phase = GetGameState<AIslandGameState>()->Phase;
    const bool bTimedOut = FPlatformTime::Seconds() >= PartyDeadline;
    if (Phase == EIslandPhase::PartyReturning)
    {
        const bool bAllUnloaded = Algo::AllOf(PartyPlayers, [](const auto& Pair) { return Pair.Value.bUnloaded; });
        if (bAllUnloaded || bTimedOut)
        {
            bPartyUnavailable |= !bAllUnloaded;
            PartyPlayers.Reset();
            PartyChestIndex = INDEX_NONE;
            SetPhase(EIslandPhase::Hunting);
            UE_LOG(LogTemp, Display, TEXT("PARTY_HUNTING round=%u unloaded=%d available=%d"), PartyRoundId, bAllUnloaded, !bPartyUnavailable);
        }
        return;
    }
    if (Phase != EIslandPhase::PartyLoading)
    {
        return;
    }
    if (bPartyPrepareFailed || bTimedOut)
    {
        UE_LOG(LogTemp, Display, TEXT("PARTY_ABORT round=%u timeout=%d"), PartyRoundId, bTimedOut);
        ReturnFromParty();
        return;
    }

    if (!PartySession)
    {
        if (!Algo::AllOf(PartyPlayers, [](const auto& Pair) { return Pair.Value.bAssetsReady; }) || GetTerrain()->IsBusy())
        {
            return;
        }
        UClass* SessionClass = nullptr;
        for (const auto& Pair : PartyPlayers)
        {
            if (Pair.Key->IsLocalController())
            {
                SessionClass = Pair.Key->GetPartySessionClass();
                break;
            }
        }
        if (SessionClass)
        {
            PartySession = GetWorld()->SpawnActor<APartySession>(SessionClass, FVector(8000, 0, 1700), FRotator::ZeroRotator);
        }
        if (!PartySession)
        {
            bPartyPrepareFailed = true;
            return;
        }
        PartySession->OnPartyFinished.BindUObject(this, &AIslandGameMode::ReturnFromParty);
        for (auto& Pair : PartyPlayers)
        {
            FPartyPlayer& Entry = Pair.Value;
            APartyPawn* Pawn = PartySession->CreateParticipant(Entry.Seat);
            if (!Pawn)
            {
                bPartyPrepareFailed = true;
                return;
            }
            Entry.Temporary = Pawn;
            Pair.Key->Possess(Pawn);
            bPartyPrepareFailed |= Pair.Key->GetPawn() != Pawn;
        }
        return;
    }

    if (Algo::AllOf(PartyPlayers, [](const auto& Pair) { return Pair.Value.bPawnReady; }))
    {
        Exposed[PartyChestIndex]->Destroy();
        Exposed[PartyChestIndex] = nullptr;
        ConsumedDecoys.Add(PartyChestIndex);
        SetPhase(EIslandPhase::PartyPlaying);
        PartySession->StartParty();
        UE_LOG(LogTemp, Display, TEXT("PARTY_START round=%u chest=%d players=%d"), PartyRoundId, PartyChestIndex, PartyPlayers.Num());
    }
}

void AIslandGameMode::ReturnFromParty()
{
    constexpr double ReturnTimeout = 15.0;
    SetPhase(EIslandPhase::PartyReturning);
    PartyDeadline = FPlatformTime::Seconds() + ReturnTimeout;

    for (auto& Pair : PartyPlayers)
    {
        AIslandPlayerController* Controller = Pair.Key.Get();
        if (!Controller)
        {
            continue;
        }
        AIslandPlayerController& Player = *Controller;
        FPartyPlayer& Entry = Pair.Value;
        if (AVoxelCharacter* Hero = Entry.Original.Get())
        {
            if (Player.GetPawn() != Hero)
            {
                Player.Possess(Hero);
            }
            const UCapsuleComponent* Capsule = Hero->GetCapsuleComponent();
            const FCollisionShape Shape = FCollisionShape::MakeCapsule(Capsule->GetScaledCapsuleRadius(), Capsule->GetScaledCapsuleHalfHeight());
            const bool bBlocked = GetWorld()->OverlapBlockingTestByProfile(Hero->GetActorLocation(),
                Hero->GetActorQuat(), Capsule->GetCollisionProfileName(), Shape, FCollisionQueryParams(SCENE_QUERY_STAT(IslandPartyReturn), false, Hero));
            if (Entry.bReturnToDock || bBlocked)
            {
                Super::RecoverPlayer(Player);
            }
            else
            {
                Hero->GetCharacterMovement()->SetMovementMode(MOVE_Falling);
                Player.SetControlRotation(Entry.View);
                Player.ClientResetView(Entry.View);
            }
        }
        else
        {
            Player.UnPossess();
            RestartPlayerAtTransform(&Player, Player.StartTransform);
            Player.ClientResetView(Player.StartTransform.Rotator());
        }
        if (APartyPawn* Pawn = Entry.Temporary.Get())
        {
            Pawn->Destroy();
        }
        SeaStates.Remove(&Player);
        Player.ClientWaterState(0);
        Player.SetVoxelInputEnabled(true);
    }
    if (PartySession)
    {
        PartySession->Destroy();
        PartySession = nullptr;
    }
    for (const auto& Pair : PartyPlayers)
    {
        if (AIslandPlayerController* Player = Pair.Key.Get())
        {
            if (!ConsumedDecoys.Contains(PartyChestIndex))
            {
                Player->ClientEditRejected(TEXT("Party challenge could not start"));
            }
            Player->ClientReturnFromParty(PartyRoundId);
        }
    }
    UE_LOG(LogTemp, Display, TEXT("PARTY_RETURN round=%u"), PartyRoundId);
}

void AIslandGameMode::RemovePartyPlayer(AIslandPlayerController& Player)
{
    FPartyPlayer Entry;
    if (!PartyPlayers.RemoveAndCopyValue(&Player, Entry))
    {
        return;
    }
    if (PartySession)
    {
        PartySession->RemoveParticipant(Entry.Seat);
    }
    if (APartyPawn* Pawn = Entry.Temporary.Get())
    {
        Pawn->Destroy();
    }
    if (AVoxelCharacter* Hero = Entry.Original.Get(); Hero && !Hero->GetController())
    {
        Hero->Destroy();
    }
}
