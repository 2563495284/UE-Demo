#include "PartyVoxelArena.h"
#include "PartyPunchHUD.h"
#include "ExpeditionText.h"
#include "Engine/Engine.h"
#include "Engine/Canvas.h"
#include "GameFramework/HUD.h"
#include "GameFramework/PlayerController.h"

void APartyVoxelPawn::DrawPartyHUD(AHUD& HUD, const FVector2D& ViewportSize, bool bAnonymous) const
{
    const auto* Game=GetSession();
    if (!Game || !GEngine) return;
    const float W=ViewportSize.X, H=ViewportSize.Y;
    const float S=FMath::Clamp(H/720.f,.68f,1.65f);
    const FLinearColor Back(.018f,.033f,.045f,.90f), Ink(.97f,.97f,.91f);
    const FLinearColor Accent=FLinearColor(Game->Theme);
    auto* Font=GEngine->GetMediumFont();
    const auto Label=[&](const FString& Text,float X,float Y,float Scale,FLinearColor Color,bool bRight=false)
    {
        float Width,Height;
        HUD.GetTextSize(Text,Width,Height,Font,Scale*S);
        const float Fit=FMath::Min(1.f,FMath::Max(1.f,W-46*S)/FMath::Max(1.f,Width));
        Width*=Fit;
        Height*=Fit;
        if (bRight) X-=Width;
        X=FMath::Clamp(X,7*S,FMath::Max(7*S,W-Width-7*S));
        HUD.DrawRect(Back,X-7*S,Y-3*S,Width+14*S,Height+6*S);
        HUD.DrawText(Text,Color,X,Y,Font,Scale*S*Fit);
    };
    const auto Center=[&](const FString& Text,float Y,float Scale,FLinearColor Color)
    {
        float Width,Height;
        HUD.GetTextSize(Text,Width,Height,Font,Scale*S);
        Label(Text,FMath::Max(23*S,(W-Width)*.5f),Y,Scale,Color);
    };
    const bool bEnglish = ExpeditionText::IsEnglish();
    if (Game->IsChaseGame())
    {
        const int32 RunnerTeam = Game->GetRunnerTeam();
        const bool bAssigned = RunnerTeam != INDEX_NONE;
        const bool bRunner = Game->IsRunner(Seat);
        const bool bCaught = Game->IsCaught(Seat);
        const auto* Viewer = HUD.GetOwningPlayerController();
        const auto* Target = Viewer ? Cast<APartyVoxelPawn>(Viewer->GetViewTarget()) : nullptr;
        const bool bWatching = bCaught && Target && Target != this && Target->GetSession() == Game
            && Game->IsRunner(Target->GetSeat()) && !Game->IsCaught(Target->GetSeat());
        if (bCaught && !bWatching) HUD.DrawRect(FLinearColor(.025f,.045f,.055f,1.f),0,0,W,H);

        const FString Title = bEnglish ? Game->EnglishTitle : Game->Title;
        const FString RoleLabel = bRunner ? EXPEDITION_TEXT("逃跑方", "Runner") : EXPEDITION_TEXT("追捕方", "Hunter");
        const FLinearColor RoleColor = FLinearColor(bRunner ? FColor(247,174,70) : FColor(98,209,227));
        Label(bAssigned ? Title + TEXT("  ·  ") + RoleLabel : Title,23*S,18*S,1.15f,Ink);
        HUD.DrawRect(bAssigned ? RoleColor : Accent,13*S,15*S,3*S,31*S);

        const auto Phase = Game->GetPhase();
        if (Phase == EPartyVoxelPhase::Results)
        {
            const int32 Mask = Game->GetWinningTeamMask();
            const bool bRunnersWon = bAssigned && (Mask & (1 << RunnerTeam)) != 0;
            const FString Result = !Mask ? EXPEDITION_TEXT("本轮结束", "Round over") : bRunnersWon
                ? EXPEDITION_TEXT("逃跑队获胜", "Runners win") : EXPEDITION_TEXT("追捕各队获胜", "Hunting teams win");
            Center(Result,H*.38f,1.6f,Ink);
            Center(EXPEDITION_TEXT("即将回到海岛揭晓宝箱", "Returning to the island to reveal the chest"),H*.49f,.93f,Ink);
            return;
        }
        if (Phase == EPartyVoxelPhase::Waiting || Phase == EPartyVoxelPhase::Countdown)
        {
            if (bAssigned && !bAnonymous)
                Label(FString::Format(EXPEDITION_TEXT("本轮逃跑队  ·  第 {0} 队", "Runner crew  ·  Team {0}"), {RunnerTeam+1}),23*S,57*S,.91f,Ink);
            if (Phase == EPartyVoxelPhase::Countdown)
                Center(FString::FromInt(FMath::Max(1,FMath::CeilToInt(Game->SecondsLeft()))),H*.40f,2.8f,Ink);
            return;
        }

        const float HeadStart = Game->GetHeadStartRemaining();
        const int32 Clock = FMath::Max(0,FMath::CeilToInt(Game->SecondsLeft()-HeadStart));
        Label(FString::Printf(TEXT("%d:%02d"),Clock/60,Clock%60),W-23*S,18*S,1.25f,
            Clock<=10 ? FLinearColor(1.f,.70f,.36f) : Ink,true);
        const FString Count = FString::Format(EXPEDITION_TEXT("在逃  {0} / {1}", "Still free  {0} / {1}"),
            {Game->GetRemainingRunners(),Game->GetRunnerCount()});
        Label(Count,W-23*S,57*S,.96f,Ink,true);
        if (bCaught)
        {
            FString Status = EXPEDITION_TEXT("已被捕  ·  等待队友视角", "Caught  ·  Waiting for a teammate");
            if (bWatching)
            {
                const auto* Row = Game->GetStandings().FindByPredicate([Target](const auto& Other){ return Other.Seat==Target->GetSeat(); });
                Status = Row && !bAnonymous ? FString::Format(EXPEDITION_TEXT("已被捕  ·  正在观战 {0}", "Caught  ·  Watching {0}"),{Row->PlayerName})
                    : FString(EXPEDITION_TEXT("已被捕  ·  正在观战队友", "Caught  ·  Watching teammate"));
            }
            Center(Status,H-53*S,1.08f,Ink);
            return;
        }
        if (HeadStart>0.f)
        {
            const int32 Remaining=FMath::CeilToInt(HeadStart);
            Center(bRunner ? FString::Format(EXPEDITION_TEXT("快躲起来！{0} 秒后开始追捕", "Find cover! Hunters start in {0}s"), {Remaining})
                : FString::Format(EXPEDITION_TEXT("等待出发  ·  {0}", "Hunt begins in {0}"), {Remaining}),H*.36f,1.3f,Ink);
        }
        if (bAssigned && !bAnonymous)
            Label(FString::Format(EXPEDITION_TEXT("逃跑队  ·  第 {0} 队", "Runner crew  ·  Team {0}"), {RunnerTeam+1}),23*S,57*S,.91f,Ink);
        if (bRunner || HeadStart<=0.f)
        {
            PartyPunch::DrawChargeRing(HUD,W*.5f,H*.5f,S,GetPunchChargeAlpha());
            const auto Aim = Game->GetChaseAim(*this,GetPawnViewLocation(),GetToolAimDirection());
            const bool bCanCatch = Aim == EPartyChaseAim::Runner;
            const auto MarkerColor = bCanCatch ? FLinearColor(FColor(247,174,70)) : Ink;
            HUD.DrawRect(Back,W*.5f-3*S,H*.5f-3*S,6*S,6*S);
            HUD.DrawRect(MarkerColor,W*.5f-1.5f*S,H*.5f-1.5f*S,3*S,3*S);
            const double RecoveryLeft = NextLocalPlace - GetWorld()->GetTimeSeconds();
            if (RecoveryLeft > 0.)
            {
                const float Ready = FMath::Clamp(1.-RecoveryLeft/APartyVoxelSession::ChaseToolRecovery(true),0.,1.);
                HUD.DrawRect(Back,W*.5f-12*S,H*.5f+8*S,24*S,3*S);
                HUD.DrawRect(Ink,W*.5f-12*S,H*.5f+8*S,24*S*Ready,3*S);
            }
            if (bCanCatch) Center(EXPEDITION_TEXT("右键  砸晕", "RMB  Catch"),H*.5f+19*S,.93f,MarkerColor);
            else if (Aim==EPartyChaseAim::Gap) Center(EXPEDITION_TEXT("右键  补墙", "RMB  Repair wall"),H*.5f+19*S,.88f,Ink);
            else if (Aim==EPartyChaseAim::Wall) Center(EXPEDITION_TEXT("松开左键出拳", "Release LMB to punch"),H*.5f+19*S,.88f,Ink);
        }
        Center(bRunner ? EXPEDITION_TEXT("坚持到时间结束    左键蓄力打孔    右键补墙", "Survive until time runs out    Hold / release LMB to punch    RMB repair wall")
            : EXPEDITION_TEXT("近身对准逃跑方，右键砸晕    左键蓄力打孔    右键补墙", "Aim at a nearby runner and RMB to catch    Hold / release LMB to punch    RMB repair wall"),H-43*S,.86f,Ink);
        return;
    }
    Label(bEnglish && !Game->EnglishTitle.IsEmpty() ? Game->EnglishTitle : Game->Title,23*S,18*S,1.25f,Ink);
    const FString& Objective = bEnglish && !Game->EnglishObjective.IsEmpty() ? Game->EnglishObjective : Game->Objective;
    constexpr float ObjectiveScale = .91f;
    float Width, Height;
    HUD.GetTextSize(Objective, Width, Height, Font, ObjectiveScale * S);
    const float AvailableWidth = FMath::Max(100.f, W - 215.f * S);
    float ObjectiveBottom = 57*S + Height;
    if (Width <= AvailableWidth) Label(Objective, 23*S, 57*S, ObjectiveScale, Ink);
    else
    {
        static FCanvasWordWrapper Wrapper;
        FTextSizingParameters Sizing(Font, ObjectiveScale * S, ObjectiveScale * S);
        Sizing.DrawXL = AvailableWidth;
        TArray<FWrappedStringElement> Lines;
        UCanvas::WrapString(Wrapper, Sizing, 0, Objective, Lines);
        float Baseline = 57*S;
        for (const FWrappedStringElement& Line : Lines)
        {
            Label(Line.Value, 23*S, Baseline, ObjectiveScale, Ink);
            Baseline += Height + 7*S;
        }
        ObjectiveBottom = Baseline-7*S;
    }
    const int32 Seconds=FMath::Max(0,FMath::CeilToInt(Game->SecondsLeft()));
    const auto Phase=Game->GetPhase();
    if (Phase==EPartyVoxelPhase::Countdown)
    {
        Center(FString::FromInt(FMath::Max(1,Seconds)),H*.40f,2.8f,Ink);
        Center(EXPEDITION_TEXT("争第一，替全队标出真箱位置", "Take first to mark a true chest for your crew"),H*.53f,.95f,Ink);
    }
    if (Phase==EPartyVoxelPhase::Playing)
    {
        Label(FString::Printf(TEXT("%d:%02d"),Seconds/60,Seconds%60),W-23*S,18*S,1.25f,
            Seconds<=10 ? FLinearColor(1.f,.70f,.36f) : Ink,true);
        const FVector Reticle = SurfaceAimPoint.IsSet() ? HUD.Project(*SurfaceAimPoint) : FVector(W*.5f,H*.5f,0.);
        const float X=Reticle.X, Y=Reticle.Y;
        PartyPunch::DrawChargeRing(HUD,X,Y,S,GetPunchChargeAlpha());
        HUD.DrawRect(Back,X-3*S,Y-3*S,6*S,6*S);
        HUD.DrawRect(bAimingAtVenue ? FLinearColor(1.f,.78f,.37f) : Ink,X-1.5f*S,Y-1.5f*S,3*S,3*S);
        if (bAimingAtVenue)
            Center(EXPEDITION_TEXT("左键  击打建筑", "LMB  Punch structure"),Y+19*S,.88f,Ink);
        const FPartyVoxelStanding* Leader=nullptr;
        for (const auto& Row : Game->GetStandings())
            if (!Leader || Row.Points>Leader->Points) Leader=&Row;
        Label(FString::Format(EXPEDITION_TEXT("我的积分  {0}", "My score  {0}"), {Game->Score(GetSeat())}),W-23*S,57*S,1.f,Ink,true);
        if (Leader && Leader->Seat!=GetSeat() && Leader->Points>Game->Score(GetSeat()))
            Label(FString::Printf(TEXT("%s  %d"),bAnonymous ? EXPEDITION_TEXT("领跑者","Leader") : *Leader->PlayerName,Leader->Points),
                W-23*S,FMath::Max(91*S,ObjectiveBottom+9*S),.83f,Ink,true);
    }
    if (Phase==EPartyVoxelPhase::Results)
    {
        const int32 Winner=Game->GetWinningSeat();
        const auto* First=Game->GetStandings().FindByPredicate([Winner](const auto& Row){ return Row.Seat==Winner; });
        const FString Result=First ? FString::Format(EXPEDITION_TEXT("第一名  {0}  ·  {1} 分", "1st  {0}  ·  {1} points"), {bAnonymous ? FString(EXPEDITION_TEXT("雾中水手","Unknown sailor")) : First->PlayerName,First->Points})
            : FString(EXPEDITION_TEXT("本局无人得分", "No one scored"));
        Center(Result,H*.38f,1.55f,Ink);
        Center(EXPEDITION_TEXT("回到海岛揭晓宝箱", "The chest will be revealed back on the island"),H*.48f,.93f,Ink);
    }
    else Center(EXPEDITION_TEXT("WASD 移动    V 切换视角    左键蓄力，松开出拳    右键扔泥，按住连扔    空格跳跃", "WASD move    V change view    Hold LMB to charge, release to punch    RMB throw mud, hold to repeat    Space jump"),H-43*S,.86f,Ink);
    HUD.DrawRect(Accent,13*S,15*S,3*S,31*S);
}
