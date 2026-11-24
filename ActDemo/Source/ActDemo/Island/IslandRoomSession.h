#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "OnlineSessionSettings.h"
#include "Interfaces/OnlineSessionInterface.h"
#include "Island/IslandRoomSettings.h"
#include "IslandRoomSession.generated.h"

struct FIslandRoomListing
{
    FString RoomId;
    FString RoomName;
    int32 Occupied = 0;
    int32 MaxPlayers = 0;
};

UCLASS()
class ACTDEMO_API UIslandRoomSession : public UGameInstanceSubsystem
{
    GENERATED_BODY()

public:
    void Initialize(FSubsystemCollectionBase& Collection) override;
    void Deinitialize() override;
    void Host(const FIslandRoomSettings& Settings);
    void Search();
    void Cancel();
    void Join(const FString& RoomId);
    void JoinAddress(const FString& Address);
    void Leave();
    void UpdateAdvertisement(const FIslandRoomSettings& Settings, int32 Occupied, bool bLocked);
    bool IsBusy() const;
    bool IsManagedRoom() const { return bHosting; }

    FIslandRoomSettings HostSettings;
    TArray<FIslandRoomListing> Rooms;
    FString Status;
    FString Error;
    FString LastJoinedRoomId;
    bool bReturnToSearch = false;

private:
    enum class EOperation { Idle, Creating, Searching, Joining, Connecting, Opening, Leaving };
    EOperation Operation = EOperation::Idle;
    bool bHosting = false;
    bool bInRoom = false;
    bool bWhiteboxDisplay = false;
    IOnlineSessionPtr Sessions() const;
    void Created(FName Name, bool bSuccess);
    void Found(bool bSuccess);
    void Joined(FName Name, EOnJoinSessionCompleteResult::Type Result);
    void Connect(const FString& Address);
    void DestroyLocalSession();
    void UnbindSessionDelegates();
    void LoadFront();
    void OnLoaded(UWorld* World);
    void UpdateDisplayForMap(const FString& Map, bool bGameWorld);
    void OnPreload(const FWorldContext& Context, const FString& Map);
    void NetworkFailed(UWorld* World, UNetDriver* Driver, ENetworkFailure::Type Kind, const FString& Message);
    void TravelFailed(UWorld* World, ETravelFailure::Type Kind, const FString& Message);
    void Fail(const FString& Message);
    TSharedPtr<FOnlineSessionSearch> SearchRequest;
    TMap<FString, FOnlineSessionSearchResult> SearchResults;
    FDelegateHandle CreateDelegate, FindDelegate, JoinDelegate;
};
