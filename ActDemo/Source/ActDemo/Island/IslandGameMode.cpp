#include "Island/IslandGameMode.h"
#include "Abilities/ExpeditionMudEffect.h"
#include "Island/IslandBreakable.h"
#include "Island/IslandNeutralProp.h"
#include "Island/IslandNeutralLayout.h"
#include "Island/ExpeditionDock.h"
#include "Island/IslandLocalPlayer.h"
#include "Island/IslandRoomSession.h"
#include "Kismet/GameplayStatics.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMeshActor.h"

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
#if !UE_BUILD_SHIPPING
    if (FParse::Param(FCommandLine::Get(), TEXT("IslandQA")))
    {
        GetWorld()->SpawnActor<AIslandProbe>();
    }
#endif
}

void AIslandGameMode::InitGame(const FString& MapName, const FString& Options, FString& Error)
{
    Super::InitGame(MapName, Options, Error);
    UIslandRoomSession* Session = GetGameInstance()->GetSubsystem<UIslandRoomSession>();
    bExpeditionRoom = MapName.EndsWith(TEXT("L_ExpeditionIsland")) || Session->IsManagedRoom();
    if (bExpeditionRoom)
    {
        Settings = Session->HostSettings;
        UIslandRoomRoster* Roster = GetGameInstance()->GetSubsystem<UIslandRoomRoster>();
        FGuid TravelId;
        if (FGuid::Parse(UGameplayStatics::ParseOption(Options, TEXT("RoomRound")), TravelId)
            && TravelId.IsValid() && TravelId == Roster->ReturnRound)
        {
            ReturningTeams = MoveTemp(Roster->ReturningTeams);
            ReturnDeadline = FPlatformTime::Seconds() + 45.0;
        }
        Roster->ReturningTeams.Reset();
        Roster->ReturnRound.Invalidate();
    }
}

void AIslandGameMode::InitGameState()
{
    Super::InitGameState();
    AIslandGameState* Round = GetGameState<AIslandGameState>();
    Round->bRoomLobby = bExpeditionRoom;
    Round->bRoomBlocked = !ReturningTeams.IsEmpty();
    if (Round->bRoomBlocked)
    {
        Round->RoomNotice = TEXT("正在等待原房间成员返回。");
    }
    Round->RoomSettings = Settings;
    if (!bExpeditionRoom)
    {
        Round->RoomSettings.MaxPlayers = IslandRules::Seats;
        Round->RoomSettings.MaxTeams = IslandRules::TeamCount;
        Round->RoomSettings.TeamCapacity = 2;
    }
}

FString AIslandGameMode::InitNewPlayer(APlayerController* Player, const FUniqueNetIdRepl& UniqueId, const FString& Options, const FString& Portal)
{
    const FString Error = Super::InitNewPlayer(Player, UniqueId, Options, Portal);
    if (bExpeditionRoom && Error.IsEmpty())
    {
        auto* State = Player->GetPlayerState<AIslandPlayerState>();
        FGuid::Parse(UGameplayStatics::ParseOption(Options, TEXT("RoomMember")), State->MemberId);
        if (!State->MemberId.IsValid())
        {
            State->MemberId = FGuid::NewGuid();
        }
        if (!ReturningTeams.IsEmpty() && !ReturningTeams.Contains(State->MemberId) && FPlatformTime::Seconds() < ReturnDeadline)
        {
            return TEXT("ISLAND_RETURNING");
        }
        for (const AVoxelPlayerController* Existing : GetPlayers())
        {
            if (Existing->GetPlayerState<AIslandPlayerState>()->MemberId == State->MemberId)
            {
                return TEXT("Duplicate room member.");
            }
        }
    }
    return Error;
}

void AIslandGameMode::Tick(float DeltaSeconds)
{
    if (bExpeditionRoom && GetGameState<AIslandGameState>()->Phase == EIslandPhase::Waiting && GetTerrain() && GetTerrain()->IsReady())
    {
        if (!ReturningTeams.IsEmpty() && FPlatformTime::Seconds() >= ReturnDeadline)
        {
            ReturningTeams.Reset();
            GetGameState<AIslandGameState>()->bRoomBlocked = false;
            GetGameState<AIslandGameState>()->RoomNotice = TEXT("未返回成员的预留席位已开放。");
            UpdateRoom();
        }
        for (AVoxelPlayerController* Player : GetPlayers())
        {
            auto* Member = Player->GetPlayerState<AIslandPlayerState>();
            if (!Member || Member->bMapReady || !Player->InitialTerrainChecksum.IsSet() || !Player->GetHero())
            {
                continue;
            }
            Member->bMapReady = Player->InitialTerrainChecksum.GetValue() == GetTerrain()->GetInitialChecksum();
            if (!Member->bMapReady)
            {
                Player->InitialTerrainChecksum.Reset();
                GetGameState<AIslandGameState>()->RoomNotice = TEXT("地图版本不一致，请使用同一版本后重新加入。");
            }
            Member->ForceNetUpdate();
        }
    }
    Super::Tick(DeltaSeconds);
    if (!BurialSites.IsEmpty()) CheckMudLandings();
}

bool AIslandGameMode::IsStartRequested() const
{
    const auto* Round = GetGameState<AIslandGameState>();
    return bExpeditionRoom ? Round && Round->Phase == EIslandPhase::Starting
        && Round->GetServerWorldTimeSeconds() >= Round->StartAtServerTime : Super::IsStartRequested();
}

bool AIslandGameMode::IsAdmissionLocked() const
{
    const auto* Round = GetGameState<AIslandGameState>();
    return bExpeditionRoom && Round && Round->Phase != EIslandPhase::Waiting;
}

FString AIslandGameMode::AdmissionError() const
{
    return bExpeditionRoom ? (IsAdmissionLocked() ? TEXT("ISLAND_STARTED") : TEXT("ISLAND_FULL")) : Super::AdmissionError();
}

void AIslandGameMode::RoundSetupFailed(const FString& Error)
{
    if (bExpeditionRoom)
    {
        GetGameState<AIslandGameState>()->bRoomBlocked = true;
        CancelStart(Error);
    }
}

bool AIslandGameMode::PrepareRound(FString& Error)
{
    if (bExpeditionRoom && !PositionCrews())
    {
        Error = TEXT("这张地图没有足够的独立出生位，请检查码头布局。");
        return false;
    }
    const int32 Decoys = bExpeditionRoom ? Settings.DecoyCount : FMath::Clamp(DecoyChestCount, 0, IslandRules::MaxDecoys);
    if (!bExpeditionRoom && Decoys != DecoyChestCount)
    {
        UE_LOG(LogTemp, Warning, TEXT("DecoyChestCount %d clamped to %d"), DecoyChestCount, Decoys);
    }
    const AVoxelTerrain* Terrain = GetTerrain();
    const auto& Circles = Terrain->GetSearchCircles();
    const auto& Density = *Terrain->GetDensity();
    const auto Survey = bExpeditionRoom ? IslandNeutral::Survey(*GetWorld(),*Terrain) : IslandNeutral::FSurface();
    const auto CanReachDecoy = [&Survey](FVector2D XY){ return IslandNeutral::CanReachDigSite(Survey,XY); };
    const TArray<FBox2D> SearchObstacles = bExpeditionRoom ? IslandRules::CollectSearchObstacles(*GetWorld()) : TArray<FBox2D>();
    if (!bExpeditionRoom && !IslandRules::GenerateLayout(0, Decoys, Circles, Density, HiddenLayout, &Error, true))
    {
        return false;
    }
    FIslandTreasureLayout RandomLayout;
    int32 LayoutSeed = static_cast<int32>(FPlatformTime::Cycles64());
#if !UE_BUILD_SHIPPING
    // Designers can replay a layout in the normal front end without starting an automated probe.
    FParse::Value(FCommandLine::Get(), TEXT("ExpeditionSeed="), LayoutSeed);
#endif
    if (IslandRules::GenerateLayout(LayoutSeed, Decoys, Circles, Density, RandomLayout, &Error, false, SearchObstacles, CanReachDecoy))
    {
        HiddenLayout = MoveTemp(RandomLayout);
    }
    else if (bExpeditionRoom)
    {
        return false;
    }
#if !UE_BUILD_SHIPPING
    if (FParse::Param(FCommandLine::Get(), TEXT("IslandQA")) && bExpeditionRoom)
    {
        UE_LOG(LogTemp, Display, TEXT("EXPEDITION_QA_SEED %d"), LayoutSeed);
        if ((FParse::Param(FCommandLine::Get(), TEXT("ExpeditionSearchAudit"))
            || FParse::Param(FCommandLine::Get(), TEXT("NeutralLayoutAudit")))
            && !IslandRules::AuditSearchAreas(Decoys, Circles, Density, SearchObstacles, CanReachDecoy))
        {
            Error = TEXT("搜索圈种子复验失败");
            return false;
        }
        if (FParse::Param(FCommandLine::Get(), TEXT("ExpeditionRepro")))
        {
            // Captured D shelf from crew-eight. Recompute the puzzle's actual line intersection.
            const FVector2D D(1830., -4673.);
            HiddenLayout.Centers[3] = FVector(D, Density.SurfaceAt(D).Get(0.) - 150.);
            FVector Intersection;
            if (!FMath::SegmentIntersection2D(HiddenLayout.Centers[0], HiddenLayout.Centers[2],
                HiddenLayout.Centers[1], HiddenLayout.Centers[3], Intersection))
            {
                Error = TEXT("Invalid reproduction fixture");
                return false;
            }
            HiddenLayout.Crossing = FVector2D(Intersection);
            HiddenLayout.Centers[IslandRules::PrizeIndex] = FVector(HiddenLayout.Crossing, Density.SurfaceAt(HiddenLayout.Crossing).Get(0.0) - 275.);
            if (!IslandRules::IsValidLayout(HiddenLayout, Circles, Density, &Error)) return false;
        }
    }
#endif
    if (bExpeditionRoom)
    {
        PrepareExpeditionParties(LayoutSeed);
#if !UE_BUILD_SHIPPING
        if (FParse::Param(FCommandLine::Get(), TEXT("IslandQA"))
            && FParse::Param(FCommandLine::Get(), TEXT("NeutralLayoutAudit"))
            && !IslandNeutral::Audit(Survey, Decoys, SearchObstacles))
        {
            Error = TEXT("中立场景物体种子复验失败");
            return false;
        }
#endif
        TArray<FIslandNeutralPlacement> Layout;
        IslandNeutral::Generate(Survey, LayoutSeed, HiddenLayout, Layout);
        for (const auto& Placement : Layout)
        {
            const FTransform Transform(FRotator(0., Placement.Yaw, 0.), FVector(Placement.Location), IslandNeutral::SiteScale(Placement.Kind));
            auto* Prop = GetWorld()->SpawnActorDeferred<AIslandNeutralProp>(AIslandNeutralProp::StaticClass(),
                Transform, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
            if (!Prop) continue;
            Prop->Initialize(Placement.Kind);
            Prop->FinishSpawning(Transform);
            NeutralProps.Add(Prop);
            if (Placement.Kind == EIslandNeutralKind::Tide) Prop->RefreshTide(*Terrain);
        }
    }
    Exposed.SetNum(HiddenLayout.Num());
    TeamClues.SetNum(bExpeditionRoom ? Settings.MaxTeams : IslandRules::TeamCount);
    for (FIslandClues& Clues : TeamClues) Clues.bUsesKeyFragments = bExpeditionRoom;
    return true;
}

void AIslandGameMode::PostLogin(APlayerController* NewPlayer)
{
    Super::PostLogin(NewPlayer);
    auto* Player = CastChecked<AIslandPlayerController>(NewPlayer);
    auto* Member = Player->GetPlayerState<AIslandPlayerState>();
    Member->bHost = NewPlayer->IsLocalController();
    if (bExpeditionRoom)
    {
        int32 Preferred = INDEX_NONE;
        if (ReturningTeams.RemoveAndCopyValue(Member->MemberId, Preferred) && ReturningTeams.IsEmpty())
        {
            GetGameState<AIslandGameState>()->bRoomBlocked = false;
            GetGameState<AIslandGameState>()->RoomNotice.Reset();
        }
        Member->Team = AvailableTeam(Preferred, Player);
        PositionCrews();
    }
    Player->ClientSetRoundActive(false, FString());
    UpdateRoom();
}

void AIslandGameMode::Logout(AController* Exiting)
{
    if (AIslandPlayerController* Player = Cast<AIslandPlayerController>(Exiting))
    {
        RemovePartyPlayer(*Player);
        SeaStates.Remove(Player);
        NextRallyAt.Remove(Player);
        BurialSites.Remove(Player);
    }
    Super::Logout(Exiting);
    if (bExpeditionRoom && !bRestarting)
    {
        CancelStart(TEXT("有成员离开，请重新确认出航。"));
        if (GetGameState<AIslandGameState>()->Phase == EIslandPhase::Waiting)
        {
            PositionCrews();
        }
    }
    UpdateRoom();
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

int32 AIslandGameMode::AvailableTeam(int32 Preferred, const AIslandPlayerController* Except) const
{
    TArray<int32> Counts;
    Counts.Init(0, Settings.MaxTeams);
    for (const AVoxelPlayerController* Existing : GetPlayers())
    {
        const auto* Crew = CastChecked<AIslandPlayerController>(Existing);
        if (Crew != Except && Counts.IsValidIndex(Crew->GetTeam()))
        {
            ++Counts[Crew->GetTeam()];
        }
    }
    if (Preferred != INDEX_NONE)
    {
        return Counts.IsValidIndex(Preferred) && Counts[Preferred] < Settings.TeamCapacity ? Preferred : INDEX_NONE;
    }
    int32 Best = INDEX_NONE;
    for (int32 Team = 0; Team < Counts.Num(); ++Team)
    {
        if (Counts[Team] < Settings.TeamCapacity && (Best == INDEX_NONE || Counts[Team] < Counts[Best]))
        {
            Best = Team;
        }
    }
    return Best;
}

bool AIslandGameMode::PositionCrews()
{
    TArray<int32> Occupied;
    TMap<int32, int32> NextSlot;
    TArray<FVector> Starts;
    for (const AVoxelPlayerController* Player : GetPlayers())
    {
        Occupied.AddUnique(Player->GetPlayerState<AIslandPlayerState>()->Team);
    }
    for (AVoxelPlayerController* Player : GetPlayers())
    {
        const int32 Team = Player->GetPlayerState<AIslandPlayerState>()->Team;
        FTransform Start;
        if (AExpeditionDock::FindStart(GetWorld(), Occupied, Team, NextSlot.FindOrAdd(Team)++, Start))
        {
            const FVector Location = Start.GetLocation();
            if (Starts.ContainsByPredicate([&Location](const FVector& Existing)
                { return FVector::DistSquared(Existing, Location) < FMath::Square(70.0); }))
            {
                return false;
            }
            Starts.Add(Location);
            AVoxelCharacter* Hero = Player->GetHero();
            if (Hero && (!Player->StartTransform.Equals(Start) || FVector::DistSquared(Hero->GetActorLocation(), Start.GetLocation()) > 10000))
            {
                Hero->ReturnToStart(Start);
                Player->ClientResetView(Start.Rotator());
            }
            Player->StartTransform = Start;
        }
    }
    return Starts.Num() == GetPlayers().Num();
}

void AIslandGameMode::UpdateRoom()
{
    auto* Round = GetGameState<AIslandGameState>();
    if (!Round)
    {
        return;
    }
    Round->JoinedCount = GetPlayers().Num();
    if (bExpeditionRoom && !bRestarting && !GetWorld()->bIsTearingDown)
    {
        Round->RoomSettings = Settings;
        GetGameInstance()->GetSubsystem<UIslandRoomSession>()->UpdateAdvertisement(Settings, GetPlayers().Num(), IsAdmissionLocked() || !ReturningTeams.IsEmpty());
    }
    Round->ForceNetUpdate();
}

void AIslandGameMode::CancelStart(const FString& Reason)
{
    auto* Round = GetGameState<AIslandGameState>();
    if (Round && Round->Phase == EIslandPhase::Starting)
    {
        Round->Phase = EIslandPhase::Waiting;
        Round->StartAtServerTime = 0.0;
        Round->RoomNotice = Reason;
        UpdateRoom();
    }
}

void AIslandGameMode::ChooseTeam(AIslandPlayerController& Player, int32 Team)
{
    auto* Round = GetGameState<AIslandGameState>();
    auto* Member = Player.GetPlayerState<AIslandPlayerState>();
    if (!bExpeditionRoom || !Member || Round->Phase != EIslandPhase::Waiting || Team < 0 || Member->Team == Team)
    {
        return;
    }
    if (AvailableTeam(Team, &Player) == INDEX_NONE)
    {
        Player.ClientEditRejected(TEXT("该队已满或已关闭。"));
        return;
    }
    Member->Team = Team;
    Member->bRoomReady = false;
    Member->ForceNetUpdate();
    PositionCrews();
}

void AIslandGameMode::SetRoomReady(AIslandPlayerController& Player, bool bReady)
{
    auto* Round = GetGameState<AIslandGameState>();
    auto* Member = Player.GetPlayerState<AIslandPlayerState>();
    if (!bExpeditionRoom || !Member || Member->bHost || (Round->Phase != EIslandPhase::Waiting
        && !(Round->Phase == EIslandPhase::Starting && !bReady)))
    {
        return;
    }
    Member->bRoomReady = bReady;
    Member->ForceNetUpdate();
    if (!bReady)
    {
        CancelStart(TEXT("有成员取消准备。"));
    }
}

void AIslandGameMode::StartExpedition(AIslandPlayerController& Player)
{
    auto* Round = GetGameState<AIslandGameState>();
    if (Player.IsLocalController() && Round->CanLaunch())
    {
        Round->Phase = EIslandPhase::Starting;
        Round->StartAtServerTime = Round->GetServerWorldTimeSeconds() + 3.0;
        Round->RoomNotice.Reset();
        UpdateRoom();
    }
}

void AIslandGameMode::ConfigureRoom(AIslandPlayerController& Player, const FIslandRoomSettings& NewSettings)
{
    if (!bExpeditionRoom || !Player.IsLocalController() || GetGameState<AIslandGameState>()->Phase != EIslandPhase::Waiting)
    {
        return;
    }
    if (!NewSettings.IsValid() || NewSettings.MaxPlayers < GetPlayers().Num())
    {
        Player.ClientEditRejected(TEXT("房间设置必须能容纳当前所有玩家。"));
        return;
    }
    TArray<int32> Counts;
    Counts.Init(0, NewSettings.MaxTeams);
    for (const AVoxelPlayerController* Existing : GetPlayers())
    {
        const int32 Team = Existing->GetPlayerState<AIslandPlayerState>()->Team;
        if (!Counts.IsValidIndex(Team) || ++Counts[Team] > NewSettings.TeamCapacity)
        {
            Player.ClientEditRejected(TEXT("请先调整队伍，当前席位超出新设置。"));
            return;
        }
    }
    const bool bRulesChanged = Settings.MaxPlayers != NewSettings.MaxPlayers || Settings.MaxTeams != NewSettings.MaxTeams
        || Settings.TeamCapacity != NewSettings.TeamCapacity || Settings.DecoyCount != NewSettings.DecoyCount;
    Settings = NewSettings;
    Settings.RoomName.TrimStartAndEndInline();
    GetGameInstance()->GetSubsystem<UIslandRoomSession>()->HostSettings = Settings;
    if (bRulesChanged)
    {
        for (AVoxelPlayerController* Existing : GetPlayers())
        {
            auto* Member = Existing->GetPlayerState<AIslandPlayerState>();
            Member->bRoomReady = false;
            Member->ForceNetUpdate();
        }
        GetGameState<AIslandGameState>()->RoomNotice = TEXT("房间设置已更新，请重新准备。");
    }
    UpdateRoom();
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
    auto* Round = GetGameState<AIslandGameState>();
    Round->Phase = EIslandPhase::Hunting;
    if (bExpeditionRoom)
    {
        for (const AVoxelPlayerController* Player : GetPlayers())
        {
            Round->LaunchedTeams.AddUnique(Player->GetPlayerState<AIslandPlayerState>()->Team);
        }
        Round->LaunchedTeams.Sort();
        Round->ActiveTeams = Round->LaunchedTeams.Num();
        for (int32 Team : Round->LaunchedTeams) SendClueSnapshot(Team);
        PositionCrews();
        UpdateRoom();
        return;
    }
    Round->LaunchedTeams = {0, 1, 2};
    Round->ActiveTeams = IslandRules::TeamCount;
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

bool AIslandGameMode::TryStrikeProp(AVoxelCharacter& Hero, const FVector& Aim)
{
    const FVector Start = Hero.GetAimOrigin();
    FCollisionQueryParams Query(SCENE_QUERY_STAT(IslandPropStrike), false, &Hero);
    FHitResult Hit;
    if (AIslandNeutralProp::TraceShovel(*GetWorld(), Hit, Start, Start + Aim * AimReach, Query))
    {
        if (auto* Neutral = Cast<AIslandNeutralProp>(Hit.GetActor()))
        {
            if (auto* Player = Cast<AIslandPlayerController>(Hero.GetController())) return Neutral->Strike(*Player, Hit);
        }
        if (AIslandBreakable* Prop = Cast<AIslandBreakable>(Hit.GetActor()))
        {
            return Prop->Break(Aim);
        }
        return AIslandSceneryDamage::RemoveAuthoredPiece(Hit);
    }
    return false;
}

void AIslandGameMode::RevealChestIfClear(int32 Index)
{
    if (Exposed[Index] || ConsumedDecoys.Contains(Index)) return;
    const bool bPrize = Index == IslandRules::PrizeIndex;
    if (bPrize && bExpeditionRoom && !TeamClues.ContainsByPredicate([](const FIslandClues& Team) { return Team.Complete(); }))
        return;

    const FVector Center = HiddenLayout.GetCenter(Index);
    if (!GetTerrain()->IsVolumeClear(FBox::BuildAABB(Center, IslandRules::ChestExtent))) return;
    if (AIslandChest* Chest = GetWorld()->SpawnActor<AIslandChest>(Center, FRotator::ZeroRotator))
    {
        Chest->SetPrize(bPrize);
        Exposed[Index] = Chest;
    }
}

void AIslandGameMode::OnTerrainCommitted(const FVoxelEdit& Edit, AVoxelPlayerController* Source)
{
    for (AIslandNeutralProp* Prop : NeutralProps)
        if (Prop->GetKind() == EIslandNeutralKind::Tide) Prop->RefreshTide(*GetTerrain());
    if (Edit.Action == EVoxelAction::Fill)
    {
        return;
    }
    AIslandPlayerController* Digger = bExpeditionRoom ? Cast<AIslandPlayerController>(Source) : nullptr;
    const FVector BrushCenter(Edit.Center);
    const double BrushRadius = VoxelPlayground::RadiusFor(Edit.BrushId);
    bool bFaintEcho = false;
    bool bStrongEcho = false;
    for (int32 Index = 0; Index < Exposed.Num(); ++Index)
    {
        const FVector Center = HiddenLayout.GetCenter(Index);
        const FBox Bounds = FBox::BuildAABB(Center, IslandRules::ChestExtent);
        RevealChestIfClear(Index);
        if (Digger && !Exposed[Index] && CanClaimChest(Digger->GetTeam(), Index))
        {
            const double ToBox = FMath::Sqrt(Bounds.ComputeSquaredDistanceToPoint(BrushCenter));
            const double Gap = ToBox - BrushRadius;
            const auto& Circles = GetTerrain()->GetSearchCircles();
            const int32 Area = HiddenLayout.IsDecoy(Index) ? HiddenLayout.Decoys[Index - HiddenLayout.Centers.Num()].ClueIndex : Index;
            const double HearingRange = Circles.IsValidIndex(Area)
                ? FMath::Clamp(Circles[Area].Radius * .55, 550., 1200.) : 550.;
            bFaintEcho |= Gap <= HearingRange;
            bStrongEcho |= Gap <= 250.;
        }
    }
    if (Digger && bFaintEcho)
    {
        Digger->ClientExcavationEcho(bStrongEcho);
    }
}

void AIslandGameMode::QueueHopperSand(AIslandNeutralProp& Hopper, const TArray<FVector>& Centers)
{
    const TWeakObjectPtr<AIslandNeutralProp> Target(&Hopper);
    EnqueueEnvironmentFill(Centers, [Target](bool bCommitted)
    {
        if (auto* Prop = Target.Get()) Prop->CompleteSandRelease(bCommitted);
    });
}

void AIslandGameMode::OnPlayerJumped(AVoxelCharacter& Hero)
{
    const auto* Player = Cast<AIslandPlayerController>(Hero.GetController());
    if (!bExpeditionRoom || !Player || !CanPlayerAct(*Player)) return;
    for (AIslandNeutralProp* Prop : NeutralProps)
        if (Prop->GetKind() == EIslandNeutralKind::Tide && Prop->TryTideJump(Hero)) break;
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
        && Super::CanPlayerAct(Player) && (!Sea || Sea->RespawnAt == 0.0)
        && (!bExpeditionRoom || !Player.GetHero()->IsMudStunned());
}

int32 AIslandGameMode::FindReachableChest(const AIslandPlayerController& Player) const
{
    const AIslandChest* Chest = Player.GetReachableChest();
    return Chest ? Exposed.IndexOfByKey(Chest) : INDEX_NONE;
}

void AIslandGameMode::RallyCrew(AIslandPlayerController& Sender)
{
    const int32 Team = Sender.GetTeam();
    if (!bExpeditionRoom || !CanPlayerAct(Sender) || !TeamClues.IsValidIndex(Team)) return;
    if (GetGameState<AIslandGameState>()->IsIdentityDisguised())
    {
        Sender.ClientEditRejected(TEXT("身份混淆期间无法发送集合信号"));
        return;
    }
    const double Now = GetGameState<AIslandGameState>()->GetServerWorldTimeSeconds();
    double& NextAllowed = NextRallyAt.FindOrAdd(&Sender);
    if (Now < NextAllowed) return;
    NextAllowed = Now + 2.0;

    TArray<AIslandPlayerController*, TInlineAllocator<8>> Crew;
    for (AVoxelPlayerController* Controller : GetPlayers())
    {
        auto* Member = CastChecked<AIslandPlayerController>(Controller);
        if (Member->GetTeam() == Team) Crew.Add(Member);
    }
    if (Crew.Num() < 2)
    {
        Sender.ClientEditRejected(TEXT("当前没有同队玩家可以集合"));
        return;
    }

    if (!ChestLocks.IsEmpty())
    {
        UpdateChestLocks(Now);
        PublishLockViews();
        if (!CanPlayerAct(Sender)) return;
    }
    const AVoxelCharacter* Hero = Sender.GetHero();
    const FVector Feet = Hero->GetActorLocation() - FVector::UpVector * Hero->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
    const bool bAtChest = CanClaimChest(Team, FindReachableChest(Sender));
    // Each owner's reliable channel delivers team replacements in server order.
    for (AIslandPlayerController* Member : Crew)
    {
        Member->ClientCrewRally(Feet, bAtChest, Now + 15.0);
    }
}

void AIslandGameMode::TryOpen(AIslandPlayerController& Player)
{
    const int32 Team = Player.GetTeam();
    if (!CanPlayerAct(Player) || !TeamClues.IsValidIndex(Team))
    {
        return;
    }
    const int32 Index = FindReachableChest(Player);
    if (Index == INDEX_NONE)
    {
        if (bExpeditionRoom)
            if (auto* Sail = Player.GetReachableSail())
            {
                Sail->UseRope(Player);
                return;
            }
        Player.ClientEditRejected(TEXT("Reach an exposed chest"));
    }
    else if (HiddenLayout.IsDecoy(Index) && !bExpeditionRoom)
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
    else if (bExpeditionRoom)
    {
        if (Index == IslandRules::PrizeIndex)
        {
            WorkChest(Player, Index);
            PublishLockViews();
        }
        else if (!HiddenLayout.IsDecoy(Index) && (TeamClues[Team].Mask & (1u << Index)))
            Player.ClientEditRejected(TEXT("本队已取得这个箱子的碎片"));
        else if (!CanClaimChest(Team, Index))
            Player.ClientEditRejected(TEXT("此箱的小游戏暂时无法进入"));
        else
        {
            UpdateChestLocks(GetGameState<AIslandGameState>()->GetServerWorldTimeSeconds());
            if (!CanPlayerAct(Player)) return;
            if (Exposed[Index]->IsChallengeResolved()) GiveClue(Team, Index);
            else BeginParty(Index, Team);
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

bool AIslandGameMode::GiveClue(int32 Team, int32 Index)
{
    if (!TeamClues.IsValidIndex(Team) || Index < 0 || Index >= IslandRules::ClueCount
        || (TeamClues[Team].Mask & (1u << Index))) return false;
    FIslandClues& Snapshot = TeamClues[Team];
    Snapshot.Mask |= 1u << Index;
    Snapshot.ScoutedMask &= ~(1u << Index);
    Snapshot.Points[Index] = FVector2D(HiddenLayout.Centers[Index]);
    if (Snapshot.Complete())
    {
        Snapshot.Crossing = HiddenLayout.Crossing;
        if (bExpeditionRoom) RevealChestIfClear(IslandRules::PrizeIndex);
    }
    SendClueSnapshot(Team);
    return true;
}

void AIslandGameMode::ScoutTreasureForWinner(int32 Team, int32 PreferredSite)
{
    if (!TeamClues.IsValidIndex(Team)) return;
    FIslandClues& Snapshot = TeamClues[Team];
    for (int32 Offset = 0; Offset < IslandRules::ClueCount; ++Offset)
    {
        const int32 Site = (PreferredSite + Offset) % IslandRules::ClueCount;
        const uint8 Bit = 1u << Site;
        if ((Snapshot.Mask | Snapshot.ScoutedMask) & Bit) continue;
        Snapshot.ScoutedMask |= Bit;
        Snapshot.Points[Site] = FVector2D(HiddenLayout.Centers[Site]);
        SendClueSnapshot(Team);
        return;
    }
}

void AIslandGameMode::SendClueSnapshot(int32 Team)
{
    for (AVoxelPlayerController* Player : GetPlayers())
    {
        AIslandPlayerController* Member = CastChecked<AIslandPlayerController>(Player);
        if (Member->GetTeam() == Team)
        {
            Member->ClientUpdateClues(TeamClues[Team]);
        }
    }
}

void AIslandGameMode::PublishWinner(int32 Team)
{
    AIslandGameState* Round = GetGameState<AIslandGameState>();
    if (Round->Phase != EIslandPhase::Hunting || !TeamClues.IsValidIndex(Team)) return;
    if (bExpeditionRoom)
    {
        if (!TeamClues[Team].CanOpenPrize()) return;
        TeamClues[Team].bKeySpent = true;
        SendClueSnapshot(Team);
        Round->DisguiseUntil = 0.;
    }
    Round->Winner = Team;
    Round->Phase = EIslandPhase::Finished;
    Round->ForceNetUpdate();
    FinishRound(FString());
    UpdateRoom();
    UE_LOG(LogTemp, Display, TEXT("ISLAND_WINNER team=%d"), Team);
}

void AIslandGameMode::RestartIsland()
{
    if (!bRestarting && GetGameState<AIslandGameState>()->Phase == EIslandPhase::Finished)
    {
        bRestarting = true;
        const FString Level = UWorld::RemovePIEPrefix(GetWorld()->GetOutermost()->GetName());
        FString URL = Level + TEXT("?listen");
        const auto* Session = GetGameInstance()->GetSubsystem<UIslandRoomSession>();
        if (Session->GetRoomNetwork() == EIslandRoomNetwork::LAN)
        {
            URL += TEXT("?bIsLanMatch");
        }
        if (bExpeditionRoom)
        {
            auto* Round = GetGameState<AIslandGameState>();
            Round->RoomNotice = TEXT("正在返回房间…");
            Round->bRoomBlocked = true;
            Round->ForceNetUpdate();
            auto* Roster = GetGameInstance()->GetSubsystem<UIslandRoomRoster>();
            Roster->ReturningTeams.Reset();
            for (const AVoxelPlayerController* Player : GetPlayers())
            {
                const auto* Member = Player->GetPlayerState<AIslandPlayerState>();
                Roster->ReturningTeams.Add(Member->MemberId, Member->Team);
            }
            Roster->ReturnRound = FGuid::NewGuid();
            URL += TEXT("?RoomRound=") + Roster->ReturnRound.ToString();
        }
        if (!GetWorld()->ServerTravel(URL))
        {
            bRestarting = false;
            if (bExpeditionRoom)
            {
                auto* Round = GetGameState<AIslandGameState>();
                Round->bRoomBlocked = false;
                Round->RoomNotice = TEXT("返回房间失败，请重试。");
                Round->ForceNetUpdate();
            }
        }
    }
}

FTransform AIslandGameMode::FindSafeReturn(const AVoxelPlayerController& Player, TOptional<FVector> InlandTarget)
{
    const FTransform& Original = Player.StartTransform;
    const AVoxelCharacter* Hero = Player.GetHero();
    const UCapsuleComponent* Capsule = Hero ? Hero->GetCapsuleComponent()
        : GetDefault<AVoxelCharacter>()->GetCapsuleComponent();
    const double HalfHeight = Capsule->GetScaledCapsuleHalfHeight();
    const FCollisionShape Shape = FCollisionShape::MakeCapsule(Capsule->GetScaledCapsuleRadius(), HalfHeight);
    const FCollisionQueryParams Query(SCENE_QUERY_STAT(IslandSafeReturn), false, Hero);
    const FCollisionResponseParams Responses(Capsule->GetCollisionResponseToChannels());
    const auto FindFloor = [&](const FVector& Start, double Reach) -> TOptional<FTransform>
    {
        FHitResult Hit;
        if (!GetWorld()->LineTraceSingleByChannel(Hit, Start, Start - FVector(0, 0, Reach), ECC_Visibility, Query)
            || Hit.ImpactPoint.Z < 0 || Hit.ImpactNormal.Z < .72
            || (!Cast<AVoxelTerrain>(Hit.GetActor()) && !Cast<AIslandBreakable>(Hit.GetActor())
                && !Cast<AStaticMeshActor>(Hit.GetActor())))
        {
            return {};
        }
        const FVector Location = Hit.ImpactPoint + FVector(0, 0, HalfHeight + 4.0);
        if (GetWorld()->OverlapBlockingTestByChannel(Location, FQuat::Identity, Capsule->GetCollisionObjectType(), Shape, Query, Responses))
        {
            return {};
        }
        return FTransform(Original.GetRotation(), Location);
    };

    if (InlandTarget.IsSet())
    {
        // Sample the current collision after excavation. Accept only a walkable floor with room for the capsule.
        for (double Radius : {0., 300., 600., 1000.})
        {
            const int32 Steps = Radius == 0. ? 1 : 12;
            for (int32 Step = 0; Step < Steps; ++Step)
            {
                const double Angle = Step * UE_TWO_PI / Steps;
                const FVector Start = InlandTarget.GetValue() + FVector(Radius * FMath::Cos(Angle), Radius * FMath::Sin(Angle), 2000.);
                if (const auto Floor = FindFloor(Start, 2100.)) return Floor.GetValue();
            }
        }
    }
    if (const TOptional<FTransform> Result = FindFloor(Original.GetLocation(), HalfHeight + 80.0))
    {
        if (Original.GetLocation().Z >= Result->GetLocation().Z - 4.0
            && !GetWorld()->OverlapBlockingTestByChannel(Original.GetLocation(), FQuat::Identity,
                Capsule->GetCollisionObjectType(), Shape, Query, Responses))
        {
            return Original;
        }
        return Result.GetValue();
    }

    for (double Radius : {150.0, 300.0, 450.0, 600.0, 900.0, 1200.0})
    {
        for (int32 Step = 0; Step < 12; ++Step)
        {
            const double Angle = Step * UE_TWO_PI / 12;
            const FVector Offset(FMath::Cos(Angle) * Radius, FMath::Sin(Angle) * Radius, 1000.0);
            if (const TOptional<FTransform> Result = FindFloor(Original.GetLocation() + Offset, 1400.0))
            {
                return Result.GetValue();
            }
        }
    }

    const int32 InlandExtent = FMath::Max(0, GetTerrain()->GetShoreExtent() - 200);
    const int32 RecoveryStep = FMath::Max(300, FMath::DivideAndRoundUp(2 * InlandExtent, 14));
    for (int32 X = -InlandExtent; X <= InlandExtent; X += RecoveryStep)
    {
        for (int32 Y = -InlandExtent; Y <= InlandExtent; Y += RecoveryStep)
        {
            if (const TOptional<FTransform> Result = FindFloor(FVector(X, Y, 1000), 1100.0))
            {
                return Result.GetValue();
            }
        }
    }

    FVector Outward = Original.GetLocation().GetSafeNormal2D();
    if (Outward.IsNearlyZero())
    {
        Outward = FVector::ForwardVector;
    }
    const FVector Escape = Outward * FMath::Max(Original.GetLocation().Size2D() + 650.0, GetTerrain()->GetShoreExtent() + 800.0);
    if (const TOptional<FTransform> Result = FindFloor(Escape + FVector(0, 0, 500), 550))
    {
        return Result.GetValue();
    }
    const FVector Platform(Escape.X, Escape.Y, 65.0);
    FActorSpawnParameters Spawn;
    Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    GetWorld()->SpawnActor<AIslandRescueRaft>(Platform, FRotator::ZeroRotator, Spawn);
    return FTransform(Original.GetRotation(), Platform + FVector(0, 0, 21 + HalfHeight));
}

void AIslandGameMode::ReturnToSafeGround(AVoxelPlayerController& Player)
{
    if (AVoxelCharacter* Hero = Player.GetHero())
    {
        BurialSites.Remove(Cast<AIslandPlayerController>(&Player));
        UExpeditionMudMarkEffect::Clear(*Hero);
        const FTransform Target = FindSafeReturn(Player);
        Hero->ReturnToStart(Target);
        Player.ClientResetView(Target.Rotator());
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
        ReturnToSafeGround(Player);
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
        if (GetTerrain()->IsInSea(Position + FVector(0, 0, 80.0)))
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
    if (!ChestLocks.IsEmpty())
    {
        const double ServerTime = GetGameState<AIslandGameState>()->GetServerWorldTimeSeconds();
        if (ServerTime >= NextLockCheck)
        {
            NextLockCheck = ServerTime + .1;
            UpdateChestLocks(ServerTime);
            PublishLockViews();
        }
    }
}

void AIslandGameMode::SetPhase(EIslandPhase Phase)
{
    AIslandGameState* Round = GetGameState<AIslandGameState>();
    Round->Phase = Phase;
    Round->ForceNetUpdate();
}
