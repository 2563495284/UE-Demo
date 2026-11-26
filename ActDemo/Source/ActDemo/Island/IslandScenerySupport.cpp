#include "Island/IslandScenerySupport.h"
#include "Island/ExpeditionDock.h"
#include "Island/IslandNeutralProp.h"
#include "Island/IslandBreakable.h"
#include "Island/IslandWorld.h"
#include "Island/IslandSceneryVoxels.h"
#include "Voxel/VoxelTerrain.h"

namespace
{
    // Permit the tiny gap between a sampled material surface and its occupied cell.
    constexpr double ContactMargin = 2.5;

    struct FSupportPart
    {
        UIslandMeshVoxels* Field = nullptr;
        TMap<int32,FVector> Centers;
        TSet<int32> Reached;
        TArray<int32> AdjacentParts;
        FBox Box = FBox(ForceInit);
        FVector Axis[3];
        double Radius = 0.;
    };

    double ExtentOn(const FSupportPart& Part, FVector Axis)
    {
        return FMath::Abs(FVector::DotProduct(Part.Axis[0],Axis)) +
            FMath::Abs(FVector::DotProduct(Part.Axis[1],Axis)) +
            FMath::Abs(FVector::DotProduct(Part.Axis[2],Axis));
    }

    // Separating-axis test on the actual world-space cell boxes. A long, thin
    // plank cannot carry a beam across the empty space inside its bounding sphere.
    bool Touches(const FSupportPart& A, const FSupportPart& B, FVector Delta)
    {
        const auto Separated = [&](FVector Axis)
        {
            const double Length = Axis.Size();
            if (Length <= UE_SMALL_NUMBER) return false;
            return FMath::Abs(FVector::DotProduct(Delta,Axis)) >
                ExtentOn(A,Axis) + ExtentOn(B,Axis) + ContactMargin * Length;
        };
        for (int32 I = 0; I < 3; ++I)
        {
            if (Separated(A.Axis[I]) || Separated(B.Axis[I])) return false;
            for (int32 J = 0; J < 3; ++J)
                if (Separated(FVector::CrossProduct(A.Axis[I], B.Axis[J]))) return false;
        }
        return true;
    }

    bool Rooted(const FSupportPart& Part, FVector Center, const FVoxelDensity& Terrain)
    {
        // Sloping banks and cave ceilings may touch just one corner.
        const double D = Terrain.SignedDistanceAt(Center);
        if (D <= ContactMargin) return true;
        for (int32 Mask = 0; Mask < 8; ++Mask)
        {
            FVector Corner = Center;
            for (int32 A = 0; A < 3; ++A) Corner += (Mask & (1 << A)) ? Part.Axis[A] : -Part.Axis[A];
            if (Terrain.SignedDistanceAt(Corner) <= ContactMargin) return true;
        }
        return false;
    }
}

void IslandScenerySupport::FindUnsupported(const FVoxelDensity& Ground, TConstArrayView<UIslandMeshVoxels*> Parts,
    TMap<UIslandMeshVoxels*,TArray<int32>>& Removed, TFunctionRef<bool(const UIslandMeshVoxels&,int32,FVector)> ExtraAnchor)
{
    Removed.Reset();
    TArray<FSupportPart> Bodies;
    TArray<FIntPoint> Queue;
    for (UIslandMeshVoxels* Field : Parts)
    {
        if (!Field || !Field->RemainingCount()) continue;
        FSupportPart& Part = Bodies.AddDefaulted_GetRef();
        Part.Field = Field;
        Part.Radius = Field->CellRadius();
        Field->GetCellAxes(Part.Axis[0],Part.Axis[1],Part.Axis[2]);
        Part.Centers.Reserve(Field->RemainingCount());
        Field->ForEachSolid([&](int32 Cell, FVector Center)
        {
            Part.Centers.Add(Cell, Center);
            Part.Box += Center;
        });
        Part.Box = Part.Box.ExpandBy(Part.Radius + ContactMargin);
    }
    for (int32 I = 0; I < Bodies.Num(); ++I)
        for (int32 J = I+1; J < Bodies.Num(); ++J)
            if (Bodies[I].Box.Intersect(Bodies[J].Box))
            {
                Bodies[I].AdjacentParts.Add(J);
                Bodies[J].AdjacentParts.Add(I);
            }
    TArray<int32> Neighbors;
    // Visit each connected island of cells once. One true contact bears the entire
    // island; further terrain samples are unnecessary until a new island begins.
    for (int32 I = 0; I < Bodies.Num(); ++I)
    {
        FSupportPart& Start = Bodies[I];
        for (const auto& Seed : Start.Centers)
        {
            if (Start.Reached.Num() == Start.Centers.Num()) break;
            if (Start.Reached.Contains(Seed.Key)) continue;
            Queue.Reset();
            Queue.Add({I,Seed.Key});
            Start.Reached.Add(Seed.Key);
            bool bAnchored = false;
            for (int32 Head=0; Head < Queue.Num(); ++Head)
            {
                const FIntPoint Node = Queue[Head];
                FSupportPart& Part = Bodies[Node.X];
                Neighbors.Reset();
                Part.Field->GetCellNeighbors(Node.Y, Neighbors);
                for (int32 N : Neighbors)
                {
                    bool bAlreadyReached = false;
                    if (!Part.Centers.Contains(N)) continue;
                    Part.Reached.Add(N, &bAlreadyReached);
                    if (!bAlreadyReached) Queue.Add({Node.X,N});
                }
                const FVector Center = Part.Centers.FindChecked(Node.Y);
                if (!bAnchored)
                    bAnchored = Rooted(Part,Center,Ground) || ExtraAnchor(*Part.Field, Node.Y, Center);
                for (int32 Other : Part.AdjacentParts)
                {
                    FSupportPart& Next = Bodies[Other];
                    if (Next.Reached.Num() == Next.Centers.Num() || Next.Box.ComputeSquaredDistanceToPoint(Center) >
                        FMath::Square(Part.Radius + ContactMargin)) continue;
                    Neighbors.Reset();
                    Next.Field->FindOccupiedNear(Center, Part.Radius + Next.Radius + ContactMargin, Neighbors);
                    for (int32 N : Neighbors)
                    {
                        if (Next.Reached.Contains(N)) continue;
                        const FVector* P = Next.Centers.Find(N);
                        if (P && Touches(Part, Next, Center - *P))
                        {
                            Next.Reached.Add(N);
                            Queue.Add({Other,N});
                        }
                    }
                }
            }
            if (!bAnchored)
                for (FIntPoint Cell : Queue) Removed.FindOrAdd(Bodies[Cell.X].Field).Add(Cell.Y);
        }
    }
    for (auto& Loss : Removed) Loss.Value.Sort();
}

void IslandScenerySupport::FindUnsupported(const FVoxelDensity& Ground,TConstArrayView<UIslandMeshVoxels*> Parts,
    TMap<UIslandMeshVoxels*,TArray<int32>>& Removed)
{
    FindUnsupported(Ground, Parts, Removed, [](const UIslandMeshVoxels&,int32,FVector){return false;});
}

void IslandScenerySupport::InspectActor(AActor& Actor, const AVoxelTerrain& Terrain,
    TMap<UIslandMeshVoxels*,TArray<int32>>& Removed)
{
    Removed.Reset();
    const FVoxelDensity* Ground = Terrain.GetDensity();
    if (!Ground || Actor.IsHidden()) return;
    TInlineComponentArray<UIslandMeshVoxels*> Parts(&Actor);
    Parts.RemoveAll([](const UIslandMeshVoxels* Part)
    {
        // A discharged hopper pin and its charge no longer transmit loads. The
        // tree, gate, rolled canvas and rigging use their current animated transforms.
        const auto* Source = Part->SourcePart();
        return !Source || !Source->GetVisibleFlag() || Source->bHiddenInGame || !Part->RemainingCount();
    });
    if (Actor.IsA<AIslandChest>())
    {
        TArray<UIslandMeshVoxels*> CompassParts;
        for (auto* Part : Parts)
            if (Part->SourcePart()->ComponentHasTag(TEXT("IslandPrizeCompass"))) CompassParts.Add(Part);
        for (auto* Part : CompassParts) Parts.RemoveSingle(Part);
        FindUnsupported(*Ground, Parts, Removed);
        if (!CompassParts.IsEmpty())
        {
            // The prize rises independently of the chest. Its animated spindle carries
            // the dial and needles only while a path of occupied cells reaches the hub.
            TMap<UIslandMeshVoxels*,TArray<int32>> CompassLoss;
            FindUnsupported(*Ground, CompassParts, CompassLoss, [](const UIslandMeshVoxels& Part,int32,FVector Center)
            {
                const auto* Source = Part.SourcePart();
                if (Source->GetFName() != TEXT("CompassRim")) return false;
                const FVector Local = Source->GetComponentTransform().InverseTransformPosition(Center);
                return FVector2D(Local).SizeSquared() <= FMath::Square(14.);
            });
            Removed.Append(MoveTemp(CompassLoss));
        }
        return;
    }
    const bool IsJetty = Actor.IsA<AExpeditionDock>();
    const bool IsRescueRaft = Actor.IsA<AIslandRescueRaft>();
    const auto* Neutral = Cast<AIslandNeutralProp>(&Actor);
    if (!IsJetty && !IsRescueRaft && (!Neutral || Neutral->GetKind() != EIslandNeutralKind::Ship))
    {
        FindUnsupported(*Ground, Parts, Removed);
        return;
    }
    TMap<const UIslandMeshVoxels*, double> WaterContact;
    for (const UIslandMeshVoxels* Part : Parts)
    {
        const UPrimitiveComponent* Source = Part->SourcePart();
        const FName Name = Source->GetFName();
        if (IsJetty || IsRescueRaft)
        {
            // The saved berth positions its pontoon underside 13 cm above sea Z.
            // Keep just that original bottom course buoyant when it is over water.
            // A rescue raft uses a 65 cm spawn origin and a 35 cm deep flotation deck.
            WaterContact.Add(Part, FMath::Clamp(Source->Bounds.GetBox().Min.Z, 0., IsRescueRaft ? 47.5 : 15.));
        }
        else if (Name == TEXT("Wreck_PortBow") || Name == TEXT("Wreck_StarboardBow") ||
                 Name == TEXT("Wreck_PortStern") || Name == TEXT("Wreck_StarboardStern") ||
                 Name == TEXT("Wreck_Transom") || Name == TEXT("Wreck_Prow"))
        {
            WaterContact.Add(Part, 0.);
        }
    }
    FindUnsupported(*Ground, Parts, Removed, [&](const UIslandMeshVoxels& Part,int32,FVector Center)
    {
        const double* WaterLine = WaterContact.Find(&Part);
        if (!WaterLine || Center.Z-Part.CellRadius() > *WaterLine+ContactMargin) return false;
        FVector A,B,C;
        Part.GetCellAxes(A,B,C);
        const double Bottom = Center.Z - FMath::Abs(A.Z) - FMath::Abs(B.Z) - FMath::Abs(C.Z);
        if (Bottom > *WaterLine+ContactMargin) return false;
        // A coast column excavated beneath sea level can admit water even where
        // the original two-dimensional shoreline is still inland.
        return Ground->SignedDistanceAt(FVector(Center.X,Center.Y,0.)) >= 0.;
    });
}
