#include "Island/IslandHUD.h"

#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "TimerManager.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/GameInstance.h"
#include "EngineUtils.h"
#include "Framework/Application/SlateApplication.h"
#include "Island/ExpeditionDock.h"
#include "Island/IslandPlayerController.h"
#include "Island/IslandRoomPanel.h"
#include "Island/IslandRoomSession.h"
#include "Island/IslandState.h"
#include "Island/IslandWorld.h"
#include "Tests/IslandUIProbe.h"
#include "PartyMinigame.h"
#include "Voxel/VoxelCharacter.h"
#include "Voxel/VoxelTerrain.h"
#include "Widgets/SWeakWidget.h"

namespace IslandHudColors
{
    const FLinearColor Panel(.018f, .030f, .038f, .93f);
    const FLinearColor Ink(.91f, .94f, .92f);
    const FLinearColor Sea(.48f, .60f, .60f);
    const FLinearColor Alert(.99f, .76f, .48f);
    const FLinearColor Amber(1.f, .68f, .20f);
    const FLinearColor Ally(.42f, .94f, .76f);
}

namespace
{
    TArray<int32> OccupiedTeams(const AIslandGameState& GS)
    {
        TArray<int32> Teams;
        for (const auto& Entry : GS.PlayerArray)
            if (const auto* P=Cast<AIslandPlayerState>(Entry.Get()); P && P->Team>=0) Teams.AddUnique(P->Team);
        Teams.Sort();
        return Teams;
    }
}

void AIslandHUD::BeginPlay()
{
    Super::BeginPlay();
    auto* Player = Cast<AIslandPlayerController>(GetOwningPlayerController());
    if (!Player || !Player->IsLocalController()) return;
    bFront = GetWorld()->GetMapName().EndsWith(TEXT("L_IslandFront"));
#if !UE_BUILD_SHIPPING
    bLegacyProbe=FParse::Param(FCommandLine::Get(),TEXT("IslandQA"));
#endif
    auto* Viewport = GetWorld()->GetGameViewport();
    if (!Viewport || !FSlateApplication::IsInitialized()) return;
    SAssignNew(Panel,SIslandRoomPanel).Controller(Player);
    ViewportLayer = SNew(SWeakWidget).PossiblyNullContent(Panel);
    Viewport->AddViewportWidgetContent(ViewportLayer.ToSharedRef(),30);
    if (bFront)
    {
        auto* Session=GetGameInstance()->GetSubsystem<UIslandRoomSession>();
        Panel->Show(Session && Session->bReturnToSearch ? EIslandPanel::Search : EIslandPanel::Home);
        if (Session) Session->bReturnToSearch=false;
        UpdateInput(*Player,true);
    }
    else if (!bLegacyProbe && GetWorld()->GetMapName().EndsWith(TEXT("L_ExpeditionIsland")))
    {
        Panel->Show(EIslandPanel::Arriving);
        UpdateInput(*Player,true);
    }
}

void AIslandHUD::EndPlay(const EEndPlayReason::Type Reason)
{
    if (ViewportLayer)
        if (auto* Viewport=GetWorld()->GetGameViewport()) Viewport->RemoveViewportWidgetContent(ViewportLayer.ToSharedRef());
    ViewportLayer.Reset();
    Panel.Reset();
    if (DockCamera.IsValid()) DockCamera->Destroy();
    if (ResultCamera.IsValid()) ResultCamera->Destroy();
    Super::EndPlay(Reason);
}

bool AIslandHUD::IsMenuOpen() const { return Panel && Panel->Current()!=EIslandPanel::Hidden; }

bool AIslandHUD::IsAcceptingWorldInput() const
{
    return !bFront && !IsMenuOpen() && FPlatformTime::Seconds()>=HandoffUntil;
}

void AIslandHUD::ToggleMenu()
{
    if (!Panel || bLegacyProbe) return;
    if (Panel->Current()==EIslandPanel::Hidden) Panel->Show(EIslandPanel::Pause);
    else Panel->Back();
    if (auto* Player=Cast<AIslandPlayerController>(GetOwningPlayerController())) UpdateInput(*Player,IsMenuOpen());
}

void AIslandHUD::UpdateInput(AIslandPlayerController& Player,bool bMenu)
{
    const bool bHandingOff=!bMenu && HandoffUntil>FPlatformTime::Seconds();
    if (bSetInput && bMenuInput==bMenu && bHandoffInput==bHandingOff) return;
    bHandoffInput=bHandingOff;
    bSetInput=true;
    bMenuInput=bMenu;
    Player.ClearHeldIslandActions();
    Player.SetIslandUIInput(bMenu);
    if (bHandingOff) Player.SetVoxelInputEnabled(false);
    Player.bShowMouseCursor=bMenu;
    if (bMenu)
    {
        FInputModeUIOnly Mode;
        Mode.SetWidgetToFocus(Panel->FocusTarget());
        Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
        Player.SetInputMode(Mode);
    }
    else
    {
        FInputModeGameOnly Mode;
        Mode.SetConsumeCaptureMouseDown(true);
        Player.SetInputMode(Mode);
        FSlateApplication::Get().SetAllUserFocusToGameViewport();
    }
}

void AIslandHUD::UpdateDockCamera(AIslandPlayerController& Player,bool bRoom)
{
    if (bFront || bLegacyProbe) return;
    auto* GS=GetWorld()->GetGameState<AIslandGameState>();
    if (!GS || !GS->bRoomLobby) return;
    if (bRoom)
    {
        if (!Player.GetHero() || Player.GetTeam()<0) return;
        const auto* Dock=AExpeditionDock::FindForTeam(GetWorld(),OccupiedTeams(*GS),Player.GetTeam());
        if (!Dock || (bRoomCamera && CameraDock==Dock->DockIndex && DockCamera.IsValid())) return;
        const FTransform Target=Dock->GetPreviewView();
        // Retarget a new camera so a seat change blends from the previous viewpoint.
        FActorSpawnParameters Params;
        Params.ObjectFlags|=RF_Transient;
        auto* Camera=GetWorld()->SpawnActor<ACameraActor>(ACameraActor::StaticClass(),Target,Params);
        if (!Camera) return;
        Camera->GetCameraComponent()->SetFieldOfView(65.f);
        Camera->GetCameraComponent()->PostProcessSettings.bOverride_MotionBlurAmount=true;
        Camera->GetCameraComponent()->PostProcessSettings.MotionBlurAmount=0.f;
        const bool bDistant=bRoomCamera && DockCamera.IsValid() && Player.PlayerCameraManager
            && FVector::DistSquared(DockCamera->GetActorLocation(),Target.GetLocation())>FMath::Square(2500.0);
        if (DockCamera.IsValid()) DockCamera->SetLifeSpan(.55f);
        DockCamera=Camera;
        CameraDock=Dock->DockIndex;
        bRoomCamera=true;
        if (bDistant)
        {
            // Keep the roster interactive while moving between distant shores.
            const FLinearColor HarbourGray(.15f,.20f,.21f);
            Player.PlayerCameraManager->StartCameraFade(0.f,1.f,.09f,HarbourGray,false,true);
            FTimerHandle Switch;
            const TWeakObjectPtr<ACameraActor> Destination=Camera;
            const TWeakObjectPtr<AIslandPlayerController> WeakPlayer=&Player;
            GetWorldTimerManager().SetTimer(Switch,FTimerDelegate::CreateWeakLambda(this,[this,Destination,WeakPlayer,HarbourGray]
            {
                if (bRoomCamera && Destination.IsValid() && DockCamera==Destination && WeakPlayer.IsValid() && WeakPlayer->PlayerCameraManager)
                {
                    WeakPlayer->SetViewTarget(Destination.Get());
                    WeakPlayer->PlayerCameraManager->StartCameraFade(1.f,0.f,.14f,HarbourGray,false,false);
                }
            }),.09f,false);
        }
        else
        {
            if (Player.PlayerCameraManager) Player.PlayerCameraManager->StopCameraFade();
            Player.SetViewTargetWithBlend(Camera,.32f,VTBlend_Cubic,0.f,true);
        }
    }
    else if (bRoomCamera && Player.GetPawn())
    {
        bRoomCamera=false;
        CameraDock=INDEX_NONE;
        HandoffUntil=FPlatformTime::Seconds()+.33;
        if (Player.PlayerCameraManager) Player.PlayerCameraManager->StopCameraFade();
        Player.SetViewTargetWithBlend(Player.GetPawn(),.33f,VTBlend_Cubic,0.f,true);
        if (DockCamera.IsValid()) DockCamera->SetLifeSpan(.48f);
        DockCamera.Reset();
    }
}

void AIslandHUD::UpdateResultCamera(AIslandPlayerController& Player)
{
    const auto* State=GetWorld()->GetGameState<AIslandGameState>();
    if (!State || State->Winner<0 || ResultCamera.IsValid()) return;
    AIslandChest* Treasure=nullptr;
    for (TActorIterator<AIslandChest> It(GetWorld());It;++It)
        if (It->IsPrize()) { Treasure=*It; break; }
    AVoxelCharacter* Winner=nullptr;
    double Nearest=DBL_MAX;
    if (Treasure) for (const auto& Entry:State->PlayerArray)
    {
        const auto* Member=Cast<AIslandPlayerState>(Entry.Get());
        auto* Hero=Member ? Cast<AVoxelCharacter>(Member->GetPawn()) : nullptr;
        if (!Hero || Member->Team!=State->Winner) continue;
        const double Distance=FVector::DistSquared(Treasure->GetActorLocation(),Hero->GetActorLocation());
        if (Distance<Nearest) { Nearest=Distance; Winner=Hero; }
    }
    TOptional<FTransform> View;
    if (Winner && Treasure)
    {
        const FVector Eye=Winner->GetAimOrigin(), Box=Treasure->GetActorLocation()+FVector(0,0,20);
        const FVector Focus=FMath::Lerp(Eye,Box,.49);
        FVector Rear=(Eye-Box).GetSafeNormal2D();
        if (Rear.IsNearlyZero()) Rear=-Winner->GetActorForwardVector().GetSafeNormal2D();
        FCollisionQueryParams Query(SCENE_QUERY_STAT(IslandResultCamera),false,Winner);
        Query.AddIgnoredActor(Treasure);
        // Choose the widest unobstructed shoulder view that can see both the opener and the chest.
        for (double Distance : {360.,250.,155.})
        {
            for (double Angle : {55.,-55.,100.,-100.,0.,165.})
            {
                const FVector Position=Eye+Rear.RotateAngleAxis(Angle,FVector::UpVector)*Distance+FVector(0,0,Distance*.43);
                if (GetWorld()->SweepTestByChannel(Eye,Position,FQuat::Identity,ECC_Visibility,FCollisionShape::MakeSphere(13),Query)
                    || GetWorld()->LineTraceTestByChannel(Position,Box,ECC_Visibility,Query)) continue;
                View=FTransform((Focus-Position).Rotation(),Position);
                break;
            }
            if (View.IsSet()) break;
        }
    }
    if (!View.IsSet())
    {
        // Let the final replication settle before falling back to the winning crew's public harbour.
        if (FPlatformTime::Seconds()-ResultAnnounced<.38) return;
        if (const auto* Dock=AExpeditionDock::FindForTeam(GetWorld(),State->LaunchedTeams,State->Winner)) View=Dock->GetPreviewView();
    }
    if (!View.IsSet()) return;
    FActorSpawnParameters Params;
    Params.ObjectFlags|=RF_Transient;
    auto* Camera=GetWorld()->SpawnActor<ACameraActor>(ACameraActor::StaticClass(),View.GetValue(),Params);
    if (!Camera) return;
    Camera->GetCameraComponent()->SetFieldOfView(62.f);
    Camera->GetCameraComponent()->PostProcessSettings.bOverride_MotionBlurAmount=true;
    Camera->GetCameraComponent()->PostProcessSettings.MotionBlurAmount=0.f;
    ResultCamera=Camera;
    if (Player.PlayerCameraManager && FVector::DistSquared(Player.PlayerCameraManager->GetCameraLocation(),Camera->GetActorLocation())>FMath::Square(1600.0))
    {
        const FLinearColor Gray(.15f,.20f,.21f);
        Player.PlayerCameraManager->StartCameraFade(0.f,1.f,.12f,Gray,false,true);
        const TWeakObjectPtr<AIslandPlayerController> WeakPlayer=&Player;
        FTimerHandle Switch;
        GetWorldTimerManager().SetTimer(Switch,FTimerDelegate::CreateWeakLambda(this,[this,WeakPlayer,Gray]
        {
            if (WeakPlayer.IsValid() && WeakPlayer->PlayerCameraManager && ResultCamera.IsValid())
            {
                WeakPlayer->SetViewTarget(ResultCamera.Get());
                WeakPlayer->PlayerCameraManager->StartCameraFade(1.f,0.f,.18f,Gray,false,false);
            }
        }),.12f,false);
    }
    else Player.SetViewTargetWithBlend(Camera,.42f,VTBlend_Cubic,0.f,true);
}

void AIslandHUD::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    auto* PC=Cast<AIslandPlayerController>(GetOwningPlayerController());
    if (!PC || !Panel || bLegacyProbe) return;
    if (!bFront)
    {
        if (auto* GS=GetWorld()->GetGameState<AIslandGameState>(); GS && GS->bRoomLobby)
        {
            const bool bRoom=GS->Phase==EIslandPhase::Waiting || GS->Phase==EIslandPhase::Starting;
            const bool bHaveSeat=PC->GetPlayerState<AIslandPlayerState>() && PC->GetTeam()>=0;
            if (bHaveSeat && LastPhase!=static_cast<uint8>(GS->Phase))
            {
                LastPhase=static_cast<uint8>(GS->Phase);
                if (bRoom && (Panel->Current()!=EIslandPanel::Lobby && Panel->Current()!=EIslandPanel::Settings))
                    Panel->Show(EIslandPanel::Lobby);
                else if (GS->Phase==EIslandPhase::Finished)
                {
                    ResultAnnounced=FPlatformTime::Seconds();
                    Panel->Show(EIslandPanel::Result);
                }
                else if (!bRoom) Panel->Show(EIslandPanel::Hidden);
            }
            UpdateDockCamera(*PC,bRoom);
            if (GS->Phase==EIslandPhase::Finished) UpdateResultCamera(*PC);
        }
    }
    UpdateInput(*PC, IsMenuOpen());
    const bool bFocus=FSlateApplication::Get().IsActive();
    if (!bFocus && bHadFocus) PC->ClearHeldIslandActions();
    bHadFocus=bFocus;
    Panel->Refresh();
#if !UE_BUILD_SHIPPING
    TickIslandUIProbe(*PC,Panel.ToSharedRef());
#endif
    ChartVisibility=FMath::FInterpConstantTo(ChartVisibility,PC->IsMapOpen()?1.f:0.f,DeltaSeconds,9.f);
}

FString AIslandHUD::ControlsText() const
{
    if (const auto* State = GetWorld()->GetGameState<AIslandGameState>(); State && State->bRoomLobby)
        return TEXT("左键 挖地    右键 丢土    空格 跳跃    E 交互    按住 Tab 看海图    按住 R 回码头");
    return TEXT("左键 凿岩    右键 挖脚下    Q 回填    Shift 攀爬    按住 Tab 看海图    按住 R 回码头");
}

void AIslandHUD::DrawHUD()
{
    AHUD::DrawHUD();
    auto* PC=Cast<AIslandPlayerController>(GetOwningPlayerController());
    const auto* GS=GetWorld()->GetGameState<AIslandGameState>();
    if (!Canvas || !PC || !GS || bFront || IsMenuOpen()) return;
    if (const auto* Pawn=Cast<APartyPawn>(PC->GetPawn()))
    {
        if (GS->Phase==EIslandPhase::PartyPlaying) Pawn->DrawPartyHUD(*this,FVector2D(Canvas->ClipX,Canvas->ClipY));
        return;
    }
    if (GS->Phase!=EIslandPhase::Hunting)
    {
        if (!GS->bRoomLobby && (GS->Phase==EIslandPhase::Waiting || GS->Phase==EIslandPhase::Finished))
            DrawText(GS->Phase==EIslandPhase::Waiting?FString::Printf(TEXT("等待队员   %u / 6"),GS->JoinedCount)
                :FString::Printf(TEXT("第 %d 队找到宝藏"),GS->Winner+1),IslandHudColors::Ink,20,20,GEngine->GetMediumFont(),1);
        return;
    }
    DrawHunt(*PC);
}

void AIslandHUD::DrawHunt(AIslandPlayerController& PC)
{
    const float W=Canvas->ClipX,H=Canvas->ClipY, S=FMath::Clamp(H/720.f,.70f,1.65f);
    auto Box=[&](const FString& Text,float X,float Y,FLinearColor Color=IslandHudColors::Ink, bool bCentered=false)
    {
        if (Text.IsEmpty()) return;
        float TW,TH; UFont* Font=GEngine->GetMediumFont();
        GetTextSize(Text,TW,TH,Font,S);
        X=bCentered?X-TW*.5f:X;
        DrawRect(IslandHudColors::Panel,X-8*S,Y-4*S,TW+16*S,TH+8*S);
        DrawText(Text,Color,X,Y,Font,S);
    };
    const int32 Team=PC.GetTeam();
    Box(FString::Printf(TEXT("第 %d 队    线索 %d / 4"),Team+1,PC.GetLocalClues().Count()),22*S,20*S);
    const auto* Round = GetWorld()->GetGameState<AIslandGameState>();
    const bool bExpedition = Round && Round->bRoomLobby;
    const bool bHasMate = bExpedition && Round->PlayerArray.ContainsByPredicate([&PC, Team](const auto& Entry)
    {
        const auto* Member = Cast<AIslandPlayerState>(Entry.Get());
        return Member && Member != PC.GetPlayerState<AIslandPlayerState>() && Member->Team == Team;
    });
    Box(PC.GetLocalClues().Complete() ? TEXT("挖开海图交点，打开宝藏") :
        TEXT("前往海图搜索圈，挖出线索箱"),22*S,52*S);
    if (ChartVisibility>.01f || (bLegacyProbe && PC.IsMapOpen()))
    {
        DrawChart(PC,bLegacyProbe?1.f:ChartVisibility);
        return;
    }
    const float X=W*.5f,Y=H*.5f;
    if (bExpedition)
    {
        DrawNearbyCrews(PC,S);
        const auto* Hero = PC.GetHero();
        if (Hero && Hero->IsMudStunned()) Box(TEXT("晕啦！醒来后按住左键挖"),X,Y+75*S,IslandHudColors::Amber,true);
        else if (Hero && Hero->IsMudCompact()) Box(TEXT("瞄准土壁或脚下，按住左键开路"),X,Y+75*S,IslandHudColors::Ally,true);
    }
    FVector Rally;
    bool bAtChest;
    if (PC.GetHero() && PC.GetCrewRally(Rally,bAtChest))
    {
        const FVector Offset = Rally - PC.GetHero()->GetActorLocation();
        const double Distance = Offset.Size2D();
        const double Bearing = FMath::FindDeltaAngleDegrees(PC.GetControlRotation().Yaw, Offset.Rotation().Yaw);
        const TCHAR* Side = TEXT("身边");
        if (Distance > 250.)
            Side = FMath::Abs(Bearing) < 40. ? TEXT("前方") : FMath::Abs(Bearing) > 140. ? TEXT("后方")
                : Bearing > 0. ? TEXT("右侧") : TEXT("左侧");
        Box(FString::Printf(TEXT("%s  ·  %s %d m"), bAtChest ? TEXT("集合撬锁") : TEXT("队伍集合"), Side,
            FMath::RoundToInt(Distance / 100.)), X, 20*S, IslandHudColors::Ally, true);
    }
    DrawRect(IslandHudColors::Panel,X-3*S,Y-3*S,6*S,6*S);
    DrawRect(IslandHudColors::Ink,X-S,Y-S,2*S,2*S);
    const auto* Chest = PC.GetReachableChest();
    if (bExpedition && Chest)
    {
        DrawLockPrompt(PC,*Chest,S);
    }
    else if (bExpedition && PC.GetEchoAge() < 1.05)
    {
        Box(PC.HasStrongEcho() ? TEXT("清晰的空响 · 箱子很近") : TEXT("微弱的空响"),
            X,Y+30*S,IslandHudColors::Amber,true);
    }
    else if (bExpedition)
    {
        Box(PC.GetNeutralHint(),X,Y+30*S,IslandHudColors::Ink,true);
    }
    else if (Chest)
    {
        FVector2D Screen;
        if (PC.ProjectWorldLocationToScreen(Chest->GetActorLocation()+FVector(0,0,50),Screen))
            Box(Chest->IsPrize() && !PC.GetLocalClues().Complete() ? TEXT("宝藏 · 需要四条线索") : TEXT("E  打开"),Screen.X,Screen.Y,IslandHudColors::Ink,true);
    }
    const FString Water=PC.WaterStatus();
    if (!Water.IsEmpty()) Box(Water,X,H*.30f,IslandHudColors::Alert,true);
    if (PC.IsNoticeVisible()) Box(PC.GetDisplayNotice(),X,H*.36f,IslandHudColors::Ink,true);
    const double Progress=PC.GetRecoverProgress();
    if (Progress>0)
    {
        Box(TEXT("松开 R 取消回码头"),X,H*.72f,IslandHudColors::Ink,true);
        DrawRect(IslandHudColors::Panel,X-70*S,H*.78f,140*S,4*S);
        DrawRect(IslandHudColors::Ink,X-70*S,H*.78f,140*S*Progress,4*S);
    }
    if (PC.ShowControlHelp())
    {
        if (bExpedition)
        {
            Box(TEXT("凿岩时空响越清，埋箱越近"),X,H-182*S,IslandHudColors::Amber,true);
            Box(TEXT("两人合撬能防夺锁   ·   单人撬锁时，对手可抢一次"),X,H-148*S,IslandHudColors::Ally,true);
        }
        Box(bExpedition ? TEXT("左键 连续挖    右键 连续丢    向脚边丢土可垫高") : TEXT("WASD 移动    空格 跳跃    左键 凿岩    右键 挖脚下"),X,H-115*S,IslandHudColors::Ink,true);
        Box(bExpedition ? TEXT("WASD 移动    空格 跳跃    E 开箱或拉帆    按住 R 回码头") : TEXT("Q 回填    按住 Shift 攀爬    E 开箱    按住 R 回码头"),X,H-81*S,IslandHudColors::Ink,true);
    }
    Box(bHasMate ? TEXT("G  呼叫队友    按住 Tab 看海图    按住 F1 看操作") :
        TEXT("按住 Tab 看海图    按住 F1 看操作"),X,H-40*S,IslandHudColors::Ink,true);
}

void AIslandHUD::DrawNearbyCrews(const AIslandPlayerController& PC, float S)
{
    const auto* GS = GetWorld()->GetGameState<AIslandGameState>();
    const APawn* Hero = PC.GetPawn();
    if (!Hero || !GS) return;
    FVector Eye;
    FRotator Rotation;
    PC.GetPlayerViewPoint(Eye, Rotation);
    const FCollisionQueryParams Query(SCENE_QUERY_STAT(ExpeditionCrewLabels), false, Hero);
    for (const APlayerState* State : GS->PlayerArray)
    {
        const auto* Crew = Cast<AIslandPlayerState>(State);
        const APawn* Other = Crew ? Crew->GetPawn() : nullptr;
        if (!Other || Other == Hero || Crew->Team < 0 || FVector::DistSquared(Other->GetActorLocation(), Hero->GetActorLocation()) > FMath::Square(1500.)) continue;
        const auto* Animal = Cast<AVoxelCharacter>(Other);
        const FVector Head = Other->GetActorLocation() + FVector(0,0, Animal && Animal->IsMudCompact() ? 8. : 65.);
        FVector2D Point;
        if (!PC.ProjectWorldLocationToScreen(Head + FVector(0,0,45), Point) ||
            GetWorld()->LineTraceTestByChannel(Eye, Head, ECC_Visibility, Query)) continue;
        const bool bAlly = Crew->Team == PC.GetTeam();
        const FString Label = bAlly ? TEXT("队友") : FString::Printf(TEXT("对手 · %d 队"), Crew->Team+1);
        float W,H;
        GetTextSize(Label,W,H,GEngine->GetMediumFont(),S*.83f);
        DrawRect(IslandHudColors::Panel,Point.X-W*.5f-5*S,Point.Y-H,W+10*S,H+3*S);
        DrawText(Label,bAlly ? IslandHudColors::Ally : IslandHudColors::Alert,Point.X-W*.5f,Point.Y-H,GEngine->GetMediumFont(),S*.83f);
    }
}

void AIslandHUD::DrawLockPrompt(const AIslandPlayerController& PC, const AIslandChest& Chest, float S)
{
    const auto* Member = PC.GetPlayerState<AIslandPlayerState>();
    const auto* GS = GetWorld()->GetGameState<AIslandGameState>();
    if (!Member || !GS) return;
    const auto& Lock = Member->LockView;
    const bool bActive = Lock.Chest.Get() == &Chest;
    const bool bOurs = Lock.Team == Member->Team;
    FLinearColor Color = IslandHudColors::Ink;
    FString Hint;
    const bool bEligible = bActive ? Lock.bEligible :
        (Chest.IsPrize() ? PC.GetLocalClues().Complete() : !PC.HasChestClue(Chest));
    if (!bEligible) Hint = Chest.IsPrize() ? TEXT("宝藏 · 需要四条线索") : TEXT("本队已取得这条线索");
    else if (!bActive) Hint = Chest.IsPrize() ? TEXT("E   撬开宝藏") : TEXT("E   撬开线索箱");
    else if (bOurs)
    {
        Color = IslandHudColors::Ally;
        if (Lock.bAssisted) Hint = TEXT("双人合撬 · 箱盖已稳住");
        else if (Lock.Role == EIslandLockRole::Observer) Hint = TEXT("E   帮队友合撬");
        else Hint = Lock.bSeized ? TEXT("已夺锁 · 留在箱边") : TEXT("撬锁中 · 留在箱边");
    }
    else
    {
        Color = IslandHudColors::Alert;
        if (Lock.bAssisted) Hint = TEXT("对手合撬 · 无法夺锁");
        else if (Lock.bSeized) Hint = TEXT("对手已夺锁 · 等待开箱");
        else Hint = TEXT("E   夺走对手的锁");
    }
    const float X = Canvas->ClipX*.5f, Y = Canvas->ClipY*.60f;
    float LabelW,H;
    GetTextSize(Hint,LabelW,H,GEngine->GetMediumFont(),S);
    const float W = FMath::Max(LabelW,140*S);
    DrawRect(IslandHudColors::Panel,X-W*.5f-10*S,Y-5*S,W+20*S,H+(bActive ? 17 : 10)*S);
    DrawText(Hint,Color,X-LabelW*.5f,Y,GEngine->GetMediumFont(),S);
    if (bActive)
    {
        const float Progress = Lock.ProgressAt(GS->GetServerWorldTimeSeconds());
        DrawRect(IslandHudColors::Sea,X-W*.5f,Y+H+3*S,W,4*S);
        DrawRect(Color,X-W*.5f,Y+H+3*S,W*Progress,4*S);
    }
}

void AIslandHUD::DrawChart(const AIslandPlayerController& PC,float Alpha)
{
    const auto* Terrain=AVoxelTerrain::Find(GetWorld());
    if (!Terrain) return;
    const float Size=FMath::Min(Canvas->ClipX*.76f,Canvas->ClipY*.78f);
    const FVector2D Center(Canvas->ClipX*.5f,Canvas->ClipY*.5f+6*(1-Alpha));
    const double Scale=Size/(Terrain->GetShoreExtent()*2.3);
    const auto Project=[&](FVector2D Point){return Center+FVector2D(Point.Y,-Point.X)*Scale;};
    const auto Tint=[Alpha](FLinearColor C){C.A*=Alpha; return C;};
    const auto Line=[&](FVector2D A,FVector2D B,FLinearColor C,float Thickness=1.f)
        { DrawLine(A.X,A.Y,B.X,B.Y,Tint(C),Thickness); };
    const auto Mark=[&](FVector2D P,const FString& Text,FLinearColor Color)
        {DrawText(Text,Tint(Color),P.X+5,P.Y-7,GEngine->GetMediumFont(),.85f);};
    DrawRect(Tint(IslandHudColors::Panel),Center.X-Size*.5f,Center.Y-Size*.5f,Size,Size);
    const auto& Coast=Terrain->GetCoastOutline();
    for (int32 I=0;I<Coast.Num();++I) Line(Project(Coast[I]),Project(Coast[(I+1)%Coast.Num()]),IslandHudColors::Sea,1.5f);
    const FIslandClues& Clues=PC.GetLocalClues();
    const auto& Circles=Terrain->GetSearchCircles();
    if (Clues.Complete())
    {
        Line(Project(Clues.Points[0]),Project(Clues.Points[2]),IslandHudColors::Ink);
        Line(Project(Clues.Points[1]),Project(Clues.Points[3]),IslandHudColors::Ink);
        const FVector2D Hit=Project(Clues.Crossing);
        Line(Hit-FVector2D(6,6),Hit+FVector2D(6,6),IslandHudColors::Alert,2);
        Line(Hit+FVector2D(-6,6),Hit+FVector2D(6,-6),IslandHudColors::Alert,2);
    }
    for (int32 I=0;I<Circles.Num();++I)
    {
        const bool Found=(Clues.Mask & (1<<I))!=0 && Clues.Points.IsValidIndex(I);
        const FVector2D P=Project(Found?Clues.Points[I]:FVector2D(Circles[I].Center));
        const FString Letter=FString::Printf(TEXT("%c"),'A'+I);
        if (Found) { DrawRect(Tint(IslandHudColors::Ink),P.X-2.5f,P.Y-2.5f,5,5); Mark(P,Letter,IslandHudColors::Ink); }
        else for (int32 J=0;J<40;++J)
        {
            const auto Ring=[&](double K){return P+Scale*Circles[I].Radius*FVector2D(FMath::Cos(K*UE_TWO_PI/40),FMath::Sin(K*UE_TWO_PI/40));};
            Line(Ring(J),Ring(J+1),IslandHudColors::Sea);
        }
    }
    for (TActorIterator<AExpeditionLandmark> It(GetWorld());It;++It)
        Mark(Project(FVector2D(It->GetActorLocation())),FString::Printf(TEXT("%c"),'A'+It->ClueIndex),IslandHudColors::Sea);
    FVector Rally;
    bool bAtChest;
    if (PC.GetCrewRally(Rally,bAtChest))
    {
        const FVector2D P = Project(FVector2D(Rally));
        const FVector2D Vert(0,7), Horiz(7,0);
        Line(P-Vert,P+Horiz,IslandHudColors::Ally,2);
        Line(P+Horiz,P+Vert,IslandHudColors::Ally,2);
        Line(P+Vert,P-Horiz,IslandHudColors::Ally,2);
        Line(P-Horiz,P-Vert,IslandHudColors::Ally,2);
        Mark(P+FVector2D(4,-14), bAtChest ? TEXT("集合撬锁") : TEXT("集合"), IslandHudColors::Ally);
    }
    if (const auto* GS=GetWorld()->GetGameState<AIslandGameState>())
    {
        const TArray<int32>& Teams=GS->LaunchedTeams;
        for (int32 Team:Teams)
            if (const auto* Dock=AExpeditionDock::FindForTeam(GetWorld(),Teams,Team))
            {
                const auto P=Project(FVector2D(Dock->GetActorLocation()));
                DrawText(FString::Printf(TEXT("码头 %d"),Team+1),Tint(Team==PC.GetTeam()?IslandHudColors::Ink:IslandHudColors::Sea),
                    P.X-48,P.Y-16,GEngine->GetMediumFont(),.95f);
                DrawRect(Tint(IslandHudColors::Sea),P.X-2,P.Y-2,4,4);
            }
        for (const auto& Entry:GS->PlayerArray)
            if (const auto* P=Cast<AIslandPlayerState>(Entry.Get()); P && P->Team==PC.GetTeam() && P->GetPawn())
            {
                const auto Pos=Project(FVector2D(P->GetPawn()->GetActorLocation()));
                const bool bSelf=P==PC.GetPlayerState<AIslandPlayerState>();
                const FLinearColor Color=bSelf?IslandHudColors::Alert:IslandHudColors::Ink;
                const double Heading=FMath::DegreesToRadians(P->GetPawn()->GetActorRotation().Yaw);
                const FVector2D Forward(FMath::Sin(Heading),-FMath::Cos(Heading)),Side(-Forward.Y,Forward.X);
                if (bSelf) Mark(Pos+FVector2D(5,5),TEXT("你"),IslandHudColors::Alert);
                Line(Pos+Forward*5,Pos-Forward*3+Side*3,Color,2);
                Line(Pos+Forward*5,Pos-Forward*3-Side*3,Color,2);
            }
    }
}
