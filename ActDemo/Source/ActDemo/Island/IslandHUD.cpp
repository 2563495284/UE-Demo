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

namespace
{
    const FLinearColor PanelColor(.018f,.030f,.038f,.93f), Ink(.91f,.94f,.92f), Sea(.48f,.60f,.60f), Alert(.99f,.76f,.48f);
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
            const TWeakObjectPtr<AIslandPlayerController> Owner=&Player;
            GetWorldTimerManager().SetTimer(Switch,FTimerDelegate::CreateWeakLambda(this,[this,Destination,Owner,HarbourGray]
            {
                if (bRoomCamera && Destination.IsValid() && DockCamera==Destination && Owner.IsValid() && Owner->PlayerCameraManager)
                {
                    Owner->SetViewTarget(Destination.Get());
                    Owner->PlayerCameraManager->StartCameraFade(1.f,0.f,.14f,HarbourGray,false,false);
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
        const TWeakObjectPtr<AIslandPlayerController> Owner=&Player;
        FTimerHandle Switch;
        GetWorldTimerManager().SetTimer(Switch,FTimerDelegate::CreateWeakLambda(this,[this,Owner,Gray]
        {
            if (Owner.IsValid() && Owner->PlayerCameraManager && ResultCamera.IsValid())
            {
                Owner->SetViewTarget(ResultCamera.Get());
                Owner->PlayerCameraManager->StartCameraFade(1.f,0.f,.18f,Gray,false,false);
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
                :FString::Printf(TEXT("第 %d 队找到宝藏"),GS->Winner+1),Ink,20,20,GEngine->GetMediumFont(),1);
        return;
    }
    DrawHunt(*PC);
}

void AIslandHUD::DrawHunt(AIslandPlayerController& PC)
{
    const float W=Canvas->ClipX,H=Canvas->ClipY, S=FMath::Clamp(H/720.f,.70f,1.65f);
    auto Box=[&](const FString& Text,float X,float Y,FLinearColor Color=Ink, bool bCentered=false)
    {
        if (Text.IsEmpty()) return;
        float TW,TH; UFont* Font=GEngine->GetMediumFont();
        GetTextSize(Text,TW,TH,Font,S);
        X=bCentered?X-TW*.5f:X;
        DrawRect(PanelColor,X-8*S,Y-4*S,TW+16*S,TH+8*S);
        DrawText(Text,Color,X,Y,Font,S);
    };
    const int32 Team=PC.GetTeam();
    Box(FString::Printf(TEXT("第 %d 队    线索 %d / 4"),Team+1,PC.GetLocalClues().Count()),22*S,20*S);
    Box(PC.GetLocalClues().Complete() ? TEXT("挖开海图交点，打开宝藏") : TEXT("前往海图搜索圈，挖出线索箱"),22*S,52*S);
    if (ChartVisibility>.01f || (bLegacyProbe && PC.IsMapOpen()))
    {
        DrawChart(PC,bLegacyProbe?1.f:ChartVisibility);
        return;
    }
    const auto* Chest=PC.GetReachableChest();
    const float X=W*.5f,Y=H*.5f;
    DrawRect(PanelColor,X-3*S,Y-3*S,6*S,6*S);
    DrawRect(Ink,X-S,Y-S,2*S,2*S);
    if (Chest)
    {
        FVector2D Screen;
        if (PC.ProjectWorldLocationToScreen(Chest->GetActorLocation()+FVector(0,0,50),Screen))
            Box(Chest->IsPrize() && !PC.GetLocalClues().Complete() ? TEXT("宝藏 · 需要四条线索") : TEXT("E  打开"),Screen.X,Screen.Y,Ink,true);
    }
    const FString Water=PC.WaterStatus();
    if (!Water.IsEmpty()) Box(Water,X,H*.30f,Alert,true);
    if (PC.IsNoticeVisible()) Box(PC.GetDisplayNotice(),X,H*.65f,Ink,true);
    const double Progress=PC.GetRecoverProgress();
    if (Progress>0)
    {
        Box(TEXT("松开 R 取消回码头"),X,H*.72f,Ink,true);
        DrawRect(PanelColor,X-70*S,H*.78f,140*S,4*S);
        DrawRect(Ink,X-70*S,H*.78f,140*S*Progress,4*S);
    }
    if (PC.ShowControlHelp())
    {
        Box(TEXT("WASD 移动    空格 跳跃    左键 凿岩    右键 挖脚下"),X,H-115*S,Ink,true);
        Box(TEXT("Q 回填    按住 Shift 攀爬    E 开箱    按住 R 回码头"),X,H-81*S,Ink,true);
    }
    Box(TEXT("按住 Tab 看海图    按住 F1 看操作"),X,H-40*S,Ink,true);
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
    DrawRect(Tint(PanelColor),Center.X-Size*.5f,Center.Y-Size*.5f,Size,Size);
    const auto& Coast=Terrain->GetCoastOutline();
    for (int32 I=0;I<Coast.Num();++I) Line(Project(Coast[I]),Project(Coast[(I+1)%Coast.Num()]),Sea,1.5f);
    const FIslandClues& Clues=PC.GetLocalClues();
    const auto& Circles=Terrain->GetSearchCircles();
    if (Clues.Complete())
    {
        Line(Project(Clues.Points[0]),Project(Clues.Points[2]),Ink);
        Line(Project(Clues.Points[1]),Project(Clues.Points[3]),Ink);
        const FVector2D Hit=Project(Clues.Crossing);
        Line(Hit-FVector2D(6,6),Hit+FVector2D(6,6),Alert,2);
        Line(Hit+FVector2D(-6,6),Hit+FVector2D(6,-6),Alert,2);
    }
    for (int32 I=0;I<Circles.Num();++I)
    {
        const bool Found=(Clues.Mask & (1<<I))!=0 && Clues.Points.IsValidIndex(I);
        const FVector2D P=Project(Found?Clues.Points[I]:FVector2D(Circles[I].Center));
        const FString Letter=FString::Printf(TEXT("%c"),'A'+I);
        if (Found) { DrawRect(Tint(Ink),P.X-2.5f,P.Y-2.5f,5,5); Mark(P,Letter,Ink); }
        else for (int32 J=0;J<40;++J)
        {
            const auto Ring=[&](double K){return P+Scale*Circles[I].Radius*FVector2D(FMath::Cos(K*UE_TWO_PI/40),FMath::Sin(K*UE_TWO_PI/40));};
            Line(Ring(J),Ring(J+1),Sea);
        }
    }
    for (TActorIterator<AExpeditionLandmark> It(GetWorld());It;++It)
        Mark(Project(FVector2D(It->GetActorLocation())),FString::Printf(TEXT("%c"),'A'+It->ClueIndex),Sea);
    if (const auto* GS=GetWorld()->GetGameState<AIslandGameState>())
    {
        const TArray<int32>& Teams=GS->LaunchedTeams;
        for (int32 Team:Teams)
            if (const auto* Dock=AExpeditionDock::FindForTeam(GetWorld(),Teams,Team))
            {
                const auto P=Project(FVector2D(Dock->GetActorLocation()));
                DrawText(FString::Printf(TEXT("码头 %d"),Team+1),Tint(Team==PC.GetTeam()?Ink:Sea),
                    P.X-48,P.Y-16,GEngine->GetMediumFont(),.95f);
                DrawRect(Tint(Sea),P.X-2,P.Y-2,4,4);
            }
        for (const auto& Entry:GS->PlayerArray)
            if (const auto* P=Cast<AIslandPlayerState>(Entry.Get()); P && P->Team==PC.GetTeam() && P->GetPawn())
            {
                const auto Pos=Project(FVector2D(P->GetPawn()->GetActorLocation()));
                const bool bSelf=P==PC.GetPlayerState<AIslandPlayerState>();
                const FLinearColor Color=bSelf?Alert:Ink;
                const double Heading=FMath::DegreesToRadians(P->GetPawn()->GetActorRotation().Yaw);
                const FVector2D Forward(FMath::Sin(Heading),-FMath::Cos(Heading)),Side(-Forward.Y,Forward.X);
                if (bSelf) Mark(Pos+FVector2D(5,5),TEXT("你"),Alert);
                Line(Pos+Forward*5,Pos-Forward*3+Side*3,Color,2);
                Line(Pos+Forward*5,Pos-Forward*3-Side*3,Color,2);
            }
    }
}
