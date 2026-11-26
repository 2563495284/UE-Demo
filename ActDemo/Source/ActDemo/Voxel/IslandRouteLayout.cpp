#include "Voxel/IslandRouteLayout.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "EngineUtils.h"
#include "Island/ExpeditionDock.h"
#include "Island/IslandRules.h"
#include "Misc/CommandLine.h"
#include "Voxel/VoxelTerrain.h"

namespace
{
    using IslandRouteFlora::EShape;

    struct FLane
    {
        FVector2D Start, End;
    };

    struct FView
    {
        FVector Eye, Target;
    };

    struct FTrail
    {
        FVector2D Center = FVector2D::ZeroVector;
        TArray<FVector2D> Beacons;
        TArray<FLane> Lanes;
        TArray<FView> Views;
        TArray<FBox2D> Reserved;
        int32 Docks = 0, Landmarks = 0;
    };

    FTrail ReadTrail(UWorld& World, const AVoxelTerrain& Terrain)
    {
        FTrail Trail;
        for (TActorIterator<AStaticMeshActor> It(&World); It; ++It)
        {
            const UStaticMesh* Mesh = It->GetStaticMeshComponent()->GetStaticMesh();
            if (Mesh && Mesh->GetFName() == TEXT("SM_TrailBeacon"))
            {
                const FVector2D XY(It->GetActorLocation());
                Trail.Beacons.Add(XY);
                Trail.Center += XY;
            }
        }
        if (Trail.Beacons.Num() < 3) return Trail;
        Trail.Center /= Trail.Beacons.Num();
        Trail.Beacons.Sort([Center=Trail.Center](FVector2D A,FVector2D B)
        {
            return FMath::Atan2(A.Y-Center.Y,A.X-Center.X) < FMath::Atan2(B.Y-Center.Y,B.X-Center.X);
        });
        TArray<FVector> Eyes;
        const auto AddEye = [&](const FVector2D Point)
        {
            if (const TOptional<double> Z = Terrain.GetDensity()->SurfaceAt(Point))
                Eyes.Emplace(Point, Z.GetValue()+150.);
        };
        AddEye(Trail.Center);
        for (int32 I=0; I<Trail.Beacons.Num(); ++I)
        {
            const auto& Here = Trail.Beacons[I];
            Trail.Lanes.Add({Here,Trail.Beacons[(I+1)%Trail.Beacons.Num()]});
            AddEye(Here);
        }
        for (TActorIterator<AExpeditionDock> It(&World); It; ++It)
        {
            const FVector2D Dock(It->GetActorLocation());
            const FVector2D Shore = Dock + FVector2D(It->GetActorForwardVector())*670.;
            Trail.Lanes.Add({Dock,Shore});
            // The eight original valleys follow the cardinal and diagonal landing axes into the crown.
            Trail.Lanes.Add({Shore,Trail.Center});
            double Near = TNumericLimits<double>::Max();
            FVector2D Entry = Shore;
            // These are the same endpoints as CheckLandingRoutes in the level's authoring commandlet.
            for (const auto& Circle : Terrain.GetSearchCircles())
            {
                const FVector2D C(Circle.Center);
                const double Distance = FVector2D::Distance(C,Shore);
                if (Distance < Near)
                {
                    Near = Distance;
                    Entry = C+(Shore-C).GetSafeNormal()*Circle.Radius;
                }
            }
            Trail.Lanes.Add({Shore,Entry});
            Eyes.Add(It->GetActorLocation()+FVector(0,0,178.));
            AddEye(Shore);
            AddEye(Entry);
            ++Trail.Docks;
        }
        const auto AddVisibleView = [&](FVector Eye,FVector Target)
        {
            const int32 Steps=FMath::Max(2,FMath::CeilToInt(FVector::Dist2D(Eye,Target)/100.));
            for (int32 I=1; I<Steps; ++I)
            {
                const FVector Sample=FMath::Lerp(Eye,Target,double(I)/Steps);
                if (Terrain.GetDensity()->SurfaceAt(FVector2D(Sample)).Get(-10000.) >= Sample.Z) return;
            }
            Trail.Views.Add({Eye,Target});
        };
        for (TActorIterator<AExpeditionLandmark> It(&World); It; ++It)
        {
            const FBox Bounds = It->GetComponentsBoundingBox(true);
            if (!Bounds.IsValid) continue;
            const FVector2D XY(It->GetActorLocation());
            for (const FVector& Eye : Eyes)
            {
                // Preserve both the type letter and the landmark's high silhouette at play height.
                AddVisibleView(Eye,FVector(XY,It->GetActorLocation().Z+230.));
                AddVisibleView(Eye,FVector(XY,Bounds.Max.Z-35.));
            }
            ++Trail.Landmarks;
        }
        Trail.Reserved = IslandRules::CollectSearchObstacles(World);
        for (const FBox2D& Box : Terrain.NeutralPropSettings.ExcludedAreas)
            if (Box.bIsValid) Trail.Reserved.Add(Box);
        return Trail;
    }

    FBox2D Footprint(FVector2D P, double Radius)
    {
        const FVector2D Half(Radius,Radius);
        return {P-Half,P+Half};
    }

    bool ClearsTrail(const FTrail& Trail, const FIslandRouteSite& Site)
    {
        const FVector2D P(Site.Location);
        const double R = IslandRouteFlora::FootprintRadius(Site.Kind);
        for (const FLane& Lane : Trail.Lanes)
        {
            const FVector2D D = Lane.End-Lane.Start;
            const double Along = FMath::Clamp(FVector2D::DotProduct(P-Lane.Start,D) / FMath::Max(1.,D.SizeSquared()),0.,1.);
            // Keep a 2.3 m-wide walking strip between the silhouettes, including the player's headroom.
            if (FVector2D::DistSquared(P,Lane.Start+D*Along) < FMath::Square(R+115.)) return false;
        }
        // The axisymmetric bounds contain every yaw. Test a widened sightline, not just a single pixel.
        const FBox Local = IslandRouteFlora::Bounds(Site.Kind);
        const FVector Half(R+45.,R+45.,0.);
        const FBox Box(Site.Location-Half+FVector(0,0,Local.Min.Z-12.),
                       Site.Location+Half+FVector(0,0,Local.Max.Z+12.));
        for (const FView& View : Trail.Views)
            if (FMath::LineBoxIntersection(Box,View.Eye,View.Target,View.Target-View.Eye)) return false;
        return true;
    }

    TArray<FIslandRouteSite> Place(const FTrail& Trail,const AVoxelTerrain& Terrain,
        const IslandNeutral::FSurface& Surface,const FIslandTreasureLayout& Treasure,
        TConstArrayView<FIslandNeutralPlacement> Neutrals)
    {
        TArray<FIslandRouteSite> Sites;
        TArray<FVector2D> GroupedPalms;
        TArray<FBox2D> Blockers, Reserved = Trail.Reserved;
        for (const auto& Prop : Neutrals)
        {
            Blockers.Add(IslandNeutral::Footprint(Prop));
            Reserved.Add(Blockers.Last().ExpandBy(320.)); // Leave the fallen tree, sail controls and ship approaches usable.
        }

        const auto TryPlace = [&](FVector2D XY, EShape Kind, int32 Variation, double Heading, int32 PalmAnchor=INDEX_NONE)
        {
            const double R = IslandRouteFlora::FootprintRadius(Kind);
            const FBox2D Foot = Footprint(XY,R);
            // Preserve the full public search zones, including places a future round could use.
            for (const auto& Circle : Terrain.GetSearchCircles())
                if (FVector2D::Distance(XY,FVector2D(Circle.Center)) < Circle.Radius+R+100.) return false;
            // The final chest can be outside the public circles.
            for (int32 I=0; I<Treasure.Num(); ++I)
                if (FVector2D::DistSquared(XY,FVector2D(Treasure.GetCenter(I))) < FMath::Square(R+420.)) return false;
            for (const FBox2D& Box : Reserved)
                if (Foot.Intersect(Box)) return false;
            for (int32 I=0; I<Sites.Num(); ++I)
            {
                const auto& Other=Sites[I];
                const double OtherRadius=IslandRouteFlora::FootprintRadius(Other.Kind);
                // A low companion can stand underneath the fan tips, with both roots well apart.
                const double Separation=I==PalmAnchor ? FMath::Max(R,OtherRadius)+48. : R+OtherRadius+45.;
                if (FVector2D::DistSquared(XY,FVector2D(Other.Location)) < FMath::Square(Separation)) return false;
            }
            const TOptional<double> Z = Terrain.GetDensity()->SurfaceAt(XY);
            if (!Z || *Z < 90.) return false;
            const FIslandRouteSite Candidate{Kind,FVector(XY,*Z),int16(FMath::RoundToInt(Heading)),Variation};
            if (!ClearsTrail(Trail,Candidate)) return false;
            // Each root samples the real authored ground, rather than a smooth radial approximation.
            for (const FVector2D Side : {FVector2D(R,0.),FVector2D(-R,0.),FVector2D(0.,R),FVector2D(0.,-R)})
            {
                const TOptional<double> Rim = Terrain.GetDensity()->SurfaceAt(XY+Side);
                if (!Rim || FMath::Abs(*Rim-*Z) > R*.40) return false;
            }
            Blockers.Add(Foot);
            if (!IslandNeutral::KeepsDigRoutes(Surface,Treasure,Blockers)) { Blockers.Pop(); return false; }
            Sites.Add(Candidate);
            return true;
        };

        // Bands follow the actual, editable inner beacons. The two-to-three piece groups
        // alternate taller green fans with low bushes and paired broken stone shelves.
        const EShape Leads[] = {EShape::FanPalm,EShape::SplitReef,EShape::Saltbush,EShape::CoralSpur};
        for (int32 I=0; I<Trail.Beacons.Num(); ++I)
        {
            const FVector2D A=Trail.Beacons[I], B=Trail.Beacons[(I+1)%Trail.Beacons.Num()];
            const FVector2D Middle=(A+B)*.5;
            const FVector2D Out=(Middle-Trail.Center).GetSafeNormal(), Side(-Out.Y,Out.X);
            const double Heading=FMath::RadiansToDegrees(FMath::Atan2(Out.Y,Out.X));
            const double SideSign = (I%2==0) ? 1. : -1.;
            const FVector2D Shoulder=Middle-Out*(465.+(I%3)*70.) + Side*SideSign*325.;
            const auto Patch = [&](FVector2D Preferred,EShape Shape,int32 Variation,double Angle)
            {
                // Slide a cluster along its existing shoulder if its original spot falls on a sightline or track.
                for (double Offset : {0.,180.,-180.,345.,-345.})
                {
                    const FVector2D P=Preferred+Side*Offset;
                    if (!TryPlace(P,Shape,Variation,Angle)) continue;
                    if (Shape != EShape::FanPalm || GroupedPalms.Num()>=2) return;
                    if (GroupedPalms.ContainsByPredicate([P](FVector2D Other)
                        { return FVector2D::DistSquared(Other,P)<FMath::Square(1050.); })) return;
                    const int32 Anchor=Sites.Num()-1;
                    bool bHasCompanion=false;
                    for (int32 J=0; J<2; ++J)
                    {
                        const EShape Low=J==0 ? EShape::Saltbush : EShape::SplitReef;
                        for (double Swing : {0.,32.,-32.,65.,-65.,100.,-100.,145.,-145.,180.})
                        {
                            const FVector2D Direction=Out.GetRotated((J==0 ? 36. : 202.)*SideSign+Swing);
                            if (TryPlace(P+Direction*(J==0 ? 216. : 232.),Low,Variation+71+J,
                                         Heading+Swing+J*59.,Anchor))
                            {
                                bHasCompanion=true;
                                break;
                            }
                        }
                    }
                    if (bHasCompanion) GroupedPalms.Add(P);
                    return;
                }
            };
            Patch(Shoulder, Leads[I%4], I*7, Heading+I*37.);
            Patch(Shoulder-Out*230.+Side*SideSign*310.,
                (I%3==0) ? EShape::SplitReef : EShape::Saltbush,I*7+1,Heading-63.);
            Patch(Middle+Out*(460.+(I%3)*50.)+Side*SideSign*335.,
                Leads[(I+1)%4], I*7+2, Heading+109.);
            if (I%2==0)
            {
                // Leave the very center open for crossing between the excavation shelves.
                const FVector2D Inset=FMath::Lerp(Trail.Center,Middle,.48)+Side*165.;
                Patch(Inset,Leads[(I/2+1)%4],I*7+3,Heading-27.);
                Patch(Inset+Side*SideSign*310.+Out*210.,EShape::Saltbush,I*7+4,Heading+71.);
            }
        }
        return Sites;
    }
}

TArray<FIslandRouteSite> IslandRouteLayout::Build(UWorld& World,const AVoxelTerrain& Terrain,
    const IslandNeutral::FSurface& Surface,const FIslandTreasureLayout& Treasure,
    TConstArrayView<FIslandNeutralPlacement> Neutrals)
{
    if (!Surface || !Terrain.GetDensity()) return {};
    const FTrail Trail = ReadTrail(World,Terrain);
    TArray<FIslandRouteSite> Sites = Place(Trail,Terrain,Surface,Treasure,Neutrals);
    TArray<FBox2D> Footprints;
    for (const auto& Prop : Neutrals) Footprints.Add(IslandNeutral::Footprint(Prop));
    int32 Kinds[4] = {};
    for (const auto& Site : Sites)
    {
        ++Kinds[int32(Site.Kind)];
        Footprints.Add(Footprint(FVector2D(Site.Location),IslandRouteFlora::FootprintRadius(Site.Kind)));
    }
    const int32 PalmGroups=Sites.FilterByPredicate([&](const FIslandRouteSite& Palm)
    {
        return Palm.Kind==EShape::FanPalm && Sites.ContainsByPredicate([&](const FIslandRouteSite& Other)
        {
            return Other.Kind!=EShape::FanPalm && FVector::DistSquared2D(Palm.Location,Other.Location)<=FMath::Square(235.);
        });
    }).Num();
    const bool Routes = IslandNeutral::KeepsDigRoutes(Surface,Treasure,Footprints);
    UE_LOG(LogTemp,Display,TEXT("ROUTE_SCENERY_LAYOUT sites=%d palms=%d bushes=%d reefs=%d coral=%d palm_groups=%d beacons=%d docks=%d landmarks=%d lanes=%d sightlines=%d dig_sites=%d routes=%d"),
        Sites.Num(),Kinds[0],Kinds[1],Kinds[2],Kinds[3],PalmGroups,Trail.Beacons.Num(),Trail.Docks,Trail.Landmarks,
        Trail.Lanes.Num(),Trail.Views.Num(),Treasure.Num(),Routes);
#if !UE_BUILD_SHIPPING
    if (FParse::Param(FCommandLine::Get(),TEXT("RouteSceneryAudit")))
    {
        const auto Obstacles = IslandRules::CollectSearchObstacles(World);
        const auto CanReach = [&](FVector2D P){ return IslandNeutral::CanReachDigSite(Surface,P); };
        int32 Passed = 0, Fewest = MAX_int32, Most = 0;
        for (int32 Seed=1; Seed<=64; ++Seed)
        {
            FIslandTreasureLayout Dig;
            if (!IslandRules::GenerateLayout(Seed,Treasure.Decoys.Num(),Terrain.GetSearchCircles(),*Terrain.GetDensity(),
                Dig,nullptr,false,Obstacles,CanReach)) continue;
            TArray<FIslandNeutralPlacement> Features;
            IslandNeutral::Generate(Surface,Seed,Dig,Features);
            const auto Batch = Place(Trail,Terrain,Surface,Dig,Features);
            TArray<FBox2D> All;
            for (const auto& Prop : Features) All.Add(IslandNeutral::Footprint(Prop));
            bool bClear = !Batch.IsEmpty();
            for (const auto& Site : Batch)
            {
                const double Radius=IslandRouteFlora::FootprintRadius(Site.Kind);
                All.Add(Footprint(FVector2D(Site.Location),Radius));
                bClear &= ClearsTrail(Trail,Site);
                for (int32 I=0; I<Dig.Num(); ++I)
                    bClear &= FVector2D::Distance(FVector2D(Site.Location),FVector2D(Dig.GetCenter(I))) >= Radius+420.;
            }
            if (bClear && IslandNeutral::KeepsDigRoutes(Surface,Dig,All)) ++Passed;
            Fewest=FMath::Min(Fewest,Batch.Num());
            Most=FMath::Max(Most,Batch.Num());
        }
        UE_LOG(LogTemp,Display,TEXT("ROUTE_SCENERY_AUDIT seeds=64 pass=%d minimum=%d maximum=%d docks=%d landmarks=%d lanes=%d sightlines=%d decoys=%d"),
            Passed,Fewest,Most,Trail.Docks,Trail.Landmarks,Trail.Lanes.Num(),Trail.Views.Num(),Treasure.Decoys.Num());
    }
#endif
    return Sites;
}
