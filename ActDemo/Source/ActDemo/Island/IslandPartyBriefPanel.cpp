#include "Island/IslandPartyBriefPanel.h"

#include "ExpeditionText.h"
#include "Engine/GameViewportClient.h"
#include "Widgets/SViewport.h"
#include "Island/IslandMenuStyle.h"
#include "Island/IslandPlayerController.h"
#include "Island/IslandState.h"
#include "PartyBriefing.h"
#include "MediaPlayer.h"
#include "MediaTexture.h"
#include "Misc/App.h"
#include "Misc/Paths.h"
#include "RHI.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SDPIScaler.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
    TSharedRef<STextBlock> BriefText(TAttribute<FText> Value, int32 Size, FLinearColor Color, bool bBold = false, bool bWrap = true)
    {
        return SNew(STextBlock).Text(Value).AutoWrapText(bWrap).ColorAndOpacity(Color)
            .Font(FCoreStyle::GetDefaultFontStyle(bBold ? TEXT("Bold") : TEXT("Regular"), Size));
    }

    TSharedRef<SWidget> BriefAction(FName Tag, TAttribute<FText> Caption, const FOnClicked& Action,
        bool bPrimary, TAttribute<bool> Enabled = true)
    {
        static const FButtonStyle Normal = IslandMenuStyle::MakeButtonStyle(false);
        static const FButtonStyle Primary = IslandMenuStyle::MakeButtonStyle(true);
        return SNew(SButton).Tag(Tag).ButtonStyle(bPrimary ? &Primary : &Normal).HAlign(HAlign_Center)
            .IsEnabled(Enabled).OnClicked(Action).AccessibleText(Caption)
            [BriefText(Caption, 16, bPrimary ? FLinearColor(.018f,.034f,.034f) : IslandMenuStyle::Ink, bPrimary, false)];
    }
}

SIslandPartyBriefPanel::SIslandPartyBriefPanel() = default;

SIslandPartyBriefPanel::~SIslandPartyBriefPanel()
{
    if (Player) Player->Close();
    MovieBrush.SetResourceObject(nullptr);
}

const AIslandGameState* SIslandPartyBriefPanel::Round() const
{
    return Controller.IsValid() && Controller->GetWorld() ? Controller->GetWorld()->GetGameState<AIslandGameState>() : nullptr;
}

const FIslandPartyBriefingMember* SIslandPartyBriefPanel::LocalMember() const
{
    const auto* Me = Controller.IsValid() ? Controller->GetPlayerState<AIslandPlayerState>() : nullptr;
    if (!Me || !RoundId || Controller->GetLocalPartyBriefingRound() != RoundId) return nullptr;
    return Controller->GetPartyBriefingMembers().FindByPredicate(
        [Me](const auto& Member){ return Member.PlayerId == Me->GetPlayerId(); });
}

void SIslandPartyBriefPanel::Construct(const FArguments& Args)
{
    Controller = Args._Controller;
    OnAction = Args._OnAction;
    Refresh();
}

void SIslandPartyBriefPanel::Refresh()
{
    if (!Controller.IsValid()) return;
    const uint32 ActiveRound = Controller->GetLocalPartyBriefingRound();
    const FName ActiveFeature = Controller->GetLocalPartyBriefingFeature();
    if (ActiveRound && !ActiveFeature.IsNone() && (RoundId != ActiveRound || Feature != ActiveFeature))
    {
        if (const auto Brief = PartyBriefings::Find(ActiveFeature))
        {
            RoundId = ActiveRound;
            Feature = ActiveFeature;
            bAgreedSent = bForceSent = bPainted = false;
            bMoviePending = true;
            MovieBrush.DrawAs = ESlateBrushDrawType::NoDrawType;
#if !UE_BUILD_SHIPPING
            bLoggedFrame = false;
#endif
            MoviePath = FPaths::Combine(FPaths::ProjectContentDir(), TEXT("Movies"), Brief->MovieFile);
            Build(*Brief);
        }
    }
    if (bPainted && bMoviePending)
    {
        bMoviePending = false;
        OpenMovie();
    }
#if !UE_BUILD_SHIPPING
    if (!bMoviePending && !bLoggedFrame && Player && Texture && Player->GetTime().GetTotalSeconds() > .05 &&
        Texture->GetWidth() > 16 && Texture->GetHeight() > 16)
    {
        bLoggedFrame = true;
        UE_LOG(LogTemp, Display, TEXT("PARTY_BRIEFING_VIDEO feature=%s round=%u size=%dx%d time=%.2f"),
            *Feature.ToString(), RoundId, Texture->GetWidth(), Texture->GetHeight(), Player->GetTime().GetTotalSeconds());
    }
#endif
}

int32 SIslandPartyBriefPanel::OnPaint(const FPaintArgs& Args, const FGeometry& Geometry, const FSlateRect& CullingRect,
    FSlateWindowElementList& DrawElements, int32 LayerId, const FWidgetStyle& Style, bool bParentEnabled) const
{
    const int32 LastLayer = SCompoundWidget::OnPaint(Args, Geometry, CullingRect, DrawElements, LayerId, Style, bParentEnabled);
    if (!bPainted && RoundId && AgreeButton.IsValid() && Controller.IsValid() &&
        Geometry.GetLocalSize().GetMin() > 1.f && FSlateRect::DoRectanglesIntersect(Geometry.GetRenderBoundingRect(), CullingRect))
    {
        bPainted = true;
#if !UE_BUILD_SHIPPING
        FVector2D ViewportSize = FVector2D::ZeroVector;
        if (const auto* Viewport = Controller->GetWorld()->GetGameViewport()) Viewport->GetViewportSize(ViewportSize);
        UE_LOG(LogTemp, Display, TEXT("PARTY_BRIEFING_PAINT round=%u feature=%s screen=%.0fx%.0f viewport=%.0fx%.0f"),
            RoundId, *Feature.ToString(), Geometry.GetLocalSize().X, Geometry.GetLocalSize().Y, ViewportSize.X, ViewportSize.Y);
#endif
        Controller->NotifyPartyBriefingPainted(RoundId);
    }
    return LastLayer;
}

void SIslandPartyBriefPanel::OpenMovie()
{
    // A headless participant still votes, but platform decoders may require a real GPU.
    if (!FApp::CanEverRender() || GUsingNullRHI) return;
    if (!Player)
    {
        Player.Reset(NewObject<UMediaPlayer>());
        Player->PlayOnOpen = true;
        Texture.Reset(NewObject<UMediaTexture>());
        Texture->NewStyleOutput = true;
        Texture->SetMediaPlayer(Player.Get());
        Texture->UpdateResource();
        MovieBrush.SetImageSize(FVector2D(640,360));
        MovieBrush.SetResourceObject(Texture.Get());
    }
    MovieBrush.DrawAs = ESlateBrushDrawType::Image;
    if (MovieImage) MovieImage->Invalidate(EInvalidateWidgetReason::Paint);
    bOpenAccepted = Player->OpenFile(FPaths::ConvertRelativePathToFull(MoviePath));
    Player->SetLooping(true);
}

bool SIslandPartyBriefPanel::MovieFailed() const
{
    return !bMoviePending && (!Player || !bOpenAccepted || Player->HasError() || Player->IsClosed());
}

FText SIslandPartyBriefPanel::MovieCaption() const
{
    if (bMoviePending) return FText::FromString(EXPEDITION_TEXT("演示载入中…", "Loading demo…"));
    if (!Player) return FText::FromString(EXPEDITION_TEXT("视频不可用", "Video unavailable"));
    if (MovieFailed()) return FText::FromString(EXPEDITION_TEXT("重试演示视频", "Retry demo"));
    if (Player->IsPlaying()) return FText::FromString(EXPEDITION_TEXT("暂停演示", "Pause demo"));
    if (Player->IsPaused()) return FText::FromString(EXPEDITION_TEXT("播放演示", "Play demo"));
    return FText::FromString(EXPEDITION_TEXT("演示载入中…", "Loading demo…"));
}

FReply SIslandPartyBriefPanel::ToggleMovie()
{
    if (!Player) return FReply::Handled();
    OnAction.ExecuteIfBound();
    if (MovieFailed()) OpenMovie();
    else if (Player->IsPlaying()) Player->Pause();
    else Player->Play();
    return FReply::Handled();
}

FReply SIslandPartyBriefPanel::Submit(bool bForce)
{
    const auto* Member = LocalMember();
    if (!Member || bForceSent || ForceRequested() || (bForce ? !Member->bHost : Member->bAgreed || bAgreedSent)) return FReply::Handled();
    OnAction.ExecuteIfBound();
    if (bForce)
    {
        bForceSent = true;
        Controller->ServerForceStartParty(RoundId);
    }
    else
    {
        bAgreedSent = true;
        Controller->ServerAgreeParty(RoundId);
    }
    return FReply::Handled();
}

TSharedRef<SWidget> SIslandPartyBriefPanel::FocusTarget()
{
    return AgreeButton.IsValid() && AgreeButton->IsEnabled() ? AgreeButton.ToSharedRef() : AsShared();
}

FText SIslandPartyBriefPanel::AgreeCaption() const
{
    const auto* Member = LocalMember();
    if (Member && Member->bAgreed) return FText::FromString(EXPEDITION_TEXT("已同意", "Ready"));
    if (bAgreedSent) return FText::FromString(EXPEDITION_TEXT("正在确认…", "Confirming…"));
    return FText::FromString(EXPEDITION_TEXT("同意开始", "Ready to play"));
}

FText SIslandPartyBriefPanel::ReadyCount() const
{
    if (!Controller.IsValid()) return FText::GetEmpty();
    const auto& Members = Controller->GetPartyBriefingMembers();
    int32 Agreed = 0;
    for (const auto& Member : Members) Agreed += Member.bAgreed ? 1 : 0;
    const TCHAR* Format = EXPEDITION_TEXT("{0} / {1} 人同意 · 全员同意后开始", "{0} / {1} ready · Starts when everyone agrees");
    if (ForceRequested())
        Format = EXPEDITION_TEXT("{0} / {1} 人同意 · 房主已强制开始", "{0} / {1} ready · Host requested start");
    else if (Agreed && Agreed == Members.Num())
        Format = EXPEDITION_TEXT("{0} / {1} 人同意", "{0} / {1} ready");
    return FText::FromString(FString::Format(Format, {Agreed, Members.Num()}));
}

bool SIslandPartyBriefPanel::ForceRequested() const
{
    const auto* State = Round();
    return State && State->PartyBriefingRound == RoundId && State->bPartyForceStartRequested;
}

FText SIslandPartyBriefPanel::SetupStatus() const
{
    if (!Controller.IsValid()) return FText::GetEmpty();
    const auto& Members = Controller->GetPartyBriefingMembers();
    int32 AssetsReady = 0, GameReady = 0, Agreed = 0;
    for (const auto& Member : Members)
    {
        AssetsReady += Member.bAssetsReady ? 1 : 0;
        GameReady += Member.bGameReady ? 1 : 0;
        Agreed += Member.bAgreed ? 1 : 0;
    }
    if (GameReady < Members.Num() && (Agreed == Members.Num() || ForceRequested()))
    {
        const bool bLoadingArena = AssetsReady < Members.Num();
        const int32 Waiting = Members.Num() - (bLoadingArena ? AssetsReady : GameReady);
        if (Waiting == 1)
            return FText::FromString(bLoadingArena ? EXPEDITION_TEXT("等待 1 位玩家载入", "Waiting for 1 player to load")
                                                   : EXPEDITION_TEXT("等待 1 位玩家就位", "Waiting for 1 player to get into position"));
        return FText::FromString(FString::Format(bLoadingArena
            ? EXPEDITION_TEXT("等待 {0} 位玩家载入", "Waiting for {0} players to load")
            : EXPEDITION_TEXT("等待 {0} 位玩家就位", "Waiting for {0} players to get into position"), {Waiting}));
    }
    if (AssetsReady < Members.Num())
        return FText::FromString(FString::Format(EXPEDITION_TEXT("场地载入 {0} / {1}", "Loading arena {0} / {1}"), {AssetsReady, Members.Num()}));
    if (GameReady < Members.Num())
        return FText::FromString(FString::Format(EXPEDITION_TEXT("角色就位 {0} / {1}", "Players in position {0} / {1}"), {GameReady, Members.Num()}));
    return FText::GetEmpty();
}

FText SIslandPartyBriefPanel::WaitingNames() const
{
    const auto* State = Round();
    if (!State || !Controller.IsValid() || State->IsIdentityDisguised() || ForceRequested()) return FText::GetEmpty();
    TArray<FString> Names;
    for (const auto& Member : Controller->GetPartyBriefingMembers())
        if (!Member.bAgreed) Names.Add(Member.Name.ReplaceCharWithEscapedChar());
    if (Names.IsEmpty()) return FText::GetEmpty();
    return FText::FromString(FString::Format(EXPEDITION_TEXT("等待 {0}", "Waiting for {0}"),
        {FString::Join(Names, EXPEDITION_TEXT("、", ", "))}));
}

FVector2D SIslandPartyBriefPanel::ViewSize() const
{
    const FVector2D Size = GetCachedGeometry().GetLocalSize();
    if (Size.GetMin() > 1.f) return Size;
    // On first layout the new panel has no cached geometry. Read the already visible viewport.
    if (const UWorld* World = Controller.IsValid() ? Controller->GetWorld() : nullptr)
        if (const auto* Viewport = World->GetGameViewport())
            if (const auto Widget = Viewport->GetGameViewportWidget())
            {
                const FVector2D View = Widget->GetCachedGeometry().GetLocalSize();
                if (View.GetMin() > 1.f) return View;
            }
    return FVector2D(1280, 720);
}

float SIslandPartyBriefPanel::PanelScale() const
{
    const FVector2D Size = ViewSize();
    return FMath::Clamp(FMath::Min(Size.X / 1280., Size.Y / 740.), .82, 1.6);
}

FMargin SIslandPartyBriefPanel::PagePadding() const
{
    const FVector2D Size = ViewSize() / PanelScale();
    return FMargin(FMath::Clamp(Size.X * .042, 16., 62.), FMath::Clamp(Size.Y * .058, 12., 45.));
}

float SIslandPartyBriefPanel::ContentWidth() const
{
    const FMargin Padding = PagePadding();
    return FMath::Clamp(float(ViewSize().X / PanelScale()) - Padding.Left - Padding.Right, 1.f, 1450.f);
}

float SIslandPartyBriefPanel::ColumnWidth(bool bMovie) const
{
    // Reserve space for the scrollbar in either the two-column or the narrow stacked layout.
    const float Width = FMath::Max(1.f, ContentWidth() - 20.f);
    return Width < 670.f ? Width : (Width - 24.f) * (bMovie ? .53f : .47f);
}

void SIslandPartyBriefPanel::Build(const FPartyBriefing& Brief)
{
    using namespace IslandMenuStyle;
    const auto Text = [](const FString& Value){ return FText::FromString(Value); };
    static const FSlateColorBrush Background(FLinearColor(.018f,.032f,.040f,1.f));
    static const FSlateColorBrush Screen(FLinearColor(.009f,.018f,.021f));
    auto Copy = SNew(SVerticalBox);
    Copy->AddSlot().AutoHeight().Padding(0,0,0,13)
        [BriefText(Text(Brief.Rules), 18, Ink, true)];
    const auto AddLine = [&Copy, &Text](const TCHAR* Name, const FString& Value)
    {
        Copy->AddSlot().AutoHeight().Padding(0,0,0,4)[BriefText(Text(Name), 14, IslandMenuStyle::Alert, true)];
        Copy->AddSlot().AutoHeight().Padding(0,0,0,12)[BriefText(Text(Value), 16, IslandMenuStyle::Ink)];
    };
    AddLine(EXPEDITION_TEXT("操作", "Controls"), Brief.Controls);
    AddLine(EXPEDITION_TEXT("目标", "Goal"), Brief.Goal);
    FString Result = Brief.Result;
    if (const auto* State = Round(); State && State->bRoomLobby)
        Result += EXPEDITION_TEXT(" 真箱让开箱队和胜队获得碎片，假箱会触发海岛事件。",
            " A real chest awards a fragment to the opening and winning crews. A decoy triggers an island event.");
    AddLine(EXPEDITION_TEXT("结果", "Result"), Result);

    auto Movie = SNew(SVerticalBox)
        + SVerticalBox::Slot().AutoHeight()
        [
            SNew(SBox).HeightOverride_Lambda([this]{ return FOptionalSize(ColumnWidth(true) * 9.f/16.f); })
            [
                SNew(SBorder).BorderImage(&Screen).Padding(0)
                [
                    SNew(SOverlay)
                    + SOverlay::Slot()[SAssignNew(MovieImage, SImage).Image(&MovieBrush)]
                    + SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)
                    [
                        SNew(STextBlock).Text(Text(EXPEDITION_TEXT("演示视频未能播放", "Demo could not be played")))
                        .Visibility_Lambda([this]{return MovieFailed() ? EVisibility::HitTestInvisible : EVisibility::Collapsed;})
                    ]
                ]
            ]
        ]
        + SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Left).Padding(0,6,0,0)
        [
            BriefAction("Island.PartyBrief.Demo", TAttribute<FText>::CreateSP(this,&SIslandPartyBriefPanel::MovieCaption),
                FOnClicked::CreateSP(this,&SIslandPartyBriefPanel::ToggleMovie), false,
                TAttribute<bool>::CreateLambda([this]{return Player && (MovieFailed() || Player->IsReady());}))
        ];
    AgreeButton = BriefAction("Island.PartyBrief.Agree", TAttribute<FText>::CreateSP(this,&SIslandPartyBriefPanel::AgreeCaption),
        FOnClicked::CreateSP(this,&SIslandPartyBriefPanel::Submit, false), true, TAttribute<bool>::CreateLambda([this]{
            const auto* Member = LocalMember();
            return Member && !Member->bAgreed && !bAgreedSent && !bForceSent && !ForceRequested();
        }));
    auto Force = BriefAction("Island.PartyBrief.Force", Text(EXPEDITION_TEXT("房主强制开始", "Force start")),
        FOnClicked::CreateSP(this,&SIslandPartyBriefPanel::Submit, true), false,
        TAttribute<bool>::CreateLambda([this]{return !bForceSent && !ForceRequested();}));
    Force->SetVisibility(TAttribute<EVisibility>::CreateLambda([this]{
        const auto* Member = LocalMember();
        return Member && Member->bHost ? EVisibility::Visible : EVisibility::Collapsed;
    }));

    ChildSlot
    [
        SNew(SBorder).BorderImage(&Background).Padding(0)
        [
            SNew(SDPIScaler).DPIScale(this,&SIslandPartyBriefPanel::PanelScale)
            [
                SNew(SBox).Padding_Lambda([this]{return PagePadding();}).HAlign(HAlign_Center).VAlign(VAlign_Fill)
                [
                    SNew(SBox).WidthOverride_Lambda([this]{return FOptionalSize(ContentWidth());})
                    [
                        SNew(SVerticalBox)
                        + SVerticalBox::Slot().AutoHeight().Padding(0,0,0,20)
                        [
                            BriefText(Text(Brief.Title), 34, Ink, true)
                        ]
                        + SVerticalBox::Slot().FillHeight(1)
                        [
                            SNew(SScrollBox)
                            + SScrollBox::Slot()
                            [
                                SNew(SWrapBox).UseAllottedSize(true).InnerSlotPadding(FVector2D(20,12))
                                + SWrapBox::Slot().VAlign(VAlign_Top)
                                [SNew(SBox).WidthOverride_Lambda([this]{return FOptionalSize(ColumnWidth(false));})[Copy]]
                                + SWrapBox::Slot().VAlign(VAlign_Top)
                                [SNew(SBox).WidthOverride_Lambda([this]{return FOptionalSize(ColumnWidth(true));})[Movie]]
                            ]
                        ]
                        + SVerticalBox::Slot().AutoHeight().Padding(0,8,0,5)
                        [
                            BriefText(TAttribute<FText>::CreateSP(this,&SIslandPartyBriefPanel::SetupStatus), 15, Alert)
                        ]
                        + SVerticalBox::Slot().AutoHeight().Padding(0,0,0,9)
                        [
                            SNew(SHorizontalBox)
                            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
                            [BriefText(TAttribute<FText>::CreateSP(this,&SIslandPartyBriefPanel::ReadyCount), 15, Ink, false, false)]
                            + SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center).Padding(12,0,0,0)
                            [
                                SNew(STextBlock).Text(this,&SIslandPartyBriefPanel::WaitingNames)
                                .ToolTipText(this,&SIslandPartyBriefPanel::WaitingNames)
                                .OverflowPolicy(ETextOverflowPolicy::Ellipsis).Clipping(EWidgetClipping::ClipToBounds)
                                .Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),14)).ColorAndOpacity(Muted)
                            ]
                        ]
                        + SVerticalBox::Slot().AutoHeight()
                        [
                            SNew(SHorizontalBox)
                            + SHorizontalBox::Slot().AutoWidth().Padding(0,0,12,0)[AgreeButton.ToSharedRef()]
                            + SHorizontalBox::Slot().AutoWidth()[Force]
                        ]
                    ]
                ]
            ]
        ]
    ];
}
