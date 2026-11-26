#include "Island/IslandRoomPanel.h"
#include "Island/IslandMessageText.h"
#include "Island/IslandClientSettingsPanel.h"
#include "Island/IslandMenuStyle.h"
#include "ExpeditionText.h"

#include "Island/IslandPlayerController.h"
#include "Island/IslandRoomSession.h"
#include "Island/IslandState.h"
#include "Framework/Application/SlateApplication.h"
#include "Engine/GameInstance.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Misc/CommandLine.h"
#include "Styling/CoreStyle.h"
#include "Brushes/SlateColorBrush.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/SBoxPanel.h"

namespace
{
    using namespace IslandMenuStyle;

    const TCHAR* CharacterName(int32 Model)
    {
        switch (Model)
        {
        case 0: return EXPEDITION_TEXT("小猪", "Pig");
        case 1: return EXPEDITION_TEXT("猴子", "Monkey");
        case 2: return EXPEDITION_TEXT("狐狸", "Fox");
        case 3: return EXPEDITION_TEXT("小牛", "Calf");
        default: return TEXT("");
        }
    }

    FText Txt(const FString& Value) { return FText::FromString(Value); }
    TSharedRef<SWidget> Label(TAttribute<FText> Value, int32 Size, FLinearColor Tint, bool bBold = false, bool bWrap = true)
    {
        return SNew(STextBlock).Text(Value).AutoWrapText(bWrap)
            .Font(FCoreStyle::GetDefaultFontStyle(bBold ? TEXT("Bold") : TEXT("Regular"), Size)).ColorAndOpacity(Tint);
    }

}

void SIslandRoomPanel::Construct(const FArguments& Args)
{
    PC = Args._Controller;
    const auto* S = Session();
    if (S && S->bReturnToSearch) SelectedNetwork = S->GetRoomNetwork();
    else SelectedNetwork = FParse::Param(FCommandLine::Get(), TEXT("IslandLAN")) ? EIslandRoomNetwork::LAN : EIslandRoomNetwork::Internet;
    SetVisibility(EVisibility::Collapsed);
}

AIslandGameState* SIslandRoomPanel::Round() const
{
    return PC.IsValid() ? PC->GetWorld()->GetGameState<AIslandGameState>() : nullptr;
}

AIslandPlayerState* SIslandRoomPanel::Me() const
{
    return PC.IsValid() ? PC->GetPlayerState<AIslandPlayerState>() : nullptr;
}

UIslandRoomSession* SIslandRoomPanel::Session() const
{
    return PC.IsValid() ? PC->GetGameInstance()->GetSubsystem<UIslandRoomSession>() : nullptr;
}

void SIslandRoomPanel::Focus(const TSharedPtr<SWidget>& Widget) const
{
    if (Widget && Widget->IsEnabled() && FSlateApplication::IsInitialized())
    {
        FSlateApplication::Get().SetKeyboardFocus(Widget, EFocusCause::SetDirectly);
    }
}

TSharedRef<SWidget> SIslandRoomPanel::FocusTarget()
{
    auto Focused=FSlateApplication::Get().GetKeyboardFocusedWidget();
    if (Focused && HasFocusedDescendants()) return Focused.ToSharedRef();
    if (ClientSettings) return ClientSettings->FocusTarget();
    if (Primary && Primary->IsEnabled()) return Primary.ToSharedRef();
    return AsShared();
}

TSharedRef<SWidget> SIslandRoomPanel::Button(TAttribute<FText> Value, TFunction<void()> Press,
    TAttribute<bool> Enabled, bool bPrimary, FName Tag)
{
    static const FButtonStyle Plain = MakeButtonStyle(false), Strong = MakeButtonStyle(true);
    return SNew(SButton).Tag(Tag).ButtonStyle(bPrimary ? &Strong : &Plain).IsEnabled(Enabled)
        .OnClicked_Lambda([this,Action = MoveTemp(Press)] { ++Actions; Action(); return FReply::Handled(); })
        [Label(Value, 17, bPrimary ? FLinearColor(.018f,.034f,.034f) : Ink, bPrimary)];
}

void SIslandRoomPanel::Add(TSharedRef<SWidget> Item, float Gap)
{
    Body->AddSlot().AutoHeight().Padding(0,0,0,Gap)[Item];
}

void SIslandRoomPanel::AddTitle(const FString& Title)
{
    Add(Label(Txt(Title), 35, Ink, true), 16);
}

void SIslandRoomPanel::AddMessage(TAttribute<FText> Value, bool bWarning)
{
    TSharedRef<SWidget> Item = Label(Value, 15, bWarning ? Alert : Muted);
    Item->SetVisibility(TAttribute<EVisibility>::CreateLambda([Value]
        { return Value.Get().IsEmpty() ? EVisibility::Collapsed : EVisibility::HitTestInvisible; }));
    Add(Item, 4);
}

void SIslandRoomPanel::Show(EIslandPanel Target)
{
    if (Target == Page || (Target == EIslandPanel::Character && !CanChooseCharacter())) return;
    const bool bFromCharacter = Page == EIslandPanel::Character && Target == EIslandPanel::Lobby;
    const bool bReturning = Page == EIslandPanel::ClientSettings && Target == SettingsOrigin;
    if (Target == EIslandPanel::ClientSettings) SettingsOrigin = Page;
    if (ClientSettings)
    {
        ChildSlot[SNullWidget::NullWidget];
        ClientSettings.Reset();
    }
    Page = Target;
    bChooseEmpty=false;
    LocalError.Reset();
    TransitionAt = FPlatformTime::Seconds();
    if (Page == EIslandPanel::Hidden)
    {
        SetVisibility(EVisibility::Collapsed);
        return;
    }
    if (Page == EIslandPanel::Settings && Round()) Draft = Round()->RoomSettings;
    if (Page == EIslandPanel::Create)
    {
        bSubmittedDraft = false;
        Draft = FIslandRoomSettings{};
        if (ExpeditionText::IsEnglish()) Draft.RoomName = TEXT("Treasure crew");
    }
    SetVisibility(EVisibility::SelfHitTestInvisible);
    Build();
    if (ClientSettings) Focus(ClientSettings->FocusTarget());
    else if (bReturning) Focus(ClientSettingsButton);
    else if (bFromCharacter && CharacterButton && CharacterButton->IsEnabled()) Focus(CharacterButton);
    else if (Page==EIslandPanel::Search && Session() && Entries.Contains(Session()->LastJoinedRoomId))
        Focus(Entries[Session()->LastJoinedRoomId]);
    else Focus(Primary);
}

void SIslandRoomPanel::Build()
{
    const float PreviousHeight=Body ? float(Body->GetCachedGeometry().GetLocalSize().Y)+32.f : 0.f;
    Primary.Reset();
    NameField.Reset();
    AddressField.Reset();
    RefreshButton.Reset();
    List.Reset();
    Entries.Reset();
    LastListings.Reset();
    ClientSettingsButton.Reset();
    CharacterButton.Reset();
    if (Page == EIslandPanel::ClientSettings)
    {
        Body.Reset();
        ChildSlot.HAlign(HAlign_Fill).VAlign(VAlign_Fill).Padding(0)
        [
            SAssignNew(ClientSettings, SIslandClientSettingsPanel)
            .OnClose(FSimpleDelegate::CreateSP(this, &SIslandRoomPanel::Show, SettingsOrigin))
            .OnAction_Lambda([this]{ ++Actions; })
        ];
        return;
    }
    ChildSlot.HAlign(HAlign_Left).VAlign(VAlign_Center).Padding(34,12)
    [
        SNew(SBox).WidthOverride(456.f)
        .HeightOverride_Lambda([this,PreviousHeight]
        {
            const float T=FMath::Clamp(float((FPlatformTime::Seconds()-TransitionAt)/.16),0.f,1.f);
            if (!Body || PreviousHeight<=32.f || T>=1.f) return FOptionalSize();
            const float Target=Body->GetDesiredSize().Y+32.f;
            return FOptionalSize(FMath::Lerp(PreviousHeight,FMath::Max(32.f,Target),FMath::SmoothStep(0.f,1.f,T)));
        })
        [
            SNew(SBorder).BorderImage(&Backing).Padding(0)
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot().AutoWidth()
                [ SNew(SBox).WidthOverride(3)[ SNew(SBorder).BorderImage(&Accent) ] ]
                + SHorizontalBox::Slot().FillWidth(1)
                [ SNew(SScrollBox).ScrollBarPadding(FMargin(1)).ConsumeMouseWheel(EConsumeMouseWheel::WhenScrollingPossible)
                  + SScrollBox::Slot().Padding(22,18,16,14)[ SAssignNew(Body,SVerticalBox) ] ]
            ]
        ]
    ];
    if (Page == EIslandPanel::Home)
    {
        AddTitle(EXPEDITION_TEXT("海岛寻宝", "Treasure Island"));
        Primary = Button(Txt(EXPEDITION_TEXT("创建房间", "Create room")), [this]{ Show(EIslandPanel::Create); }, true, true, "Island.Create");
        Add(Primary.ToSharedRef());
        Add(Button(Txt(EXPEDITION_TEXT("搜索房间", "Find rooms")), [this]{
            if (auto* S = Session()) S->Search(SelectedNetwork);
            Show(EIslandPanel::Search);
        },true,false,"Island.Search"));
        if (SelectedNetwork == EIslandRoomNetwork::Internet)
        {
            auto Retry = Button(Txt(EXPEDITION_TEXT("重试联网", "Retry connection")), [this]{
                if (auto* S = Session()) S->WarmUpInternet();
            }, true, false, "Island.RetryInternet");
            Retry->SetVisibility(TAttribute<EVisibility>::CreateLambda([this]{
                return Session() && Session()->CanRetryInternet() ? EVisibility::Visible : EVisibility::Collapsed;
            }));
            Add(Retry);
        }
        AddClientSettingsButton();
        Add(Button(Txt(EXPEDITION_TEXT("退出游戏", "Quit game")), [this]{
            if (PC.IsValid()) UKismetSystemLibrary::QuitGame(PC.Get(),PC.Get(),EQuitPreference::Quit,false);
        },true,false,"Island.Quit"));
    }
    else if (Page==EIslandPanel::Arriving)
    {
        AddTitle(EXPEDITION_TEXT("正在进入码头", "Entering the dock"));
        AddMessage(Txt(EXPEDITION_TEXT("正在接收房间和队伍信息…", "Receiving room and team details…")));
    }
    else if (Page == EIslandPanel::Create || Page == EIslandPanel::Settings) BuildForm();
    else if (Page == EIslandPanel::Search) BuildBrowser();
    else if (Page == EIslandPanel::Lobby) BuildLobby();
    else if (Page == EIslandPanel::Character) BuildCharacterPicker();
    else if (Page == EIslandPanel::Result)
    {
        const auto* GS = Round();
        AddTitle(!GS || GS->ActiveTeams<=1 || GS->IsIdentityDisguised() ? EXPEDITION_TEXT("找到宝藏", "Treasure found") :
            Me() && Me()->Team==GS->Winner ? EXPEDITION_TEXT("你的队伍找到宝藏", "Your team found the treasure") : FString::Format(EXPEDITION_TEXT("第 {0} 队找到宝藏", "Team {0} found the treasure"), {GS->Winner+1}));
        Add(Label(TAttribute<FText>::CreateLambda([this]
        {
            if (!PC.IsValid()) return FText::GetEmpty();
            const auto& Loot=PC->GetLocalClues();
            const FString Key=Loot.bKeySpent ? FString(EXPEDITION_TEXT("已使用钥匙打开大奖","Treasure unlocked with your key"))
                : Loot.HasKey() ? FString(EXPEDITION_TEXT("已合成钥匙","Key assembled"))
                : FString(EXPEDITION_TEXT("尚未合成钥匙","Key incomplete"));
            return Txt(FString::Format(EXPEDITION_TEXT("真箱点位 {0}/4  ·  散片 {1}  ·  {2}","True sites {0}/4  ·  Spare fragments {1}  ·  {2}"),
                {Loot.Count(),Loot.FragmentCount(),Key}));
        }),16,Ink),14);
        if (Me() && Me()->bHost)
        {
            Primary = Button(Txt(EXPEDITION_TEXT("返回房间", "Return to room")), [this]{ if (PC.IsValid()) PC->ServerReturnToRoom(); },
                TAttribute<bool>::CreateLambda([this]{ return Round() && !Round()->bRoomBlocked; }),true,"Island.ReturnRoom");
            Add(Primary.ToSharedRef());
        }
        AddMessage(TAttribute<FText>::CreateLambda([this]
        {
            if (const auto* State=Round(); State && !State->RoomNotice.IsEmpty()) return Txt(IslandMessageText::Display(State->RoomNotice));
            return Me() && Me()->bHost ? FText::GetEmpty() : Txt(EXPEDITION_TEXT("等待房主返回房间", "Waiting for the host to return to the room"));
        }));
        AddClientSettingsButton();
        Add(Button(Txt(EXPEDITION_TEXT("离开房间", "Leave room")), [this]{ Show(EIslandPanel::Pause); }));
    }
    else if (Page == EIslandPanel::Pause)
    {
        const bool bHost = Me() && Me()->bHost;
        AddTitle(bHost ? EXPEDITION_TEXT("房间菜单", "Room menu") : EXPEDITION_TEXT("游戏菜单", "Game menu"));
        Primary = Button(Txt(EXPEDITION_TEXT("返回", "Back")), [this]{ Back(); },true,true,"Island.Resume");
        Add(Primary.ToSharedRef());
        AddClientSettingsButton();
        Add(Button(Txt(bHost ? EXPEDITION_TEXT("解散房间并离开", "Close room and leave") : EXPEDITION_TEXT("离开房间", "Leave room")), [this]{ if (Session()) Session()->Leave(); }, true, false, "Island.ConfirmLeave"));
    }
    const auto FeedbackSession = [this]{ return Page == EIslandPanel::Create && !bSubmittedDraft ? nullptr : Session(); };
    AddMessage(TAttribute<FText>::CreateLambda([this,FeedbackSession]
    {
        if (!LocalError.IsEmpty()) return Txt(LocalError);
        if (auto* S = FeedbackSession(); S && !S->Error.IsEmpty()) return Txt(IslandMessageText::Display(S->Error));
        if ((Page==EIslandPanel::Lobby || Page==EIslandPanel::Settings) && PC.IsValid() && PC->IsNoticeVisible()) return Txt(PC->GetDisplayNotice());
        return FText::GetEmpty();
    }), true);
    AddMessage(TAttribute<FText>::CreateLambda([FeedbackSession]{ auto* S=FeedbackSession(); return S ? Txt(IslandMessageText::Display(S->Status)) : FText::GetEmpty(); }));
}

void SIslandRoomPanel::AddClientSettingsButton()
{
    ClientSettingsButton = Button(Txt(EXPEDITION_TEXT("游戏设置", "Game settings")),
        [this]{ Show(EIslandPanel::ClientSettings); }, true, false, "Island.ClientSettings");
    Add(ClientSettingsButton.ToSharedRef());
}

void SIslandRoomPanel::BuildForm()
{
    const bool bNew = Page == EIslandPanel::Create;
    AddTitle(bNew ? EXPEDITION_TEXT("创建房间", "Create room") : EXPEDITION_TEXT("房间设置", "Room settings"));
    Add(Label(Txt(EXPEDITION_TEXT("房间名称", "Room name")),15,Muted),2);
    Add(SAssignNew(NameField, SEditableTextBox).Tag("Island.RoomName").Font(FCoreStyle::GetDefaultFontStyle("Regular",16)).Text(Txt(Draft.RoomName))
        .OnTextChanged_Lambda([this](const FText& Value){ Draft.RoomName = Value.ToString(); }));
    const auto Number = [this](const TCHAR* Caption, int32 FIslandRoomSettings::* Member, FName Tag, int32 Minimum=1, int32 Maximum=8)
    {
        Add(SNew(SHorizontalBox)
            + SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)[Label(Txt(Caption),16,Ink)]
            + SHorizontalBox::Slot().AutoWidth()[ SNew(SBox).WidthOverride(106)
                [SNew(SSpinBox<int32>).Tag(Tag).Font(FCoreStyle::GetDefaultFontStyle("Regular",16)).MinValue(Minimum).MaxValue(Maximum)
                 .Value_Lambda([this,Member]{return Draft.*Member;})
                 .OnValueChanged_Lambda([this,Member](int32 Value){ Draft.*Member = Value; })] ]);
    };
    Number(EXPEDITION_TEXT("房间人数上限", "Player limit"), &FIslandRoomSettings::MaxPlayers, "Island.MaxPlayers");
    Number(EXPEDITION_TEXT("队伍数上限", "Team limit"), &FIslandRoomSettings::MaxTeams, "Island.MaxTeams");
    Number(EXPEDITION_TEXT("每队人数上限", "Players per team"), &FIslandRoomSettings::TeamCapacity, "Island.TeamCapacity");
    Number(EXPEDITION_TEXT("额外假箱数量", "Additional decoy chests"), &FIslandRoomSettings::DecoyCount, "Island.DecoyCount", 0, IslandRules::MaxDecoys);
    const TCHAR* CreateLabel = SelectedNetwork == EIslandRoomNetwork::Internet
        ? EXPEDITION_TEXT("创建互联网房间", "Create internet room") : EXPEDITION_TEXT("创建局域网房间", "Create LAN room");
    Primary = Button(Txt(bNew ? CreateLabel : EXPEDITION_TEXT("保存设置", "Save settings")), [this]{ SubmitForm(); },
        TAttribute<bool>::CreateLambda([this]{ return Session() && !Session()->IsBusy(); }), true, "Island.SubmitRoom");
    Add(Primary.ToSharedRef());
    Add(Button(Txt(EXPEDITION_TEXT("返回", "Back")), [this]{ Back(); },true,false,"Island.Back"));
}

void SIslandRoomPanel::SubmitForm()
{
    LocalError.Reset();
    Draft.RoomName.TrimStartAndEndInline();
    if (Draft.RoomName.IsEmpty()) { LocalError = EXPEDITION_TEXT("请输入房间名称", "Enter a room name."); Focus(NameField); return; }
    if (Draft.MaxPlayers > Draft.MaxTeams * Draft.TeamCapacity)
    {
        LocalError = EXPEDITION_TEXT("队伍席位少于房间人数，请增加队数或每队人数", "Add more teams or increase the number of players per team."); return;
    }
    if (Draft.RoomName.Len()>32) { LocalError=EXPEDITION_TEXT("房间名称最多 32 个字符", "Use up to 32 characters for the room name."); Focus(NameField); return; }
    if (Page == EIslandPanel::Settings && PC.IsValid())
    {
        PC->ServerConfigureRoom(Draft);
        Show(EIslandPanel::Lobby);
    }
    else if (Session() && !Session()->IsBusy())
    {
        bSubmittedDraft = true;
        Session()->Host(Draft, SelectedNetwork);
    }
}

void SIslandRoomPanel::BuildBrowser()
{
    AddTitle(SelectedNetwork == EIslandRoomNetwork::Internet
        ? EXPEDITION_TEXT("互联网房间", "Internet rooms") : EXPEDITION_TEXT("局域网房间", "LAN rooms"));
    RefreshButton = Button(TAttribute<FText>::CreateLambda([this]{
        return Txt(Session() && Session()->IsBusy() ? EXPEDITION_TEXT("取消搜索或连接", "Cancel search or connection") : EXPEDITION_TEXT("刷新房间", "Refresh rooms")); }),
        [this]{ if (auto* S = Session()) { if (S->IsBusy()) S->Cancel(); else S->Search(SelectedNetwork); } },true,true,"Island.Refresh");
    Primary = RefreshButton;
    Add(Primary.ToSharedRef());
    Add(SAssignNew(List,SVerticalBox));
    AddMessage(TAttribute<FText>::CreateLambda([this]{
        return Session() && Session()->Status.IsEmpty() && Session()->Error.IsEmpty() && !Session()->IsBusy() && Session()->Rooms.IsEmpty() ? Txt(EXPEDITION_TEXT("没有可加入的房间", "No rooms available to join")) : FText::GetEmpty(); }));
    if (SelectedNetwork == EIslandRoomNetwork::LAN)
    {
        Add(SNew(SHorizontalBox)
            + SHorizontalBox::Slot().FillWidth(1).Padding(0,0,6,0)
            [SAssignNew(AddressField,SEditableTextBox).Tag("Island.Address").Font(FCoreStyle::GetDefaultFontStyle("Regular",16)).HintText(Txt(EXPEDITION_TEXT("局域网 IP 地址", "LAN IP address")))
             .Text(Txt(Address)).OnTextChanged_Lambda([this](const FText& Text){ Address = Text.ToString(); })]
            + SHorizontalBox::Slot().AutoWidth()[Button(Txt(EXPEDITION_TEXT("直连", "Connect")), [this]{
                if (Session()) Session()->JoinAddress(Address.TrimStartAndEnd()); },
                TAttribute<bool>::CreateLambda([this]{ return Session() && !Session()->IsBusy() && !Address.TrimStartAndEnd().IsEmpty(); }),false,"Island.JoinAddress")]);
    }
    Add(Button(Txt(EXPEDITION_TEXT("返回", "Back")), [this]{ Back(); },true,false,"Island.Back"));
    UpdateListings();
}

void SIslandRoomPanel::UpdateListings()
{
    auto* S = Session();
    if (!S || !List || S->GetRoomNetwork() != SelectedNetwork) return;
    FString Signature;
    for (const auto& Room : S->Rooms)
        Signature += FString::Printf(TEXT("%d/%s/%s/%d/%d|"),Room.RoomId.Len(), *Room.RoomId,*Room.RoomName, Room.Occupied,Room.MaxPlayers);
    if (Signature == LastListings) return;
    bool bRowFocused = false;
    if (FSlateApplication::IsInitialized())
        for (const auto& It : Entries) if (It.Value->HasKeyboardFocus()) { LastRoomId = It.Key; bRowFocused = true; break; }
    LastListings = MoveTemp(Signature);
    Entries.Reset();
    List->ClearChildren();
    for (const auto& Room : S->Rooms)
    {
        const FString Id = Room.RoomId;
        const FString Title = FString::Printf(TEXT("%s   %d / %d"), *Room.RoomName, Room.Occupied, Room.MaxPlayers);
        auto Row = Button(Txt(Title), [this,Id]{
            if (auto* SessionState = Session(); SessionState && !SessionState->IsBusy() && SessionState->GetRoomNetwork() == SelectedNetwork)
            {
                LastRoomId = Id;
                SessionState->Join(Id);
            }
        },
            TAttribute<bool>::CreateLambda([this]{ return Session() && !Session()->IsBusy(); }), false, "Island.JoinListing");
        Entries.Add(Id, Row);
        List->AddSlot().AutoHeight().Padding(0,2)[Row];
    }
    if (bRowFocused) Focus(Entries.Contains(LastRoomId) ? Entries[LastRoomId] : RefreshButton);
}

int32 SIslandRoomPanel::CrewSize(int32 Team) const
{
    int32 Count=0;
    if (const auto* GS=Round()) for (const auto& Entry:GS->PlayerArray)
        if (const auto* P=Cast<AIslandPlayerState>(Entry.Get()); P && P->Team==Team) ++Count;
    return Count;
}

FString SIslandRoomPanel::TeamMembers(int32 Team) const
{
    TArray<FString> Members;
    if (const auto* GS=Round()) for (const auto& Entry:GS->PlayerArray)
    {
        const auto* Player=Cast<AIslandPlayerState>(Entry.Get());
        if (!Player || Player->Team!=Team) continue;
        const FString Name=Player==Me()?EXPEDITION_TEXT("你", "You"):Player->GetPlayerName();
        const FString Status=Player->bHost ? EXPEDITION_TEXT("房主", "Host") : Player->bRoomReady ? EXPEDITION_TEXT("已准备", "Ready") : EXPEDITION_TEXT("未准备", "Not ready");
        Members.Add(Name+TEXT(" · ")+CharacterName(Player->GetCharacterModel())+TEXT(" · ")+Status+(Player->bMapReady?TEXT(""):EXPEDITION_TEXT(" · 同步中", " · Syncing")));
    }
    return FString::Join(Members,TEXT("\n"));
}

FString SIslandRoomPanel::RoomStatus() const
{
    const auto* GS = Round();
    if (!GS) return EXPEDITION_TEXT("正在接收房间状态", "Receiving room status");
    if (GS->Phase == EIslandPhase::Starting)
        return FString::Format(EXPEDITION_TEXT("{0} 秒后登岛", "Landing in {0}s"), {FMath::Max(0,FMath::CeilToInt(GS->StartAtServerTime-GS->GetServerWorldTimeSeconds()))});
    if (GS->CanLaunch()) return EXPEDITION_TEXT("所有成员已就绪", "Everyone is ready");
    if (!GS->RoomNotice.IsEmpty()) return IslandMessageText::Display(GS->RoomNotice);
    for (const auto& Entry : GS->PlayerArray)
        if (const auto* P=Cast<AIslandPlayerState>(Entry.Get()); P && !P->bMapReady) return EXPEDITION_TEXT("正在同步海岛地形", "Syncing island terrain");
    return EXPEDITION_TEXT("等待队员准备", "Waiting for players to ready up");
}

bool SIslandRoomPanel::CanChooseCharacter() const
{
    const auto* GS = Round();
    const auto* Self = Me();
    return GS && Self && Self->Team >= 0 && GS->Phase == EIslandPhase::Waiting;
}

void SIslandRoomPanel::BuildCharacterPicker()
{
    AddTitle(EXPEDITION_TEXT("选择角色", "Choose character"));
    static const FButtonStyle ChoiceStyle = []
    {
        FButtonStyle Style = MakeButtonStyle(false);
        Style.SetNormalPadding(FMargin(3, 6));
        Style.SetPressedPadding(FMargin(3, 7, 3, 5));
        return Style;
    }();
    static const FSlateColorBrush Clear(FLinearColor::Transparent);
    const auto Enabled = TAttribute<bool>::CreateSP(this, &SIslandRoomPanel::CanChooseCharacter);
    TSharedPtr<SHorizontalBox> Row;
    for (int32 Model = 0; Model < IslandRules::CharacterModelCount; ++Model)
    {
        const bool FirstColumn = Model % 2 == 0;
        if (FirstColumn)
        {
            Row = SNew(SHorizontalBox);
            Add(Row.ToSharedRef(), 8);
        }
        const auto Selected = [this, Model]{ return Me() && Me()->GetCharacterModel() == Model; };
        const auto Caption = TAttribute<FText>::CreateLambda([Model, Selected]
        {
            return Selected() ? Txt(FString::Format(EXPEDITION_TEXT("{0}，已选", "{0}, selected"), {CharacterName(Model)}))
                              : Txt(CharacterName(Model));
        });
        TSharedRef<SWidget> Option = SNew(SButton)
            .Tag(FName(*FString::Printf(TEXT("Island.Character.%d"), Model)))
            .ButtonStyle(&ChoiceStyle).IsEnabled(Enabled).AccessibleText(Caption)
            .ContentPadding(FMargin(0)).HAlign(HAlign_Fill).VAlign(VAlign_Fill)
            .OnClicked_Lambda([this, Model]
            {
                ++Actions;
                if (CanChooseCharacter()) PC->ServerChooseCharacter(Model);
                return FReply::Handled();
            })
            [
                SNew(SBox).MinDesiredHeight(54)
                [
                    SNew(SBorder).Padding(0).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
                    .BorderBackgroundColor_Lambda([Selected]{
                        return Selected() ? FLinearColor(.47f,.62f,.57f,.24f) : FLinearColor::Transparent;
                    })
                    [
                        SNew(SHorizontalBox)
                        + SHorizontalBox::Slot().AutoWidth()
                        [SNew(SBox).WidthOverride(4)[SNew(SBorder).BorderImage_Lambda([Selected]{ return Selected() ? &Accent : &Clear; })]]
                        + SHorizontalBox::Slot().FillWidth(1).Padding(9,0,6,0).VAlign(VAlign_Center)
                        [Label(Txt(CharacterName(Model)), 19, Ink, true, false)]
                        + SHorizontalBox::Slot().AutoWidth().Padding(2,0,8,0).VAlign(VAlign_Center)
                        [SNew(SBox).Tag(FName(*FString::Printf(TEXT("Island.Character.Current.%d"), Model)))
                            .Visibility_Lambda([Selected]{ return Selected() ? EVisibility::HitTestInvisible : EVisibility::Collapsed; })
                            [Label(Txt(EXPEDITION_TEXT("已选", "Selected")), 14, Ink, false, false)]]
                    ]
                ]
            ];
        Row->AddSlot().FillWidth(1).Padding(FirstColumn ? 0.f : 4.f, 0, FirstColumn ? 4.f : 0.f, 0)[Option];
        if (Selected()) Primary = Option;
    }
    auto Return = Button(Txt(EXPEDITION_TEXT("返回房间", "Back to room")), [this]{ Back(); }, true, false, "Island.Character.Back");
    Add(Return, 0);
    if (!Primary) Primary = Return;
}

void SIslandRoomPanel::BuildLobby()
{
    LobbyName = Round() ? Round()->RoomSettings.RoomName : EXPEDITION_TEXT("房间", "Room");
    bHostLayout = Me() && Me()->bHost;
    AddTitle(LobbyName);
    auto ChestRules=Label(TAttribute<FText>::CreateLambda([this]
    {
        const auto* RoomSession = Session();
        const TCHAR* Network = RoomSession && RoomSession->GetRoomNetwork() == EIslandRoomNetwork::Internet
            ? EXPEDITION_TEXT("互联网", "Internet") : EXPEDITION_TEXT("局域网", "LAN");
        return Round() ? Txt(FString::Format(EXPEDITION_TEXT("{0}  ·  真箱 4  ·  假箱 {1}","{0}  ·  True chests 4  ·  Decoys {1}"),
            {Network, Round()->RoomSettings.DecoyCount})) : FText::GetEmpty();
    }),15,Muted);
    ChestRules->SetTag("Island.LobbyChestRules");
    Add(ChestRules,4);
    AddMessage(TAttribute<FText>::CreateLambda([this]{ return Txt(RoomStatus()); }));
    CharacterButton = Button(Txt(EXPEDITION_TEXT("选择角色", "Choose character")), [this]{ Show(EIslandPanel::Character); },
        TAttribute<bool>::CreateSP(this, &SIslandRoomPanel::CanChooseCharacter), false, "Island.Character");
    Add(CharacterButton.ToSharedRef(), 10);
    TeamRows = Round() ? Round()->RoomSettings.MaxTeams : 0;
    for (int32 Team=0; Team<TeamRows; ++Team)
    {
        auto Crew=SNew(SVerticalBox);
        Crew->SetVisibility(TAttribute<EVisibility>::CreateLambda([this,Team]{ return CrewSize(Team)>0?EVisibility::Visible:EVisibility::Collapsed; }));
        Crew->AddSlot().AutoHeight().Padding(0,4,0,2)
        [
            SNew(SHorizontalBox)
            + SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)
            [Label(TAttribute<FText>::CreateLambda([this,Team]{
                return Txt(FString::Format(EXPEDITION_TEXT("第 {0} 队{1}", "Team {0}{1}"), {Team+1,Me() && Me()->Team==Team?EXPEDITION_TEXT(" · 本队", " · Your team"):TEXT("")}));
            }),17,Ink,true)]
            + SHorizontalBox::Slot().AutoWidth()
            [SNew(SBox).Visibility_Lambda([this,Team]{ return Me() && Me()->Team!=Team?EVisibility::Visible:EVisibility::Collapsed; })
                [Button(Txt(EXPEDITION_TEXT("加入", "Join")), [this,Team]{ if (PC.IsValid()) PC->ServerChooseTeam(Team); },
                    TAttribute<bool>::CreateLambda([this,Team]{return Round() && Me() && Round()->Phase==EIslandPhase::Waiting
                        && !Me()->bRoomReady && CrewSize(Team)<Round()->RoomSettings.TeamCapacity;}),false,
                    FName(*FString::Printf(TEXT("Island.Team.%d"),Team)))]]
        ];
        Crew->AddSlot().AutoHeight().Padding(0,0,0,10)[Label(TAttribute<FText>::CreateLambda([this,Team]{return Txt(TeamMembers(Team));}),15,Ink)];
        Body->AddSlot().AutoHeight()[Crew];
    }
    const auto EmptyToggle=Button(TAttribute<FText>::CreateLambda([this]{return Txt(bChooseEmpty?EXPEDITION_TEXT("收起空队", "Hide empty teams"):EXPEDITION_TEXT("选择其他空队", "Choose an empty team"));}),
        [this]{bChooseEmpty=!bChooseEmpty;},TAttribute<bool>::CreateLambda([this]{return Round() && Me() && !Me()->bRoomReady && Round()->Phase==EIslandPhase::Waiting;}));
    EmptyToggle->SetVisibility(TAttribute<EVisibility>::CreateLambda([this]{
        for (int32 Team=0;Team<TeamRows;++Team) if (!CrewSize(Team)) return EVisibility::Visible;
        return EVisibility::Collapsed;
    }));
    Add(EmptyToggle,6);
    for (int32 Team=0; Team<TeamRows; ++Team)
    {
        auto Empty=Button(Txt(FString::Format(EXPEDITION_TEXT("加入第 {0} 队", "Join team {0}"), {Team+1})),[this,Team]{
            if (PC.IsValid()) PC->ServerChooseTeam(Team);
            bChooseEmpty=false;
        });
        Empty->SetVisibility(TAttribute<EVisibility>::CreateLambda([this,Team]{
            return bChooseEmpty && Me() && !Me()->bRoomReady && Round() && Round()->Phase==EIslandPhase::Waiting && !CrewSize(Team)
                ? EVisibility::Visible : EVisibility::Collapsed;
        }));
        Body->AddSlot().AutoHeight()[Empty];
    }
    if (Me() && Me()->bHost)
    {
        Primary = Button(Txt(EXPEDITION_TEXT("开始寻宝", "Start expedition")), [this]{ if (PC.IsValid()) PC->ServerStartExpedition(); },
            TAttribute<bool>::CreateLambda([this]{ auto* GS=Round(); return GS && GS->Phase==EIslandPhase::Waiting && GS->CanLaunch(); }),true,"Island.Start");
        Add(Primary.ToSharedRef());
        Add(Button(Txt(EXPEDITION_TEXT("房间设置", "Room settings")), [this]{ Show(EIslandPanel::Settings); },
            TAttribute<bool>::CreateLambda([this]{return Round() && Round()->Phase==EIslandPhase::Waiting;}), false,"Island.Settings"));
    }
    else
    {
        Primary = Button(TAttribute<FText>::CreateLambda([this]{ return Txt(Me() && Me()->bRoomReady ? EXPEDITION_TEXT("取消准备", "Cancel ready") : EXPEDITION_TEXT("准备", "Ready up")); }),
            [this]{ if (PC.IsValid() && Me()) PC->ServerSetRoomReady(!Me()->bRoomReady); },
            TAttribute<bool>::CreateLambda([this]{ return Me() && Me()->Team>=0 && Round()
                && (Round()->Phase==EIslandPhase::Waiting || Me()->bRoomReady); }),true,"Island.Ready");
        Add(Primary.ToSharedRef());
    }
    AddClientSettingsButton();
    Add(Button(Txt(EXPEDITION_TEXT("离开房间", "Leave room")), [this]{ Show(EIslandPanel::Pause); }, true, false,"Island.Leave"));
}

void SIslandRoomPanel::Refresh()
{
    if (Page==EIslandPanel::Hidden) return;
    const double Alpha = FMath::Clamp((FPlatformTime::Seconds()-TransitionAt)/.14,0.,1.);
    SetRenderOpacity(.72f + .28f*Alpha);
    SetRenderTransform(FSlateRenderTransform(FVector2D(-6.f*(1.f-Alpha),0)));
    if (Page==EIslandPanel::Search) UpdateListings();
    if (Page==EIslandPanel::Lobby && Round() && (TeamRows!=Round()->RoomSettings.MaxTeams
        || (Me() && Me()->bHost)!=bHostLayout || Round()->RoomSettings.RoomName!=LobbyName))
    {
        Build();
        Focus(Primary);
    }
}

void SIslandRoomPanel::Back()
{
    if (ClientSettings) { ClientSettings->Back(); return; }
    if (Page==EIslandPanel::Home || Page==EIslandPanel::Result) return;
    if (Page==EIslandPanel::Create || Page==EIslandPanel::Search)
    {
        if (Session() && Session()->IsBusy()) Session()->Cancel();
        Show(EIslandPanel::Home);
    }
    else if (Page==EIslandPanel::Lobby) Show(EIslandPanel::Pause);
    else if (Page==EIslandPanel::Settings || Page==EIslandPanel::Character) Show(EIslandPanel::Lobby);
    else if (Page==EIslandPanel::Pause)
    {
        const AIslandGameState* GS=Round();
        Show(GS && GS->Phase==EIslandPhase::Finished ? EIslandPanel::Result :
            GS && (GS->Phase==EIslandPhase::Waiting || GS->Phase==EIslandPhase::Starting) ? EIslandPanel::Lobby : EIslandPanel::Hidden);
    }
}

FReply SIslandRoomPanel::OnPreviewKeyDown(const FGeometry& Geometry, const FKeyEvent& E)
{
    if (ClientSettings) return FReply::Unhandled();
    if (E.GetKey()==EKeys::Escape || (Page==EIslandPanel::Character && E.GetKey()==EKeys::Gamepad_FaceButton_Right)) { if (!E.IsRepeat()) Back(); return FReply::Handled(); }
    return SCompoundWidget::OnPreviewKeyDown(Geometry,E);
}
