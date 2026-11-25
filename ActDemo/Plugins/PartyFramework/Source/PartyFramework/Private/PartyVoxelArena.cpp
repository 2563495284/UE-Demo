#include "PartyVoxelArena.h"

#include "Components/CapsuleComponent.h"
#include "PartyDensityTerrain.h"
#include "VoxelSurfaceKernel.h"
#include "Engine/GameInstance.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerState.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Net/UnrealNetwork.h"
#include "UObject/ConstructorHelpers.h"

namespace PartyGrid
{
    constexpr double Size = 100.;
    constexpr double Reach = 435.;
    constexpr int32 Seats = 8;
    bool InGrid(FIntVector P)
    {
        return FMath::Abs(P.X)<=48 && FMath::Abs(P.Y)<=48 && P.Z>=-12 && P.Z<=48;
    }
    uint32 TieKey(int32 Seed,int32 Seat)
    {
        FRandomStream Draw(HashCombineFast(GetTypeHash(Seed), GetTypeHash(Seat)));
        return Draw.GetUnsignedInt();
    }
}

APartyVoxelSession::APartyVoxelSession()
{
    SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Arena")));
    PrimaryActorTick.bCanEverTick = true;
    SetReplicateMovement(true);
    SetNetUpdateFrequency(30.f);
    Terrain = CreateDefaultSubobject<UPartyDensityTerrain>(TEXT("DensityTerrain"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Disk(TEXT("/Engine/BasicShapes/Cylinder"));
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> Paint(TEXT("/Engine/BasicShapes/BasicShapeMaterial"));
    Material=Paint.Object;
    BeaconMesh=Disk.Object;
    Changes.Arena=this;
}

void APartyVoxelSession::BeginPlay()
{
    Super::BeginPlay();
#if !UE_BUILD_SHIPPING
    bQAViews=FParse::Param(FCommandLine::Get(),TEXT("ExpeditionGamesQA"));
#endif
    Changes.Arena=this;
    EnsureGrid();
}

void APartyVoxelSession::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME_CONDITION(APartyVoxelSession,Seed,COND_InitialOnly);
    DOREPLIFETIME(APartyVoxelSession,Phase);
    DOREPLIFETIME(APartyVoxelSession,Winner);
    DOREPLIFETIME(APartyVoxelSession,Deadline);
    DOREPLIFETIME(APartyVoxelSession,Standings);
    DOREPLIFETIME(APartyVoxelSession,Changes);
    DOREPLIFETIME(APartyVoxelSession,Beacons);
    DOREPLIFETIME(APartyVoxelSession,DestroyedVenueStructures);
}

FVector APartyVoxelSession::CellCenter(FIntVector P) const
{
    return GetActorLocation()+(FVector(P)+FVector(.5))*PartyGrid::Size;
}

FIntVector APartyVoxelSession::CellAt(FVector WorldPoint) const
{
    const FVector P=(WorldPoint-GetActorLocation())/PartyGrid::Size;
    return {FMath::FloorToInt(P.X),FMath::FloorToInt(P.Y),FMath::FloorToInt(P.Z)};
}

EPartyBlock APartyVoxelSession::BlockAt(FIntVector Cell) const
{
    if (bBuilt) return Terrain->MaterialAt(CellCenter(Cell)-GetActorLocation());
    return Blocks.FindRef(Cell);
}

float APartyVoxelSession::TerrainDensityAt(FVector WorldPoint) const
{
    return Terrain->DensityAt(WorldPoint - GetActorLocation());
}

void APartyVoxelSession::EnsureGrid()
{
    if (bBuildStarted) return;
    bBuildStarted = true;
    BuildArena();
    for (const auto& Pair : Blocks)
    {
        MinCell = MinCell.ComponentMin(Pair.Key);
        MaxCell = MaxCell.ComponentMax(Pair.Key);
    }
    TArray<FVector> Berths;
    for (const auto& Entry : Starts) Berths.Add(Entry.Value.GetLocation()-GetActorLocation());
    Terrain->Initialize(Blocks,Theme,Berths);
    for (const auto& Pair : InitialBeacons) ApplyBeacon(Pair.Key,Pair.Value);
    InitialBeacons.Empty();
    bBuilt = true;
#if !UE_BUILD_SHIPPING
    if (bQAViews) InitialGridDigest = GridDigest();
#endif
    TArray<int32> Received;
    for (int32 I=0; I<Changes.Items.Num(); ++I) Received.Add(I);
    ReplayBrushes(Received);
    OnRep_Beacons();
}

void APartyVoxelSession::Block(FIntVector Cell,EPartyBlock Kind)
{
    check(!bBuilt);
    if (!PartyGrid::InGrid(Cell)) return;
    if (Kind==EPartyBlock::Air) Blocks.Remove(Cell);
    else Blocks.Add(Cell,Kind);
}

void APartyVoxelSession::ReplayBrushes(TArrayView<const int32> Indices)
{
    if (!bBuilt) return;
    for (int32 Index : Indices)
    {
        const auto& Brush = Changes.Items[Index];
        if (Brush.Sequence > AppliedEdits) PendingBrushes.Add(Brush.Sequence,Index);
    }
    int32 Index;
    while (PendingBrushes.RemoveAndCopyValue(AppliedEdits + 1,Index))
    {
        const auto& Brush = Changes.Items[Index];
        Terrain->ApplyBrush(Brush.Center,Brush.Normal,Brush.bFill,Brush.ProtectedMin,Brush.ProtectedMax);
        ++AppliedEdits;
    }
}

bool APartyVoxelSession::CommitBrush(FPartyVoxelChange& Brush)
{
    if (!Terrain->ApplyBrush(Brush.Center,Brush.Normal,Brush.bFill,Brush.ProtectedMin,Brush.ProtectedMax)) return false;
    Brush.Sequence = ++AppliedEdits;
    auto& Item = Changes.Items.Add_GetRef(Brush);
    Changes.MarkItemDirty(Item);
    ForceNetUpdate();
    return true;
}

void APartyVoxelSession::Box(FIntVector Min,FIntVector Max,EPartyBlock Kind)
{
    for (int32 Z=Min.Z;Z<=Max.Z;++Z)
        for (int32 Y=Min.Y;Y<=Max.Y;++Y)
            for (int32 X=Min.X;X<=Max.X;++X) Block({X,Y,Z},Kind);
}

void FPartyVoxelChanges::PostReplicatedAdd(TArrayView<int32 const> Indices,int32)
{
    if (Arena) Arena->ReplayBrushes(Indices);
}

void FPartyVoxelChanges::PostReplicatedChange(TArrayView<int32 const> Indices,int32 Size)
{
    PostReplicatedAdd(Indices,Size);
}

void APartyVoxelSession::ApplyBeacon(FIntVector Cell,FColor Color)
{
    auto& Slot=BeaconMeshes.FindOrAdd(Cell);
    if (!Slot)
    {
        Slot=NewObject<UStaticMeshComponent>(this);
        Slot->SetupAttachment(GetRootComponent());
        Slot->SetStaticMesh(BeaconMesh);
        Slot->SetRelativeLocation(FVector(Cell.X+.5,Cell.Y+.5,Cell.Z+.06)*PartyGrid::Size);
        Slot->SetRelativeScale3D(FVector(.78,.78,.09));
        Slot->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Slot->SetCanEverAffectNavigation(false);
        Slot->RegisterComponent();
        AddInstanceComponent(Slot);
        Slot->SetMaterial(0,UMaterialInstanceDynamic::Create(Material,this));
    }
    CastChecked<UMaterialInstanceDynamic>(Slot->GetMaterial(0))->SetVectorParameterValue(TEXT("Color"),FLinearColor(Color));
}

void APartyVoxelSession::Marker(FIntVector Cell,FColor Color)
{
    if (!bBuilt) InitialBeacons.Add(Cell,Color);
    else SetBeacon(Cell,Color);
}

void APartyVoxelSession::SetBeacon(FIntVector Cell,FColor Color)
{
    if (!HasAuthority() || !PartyGrid::InGrid(Cell)) return;
    if (!bBuilt) { InitialBeacons.Add(Cell,Color); return; }
    auto* Existing=Beacons.FindByPredicate([Cell](const FPartyVoxelBeacon& B){return B.Cell==Cell;});
    if (Existing && Existing->Color==Color) return;
    if (!Existing) { Existing=&Beacons.AddDefaulted_GetRef(); Existing->Cell=Cell; }
    Existing->Color=Color;
    ApplyBeacon(Cell,Color);
    ForceNetUpdate();
}

void APartyVoxelSession::OnRep_Beacons()
{
    if (bBuilt)
        for (const auto& B : Beacons) ApplyBeacon(B.Cell,B.Color);
}

void APartyVoxelSession::SetStart(int32 Seat,FIntVector Feet,float Yaw)
{
    // A UCharacterMovementComponent floor sits slightly above the collision face.
    FVector Position=CellCenter(Feet);
    Position.Z+=41.;
    Starts.Add(Seat,FTransform(FRotator(0,Yaw,0),Position));
}

APartyPawn* APartyVoxelSession::CreateParticipant(int32 Seat)
{
    if (!HasAuthority() || Seat<0 || Seat>=PartyGrid::Seats || Participants.Contains(Seat)) return nullptr;
    EnsureGrid();
    const FTransform* Pose=Starts.Find(Seat);
    if (!Pose) return nullptr;
    FActorSpawnParameters Params;
    Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    auto* Pawn=GetWorld()->SpawnActor<APartyVoxelPawn>(APartyVoxelPawn::StaticClass(),*Pose,Params);
    if (!Pawn) return nullptr;
    Pawn->Arena=this;
    Pawn->Seat=Seat;
    Participants.Add(Seat,Pawn);
    FPartyVoxelStanding& Row=Standings.AddDefaulted_GetRef();
    Row.Seat=Seat;
    ScoreFrames.Add(Seat,0);
    return Pawn;
}

void APartyVoxelSession::RemoveParticipant(int32 Seat)
{
    Participants.Remove(Seat);
    Standings.RemoveAll([Seat](const FPartyVoxelStanding& Row){return Row.Seat==Seat;});
    Completed.Remove(Seat);
    ScoreFrames.Remove(Seat);
    if (Phase==EPartyVoxelPhase::Results && Winner==Seat)
    {
        const double ReturnAt=Deadline;
        Results();
        Deadline=ReturnAt;
    }
    ForceNetUpdate();
}

double APartyVoxelSession::Clock() const
{
    const auto* State=GetWorld()->GetGameState();
    return State ? State->GetServerWorldTimeSeconds() : GetWorld()->GetTimeSeconds();
}

float APartyVoxelSession::SecondsLeft() const
{
    return FMath::Max(0.,Deadline-Clock());
}

int32 APartyVoxelSession::Score(int32 Seat) const
{
    const auto* Row=Standings.FindByPredicate([Seat](const FPartyVoxelStanding& R){return R.Seat==Seat;});
    return Row ? Row->Points : 0;
}

void APartyVoxelSession::SetScore(int32 Seat,int32 Points)
{
    if (!HasAuthority() || Phase!=EPartyVoxelPhase::Playing) return;
    auto* Row=Standings.FindByPredicate([Seat](const FPartyVoxelStanding& R){return R.Seat==Seat;});
    if (!Row || Points==Row->Points) return;
    Row->Points=FMath::Max(0,Points);
    ScoreFrames.Add(Seat,GFrameCounter);
    ForceNetUpdate();
}

TArray<int32> APartyVoxelSession::PlayingSeats() const
{
    TArray<int32> Out;
    for (const auto& Row : Standings) Out.Add(Row.Seat);
    Out.Sort();
    return Out;
}

FIntVector APartyVoxelSession::FeetCell(int32 Seat) const
{
    return CellAt(FeetLocation(Seat));
}

FVector APartyVoxelSession::FeetLocation(int32 Seat) const
{
    const auto* Pawn = Participants.FindRef(Seat).Get();
    if (!Pawn) return CellCenter({0,0,-100});
    return Pawn->GetActorLocation()-FVector(0,0,Pawn->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()-2.);
}

bool APartyVoxelSession::IsGrounded(int32 Seat) const
{
    const auto* Entry=Participants.Find(Seat);
    const auto* Pawn=Entry ? Entry->Get() : nullptr;
    return Pawn && Pawn->GetCharacterMovement()->IsMovingOnGround();
}

void APartyVoxelSession::StartParty()
{
    if (!HasAuthority() || Phase!=EPartyVoxelPhase::Waiting) return;
    for (auto& Row : Standings)
        if (const auto* Pawn=Participants.FindRef(Row.Seat).Get())
            if (const auto* Player=Pawn->GetPlayerState()) Row.PlayerName=Player->GetPlayerName();
    Phase=EPartyVoxelPhase::Countdown;
    Deadline=Clock()+2.;
    ForceNetUpdate();
}

void APartyVoxelSession::Finish(int32 Seat)
{
    if (HasAuthority() && Phase==EPartyVoxelPhase::Playing && Participants.Contains(Seat)) Completed.Add(Seat);
}

void APartyVoxelSession::Results()
{
    const bool bFinished=!Completed.IsEmpty();
    Winner=INDEX_NONE;
    for (const auto& Row : Standings)
    {
        if (bFinished && !Completed.Contains(Row.Seat)) continue;
        if (!bFinished && Row.Points<=0) continue;
        if (Winner==INDEX_NONE || Row.Points>Score(Winner)
            || (Row.Points==Score(Winner) && (ScoreFrames.FindRef(Row.Seat)<ScoreFrames.FindRef(Winner)
            || (ScoreFrames.FindRef(Row.Seat)==ScoreFrames.FindRef(Winner)
                && PartyGrid::TieKey(Seed,Row.Seat)<PartyGrid::TieKey(Seed,Winner))))) Winner=Row.Seat;
    }
    Phase=EPartyVoxelPhase::Results;
    Deadline=Clock()+3.;
    ForceNetUpdate();
    UE_LOG(LogTemp,Display,TEXT("PARTY_VOXEL_RESULT game=%s winner=%d score=%d"),*GetClass()->GetName(),Winner,Score(Winner));
}

void APartyVoxelSession::CheckParticipantBounds(APartyVoxelPawn& Pawn)
{
    const FIntVector P=FeetCell(Pawn.GetSeat());
    if (P.Z<MinCell.Z-4 || P.X<MinCell.X-4 || P.X>MaxCell.X+4
        || P.Y<MinCell.Y-4 || P.Y>MaxCell.Y+4) Respawn(Pawn);
}

void APartyVoxelSession::Respawn(APartyVoxelPawn& Pawn)
{
    const FTransform* Pose=Starts.Find(Pawn.Seat);
    if (!Pose) return;
    const auto& Capsule = *Pawn.GetCapsuleComponent();
    const double Height = Capsule.GetScaledCapsuleHalfHeight();
    const double Radius = Capsule.GetScaledCapsuleRadius();
    const FVector Foot = Pose->GetLocation()-FVector(0.,0.,Height);
    const FCollisionQueryParams Query(SCENE_QUERY_STAT(PartyRespawn),false,&Pawn);
    FVector Destination;
    const auto FindFooting = [&](FVector Point,double TraceTopOffset=60.)
    {
        FHitResult Floor;
        if (!GetWorld()->LineTraceSingleByChannel(Floor,Point+FVector(0,0,TraceTopOffset),Point-FVector(0,0,250.),ECC_Visibility,Query)
            || !Terrain->OwnsSurface(Floor.GetComponent()) || Floor.ImpactNormal.Z < Pawn.GetCharacterMovement()->GetWalkableFloorZ()) return false;
        Destination = Floor.ImpactPoint + FVector(0,0,Height+3.);
        const auto Shape = FCollisionShape::MakeCapsule(Radius,Height);
        return !GetWorld()->OverlapBlockingTestByChannel(Destination,FQuat::Identity,ECC_Pawn,Shape,Query)
            && !GetWorld()->OverlapAnyTestByObjectType(Destination,FQuat::Identity,FCollisionObjectQueryParams(ECC_Pawn),Shape,Query);
    };
    if (!FindFooting(Foot))
    {
        // Try the surviving original surfaces, closest first, using their current collision.
        const FIntVector Berth = CellAt(Foot);
        TArray<FIntVector> Candidates;
        for (const auto& Tile : Blocks)
            if (!Blocks.Contains(Tile.Key+FIntVector(0,0,1))) Candidates.Add(Tile.Key);
        Candidates.Sort([Berth](FIntVector A,FIntVector B)
        {
            const double DA = FVector(A-Berth).SizeSquared(), DB = FVector(B-Berth).SizeSquared();
            if (DA!=DB) return DA<DB;
            if (A.X!=B.X) return A.X<B.X;
            if (A.Y!=B.Y) return A.Y<B.Y;
            return A.Z<B.Z;
        });
        const bool bFound = Candidates.ContainsByPredicate([&](FIntVector C){ return FindFooting(CellCenter(C)+FVector(0,0,50.)); });
        if (!bFound)
        {
            // Deposits can bury all original standings. Search from above the build
            // ceiling for a free surface on the surviving foundations and their soil.
            const double AboveBuildLimit=(MaxCell.Z-MinCell.Z+10)*PartyGrid::Size;
            if (!Candidates.ContainsByPredicate([&](FIntVector C)
                { return FindFooting(CellCenter(C)+FVector(0,0,50.),AboveBuildLimit); })) return;
        }
    }
    Pawn.TeleportTo(Destination,Pose->Rotator());
    Pawn.GetCharacterMovement()->StopMovementImmediately();
}

#if !UE_BUILD_SHIPPING
uint32 APartyVoxelSession::GridDigest() const
{
    return Terrain->Checksum();
}
#endif

void APartyVoxelSession::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
#if !UE_BUILD_SHIPPING
    // Sample from the first visible results tick, including on clients correcting their server clock.
    if (bQAViews && Phase==EPartyVoxelPhase::Results && FPlatformTime::Seconds()>=NextProofAt)
    {
        NextProofAt=FPlatformTime::Seconds()+.25;
        TArray<FString> Scores;
        for (int32 I : PlayingSeats()) Scores.Add(FString::Printf(TEXT("%d:%d"),I,Score(I)));
        UE_LOG(LogTemp,Display,TEXT("PARTY_GRID_VIEW game=%s seed=%d initial=%08x grid=%08x changed=%d winner=%d scores=%s"),
            *GetClass()->GetName(),Seed,InitialGridDigest,GridDigest(),Changes.Items.Num(),Winner,*FString::Join(Scores,TEXT(",")));
    }
#endif
    if (!HasAuthority() || bReturned) return;
    const double Now=Clock();
    if (Phase==EPartyVoxelPhase::Countdown && Now>=Deadline)
    {
        Phase=EPartyVoxelPhase::Playing;
        Deadline=Now+PreparationSeconds()+FMath::Max(3.f,RoundSeconds);
        OnArenaStarted();
        ForceNetUpdate();
    }
    if (Phase==EPartyVoxelPhase::Playing)
    {
        for (const auto& Pair : Participants)
            if (auto* Pawn=Pair.Value.Get())
                CheckParticipantBounds(*Pawn);
        OnArenaStep(DeltaSeconds);
        if (Phase==EPartyVoxelPhase::Playing && (!Completed.IsEmpty() || Now>=Deadline || Standings.IsEmpty())) Results();
    }
    if (Phase==EPartyVoxelPhase::Results && Now>=Deadline)
    {
        bReturned=true;
        OnPartyFinished.ExecuteIfBound();
    }
}

bool APartyVoxelSession::TraceTerrain(FVector From, FVector Direction, const AActor* Ignore, FHitResult& Hit) const
{
    if (!bBuilt || Direction.IsNearlyZero()) return false;
    const FCollisionQueryParams Query(SCENE_QUERY_STAT(PartyTerrainTool),false,Ignore);
    return GetWorld()->LineTraceSingleByChannel(Hit,From,From+Direction.GetSafeNormal()*PartyGrid::Reach,ECC_Visibility,Query)
        && Terrain->OwnsSurface(Hit.GetComponent());
}

void APartyVoxelSession::ProtectParticipants(FPartyVoxelChange& Brush, TMap<APartyVoxelPawn*,FVector>& Lifts) const
{
    const FVector Center(Brush.Center);
    const FBox Footprint(Center-FVector(110.,110.,65.),Center+FVector(110.,110.,45.));
    for (const auto& Pair : Participants)
    {
        auto* Pawn = Pair.Value.Get();
        if (!Pawn) continue;
        const auto& Capsule = *Pawn->GetCapsuleComponent();
        const double Radius = Capsule.GetScaledCapsuleRadius();
        const double HalfHeight = Capsule.GetScaledCapsuleHalfHeight();
        const FVector Local = Pawn->GetActorLocation()-GetActorLocation();
        const FBox Body = FBox::BuildAABB(Local,FVector(Radius+4.,Radius+4.,HalfHeight+3.));
        if (!Body.Intersect(Footprint)) continue;
        // A standing player can raise a pad underfoot when there is headroom.
        const double Feet = Local.Z-HalfHeight;
        if (Brush.Normal.Z >= 650 && Feet>=Center.Z-10. && Feet<=Center.Z+40.
            && FVector::DistSquared2D(Local,Center)<=FMath::Square(57.))
        {
            const FVector From = Pawn->GetActorLocation();
            const FVector To(From.X, From.Y, GetActorLocation().Z + Center.Z + 43. + HalfHeight);
            const FCollisionQueryParams Query(SCENE_QUERY_STAT(PartyMudLift),false,Pawn);
            const FCollisionShape Shape = FCollisionShape::MakeCapsule(Radius,HalfHeight);
            FHitResult Hit;
            const bool bBlocked = GetWorld()->SweepSingleByChannel(Hit,From,To,FQuat::Identity,ECC_Pawn,Shape,Query)
                && (Hit.bStartPenetrating || Hit.ImpactNormal.Z<.7);
            if (!bBlocked && !GetWorld()->OverlapBlockingTestByChannel(To,FQuat::Identity,ECC_Pawn,Shape,Query))
            {
                Lifts.Add(Pawn,To);
                continue;
            }
        }
        Brush.ProtectedMin.Add(FIntVector(Body.Min)-FIntVector(1));
        Brush.ProtectedMax.Add(FIntVector(Body.Max)+FIntVector(1));
    }
}

bool APartyVoxelSession::UseTerrainTool(APartyVoxelPawn& Pawn,bool bPlace,FVector Direction)
{
    if (!HasAuthority() || Phase!=EPartyVoxelPhase::Playing || Participants.FindRef(Pawn.Seat).Get()!=&Pawn) return false;
    FHitResult Hit;
    if (!TraceTerrain(Pawn.GetPawnViewLocation(),Direction,&Pawn,Hit))
    {
        if (bPlace) return false;
        const int32 Structure = TraceVenueStructure(Pawn.GetPawnViewLocation(), Direction, &Pawn);
        if (Structure == INDEX_NONE) return false;
        DestroyedVenueStructures.Add(Structure);
        ApplyVenueDestruction(true);
        ForceNetUpdate();
        return true;
    }
    const FIntVector Cell = CellAt(Hit.ImpactPoint-Hit.ImpactNormal*2.);
    if (bPlace && (Cell.X<MinCell.X || Cell.X>MaxCell.X || Cell.Y<MinCell.Y || Cell.Y>MaxCell.Y
        || Cell.Z<MinCell.Z-1 || Cell.Z>MaxCell.Z+6)) return false;
    FPartyVoxelChange Brush;
    Brush.Center = VoxelSurfaceKernel::QuantizeCenter(Hit.ImpactPoint-GetActorLocation());
    Brush.Normal = VoxelSurfaceKernel::QuantizeNormal(Hit.ImpactNormal);
    Brush.bFill = bPlace;
    TMap<APartyVoxelPawn*,FVector> Lifts;
    if (bPlace) ProtectParticipants(Brush,Lifts);
    if (!CommitBrush(Brush)) return false;
    for (const auto& Lift : Lifts) Lift.Key->SetActorLocation(Lift.Value,false,nullptr,ETeleportType::TeleportPhysics);
    OnTerrainEdited(Pawn.Seat,FBox::BuildAABB(GetActorLocation()+FVector(Brush.Center),FVector(110.)),bPlace);
    return true;
}
