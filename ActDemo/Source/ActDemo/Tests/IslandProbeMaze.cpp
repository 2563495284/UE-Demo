#include "Tests/IslandProbe.h"

#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Island/IslandGameMode.h"
#include "Island/IslandPlayerController.h"
#include "Voxel/VoxelCharacter.h"
#include "PartyVoxelArena.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "UnrealClient.h"
#include "Misc/Paths.h"

namespace
{
double MazeWallDepth(const APartyVoxelSession& Arena, FVector Midpoint, FVector Normal)
{
    FHitResult Hit;
    const FVector From = Midpoint - Normal * 45.;
    FCollisionObjectQueryParams Objects(ECC_WorldStatic);
    Objects.AddObjectTypesToQuery(ECC_WorldDynamic);
    const bool bWall=Arena.GetWorld()->LineTraceSingleByObjectType(Hit, From, Midpoint + Normal * 45.,Objects)
        && Hit.GetComponent() && Hit.GetComponent()->ComponentHasTag(TEXT("MazeWall"));
    return bWall ? Hit.Distance : -1.;
}

bool HasMazeWall(const APartyVoxelSession& Arena,FVector Midpoint,FVector Normal)
{
    return MazeWallDepth(Arena,Midpoint,Normal)>=0.;
}

// Read the actual collisions above player height so positions of moving pawns cannot affect the layout check.
bool InspectLayout(const APartyVoxelSession& Arena, uint32& Digest)
{
    constexpr int32 Width = 9;
    const FVector FirstCenter = Arena.GetActorLocation() + FVector(-2350., -2350., 300.);
    const FVector Axes[] = {FVector::ForwardVector, FVector::RightVector};
    TArray<int32> Links[Width * Width];
    Digest = 0;
    for (int32 Y = 0; Y < Width; ++Y)
        for (int32 X = 0; X < Width; ++X)
        {
            const int32 Cell = X + Y * Width;
            for (int32 Axis = 0; Axis < 2; ++Axis)
            {
                if ((Axis == 0 ? X : Y) == Width - 1) continue;
                const int32 Next = Cell + (Axis == 0 ? 1 : Width);
                const FVector Midpoint = FirstCenter + FVector(X * 600., Y * 600., 0.) + Axes[Axis] * 300.;
                const bool bWall = HasMazeWall(Arena, Midpoint, Axes[Axis]);
                Digest = HashCombineFast(Digest, uint32(bWall));
                if (!bWall) { Links[Cell].Add(Next); Links[Next].Add(Cell); }
            }
        }
    TArray<int32> Reachable = {0};
    for (int32 I = 0; I < Reachable.Num(); ++I)
        for (const int32 Next : Links[Reachable[I]]) Reachable.AddUnique(Next);
    return Reachable.Num() == Width * Width;
}
}

void AIslandProbePeer::ClientCheckMaze_Implementation(int32 Round, int32 Stage, FVector Midpoint, FVector Normal)
{
    auto* PC = Cast<AIslandPlayerController>(GetOwner());
    const auto* Pawn = PC ? Cast<APartyVoxelPawn>(PC->GetPawn()) : nullptr;
    const auto* Game = Pawn ? Pawn->GetSession() : nullptr;
    if (!Game || !Game->IsChaseGame() || Game->GetPhase() != EPartyVoxelPhase::Playing) return;
    if (Stage == 6)
    {
        if (Game->GetRemainingRunners() != 1) return;
        // A stationary teammate can look sideways while their body still faces the passage.
        constexpr double SpectatorYawOffset = 85.;
        if (Game->IsRunner(Pawn->GetSeat()) && !Game->IsCaught(Pawn->GetSeat()))
            PC->SetControlRotation(FRotator(0., Pawn->GetActorRotation().Yaw + SpectatorYawOffset, 0.));
        if (Game->IsCaught(Pawn->GetSeat()))
        {
            const auto* Watching = Cast<APartyVoxelPawn>(PC->GetViewTarget());
            if (!Watching || Watching->GetSession() != Game || Watching == Pawn || !Game->IsRunner(Watching->GetSeat())
                || Game->IsCaught(Watching->GetSeat()) || Pawn->GetCharacterMovement()->MovementMode != MOVE_None) return;
            FVector Eye;
            FRotator View;
            PC->GetPlayerViewPoint(Eye, View);
            const double Yaw = FMath::FindDeltaAngleDegrees(Watching->GetActorRotation().Yaw, View.Yaw);
            if (FMath::Abs(Yaw - SpectatorYawOffset) > 3.) return;
            UE_LOG(LogTemp, Display, TEXT("MAZE_QA_SPECTATE round=%d caught=%d watching=%d frozen=1 view_body_yaw=%.2f"),
                Round, Pawn->GetSeat(), Watching->GetSeat(), Yaw);
        }
    }
    else
    {
        const FVector Tangent=FVector::CrossProduct(Normal,FVector::UpVector);
        const FVector Center=Midpoint+(Stage==16 ? Tangent*195. : FVector::ZeroVector);
        const double CenterDepth=MazeWallDepth(*Game,Center,Normal);
        if ((CenterDepth>=0.)!=(Stage!=3 && Stage!=16) || (Stage==10 && CenterDepth<33.)) return;
        uint32 Profile=0;
        for (int32 Y=-144;Y<=144;Y+=24)
            for (int32 Z=-72;Z<=120;Z+=24)
            {
                const double Depth=MazeWallDepth(*Game,Center+Tangent*Y+FVector(0,0,Z),Normal);
                Profile=HashCombineFast(Profile,GetTypeHash(FMath::RoundToInt(Depth)));
            }
        UE_LOG(LogTemp,Display,TEXT("MAZE_COLLISION_PROFILE round=%d stage=%d hash=%08x rays=117"),
            Round,Stage,Profile);
    }
    if (Stage == 1)
    {
        uint32 Digest;
        if (!InspectLayout(*Game, Digest)) return;
        UE_LOG(LogTemp, Display, TEXT("MAZE_QA_LAYOUT round=%d seed=%d hash=%08x reachable=81"),
            Round, Game->ArenaSeed(), Digest);
    }
    UE_LOG(LogTemp, Display, TEXT("MAZE_QA_VIEW round=%d stage=%d seat=%d"), Round, Stage, Pawn->GetSeat());
    ServerCheckedMaze(Round, Stage);
}

void AIslandProbePeer::ServerCheckedMaze_Implementation(int32 Round, int32 Stage)
{
    MazeObservedRound = Round;
    MazeObservedStage = Stage;
}

bool AIslandProbe::CaptureMazeStage(APartyVoxelSession& Arena,int32 Phase,const TCHAR* Label)
{
    if (GameCount!=0 || !FApp::CanEverRender() || FParse::Param(FCommandLine::Get(),TEXT("nullrhi"))) return true;
    FString Directory;
    if (!FParse::Value(FCommandLine::Get(),TEXT("FieldShotsDir="),Directory)) return true;
    auto* PC=GetWorld()->GetFirstPlayerController();
    if (!PC || !PC->IsLocalController()) return true;
    if (!MazeProofCamera) MazeProofCamera=GetWorld()->SpawnActor<ACameraActor>();
    if (!MazeProofCamera) return true;
    if (MazePhotoStage!=Phase)
    {
        MazePhotoStage=Phase; MazePhotoFrame=GFrameCounter;
        const FVector Side=FVector::CrossProduct(MazeWallNormal,FVector::UpVector);
        const FVector Focus=MazeWallCenter+FVector(0,0,32);
        // Look past the player's shoulder so the near rim, back wall and repaired patch stay in shot.
        const FVector Eye=Focus-MazeWallNormal*350.+Side*244.+FVector(0,0,92.);
        MazeProofCamera->SetActorLocationAndRotation(Eye,(Focus-Eye).Rotation());
        MazeProofCamera->GetCameraComponent()->SetFieldOfView(68.f);
        PC->SetViewTarget(MazeProofCamera);
        return false;
    }
    if (GFrameCounter-MazePhotoFrame<10 || FScreenshotRequest::IsScreenshotRequested()) return false;
    if (MazePhotoFrame!=0)
    {
        FScreenshotRequest::RequestScreenshot(FPaths::Combine(Directory,FString(Label)+TEXT(".png")),false,false);
        MazePhotoFrame=0;
        return false;
    }
    PC->SetViewTarget(PC->GetPawn());
    return true;
}

void AIslandProbe::PlayMazeChase(APartyVoxelSession& Arena)
{
    if (!FParse::Param(FCommandLine::Get(), TEXT("ExpeditionMazeQA")))
    {
        // In a mixed deck this checks ordinary survival and reward settlement.
        GameAction = 1;
        return;
    }
    if (Arena.GetHeadStartRemaining() > 0.f) return;
    const auto& Players = Mode->GetPlayers();
    TArray<APartyVoxelPawn*> Runners;
    APartyVoxelPawn* Hunter = nullptr;
    for (const auto& Controller : Players)
    {
        auto* Pawn = CastChecked<APartyVoxelPawn>(Controller->GetPawn());
        if (Arena.IsRunner(Pawn->GetSeat())) Runners.Add(Pawn);
        else if (!Hunter || Pawn->GetSeat() > Hunter->GetSeat()) Hunter = Pawn;
    }
    if (!Hunter || Runners.Num() != 2) { Require(false, TEXT("two runners and two hunting crews entered the maze")); return; }
    APartyVoxelPawn& First = *Runners[0];
    auto* Peer = Peers[Hunter->GetSeat()].Get();
    const auto Observe = [&](int32 Stage)
    {
        bool bReady = true;
        for (const auto& Observer : Peers)
            if (Observer->MazeObservedRound != GameCount + 1 || Observer->MazeObservedStage != Stage)
            {
                Observer->ClientCheckMaze(GameCount + 1, Stage, MazeWallCenter, MazeWallNormal);
                bReady = false;
            }
        return bReady;
    };
    const auto Position = [&](APartyVoxelPawn& Pawn, FVector Offset)
    {
        FVector Location = MazeWallCenter + Offset;
        Location.Z = Arena.GetActorLocation().Z + Pawn.GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 3.;
        Pawn.TeleportTo(Location, Pawn.GetActorRotation(), false, true);
        Pawn.GetCharacterMovement()->StopMovementImmediately();
        Pawn.ForceNetUpdate();
    };
    const auto Capture = [&](APartyVoxelPawn& Pawn) { Peer->ClientGameTool(Pawn.GetActorLocation(), true); };
    const FVector AlongWall=FVector::CrossProduct(MazeWallNormal,FVector::UpVector);
    const FVector NarrowHole=AlongWall*195.;
    const auto CheckCut = [&](FVector Center, const TCHAR* Grade, double MinDepth, double MaxDepth)
    {
        const double Fresh = MazeWallDepth(Arena,Center+FVector(0.,0.,235.),MazeWallNormal);
        const double Back = MazeWallDepth(Arena,Center,MazeWallNormal);
        const double Cut = Back-Fresh;
        Require(Fresh>=0. && Back>=0. && Cut>=MinDepth && Cut<=MaxDepth,
            TEXT("the first maze punch leaves a back wall at its charge-dependent depth"));
        UE_LOG(LogTemp,Display,TEXT("MAZE_PUNCH_DENT round=%d grade=%s depth_cm=%.1f"),GameCount+1,Grade,Cut);
    };
    switch (GameAction)
    {
    case 0:
    {
        for (int32 Seat = 0; Seat < Players.Num(); ++Seat)
        {
            const auto* IslandHero = CastChecked<AVoxelCharacter>(OriginalPawns[Seat].Get());
            const auto* Player = CastChecked<APartyVoxelPawn>(Players[Seat]->GetPawn());
            Require(FMath::IsNearlyEqual(Player->GetCharacterMovement()->MaxWalkSpeed, IslandHero->GetCharacterMovement()->MaxWalkSpeed),
                TEXT("each runner and hunter retains their island movement speed"));
        }
        TInlineComponentArray<UStaticMeshComponent*> Meshes(&Arena);
        auto* const* Chosen = Meshes.FindByPredicate([](const UStaticMeshComponent* Mesh)
        { return Mesh->ComponentHasTag(TEXT("MazeWall")) && Mesh->IsVisible() && Mesh->IsCollisionEnabled(); });
        if (!Chosen) { Require(false, TEXT("the generated maze has a solid interior wall")); return; }
        MazeWallCenter = (*Chosen)->Bounds.Origin;
        MazeWallCenter.Z = Arena.GetActorLocation().Z + 96.;
        const FVector Extent = (*Chosen)->Bounds.BoxExtent;
        MazeWallNormal = Extent.X < Extent.Y ? FVector::ForwardVector : FVector::RightVector;
        Position(*Hunter, -MazeWallNormal * 88.);
        Position(First, MazeWallNormal * 88.);
        ++GameAction;
        return;
    }
    case 1:
        if (!Observe(GameAction) || !CaptureMazeStage(Arena,1,TEXT("MazePunchIntact"))) return;
        Capture(First);
        MazeBlockedAt = FPlatformTime::Seconds();
        ++GameAction;
        return;
    case 2:
        if (FPlatformTime::Seconds() - MazeBlockedAt < 1.8) return;
        Require(!Arena.IsCaught(First.GetSeat()) && HasMazeWall(Arena,MazeWallCenter,MazeWallNormal),
            TEXT("a close right click into a wall does not catch the runner behind it"));
        Position(First, MazeWallNormal * 110. + FVector::CrossProduct(MazeWallNormal, FVector::UpVector) * 210.);
        Position(*Hunter,-MazeWallNormal*166.);
        MazeLastEdit=Arena.GetAppliedEditCount();
        Peer->ClientTryArenaPunch(GameCount+1,1,MazeWallCenter);
        GameAction=17;
        return;
    case 17:
        if (Peer->TriedPunchRound!=GameCount+1 || Peer->TriedPunchTrial!=1) return;
        Require(Arena.GetAppliedEditCount()==MazeLastEdit,TEXT("cancelling a maze punch leaves the wall intact"));
        Peer->ClientGameTool(MazeWallCenter,false,2.0f);
        GameAction=10;
        return;
    case 10:
        if (Arena.GetAppliedEditCount()==MazeLastEdit) return;
        Require(HasMazeWall(Arena,MazeWallCenter,MazeWallNormal),TEXT("first full punch retains a solid back wall"));
        if (!Observe(10) || !CaptureMazeStage(Arena,10,TEXT("MazePunchDent"))) return;
        CheckCut(MazeWallCenter,TEXT("full"),42.,52.);
        MazeLastEdit=Arena.GetAppliedEditCount();
        Peer->ClientTryArenaPunch(GameCount+1,4,MazeWallCenter);
        GameAction=3;
        return;
    case 3:
        if (Arena.GetAppliedEditCount()==MazeLastEdit) return;
        Require(!HasMazeWall(Arena,MazeWallCenter,MazeWallNormal),TEXT("second full punch opens the wall"));
        if (!Observe(GameAction) || !CaptureMazeStage(Arena,3,TEXT("MazePunchThrough"))) return;
        {
            Position(*Hunter,-AlongWall*190.-MazeWallNormal*158.);
            Position(*Runners[1],AlongWall*190.-MazeWallNormal*158.);
            Position(First,-MazeWallNormal*105.);
            FHitResult Move;
            const FVector Start=First.GetActorLocation();
            First.SetActorLocation(Start+MazeWallNormal*210.,true,&Move);
            const double Crossed=(First.GetActorLocation()-Start).Dot(MazeWallNormal);
            Require(Crossed>200.,TEXT("the real runner capsule crosses the full-height hole"));
            UE_LOG(LogTemp,Display,TEXT("MAZE_PUNCH_CROSSED first_blocked=1 full_hits=2 cm=%.0f"),Crossed);
        }
        Position(First,-MazeWallNormal*105.);
        Peers[First.GetSeat()]->ClientGameWalk(MazeWallCenter+MazeWallNormal*112.);
        MazeBlockedAt=FPlatformTime::Seconds();
        GameAction=19;
        return;
    case 19:
    {
        const double Walked=(First.GetActorLocation()-MazeWallCenter).Dot(MazeWallNormal)+105.;
        if (Walked<195. && FPlatformTime::Seconds()-MazeBlockedAt<7.) return;
        Require(Walked>=195.,TEXT("the runner walked through the opening with character movement input"));
        UE_LOG(LogTemp,Display,TEXT("MAZE_RUNNER_WALK cm=%.1f"),Walked);
        Position(First,AlongWall*45.);
        Position(*Runners[1],-MazeWallNormal*150.);
        MazeLastEdit=Arena.GetAppliedEditCount();
        MazeBlockedAt=FPlatformTime::Seconds();
        GameAction=11;
        return;
    }
    case 11:
        if (FPlatformTime::Seconds()-MazeBlockedAt<1.8) return;
        Peers[Runners[1]->GetSeat()]->ClientGameTool(MazeWallCenter,true);
        MazeBlockedAt=FPlatformTime::Seconds();
        GameAction=12;
        return;
    case 12:
        if (FPlatformTime::Seconds()-MazeBlockedAt<1.3) return;
        Require(Arena.GetAppliedEditCount()==MazeLastEdit,TEXT("repair does not seal an occupied hole"));
        Position(First,MazeWallNormal*110.+FVector::CrossProduct(MazeWallNormal,FVector::UpVector)*210.);
        Position(*Runners[1],FVector::CrossProduct(MazeWallNormal,FVector::UpVector)*230.-MazeWallNormal*160.);
        Position(*Hunter,-MazeWallNormal*166.);
        Peer->ClientGameTool(MazeWallCenter,true);
        GameAction=4;
        return;
    case 4:
        if (!HasMazeWall(Arena,MazeWallCenter,MazeWallNormal)) { Peer->ClientGameTool(MazeWallCenter, true); return; }
        if (!Observe(GameAction) || !CaptureMazeStage(Arena,4,TEXT("MazePunchRepair"))) return;
        MazeLastEdit=Arena.GetAppliedEditCount();
        Position(*Hunter,-NarrowHole-MazeWallNormal*150.);
        Position(*Runners[1],AlongWall*160.-MazeWallNormal*180.);
        Peer->ClientTryArenaPunch(GameCount+1,3,MazeWallCenter-NarrowHole);
        GameAction=18;
        return;
    case 18:
        if (Arena.GetAppliedEditCount()==MazeLastEdit) return;
        Require(HasMazeWall(Arena,MazeWallCenter-NarrowHole,MazeWallNormal),TEXT("half charge dents an untouched part of the wall"));
        CheckCut(MazeWallCenter-NarrowHole,TEXT("half"),33.,42.);
        // Move to untouched stone on the same slab. A small through-hole should transmit a tag
        // ray without allowing the actual character capsule to follow.
        Position(*Hunter,NarrowHole-MazeWallNormal*165.);
        Position(First,-AlongWall*175.+MazeWallNormal*110.);
        Position(*Runners[1],-AlongWall*180.-MazeWallNormal*180.);
        MazeBlockedAt=FPlatformTime::Seconds();
        GameAction=13;
        return;
    case 13:
    case 15:
        if (FPlatformTime::Seconds()-MazeBlockedAt<1.) return;
        MazeLastEdit=Arena.GetAppliedEditCount();
        Peer->ClientGameTool(MazeWallCenter+NarrowHole,false,.10f);
        ++GameAction;
        return;
    case 14:
        if (Arena.GetAppliedEditCount()==MazeLastEdit) return;
        CheckCut(MazeWallCenter+NarrowHole,TEXT("light"),24.,33.);
        MazeBlockedAt=FPlatformTime::Seconds();
        ++GameAction;
        return;
    case 16:
    {
        if (Arena.GetAppliedEditCount()==MazeLastEdit) return;
        if (!Observe(16)) return;
        const bool bVisible=!HasMazeWall(Arena,MazeWallCenter+NarrowHole,MazeWallNormal);
        Require(bVisible,TEXT("the narrow hole admits a sight ray"));
        Position(*Hunter,-MazeWallNormal*160.);
        Position(First,NarrowHole-MazeWallNormal*105.);
        const FVector Start=First.GetActorLocation();
        FHitResult Blocking;
        First.SetActorLocation(Start+MazeWallNormal*210.,true,&Blocking);
        const double Moved=(First.GetActorLocation()-Start).Dot(MazeWallNormal);
        Require(Blocking.bBlockingHit && Moved<125.,TEXT("the real runner cannot fit through the narrow hole"));
        UE_LOG(LogTemp,Display,TEXT("MAZE_PUNCH_NARROW light_hits=2 sight=%d capsule_blocked=%d moved_cm=%.1f"),
            bVisible,Blocking.bBlockingHit,Moved);
        Position(*Hunter,NarrowHole-MazeWallNormal*88.);
        Position(First,NarrowHole+MazeWallNormal*88.);
        GameAction=5;
        return;
    }
    case 5:
        if (!Arena.IsCaught(First.GetSeat())) { Capture(First); return; }
        Require(Arena.GetRemainingRunners() == 1 && First.GetCharacterMovement()->MovementMode == MOVE_None,
            TEXT("the tagged player is immobilized and counted exactly once"));
        ++GameAction;
        return;
    case 6:
        if (!Observe(GameAction)) return;
        if (GameCount > 0) Position(*Runners[1],NarrowHole-MazeWallNormal*230.);
        ++GameAction;
        return;
    default:
        // Let the first deadline expire with a live teammate. End the next round by an actual second hit.
        if (GameCount > 0 && !Arena.IsCaught(Runners[1]->GetSeat())) Capture(*Runners[1]);
        return;
    }
}
