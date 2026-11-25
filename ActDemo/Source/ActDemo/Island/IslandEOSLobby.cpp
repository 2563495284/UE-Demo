#include "Island/IslandEOSLobby.h"

#include "InternetAddrEOS.h"
#include "Misc/NetworkVersion.h"
#include "Misc/ScopeExit.h"
#include "eos_sdk.h"

namespace
{
    // EOS copies options on dispatch. Its completion carries this function across SDK ticks.
    template<typename Info>
    void EOS_CALL LobbyCompleted(const Info* Data)
    {
        if (!EOS_EResult_IsOperationComplete(Data->ResultCode)) return;
        TUniquePtr<TFunction<void(const Info&)>> Fn(static_cast<TFunction<void(const Info&)>*>(Data->ClientData));
        (*Fn)(*Data);
    }

    template<typename H, typename Options, typename Info, typename Done>
    void LobbyCall(void (EOS_CALL *Start)(H, const Options*, void*, void (EOS_CALL *)(const Info*)),
        H Handle, const Options& Opt, Done&& Fn)
    {
        Start(Handle, &Opt, new TFunction<void(const Info&)>(Forward<Done>(Fn)), &LobbyCompleted<Info>);
    }

    bool LobbyOK(EOS_EResult Result)
    {
        return Result == EOS_EResult::EOS_Success || Result == EOS_EResult::EOS_NoChange;
    }

    void LogLobbyResult(const TCHAR* Action, EOS_EResult Result)
    {
        if (!LobbyOK(Result))
            UE_LOG(LogTemp, Warning, TEXT("ISLAND_EOS_LOBBY %s result=%s"), Action, UTF8_TO_TCHAR(EOS_EResult_ToString(Result)));
    }

    FString LobbyBucket()
    {
        return FString::Printf(TEXT("Expedition-Guest-1-%08x"), FNetworkVersion::GetLocalNetworkVersion());
    }

    bool ReadLobbyRow(EOS_HLobbyDetails Detail, FIslandRoomListing& Row)
    {
        EOS_LobbyDetails_CopyInfoOptions Copy = { EOS_LOBBYDETAILS_COPYINFO_API_LATEST };
        EOS_LobbyDetails_Info* Info = nullptr;
        if (!LobbyOK(EOS_LobbyDetails_CopyInfo(Detail, &Copy, &Info))) return false;
        ON_SCOPE_EXIT { EOS_LobbyDetails_Info_Release(Info); };
        if (!Info->LobbyId || !Info->BucketId || LobbyBucket() != UTF8_TO_TCHAR(Info->BucketId)
            || Info->PermissionLevel != EOS_ELobbyPermissionLevel::EOS_LPL_PUBLICADVERTISED
            || !Info->AvailableSlots || Info->MaxMembers > 8 || !Info->MaxMembers) return false;
        Row.RoomId = UTF8_TO_TCHAR(Info->LobbyId);
        Row.MaxPlayers = int32(Info->MaxMembers);

        EOS_LobbyDetails_CopyAttributeByKeyOptions Key = { EOS_LOBBYDETAILS_COPYATTRIBUTEBYKEY_API_LATEST, "ISLAND_ROOM" };
        EOS_Lobby_Attribute* Attribute = nullptr;
        if (!LobbyOK(EOS_LobbyDetails_CopyAttributeByKey(Detail, &Key, &Attribute))) return false;
        if (Attribute->Data && Attribute->Data->ValueType == EOS_ELobbyAttributeType::EOS_AT_STRING
            && Attribute->Data->Value.AsUtf8)
            Row.RoomName = UTF8_TO_TCHAR(Attribute->Data->Value.AsUtf8);
        EOS_Lobby_Attribute_Release(Attribute);
        if (Row.RoomName.IsEmpty() || Row.RoomName.Len() > 32) return false;
        Key.AttrKey = "ISLAND_PLAYERS";
        Attribute = nullptr;
        if (!LobbyOK(EOS_LobbyDetails_CopyAttributeByKey(Detail, &Key, &Attribute))) return false;
        const int64 Count = Attribute->Data && Attribute->Data->ValueType == EOS_ELobbyAttributeType::EOS_AT_INT64
            ? Attribute->Data->Value.AsInt64 : -1;
        EOS_Lobby_Attribute_Release(Attribute);
        if (Count < 1 || Count >= Row.MaxPlayers) return false;
        Row.Occupied = int32(Count);
        return true;
    }
}

FIslandEOSLobby::FIslandEOSLobby(IEOSPlatformHandlePtr InPlatform, EOS_ProductUserId InUser)
    : Platform(MoveTemp(InPlatform)), Handle(EOS_Platform_GetLobbyInterface(*Platform)), User(InUser)
{
    check(Handle && User);
}

FIslandEOSLobby::~FIslandEOSLobby()
{
    CancelSearch();
    if (StatusNotification != EOS_INVALID_NOTIFICATIONID)
        EOS_Lobby_RemoveNotifyLobbyMemberStatusReceived(Handle, StatusNotification);
}

void FIslandEOSLobby::WatchClosure(TFunction<void()> Callback)
{
    OnClosed = MoveTemp(Callback);
    EOS_Lobby_AddNotifyLobbyMemberStatusReceivedOptions Opt = { EOS_LOBBY_ADDNOTIFYLOBBYMEMBERSTATUSRECEIVED_API_LATEST };
    StatusNotification = EOS_Lobby_AddNotifyLobbyMemberStatusReceived(Handle, &Opt, this,
        [](const EOS_Lobby_LobbyMemberStatusReceivedCallbackInfo* Data)
        {
            auto* Self = static_cast<FIslandEOSLobby*>(Data->ClientData);
            if (Self->bShutdown || !Data->LobbyId || Self->ActiveId != UTF8_TO_TCHAR(Data->LobbyId)) return;
            const bool bRemoved = Data->TargetUserId == Self->User && (Data->CurrentStatus == EOS_ELobbyMemberStatus::EOS_LMS_KICKED
                || Data->CurrentStatus == EOS_ELobbyMemberStatus::EOS_LMS_DISCONNECTED);
            if (Data->CurrentStatus != EOS_ELobbyMemberStatus::EOS_LMS_CLOSED && !bRemoved) return;
            Self->ActiveId.Reset();
            Self->bOwner = false;
            if (Self->OnClosed) Self->OnClosed();
        });
}

void FIslandEOSLobby::Create(int32 Capacity, FResult Complete)
{
    EOS_Lobby_CreateLobbyOptions Opt = {};
    Opt.ApiVersion = EOS_LOBBY_CREATELOBBY_API_LATEST;
    Opt.LocalUserId = User;
    Opt.MaxLobbyMembers = Capacity;
    Opt.PermissionLevel = EOS_ELobbyPermissionLevel::EOS_LPL_INVITEONLY;
    Opt.bDisableHostMigration = EOS_TRUE;
    FTCHARToUTF8 Bucket(*LobbyBucket());
    Opt.BucketId = Bucket.Get();
    LobbyCall(&EOS_Lobby_CreateLobby, Handle, Opt,
        [Self = AsShared(), Complete = MoveTemp(Complete)](const EOS_Lobby_CreateLobbyCallbackInfo& Data)
        {
            LogLobbyResult(TEXT("create"), Data.ResultCode);
            const bool bSuccess = LobbyOK(Data.ResultCode) && Data.LobbyId;
            if (bSuccess)
            {
                Self->ActiveId = UTF8_TO_TCHAR(Data.LobbyId);
                Self->bOwner = true;
            }
            if (Self->bShutdown) Self->Close();
            else Complete(bSuccess);
        });
}

void FIslandEOSLobby::CancelSearch()
{
    ++SearchGeneration;
    for (const auto& Item : Results) EOS_LobbyDetails_Release(Item.Value);
    Results.Reset();
}

void FIslandEOSLobby::Search(TFunction<void(bool, TArray<FIslandRoomListing>)> Complete)
{
    CancelSearch();
    EOS_Lobby_CreateLobbySearchOptions Opt = { EOS_LOBBY_CREATELOBBYSEARCH_API_LATEST, 100 };
    EOS_HLobbySearch Search = nullptr;
    EOS_EResult Result = EOS_Lobby_CreateLobbySearch(Handle, &Opt, &Search);
    if (!LobbyOK(Result)) { LogLobbyResult(TEXT("search"), Result); Complete(false, {}); return; }

    FTCHARToUTF8 Bucket(*LobbyBucket());
    EOS_Lobby_AttributeData Match = {};
    Match.ApiVersion = EOS_LOBBY_ATTRIBUTEDATA_API_LATEST;
    Match.Key = EOS_LOBBY_SEARCH_BUCKET_ID;
    Match.ValueType = EOS_ELobbyAttributeType::EOS_AT_STRING;
    Match.Value.AsUtf8 = Bucket.Get();
    EOS_LobbySearch_SetParameterOptions Filter = { EOS_LOBBYSEARCH_SETPARAMETER_API_LATEST, &Match, EOS_EComparisonOp::EOS_CO_EQUAL };
    Result = EOS_LobbySearch_SetParameter(Search, &Filter);
    if (!LobbyOK(Result)) { LogLobbyResult(TEXT("filter"), Result); EOS_LobbySearch_Release(Search); Complete(false, {}); return; }
    EOS_LobbySearch_FindOptions Find = { EOS_LOBBYSEARCH_FIND_API_LATEST, User };
    LobbyCall(&EOS_LobbySearch_Find, Search, Find,
        [Self = AsShared(), Search, Generation = SearchGeneration, Complete = MoveTemp(Complete)](const EOS_LobbySearch_FindCallbackInfo& Data)
        {
            ON_SCOPE_EXIT { EOS_LobbySearch_Release(Search); };
            if (Generation != Self->SearchGeneration || Self->bShutdown) return;
            TArray<FIslandRoomListing> Rows;
            if (!LobbyOK(Data.ResultCode) && Data.ResultCode != EOS_EResult::EOS_NotFound)
            { LogLobbyResult(TEXT("find"), Data.ResultCode); Complete(false, {}); return; }
            EOS_LobbySearch_GetSearchResultCountOptions Count = { EOS_LOBBYSEARCH_GETSEARCHRESULTCOUNT_API_LATEST };
            const uint32 Total = EOS_LobbySearch_GetSearchResultCount(Search, &Count);
            for (uint32 Index = 0; Index < Total; ++Index)
            {
                EOS_LobbySearch_CopySearchResultByIndexOptions Copy = { EOS_LOBBYSEARCH_COPYSEARCHRESULTBYINDEX_API_LATEST, Index };
                EOS_HLobbyDetails Detail = nullptr;
                if (!LobbyOK(EOS_LobbySearch_CopySearchResultByIndex(Search, &Copy, &Detail))) continue;
                FIslandRoomListing Row;
                if (ReadLobbyRow(Detail, Row) && !Self->Results.Contains(Row.RoomId) && !Self->ClosingIds.Contains(Row.RoomId))
                {
                    Self->Results.Add(Row.RoomId, Detail);
                    Rows.Add(MoveTemp(Row));
                }
                else EOS_LobbyDetails_Release(Detail);
            }
            Complete(true, MoveTemp(Rows));
        });
}

void FIslandEOSLobby::Join(const FString& Id, TFunction<void(FString, FString)> Complete)
{
    const EOS_HLobbyDetails* Found = Results.Find(Id);
    if (!Found || ClosingIds.Contains(Id)) { Complete({}, TEXT("房间已失效，请刷新。")); return; }
    EOS_Lobby_JoinLobbyOptions Opt = {};
    Opt.ApiVersion = EOS_LOBBY_JOINLOBBY_API_LATEST;
    Opt.LobbyDetailsHandle = *Found;
    Opt.LocalUserId = User;
    LobbyCall(&EOS_Lobby_JoinLobby, Handle, Opt,
        [Self = AsShared(), Complete = MoveTemp(Complete)](const EOS_Lobby_JoinLobbyCallbackInfo& Data)
        {
            LogLobbyResult(TEXT("join"), Data.ResultCode);
            FString Address;
            if (LobbyOK(Data.ResultCode) && Data.LobbyId)
            {
                Self->ActiveId = UTF8_TO_TCHAR(Data.LobbyId);
                Self->bOwner = false;
                EOS_Lobby_CopyLobbyDetailsHandleOptions Details = { EOS_LOBBY_COPYLOBBYDETAILSHANDLE_API_LATEST, Data.LobbyId, Self->User };
                EOS_HLobbyDetails Joined = nullptr;
                if (LobbyOK(EOS_Lobby_CopyLobbyDetailsHandle(Self->Handle, &Details, &Joined)))
                {
                    EOS_LobbyDetails_GetLobbyOwnerOptions OwnerOptions = { EOS_LOBBYDETAILS_GETLOBBYOWNER_API_LATEST };
                    EOS_ProductUserId Owner = EOS_LobbyDetails_GetLobbyOwner(Joined, &OwnerOptions);
                    if (Owner && EOS_ProductUserId_IsValid(Owner) && Owner != Self->User)
                        Address = FString::Printf(TEXT("[%s]"), *FInternetAddrEOS(Owner).ToString(true));
                    EOS_LobbyDetails_Release(Joined);
                }
                if (Address.IsEmpty() || Self->bShutdown) Self->Close();
            }
            if (Self->bShutdown) return;
            const FString Error = Data.ResultCode == EOS_EResult::EOS_Lobby_TooManyPlayers
                ? TEXT("房间已满。") : TEXT("房间已关闭或无法连接。");
            Complete(MoveTemp(Address), Error);
        });
}

void FIslandEOSLobby::Publish(const FString& Name, int32 Seats, int32 Players, bool bLocked, FResult Complete)
{
    if (!bOwner || ActiveId.IsEmpty()) { Complete(false); return; }
    FTCHARToUTF8 Lobby(*ActiveId);
    EOS_Lobby_UpdateLobbyModificationOptions Start = { EOS_LOBBY_UPDATELOBBYMODIFICATION_API_LATEST, User, Lobby.Get() };
    EOS_HLobbyModification Change = nullptr;
    EOS_EResult Result = EOS_Lobby_UpdateLobbyModification(Handle, &Start, &Change);
    if (!LobbyOK(Result)) { LogLobbyResult(TEXT("modify"), Result); Complete(false); return; }
    ON_SCOPE_EXIT { EOS_LobbyModification_Release(Change); };

    const auto Permission = !bLocked && Players > 0 && Players < Seats
        ? EOS_ELobbyPermissionLevel::EOS_LPL_PUBLICADVERTISED : EOS_ELobbyPermissionLevel::EOS_LPL_INVITEONLY;
    EOS_LobbyModification_SetPermissionLevelOptions Visibility = { EOS_LOBBYMODIFICATION_SETPERMISSIONLEVEL_API_LATEST, Permission };
    EOS_LobbyModification_SetMaxMembersOptions Capacity = { EOS_LOBBYMODIFICATION_SETMAXMEMBERS_API_LATEST, uint32(Seats) };
    Result = EOS_LobbyModification_SetPermissionLevel(Change, &Visibility);
    if (LobbyOK(Result)) Result = EOS_LobbyModification_SetMaxMembers(Change, &Capacity);
    FTCHARToUTF8 Label(*Name);
    EOS_Lobby_AttributeData Field = {};
    Field.ApiVersion = EOS_LOBBY_ATTRIBUTEDATA_API_LATEST;
    Field.Key = "ISLAND_ROOM";
    Field.ValueType = EOS_ELobbyAttributeType::EOS_AT_STRING;
    Field.Value.AsUtf8 = Label.Get();
    EOS_LobbyModification_AddAttributeOptions Attribute = { EOS_LOBBYMODIFICATION_ADDATTRIBUTE_API_LATEST, &Field, EOS_ELobbyAttributeVisibility::EOS_LAT_PUBLIC };
    if (LobbyOK(Result)) Result = EOS_LobbyModification_AddAttribute(Change, &Attribute);
    Field.Key = "ISLAND_PLAYERS";
    Field.ValueType = EOS_ELobbyAttributeType::EOS_AT_INT64;
    Field.Value.AsInt64 = Players;
    if (LobbyOK(Result)) Result = EOS_LobbyModification_AddAttribute(Change, &Attribute);
    if (!LobbyOK(Result)) { LogLobbyResult(TEXT("attributes"), Result); Complete(false); return; }

    EOS_Lobby_UpdateLobbyOptions Update = { EOS_LOBBY_UPDATELOBBY_API_LATEST, Change };
    LobbyCall(&EOS_Lobby_UpdateLobby, Handle, Update,
        [Self = AsShared(), Complete = MoveTemp(Complete), Permission, Players, Seats](const EOS_Lobby_UpdateLobbyCallbackInfo& Data)
        {
            LogLobbyResult(TEXT("publish"), Data.ResultCode);
            if (LobbyOK(Data.ResultCode))
                UE_LOG(LogTemp, Display, TEXT("ISLAND_EOS_ADVERTISED visible=%d occupied=%d seats=%d"),
                    Permission == EOS_ELobbyPermissionLevel::EOS_LPL_PUBLICADVERTISED, Players, Seats);
            if (!Self->bShutdown) Complete(LobbyOK(Data.ResultCode));
        });
}

void FIslandEOSLobby::Close()
{
    if (ActiveId.IsEmpty()) return;
    FString Id = MoveTemp(ActiveId);
    const bool bDestroy = bOwner;
    bOwner = false;
    ClosingIds.Add(Id);
    CloseById(Id, bDestroy, 1.f);
}

void FIslandEOSLobby::CloseById(const FString& Id, bool bDestroy, float RetryDelay)
{
    FTCHARToUTF8 Lobby(*Id);
    auto Closed = [Self = AsShared(), Id, bDestroy, RetryDelay](const auto& Data)
    {
        LogLobbyResult(TEXT("close"), Data.ResultCode);
        if (bDestroy && Data.ResultCode == EOS_EResult::EOS_Success)
            UE_LOG(LogTemp, Display, TEXT("ISLAND_EOS_HOST_CLOSED"));
        const bool bTransient = Data.ResultCode == EOS_EResult::EOS_NoConnection || Data.ResultCode == EOS_EResult::EOS_TimedOut
            || Data.ResultCode == EOS_EResult::EOS_ServiceFailure || Data.ResultCode == EOS_EResult::EOS_TooManyRequests;
        if (!bTransient || Self->bShutdown) { Self->ClosingIds.Remove(Id); return; }

        // Retire this membership by ID. Other rooms can be searched and played while the network recovers.
        FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda(
            [Weak = Self->AsWeak(), Id, bDestroy, RetryDelay](float)
            {
                if (auto Lobby = Weak.Pin(); Lobby && !Lobby->bShutdown)
                    Lobby->CloseById(Id, bDestroy, FMath::Min(30.f, RetryDelay * 2.f));
                return false;
            }), RetryDelay);
    };
    if (bDestroy)
    {
        EOS_Lobby_DestroyLobbyOptions Opt = { EOS_LOBBY_DESTROYLOBBY_API_LATEST, User, Lobby.Get() };
        LobbyCall(&EOS_Lobby_DestroyLobby, Handle, Opt, MoveTemp(Closed));
    }
    else
    {
        EOS_Lobby_LeaveLobbyOptions Opt = { EOS_LOBBY_LEAVELOBBY_API_LATEST, User, Lobby.Get() };
        LobbyCall(&EOS_Lobby_LeaveLobby, Handle, Opt, MoveTemp(Closed));
    }
}

void FIslandEOSLobby::Shutdown()
{
    bShutdown = true;
    OnClosed = {};
    CancelSearch();
    Close();
}
