#include "Island/IslandPlayerController.h"

#include "Components/InputComponent.h"
#include "Engine/AssetManager.h"
#include "Engine/Engine.h"
#include "Engine/StreamableManager.h"
#include "EngineUtils.h"
#include "GameFeaturesSubsystem.h"
#include "Interfaces/IPluginManager.h"
#include "PartyMinigame.h"
#include "Island/IslandGameMode.h"
#include "Island/IslandState.h"

void AIslandPlayerController::SetupInputComponent()
{
    Super::SetupInputComponent();
#if !UE_BUILD_SHIPPING
    if (FParse::Param(FCommandLine::Get(), TEXT("IslandQA")))
    {
        return;
    }
#endif
    if (InputComponent)
    {
        InputComponent->BindKey(EKeys::E, IE_Pressed, this, &AIslandPlayerController::Interact);
        InputComponent->BindKey(EKeys::Tab, IE_Pressed, this, &AIslandPlayerController::OpenMap);
        InputComponent->BindKey(EKeys::Tab, IE_Released, this, &AIslandPlayerController::CloseMap);
        InputComponent->BindKey(EKeys::Enter, IE_Pressed, this, &AIslandPlayerController::RestartIsland);
    }
}

int32 AIslandPlayerController::GetTeam() const
{
    const AIslandPlayerState* IslandState = GetPlayerState<AIslandPlayerState>();
    return IslandState ? IslandState->Team : -1;
}

void AIslandPlayerController::OpenMap() { bMapOpen = IsVoxelInputEnabled() && GetTeam() >= 0; }
void AIslandPlayerController::CloseMap() { bMapOpen = false; }
void AIslandPlayerController::Interact() { if (IsVoxelInputEnabled() && IsPlaying() && WaterState < 2) { ServerInteract(); } }
void AIslandPlayerController::RestartIsland() { if (IsVoxelInputEnabled()) { ServerRestartIsland(); } }

void AIslandPlayerController::ServerInteract_Implementation()
{
    if (AIslandGameMode* Mode = GetWorld()->GetAuthGameMode<AIslandGameMode>())
    {
        Mode->TryOpen(*this);
    }
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
    ClientEditRejected(FString::Printf(TEXT("Clues %d/4"), LocalClues.Count()));
}

void AIslandPlayerController::ClientWaterState_Implementation(uint8 State)
{
    WaterState = State;
    WaterChangedAt = FPlatformTime::Seconds();
}

FString AIslandPlayerController::WaterStatus() const
{
    if (WaterState == 2)
    {
        return TEXT("Drowned. Returning to your dock");
    }
    if (WaterState == 1)
    {
        const int32 Left = FMath::Max(1, FMath::CeilToInt(IslandRules::DrownTime - (FPlatformTime::Seconds() - WaterChangedAt)));
        return FString::Printf(TEXT("Drowning  %ds"), Left);
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
