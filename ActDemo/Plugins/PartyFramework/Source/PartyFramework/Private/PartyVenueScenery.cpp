#include "PartyVenueScenery.h"
#include "PartyVoxelArena.h"
#include "PartyCarvableMesh.h"
#include "PartyPunch.h"

#include "Components/StaticMeshComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/Level.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "Misc/App.h"

namespace
{
    FName AuthoredGroup(const AActor& Actor)
    {
        for (FName Tag : Actor.Tags)
            if (Tag.GetNumber() > 0 && FName(Tag, 0) == PartyVenueScenery::StructureTag)
                return Tag;
        return NAME_None;
    }

    // The original six maps predate group tags. They use these two distinct sized
    // cylinders for the lagoon and ten island shelves. Everything atop each shelf
    // was authored as one skyline structure.
    bool IsLegacyCylinder(const UStaticMeshComponent& Part, FVector Scale)
    {
        return Part.GetStaticMesh()->GetFName() == TEXT("Cylinder")
            && Part.GetComponentScale().GetAbs().Equals(Scale, .01);
    }

    void EnableToolTrace(UStaticMeshComponent& Part)
    {
        Part.SetCollisionEnabled(ECollisionEnabled::QueryOnly);
        Part.SetCollisionResponseToAllChannels(ECR_Ignore);
        Part.SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
    }


}

void APartyVoxelSession::BindVenueLevel(ULevel& Level)
{
    if (BoundVenue.Get() == &Level || Level.GetWorld() != GetWorld()) return;
    BoundVenue = &Level;
    VenueStructures.Reset();
    VenueFields.Reset();
    bVenuePlaced=false;
    VenuePartIds.Reset();

    TArray<UStaticMeshComponent*> Shelves, Parts;
    bool bHasOriginalLagoon = false;
    for (AActor* Actor : Level.Actors)
    {
        auto* Scenery = Cast<AStaticMeshActor>(Actor);
        if (!Scenery) continue;
        auto* Part = Scenery->GetStaticMeshComponent();
        if (!Part || !Part->GetStaticMesh() || !Part->IsVisible()) continue;
        if (IsLegacyCylinder(*Part, FVector(245.,245.,1.8)))
        {
            bHasOriginalLagoon = true;
            continue;
        }
        if (Actor->ActorHasTag(PartyVenueScenery::WaterTag)) continue;
        if (Actor->ActorHasTag(PartyVenueScenery::ShelfTag) || IsLegacyCylinder(*Part, FVector(17.5,14.2,13.2)))
        {
            Shelves.Add(Part);
            EnableToolTrace(*Part);
        }
        else Parts.Add(Part);
    }

    TMap<FName, TArray<TWeakObjectPtr<UStaticMeshComponent>>> Groups;
    const bool bOriginalLayout = bHasOriginalLagoon && Shelves.Num() == 10;
    for (UStaticMeshComponent* Part : Parts)
    {
        FName Group = AuthoredGroup(*Part->GetOwner());
        if (Group.IsNone() && bOriginalLayout)
        {
            UStaticMeshComponent* Nearest = nullptr;
            double NearestDistance = FMath::Square(1000.);
            for (UStaticMeshComponent* Shelf : Shelves)
            {
                const double Distance = FVector::DistSquared2D(Part->GetComponentLocation(), Shelf->GetComponentLocation());
                if (Distance < NearestDistance)
                {
                    Nearest = Shelf;
                    NearestDistance = Distance;
                }
            }
            if (Nearest)
            {
                Group = AuthoredGroup(*Nearest->GetOwner());
                if (Group.IsNone()) Group = Nearest->GetOwner()->GetFName();
            }
        }
        if (!Group.IsNone()) Groups.FindOrAdd(Group).Add(Part);
    }

    // A streaming instance has its own object/package pointers on each peer.
    // Sort package-local group names so one replicated index selects the same building.
    TArray<FName> Names;
    Groups.GetKeys(Names);
    Names.Sort(FNameLexicalLess());
    for (FName Name : Names)
    {
        Groups.FindChecked(Name).Sort([](const TWeakObjectPtr<UStaticMeshComponent>& A,const TWeakObjectPtr<UStaticMeshComponent>& B)
        { return A->GetOwner()->GetFName().LexicalLess(B->GetOwner()->GetFName()); });
        const int32 Id = VenueStructures.Add(MoveTemp(Groups.FindChecked(Name)));
        for (const auto& Weak : VenueStructures[Id])
        {
            VenuePartIds.Add(Weak, Id);
            EnableToolTrace(*Weak.Get());
        }
    }
    VenueFields.SetNum(VenueStructures.Num());
    PlaceVenueBuildings();
    ReplayBrushes({});
}

void APartyVoxelSession::GetVenueStructureParts(int32 Id, TArray<UStaticMeshComponent*>& Out) const
{
    Out.Reset();
    if (VenueStructures.IsValidIndex(Id))
        for (const auto& Weak : VenueStructures[Id])
            if (UStaticMeshComponent* Part = Weak.Get()) Out.Add(Part);
}

int32 APartyVoxelSession::TraceVenueStructure(FVector From, FVector Direction, const AActor* Ignore) const
{
    if (!BoundVenue.IsValid() || Direction.IsNearlyZero()) return INDEX_NONE;
    FHitResult Hit;
    const FCollisionQueryParams Query(SCENE_QUERY_STAT(PartyVenueDemolition), false, Ignore);
    if (!GetWorld()->LineTraceSingleByChannel(Hit, From, From + Direction.GetSafeNormal() * StructureReach, ECC_Visibility, Query))
        return INDEX_NONE;
    auto* Field=Cast<UPartyCarvableMesh>(Hit.GetComponent());
    auto* Part = Field ? Field->Source() : Cast<UStaticMeshComponent>(Hit.GetComponent());
    const int32* Id = Part ? VenuePartIds.Find(Part) : nullptr;
    return Id && !IsVenueStructureDestroyed(*Id) ? *Id : INDEX_NONE;
}

// An optional pier on the starting side of the course. A single entrance keeps
// the structures accessible without bridging a scoring gap or shortcutting a race.
void APartyVoxelSession::BuildVenuePaths()
{
    const FTransform* Start=Starts.Find(0);
    if (!Start || Blocks.IsEmpty()) return;
    const int32 Y=CellAt(Start->GetLocation()).Y;
    int32 West=MAX_int32;
    for (const auto& Entry:Blocks) West=FMath::Min(West,Entry.Key.X);
    const int32 MidY=FMath::Clamp(Y,-22,22), Spine=West-11;
    Box({Spine-1,MidY-1,0},{West,MidY+1,0},EPartyBlock::Stone);
    Box({Spine-1,MidY-17,0},{Spine+1,MidY+17,0},EPartyBlock::Stone);
    for (int32 I=0;I<10;++I)
    {
        const FIntVector Pad(Spine+(I%2 ? 5 : -5),MidY+8*(I/2-2),0);
        VenuePads.Add(Pad);
        Box({Pad.X-3,Pad.Y-3,0},{Pad.X+3,Pad.Y+3,0},EPartyBlock::Stone);
        Box({FMath::Min(Pad.X,Spine),Pad.Y-1,0},{FMath::Max(Pad.X,Spine),Pad.Y+1,0},EPartyBlock::Stone);
    }
}

void APartyVoxelSession::PlaceVenueBuildings()
{
    if (!bBuilt || !BoundVenue.IsValid() || bVenuePlaced || IsChaseGame() || VenuePads.IsEmpty()) return;
    for (int32 Id=0;Id<FMath::Min(VenuePads.Num(),VenueStructures.Num());++Id)
    {
        FBox Bounds(ForceInit);
        for (const auto& Weak:VenueStructures[Id]) if (Weak.IsValid()) Bounds+=Weak->Bounds.GetBox();
        if (!Bounds.IsValid) continue;
        const FVector Base(Bounds.GetCenter().X,Bounds.GetCenter().Y,Bounds.Min.Z);
        const FVector Landing=CellCenter(VenuePads[Id])+FVector(0,0,46.);
        constexpr double Scale=.48;
        for (const auto& Weak:VenueStructures[Id])
        {
            auto* Part=Weak.Get(); if (!Part) continue;
            const FVector Relative=Part->GetComponentLocation()-Base;
            Part->SetMobility(EComponentMobility::Movable);
            Part->SetWorldLocation(Landing+Relative*Scale);
            Part->SetWorldScale3D(Part->GetComponentScale()*Scale);
            Part->SetCollisionProfileName(TEXT("BlockAll"));
            Part->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
        }
    }
    bVenuePlaced=true;
}

bool APartyVoxelSession::EnsureVenueFields(int32 Id,TArray<UPartyCarvableMesh*>& Out)
{
    if (!bVenuePlaced || !VenueFields.IsValidIndex(Id)) return false;
    auto& Fields=VenueFields[Id];
    if (Fields.IsEmpty())
        for (const auto& Part:VenueStructures[Id])
            Fields.Add(Part.IsValid() ? UPartyCarvableMesh::Create(*Part.Get(),18.) : nullptr);
    for (const auto& Field:Fields) if (Field.IsValid()) Out.Add(Field.Get());
    return Out.Num()>0;
}

bool APartyVoxelSession::ApplyStructureBrush(const FPartyVoxelChange& Brush)
{
    TArray<UPartyCarvableMesh*> Parts;
    if (Brush.bFill || !EnsureVenueFields(Brush.SurfaceId,Parts)) return false;
    const FVector Point=GetActorLocation()+FVector(Brush.Center);
    const FVector Normal=FVector(Brush.Normal).GetSafeNormal();
    bool Changed=false;
    for (auto* Part:Parts)
    {
        TArray<int32> Cells;
        Part->SelectBrush(Point,Normal,62.*PartyPunch::RadiusScale(Brush.Power),40.,false,Cells);
        Changed |= Part->SetCells(Cells,false);
    }
    if (Changed) UPartyCarvableMesh::DropUnsupported(Parts,[this](FVector P){ return TerrainDensityAt(P)<=2.; });
    return Changed;
}

void APartyVoxelSession::CheckVenueFoundations(const FBox& Cut)
{
    if (!bVenuePlaced) return;
    for (int32 Id=0;Id<VenueStructures.Num();++Id)
    {
        if (!VenueStructures[Id].ContainsByPredicate([&](const auto& W){return W.IsValid() && W->Bounds.GetBox().ExpandBy(25.).Intersect(Cut);})) continue;
        TArray<UPartyCarvableMesh*> Parts;
        if (EnsureVenueFields(Id,Parts)) UPartyCarvableMesh::DropUnsupported(Parts,[this](FVector P){return TerrainDensityAt(P)<=2.;});
    }
}

bool APartyVoxelSession::IsVenueFullyCarved(int32 Id) const
{
    if (!VenueFields.IsValidIndex(Id) || VenueFields[Id].IsEmpty()) return false;
    return !VenueFields[Id].ContainsByPredicate([](const auto& W){return !W.IsValid() || W->Data().Remaining>0;});
}

#if !UE_BUILD_SHIPPING
uint32 APartyVoxelSession::VenueDamageDigest(int32 Group) const
{
    if (!VenueFields.IsValidIndex(Group)) return 0;
    uint32 Hash=0;
    for (int32 P=0;P<VenueFields[Group].Num();++P)
        if (auto* Field=VenueFields[Group][P].Get())
            for (TConstSetBitIterator<> I(Field->Data().Missing);I;++I)
                Hash=HashCombineFast(Hash,HashCombineFast(P+1,I.GetIndex()+1));
    return Hash;
}

int32 APartyVoxelSession::ReachableVenuePads(int32 Seat) const
{
    if (!Starts.Contains(Seat) || !bVenuePlaced) return 0;
    const FIntVector Start=CellAt(Starts.FindChecked(Seat).GetLocation());
    TSet<FIntPoint> Seen;
    TArray<FIntPoint> Queue{{Start.X,Start.Y}};
    Seen.Add(Queue[0]);
    for (int32 I=0;I<Queue.Num();++I)
        for (FIntPoint Step:{FIntPoint(1,0),FIntPoint(0,1),FIntPoint(-1,0),FIntPoint(0,-1)})
        {
            const FIntPoint At=Queue[I]+Step;
            if (Seen.Contains(At) || BlockAt({At.X,At.Y,0})==EPartyBlock::Air
                || BlockAt({At.X,At.Y,1})!=EPartyBlock::Air || BlockAt({At.X,At.Y,2})!=EPartyBlock::Air) continue;
            Seen.Add(At); Queue.Add(At);
        }
    int32 Count=0;
    for (FIntVector Pad:VenuePads) if (Seen.Contains({Pad.X,Pad.Y})) ++Count;
    return Count;
}

bool APartyVoxelSession::VenueApproach(int32 Group,const APartyVoxelPawn& Pawn,FVector& Stand,FVector& Target) const
{
    if (!VenuePads.IsValidIndex(Group) || !VenueStructures.IsValidIndex(Group)) return false;
    const FVector Pad=CellCenter(VenuePads[Group])+FVector(0,0,50.);
    const auto* Cap=Pawn.GetCapsuleComponent();
    FCollisionQueryParams Q(SCENE_QUERY_STAT(VenueApproach),false,&Pawn);
    for (int32 N=0;N<24;++N)
    {
        const FVector Out=FRotator(0,N*15.,0).Vector();
        for (double Radius:{255.,205.,158.,115.})
        {
            const FVector Feet=Pad+Out*Radius;
            if (TerrainDensityAt(Feet-FVector(0,0,16.))>0.) continue;
            Stand=Feet+FVector(0,0,Cap->GetScaledCapsuleHalfHeight()+3.);
            if (GetWorld()->OverlapBlockingTestByChannel(Stand,FQuat::Identity,ECC_Pawn,FCollisionShape::MakeCapsule(
                Cap->GetScaledCapsuleRadius(),Cap->GetScaledCapsuleHalfHeight()),Q)) continue;
            const FVector Eye=Stand+FVector(0,0,Pawn.BaseEyeHeight);
            for (const auto& W:VenueStructures[Group])
            {
                auto* Part=W.Get(); if (!Part) continue;
                const FBox Box=Part->Bounds.GetBox();
                const FVector Aim(Box.GetCenter().X,Box.GetCenter().Y,FMath::Clamp(Eye.Z,Box.Min.Z+1.,Box.Max.Z-1.));
                const FVector Direction=(Aim-Eye).GetSafeNormal();
                FHitResult Hit;
                if (TraceVenueStructure(Eye,Direction,&Pawn)==Group && GetWorld()->LineTraceSingleByChannel(
                    Hit,Eye,Eye+Direction*PartyPunch::Reach,ECC_Visibility,Q)) { Target=Hit.ImpactPoint; return true; }
            }
        }
    }
    return false;
}
#endif
