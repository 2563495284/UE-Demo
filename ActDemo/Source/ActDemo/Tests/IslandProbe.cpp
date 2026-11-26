#include "Tests/IslandProbe.h"

#include "Components/StaticMeshComponent.h"
#include "Components/CapsuleComponent.h"
#include "Algo/AllOf.h"
#include "Algo/Count.h"
#include "DynamicRHI.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "InputKeyEventArgs.h"
#include "TimerManager.h"
#include "PartyMinigame.h"
#include "RenderTimer.h"
#include "Island/IslandGameMode.h"
#include "Island/ExpeditionDock.h"
#include "Island/IslandBreakable.h"
#include "Island/IslandPlayerController.h"
#include "Island/IslandState.h"
#include "Island/IslandWorld.h"
#include "UnrealClient.h"
#include "Voxel/VoxelCharacter.h"
#include "Voxel/VoxelTerrain.h"

namespace
{
    TOptional<FVector> PreviousFirstChest;
    TMap<FGuid, int32> ReturningExpeditionMembers;
    constexpr int32 EditsPerStage = 150;
    const FIntPoint StressSites[] = {{-400, -400}, {800, 800}, {-800, 800}};
    // Away from the four search areas, the prize intersection, starting docks and landmarks.
    const FIntPoint ExpeditionStressSites[] = {{2000,2000}, {-2000,2000}, {-2000,-2000}, {2000,-2000}};

    FString ExpeditionDigest(const AVoxelTerrain& Terrain)
    {
        FString Result = FString::Printf(TEXT("%08x"), Terrain.GetChecksum());
        for (const FIntPoint& Site : ExpeditionStressSites)
        {
            FHitResult Floor;
            const FVector Start(Site.X + 12, Site.Y + 12, 2000.0);
            Result += Terrain.TraceRock(Start, Start - FVector(0,0,3000), nullptr, Floor)
                ? FString::Printf(TEXT(":%d"), FMath::RoundToInt(Floor.ImpactPoint.Z)) : TEXT(":missing");
        }
        return Result;
    }

    AIslandBreakable* FindBreakable(UWorld* World, EIslandBreakEffect Effect, bool bRedDock = false)
    {
        for (TActorIterator<AIslandBreakable> It(World); It; ++It)
        {
            if (It->Effect == Effect && (!bRedDock || It->GetActorLocation().X < -2380.0))
            {
                return *It;
            }
        }
        return nullptr;
    }

    // Look for a walkable lip with an unobstructed, in-range view of a buried corner.
    // If the central shaft has broken through, open an adjacent shallow shaft first.
    bool TrySideApproach(AIslandPlayerController& Player, AVoxelTerrain& Terrain, const FVector& Target)
    {
        AVoxelCharacter& Hero = *Player.GetHero();
        const UCapsuleComponent& Capsule = *Hero.GetCapsuleComponent();
        const double HalfHeight = Capsule.GetScaledCapsuleHalfHeight();
        const FCollisionShape Shape = FCollisionShape::MakeCapsule(Capsule.GetScaledCapsuleRadius(), HalfHeight);
        const FCollisionResponseParams Responses(Capsule.GetCollisionResponseToChannels());
        const FCollisionQueryParams Query(SCENE_QUERY_STAT(ExpeditionQAStand), false, &Hero);
        const FVector EyeOffset = Hero.GetAimOrigin() - Hero.GetActorLocation();
        const auto& Density = *Terrain.GetDensity();
        const FVector Low = (Target - IslandRules::ChestExtent - FVector(Density.Grid.Origin)) / VoxelPlayground::Spacing;
        const FVector High = (Target + IslandRules::ChestExtent - FVector(Density.Grid.Origin)) / VoxelPlayground::Spacing;
        TArray<FVector> Residual;
        for (int32 Z = FMath::FloorToInt(Low.Z); Z <= FMath::CeilToInt(High.Z); ++Z)
            for (int32 Y = FMath::FloorToInt(Low.Y); Y <= FMath::CeilToInt(High.Y); ++Y)
                for (int32 X = FMath::FloorToInt(Low.X); X <= FMath::CeilToInt(High.X); ++X)
                    if (Density.Sample({X,Y,Z}) < 0)
                        Residual.Add(FVector(Density.Grid.Origin + FIntVector(X,Y,Z) * VoxelPlayground::Spacing));

        TOptional<FVector> Approach, SideStand;
        FVector SideAim = FVector::ZeroVector;
        int32 MostResidualReached = 0;
        double BestDepth = TNumericLimits<double>::Max();
        for (const double Radius : {85.0, 125.0, 165.0})
        {
            for (int32 Heading = 0; Heading < 12; ++Heading)
            {
                const double Angle = Heading * UE_TWO_PI / 12.0;
                const FVector Column = Target + Radius * FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0);
                FHitResult Floor;
                if (!Terrain.TraceRock(Column + FVector(0,0,1600), Column - FVector(0,0,1300), &Hero, Floor))
                {
                    continue;
                }
                const FVector Stand = Floor.ImpactPoint + FVector(0,0,HalfHeight + 8.0);
                if (Floor.ImpactNormal.Z < .70 || Terrain.GetWorld()->OverlapBlockingTestByChannel(
                    Stand, FQuat::Identity, Capsule.GetCollisionObjectType(), Shape, Query, Responses))
                {
                    continue;
                }
                const double Depth = Floor.ImpactPoint.Z - Target.Z;
                if (Depth > -180.0 && FMath::Abs(Depth + 100.0) < BestDepth)
                {
                    BestDepth = FMath::Abs(Depth + 100.0);
                    Approach = Stand;
                }
                const FVector Eye = Stand + EyeOffset;
                for (const FVector& Residue : Residual)
                {
                    const FVector Aim = (Residue - Eye).GetSafeNormal();
                    FHitResult Side;
                    if (!Terrain.TraceRock(Eye, Eye + Aim * 235.0, &Hero, Side))
                    {
                        continue;
                    }
                    // Use the actual punch brush center and radius to pick a wall that reaches the chest.
                    const FVector Center = Side.ImpactPoint;
                    const int32 Reached = Algo::CountIf(Residual, [&Center](const FVector& P)
                        { return FVector::DistSquared(P, Center) < FMath::Square(46.0); });
                    if (Reached > MostResidualReached)
                    {
                        MostResidualReached = Reached;
                        SideStand = Stand;
                        SideAim = Aim;
                    }
                }
            }
        }
        const TOptional<FVector>& Stand = SideStand ? SideStand : Approach;
        if (!Stand || !Hero.TeleportTo(*Stand, FRotator::ZeroRotator))
        {
            return false;
        }
        Hero.GetCharacterMovement()->StopMovementImmediately();
        if (SideStand)
        {
            Player.SubmitEdit(EVoxelAction::Punch, SideAim);
        }
        else
        {
            Player.SubmitEdit(EVoxelAction::Punch, -FVector::UpVector);
        }
        UE_LOG(LogTemp, Display, TEXT("EXPEDITION_QA_SIDE_APPROACH action=%s stand=%s aim=%s"),
            SideStand ? TEXT("punch") : TEXT("dig"), *Stand->ToCompactString(), *SideAim.ToCompactString());
        return true;
    }

    FString TerrainDigest(const AVoxelTerrain& Terrain)
    {
        if (Terrain.Layout == EVoxelLayout::Expedition)
        {
            return ExpeditionDigest(Terrain);
        }
        FString Result = FString::Printf(TEXT("%08x:%s"), Terrain.GetChecksum(), *Terrain.GetChunkChecksums());
        for (const FIntPoint& Site : StressSites)
        {
            const int32 Top = VoxelPlayground::IslandSurfaceHeight(Site.X, Site.Y);
            for (const FVector2D& Offset : {FVector2D(-1, -1), FVector2D(0, 0), FVector2D(1, 1)})
            {
                const FVector Ray(Site.X + Offset.X, Site.Y + Offset.Y, Top + 80.0);
                FHitResult Floor;
                const bool bFound = Terrain.TraceRock(Ray, Ray - FVector(0, 0, 650), nullptr, Floor);
                Result += bFound ? FString::Printf(TEXT(":%d"), FMath::RoundToInt(Floor.ImpactPoint.Z)) : TEXT(":missing");
            }
        }
        int32 Count = 0;
        int32 Broken = 0;
        int32 Collidable = 0;
        for (TActorIterator<AIslandBreakable> It(Terrain.GetWorld()); It; ++It)
        {
            ++Count;
            Broken += It->IsBroken();
            Collidable += It->GetMesh().IsCollisionEnabled();
        }
        return Result + FString::Printf(TEXT(":props=%d/%d/%d"), Count, Broken, Collidable);
    }
}

AIslandProbePeer::AIslandProbePeer()
{
    bReplicates = true;
    bOnlyRelevantToOwner = true;
    PrimaryActorTick.bCanEverTick = true;
}

void AIslandProbePeer::ClientInspect_Implementation(int32 Epoch, bool bDigest)
{
    AIslandPlayerController* Player = Cast<AIslandPlayerController>(GetOwner());
    AIslandGameState* State = GetWorld()->GetGameState<AIslandGameState>();
    const AVoxelTerrain* Terrain = AVoxelTerrain::Find(GetWorld());
    if (!Player || !State || !Terrain || !Terrain->IsReady())
    {
        return;
    }
    int32 Boxes = 0;
    bool bVaultRevealed = false;
    for (TActorIterator<AIslandChest> It(GetWorld()); It; ++It)
    {
        ++Boxes;
        bVaultRevealed |= It->IsPrize() && It->IsPrizeRevealed();
    }
    if (FParse::Param(FCommandLine::Get(), TEXT("ExpeditionInteractionQA")) && State->Phase == EIslandPhase::Finished)
    {
        if (!bVaultRevealed) return;
        UE_LOG(LogTemp,Display,TEXT("TREASURE_VAULT_REVEALED winner=%d local_team=%d spent=%d"),
            State->Winner, Player->GetTeam(), Player->GetLocalClues().bKeySpent);
    }
    const FIslandClues Clues = Player->GetLocalClues();
    UE_LOG(LogTemp, Display, TEXT("ISLAND_QA_VIEW epoch=%d team=%d mask=%u boxes=%d winner=%d seq=%u"),
        Epoch, Player->GetTeam(), Clues.Mask, Boxes, State->Winner, Terrain->GetAppliedSequence());
    ServerMeasure(Epoch, Clues, Boxes, State->Winner, Terrain->GetAppliedSequence(), bDigest ? TerrainDigest(*Terrain) : FString());
}

void AIslandProbePeer::ClientOpenChest_Implementation()
{
    if (AIslandPlayerController* Player = Cast<AIslandPlayerController>(GetOwner()))
    {
        UE_LOG(LogTemp, Display, TEXT("ISLAND_QA_INTERACT team=%d"), Player->GetTeam());
        if (FParse::Param(FCommandLine::Get(), TEXT("ExpeditionFieldQA")) || FParse::Param(FCommandLine::Get(), TEXT("ExpeditionNeutralQA"))
            || FParse::Param(FCommandLine::Get(), TEXT("ExpeditionGamesQA")))
        {
            Player->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::E, IE_Pressed, 1.f));
            Player->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::E, IE_Released, 0.f));
            return;
        }
        Player->Interact();
    }
}

void AIslandProbePeer::ClientJoinCrew_Implementation(int32 Team, bool bReady)
{
    if (auto* Player = Cast<AIslandPlayerController>(GetOwner()))
    {
        Player->ServerChooseTeam(Team);
        Player->ServerSetRoomReady(bReady);
    }
}

void AIslandProbePeer::ClientStrike_Implementation(FVector Target)
{
    if (AIslandPlayerController* Player = Cast<AIslandPlayerController>(GetOwner()); Player && Player->GetHero())
    {
        Player->SubmitEdit(EVoxelAction::Punch, (Target - Player->GetHero()->GetAimOrigin()).GetSafeNormal());
    }
}

void AIslandProbePeer::ClientShowChart_Implementation()
{
    if (AIslandPlayerController* Player = Cast<AIslandPlayerController>(GetOwner()))
    {
        if (FParse::Param(FCommandLine::Get(), TEXT("ExpeditionGamesQA")))
        {
            Player->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::Tab, IE_Pressed, 1.f));
            FTimerHandle Shot, Release;
            const int32 ChartPoints = Player->GetLocalClues().Count();
            const auto* Round = GetWorld()->GetGameState<AIslandGameState>();
            const bool bFalse = FParse::Param(FCommandLine::Get(), TEXT("ExpeditionInteractionQA")) && Round &&
                Round->PartyOutcome == EIslandChestOutcome::Decoy;
            GetWorldTimerManager().SetTimer(Shot, FTimerDelegate::CreateWeakLambda(Player, [ChartPoints, bFalse]()
            {
                FString Dir;
                FParse::Value(FCommandLine::Get(), TEXT("FieldShotsDir="), Dir);
                const FString Name = bFalse ? TEXT("TreasureFalseChart.png") : ChartPoints == 1 ? TEXT("PartyTreasureHint.png") : FString::Printf(TEXT("TreasureChart%d.png"), ChartPoints);
                FScreenshotRequest::RequestScreenshot(FPaths::Combine(Dir, Name), true, false);
            }), .35f, false);
            GetWorldTimerManager().SetTimer(Release, FTimerDelegate::CreateWeakLambda(Player, [Player]()
            {
                Player->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::Tab, IE_Released, 0.f));
            }), .70f, false);
            return;
        }
        Player->OpenMap();
        FScreenshotRequest::RequestScreenshot(TEXT("TreasureIslandQA"), true, true);
        FTimerHandle CloseTimer;
        GetWorldTimerManager().SetTimer(CloseTimer, FTimerDelegate::CreateWeakLambda(Player, [Player]
        {
            Player->CloseMap();
        }), .5f, false);
    }
}

void AIslandProbePeer::ClientPartySpace_Implementation(bool bPressed, bool bCheckJump)
{
    if (AIslandPlayerController* Player = Cast<AIslandPlayerController>(GetOwner()))
    {
        Player->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::SpaceBar, bPressed ? IE_Pressed : IE_Released,
            bPressed ? 1.0f : 0.0f));
        if (bCheckJump)
        {
            ObserveJumpUntil = FPlatformTime::Seconds() + 1.0;
        }
    }
}

void AIslandProbePeer::ClientStress_Implementation(int32 Stage)
{
    StressStage = Stage;
    NextRequestAt = FPlatformTime::Seconds();
}

void AIslandProbePeer::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    auto* Controller = Cast<AIslandPlayerController>(GetOwner());
    if (Controller && Controller->IsLocalController() && !Controller->HasAuthority()
        && FParse::Param(FCommandLine::Get(), TEXT("ExpeditionAbortQA")))
        TickAbortRecovery(*Controller);
    if (Controller && Controller->IsLocalController() && FParse::Param(FCommandLine::Get(), TEXT("ExpeditionInteractionQA")))
        TickInteractionProof(*Controller);
    if (Controller && Controller->IsLocalController() && FParse::Param(FCommandLine::Get(), TEXT("ExpeditionArenaViewsQA")))
        TickArenaViews(*Controller);
    if (Controller && Controller->IsLocalController())
    {
        if (HeldArenaKey.IsValid() && FPlatformTime::Seconds() >= ReleaseArenaKeyAt)
        {
            Controller->InputKey(FInputKeyEventArgs::CreateSimulated(HeldArenaKey, IE_Released, 0.f));
            HeldArenaKey = FKey();
        }
        if (GameToolTarget && !HeldArenaKey.IsValid()) TickGameTool(*Controller);
        if (TerrainSampleStage != INDEX_NONE) TickArenaTerrainSample(*Controller);
    }
    if (Controller && Controller->IsLocalController() && GameWalkTarget) TickGameWalk();
    if (Controller && Controller->IsLocalController() && FParse::Param(FCommandLine::Get(), TEXT("ExpeditionFieldQA")))
        TickFieldInput();
    else if (Controller && Controller->IsLocalController() && (FParse::Param(FCommandLine::Get(), TEXT("ExpeditionNeutralQA")) || FParse::Param(FCommandLine::Get(), TEXT("ExpeditionMudQA"))))
        TickFieldWalk(DeltaSeconds);
    const double Now = FPlatformTime::Seconds();
    if (Controller && Now < ObserveJumpUntil)
    {
        if (const AVoxelCharacter* Hero = Controller->GetHero(); Hero && Hero->GetVelocity().Z > 100.0)
        {
            UE_LOG(LogTemp, Display, TEXT("PARTY_QA_ISLAND_JUMP"));
            ObserveJumpUntil = 0.0;
        }
    }
    if (Controller && Controller->IsLocalController() && StressStage > 0 && Now >= NextRequestAt)
    {
        NextRequestAt = Now + .56;
        ServerStress(StressStage);
    }
}

void AIslandProbePeer::ServerMeasure_Implementation(int32 Epoch, FIslandClues Clues, int32 Boxes, int32 Winner,
    uint32 Sequence, const FString& Digest)
{
    if (AIslandProbe* Probe = Coordinator.Get())
    {
        Probe->Record(Seat, Epoch, Clues, Boxes, Winner, Sequence, Digest);
    }
}

void AIslandProbePeer::ServerStress_Implementation(int32 Stage)
{
    if (AIslandProbe* Probe = Coordinator.Get())
    {
        Probe->RequestStress(Seat, Stage);
    }
}

bool AIslandProbe::TryGetWalkTarget(const AIslandGameMode* Mode, int32 Index, FVector& Target, bool& Exposed)
{
#if !UE_BUILD_SHIPPING
    if (Mode && Mode->HasAuthority() && Mode->IsPlaying()
        && FParse::Param(FCommandLine::Get(),TEXT("ExpeditionTreasureWalkQA"))
        && Index>=0 && Index<=IslandRules::PrizeIndex && Mode->Exposed.IsValidIndex(Index))
    {
        Target = Mode->HiddenLayout.GetCenter(Index);
        Exposed = IsValid(Mode->Exposed[Index]);
        return true;
    }
#endif
    return false;
}

AIslandProbe::AIslandProbe()
{
    PrimaryActorTick.bCanEverTick = true;
    FParse::Value(FCommandLine::Get(), TEXT("ExpeditionPlayers="), ExpeditionPlayers);
    bOneCrew = FParse::Param(FCommandLine::Get(), TEXT("ExpeditionTogether"));
    bMudScenario = FParse::Param(FCommandLine::Get(), TEXT("ExpeditionMudQA"));
    bEarlyPrizeCheck = FParse::Param(FCommandLine::Get(), TEXT("ExpeditionEarlyPrize"));
    bNeutralScenario = FParse::Param(FCommandLine::Get(), TEXT("ExpeditionNeutralQA"));
    bGamesScenario = FParse::Param(FCommandLine::Get(), TEXT("ExpeditionGamesQA"));
    bAllVenueBuildings = FParse::Param(FCommandLine::Get(), TEXT("ExpeditionVenueDemolitionQA"));
    bVenueProof = bAllVenueBuildings || FParse::Param(FCommandLine::Get(), TEXT("ExpeditionVenueSmokeQA"));
    bGamesDrop = FParse::Param(FCommandLine::Get(), TEXT("ExpeditionGamesDropQA"));
    FString GameName;
    if (FParse::Value(FCommandLine::Get(), TEXT("ExpeditionGameOnly="), GameName)) OnlyGame = FName(*GameName);
    bFieldScenario = FParse::Param(FCommandLine::Get(), TEXT("ExpeditionFieldQA"));
    bProfileFrames = FParse::Param(FCommandLine::Get(), TEXT("VoxelProfile"));
}

void AIslandProbe::Require(bool bSuccess, const TCHAR* Message)
{
    UE_LOG(LogTemp, Display, TEXT("ISLAND_QA_CHECK passed=%d %s"), bSuccess, Message);
    if (!bSuccess)
    {
        bFailed = true;
        FPlatformMisc::RequestExitWithStatus(false, 1);
    }
}

void AIslandProbe::Advance(EStep Next)
{
    Step = Next;
    StepAt = FPlatformTime::Seconds();
    LastAction = StepAt;
    bIssued = false;
}

void AIslandProbe::Place(int32 Seat, const FVector& Center)
{
    auto& Player = *Mode->GetPlayers()[Seat];
    Player.GetHero()->ReturnToStart(FTransform(FRotator::ZeroRotator, Center));
    Player.ClientResetView(FRotator::ZeroRotator);
}

void AIslandProbe::PrepareDig(int32 Index)
{
    NextDig = Index;
    const FVector Point = Mode->HiddenLayout.GetCenter(Index);
    const double Top = Mode->GetTerrain()->GetDensity()->SurfaceAt(FVector2D(Point)).Get(
        VoxelPlayground::IslandSurfaceHeight(FMath::RoundToInt(Point.X), FMath::RoundToInt(Point.Y)));
    if (ExpeditionPlayers > 0)
    {
        UE_LOG(LogTemp, Display, TEXT("EXPEDITION_QA_EXCAVATE box=%d x=%.0f y=%.0f z=%.0f surface=%.0f"), Index, Point.X, Point.Y, Point.Z, Top);
    }
    Place(0, {Point.X, Point.Y, Top + (bPartyScenario ? 165.0 : 97.0)});
    Advance(EStep::Dig);
}

void AIslandProbe::PrepareOpen(int32 Team)
{
    OpeningTeam = Team;
    const FVector Site = Mode->GetPlayers()[0]->GetHero()->GetActorLocation();
    Place(Team, Site);
    Place(Team + IslandRules::TeamCount, Site);
}

void AIslandProbe::InspectThen(EStep After, bool bDigest)
{
    ++Epoch;
    Reports.Reset();
    ExpectedDigest = bDigest ? TerrainDigest(*Mode->GetTerrain()) : FString();
    AfterInspection = After;
    Advance(EStep::Inspect);
}

void AIslandProbe::Record(int32 Seat, int32 ReportEpoch, const FIslandClues& Clues, int32 Boxes, int32 Winner,
    uint32 Sequence, const FString& Digest)
{
    if (Step != EStep::Inspect || Epoch != ReportEpoch || !Mode->GetPlayers().IsValidIndex(Seat))
    {
        return;
    }

    const int32 Team = CastChecked<AIslandPlayerController>(Mode->GetPlayers()[Seat])->GetTeam();
    const FIslandClues& Expected = Mode->TeamClues[Team];
    const uint8 Visible = Clues.Mask | Clues.ScoutedMask;
    bool bPrivate = Clues.Points.Num() == IslandRules::ClueCount && (Visible & ~(Expected.Mask | Expected.ScoutedMask)) == 0
        && (Expected.Complete() || Clues.Crossing.IsNearlyZero());
    if (bPrivate)
    {
        for (int32 Index = 0; Index < IslandRules::ClueCount; ++Index)
        {
            bPrivate &= Clues.Points[Index].Equals((Visible & (1u << Index)) != 0 ? Expected.Points[Index] : FVector2D::ZeroVector, .01);
        }
    }
    if (!bPrivate)
    {
        Require(false, TEXT("owner received a foreign or unknown clue coordinate"));
        return;
    }

    int32 Exposed = 0;
    for (const AIslandChest* Chest : Mode->Exposed)
    {
        Exposed += Chest != nullptr;
    }
    if (Clues.Mask != Expected.Mask || Clues.ScoutedMask != Expected.ScoutedMask || Clues.bKeySpent != Expected.bKeySpent || Clues.bUsesKeyFragments != Expected.bUsesKeyFragments || Boxes != Exposed || Winner != Mode->GetGameState<AIslandGameState>()->Winner
        || !Clues.Crossing.Equals(Expected.Crossing, .01)
        || (!ExpectedDigest.IsEmpty() && Sequence != Mode->GetTerrain()->GetAppliedSequence()))
    {
        return; // A different actor channel can deliver the last chest or team update slightly later.
    }
    if (!ExpectedDigest.IsEmpty() && (Digest != ExpectedDigest || Digest.Contains(TEXT(":missing"))))
    {
        Require(false, TEXT("global density, chunk density or seam floor collision mismatch"));
        return;
    }
    Reports.Add(Seat);
}

void AIslandProbe::StartStress(int32 Stage)
{
    GroupStart = Mode->GetTerrain()->GetAppliedSequence();
    if (Stage == 1)
    {
        StressStart = GroupStart;
        for (int32 Seat = 0; Seat < IslandRules::Seats; ++Seat)
        {
            Mode->RecoverPlayer(*Mode->GetPlayers()[Seat]);
        }
        if (FParse::Param(FCommandLine::Get(), TEXT("IslandScreenshot")))
        {
            Peers[0]->ClientShowChart();
        }
    }
    Advance(Stage == 1 ? EStep::StressShared : EStep::StressApart);
    LastFrame = FPlatformTime::Seconds();
    for (AIslandProbePeer* Peer : Peers)
    {
        Peer->ClientStress(Stage);
    }
}

void AIslandProbe::RequestStress(int32 Seat, int32 Stage)
{
    if (Stage == 3 && RoomCheck == ERoomCheck::Stress && Mode->GetPlayers().IsValidIndex(Seat))
    {
        const FIntPoint Site = ExpeditionStressSites[Seat / 2];
        const bool bFill = (Seat % 2) == 0;
        const int32 Z = VoxelPlayground::BaseHeight(EVoxelLayout::Expedition, Site.X, Site.Y);
        Mode->EnqueueEdit(*Mode->GetPlayers()[Seat], bFill ? EVoxelAction::Fill : EVoxelAction::Punch,
            FVector::ForwardVector, FIntVector(Site.X, Site.Y, Z - (bFill ? 130 : 65)));
        return;
    }
    if ((Stage == 1 && Step != EStep::StressShared) || (Stage == 2 && Step != EStep::StressApart)
        || Stage < 1 || Stage > 2)
    {
        return;
    }
    if (!Mode->GetPlayers().IsValidIndex(Seat))
    {
        return;
    }
    const FIntPoint Position = StressSites[Stage == 1 ? 0 : Seat % IslandRules::TeamCount];
    const bool bFill = Seat < IslandRules::TeamCount;
    const int32 Z = VoxelPlayground::IslandSurfaceHeight(Position.X, Position.Y);
    Mode->EnqueueEdit(*Mode->GetPlayers()[Seat], bFill ? EVoxelAction::Fill : EVoxelAction::Punch,
        FVector::ForwardVector, FIntVector(Position.X, Position.Y, Z - (bFill ? 130 : 65)));
}

void AIslandProbe::TickExpedition(double Now)
{
    Mode = GetWorld()->GetAuthGameMode<AIslandGameMode>();
    if (!Mode || !Mode->GetTerrain() || !Mode->GetTerrain()->IsReady())
    {
        return;
    }
    auto* Round = Mode->GetGameState<AIslandGameState>();
    const auto& Players = Mode->GetPlayers();
    if (RoomCheck == ERoomCheck::Join)
    {
        if (Players.Num() != ExpeditionPlayers || !Algo::AllOf(Players, [](const AVoxelPlayerController* P)
            { return P->GetPlayerState<AIslandPlayerState>()->bMapReady && P->GetHero(); }))
        {
            return;
        }
        Require(Round->Phase == EIslandPhase::Waiting && !Mode->IsPlaying(), TEXT("room waits for the host at every occupancy"));
        if (!ReturningExpeditionMembers.IsEmpty())
        {
            for (const AVoxelPlayerController* Player : Players)
            {
                const auto* Member = Player->GetPlayerState<AIslandPlayerState>();
                const int32* OldTeam = ReturningExpeditionMembers.Find(Member->MemberId);
                Require(OldTeam && *OldTeam == Member->Team && !Member->bRoomReady, TEXT("return preserves team and clears ready"));
            }
            Require(Round->Winner == INDEX_NONE && Mode->GetTerrain()->GetAppliedSequence() == 0
                && Round->RoomSettings.RoomName == TEXT("Expedition QA") && Mode->Exposed.IsEmpty(), TEXT("return restores settings and pristine island"));
            UE_LOG(LogTemp, Display, TEXT("EXPEDITION_QA_DONE players=%d teams=%d returned=1"), ExpeditionPlayers, bOneCrew ? 1 : ExpeditionPlayers);
            Advance(EStep::Done);
            return;
        }
        auto* Host = CastChecked<AIslandPlayerController>(Players[0]);
        FIslandRoomSettings Settings = Round->RoomSettings;
        Require(Settings.MaxPlayers == 8 && Settings.MaxTeams == 8 && Settings.TeamCapacity == 8, TEXT("default expedition supports eight players and crews"));
        Settings.RoomName = TEXT("Expedition QA");
        FParse::Value(FCommandLine::Get(), TEXT("ExpeditionDecoys="), Settings.DecoyCount);
        Host->ServerConfigureRoom(Settings);
        Require(Round->RoomSettings.DecoyCount == Settings.DecoyCount, TEXT("room host can configure the number of additional decoys"));
        if (Players.Num() > 1)
        {
            Host->ServerStartExpedition();
            Require(Round->Phase == EIslandPhase::Waiting, TEXT("host cannot start while a guest is unready"));
        }
        for (int32 Seat = 0; Seat < Players.Num(); ++Seat)
        {
            auto* Peer = GetWorld()->SpawnActor<AIslandProbePeer>();
            Peer->SetOwner(Players[Seat]);
            Peer->Seat = Seat;
            Peer->Coordinator = this;
            Peers.Add(Peer);
            Peer->ClientJoinCrew(ExpeditionTeamForSeat(Seat), true);
        }
        RoomCheck = ERoomCheck::Ready;
        StepAt = Now;
        return;
    }
    if (RoomCheck == ERoomCheck::Return && !DepartureTeams.IsEmpty() && Players.Num() == ExpeditionPlayers - 1)
    {
        Require(Mode->IsPlaying() && Round->ActiveTeams == DepartureTeams.Num() && Round->LaunchedTeams == DepartureTeams,
            TEXT("an empty crew keeps its launch slot in the map"));
        for (const AVoxelPlayerController* Existing : Players)
        {
            const auto* Member = Existing->GetPlayerState<AIslandPlayerState>();
            const FTransform* Before = DepartureBerths.Find(Member->MemberId);
            FTransform Mapped;
            Require(Before && Before->Equals(Existing->StartTransform) &&
                AExpeditionDock::FindStart(GetWorld(), Round->LaunchedTeams, Member->Team, 0, Mapped) && Mapped.Equals(*Before),
                TEXT("remaining player's return point and chart still identify the original berth"));
        }
        UE_LOG(LogTemp, Display, TEXT("EXPEDITION_QA_DROP_DONE launched=%d remaining=%d"), ExpeditionPlayers, Players.Num());
        Advance(EStep::Done);
        return;
    }
    if (Players.Num() != ExpeditionPlayers && !(bGamesDrop && bGameDropSent && Players.Num() == ExpeditionPlayers - 1))
    {
        return;
    }
    const bool bRealMazeClock = RoomCheck == ERoomCheck::Games && GameCount == 0
        && FParse::Param(FCommandLine::Get(), TEXT("ExpeditionMazeQA")) && FParse::Param(FCommandLine::Get(), TEXT("IslandScreenshot"));
    if (Now - StepAt > (bRealMazeClock ? 280.0 : RoomCheck == ERoomCheck::Stress ? 240.0 : 100.0))
    {
        UE_LOG(LogTemp, Error, TEXT("ISLAND_QA_TIMEOUT room=%d index=%d phase=%d"), int32(RoomCheck), NextDig, int32(Round->Phase));
        Require(false, TEXT("expedition timed out"));
        return;
    }
    auto* Host = CastChecked<AIslandPlayerController>(Players[0]);
    if (RoomCheck == ERoomCheck::Ready && Round->CanLaunch())
    {
        TArray<FVector> Starts;
        for (int32 Seat = 0; Seat < Players.Num(); ++Seat)
        {
            Require(Players[Seat]->GetPlayerState<AIslandPlayerState>()->Team == ExpeditionTeamForSeat(Seat), TEXT("requested team assignment applied"));
            const FVector Position = Players[Seat]->StartTransform.GetLocation();
            for (const FVector& Previous : Starts)
            {
                Require(FVector::DistSquared(Previous, Position) >= FMath::Square(70.0), TEXT("spawn berths do not overlap"));
            }
            Starts.Add(Position);
        }
        Host->ServerStartExpedition();
        Require(Round->Phase == EIslandPhase::Starting, TEXT("host starts synchronized countdown"));
        // The lobby scenarios exercise the cancellation round-trip. Arena scenarios follow a normal departure.
        if (Players.Num() > 1 && !bGamesScenario)
        {
            Peers.Last()->ClientJoinCrew(ExpeditionTeamForSeat(Players.Num() - 1), false);
            RoomCheck = ERoomCheck::Withdraw;
        }
        else
        {
            RoomCheck = ERoomCheck::Depart;
        }
        return;
    }
    if (RoomCheck == ERoomCheck::Withdraw)
    {
        if (Round->Phase == EIslandPhase::Waiting && !bIssued)
        {
            Require(!Round->CanLaunch(), TEXT("guest withdrawing cancels departure and unlocks the room"));
            Peers.Last()->ClientJoinCrew(ExpeditionTeamForSeat(Players.Num() - 1), true);
            bIssued = true;
        }
        if (Round->CanLaunch() && bIssued)
        {
            Host->ServerStartExpedition();
            RoomCheck = ERoomCheck::Depart;
        }
        return;
    }
    if (RoomCheck == ERoomCheck::Depart && Mode->IsPlaying())
    {
        Require(Round->ActiveTeams == ExpeditionTeamForSeat(Players.Num() - 1) + 1
            && Mode->HiddenLayout.Num() == IslandRules::ClueCount + 1 + Round->RoomSettings.DecoyCount
            && Mode->HiddenLayout.Decoys.Num() == Round->RoomSettings.DecoyCount,
            TEXT("occupied crews depart with four genuine sites, one vault and the host's additional decoys"));
        UE_LOG(LogTemp, Display, TEXT("EXPEDITION_CHEST_LAYOUT true=4 false=%d vault=1"), Mode->HiddenLayout.Decoys.Num());
        if (bGamesScenario)
        {
            StartNextGame();
            return;
        }
        if (bMudScenario)
        {
            RoomCheck = ERoomCheck::Mud;
            SetMudStep(0, Now);
            return;
        }
        if (bNeutralScenario)
        {
            if (FParse::Param(FCommandLine::Get(),TEXT("NeutralDemolitionQA")))
            {
                RoomCheck=ERoomCheck::Dig;
                PrepareDig(0);
            }
            else StartNeutralCheck(Now);
            return;
        }
        if (bFieldScenario)
        {
            StartFieldCheck(Now);
            return;
        }
        if (FParse::Param(FCommandLine::Get(), TEXT("ExpeditionDrop")))
        {
            DepartureTeams = Round->LaunchedTeams;
            for (const AVoxelPlayerController* Existing : Players)
            {
                const auto* Member = Existing->GetPlayerState<AIslandPlayerState>();
                DepartureBerths.Add(Member->MemberId, Existing->StartTransform);
            }
            UE_LOG(LogTemp, Display, TEXT("EXPEDITION_QA_DROP_READY"));
            RoomCheck = ERoomCheck::Return;
            return;
        }
        if (ExpeditionPlayers == 8 && FParse::Param(FCommandLine::Get(), TEXT("ExpeditionStress")))
        {
            RoomCheck = ERoomCheck::Stress;
            StepAt = LastFrame = Now;
            StressStart = Mode->GetTerrain()->GetAppliedSequence();
            for (AIslandProbePeer* Peer : Peers) Peer->ClientStress(3);
            return;
        }
        RoomCheck = ERoomCheck::Dig;
        if (bEarlyPrizeCheck) PrepareDig(IslandRules::PrizeIndex);
        else PrepareDig(FParse::Param(FCommandLine::Get(), TEXT("ExpeditionRepro")) ? 3 : 0);
        return;
    }
    if (RoomCheck == ERoomCheck::Games) { TickExpeditionGame(Now); return; }
    if (RoomCheck == ERoomCheck::Mud) { TickMudCheck(Now); return; }
    if (RoomCheck == ERoomCheck::Neutral)
    {
        TickNeutralCheck(Now);
        return;
    }
    if (RoomCheck == ERoomCheck::Field)
    {
        TickFieldCheck(Now);
        return;
    }
    if (RoomCheck == ERoomCheck::Stress)
    {
        FrameTimes.Add((Now - LastFrame) * 1000.0);
        LastFrame = Now;
        if (bProfileFrames)
        {
            // Match stat unit's raw clocks. RHI reports the most recently completed GPU frame.
            const uint32 Cycles[] = { GGameThreadTime, GRenderThreadTime, RHIGetGPUFrameCycles(),
                GGameThreadWaitTime, GRenderThreadWaitTime, GSwapBufferTime };
            static_assert(UE_ARRAY_COUNT(Cycles) == static_cast<int32>(EProfileClock::Count));
            for (int32 Clock = 0; Clock < UE_ARRAY_COUNT(Cycles); ++Clock)
            {
                if (Cycles[Clock] != 0)
                    ProfileFrames[Clock].Add(FPlatformTime::ToMilliseconds(Cycles[Clock]));
            }
        }
        if (Mode->GetTerrain()->GetAppliedSequence() - StressStart >= 300)
        {
            for (AIslandProbePeer* Peer : Peers) Peer->ClientStress(0);
            RoomCheck = ERoomCheck::Drain;
            StepAt = Now;
        }
        return;
    }
    if (RoomCheck == ERoomCheck::Drain && !Mode->GetTerrain()->IsBusy() && Mode->Requests.IsEmpty())
    {
        FrameTimes.Sort();
        const auto& Terrain = *Mode->GetTerrain();
        UE_LOG(LogTemp, Display, TEXT("EXPEDITION_QA_STRESS first=%u last=%u frame95=%.3f frames=%d"),
            StressStart + 1, Terrain.GetAppliedSequence(), FrameTimes[FMath::CeilToInt(FrameTimes.Num() * .95) - 1], FrameTimes.Num());
        if (bProfileFrames)
        {
            const TCHAR* ClockNames[] = { TEXT("game"), TEXT("render"), TEXT("gpu"), TEXT("game_wait"), TEXT("render_wait"), TEXT("present") };
            static_assert(UE_ARRAY_COUNT(ClockNames) == static_cast<int32>(EProfileClock::Count));
            for (int32 Clock = 0; Clock < UE_ARRAY_COUNT(ClockNames); ++Clock)
            {
                TArray<double>& Samples = ProfileFrames[Clock];
                Samples.Sort();
                const double P95 = Samples.IsEmpty() ? -1.0 : Samples[FMath::CeilToInt(Samples.Num() * .95) - 1];
                UE_LOG(LogTemp, Display, TEXT("EXPEDITION_QA_CLOCK source=%s p95=%.3f valid=%d wall_frames=%d"),
                    ClockNames[Clock], P95, Samples.Num(), FrameTimes.Num());
            }
        }
        InspectThen(EStep::Dig, true);
        RoomCheck = ERoomCheck::VerifyEdits;
        return;
    }
    if (RoomCheck == ERoomCheck::VerifyEdits && Now - LastAction >= .65)
    {
        LastAction = Now;
        if (Reports.Num() == Players.Num())
        {
            UE_LOG(LogTemp, Display, TEXT("EXPEDITION_QA_TERRAIN peers=%d digest=%s"), Reports.Num(), *ExpectedDigest);
            RoomCheck = ERoomCheck::Dig;
            PrepareDig(0);
        }
        else
        {
            for (int32 Seat = 0; Seat < Players.Num(); ++Seat)
                if (!Reports.Contains(Seat)) Peers[Seat]->ClientInspect(Epoch, true);
        }
        return;
    }
    if (RoomCheck == ERoomCheck::Dig && Now - LastAction > .65 && !Mode->GetTerrain()->IsBusy())
    {
        if (bEarlyPrizeCheck && OnPrize() && Mode->TeamClues[Host->GetTeam()].Mask == 0)
        {
            const FBox Bounds = FBox::BuildAABB(Mode->HiddenLayout.GetCenter(NextDig), IslandRules::ChestExtent);
            if (Mode->GetTerrain()->IsVolumeClear(Bounds))
            {
                Require(!Mode->Exposed[NextDig], TEXT("an early blind dig clears the prize site without revealing its actor"));
                PrepareDig(0);
                return;
            }
        }
        if (!Mode->Exposed[NextDig])
        {
            // A buried corner can remain above a deep vertical shaft on a slope. Walk the
            // probe around the target's footprint and dig the highest remaining column.
            const FVector Target = Mode->HiddenLayout.GetCenter(NextDig);
            TOptional<FVector> Highest;
            for (double X : {-58.0, 0.0, 58.0})
            {
                for (double Y : {-48.0, 0.0, 48.0})
                {
                    const FVector Column = Target + FVector(X, Y, 0);
                    FHitResult Rock;
                    if (Mode->GetTerrain()->TraceRock(Column + FVector(0, 0, 1500), Column - FVector(0, 0, 1100), Host->GetHero(), Rock)
                        && (!Highest || Rock.ImpactPoint.Z > Highest->Z))
                    {
                        Highest = Rock.ImpactPoint;
                    }
                }
            }
            if ((!Highest || Highest->Z < Target.Z - 60.0 || Now - StepAt > 10.0)
                && TrySideApproach(*Host, *Mode->GetTerrain(), Target))
            {
                LastAction = Now;
                return;
            }
            if (Highest)
            {
                Host->GetHero()->TeleportTo(Highest.GetValue() + FVector(0, 0, 105), FRotator::ZeroRotator, false, true);
                Host->GetHero()->GetCharacterMovement()->StopMovementImmediately();
            }
            else
            {
                UE_LOG(LogTemp, Warning, TEXT("EXPEDITION_QA_NO_SURFACE box=%d hero=%s"), NextDig, *Host->GetHero()->GetActorLocation().ToCompactString());
            }
            if (FParse::Param(FCommandLine::Get(), TEXT("ExpeditionRepro")))
            {
                const auto& Density = *Mode->GetTerrain()->GetDensity();
                const FVector Min = (Target - IslandRules::ChestExtent - FVector(Density.Grid.Origin)) / VoxelPlayground::Spacing;
                const FVector Max = (Target + IslandRules::ChestExtent - FVector(Density.Grid.Origin)) / VoxelPlayground::Spacing;
                FString Residue;
                int32 Solid = 0, Inside = 0;
                const FBox Box = FBox::BuildAABB(Target, IslandRules::ChestExtent);
                for (int32 Z = FMath::FloorToInt(Min.Z); Z <= FMath::CeilToInt(Max.Z); ++Z)
                {
                    for (int32 Y = FMath::FloorToInt(Min.Y); Y <= FMath::CeilToInt(Max.Y); ++Y)
                    {
                        for (int32 X = FMath::FloorToInt(Min.X); X <= FMath::CeilToInt(Max.X); ++X)
                        {
                            const FIntVector Grid(X,Y,Z);
                            if (Density.Sample(Grid) >= 0) continue;
                            const FVector P(Density.Grid.Origin + Grid * VoxelPlayground::Spacing);
                            Inside += Box.IsInsideOrOn(P);
                            if (Solid < 8) Residue += (P - Target).ToCompactString() + TEXT(" ");
                            ++Solid;
                        }
                    }
                }
                const FVector HeroPos = Host->GetHero()->GetActorLocation();
                FHitResult Aim;
                FCollisionQueryParams Params(SCENE_QUERY_STAT(ExpeditionShaftDiagnostic), false, Host->GetHero());
                GetWorld()->LineTraceSingleByChannel(Aim, HeroPos, HeroPos - FVector(0,0,235), ECC_Visibility, Params);
                UE_LOG(LogTemp, Display, TEXT("EXPEDITION_QA_SHAFT box=%d target=%s footZ=%.1f character=%s rockSurface=%s actor=%s component=%s impact=%s remainingSolid=%d samplesInBox=%d residualOffsets=%s"),
                    NextDig, *Target.ToCompactString(), HeroPos.Z - Host->GetHero()->GetCapsuleComponent()->GetScaledCapsuleHalfHeight(), *HeroPos.ToCompactString(),
                    Highest ? *Highest->ToCompactString() : TEXT("none"), *GetNameSafe(Aim.GetActor()), *GetNameSafe(Aim.GetComponent()),
                    *Aim.ImpactPoint.ToCompactString(), Solid, Inside, *Residue);
            }
            Host->SubmitEdit(EVoxelAction::Punch, -FVector::UpVector);
            LastAction = Now;
            return;
        }
        if (bNeutralScenario)
        {
            StartNeutralCheck(Now);
            return;
        }
        if (bFieldScenario)
        {
            BeginLockCheck(Now);
            return;
        }
        // Cooperative players alternate discoveries. In a race, the final crew wins.
        if (!bGamesScenario) ExpeditionOpener = bOneCrew ? NextDig % Players.Num() : Players.Num() - 1;
        if (ExpeditionOpener != 0)
        {
            Mode->RecoverPlayer(*Host);
        }
        auto* Opener = Players[ExpeditionOpener]->GetHero();
        Opener->GetCharacterMovement()->StopMovementImmediately();
        if (OnPrize())
        {
            auto* Controller = CastChecked<AIslandPlayerController>(Players[ExpeditionOpener]);
            auto* Vault = Mode->Exposed[NextDig].Get();
            const FVector Chest = Vault->GetActorLocation();
            const auto* Capsule = Opener->GetCapsuleComponent();
            const FCollisionShape Shape = FCollisionShape::MakeCapsule(Capsule->GetScaledCapsuleRadius(), Capsule->GetScaledCapsuleHalfHeight());
            const FCollisionResponseParams Responses(Capsule->GetCollisionResponseToChannels());
            const FCollisionQueryParams Query(SCENE_QUERY_STAT(ExpeditionVaultApproach), false, Opener);
            const FVector EyeOffset = Opener->GetAimOrigin() - Opener->GetActorLocation();
            FHitResult PreviousHit;
            GetWorld()->LineTraceSingleByChannel(PreviousHit, Chest + FVector(-30.,52.,62.) + EyeOffset, Chest, ECC_Visibility, Query);
            UE_LOG(LogTemp, Display, TEXT("EXPEDITION_QA_VAULT_RAY actor=%s component=%s distance=%.1f"),
                *GetNameSafe(PreviousHit.GetActor()), *GetNameSafe(PreviousHit.GetComponent()), PreviousHit.Distance);
            TOptional<FVector> DigStand;
            FVector DigAim = -FVector::UpVector;
            double BestDistance = MAX_dbl;
            bool bInReach = false;
            for (double Radius : {0., 35., 65., 95., 125., 155., 180.})
            {
                const int32 Steps = Radius == 0. ? 1 : 24;
                for (int32 Heading = 0; Heading < Steps; ++Heading)
                {
                    const double Angle = Heading * UE_TWO_PI / Steps;
                    const FVector Column = Chest + Radius * FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0.);
                    FHitResult Floor;
                    if (!GetWorld()->SweepSingleByChannel(Floor, Column + FVector(0.,0.,1500.), Column - FVector(0.,0.,1100.),
                        FQuat::Identity, Capsule->GetCollisionObjectType(), Shape, Query, Responses)
                        || !Opener->GetCharacterMovement()->IsWalkable(Floor)) continue;
                    const FVector Stand = Floor.Location + FVector(0.,0.,3.);
                    const FVector Eye = Stand + EyeOffset;
                    FHitResult Sight;
                    GetWorld()->LineTraceSingleByChannel(Sight, Eye, Chest, ECC_Visibility, Query);
                    const double Distance = FVector::Dist(Eye, Chest);
                    if (Distance <= IslandRules::OpenReach && Sight.GetActor() == Vault
                        && Opener->TeleportTo(Stand, FRotator(0., (Chest - Eye).Rotation().Yaw, 0.))
                        && Controller->GetReachableChest() == Vault)
                    {
                        Controller->ClientResetView((Chest - Opener->GetAimOrigin()).Rotation());
                        UE_LOG(LogTemp, Display, TEXT("EXPEDITION_QA_VAULT_REACH actor=%s component=%s distance=%.1f stand=%s"),
                            *GetNameSafe(Sight.GetActor()), *GetNameSafe(Sight.GetComponent()), Distance, *Stand.ToCompactString());
                        bInReach = true;
                        break;
                    }
                    // A cleared chest volume can still have a high rim. Use another normal
                    // punch from a walkable surface to lower the rim or break the intervening wall.
                    if (Distance >= BestDistance || Stand.Z < Chest.Z - 90.) continue;
                    BestDistance = Distance;
                    DigStand = Stand;
                    DigAim = Sight.GetActor() == Mode->GetTerrain() && Sight.Distance < 220.
                        ? (Chest - Eye).GetSafeNormal() : -FVector::UpVector;
                }
                if (bInReach) break;
            }
            if (!bInReach && DigStand && Now - StepAt < 60. && Opener->TeleportTo(*DigStand, FRotator::ZeroRotator))
            {
                UE_LOG(LogTemp, Display, TEXT("EXPEDITION_QA_VAULT_APPROACH distance=%.1f stand=%s aim=%s"),
                    BestDistance, *DigStand->ToCompactString(), *DigAim.ToCompactString());
                Controller->SubmitEdit(EVoxelAction::Punch, DigAim);
                LastAction = Now;
                return;
            }
            Require(bInReach, TEXT("final vault is within legal reach of the key holder"));
            Opener->GetCharacterMovement()->DisableMovement();
        }
        else
        {
            Opener->TeleportTo(Mode->HiddenLayout.GetCenter(NextDig) + FVector(0, 0, 98), FRotator::ZeroRotator, false, true);
        }
        const bool bGameChest = !OnPrize();
        if (bGameChest)
        {
            GameTerrainCRC = Mode->GetTerrain()->GetChecksum();
            OriginalPawns.Reset();
            GamePreviousClues.Reset();
            GameBeforePositions.Reset();
            UnsafeReturnSeats.Reset();
            BoostMaskBeforeGame = Round->DecoyToolBoostMask;
            ActiveBeforeGame = Round->ActiveDecoyEvent;
            CeasefireGroundSequence = INDEX_NONE;
            bGameStarted = bGameOverviewShot = bStoneDug = false;
            StoneProofCell.Reset();
            TerrainStage = 0;
            TerrainInputSequence = INDEX_NONE;
            bTerrainSampleRequested = false;
            GameAction = 0;
            GameWinnerMask = 0;
            Reports.Reset();
            for (const AVoxelPlayerController* Existing : Players)
            {
                OriginalPawns.Add(Existing->GetPawn());
                const int32 Team = Existing->GetPlayerState<AIslandPlayerState>()->Team;
                GamePreviousClues.Add(Team, Mode->TeamClues[Team]);
                GameBeforePositions.Add(Existing->GetPawn()->GetActorLocation());
            }
        }
        RoomCheck = bGameChest ? ERoomCheck::Games : ERoomCheck::Open;
        StepAt = LastAction = Now;
        return;
    }
    if (RoomCheck == ERoomCheck::Open && Now - LastAction > .55)
    {
        const int32 Team = Players[ExpeditionOpener]->GetPlayerState<AIslandPlayerState>()->Team;
        if (OnPrize() ? Round->Winner == Team : (Mode->TeamClues[Team].Mask & (1u << NextDig)) != 0)
        {
            Require(Mode->TeamClues[Team].bKeySpent && !Mode->TeamClues[Team].HasKey(), TEXT("winning crew spent the crafted key on the vault"));
            ExpectedDigest.Reset();
            Reports.Reset();
            ++Epoch;
            Step = EStep::Inspect;
            RoomCheck = ERoomCheck::Inspect;
            StepAt = Now;
        }
        else
        {
            Peers[ExpeditionOpener]->ClientOpenChest();
        }
        LastAction = Now;
        return;
    }
    if (RoomCheck == ERoomCheck::Inspect && Now - LastAction > .50)
    {
        LastAction = Now;
        if (Reports.Num() < Players.Num())
        {
            for (int32 Seat = 0; Seat < Players.Num(); ++Seat)
            {
                if (!Reports.Contains(Seat)) Peers[Seat]->ClientInspect(Epoch, false);
            }
            return;
        }
        UE_LOG(LogTemp, Display, TEXT("EXPEDITION_QA_PRIVATE box=%d peers=%d"), NextDig, Reports.Num());
        if (FParse::Param(FCommandLine::Get(), TEXT("ExpeditionRepro")))
        {
            UE_LOG(LogTemp, Display, TEXT("EXPEDITION_QA_REPRO_DONE"));
            Advance(EStep::Done);
            return;
        }
        if (bEarlyPrizeCheck && !OnPrize())
        {
            const auto& Progress = Mode->TeamClues[Host->GetTeam()];
            if (Progress.Count() == 3)
            {
                Require(Progress.HasKey() && !Progress.Complete() && Progress.Crossing.IsNearlyZero()
                    && !Mode->Exposed[IslandRules::PrizeIndex] && !Mode->CanClaimChest(Host->GetTeam(), IslandRules::PrizeIndex),
                    TEXT("a team with a crafted key and three points cannot locate or open the excavated vault"));
                UE_LOG(LogTemp, Display, TEXT("EXPEDITION_KEY_GATE points=3 key=1 vault_hidden=1"));
            }
            if (Progress.Count() == 4)
            {
                Require(Progress.Complete() && Progress.HasKey() && Mode->Exposed[IslandRules::PrizeIndex]
                    && Mode->CanClaimChest(Host->GetTeam(), IslandRules::PrizeIndex),
                    TEXT("four points publish the exact crossing and expose the excavated vault"));
                UE_LOG(LogTemp, Display, TEXT("EXPEDITION_KEY_GATE points=4 key=1 vault_exposed=1"));
            }
        }
        if (!OnPrize())
        {
            Mode->RecoverPlayer(*Players[ExpeditionOpener]);
            RoomCheck = ERoomCheck::Dig;
            PrepareDig(NextDig + 1);
            return;
        }
        for (const AVoxelPlayerController* Existing : Players)
        {
            const auto* Member = Existing->GetPlayerState<AIslandPlayerState>();
            ReturningExpeditionMembers.Add(Member->MemberId, Member->Team);
        }
        Require(Round->Phase == EIslandPhase::Finished && Round->Winner == Players[ExpeditionOpener]->GetPlayerState<AIslandPlayerState>()->Team,
            TEXT("all peers see the eligible treasure winner"));
        if (bGamesScenario && FParse::Param(FCommandLine::Get(),TEXT("ExpeditionInteractionQA")))
        {
            if (FParse::Param(FCommandLine::Get(),TEXT("IslandScreenshot")))
            {
                if (Now-StepAt < 1.6) return;
                FString Dir;
                FParse::Value(FCommandLine::Get(), TEXT("FieldShotsDir="), Dir);
                const FString Path = FPaths::Combine(Dir,TEXT("TreasureVaultOpened.png"));
                if (!FPaths::FileExists(Path)) { FScreenshotRequest::RequestScreenshot(Path,true,false); return; }
            }
            UE_LOG(LogTemp, Display, TEXT("EXPEDITION_GAMES_DONE rounds=%d players=%d"),GameCount,Players.Num());
        }
        Host->ServerReturnToRoom();
        RoomCheck = ERoomCheck::Return;
    }
}

void AIslandProbe::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if (bFailed || Step == EStep::Done)
    {
        return;
    }
    const double Now = FPlatformTime::Seconds();
    if (ExpeditionPlayers > 0)
    {
        if (FParse::Param(FCommandLine::Get(), TEXT("CharacterSelectionQA"))) TickCharacterSelection(Now);
        else TickExpedition(Now);
        return;
    }
    if (Step == EStep::Waiting)
    {
        Mode = GetWorld()->GetAuthGameMode<AIslandGameMode>();
        if (!Mode || !Mode->IsPlaying())
        {
            return;
        }

        bPartyScenario = FParse::Param(FCommandLine::Get(), TEXT("IslandPartyQA"));
        bPartyTimeout = FParse::Param(FCommandLine::Get(), TEXT("IslandPartyTimeout"));
        bool bValid = true;
        for (int32 Seed = 0; Seed < 200; ++Seed)
        {
            for (int32 Count : {0, 1, 4, 5, 24})
            {
                FIslandTreasureLayout Layout;
                const AVoxelTerrain* Terrain = Mode->GetTerrain();
                bValid &= IslandRules::GenerateLayout(Seed, Count, Terrain->GetSearchCircles(), *Terrain->GetDensity(), Layout)
                    && Layout.Decoys.Num() == Count;
            }
        }
        Require(bValid && Mode->GetTerrain()->GetAppliedSequence() == 0, TEXT("1000 valid layouts and pristine island on start"));
        Require(bPartyScenario ? Mode->HiddenLayout.Decoys.Num() >= 2 : Mode->HiddenLayout.Decoys.IsEmpty(),
            TEXT("host applied the scenario's decoy configuration"));
        UE_LOG(LogTemp, Display, TEXT("ISLAND_QA_DECOYS count=%d"), Mode->HiddenLayout.Decoys.Num());
        if (bPartyScenario)
        {
            // Keep the scripted vertical shafts separated. Players can also dig from the side.
            while (NextDig == Mode->HiddenLayout.Decoys[0].ClueIndex || NextDig == Mode->HiddenLayout.Decoys[1].ClueIndex)
            {
                ++NextDig;
            }
        }
        for (int32 Seat = 0; Seat < Mode->GetPlayers().Num(); ++Seat)
        {
            auto* Controller = CastChecked<AIslandPlayerController>(Mode->GetPlayers()[Seat]);
            Require(Controller->GetTeam() == Seat % 3, TEXT("six seats assigned round-robin to the three docks"));
            AIslandProbePeer* Peer = GetWorld()->SpawnActor<AIslandProbePeer>();
            Peer->SetOwner(Controller);
            Peer->Seat = Seat;
            Peer->Coordinator = this;
            Peers.Add(Peer);
        }
        bReopened = PreviousFirstChest.IsSet();
        if (bReopened)
        {
            Require(!PreviousFirstChest.GetValue().Equals(Mode->HiddenLayout.Centers[0], .01),
                TEXT("host reopened with a new treasure layout"));
            for (TActorIterator<AIslandBreakable> It(GetWorld()); It; ++It)
            {
                Require(!It->IsBroken() && It->GetMesh().IsCollisionEnabled(), TEXT("reopen restores breakable props"));
            }
        }
        InspectThen(bReopened ? EStep::Done : EStep::Dig, true);
        return;
    }

    const AVoxelTerrain& Terrain = *Mode->GetTerrain();
    const uint32 Sequence = Terrain.GetAppliedSequence();
    if (Now - StepAt > 115.0)
    {
        UE_LOG(LogTemp, Error, TEXT("ISLAND_QA_TIMEOUT step=%u dig=%d team=%d seq=%u reports=%d"),
            static_cast<uint8>(Step), NextDig, OpeningTeam, Sequence, Reports.Num());
        Require(false, TEXT("scenario timeout"));
        return;
    }

    switch (Step)
    {
    case EStep::Inspect:
        if (Reports.Num() == Mode->GetPlayers().Num())
        {
            UE_LOG(LogTemp, Display, TEXT("ISLAND_QA_ISOLATION epoch=%d peers=%d checksum=%s"),
                Epoch, Reports.Num(), ExpectedDigest.IsEmpty() ? TEXT("checked separately") : TEXT("matched"));
            if (AfterInspection == EStep::Done)
            {
                Advance(EStep::Done);
                UE_LOG(LogTemp, Display, TEXT("ISLAND_QA_DONE peers=6 reopened=1"));
            }
            else if (AfterInspection == EStep::Dig)
            {
                PrepareDig(NextDig);
            }
            else
            {
                Advance(AfterInspection);
            }
        }
        else if (Now - LastAction >= .65)
        {
            LastAction = Now;
            for (int32 Seat = 0; Seat < Mode->GetPlayers().Num(); ++Seat)
            {
                if (!Reports.Contains(Seat))
                {
                    Peers[Seat]->ClientInspect(Epoch, !ExpectedDigest.IsEmpty());
                }
            }
        }
        break;
    case EStep::Dig:
        if (Mode->Exposed[NextDig] && Now - LastAction > .50)
        {
            Require(true, TEXT("player dug down and uncovered a chest"));
            if (OnPrize())
            {
                PrizePose = Mode->GetPlayers()[0]->GetHero()->GetActorLocation();
            }
            PrepareOpen(0);
            if (Mode->HiddenLayout.IsDecoy(NextDig))
            {
                Advance(EStep::PartyOpen);
            }
            else
            {
                Advance(OnPrize() ? EStep::ExpectDenied : EStep::Open);
            }
        }
        else if (!Mode->Exposed[NextDig] && Now - LastAction > .62 && !Terrain.IsBusy())
        {
            Mode->GetPlayers()[0]->SubmitEdit(EVoxelAction::DigDown, -FVector::UpVector);
            LastAction = Now;
        }
        break;
    case EStep::Open:
    {
        const uint8 Bit = 1u << NextDig;
        if (Now - StepAt > .40 && !bIssued)
        {
            Peers[OpeningTeam]->ClientOpenChest();
            Peers[OpeningTeam + 3]->ClientOpenChest();
            bIssued = true;
        }
        if ((Mode->TeamClues[OpeningTeam].Mask & Bit) != 0)
        {
            if (bPartyScenario)
            {
                NextDig = IslandRules::PrizeIndex + 1;
                InspectThen(EStep::Dig, true);
            }
            else if (OpeningTeam < 2)
            {
                PrepareOpen(OpeningTeam + 1);
                InspectThen(EStep::Open);
            }
            else if (NextDig == 2)
            {
                NextDig = IslandRules::PrizeIndex;
                InspectThen(EStep::Dig);
            }
            else if (NextDig != 3)
            {
                ++NextDig;
                InspectThen(EStep::Dig);
            }
            else
            {
                DrownSequence = Sequence;
                Place(4, {2700, 0, -150});
                InspectThen(EStep::Drown);
            }
        }
        break;
    }
    case EStep::ExpectDenied:
        if (!bIssued && Now - StepAt > .40)
        {
            Peers[0]->ClientOpenChest();
            bIssued = true;
        }
        if (Mode->GetPlayers()[0]->GetNotice() == TEXT("Collect all four clues to open the prize"))
        {
            Require(Mode->TeamClues[0].Count() == 3 && Mode->GetGameState<AIslandGameState>()->Winner < 0,
                TEXT("exposed prize rejects a crew with three clues"));
            NextDig = 3;
            InspectThen(EStep::Dig);
        }
        break;
    case EStep::Drown:
    {
        AVoxelPlayerController& Player = *Mode->GetPlayers()[4];
        const AIslandGameMode::FSeaState* Sea = Mode->SeaStates.Find(&Player);
        bSawDrown |= Sea && Sea->RespawnAt > 0.0;
        if (bSawDrown && FVector::Dist2D(Player.GetHero()->GetActorLocation(), Player.StartTransform.GetLocation()) < 15.0)
        {
            Require(Mode->TeamClues[1].Complete() && Sequence == DrownSequence, TEXT("drowning respawns at team dock with clues and terrain intact"));
            Advance(EStep::BreakTree);
        }
        break;
    }
    case EStep::BreakTree:
    {
        AIslandBreakable* Tree = FindBreakable(GetWorld(), EIslandBreakEffect::TreeFall);
        if (!Tree)
        {
            Require(false, TEXT("authored example tree exists"));
            break;
        }
        if (!bIssued)
        {
            Place(0, Tree->GetActorLocation() + FVector(-155, -70, 105));
            Place(1, Tree->GetActorLocation() + FVector(-155, 70, 105));
            bIssued = true;
        }
        if (Tree->IsBroken())
        {
            Require(!Tree->GetMesh().IsCollisionEnabled() && Sequence == DrownSequence,
                TEXT("simultaneous player strikes disable tree collision without consuming a terrain sequence"));
            Advance(EStep::BreakDock);
        }
        else if (Now - LastAction > .9)
        {
            Peers[0]->ClientStrike(Tree->GetMesh().Bounds.Origin);
            Peers[1]->ClientStrike(Tree->GetMesh().Bounds.Origin);
            LastAction = Now;
        }
        break;
    }
    case EStep::BreakDock:
    {
        AIslandBreakable* Dock = FindBreakable(GetWorld(), EIslandBreakEffect::Fragments, true);
        if (!Dock)
        {
            Require(false, TEXT("authored red dock is breakable"));
            break;
        }
        if (!bIssued)
        {
            const FVector DockPos = Dock->GetActorLocation();
            Place(0, {DockPos.X + 115.0, DockPos.Y, Dock->GetMesh().Bounds.GetBox().Max.Z + 96.0});
            bIssued = true;
        }
        if (Dock->IsBroken())
        {
            Require(!Dock->GetMesh().IsCollisionEnabled(), TEXT("dock destruction disables collision"));
            InspectThen(EStep::ReturnAfterBreak, true);
        }
        else if (Now - LastAction > .85)
        {
            Peers[0]->ClientStrike(Dock->GetMesh().Bounds.Origin);
            LastAction = Now;
        }
        break;
    }
    case EStep::ReturnAfterBreak:
    {
        AVoxelPlayerController& Player = *Mode->GetPlayers()[0];
        Mode->RecoverPlayer(Player);
        const FVector Position = Player.GetHero()->GetActorLocation();
        Require(Position.Z > 0 && FVector::Dist2D(Position, Player.StartTransform.GetLocation()) > 20.0,
            TEXT("recovery finds dry footing after the dock is destroyed"));
        StartStress(1);
        break;
    }
    case EStep::StressShared:
    case EStep::StressApart:
        FrameTimes.Add((Now - LastFrame) * 1000.0);
        LastFrame = Now;
        if (Sequence - GroupStart >= EditsPerStage)
        {
            if (Step == EStep::StressShared)
            {
                StartStress(2);
            }
            else
            {
                for (AIslandProbePeer* Peer : Peers)
                {
                    Peer->ClientStress(0);
                }
                Advance(EStep::Drain);
            }
        }
        break;
    case EStep::Drain:
        if (!Terrain.IsBusy() && Mode->Requests.IsEmpty())
        {
            FrameTimes.Sort();
            UE_LOG(LogTemp, Display, TEXT("ISLAND_QA_STRESS first=%u last=%u p95=%.3f frames=%d"),
                StressStart + 1, Sequence, FrameTimes[FMath::CeilToInt(FrameTimes.Num() * .95) - 1], FrameTimes.Num());
            UE_LOG(LogTemp, Display, TEXT("ISLAND_QA_DENSITY seq=%u crc=%08x chunks=%d"),
                Sequence, Terrain.GetChecksum(), FVoxelGrid::ForLayout(EVoxelLayout::Island).NumChunks());
            InspectThen(EStep::Race, true);
        }
        break;
    case EStep::Race:
        if (!bIssued)
        {
            Place(1, PrizePose);
            Place(2, PrizePose);
            Peers[1]->ClientOpenChest();
            Peers[2]->ClientOpenChest();
            bIssued = true;
        }
        if (Mode->GetGameState<AIslandGameState>()->Phase == EIslandPhase::Finished)
        {
            const int8 Winner = Mode->GetGameState<AIslandGameState>()->Winner;
            Require(Winner == 1 || Winner == 2, TEXT("two eligible rival crews tried the prize and one won"));
            InspectThen(EStep::Restart, true);
        }
        break;
    case EStep::Restart:
        if (!bIssued)
        {
            PreviousFirstChest = Mode->HiddenLayout.Centers[0];
            Mode->RestartIsland();
            bIssued = true;
        }
        break;
    case EStep::PartyOpen:
    case EStep::PartyPlay:
    case EStep::PartyJump:
    case EStep::PartyDone:
        TickParty(Now);
        break;
    default:
        break;
    }
}

void AIslandProbe::TickParty(double Now)
{
    const EIslandPhase Phase = Mode->GetGameState<AIslandGameState>()->Phase;
    if (Step == EStep::PartyOpen && PartyAttempts == 1 && !bIssued && Now - StepAt > .2)
    {
        Peers[1]->ClientPartySpace(true, false);
        bIssued = true;
    }
    if (Step == EStep::PartyOpen && Now - StepAt > .6)
    {
        OriginalPawns.Reset();
        for (AVoxelPlayerController* Player : Mode->GetPlayers())
        {
            OriginalPawns.Add(Player->GetPawn());
        }
        BeforePartyDigest = TerrainDigest(*Mode->GetTerrain());
        PartyStartedAt = 0.0;
        ++PartyAttempts;
        Peers[0]->ClientOpenChest();
        Peers[3]->ClientOpenChest();
        Advance(EStep::PartyPlay);
    }
    else if (Step == EStep::PartyPlay)
    {
        if (Phase == EIslandPhase::PartyPlaying && PartyStartedAt == 0.0)
        {
            PartyStartedAt = Now;
            Require(Mode->PartyRoundId == static_cast<uint32>(PartyAttempts), TEXT("simultaneous opens yielded one party"));
            for (AVoxelPlayerController* Player : Mode->GetPlayers())
            {
                Require(Cast<APartyPawn>(Player->GetPawn()) != nullptr, TEXT("online player possessed a party pawn"));
            }
        }
        if (Phase == EIslandPhase::PartyPlaying && PartyAttempts == 1 && !bIssued && Now - PartyStartedAt >= 3.9)
        {
            if (FParse::Param(FCommandLine::Get(), TEXT("IslandScreenshot")))
            {
                FScreenshotRequest::RequestScreenshot(TEXT("IslandPartyQA"), true, true);
            }
            for (AIslandProbePeer* Peer : Peers)
            {
                Peer->ClientPartySpace(true, false);
            }
            bIssued = true;
        }
        if (Mode->PartyRoundId != static_cast<uint32>(PartyAttempts) || Phase != EIslandPhase::Hunting)
        {
            return;
        }
        Require(!Mode->bPartyUnavailable && !Mode->PartySession && Mode->PartyPlayers.IsEmpty(),
            TEXT("feature returned to installed on every online peer"));
        Require(Mode->ConsumedDecoys.Num() == (bPartyTimeout ? 0 : PartyAttempts),
            TEXT("only parties that started consumed a fake chest"));
        Require(TerrainDigest(*Mode->GetTerrain()) == BeforePartyDigest && Mode->TeamClues[0].Count() == 1,
            TEXT("party preserved the original terrain and private clue"));
        for (int32 Seat = 0; Seat < Mode->GetPlayers().Num(); ++Seat)
        {
            Require(Mode->GetPlayers()[Seat]->GetPawn() == OriginalPawns[Seat].Get(), TEXT("player got the original island pawn back"));
            Peers[Seat]->ClientPartySpace(false, false);
        }
        if (!bPartyTimeout && PartyAttempts == 1)
        {
            ++NextDig;
            InspectThen(EStep::Dig, true);
        }
        else
        {
            Mode->RecoverPlayer(*Mode->GetPlayers()[0]);
            InspectThen(EStep::PartyJump, true);
        }
    }
    else if (Step == EStep::PartyJump)
    {
        const AVoxelCharacter* Hero = Mode->GetPlayers()[0]->GetHero();
        if (Hero && Hero->GetCharacterMovement()->IsMovingOnGround())
        {
            Peers[0]->ClientPartySpace(true, true);
            Advance(EStep::PartyDone);
        }
    }
    else if (Step == EStep::PartyDone && Now - StepAt > .8)
    {
        Peers[0]->ClientPartySpace(false, false);
        UE_LOG(LogTemp, Display, TEXT("PARTY_QA_DONE rounds=%d players=%d"), PartyAttempts, Mode->GetPlayers().Num());
        Advance(EStep::Done);
    }
}
