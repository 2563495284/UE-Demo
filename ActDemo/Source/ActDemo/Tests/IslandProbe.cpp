#include "Tests/IslandProbe.h"

#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "InputKeyEventArgs.h"
#include "PartyMinigame.h"
#include "Island/IslandGameMode.h"
#include "Island/IslandPlayerController.h"
#include "Island/IslandState.h"
#include "Island/IslandWorld.h"
#include "UnrealClient.h"
#include "Voxel/VoxelCharacter.h"
#include "Voxel/VoxelTerrain.h"

namespace
{
    TOptional<FVector> PreviousFirstChest;
    constexpr int32 EditsPerStage = 150;
    const FIntPoint StressSites[] = {{-400, -400}, {800, 800}, {-800, 800}};

    FString TerrainDigest(const AVoxelTerrain& Terrain)
    {
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
        return Result;
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
    for (TActorIterator<AIslandChest> It(GetWorld()); It; ++It)
    {
        ++Boxes;
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
        Player->Interact();
    }
}

void AIslandProbePeer::ClientShowChart_Implementation()
{
    if (AIslandPlayerController* Player = Cast<AIslandPlayerController>(GetOwner()))
    {
        Player->OpenMap();
        FScreenshotRequest::RequestScreenshot(TEXT("TreasureIslandQA"), true, true);
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
    const auto* Controller = Cast<AIslandPlayerController>(GetOwner());
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

AIslandProbe::AIslandProbe()
{
    PrimaryActorTick.bCanEverTick = true;
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
    const double Top = VoxelPlayground::IslandSurfaceHeight(FMath::RoundToInt(Point.X), FMath::RoundToInt(Point.Y));
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
    bool bPrivate = Clues.Points.Num() == IslandRules::ClueCount && (Clues.Mask & ~Expected.Mask) == 0
        && (Expected.Complete() || Clues.Crossing.IsNearlyZero());
    if (bPrivate)
    {
        for (int32 Index = 0; Index < IslandRules::ClueCount; ++Index)
        {
            bPrivate &= Clues.Points[Index].Equals((Clues.Mask & (1u << Index)) != 0 ? Expected.Points[Index] : FVector2D::ZeroVector, .01);
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
    if (Clues.Mask != Expected.Mask || Boxes != Exposed || Winner != Mode->GetGameState<AIslandGameState>()->Winner
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

void AIslandProbe::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if (bFailed || Step == EStep::Done)
    {
        return;
    }
    const double Now = FPlatformTime::Seconds();
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
                const FIslandTreasureLayout Layout = IslandRules::GenerateLayout(Seed, Count);
                bValid &= Layout.Decoys.Num() == Count && IslandRules::IsValidLayout(Layout);
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
            StartStress(1);
        }
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
