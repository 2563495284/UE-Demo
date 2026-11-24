#include "Island/IslandPlayerController.h"
#include "Island/IslandNeutralProp.h"
#include "Island/IslandNeutralLayout.h"

#include "Components/InputComponent.h"
#include "Engine/GameViewportClient.h"
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
    return IsPlaying() && (!HUD || HUD->IsAcceptingWorldInput());
}

void AIslandPlayerController::ToggleMenu()
{
    if (auto* HUD=GetHUD<AIslandHUD>()) HUD->ToggleMenu();
}

void AIslandPlayerController::OpenHelp() { bHelpOpen=CanUseIslandWorld() && !bMapOpen; }
void AIslandPlayerController::CloseHelp() { bHelpOpen=false; }

void AIslandPlayerController::OpenMap()
{
    if (bMapOpen || !CanUseIslandWorld() || !IsVoxelInputEnabled() || GetTeam()<0) return;
    StopRecover();
    StopClimb();
    bHelpOpen=false;
    bMapOpen=true;
    SetVoxelInputEnabled(false);
}

void AIslandPlayerController::CloseMap()
{
    if (!bMapOpen) return;
    bMapOpen=false;
    const auto* HUD=GetHUD<AIslandHUD>();
    if (!HUD || !HUD->IsMenuOpen()) SetVoxelInputEnabled(true);
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
    if (!GS || GS->Phase != EIslandPhase::Hunting || GS->GetServerWorldTimeSeconds() >= RallyExpiresAt) return false;
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
        return Sail->GetPhase() == 0 ? TEXT("E  放下帆布") : TEXT("E  收起帆布");
    const auto* Hero = GetHero();
    if (!Hero) return {};
    const FVector Eye = Hero->GetAimOrigin();
    const FVector Forward = GetControlRotation().Vector();
    for (TActorIterator<AIslandNeutralProp> It(GetWorld()); It; ++It)
    {
        const FVector Offset = It->GetInteractionPoint() - Eye;
        if (Offset.SizeSquared() > FMath::Square(480.) || FVector::DotProduct(Forward, Offset.GetSafeNormal()) < .72) continue;
        switch (It->GetKind())
        {
        case EIslandNeutralKind::Tree:
            if (It->GetPhase() < 2) return TEXT("左键敲树根");
            break;
        case EIslandNeutralKind::Ship: return TEXT("挖通船舱  ·  右键丢土搭上甲板");
        case EIslandNeutralKind::Hopper:
            if (It->GetPhase() == 0) return TEXT("左键敲木销放砂");
            break;
        case EIslandNeutralKind::Tide: return TEXT("右键丢土堵一孔  ·  白泡冒起时跳入另一孔");
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
    LocalClues = Snapshot;
    if (Snapshot.Count() > 0) ClientEditRejected(FString::Printf(TEXT("获得线索  %d / 4"), LocalClues.Count()));
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
        return TEXT("已溺水，即将返回本队码头");
    }
    if (WaterState == 1)
    {
        const int32 Left = FMath::Max(1, FMath::CeilToInt(IslandRules::DrownTime - (FPlatformTime::Seconds() - WaterChangedAt)));
        return FString::Printf(TEXT("深水 · %d 秒后溺水"), Left);
    }
    return FString();
}

void AIslandPlayerController::PlayerTick(float DeltaTime)
{
    Super::PlayerTick(DeltaTime);
    if (IsLocalController() && LocalPartyRound != 0)
    {
        UpdatePartyClient();
    }
}

void AIslandPlayerController::ClientPrepareParty_Implementation(uint32 RoundId, FName Feature)
{
    if (RoundId <= LocalPartyRound)
    {
        return;
    }
    LocalPartyRound = RoundId;
    bPartyReturning = false;
    bPartyUnloadStarted = false;
    bPartyPawnReadySent = false;
    bMapOpen = false;
    SetVoxelInputEnabled(false);

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
    if (!HasAuthority() && FParse::Param(FCommandLine::Get(), TEXT("IslandPartySkipReady")))
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
        const bool bReady = PartySessionClass.IsValid();
        UE_LOG(LogTemp, Display, TEXT("PARTY_FEATURE_ACTIVE round=%u success=%d"), RoundId, bReady);
        ServerPartyAssetsReady(RoundId, bReady);
    }
}

void AIslandPlayerController::ClientReturnFromParty_Implementation(uint32 RoundId)
{
    if (RoundId != LocalPartyRound || bPartyReturning)
    {
        return;
    }
    bPartyReturning = true;
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
    if (!bPartyReturning)
    {
        const APartyPawn* Pawn = Cast<APartyPawn>(GetPawn());
        if (!bPartyPawnReadySent && Pawn && Pawn->HasLocalPartyInput())
        {
            bPartyPawnReadySent = true;
            UE_LOG(LogTemp, Display, TEXT("PARTY_PAWN_READY round=%u"), LocalPartyRound);
            ServerPartyPawnReady(LocalPartyRound);
        }
        return;
    }

    if (bPartyUnloadStarted || !GetHero() || TActorIterator<APartySession>(GetWorld()) || TActorIterator<APartyPawn>(GetWorld()))
    {
        return;
    }
    bPartyUnloadStarted = true;
    SetVoxelInputEnabled(true);
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
    UE_LOG(LogTemp, Display, TEXT("PARTY_FEATURE_INSTALLED round=%u success=%d"), RoundId, bSuccess);
    PartyPluginURL.Reset();
    ServerPartyUnloaded(RoundId, bSuccess);
}

void AIslandPlayerController::ReleasePartyAssets()
{
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
