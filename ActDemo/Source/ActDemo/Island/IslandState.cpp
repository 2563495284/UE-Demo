#include "Island/IslandState.h"
#include "Net/UnrealNetwork.h"

void AIslandGameState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(AIslandGameState, Phase);
    DOREPLIFETIME(AIslandGameState, Winner);
    DOREPLIFETIME(AIslandGameState, JoinedCount);
}

void AIslandPlayerState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(AIslandPlayerState, Team);
}
