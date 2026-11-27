#include "Island/IslandPlayerController.h"
#include "Island/IslandEchoAudio.h"
#include "Island/IslandExcavationEcho.h"
#include "Island/IslandLocalPlayer.h"
#include "ExpeditionText.h"
#include "Island/IslandNeutralProp.h"
#include "Island/IslandNeutralLayout.h"

#include "Components/InputComponent.h"
#include "Components/AudioComponent.h"
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
#include "Sound/SoundAttenuation.h"
#include "Sound/SoundBase.h"
#include "UObject/ConstructorHelpers.h"
#include "GameFeaturesSubsystem.h"
#include "Interfaces/IPluginManager.h"
#include "RHIGlobals.h"
#include "PartyMinigame.h"
#include "PartyVoxelArena.h"
#include "Island/IslandGameMode.h"
#include "Island/IslandState.h"

AIslandPlayerController::AIslandPlayerController()
{
    PlayerCameraManagerClass = AExpeditionImpactCamera::StaticClass();
    static ConstructorHelpers::FObjectFinder<USoundBase> Echo(TEXT("/Game/ActDemo/Audio/MS_ExcavationEcho"));
    EchoSound = Echo.Object;
    EchoAttenuation = CreateDefaultSubobject<USoundAttenuation>(TEXT("ExcavationEchoAttenuation"));
    EchoAttenuation->Attenuation.bSpatialize = true;
    EchoAttenuation->Attenuation.bAttenuate = false; // The strike-to-lid distance sets the gain.
    EchoAttenuation->Attenuation.bEnableReverbSend = false;
}

void AIslandPlayerController::AutoManageActiveCameraTarget(AActor* SuggestedTarget)
{
    // ClientRestart can arrive after a networked crew has already framed its dock.
    const auto* HUD = GetHUD<AIslandHUD>();
    if (IsLocalController() && HUD && HUD->OwnsDockView()) return;
    Super::AutoManageActiveCameraTarget(SuggestedTarget);
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
    return PartyVeil == EIslandPartyVeil::Clear && IsPlaying() && !IsInPartyBriefing() && (!HUD || HUD->IsAcceptingWorldInput());
}

bool AIslandPlayerController::IsInPartyBriefing() const
{
    if (IsLocalController()) return GetLocalPartyBriefingRound() != 0;
    const auto* World = GetWorld();
    const auto* Round = World ? World->GetGameState<AIslandGameState>() : nullptr;
    return Round && Round->Phase == EIslandPhase::PartyBriefing;
}

void AIslandPlayerController::ToggleMenu()
{
    if (auto* HUD=GetHUD<AIslandHUD>()) HUD->ToggleMenu();
}

void AIslandPlayerController::OpenHelp()
{
    if (!CanUseIslandWorld() || bMapOpen) return;
    CancelPunchCharge();
    bHelpOpen=true;
}
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

void AIslandPlayerController::Punch()
{
    if (!CanUseIslandWorld() || !IsVoxelInputEnabled() || bHelpOpen || bMapOpen || WaterState >= 2) return;
    if (auto* Hero = GetHero(); Hero && Hero->IsMudAnchored())
    {
        Super::Punch();
        return;
    }
    FVector Aim;
    if (AIslandChest* Chest = GetAimedChest(&Aim))
        ServerClickChest(Chest, Aim);
    else
        Super::Punch();
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
        const double Distance=Chest->GetInteractionDistanceSquared(Origin);
        if (Distance<=Nearest && Chest->IsReachableFrom(*Hero))
        {
            Target=Chest;
            Nearest=Distance;
        }
    }
    return Target;
}

AIslandChest* AIslandPlayerController::TraceChest(const FVector& Start, const FVector& End, FVector& Impact) const
{
    const auto* Hero = GetHero();
    if (!Hero) return nullptr;
    const FCollisionQueryParams Query(SCENE_QUERY_STAT(IslandChestAim), false, Hero);
    FHitResult Hit;
    const bool bHit = AIslandNeutralProp::TraceShovel(*GetWorld(), Hit, Start, End, Query);
    Impact = bHit ? Hit.ImpactPoint : End;
    AIslandChest* Result = bHit ? Cast<AIslandChest>(Hit.GetActor()) : nullptr;
    // Aim is checked again from the player's eyes before use. An exposed lip can
    // be in reach while the centre of the chest is still behind the pit wall.
    if (Result && FVector::DistSquared(Hero->GetAimOrigin(),Impact) > FMath::Square(IslandRules::OpenReach)) Result = nullptr;

    // An exposed chest can settle below its discovery point, or lose both wooden panels.
    // Keep a small target there, clipped by the same first occluder as the visible chest.
    const FVector Direction = (End - Start).GetSafeNormal();
    double Closest = FVector::Dist(Start, Impact);
    for (TActorIterator<AIslandChest> It(GetWorld()); It; ++It)
    {
        const FVector Center = It->GetDiscoveryPoint();
        if (FVector::DistSquared(Hero->GetAimOrigin(), Center) > FMath::Square(IslandRules::OpenReach)
            || !It->HasDiscoveryTarget()) continue;
        constexpr double Radius = 24.;
        const FVector ToCenter = Center - Start;
        const double Along = ToCenter.Dot(Direction);
        const double AcrossSq = (ToCenter - Direction * Along).SizeSquared();
        if (Along < 0. || AcrossSq > Radius * Radius) continue;
        const double Entry = FMath::Max(0., Along - FMath::Sqrt(Radius * Radius - AcrossSq));
        if (Entry >= Closest) continue;
        FHitResult Obstruction;
        if (GetWorld()->LineTraceSingleByChannel(Obstruction, Hero->GetAimOrigin(), Center, ECC_Visibility, Query)
            && Obstruction.GetActor() != *It) continue;
        Result = *It;
        Closest = Entry;
        Impact = Start + Direction * FMath::Max(1., Entry);
    }
    return Result;
}

AIslandChest* AIslandPlayerController::GetAimedChest(FVector* OutAim) const
{
    const AVoxelCharacter* Hero = GetHero();
    if (!Hero || Hero->IsMudAnchored()) return nullptr;
    const FVector Eye = Hero->GetAimOrigin();
    FVector Camera, Point;
    FRotator View;
    GetPlayerViewPoint(Camera, View);
    AIslandChest* Chest = TraceChest(Camera, Camera + View.Vector() * 1000., Point);
    if (!Chest) return nullptr;
    const FVector Aim = (Point - Eye).GetSafeNormal();
    if (GetChestAlongAim(Aim) != Chest) return nullptr;
    if (OutAim) *OutAim = Aim;
    return Chest;
}

AIslandChest* AIslandPlayerController::GetChestAlongAim(const FVector& Aim) const
{
    const AVoxelCharacter* Hero = GetHero();
    if (!Hero || Hero->IsMudAnchored() || Aim.ContainsNaN() || !Aim.IsUnit(.05)) return nullptr;
    FVector Impact;
    const FVector Eye = Hero->GetAimOrigin();
    AIslandChest* Chest = TraceChest(Eye, Eye + Aim * (IslandRules::OpenReach + IslandRules::ChestExtent.Size()), Impact);
    if (Chest && UsesExpeditionAbilities())
    {
        FHitResult CrewMud;
        const double Distance = FMath::Min<double>(AVoxelGameMode::AimReach, FVector::Dist(Eye, Impact));
        if (AIslandGameMode::TraceCrewMud(*Hero, Eye, Eye + Aim * Distance, CrewMud)) return nullptr;
    }
    return Chest;
}

void AIslandPlayerController::ServerClickChest_Implementation(AIslandChest* Chest, FVector_NetQuantizeNormal Aim)
{
    if (IsHoldingExpeditionDig() || !IsValid(Chest) || GetChestAlongAim(Aim) != Chest) return;
    if (UsesExpeditionAbilities()) TriggerExpeditionEvent(EExpeditionAction::Interact, false, Chest);
    else if (auto* Mode = GetWorld()->GetAuthGameMode<AIslandGameMode>()) Mode->TryOpen(*this, *Chest);
}

FString AIslandPlayerController::GetNeutralHint() const
{
    const auto* Hero = GetHero();
    if (!Hero || Hero->IsMudAnchored()) return {};
    FHitResult Hit;
    const FVector Eye = Hero->GetAimOrigin();
    const FVector Aim = GetExcavationAim(*Hero);
    const FCollisionQueryParams Query(SCENE_QUERY_STAT(IslandNeutralHint), false, Hero);
    if (!AIslandNeutralProp::TraceShovel(*GetWorld(), Hit, Eye, Eye + Aim * 1000., Query)) return {};
    const auto* Prop = Cast<AIslandNeutralProp>(Hit.GetActor());
    if (!Prop || Prop->IsDemolished() || Prop->GetTriggerHits() >= 3) return {};
    const TCHAR* Action = nullptr;
    switch (Prop->GetKind())
    {
    case EIslandNeutralKind::Bridge: Action = EXPEDITION_TEXT("左键断索放桥", "LMB  Release the bridge"); break;
    case EIslandNeutralKind::Dam: Action = EXPEDITION_TEXT("左键拆坝放水", "LMB  Breach the dam"); break;
    case EIslandNeutralKind::SandDepot: Action = EXPEDITION_TEXT("右键填仓放砂", "RMB  Fill the sand counterweight"); break;
    case EIslandNeutralKind::WindTower: Action = EXPEDITION_TEXT("右键封住风塔", "RMB  Seal the wind tower"); break;
    default: return {};
    }
    if ((Prop->GetKind() == EIslandNeutralKind::Bridge || Prop->GetKind() == EIslandNeutralKind::Dam)
        && Hit.Distance > AVoxelGameMode::AimReach) return {};
    return FString::Printf(TEXT("%s · %d/3"), Action, int32(Prop->GetTriggerHits()));
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

float AIslandPlayerController::GetEchoProximity() const
{
    return EchoDistanceCm >= 0.f ? FIslandExcavationEcho::Proximity(EchoDistanceCm) : 0.f;
}

void AIslandPlayerController::ClientExcavationEcho_Implementation(float DistanceCm, FVector_NetQuantizeNormal DigDirection)
{
    EchoDistanceCm = DistanceCm;
    EchoDirection = DigDirection.GetSafeNormal();
    if (DistanceCm < 0.f)
    {
        LastEchoAt = -1e9;
        EchoDirection = FVector::ZeroVector;
        return;
    }
    LastEchoAt = FPlatformTime::Seconds();
    PlayExcavationEcho();
#if !UE_BUILD_SHIPPING
    if (FParse::Param(FCommandLine::Get(), TEXT("IslandQA")))
        UE_LOG(LogTemp, Display, TEXT("ISLAND_EXCAVATION_ECHO strong=%d distance_cm=%.1f proximity=%.4f direction=%.4f,%.4f,%.4f"),
            HasStrongEcho(), DistanceCm, GetEchoProximity(), EchoDirection.X, EchoDirection.Y, EchoDirection.Z);
#endif
}

void AIslandPlayerController::PlayExcavationEcho()
{
    if (!IsLocalController() || !EchoSound || !GEngine || !GEngine->UseSound() || IsPartyTravelLocked()) return;
    FVector Ear, Forward, Right;
    GetAudioListenerPosition(Ear, Forward, Right);
    const FIslandEchoAudio Cue = FIslandEchoAudio::ForListener(Ear, Forward, EchoDirection, GetEchoProximity());
    // Both attacks live on one MetaSound render clock. A late gameplay frame
    // cannot stretch the spacing or drop the rising/falling reply.
    auto* Params = NewObject<UInitialActiveSoundParams>(this);
    Params->AudioParams.Emplace(TEXT("ImpactPitch"), 12.f * FMath::Log2(Cue.Pitch));
    Params->AudioParams.Emplace(TEXT("AnswerPitch"), 12.f * FMath::Log2(Cue.AnswerPitch));
    Params->AudioParams.Emplace(TEXT("AnswerDelay"), Cue.AnswerDelay);
    UGameplayStatics::PlaySoundAtLocation(this, EchoSound, Cue.Position, Cue.Volume, 1.f, 0.f, EchoAttenuation, nullptr, Params);
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
            PartyPawn->SetLocalFirstPersonView(WantsFirstPersonView());
        }
        UpdatePartyClient();
    }
}

namespace
{
    constexpr double ReturnVeilSeconds = 1.05;
    constexpr double RevealVeilSeconds = .85;
}

FIslandPartyTransitionView AIslandPlayerController::GetPartyTransitionView() const
{
    const double Age = FMath::Max(0., FPlatformTime::Seconds() - VeilStart);
    float Coverage = 0.f;
    if (PartyVeil == EIslandPartyVeil::Black) Coverage = 1.f;
    else if (PartyVeil == EIslandPartyVeil::Closing)
        Coverage = FMath::Clamp(Age / ReturnVeilSeconds, 0., 1.);
    else if (PartyVeil == EIslandPartyVeil::Opening)
        Coverage = 1. - FMath::Clamp(Age / RevealVeilSeconds, 0., 1.);
    return {PartyVeil, Coverage, bPartyReturning, LocalPartyRound};
}

void AIslandPlayerController::StartPartyReturnVeil()
{
    // During a failed preparation the page stays up until the return veil paints solid black.
    const bool bReturningFromBriefing = GetLocalPartyBriefingRound() != 0;
    bPartyReturning = true;
    bBlackReported = false;
    bRevealRequested = false;
    PartyVeil = EIslandPartyVeil::Closing;
    VeilStart = FPlatformTime::Seconds() - (bReturningFromBriefing ? ReturnVeilSeconds : 0.);
    UE_LOG(LogTemp, Display, TEXT("PARTY_VEIL stage=close round=%u direction=out"), LocalPartyRound);
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
        if (auto* ControlledCharacter = Cast<ACharacter>(GetPawn()))
        {
            ControlledCharacter->StopJumping();
            ControlledCharacter->GetCharacterMovement()->StopMovementImmediately();
            ControlledCharacter->GetCharacterMovement()->DisableMovement();
        }
        if (auto* PartyPawn = Cast<APartyPawn>(GetPawn())) PartyPawn->ReleaseLocalPartyInput();
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
        LocalPartyBriefingFeature = NAME_None;
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

const TArray<FIslandPartyBriefingMember>& AIslandPlayerController::GetPartyBriefingMembers() const
{
    const auto* Round = GetWorld()->GetGameState<AIslandGameState>();
    if (Round && Round->PartyBriefingRound == LocalPartyRound && !Round->PartyBriefingMembers.IsEmpty())
        return Round->PartyBriefingMembers;
    return InitialPartyMembers;
}

void AIslandPlayerController::NotifyPartyBriefingPainted(uint32 RoundId)
{
    if (RoundId && RoundId == GetLocalPartyBriefingRound() && !bPartyReturning && !GUsingNullRHI && FApp::CanEverRender())
        RecordPartyBriefingPresentation(true);
}

void AIslandPlayerController::RecordPartyBriefingPresentation(bool bRendered)
{
    if (BriefingPresentedFrame != MAX_uint64) return;
    BriefingPresentedFrame = GFrameCounter;
    UE_LOG(LogTemp, Display, TEXT("PARTY_BRIEFING_PRESENTED round=%u rendered=%d"), LocalPartyRound, bRendered);
#if !UE_BUILD_SHIPPING
    EarliestPartyPreloadTime = FParse::Param(FCommandLine::Get(), TEXT("PartyBriefSlowLoadQA"))
        ? FPlatformTime::Seconds() + 8. : 0.;
#endif
}

void AIslandPlayerController::ClientPrepareParty_Implementation(uint32 RoundId, FName Feature, bool bAnimate,
    const TArray<FIslandPartyBriefingMember>& Members)
{
    if (RoundId <= LocalPartyRound)
    {
        return;
    }
    LocalPartyRound = RoundId;
    LocalPartyBriefingFeature = Feature;
    InitialPartyMembers = Members;
    BriefingPresentedFrame = MAX_uint64;
    bPartyPreloadStarted = false;
    bPartyActivating = false;
    bPartyReturning = false;
    bBlackReported = false;
    bAnimateParty = bAnimate;
    bCanUnloadParty = false;
    bWaitingForVenue = false;
    bPartyUnloadStarted = false;
    bPartyPawnReadySent = false;
    PartyVeil = EIslandPartyVeil::Clear;
    SetPartyTravelLocked(true);
    if (auto* HUD = GetHUD<AIslandHUD>()) HUD->ShowLocalPartyBriefing(*this);
    if (GUsingNullRHI || !FApp::CanEverRender()) RecordPartyBriefingPresentation(false);
}

void AIslandPlayerController::StartPartyPreload(uint32 RoundId, FName Feature)
{
    // A return timeout can leave local cleanup pending. Even that work can
    // wait until the next round's page has painted.
    ReleasePartyAssets();
    PartyVenue = nullptr;
    const FString PreviousPluginURL = MoveTemp(PartyPluginURL);
    PartyPluginURL.Reset();
    UGameFeaturesSubsystem& Features = UGameFeaturesSubsystem::Get();
    if (!Features.GetPluginURLByName(Feature.ToString(), PartyPluginURL))
    {
        const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(Feature.ToString());
        if (Plugin && Plugin->GetDescriptor().bExplicitlyLoaded)
        {
            PartyPluginURL = UGameFeaturesSubsystem::GetPluginURL_FileProtocol(Plugin->GetDescriptorFileName());
        }
    }
    if (!PreviousPluginURL.IsEmpty() && PreviousPluginURL != PartyPluginURL)
        Features.UnloadGameFeaturePlugin(PreviousPluginURL);
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
            if (!bPartyReturning)
            {
                bPartyActivating = false;
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
    if (bAnimateParty) StartPartyReturnVeil();
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
    if (APartyPawn* PartyPawn = Cast<APartyPawn>(GetPawn()))
    {
        PartyPawn->ReleaseLocalPartyInput();
    }
    ReleasePartyAssets();
    if (bPartyActivating)
    {
        UGameFeaturesSubsystem::Get().CancelGameFeatureStateChange(PartyPluginURL,
            FGameFeaturePluginChangeStateComplete::CreateWeakLambda(this, [this, RoundId](const UE::GameFeatures::FResult&)
            {
                if (LocalPartyRound == RoundId) bPartyActivating = false;
            }));
    }
}

void AIslandPlayerController::UpdatePartyClient()
{
    TickPartyVeil();
    if (!bPartyReturning)
    {
        if (GetLocalPartyBriefingRound())
        {
            const auto* Round = GetWorld()->GetGameState<AIslandGameState>();
            if (Round && Round->PartyBriefingRound == LocalPartyRound && Round->Phase == EIslandPhase::PartyPlaying)
            {
                LocalPartyBriefingFeature = NAME_None;
                SetPartyTravelLocked(false);
            }
            else if (!bPartyPreloadStarted && GFrameCounter > BriefingPresentedFrame)
            {
                // OnPaint only records the frame. Run GameFeatures on the next game tick,
                // after Slate has submitted the opaque page and its controls.
                if (!bBlackReported)
                {
                    bBlackReported = true;
                    ServerPartyScreenBlack(LocalPartyRound, false);
                }
#if !UE_BUILD_SHIPPING
                if (FPlatformTime::Seconds() < EarliestPartyPreloadTime) return;
#endif
                bPartyPreloadStarted = true;
                StartPartyPreload(LocalPartyRound, LocalPartyBriefingFeature);
            }
        }
        if (bWaitingForVenue && PartyVenue && (PartyVenue->IsLevelVisible() ||
            PartyVenue->GetLevelStreamingState()==ELevelStreamingState::FailedToLoad))
            ReportPartyAssets(LocalPartyRound);
        const APartyPawn* PartyPawn = Cast<APartyPawn>(GetPawn());
        if (const auto* VoxelPawn = Cast<APartyVoxelPawn>(PartyPawn); VoxelPawn && VoxelPawn->GetSession()
            && PartyVenue && PartyVenue->IsLevelVisible() && PartyVenue->GetLoadedLevel())
            VoxelPawn->GetSession()->BindVenueLevel(*PartyVenue->GetLoadedLevel());
        if (!bPartyPawnReadySent && PartyPawn && PartyPawn->HasLocalPartyInput())
        {
            bPartyPawnReadySent = true;
            UE_LOG(LogTemp, Display, TEXT("PARTY_PAWN_READY round=%u"), LocalPartyRound);
            ServerPartyPawnReady(LocalPartyRound);
        }
        return;
    }

    if (!bCanUnloadParty || bPartyActivating || bPartyUnloadStarted || !GetHero() || TActorIterator<APartySession>(GetWorld()) || TActorIterator<APartyPawn>(GetWorld()))
    {
        return;
    }
    if (PartyVenue && (PartyVenue->HasLoadedLevel() || PartyVenue->IsStreamingStatePending())) return;
    PartyVenue=nullptr;
    bPartyUnloadStarted = true;
    if (!bAnimateParty)
    {
        LocalPartyBriefingFeature = NAME_None;
        SetPartyTravelLocked(false);
        SetVoxelInputEnabled(true);
    }
    const uint32 RoundId = LocalPartyRound;
    UGameFeaturesSubsystem& Features = UGameFeaturesSubsystem::Get();
    if (PartyPluginURL.IsEmpty() || (Features.IsGameFeaturePluginInstalled(PartyPluginURL) && !Features.IsGameFeaturePluginMounted(PartyPluginURL)))
    {
        FinishPartyUnload(RoundId, true);
        return;
    }
    // Unload targets the Installed state, including deactivation of any active feature.
    Features.UnloadGameFeaturePlugin(PartyPluginURL,
        FGameFeaturePluginUnloadComplete::CreateWeakLambda(this, [this, RoundId](const UE::GameFeatures::FResult& Result)
        {
            if (RoundId != LocalPartyRound) return;
            if (!Result.HasValue())
                UE_LOG(LogTemp, Warning, TEXT("PARTY_FEATURE_UNLOAD_FAILED round=%u %s"), RoundId, *Result.GetError());
            const auto& Subsystem = UGameFeaturesSubsystem::Get();
            const bool bInstalled = Subsystem.IsGameFeaturePluginInstalled(PartyPluginURL) && !Subsystem.IsGameFeaturePluginMounted(PartyPluginURL);
            FinishPartyUnload(RoundId, Result.HasValue() && bInstalled);
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

void AIslandPlayerController::ServerAgreeParty_Implementation(uint32 RoundId)
{
    if (auto* Mode = GetWorld()->GetAuthGameMode<AIslandGameMode>())
        Mode->PartyBriefingResponse(*this, RoundId, false);
}

void AIslandPlayerController::ServerForceStartParty_Implementation(uint32 RoundId)
{
    if (auto* Mode = GetWorld()->GetAuthGameMode<AIslandGameMode>())
        Mode->PartyBriefingResponse(*this, RoundId, true);
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

void AIslandPlayerController::ServerChooseCharacter_Implementation(int32 ModelIndex)
{
    if (auto* Mode = GetWorld()->GetAuthGameMode<AIslandGameMode>())
        Mode->ChooseCharacter(*this, ModelIndex);
}

void AIslandPlayerController::ClientCharacterChosen_Implementation(int32 ModelIndex)
{
    if (auto* Local = Cast<UIslandLocalPlayer>(GetLocalPlayer()))
        Local->RememberCharacterModel(ModelIndex);
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
