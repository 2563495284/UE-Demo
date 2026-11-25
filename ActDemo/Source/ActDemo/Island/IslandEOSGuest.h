#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "IEOSSDKManager.h"

class FIslandEOSSocketUser;
class ISocketSubsystem;
class UGameInstance;

/** One anonymous EOS Connect identity for the game instance, shared by the lobby and NetDriverEOS. */
class FIslandEOSGuest : public TSharedFromThis<FIslandEOSGuest>
{
public:
    using FReady = TFunction<void(bool, const FString&)>;

    explicit FIslandEOSGuest(UGameInstance* GameInstance);
    ~FIslandEOSGuest();

    static bool IsConfigured();
    void EnsureReady(FReady Callback);
    IEOSPlatformHandlePtr GetPlatformHandle() const { return Platform; }
    EOS_HPlatform GetPlatform() const;
    EOS_ProductUserId GetUser() const;
    void Shutdown();

private:
    FString OpenPlatform();
    void CreateDevice();
    void Login();
    void CreateUser(EOS_ContinuanceToken Token);
    void Authenticated(EOS_ProductUserId NewUser);
    void AuthFailed(const TCHAR* Stage, EOS_EResult Result);
    void Finish(const FString& Error);
    void ScheduleRenewal();
    void RemovePeerNotification();

    TWeakObjectPtr<UGameInstance> Owner;
    IEOSPlatformHandlePtr Platform;
    TSharedPtr<FIslandEOSSocketUser> SocketUser;
    TUniquePtr<ISocketSubsystem> Sockets;
    TArray<FReady> Waiting;
    FTSTicker::FDelegateHandle Retry;
    float RetryDelay = 10.f;
    EOS_NotificationId ExpirationId = EOS_INVALID_NOTIFICATIONID;
    EOS_NotificationId StatusId = EOS_INVALID_NOTIFICATIONID;
    EOS_NotificationId PeerId = EOS_INVALID_NOTIFICATIONID;
    bool bRequestPending = false;
    bool bDeviceReady = false;
    bool bRenewing = false;
    bool bShutdown = false;
};
