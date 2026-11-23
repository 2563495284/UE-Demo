#include "Island/IslandGameMode.h"

#include "EngineUtils.h"
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
    HiddenLayout = IslandRules::GenerateLayout(static_cast<int32>(FPlatformTime::Cycles64()));
    Exposed.SetNum(IslandRules::ClueCount + 1);
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
    SeaStates.Remove(Cast<AVoxelPlayerController>(Exiting));
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
        const FVector Center = HiddenLayout.Centers[Index];
        const FBox Bounds = FBox::BuildAABB(Center, IslandRules::ChestExtent);
        if (!Exposed[Index] && GetTerrain()->IsVolumeClear(Bounds))
        {
            AIslandChest* Chest = GetWorld()->SpawnActor<AIslandChest>(Center, FRotator::ZeroRotator);
            Chest->SetPrize(Index == IslandRules::PrizeIndex);
            Exposed[Index] = Chest;
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
    return Super::CanPlayerAct(Player) && (!Sea || Sea->RespawnAt == 0.0);
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
    if (const FSeaState* Sea = SeaStates.Find(&Player); !Sea || Sea->RespawnAt == 0.0)
    {
        SeaStates.Remove(&Player);
        CastChecked<AIslandPlayerController>(&Player)->ClientWaterState(0);
        Super::RecoverPlayer(Player);
    }
}

void AIslandGameMode::UpdateEnvironment()
{
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
