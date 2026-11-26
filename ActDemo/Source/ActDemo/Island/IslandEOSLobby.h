#pragma once

#include "Island/IslandRoomSession.h"
#include "IEOSSDKManager.h"
#include "eos_lobby.h"

// Created after Connect has logged in. Outstanding callbacks retain the same platform during map travel.
class FIslandEOSLobby : public TSharedFromThis<FIslandEOSLobby>
{
public:
    using FResult = TFunction<void(bool)>;
    FIslandEOSLobby(IEOSPlatformHandlePtr InPlatform, EOS_ProductUserId InUser);
    ~FIslandEOSLobby();

    void WatchClosure(TFunction<void()> Callback);
    void Create(int32 Capacity, FResult Complete);
    void Search(TFunction<void(bool, TArray<FIslandRoomListing>)> Complete);
    void CancelSearch();
    void Join(const FString& RoomId, TFunction<void(FString Address, FString Error)> Complete);
    void Publish(const FString& Name, int32 Seats, int32 Players, bool bLocked, FResult Complete);
    void Close();
    void Shutdown();

private:
    IEOSPlatformHandlePtr Platform;
    EOS_HLobby Handle;
    EOS_ProductUserId User;
    FString ActiveId;
    bool bOwner = false;
    bool bShutdown = false;
    TSet<FString> ClosingIds;
    uint64 SearchGeneration = 0;
    TMap<FString, EOS_HLobbyDetails> Results;
    EOS_NotificationId StatusNotification = EOS_INVALID_NOTIFICATIONID;
    TFunction<void()> OnClosed;
    void CloseById(const FString& Id, bool bDestroy, float RetryDelay);
};
