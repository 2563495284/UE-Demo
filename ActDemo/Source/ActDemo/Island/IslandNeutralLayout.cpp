#include "Island/IslandNeutralLayout.h"

#include "Algo/Reverse.h"
#include "Components/StaticMeshComponent.h"
#include "EngineUtils.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Pawn.h"
#include "Island/ExpeditionDock.h"
#include "Island/IslandSceneryVoxels.h"
#include "Island/IslandRules.h"
#include "Voxel/VoxelTerrain.h"

FVector IslandNeutral::SiteScale(EIslandNeutralKind Kind)
{
    switch (Kind)
    {
        case EIslandNeutralKind::Tree:   return FVector(3.);
        case EIslandNeutralKind::Ship:   return {2.25,3.10,1.};
        case EIslandNeutralKind::Hopper: return {2.8,2.8,1.8};
        case EIslandNeutralKind::Sail:   return {3.6,3.6,2.1};
        case EIslandNeutralKind::Tide:   return {2.8,2.8,2.5};
        default: return FVector::OneVector;
    }
}

namespace NeutralLayoutDetail
{
    // A path connects neighbouring floor samples. A blocker is expanded by the pawn
    // radius plus half an edge, so a graph edge can never jump across a narrow post.
    constexpr int32 Step = 200;
    constexpr double PawnRadius = 40.;
    constexpr double EdgeClearance = PawnRadius + Step * .5;
    constexpr EIslandNeutralKind RouteKinds[] = {EIslandNeutralKind::Bridge, EIslandNeutralKind::Dam,
        EIslandNeutralKind::SandDepot, EIslandNeutralKind::WindTower};
    constexpr int32 RouteCount = UE_ARRAY_COUNT(RouteKinds);
    constexpr double StationRadius = 340.;
    constexpr double CorridorRadius = 225.;
    constexpr double ChestClearance = 80.; // covers the 45 x 34 cm half-extents and their diagonal
    const FIntPoint Neighbours[] = {{1,0}, {0,1}, {-1,0}, {0,-1}};
    constexpr int32 Opposite[] = {2,3,0,1};
    constexpr double NoGround = -100000.;

    struct FCell
    {
        FVector Point;
        bool bOpen = false;
        uint8 Links = 0;
    };

    FBox2D LocalEnvelope(EIslandNeutralKind Kind)
    {
        // Cover every solid in both poses, including the crest of the released sand.
        switch (Kind)
        {
            case EIslandNeutralKind::Tree: return {{-100,-165},{650,165}};
            case EIslandNeutralKind::Ship: return {{-195,-413},{195,413}};
            case EIslandNeutralKind::Hopper: return {{-275,-230},{415,230}};
            case EIslandNeutralKind::Sail: return {{-75,-260},{75,260}};
            case EIslandNeutralKind::Tide: return {{-125,-270},{525,270}};
            default: return {{-StationRadius,-StationRadius},{StationRadius,StationRadius}};
        }
    }

    FBox2D WorldEnvelope(const FIslandNeutralPlacement& P)
    {
        const FBox2D Modules = LocalEnvelope(P.Kind);
        const FVector2D Scale(IslandNeutral::SiteScale(P.Kind));
        const FBox2D Local(Modules.Min * Scale, Modules.Max * Scale);
        const FVector2D Axis = FVector2D(FRotator(0,P.Yaw,0).Vector());
        const FVector2D Across(-Axis.Y,Axis.X);
        const FVector2D Center = FVector2D(P.Location) + Axis * Local.GetCenter().X + Across * Local.GetCenter().Y;
        const FVector2D Half = Local.GetExtent();
        const FVector2D Extent(FMath::Abs(Axis.X)*Half.X + FMath::Abs(Across.X)*Half.Y,
            FMath::Abs(Axis.Y)*Half.X + FMath::Abs(Across.Y)*Half.Y);
        return {Center-Extent, Center+Extent};
    }

    FBox2D SegmentBox(const FVector& From, const FVector& To, double Radius)
    {
        FBox2D Bounds(ForceInit);
        Bounds += FVector2D(From);
        Bounds += FVector2D(To);
        return Bounds.ExpandBy(Radius);
    }

    double RouteLength(const TArray<FVector>& Path)
    {
        double Length=0.;
        for (int32 I=1; I<Path.Num(); ++I) Length+=FVector::Dist(Path[I-1],Path[I]);
        return Length;
    }

    struct FCandidate
    {
        TArray<FVector> Path;
        TArray<FBox2D> Corridor;
        FBox2D Envelope = FBox2D(ForceInit);
    };

    template<typename T>
    void Shuffle(TArray<T>& Array, FRandomStream& Stream)
    {
        for (int32 I=Array.Num()-1; I>0; --I) Array.Swap(I,Stream.RandRange(0,I));
    }

    struct FIslandSurface
    {
        const AVoxelTerrain& Terrain;
        const FVoxelDensity& Density;
        const FVector2D Min;
        const int32 Width;
        const int32 Height;
        TArray<FCell> Cells;
        TArray<FBox2D> Reserved;
        TArray<FBox> Scenery;
        TArray<int32> Docks;
        TBitArray<> DockReachable;
        TStaticArray<TArray<FCandidate>,RouteCount> Candidates;
        bool bDockConnections = true;

        FIslandSurface(UWorld& World, const AVoxelTerrain& InTerrain)
            : Terrain(InTerrain), Density(*Terrain.GetDensity()), Min(Density.Grid.Origin.X,Density.Grid.Origin.Y),
              Width(Density.Grid.Cells.X * VoxelPlayground::Spacing / Step + 1),
              Height(Density.Grid.Cells.Y * VoxelPlayground::Spacing / Step + 1)
        {
            const double Begin = FPlatformTime::Seconds();
            TArray<AExpeditionDock*> Landings;
            for (TActorIterator<AActor> It(&World); It; ++It)
            {
                if (*It == &Terrain || It->IsA<APawn>()) continue;
                if (auto* Dock = Cast<AExpeditionDock>(*It)) Landings.Add(Dock);
                TInlineComponentArray<UStaticMeshComponent*> Meshes(*It);
                for (UStaticMeshComponent* Mesh : Meshes)
                {
                    const UPrimitiveComponent* Physical = IslandSceneryVoxels::SurfaceFor(Mesh);
                    if (!Mesh->GetStaticMesh() || !Physical->IsQueryCollisionEnabled() ||
                        Physical->GetCollisionResponseToChannel(ECC_Pawn) != ECR_Block) continue;
                    const FBox Box = Mesh->Bounds.GetBox();
                    Scenery.Add(Box);
                    Reserved.Emplace(FVector2D(Box.Min),FVector2D(Box.Max));
                }
            }
            for (const FBox2D& Box : Terrain.NeutralPropSettings.ExcludedAreas)
                if (Box.bIsValid) Reserved.Add(Box);
            BuildGraph();
            for (const AExpeditionDock* Dock : Landings)
            {
                const FVector Start = Dock->GetActorLocation();
                const FVector End = Start + Dock->GetActorForwardVector()*770.;
                // This includes the physical deck and its short walk onto land. No generated
                // object is allowed in this initial corridor even when a landing was moved.
                FBox2D Corridor(ForceInit);
                Corridor += FVector2D(Start);
                Corridor += FVector2D(End);
                Reserved.Add(Corridor.ExpandBy(170.));
                int32 Anchor = NearestCell(FVector2D(End), 230.);
                if (Anchor == INDEX_NONE || !DockReachesGround(*Dock,Cells[Anchor].Point)) bDockConnections=false;
                else Docks.Add(Anchor);
            }
            bDockConnections &= !Docks.IsEmpty();
            DockReachable.Init(false,Cells.Num());
            if (bDockConnections) FloodFrom(Docks[0],DockReachable);
            FindCandidates();
            UE_LOG(LogTemp,Display,TEXT("NEUTRAL_TERRAIN cells=%d docks=%d connected=%d corridors=%d,%d,%d,%d ms=%.1f"),
                Cells.Num(),Docks.Num(),bDockConnections,Candidates[0].Num(),Candidates[1].Num(),
                Candidates[2].Num(),Candidates[3].Num(),(FPlatformTime::Seconds()-Begin)*1000.);
        }

        bool DockReachesGround(const AExpeditionDock& Dock, const FVector& To) const
        {
            const FVector Start = Dock.GetActorLocation();
            const auto& Deck = Dock.GetMesh();
            if (!Deck.GetStaticMesh()) return false;
            const FBox Local = Deck.GetStaticMesh()->GetBoundingBox();
            const double Top = Deck.GetComponentTransform().TransformPosition({0.,0.,Local.Max.Z}).Z;
            TOptional<double> Previous;
            const int32 Samples = FMath::Max(1,FMath::CeilToInt(FVector::Dist2D(Start,To)/35.));
            for (int32 I=0; I<=Samples; ++I)
            {
                const FVector P = FMath::Lerp(Start,To,double(I)/Samples);
                const FVector DeckPoint = Deck.GetComponentTransform().InverseTransformPosition(P);
                double Floor = Density.SurfaceAt(FVector2D(P)).Get(NoGround);
                if (DeckPoint.X >= Local.Min.X && DeckPoint.X <= Local.Max.X &&
                    DeckPoint.Y >= Local.Min.Y && DeckPoint.Y <= Local.Max.Y) Floor = FMath::Max(Floor,Top);
                if (Floor < -25. || (Previous.IsSet() && FMath::Abs(Floor-*Previous)>48.)) return false;
                Previous = Floor;
            }
            return true;
        }

        void BuildGraph()
        {
            Cells.SetNum(Width*Height);
            for (int32 Y=0; Y<Height; ++Y) for (int32 X=0; X<Width; ++X)
            {
                const FVector2D P = Min + FVector2D(X*Step,Y*Step);
                FCell& Cell = Cells[X+Y*Width];
                Cell.Point = FVector(P,NoGround);
                if (Density.ShoreDistance(P.X,P.Y)>-int32(PawnRadius)) continue;
                Cell.Point.Z = Density.SurfaceAt(P).Get(NoGround);
                if (Cell.Point.Z < -25.) continue;
                Cell.bOpen = true;
                for (const FBox& Box : Scenery)
                {
                    const FBox2D Footprint(FVector2D(Box.Min),FVector2D(Box.Max));
                    if (Box.Max.Z > Cell.Point.Z+5. && Box.Min.Z < Cell.Point.Z+180. &&
                        Footprint.ExpandBy(EdgeClearance).IsInsideOrOn(P))
                    {
                        Cell.bOpen = false;
                        break;
                    }
                }
            }
            for (int32 Y=0; Y<Height-1; ++Y) for (int32 X=0; X<Width-1; ++X)
            {
                FCell& A=Cells[X+Y*Width];
                if (!A.bOpen) continue;
                for (int32 D=0; D<2; ++D)
                {
                    FCell& B = Cells[X+Neighbours[D].X+(Y+Neighbours[D].Y)*Width];
                    if (!B.bOpen || FMath::Abs(A.Point.Z-B.Point.Z)>Step*.85) continue;
                    if (!FloorSupportsEdge(A.Point,B.Point)) continue;
                    A.Links |= 1<<D;
                    B.Links |= 1<<Opposite[D];
                }
            }
        }

        bool FloorSupportsEdge(const FVector& A, const FVector& B) const
        {
            const FVector2D Axis = (FVector2D(B)-FVector2D(A))/Step;
            const FVector2D Side(-Axis.Y,Axis.X);
            for (int32 Lane=-1; Lane<=1; ++Lane)
            {
                const FVector2D Offset=Side*(Lane*PawnRadius);
                double Last = Lane==0 ? A.Z : Density.SurfaceAt(FVector2D(A)+Offset).Get(NoGround);
                for (int32 K=1; K<=4; ++K)
                {
                    const FVector2D XY=FVector2D(A)+Offset+Axis*(Step*K/4.);
                    const double H=Lane==0 && K==4 ? B.Z : Density.SurfaceAt(XY).Get(NoGround);
                    if (H < -25. || FMath::Abs(H-Last)>48.) return false;
                    Last=H;
                }
            }
            return true;
        }

        int32 NearestCell(FVector2D P, double Range, bool bFromDock = false) const
        {
            const int32 CX=FMath::RoundToInt((P.X-Min.X)/Step), CY=FMath::RoundToInt((P.Y-Min.Y)/Step);
            const int32 Radius=FMath::CeilToInt(Range/Step);
            int32 Best=INDEX_NONE;
            double BestDS=Range*Range;
            for (int32 Y=FMath::Max(0,CY-Radius); Y<=FMath::Min(Height-1,CY+Radius); ++Y)
                for (int32 X=FMath::Max(0,CX-Radius); X<=FMath::Min(Width-1,CX+Radius); ++X)
                {
                    const int32 Id=X+Y*Width;
                    const double DS=FVector2D::DistSquared(P,FVector2D(Cells[Id].Point));
                    if (Cells[Id].Links && (!bFromDock || DockReachable[Id]) && DS<BestDS) {BestDS=DS; Best=Id;}
                }
            return Best;
        }

        double HeightAt(FVector2D P) const
        {
            const FVector2D Grid=(P-Min)/Step;
            const int32 X=FMath::FloorToInt(Grid.X),Y=FMath::FloorToInt(Grid.Y);
            if (X<0 || X+1>=Width || Y<0 || Y+1>=Height) return NoGround;
            const int32 Id=X+Y*Width;
            const double A=Cells[Id].Point.Z, B=Cells[Id+1].Point.Z;
            const double C=Cells[Id+Width].Point.Z, D=Cells[Id+Width+1].Point.Z;
            if (FMath::Min(FMath::Min(A,B),FMath::Min(C,D))<-25.) return NoGround;
            return FMath::Lerp(FMath::Lerp(A,B,Grid.X-X),FMath::Lerp(C,D,Grid.X-X),Grid.Y-Y);
        }

        bool IsReserved(const FBox2D& Box) const
        {
            return Reserved.ContainsByPredicate([&Box](const FBox2D& Other){ return Other.Intersect(Box); });
        }

        TArray<FVector2D> Gateways(const FIslandSearchCircle& Circle, FVector2D Axis) const
        {
            const FVector2D Center(Circle.Center), Side(-Axis.Y,Axis.X);
            TArray<FVector2D> Points;
            for (double Along : {-.72,-.36,0.,.36,.72})
            {
                for (double Across : {-.88,-.66,-.44,-.22,0.,.22,.44,.66,.88})
                {
                    const FVector2D Offset=Axis*Along+Side*Across;
                    if (Offset.SizeSquared()>1.) continue;
                    // The player can stand on either endpoint entirely inside its search circle.
                    const FVector2D P=Center+Offset*(Circle.Radius-PawnRadius);
                    if (NearestCell(P,175.,true)!=INDEX_NONE) Points.Add(P);
                }
            }
            return Points;
        }

        void FindCandidates()
        {
            const auto& Circles=Terrain.GetSearchCircles();
            if (Circles.Num()!=RouteCount || !bDockConnections) return;
            for (int32 Edge=0; Edge<RouteCount; ++Edge)
            {
                const auto& A=Circles[Edge];
                const auto& B=Circles[(Edge+1)%RouteCount];
                const FVector2D Axis=(FVector2D(B.Center)-FVector2D(A.Center)).GetSafeNormal();
                const auto Starts=Gateways(A,Axis);
                const auto Finishes=Gateways(B,Axis);
                for (const FVector2D Start : Starts)
                    for (const FVector2D Finish : Finishes)
                    {
                        const double Span=FVector2D::Distance(Start,Finish);
                        if (Span<IslandNeutral::MinRouteSpan || Span>IslandNeutral::MaxRouteLength) continue;
                        const FVector2D Forward=(Finish-Start)/Span, Side(-Forward.Y,Forward.X);
                        for (double Bend : {0.,-550.,550.,-950.,950.})
                        {
                            FCandidate Candidate;
                            Candidate.Path.Reserve(IslandNeutral::RouteSegments+1);
                            Candidate.Corridor.Reserve(IslandNeutral::RouteSegments);
                            for (int32 I=0; I<=IslandNeutral::RouteSegments; ++I)
                            {
                                const double Alpha=double(I)/IslandNeutral::RouteSegments;
                                // A shallow arc can run around a landmark or the entrance to a dig site.
                                const FVector2D XY=FMath::Lerp(Start,Finish,Alpha)+Side*(Bend*4.*Alpha*(1.-Alpha));
                                const auto Z=Density.SurfaceAt(XY);
                                if (!Z || *Z<0. || Density.ShoreDistance(XY.X,XY.Y)>-360) break;
                                const FVector Ground(XY,*Z);
                                if (I)
                                {
                                    const FVector Previous=Candidate.Path.Last();
                                    if (FMath::Abs(Ground.Z-Previous.Z)>.58*FVector::Dist2D(Previous,Ground)) break;
                                    const FBox2D Strip=SegmentBox(Previous,Ground,CorridorRadius);
                                    if (IsReserved(Strip)) break;
                                    Candidate.Corridor.Add(Strip);
                                    Candidate.Envelope+=Strip;
                                }
                                Candidate.Path.Add(Ground);
                            }
                            if (Candidate.Path.Num()==IslandNeutral::RouteSegments+1 && RouteLength(Candidate.Path)<=IslandNeutral::MaxRouteLength)
                                Candidates[Edge].Add(MoveTemp(Candidate));
                        }
                    }
            }
        }

        bool RollWreckPreview(int32 Seed, const FIslandTreasureLayout& Treasure, TArray<FIslandNeutralPlacement>& Out) const
        {
            // The dedicated ship art inspection starts at the original port-side view on the default island.
            const auto TryShip=[&](FVector2D XY, int16 Yaw)
            {
                FIslandNeutralPlacement Ship;
                Ship.Kind=EIslandNeutralKind::Ship;
                Ship.Location=FVector(XY,HeightAt(XY));
                Ship.Yaw=Yaw;
                const FBox2D Box=WorldEnvelope(Ship);
                if (Ship.Location.Z<=0. || IsReserved(Box.ExpandBy(45.))) return false;
                for (int32 I=0; I<Treasure.Num(); ++I)
                    if (Box.ExpandBy(250.).IsInsideOrOn(FVector2D(Treasure.GetCenter(I)))) return false;
                const TArray<FBox2D> Bounds{Box};
                if (!RoutesOpen(Bounds,Endpoints(Treasure))) return false;
                Out.Add(MoveTemp(Ship));
                return true;
            };
            if (TryShip({-2800.,6400.},270)) return true;
            FRandomStream Stream(Seed);
            const int32 First=Stream.RandRange(0,Cells.Num()-1);
            for (int32 I=0; I<Cells.Num(); ++I)
            {
                const auto& Cell=Cells[(I+First)%Cells.Num()];
                if (!Cell.Links) continue;
                for (int16 Yaw : {0,90,180,270})
                    if (TryShip(FVector2D(Cell.Point),Yaw)) return true;
            }
            return false;
        }

        TArray<int32> Endpoints(const FIslandTreasureLayout& Treasure) const
        {
            TArray<int32> Result=Docks;
            for (int32 I=0; I<Treasure.Num(); ++I)
            {
                // Reach the surface immediately over each actual box; players dig from here.
                Result.AddUnique(NearestCell(FVector2D(Treasure.GetCenter(I)),210.,true));
            }
            return Result;
        }

        void FloodFrom(int32 Start,TBitArray<>& Visited) const
        {
            TArray<int32> Queue;
            Queue.Reserve(Cells.Num());
            Queue.Add(Start);
            Visited[Start]=true;
            const int32 Offset[]={1,Width,-1,-Width};
            for (int32 Head=0; Head<Queue.Num(); ++Head)
            {
                const int32 Id=Queue[Head];
                for (int32 D=0; D<4; ++D)
                {
                    if (!(Cells[Id].Links & (1<<D))) continue;
                    const int32 Next=Id+Offset[D];
                    if (!Visited[Next]) {Visited[Next]=true; Queue.Add(Next);}
                }
            }
        }

        bool RoutesOpen(TConstArrayView<FBox2D> Taken,TConstArrayView<int32> Destinations) const
        {
            if (!bDockConnections || Destinations.IsEmpty() || Destinations.Contains(INDEX_NONE)) return false;
            TBitArray<> Blocked(false,Cells.Num());
            for (const FBox2D& Footprint : Taken)
            {
                const FBox2D R=Footprint.ExpandBy(EdgeClearance);
                const FIntPoint A(FMath::Max(0,FMath::CeilToInt((R.Min.X-Min.X)/Step)),
                    FMath::Max(0,FMath::CeilToInt((R.Min.Y-Min.Y)/Step)));
                const FIntPoint B(FMath::Min(Width-1,FMath::FloorToInt((R.Max.X-Min.X)/Step)),
                    FMath::Min(Height-1,FMath::FloorToInt((R.Max.Y-Min.Y)/Step)));
                for (int32 Y=A.Y; Y<=B.Y; ++Y) for (int32 X=A.X; X<=B.X; ++X) Blocked[X+Y*Width]=true;
            }
            for (int32 Id : Destinations) if (Blocked[Id]) return false;
            FloodFrom(Destinations[0],Blocked);
            for (int32 Id : Destinations) if (!Blocked[Id]) return false;
            return true;
        }

        bool HasWideDigApproach(const FCandidate& Candidate, const FIslandTreasureLayout& Treasure) const
        {
            for (int32 I=0; I<Treasure.Num(); ++I)
            {
                const FVector2D XY(Treasure.GetCenter(I));
                if (!Candidate.Envelope.ExpandBy(225.).IsInsideOrOn(XY)) continue;
                if (Candidate.Corridor.ContainsByPredicate([&](const FBox2D& Strip) {
                    return Strip.ExpandBy(225.).IsInsideOrOn(XY); })) return false;
            }
            return true;
        }

        double TreasureClearance(const FCandidate& Candidate, const FIslandTreasureLayout& Treasure) const
        {
            // The public route has no excavation effect. Reserve buried boxes against actual solids,
            // measuring to the polyline rather than the empty corners of its diagonal bounds.
            constexpr double Limit=CorridorRadius+100.;
            double NearestSq=Limit*Limit;
            const FBox2D Bounds=Candidate.Envelope.ExpandBy(Limit);
            for (int32 I=0; I<Treasure.Num(); ++I)
            {
                const FVector2D XY(Treasure.GetCenter(I));
                if (!Bounds.IsInsideOrOn(XY)) continue;
                for (int32 J=1; J<Candidate.Path.Num(); ++J)
                {
                    const FVector2D A(Candidate.Path[J-1]), D=FVector2D(Candidate.Path[J])-A;
                    const double Alpha=FMath::Clamp(FVector2D::DotProduct(XY-A,D)/D.SizeSquared(),0.,1.);
                    NearestSq=FMath::Min(NearestSq,FVector2D::DistSquared(XY,A+D*Alpha));
                }
            }
            return FMath::Sqrt(NearestSq);
        }

        bool CanUseRoute(const FIslandNeutralPlacement& P, const FIslandTreasureLayout& Treasure,
            const TArray<FIslandNeutralPlacement>& Others, TConstArrayView<FBox2D> Occupied,bool bUseExactClearance) const
        {
            const FBox2D Station=SegmentBox(P.Location,P.Location,StationRadius);
            const auto IntersectsOther=[&](const FBox2D& Box)
            {
                return Occupied.ContainsByPredicate([&](const FBox2D& Other){return Box.Intersect(Other);});
            };
            if (IsReserved(Station) || IntersectsOther(Station)) return false;
            for (int32 I=0; I<Treasure.Num(); ++I)
                if (Station.ExpandBy(bUseExactClearance ? ChestClearance : 250.).IsInsideOrOn(FVector2D(Treasure.GetCenter(I)))) return false;
            for (const auto& Other : Others)
                if (FVector::DistSquared2D(Other.Location,P.Location)<FMath::Square(double(Terrain.NeutralPropSettings.MinimumSpacing))) return false;

            if (P.Kind==EIslandNeutralKind::WindTower)
            {
                const FVector2D Last(P.RouteNodes.Last());
                const FVector2D Forward=(Last-FVector2D(P.RouteNodes.Last(1))).GetSafeNormal();
                const FVector2D Exit=Last+Forward*200.;
                const auto HeightThere=Density.SurfaceAt(Exit);
                const FBox2D Landing=SegmentBox(FVector(Last,0.),FVector(Exit,0.),125.);
                if (!HeightThere || FMath::Abs(*HeightThere-P.RouteNodes.Last().Z)>90. ||
                    NearestCell(Exit,150.,true)==INDEX_NONE || IsReserved(Landing) || IntersectsOther(Landing)) return false;
                for (int32 I=0; I<Treasure.Num(); ++I)
                    if (Landing.ExpandBy(bUseExactClearance ? ChestClearance : 225.).IsInsideOrOn(FVector2D(Treasure.GetCenter(I)))) return false;
            }
            return true;
        }

        bool Roll(int32 Seed,const FIslandTreasureLayout& Treasure,TArray<FIslandNeutralPlacement>& Out,bool bUseExactClearance=false) const
        {
            Out.Reset();
            const auto Destinations=Endpoints(Treasure);
            TArray<FBox2D> Solids;
            if (!RoutesOpen(Solids,Destinations)) return false;
            const auto& Settings=Terrain.NeutralPropSettings;
            const bool Enabled[]={Settings.bBridge,Settings.bDam,Settings.bSandDepot,Settings.bWindTower};
            TArray<EIslandNeutralKind> Kinds;
            for (int32 I=0; I<RouteCount; ++I) if (Enabled[I]) Kinds.Add(RouteKinds[I]);
            FRandomStream Stream(static_cast<int32>(uint32(Seed)^0x9A7FC5B3u));
            Shuffle(Kinds,Stream);

            TStaticArray<TArray<int32>,RouteCount> Pools;
            TStaticArray<TArray<double>,RouteCount> Clearances;
            TArray<int32> Edges;
            for (int32 Edge=0; Edge<RouteCount; ++Edge)
            {
                for (int32 Index=0; Index<Candidates[Edge].Num(); ++Index)
                {
                    if (bUseExactClearance) Clearances[Edge].Add(TreasureClearance(Candidates[Edge][Index],Treasure));
                    if (bUseExactClearance || HasWideDigApproach(Candidates[Edge][Index],Treasure)) Pools[Edge].Add(Index);
                }
                Shuffle(Pools[Edge],Stream);
                if (!Pools[Edge].IsEmpty()) Edges.Add(Edge);
            }
            Shuffle(Edges,Stream);
            // Place the constrained search-area pairs first. Keep the first tie order from this seed.
            Edges.StableSort([&](int32 A,int32 B){return Pools[A].Num()<Pools[B].Num();});

            TArray<FBox2D> Occupied;
            TArray<FIslandNeutralPlacement> Selected;
            const bool bClockwise=Stream.RandRange(0,1)!=0;
            int32 Checks=0;
            const auto Place=[&](auto&& Self,int32 Slot)->bool
            {
                if (Selected.Num()>Out.Num()) Out=Selected;
                if (Slot>=Kinds.Num() || Slot>=Edges.Num()) return true;
                const int32 Edge=Edges[Slot];
                for (const auto Kind : Kinds)
                {
                    if (Selected.ContainsByPredicate([Kind](const auto& Other){return Other.Kind==Kind;})) continue;
                    for (const int32 Index : Pools[Edge])
                    {
                        // Flow and wind do not edit the terrain below them. The station and wind
                        // landings are still checked against the actual buried boxes and the walking graph.
                        const double Required=Kind==EIslandNeutralKind::SandDepot ? 215.+ChestClearance
                            : Kind==EIslandNeutralKind::Bridge ? 110.+ChestClearance : 0.;
                        if (bUseExactClearance && Clearances[Edge][Index]<Required) continue;
                        const auto& Candidate=Candidates[Edge][Index];
                        bool bIntersects=false;
                        for (const FBox2D& Used : Occupied)
                        {
                            if (Candidate.Envelope.Intersect(Used) &&
                                Candidate.Corridor.ContainsByPredicate([&](const FBox2D& Strip){return Strip.Intersect(Used);}))
                            { bIntersects=true; break; }
                        }
                        if (bIntersects) continue;
                        FIslandNeutralPlacement P;
                        P.Kind=Kind;
                        P.RouteNodes=Candidate.Path;
                        const bool bReverse = Kind==EIslandNeutralKind::Dam ? P.RouteNodes[0].Z<P.RouteNodes.Last().Z
                            : Kind==EIslandNeutralKind::SandDepot ? P.RouteNodes[0].Z>P.RouteNodes.Last().Z : bClockwise;
                        if (bReverse) Algo::Reverse(P.RouteNodes);
                        const int32 Directions=(Kind==EIslandNeutralKind::WindTower || Kind==EIslandNeutralKind::Bridge) ? 2 : 1;
                        for (int32 Heading=0; Heading<Directions; ++Heading)
                        {
                            if (Heading) Algo::Reverse(P.RouteNodes);
                            P.Location=P.RouteNodes[0];
                            P.Yaw=FMath::RoundToInt((P.RouteNodes[1]-P.Location).Rotation().Yaw);
                            if (!CanUseRoute(P,Treasure,Selected,Occupied,bUseExactClearance)) continue;
                            const int32 PrevSolids=Solids.Num(), PrevOccupied=Occupied.Num();
                            IslandNeutral::AppendFootprints(P,Solids);
                            // The original dock-to-treasure route must survive every solid effect together.
                            if (RoutesOpen(Solids,Destinations))
                            {
                                IslandNeutral::AppendFootprints(P,Occupied,true);
                                Selected.Add(P);
                                if (Self(Self,Slot+1)) return true;
                                Selected.Pop();
                                Occupied.SetNum(PrevOccupied);
                            }
                            Solids.SetNum(PrevSolids);
                            if (++Checks>=2048) return false;
                        }
                    }
                }
                return false;
            };
            Place(Place,0);
            if (!bUseExactClearance && Out.Num()!=Kinds.Num())
            {
                // Prefer broad excavation space. Crowded rounds can run a non-solid wind or water
                // corridor past a box as long as its station, landings and the normal dig route are clear.
                TArray<FIslandNeutralPlacement> Exact;
                Roll(Seed,Treasure,Exact,true);
                if (Exact.Num()>Out.Num()) Out=MoveTemp(Exact);
                return true;
            }
            if (Out.Num()!=Kinds.Num())
                UE_LOG(LogTemp,Warning,TEXT("NEUTRAL_ROUTE_INCOMPLETE seed=%d installed=%d requested=%d pools=%d,%d,%d,%d checks=%d"),
                    Seed,Out.Num(),Kinds.Num(),Pools[0].Num(),Pools[1].Num(),Pools[2].Num(),Pools[3].Num(),Checks);
            return true;
        }
    };
}

IslandNeutral::FSurface IslandNeutral::Survey(UWorld& World,const AVoxelTerrain& Terrain)
{
    if (Terrain.Layout!=EVoxelLayout::Expedition || !Terrain.GetDensity()) return {};
    return MakeShared<NeutralLayoutDetail::FIslandSurface>(World,Terrain);
}

bool IslandNeutral::CanReachDigSite(const FSurface& Surface,FVector2D Point)
{
    return Surface && Surface->NearestCell(Point,210.,true)!=INDEX_NONE;
}

void IslandNeutral::AppendFootprints(const FIslandNeutralPlacement& P,TArray<FBox2D>& Out,bool bEntireCorridor)
{
    using namespace NeutralLayoutDetail;
    if (P.RouteNodes.IsEmpty()) { Out.Add(WorldEnvelope(P)); return; }
    Out.Add(SegmentBox(P.Location,P.Location,StationRadius));
    if (bEntireCorridor || P.Kind==EIslandNeutralKind::Bridge || P.Kind==EIslandNeutralKind::SandDepot)
    {
        const double HalfWidth = bEntireCorridor ? CorridorRadius : P.Kind==EIslandNeutralKind::Bridge ? 110. : 215.;
        for (int32 I=1; I<P.RouteNodes.Num(); ++I)
            Out.Add(SegmentBox(P.RouteNodes[I-1],P.RouteNodes[I],HalfWidth));
        if (P.Kind==EIslandNeutralKind::WindTower && P.RouteNodes.Num()>=2)
        {
            const FVector Last=P.RouteNodes.Last();
            const FVector Exit=Last+(Last-P.RouteNodes.Last(1)).GetSafeNormal2D()*200.;
            Out.Add(SegmentBox(Last,Exit,125.));
        }
    }
    else if (P.Kind==EIslandNeutralKind::WindTower)
    {
        for (const int32 I : {P.RouteNodes.Num()/2,P.RouteNodes.Num()-1})
            Out.Add(SegmentBox(P.RouteNodes[I],P.RouteNodes[I],125.));
    }
}

bool IslandNeutral::KeepsDigRoutes(const FSurface& Surface,const FIslandTreasureLayout& Treasure,
    TConstArrayView<FBox2D> Footprints)
{
    return Surface && Surface->RoutesOpen(Footprints,Surface->Endpoints(Treasure));
}

void IslandNeutral::Generate(const FSurface& Surface,int32 RoundSeed,const FIslandTreasureLayout& Treasure,TArray<FIslandNeutralPlacement>& Out)
{
    Out.Reset();
    if (!Surface) return;
#if !UE_BUILD_SHIPPING
    if (FParse::Param(FCommandLine::Get(),TEXT("WreckPreview")) || FParse::Param(FCommandLine::Get(),TEXT("WreckStudyPhotos")))
    {
        Surface->RollWreckPreview(RoundSeed,Treasure,Out);
        return;
    }
#endif
    const bool bReachable=Surface->Roll(RoundSeed,Treasure,Out);
    UE_LOG(LogTemp,Display,TEXT("NEUTRAL_LAYOUT count=%d routes=%d"),Out.Num(),bReachable);
}

#if !UE_BUILD_SHIPPING
bool IslandNeutral::Audit(const FSurface& Surveyed,int32 DecoyCount,TConstArrayView<FBox2D> Obstacles)
{
    using namespace NeutralLayoutDetail;
    if (!Surveyed) return false;
    const FIslandSurface& Surface=*Surveyed;
    const AVoxelTerrain& Terrain=Surface.Terrain;
    const auto CanReach=[&Surveyed](FVector2D P){return CanReachDigSite(Surveyed,P);};
    int32 Counts[RouteCount]={};
    int32 FullSeed=INDEX_NONE;
    TSet<uint32> Unique;
    bool bPassed=true;
    for (int32 Seed=1; Seed<=64; ++Seed)
    {
        FIslandTreasureLayout Treasure;
        const bool bValid=IslandRules::GenerateLayout(Seed,DecoyCount,Terrain.GetSearchCircles(),*Terrain.GetDensity(),Treasure,nullptr,false,Obstacles,CanReach);
        TArray<FIslandNeutralPlacement> A,B;
        bool bRoutes=bValid && Surface.Roll(Seed,Treasure,A);
        const bool bReplay=bRoutes && Surface.Roll(Seed,Treasure,B) && A==B;
        uint32 Hash=2166136261u, Kinds=0, Regions=0;
        double MinSpan=MAX_dbl, MaxLength=0.;
        FString Items;
        TArray<FBox2D> Footprints;
        const auto& Circles=Terrain.GetSearchCircles();
        const auto RegionOf=[&](const FVector& P)
        {
            return Circles.IndexOfByPredicate([&](const FIslandSearchCircle& C){
                return FVector2D::DistSquared(FVector2D(P),FVector2D(C.Center))<=FMath::Square(double(C.Radius)); });
        };
        for (const auto& P : A)
        {
            const int32 K=int32(P.Kind)-int32(EIslandNeutralKind::Bridge);
            if (K<0 || K>=RouteCount || P.RouteNodes.Num()!=RouteSegments+1) { bRoutes=false; continue; }
            ++Counts[K];
            Kinds|=1u<<K;
            const int32 From=RegionOf(P.RouteNodes[0]), To=RegionOf(P.RouteNodes.Last());
            const double Span=FVector::Dist2D(P.RouteNodes[0],P.RouteNodes.Last()), Length=RouteLength(P.RouteNodes);
            bRoutes &= From!=INDEX_NONE && To!=INDEX_NONE && (FMath::Abs(From-To)==1 || FMath::Abs(From-To)==3)
                && Span>=MinRouteSpan && Length<=MaxRouteLength;
            if (From>=0 && To>=0) Regions|=(1u<<From)|(1u<<To);
            MinSpan=FMath::Min(MinSpan,Span);
            MaxLength=FMath::Max(MaxLength,Length);
            Hash=(Hash^uint32(K))*16777619u;
            for (const auto& Node : P.RouteNodes)
                for (const double Coord : {Node.X,Node.Y,Node.Z}) Hash=(Hash^uint32(FMath::RoundToInt(Coord)))*16777619u;
            Items+=FString::Printf(TEXT("%s%d@%c-%c/%d"),Items.IsEmpty()?TEXT(""):TEXT("|"),K,'A'+From,'A'+To,FMath::RoundToInt(Length));
            AppendFootprints(P,Footprints);
        }
        bRoutes &= bValid && Surface.RoutesOpen(Footprints,Surface.Endpoints(Treasure));
        if (FullSeed==INDEX_NONE && Kinds==15) FullSeed=Seed;
        bPassed &= bReplay && bRoutes && A.Num()==RouteCount && Kinds==15 && Regions==15;
        Unique.Add(Hash);
        UE_LOG(LogTemp,Display,TEXT("NEUTRAL_AUDIT_SEED seed=%d count=%d kinds=%u regions=%u hash=%08x replay=%d routes=%d items=%s min_span_cm=%d max_length_cm=%d"),
            Seed,A.Num(),Kinds,Regions,Hash,bReplay,bRoutes,*Items,A.IsEmpty()?0:FMath::RoundToInt(MinSpan),FMath::RoundToInt(MaxLength));
    }
    bPassed &= FullSeed!=INDEX_NONE && Unique.Num()==64;
    UE_LOG(LogTemp,Display,TEXT("NEUTRAL_AUDIT_DONE seeds=64 passed=%d full_seed=%d bridge=%d dam=%d sand=%d wind=%d"),
        bPassed,FullSeed,Counts[0],Counts[1],Counts[2],Counts[3]);
    return bPassed;
}
#endif
