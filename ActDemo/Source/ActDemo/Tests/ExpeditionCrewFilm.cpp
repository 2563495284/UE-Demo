#include "Tests/ExpeditionWalk.h"
#include "Tests/IslandProbe.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Engine/DirectionalLight.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/HUD.h"
#include "HAL/FileManager.h"
#include "Island/IslandGameMode.h"
#include "Island/ExpeditionDock.h"
#include "Island/ExpeditionMudBall.h"
#include "Island/IslandCrewPortrait.h"
#include "Island/ExpeditionAnimalComponent.h"
#include "Island/IslandPlayerController.h"
#include "Island/IslandState.h"
#include "Island/IslandWorld.h"
#include "Misc/Paths.h"
#include "Net/UnrealNetwork.h"
#include "UnrealClient.h"
#include "Voxel/VoxelCharacter.h"
#include "Voxel/VoxelTerrain.h"
#if WITH_EDITOR
#include "ShaderCompiler.h"
#endif

void AExpeditionWalk::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(AExpeditionWalk,FilmSubject);
    DOREPLIFETIME(AExpeditionWalk,FilmServerFrame);
}

AIslandChest* AIslandProbe::PrepareFilmPrize(AIslandGameMode& Mode,AIslandPlayerController& Player,FVector Center)
{
#if !UE_BUILD_SHIPPING
    if (!Mode.HasAuthority() || (!FParse::Param(FCommandLine::Get(),TEXT("CrewPunchFilm"))
        && !FParse::Param(FCommandLine::Get(),TEXT("CrewPunchFilmPair")))) return nullptr;
    const int32 Team=Player.GetTeam(), Prize=IslandRules::PrizeIndex;
    if (!Mode.TeamClues.IsValidIndex(Team) || !Mode.Exposed.IsValidIndex(Prize)) return nullptr;
    // A repeatable movie fixture for the last two seconds of a hunt. The performers
    // still have to reach the real coffer and become participants in its live lock.
    auto* Chest=Mode.GetWorld()->SpawnActor<AIslandChest>(Center,FRotator::ZeroRotator);
    if (!Chest) return nullptr;
    Chest->SetPrize(true);
    Mode.Exposed[Prize]=Chest;
    Mode.TeamClues[Team].Mask=15;
    Mode.SendClueSnapshot(Team);
    return Chest;
#else
    return nullptr;
#endif
}

void AExpeditionWalk::TickCrewFilm()
{
#if !UE_BUILD_SHIPPING
    auto& World=*GetWorld();
    auto* PC=Cast<AIslandPlayerController>(World.GetFirstPlayerController());
    auto* Game=World.GetGameState<AIslandGameState>();
    auto* Terrain=AVoxelTerrain::Find(&World);
    if (!PC || !PC->IsLocalController() || !PC->GetHero() || !Game || !Terrain || !Terrain->IsReady()) return;
    const bool Pair=FParse::Param(FCommandLine::Get(),TEXT("CrewPunchFilmPair"));
    const bool Shape=FParse::Param(FCommandLine::Get(),TEXT("CrewShapeFilm"));
    const bool Buttons=FParse::Param(FCommandLine::Get(),TEXT("CrewButtonFilm"));
    if (HasAuthority() && bFilmPryStarted && Game->Phase==EIslandPhase::Finished)
    {
        UE_LOG(LogTemp,Display,TEXT("CREW_FILM_DONE frames=%d authority=1 pry=1 chest_finished=1"),FilmFrame);
        FPlatformMisc::RequestExitWithStatus(false,0);
        return;
    }
    auto* Mode=World.GetAuthGameMode<AIslandGameMode>();
    TArray<AIslandPlayerController*> Others;
    if (HasAuthority())
        for (TActorIterator<AIslandPlayerController> It(&World);It;++It) if (*It!=PC && It->GetHero()) Others.Add(*It);
    if (HasAuthority() && !bStarted && Game->Phase==EIslandPhase::Waiting)
    {
        if (Pair && Others.IsEmpty()) return;
        int32 Model=0;
        FParse::Value(FCommandLine::Get(),TEXT("CrewFilmModel="),Model);
        PC->ServerChooseCharacter(FMath::Clamp(Model,0,IslandRules::CharacterModelCount-1));
        for (auto* Guest:Others) { Mode->ChooseTeam(*Guest,PC->GetTeam()); Mode->ChooseCharacter(*Guest,2); Mode->SetRoomReady(*Guest,true); }
        if (Game->CanLaunch()) { PC->ServerStartExpedition(); bStarted=true; }
        return;
    }
    if (Game->Phase!=EIslandPhase::Hunting || !PC->IsPlaying() || !PC->IsVoxelInputEnabled()) return;
    const FRotator Heading(0.,149.,0.);
    const FVector Forward=Heading.Vector(), Side=FVector::CrossProduct(FVector::UpVector,Forward);
    if (HasAuthority() && !FilmSubject)
    {
        if (!Mode->CanPlayerAct(*PC)) return;
        FilmSubject=PC->GetHero();
        const FVector2D ShapeShore(6460.,-2530.);
        const double Ground=Terrain->GetDensity()->SurfaceAt(ShapeShore).Get(380.);
        FVector Stand(ShapeShore,Ground+FilmSubject->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()+3.);
        if (!Shape)
        {
            // The portrait sits on the working approach to the real team pier.
            // Its piles and deck stay behind the subject, clear of the facial silhouette.
            if (auto* Dock=AExpeditionDock::FindForTeam(&World,Game->LaunchedTeams,PC->GetTeam()))
            {
                const FVector Site=Dock->GetActorTransform().TransformPositionNoScale({535.,-268.,0.});
                if (const auto Deck=Mode->FindStandingFloor(FilmSubject,Site+FVector(0.,0.,1300.),1800.))
                    Stand=*Deck;
                UE_LOG(LogTemp,Display,TEXT("CREW_FILM_WORKSITE dock=%d position=%s"),Dock->DockIndex,*Stand.ToCompactString());
            }
        }
        FilmSubject->TeleportTo(Stand,Heading);
        PC->SetControlRotation(Heading);
        for (auto* Guest:Others)
            Guest->GetHero()->TeleportTo(FilmSubject->GetActorLocation()+Side*260.+Forward*90.+FVector(0,0,70.),Heading);
        ForceNetUpdate();
    }
    if (!FilmSubject || FScreenshotRequest::IsScreenshotRequested()) return;
    const int32 Frame=HasAuthority() ? FilmFrame : FilmServerFrame;
    auto* Hero=FilmSubject.Get();
    if (!ArtCamera)
    {
        ArtCamera=World.SpawnActor<ACameraActor>();
        ArtCamera->GetCameraComponent()->SetFieldOfView(46.f);
        // In this opt-in take the lens and shoreline stay registered between
        // lighting studies. Rake the existing island sun across the cheek.
        const bool Baseline=Shape && FParse::Param(FCommandLine::Get(),TEXT("CrewShapeBaseline"));
        if (TActorIterator<ADirectionalLight> Sun(&World); Sun && !Baseline)
        {
            Sun->GetLightComponent()->SetMobility(EComponentMobility::Movable);
            FRotator LightAngle=Sun->GetActorRotation();
            LightAngle.Yaw-=64.f;
            Sun->SetActorRotation(LightAngle);
        }
        IslandCrewPortrait::Light(*ArtCamera,*Hero,Baseline);
        if (auto* HUD=PC->GetHUD()) HUD->bShowHUD=false;
        const FString Dir=FPaths::ProjectSavedDir()/TEXT("CrewPunchFilm");
        IFileManager::Get().MakeDirectory(*Dir,true);
    }
    const auto Key=[&](FKey KeyCode,bool Down)
    {
        PC->InputKey(FInputKeyEventArgs::CreateSimulated(KeyCode,Down ? IE_Pressed : IE_Released,Down ? 1.f : 0.f));
    };
    if (HasAuthority() && !Shape)
    {
        if (Frame>=32 && Frame<66) Hero->MoveOnGrid({0,1},Heading.Yaw);
        if (Frame==81) Hero->Jump();
        if (Frame==85) Hero->StopJumping();
        if (Frame==114 || Frame==140 || Frame==243) PC->SetExpeditionDigHeld(true);
        if (Frame==116 || Frame==173 || Frame==278) PC->SetExpeditionDigHeld(false);
        if (Frame==197 || (Buttons && (Frame==232 || Frame==265)))
        {
            if (!Buttons) PC->SetControlRotation(FRotator(6.,Heading.Yaw,0.));
            PC->SetExpeditionThrowHeld(true);
        }
        if (Frame==200 || (Buttons && (Frame==235 || Frame==268))) PC->SetExpeditionThrowHeld(false);
        if (Frame==223) { PC->SetViewTarget(Hero); Key(EKeys::V,true); }
        if (Frame==224 || Frame==301) Key(EKeys::V,false);
        if (Frame==300) Key(EKeys::V,true);
        if (Frame==307)
        {
            FVector Center=Hero->GetActorLocation()+Forward*67.;
            if (const auto Floor=Mode->FindStandingFloor(Hero,Center+FVector(0.,0.,340.),700.))
                Center.Z=Floor->Z-Hero->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()+30.;
            else Center.Z=Terrain->GetDensity()->SurfaceAt(FVector2D(Center)).Get(Center.Z-90.)+34.;
            ArtChest=AIslandProbe::PrepareFilmPrize(*Mode,*PC,Center);
            if (ArtChest)
            {
                Mode->TryOpen(*PC,*ArtChest);
                for (auto* Guest:Others)
                {
                    const FVector Spot=Center+Side*75.+FVector(0,0,60.);
                    Guest->GetHero()->TeleportTo(Spot,(-Side).Rotation());
                    Guest->SetControlRotation((-Side).Rotation());
                    Guest->ClientSetRotation((-Side).Rotation(),true);
                    Mode->TryOpen(*Guest,*ArtChest);
                }
            }
            const auto* Member=PC->GetPlayerState<AIslandPlayerState>();
            bFilmPryStarted=Member && Member->bPrying;
        }
        FilmServerFrame=Frame;
        if (Frame%3==0) ForceNetUpdate();
    }

    const bool Eyes=HasAuthority() && Frame>=223 && Frame<=301;
    const bool Shoulder=HasAuthority() && Frame>=(Buttons ? 191 : 216) && Frame<223;
    if (Buttons && HasAuthority())
        if (auto* HUD=PC->GetHUD()) HUD->bShowHUD=Shoulder || Eyes;
    if (Shape)
    {
        const int32 Section=FMath::Min(Frame/12,3);
        const double Angle[]={24.,90.,166.,28.};
        const FVector Facing=Forward.RotateAngleAxis(Angle[Section],FVector::UpVector);
        const double Range=Section==3 ? 2280. : 476.;
        const FVector Focus=Hero->GetActorLocation()-FVector(0.,0.,5.);
        const FVector Eye=Focus+Facing*Range+FVector(0.,0.,Section==3 ? 366. : 113.);
        ArtCamera->SetActorLocationAndRotation(Eye,(Focus-Eye).Rotation());
        PC->SetViewTarget(ArtCamera);
    }
    else if (Shoulder)
    {
        PC->SetControlRotation(FRotator(Buttons ? -12. : -6.,Heading.Yaw,0.));
        PC->SetViewTarget(Hero);
    }
    else if (!Eyes)
    {
        FVector Focus=Hero->GetActorLocation()+FVector(0,0,-5.);
        const bool AtChest=Frame>=307;
        if (AtChest) Focus+=Forward*35.+FVector(0,0,-18.);
        // The separate client has its own lens and follows a networked puppet.
        const FVector Flank=HasAuthority() ? Side : -Side;
        const double Front=Frame<32 ? 1.18-Frame*.013 : Frame<114 ? .37 : .58;
        // Begin at the worker's upper torso height with the horizon below the face.
        const double Crane=AtChest ? 128. : FMath::Lerp(13.,92.,FMath::Clamp((Frame-25.)/35.,0.,1.));
        const FVector Eye=Focus+(Forward*Front+Flank*.82).GetSafeNormal2D()*(AtChest ? 498. : 478.)+FVector(0,0,Crane);
        ArtCamera->SetActorLocationAndRotation(Eye,(Focus-Eye).Rotation());
        PC->SetViewTarget(ArtCamera);
    }
    if (Frame==0)
    {
#if WITH_EDITOR
        if (GShaderCompilingManager && GShaderCompilingManager->IsCompiling()) return;
#endif
        if (++ArtCameraFrame<24) return;
    }
    const FString File=FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()/TEXT("CrewPunchFilm")
        /FString::Printf(TEXT("Take_%04d.png"),FilmFrame));
    FScreenshotRequest::RequestScreenshot(File,false,false);
    if (Shape && Frame>=47)
    {
        UE_LOG(LogTemp,Display,TEXT("CREW_FILM_DONE frames=48 shape=1"));
        FPlatformMisc::RequestExitWithStatus(false,0);
        return;
    }
    if (const auto* Animal=Hero->FindComponentByClass<UExpeditionAnimalComponent>())
    {
        const auto& Pose=Animal->GetVisiblePose();
        if (!HasAuthority() && !bFilmSawPeerHit && Pose.Strength>.8f && Pose.Extension>.7f)
        {
            bFilmSawPeerHit=true;
            UE_LOG(LogTemp,Display,TEXT("CREW_FILM_PEER_HIT frame=%d strength=%.2f extension=%.2f"),FilmFrame,Pose.Strength,Pose.Extension);
        }
        if (HasAuthority() && (Frame==31 || Frame==67 || Frame==88 || Frame==118 || Frame==170 || Frame==178 || Frame==275 || Frame==341))
            UE_LOG(LogTemp,Display,TEXT("CREW_FILM_POSE frame=%d run=%.2f air=%.2f load=%.2f reach=%.2f strength=%.2f pry=%.2f"),
                Frame,Pose.Speed,Pose.Air,Pose.Load,Pose.Extension,Pose.Strength,Pose.Pry);
        if (Buttons && HasAuthority() && Frame>=110 && Frame<295)
        {
            const AVoxelCharacter* Subject=Hero;
            int32 Clods=0;
            for (TActorIterator<AExpeditionMudBall> It(&World);It;++It)
                if (It->GetInstigator()==Subject) ++Clods;
            UE_LOG(LogTemp,Display,TEXT("CREW_BUTTON_FRAME frame=%d first=%d load=%.3f punch=%.3f recoil=%.3f toss=%.3f follow=%.3f clods=%d"),
                Frame,Hero->IsInFirstPersonView(),Pose.Load,Pose.Extension,Pose.Recoil,Pose.Toss,Pose.TossFollow,Clods);
        }
    }
    if (Frame==226 && HasAuthority()) UE_LOG(LogTemp,Display,TEXT("CREW_FILM_EYES shown=%d"),Hero->IsInFirstPersonView());
    if (Frame>=353 || (!HasAuthority() && FilmFrame>520))
    {
        UE_LOG(LogTemp,Display,TEXT("CREW_FILM_DONE frames=%d authority=%d pry=%d subject=%s"),FilmFrame+1,HasAuthority(),bFilmPryStarted,*Hero->GetName());
        FPlatformMisc::RequestExitWithStatus(false,HasAuthority() && !bFilmPryStarted ? 1 : 0);
    }
    ++FilmFrame;
#endif
}
