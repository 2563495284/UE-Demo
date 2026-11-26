#include "Island/IslandLocalPlayer.h"
#include "Island/IslandRules.h"

FString UIslandLocalPlayer::GetGameLoginOptions() const
{
    if (!MemberId.IsValid())
    {
        MemberId = FGuid::NewGuid();
    }
    FString Options = TEXT("RoomMember=") + MemberId.ToString();
    if (IslandRules::IsCharacterModel(PreferredCharacter))
        Options += FString::Printf(TEXT("?CrewModel=%d"), PreferredCharacter);
    return Options;
}
