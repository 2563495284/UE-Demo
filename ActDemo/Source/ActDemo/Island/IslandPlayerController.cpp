#include "Island/IslandPlayerController.h"

#include "Components/InputComponent.h"
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

void AIslandPlayerController::OpenMap() { bMapOpen = GetTeam() >= 0; }
void AIslandPlayerController::CloseMap() { bMapOpen = false; }
void AIslandPlayerController::Interact() { if (IsPlaying() && WaterState < 2) { ServerInteract(); } }
void AIslandPlayerController::RestartIsland() { ServerRestartIsland(); }

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
