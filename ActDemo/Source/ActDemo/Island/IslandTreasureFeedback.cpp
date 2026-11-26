#include "Island/IslandHUD.h"

#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "ExpeditionText.h"
#include "Island/IslandPlayerController.h"
#include "Island/IslandState.h"
#include "Voxel/VoxelCharacter.h"

namespace IslandTreasureUI
{
    constexpr float PickupSeconds = 1.25f;
    constexpr float StampSeconds = .85f;
    constexpr float ForgeSeconds = 2.f;
    constexpr float PlotSeconds = 2.15f;
    constexpr float DecoySeconds = 2.25f;
    const FLinearColor Paper(.018f,.032f,.040f,.96f);
    const FLinearColor Ink(.91f,.94f,.92f);
    const FLinearColor Inlay(.20f,.28f,.29f);
    const FLinearColor Gold(1.f,.67f,.18f);
    const FLinearColor Mint(.42f,.94f,.76f);

    // Bow, shaft and bit retain the same silhouettes in the inventory and in motion.
    void KeyPart(AHUD& HUD, int32 Part, FVector2D Origin, float S, FLinearColor Color)
    {
        auto Stroke = [&](FVector2D A, FVector2D B,float Width)
        {
            A=Origin+A*S;
            B=Origin+B*S;
            HUD.DrawLine(A.X,A.Y,B.X,B.Y,Color,Width*S);
        };
        if (Part==0)
        {
            const FVector2D Bow[]={{-19,0},{-16,-6},{-10,-8},{-4,-5},{-2,0},{-4,5},{-10,8},{-16,6}};
            for (int32 I=0; I<UE_ARRAY_COUNT(Bow); ++I) Stroke(Bow[I],Bow[(I+1)%UE_ARRAY_COUNT(Bow)],2.8f);
        }
        else if (Part==1) Stroke({-2,0},{10,0},3.0f);
        else
        {
            Stroke({10,0},{20,0},3.f);
            Stroke({14,0},{14,5},3.f);
            Stroke({20,0},{20,7},3.f);
        }
    }


}

bool AIslandHUD::GetTreasureFeedbackFrame(FName& OutStage,float& OutAge) const
{
    using namespace IslandTreasureUI;
    OutStage=NAME_None;
    OutAge=0.f;
    if (!bCanPresentRewards) return false;
    if (RewardSite!=INDEX_NONE)
    {
        OutAge=RewardAge;
        if (RewardAge<PickupSeconds) OutStage=TEXT("Fragment");
        else
        {
            OutAge-=PickupSeconds;
            const int32 Count=FPlatformMath::CountBits(uint32(PresentedSites));
            OutStage=Count==FIslandClues::FragmentsPerKey ? TEXT("Forge") : Count==IslandRules::ClueCount ? TEXT("Vault") : TEXT("Stamp");
        }
        return true;
    }
    if (RevealedDecoy!=INDEX_NONE)
    {
        OutStage=TEXT("Decoy");
        OutAge=DecoyAge;
        return true;
    }
    return false;
}

void AIslandHUD::UpdateTreasureFeedback(const AIslandPlayerController& Player,const AIslandGameState& Round,float DeltaSeconds)
{
    using namespace IslandTreasureUI;
    const FIslandClues& Clues=Player.GetLocalClues();
    if (Round.Phase==EIslandPhase::Waiting || Round.Phase==EIslandPhase::Starting ||
        TreasureTeam!=Player.GetTeam() || (SeenSites & ~Clues.Mask)!=0)
    {
        TreasureTeam=Player.GetTeam();
        SeenSites=PresentedSites=0;
        PendingSites.Reset();
        RewardSite=RevealedDecoy=INDEX_NONE;
        RewardAge=DecoyAge=0.f;
        SeenDecoys=0;
    }
    bCanPresentRewards=Round.Phase==EIslandPhase::Hunting && !IsMenuOpen() && TreasureTeam>=0 &&
        Player.GetPartyTransitionView().Phase==EIslandPartyVeil::Clear;
    if (!bCanPresentRewards) return;

    // Credit comes exclusively from the owner's reliable snapshot. A repeated snapshot
    // or a second team's success cannot enqueue a duplicate.
    for (int32 Site=0; Site<IslandRules::ClueCount; ++Site)
        if ((Clues.Mask & (1u<<Site))!=0 && (SeenSites & (1u<<Site))==0) PendingSites.Add(Site);
    SeenSites=Clues.Mask;
    if (RewardSite!=INDEX_NONE)
    {
        RewardAge+=DeltaSeconds;
        const int32 Count=FPlatformMath::CountBits(uint32(PresentedSites));
        const float Hold=Count==FIslandClues::FragmentsPerKey ? ForgeSeconds : Count==IslandRules::ClueCount ? PlotSeconds : StampSeconds;
        if (RewardAge>=PickupSeconds+Hold) RewardSite=INDEX_NONE;
    }
    if (RewardSite==INDEX_NONE && !PendingSites.IsEmpty())
    {
        RewardSite=PendingSites[0];
        PendingSites.RemoveAt(0);
        PresentedSites|=1u<<RewardSite;
        RewardAge=0.f;
        UE_LOG(LogTemp,Verbose,TEXT("TREASURE_HUD_AWARD team=%d site=%d owned=%d"),TreasureTeam,RewardSite,PresentedSites);
    }
    if (Round.SpentFalsePoints.Num()>SeenDecoys)
    {
        RevealedDecoy=Round.SpentFalsePoints.Num()-1;
        DecoyAge=0.f;
    }
    else if (RevealedDecoy!=INDEX_NONE && (DecoyAge+=DeltaSeconds)>=DecoySeconds) RevealedDecoy=INDEX_NONE;
    SeenDecoys=Round.SpentFalsePoints.Num();
}

void AIslandHUD::DrawTreasureLedger(const FIslandClues& Clues,int32 Team,float S)
{
    using namespace IslandTreasureUI;
    const FString Crew=Team<0 ? FString() : FString::Format(EXPEDITION_TEXT("第 {0} 队    ","Team {0}    "),{Team+1});
    const FString Inventory=Clues.HasKey() ? FString(EXPEDITION_TEXT("队伍钥匙","Crew key")) :
        FString::Format(EXPEDITION_TEXT("钥匙碎片  {0}/3","Key fragments  {0}/3"),{Clues.FragmentCount()});
    const FString Sites=FString::Format(EXPEDITION_TEXT("真箱点位 {0}/4","True sites {0}/4"),{Clues.Count()});
    const FString Action=Clues.Complete() ? FString(EXPEDITION_TEXT("按住 Tab 看藏宝交点","Hold Tab for the vault")) :
        Clues.HasKey() ? FString(EXPEDITION_TEXT("再找一处真箱定位宝藏","One more site locates the vault")) :
        FString(EXPEDITION_TEXT("按住 Tab 查看搜索区","Hold Tab for search areas"));
    const FString Top=Crew+Inventory+(Clues.HasKey() && Clues.FragmentCount()>0 ?
        FString::Format(EXPEDITION_TEXT("  ·  余片 {0}","  ·  Spare {0}"),{Clues.FragmentCount()}) : FString());
    const FString Bottom=Sites+TEXT("    ")+Action;
    float W1,H1,W2,H2;
    UFont* Font=GEngine->GetMediumFont();
    GetTextSize(Top,W1,H1,Font,S);
    GetTextSize(Bottom,W2,H2,Font,S*.85f);
    const float Fit=FMath::Min(1.f,(Canvas->ClipX-90*S)/FMath::Max(1.f,FMath::Max(W1,W2)));
    DrawRect(Paper,14*S,13*S,64*S+Fit*FMath::Max(W1,W2),52*S);
    DrawRect(Clues.HasKey()?Gold:Mint,14*S,13*S,3*S,52*S);
    const FVector2D Origin(43*S,39*S);
    for (int32 Part=0; Part<FIslandClues::FragmentsPerKey; ++Part)
        KeyPart(*this,Part,Origin,S*.99f,Part<Clues.Count()?Gold:Inlay);
    DrawText(Top,Ink,70*S,17*S,Font,S*Fit);
    DrawText(Bottom,Clues.Complete()?Gold:Ink,70*S,40*S,Font,S*.85f*Fit);
}

void AIslandHUD::DrawTreasureFeedback(const AIslandPlayerController& Player,float S,bool bFullChart)
{
    using namespace IslandTreasureUI;
    FName Stage;
    float Age=0.f;
    if (!GetTreasureFeedbackFrame(Stage,Age)) return;
    if (!bFullChart) DrawChart(Player,1.f,true);

    const float Width=FMath::Min(Canvas->ClipX*.48f,262.f*S);
    const float Height=48*S, Right=Canvas->ClipX-16*S, Left=Right-Width, Top=Canvas->ClipY-Height-16*S;
    DrawRect(Paper,Left,Top,Width,Height);
    const bool bDecoy=Stage==TEXT("Decoy");
    const FLinearColor Accent=bDecoy ? Ink : Gold;
    DrawRect(Accent,Left,Top,3*S,Height);
    const FVector2D Icon(Left+33*S,Top+Height*.5f);
    FString Message;
    if (bDecoy)
    {
        const float R=S*FMath::Lerp(11.f,6.f,FMath::SmoothStep(0.f,.45f,Age));
        DrawLine(Icon.X-R,Icon.Y-R,Icon.X+R,Icon.Y+R,Ink,2.5f*S);
        DrawLine(Icon.X-R,Icon.Y+R,Icon.X+R,Icon.Y-R,Ink,2.5f*S);
        Message=EXPEDITION_TEXT("假箱已标记","Decoy crossed off");
        if (const auto* Round = GetWorld()->GetGameState<AIslandGameState>(); bFullChart && Round && Round->PartyDecoyEvent != EIslandDecoyEvent::None)
            Message=FString::Format(EXPEDITION_TEXT("假箱 · {0}", "Decoy · {0}"), {DecoyEventName(Round->PartyDecoyEvent)});
    }
    else
    {
        const int32 Count=FPlatformMath::CountBits(uint32(PresentedSites));
        const int32 LatestPart=(Count-1)%FIslandClues::FragmentsPerKey;
        const FString Letter=FString::Chr('A'+RewardSite);
        if (Stage==TEXT("Fragment"))
        {
            const float Fly=1.f-FMath::SmoothStep(0.f,.7f,Age);
            for (int32 Part=0; Part<3; ++Part)
            {
                const FVector2D Offset=Part==LatestPart ? FVector2D(-11,-12)*Fly*S : FVector2D::ZeroVector;
                KeyPart(*this,Part,Icon+Offset,S,Part<=LatestPart?Gold:Inlay);
            }
            Message=FString::Format(EXPEDITION_TEXT("{0} 区 · 获得钥匙碎片","Site {0} · Fragment gained"),{Letter});
        }
        else if (Stage==TEXT("Forge"))
        {
            const float Gap=1.f-FMath::SmoothStep(0.f,.8f,Age);
            const float Gleam=1.f-FMath::Clamp(FMath::Abs(Age-.85f)/.26f,0.f,1.f);
            const FVector2D Split[]={{-8,-8},{0,9},{7,-5}};
            for (int32 Part=0; Part<3; ++Part) KeyPart(*this,Part,Icon+Split[Part]*Gap*S,S,FMath::Lerp(Gold,Ink,Gleam));
            Message=EXPEDITION_TEXT("钥匙已合成","Key assembled");
        }
        else if (Stage==TEXT("Vault"))
        {
            for (int32 Part=0; Part<3; ++Part) KeyPart(*this,Part,Icon,S,Gold);
            Message=EXPEDITION_TEXT("带钥匙去挖最终宝藏","Take your key to the vault");
        }
        else
        {
            for (int32 Part=0; Part<3; ++Part) KeyPart(*this,Part,Icon,S,Part<Count?Gold:Inlay);
            Message=FString::Format(EXPEDITION_TEXT("{0} 区真箱已标记","Site {0} marked"),{Letter});
        }
    }
    UFont* Font=GEngine->GetMediumFont();
    float W,H;
    GetTextSize(Message,W,H,Font,S);
    const float Fit=FMath::Min(1.f,(Width-68*S)/FMath::Max(1.f,W));
    DrawText(Message,Ink,Left+61*S,Top+(Height-H*Fit)*.5f,Font,S*Fit);
}

void AIslandHUD::DrawVaultBearing(const AIslandPlayerController& Player,float S)
{
    using namespace IslandTreasureUI;
    const AVoxelCharacter* Hero=Player.GetHero();
    if (!Hero) return;
    const FVector2D Delta=Player.GetLocalClues().Crossing-FVector2D(Hero->GetActorLocation());
    const double Bearing=FMath::FindDeltaAngleDegrees(Player.GetControlRotation().Yaw,FMath::RadiansToDegrees(FMath::Atan2(Delta.Y,Delta.X)));
    const double Meters=Delta.Length()/100.;
    const FString Arrow=Meters<2. ? FString() : FMath::Abs(Bearing)<=18. ? FString(TEXT("↑ ")) : Bearing>0 ? FString(TEXT("→ ")) : FString(TEXT("← "));
    const FString Label=Meters<2. ? FString(EXPEDITION_TEXT("在脚下挖宝","Dig here")) :
        FString::Format(EXPEDITION_TEXT("最终宝藏  {0} m","Vault  {0} m"),{FMath::RoundToInt(Meters)});
    const FString Cue=Arrow+Label;
    float W,H;
    GetTextSize(Cue,W,H,GEngine->GetMediumFont(),S);
    const float X=(Canvas->ClipX-W)*.5f,Y=(Canvas->ClipX<880*S ? 72.f : 13.f)*S;
    DrawRect(Paper,X-8*S,Y,W+16*S,H+6*S);
    DrawText(Cue,Gold,X,Y+3*S,GEngine->GetMediumFont(),S);
}
