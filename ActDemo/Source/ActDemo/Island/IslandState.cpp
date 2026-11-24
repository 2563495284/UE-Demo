#include "Island/IslandState.h"
#include "Net/UnrealNetwork.h"

void AIslandGameState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(AIslandGameState, Phase);
    DOREPLIFETIME(AIslandGameState, Winner);
    DOREPLIFETIME(AIslandGameState, JoinedCount);
    DOREPLIFETIME(AIslandGameState, RoomSettings);
    DOREPLIFETIME(AIslandGameState, bRoomLobby);
    DOREPLIFETIME(AIslandGameState, bRoomBlocked);
    DOREPLIFETIME(AIslandGameState, ActiveTeams);
    DOREPLIFETIME(AIslandGameState, LaunchedTeams);
    DOREPLIFETIME(AIslandGameState, StartAtServerTime);
    DOREPLIFETIME(AIslandGameState, RoomNotice);
}

void AIslandPlayerState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(AIslandPlayerState, Team);
    DOREPLIFETIME(AIslandPlayerState, bHost);
    DOREPLIFETIME(AIslandPlayerState, bRoomReady);
    DOREPLIFETIME(AIslandPlayerState, bMapReady);
}

bool AIslandGameState::CanLaunch() const
{
    if (!bRoomLobby || bRoomBlocked || Phase != EIslandPhase::Waiting || JoinedCount == 0 || JoinedCount != PlayerArray.Num())
    {
        return false;
    }
    for (const APlayerState* Player : PlayerArray)
    {
        const auto* Member = Cast<AIslandPlayerState>(Player);
        if (!Member || Member->Team < 0 || !Member->bMapReady || (!Member->bHost && !Member->bRoomReady))
        {
            return false;
        }
    }
    return true;
}
