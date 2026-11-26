#include "Island/IslandNeutralLayout.h"

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
    }
    return FVector::OneVector;
}

namespace NeutralLayoutDetail
{
    // A path connects neighbouring floor samples. A blocker is expanded by the pawn
    // radius plus half an edge, so a graph edge can never jump across a narrow post.
    constexpr int32 Step = 200;
    constexpr double PawnRadius = 40.;
    constexpr double EdgeClearance = PawnRadius + Step * .5;
    constexpr int32 KindCount = 5;
    const FIntPoint Neighbours[] = {{1,0}, {0,1}, {-1,0}, {0,-1}};
    constexpr int32 Opposite[] = {2,3,0,1};
    constexpr double NoGround = -100000.;

    struct FCandidate
    {
        FIslandNeutralPlacement Placement;
        FBox2D Envelope;
        int32 Region = 0;
    };

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
        }
        return FBox2D(ForceInit);
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
        TStaticArray<TArray<FCandidate>,KindCount> Candidates;
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
            UE_LOG(LogTemp,Display,TEXT("NEUTRAL_TERRAIN cells=%d docks=%d connected=%d candidates=%d,%d,%d,%d,%d ms=%.1f"),
                Cells.Num(),Docks.Num(),bDockConnections,Candidates[0].Num(),Candidates[1].Num(),Candidates[2].Num(),
                Candidates[3].Num(),Candidates[4].Num(),(FPlatformTime::Seconds()-Begin)*1000.);
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

        bool ForkAt(FVector2D P, FVector2D Axis, FVector2D Side, double Z) const
        {
            // Two diverging exits around a higher patch and a lower incoming branch.
            const double ExitWidth=215.*IslandNeutral::SiteScale(EIslandNeutralKind::Sail).Y;
            const double Left=HeightAt(P+Axis*720.+Side*ExitWidth);
            const double Right=HeightAt(P+Axis*720.-Side*ExitWidth);
            const double Divider=HeightAt(P+Axis*720.);
            const double Approach=HeightAt(P-Axis*720.);
            return FMath::Min3(Left,Right,Approach)>-25. && FMath::Abs(Left-Z)<175. &&
                FMath::Abs(Right-Z)<175. && FMath::Abs(Approach-Z)<175. &&
                Divider > FMath::Max(Left,Right)+24.;
        }

        void FindCandidates()
        {
            for (int32 Y=2; Y<Height-2; Y+=2) for (int32 X=2; X<Width-2; X+=2)
            {
                const FCell& Cell=Cells[X+Y*Width];
                if (!Cell.Links) continue;
                const FVector2D P(Cell.Point);
                const int32 Shore=Density.ShoreDistance(P.X,P.Y);
                if (Shore>-330) continue;
                int32 Region=0;
                double Best=TNumericLimits<double>::Max();
                for (int32 I=0; I<Terrain.GetSearchCircles().Num(); ++I)
                {
                    const auto& Circle=Terrain.GetSearchCircles()[I];
                    const double DS=FVector2D::DistSquared(P,FVector2D(Circle.Center));
                    if (DS<Best) {Best=DS; Region=I;}
                }
                for (int32 Dir=0; Dir<8; ++Dir)
                {
                    const int16 Yaw=Dir*45;
                    const FVector2D Axis(FRotator(0,Yaw,0).Vector()), Side(-Axis.Y,Axis.X);
                    const double Z=Cell.Point.Z;
                    const double Fwd=HeightAt(P+Axis*400.), Aft=HeightAt(P-Axis*400.);
                    const double Left=HeightAt(P+Side*300.), Right=HeightAt(P-Side*300.);
                    if (FMath::Min(FMath::Min(Fwd,Aft),FMath::Min(Left,Right))<-25.) continue;
                    const auto Add=[&](EIslandNeutralKind Kind)
                    {
                        FIslandNeutralPlacement Prop;
                        Prop.Kind=Kind;
                        Prop.Location=Cell.Point;
                        Prop.Yaw=Yaw;
                        const FBox2D Envelope=WorldEnvelope(Prop);
                        for (const FBox2D& Box : Reserved) if (Envelope.ExpandBy(45.).Intersect(Box)) return;
                        // Natural entrances and their substrate must remain on walkable land.
                        for (int32 Corner=0; Corner<4; ++Corner)
                        {
                            const FVector2D XY(Corner&1 ? Envelope.Max.X : Envelope.Min.X,Corner&2 ? Envelope.Max.Y : Envelope.Min.Y);
                            if (Density.ShoreDistance(XY.X,XY.Y)>-50) return;
                        }
                        Candidates[int32(Kind)].Add({Prop,Envelope,Region});
                    };
                    const double TreeEnd=HeightAt(P+Axis*(485.*IslandNeutral::SiteScale(EIslandNeutralKind::Tree).X));
                    const double UnderTree=HeightAt(P+Axis*(245.*IslandNeutral::SiteScale(EIslandNeutralKind::Tree).X));
                    if (FMath::Abs(TreeEnd-Z)<78. && UnderTree<FMath::Min(Z,TreeEnd)-2. && UnderTree>Z-56. &&
                        FMath::Abs(Left-Right)<125. && FMath::Max(Fwd,Aft)-FMath::Min(Fwd,Aft)>10.)
                        Add(EIslandNeutralKind::Tree);
                    const double Port=HeightAt(P+Axis*(370.*IslandNeutral::SiteScale(EIslandNeutralKind::Ship).X)), Starboard=HeightAt(P-Axis*(370.*IslandNeutral::SiteScale(EIslandNeutralKind::Ship).X));
                    // The open hull only needs its crossing and both approach banks on dry land.
                    // Bow and stern may sit on different contours along the ridge.
                    if (Shore>-6400 && FMath::Max(Port,Starboard)<Z+28. && FMath::Min(Port,Starboard)>Z-185. &&
                        FMath::Min(Port,Starboard)<Z-12. &&
                        FMath::Abs(HeightAt(P+Side*(320.*IslandNeutral::SiteScale(EIslandNeutralKind::Ship).Y))-Z)<200. &&
                        FMath::Abs(HeightAt(P-Side*(320.*IslandNeutral::SiteScale(EIslandNeutralKind::Ship).Y))-Z)<200.)
                        Add(EIslandNeutralKind::Ship);
                    const double Landing=HeightAt(P+Axis*(380.*IslandNeutral::SiteScale(EIslandNeutralKind::Hopper).X));
                    const double PinGround=HeightAt(P-Axis*(95.*IslandNeutral::SiteScale(EIslandNeutralKind::Hopper).X)-Side*(157.*IslandNeutral::SiteScale(EIslandNeutralKind::Hopper).Y));
                    if (Landing>Z+86. && Landing<Z+218. && PinGround>Z-78. && PinGround<Z+75. &&
                        FMath::Abs(Left-Right)<150.) Add(EIslandNeutralKind::Hopper);
                    if (FMath::Abs(HeightAt(P+Side*(217.*IslandNeutral::SiteScale(EIslandNeutralKind::Sail).Y))-Z)<98. && FMath::Abs(HeightAt(P-Side*(217.*IslandNeutral::SiteScale(EIslandNeutralKind::Sail).Y))-Z)<98. &&
                        ForkAt(P,Axis,Side,Z)) Add(EIslandNeutralKind::Sail);
                    if (Shore>-2250 && FMath::Abs(Left-Z)<100. && FMath::Abs(Right-Z)<100.)
                    {
                        bool bBothLandings=true;
                        for (double Sign : {-1.,1.})
                        {
                            const FVector2D Mouth=P+Side*(150.*Sign*IslandNeutral::SiteScale(EIslandNeutralKind::Tide).Y);
                            const double Rise=HeightAt(Mouth+Axis*IslandNeutral::TideTravel)-HeightAt(Mouth);
                            if (Rise<46. || Rise>264. || NearestCell(Mouth+Axis*IslandNeutral::TideTravel,145.)==INDEX_NONE)
                                bBothLandings=false;
                        }
                        if (bBothLandings) Add(EIslandNeutralKind::Tide);
                    }
                }
            }
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

        bool Roll(int32 Seed,const FIslandTreasureLayout& Treasure,TArray<FIslandNeutralPlacement>& Out) const
        {
            Out.Reset();
            const TArray<int32> Destinations=Endpoints(Treasure);
            TArray<FBox2D> Taken;
            if (!RoutesOpen(Taken,Destinations)) return false;
            const FIslandNeutralSettings& Settings=Terrain.NeutralPropSettings;
            const int32 Weights[]={Settings.TreeWeight,Settings.ShipWeight,Settings.HopperWeight,Settings.SailWeight,Settings.TideWeight};
            FRandomStream Stream(static_cast<int32>(uint32(Seed)^0x9A7FC5B3u));
            const int32 Target=Stream.RandRange(4,6);
            uint8 UsedKinds=0, UsedRegions=0;
            for (int32 Slot=0; Slot<Target; ++Slot)
            {
                int32 SlotWeights[KindCount];
                for (int32 K=0; K<KindCount; ++K)
                    SlotWeights[K]=Candidates[K].IsEmpty() ? 0 : FMath::Clamp(Weights[K],0,100)*((UsedKinds&(1<<K)) ? 1:3);
                bool bPlaced=false;
                for (int32 Choice=0; Choice<KindCount && !bPlaced; ++Choice)
                {
                    int32 Total=0;
                    for (int32 Weight:SlotWeights) Total+=Weight;
                    if (Total==0) break;
                    int32 Pick=Stream.RandRange(0,Total-1),K=0;
                    while (Pick>=SlotWeights[K]) Pick-=SlotWeights[K++];
                    SlotWeights[K]=0;
                    const auto& Pool=Candidates[K];
                    const int32 First=Stream.RandRange(0,Pool.Num()-1);
                    // Different strides avoid picking neighbouring headings of the same place.
                    int32 Stride=FMath::Max(1,Pool.Num()/127);
                    while (FMath::GreatestCommonDivisor(Stride,Pool.Num())!=1) ++Stride;
                    const int32 Tries=FMath::Min(128,Pool.Num());
                    for (int32 Attempt=0; Attempt<Tries; ++Attempt)
                    {
                        const FCandidate& C=Pool[(First+Attempt*Stride)%Pool.Num()];
                        if (Attempt<Tries/2 && UsedRegions!=15 && (UsedRegions&(1<<C.Region))) continue;
                        bool bClear=true;
                        for (int32 I=0; I<Treasure.Num(); ++I)
                            if (C.Envelope.ExpandBy(250.).IsInsideOrOn(FVector2D(Treasure.GetCenter(I)))) {bClear=false; break;}
                        for (int32 I=0; I<Taken.Num() && bClear; ++I)
                            bClear=!C.Envelope.ExpandBy(100.).Intersect(Taken[I]) &&
                                FVector::DistSquared2D(C.Placement.Location,Out[I].Location)>=FMath::Square(double(Settings.MinimumSpacing));
                        if (!bClear) continue;
                        Taken.Add(C.Envelope);
                        if (!RoutesOpen(Taken,Destinations)) {Taken.Pop(); continue;}
                        Out.Add(C.Placement);
                        UsedKinds|=1<<K;
                        UsedRegions|=1<<C.Region;
                        bPlaced=true;
                        break;
                    }
                }
                if (!bPlaced) break;
            }
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

FBox2D IslandNeutral::Footprint(const FIslandNeutralPlacement& Placement)
{
    return NeutralLayoutDetail::WorldEnvelope(Placement);
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
    int32 Counts[KindCount]={};
    int32 FullSeed=INDEX_NONE;
    TSet<uint32> Unique;
    bool bPassed=true;
    for (int32 Seed=1; Seed<=64; ++Seed)
    {
        FIslandTreasureLayout Treasure;
        bool bValid=IslandRules::GenerateLayout(Seed,DecoyCount,Terrain.GetSearchCircles(),*Terrain.GetDensity(),Treasure,nullptr,false,Obstacles,CanReach);
        TArray<FIslandNeutralPlacement> A,B;
        bool bRoutes=bValid && Surface.Roll(Seed,Treasure,A);
        const bool bReplay=bRoutes && Surface.Roll(Seed,Treasure,B) && A==B;
        uint32 Hash=2166136261u;
        uint8 Kinds=0,Regions=0;
        FString Items;
        TArray<FBox2D> Footprints;
        for (const auto& P:A)
        {
            const int32 K=int32(P.Kind);
            ++Counts[K];
            Kinds|=1<<K;
            int32 R=0;
            double Best=TNumericLimits<double>::Max();
            for (int32 I=0; I<Terrain.GetSearchCircles().Num(); ++I)
            {
                const double DS=FVector2D::DistSquared(FVector2D(P.Location),FVector2D(Terrain.GetSearchCircles()[I].Center));
                if (DS<Best) {Best=DS; R=I;}
            }
            Regions|=1<<R;
            const FIntVector Location(FMath::RoundToInt(P.Location.X),FMath::RoundToInt(P.Location.Y),FMath::RoundToInt(P.Location.Z));
            for (int32 Word : {K,Location.X,Location.Y,Location.Z,int32(P.Yaw)}) Hash=(Hash^uint32(Word))*16777619u;
            Items+=FString::Printf(TEXT("%s%d@%d,%d,%d/%d:%c"),Items.IsEmpty()?TEXT(""):TEXT("|"),K,Location.X,Location.Y,Location.Z,P.Yaw,'A'+R);
            Footprints.Add(WorldEnvelope(P));
        }
        bRoutes &= bValid && Surface.RoutesOpen(Footprints,Surface.Endpoints(Treasure));
        if (FullSeed==INDEX_NONE && Kinds==31) FullSeed=Seed;
        bPassed &= bReplay && bRoutes && A.Num()>=4 && A.Num()<=6;
        Unique.Add(Hash);
        UE_LOG(LogTemp,Display,TEXT("NEUTRAL_AUDIT_SEED seed=%d count=%d kinds=%u regions=%u hash=%08x replay=%d routes=%d items=%s"),
            Seed,A.Num(),Kinds,Regions,Hash,bReplay,bRoutes,*Items);
    }
    for (int32 Count:Counts) bPassed &= Count>0;
    bPassed &= FullSeed!=INDEX_NONE && Unique.Num()==64;
    UE_LOG(LogTemp,Display,TEXT("NEUTRAL_AUDIT_DONE seeds=64 passed=%d full_seed=%d tree=%d ship=%d hopper=%d sail=%d tide=%d"),
        bPassed,FullSeed,Counts[0],Counts[1],Counts[2],Counts[3],Counts[4]);
    return bPassed;
}
#endif
