#include "Tests/IslandProbe.h"
#include "Tests/ExpeditionIdentityProof.h"

#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/HUD.h"
#include "HAL/FileManager.h"
#include "InputKeyEventArgs.h"
#include "Island/IslandGameMode.h"
#include "Island/IslandHUD.h"
#include "Island/IslandPlayerController.h"
#include "Island/IslandState.h"
#include "Island/IslandWorld.h"
#include "PartyVoxelArena.h"
#include "Voxel/VoxelCharacter.h"
#include "Voxel/VoxelTerrain.h"
#include "UnrealClient.h"

namespace
{
constexpr int32 VenueBuildingCount = 10;
constexpr int32 AllVenueBuildings = (1 << VenueBuildingCount) - 1;

int32 PartsPerBuilding(const FName Feature)
{
    return Feature == TEXT("PartyBeaconClimb") || Feature == TEXT("PartyBridgeDash") ||
        Feature == TEXT("PartyCrownHold") ? 4 : 3;
}

// Sample the collision mesh at unaligned positions, including the brush skirt and untouched ground.
constexpr double ProfileRadii[] = {0., 29., 48., 57., 66., 75., 86., 130.};
bool SampleFloor(const APartyVoxelSession& Arena, const FVector& Near, FHitResult& Hit)
{
    const FCollisionQueryParams Params(SCENE_QUERY_STAT(PartyTerrainProfile), false);
    return Arena.GetWorld()->LineTraceSingleByChannel(Hit, Near+FVector(0,0,165.), Near-FVector(0,0,245.), ECC_Visibility, Params)
        && Hit.GetActor() == &Arena && Hit.GetComponent() && Hit.GetComponent()->ComponentHasTag(TEXT("PartyDensitySurface"))
        && Hit.ImpactNormal.Z > 0.;
}

bool CanStandOnBoard(const APartyVoxelSession& Arena, FIntVector Feet)
{
    return Arena.BlockAt(Feet - FIntVector(0,0,1)) != EPartyBlock::Air &&
        Arena.BlockAt(Feet) == EPartyBlock::Air && Arena.BlockAt(Feet + FIntVector(0,0,1)) == EPartyBlock::Air;
}

FVector VenueStandingPose(const APartyVoxelSession& Arena, const APartyVoxelPawn& Pawn, FIntVector Feet)
{
    return Arena.CellCenter(Feet) + FVector(0., 0., Pawn.GetCapsuleComponent()->GetScaledCapsuleHalfHeight() - 48.);
}

bool MoveToVenuePose(APartyVoxelSession& Arena, APartyVoxelPawn& Pawn, FIntVector Feet)
{
    if (!CanStandOnBoard(Arena, Feet)) return false;
    if (Arena.FeetCell(Pawn.GetSeat()) == Feet) return Arena.IsGrounded(Pawn.GetSeat());
    Pawn.TeleportTo(VenueStandingPose(Arena, Pawn, Feet), Pawn.GetActorRotation(), false, false);
    Pawn.GetCharacterMovement()->StopMovementImmediately();
    Pawn.ForceNetUpdate();
    return false;
}

bool VisibleVenuePart(const APartyVoxelSession& Arena, const APartyVoxelPawn& Pawn,
    const TArray<UStaticMeshComponent*>& Parts, int32 Id, FVector Eye, FVector& Target)
{
    for (const auto* Part : Parts)
    {
        if (!Part) continue;
        // Include the upper face of tall structures above low walls, and their silhouettes at eye height.
        for (const double Height : {0., .75, -.40})
        {
            const FVector Point = Part->Bounds.Origin + FVector(0., 0., Part->Bounds.BoxExtent.Z * Height);
            if (Arena.TraceVenueStructure(Eye, (Point - Eye).GetSafeNormal(), &Pawn) == Id)
            {
                Target = Point;
                return true;
            }
        }
    }
    return false;
}

FString VenuePhotoPath(const TCHAR* Name)
{
    FString Directory;
    FParse::Value(FCommandLine::Get(), TEXT("FieldShotsDir="), Directory);
    return FPaths::Combine(Directory, Name);
}

void CaptureGameOverview(APlayerController& Player, APartyVoxelSession& Arena, FName Feature)
{
    // Position the wide shot along the side of the course so its long axis reads across the image.
    FVector ViewDirection(-.45, 1., .90);
    if (Feature == TEXT("PartyMazeChase")) ViewDirection = FVector(-.44, .66, 1.3);
    if (Feature == TEXT("PartyFloodGates")) ViewDirection = FVector(-1., .18, 1.22);
    if (Feature == TEXT("PartyBeaconClimb")) ViewDirection = FVector(-.47, 1., .68);
    if (Feature == TEXT("PartyCrystalHunt")) ViewDirection = FVector(-.52, 1., 1.35);
    if (Feature == TEXT("PartyCrownHold")) ViewDirection = FVector(-.88, .55, 1.08);
    ViewDirection.Normalize();
    const FRotator Rotation = (-ViewDirection).Rotation();
    const FRotationMatrix Axes(Rotation);
    const FVector Right = Axes.GetUnitAxis(EAxis::Y), Up = Axes.GetUnitAxis(EAxis::Z);
    FVector Center, Extent;
    Arena.GetActorBounds(false, Center, Extent);
    int32 Width = 0, Height = 0;
    Player.GetViewportSize(Width, Height);
    const double Aspect = Width > 0 && Height > 0 ? double(Width) / Height : 16. / 9.;
    constexpr float FOV = 54.f;
    const double TanX = FMath::Tan(FMath::DegreesToRadians(FOV * .5));
    // Each corner must satisfy both perspective-frustum inequalities at its actual depth.
    double Distance = 100.;
    for (double X : {-Extent.X, Extent.X})
        for (double Y : {-Extent.Y, Extent.Y})
            for (double Z : {-Extent.Z, Extent.Z})
            {
                const FVector Corner(X, Y, Z);
                const double Side = FMath::Abs(Corner.Dot(Right)) / TanX;
                const double Vertical = FMath::Abs(Corner.Dot(Up)) * Aspect / TanX;
                Distance = FMath::Max(Distance, Corner.Dot(ViewDirection) + 1.13 * FMath::Max(Side, Vertical));
            }
    auto* Camera = Player.GetWorld()->SpawnActor<ACameraActor>(Center + ViewDirection * Distance, Rotation);
    if (!Camera) return;
    Camera->GetCameraComponent()->SetFieldOfView(FOV);
    Camera->GetCameraComponent()->SetConstraintAspectRatio(false);
    const TWeakObjectPtr<APlayerController> Viewer(&Player);
    const TWeakObjectPtr<AActor> Previous = Player.GetViewTarget();
    const TWeakObjectPtr<AHUD> HUD = Player.GetHUD();
    const bool bPreviousHUD = HUD.IsValid() && HUD->bShowHUD;
    if (HUD.IsValid()) HUD->bShowHUD = false;
    Player.SetViewTarget(Camera);
    FString Dir;
    FParse::Value(FCommandLine::Get(), TEXT("FieldShotsDir="), Dir);
    const FString Path = FPaths::Combine(Dir, Feature.ToString()+TEXT("Overview.png"));
    FTimerHandle Capture, Restore;
    Player.GetWorldTimerManager().SetTimer(Capture, FTimerDelegate::CreateWeakLambda(Camera, [Path]()
    {
        FScreenshotRequest::RequestScreenshot(Path, false, false);
    }), .4f, false);
    Player.GetWorldTimerManager().SetTimer(Restore, FTimerDelegate::CreateWeakLambda(Camera, [Camera, Viewer, Previous, HUD, bPreviousHUD]()
    {
        if (Viewer.IsValid() && Viewer->GetViewTarget() == Camera)
            Viewer->SetViewTarget(Previous.IsValid() ? Previous.Get() : Viewer->GetPawn());
        if (HUD.IsValid()) HUD->bShowHUD = bPreviousHUD;
        Camera->Destroy();
    }), .85f, false);
}
}

void AIslandProbePeer::TickAbortRecovery(AIslandPlayerController& Player)
{
    const auto Veil = Player.GetPartyTransitionView();
    const auto* Round = GetWorld()->GetGameState<AIslandGameState>();
    const auto* Hero = Player.GetHero();
    if (bAbortWalkConfirmed || !Round || Round->Phase != EIslandPhase::Hunting || !Hero || Veil.Round != 1
        || !Veil.bReturning || Veil.Phase != EIslandPartyVeil::Clear || Player.IsPartyTravelLocked()) return;
    if (!AbortWalkStart) AbortWalkStart = Hero->GetActorLocation();
    const double Distance = FVector::Dist2D(AbortWalkStart.GetValue(), Hero->GetActorLocation());
    if (Distance < 25.)
        Player.ApplyMove(FVector2D(0., 1.));
    else if (Player.IsVoxelInputEnabled())
    {
        bAbortWalkConfirmed = true;
        UE_LOG(LogTemp, Display, TEXT("PARTY_ABORT_WALK target=guest cm=%.1f unlocked=1"), Distance);
    }
}

void AIslandProbePeer::TickInteractionProof(AIslandPlayerController& Player)
{
    const bool bShots = FParse::Param(FCommandLine::Get(), TEXT("IslandScreenshot"));
    if (bShots)
    {
        FName Stage;
        float Age = 0.f;
        const auto* HUD = Cast<AIslandHUD>(Player.GetHUD());
        if (HUD && HUD->GetTreasureFeedbackFrame(Stage, Age) && Stage != LastTreasureShot)
        {
            const float DesiredAge = Stage == TEXT("Forge") ? .95f : Stage == TEXT("Vault") ? 1.2f : Stage == TEXT("Intel") ? .85f : .45f;
            if (Age >= DesiredAge && (Stage == TEXT("Fragment") || Stage == TEXT("Forge") || Stage == TEXT("Vault") || Stage == TEXT("Intel")))
            {
                const FString Path = VenuePhotoPath(*(TEXT("Treasure") + Stage.ToString() + TEXT(".png")));
                if (!IFileManager::Get().FileExists(*Path))
                {
                    FScreenshotRequest::RequestScreenshot(Path, true, false);
                    UE_LOG(LogTemp, Display, TEXT("TREASURE_FEEDBACK_VIEW stage=%s age=%.2f points=%d key=%d"),
                        *Stage.ToString(), Age, Player.GetLocalClues().Count(), Player.GetLocalClues().HasKey());
                }
                LastTreasureShot = Stage;
            }
        }
    }
    const FIslandPartyTransitionView View = Player.GetPartyTransitionView();
    if (View.Phase == EIslandPartyVeil::Clear || !View.Round) return;
    auto* Character = Cast<ACharacter>(Player.GetPawn());
    if (!Character) return;
    const bool bFirstTrip = View.Round == 1;
    if (!bVeilObserved || VeilRound != View.Round || bVeilReturn != View.bReturning)
    {
        bVeilObserved = true;
        VeilRound = View.Round;
        bVeilReturn = View.bReturning;
        bVeilBlack = bVeilSawSwap = bVeilLockedEarly = bVeilUnlockedBlack = bVeilLogged = false;
        bVeilClosingPhoto = bVeilBlackPhoto = bVeilOpeningPhoto = false;
        VeilStart = Character->GetActorLocation();
        VeilBlackPawn.Reset();
        VeilMoved = VeilBlackDrift = VeilDestinationDrift = VeilBlockedInput = 0.;
        bVeilMoving = bFirstTrip;
        if (bFirstTrip)
        {
            // Choose a clear direction with short ground support at this actual chest/arena location.
            const FVector Feet = VeilStart - FVector(0,0,Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()-20.);
            FCollisionQueryParams Params(SCENE_QUERY_STAT(IslandVeilMovement),false,Character);
            double Best = -1.;
            FRotator Look = Player.GetControlRotation();
            for (int32 I=0; I<16; ++I)
            {
                const FRotator Yaw(0., I * 22.5, 0.);
                const FVector Dir = Yaw.Vector();
                FHitResult Wall, Ground;
                const bool bWall = GetWorld()->SweepSingleByChannel(Wall,Feet,Feet+Dir*125.,FQuat::Identity,
                    ECC_Visibility,FCollisionShape::MakeSphere(15.),Params);
                const FVector Ahead = Feet + Dir*60.;
                const bool bFloor = GetWorld()->LineTraceSingleByChannel(Ground,Ahead+FVector(0.,0.,50.),
                    Ahead-FVector(0.,0.,85.),ECC_Visibility,Params) && Ground.ImpactNormal.Z > .65;
                const double Free = (bWall ? Wall.Distance : 125.) + (bFloor ? 150. : 0.);
                if (Free>Best) { Best=Free; Look.Yaw=Yaw.Yaw; }
            }
            Player.SetControlRotation(Look);
            Player.InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::W,IE_Pressed,1.f));
        }
    }
    const auto Photo = [&](bool& bTaken, const TCHAR* Enter, const TCHAR* Leave)
    {
        if (!bFirstTrip || !bShots || bTaken) return;
        FScreenshotRequest::RequestScreenshot(VenuePhotoPath(View.bReturning ? Leave : Enter), true, false);
        bTaken = true;
    };
    const bool bIsDestination = View.bReturning ? Player.GetHero()!=nullptr : Cast<APartyVoxelPawn>(Character)!=nullptr;
    if (View.Phase == EIslandPartyVeil::Closing)
    {
        bVeilLockedEarly |= Player.IsPartyTravelLocked() || bIsDestination;
        VeilMoved = FMath::Max(VeilMoved,FVector::Dist2D(VeilStart,Character->GetActorLocation()));
        if (bVeilMoving && VeilMoved >= 35.)
        {
            // Let the last accepted movement packet settle before testing a fresh input against the black gate.
            Player.InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::W,IE_Released,0.f));
            bVeilMoving = false;
        }
        if (bVeilMoving && Player.GetHero()) Player.ApplyMove(FVector2D(0.,1.));
        if (View.Coverage >= .48f) Photo(bVeilClosingPhoto,TEXT("PartyPull.png"),TEXT("TransitionOutClosing.png"));
    }
    else if (View.Phase == EIslandPartyVeil::Black)
    {
        if (!bVeilBlack && bFirstTrip && !bVeilMoving)
        {
            Player.InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::W,IE_Pressed,1.f));
            bVeilMoving = true;
        }
        bVeilBlack = true;
        bVeilUnlockedBlack |= !Player.IsPartyTravelLocked() || !Player.IsMoveInputIgnored() || View.Coverage < .999f;
        if (bIsDestination) bVeilSawSwap = true;
        if (VeilBlackPawn.Get() != Character)
        {
            VeilBlackPawn = Character;
            VeilBlackPosition = Character->GetActorLocation();
        }
        else
        {
            double& Drift = bIsDestination ? VeilDestinationDrift : VeilBlackDrift;
            Drift = FMath::Max(Drift, FVector::Dist2D(VeilBlackPosition,Character->GetActorLocation()));
        }
        // Submit the same input while covered, including on the newly possessed island character.
        const FVector Pending = Character->GetPendingMovementInputVector();
        if (Player.GetHero()) Player.ApplyMove(FVector2D(0.,1.));
        else Character->AddMovementInput(Character->GetActorForwardVector());
        VeilBlockedInput = FMath::Max(VeilBlockedInput,(Character->GetPendingMovementInputVector()-Pending).Size());
        Photo(bVeilBlackPhoto,TEXT("TransitionInBlack.png"),TEXT("TransitionOutBlack.png"));
    }
    else if (View.Phase == EIslandPartyVeil::Opening && bIsDestination)
    {
        // Possession and reveal can be observed in the same client tick. Its preceding frame must have been black.
        if (bVeilBlack && View.Coverage >= .85f) bVeilSawSwap = true;
        if (!bVeilLogged)
        {
            bVeilLogged = true;
            if (bFirstTrip) Player.InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::W,IE_Released,0.f));
            UE_LOG(LogTemp, Display, TEXT("VEIL_PROOF round=%u direction=%s moved=%.1f early=%d black=%d unlocked=%d drift=%.1f swap=%d destination_correction=%.1f black_input=%.3f"),
                View.Round, View.bReturning ? TEXT("out") : TEXT("in"), VeilMoved, bVeilLockedEarly,
                bVeilBlack, bVeilUnlockedBlack, VeilBlackDrift, bVeilSawSwap, VeilDestinationDrift, VeilBlockedInput);
        }
        if (View.Coverage <= .45f) Photo(bVeilOpeningPhoto,TEXT("TransitionInArrival.png"),TEXT("TransitionOutArrival.png"));
    }
}

void AIslandProbePeer::ClientInspectArenaView_Implementation(int32 Round, FName Feature)
{
    ViewFeature = Feature;
    ViewRound = Round;
    ViewStep = EViewStep::Photo;
    ViewsMeasured = 0;
    SceneView = GetOwner<AIslandPlayerController>()->GetControlRotation();
    ViewNext = FPlatformTime::Seconds() + .25;
}

void AIslandProbePeer::ServerArenaViewReady_Implementation(int32 Round)
{
    ReadyArenaViewRound = Round;
}

void AIslandProbePeer::TickArenaViews(AIslandPlayerController& Player)
{
    const auto* Round = GetWorld()->GetGameState<AIslandGameState>();
    const double Now = FPlatformTime::Seconds();
    if (bViewKeyDown)
    {
        Player.InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::V, IE_Released, 0.f));
        bViewKeyDown = false;
    }
    if (!Round || Now < ViewNext || Player.IsPartyTravelLocked()) return;
    const auto Toggle = [&]()
    {
        Player.InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::V, IE_Pressed, 1.f));
        bViewKeyDown = true;
        bViewFirst = !bViewFirst;
        ViewNext = Now + .35;
    };
    if (Round->Phase == EIslandPhase::Hunting && Player.GetHero() && Player.GetViewTarget() == Player.GetHero())
    {
        if (ViewStep == EViewStep::Idle && !Player.HasAuthority() && !bSetGuestView && Player.IsVoxelInputEnabled())
        {
            bSetGuestView = true;
            Toggle(); // Start the guest in a different view from the host.
        }
        else if (ViewStep == EViewStep::Return)
        {
            if (Player.GetHero()->IsInFirstPersonView() != bViewFirst) return;
            UE_LOG(LogTemp,Display,TEXT("ARENA_VIEW_RETURN round=%d first=%d"),ViewRound,bViewFirst);
            ViewStep = EViewStep::Idle;
        }
        return;
    }
    auto* Pawn = Cast<APartyVoxelPawn>(Player.GetPawn());
    if (!Pawn || !Pawn->GetSession() || Player.GetViewTarget() != Pawn
        || Pawn->GetSession()->GetPhase() != EPartyVoxelPhase::Playing || Player.GetPartyTransitionView().Phase != EIslandPartyVeil::Clear) return;
    if (ViewStep == EViewStep::Toggle)
    {
        Toggle();
        ViewStep = EViewStep::Photo;
        return;
    }
    if (ViewStep == EViewStep::LookAtFloor)
    {
        if (FScreenshotRequest::IsScreenshotRequested()) return;
        Player.SetControlRotation(FRotator(-47., SceneView.Yaw, 0.));
        ViewNext = Now + .30;
        ViewStep = EViewStep::Aim;
        return;
    }
    if (ViewStep != EViewStep::Photo && ViewStep != EViewStep::Aim) return;
    FVector Camera;
    FRotator Rotation;
    Player.GetPlayerViewPoint(Camera, Rotation);
    const double Gap = FVector::Dist(Camera, Pawn->GetPawnViewLocation());
    if (Pawn->IsInFirstPersonView() != bViewFirst || (bViewFirst ? Gap > 8. : Gap < 75.)) return;
    if (ViewStep == EViewStep::Photo)
    {
        if (FScreenshotRequest::IsScreenshotRequested()) return;
        if (FParse::Param(FCommandLine::Get(),TEXT("IslandScreenshot")))
        {
            const FString Name = FString::Printf(TEXT("%s-R%d-%s.png"),*ViewFeature.ToString(),ViewRound,bViewFirst ? TEXT("First") : TEXT("Third"));
            FScreenshotRequest::RequestScreenshot(VenuePhotoPath(*Name),true,false);
        }
        bool bSuitVisible = false;
        int32 Paws = 0;
        TInlineComponentArray<UStaticMeshComponent*> Meshes(Pawn);
        for (const auto* Part : Meshes)
        {
            if (Part->GetFName() == TEXT("Suit")) bSuitVisible = Part->IsVisible() && !Part->bOwnerNoSee;
            if (Part->bOnlyOwnerSee && Part->GetName().StartsWith(TEXT("FirstPerson")) && Part->IsVisible()) ++Paws;
        }
        UE_LOG(LogTemp,Display,TEXT("ARENA_VIEW round=%d mode=%s gap_cm=%.2f body=%d paws=%d"),
            ViewRound,bViewFirst ? TEXT("first") : TEXT("third"),Gap,bSuitVisible,Paws);
        ViewStep = EViewStep::LookAtFloor;
        return;
    }
    FHitResult Hit;
    const FCollisionQueryParams Query(SCENE_QUERY_STAT(PartyCameraTest), false, Pawn);
    FHitResult ToolHit;
    const auto* Arena = Pawn->GetSession();
    const bool bTargets = GetWorld()->LineTraceSingleByChannel(Hit,Camera,Camera+Rotation.Vector()*1800.,ECC_Visibility,Query)
        && Hit.GetActor()==Arena && Pawn->AimTerrain(ToolHit)
        && FVector::Dist(Hit.ImpactPoint,ToolHit.ImpactPoint) < 3.;
    UE_LOG(LogTemp,Display,TEXT("ARENA_VIEW_TARGET round=%d first=%d matched=%d"),ViewRound,bViewFirst,bTargets);
    Player.SetControlRotation(SceneView);
    ++ViewsMeasured;
    ViewNext = Now + .20;
    if (ViewsMeasured < 2) ViewStep = EViewStep::Toggle;
    else
    {
        ViewStep = EViewStep::Return;
        ServerArenaViewReady(ViewRound);
    }
}

void AIslandProbePeer::ClientSampleArenaTerrain_Implementation(int32 Round, FName Feature, int32 Stage, FVector Surface, int32 Sequence)
{
    TerrainSampleRound = Round;
    TerrainSampleGame = Feature;
    TerrainSampleStage = Stage;
    TerrainSampleOrigin = Surface;
    TerrainSampleSequence = Sequence;
    TerrainPhotoAt = 0.;
    bTerrainPhotoRequested = false;
    TerrainPhotoFile.Reset();
    if (!Stage)
        if (const auto* Pawn = Cast<APartyVoxelPawn>(GetOwner<AIslandPlayerController>()->GetPawn()))
            bTerrainOriginallyFirst = Pawn->IsInFirstPersonView();
}

void AIslandProbePeer::ServerSampledArenaTerrain_Implementation(int32 Round, int32 Stage)
{
    SampledTerrainRound = Round;
    SampledTerrainStage = Stage;
}

void AIslandProbePeer::TickArenaTerrainSample(AIslandPlayerController& Player)
{
    auto* Pawn = Cast<APartyVoxelPawn>(Player.GetPawn());
    const auto* Arena = Pawn ? Pawn->GetSession() : nullptr;
    if (!Arena || Arena->GetAppliedEditCount() != TerrainSampleSequence || Player.GetViewTarget() != Pawn) return;
    if (Player.GetPartyTransitionView().Phase != EIslandPartyVeil::Clear) return;
    const bool bPhotos = FParse::Param(FCommandLine::Get(),TEXT("IslandScreenshot"));
    const auto ToggleView = [&]()
    {
        Player.InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::V,IE_Pressed,1.f));
        Player.InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::V,IE_Released,0.f));
    };
    if (bTerrainPhotoRequested)
    {
        if (FScreenshotRequest::IsScreenshotRequested() || IFileManager::Get().FileSize(*TerrainPhotoFile)<=0) return;
        if (TerrainSampleStage == 3 && !bTerrainOriginallyFirst) ToggleView();
        ServerSampledArenaTerrain(TerrainSampleRound,TerrainSampleStage);
        TerrainSampleStage = INDEX_NONE;
        return;
    }
    if (bPhotos && !Pawn->IsInFirstPersonView())
    {
        ToggleView();
        return;
    }
    const FRotator Desired = (TerrainSampleOrigin-Pawn->GetPawnViewLocation()).Rotation();
    if (TerrainPhotoAt == 0.)
    {
        Player.SetControlRotation(Desired);
        TerrainPhotoAt = FPlatformTime::Seconds() + .35;
        return;
    }
    FVector Camera;
    FRotator Actual;
    Player.GetPlayerViewPoint(Camera,Actual);
    if (bPhotos && (FVector::Dist(Camera,Pawn->GetPawnViewLocation())>8. || !Actual.Equals(Desired,1.)))
    {
        Player.SetControlRotation(Desired);
        return;
    }
    if (FPlatformTime::Seconds() < TerrainPhotoAt || FScreenshotRequest::IsScreenshotRequested()) return;
    TArray<FString> Heights, Normals;
    for (double Radius : ProfileRadii)
    {
        const FVector Probe = TerrainSampleOrigin + FVector(0.,Radius,0.);
        FHitResult Hit;
        if (!SampleFloor(*Arena, Probe, Hit)) return;
        Heights.Add(FString::FromInt(FMath::RoundToInt((Hit.ImpactPoint.Z - TerrainSampleOrigin.Z)*10.)));
        Normals.Add(FString::FromInt(FMath::RoundToInt(Hit.ImpactNormal.Z*1000.)));
    }
    UE_LOG(LogTemp, Display, TEXT("ARENA_TERRAIN_PROFILE round=%d game=%s stage=%d seq=%d height_mm=%s normals=%s origin_mm=%d"),
        TerrainSampleRound,*TerrainSampleGame.ToString(),TerrainSampleStage,TerrainSampleSequence,
        *FString::Join(Heights,TEXT(",")),*FString::Join(Normals,TEXT(",")),
        FMath::RoundToInt((TerrainSampleOrigin.Z-Arena->GetActorLocation().Z)*10.));
    if (bPhotos && TerrainSampleStage != 2)
    {
        const TCHAR* Name[] = {TEXT("Before"),TEXT("Cut"),TEXT("Refill"),TEXT("Mound")};
        const FString File = FString::Printf(TEXT("%s-R%d-%s.png"),*TerrainSampleGame.ToString(),TerrainSampleRound,Name[TerrainSampleStage]);
        TerrainPhotoFile = VenuePhotoPath(*File);
        bTerrainPhotoRequested = true;
        FScreenshotRequest::RequestScreenshot(TerrainPhotoFile,true,false);
        return;
    }
    ServerSampledArenaTerrain(TerrainSampleRound,TerrainSampleStage);
    TerrainSampleStage = INDEX_NONE;
}

bool AIslandProbe::TickArenaTerrain(APartyVoxelSession& Arena)
{
    if (TerrainStage > 3) return true;
    const int32 Round = GameCount + 1;
    auto* Pawn = CastChecked<APartyVoxelPawn>(Mode->GetPlayers()[0]->GetPawn());
    if (!bTerrainSampleRequested)
    {
        if (TerrainStage == 0)
        {
            // A point off both authoring and sample grid centers, in reach of the normal camera.
            const FVector Near = Pawn->GetActorLocation() + FVector(174.,13.,-Pawn->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
            FHitResult Surface;
            if (!SampleFloor(Arena, Near, Surface)) return false;
            TerrainSamplePoint = Surface.ImpactPoint;
            TerrainObserverPoses.Reset();
            if (FParse::Param(FCommandLine::Get(),TEXT("IslandScreenshot")))
                for (int32 Seat=1; Seat<Peers.Num(); ++Seat)
                {
                    auto* Observer = CastChecked<APartyVoxelPawn>(Mode->GetPlayers()[Seat]->GetPawn());
                    const FVector NearObserver = TerrainSamplePoint + FVector(-141.,72.+76.*Seat,0.);
                    FHitResult Floor;
                    if (!SampleFloor(Arena,NearObserver,Floor)) continue;
                    TerrainObserverPoses.Add(Seat,Observer->GetActorTransform());
                    Observer->TeleportTo(Floor.ImpactPoint+FVector(0,0,Observer->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()+2.),
                        Observer->GetActorRotation(),false,true);
                    Observer->GetCharacterMovement()->StopMovementImmediately();
                    Observer->ForceNetUpdate();
                }
            Require(Arena.GetAppliedEditCount()==0,TEXT("reentering an arena starts with freshly generated terrain"));
        }
        else if (Arena.GetAppliedEditCount() == TerrainInputSequence)
        {
            FHitResult Surface;
            if (SampleFloor(Arena,TerrainSamplePoint,Surface)) Peers[0]->ClientGameTool(Surface.ImpactPoint,TerrainStage != 1);
            return false;
        }
        Require(Arena.GetAppliedEditCount() == TerrainStage,TEXT("one real mouse click applies one density edit"));
        for (const auto& Peer : Peers)
            Peer->ClientSampleArenaTerrain(Round, Mode->CurrentPartyFeature, TerrainStage, TerrainSamplePoint, Arena.GetAppliedEditCount());
        bTerrainSampleRequested = true;
        return false;
    }
    for (const auto& Peer : Peers)
        if (Peer->SampledTerrainRound != Round || Peer->SampledTerrainStage != TerrainStage) return false;
    if (TerrainStage == 1)
    {
        // The physical floor cut above also covers the original stone-foundation regression.
        bStoneDug = true;
        UE_LOG(LogTemp,Display,TEXT("EXPEDITION_STONE_DUG feature=%s point=%s"),*Mode->CurrentPartyFeature.ToString(),*TerrainSamplePoint.ToCompactString());
    }
    if (TerrainStage == 3)
        for (const auto& Entry : TerrainObserverPoses)
        {
            APawn* Observer = Mode->GetPlayers()[Entry.Key]->GetPawn();
            Observer->TeleportTo(Entry.Value.GetLocation(),Entry.Value.Rotator(),false,true);
            Observer->ForceNetUpdate();
        }
    TerrainInputSequence = Arena.GetAppliedEditCount();
    ++TerrainStage;
    bTerrainSampleRequested = false;
    return TerrainStage > 3;
}

void AIslandProbePeer::ClientGameTool_Implementation(FVector Target, bool bPlace)
{
    GameToolTarget = Target;
    bGameToolPlaces = bPlace;
}

void AIslandProbePeer::TickGameTool(AIslandPlayerController& Player)
{
    auto* Pawn = Cast<APartyVoxelPawn>(Player.GetPawn());
    if (!Pawn || !Pawn->GetSession() || Pawn->GetSession()->GetPhase()!=EPartyVoxelPhase::Playing)
    {
        GameToolTarget.Reset();
        return;
    }
    if (Player.GetViewTarget()!=Pawn) return;
    if (Pawn->GetSession()->IsChaseGame() && !Pawn->IsInFirstPersonView())
    {
        Player.InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::V, IE_Pressed, 1.f));
        Player.InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::V, IE_Released, 0.f));
        return;
    }
    FVector Camera;
    FRotator View;
    Player.GetPlayerViewPoint(Camera,View);
    const FRotator Desired = (GameToolTarget.GetValue()-Camera).Rotation();
    const FRotator Delta = (Desired-View).GetNormalized();
    if (FMath::Abs(Delta.Pitch)>.16 || FMath::Abs(Delta.Yaw)>.16)
    {
        Player.SetControlRotation((Player.GetControlRotation()+Delta).GetNormalized());
        return;
    }
    FHitResult Surface;
    const bool bHit = Pawn->AimTerrain(Surface);
    const FIntVector Cell = bHit ? Pawn->GetSession()->CellAt(Surface.ImpactPoint-Surface.ImpactNormal*2.) : FIntVector::ZeroValue;
    UE_LOG(LogTemp, Display, TEXT("EXPEDITION_GAME_TOOL seat=%d place=%d hit=%d cell=%s view=%s"),
        Pawn->GetSeat(), bGameToolPlaces, bHit, *Cell.ToString(), *Camera.ToCompactString());
    if (Pawn->GetSession()->IsChaseGame())
    {
        const EPartyChaseAim Aim = Pawn->GetSession()->GetChaseAim(*Pawn,Pawn->GetPawnViewLocation(),Pawn->GetToolAimDirection());
        UE_LOG(LogTemp, Display, TEXT("MAZE_QA_TOOL seed=%d place=%d target=%s"), Pawn->GetSession()->ArenaSeed(),
            bGameToolPlaces, Aim == EPartyChaseAim::Wall ? TEXT("wall") : Aim == EPartyChaseAim::Gap ? TEXT("gap") :
            Aim == EPartyChaseAim::Runner ? TEXT("runner") : TEXT("none"));
    }
    HeldArenaKey = bGameToolPlaces ? EKeys::RightMouseButton : EKeys::LeftMouseButton;
    ReleaseArenaKeyAt = FPlatformTime::Seconds() + .12;
    Player.InputKey(FInputKeyEventArgs::CreateSimulated(HeldArenaKey, IE_Pressed, 1.f));
    GameToolTarget.Reset();
}

void AIslandProbePeer::ClientGameWalk_Implementation(FVector Destination)
{
    GameWalkTarget = Destination;
}

void AIslandProbePeer::ClientInspectVenue_Implementation(int32 Round, int32 ExpectedGone, int32 ExpectedParts)
{
    const auto* PC = CastChecked<AIslandPlayerController>(GetOwner());
    const auto* Pawn = Cast<APartyVoxelPawn>(PC->GetPawn());
    const auto* Arena = Pawn ? Pawn->GetSession() : nullptr;
    if (!Arena || Arena->GetVenueStructureCount() != VenueBuildingCount) return;
    int32 Pieces = 0;
    for (int32 Id = 0; Id < VenueBuildingCount; ++Id)
    {
        const bool bGone = (ExpectedGone & (1 << Id)) != 0;
        if (Arena->IsVenueStructureDestroyed(Id) != bGone) return;
        TArray<UStaticMeshComponent*> Parts;
        Arena->GetVenueStructureParts(Id, Parts);
        if (Parts.Num() != ExpectedParts) return;
        for (const auto* Part : Parts)
        {
            if (!Part || !Part->IsRegistered() || !Part->GetStaticMesh() || Part->IsVisible() == bGone) return;
            if (bGone ? Part->GetCollisionEnabled() != ECollisionEnabled::NoCollision :
                Part->GetCollisionEnabled() != ECollisionEnabled::QueryOnly ||
                Part->GetCollisionResponseToChannel(ECC_Visibility) != ECR_Block) return;
        }
        Pieces += Parts.Num();
    }
    UE_LOG(LogTemp, Display, TEXT("EXPEDITION_VENUE_VIEW round=%d gone=%d pieces=%d seat=%d"),
        Round, ExpectedGone, Pieces, Pawn->GetSeat());
    ServerVenueReport(Round, ExpectedGone, Pieces);
}

void AIslandProbePeer::ServerVenueReport_Implementation(int32 Round, int32 Gone, int32 Pieces)
{
    if (auto* Probe = Coordinator.Get()) Probe->RecordVenueReport(Seat, Round, Gone, Pieces);
}

void AIslandProbe::RecordVenueReport(int32 Seat, int32 Round, int32 Gone, int32 Pieces)
{
    if (Round != GameCount + 1 || Gone != ExpectedVenueGone || !bGameStarted ||
        Pieces != VenueBuildingCount * PartsPerBuilding(Mode->CurrentPartyFeature)) return;
    VenueReports.Add(Seat);
}

bool AIslandProbe::CollectVenueReports(int32 Gone)
{
    if (ExpectedVenueGone != Gone)
    {
        ExpectedVenueGone = Gone;
        VenueReports.Reset();
    }
    for (int32 Seat = 0; Seat < Mode->GetPlayers().Num(); ++Seat)
        if (!VenueReports.Contains(Seat)) Peers[Seat]->ClientInspectVenue(GameCount + 1, Gone, PartsPerBuilding(Mode->CurrentPartyFeature));
    return VenueReports.Num() == Mode->GetPlayers().Num();
}

void AIslandProbePeer::TickGameWalk()
{
    const auto* PC = Cast<AIslandPlayerController>(GetOwner());
    auto* Pawn = PC ? Cast<APartyVoxelPawn>(PC->GetPawn()) : nullptr;
    if (!Pawn || FVector::Dist2D(*GameWalkTarget, Pawn->GetActorLocation()) < 15.)
    {
        GameWalkTarget.Reset();
        return;
    }
    Pawn->AddMovementInput((*GameWalkTarget - Pawn->GetActorLocation()).GetSafeNormal2D());
}

void AIslandProbePeer::ClientGameReceipt_Implementation(int32 Round, bool bVerifyRepeatedClaim)
{
    const auto* Player = CastChecked<AIslandPlayerController>(GetOwner());
    const auto* GS = GetWorld()->GetGameState<AIslandGameState>();
    const auto* Self = Player->GetPlayerState<AIslandPlayerState>();
    if (!GS || !Self || !Player->GetHero() || GS->Phase != EIslandPhase::Hunting) return;
    if (bVerifyRepeatedClaim)
    {
        const auto* Chest = Player->GetReachableChest();
        if (!Chest || !Chest->IsChallengeResolved() || Player->GetNotice() != TEXT("本队已取得这个箱子的碎片")) return;
    }
    const FIslandClues& Clues = Player->GetLocalClues();
    uint8 Coordinates = 0;
    for (int32 Index = 0; Index < Clues.Points.Num(); ++Index)
        if (!Clues.Points[Index].IsNearlyZero()) Coordinates |= 1u << Index;
    UE_LOG(LogTemp, Display, TEXT("%s round=%d team=%d mask=%d points=%d key=%d outcome=%d scouted=%d coordinates=%d fragments=%d crossing=%d"),
        Round ? TEXT("EXPEDITION_GAME_RECEIPT") : TEXT("EXPEDITION_REVISIT_RECEIPT"),
        Round, Self->Team, Clues.Mask, Clues.Count(), Clues.HasKey(), int32(GS->PartyOutcome), Clues.ScoutedMask,
        Coordinates, Clues.FragmentCount(), !Clues.Crossing.IsNearlyZero());
    ServerGameReceipt(Round, Clues, uint8(GS->PartyOutcome), GS->IsIdentityDisguised());
}

void AIslandProbePeer::ServerGameReceipt_Implementation(int32 Round, const FIslandClues& Clues, uint8 Outcome, bool bDisguised)
{
    if (auto* Probe = Coordinator.Get()) Probe->RecordGameReceipt(Seat, Round, Clues, Outcome, bDisguised);
}

void AIslandProbe::RecordGameReceipt(int32 Seat, int32 Round, const FIslandClues& Clues, uint8 Outcome, bool bDisguised)
{
    if (!Mode->GetPlayers().IsValidIndex(Seat)) return;
    if (bTestingChestRevisit ? Round != 0 : (!bGameStarted || Round != GameCount + 1)) return;
    const auto* State = Mode->GetGameState<AIslandGameState>();
    const auto* Member = Mode->GetPlayers()[Seat]->GetPlayerState<AIslandPlayerState>();
    const auto& Expected = Mode->TeamClues[Member->Team];
    // HUD state and private progress travel on different channels. Give the last update time to arrive.
    if (Outcome != uint8(State->PartyOutcome) || Clues.Mask != Expected.Mask || Clues.ScoutedMask != Expected.ScoutedMask ||
        Clues.bKeySpent != Expected.bKeySpent || Clues.bUsesKeyFragments != Expected.bUsesKeyFragments || Clues.HasKey() != Expected.HasKey() || Clues.Points.Num() != IslandRules::ClueCount ||
        bDisguised != State->IsIdentityDisguised()) return;
    for (int32 Index = 0; Index < IslandRules::ClueCount; ++Index)
    {
        const FVector2D ExpectedPoint = ((Clues.Mask | Clues.ScoutedMask) & (1u << Index)) ?
            FVector2D(Mode->HiddenLayout.GetCenter(Index)) : FVector2D::ZeroVector;
        Require(Clues.Points[Index].Equals(ExpectedPoint, .02), TEXT("local exact positions match only this crew's awarded or scouted chests"));
    }
    Require(Clues.Crossing.Equals(Expected.Crossing, .02), TEXT("client chart has the team's correct vault information"));
    Reports.Add(Seat);
}

void AIslandProbe::StartNextGame()
{
    const auto& Players = Mode->GetPlayers();
    ExpeditionOpener = 0;
    int32 Chest = INDEX_NONE;
    const int32 UniqueChests = IslandRules::ClueCount + Mode->HiddenLayout.Decoys.Num();
    if (GameCount == 1 && FParse::Param(FCommandLine::Get(), TEXT("ExpeditionMazeQA")))
        Chest = IslandRules::PrizeIndex + 1;
    else if (!OnlyGame.IsNone() && GameCount == 0)
    {
        const bool bFalseFirst=FParse::Param(FCommandLine::Get(),TEXT("ExpeditionIdentityShots"))
            || FParse::Param(FCommandLine::Get(),TEXT("ExpeditionInteractionQA"))
            || FParse::Param(FCommandLine::Get(),TEXT("ExpeditionAbortQA"));
        if (bFalseFirst && Mode->HiddenLayout.Decoys.IsEmpty())
        {
            Require(false,TEXT("false-chest interaction capture needs at least one additional decoy"));
            return;
        }
        Chest=bFalseFirst ? IslandRules::PrizeIndex+1 : 0;
    }
    else if (OnlyGame.IsNone() && GameCount < UniqueChests)
        Chest = GameCount < IslandRules::ClueCount ? GameCount : GameCount + 1;
    else if (!bTestingChestRevisit && (GameCount == UniqueChests || (!OnlyGame.IsNone() && GameCount == 1)))
    {
        // Another crew comes back for a true chest already known to the host.
        for (int32 Seat = 1; Seat < Players.Num() && Chest == INDEX_NONE; ++Seat)
        {
            const int32 Team = Players[Seat]->GetPlayerState<AIslandPlayerState>()->Team;
            for (int32 I = 0; I < IslandRules::ClueCount; ++I)
                if ((Mode->TeamClues[Team].ScoutedMask & (1u << I)) && IsValid(Mode->Exposed[I]) &&
                    Mode->Exposed[I]->IsChallengeResolved() && Mode->CanClaimChest(Team, I))
                {
                    ExpeditionOpener = Seat;
                    Chest = I;
                    break;
                }
            for (int32 I = 0; Chest == INDEX_NONE && I < IslandRules::ClueCount; ++I)
                if (IsValid(Mode->Exposed[I]) && Mode->Exposed[I]->IsChallengeResolved() && Mode->CanClaimChest(Team, I))
                {
                    ExpeditionOpener = Seat;
                    Chest = I;
                    break;
                }
        }
        bTestingChestRevisit = Chest != INDEX_NONE;
    }
    if (Chest == INDEX_NONE)
    {
        Require(!bGamesDrop || Players.Num() == ExpeditionPlayers - 1, TEXT("remaining players can finish after a disconnect"));
        if (OnlyGame.IsNone() && FParse::Param(FCommandLine::Get(),TEXT("ExpeditionInteractionQA")))
        {
            Require(Mode->CanClaimChest(Players[0]->GetPlayerState<AIslandPlayerState>()->Team,IslandRules::PrizeIndex),
                TEXT("interaction run reached the real vault with four points and a crafted key"));
            ExpeditionOpener = 0;
            RoomCheck = ERoomCheck::Dig;
            PrepareDig(IslandRules::PrizeIndex);
            return;
        }
        UE_LOG(LogTemp, Display, TEXT("EXPEDITION_GAMES_DONE rounds=%d players=%d"), GameCount, Players.Num());
        Advance(EStep::Done);
        return;
    }
    RoomCheck = ERoomCheck::Dig;
    PrepareDig(Chest);
}

void AIslandProbe::TickChestRevisit()
{
    const auto* State = Mode->GetGameState<AIslandGameState>();
    Require(State->Phase == EIslandPhase::Hunting && Mode->PartyRoundId == uint32(GameCount) && !Mode->PartySession,
        TEXT("revisiting a settled genuine chest keeps the room on the island without starting another game"));
    const auto& Players = Mode->GetPlayers();
    const int32 Team = Players[ExpeditionOpener]->GetPlayerState<AIslandPlayerState>()->Team;
    const uint8 Bit = 1u << NextDig;
    const auto& Before = GamePreviousClues.FindChecked(Team);
    const auto& After = Mode->TeamClues[Team];
    if (!(After.Mask & Bit))
    {
        Peers[ExpeditionOpener]->ClientOpenChest();
        return;
    }
    if (GameAction == 0)
    {
        Require(!(Before.Mask & Bit) && !Mode->CanClaimChest(Team, NextDig),
            TEXT("the new claimant can receive this site's fragment only once"));
        Peers[ExpeditionOpener]->ClientOpenChest();
        ++GameAction;
        return;
    }
    for (int32 Seat = 0; Seat < Players.Num(); ++Seat)
    {
        const int32 Crew = Players[Seat]->GetPlayerState<AIslandPlayerState>()->Team;
        const auto& Old = GamePreviousClues.FindChecked(Crew);
        const uint8 Added = Crew == Team ? Bit : 0;
        const auto& Current = Mode->TeamClues[Crew];
        Require(Current.Mask == (Old.Mask | Added) && Current.ScoutedMask == (Old.ScoutedMask & ~Added)
            && Players[Seat]->GetPawn() == OriginalPawns[Seat].Get(),
            TEXT("a direct pickup changes only the claiming crew's progress and leaves every island pawn in place"));
        if (!Reports.Contains(Seat)) Peers[Seat]->ClientGameReceipt(0, Seat == ExpeditionOpener);
    }
    if (Reports.Num() != Players.Num()) return;
    UE_LOG(LogTemp, Display, TEXT("EXPEDITION_CHEST_REVISIT site=%d team=%d before=%d after=%d party_round=%u"),
        NextDig, Team, Before.Mask, After.Mask, Mode->PartyRoundId);
    StartNextGame();
}

void AIslandProbe::TickExpeditionGame(double Now)
{
    const auto* Round = Mode->GetGameState<AIslandGameState>();
    const bool bShots = FParse::Param(FCommandLine::Get(), TEXT("IslandScreenshot"));
    const bool bInteraction = FParse::Param(FCommandLine::Get(), TEXT("ExpeditionInteractionQA"));
    const bool bAbort = FParse::Param(FCommandLine::Get(), TEXT("ExpeditionAbortQA"));
    const bool bViews = FParse::Param(FCommandLine::Get(), TEXT("ExpeditionArenaViewsQA"));
    const bool bTerrain = FParse::Param(FCommandLine::Get(), TEXT("ExpeditionArenaTerrainQA"));
    const bool bMazeProof = FParse::Param(FCommandLine::Get(), TEXT("ExpeditionMazeQA"));
    if (bShots && !bInteraction && GameCount == 0 && !bIssued && Round->Phase == EIslandPhase::PartyLoading
        && Round->GetServerWorldTimeSeconds() - Round->PartyStartedAt >= .42)
    {
        FString Dir;
        FParse::Value(FCommandLine::Get(), TEXT("FieldShotsDir="), Dir);
        FScreenshotRequest::RequestScreenshot(FPaths::Combine(Dir, TEXT("PartyPull.png")), true, false);
        bIssued = true;
    }
    if (Now - LastAction < .45) return;
    LastAction = Now;
    if (bTestingChestRevisit)
    {
        TickChestRevisit();
        return;
    }
    const auto& Players = Mode->GetPlayers();
    auto* Session = Cast<APartyVoxelSession>(Mode->PartySession);
    if (Session && !bGameStarted)
    {
        if (Session->IsChaseGame())
        {
            if (!bGameStarted && Session->RoundSeconds == 180.f)
                UE_LOG(LogTemp, Display, TEXT("MAZE_QA_DEFAULT limit=180"));
            if (!(bMazeProof && bShots && GameCount == 0)) Session->RoundSeconds = bMazeProof ? 34.f : 8.f;
        }
        else Session->RoundSeconds = bAllVenueBuildings || bTerrain ? 44.f : (bVenueProof || bViews) ? 26.f : 23.f;
    }
    if (Round->Phase == EIslandPhase::PartyPlaying && Session)
    {
        if (!bGameStarted)
        {
            bGameStarted = true;
            if (bViews)
                for (const auto& Peer : Peers) Peer->ClientInspectArenaView(GameCount+1, Mode->CurrentPartyFeature);
            VenueStep = EVenueStep::Entry;
            VenueReports.Reset();
            ExpectedVenueGone = 0;
            VenueId = 0;
            GameWinnerSeat = (bGamesDrop || GameCount >= IslandRules::ClueCount) ? 0 : GameCount % Players.Num();
            if (bInteraction && !bGamesDrop && OnlyGame.IsNone() && Players.Num() >= 3)
            {
                // Keep the third crew blind and let the second crew win the first decoy.
                if (GameCount == 2) GameWinnerSeat = 0;
                if (GameCount == IslandRules::ClueCount) GameWinnerSeat = 1;
            }
            Require(Round->PartyOutcome == EIslandChestOutcome::Unknown && Mode->PartyRoundId == uint32(GameCount + (bAbort ? 2 : 1))
                && Session->GetActorLocation().Equals(APartyVoxelSession::ArenaOrigin(), 1.), TEXT("all players entered the selected remote arena once"));
            for (AVoxelPlayerController* Player : Players)
                Require(Cast<APartyVoxelPawn>(Player->GetPawn()) != nullptr, TEXT("participant possesses the arena character"));
            UE_LOG(LogTemp, Display, TEXT("EXPEDITION_GAME_ENTER feature=%s round=%d peers=%d"),
                *Mode->CurrentPartyFeature.ToString(), GameCount + 1, Players.Num());
            if (bGamesDrop && GameCount == 0)
            {
                bGameDropSent = true;
                UE_LOG(LogTemp, Display, TEXT("EXPEDITION_GAMES_DROP_READY"));
            }
        }
        if (Round->PartyOutcome != EIslandChestOutcome::Unknown || Round->PartyWinnerTeam != INDEX_NONE || Round->PartyWinnerMask)
            Require(false, TEXT("early public reveal"));
        for (const auto& Entry : GamePreviousClues)
            if (Mode->TeamClues[Entry.Key].Mask != Entry.Value.Mask || Mode->TeamClues[Entry.Key].ScoutedMask != Entry.Value.ScoutedMask)
                Require(false, TEXT("a crew received clues or scouting before settlement"));
        if (Session->GetPhase() == EPartyVoxelPhase::Playing)
        {
            if (bViews && Peers.ContainsByPredicate([this](const auto& Peer){ return Peer->ReadyArenaViewRound != GameCount+1; })) return;
            if (bTerrain && !TickArenaTerrain(*Session)) return;
            if (bVenueProof && !TickVenueProof(*Session, bShots)) return;
            if (bShots && bMazeProof && GameCount == 0 &&
                !IFileManager::Get().FileExists(*VenuePhotoPath(TEXT("MazeChasePlay.png"))))
            {
                // Give the rendered HUD a few frames after the countdown while the pursuit clock reads 3:00.
                if (Session->GetHeadStartRemaining() > 7.f) return;
                if (!FScreenshotRequest::IsScreenshotRequested())
                    FScreenshotRequest::RequestScreenshot(VenuePhotoPath(TEXT("MazeChasePlay.png")), true, false);
                return;
            }
            if (bShots && !bMazeProof && GameAction == 0)
            {
                FString Dir;
                FParse::Value(FCommandLine::Get(), TEXT("FieldShotsDir="), Dir);
                FScreenshotRequest::RequestScreenshot(FPaths::Combine(Dir, Mode->CurrentPartyFeature.ToString()+TEXT(".png")), false, false);
            }
            if (Session->IsChaseGame()) PlayMazeChase(*Session);
            else PlayExpeditionGame(*Session);
            const bool bOverviewReady = Session->IsChaseGame() ? (!bMazeProof || GameCount == 0)
                : Session->Score(GameWinnerSeat) > 0;
            if (bShots && !bGameOverviewShot && bOverviewReady)
            {
                bGameOverviewShot = true;
                CaptureGameOverview(*Players[0], *Session, Mode->CurrentPartyFeature);
            }
        }
        else if (Session->GetPhase() == EPartyVoxelPhase::Results)
        {
            if (bVenueProof) Require(VenueStep == EVenueStep::Restored, TEXT("every peer inspected each removed building before settlement"));
            if (Session->IsChaseGame())
            {
                int32 Hunters = 0;
                for (const AVoxelPlayerController* Controller : Players)
                {
                    const int32 Team = Controller->GetPlayerState<AIslandPlayerState>()->Team;
                    if (Team != Session->GetRunnerTeam()) Hunters |= 1 << Team;
                }
                const bool bSurvived = Session->GetRemainingRunners() > 0;
                GameWinnerMask = bSurvived ? 1 << Session->GetRunnerTeam() : Hunters;
                Require(GameWinnerMask == Session->GetWinningTeamMask(), TEXT("the winning side includes every eligible crew"));
                if (FParse::Param(FCommandLine::Get(), TEXT("ExpeditionMazeQA")))
                    Require(GameAction >= 7 && bSurvived == (GameCount == 0),
                        TEXT("maze finished by survival in round one and full capture in round two"));
            }
            else
            {
                Require(Session->GetWinningSeat() == GameWinnerSeat && Session->Score(GameWinnerSeat) > 0,
                    TEXT("an actual positive arena score won the game"));
                GameWinnerMask = 1 << Players[GameWinnerSeat]->GetPlayerState<AIslandPlayerState>()->Team;
            }
            if (Mode->CurrentPartyFeature == TEXT("PartyBridgeDash") && Players.Num() > 1)
            {
                const int32 WaitingSeat = GameWinnerSeat == 0 ? 1 : 0;
                Require(Session->FeetCell(WaitingSeat) == FIntVector(10, 3, 1) && Session->Score(WaitingSeat) == 0,
                    TEXT("a stationary far-bank spectator received no crossing points after the bridge connected"));
            }
        }
        return;
    }
    if (Round->Phase != EIslandPhase::Hunting) return;
    if (!bGameStarted)
    {
        if (bAbort && Mode->PartyRoundId == 1)
        {
            if (!AbortRecoveredAt)
            {
                AbortRecoveredAt = Now;
                Require(Round->PartyOutcome == EIslandChestOutcome::Unknown && !Round->PartyRewardMask
                    && Round->SpentFalsePoints.IsEmpty() && !Round->IsIdentityDisguised()
                    && !Mode->TeamClues.ContainsByPredicate([](const FIslandClues& Clues) { return Clues.Mask || Clues.ScoutedMask; })
                    && Mode->CanClaimChest(Players[ExpeditionOpener]->GetPlayerState<AIslandPlayerState>()->Team, NextDig),
                    TEXT("failed loading leaves the false chest available without an effect, chart marker or reward"));
            }
            if (Now - AbortRecoveredAt < 2.) return;
        }
        Peers[ExpeditionOpener]->ClientOpenChest();
        return;
    }
    Require(!Session && Mode->PartyPlayers.IsEmpty() && Mode->GetTerrain()->GetChecksum() == GameTerrainCRC,
        TEXT("arena unloaded with island edits preserved"));
    const int32 OpenerTeam = Players[ExpeditionOpener]->GetPlayerState<AIslandPlayerState>()->Team;
    const bool bTrue = !Mode->HiddenLayout.IsDecoy(NextDig);
    const int32 Area = bTrue ? NextDig : Mode->HiddenLayout.Decoys[NextDig - IslandRules::PrizeIndex - 1].ClueIndex;
    int32 NewlyCredited = 0;
    const bool bScatter = Round->PartyOutcome == EIslandChestOutcome::Scatter;
    Require(Round->PartySite == Area && Round->PartyOpeningTeam == OpenerTeam && Round->PartyWinnerMask == GameWinnerMask
        && (bTrue ? Round->PartyOutcome == EIslandChestOutcome::Genuine : bScatter || Round->PartyOutcome == EIslandChestOutcome::Disguise),
        TEXT("only the finished minigame reveals this chest and the opening/winning crews"));
    for (int32 Seat = 0; Seat < Players.Num(); ++Seat)
    {
        const int32 Team = Players[Seat]->GetPlayerState<AIslandPlayerState>()->Team;
        const auto& Before = GamePreviousClues.FindChecked(Team);
        const uint8 Previous = Before.Mask;
        const bool bWon = (GameWinnerMask & (1 << Team)) != 0;
        const bool bAwarded = bTrue && (Team == OpenerTeam || bWon);
        const uint8 Expected = bAwarded ? Previous | (1u << NextDig) : Previous;
        const auto& Progress = Mode->TeamClues[Team];
        const uint8 NewScouts = Progress.ScoutedMask & ~Before.ScoutedMask;
        const bool bEarnsScout = !bTrue && bWon && (Before.Mask | Before.ScoutedMask) != 15;
        const uint8 CarriedScouts = Before.ScoutedMask & ~Expected;
        Require((Progress.ScoutedMask & Expected) == 0 && (Progress.ScoutedMask & ~NewScouts) == CarriedScouts
            && (bEarnsScout ? FMath::IsPowerOfTwo(uint32(NewScouts)) && NewScouts != 0 : NewScouts == 0),
            TEXT("only the decoy winner learns one new genuine location; claiming it retires the scout marker"));
        for (int32 I = 0; I < IslandRules::ClueCount; ++I)
            if (NewScouts & (1u << I))
                Require(Mode->CanClaimChest(Team, I), TEXT("a scouted genuine chest remains claimable in play"));
        if (Expected != Previous) NewlyCredited |= 1 << Team;
        Require(Progress.Mask == Expected && Progress.HasKey() == (Progress.Count() >= 3)
            && Progress.FragmentCount() == Progress.Count() % 3 && Progress.Complete() == (Progress.Count() == 4),
            TEXT("three fragments craft the key; each distinct genuine chest adds one point and a fragment"));
        if (bAwarded || !bTrue)
            Require(!Mode->CanClaimChest(Team, NextDig), TEXT("a crew cannot reclaim its own reward or a spent decoy"));
        if (!Progress.Complete())
            Require(Progress.Crossing.IsNearlyZero() && !Mode->CanClaimChest(Team, IslandRules::PrizeIndex),
                TEXT("a crew without the four private points cannot locate or open the final vault"));
        Require(Players[Seat]->GetPawn() == OriginalPawns[Seat].Get(), TEXT("participant returned to the same island pawn"));
        if (bScatter)
        {
            const FVector Location = Players[Seat]->GetPawn()->GetActorLocation();
            Require(FVector::Dist2D(GameBeforePositions[Seat], Location) > 200., TEXT("scatter moved every participant"));
            for (int32 Other = 0; Other < Seat; ++Other)
                Require(FVector::Dist2D(Players[Other]->GetPawn()->GetActorLocation(), Location) > 85., TEXT("scatter destinations are distinct"));
        }
        if (!Reports.Contains(Seat)) Peers[Seat]->ClientGameReceipt(GameCount + 1);
    }
    Require(NewlyCredited == Round->PartyRewardMask, TEXT("the published recipients are exactly the crews whose inventory changed"));
    if (Reports.Num() != Players.Num()) return;
    if (bInteraction && CastChecked<AIslandPlayerController>(Players[0])->GetPartyTransitionView().Phase != EIslandPartyVeil::Clear)
        return;
    if (bInteraction && !bTrue)
    {
        const FVector2D Spent(Mode->HiddenLayout.GetCenter(NextDig));
        Require(Round->SpentFalsePoints.ContainsByPredicate([&](const FVector2D& Point){ return Point.Equals(Spent,.02); }),
            TEXT("spent false chest is marked on the public chart"));
        if (Round->PartyOutcome == EIslandChestOutcome::Disguise)
            Require(Round->IsIdentityDisguised(), TEXT("the disguise event remains active on return"));
    }
    if (FParse::Param(FCommandLine::Get(), TEXT("ExpeditionIdentityShots")) && !CaptureExpeditionIdentity(*Mode, Players)) return;
    if (bInteraction && bShots && !bTrue && Mode->TeamClues[OpenerTeam].ScoutedMask != GamePreviousClues.FindChecked(OpenerTeam).ScoutedMask
        && Players[0]->GetPlayerState<AIslandPlayerState>()->Team == OpenerTeam && !IFileManager::Get().FileExists(*VenuePhotoPath(TEXT("TreasureIntel.png"))))
        return;
    if (bInteraction && bShots && bTrue && OnlyGame.IsNone())
    {
        const TCHAR* Needed = GameCount == 0 ? TEXT("TreasureFragment.png") : GameCount == 2 ? TEXT("TreasureForge.png") :
            GameCount == 3 ? TEXT("TreasureVault.png") : nullptr;
        if (Needed && !IFileManager::Get().FileExists(*VenuePhotoPath(Needed))) return;
    }
    if (bInteraction && bShots && !bTrue && GameAction != -1 && !IFileManager::Get().FileExists(*VenuePhotoPath(TEXT("TreasureFalseChart.png"))))
    {
        Peers[0]->ClientShowChart();
        GameAction = -1;
        LastAction = Now + .9;
        return;
    }
    if (bShots && bTrue && bGamesScenario && (GameCount == 0 || GameCount == 2 || GameCount == 3) && GameAction != -1)
    {
        Peers[0]->ClientShowChart();
        GameAction = -1;
        LastAction = Now + .6;
        return;
    }
    UE_LOG(LogTemp, Display, TEXT("EXPEDITION_GAME_RETURN round=%d feature=%s site=%d opening=%d winner=%d result=%s credited_mask=%d winners=%d"),
        GameCount + 1, *Mode->CurrentPartyFeature.ToString(), NextDig, OpenerTeam, Round->PartyWinnerTeam,
        bTrue ? TEXT("true") : bScatter ? TEXT("scatter") : TEXT("disguise"), Round->PartyRewardMask, Round->PartyWinnerMask);
    ++GameCount;
    if (bGamesScenario) StartNextGame();
    else
    {
        ++Epoch;
        Reports.Reset();
        ExpectedDigest.Reset();
        Step = EStep::Inspect;
        RoomCheck = ERoomCheck::Inspect;
        StepAt = Now;
    }
}

bool AIslandProbe::TickVenueProof(APartyVoxelSession& Arena, bool bShots)
{
    const FName Feature = Mode->CurrentPartyFeature;
    const bool bReef = Feature == TEXT("PartyReefRun");
    const bool bPhotograph = bAllVenueBuildings && bShots && Feature == TEXT("PartyBridgeDash") && VenueId == 0;
    // The listen host supplies the screenshot and the synchronous occlusion control. Other arenas use their winner's client.
    const int32 Seat = bReef || Feature == TEXT("PartyBridgeDash") ? 0 : GameWinnerSeat;
    AVoxelPlayerController* PC = Mode->GetPlayers()[Seat];
    auto* Pawn = CastChecked<APartyVoxelPawn>(PC->GetPawn());
    auto* Peer = Peers[Seat].Get();
    const auto Photo = [](const TCHAR* Name)
    {
        const FString Path = VenuePhotoPath(Name);
        if (IFileManager::Get().FileExists(*Path)) IFileManager::Get().Delete(*Path);
        FScreenshotRequest::RequestScreenshot(Path, false, false);
    };
    const auto PhotoSaved = [](const TCHAR* Name)
    {
        return !FScreenshotRequest::IsScreenshotRequested() && IFileManager::Get().FileSize(*VenuePhotoPath(Name)) > 0;
    };
    if (VenueStep == EVenueStep::Restored) return true;
    if (VenueStep == EVenueStep::Entry)
    {
        if (!CollectVenueReports(0) || !Arena.IsGrounded(Seat)) return false;
        VenueSpawn = Arena.FeetCell(Seat);
        VenueStep = bReef ? EVenueStep::OccludedFar : EVenueStep::Strike;
    }
    if (VenueStep == EVenueStep::OccludedFar || VenueStep == EVenueStep::OccludedNear)
    {
        const bool bFar = VenueStep == EVenueStep::OccludedFar;
        if (!MoveToVenuePose(Arena, *Pawn, {bFar ? -1 : 3, 0, 1})) return false;
        TArray<UStaticMeshComponent*> Parts;
        Arena.GetVenueStructureParts(0, Parts);
        for (const auto* Part : Parts)
        {
            if (!Part) continue;
            const FVector Eye = Pawn->GetPawnViewLocation();
            const FVector Point = Part->Bounds.Origin;
            const FVector Direction = (Point - Eye).GetSafeNormal();
            FHitResult Hit;
            if (!GetWorld()->LineTraceSingleByChannel(Hit, Eye, Point, ECC_Visibility,
                FCollisionQueryParams(SCENE_QUERY_STAT(VenueOcclusionProof), false, Pawn)) || Hit.GetActor() != &Arena) continue;
            const FIntVector Cell = Arena.CellAt(Hit.ImpactPoint - 2.*Hit.ImpactNormal);
            FHitResult ShortHit;
            if (Cell.X != 5 || Arena.BlockAt(Cell) != EPartyBlock::Clay ||
                Arena.TraceVenueStructure(Eye, Direction, Pawn) != INDEX_NONE ||
                Arena.TraceTerrain(Eye, Direction, Pawn, ShortHit) == bFar) continue;
            VenueWall = Cell;
            VenueWallEditSequence = Arena.GetAppliedEditCount();
            Peer->ClientGameTool(Point, false);
            VenueStep = bFar ? EVenueStep::CheckFar : EVenueStep::CheckNear;
            return false;
        }
        Require(false, TEXT("a reef wall lies between the standing player and the target structure"));
        return false;
    }
    if (VenueStep == EVenueStep::CheckFar || VenueStep == EVenueStep::CheckNear)
    {
        const bool bFar = VenueStep == EVenueStep::CheckFar;
        Require(!Arena.IsVenueStructureDestroyed(0) &&
            Arena.GetAppliedEditCount()==VenueWallEditSequence+(bFar ? 0 : 1),
            TEXT("a distant wall blocks the tool and a reachable wall receives exactly one cut"));
        UE_LOG(LogTemp, Display, TEXT("EXPEDITION_VENUE_OCCLUSION round=%d far=%d wall=%s structure=0"),
            GameCount + 1, bFar, *VenueWall.ToString());
        VenueStep = bFar ? EVenueStep::OccludedNear : EVenueStep::Strike;
        return false;
    }
    if (VenueStep == EVenueStep::Strike)
    {
        TArray<UStaticMeshComponent*> Parts;
        Arena.GetVenueStructureParts(VenueId, Parts);
        if (!Arena.IsGrounded(Seat)) return false;
        const FVector Eye = Pawn->GetPawnViewLocation();
        if (!VisibleVenuePart(Arena, *Pawn, Parts, VenueId, Eye, VenueTarget))
        {
            const FVector ViewOffset = Eye - Pawn->GetActorLocation();
            // Search supported two-cell-high stands around the starting bank, nearest first.
            // Include the top of the course walls, but keep the probe away from scoring platforms and finish lines.
            for (int32 Radius = 0; Radius <= 18; ++Radius)
                for (int32 X = -Radius; X <= Radius; ++X)
                    for (int32 Y = -Radius; Y <= Radius; ++Y)
                    {
                        if (FMath::Max(FMath::Abs(X), FMath::Abs(Y)) != Radius) continue;
                        for (int32 Z : {1, 2, 3, 4})
                        {
                            const FIntVector Feet(VenueSpawn.X + X, VenueSpawn.Y + Y, Z);
                            if ((bReef || Feature == TEXT("PartyBridgeDash")) && Feet.X > 4) continue;
                            if (!bReef && Z != 1) continue;
                            if (!CanStandOnBoard(Arena, Feet) || Feet == Arena.FeetCell(Seat)) continue;
                            bool bOccupied = false;
                            for (const AVoxelPlayerController* Other : Mode->GetPlayers())
                                if (Other != PC && FVector::Dist2D(Other->GetPawn()->GetActorLocation(), Arena.CellCenter(Feet)) < 80.) bOccupied = true;
                            if (bOccupied) continue;
                            const FVector AtEye = VenueStandingPose(Arena, *Pawn, Feet) + ViewOffset;
                            if (!VisibleVenuePart(Arena, *Pawn, Parts, VenueId, AtEye, VenueTarget)) continue;
                            MoveToVenuePose(Arena, *Pawn, Feet);
                            return false;
                        }
                    }
            UE_LOG(LogTemp, Error, TEXT("Venue proof missing sightline feature=%s structure=%d"), *Feature.ToString(), VenueId);
            Require(false, TEXT("a supported standing position on the starting bank can see the building"));
            return false;
        }
        Require(CanStandOnBoard(Arena, Arena.FeetCell(Seat)), TEXT("venue shot was taken from a solid board floor"));
        UE_LOG(LogTemp, Display, TEXT("EXPEDITION_VENUE_AIM round=%d id=%d seat=%d range_cm=%d feet=%s"),
            GameCount + 1, VenueId, Seat, FMath::RoundToInt(FVector::Dist(Eye, VenueTarget)), *Arena.FeetCell(Seat).ToString());
        if (bPhotograph)
        {
            PC->SetControlRotation((VenueTarget - Eye).Rotation());
            VenueStep = EVenueStep::BeforePhoto;
            return false;
        }
        Peer->ClientGameTool(VenueTarget, false);
        VenueStep = EVenueStep::CheckStrike;
        return false;
    }
    if (VenueStep == EVenueStep::BeforePhoto)
    {
        Photo(TEXT("VenueBridgeBefore.png"));
        VenueStep = EVenueStep::WaitBeforePhoto;
        return false;
    }
    if (VenueStep == EVenueStep::WaitBeforePhoto)
    {
        if (!PhotoSaved(TEXT("VenueBridgeBefore.png"))) return false;
        Peer->ClientGameTool(VenueTarget, false);
        VenueStep = EVenueStep::CheckStrike;
        return false;
    }
    if (VenueStep == EVenueStep::CheckStrike)
    {
        // On remote seats, movement correction may arrive after the tool RPC. Retry from the actual replicated eye.
        if (!Arena.IsVenueStructureDestroyed(VenueId)) { Peer->ClientGameTool(VenueTarget, false); return false; }
        UE_LOG(LogTemp, Display, TEXT("EXPEDITION_VENUE_REMOVED round=%d feature=%s id=%d"),
            GameCount + 1, *Feature.ToString(), VenueId);
        VenueStep = bPhotograph ? EVenueStep::AfterPhoto : EVenueStep::Final;
    }
    if (VenueStep == EVenueStep::AfterPhoto)
    {
        Photo(TEXT("VenueBridgeAfter.png"));
        VenueStep = EVenueStep::WaitAfterPhoto;
        return false;
    }
    if (VenueStep == EVenueStep::WaitAfterPhoto)
    {
        if (!PhotoSaved(TEXT("VenueBridgeAfter.png"))) return false;
        VenueStep = EVenueStep::Final;
    }
    if (VenueStep == EVenueStep::Final)
    {
        const int32 Gone = (1 << (VenueId + 1)) - 1;
        if (!CollectVenueReports(Gone)) return false;
        if (bAllVenueBuildings && Gone != AllVenueBuildings)
        {
            ++VenueId;
            VenueStep = EVenueStep::Strike;
            return false;
        }
        if (!MoveToVenuePose(Arena, *Pawn, VenueSpawn)) return false;
        VenueStep = EVenueStep::Restored;
        return true;
    }
    return false;
}

void AIslandProbe::PlayExpeditionGame(APartyVoxelSession& Arena)
{
    const int32 Seat = GameWinnerSeat;
    auto* Pawn = CastChecked<APartyVoxelPawn>(Mode->GetPlayers()[Seat]->GetPawn());
    auto* Peer = Peers[Seat].Get();
    if (bGamesScenario && !bStoneDug)
    {
        if (!StoneProofCell.IsSet())
        {
            const FIntVector Foot = Arena.FeetCell(Seat);
            for (const FIntVector Delta : {FIntVector(1,0,-1), FIntVector(-1,0,-1), FIntVector(0,1,-1), FIntVector(0,-1,-1)})
            {
                const FIntVector Cell = Foot + Delta;
                if (Arena.BlockAt(Cell) == EPartyBlock::Stone && Arena.BlockAt(Cell + FIntVector(0,0,1)) == EPartyBlock::Air)
                {
                    FHitResult Ground;
                    if (!SampleFloor(Arena, Arena.CellCenter(Cell), Ground)) continue;
                    StoneProofCell = Cell;
                    StoneHeightBefore = Ground.ImpactPoint.Z;
                    break;
                }
            }
            Require(StoneProofCell.IsSet(), TEXT("arena start has an exposed stone foundation within real tool reach"));
            if (!StoneProofCell.IsSet()) return;
        }
        FHitResult Ground;
        if (SampleFloor(Arena, Arena.CellCenter(*StoneProofCell), Ground) && Ground.ImpactPoint.Z > StoneHeightBefore-16.)
        {
            Peer->ClientGameTool(Ground.ImpactPoint, false);
            return;
        }
        bStoneDug = true;
        UE_LOG(LogTemp, Display, TEXT("EXPEDITION_STONE_DUG feature=%s cell=%s"), *Mode->CurrentPartyFeature.ToString(), *StoneProofCell->ToString());
    }
    const FName Feature = Mode->CurrentPartyFeature;
    const bool bReopenChannel = OnlyGame==TEXT("PartyFloodGates") && Mode->GetPlayers().Num()>=2
        && FParse::Param(FCommandLine::Get(),TEXT("ExpeditionArenaTerrainQA"));
    if (Arena.Score(Seat)>0 && !bReopenChannel) return;
    const auto PositionOnCell = [&](APartyVoxelPawn& Character, FIntVector Feet, FVector Offset)
    {
        const FVector Spot = Arena.CellCenter(Feet) + Offset + FVector(0.,0.,Character.GetCapsuleComponent()->GetScaledCapsuleHalfHeight()-48.);
        Character.TeleportTo(Spot, FRotator::ZeroRotator, false, true);
        Character.GetCharacterMovement()->StopMovementImmediately();
        Character.ForceNetUpdate();
    };
    const auto Stand = [&](FIntVector Feet, FVector Offset = FVector::ZeroVector)
    {
        PositionOnCell(*Pawn, Feet, Offset);
        ++GameAction;
    };
    const auto Aim = [&](FIntVector Cell, bool bPlace, FVector Offset = FVector::ZeroVector)
    {
        Peer->ClientGameTool(Arena.CellCenter(Cell) + Offset, bPlace);
    };
    if (Feature == TEXT("PartyReefRun"))
    {
        if (GameAction == 0) { Stand({3,0,1}); return; }
        const FVector Origin = Arena.GetActorLocation();
        const FVector Foot = Pawn->GetActorLocation()-Origin-FVector(0,0,Pawn->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
        const FCollisionQueryParams Query(SCENE_QUERY_STAT(PartyReefPassage),false,Pawn);
        if (GameAction==1)
        {
            // Probe parallel slices across the entire wall, rather than a fan of eye rays.
            // A fan can see the far side of a pinhole that will still block the character's shoulders.
            for (const double Height : {262.,197.,151.,280.,125.})
                for (const double Y : {50.,10.,90.})
                {
                    const FVector Start = Origin+FVector(410.,Y,Height);
                    const FVector End = Origin+FVector(645.,Y,Height);
                    FHitResult Wall;
                    if (!GetWorld()->LineTraceSingleByChannel(Wall,Start,End,ECC_Visibility,Query) || Wall.GetActor()!=&Arena) continue;
                    const FVector Into = (Wall.ImpactPoint-Pawn->GetPawnViewLocation()).GetSafeNormal();
                    UE_LOG(LogTemp,Display,TEXT("ARENA_REEF_CUT foot=%s surface=%s normal=%s"),
                        *Foot.ToCompactString(),*(Wall.ImpactPoint-Origin).ToCompactString(),*Wall.ImpactNormal.ToCompactString());
                    Peer->ClientGameTool(Wall.ImpactPoint+Into*4.,false);
                    return;
                }
            for (double Y : {10.,50.,90.})
            {
                TArray<FString> Bed;
                for (int32 X=400; X<=675; X+=25)
                {
                    FHitResult Surface;
                    const FVector XY=Origin+FVector(X,Y,0.);
                    const bool bFloor=GetWorld()->LineTraceSingleByChannel(Surface,XY+FVector(0,0,133.),XY-FVector(0,0,100.),ECC_Visibility,Query);
                    Bed.Add(bFloor ? FString::FromInt(FMath::RoundToInt(Surface.ImpactPoint.Z-Origin.Z)) : TEXT("hole"));
                }
                UE_LOG(LogTemp,Display,TEXT("ARENA_REEF_BED y=%.0f x400_to_675_by_25=%s"),Y,*FString::Join(Bed,TEXT(",")));
            }
            GameAction=2;
        }
        const auto* Capsule = Pawn->GetCapsuleComponent();
        const double Half = Capsule->GetScaledCapsuleHalfHeight();
        const FCollisionShape Shape = FCollisionShape::MakeCapsule(Capsule->GetScaledCapsuleRadius(),Half);
        const FVector Ahead=Origin+FVector(720.,50.,Half+112.);
        FHitResult Obstruction;
        const bool bBlocked=GetWorld()->SweepSingleByChannel(Obstruction,Pawn->GetActorLocation()+FVector(0,0,10.),Ahead,FQuat::Identity,ECC_Pawn,Shape,Query);
        UE_LOG(LogTemp,Display,TEXT("ARENA_REEF_WALK foot=%s grounded=%d capsule_blocked=%d rim=%s normal=%s"),
            *Foot.ToCompactString(),Arena.IsGrounded(Seat),bBlocked,*(Obstruction.ImpactPoint-Origin).ToCompactString(),*Obstruction.ImpactNormal.ToCompactString());
        if (bBlocked && !Obstruction.bStartPenetrating && Obstruction.ImpactNormal.Z < .55 && Obstruction.ImpactPoint.Z>Origin.Z+112.)
        {
            const FVector Into = (Obstruction.ImpactPoint-Pawn->GetPawnViewLocation()).GetSafeNormal();
            Peer->ClientGameTool(Obstruction.ImpactPoint+Into*4.,false);
        }
        Peer->ClientGameWalk(Arena.CellCenter({7,0,1}));
    }
    else if (Feature == TEXT("PartyBridgeDash"))
    {
        if (GameAction == 0)
        {
            const auto& Players = Mode->GetPlayers();
            if (Players.Num() > 1)
            {
                const int32 WaitingSeat = Seat == 0 ? 1 : 0;
                auto* WaitingPawn = CastChecked<APartyVoxelPawn>(Players[WaitingSeat]->GetPawn());
                PositionOnCell(*WaitingPawn, {10,3,1}, FVector::ZeroVector);
            }
            Stand({4,-3,1}, {12.,-62.,0.});
            return;
        }
        // Raise the submerged footings from each bank. Re-aim at every new soil surface.
        // The human-sized runner must then cross the completed mud path on foot.
        if (GameAction <= 2)
        {
            const double LaneY = -400.;
            for (int32 I=0; I<=8; ++I)
            {
                // Cover the joins between footings as well as their centers. The capsule
                // needs a continuous tread, and a raised near footing can hide a low far one.
                const double X = GameAction == 1 ? 700.-25.*I : 700.+25.*I;
                const FVector Column = Arena.GetActorLocation()+FVector(X,LaneY,60.);
                FHitResult Surface;
                if (!SampleFloor(Arena,Column,Surface)) return;
                if (Surface.ImpactPoint.Z < Arena.GetActorLocation().Z + 95.)
                {
                    Peer->ClientGameTool(Surface.ImpactPoint,true);
                    return;
                }
            }
            if (GameAction == 1) Stand({9,-3,1}, {-12.,-62.,0.});
            else
            {
                for (const double Y : {-425.,LaneY,-375.})
                {
                    TArray<FString> Heights;
                    for (int32 X=450; X<=950; X+=25)
                    {
                        FHitResult Floor;
                        const FVector Q = Arena.GetActorLocation()+FVector(X,Y,65.);
                        Heights.Add(SampleFloor(Arena,Q,Floor) ? FString::FromInt(FMath::RoundToInt(Floor.ImpactPoint.Z-Arena.GetActorLocation().Z)) : TEXT("void"));
                    }
                    UE_LOG(LogTemp, Display, TEXT("ARENA_BRIDGE_TREAD y=%.0f x450_to_950_by_25=%s"),Y,*FString::Join(Heights,TEXT(",")));
                }
                Stand({3,-4,1}, {0.,-50.,0.});
            }
            return;
        }
        UE_LOG(LogTemp,Display,TEXT("ARENA_BRIDGE_WALK foot=%s grounded=%d"),
            *(Pawn->GetActorLocation()-Arena.GetActorLocation()-FVector(0,0,Pawn->GetCapsuleComponent()->GetScaledCapsuleHalfHeight())).ToCompactString(),Arena.IsGrounded(Seat));
        Peer->ClientGameWalk(Arena.CellCenter({10,-4,1})+FVector(0.,-50.,0.));
    }
    else if (Feature == TEXT("PartyCrystalHunt"))
    {
        // Expose the seeded crystal from the side, then strike it through the opening.
        if (GameAction == 0) { Stand({-9,-8,1}); return; }
        for (int32 Z=1; Z<=3; ++Z)
        {
            const FIntVector Crystal(-6,-8,Z);
            if (Arena.BlockAt(Crystal)==EPartyBlock::Gold) { Aim(Crystal, false); return; }
        }
    }
    else if (Feature == TEXT("PartyFloodGates"))
    {
        const int32 Y = (Seat - 4) * 9 + 1;
        if (GameAction == 0)
        {
            FloodSealedSequence = 0;
            FloodScoreAfterBreach = 0;
            FloodBreachTime = 0.;
            Stand({-1,Y,1});
            return;
        }
        if (bReopenChannel && GameAction<4 && Arena.SecondsLeft()<3.)
        {
            Require(false,TEXT("another contestant opened the dam and stopped its score for two seconds"));
            return;
        }
        FHitResult Surface;
        const FVector Column = Arena.CellCenter({2,Y,1});
        if (!bReopenChannel || GameAction==1)
        {
            if (!SampleFloor(Arena,Column,Surface)) return;
            if (!bReopenChannel || Surface.ImpactPoint.Z<Column.Z-19.)
                Peer->ClientGameTool(Surface.ImpactPoint,true);
            if (!bReopenChannel || Arena.Score(Seat)<2) return;
            // Let the other contestant operate from this bank after two genuine scoring ticks.
            const int32 Other = (Seat+1)%Peers.Num();
            PositionOnCell(*Pawn,{-3,Y+3,1},FVector::ZeroVector);
            auto* Rival = CastChecked<APartyVoxelPawn>(Mode->GetPlayers()[Other]->GetPawn());
            PositionOnCell(*Rival,{-1,Y,1},FVector::ZeroVector);
            FloodSealedSequence = Arena.GetAppliedEditCount();
            UE_LOG(LogTemp,Display,TEXT("ARENA_CHANNEL_SEALED owner=%d rival=%d score=%d seq=%d"),Seat,Other,Arena.Score(Seat),FloodSealedSequence);
            GameAction=2;
            return;
        }
        if (GameAction==2)
        {
            // A clear, straight line through the former dam at the waterline proves an actual
            // opening on the server. Keep aiming at the current mud surface while it blocks it.
            const FVector Inlet = Arena.CellCenter({0,Y,1}) + FVector(0,0,-24.);
            const FVector Outlet = Arena.CellCenter({7,Y,1}) + FVector(0,0,-24.);
            const FCollisionQueryParams Query(SCENE_QUERY_STAT(PartyFloodBreach));
            FHitResult Dam;
            if (GetWorld()->LineTraceSingleByChannel(Dam,Inlet,Outlet,ECC_Visibility,Query))
            {
                if (SampleFloor(Arena,Column,Surface)) Peers[(Seat+1)%Peers.Num()]->ClientGameTool(Surface.ImpactPoint,false);
                return;
            }
            Require(Arena.GetAppliedEditCount()>FloodSealedSequence,TEXT("the opponent's real tool changed the sealed channel"));
            FloodScoreAfterBreach = Arena.Score(Seat);
            FloodBreachTime = GetWorld()->GetTimeSeconds();
            GameAction=3;
            return;
        }
        if (GameAction==3)
        {
            Require(Arena.Score(Seat)==FloodScoreAfterBreach,TEXT("an open waterway earns no further points"));
            const double Elapsed = GetWorld()->GetTimeSeconds()-FloodBreachTime;
            if (Elapsed>=2.1)
            {
                UE_LOG(LogTemp,Display,TEXT("ARENA_CHANNEL_REOPEN owner=%d rival=%d score=%d elapsed=%.2f seq=%d"),
                    Seat,(Seat+1)%Peers.Num(),FloodScoreAfterBreach,Elapsed,Arena.GetAppliedEditCount());
                GameAction=4;
            }
        }
    }
    else
    {
        // Exercise a real placed stair, then isolate the rule's grounded-platform recognition.
        // These setup poses do not award points; the session samples the standing capsule.
        const bool bTower = Feature == TEXT("PartyBeaconClimb");
        const FIntVector Crowns[] = {{1,-7,3},{7,0,4},{1,7,3}};
        const FIntVector Platform = bTower ? FIntVector(-2,0,3) : Crowns[uint32(Arena.ArenaSeed()) % 3];
        const int32 EdgeX = Platform.X - (bTower ? 1 : 2);
        const FIntVector Base(EdgeX, Platform.Y, 0);
        if (GameAction == 0) { Stand({EdgeX-2,Platform.Y,1}); return; }
        FHitResult StairSurface;
        if (SampleFloor(Arena,Arena.CellCenter(Base),StairSurface) && StairSurface.ImpactPoint.Z < Arena.CellCenter(Base).Z+78.)
        { Peer->ClientGameTool(StairSurface.ImpactPoint, true); return; }
        if (GameAction == 1) Stand(Platform+FIntVector(0,0,1));
    }
}
