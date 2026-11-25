#include "Island/IslandPlayerController.h"
#include "ExpeditionText.h"
#include "Island/IslandNeutralProp.h"
#include "Island/IslandNeutralLayout.h"

#include "Components/InputComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LevelStreamingDynamic.h"
#include "Island/IslandHUD.h"
#include "Island/IslandWorld.h"
#include "Voxel/VoxelCharacter.h"
#include "Voxel/ExpeditionImpact.h"
#include "Engine/AssetManager.h"
#include "Engine/Engine.h"
#include "Engine/StreamableManager.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundWave.h"
#include "UObject/ConstructorHelpers.h"
#include "GameFeaturesSubsystem.h"
#include "Interfaces/IPluginManager.h"
#include "PartyMinigame.h"
#include "PartyVoxelArena.h"
#include "Island/IslandGameMode.h"
#include "Island/IslandState.h"

AIslandPlayerController::AIslandPlayerController()
{
    PlayerCameraManagerClass = AExpeditionImpactCamera::StaticClass();
    static ConstructorHelpers::FObjectFinder<USoundWave> Echo(TEXT("/Game/ActDemo/Audio/RockEcho"));
    EchoSound = Echo.Object;
}

void AIslandPlayerController::SetupInputComponent()
{
    Super::SetupInputComponent();
#if !UE_BUILD_SHIPPING
    if (FParse::Param(FCommandLine::Get(), TEXT("IslandQA"))
        && !FParse::Param(FCommandLine::Get(), TEXT("ExpeditionFieldQA"))
        && !FParse::Param(FCommandLine::Get(), TEXT("ExpeditionMudQA"))
        && !FParse::Param(FCommandLine::Get(), TEXT("ExpeditionGamesQA"))
        && !FParse::Param(FCommandLine::Get(), TEXT("ExpeditionNeutralQA")))
    {
        return;
    }
#endif
    if (InputComponent)
    {
        InputComponent->BindKey(EKeys::E, IE_Pressed, this, &AIslandPlayerController::Interact);
        InputComponent->BindKey(EKeys::G, IE_Pressed, this, &AIslandPlayerController::Rally);
        InputComponent->BindKey(EKeys::Tab, IE_Pressed, this, &AIslandPlayerController::OpenMap);
        InputComponent->BindKey(EKeys::Tab, IE_Released, this, &AIslandPlayerController::CloseMap);
        InputComponent->BindKey(EKeys::Escape, IE_Pressed, this, &AIslandPlayerController::ToggleMenu);
        InputComponent->BindKey(EKeys::F1, IE_Pressed, this, &AIslandPlayerController::OpenHelp);
        InputComponent->BindKey(EKeys::F1, IE_Released, this, &AIslandPlayerController::CloseHelp);
        InputComponent->BindKey(EKeys::Enter, IE_Pressed, this, &AIslandPlayerController::RestartIsland);
    }
}

int32 AIslandPlayerController::GetTeam() const
{
    const AIslandPlayerState* IslandState = GetPlayerState<AIslandPlayerState>();
    return IslandState ? IslandState->Team : -1;
}

bool AIslandPlayerController::CanUseIslandWorld() const
{
    const auto* HUD=GetHUD<AIslandHUD>();
    return PartyVeil == EIslandPartyVeil::Clear && IsPlaying() && (!HUD || HUD->IsAcceptingWorldInput());
}

void AIslandPlayerController::ToggleMenu()
{
    if (auto* HUD=GetHUD<AIslandHUD>()) HUD->ToggleMenu();
}

void AIslandPlayerController::OpenHelp() { bHelpOpen=CanUseIslandWorld() && !bMapOpen; }
void AIslandPlayerController::CloseHelp() { bHelpOpen=false; }

void AIslandPlayerController::OpenMap()
{
    if (!CanUseIslandWorld() || !IsVoxelInputEnabled() || GetTeam()<0) return;
    ReleaseHeldExpeditionInputs();
    StopRecover();
    StopClimb();
    bHelpOpen=false;
    bMapOpen=true;
}

void AIslandPlayerController::CloseMap()
{
    bMapOpen=false;
}

void AIslandPlayerController::ClearHeldIslandActions()
{
    CloseMap();
    bHelpOpen=false;
    ReleaseHeldExpeditionInputs();
    StopRecover();
    if (GetHero()) StopClimb();
}

void AIslandPlayerController::Interact()
{
    if (!CanUseIslandWorld() || !IsVoxelInputEnabled() || WaterState >= 2) return;
    if (UsesExpeditionAbilities()) DispatchExpeditionAction(EExpeditionAction::Interact);
    else ServerInteract();
}

void AIslandPlayerController::RestartIsland()
{
    const auto* GS=GetWorld()->GetGameState<AIslandGameState>();
    if (GS && !GS->bRoomLobby && IsVoxelInputEnabled()) ServerRestartIsland();
}

void AIslandPlayerController::Rally()
{
    if (!CanUseIslandWorld() || !IsVoxelInputEnabled() || WaterState >= 2) return;
    if (UsesExpeditionAbilities()) DispatchExpeditionAction(EExpeditionAction::Rally);
    else ServerRally();
}

void AIslandPlayerController::ServerRally_Implementation()
{
    if (UsesExpeditionAbilities()) return;
    if (auto* Mode = GetWorld()->GetAuthGameMode<AIslandGameMode>()) Mode->RallyCrew(*this);
}

void AIslandPlayerController::ClientCrewRally_Implementation(FVector_NetQuantize Position, bool bAtChest, double ExpiresAt)
{
    RallyPosition = Position;
    bRallyAtChest = bAtChest;
    RallyExpiresAt = ExpiresAt;
#if !UE_BUILD_SHIPPING
    if (FParse::Param(FCommandLine::Get(), TEXT("IslandQA")))
        UE_LOG(LogTemp, Display, TEXT("ISLAND_CREW_RALLY received team=%d chest=%d"), GetTeam(), bAtChest);
#endif
}

bool AIslandPlayerController::GetCrewRally(FVector& Position, bool& bAtChest) const
{
    const auto* GS = GetWorld()->GetGameState<AIslandGameState>();
    if (!GS || GS->Phase != EIslandPhase::Hunting || GS->IsIdentityDisguised() || GS->GetServerWorldTimeSeconds() >= RallyExpiresAt) return false;
    Position = RallyPosition;
    bAtChest = bRallyAtChest;
    return true;
}

AIslandChest* AIslandPlayerController::GetReachableChest() const
{
    const AVoxelCharacter* Hero=GetHero();
    if (!Hero) return nullptr;
    const FVector Origin=Hero->GetAimOrigin();
    double Nearest=FMath::Square(IslandRules::OpenReach);
    AIslandChest* Target=nullptr;
    for (TActorIterator<AIslandChest> It(GetWorld()); It; ++It)
    {
        AIslandChest* Chest=*It;
        const FVector Center=Chest->GetActorLocation();
        const double Distance=FVector::DistSquared(Origin,Center);
        if (Distance<=Nearest && Chest->IsReachableFrom(*Hero))
        {
            Target=Chest;
            Nearest=Distance;
        }
    }
    return Target;
}

void AIslandPlayerController::ServerInteract_Implementation()
{
    if (UsesExpeditionAbilities()) return;
    if (AIslandGameMode* Mode = GetWorld()->GetAuthGameMode<AIslandGameMode>())
    {
        Mode->TryOpen(*this);
    }
}

AIslandNeutralProp* AIslandPlayerController::GetReachableSail() const
{
    for (TActorIterator<AIslandNeutralProp> It(GetWorld()); It; ++It)
        if (It->GetKind() == EIslandNeutralKind::Sail && It->CanUseRope(*this)) return *It;
    return nullptr;
}

FString AIslandPlayerController::GetNeutralHint() const
{
    if (const auto* Sail = GetReachableSail())
        return Sail->GetPhase() == 0 ? EXPEDITION_TEXT("E  放下帆布", "E  Lower sail") : EXPEDITION_TEXT("E  收起帆布", "E  Raise sail");
    const auto* Hero = GetHero();
    if (!Hero) return {};
    const FVector Eye = Hero->GetAimOrigin();
    const FVector Forward = GetControlRotation().Vector();
    for (TActorIterator<AIslandNeutralProp> It(GetWorld()); It; ++It)
    {
        if (It->IsDemolished()) continue;
        const FVector Offset = It->GetInteractionPoint() - Eye;
        if (Offset.SizeSquared() > FMath::Square(480.) || FVector::DotProduct(Forward, Offset.GetSafeNormal()) < .72) continue;
        switch (It->GetKind())
        {
        case EIslandNeutralKind::Tree:
            if (It->GetPhase() < 2) return EXPEDITION_TEXT("左键敲树根", "LMB  Strike the tree roots");
            break;
        case EIslandNeutralKind::Ship: return EXPEDITION_TEXT("挖通船舱  ·  右键丢土搭上甲板", "Dig into the hold · RMB to build up to the deck");
        case EIslandNeutralKind::Hopper:
            if (It->GetPhase() == 0) return EXPEDITION_TEXT("左键敲木销放砂", "LMB  Knock out the pin to release sand");
            break;
        case EIslandNeutralKind::Tide: return EXPEDITION_TEXT("右键丢土堵一孔  ·  白泡冒起时跳入另一孔", "RMB to plug one vent · Jump into the other when it bubbles");
        default: break;
        }
    }
    return {};
}

void AIslandPlayerController::ServerRestartIsland_Implementation()
{
    if (IsLocalController())
    {
        if (AIslandGameMode* Mode = GetWorld()->GetAuthGameMode<AIslandGameMode>())
        {
            Mode->RestartIsland();
        }
    }
}

void AIslandPlayerController::ClientUpdateClues_Implementation(const FIslandClues& Snapshot)
{
    const bool bObtained = Snapshot.Mask != LocalClues.Mask && Snapshot.Mask != 0;
    LocalClues = Snapshot;
    if (bObtained && !Snapshot.bUsesKeyFragments)
        ClientEditRejected(FString::Format(EXPEDITION_TEXT("获得线索  {0} / 4", "Clue found  {0} / 4"), {Snapshot.Count()}));
}

void AIslandPlayerController::ClientWaterState_Implementation(uint8 State)
{
    if (State >= 2)
    {
        ReleaseHeldExpeditionInputs();
        StopRecover();
    }
    WaterState = State;
    WaterChangedAt = FPlatformTime::Seconds();
}

void AIslandPlayerController::ClientExcavationEcho_Implementation(bool bStrong)
{
    LastEchoAt = FPlatformTime::Seconds();
    bStrongEcho = bStrong;
    UGameplayStatics::PlaySound2D(this, EchoSound, bStrong ? .74f : .41f, bStrong ? 1.17f : .77f);
#if !UE_BUILD_SHIPPING
    if (FParse::Param(FCommandLine::Get(), TEXT("IslandQA")))
        UE_LOG(LogTemp, Display, TEXT("ISLAND_EXCAVATION_ECHO strong=%d"), bStrong);
#endif
}

bool AIslandPlayerController::HasChestClue(const AIslandChest& Chest) const
{
    if (Chest.IsPrize()) return false;
    for (int32 Index = 0; Index < LocalClues.Points.Num(); ++Index)
        if ((LocalClues.Mask & (1u << Index)) && LocalClues.Points[Index].Equals(FVector2D(Chest.GetActorLocation()), 1.))
            return true;
    return false;
}

FString AIslandPlayerController::WaterStatus() const
{
    if (WaterState == 2)
    {
        return EXPEDITION_TEXT("已溺水，即将返回本队码头", "Drowned · Returning to your dock");
    }
    if (WaterState == 1)
    {
        const int32 Left = FMath::Max(1, FMath::CeilToInt(IslandRules::DrownTime - (FPlatformTime::Seconds() - WaterChangedAt)));
        return FString::Format(EXPEDITION_TEXT("深水 · {0} 秒后溺水", "Deep water · Drowning in {0}s"), {Left});
    }
    return FString();
}

void AIslandPlayerController::PlayerTick(float DeltaTime)
{
    Super::PlayerTick(DeltaTime);
    if (IsLocalController() && LocalPartyRound != 0)
    {
        if (auto* PartyPawn = Cast<APartyVoxelPawn>(GetPawn()))
        {
            PartyPawn->SetLocalLookScale(GetLocalLookScale());
            PartyPawn->SetLocalFirstPersonView(WantsFirstPersonView());
        }
        UpdatePartyClient();
    }
}

namespace
{
    constexpr double EnterVeilSeconds = 1.35;
    constexpr double ReturnVeilSeconds = 1.05;
    constexpr double RevealVeilSeconds = .85;
}

FIslandPartyTransitionView AIslandPlayerController::GetPartyTransitionView() const
{
    const double Age = FMath::Max(0., FPlatformTime::Seconds() - VeilStart);
    float Coverage = 0.f;
    if (PartyVeil == EIslandPartyVeil::Black) Coverage = 1.f;
    else if (PartyVeil == EIslandPartyVeil::Closing)
        Coverage = FMath::Clamp(Age / (bPartyReturning ? ReturnVeilSeconds : EnterVeilSeconds), 0., 1.);
    else if (PartyVeil == EIslandPartyVeil::Opening)
        Coverage = 1. - FMath::Clamp(Age / RevealVeilSeconds, 0., 1.);
    return {PartyVeil, Coverage, bPartyReturning, LocalPartyRound};
}

void AIslandPlayerController::StartPartyVeil(bool bReturning)
{
    // An aborted load can already be partly or entirely black. Continue from that picture.
    const double PreviousCoverage = GetPartyTransitionView().Coverage;
    bPartyReturning = bReturning;
    bBlackReported = false;
    bRevealRequested = false;
    PartyVeil = EIslandPartyVeil::Closing;
    VeilStart = FPlatformTime::Seconds() - PreviousCoverage * (bReturning ? ReturnVeilSeconds : EnterVeilSeconds);
    UE_LOG(LogTemp, Display, TEXT("PARTY_VEIL stage=close round=%u direction=%s"), LocalPartyRound, bReturning ? TEXT("out") : TEXT("in"));
}

void AIslandPlayerController::SetPartyTravelLocked(bool bLocked)
{
    if (bPartyTravelLocked == bLocked) return;
    // Possession resets the engine's stacked ignore flags. The controller's
    // Is*InputIgnored overrides keep this gate intact until the veil clears.
    bPartyTravelLocked = bLocked;
    if (bLocked)
    {
        ClearHeldIslandActions();
        SetVoxelInputEnabled(false);
        if (auto* Character = Cast<ACharacter>(GetPawn()))
        {
            Character->StopJumping();
            Character->GetCharacterMovement()->StopMovementImmediately();
            Character->GetCharacterMovement()->DisableMovement();
        }
        if (auto* Pawn = Cast<APartyPawn>(GetPawn())) Pawn->ReleaseLocalPartyInput();
    }
    else if (GetHero())
    {
        const auto* HUD = GetHUD<AIslandHUD>();
        SetVoxelInputEnabled(!HUD || !HUD->IsMenuOpen());
    }
}

void AIslandPlayerController::TickPartyVeil()
{
    if (!bAnimateParty || PartyVeil == EIslandPartyVeil::Clear) return;
    const float Coverage = GetPartyTransitionView().Coverage;
    const TCHAR* Direction = bPartyReturning ? TEXT("out") : TEXT("in");
    if (PartyVeil == EIslandPartyVeil::Closing && Coverage >= 1.f)
    {
        // The viewport paints the last closing frame solid black. A stalled or occluded
        // renderer must not freeze the character while the player still sees the island.
        const auto* Camera = Cast<AExpeditionImpactCamera>(PlayerCameraManager);
        if (!Camera || !Camera->HasPaintedPassageBlack(LocalPartyRound, bPartyReturning)) return;
        PartyVeil = EIslandPartyVeil::Black;
        BlackAtFrame = GFrameCounter;
        SetPartyTravelLocked(true);
        UE_LOG(LogTemp, Display, TEXT("PARTY_VEIL stage=black round=%u direction=%s"), LocalPartyRound, Direction);
    }
    else if (PartyVeil == EIslandPartyVeil::Black && !bBlackReported && GFrameCounter > BlackAtFrame)
    {
        bBlackReported = true;
        ServerPartyScreenBlack(LocalPartyRound, bPartyReturning);
    }
    else if (PartyVeil == EIslandPartyVeil::Black && bBlackReported && bRevealRequested
        && (bPartyReturning ? GetHero() != nullptr : Cast<APartyPawn>(GetPawn()) != nullptr))
    {
        // A cancelled load keeps the same autonomous proxy, so there may be no
        // possession change to restore its movement mode on this client.
        if (bPartyReturning && GetHero()->GetCharacterMovement()->MovementMode == MOVE_None)
            GetHero()->GetCharacterMovement()->SetMovementMode(MOVE_Falling);
        PartyVeil = EIslandPartyVeil::Opening;
        VeilStart = FPlatformTime::Seconds();
        UE_LOG(LogTemp, Display, TEXT("PARTY_VEIL stage=open round=%u direction=%s"), LocalPartyRound, Direction);
    }
    else if (PartyVeil == EIslandPartyVeil::Opening && Coverage <= 0.f)
    {
        PartyVeil = EIslandPartyVeil::Clear;
        SetPartyTravelLocked(false);
        UE_LOG(LogTemp, Display, TEXT("PARTY_VEIL stage=clear round=%u direction=%s"), LocalPartyRound, Direction);
        ServerPartyScreenClear(LocalPartyRound, bPartyReturning);
    }
}

void AIslandPlayerController::ClientPrepareParty_Implementation(uint32 RoundId, FName Feature, bool bAnimate)
{
    if (RoundId <= LocalPartyRound)
    {
        return;
    }
    LocalPartyRound = RoundId;
    bAnimateParty = bAnimate;
    bCanUnloadParty = false;
    bWaitingForVenue = false;
    bPartyUnloadStarted = false;
    bPartyPawnReadySent = false;
    ClearHeldIslandActions();
    if (bAnimateParty) StartPartyVeil(false);
    else
    {
        bPartyReturning = false;
        SetVoxelInputEnabled(false);
    }

    UGameFeaturesSubsystem& Features = UGameFeaturesSubsystem::Get();
    if (!Features.GetPluginURLByName(Feature.ToString(), PartyPluginURL))
    {
        const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(Feature.ToString());
        if (Plugin && Plugin->GetDescriptor().bExplicitlyLoaded)
        {
            PartyPluginURL = UGameFeaturesSubsystem::GetPluginURL_FileProtocol(Plugin->GetDescriptorFileName());
        }
    }
    if (PartyPluginURL.IsEmpty())
    {
        ServerPartyAssetsReady(RoundId, false);
        return;
    }
    UE_LOG(LogTemp, Display, TEXT("PARTY_FEATURE_BEGIN round=%u installed=%d"), RoundId,
        Features.IsGameFeaturePluginInstalled(PartyPluginURL) && !Features.IsGameFeaturePluginMounted(PartyPluginURL));
#if !UE_BUILD_SHIPPING
    if (!HasAuthority() && FParse::Param(FCommandLine::Get(), TEXT("IslandPartySkipReady"))
        && (RoundId == 1 || !FParse::Param(FCommandLine::Get(), TEXT("ExpeditionAbortQA"))))
    {
        return;
    }
#endif
    bPartyActivating = true;
    Features.LoadAndActivateGameFeaturePlugin(PartyPluginURL,
        FGameFeaturePluginLoadComplete::CreateWeakLambda(this, [this, RoundId](const UE::GameFeatures::FResult& Result)
        {
            if (RoundId != LocalPartyRound)
            {
                return;
            }
            bPartyActivating = false;
            if (!bPartyReturning)
            {
                if (Result.HasValue())
                {
                    LoadPartyAssets(RoundId);
                }
                else
                {
                    UE_LOG(LogTemp, Warning, TEXT("PARTY_FEATURE_LOAD_FAILED round=%u %s"), RoundId, *Result.GetError());
                    ServerPartyAssetsReady(RoundId, false);
                }
            }
        }));
}

UClass* AIslandPlayerController::GetPartySessionClass() const
{
    return PartySessionClass.Get();
}

void AIslandPlayerController::LoadPartyAssets(uint32 RoundId)
{
    const auto* Data = Cast<UPartyMinigameFeatureData>(UGameFeaturesSubsystem::Get().GetGameFeatureDataForActivePluginByURL(PartyPluginURL));
    PartySessionClass = Data ? Data->SessionClass : TSoftClassPtr<APartySession>();
    if (Data && !Data->ArenaMap.IsNull())
    {
        bool bQueued = false;
        PartyVenue = ULevelStreamingDynamic::LoadLevelInstanceBySoftObjectPtr(this, Data->ArenaMap,
            APartyVoxelSession::ArenaOrigin(), FRotator::ZeroRotator, bQueued,
            FString::Printf(TEXT("IslandPartyVenue_%u"), RoundId), {}, true);
        if (!bQueued || !PartyVenue)
        {
            UE_LOG(LogTemp, Warning, TEXT("PARTY_VENUE_FAILED round=%u asset=%s"), RoundId, *Data->ArenaMap.ToString());
            ServerPartyAssetsReady(RoundId,false);
            return;
        }
        // Only the scenery lives in this streamed level. Pawns and the editable board
        // replicate in the persistent world, and our ready RPC covers local visibility.
        PartyVenue->bClientOnlyVisible = true;
        UE_LOG(LogTemp,Display,TEXT("PARTY_VENUE_REQUEST round=%u asset=%s"),RoundId,*Data->ArenaMap.ToString());
    }
    if (PartySessionClass.IsNull() || PartySessionClass.IsValid())
    {
        ReportPartyAssets(RoundId);
        return;
    }
    PartyAssets = UAssetManager::GetStreamableManager().RequestAsyncLoad(PartySessionClass.ToSoftObjectPath(),
        FStreamableDelegate::CreateWeakLambda(this, [this, RoundId]() { ReportPartyAssets(RoundId); }));
    if (!PartyAssets)
    {
        ReportPartyAssets(RoundId);
    }
}

void AIslandPlayerController::ReportPartyAssets(uint32 RoundId)
{
    if (RoundId == LocalPartyRound && !bPartyReturning)
    {
        if (PartySessionClass.IsValid() && PartyVenue && !PartyVenue->IsLevelVisible() &&
            PartyVenue->GetLevelStreamingState()!=ELevelStreamingState::FailedToLoad)
        {
            bWaitingForVenue = true;
            return;
        }
        bWaitingForVenue = false;
        const bool bReady = PartySessionClass.IsValid() && (!PartyVenue || PartyVenue->IsLevelVisible());
        if (bReady && PartyVenue)
            UE_LOG(LogTemp,Display,TEXT("PARTY_VENUE_VISIBLE round=%u"),RoundId);
        UE_LOG(LogTemp, Display, TEXT("PARTY_FEATURE_ACTIVE round=%u success=%d"), RoundId, bReady);
        ServerPartyAssetsReady(RoundId, bReady);
    }
}

void AIslandPlayerController::ClientBeginPartyReturn_Implementation(uint32 RoundId)
{
    if (RoundId != LocalPartyRound || bPartyReturning) return;
    if (bAnimateParty) StartPartyVeil(true);
    else bPartyReturning = true;
}

void AIslandPlayerController::ClientRevealParty_Implementation(uint32 RoundId, bool bReturning)
{
    if (RoundId == LocalPartyRound && bPartyReturning == bReturning) bRevealRequested = true;
}

void AIslandPlayerController::ClientReturnFromParty_Implementation(uint32 RoundId)
{
    if (RoundId != LocalPartyRound || bCanUnloadParty) return;
    bPartyReturning = true;
    bCanUnloadParty = true;
    if (APartyPawn* Pawn = Cast<APartyPawn>(GetPawn()))
    {
        Pawn->ReleaseLocalPartyInput();
    }
    ReleasePartyAssets();
    if (bPartyActivating)
    {
        UGameFeaturesSubsystem::Get().CancelGameFeatureStateChange(PartyPluginURL);
        bPartyActivating = false;
    }
}

void AIslandPlayerController::UpdatePartyClient()
{
    TickPartyVeil();
    if (!bPartyReturning)
    {
        if (bWaitingForVenue && PartyVenue && (PartyVenue->IsLevelVisible() ||
            PartyVenue->GetLevelStreamingState()==ELevelStreamingState::FailedToLoad))
            ReportPartyAssets(LocalPartyRound);
        const APartyPawn* Pawn = Cast<APartyPawn>(GetPawn());
        if (const auto* VoxelPawn = Cast<APartyVoxelPawn>(Pawn); VoxelPawn && VoxelPawn->GetSession()
            && PartyVenue && PartyVenue->IsLevelVisible() && PartyVenue->GetLoadedLevel())
            VoxelPawn->GetSession()->BindVenueLevel(*PartyVenue->GetLoadedLevel());
        if (!bPartyPawnReadySent && Pawn && Pawn->HasLocalPartyInput())
        {
            bPartyPawnReadySent = true;
            UE_LOG(LogTemp, Display, TEXT("PARTY_PAWN_READY round=%u"), LocalPartyRound);
            ServerPartyPawnReady(LocalPartyRound);
        }
        return;
    }

    if (!bCanUnloadParty || bPartyUnloadStarted || !GetHero() || TActorIterator<APartySession>(GetWorld()) || TActorIterator<APartyPawn>(GetWorld()))
    {
        return;
    }
    if (PartyVenue && (PartyVenue->HasLoadedLevel() || PartyVenue->IsStreamingStatePending())) return;
    PartyVenue=nullptr;
    bPartyUnloadStarted = true;
    if (!bAnimateParty) SetVoxelInputEnabled(true);
    const uint32 RoundId = LocalPartyRound;
    UGameFeaturesSubsystem& Features = UGameFeaturesSubsystem::Get();
    if (PartyPluginURL.IsEmpty() || (Features.IsGameFeaturePluginInstalled(PartyPluginURL) && !Features.IsGameFeaturePluginMounted(PartyPluginURL)))
    {
        FinishPartyUnload(RoundId, true);
        return;
    }
    Features.DeactivateGameFeaturePlugin(PartyPluginURL,
        FGameFeaturePluginDeactivateComplete::CreateWeakLambda(this, [this, RoundId](const UE::GameFeatures::FResult& Result)
        {
            if (RoundId != LocalPartyRound)
            {
                return;
            }
            if (!Result.HasValue())
            {
                UE_LOG(LogTemp, Warning, TEXT("PARTY_FEATURE_DEACTIVATE_FAILED round=%u %s"), RoundId, *Result.GetError());
            }
            UGameFeaturesSubsystem::Get().UnloadGameFeaturePlugin(PartyPluginURL,
                FGameFeaturePluginUnloadComplete::CreateWeakLambda(this, [this, RoundId](const UE::GameFeatures::FResult& Unload)
                {
                    const UGameFeaturesSubsystem& Subsystem = UGameFeaturesSubsystem::Get();
                    const bool bInstalled = Subsystem.IsGameFeaturePluginInstalled(PartyPluginURL) && !Subsystem.IsGameFeaturePluginMounted(PartyPluginURL);
                    if (!Unload.HasValue())
                    {
                        UE_LOG(LogTemp, Warning, TEXT("PARTY_FEATURE_UNLOAD_FAILED round=%u %s"), RoundId, *Unload.GetError());
                    }
                    FinishPartyUnload(RoundId, Unload.HasValue() && bInstalled);
                }));
        }));
}

void AIslandPlayerController::FinishPartyUnload(uint32 RoundId, bool bSuccess)
{
    if (RoundId != LocalPartyRound) return;
    UE_LOG(LogTemp, Display, TEXT("PARTY_FEATURE_INSTALLED round=%u success=%d"), RoundId, bSuccess);
    PartyPluginURL.Reset();
    ServerPartyUnloaded(RoundId, bSuccess);
}

void AIslandPlayerController::ReleasePartyAssets()
{
    bWaitingForVenue=false;
    if (PartyVenue)
    {
        PartyVenue->SetShouldBeVisible(false);
        PartyVenue->SetShouldBeLoaded(false);
        PartyVenue->SetIsRequestingUnloadAndRemoval(true);
    }
    if (PartyAssets)
    {
        PartyAssets->CancelHandle();
        PartyAssets.Reset();
    }
    PartySessionClass.Reset();
}

void AIslandPlayerController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    ReleasePartyAssets();
    if (IsLocalController() && !PartyPluginURL.IsEmpty() && GEngine)
    {
        if (UGameFeaturesSubsystem* Features = GEngine->GetEngineSubsystem<UGameFeaturesSubsystem>())
        {
            bPartyReturning = true;
            Features->CancelGameFeatureStateChange(PartyPluginURL);
            Features->UnloadGameFeaturePlugin(PartyPluginURL);
        }
    }
    Super::EndPlay(EndPlayReason);
}

void AIslandPlayerController::ServerPartyAssetsReady_Implementation(uint32 RoundId, bool bSuccess)
{
    if (AIslandGameMode* Mode = GetWorld()->GetAuthGameMode<AIslandGameMode>())
    {
        Mode->PartyAssetsReady(*this, RoundId, bSuccess);
    }
}

void AIslandPlayerController::ServerPartyScreenBlack_Implementation(uint32 RoundId, bool bReturning)
{
    if (auto* Mode = GetWorld()->GetAuthGameMode<AIslandGameMode>())
        Mode->PartyScreenBlack(*this, RoundId, bReturning);
}

void AIslandPlayerController::ServerPartyScreenClear_Implementation(uint32 RoundId, bool bReturning)
{
    if (auto* Mode = GetWorld()->GetAuthGameMode<AIslandGameMode>())
        Mode->PartyScreenClear(*this, RoundId, bReturning);
}

void AIslandPlayerController::ServerPartyPawnReady_Implementation(uint32 RoundId)
{
    if (AIslandGameMode* Mode = GetWorld()->GetAuthGameMode<AIslandGameMode>())
    {
        Mode->PartyPawnReady(*this, RoundId);
    }
}

void AIslandPlayerController::ServerPartyUnloaded_Implementation(uint32 RoundId, bool bSuccess)
{
    if (AIslandGameMode* Mode = GetWorld()->GetAuthGameMode<AIslandGameMode>())
    {
        Mode->PartyUnloaded(*this, RoundId, bSuccess);
    }
}

void AIslandPlayerController::ServerChooseTeam_Implementation(int32 Team)
{
    if (auto* Mode = GetWorld()->GetAuthGameMode<AIslandGameMode>())
    {
        Mode->ChooseTeam(*this, Team);
    }
}

void AIslandPlayerController::ServerSetRoomReady_Implementation(bool bReady)
{
    if (auto* Mode = GetWorld()->GetAuthGameMode<AIslandGameMode>())
    {
        Mode->SetRoomReady(*this, bReady);
    }
}

void AIslandPlayerController::ServerStartExpedition_Implementation()
{
    if (auto* Mode = GetWorld()->GetAuthGameMode<AIslandGameMode>())
    {
        Mode->StartExpedition(*this);
    }
}

void AIslandPlayerController::ServerConfigureRoom_Implementation(const FIslandRoomSettings& Settings)
{
    if (auto* Mode = GetWorld()->GetAuthGameMode<AIslandGameMode>())
    {
        Mode->ConfigureRoom(*this, Settings);
    }
}

void AIslandPlayerController::ServerReturnToRoom_Implementation()
{
    if (IsLocalController())
    {
        if (auto* Mode = GetWorld()->GetAuthGameMode<AIslandGameMode>())
        {
            Mode->RestartIsland();
        }
    }
}
