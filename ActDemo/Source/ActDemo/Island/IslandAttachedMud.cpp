#include "Island/IslandAttachedMud.h"

#include "EngineUtils.h"
#include "Island/IslandFallingScenery.h"
#include "Island/IslandNeutralProp.h"
#include "Island/IslandSceneryVoxels.h"
#include "Net/UnrealNetwork.h"
#include "Voxel/IslandRouteVoxel.h"
#include "Voxel/VoxelTerrain.h"
#include "VoxelSurfaceKernel.h"

namespace
{
    constexpr double MudCellSpacing = 10.;
    const FColor Clay(163, 102, 64);

    FIntVector RoundCell(FVector Cell)
    {
        return FIntVector(FMath::RoundToInt(Cell.X), FMath::RoundToInt(Cell.Y), FMath::RoundToInt(Cell.Z));
    }

    UPrimitiveComponent* StructuralSurface(UPrimitiveComponent* Hit)
    {
        if (Hit && Hit->GetOwner()->IsA<AIslandAttachedMud>())
            return Cast<UPrimitiveComponent>(Hit->GetAttachParent());
        return Hit;
    }

    struct FMudBody
    {
        AIslandAttachedMud* Mud = nullptr;
        FVector Start;
        FBox Bounds;
        // The integer identifies one occupied sample in the flood queue.
        TMap<FIntVector, int32> Samples;
        TArray<int32> Neighbors;
        TArray<UPrimitiveComponent*> Contacts;

        FIntVector CellAt(FVector World) const
        {
            return RoundCell((Mud->GetActorTransform().InverseTransformPosition(World) - Start) / MudCellSpacing);
        }
    };

    struct FMudNode
    {
        FIntVector Cell;
        FVector World;
        int32 Body;
        bool Reached = false;
    };

    bool StructuralContact(const UPrimitiveComponent& Part, FVector World)
    {
        if (!Part.IsVisible() || !Part.Bounds.GetBox().ExpandBy(MudCellSpacing).IsInsideOrOn(World)) return false;
        const auto* Mesh = Cast<UIslandMeshVoxels>(&Part);
        const auto* Route = Cast<UIslandRouteVoxelComponent>(&Part);
        if (!Mesh && !Route) return false;
        const auto Solid = [&](FVector Point) { return Mesh ? Mesh->IsSolidWorld(Point) : Route->IsSolidWorld(Point); };
        if (Solid(World)) return true;
        // A thin plank or an oblique hull can cross the edge of a 10 cm mud sample.
        for (const FIntVector Offset : VoxelSurfaceKernel::FaceNeighbors)
            if (Solid(World + FVector(Offset) * (MudCellSpacing * .85))) return true;
        return false;
    }

    void RefreshTideNear(UWorld& World, const FBox& Area)
    {
        AVoxelTerrain* Terrain = AVoxelTerrain::Find(&World);
        if (!Terrain) return;
        for (TActorIterator<AIslandNeutralProp> It(&World); It; ++It)
            if (It->GetKind() == EIslandNeutralKind::Tide && Area.Intersect(It->GetComponentsBoundingBox(true)))
                It->RefreshTide(*Terrain);
    }
}

// Boolean difference against pre-existing mud. Replicas restore the cropped
// samples with the same deltas used for later shovel cuts.
bool UIslandRouteVoxelComponent::SubtractVolume(TFunctionRef<double(FVector)> LocalDistance)
{
    bool bChanged = false;
    for (int32 I = 0; I < Samples.Num(); ++I)
    {
        const double Distance = LocalDistance(GridStart + FVector(Coordinates(I)) * MudCellSpacing);
        const int8 Value = FMath::RoundToInt(FMath::Clamp(-Distance * VoxelSurfaceKernel::Spacing / MudCellSpacing,
            -double(VoxelSurfaceKernel::Band), double(VoxelSurfaceKernel::Band)));
        if (Value <= Samples[I]) continue;
        if (Samples[I] < 0 && Value >= 0) --SolidCount;
        Samples[I] = Value;
        bChanged = true;
    }
    if (bChanged) UpdateSurface();
    return bChanged;
}

void UIslandRouteVoxelComponent::VisitSolidSamples(TFunctionRef<void(FVector)> Visit) const
{
    for (int32 I = 0; I < Samples.Num(); ++I)
        if (Samples[I] < 0) Visit(GridStart + FVector(Coordinates(I)) * MudCellSpacing);
}

AIslandAttachedMud::AIslandAttachedMud()
{
    bReplicates = true;
    bNetLoadOnClient = false;
    SetReplicateMovement(true);
    Voxels = CreateDefaultSubobject<UIslandRouteVoxelComponent>(TEXT("ClayVolume"));
    Voxels->SetAbsolute(false, false, true);
    Voxels->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
    SetRootComponent(Voxels);
}

void AIslandAttachedMud::BeginPlay()
{
    Super::BeginPlay();
    RebuildInitial();
}

void AIslandAttachedMud::RebuildInitial()
{
    if (Stamp.BrushId != VoxelPlayground::MudPad || Voxels->InitialCount()) return;
    const FBox Local = VoxelPlayground::PartyBrushBounds(Stamp).ShiftBy(-FVector(Stamp.Center));
    Voxels->InitializeField(Local, MudCellSpacing, [&](FVector P)
    {
        const FIntVector Point = Stamp.Center + RoundCell(P);
        const int32 D = VoxelPlayground::PartyBrushDistance(Stamp, Point);
        const int32 Value = FMath::Clamp(FMath::RoundToInt(P.Z * .18), -9, 7);
        return FRouteVoxelSample{double(D), FColor(Clay.R + Value, Clay.G + Value, Clay.B + Value)};
    });
    ApplyChanges();
}

void AIslandAttachedMud::ApplyChanges()
{
    if (Voxels->InitialCount() && !Changes.IsEmpty()) Voxels->ReadChanges(Changes);
}

void AIslandAttachedMud::Publish()
{
    Voxels->WriteChanges(Changes);
    ForceNetUpdate();
    // The falling copy may still need this field's original samples on a peer.
    if (!Voxels->RemainingCount()) SetLifeSpan(5.f);
}

void AIslandAttachedMud::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME_CONDITION(AIslandAttachedMud, Stamp, COND_InitialOnly);
    DOREPLIFETIME(AIslandAttachedMud, Changes);
}

bool AIslandAttachedMud::CanReceive(const UPrimitiveComponent* Surface)
{
    return Surface && Surface->IsVisible() && (Surface->IsA<UIslandMeshVoxels>() || Surface->IsA<UIslandRouteVoxelComponent>());
}

void AIslandAttachedMud::GatherNearby(UWorld& World, const FBox& Area, TArray<AIslandAttachedMud*>& Out)
{
    Out.Reset();
    for (TActorIterator<AIslandAttachedMud> It(&World); It; ++It)
        if (It->Voxels->RemainingCount() && Area.Intersect(It->Voxels->Bounds.GetBox()))
            Out.Add(*It);
}

bool AIslandAttachedMud::ContainsMud(const TArray<AIslandAttachedMud*>& Nearby, FVector WorldPoint)
{
    return Nearby.ContainsByPredicate([&](const AIslandAttachedMud* Mud)
    {
        return Mud->Voxels->Bounds.GetBox().IsInsideOrOn(WorldPoint) && Mud->Voxels->IsSolidWorld(WorldPoint);
    });
}

AIslandAttachedMud* AIslandAttachedMud::Place(const FHitResult& Hit, const FVoxelEdit& Edit)
{
    UPrimitiveComponent* Support = StructuralSurface(Hit.GetComponent());
    if (!CanReceive(Support) || !Support->GetOwner()->HasAuthority() || Edit.BrushId != VoxelPlayground::MudPad) return nullptr;
    UWorld& World = *Support->GetWorld();
    const FTransform Transform(FVector(Edit.Center));
    const FBox Area = VoxelPlayground::PartyBrushBounds(Edit);
    TArray<AIslandAttachedMud*> Nearby;
    GatherNearby(World, Area.ExpandBy(MudCellSpacing), Nearby);
    // Different moving frames may later pull apart. Only clay carried by this
    // structure participates in its union and can transmit support to a new throw.
    Nearby.RemoveAll([&](const AIslandAttachedMud* Other) { return Other->GetAttachParentActor() != Support->GetOwner(); });
    AIslandAttachedMud* Mud = World.SpawnActorDeferred<AIslandAttachedMud>(StaticClass(), Transform, nullptr,
        nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
    if (!Mud) return nullptr;
    Mud->Stamp = Edit;
    Mud->FinishSpawning(Transform);
    Mud->RebuildInitial();
    // Inherited scale would squash a tread attached to a scaled beam or a rolled-up sail.
    Mud->AttachToComponent(Support, FAttachmentTransformRules::KeepWorldTransform);
    if (!Nearby.IsEmpty())
    {
        Mud->Voxels->SubtractVolume([&](FVector P)
        {
            const FVector W = Transform.TransformPosition(P);
            double D = VoxelSurfaceKernel::Band;
            for (const auto* Existing : Nearby)
            {
                if (!Existing->Voxels->Bounds.GetBox().ExpandBy(MudCellSpacing * 2.).IsInsideOrOn(W)) continue;
                const FVector Local = Existing->GetActorTransform().InverseTransformPosition(W);
                D = FMath::Min(D, Existing->Voxels->SignedDistanceLocal(Local));
            }
            return D;
        });
    }
    // An addition cannot disconnect an older pad. Check only the fresh field
    // against the intact host and existing grounded mud. Full graph traversal
    // is reserved for a cut or a change to the supporting structure.
    Mud->Voxels->CullUnsupported([&](FVector Local)
    {
        const FVector W = Transform.TransformPosition(Local);
        if (StructuralContact(*Support, W)) return true;
        return Nearby.ContainsByPredicate([&](AIslandAttachedMud* Other)
        {
            return Other->Voxels->Bounds.GetBox().ExpandBy(MudCellSpacing).IsInsideOrOn(W) &&
                Other->Voxels->SignedDistanceLocal(Other->GetActorTransform().InverseTransformPosition(W)) <= MudCellSpacing;
        });
    });
    if (!Mud->Voxels->RemainingCount())
    {
        // A clipped or duplicate throw never claims an empty movement base.
        if (Mud->GetLifeSpan() <= 0.) Mud->Destroy();
        return nullptr;
    }
    Mud->Publish();
    RefreshTideNear(World, Area);
    return Mud;
}

bool AIslandAttachedMud::Strike(const FHitResult& Hit, float RadiusScale, float DepthScale)
{
    if (!HasAuthority() || Hit.GetComponent() != Voxels) return false;
    const double Radius = 62.*RadiusScale;
    const FBox Area = FBox::BuildAABB(Hit.ImpactPoint, FVector(Radius * 1.8));
    TArray<AIslandAttachedMud*> Nearby;
    GatherNearby(*GetWorld(), Area, Nearby);
    TSet<AActor*> Owners;
    bool bChanged = false;
    for (AIslandAttachedMud* Mud : Nearby)
    {
        // One shovel carves the entire contact volume, including neighbouring throws.
        if (Mud->GetAttachParentActor()!=GetAttachParentActor() || !Mud->Voxels->ApplyWorldCut(Hit.ImpactPoint, Hit.ImpactNormal, Radius, DepthScale)) continue;
        if (AActor* Owner = Mud->GetAttachParentActor()) Owners.Add(Owner);
        Mud->Publish();
        bChanged = true;
    }
    if (!bChanged) return false;
    for (AActor* Owner : Owners) RefreshFor(*Owner);
    AIslandVoxelEdits::EmitChips(*GetWorld(), Hit, EIslandChipSurface::Sand, Clay);
    RefreshTideNear(*GetWorld(), Area);
    return true;
}

void AIslandAttachedMud::RefreshFor(AActor& Structure)
{
    if (!Structure.HasAuthority()) return;
    TArray<AActor*> Children;
    Structure.GetAttachedActors(Children);
    TArray<FMudBody> Bodies;
    TArray<FMudNode> Nodes;
    for (AActor* Child : Children)
    {
        AIslandAttachedMud* Mud = Cast<AIslandAttachedMud>(Child);
        if (!Mud || !Mud->Voxels->RemainingCount()) continue;
        FMudBody& Body = Bodies.AddDefaulted_GetRef();
        const int32 BodyIndex = Bodies.Num() - 1;
        Body.Mud = Mud;
        Body.Start = Mud->Voxels->GetFieldLocalBounds().Min;
        Body.Bounds = Mud->Voxels->Bounds.GetBox().ExpandBy(MudCellSpacing * 1.5);
        Mud->Voxels->VisitSolidSamples([&](FVector Local)
        {
            const FIntVector Cell = RoundCell((Local - Body.Start) / MudCellSpacing);
            Body.Samples.Add(Cell, Nodes.Num());
            Nodes.Add({Cell, Mud->GetActorTransform().TransformPosition(Local), BodyIndex});
        });
    }
    if (Bodies.IsEmpty()) return;
    for (int32 A = 0; A < Bodies.Num(); ++A)
        for (int32 B = A + 1; B < Bodies.Num(); ++B)
            if (Bodies[A].Bounds.Intersect(Bodies[B].Bounds))
            {
                Bodies[A].Neighbors.Add(B);
                Bodies[B].Neighbors.Add(A);
            }

    TInlineComponentArray<UPrimitiveComponent*> Parts(&Structure);
    Parts.RemoveAll([](UPrimitiveComponent* Part) { return !CanReceive(Part); });
    if (!Structure.IsHidden())
        for (FMudBody& Body : Bodies)
            for (UPrimitiveComponent* Part : Parts)
                if (Body.Bounds.Intersect(Part->Bounds.GetBox())) Body.Contacts.Add(Part);
    const auto* Terrain = AVoxelTerrain::Find(Structure.GetWorld());
    const auto* Ground = Terrain ? Terrain->GetDensity() : nullptr;
    TArray<int32> Reachable;
    for (int32 I = 0; I < Nodes.Num(); ++I)
    {
        FMudNode& Node = Nodes[I];
        // Only the host or the terrain may seed the flood. A stack of mud cannot
        // keep itself suspended by feeding support back to its neighbour.
        const bool TouchesSoil = Ground && Ground->SignedDistanceAt(Node.World) <= MudCellSpacing * .6;
        if (TouchesSoil || Bodies[Node.Body].Contacts.ContainsByPredicate([&](const UPrimitiveComponent* Part)
            { return StructuralContact(*Part, Node.World); }))
        {
            Node.Reached = true;
            Reachable.Add(I);
        }
    }
    const auto Reach = [&](const int32* I)
    {
        if (I && !Nodes[*I].Reached)
        {
            Nodes[*I].Reached = true;
            Reachable.Add(*I);
        }
    };
    for (int32 Head = 0; Head < Reachable.Num(); ++Head)
    {
        const FMudNode& Node = Nodes[Reachable[Head]];
        const FMudBody& Body = Bodies[Node.Body];
        for (FIntVector Dir : VoxelSurfaceKernel::FaceNeighbors) Reach(Body.Samples.Find(Node.Cell + Dir));
        for (int32 Peer : Body.Neighbors)
        {
            const FMudBody& Other = Bodies[Peer];
            if (!Other.Bounds.IsInsideOrOn(Node.World)) continue;
            const FIntVector Center = Other.CellAt(Node.World);
            for (int32 Z = -1; Z <= 1; ++Z)
                for (int32 Y = -1; Y <= 1; ++Y)
                    for (int32 X = -1; X <= 1; ++X)
                    {
                        const int32* N = Other.Samples.Find(Center + FIntVector(X,Y,Z));
                        if (N && !Nodes[*N].Reached && FVector::DistSquared(Nodes[*N].World, Node.World) <= MudCellSpacing * MudCellSpacing * 2.15)
                            Reach(N);
                    }
        }
    }
    if (Reachable.Num() == Nodes.Num()) return;
    FBox Changed(ForceInit);
    TMap<AIslandAttachedMud*, TArray<FIslandLooseSamples>> Falls;
    for (const FMudBody& Body : Bodies)
    {
        AIslandAttachedMud* Mud = Body.Mud;
        TArray<FIslandLooseSamples> Groups;
        if (Mud->Voxels->CullUnsupported([&](FVector Local)
        {
            const int32* N = Body.Samples.Find(RoundCell((Local - Body.Start) / MudCellSpacing));
            return N && Nodes[*N].Reached;
        }, &Groups))
        {
            Falls.Add(Mud, MoveTemp(Groups));
            Mud->Publish();
            Changed += Body.Bounds;
        }
    }
    // Resolve landing surfaces after every loose course has relinquished its
    // collision. An upper course cannot land on a lower course in the same fall.
    for (const auto& Fall : Falls)
        AIslandFallingScenery::Release(*Fall.Key->Voxels, Fall.Value, EIslandChipSurface::Sand);
    if (Changed.IsValid) RefreshTideNear(*Structure.GetWorld(), Changed);
}
