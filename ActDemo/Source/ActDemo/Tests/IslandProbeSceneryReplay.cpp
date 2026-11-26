#include "Tests/IslandProbe.h"

#include "AudioMixerBlueprintLibrary.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/HUD.h"
#include "HAL/FileManager.h"
#include "InputKeyEventArgs.h"
#include "Island/ExpeditionDock.h"
#include "Island/IslandAttachedMud.h"
#include "Island/IslandGameMode.h"
#include "Island/IslandNeutralProp.h"
#include "Island/IslandPlayerController.h"
#include "Island/IslandSceneryVoxels.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"
#include "Voxel/IslandRouteVoxel.h"
#include "Voxel/VoxelCharacter.h"

namespace
{
    FString ReplayRay(UWorld& World, FVector From, FVector To, AActor* Ignore, bool bMud)
    {
        FHitResult Hit;
        if (!AIslandNeutralProp::TraceShovel(World,Hit,From,To,
            FCollisionQueryParams(SCENE_QUERY_STAT(SceneryReplayRay),false,Ignore))) return TEXT("clear");
        if (const auto* Mud=Cast<AIslandAttachedMud>(Hit.GetActor()))
            return FString::Printf(TEXT("mud:%08x:%d"),Mud->Field()->Digest(),FMath::RoundToInt(Hit.Distance));
        return bMud ? TEXT("miss") : FString::Printf(TEXT("solid:%d"),FMath::RoundToInt(Hit.Distance));
    }

    void StandAtEye(AVoxelCharacter& Hero,FVector Eye,FVector Direction)
    {
        auto* Movement=Hero.GetCharacterMovement();
        Movement->StopMovementImmediately();
        // The normal island character exits flying mode unless it is actively climbing a wall.
        Movement->GravityScale=0.f;
        Movement->SetMovementMode(MOVE_Falling);
        Hero.SetActorLocationAndRotation(Eye-(Hero.GetAimOrigin()-Hero.GetActorLocation()),
            FRotator(0,Direction.Rotation().Yaw,0),false,nullptr,ETeleportType::TeleportPhysics);
        Hero.GetController()->SetControlRotation(Direction.Rotation());
    }

    // On each ordinary strike, approach an exposed cell in a horizontal band across the actual pier.
    bool NextPierFace(UIslandMeshVoxels& Pier, AVoxelCharacter& Hero, double Height, FVector& Eye, FVector& Aim)
    {
        bool Found=false;
        const FVector Forward=Pier.GetOwner()->GetActorForwardVector();
        const FVector Right=Pier.GetOwner()->GetActorRightVector();
        Pier.ForEachSolid([&](int32,FVector Point)
        {
            if (Found || FMath::Abs(Point.Z-Height)>Pier.CellRadius()+8.) return;
            Point.Z=Height;
            for (FVector Side : {Forward,-Forward,Right,-Right})
            {
                const FVector From=Point+Side*310.;
                FHitResult Hit;
                if (!AIslandNeutralProp::TraceShovel(*Hero.GetWorld(),Hit,From,Point-Side*18.,
                    FCollisionQueryParams(SCENE_QUERY_STAT(SceneryPierSection),false,&Hero)) ||
                    Hit.GetComponent()!=&Pier) continue;
                Eye=Hit.ImpactPoint+Side*95.;
                Aim=-Side;
                Found=true;
                break;
            }
        });
        return Found;
    }
}

void AIslandProbePeer::ClientSceneryRay_Implementation(int32 Stage,FVector From,FVector To,AActor* Ignore,bool bMud)
{
    const FString Reading=ReplayRay(*GetWorld(),From,To,Ignore,bMud);
    UE_LOG(LogTemp,Display,TEXT("SCENERY_REPLAY_RAY stage=%d observation=%s"),Stage,*Reading);
    ServerNeutralReport(Stage,Reading);
}

void AIslandProbePeer::ClientSceneryCamera_Implementation(FName Clip,FVector View,FVector Focus,bool bUsePlayer)
{
    FString RecordDir;
    const bool bCapture=FParse::Param(FCommandLine::Get(),TEXT("IslandScreenshot")) &&
        FParse::Value(FCommandLine::Get(),TEXT("FieldShotsDir="),RecordDir);
    if (bCapture && !SceneryClip.IsNone())
        UAudioMixerBlueprintLibrary::StopRecordingOutput(this,EAudioRecordingExportType::WavFile,SceneryClip.ToString(),RecordDir);
    SceneryClip=NAME_None;
    if (Clip.IsNone() || !bCapture) return;
    auto* Player=CastChecked<AIslandPlayerController>(GetOwner());
    if (bUsePlayer)
    {
        Player->SetViewTarget(Player->GetHero());
        const bool First=Clip==TEXT("Impact");
        if (Player->WantsFirstPersonView()!=First)
        {
            Player->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::V,IE_Pressed,1.f));
            Player->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::V,IE_Released,0.f));
        }
        Player->SetControlRotation((Focus-Player->GetHero()->GetAimOrigin()).Rotation());
    }
    else
    {
        if (!SceneryCamera.IsValid()) SceneryCamera=GetWorld()->SpawnActor<ACameraActor>();
        SceneryCamera->SetActorLocationAndRotation(View,(Focus-View).Rotation());
        SceneryCamera->GetCameraComponent()->SetFieldOfView(57.f);
        Player->SetViewTarget(SceneryCamera.Get());
    }
    if (auto* HUD=Player->GetHUD()) HUD->bShowHUD=bUsePlayer;
    SceneryClip=Clip;
    SceneryClipStarted=FPlatformTime::Seconds();
    UAudioMixerBlueprintLibrary::StartRecordingOutput(this,12.f);
    NextSceneryFrame=SceneryClipStarted+.12;
}

void AIslandProbePeer::CaptureSceneryFrame()
{
    FString Dir;
    const double Now=FPlatformTime::Seconds();
    if (Now<NextSceneryFrame || FScreenshotRequest::IsScreenshotRequested() ||
        !FParse::Value(FCommandLine::Get(),TEXT("FieldShotsDir="),Dir)) return;
    NextSceneryFrame=Now+.06;
    const FString Folder=FPaths::Combine(Dir,SceneryClip.ToString()+TEXT("_frames"));
    IFileManager::Get().MakeDirectory(*Folder,true);
    FScreenshotRequest::RequestScreenshot(FPaths::Combine(Folder,
        FString::Printf(TEXT("%09d.png"),FMath::RoundToInt((Now-SceneryClipStarted)*1000.))),true,false);
}

void AIslandProbe::TickSceneryReplay(double Now)
{
    if (Now-StepAt>75.)
    {
        UE_LOG(LogTemp,Error,TEXT("SCENERY_REPLAY_STALLED stage=%d hits=%d expected=%s reports=%d"),NeutralStep,ReplayHits,*NeutralExpected,Reports.Num());
        Require(false,TEXT("scenery interaction replay stalled"));
        return;
    }
    auto& Player=*Mode->GetPlayers()[0];
    auto& Hero=*Player.GetHero();
    if (!ReplayArch.IsValid())
    {
        for (TActorIterator<AExpeditionLandmark> It(GetWorld()); It; ++It)
            if (!It->ClueIndex) {ReplayArch=*It;break;}
        if (!ReplayArch.IsValid()) {Require(false,TEXT("scenery replay needs the authored stone arch"));return;}
        TInlineComponentArray<UIslandMeshVoxels*> Fields(ReplayArch.Get());
        for (auto* Field:Fields)
        {
            const FString Name=Field->SourcePart()->GetName();
            if (Name.StartsWith(TEXT("WestPier"))) WestPier=Field;
            if (Name.StartsWith(TEXT("EastPier"))) EastPier=Field;
            if (Name.StartsWith(TEXT("Lintel"))) ArchBeam=Field;
        }
    }
    if (!WestPier.IsValid() || !EastPier.IsValid() || !ArchBeam.IsValid())
    {Require(false,TEXT("the arch must expose both piers and its lintel"));return;}
    const auto& Transform=ReplayArch->GetActorTransform();
    const auto P=[&](double X,double Y,double Z){return Transform.TransformPosition({X,Y,Z});};
    const FVector Forward=ReplayArch->GetActorForwardVector();
    const double Age=Now-StepAt;
    auto Camera=[&](FName Name,FVector View,FVector Focus,bool PlayerView=false)
    {
        for (int32 I=0;I<Peers.Num();++I)
            Peers[I]->ClientSceneryCamera(Name,View,Focus,PlayerView && I==0);
    };
    auto Next=[&](){SetNeutralStep(NeutralStep+1,Now);};
    auto RaysAgree=[&](bool bMud)
    {
        NeutralExpected=ReplayRay(*GetWorld(),ReplayRayStart,ReplayRayEnd,&Hero,bMud);
        if (bMud && !NeutralExpected.StartsWith(TEXT("mud:")))
        {
            if (Age>2. && Now-LastAction>2.)
            {
                LastAction=Now;
                FHitResult Hit;
                AIslandNeutralProp::TraceShovel(*GetWorld(),Hit,ReplayRayStart,ReplayRayEnd,FCollisionQueryParams(NAME_None,false,&Hero));
                UE_LOG(LogTemp,Warning,TEXT("SCENERY_REPLAY_AIM ray=%s first=%s/%s hero=%s relative=%s pier=%s"),*NeutralExpected,*GetNameSafe(Hit.GetActor()),*GetNameSafe(Hit.GetComponent()),*Hero.GetAimOrigin().ToCompactString(),*Transform.InverseTransformPosition(Hit.ImpactPoint).ToCompactString(),*WestPier->Bounds.GetBox().ToString());
            }
            return false;
        }
        if (Now-LastAction>.25)
        {
            for (int32 I=0;I<Peers.Num();++I)
                if (!Reports.Contains(I)) Peers[I]->ClientSceneryRay(NeutralStep,ReplayRayStart,ReplayRayEnd,&Hero,bMud);
            LastAction=Now;
        }
        return Reports.Num()==Peers.Num();
    };
    switch (NeutralStep)
    {
    case 0:
        for (int32 I=1; I<Peers.Num(); ++I)
        {
            auto* Observer=Mode->GetPlayers()[I]->GetHero();
            StandAtEye(*Observer,P(650,600+100*I,370),-Forward);
        }
        ReplayTarget=P(125,-260,240);
        StandAtEye(Hero,P(460,-260,240),-Forward);
        Camera(TEXT("Mud"),P(530,-670,380),ReplayTarget,true);
        ReplayHits=0;
        Next();
        return;
    case 1:
        if (Age<.75 || Now-LastAction<.75) return;
        if (ReplayHits<3)
        {
            StandAtEye(Hero,P(460,-260,240),-Forward);
            Player.SubmitExpeditionThrow(-Forward);
            ++ReplayHits;
            LastAction=Now;
            return;
        }
        ReplayRayStart=P(450,-260,246);
        ReplayRayEnd=P(0,-260,246);
        Next();
        return;
    case 2:
        if (!RaysAgree(true)) return;
        {
            FHitResult Wall,Top;
            const FCollisionQueryParams Query(SCENE_QUERY_STAT(SceneryMudStep),false,&Hero);
            GetWorld()->LineTraceSingleByChannel(Wall,ReplayRayStart,ReplayRayEnd,ECC_Visibility,Query);
            const FVector Tread=Wall.ImpactPoint-Forward*32.;
            if (!GetWorld()->LineTraceSingleByChannel(Top,Tread+FVector(0,0,170),Tread-FVector(0,0,25),ECC_Visibility,Query) ||
                !Cast<AIslandAttachedMud>(Top.GetActor())) {Require(false,TEXT("the wall deposit needs a walkable tread"));return;}
            Hero.SetActorLocation(Top.ImpactPoint+FVector(0,0,Hero.GetSimpleCollisionHalfHeight()+12.),false,nullptr,ETeleportType::TeleportPhysics);
            Hero.GetCharacterMovement()->GravityScale=Hero.GetClass()->GetDefaultObject<AVoxelCharacter>()->GetCharacterMovement()->GravityScale;
            Hero.GetCharacterMovement()->SetMovementMode(MOVE_Falling);
        }
        Next();
        return;
    case 3:
        if (Age<.55 || !Hero.GetMovementBaseObject() || !Hero.GetMovementBaseObject()->GetTypedOuter<AIslandAttachedMud>()) return;
        UE_LOG(LogTemp,Display,TEXT("SCENERY_REPLAY_FOOT supported=1"));
        // Keep the actor's camera on the opposite pier while successive normal shovel strokes open it.
        ReplayRayStart=P(380,260,240); ReplayRayEnd=P(-235,260,240);
        StandAtEye(Hero,P(280,260,240),-Forward);
        Camera(TEXT("Impact"),P(450,545,325),P(75,260,240),true);
        ReplayHits=0;
        Next();
        return;
    case 4:
        {
            if (Age<.8 || Now-LastAction<.62) return;
            FHitResult Hit;
            if (EastPier->LineTraceComponent(Hit,ReplayRayStart,ReplayRayEnd,FCollisionQueryParams::DefaultQueryParam))
            {
                StandAtEye(Hero,Hit.ImpactPoint+Forward*155.,-Forward);
                Player.SubmitEdit(EVoxelAction::Punch,-Forward);
                ++ReplayHits;
                LastAction=Now;
                return;
            }
            if (!ReplayHits) {Require(false,TEXT("the demonstration must actually punch through stone"));return;}
            UE_LOG(LogTemp,Display,TEXT("SCENERY_REPLAY_BORE strokes=%d"),ReplayHits);
            Next();
            return;
        }
    case 5:
        if (Age<.75 || !RaysAgree(false)) return;
        Camera(NAME_None,FVector::ZeroVector,FVector::ZeroVector);
        ReplayHits=0;
        Next();
        return;
    case 6:
    case 7:
        {
            if (Age<.7 || Now-LastAction<.55) return;
            FVector Eye,Aim;
            const bool LastPier=NeutralStep==7;
            if (!ArchBeam->RemainingCount())
            {
                Require(LastPier,TEXT("one intact pier must carry the lintel"));
                if (!LastPier) return;
                UE_LOG(LogTemp,Display,TEXT("SCENERY_REPLAY_RELEASED cuts=%d"),ReplayHits);
                Next();
                return;
            }
            if (!NextPierFace(*(LastPier?EastPier:WestPier).Get(),Hero,P(0,0,365).Z,Eye,Aim))
            {
                if (LastPier) {Require(false,TEXT("the severed arch must fall"));return;}
                Camera(TEXT("Collapse"),P(1480,-450,555),P(0,0,360));
                Next();
                return;
            }
            StandAtEye(Hero,Eye,Aim);
            Player.SubmitEdit(EVoxelAction::Punch,Aim);
            LastAction=Now;
            ++ReplayHits;
            return;
        }
    case 8:
        if (Age<2.8) return;
        Camera(NAME_None,FVector::ZeroVector,FVector::ZeroVector);
        UE_LOG(LogTemp,Display,TEXT("SCENERY_REPLAY_DONE peers=%d"),Peers.Num());
        Advance(EStep::Done);
        return;
    }
}
