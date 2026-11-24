#pragma once

#include "CoreMinimal.h"
#include "IslandRoomSettings.generated.h"

USTRUCT()
struct ACTDEMO_API FIslandRoomSettings
{
    GENERATED_BODY()

    UPROPERTY() FString RoomName = TEXT("寻宝小队");
    UPROPERTY() int32 MaxPlayers = 8;
    UPROPERTY() int32 MaxTeams = 8;
    UPROPERTY() int32 TeamCapacity = 8;

    bool IsValid() const
    {
        return !RoomName.TrimStartAndEnd().IsEmpty() && RoomName.Len() <= 32
            && MaxPlayers >= 1 && MaxPlayers <= 8 && MaxTeams >= 1 && MaxTeams <= 8
            && TeamCapacity >= 1 && TeamCapacity <= 8 && MaxPlayers <= MaxTeams * TeamCapacity;
    }
};
