#include "Island/IslandEOSGuest.h"

#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "EOSSharedTypes.h"
#include "Misc/CommandLine.h"
#include "Misc/Crc.h"
#include "SocketSubsystemEOS.h"
#include "eos_connect.h"
#include "eos_p2p.h"
#include "eos_sdk.h"

class FIslandEOSSocketUser : public ISocketSubsystemEOSUtils
{
public:
    explicit FIslandEOSSocketUser(FName InInstanceName) : InstanceName(InInstanceName) {}
    EOS_ProductUserId GetLocalUserId() override { return User; }
    FString GetSessionId() override { return {}; }
    FName GetSubsystemInstanceName() override { return InstanceName; }
    bool IsLoggedIn() override { return User != nullptr; }

    EOS_ProductUserId User = nullptr;

private:
    FName InstanceName;
};

namespace
{
    const TCHAR* PlatformConfigName = TEXT("ExpeditionIsland");
    // The engine registers EOS sockets under one process-wide name. Two PIE games under the
    // same OS profile would also share the same Device ID. Keep the second instance on IP for QA.
    TWeakPtr<FIslandEOSGuest> SocketOwner;

    template<typename DataType, typename FunctionType>
    auto* AuthCallback(FIslandEOSGuest& Guest, FunctionType&& Function)
    {
        using FCallback = TEOSCallback<void (EOS_CALL*)(const DataType*), DataType, FIslandEOSGuest>;
        auto* Callback = new FCallback(Guest.AsWeak());
        Callback->CallbackLambda = Forward<FunctionType>(Function);
        return Callback;
    }

    void EOS_CALL PeerEstablished(const EOS_P2P_OnPeerConnectionEstablishedInfo* Info)
    {
        const TCHAR* Path = TEXT("unknown");
        if (Info->NetworkType == EOS_ENetworkConnectionType::EOS_NCT_DirectConnection) Path = TEXT("direct");
        if (Info->NetworkType == EOS_ENetworkConnectionType::EOS_NCT_RelayedConnection) Path = TEXT("relay");
        UE_LOG(LogTemp, Display, TEXT("ISLAND_EOS_CONNECTION %s"), Path);
    }
}

FIslandEOSGuest::FIslandEOSGuest(UGameInstance* GameInstance) : Owner(GameInstance) {}
FIslandEOSGuest::~FIslandEOSGuest() { Shutdown(); }

bool FIslandEOSGuest::IsConfigured()
{
    IEOSSDKManager* Manager = IEOSSDKManager::Get();
    const FEOSSDKPlatformConfig* Config = Manager ? Manager->GetPlatformConfig(PlatformConfigName, true) : nullptr;
    if (!Config) return false;
    FGuid Guid;
    return FGuid::ParseExact(Config->ProductId, EGuidFormats::Digits, Guid)
        && FGuid::ParseExact(Config->SandboxId, EGuidFormats::Digits, Guid)
        && FGuid::ParseExact(Config->DeploymentId, EGuidFormats::Digits, Guid)
        && !Config->ClientId.TrimStartAndEnd().IsEmpty() && !Config->ClientSecret.TrimStartAndEnd().IsEmpty();
}

EOS_HPlatform FIslandEOSGuest::GetPlatform() const
{
    return Platform ? static_cast<EOS_HPlatform>(*Platform) : nullptr;
}

EOS_ProductUserId FIslandEOSGuest::GetUser() const { return SocketUser ? SocketUser->User : nullptr; }

FString FIslandEOSGuest::OpenPlatform()
{
    if (Platform) return {};
    if (!IsConfigured()) return TEXT("此版本缺少互联网联机配置。");
    IEOSSDKManager* Manager = IEOSSDKManager::Get();
    if (!Manager || !Manager->IsInitialized()) return TEXT("在线服务尚未就绪，请重试。");
    const FWorldContext* World = Owner.IsValid() ? Owner->GetWorldContext() : nullptr;
    if (!World) return TEXT("游戏尚未就绪，请重试。");
    if (SocketOwner.IsValid())
        return TEXT("同一进程已使用游客身份，请在另一台设备上联机。");

    // EOSShared retains the named platform and drives its SDK ticks on the game thread.
    Platform = Manager->CreatePlatform(PlatformConfigName, World->ContextHandle);
    if (!Platform) return TEXT("无法启动在线服务，请检查网络后重试。");
    SocketUser = MakeShared<FIslandEOSSocketUser>(World->ContextHandle);
    Sockets = MakeUnique<FSocketSubsystemEOS>(Platform, SocketUser);
    FString Error;
    if (!Sockets->Init(Error))
    {
        Sockets.Reset();
        SocketUser.Reset();
        Platform.Reset();
        return TEXT("互联网连接初始化失败，请重试。");
    }
    SocketOwner = AsWeak();
    if (FParse::Param(FCommandLine::Get(), TEXT("IslandForceRelay")))
    {
        EOS_P2P_SetRelayControlOptions Options{};
        Options.ApiVersion = EOS_P2P_SETRELAYCONTROL_API_LATEST;
        Options.RelayControl = EOS_ERelayControl::EOS_RC_ForceRelays;
        const EOS_EResult Result = EOS_P2P_SetRelayControl(EOS_Platform_GetP2PInterface(GetPlatform()), &Options);
        UE_LOG(LogTemp, Display, TEXT("ISLAND_EOS_FORCE_RELAY %hs"), EOS_EResult_ToString(Result));
    }
    const EOS_HConnect Connect = EOS_Platform_GetConnectInterface(GetPlatform());
    EOS_Connect_AddNotifyAuthExpirationOptions Expiration{};
    Expiration.ApiVersion = EOS_CONNECT_ADDNOTIFYAUTHEXPIRATION_API_LATEST;
    ExpirationId = EOS_Connect_AddNotifyAuthExpiration(Connect, &Expiration, this,
        [](const EOS_Connect_AuthExpirationCallbackInfo* Info)
        {
            auto* Guest = static_cast<FIslandEOSGuest*>(Info->ClientData);
            if (Guest->GetUser() == Info->LocalUserId && !Guest->bRequestPending)
            {
                Guest->bRenewing = true;
                Guest->bRequestPending = true;
                Guest->Login();
            }
        });
    EOS_Connect_AddNotifyLoginStatusChangedOptions Changed{};
    Changed.ApiVersion = EOS_CONNECT_ADDNOTIFYLOGINSTATUSCHANGED_API_LATEST;
    StatusId = EOS_Connect_AddNotifyLoginStatusChanged(Connect, &Changed, this,
        [](const EOS_Connect_LoginStatusChangedCallbackInfo* Info)
        {
            auto* Guest = static_cast<FIslandEOSGuest*>(Info->ClientData);
            if (Info->CurrentStatus == EOS_ELoginStatus::EOS_LS_NotLoggedIn && Guest->GetUser() == Info->LocalUserId)
            {
                Guest->RemovePeerNotification();
                Guest->SocketUser->User = nullptr;
                Guest->bRenewing = true;
                Guest->ScheduleRenewal();
            }
        });
    return {};
}

void FIslandEOSGuest::EnsureReady(FReady Callback)
{
    if (bShutdown) { if (Callback) Callback(false, TEXT("在线服务已关闭。")); return; }
    if (GetUser()) { if (Callback) Callback(true, {}); return; }
    if (Callback) Waiting.Add(MoveTemp(Callback));
    if (bRequestPending) return;
    const FString Error = OpenPlatform();
    if (!Error.IsEmpty()) { Finish(Error); return; }
    bRequestPending = true;
    if (bDeviceReady) Login();
    else CreateDevice();
}

void FIslandEOSGuest::CreateDevice()
{
    EOS_Connect_CreateDeviceIdOptions Options{};
    Options.ApiVersion = EOS_CONNECT_CREATEDEVICEID_API_LATEST;
    Options.DeviceModel = FPlatformProperties::IniPlatformName();
    auto* Callback = AuthCallback<EOS_Connect_CreateDeviceIdCallbackInfo>(*this, [this](const auto* Info)
    {
        if (bShutdown) return;
        if (Info->ResultCode != EOS_EResult::EOS_Success && Info->ResultCode != EOS_EResult::EOS_DuplicateNotAllowed)
        { AuthFailed(TEXT("CreateDevice"), Info->ResultCode); return; }
        bDeviceReady = true;
        Login();
    });
    EOS_Connect_CreateDeviceId(EOS_Platform_GetConnectInterface(GetPlatform()), &Options, Callback, Callback->GetCallbackPtr());
}

void FIslandEOSGuest::Login()
{
    EOS_Connect_Credentials Credentials{};
    Credentials.ApiVersion = EOS_CONNECT_CREDENTIALS_API_LATEST;
    Credentials.Type = EOS_EExternalCredentialType::EOS_ECT_DEVICEID_ACCESS_TOKEN;
    EOS_Connect_UserLoginInfo UserInfo{};
    UserInfo.ApiVersion = EOS_CONNECT_USERLOGININFO_API_LATEST;
    UserInfo.DisplayName = "Island Guest";
    EOS_Connect_LoginOptions Options{};
    Options.ApiVersion = EOS_CONNECT_LOGIN_API_LATEST;
    Options.Credentials = &Credentials;
    Options.UserLoginInfo = &UserInfo;
    auto* Callback = AuthCallback<EOS_Connect_LoginCallbackInfo>(*this, [this](const auto* Info)
    {
        if (bShutdown) return;
        if (Info->ResultCode == EOS_EResult::EOS_Success) Authenticated(Info->LocalUserId);
        else if (Info->ResultCode == EOS_EResult::EOS_InvalidUser && Info->ContinuanceToken) CreateUser(Info->ContinuanceToken);
        else AuthFailed(TEXT("Login"), Info->ResultCode);
    });
    EOS_Connect_Login(EOS_Platform_GetConnectInterface(GetPlatform()), &Options, Callback, Callback->GetCallbackPtr());
}

void FIslandEOSGuest::CreateUser(EOS_ContinuanceToken Token)
{
    EOS_Connect_CreateUserOptions Options{};
    Options.ApiVersion = EOS_CONNECT_CREATEUSER_API_LATEST;
    Options.ContinuanceToken = Token;
    auto* Callback = AuthCallback<EOS_Connect_CreateUserCallbackInfo>(*this, [this](const auto* Info)
    {
        if (bShutdown) return;
        if (Info->ResultCode == EOS_EResult::EOS_Success) Authenticated(Info->LocalUserId);
        else AuthFailed(TEXT("CreateUser"), Info->ResultCode);
    });
    EOS_Connect_CreateUser(EOS_Platform_GetConnectInterface(GetPlatform()), &Options, Callback, Callback->GetCallbackPtr());
}

void FIslandEOSGuest::Authenticated(EOS_ProductUserId NewUser)
{
    if (!NewUser || !EOS_ProductUserId_IsValid(NewUser)) { AuthFailed(TEXT("Identity"), EOS_EResult::EOS_InvalidUser); return; }
    if (NewUser != GetUser() || PeerId == EOS_INVALID_NOTIFICATIONID)
    {
        RemovePeerNotification();
        SocketUser->User = NewUser;
        EOS_P2P_AddNotifyPeerConnectionEstablishedOptions Options{};
        Options.ApiVersion = EOS_P2P_ADDNOTIFYPEERCONNECTIONESTABLISHED_API_LATEST;
        Options.LocalUserId = NewUser;
        PeerId = EOS_P2P_AddNotifyPeerConnectionEstablished(EOS_Platform_GetP2PInterface(GetPlatform()), &Options, nullptr, &PeerEstablished);
    }
    bRenewing = false;
    RetryDelay = 10.f;
    FTSTicker::RemoveTicker(Retry);
    Retry.Reset();
    char UserString[EOS_PRODUCTUSERID_MAX_LENGTH + 1]{};
    int32_t Length = sizeof(UserString);
    if (EOS_ProductUserId_ToString(NewUser, UserString, &Length) == EOS_EResult::EOS_Success)
        UE_LOG(LogTemp, Display, TEXT("ISLAND_EOS_GUEST_READY identity=%08x"), FCrc::StrCrc32(UserString));
    Finish(FString());
}

void FIslandEOSGuest::AuthFailed(const TCHAR* Stage, EOS_EResult Result)
{
    UE_LOG(LogTemp, Warning, TEXT("ISLAND_EOS_GUEST_ERROR stage=%s result=%hs"), Stage, EOS_EResult_ToString(Result));
    if (Result == EOS_EResult::EOS_NotFound) bDeviceReady = false;
    if (bRenewing) ScheduleRenewal();
    Finish(TEXT("无法连接在线服务，请检查网络后重试。"));
}

void FIslandEOSGuest::Finish(const FString& Error)
{
    bRequestPending = false;
    TArray<FReady> Callbacks = MoveTemp(Waiting);
    for (FReady& Callback : Callbacks) Callback(Error.IsEmpty(), Error);
}

void FIslandEOSGuest::ScheduleRenewal()
{
    if (Retry.IsValid() || bShutdown) return;
    Retry = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateSPLambda(AsShared(), [this](float)
    {
        Retry.Reset();
        if (!bRequestPending && !bShutdown)
        {
            bRequestPending = true;
            if (bDeviceReady) Login();
            else CreateDevice();
        }
        return false;
    }), RetryDelay);
    RetryDelay = FMath::Min(60.f, RetryDelay * 2.f);
}

void FIslandEOSGuest::RemovePeerNotification()
{
    if (PeerId == EOS_INVALID_NOTIFICATIONID) return;
    EOS_P2P_RemoveNotifyPeerConnectionEstablished(EOS_Platform_GetP2PInterface(GetPlatform()), PeerId);
    PeerId = EOS_INVALID_NOTIFICATIONID;
}

void FIslandEOSGuest::Shutdown()
{
    if (bShutdown) return;
    bShutdown = true;
    FTSTicker::RemoveTicker(Retry);
    Waiting.Reset();
    if (Platform)
    {
        const EOS_HConnect Connect = EOS_Platform_GetConnectInterface(GetPlatform());
        if (ExpirationId != EOS_INVALID_NOTIFICATIONID) EOS_Connect_RemoveNotifyAuthExpiration(Connect, ExpirationId);
        if (StatusId != EOS_INVALID_NOTIFICATIONID) EOS_Connect_RemoveNotifyLoginStatusChanged(Connect, StatusId);
        RemovePeerNotification();
    }
    if (Sockets)
    {
        Sockets->Shutdown();
        Sockets.Reset();
        SocketOwner.Reset();
    }
    SocketUser.Reset();
    Platform.Reset();
}
