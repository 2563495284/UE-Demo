#include "Voxel/IslandRouteFlora.h"

namespace
{
    using IslandRouteFlora::FSample;

    enum ECoastPigment : uint8
    {
        Husk, PalmShade, PalmGreen, PalmTip, Twig, Myrtle, Sage, Sunleaf,
        DeepShale, BlueShale, SeaLichen, OchreReef, SunReef,
        PalmRib, Nut, SaltBud, WetStone, ShellLime, CoralRose
    };

    void Add(FSample& Solid, float Distance, uint8 Pigment)
    {
        if (Distance < Solid.Distance) Solid = {Distance, Pigment};
    }

    // Four pairs of flat planes give a bevelled, asymmetric octagonal outline.
    float Octagon(float X, float Y)
    {
        X = FMath::Abs(X);
        Y = FMath::Abs(Y);
        return FMath::Max3(X, Y, .72f * X + .72f * Y);
    }

    float Stem(FVector3f P, FVector3f From, FVector3f To, float Root, float Tip)
    {
        const FVector3f Axis = To - From;
        const float T = FMath::Clamp(FVector3f::DotProduct(P - From, Axis) / Axis.SizeSquared(), 0.f, 1.f);
        const FVector3f D = P - From - T * Axis;
        return FMath::Max(Octagon(D.X,D.Y),FMath::Abs(D.Z)) - FMath::Lerp(Root,Tip,T);
    }

    struct FFrond
    {
        float X, Y, Reach, TipHeight, ShoulderHeight, FanWidth;
        uint8 Pigment;
    };

    // A fan blade opens on a narrow petiole into tapered pleats. The clefts
    // interrupt its outer edge, leaving recognisable fingers in the skyline.
    FSample Frond(FVector3f P, const FFrond& Leaf)
    {
        const float U = P.X*Leaf.X + P.Y*Leaf.Y;
        const float V = P.Y*Leaf.X - P.X*Leaf.Y;
        const float T = FMath::Clamp(U/Leaf.Reach,0.f,1.f);
        const float Width = 8.f + Leaf.FanWidth*FMath::Clamp((T-.18f)/.56f,0.f,1.f);
        const float Across = FMath::Abs(V)/Width;
        const float Pleat = FMath::Abs(FMath::Fmod(Across*1.25f+.5f,1.f)-.5f)*2.f;
        const float Tear = FMath::Clamp((Pleat-.32f)/.64f,0.f,1.f);
        const float Reach = Leaf.Reach*(1.f-.17f*Across-0.130f*Tear);
        const float Height = T<.49f ? Leaf.ShoulderHeight*T/.49f :
            FMath::Lerp(Leaf.ShoulderHeight,Leaf.TipHeight,(T-.49f)/.51f);
        const float Fold = (1.f-Pleat)*6.f*T - .23f*FMath::Abs(V) + .10f*V;
        const float Thickness = 12.f-1.5f*T;
        const float D = FMath::Max3(FMath::Max(-U,U-Reach), FMath::Abs(V)-Width,
            .8f*(FMath::Abs(P.Z-Height-Fold)-Thickness));
        uint8 Paint = Leaf.Pigment;
        if (Across<.14f && T>.20f) Paint=PalmRib;
        else if (V>0.f && Across>.38f) Paint=Leaf.Pigment==PalmShade ? PalmGreen : PalmTip;
        return {D,Paint};
    }

    FSample FanPalm(FVector3f P, int32 Variant)
    {
        if (Variant & 1) P.Y = -P.Y;
        const bool bBent = (Variant&2)!=0;
        const FVector3f Heel(-47,-8,-27), Bend(-30,-12,48), Crown(bBent?15.f:7.f,2,108);
        const float Band = FMath::Fmod(P.Z+82.f,26.f);
        const float Collar = Band<8.f ? 3.f*(1.f-Band/8.f) : 0.f;
        const uint8 Bark = Band<8.f ? Twig : Husk;
        FSample S{Stem(P,Heel,Bend,20.f+Collar,13.f+Collar),Bark};
        Add(S,Stem(P,Bend,Crown,13.f+Collar,10.f+Collar),Bark);
        const FVector3f Hub=P-Crown;
        static constexpr FFrond Leaves[] = {
            { .96f, .28f, 132,-44,24,35,PalmShade},
            { .46f,-.89f, 122,-22,30,34,PalmGreen},
            {-.48f, .88f,  81, 49,40,49,PalmTip},
            {-.94f,-.34f,  72, 59,42,43,PalmGreen}
        };
        for (const FFrond& Leaf : Leaves)
        {
            const FSample Blade=Frond(Hub,Leaf);
            Add(S,Blade.Distance,Blade.Pigment);
        }
        Add(S,Stem(Hub,{0,0,-7},{-14,12,66},11.f,5.f),PalmRib);
        // A pair of amber seed cases sits partly under the crown.
        Add(S,Stem(Hub,{-9,-3,-4},{-23,-6,-18},14.f,10.f),Nut);
        Add(S,Stem(Hub,{3,-8,-2},{12,-16,-16},13.f,9.f),Nut);
        return S;
    }

    // Clipped tops and angular shoulders maintain distinct silhouettes, even at one flat colour.
    float CutStone(FVector3f P, FVector3f Center, FVector3f Half, float Lean, float Phase)
    {
        P -= Center;
        P.X -= Lean * P.Z;
        const float X = P.X / Half.X, Y = P.Y / Half.Y;
        const float Z = (P.Z + Phase * P.X + .075f * P.Y) / Half.Z;
        const float R = Octagon(X,Y);
        const float D = FMath::Max3(R - 1.f, FMath::Abs(Z) - 1.f,
            R + .55f * FMath::Abs(Z) - 1.32f);
        return D * FMath::Min3(Half.X,Half.Y,Half.Z);
    }

    // A fleshy, folded leaf has a wide shoulder, a keel and a pointed tip.
    // Work in its own plane so an upright leaf stays thin when viewed edge-on.
    void FleshyLeaf(FSample& S, FVector3f P, FVector3f Root, FVector3f Tip, float HalfWidth, uint8 Paint)
    {
        const FVector3f Axis=Tip-Root;
        const float Length2=Axis.SizeSquared();
        const float T=FVector3f::DotProduct(P-Root,Axis)/Length2;
        const float Along=FMath::Clamp(T,0.f,1.f);
        const FVector3f Delta=P-Root-Axis*Along;
        const FVector3f Side(-Axis.Y,Axis.X,0.f);
        const float SideLength=FMath::Sqrt(Side.SizeSquared());
        const float WidthCoordinate=FVector3f::DotProduct(Delta,Side)/SideLength;
        const FVector3f Up=FVector3f::CrossProduct(Axis,Side);
        const float Height=FVector3f::DotProduct(Delta,Up)/FMath::Sqrt(Up.SizeSquared());
        const float Shoulder=FMath::Min(Along/.64f,(1.f-Along)/.36f);
        const float Width=3.6f+HalfWidth*Shoulder;
        const float Thickness=4.8f+4.8f*Shoulder;
        const float Fold=Height-.31f*FMath::Abs(WidthCoordinate);
        const float D=FMath::Max3(FMath::Max(-T,T-1.f)*FMath::Sqrt(Length2),FMath::Abs(WidthCoordinate)-Width,
                                    FMath::Abs(Fold)-Thickness);
        const uint8 Pigment=WidthCoordinate>3.f || Paint==Sunleaf ? Paint : (Paint==Myrtle?Sage:Sunleaf);
        Add(S,D,Pigment);
    }

    FSample Saltbush(FVector3f P, int32 Variant)
    {
        if (Variant&1) P.Y=-P.Y;
        const FVector3f Heel(-3,0,-25), Fork(-3,0,17);
        const FVector3f Left(-48,-11,42), Right(40,2,33), High(1,25,59);
        FSample S{Stem(P,Heel,Fork,20.f,9.f),Twig};
        Add(S,Stem(P,{-29,-11,-34},Fork,14,9),Husk);
        Add(S,Stem(P,{24,10,-32},Fork,14,9),Twig);
        Add(S,Stem(P,Fork,Left,10,7),Twig);
        Add(S,Stem(P,Fork,Right,10,7),Husk);
        Add(S,Stem(P,Fork,High,11,7),Twig);
        if (P.Z < -9.f) return S;
        // Three separate sprays, each with an outward spear and a younger upright leaf.
        FleshyLeaf(S,P,Left,{-106,-13,66},18,Myrtle);
        FleshyLeaf(S,P,Left,{-70,-47,78},17,Sage);
        FleshyLeaf(S,P,Left,{-49,7,85},14,Myrtle);
        FleshyLeaf(S,P,Right,{102,20,55},20,Sage);
        FleshyLeaf(S,P,Right,{56,-49,55},18,Myrtle);
        FleshyLeaf(S,P,Right,{62,6,82},15,Sage);
        FleshyLeaf(S,P,High,{-24,62,82},16,Sage);
        FleshyLeaf(S,P,High,{12,42,(Variant&2)?102.f:99.f},16,Sunleaf);
        // An ivory seed head and two scale-like buds sit on their own stalk.
        const FVector3f Bud(-12,-22,74);
        Add(S,Stem(P,Fork,Bud,7,6),Twig);
        Add(S,CutStone(P,Bud+FVector3f(-3,0,3),{8,8,10},-.15f,.10f),SaltBud);
        Add(S,CutStone(P,Bud+FVector3f(8,0,-7),{7,7,8},.20f,-.10f),SaltBud);
        return S;
    }

    FSample SplitReef(FVector3f P, int32 Variant)
    {
        if (Variant&1) P.Y=-P.Y;
        P.Y+=.23f*P.X;
        FSample S{CutStone(P,{-27,4,-9},{73,45,24},-.18f,.14f),WetStone};
        Add(S,CutStone(P,{44,-5,-10},{63,39,22},-.15f,.15f),WetStone);
        Add(S,Stem(P,{-18,0,-44},{-31,5,6},23,29),WetStone);
        // A wind-cut plate exposes a chisel edge. Three much lower, tilted
        // beds step down beside it, with daylight through the cleft.
        const float Knife=CutStone(P,{-42,4,45},{29,43,61},-.61f,.32f);
        Add(S,FMath::Max3(Knife,(P.Z+.40f*P.X+.43f*P.Y-81.f)*.65f,P.Z-109.f),BlueShale);
        Add(S,CutStone(P,{25,15,19},{20,35,38},-.47f,.25f),DeepShale);
        Add(S,CutStone(P,{57,-5,12},{23,35,26},-.49f,.29f),BlueShale);
        Add(S,CutStone(P,{86,-14,5},{29,28,18},-.50f,.27f),DeepShale);
        const float Strata=P.Z+.45f*P.X-.15f*P.Y + ((Variant&2)?4.f:0.f);
        if (S.Pigment!=WetStone)
        {
            if (Strata>28.f && Strata<37.f) S.Pigment=ShellLime;
            else if (Strata>65.f) S.Pigment=SeaLichen;
        }
        return S;
    }

    FSample CoralSpur(FVector3f P, int32 Variant)
    {
        if (Variant&1) P.X=-P.X;
        // A thin pair of weathered holdfasts follows the ground. The buried roots
        // taper down separately so a pitched bank does not reveal a vertical plinth.
        FSample S{CutStone(P,{-24,7,-8},{53,36,20},.18f,.17f),WetStone};
        Add(S,CutStone(P,{29,-7,-8},{43,30,19},.12f,-.15f),DeepShale);
        Add(S,Stem(P,{-23,6,-43},{-27,5,5},17,24),WetStone);
        Add(S,Stem(P,{18,-8,-42},{29,-10,3},16,21),WetStone);
        if (P.Z < -13.f) return S;
        // Two growing fans face different directions. Their rounded stumps and
        // short side forks leave windows between the taller fingers from every bank.
        const FVector3f Fork(-32,7,47), Outer(28,-12,36);
        Add(S,Stem(P,{-24,5,0},Fork,17,12),OchreReef);
        Add(S,Stem(P,Fork,{-58,1,68},13,9),CoralRose);
        Add(S,Stem(P,{-58,1,68},{-82,-12,104},10,6.5f),CoralRose);
        Add(S,Stem(P,{-58,1,68},{-60,7,102},9,6.5f),SunReef);
        Add(S,Stem(P,Fork,{-24,13,88},13,10),CoralRose);
        Add(S,Stem(P,{-24,13,88},{-34,7,125},10,6.5f),SunReef);
        Add(S,Stem(P,{-24,13,88},{5,25,117},10,6.5f),CoralRose);
        Add(S,Stem(P,{24,-10,0},Outer,15,12),OchreReef);
        Add(S,Stem(P,Outer,{55,-10,57},12,9),CoralRose);
        Add(S,Stem(P,{55,-10,57},{82,1,88},10,7),CoralRose);
        Add(S,Stem(P,{55,-10,57},{52,-16,91},9,6.5f),SunReef);
        Add(S,Stem(P,Outer,{24,-27,62},10,9),CoralRose);
        Add(S,Stem(P,{24,-27,62},{32,-37,(Variant&2)?94.f:84.f},9,6),SunReef);
        if (S.Pigment!=WetStone && S.Pigment!=DeepShale)
        {
            if (P.Z+P.Y*.14f > 108.f) S.Pigment=ShellLime;
            else if (P.Z < 20.f) S.Pigment=OchreReef;
        }
        return S;
    }
}

IslandRouteFlora::FSample IslandRouteFlora::Sample(EShape Shape, const FVector& Local, int32 Variation)
{
    const FVector3f P(Local);
    switch (Shape)
    {
        case EShape::FanPalm: return FanPalm(P,Variation);
        case EShape::Saltbush: return Saltbush(P,Variation);
        case EShape::SplitReef: return SplitReef(P,Variation);
        case EShape::CoralSpur: return CoralSpur(P,Variation);
    }
    return {MAX_flt,0};
}

float IslandRouteFlora::FootprintRadius(EShape Shape)
{
    switch (Shape)
    {
        case EShape::FanPalm: return 160.f;
        case EShape::Saltbush: return 140.f;
        case EShape::SplitReef: return 158.f;
        case EShape::CoralSpur: return 125.f;
    }
    return 0.f;
}

FBox IslandRouteFlora::Bounds(EShape Shape)
{
    static constexpr float Height[] = {185.f,110.f,120.f,140.f};
    const float Radius = FootprintRadius(Shape);
    if (!Radius) return FBox(ForceInit);
    return FBox(FVector(-Radius,-Radius,-70), FVector(Radius,Radius,Height[uint8(Shape)]));
}

bool IslandRouteFlora::IsWoodyPigment(uint8 Pigment)
{
    return Pigment==Husk || Pigment==Twig || Pigment==Nut;
}

FColor IslandRouteFlora::PigmentColor(uint8 Pigment)
{
    // Alpha is the finish roughness in M_RoutePigment. The same material
    // identity survives extraction of an intact facet and excavation of its interior.
    static constexpr FColor Colors[] = {
        {142,100,71,222}, {43,106,92,171}, {65,137,101,163}, {110,160,103,155},
        {116,86,62,229}, {58,122,109,162}, {99,153,133,158}, {152,181,142,157},
        {70,93,106,195}, {95,124,137,182}, {139,157,147,193}, {148,99,77,211},
        {214,150,109,200}, {150,171,101,173}, {207,137,66,194}, {222,202,155,186},
        {57,78,84,115}, {189,180,148,203}, {184,115,98,198}
    };
    return Pigment < UE_ARRAY_COUNT(Colors) ? Colors[Pigment] : FColor::White;
}

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FExpeditionRouteFloraSamples, "ActDemo.Island.RouteFlora.GroundedSilhouettes",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FExpeditionRouteFloraSamples::RunTest(const FString& Parameters)
{
    using namespace IslandRouteFlora;
    // Half-cell offsets catch thin, diagonally touching fronds which disappear after a real support edit.
    for (EShape Shape : {EShape::FanPalm, EShape::Saltbush, EShape::SplitReef, EShape::CoralSpur})
    for (int32 Variant : {0,1,2,3})
    for (int32 ProbeGrid = 0; ProbeGrid < 3; ++ProbeGrid)
    {
        const auto Box = Bounds(Shape);
        const double Step = SampleSpacing(Shape);
        const FVector Phase = ProbeGrid==2 ? FVector(FMath::Frac(Box.Min.X/Step),FMath::Frac(Box.Min.Y/Step),FMath::Frac(Box.Min.Z/Step)) : FVector(.5*ProbeGrid);
        TSet<FIntVector> Remaining;
        TSet<uint8> Pigments;
        TArray<FIntVector> Flood;
        for (int32 Z=FMath::FloorToInt(Box.Min.Z/Step)-1; Z<=FMath::CeilToInt(Box.Max.Z/Step)+1; ++Z)
        for (int32 Y=FMath::FloorToInt(Box.Min.Y/Step)-1; Y<=FMath::CeilToInt(Box.Max.Y/Step)+1; ++Y)
        for (int32 X=FMath::FloorToInt(Box.Min.X/Step)-1; X<=FMath::CeilToInt(Box.Max.X/Step)+1; ++X)
        {
            const FIntVector Cell(X,Y,Z);
            const FVector P = (FVector(Cell) + Phase) * Step;
            const FSample S = Sample(Shape,P,Variant);
            if (S.Distance >= 0.f) continue;
            if (!Box.IsInsideOrOn(P) || P.SizeSquared2D() > FMath::Square(FootprintRadius(Shape)))
                AddError(TEXT("A route silhouette exceeds its sightline reservation"));
            Pigments.Add(S.Pigment);
            Remaining.Add(Cell);
            if (P.Z <= 0.) Flood.Add(Cell);
        }
        TestTrue(TEXT("The shape has a substantial, multicolour volume"), Remaining.Num()>90 && Pigments.Num()>=3);
        static const FIntVector Neighbors[] = {{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}};
        while (!Flood.IsEmpty())
        {
            const FIntVector Cell = Flood.Pop(EAllowShrinking::No);
            if (!Remaining.Remove(Cell)) continue;
            for (const FIntVector Offset : Neighbors)
                if (Remaining.Contains(Cell+Offset)) Flood.Add(Cell+Offset);
        }
        TestEqual(TEXT("Every solid sample connects by a face to the planted base"), Remaining.Num(), 0);
    }
    return !HasAnyErrors();
}
#endif
