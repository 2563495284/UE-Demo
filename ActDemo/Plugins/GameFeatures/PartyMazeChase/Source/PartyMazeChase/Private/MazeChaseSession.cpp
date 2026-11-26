#include "MazeChaseSession.h"

#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/OverlapResult.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Net/UnrealNetwork.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
    constexpr int32 Width = 9;
    constexpr double Pitch = 600.;
    constexpr double WallHeight = 540.;
    constexpr double HalfWall = 28.;
    constexpr double ToolRange = 365.;
    constexpr double CaptureRange = 210.;
    constexpr double Mid = 50.;
    constexpr double HalfBoard = Width * Pitch * .5;
    constexpr int32 MaxSeats = 8;

    int32 SeatBit(int32 Seat)
    {
        return Seat >= 0 && Seat < MaxSeats ? 1 << Seat : 0;
    }

    FVector RoomCenter(FIntPoint Room)
    {
        return FVector(Mid + (Room.X - Width/2)*Pitch, Mid + (Room.Y - Width/2)*Pitch, 0.);
    }

    bool IsRoom(FIntPoint Room)
    {
        return Room.X >= 0 && Room.X < Width && Room.Y >= 0 && Room.Y < Width;
    }

    int32 EdgeId(FIntPoint A, FIntPoint B)
    {
        return 2*(FMath::Min(A.X,B.X) + Width*FMath::Min(A.Y,B.Y)) + (A.Y != B.Y);
    }
}

AMazeChaseSession::AMazeChaseSession()
{
    Title = TEXT("迷宫追捕");
    EnglishTitle = TEXT("Maze Chase");
    Objective = TEXT("逃方撑过三分钟，追方要抓住全部逃方队员。");
    EnglishObjective = TEXT("Runners survive three minutes. Hunters win by tagging every runner.");
    Theme = FColor(74,186,164);
    RoundSeconds = 180.f;
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Shape(TEXT("/Engine/BasicShapes/Cube"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Stone(TEXT("/Game/TreasureIsland/Art/Neutral/SM_HewnBlock"));
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> Material(TEXT("/Engine/BasicShapes/BasicShapeMaterial"));
    Cube = Shape.Object;
    HewnWall = Stone.Object;
    BasePaint = Material.Object;
}

void AMazeChaseSession::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(AMazeChaseSession, RunnerTeam);
    DOREPLIFETIME(AMazeChaseSession, RunnerSeats);
    DOREPLIFETIME(AMazeChaseSession, ActiveSeats);
    DOREPLIFETIME(AMazeChaseSession, CaughtSeats);
    DOREPLIFETIME(AMazeChaseSession, WinningTeams);
    DOREPLIFETIME(AMazeChaseSession, HuntersReleasedAt);
    DOREPLIFETIME(AMazeChaseSession, OpenWalls);
}

UStaticMeshComponent* AMazeChaseSession::MakeBlock(FName Tag, const FBox& Bounds, UMaterialInterface* Paint, bool bSolid)
{
    // CharacterMovement transmits its floor as a component reference. Each peer builds
    // these components in seed order, so the same name resolves the same physical base.
    const FName Name(*FString::Printf(TEXT("%s_%d"), *Tag.ToString(), NextPartId++));
    auto* Part = NewObject<UStaticMeshComponent>(this, Name);
    Part->SetNetAddressable();
    Part->SetupAttachment(GetRootComponent());
    Part->SetStaticMesh(Tag == TEXT("MazeWall") ? HewnWall.Get() : Cube.Get());
    Part->SetRelativeLocation(Bounds.GetCenter());
    Part->SetRelativeScale3D(Bounds.GetSize()/100.);
    Part->SetMaterial(0,Paint);
    Part->SetCanEverAffectNavigation(false);
    Part->SetCollisionProfileName(TEXT("BlockAll"));
    Part->SetCollisionEnabled(bSolid ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);
    Part->ComponentTags.Add(Tag);
    AddInstanceComponent(Part);
    Part->RegisterComponent();
    return Part;
}

void AMazeChaseSession::BuildWall(FIntPoint Room, bool bBetweenColumns)
{
    FVector Center = RoomCenter(Room);
    Center[bBetweenColumns ? 0 : 1] += Pitch*.5;
    Center.Z = WallHeight*.5;
    FVector Extent(Pitch*.5+HalfWall, Pitch*.5+HalfWall, WallHeight*.5);
    Extent[bBetweenColumns ? 0 : 1] = HalfWall;
    const FBox Bounds = FBox::BuildAABB(Center, Extent);
    const int32 District = (Room.X >= Width/2 ? 1 : 0) + (Room.Y >= Width/2 ? 2 : 0);
    Walls.Add(MakeBlock(TEXT("MazeWall"), Bounds, Paints[2+District]));
    LocalWallBounds.Add(Bounds);
    // A short ochre seam remains under each editable wall and shows where it can be repaired.
    Center.Z = 1.5;
    Extent.Z = 1.5;
    MakeBlock(TEXT("MazeSeam"),FBox::BuildAABB(Center,Extent),Paints[6],false);
}

void AMazeChaseSession::BuildArena()
{
    for (const FColor Color : {FColor(198,184,148), FColor(57,79,91), FColor(68,141,135), FColor(92,146,168),
                               FColor(171,123,87), FColor(114,147,109), FColor(243,183,88)})
    {
        auto* Paint = UMaterialInstanceDynamic::Create(BasePaint,this);
        Paint->SetVectorParameterValue(TEXT("Color"),FLinearColor(Color));
        Paints.Add(Paint);
    }
    MakeBlock(TEXT("MazeFloor"),FBox::BuildAABB(FVector(Mid,Mid,-55.),FVector(HalfBoard+HalfWall,HalfBoard+HalfWall,55.)),Paints[0]);
    for (int32 Axis = 0; Axis < 2; ++Axis)
    {
        for (double Side : {-1.,1.})
        {
            FVector Center(Mid,Mid,WallHeight*.5);
            Center[Axis] += HalfBoard*Side;
            FVector Border(HalfBoard+HalfWall,HalfBoard+HalfWall,WallHeight*.5);
            Border[Axis] = HalfWall;
            MakeBlock(TEXT("MazeBoundary"),FBox::BuildAABB(Center,Border),Paints[1]);
        }
    }

    // Carve a spanning tree first. Extra passages turn the occasional dead end into a loop.
    FRandomStream Random(ArenaSeed());
    TSet<int32> Passages;
    TSet<FIntPoint> Seen;
    TArray<FIntPoint> Stack = {FIntPoint(0,0)};
    Seen.Add(Stack[0]);
    const FIntPoint Dirs[] = {{1,0},{-1,0},{0,1},{0,-1}};
    while (!Stack.IsEmpty())
    {
        const FIntPoint Here = Stack.Last();
        TArray<FIntPoint,TInlineAllocator<4>> Choices;
        for (FIntPoint Step : Dirs)
            if (IsRoom(Here+Step) && !Seen.Contains(Here+Step)) Choices.Add(Here+Step);
        if (Choices.IsEmpty())
        {
            Stack.Pop(EAllowShrinking::No);
            continue;
        }
        const FIntPoint Next = Choices[Random.RandRange(0,Choices.Num()-1)];
        Passages.Add(EdgeId(Here,Next));
        Seen.Add(Next);
        Stack.Add(Next);
    }
    for (int32 Y=0; Y<Width; ++Y)
        for (int32 X=0; X<Width; ++X)
            for (bool bColumns : {true,false})
            {
                const FIntPoint A(X,Y), B = A + (bColumns ? FIntPoint(1,0) : FIntPoint(0,1));
                if (IsRoom(B) && !Passages.Contains(EdgeId(A,B)) && Random.FRand() >= .12f)
                    BuildWall(A,bColumns);
            }

    if (HasAuthority())
    {
        TArray<int32> Seats;
        SeatTeams.GetKeys(Seats);
        Seats.Sort();
        AssignSides(Seats);
        DefineBerths(Seats,false);
    }
    OnRep_OpenWalls();
}

void AMazeChaseSession::OnRep_OpenWalls()
{
    for (int32 I=0; I<Walls.Num(); ++I)
    {
        const bool bOpen = OpenWalls.Contains(uint8(I));
        auto& Part = *Walls[I];
        Part.SetVisibility(!bOpen);
        Part.SetCollisionEnabled(bOpen ? ECollisionEnabled::NoCollision : ECollisionEnabled::QueryAndPhysics);
    }
}

void AMazeChaseSession::SetParticipantTeam(int32 Seat, int32 Team)
{
    if (HasAuthority() && SeatBit(Seat) && Team>=0 && Team<31)
        SeatTeams.Add(Seat,Team);
}

void AMazeChaseSession::AssignSides(const TArray<int32>& Seats)
{
    TArray<int32> Teams;
    for (const int32 Seat : Seats)
        if (const int32* Team = SeatTeams.Find(Seat)) Teams.AddUnique(*Team);
    Teams.Sort();
    FRandomStream Draw(HashCombineFast(uint32(ArenaSeed()),0xB14E600Du));
    RunnerTeam = Teams.IsEmpty() ? INDEX_NONE : Teams[Draw.RandRange(0,Teams.Num()-1)];
    RunnerSeats = ActiveSeats = 0;
    for (const int32 Seat : Seats)
    {
        const int32* Team = SeatTeams.Find(Seat);
        if (!Team) continue;
        ActiveSeats |= SeatBit(Seat);
        if (*Team==RunnerTeam) RunnerSeats |= SeatBit(Seat);
    }
}

void AMazeChaseSession::DefineBerths(const TArray<int32>& Seats, bool bMove)
{
    int32 Runners = 0, Hunters = 0;
    const bool bFlip = (uint32(ArenaSeed()) & 1u) != 0;
    const int32 SpawnLanes[MaxSeats] = {4,2,6,1,7,3,5,0};
    for (const int32 Seat : Seats)
    {
        const bool bRunner = IsRunner(Seat);
        const int32 Lane = SpawnLanes[bRunner ? Runners++ : Hunters++];
        const bool bRight = bRunner != bFlip;
        const FVector Center = RoomCenter({bRight ? Width-1 : 0,Lane});
        const FIntVector Feet = CellAt(GetActorLocation()+Center);
        const float Yaw = bRight ? 180.f : 0.f;
        SetStart(Seat,Feet,Yaw);
        const FVector Location = GetActorLocation()+Center+FVector(0,0,91.);
        Berths.Add(Seat,Location);
        if (APartyVoxelPawn* Pawn = Participant(Seat); bMove && Pawn)
        {
            Pawn->TeleportTo(Location,FRotator(0,Yaw,0),false,true);
            if (auto* PC = Pawn->GetController<APlayerController>())
            {
                PC->SetControlRotation(FRotator(0,Yaw,0));
                PC->ClientSetRotation(FRotator(0,Yaw,0));
            }
        }
    }
}

void AMazeChaseSession::StartParty()
{
    if (!HasAuthority() || GetPhase()!=EPartyVoxelPhase::Waiting) return;
    const int32 PreviousSide = RunnerTeam;
    const TArray<int32> Seats = PlayingSeats();
    AssignSides(Seats);
    DefineBerths(Seats,PreviousSide!=RunnerTeam);
    for (const int32 Seat : Seats)
    {
        if (auto* Pawn = Participant(Seat))
        {
            Pawn->GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Visibility,ECR_Block);
            auto* Move = Pawn->GetCharacterMovement();
            Move->MaxWalkSpeed = IsRunner(Seat) ? ChaseRunnerSpeed : ChaseHunterSpeed;
            Move->StopMovementImmediately();
            Move->DisableMovement();
        }
    }
    Super::StartParty();
}

bool AMazeChaseSession::IsRunner(int32 Seat) const { return (RunnerSeats & SeatBit(Seat)) != 0; }
bool AMazeChaseSession::IsCaught(int32 Seat) const { return (CaughtSeats & SeatBit(Seat)) != 0; }
int32 AMazeChaseSession::GetRunnerCount() const { return FMath::CountBits(uint32(RunnerSeats)); }
int32 AMazeChaseSession::GetRemainingRunners() const { return FMath::CountBits(uint32(ActiveSeats & RunnerSeats & ~CaughtSeats)); }

float AMazeChaseSession::GetHeadStartRemaining() const
{
    return GetPhase()==EPartyVoxelPhase::Playing ? FMath::Max(0.,HuntersReleasedAt-Clock()) : 0.f;
}

void AMazeChaseSession::OnArenaStarted()
{
    HuntersReleasedAt = Clock()+PreparationSeconds();
    for (const int32 Seat : PlayingSeats())
        if (auto* Pawn = Participant(Seat); Pawn && IsRunner(Seat))
            Pawn->GetCharacterMovement()->SetMovementMode(MOVE_Falling);
    UE_LOG(LogTemp,Display,TEXT("MAZE_CHASE_START team=%d seats=%d runners=%d seconds=%.0f headstart=%.0f walls=%d"),
        RunnerTeam,ActiveSeats,RunnerSeats,RoundSeconds,PreparationSeconds(),Walls.Num());
    CheckOutcome();
}

void AMazeChaseSession::OnArenaStep(float DeltaSeconds)
{
    if (!bReleasedHunters && GetHeadStartRemaining()<=0.f)
    {
        bReleasedHunters = true;
        for (const int32 Seat : PlayingSeats())
            if (auto* Pawn = Participant(Seat); Pawn && !IsRunner(Seat))
                Pawn->GetCharacterMovement()->SetMovementMode(MOVE_Falling);
    }
    CheckOutcome();
}

void AMazeChaseSession::CheckParticipantBounds(APartyVoxelPawn& Pawn)
{
    const int32 Seat = Pawn.GetSeat();
    if (IsCaught(Seat)) return;
    const FVector Feet = FeetLocation(Seat)-GetActorLocation();
    if (FMath::Abs(Feet.X-Mid) < HalfBoard && FMath::Abs(Feet.Y-Mid) < HalfBoard && Feet.Z >= -30. && Feet.Z <= 360.)
    {
        if (Feet.Z<=10. && IsGrounded(Seat)) LastGround.Add(Seat,Pawn.GetActorLocation());
        return;
    }
    // Recover at the last valid footing when possible. Check it in case someone restored a wall there.
    FVector Safe = LastGround.Contains(Seat) ? LastGround.FindRef(Seat) : Berths.FindRef(Seat);
    const auto* Capsule = Pawn.GetCapsuleComponent();
    const FCollisionQueryParams Query(SCENE_QUERY_STAT(MazeRecovery),false,&Pawn);
    if (GetWorld()->OverlapBlockingTestByChannel(Safe,FQuat::Identity,ECC_Pawn,
        FCollisionShape::MakeCapsule(Capsule->GetScaledCapsuleRadius(),Capsule->GetScaledCapsuleHalfHeight()),Query))
        Safe = Berths.FindRef(Seat);
    Pawn.TeleportTo(Safe,Pawn.GetActorRotation(),false,true);
    Pawn.GetCharacterMovement()->StopMovementImmediately();
}

void AMazeChaseSession::RemoveParticipant(int32 Seat)
{
    // Resolve a just-expired clock using the roster that actually reached the deadline.
    if (GetPhase()==EPartyVoxelPhase::Playing && SecondsLeft()<=0.f) CheckOutcome();
    ActiveSeats &= ~SeatBit(Seat);
    Super::RemoveParticipant(Seat);
    CheckOutcome();
}

void AMazeChaseSession::CheckOutcome()
{
    if (!HasAuthority() || GetPhase()!=EPartyVoxelPhase::Playing) return;
    const bool bCaughtAll = GetRemainingRunners()==0;
    const bool bHuntersGone = (ActiveSeats & ~RunnerSeats)==0;
    if (!bCaughtAll && !bHuntersGone && SecondsLeft()>0.f) return;
    const bool bRunnersWin = !bCaughtAll;
    WinningTeams = 0;
    for (const int32 Seat : PlayingSeats())
        if (const int32* Team = SeatTeams.Find(Seat); Team && IsRunner(Seat)==bRunnersWin)
            WinningTeams |= 1 << *Team;
    UE_LOG(LogTemp,Display,TEXT("MAZE_CHASE_RESULT team=%d runnerswin=%d winners=%d remaining=%d"),
        RunnerTeam,bRunnersWin,WinningTeams,GetRemainingRunners());
    Results();
}

bool AMazeChaseSession::WallIsOccupied(int32 WallIndex) const
{
    const FBox& Box = LocalWallBounds[WallIndex];
    TArray<FOverlapResult> Touching;
    GetWorld()->OverlapMultiByObjectType(Touching,GetActorLocation()+Box.GetCenter(),FQuat::Identity,
        FCollisionObjectQueryParams(ECC_Pawn),FCollisionShape::MakeBox(Box.GetExtent()+FVector(4.)));
    for (const FOverlapResult& Hit : Touching)
        if (const auto* Pawn = Cast<APartyVoxelPawn>(Hit.GetActor()); Pawn && Pawn->GetSession()==this && !IsCaught(Pawn->GetSeat()))
            return true;
    return false;
}

EPartyChaseAim AMazeChaseSession::FindTarget(const APartyVoxelPawn& Pawn, FVector From, FVector Direction,
    int32& WallIndex, int32& TargetSeat, bool bCheckGaps) const
{
    WallIndex = TargetSeat = INDEX_NONE;
    const FVector Ray = Direction.GetSafeNormal();
    if (Ray.IsNearlyZero() || Ray.ContainsNaN()) return EPartyChaseAim::None;
    const FVector End = From+Ray*ToolRange;
    FCollisionQueryParams Query(SCENE_QUERY_STAT(MazeTool),false,&Pawn);
    FHitResult Hit;
    const bool bHit = GetWorld()->LineTraceSingleByChannel(Hit,From,End,ECC_Visibility,Query);
    if (bHit)
    {
        const auto* Other = Cast<APartyVoxelPawn>(Hit.GetActor());
        if (bCheckGaps && Other && Other->GetSession()==this && Hit.Distance<=CaptureRange && !IsRunner(Pawn.GetSeat())
            && IsRunner(Other->GetSeat()) && !IsCaught(Other->GetSeat()))
        {
            TargetSeat = Other->GetSeat();
            return EPartyChaseAim::Runner;
        }
    }

    // Open walls have no collision. Intersect their old volumes, clipped against the first solid obstruction.
    double Nearest = bHit ? Hit.Distance : ToolRange;
    const FVector LocalFrom = From-GetActorLocation();
    for (const uint8 I : OpenWalls)
    {
        if (!bCheckGaps) break;
        if (!LocalWallBounds.IsValidIndex(I)) continue;
        FVector Point, Normal;
        float Fraction;
        if (FMath::LineExtentBoxIntersection(LocalWallBounds[I],LocalFrom,LocalFrom+Ray*ToolRange,
            FVector::ZeroVector,Point,Normal,Fraction) && Fraction*ToolRange < Nearest)
        {
            Nearest = Fraction*ToolRange;
            WallIndex = I;
        }
    }
    if (WallIndex!=INDEX_NONE && !WallIsOccupied(WallIndex)) return EPartyChaseAim::Gap;
    if (!bHit || Hit.GetActor()!=this || !Hit.GetComponent()->ComponentHasTag(TEXT("MazeWall"))) return EPartyChaseAim::None;
    WallIndex = Walls.IndexOfByKey(Cast<UStaticMeshComponent>(Hit.GetComponent()));
    return WallIndex!=INDEX_NONE ? EPartyChaseAim::Wall : EPartyChaseAim::None;
}

EPartyChaseAim AMazeChaseSession::GetChaseAim(const APartyVoxelPawn& Pawn, FVector From, FVector Direction) const
{
    int32 WallIndex, TargetSeat;
    return FindTarget(Pawn,From,Direction,WallIndex,TargetSeat);
}

bool AMazeChaseSession::UseTerrainTool(APartyVoxelPawn& Pawn, bool bPlace, FVector Direction)
{
    const int32 Seat = Pawn.GetSeat();
    const double Now = Clock();
    const double ReadyAt = NextToolTime.FindRef(Seat);
    if (!HasAuthority() || GetPhase()!=EPartyVoxelPhase::Playing || SecondsLeft()<=0.f || Participant(Seat)!=&Pawn
        || IsCaught(Seat) || (!IsRunner(Seat) && GetHeadStartRemaining()>0.f) || Now+.03<ReadyAt) return false;
    // Both buttons consume one recovery window, including missed swings.
    NextToolTime.Add(Seat,FMath::Max(Now,ReadyAt)+ChaseToolRecovery(bPlace));
    int32 Wall, Other;
    const EPartyChaseAim Aim = FindTarget(Pawn,Pawn.GetPawnViewLocation(),Direction,Wall,Other,bPlace);
    if (bPlace && Aim==EPartyChaseAim::Runner)
    {
        auto* Target = Participant(Other);
        if (!Target || IsCaught(Other)) return false;
        CaughtSeats |= SeatBit(Other);
        Target->GetCharacterMovement()->StopMovementImmediately();
        Target->GetCharacterMovement()->DisableMovement();
        Target->SetActorEnableCollision(false);
        Target->SetActorHiddenInGame(true);
        Target->ForceNetUpdate();
        UE_LOG(LogTemp,Display,TEXT("MAZE_CHASE_CAPTURE hunter=%d runner=%d remaining=%d"),Seat,Other,GetRemainingRunners());
        ForceNetUpdate();
        CheckOutcome();
        return true;
    }
    if (!bPlace && Aim==EPartyChaseAim::Wall)
    {
        OpenWalls.Add(uint8(Wall));
        RefillAfter.Add(Wall,Now+1.5);
    }
    else if (bPlace && Aim==EPartyChaseAim::Gap)
    {
        if (Now<RefillAfter.FindRef(Wall)) return false;
        OpenWalls.Remove(uint8(Wall));
    }
    else return false;
    OnRep_OpenWalls();
    ForceNetUpdate();
    UE_LOG(LogTemp,Display,TEXT("MAZE_CHASE_WALL seat=%d wall=%d open=%d"),Seat,Wall,!bPlace);
    return true;
}
