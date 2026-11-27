#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "OnlineSessionSettings.h"
#include "Interfaces/OnlineSessionInterface.h"
#include "Island/IslandRoomSettings.h"
#include "IslandRoomSession.generated.h"

class FIslandEOSGuest;
class FIslandEOSLobby;
class SIslandTravelScreen;

enum class EIslandRoomNetwork : uint8 { Internet, LAN };

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
    void Host(const FIslandRoomSettings& Settings, EIslandRoomNetwork Network = EIslandRoomNetwork::Internet);
    void Search(EIslandRoomNetwork Network = EIslandRoomNetwork::Internet);
    EIslandRoomNetwork GetRoomNetwork() const { return RoomNetwork; }
    bool IsInternetConfigured() const { return bInternetConfigured; }
    void WarmUpInternet();
    bool CanRetryInternet() const { return bGuestLoginFailed && !IsBusy(); }
    void Cancel();
    void Join(const FString& RoomId);
    void JoinAddress(const FString& Address);
    void Leave();
    void UpdateAdvertisement(const FIslandRoomSettings& Settings, int32 Occupied, bool bLocked);
    bool IsBusy() const;
    bool IsManagedRoom() const { return bHosting; }
    // Called after the destination's local HUD and camera have produced their first usable view.
    void RevealTravelScreen(bool bImmediate = false);
    bool IsTravelInputBlocked() const { return bTravelInputBlocked; }

    FIslandRoomSettings HostSettings;
    TArray<FIslandRoomListing> Rooms;
    FString Status;
    FString Error;
    FString LastJoinedRoomId;
    bool bReturnToSearch = false;

private:
    enum class EOperation { Idle, AuthenticatingHost, AuthenticatingSearch, AuthenticatingJoin, Creating, Searching, Joining, Connecting, Opening, Leaving };
    EOperation Operation = EOperation::Idle;
    EIslandRoomNetwork RoomNetwork = EIslandRoomNetwork::LAN;
    bool bInternetConfigured = false;
    bool bCancelWhenComplete = false;
    bool bHosting = false;
    bool bInRoom = false;
    bool bWhiteboxDisplay = false;
    IOnlineSessionPtr Sessions() const;
    TSharedPtr<FIslandEOSGuest> Guest;
    TSharedPtr<FIslandEOSLobby> InternetLobby;
    uint64 IntentGeneration = 0;
    uint64 AdvertisementGeneration = 0;
    bool bGuestLoginFailed = false;
    FTSTicker::FDelegateHandle AdvertisementRetry;
    float AdvertisementRetryDelay = 1.f;
    void EnsureLobby();
    void ClearAdvertisementRetry();
    void SelectNetwork(EIslandRoomNetwork Network);
    void Authenticate(EOperation Intent);
    void BeginAuthenticatedOperation();
    void CreateRoom();
    void FindRooms();
    void JoinRoom();
    void Created(FName Name, bool bSuccess);
    void Found(bool bSuccess);
    void Joined(FName Name, EOnJoinSessionCompleteResult::Type Result);
    void Connect(const FString& Address);
    void DestroyLocalSession();
    void FinishCancelledSession(bool bCreated);
    bool bUpdatingAdvertisement = false;
    int32 DesiredOccupancy = 0;
    bool bDesiredLocked = false;
    bool bAdvertisementChanged = false;
    void PublishInternetAdvertisement();
    void AdvertisementUpdated(bool bSuccess);
    void FinishSearch();
    void UnbindSessionDelegates();
    void LoadFront();
    void OpenHostedIsland();
    bool ShowTravelScreen(const FText& Caption, float Opacity, bool bBehindMenu = false);
    void ClearTravelScreen();
    TSharedPtr<SIslandTravelScreen> TravelScreen;
    TWeakObjectPtr<class UGameViewportClient> TravelViewport;
    TWeakObjectPtr<UWorld> LoadedTravelWorld;
    FTSTicker::FDelegateHandle TravelFade;
    bool bTravelInputBlocked = false;
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
