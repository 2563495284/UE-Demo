#include "Island/IslandLocalPlayer.h"

FString UIslandLocalPlayer::GetGameLoginOptions() const
{
    if (!MemberId.IsValid())
    {
        MemberId = FGuid::NewGuid();
    }
    return TEXT("RoomMember=") + MemberId.ToString();
}
