#include "Island/IslandHUD.h"
#include "PartyPunchHUD.h"

#include "ExpeditionText.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "CanvasTypes.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInterface.h"
#include "TimerManager.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/GameInstance.h"
#include "EngineUtils.h"
#include "Framework/Application/SlateApplication.h"
#include "Island/ExpeditionDock.h"
#include "Island/IslandNeutralProp.h"
#include "Island/IslandPlayerController.h"
#include "Island/IslandRoomPanel.h"
#include "Island/IslandRoomSession.h"
#include "Island/IslandSceneryVoxels.h"
#include "Island/IslandState.h"
#include "Island/IslandWorld.h"
#include "Tests/IslandUIProbe.h"
#include "PartyVoxelArena.h"
#include "PartyCrewRig.h"
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
    constexpr float DockToPawnSeconds = .56f;

    bool HasDrawnWithMaterials(const UPrimitiveComponent* Mesh, float Since)
    {
        if (!Mesh || !Mesh->IsVisible() || Mesh->GetLastRenderTimeOnScreen() <= 0.f
            || Mesh->GetLastRenderTimeOnScreen() < Since) return false;
        for (int32 Slot = 0; Slot < Mesh->GetNumMaterials(); ++Slot)
            if (const auto* Material = Mesh->GetMaterial(Slot); Material && !Material->IsComplete()) return false;
        return true;
    }

    FString ExcavationEchoCaption(const AIslandPlayerController& Player)
    {
        const FVector Direction = Player.GetEchoDirection();
        if (Direction.IsNearlyZero()) return EXPEDITION_TEXT("空响 · 就在这里", "Echo · Right here");

        const float Proximity = Player.GetEchoProximity();
        FString Caption = Proximity < .33f ? EXPEDITION_TEXT("微弱回音", "Faint echo")
            : Proximity > .75f ? EXPEDITION_TEXT("清晰回音", "Clear echo") : EXPEDITION_TEXT("回音", "Echo");
        if (FMath::Abs(Direction.Z) < .88)
        {
            const double Bearing = FMath::FindDeltaAngleDegrees(Player.GetControlRotation().Yaw, Direction.Rotation().Yaw);
            const int32 Sector = (FMath::RoundToInt(Bearing / 45.) + 8) % 8;
            const TCHAR* const SidesZh[] = {TEXT("前方"), TEXT("右前方"), TEXT("右侧"), TEXT("右后方"),
                TEXT("后方"), TEXT("左后方"), TEXT("左侧"), TEXT("左前方")};
            const TCHAR* const SidesEn[] = {TEXT("Ahead"), TEXT("Front right"), TEXT("Right"), TEXT("Back right"),
                TEXT("Behind"), TEXT("Back left"), TEXT("Left"), TEXT("Front left")};
            Caption += TEXT(" · ");
            Caption += ExpeditionText::IsEnglish() ? SidesEn[Sector] : SidesZh[Sector];
        }
        if (Direction.Z < -.32) Caption += EXPEDITION_TEXT(" · 往下挖", " · Dig down");
        if (Direction.Z >  .32) Caption += EXPEDITION_TEXT(" · 往上挖", " · Dig up");
        return Caption;
    }

    const TCHAR* DecoyResult(EIslandDecoyEvent Event, bool bWon, bool bHasMate)
    {
        switch (Event)
        {
        case EIslandDecoyEvent::WideTools:
            return bWon ? EXPEDITION_TEXT("本队出拳和堆土的范围已扩大", "Your crew punches and builds over a wider area")
                : EXPEDITION_TEXT("胜队的出拳与堆土范围扩大", "Winning crews punch and build over a wider area");
        case EIslandDecoyEvent::Scatter:
            return bWon ? EXPEDITION_TEXT("败队队员已被冲散", "The tide scattered the losing crews")
                : EXPEDITION_TEXT("你被冲到了新的落点 · 按住 Tab 辨认方向", "You washed up elsewhere · Hold Tab to get your bearings");
        case EIslandDecoyEvent::Disguise:
            return EXPEDITION_TEXT("全员穿上雾衣，姓名与队伍标记暂时隐藏", "Fog suits hide everyone's identity and crew markers");
        case EIslandDecoyEvent::SpringTide:
            return EXPEDITION_TEXT("按空格跳得更高，可以试着越过石坎", "Press Space to jump higher and clear ledges");
        case EIslandDecoyEvent::Ceasefire:
            return EXPEDITION_TEXT("泥团仍可堆土，暂时不会粘在对手身上", "Mud still builds paths. Rivals shed incoming mud.");
        case EIslandDecoyEvent::CrewRally:
            return bHasMate
                ? EXPEDITION_TEXT("队友在原先的勘探点附近集合，看看谁在身边", "Look around for crewmates at your crew’s search site.")
                : EXPEDITION_TEXT("其他队伍在集合，你独自出航，按原位返岛", "Other crews regrouped. You sailed solo and returned as usual.");
        case EIslandDecoyEvent::SandRamp:
            return EXPEDITION_TEXT("假箱旁拱起了砂坡，可以踩上去，也可以挖开", "A sand ramp rose by the decoy. Walk up it or dig it away.");
        case EIslandDecoyEvent::SailGust:
            return EXPEDITION_TEXT("栈桥站的遮视帆受风翻转了", "Wind flipped the screens at the bridge stations.");
        default:
            return TEXT("");
        }
    }

    TArray<int32> OccupiedTeams(const AIslandGameState& GS)
    {
        TArray<int32> Teams;
        for (const auto& Entry : GS.PlayerArray)
            if (const auto* P=Cast<AIslandPlayerState>(Entry.Get()); P && P->Team>=0) Teams.AddUnique(P->Team);
        Teams.Sort();
        return Teams;
    }
}

const TCHAR* AIslandHUD::DecoyEventName(EIslandDecoyEvent Event)
{
    switch (Event)
    {
    case EIslandDecoyEvent::WideTools: return EXPEDITION_TEXT("开路好手","Wide tools");
    case EIslandDecoyEvent::Scatter: return EXPEDITION_TEXT("乱流冲散","Riptide");
    case EIslandDecoyEvent::Disguise: return EXPEDITION_TEXT("迷雾换装","Fog suits");
    case EIslandDecoyEvent::SpringTide: return EXPEDITION_TEXT("弹跳潮","High jump");
    case EIslandDecoyEvent::Ceasefire: return EXPEDITION_TEXT("停火潮","Mud truce");
    case EIslandDecoyEvent::CrewRally: return EXPEDITION_TEXT("船员集合","Crew rendezvous");
    case EIslandDecoyEvent::SandRamp: return EXPEDITION_TEXT("翻砂成坡","Sand ramp");
    case EIslandDecoyEvent::SailGust: return EXPEDITION_TEXT("风卷帆布","Sail gust");
    default: return TEXT("");
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
    if (auto* Player = Cast<AIslandPlayerController>(GetOwningPlayerController()))
        UpdateCrewPreview(*Player, false);
    if (DockCamera.IsValid()) DockCamera->Destroy();
    if (ResultCamera.IsValid()) ResultCamera->Destroy();
    if (auto* Player=GetOwningPlayerController())
        for (const auto& Occluder:ResultOccluders)
            if (Occluder.IsValid()) Player->HiddenActors.Remove(Occluder.Get());
    Super::EndPlay(Reason);
}

bool AIslandHUD::IsMenuOpen() const { return Panel && Panel->Current()!=EIslandPanel::Hidden; }

bool AIslandHUD::IsAcceptingWorldInput() const
{
    return !bFront && !IsMenuOpen() && !bTakingControl;
}

void AIslandHUD::ToggleMenu()
{
    if (!Panel || bLegacyProbe || bTakingControl) return;
    if (Panel->Current()==EIslandPanel::Hidden) Panel->Show(EIslandPanel::Pause);
    else Panel->Back();
    if (auto* Player=Cast<AIslandPlayerController>(GetOwningPlayerController())) UpdateInput(*Player,IsMenuOpen());
}

void AIslandHUD::UpdateInput(AIslandPlayerController& Player,bool bMenu)
{
    const bool bChangeFocus = !bSetInput || bMenuInput != bMenu;
    if (!bChangeFocus && bHandoffInput==bTakingControl) return;
    bHandoffInput=bTakingControl;
    bSetInput=true;
    bMenuInput=bMenu;
    Player.ClearHeldIslandActions();
    Player.SetIslandUIInput(bMenu || bTakingControl || Player.IsPartyTravelLocked() || Cast<APartyPawn>(Player.GetPawn()));
    // The viewport already captured the mouse at the start of the blend. Requesting
    // precision capture again on unlock can deliver a cursor warp as the first aim delta.
    if (!bChangeFocus) return;
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
    if (UpdateCrewPreview(Player, bRoom && GS->Phase == EIslandPhase::Waiting && Panel->Current() == EIslandPanel::Character)) return;
    if (bRoom)
    {
        if (!Player.GetHero() || Player.GetTeam()<0) return;
        auto* Dock=AExpeditionDock::FindForTeam(GetWorld(),OccupiedTeams(*GS),Player.GetTeam());
        if (!Dock || (bRoomCamera && CameraDock==Dock->DockIndex && DockCamera.IsValid())) return;
        const FTransform Target=Dock->GetPreviewView();
        // Retarget a new camera so a seat change blends from the previous viewpoint.
        FActorSpawnParameters Params;
        Params.ObjectFlags|=RF_Transient;
        auto* Camera=GetWorld()->SpawnActor<ACameraActor>(ACameraActor::StaticClass(),Target,Params);
        if (!Camera) return;
        Camera->GetCameraComponent()->SetConstraintAspectRatio(false);
        Camera->GetCameraComponent()->SetFieldOfView(65.f);
        Camera->GetCameraComponent()->PostProcessSettings.bOverride_MotionBlurAmount=true;
        Camera->GetCameraComponent()->PostProcessSettings.MotionBlurAmount=0.f;
        const bool bFirstDock=!bRoomCamera;
        const bool bDistant=bRoomCamera && DockCamera.IsValid() && Player.PlayerCameraManager
            && FVector::DistSquared(DockCamera->GetActorLocation(),Target.GetLocation())>FMath::Square(2500.0);
        if (DockCamera.IsValid()) DockCamera->SetLifeSpan(.55f);
        DockCamera=Camera;
        CameraDock=Dock->DockIndex;
        ArrivalDock=Dock;
        DockViewWorldTime=GetWorld()->GetTimeSeconds();
        bRoomCamera=true;
        if (bFirstDock)
        {
            // The travel screen owns the viewport until this exact view has drawn.
            Player.SetViewTarget(Camera);
        }
        else if (bDistant)
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
        // The owning-client RPC and the replicated phase can arrive in either order.
        if (GS->Phase == EIslandPhase::Hunting && (!Player.IsPlaying() || !Player.GetHero())) return;
        bRoomCamera=false;
        CameraDock=INDEX_NONE;
        if (Player.PlayerCameraManager) Player.PlayerCameraManager->StopCameraFade();
        bTakingControl = GS->Phase == EIslandPhase::Hunting;
        if (bTakingControl)
        {
            HandoffProgress = 0.f;
            Panel->SetVisibility(EVisibility::HitTestInvisible);
            Player.SetViewTargetWithBlend(Player.GetPawn(),DockToPawnSeconds,VTBlend_Cubic,0.f,true);
            UE_LOG(LogTemp, Display, TEXT("ISLAND_PRESENTATION handoff_begin"));
        }
        else Player.SetViewTarget(Player.GetPawn());
        if (DockCamera.IsValid()) DockCamera->SetLifeSpan(DockToPawnSeconds + .2f);
        DockCamera.Reset();
    }
}

void AIslandHUD::UpdateHuntHandoff(AIslandPlayerController& Player)
{
    if (!bTakingControl) return;
    const auto* Camera = Player.PlayerCameraManager.Get();
    const auto* Round = GetWorld()->GetGameState<AIslandGameState>();
    if (Round && Round->Phase == EIslandPhase::Hunting && Camera && Camera->PendingViewTarget.Target == Player.GetPawn() && Player.GetPawn())
    {
        HandoffProgress = FMath::Clamp(1.f - Camera->BlendTimeToGo / DockToPawnSeconds, 0.f, 1.f);
        Panel->SetRenderOpacity(1.f - FMath::SmoothStep(0.f, .65f, HandoffProgress));
        return;
    }

    bTakingControl = false;
    if (Round && Round->Phase == EIslandPhase::Hunting) Panel->Show(EIslandPanel::Hidden);
    Panel->SetRenderOpacity(1.f);
    UE_LOG(LogTemp, Display, TEXT("ISLAND_PRESENTATION handoff_complete"));
}

void AIslandHUD::PresentRoomView(const AIslandPlayerController& Player)
{
    if (bRoomPresented) return;
    const auto* State = GetWorld()->GetGameState<AIslandGameState>();
    if (LobbyShownAt < 0.)
    {
        // A client that catches up after countdown may receive Hunting in its first snapshot.
        if (!State || State->Phase == EIslandPhase::Waiting || State->Phase == EIslandPhase::Starting
            || !Player.GetPawn() || Player.GetTeam() < 0) return;
        bRoomPresented = true;
        GetGameInstance()->GetSubsystem<UIslandRoomSession>()->RevealTravelScreen();
        return;
    }
    const auto* Camera = Player.PlayerCameraManager.Get();
    const bool bDockView = Camera && DockCamera.IsValid() && Player.GetViewTarget() == DockCamera.Get()
        && Camera->GetCameraLocation().Equals(DockCamera->GetActorLocation(), 2.);
    const auto* Hero = Player.GetHero();
    const auto* Crew = Hero ? Hero->FindComponentByClass<UPartyCrewRig>() : nullptr;
    const auto* Deck = ArrivalDock.IsValid() ? IslandSceneryVoxels::SurfaceFor(&ArrivalDock->GetMesh()) : nullptr;
    const bool bSceneDrawn = bDockView && HasDrawnWithMaterials(Deck, DockViewWorldTime)
        && HasDrawnWithMaterials(Crew, DockViewWorldTime);
    // A malformed custom map must still expose the leave-room action.
    if (!bSceneDrawn && FPlatformTime::Seconds() - LobbyShownAt < 3.) return;
    bRoomPresented = true;
    GetGameInstance()->GetSubsystem<UIslandRoomSession>()->RevealTravelScreen();
    UE_LOG(LogTemp, Display, TEXT("ISLAND_PRESENTATION dock_ready dock=%d framed=%d scene_drawn=%d"), CameraDock, bDockView, bSceneDrawn);
}

void AIslandHUD::UpdateResultCamera(AIslandPlayerController& Player)
{
    const auto* State=GetWorld()->GetGameState<AIslandGameState>();
    if (!State || State->Winner<0 || ResultCamera.IsValid()) return;
    AIslandChest* Treasure=nullptr;
    for (TActorIterator<AIslandChest> It(GetWorld());It;++It)
        if (It->IsPrize()) { Treasure=*It; break; }
    // The result and the exposed prize can replicate in either order.
    if (!Treasure) return;
    const FVector Box=Treasure->GetActorLocation();
    const FVector Lid=Box+FVector(0,0,24), Compass=Box+FVector(0,0,98);
    const FVector Focus=FMath::Lerp(Lid,Compass,.48);
    FCollisionQueryParams Query(SCENE_QUERY_STAT(IslandResultCamera),false,Treasure);
    AVoxelCharacter* Winner=nullptr;
    TArray<AVoxelCharacter*> Characters;
    for (const auto& Entry:State->PlayerArray)
    {
        const auto* Member=Cast<AIslandPlayerState>(Entry.Get());
        auto* Hero=Member ? Cast<AVoxelCharacter>(Member->GetPawn()) : nullptr;
        if (!Hero) continue;
        Characters.Add(Hero);
        Query.AddIgnoredActor(Hero);
        if (Member->Team==State->Winner && (!Winner || FVector::DistSquared(Box,Hero->GetActorLocation()) <
            FVector::DistSquared(Box,Winner->GetActorLocation()))) Winner=Hero;
    }
    UWorld* World=GetWorld();
    TOptional<FTransform> View;
    double BestScore=-DBL_MAX;
    const auto BlocksPrize=[&](const AVoxelCharacter& Hero,const FVector& Camera)
    {
        const FVector Head=Hero.GetAimOrigin()-FVector(0,0,23);
        return FMath::PointDistToSegment(Head,Camera,Compass)<56. || FMath::PointDistToSegment(Head,Camera,Lid)<56.;
    };
    const auto Consider=[&](const FVector& Position)
    {
        if (World->OverlapBlockingTestByChannel(Position,FQuat::Identity,ECC_Visibility,FCollisionShape::MakeSphere(12),Query)) return;
        const auto ClearSight=[&](const FVector& Subject)
        {
            return !World->LineTraceTestByChannel(Subject,Position,ECC_Visibility,Query) &&
                !World->LineTraceTestByChannel(Position,Subject,ECC_Visibility,Query);
        };
        const bool bLid=ClearSight(Lid), bCompass=ClearSight(Compass);
        if (!bLid && !bCompass) return;
        const FVector Aim=bLid && bCompass ? Focus : bCompass ? Compass : Lid;
        const double Distance=FVector::Dist(Position,Aim);
        if (Distance<80.) return; // Keep the near plane outside the chest and its rising compass.
        const FVector TowardCamera=(Position-Aim)/Distance;
        const double CompassFace=FMath::Abs(TowardCamera | FVector(.883,.469,0));
        double Score=(bCompass ? 1000. : 0.)+(bLid ? 800. : 0.)+CompassFace*60.-FMath::Abs(Distance-245.)*.28;
        for (const auto* Hero:Characters)
            if (BlocksPrize(*Hero,Position)) Score-=180.;
        if (Score>BestScore)
        {
            BestScore=Score;
            View=FTransform((Aim-Position).Rotation(),Position);
        }
    };
    // Sample the excavated pocket itself. A free camera need not travel through
    // an intact shoulder wall, but both ends of the reward need a clear sightline.
    for (double Radius : {235.,155.,94.})
        for (double Pitch : {18.,45.,72.})
            for (int32 Side=0;Side<8;++Side)
                Consider(Focus+FRotator(Pitch,28.+45.*Side,0).Vector()*Radius);
    if (Winner)
    {
        const FVector Eye=Winner->GetAimOrigin();
        Consider(Eye);
        Consider(FMath::Lerp(Eye,Lid,.22));
    }
    if (!View.IsSet()) return;
#if !UE_BUILD_SHIPPING
    UE_LOG(LogTemp,Display,TEXT("ISLAND_RESULT_CAMERA box=%s view=%s score=%.1f"),*Box.ToCompactString(),*View->GetLocation().ToCompactString(),BestScore);
#endif
    FActorSpawnParameters Params;
    Params.ObjectFlags|=RF_Transient;
    auto* Camera=GetWorld()->SpawnActor<ACameraActor>(ACameraActor::StaticClass(),View.GetValue(),Params);
    if (!Camera) return;
    for (auto* Hero:Characters)
        if (BlocksPrize(*Hero,View->GetLocation()) && !Player.HiddenActors.Contains(Hero))
        {
            // Only this local camera hides a pawn standing over the reward.
            Player.HiddenActors.Add(Hero);
            ResultOccluders.Add(Hero);
        }
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

void AIslandHUD::ShowLocalPartyBriefing(AIslandPlayerController& Player)
{
    if (!Panel || !Player.GetLocalPartyBriefingRound()) return;
    if (Panel->Current() == EIslandPanel::PartyBrief)
    {
        Panel->Refresh();
        return;
    }
    // The opaque briefing replaces any remaining dock handoff, including its input capture.
    bTakingControl = false;
    Panel->SetRenderOpacity(1.f);
    Panel->Show(EIslandPanel::PartyBrief);
    if (auto* Room = GetGameInstance()->GetSubsystem<UIslandRoomSession>()) Room->RevealTravelScreen(true);
    UpdateInput(Player, true);
}

void AIslandHUD::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    auto* PC=Cast<AIslandPlayerController>(GetOwningPlayerController());
    if (!PC || !Panel) return;
    const auto* Round=GetWorld()->GetGameState<AIslandGameState>();
    if (PC->GetLocalPartyBriefingRound())
    {
        ShowLocalPartyBriefing(*PC);
#if !UE_BUILD_SHIPPING
        TickIslandUIProbe(*PC, Panel.ToSharedRef());
#endif
        return;
    }
    if (Round && Round->bRoomLobby) UpdateTreasureFeedback(*PC,*Round,DeltaSeconds);
    if (Panel->Current() == EIslandPanel::PartyBrief)
    {
        Panel->Show(EIslandPanel::Hidden);
        UpdateInput(*PC, false);
    }
    if (bLegacyProbe && (!Round || Round->Phase!=EIslandPhase::Finished ||
        !FParse::Param(FCommandLine::Get(),TEXT("ExpeditionInteractionQA")))) return;
    if (!bFront)
    {
        if (auto* GS=GetWorld()->GetGameState<AIslandGameState>(); GS && GS->bRoomLobby)
        {
            const bool bRoom=GS->Phase==EIslandPhase::Waiting || GS->Phase==EIslandPhase::Starting;
            const bool bHaveSeat=PC->GetPlayerState<AIslandPlayerState>() && PC->GetTeam()>=0;
            if (bHaveSeat && LastPhase!=static_cast<uint8>(GS->Phase))
            {
                LastPhase=static_cast<uint8>(GS->Phase);
                if (bRoom && !(GS->Phase==EIslandPhase::Waiting && Panel->Current()==EIslandPanel::Character)
                    && Panel->Current()!=EIslandPanel::Lobby && Panel->Current()!=EIslandPanel::Settings
                    && Panel->Current()!=EIslandPanel::ClientSettings)
                {
                    Panel->Show(EIslandPanel::Lobby);
                    if (LobbyShownAt < 0.) LobbyShownAt = FPlatformTime::Seconds();
                }
                else if (GS->Phase==EIslandPhase::Finished)
                {
                    Panel->Show(EIslandPanel::Result);
                }
                else if (!bRoom && !(GS->Phase == EIslandPhase::Hunting && bRoomCamera))
                    Panel->Show(EIslandPanel::Hidden);
            }
            UpdateDockCamera(*PC,bRoom);
            UpdateHuntHandoff(*PC);
            if (GS->Phase==EIslandPhase::Finished) UpdateResultCamera(*PC);
        }
    }
    UpdateInput(*PC, IsMenuOpen() && !bTakingControl);
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
        return EXPEDITION_TEXT("左键 挖地或开箱    右键 丢土    空格 跳跃    V 切换视角    按住 Tab 看海图    按住 R 回码头", "LMB dig / open chest   RMB throw   Space jump   V view   Hold Tab for chart   Hold R for dock");
    return EXPEDITION_TEXT("左键 凿岩    右键 挖脚下    Q 回填    Shift 攀爬    按住 Tab 看海图    按住 R 回码头", "LMB break rock   RMB dig down   Q fill   Shift climb   Hold Tab for chart   Hold R for dock");
}

void AIslandHUD::DrawHUD()
{
#if !UE_BUILD_SHIPPING
    DrawnCrewCounts = FIntPoint::ZeroValue;
#endif
    AHUD::DrawHUD();
    auto* PC=Cast<AIslandPlayerController>(GetOwningPlayerController());
    const auto* GS=GetWorld()->GetGameState<AIslandGameState>();
    if (!Canvas || !PC || !GS || bFront) return;
    if (PC->GetLocalPartyBriefingRound()) return;
    if (GS->bRoomLobby && !bLegacyProbe) PresentRoomView(*PC);
    const bool bAnonymous = GS->IsIdentityDisguised();
    if (GS->Phase == EIslandPhase::PartyPlaying)
    {
        for (const auto& Entry : GS->PlayerArray)
        {
            const auto* Crew = Cast<AIslandPlayerState>(Entry.Get());
            if (auto* Pawn = Crew ? Cast<APartyPawn>(Crew->GetPawn()) : nullptr)
                Pawn->SetCrewTint(bAnonymous ? FLinearColor(FColor(143,161,155)) : IslandRules::TeamColor(Crew->Team));
        }
    }
    if (GS->bRoomLobby && GS->Phase==EIslandPhase::Finished && PC->GetTeam()>=0)
    {
        // The result menu occupies the left side. Keep the vault camera visible in the middle.
        const float S=FMath::Clamp(Canvas->ClipY/720.f,.70f,1.65f);
        if (Canvas->ClipX-FMath::Min(Canvas->ClipX*.48f,262.f*S)-16*S>=500.f) DrawChart(*PC,1.f,true);
        return;
    }
    if (IsMenuOpen() && !bTakingControl) return;
    if (const auto* Pawn=Cast<APartyPawn>(PC->GetPawn()))
    {
        if (GS->Phase==EIslandPhase::PartyPlaying)
        {
            const auto* VoxelPawn = Cast<APartyVoxelPawn>(Pawn);
            const auto* Arena = VoxelPawn ? VoxelPawn->GetSession() : nullptr;
            if (!bAnonymous && (!Arena || !Arena->IsChaseGame()))
                DrawNearbyCrews(*PC,FMath::Clamp(Canvas->ClipY/720.f,.70f,1.65f));
            Pawn->DrawPartyHUD(*this,FVector2D(Canvas->ClipX,Canvas->ClipY),bAnonymous);
        }
        return;
    }
    if (GS->Phase!=EIslandPhase::Hunting)
    {
        if (!GS->bRoomLobby && (GS->Phase==EIslandPhase::Waiting || GS->Phase==EIslandPhase::Finished))
            DrawText(GS->Phase==EIslandPhase::Waiting?FString::Format(EXPEDITION_TEXT("等待队员   {0} / 6", "Waiting for players   {0} / 6"), {GS->JoinedCount})
                :FString::Format(EXPEDITION_TEXT("第 {0} 队找到宝藏", "Team {0} found the treasure"), {GS->Winner+1}),IslandHudColors::Ink,20,20,GEngine->GetMediumFont(),1);
        return;
    }
    if (bTakingControl && Canvas->Canvas)
    {
        TGuardValue<float> HudOpacity(Canvas->Canvas->AlphaModulate,
            Canvas->Canvas->AlphaModulate * FMath::SmoothStep(.42f, 1.f, HandoffProgress));
        DrawHunt(*PC);
    }
    else DrawHunt(*PC);
}

void AIslandHUD::DrawHunt(AIslandPlayerController& PC)
{
    const float W=Canvas->ClipX,H=Canvas->ClipY, S=FMath::Clamp(H/720.f,.70f,1.65f);
    auto Box=[&](const FString& Text,float X,float Y,FLinearColor Color=IslandHudColors::Ink, bool bCentered=false)
    {
        if (Text.IsEmpty()) return;
        float TW,TH; UFont* Font=GEngine->GetMediumFont();
        GetTextSize(Text,TW,TH,Font,S);
        const float Margin = 12*S;
        const float Fit = FMath::Min(1.f, FMath::Max(1.f,W-2*Margin) / FMath::Max(1.f,TW));
        TW *= Fit;
        TH *= Fit;
        X=FMath::Clamp(bCentered ? X-TW*.5f : X, Margin, FMath::Max(Margin,W-Margin-TW));
        Y=FMath::Clamp(Y, 4*S, FMath::Max(4*S,H-4*S-TH));
        DrawRect(IslandHudColors::Panel,X-8*S,Y-4*S,TW+16*S,TH+8*S);
        DrawText(Text,Color,X,Y,Font,S*Fit);
    };
    const int32 Team=PC.GetTeam();
    const auto* Round = GetWorld()->GetGameState<AIslandGameState>();
    const bool bExpedition = Round && Round->bRoomLobby;
    const bool bAnonymous = bExpedition && Round->IsIdentityDisguised();
    const auto& Clues = PC.GetLocalClues();
    const bool bHasMate = bExpedition && !bAnonymous && Round->PlayerArray.ContainsByPredicate([&PC, Team](const auto& Entry)
    {
        const auto* Member = Cast<AIslandPlayerState>(Entry.Get());
        return Member && Member != PC.GetPlayerState<AIslandPlayerState>() && Member->Team == Team;
    });
    if (bExpedition)
    {
        DrawTreasureLedger(Clues,bAnonymous ? INDEX_NONE : Team,S);
    }
    else
    {
        Box(FString::Format(EXPEDITION_TEXT("第 {0} 队    线索 {1} / 4", "Team {0}    Clues {1} / 4"),{Team+1,Clues.Count()}),22*S,20*S);
        Box(Clues.Complete() ? EXPEDITION_TEXT("挖开海图交点，打开宝藏", "Dig at the chart intersection to uncover the treasure") :
            EXPEDITION_TEXT("前往海图搜索圈，挖出线索箱", "Dig in the circles on your chart to find clue chests"),22*S,52*S);
    }
    if (bExpedition && Round->ActiveDecoyEvent != EIslandDecoyEvent::None)
    {
        const bool bWideTools = Round->ActiveDecoyEvent == EIslandDecoyEvent::WideTools;
        if (!bWideTools || Round->HasDecoyToolBoost(Team))
        {
            const TCHAR* Effect = bWideTools ? EXPEDITION_TEXT("拳击范围 ×2–3 · 土台范围 ×2", "Punch radius ×2–3 · Fill radius ×2")
                : DecoyEventName(Round->ActiveDecoyEvent);
            const FString Status = FString::Format(EXPEDITION_TEXT("{0} · 至下一场小游戏开赛","{0} · Until the next minigame starts"), {Effect});
            Box(Status,22*S,84*S,IslandHudColors::Amber);
        }
    }
    if (ChartVisibility>.01f || (bLegacyProbe && PC.IsMapOpen()))
    {
        DrawChart(PC,bLegacyProbe?1.f:ChartVisibility);
        if (bExpedition) DrawTreasureFeedback(PC,S,true);
        return;
    }
    if (bExpedition)
    {
        DrawTreasureFeedback(PC,S,false);
        if (Clues.CanOpenPrize() && !(RewardSite!=INDEX_NONE && PresentedSites==15 && RewardAge<2.03f)) DrawVaultBearing(PC,S);
    }
    const float X=W*.5f,Y=H*.5f;
    if (bExpedition && Round->PartyWinnerUntil>Round->GetServerWorldTimeSeconds()
        && Round->PartyOutcome!=EIslandChestOutcome::Unknown && Clues.Points.IsValidIndex(Round->PartySite))
    {
        const FString Site=FString::Chr('A'+Round->PartySite);
        const bool bTrue=Round->PartyOutcome==EIslandChestOutcome::Genuine;
        FString Reveal=FString::Format(bTrue ? EXPEDITION_TEXT("{0} 区发现真箱","True chest found in {0}")
            : EXPEDITION_TEXT("{0} 区发现假箱","Decoy found in {0}"),{Site});
        if (!bTrue && Round->PartyDecoyEvent != EIslandDecoyEvent::None)
            Reveal += FString::Printf(TEXT(" · %s"), DecoyEventName(Round->PartyDecoyEvent));
        if (!bAnonymous)
        {
            Reveal+=FString::Format(EXPEDITION_TEXT("  ·  开箱  第 {0} 队","  ·  Opened by team {0}"),{Round->PartyOpeningTeam+1});
            if (Round->PartyWinnerTeam>=0)
                Reveal+=FString::Format(EXPEDITION_TEXT("  ·  优胜  {0}（{1} 队）","  ·  Winner  {0} (team {1})"),{Round->PartyWinnerName,Round->PartyWinnerTeam+1});
            else if (Round->PartyWinnerMask)
            {
                TArray<FString> Teams;
                for (int32 Index = 0; Index < Round->RoomSettings.MaxTeams; ++Index)
                    if (Round->IsPartyWinner(Index)) Teams.Add(FString::FromInt(Index + 1));
                Reveal+=FString::Format(EXPEDITION_TEXT("  ·  获胜队伍  {0}","  ·  Winning crews  {0}"),
                    {FString::Join(Teams, EXPEDITION_TEXT("、", ", "))});
            }
        }
        Box(Reveal,X,H*.20f,bTrue ? IslandHudColors::Amber : IslandHudColors::Alert,true);
        FString Result;
        if (bTrue && Team>=0 && ((1<<Team)&Round->PartyRewardMask))
            Result.Reset(); // The local fragment and chart animation conveys the award.
        else if (bTrue && (Clues.Mask & (1u << Round->PartySite)))
            Result=EXPEDITION_TEXT("本队已记录这个真箱点位并领取过碎片","Your crew already charted this chest and claimed its fragment.");
        else if (bTrue)
            Result=EXPEDITION_TEXT("靠近真箱，瞄准箱盖点左键领取钥匙碎片","Aim at this chest and click LMB to claim your crew’s key fragment.");
        else
            Result=DecoyResult(Round->PartyDecoyEvent, Round->IsPartyWinner(Team), bHasMate);
        Box(Result,X,H*.20f+31*S,IslandHudColors::Ink,true);
    }
    if (bExpedition)
    {
        if (!bAnonymous) DrawNearbyCrews(PC,S);
        const auto* Hero = PC.GetHero();
        if (Hero && Hero->IsMudAnchored()) Box(EXPEDITION_TEXT("左键出拳脱困", "Punch with LMB to break free"),X,Y+75*S,IslandHudColors::Amber,true);
    }
    FVector Rally;
    bool bAtChest;
    if (!bAnonymous && PC.GetHero() && PC.GetCrewRally(Rally,bAtChest))
    {
        const FVector Offset = Rally - PC.GetHero()->GetActorLocation();
        const double Distance = Offset.Size2D();
        const double Bearing = FMath::FindDeltaAngleDegrees(PC.GetControlRotation().Yaw, Offset.Rotation().Yaw);
        const TCHAR* Side = EXPEDITION_TEXT("身边", "nearby");
        if (Distance > 250.)
            Side = FMath::Abs(Bearing) < 40. ? EXPEDITION_TEXT("前方", "ahead") : FMath::Abs(Bearing) > 140. ? EXPEDITION_TEXT("后方", "behind")
                : Bearing > 0. ? EXPEDITION_TEXT("右侧", "right") : EXPEDITION_TEXT("左侧", "left");
        Box(FString::Printf(TEXT("%s  ·  %s %d m"), bAtChest ? EXPEDITION_TEXT("集合撬锁", "Rally at chest") : EXPEDITION_TEXT("队伍集合", "Team rally"), Side,
            FMath::RoundToInt(Distance / 100.)), W, W<640*S ? 84*S : 20*S, IslandHudColors::Ally);
    }
    DrawRect(IslandHudColors::Panel,X-3*S,Y-3*S,6*S,6*S);
    DrawRect(IslandHudColors::Ink,X-S,Y-S,2*S,2*S);
    if (bExpedition && PC.IsHoldingExpeditionDig()) PartyPunch::DrawChargeRing(*this,X,Y,S,PC.GetPunchChargeAlpha());
    const AIslandChest* Chest = PC.GetAimedChest();
    const auto* Nearby = PC.GetReachableChest();
    if (Nearby && Nearby->HasDiscoveryTarget() && PC.GetHero())
    {
        const FVector Point = Nearby->GetDiscoveryPoint();
        FVector2D Mark;
        const FVector Eye = PC.GetHero()->GetAimOrigin();
        const FCollisionQueryParams Query(SCENE_QUERY_STAT(IslandDiscoveryMarker),false,PC.GetHero());
        FHitResult Hit;
        if (FVector::DistSquared(Point,Eye) <= FMath::Square(IslandRules::OpenReach)
            && (!GetWorld()->LineTraceSingleByChannel(Hit,Eye,Point,ECC_Visibility,Query) || Hit.GetActor() == Nearby)
            && PC.ProjectWorldLocationToScreen(Point,Mark))
        {
            const float R = 5*S;
            for (int32 I=0; I<4; ++I)
            {
                const FVector2D A = Mark + FVector2D(FMath::Cos(I*UE_HALF_PI),FMath::Sin(I*UE_HALF_PI))*R;
                const FVector2D B = Mark + FVector2D(FMath::Cos((I+1)*UE_HALF_PI),FMath::Sin((I+1)*UE_HALF_PI))*R;
                DrawLine(A.X,A.Y,B.X,B.Y,IslandHudColors::Amber,1.5*S);
            }
        }
    }
    const auto* LocalMember = PC.GetPlayerState<AIslandPlayerState>();
    if (bExpedition && !Chest && LocalMember && LocalMember->LockView.Role != EIslandLockRole::Observer && PC.GetHero())
    {
        const auto* WorkingChest = LocalMember->LockView.Chest.Get();
        if (WorkingChest && WorkingChest->IsReachableFrom(*PC.GetHero())) Chest = WorkingChest;
    }
    if (bExpedition && Chest)
    {
        DrawLockPrompt(PC,*Chest,S);
    }
    else if (bExpedition && PC.GetEchoAge() < 1.05)
    {
        Box(ExcavationEchoCaption(PC), X, Y+30*S, FMath::Lerp(IslandHudColors::Ink, IslandHudColors::Amber, PC.GetEchoProximity()), true);
    }
    else if (bExpedition)
    {
        Box(PC.GetNeutralHint(),X,Y+30*S,IslandHudColors::Ink,true);
    }
    else if (Chest)
    {
        FVector2D Screen;
        if (PC.ProjectWorldLocationToScreen(Chest->GetActorLocation()+FVector(0,0,50),Screen))
            Box(Chest->IsPrize() && !PC.GetLocalClues().Complete() ? EXPEDITION_TEXT("宝藏 · 需要四条线索", "Treasure · Four clues needed") : EXPEDITION_TEXT("左键  打开", "LMB  Open"),Screen.X,Screen.Y,IslandHudColors::Ink,true);
    }
    const FString Water=PC.WaterStatus();
    if (!Water.IsEmpty()) Box(Water,X,H*.30f,IslandHudColors::Alert,true);
    if (PC.IsNoticeVisible()) Box(PC.GetDisplayNotice(),X,H*.36f,IslandHudColors::Ink,true);
    const double Progress=PC.GetRecoverProgress();
    if (Progress>0)
    {
        Box(EXPEDITION_TEXT("松开 R 取消回码头", "Release R to cancel return"),X,H*.72f,IslandHudColors::Ink,true);
        DrawRect(IslandHudColors::Panel,X-70*S,H*.78f,140*S,4*S);
        DrawRect(IslandHudColors::Ink,X-70*S,H*.78f,140*S*Progress,4*S);
    }
    if (PC.ShowControlHelp())
    {
        if (bExpedition)
        {
            Box(EXPEDITION_TEXT("朝回音方向挖，越响越近。露出箱盖就能点开", "Dig toward the echo. Louder means closer. Click the exposed lid."),X,H-182*S,IslandHudColors::Amber,true);
            Box(EXPEDITION_TEXT("大奖箱可双人合撬；独自撬锁时，对手可抢一次", "Two teammates secure the treasure lock. A rival can steal a solo lock once."),X,H-148*S,IslandHudColors::Ally,true);
        }
        Box(bExpedition ? EXPEDITION_TEXT("左键 点按出拳，长按蓄力后松开    右键 连续丢    向脚边丢土可垫高", "Tap LMB to punch · Hold to charge, release to strike    Hold RMB to throw    Throw at your feet to climb") : EXPEDITION_TEXT("WASD 移动    空格 跳跃    左键 凿岩    右键 挖脚下", "WASD move    Space jump    LMB break rock    RMB dig down"),X,H-115*S,IslandHudColors::Ink,true);
        Box(bExpedition ? EXPEDITION_TEXT("WASD 移动    空格 跳跃    瞄准箱盖点左键开箱    按住 R 回码头", "WASD move    Space jump    Aim at chest + click LMB to open    Hold R for dock") : EXPEDITION_TEXT("Q 回填    按住 Shift 攀爬    瞄准箱盖点左键开箱    按住 R 回码头", "Q fill    Hold Shift to climb    Aim at chest + click LMB to open    Hold R for dock"),X,H-81*S,IslandHudColors::Ink,true);
    }
    FString Shortcuts = bExpedition ? EXPEDITION_TEXT("V 切换视角    ", "V view    ") : TEXT("");
    if (bHasMate) Shortcuts += EXPEDITION_TEXT("G 呼叫队友    ", "G rally    ");
    Shortcuts += EXPEDITION_TEXT("按住 Tab 看海图    按住 F1 看操作", "Hold Tab for chart    Hold F1 for controls");
    Box(Shortcuts,X,H-40*S,IslandHudColors::Ink,true);
}

void AIslandHUD::DrawNearbyCrews(const AIslandPlayerController& PC, float S)
{
    const auto* GS = GetWorld()->GetGameState<AIslandGameState>();
    const APawn* Hero = PC.GetPawn();
    if (!Hero || !GS || GS->IsIdentityDisguised()) return;
    struct FVisibleCrew
    {
        const AIslandPlayerState* Member;
        FVector2D Head;
        double Distance;
    };
    TArray<FVisibleCrew,TInlineAllocator<8>> Visible;
    FVector Eye;
    FRotator Rotation;
    PC.GetPlayerViewPoint(Eye,Rotation);
    for (const auto& Entry : GS->PlayerArray)
    {
        const auto* Crew = Cast<AIslandPlayerState>(Entry.Get());
        const APawn* Other = Crew ? Crew->GetPawn() : nullptr;
        if (!Other || Other == Hero || Crew->Team < 0 || Other->IsHidden()) continue;
        const double Distance = FVector::DistSquared(Other->GetActorLocation(),Hero->GetActorLocation());
        if (Distance > FMath::Square(2500.)) continue;
        const auto* Animal = Cast<AVoxelCharacter>(Other);
        const FVector Head = Other->GetActorLocation() + FVector(0,0,Animal && Animal->IsMudCompact() ? 8. : 65.);
        FVector2D Point;
        if (!PC.ProjectWorldLocationToScreen(Head+FVector(0,0,38),Point)
            || Point.X < 0 || Point.X > Canvas->ClipX || Point.Y < 0 || Point.Y > Canvas->ClipY) continue;
        FCollisionQueryParams Query(SCENE_QUERY_STAT(ExpeditionCrewLabels),false,Hero);
        Query.AddIgnoredActor(Other);
        if (GetWorld()->LineTraceTestByChannel(Eye,Head,ECC_Visibility,Query)
            || AIslandNeutralProp::AnySailObscures(*GetWorld(),Eye,Head)) continue;
        Visible.Add({Crew,Point,Distance});
    }
    // Pack nearby head labels first; a short leader keeps displaced labels attached to the right sailor.
    Visible.Sort([](const FVisibleCrew& A,const FVisibleCrew& B){ return A.Distance < B.Distance; });
    TArray<FBox2D,TInlineAllocator<8>> Plates;
    for (const FVisibleCrew& Sailor : Visible)
    {
        const auto* Crew = Sailor.Member;
        const bool bAlly = Crew->Team == PC.GetTeam();
        FString Name = Crew->GetPlayerName().Replace(TEXT("\n"),TEXT(" ")).Replace(TEXT("\r"),TEXT(" ")).TrimStartAndEnd();
        const bool bDuplicate = GS->PlayerArray.ContainsByPredicate([&](const auto& Entry)
            { return Entry && Entry != Crew && Entry->GetPlayerName() == Crew->GetPlayerName(); });
        const bool bTruncated = Name.Len()>21;
        if (bTruncated) Name=Name.Left(20)+TEXT("…");
        if (Name.IsEmpty()) Name=EXPEDITION_TEXT("水手","Sailor");
        if (bDuplicate || bTruncated) Name+=FString::Printf(TEXT(" #%d"),Crew->GetPlayerId());
        const FString Role = bAlly ? EXPEDITION_TEXT("队友","Ally")
            : FString::Format(EXPEDITION_TEXT("敌队 {0}","Rival {0}"),{Crew->Team+1});
        const FString Label = Name+TEXT("  ·  ")+Role;
        const FLinearColor Accent = bAlly ? IslandHudColors::Ally : IslandHudColors::Alert;
        float W,H;
        GetTextSize(Label,W,H,GEngine->GetMediumFont(),.85f*S);
        const FVector2D Size(W+24*S,H+6*S);
        const float Left=FMath::Clamp(float(Sailor.Head.X-Size.X*.5),3*S,FMath::Max(3*S,float(Canvas->ClipX-Size.X-3*S)));
        const float Top=FMath::Clamp(float(Sailor.Head.Y-Size.Y),2*S,FMath::Max(2*S,float(Canvas->ClipY-Size.Y-2*S)));
        FBox2D Plate(FVector2D(Left,Top),FVector2D(Left,Top)+Size);
        while (Plates.ContainsByPredicate([&](const FBox2D& Other){ return Plate.Intersect(Other); }))
            Plate=Plate.ShiftBy(FVector2D(0,-Size.Y-2*S));
        if (Plate.Min.Y<0) continue;
        Plates.Add(Plate);
        if (Top-Plate.Min.Y>1.f)
            DrawLine(Sailor.Head.X,Sailor.Head.Y,Sailor.Head.X,Plate.Max.Y,Accent,1.f*S);
        DrawRect(IslandHudColors::Panel,Plate.Min.X,Plate.Min.Y,Size.X,Size.Y);
        const FVector2D Icon(Left+10*S,Plate.GetCenter().Y);
        const int32 Edges=bAlly ? 12 : 4;
        for (int32 I=0;I<Edges;++I)
        {
            const double Angle=I*UE_TWO_PI/Edges, Next=(I+1)*UE_TWO_PI/Edges;
            const FVector2D A=Icon+FVector2D(FMath::Sin(Angle),-FMath::Cos(Angle))*5.*S;
            const FVector2D B=Icon+FVector2D(FMath::Sin(Next),-FMath::Cos(Next))*5.*S;
            DrawLine(A.X,A.Y,B.X,B.Y,Accent,1.8f*S);
        }
        DrawText(Label,Accent,Left+20*S,Plate.Min.Y+3*S,GEngine->GetMediumFont(),.85f*S);
#if !UE_BUILD_SHIPPING
        ++(bAlly ? DrawnCrewCounts.X : DrawnCrewCounts.Y);
#endif
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
        (Chest.IsPrize() ? PC.GetLocalClues().CanOpenPrize() : !PC.HasChestClue(Chest));
    if (!bEligible) Hint = Chest.IsPrize() ? EXPEDITION_TEXT("大奖已锁 · 需要四个真箱点位和钥匙", "Treasure locked · Chart four true chests and bring the key") : EXPEDITION_TEXT("本队已取得这只箱子的碎片", "Your crew has this fragment");
    else if (!bActive)
    {
        if (Chest.IsPrize()) Hint = EXPEDITION_TEXT("左键   撬开宝藏", "LMB   Unlock treasure");
        else if (Chest.IsChallengeResolved()) Hint = EXPEDITION_TEXT("左键   领取钥匙碎片", "LMB   Claim key fragment");
        else Hint = EXPEDITION_TEXT("左键   开箱 · 全房进入小游戏", "LMB   Open chest · Everyone enters the minigame");
    }
    else if (GS->IsIdentityDisguised())
        Hint = Lock.Role == EIslandLockRole::Observer ? EXPEDITION_TEXT("左键   介入撬锁","LMB   Work the lock")
            : EXPEDITION_TEXT("撬锁中 · 留在箱边","Unlocking · Stay by the chest");
    else if (bOurs)
    {
        Color = IslandHudColors::Ally;
        if (Lock.bAssisted) Hint = EXPEDITION_TEXT("双人合撬 · 箱盖已稳住", "Two teammates have secured the lock");
        else if (Lock.Role == EIslandLockRole::Observer) Hint = EXPEDITION_TEXT("左键   帮队友合撬", "LMB   Help secure the lock");
        else Hint = Lock.bSeized ? EXPEDITION_TEXT("已夺锁 · 留在箱边", "Lock stolen · Stay by the chest") : EXPEDITION_TEXT("撬锁中 · 留在箱边", "Unlocking · Stay by the chest");
    }
    else
    {
        Color = IslandHudColors::Alert;
        if (Lock.bAssisted) Hint = EXPEDITION_TEXT("对手合撬 · 无法夺锁", "Two rivals have secured the lock");
        else if (Lock.bSeized) Hint = EXPEDITION_TEXT("对手已夺锁 · 等待开箱", "Rival took the lock · Wait for the chest to open");
        else Hint = EXPEDITION_TEXT("左键   夺走对手的锁", "LMB   Steal the lock");
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

void AIslandHUD::DrawChart(const AIslandPlayerController& PC,float Alpha,bool bInset)
{
    const auto* Terrain=AVoxelTerrain::Find(GetWorld());
    if (!Terrain) return;
    const float S=FMath::Clamp(Canvas->ClipY/720.f,.70f,1.65f);
    const float Size=bInset ? FMath::Min3(210.f*S,Canvas->ClipX*.32f,Canvas->ClipY*.34f) : FMath::Min(Canvas->ClipX*.76f,Canvas->ClipY*.78f);
    const float InsetWidth=FMath::Min(Canvas->ClipX*.48f,262.f*S);
    const FVector2D Center=bInset ? FVector2D(Canvas->ClipX-16*S-InsetWidth*.5f, Canvas->ClipY-64*S-Size*.5f) :
        FVector2D(Canvas->ClipX*.5f,Canvas->ClipY*.5f+6*(1-Alpha));
    const double Scale=Size/(Terrain->GetShoreExtent()*2.3);
    const auto Project=[&](FVector2D Point){return Center+FVector2D(Point.Y,-Point.X)*Scale;};
    const auto Tint=[Alpha](FLinearColor C){C.A*=Alpha; return C;};
    const auto Line=[&](FVector2D A,FVector2D B,FLinearColor C,float Thickness=1.f)
        { DrawLine(A.X,A.Y,B.X,B.Y,Tint(C),Thickness); };
    const auto Mark=[&](FVector2D P,const FString& Text,FLinearColor Color,bool bDock=false)
    {
        const float TextScale = bInset ? .69f*S : (bDock ? .95f : .85f);
        float Width, Height;
        GetTextSize(Text,Width,Height,GEngine->GetMediumFont(),TextScale);
        const float Left = Center.X-Size*.5f+3, Top = Center.Y-Size*.5f+3;
        const float X = FMath::Clamp(float(P.X)+(bDock ? -Width-5 : 5),Left,FMath::Max(Left,Left+Size-6-Width));
        const float Y = FMath::Clamp(float(P.Y)-(bDock ? Height : 7),Top,FMath::Max(Top,Top+Size-6-Height));
        DrawText(Text,Tint(Color),X,Y,GEngine->GetMediumFont(),TextScale);
    };
    const float PaperWidth=bInset ? InsetWidth : Size;
    DrawRect(Tint(IslandHudColors::Panel),Center.X-PaperWidth*.5f,Center.Y-Size*.5f,PaperWidth,Size);
    const auto& Coast=Terrain->GetCoastOutline();
    for (int32 I=0;I<Coast.Num();++I) Line(Project(Coast[I]),Project(Coast[(I+1)%Coast.Num()]),IslandHudColors::Sea,1.5f);
    const FIslandClues& Clues=PC.GetLocalClues();
    const auto& Circles=Terrain->GetSearchCircles();
    const bool bExpedition = Terrain->Layout == EVoxelLayout::Expedition;
    const auto* Round=GetWorld()->GetGameState<AIslandGameState>();
    const bool bAnonymous=Round && Round->IsIdentityDisguised();
    const bool bFinished=Round && Round->Phase==EIslandPhase::Finished;
    if (Clues.Complete() && Clues.Points.Num()>=IslandRules::ClueCount)
    {
        const bool bPlotting=bExpedition && !bFinished && RewardSite!=INDEX_NONE && PresentedSites==15;
        const float Trace=bPlotting ? FMath::SmoothStep(1.25f,2.03f,RewardAge) : 1.f;
        const FVector2D Hit=Project(Clues.Crossing);
        for (int32 I=0;I<4 && Trace>0.f;++I)
        {
            const FVector2D A=Project(Clues.Points[I]);
            Line(A,FMath::Lerp(A,Hit,double(Trace)),IslandHudColors::Ink,bInset ? 1.f : 1.6f);
        }
        if (Trace>.95f)
        {
            const float R=bInset ? 4*S : 6*S;
            Line(Hit-FVector2D(R,R),Hit+FVector2D(R,R),IslandHudColors::Amber,2*S);
            Line(Hit+FVector2D(-R,R),Hit+FVector2D(R,-R),IslandHudColors::Amber,2*S);
            if (bExpedition)
            {
                const bool bClaimed=bFinished && Round->Winner>=0;
                Mark(Hit+FVector2D(R,-R),bClaimed ? EXPEDITION_TEXT("已取宝","Claimed") : EXPEDITION_TEXT("最终宝藏","Vault"),IslandHudColors::Amber);
                if (bClaimed)
                    for (int32 J=0;J<12;++J)
                    {
                        const auto Rim=[&](int32 K){return Hit+R*1.75*FVector2D(FMath::Cos(K*UE_TWO_PI/12),FMath::Sin(K*UE_TWO_PI/12));};
                        Line(Rim(J),Rim(J+1),IslandHudColors::Amber,1.4f*S);
                    }
            }
        }
    }
    for (int32 I=0;I<Circles.Num();++I)
    {
        const bool Found=(Clues.Mask & (1<<I))!=0 && Clues.Points.IsValidIndex(I);
        const FVector2D P=Project(Found?Clues.Points[I]:FVector2D(Circles[I].Center));
        const FString Letter=FString::Printf(TEXT("%c"),'A'+I);
        if (Found)
        {
            if (I==RewardSite && !bFinished && RewardAge<1.f)
            {
                const double T=FMath::SmoothStep(0.f,.9f,RewardAge);
                const FVector2D Origin=FMath::Lerp(Project(FVector2D(Circles[I].Center)),P,T);
                const double Radius=FMath::Lerp(Scale*Circles[I].Radius,5.*S,T);
                for (int32 J=0; J<36; ++J)
                {
                    const auto Arc=[&](int32 K){return Origin+Radius*FVector2D(FMath::Cos(K*UE_TWO_PI/36),FMath::Sin(K*UE_TWO_PI/36));};
                    Line(Arc(J),Arc(J+1),IslandHudColors::Amber,1.65f*S);
                }
            }
            const float Dot=bInset ? 1.7f*S : 2.5f*S;
            DrawRect(Tint(IslandHudColors::Ink),P.X-Dot,P.Y-Dot,Dot*2,Dot*2);
            Mark(P,Letter,IslandHudColors::Ink);
        }
        else
        {
            const double Radius = Scale * Circles[I].Radius;
            const auto Ring=[&](int32 J){return P+Radius*FVector2D(FMath::Cos(J*UE_TWO_PI/40),FMath::Sin(J*UE_TWO_PI/40));};
            for (int32 J=0;J<40;++J) Line(Ring(J),Ring(J+1),IslandHudColors::Sea,bExpedition ? 1.5f : 1.f);
            if (bExpedition)
            {
                FLinearColor Hatch = IslandHudColors::Sea;
                Hatch.A *= .34f;
                const FVector2D Diagonal(UE_INV_SQRT_2,UE_INV_SQRT_2);
                const FVector2D Normal(-Diagonal.Y,Diagonal.X);
                for (int32 Stripe=-2;Stripe<=2;++Stripe)
                {
                    const double Offset = Radius * Stripe / 3.;
                    const double HalfLength = FMath::Sqrt(Radius*Radius-Offset*Offset);
                    const FVector2D Middle = P + Normal*Offset;
                    Line(Middle-Diagonal*HalfLength,Middle+Diagonal*HalfLength,Hatch);
                }
                Mark(P+FVector2D(-.80,-.80)*Radius-FVector2D(8,5),Letter,IslandHudColors::Ink);
            }
        }
    }
    if (bExpedition && Round)
    {
        for (int32 I=0;I<Round->SpentFalsePoints.Num();++I)
        {
            const FVector2D P=Project(Round->SpentFalsePoints[I]);
            const bool bStamp=I==RevealedDecoy && !bFinished;
            const double R=(bStamp ? FMath::Lerp(12.f,3.5f,FMath::SmoothStep(0.f,.65f,DecoyAge)) : 3.5)*S;
            const FLinearColor Color=bStamp ? IslandHudColors::Ink : IslandHudColors::Sea;
            Line(P-FVector2D(R,R),P+FVector2D(R,R),Color,1.8f*S);
            Line(P+FVector2D(-R,R),P+FVector2D(R,-R),Color,1.8f*S);
        }
        if (!bInset)
        {
            FVector2D At(Center.X-Size*.5f,Center.Y+Size*.5f+2.f);
            const float LabelScale=.85f*S;
            UFont* Font=GEngine->GetMediumFont();
            float W,H;
            if (!Round->SpentFalsePoints.IsEmpty())
            {
                const FString Key=EXPEDITION_TEXT("× 已开启的假箱","× Opened decoy");
                GetTextSize(Key,W,H,Font,LabelScale);
                DrawRect(Tint(IslandHudColors::Panel),At.X,At.Y,W+12*S,H+4*S);
                DrawText(Key,Tint(IslandHudColors::Ink),At.X+6*S,At.Y+2*S,Font,LabelScale);

            }
        }
    }
    if (bInset && bFinished)
    {
        const FString Caption=FString::Format(EXPEDITION_TEXT("本队海图  {0}/4","Crew chart  {0}/4"),{Clues.Count()});
        DrawRect(Tint(IslandHudColors::Panel),Center.X-PaperWidth*.5f,Center.Y+Size*.5f,PaperWidth,30*S);
        DrawText(Caption,Tint(IslandHudColors::Ink),Center.X-PaperWidth*.5f+10*S,Center.Y+Size*.5f+4*S,GEngine->GetMediumFont(),S);
    }
    if (!bExpedition)
        for (TActorIterator<AExpeditionLandmark> It(GetWorld());It;++It)
            Mark(Project(FVector2D(It->GetActorLocation())),FString::Printf(TEXT("%c"),'A'+It->ClueIndex),IslandHudColors::Sea);
    FVector Rally;
    bool bAtChest;
    if (!bInset && !bAnonymous && PC.GetCrewRally(Rally,bAtChest))
    {
        const FVector2D P = Project(FVector2D(Rally));
        const FVector2D Vert(0,7), Horiz(7,0);
        Line(P-Vert,P+Horiz,IslandHudColors::Ally,2);
        Line(P+Horiz,P+Vert,IslandHudColors::Ally,2);
        Line(P+Vert,P-Horiz,IslandHudColors::Ally,2);
        Line(P-Horiz,P-Vert,IslandHudColors::Ally,2);
        Mark(P+FVector2D(4,-14), bAtChest ? EXPEDITION_TEXT("集合撬锁", "Rally at chest") : EXPEDITION_TEXT("集合", "Rally"), IslandHudColors::Ally);
    }
    if (const auto* GS=GetWorld()->GetGameState<AIslandGameState>())
    {
        const TArray<int32>& Teams=GS->LaunchedTeams;
        if (!bInset) for (int32 Team:Teams)
            if (const auto* Dock=AExpeditionDock::FindForTeam(GetWorld(),Teams,Team))
            {
                const auto P=Project(FVector2D(Dock->GetActorLocation()));
                Mark(P,bAnonymous ? FString(EXPEDITION_TEXT("码头","Dock")) : FString::Format(EXPEDITION_TEXT("码头 {0}", "Dock {0}"), {Team+1}),
                    !bAnonymous && Team==PC.GetTeam()?IslandHudColors::Ink:IslandHudColors::Sea,true);
                DrawRect(Tint(IslandHudColors::Sea),P.X-2,P.Y-2,4,4);
            }
        for (const auto& Entry:GS->PlayerArray)
            if (const auto* P=Cast<AIslandPlayerState>(Entry.Get()); P && P->Team==PC.GetTeam())
            {
                const bool bSelf=P==PC.GetPlayerState<AIslandPlayerState>();
                if ((bAnonymous || bInset) && !bSelf) continue;
                const APawn* Pawn=bSelf ? PC.GetPawn().Get() : P->GetPawn();
                if (!Pawn) continue;
                const auto Pos=Project(FVector2D(Pawn->GetActorLocation()));
                const FLinearColor Color=bSelf?IslandHudColors::Alert:IslandHudColors::Ink;
                const double Heading=FMath::DegreesToRadians(Pawn->GetActorRotation().Yaw);
                const FVector2D Forward(FMath::Sin(Heading),-FMath::Cos(Heading)),Side(-Forward.Y,Forward.X);
                if (bSelf && !bInset) Mark(Pos+FVector2D(5,5),EXPEDITION_TEXT("你", "You"),IslandHudColors::Alert);
                Line(Pos+Forward*5,Pos-Forward*3+Side*3,Color,2);
                Line(Pos+Forward*5,Pos-Forward*3-Side*3,Color,2);
            }
    }
}
