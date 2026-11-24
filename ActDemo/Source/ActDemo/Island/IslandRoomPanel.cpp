#include "Island/IslandRoomPanel.h"

#include "Island/IslandPlayerController.h"
#include "Island/IslandRoomSession.h"
#include "Island/IslandState.h"
#include "Framework/Application/SlateApplication.h"
#include "Engine/GameInstance.h"
#include "Kismet/KismetSystemLibrary.h"
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
    const FLinearColor Ink(.9f, .93f, .91f);
    const FLinearColor Muted(.62f, .69f, .68f);
    const FLinearColor Alert(.98f, .76f, .50f);
    const FSlateColorBrush Backing(FLinearColor(.018f, .032f, .040f, .95f));
    const FSlateColorBrush Accent(FLinearColor(.61f, .74f, .70f));
    FText Txt(const FString& Value) { return FText::FromString(Value); }
    TSharedRef<SWidget> Label(TAttribute<FText> Value, int32 Size, FLinearColor Tint, bool bBold = false)
    {
        return SNew(STextBlock).Text(Value).AutoWrapText(true)
            .Font(FCoreStyle::GetDefaultFontStyle(bBold ? TEXT("Bold") : TEXT("Regular"), Size)).ColorAndOpacity(Tint);
    }
    FButtonStyle MakeButtonStyle(bool bActive)
    {
        FButtonStyle Style = FCoreStyle::Get().GetWidgetStyle<FButtonStyle>("Button");
        const FLinearColor Normal = bActive ? FLinearColor(.60f,.74f,.69f) : FLinearColor(.075f,.103f,.113f);
        Style.SetNormal(FSlateColorBrush(Normal));
        Style.SetHovered(FSlateColorBrush(bActive ? FLinearColor(.79f,.89f,.83f) : FLinearColor(.15f,.21f,.22f)));
        Style.SetDisabled(FSlateColorBrush(bActive ? FLinearColor(.42f,.49f,.46f) : FLinearColor(.12f,.15f,.15f)));
        Style.SetPressed(FSlateColorBrush(bActive ? FLinearColor(.47f,.61f,.57f) : FLinearColor(.10f,.15f,.17f)));
        Style.SetNormalPadding(FMargin(14,9));
        Style.SetPressedPadding(FMargin(14,10,14,8));
        return Style;
    }
}

void SIslandRoomPanel::Construct(const FArguments& Args)
{
    PC = Args._Controller;
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
    if (Target == Page) return;
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
    if (Page == EIslandPanel::Create) Draft = FIslandRoomSettings{};
    SetVisibility(EVisibility::SelfHitTestInvisible);
    Build();
    if (Page==EIslandPanel::Search && Session() && Entries.Contains(Session()->LastJoinedRoomId))
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
        AddTitle(TEXT("海岛寻宝"));
        Primary = Button(Txt(TEXT("创建房间")), [this]{ Show(EIslandPanel::Create); }, true, true, "Island.Create");
        Add(Primary.ToSharedRef());
        Add(Button(Txt(TEXT("搜索房间")), [this]{ Show(EIslandPanel::Search); if (Session()) Session()->Search(); },true,false,"Island.Search"));
        Add(Button(Txt(TEXT("退出游戏")), [this]{
            if (PC.IsValid()) UKismetSystemLibrary::QuitGame(PC.Get(),PC.Get(),EQuitPreference::Quit,false);
        },true,false,"Island.Quit"));
    }
    else if (Page==EIslandPanel::Arriving)
    {
        AddTitle(TEXT("正在进入码头"));
        AddMessage(Txt(TEXT("正在接收房间和队伍信息…")));
    }
    else if (Page == EIslandPanel::Create || Page == EIslandPanel::Settings) BuildForm();
    else if (Page == EIslandPanel::Search) BuildBrowser();
    else if (Page == EIslandPanel::Lobby) BuildLobby();
    else if (Page == EIslandPanel::Result)
    {
        const auto* GS = Round();
        AddTitle(!GS || GS->ActiveTeams<=1 ? TEXT("找到宝藏") :
            Me() && Me()->Team==GS->Winner ? TEXT("你的队伍找到宝藏") : FString::Printf(TEXT("第 %d 队找到宝藏"),GS->Winner+1));
        if (Me() && PC.IsValid())
            Add(Label(Txt(FString::Printf(TEXT("本队  第 %d 队     线索  %d / 4"),Me()->Team+1,PC->GetLocalClues().Count())),16,Ink),14);
        if (Me() && Me()->bHost)
        {
            Primary = Button(Txt(TEXT("返回房间")), [this]{ if (PC.IsValid()) PC->ServerReturnToRoom(); },
                TAttribute<bool>::CreateLambda([this]{ return Round() && !Round()->bRoomBlocked; }),true,"Island.ReturnRoom");
            Add(Primary.ToSharedRef());
        }
        AddMessage(TAttribute<FText>::CreateLambda([this]
        {
            if (const auto* State=Round(); State && !State->RoomNotice.IsEmpty()) return Txt(State->RoomNotice);
            return Me() && Me()->bHost ? FText::GetEmpty() : Txt(TEXT("等待房主返回房间"));
        }));
        Add(Button(Txt(TEXT("离开房间")), [this]{ Show(EIslandPanel::Pause); }));
    }
    else if (Page == EIslandPanel::Pause)
    {
        const bool bHost = Me() && Me()->bHost;
        AddTitle(bHost ? TEXT("房间菜单") : TEXT("游戏菜单"));
        Primary = Button(Txt(TEXT("返回")), [this]{ Back(); },true,true,"Island.Resume");
        Add(Primary.ToSharedRef());
        Add(Button(Txt(bHost ? TEXT("解散房间并离开") : TEXT("离开房间")), [this]{ if (Session()) Session()->Leave(); }));
    }
    AddMessage(TAttribute<FText>::CreateLambda([this]
    {
        if (!LocalError.IsEmpty()) return Txt(LocalError);
        if (auto* S = Session(); S && !S->Error.IsEmpty()) return Txt(S->Error);
        if ((Page==EIslandPanel::Lobby || Page==EIslandPanel::Settings) && PC.IsValid() && PC->IsNoticeVisible()) return Txt(PC->GetDisplayNotice());
        return FText::GetEmpty();
    }), true);
    AddMessage(TAttribute<FText>::CreateLambda([this]{ auto* S=Session(); return S ? Txt(S->Status) : FText::GetEmpty(); }));
}

void SIslandRoomPanel::BuildForm()
{
    const bool bNew = Page == EIslandPanel::Create;
    AddTitle(bNew ? TEXT("创建房间") : TEXT("房间设置"));
    Add(Label(Txt(TEXT("房间名称")),15,Muted),2);
    Add(SAssignNew(NameField, SEditableTextBox).Tag("Island.RoomName").Font(FCoreStyle::GetDefaultFontStyle("Regular",16)).Text(Txt(Draft.RoomName))
        .OnTextChanged_Lambda([this](const FText& Value){ Draft.RoomName = Value.ToString(); }));
    const auto Number = [this](const TCHAR* Caption, int32 FIslandRoomSettings::* Member, FName Tag)
    {
        Add(SNew(SHorizontalBox)
            + SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)[Label(Txt(Caption),16,Ink)]
            + SHorizontalBox::Slot().AutoWidth()[ SNew(SBox).WidthOverride(106)
                [SNew(SSpinBox<int32>).Tag(Tag).Font(FCoreStyle::GetDefaultFontStyle("Regular",16)).MinValue(1).MaxValue(8)
                 .Value_Lambda([this,Member]{return Draft.*Member;})
                 .OnValueChanged_Lambda([this,Member](int32 Value){ Draft.*Member = Value; })] ]);
    };
    Number(TEXT("房间人数上限"), &FIslandRoomSettings::MaxPlayers, "Island.MaxPlayers");
    Number(TEXT("队伍数上限"), &FIslandRoomSettings::MaxTeams, "Island.MaxTeams");
    Number(TEXT("每队人数上限"), &FIslandRoomSettings::TeamCapacity, "Island.TeamCapacity");
    Primary = Button(Txt(bNew ? TEXT("创建") : TEXT("保存设置")), [this]{ SubmitForm(); },
        TAttribute<bool>::CreateLambda([this]{ return Session() && !Session()->IsBusy(); }), true, "Island.SubmitRoom");
    Add(Primary.ToSharedRef());
    Add(Button(Txt(TEXT("返回")), [this]{ Back(); },true,false,"Island.Back"));
}

void SIslandRoomPanel::SubmitForm()
{
    Draft.RoomName.TrimStartAndEndInline();
    if (Draft.RoomName.IsEmpty()) { LocalError = TEXT("请输入房间名称"); Focus(NameField); return; }
    if (Draft.MaxPlayers > Draft.MaxTeams * Draft.TeamCapacity)
    {
        LocalError = TEXT("队伍席位少于房间人数，请增加队数或每队人数"); return;
    }
    if (Draft.RoomName.Len()>32) { LocalError=TEXT("房间名称最多 32 个字符"); Focus(NameField); return; }
    if (Page == EIslandPanel::Settings && PC.IsValid())
    {
        PC->ServerConfigureRoom(Draft);
        Show(EIslandPanel::Lobby);
    }
    else if (Session() && !Session()->IsBusy()) Session()->Host(Draft);
}

void SIslandRoomPanel::BuildBrowser()
{
    AddTitle(TEXT("局域网房间"));
    RefreshButton = Button(TAttribute<FText>::CreateLambda([this]{
        return Txt(Session() && Session()->IsBusy() ? TEXT("取消搜索或连接") : TEXT("刷新房间")); }),
        [this]{ if (auto* S = Session()) { if (S->IsBusy()) S->Cancel(); else S->Search(); } },true,true,"Island.Refresh");
    Primary = RefreshButton;
    Add(Primary.ToSharedRef());
    Add(SAssignNew(List,SVerticalBox));
    AddMessage(TAttribute<FText>::CreateLambda([this]{
        return Session() && Session()->Status.IsEmpty() && Session()->Error.IsEmpty() && !Session()->IsBusy() && Session()->Rooms.IsEmpty() ? Txt(TEXT("没有可加入的房间")) : FText::GetEmpty(); }));
    Add(SNew(SHorizontalBox)
        + SHorizontalBox::Slot().FillWidth(1).Padding(0,0,6,0)
        [SAssignNew(AddressField,SEditableTextBox).Tag("Island.Address").Font(FCoreStyle::GetDefaultFontStyle("Regular",16)).HintText(Txt(TEXT("局域网 IP 地址")))
         .Text(Txt(Address)).OnTextChanged_Lambda([this](const FText& Text){ Address = Text.ToString(); })]
        + SHorizontalBox::Slot().AutoWidth()[Button(Txt(TEXT("直连")), [this]{
            if (Session()) Session()->JoinAddress(Address.TrimStartAndEnd()); },
            TAttribute<bool>::CreateLambda([this]{ return Session() && !Session()->IsBusy() && !Address.TrimStartAndEnd().IsEmpty(); }),false,"Island.JoinAddress")]);
    Add(Button(Txt(TEXT("返回")), [this]{ Back(); },true,false,"Island.Back"));
    UpdateListings();
}

void SIslandRoomPanel::UpdateListings()
{
    auto* S = Session();
    if (!S || !List) return;
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
        auto Row = Button(Txt(Title), [this,Id]{ LastRoomId=Id; if (Session() && !Session()->IsBusy()) Session()->Join(Id); },
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
        const FString Name=Player==Me()?TEXT("你"):Player->GetPlayerName();
        const FString Status=Player->bHost ? TEXT("房主") : Player->bRoomReady ? TEXT("已准备") : TEXT("未准备");
        Members.Add(Name+TEXT(" · ")+Status+(Player->bMapReady?TEXT(""):TEXT(" · 同步中")));
    }
    return FString::Join(Members,TEXT("\n"));
}

FString SIslandRoomPanel::RoomStatus() const
{
    const auto* GS = Round();
    if (!GS) return TEXT("正在接收房间状态");
    if (GS->Phase == EIslandPhase::Starting)
        return FString::Printf(TEXT("%d 秒后登岛"), FMath::Max(0,FMath::CeilToInt(GS->StartAtServerTime-GS->GetServerWorldTimeSeconds())));
    if (GS->CanLaunch()) return TEXT("所有成员已就绪");
    if (!GS->RoomNotice.IsEmpty()) return GS->RoomNotice;
    for (const auto& Entry : GS->PlayerArray)
        if (const auto* P=Cast<AIslandPlayerState>(Entry.Get()); P && !P->bMapReady) return TEXT("正在同步海岛地形");
    return TEXT("等待队员准备");
}

void SIslandRoomPanel::BuildLobby()
{
    LobbyName = Round() ? Round()->RoomSettings.RoomName : TEXT("房间");
    bHostLayout = Me() && Me()->bHost;
    AddTitle(LobbyName);
    AddMessage(TAttribute<FText>::CreateLambda([this]{ return Txt(RoomStatus()); }));
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
                return Txt(FString::Printf(TEXT("第 %d 队%s"),Team+1,Me() && Me()->Team==Team?TEXT(" · 本队"):TEXT("")));
            }),17,Ink,true)]
            + SHorizontalBox::Slot().AutoWidth()
            [SNew(SBox).Visibility_Lambda([this,Team]{ return Me() && Me()->Team!=Team?EVisibility::Visible:EVisibility::Collapsed; })
                [Button(Txt(TEXT("加入")), [this,Team]{ if (PC.IsValid()) PC->ServerChooseTeam(Team); },
                    TAttribute<bool>::CreateLambda([this,Team]{return Round() && Me() && Round()->Phase==EIslandPhase::Waiting
                        && !Me()->bRoomReady && CrewSize(Team)<Round()->RoomSettings.TeamCapacity;}),false,
                    FName(*FString::Printf(TEXT("Island.Team.%d"),Team)))]]
        ];
        Crew->AddSlot().AutoHeight().Padding(0,0,0,10)[Label(TAttribute<FText>::CreateLambda([this,Team]{return Txt(TeamMembers(Team));}),15,Ink)];
        Body->AddSlot().AutoHeight()[Crew];
    }
    const auto EmptyToggle=Button(TAttribute<FText>::CreateLambda([this]{return Txt(bChooseEmpty?TEXT("收起空队"):TEXT("选择其他空队"));}),
        [this]{bChooseEmpty=!bChooseEmpty;},TAttribute<bool>::CreateLambda([this]{return Round() && Me() && !Me()->bRoomReady && Round()->Phase==EIslandPhase::Waiting;}));
    EmptyToggle->SetVisibility(TAttribute<EVisibility>::CreateLambda([this]{
        for (int32 Team=0;Team<TeamRows;++Team) if (!CrewSize(Team)) return EVisibility::Visible;
        return EVisibility::Collapsed;
    }));
    Add(EmptyToggle,6);
    for (int32 Team=0; Team<TeamRows; ++Team)
    {
        auto Empty=Button(Txt(FString::Printf(TEXT("加入第 %d 队"),Team+1)),[this,Team]{
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
        Primary = Button(Txt(TEXT("开始寻宝")), [this]{ if (PC.IsValid()) PC->ServerStartExpedition(); },
            TAttribute<bool>::CreateLambda([this]{ auto* GS=Round(); return GS && GS->Phase==EIslandPhase::Waiting && GS->CanLaunch(); }),true,"Island.Start");
        Add(Primary.ToSharedRef());
        Add(Button(Txt(TEXT("房间设置")), [this]{ Show(EIslandPanel::Settings); },
            TAttribute<bool>::CreateLambda([this]{return Round() && Round()->Phase==EIslandPhase::Waiting;}), false,"Island.Settings"));
    }
    else
    {
        Primary = Button(TAttribute<FText>::CreateLambda([this]{ return Txt(Me() && Me()->bRoomReady ? TEXT("取消准备") : TEXT("准备")); }),
            [this]{ if (PC.IsValid() && Me()) PC->ServerSetRoomReady(!Me()->bRoomReady); },
            TAttribute<bool>::CreateLambda([this]{ return Me() && Me()->Team>=0 && Round()
                && (Round()->Phase==EIslandPhase::Waiting || Me()->bRoomReady); }),true,"Island.Ready");
        Add(Primary.ToSharedRef());
    }
    Add(Button(Txt(TEXT("离开房间")), [this]{ Show(EIslandPanel::Pause); }, true, false,"Island.Leave"));
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
    if (Page==EIslandPanel::Home || Page==EIslandPanel::Result) return;
    if (Page==EIslandPanel::Create || Page==EIslandPanel::Search)
    {
        if (Session() && Session()->IsBusy()) Session()->Cancel();
        Show(EIslandPanel::Home);
    }
    else if (Page==EIslandPanel::Lobby) Show(EIslandPanel::Pause);
    else if (Page==EIslandPanel::Settings) Show(EIslandPanel::Lobby);
    else if (Page==EIslandPanel::Pause)
    {
        const AIslandGameState* GS=Round();
        Show(GS && GS->Phase==EIslandPhase::Finished ? EIslandPanel::Result :
            GS && (GS->Phase==EIslandPhase::Waiting || GS->Phase==EIslandPhase::Starting) ? EIslandPanel::Lobby : EIslandPanel::Hidden);
    }
}

FReply SIslandRoomPanel::OnPreviewKeyDown(const FGeometry& Geometry, const FKeyEvent& E)
{
    if (E.GetKey()==EKeys::Escape) { if (!E.IsRepeat()) Back(); return FReply::Handled(); }
    return SCompoundWidget::OnPreviewKeyDown(Geometry,E);
}
