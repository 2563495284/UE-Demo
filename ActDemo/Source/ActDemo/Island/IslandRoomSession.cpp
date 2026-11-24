#include "Island/IslandRoomSession.h"

#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "GameFramework/PlayerController.h"
#include "Interfaces/IPv4/IPv4Address.h"
#include "HAL/IConsoleManager.h"
#include "Misc/App.h"
#include "Kismet/GameplayStatics.h"
#include "MoviePlayer.h"
#include "OnlineSubsystemUtils.h"
#include "Tests/IslandViewportRecording.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
    const FName DisplayNameKey(TEXT("ISLAND_ROOM"));
    const FName OccupiedKey(TEXT("ISLAND_PLAYERS"));
    const FName ProtocolKey(TEXT("ISLAND_PROTOCOL"));
    constexpr int32 RoomProtocol = 1;
    const FName IslandMap(TEXT("/Game/TreasureIsland/L_ExpeditionIsland"));
    const FName FrontMap(TEXT("/Game/TreasureIsland/L_IslandFront"));

    const FName WhiteboxDisplayTag(TEXT("IslandWhiteboxDisplay"));

    void SetWhiteboxDisplay(bool bEnabled)
    {
        auto& Console = IConsoleManager::Get();
        if (!bEnabled)
        {
            Console.UnsetAllConsoleVariablesWithTag(WhiteboxDisplayTag, ECVF_SetByPluginHighPriority);
            return;
        }
        // Native-resolution temporal AA keeps the shore and the grid legible. A movable sun
        // supplies the whitebox silhouettes, which do not need film-quality penumbras or motion blur.
        auto Limit = [&](const TCHAR* Name, float Value, bool bMinimum = false)
        {
            auto* Variable = Console.FindConsoleVariable(Name);
            if (Variable && (bMinimum ? Variable->GetFloat() < Value : Variable->GetFloat() > Value))
                Variable->Set(Value, ECVF_SetByPluginHighPriority, WhiteboxDisplayTag);
        };
        Limit(TEXT("r.AntiAliasingMethod"), 2.f);
        Limit(TEXT("r.MotionBlurQuality"), 0.f);
        Limit(TEXT("r.Shadow.Virtual.ResolutionLodBiasDirectional"), .5f, true);
        Limit(TEXT("r.Shadow.Virtual.SMRT.RayCountDirectional"), 2.f);
    }

    FString JoinError(const FString& Detail)
    {
        if (Detail.Contains(TEXT("ISLAND_STARTED"))) return TEXT("该房间已开局，请选择其他房间。");
        if (Detail.Contains(TEXT("ISLAND_FULL"))) return TEXT("房间已满，请选择其他房间。");
        if (Detail.Contains(TEXT("ISLAND_RETURNING"))) return TEXT("上一局的成员正在返回码头，请稍后刷新。");
        if (Detail.Contains(TEXT("full"), ESearchCase::IgnoreCase)) return TEXT("房间已满，请选择其他房间。");
        if (Detail.Contains(TEXT("locked"), ESearchCase::IgnoreCase)) return TEXT("该房间已开局。");
        if (Detail.Contains(TEXT("version"), ESearchCase::IgnoreCase) || Detail.Contains(TEXT("outdated"), ESearchCase::IgnoreCase))
            return TEXT("游戏版本不同，请使用与房主相同的版本。");
        return Detail.IsEmpty() ? TEXT("连接未成功，请刷新房间或检查局域网地址。") : Detail;
    }
}

void UIslandRoomSession::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    GEngine->OnNetworkFailure().AddUObject(this, &ThisClass::NetworkFailed);
    GEngine->OnTravelFailure().AddUObject(this, &ThisClass::TravelFailed);
    FCoreUObjectDelegates::PreLoadMapWithContext.AddUObject(this, &ThisClass::OnPreload);
    FCoreUObjectDelegates::PostLoadMapWithWorld.AddUObject(this, &ThisClass::OnLoaded);
    StartIslandViewportRecording(*GetGameInstance());
}

void UIslandRoomSession::Deinitialize()
{
    if (bWhiteboxDisplay) SetWhiteboxDisplay(false);
    UnbindSessionDelegates();
    if (bHosting || bInRoom || Operation == EOperation::Connecting) DestroyLocalSession();
    if (GEngine)
    {
        GEngine->OnNetworkFailure().RemoveAll(this);
        GEngine->OnTravelFailure().RemoveAll(this);
    }
    FCoreUObjectDelegates::PreLoadMapWithContext.RemoveAll(this);
    FCoreUObjectDelegates::PostLoadMapWithWorld.RemoveAll(this);
    Super::Deinitialize();
}

IOnlineSessionPtr UIslandRoomSession::Sessions() const
{
    return GetWorld() ? Online::GetSessionInterface(GetWorld()) : nullptr;
}

bool UIslandRoomSession::IsBusy() const { return Operation != EOperation::Idle; }

void UIslandRoomSession::UnbindSessionDelegates()
{
    if (const auto Interface = Sessions())
    {
        Interface->ClearOnCreateSessionCompleteDelegate_Handle(CreateDelegate);
        Interface->ClearOnFindSessionsCompleteDelegate_Handle(FindDelegate);
        Interface->ClearOnJoinSessionCompleteDelegate_Handle(JoinDelegate);
    }
}

void UIslandRoomSession::DestroyLocalSession()
{
    if (const auto Interface = Sessions(); Interface && Interface->GetNamedSession(NAME_GameSession))
        Interface->DestroySession(NAME_GameSession);
}

void UIslandRoomSession::Host(const FIslandRoomSettings& Settings)
{
    if (IsBusy() || bInRoom) return;
    Error.Reset();
    bReturnToSearch = false;
    if (!Settings.IsValid()) { Error = TEXT("请填写房名，并为每位玩家留出可用的队伍位置。"); return; }
    const auto Interface = Sessions();
    if (!Interface) { Fail(TEXT("局域网服务未就绪。")); return; }
    HostSettings = Settings;
    HostSettings.RoomName.TrimStartAndEndInline();
    FOnlineSessionSettings Options;
    Options.bIsLANMatch = true;
    Options.bShouldAdvertise = true;
    Options.bAllowJoinInProgress = false;
    Options.NumPublicConnections = HostSettings.MaxPlayers;
    Options.Set(ProtocolKey, RoomProtocol, EOnlineDataAdvertisementType::ViaOnlineService);
    Options.Set(DisplayNameKey, HostSettings.RoomName, EOnlineDataAdvertisementType::ViaOnlineService);
    // NULL opens its beacon at creation. Hide this entry until the host has logged into the listen world.
    Options.Set(OccupiedKey, HostSettings.MaxPlayers, EOnlineDataAdvertisementType::ViaOnlineService);
    Operation = EOperation::Creating;
    Status = TEXT("正在创建房间…");
    CreateDelegate = Interface->AddOnCreateSessionCompleteDelegate_Handle(
        FOnCreateSessionCompleteDelegate::CreateUObject(this, &ThisClass::Created));
    if (!Interface->CreateSession(0, NAME_GameSession, Options) && Operation == EOperation::Creating) Fail(TEXT("无法在本机创建房间。"));
}

void UIslandRoomSession::Created(FName Name, bool bSuccess)
{
    if (Name != NAME_GameSession || Operation != EOperation::Creating) return;
    UnbindSessionDelegates();
    if (!bSuccess) { Fail(TEXT("无法创建局域网房间。")); return; }
    bHosting = true;
    Operation = EOperation::Opening;
    Status = TEXT("正在前往码头…");
    UGameplayStatics::OpenLevel(this, IslandMap, true, TEXT("listen"));
}

void UIslandRoomSession::UpdateAdvertisement(const FIslandRoomSettings& Settings, int32 Occupied, bool bLocked)
{
    if (!bHosting) return;
    const auto Interface = Sessions();
    FNamedOnlineSession* Named = Interface ? Interface->GetNamedSession(NAME_GameSession) : nullptr;
    if (!Named) return;
    HostSettings = Settings;
    FOnlineSessionSettings Options = Named->SessionSettings;
    Options.NumPublicConnections = Settings.MaxPlayers;
    Options.bShouldAdvertise = true;
    Options.Set(DisplayNameKey, Settings.RoomName, EOnlineDataAdvertisementType::ViaOnlineService);
    Options.Set(OccupiedKey, Occupied, EOnlineDataAdvertisementType::ViaOnlineService);
    Named->NumOpenPublicConnections = FMath::Max(0, Settings.MaxPlayers - Occupied);
    Interface->UpdateSession(NAME_GameSession, Options, true);
    // NULL starts/stops its LAN beacon on Start/EndSession, not UpdateSession.
    if (bLocked && Named->SessionState != EOnlineSessionState::InProgress)
        Interface->StartSession(NAME_GameSession);
    else if (!bLocked && Named->SessionState == EOnlineSessionState::InProgress)
        Interface->EndSession(NAME_GameSession);
}

void UIslandRoomSession::Search()
{
    if (IsBusy() || bInRoom) return;
    Error.Reset();
    const auto Interface = Sessions();
    if (!Interface) { Fail(TEXT("局域网服务未就绪。")); return; }
    bReturnToSearch = true;
    Status = TEXT("正在搜索局域网…");
    Operation = EOperation::Searching;
    SearchRequest = MakeShared<FOnlineSessionSearch>();
    SearchRequest->bIsLanQuery = true;
    SearchRequest->MaxSearchResults = 100;
    FindDelegate = Interface->AddOnFindSessionsCompleteDelegate_Handle(FOnFindSessionsCompleteDelegate::CreateUObject(this, &ThisClass::Found));
    if (!Interface->FindSessions(0, SearchRequest.ToSharedRef()) && Operation == EOperation::Searching) Fail(TEXT("无法搜索局域网。"));
}

void UIslandRoomSession::Found(bool bSuccess)
{
    if (Operation != EOperation::Searching) return;
    UnbindSessionDelegates();
    if (!bSuccess || !SearchRequest) { Fail(TEXT("搜索失败，请检查局域网连接。")); return; }
    Rooms.Reset();
    SearchResults.Reset();
    for (const auto& Result : SearchRequest->SearchResults)
    {
        const auto& Options = Result.Session.SessionSettings;
        int32 Protocol = 0;
        if (!Result.IsValid() || !Options.Get(ProtocolKey, Protocol) || Protocol != RoomProtocol) continue;
        FIslandRoomListing Row;
        Row.RoomId = Result.GetSessionIdStr();
        if (!Options.Get(DisplayNameKey, Row.RoomName) || SearchResults.Contains(Row.RoomId)) continue;
        Options.Get(OccupiedKey, Row.Occupied);
        Row.MaxPlayers = Options.NumPublicConnections;
        if (Row.Occupied >= Row.MaxPlayers) continue;
        SearchResults.Add(Row.RoomId, Result);
        Rooms.Add(MoveTemp(Row));
    }
    Rooms.Sort([](const auto& A, const auto& B) { return A.RoomName == B.RoomName ? A.RoomId < B.RoomId : A.RoomName < B.RoomName; });
    Operation = EOperation::Idle;
    Status = Rooms.IsEmpty() ? TEXT("尚未找到可加入的房间") : FString();
    UE_LOG(LogTemp, Display, TEXT("ISLAND_LAN_SEARCH results=%d"), Rooms.Num());
}

void UIslandRoomSession::Join(const FString& RoomId)
{
    if (IsBusy() || bInRoom) return;
    const auto* Target = SearchResults.Find(RoomId);
    const auto Interface = Sessions();
    if (!Interface || !Target) { Fail(TEXT("房间已失效，请刷新。")); return; }
    LastJoinedRoomId = RoomId;
    Error.Reset();
    bReturnToSearch = true;
    Operation = EOperation::Joining;
    Status = TEXT("正在加入房间…");
    JoinDelegate = Interface->AddOnJoinSessionCompleteDelegate_Handle(FOnJoinSessionCompleteDelegate::CreateUObject(this, &ThisClass::Joined));
    if (!Interface->JoinSession(0, NAME_GameSession, *Target) && Operation == EOperation::Joining) Fail(TEXT("未能加入房间，请刷新。"));
}

void UIslandRoomSession::Joined(FName Name, EOnJoinSessionCompleteResult::Type Result)
{
    if (Name != NAME_GameSession || Operation != EOperation::Joining) return;
    UnbindSessionDelegates();
    FString ConnectString;
    const auto Interface = Sessions();
    if (Result != EOnJoinSessionCompleteResult::Success || !Interface || !Interface->GetResolvedConnectString(Name, ConnectString))
    {
        Fail(Result == EOnJoinSessionCompleteResult::SessionIsFull ? TEXT("房间已满。") : TEXT("房间已关闭或无法连接。"));
        return;
    }
    Connect(ConnectString);
}

void UIslandRoomSession::JoinAddress(const FString& Address)
{
    if (IsBusy() || bInRoom) return;
    bReturnToSearch = true;
    FString HostIP = Address.TrimStartAndEnd(), Port;
    int32 PortNumber = 7777;
    if (HostIP.Contains(TEXT(":")))
    {
        FString IP;
        HostIP.Split(TEXT(":"), &IP, &Port);
        HostIP = IP;
        if (Port.IsEmpty() || Port.Len() > 5 || !Port.IsNumeric()) { Error = TEXT("端口应在 1 到 65535 之间。"); return; }
        PortNumber = FCString::Atoi(*Port);
    }
    FIPv4Address IP;
    if (!FIPv4Address::Parse(HostIP, IP) || IP.ToString() != HostIP || PortNumber < 1 || PortNumber > 65535)
    { Error = TEXT("请输入局域网 IPv4 地址，可附端口。"); return; }
    Connect(FString::Printf(TEXT("%s:%d"), *HostIP, PortNumber));
}

void UIslandRoomSession::Connect(const FString& Address)
{
    APlayerController* PC = GetGameInstance()->GetFirstLocalPlayerController();
    if (!PC) { Fail(TEXT("控制器尚未就绪。")); return; }
    Operation = EOperation::Connecting;
    Status = TEXT("正在连接码头…");
    Error.Reset();
    PC->ClientTravel(Address, TRAVEL_Absolute);
}

void UIslandRoomSession::Cancel()
{
    const EOperation Previous = Operation;
    if (Previous == EOperation::Idle || Previous == EOperation::Leaving) return;
    Operation = EOperation::Idle;
    UnbindSessionDelegates();
    if (Previous == EOperation::Searching)
    {
        if (const auto Interface = Sessions()) Interface->CancelFindSessions();
        SearchRequest.Reset();
    }
    else
    {
        if (GEngine && GetWorld()) GEngine->CancelPending(GetWorld());
        DestroyLocalSession();
        bHosting = false;
        if (Previous == EOperation::Connecting || Previous == EOperation::Opening) LoadFront();
    }
    Status.Reset();
    Error.Reset();
}

void UIslandRoomSession::Leave()
{
    if (Operation == EOperation::Leaving) return;
    bReturnToSearch = !bHosting;
    bHosting = bInRoom = false;
    Operation = EOperation::Leaving;
    Error.Reset();
    Status = TEXT("正在离开房间…");
    DestroyLocalSession();
    LoadFront();
}

void UIslandRoomSession::LoadFront()
{
    if (GetWorld()) UGameplayStatics::OpenLevel(this, FrontMap, true);
}

void UIslandRoomSession::Fail(const FString& Message)
{
    UnbindSessionDelegates();
    DestroyLocalSession();
    Operation = EOperation::Idle;
    bHosting = bInRoom = false;
    Status.Reset();
    Error = Message;
    UE_LOG(LogTemp, Warning, TEXT("ISLAND_LAN_ERROR %s"), *Message);
}

void UIslandRoomSession::NetworkFailed(UWorld* World, UNetDriver*, ENetworkFailure::Type Kind, const FString& Message)
{
    if ((World && World->GetGameInstance() != GetGameInstance()) || (!bInRoom && Operation != EOperation::Connecting && Operation != EOperation::Opening)) return;
    if (bHosting)
    {
        if (Operation == EOperation::Opening)
        {
            Fail(TEXT("本机未能打开联机端口，请返回后重试。"));
            LoadFront();
        }
        return;
    }
    bReturnToSearch = true;
    if (Kind == ENetworkFailure::OutdatedClient || Kind == ENetworkFailure::OutdatedServer)
        Fail(TEXT("游戏版本不同，请使用与房主相同的版本。"));
    else if (Kind == ENetworkFailure::ConnectionLost || Kind == ENetworkFailure::ConnectionTimeout)
        Fail(bInRoom ? TEXT("与房主失去连接，请刷新房间列表。") : TEXT("连接超时，请检查房主地址和局域网连接。"));
    else
        Fail(JoinError(Message));
    // Engine failure recovery travels to GameDefaultMap.
}

void UIslandRoomSession::TravelFailed(UWorld* World, ETravelFailure::Type, const FString& Message)
{
    if ((World && World->GetGameInstance() != GetGameInstance()) || (!bInRoom && !IsBusy())) return;
    bReturnToSearch = !bHosting;
    Fail(JoinError(Message));
}

void UIslandRoomSession::UpdateDisplayForMap(const FString& Map, bool bGameWorld)
{
    const bool bDisplay = bGameWorld && FApp::CanEverRender() &&
        (Map.EndsWith(TEXT("L_IslandFront")) || Map.EndsWith(TEXT("L_ExpeditionIsland")));
    if (bDisplay == bWhiteboxDisplay) return;
    SetWhiteboxDisplay(bDisplay);
    bWhiteboxDisplay = bDisplay;
}

void UIslandRoomSession::OnPreload(const FWorldContext& Context, const FString& Map)
{
    if (Context.OwningGameInstance != GetGameInstance()) return;
    UpdateDisplayForMap(Map, Context.WorldType == EWorldType::Game);
    if ((!IsBusy() && !bInRoom) || !IsMoviePlayerEnabled()) return;
    FLoadingScreenAttributes Loading;
    Loading.bAutoCompleteWhenLoadingCompletes = true;
    Loading.WidgetLoadingScreen = SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
        .BorderBackgroundColor(FLinearColor(.055f,.079f,.09f)).HAlign(HAlign_Center).VAlign(VAlign_Center)
        [ SNew(STextBlock).Text(FText::FromString(Status.IsEmpty() ? TEXT("正在返回码头…") : Status)) ];
    GetMoviePlayer()->SetupLoadingScreen(Loading);
}

void UIslandRoomSession::OnLoaded(UWorld* World)
{
    if (!World || World->GetGameInstance() != GetGameInstance()) return;
    UpdateDisplayForMap(World->GetMapName(), World->WorldType == EWorldType::Game);
    if (World->GetMapName().EndsWith(TEXT("L_IslandFront")))
    {
        bHosting = bInRoom = false;
        Operation = EOperation::Idle;
        Status.Reset();
    }
    else if (Operation == EOperation::Connecting || Operation == EOperation::Opening)
    {
        bInRoom = true;
        Operation = EOperation::Idle;
        Status.Reset();
        UE_LOG(LogTemp, Display, TEXT("ISLAND_LAN_ARRIVED host=%d"), bHosting);
    }
}
