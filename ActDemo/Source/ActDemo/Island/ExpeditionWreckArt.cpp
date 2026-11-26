#include "Island/ExpeditionWreckArt.h"

#include "GameFramework/Actor.h"
#include "Island/IslandSceneryVoxels.h"
#include "Island/IslandNeutralLayout.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "ProceduralMeshComponent.h"

namespace
{
    enum EFinish
    {
        Tar, Walnut, Underpaint, Oxide, Vermilion, Coral, Oak, Endgrain, Flax, Brass, Lagoon,
        Saltwood, CanvasShade, Feather, FinishCount
    };

    struct FFinish
    {
        FColor Color;
        float Roughness;
        float Metallic;
    };

    constexpr FFinish Finishes[] = {
        {{47,55,56},.83f,0.f},      // Tarred keel
        {{90,62,52},.89f,0.f},      // Dark timber
        {{114,73,64},.82f,0.f},     // Exposed underpaint
        {{145,73,59},.74f,0.f},     // Oxide red
        {{178,81,60},.66f,0.f},     // Vermilion paint
        {{192,94,71},.69f,0.f},     // Sun-faded coral
        {{153,109,78},.84f,0.f},    // Oak
        {{201,156,105},.91f,0.f},   // Broken endgrain
        {{239,224,192},.94f,0.f},   // Bleached flax
        {{211,160,88},.41f,.68f},   // Worn brass
        {{48,122,123},.56f,0.f},    // Blue-green paint
        {{169,139,111},.9f,0.f},    // Sun- and salt-bleached timber
        {{190,183,158},.96f,0.f},   // Weathered canvas
        {{67,145,147},.49f,0.f}     // Painted crest
    };
    static_assert(UE_ARRAY_COUNT(Finishes)==FinishCount);

    enum EWreckPart
    {
        PortBow, StarboardBow, PortStern, StarboardStern, PortRail, StarboardRail,
        Transom, Prow, ForeDeck, AftDeck, MastAndYard, Canvas, Forestay, Backstays, PartCount
    };

    // Rigging is a shovel target. Decks and structural timbers also carry characters.
    const TCHAR* PartNames[PartCount] = {
        TEXT("PortBow"),TEXT("StarboardBow"),TEXT("PortStern"),TEXT("StarboardStern"),
        TEXT("PortRail"),TEXT("StarboardRail"),TEXT("Transom"),TEXT("Prow"),TEXT("ForeDeck"),
        TEXT("AftDeck"),TEXT("MastAndYard"),TEXT("Canvas"),TEXT("Forestay"),TEXT("Backstays")
    };
    struct FTimber
    {
        FProcMeshSection Finishes[FinishCount];
    };

    // Round spars and rope are sized in world centimetres because the ship site
    // stretches X and Y differently. All surfaces share one editable local frame.
    struct FShipMesh
    {
        FTimber Parts[PartCount];
        EWreckPart Current = PortBow;
        const FVector Scale = IslandNeutral::SiteScale(EIslandNeutralKind::Ship);

        void Face(EFinish Finish, TConstArrayView<FVector> Points, const FVector& Outside)
        {
            auto& Section = Parts[Current].Finishes[Finish];
            const FVector Cross = FVector::CrossProduct(Points[1]-Points[0], Points[2]-Points[0]);
            if (Cross.IsNearlyZero()) return;
            const bool bReverse = FVector::DotProduct(Cross, Outside) < 0.;
            const FVector Normal = (bReverse ? -Cross : Cross).GetSafeNormal();
            const uint32 First = Section.ProcVertexBuffer.Num();
            for (const FVector& Point : Points)
            {
                FProcMeshVertex& Vertex = Section.ProcVertexBuffer.AddDefaulted_GetRef();
                Vertex.Position = Point;
                Vertex.Normal = Normal;
                Vertex.Tangent = FProcMeshTangent((Points[1]-Points[0]).GetSafeNormal(), false);
                Section.SectionLocalBox += Point;
            }
            for (int32 I = 1; I+1 < Points.Num(); ++I)
            {
                Section.ProcIndexBuffer.Add(First);
                Section.ProcIndexBuffer.Add(First + (bReverse ? I : I+1));
                Section.ProcIndexBuffer.Add(First + (bReverse ? I+1 : I));
            }
        }

        void Quad(EFinish Finish, FVector A, FVector B, FVector C, FVector D, FVector Outside)
        {
            const FVector Points[] = {A,B,C,D};
            Face(Finish, Points, Outside);
        }

        void Tube(FVector A, FVector B, double RadiusA, double RadiusB, EFinish Finish, int32 Sides = 8)
        {
            const FVector Axis = ((B-A)*Scale).GetSafeNormal();
            FVector U,V;
            Axis.FindBestAxisVectors(U,V);
            TArray<FVector,TInlineAllocator<12>> Base, Tip;
            for (int32 I = 0; I < Sides; ++I)
            {
                const double Angle = UE_TWO_PI * I / Sides;
                const FVector Radial = (U*FMath::Cos(Angle) + V*FMath::Sin(Angle)) / Scale;
                Base.Add(A + Radial*RadiusA);
                Tip.Add(B + Radial*RadiusB);
            }
            for (int32 I = 0; I < Sides; ++I)
            {
                const int32 J = (I+1)%Sides;
                Quad(Finish, Base[I], Base[J], Tip[J], Tip[I], (Base[I]+Base[J])*.5-A);
            }
            Face(Finish, Base, A-B);
            Face(Finish, Tip, B-A);
        }
    };

    // Distance along the keel, half breadth at the rail, and height of the sheer.
    struct FStation { double Y, Breadth, Rise; };
    constexpr FStation Stations[] = {
        {-409,3,151}, {-389,25,110}, {-363,81,68}, {-317,128,37}, {-260,154,15},
        {-188,167,3}, {-105,170,0}, {0,170,0}, {106,170,0}, {189,167,2},
        {259,156,10}, {317,128,19}, {362,92,30}, {387,73,43}
    };

    FVector Skin(double Side, double Y, double Level, double Offset = 0.)
    {
        int32 I = 1;
        while (I+1 < UE_ARRAY_COUNT(Stations) && Stations[I].Y < Y) ++I;
        const FStation& A = Stations[I-1];
        const FStation& B = Stations[I];
        const double T = FMath::Clamp((Y-A.Y)/(B.Y-A.Y),0.,1.);
        const double Breadth = FMath::Lerp(A.Breadth,B.Breadth,T);
        const double Height = FMath::Clamp((Level+165.)/315.,0.,1.);
        const double Flare = .78 + .22*Height;
        const double Rise = FMath::Lerp(A.Rise,B.Rise,T);
        return {Side*(Breadth*Flare+Offset), Y, Level + Rise*(.4+.6*Height)};
    }

    // A closed overlapping strake with a bevel along the upper edge. The raw timber is
    // visible where each piece breaks off at the mouth of the cross-ship passage.
    void Plank(FShipMesh& Mesh, double Side, double From, double To, double Bottom, double Top,
        EFinish Paint, double Proud, double Thickness, EFinish UpperEdge)
    {
        TArray<double,TInlineAllocator<18>> Knots;
        Knots.Add(From);
        for (auto& Station : Stations)
            if (Station.Y > From+.1 && Station.Y < To-.1) Knots.Add(Station.Y);
        Knots.Add(To);

        FVector Previous[5];
        for (int32 I = 0; I < Knots.Num(); ++I)
        {
            const double Y = Knots[I];
            const double Bevel = FMath::Min(4.5, (Top-Bottom)*.28);
            const FVector Ring[] = {
                Skin(Side,Y,Bottom,Proud-1.), Skin(Side,Y,Top-Bevel,Proud+1.),
                Skin(Side,Y,Top,Proud-1.5), Skin(Side,Y,Top,Proud-Thickness),
                Skin(Side,Y,Bottom,Proud-Thickness)
            };
            if (I)
            {
                const EFinish Faces[] = {Paint,UpperEdge,UpperEdge,Walnut,Underpaint};
                const FVector Outside[] = {{Side,0,0},{Side,0,1},{0,0,1},{-Side,0,0},{0,0,-1}};
                for (int32 K = 0; K < 5; ++K)
                {
                    const int32 Next = (K+1)%5;
                    Mesh.Quad(Faces[K], Previous[K], Previous[Next], Ring[Next], Ring[K], Outside[K]);
                }
            }
            if (I == 0 || I == Knots.Num()-1)
                Mesh.Face(Endgrain, Ring, FVector(0,I == 0 ? -1. : 1.,0));
            for (int32 K=0; K<5; ++K) Previous[K]=Ring[K];
        }
    }

    // The bow carving has a narrow cut edge and a chisel ridge on each cheek.
    // Every face belongs to the prow's voxel surface, including the painted eye.
    void CarvedPiece(FShipMesh& Mesh, TConstArrayView<FVector2D> Outline, FVector2D Ridge,
        double EdgeWidth, double RidgeWidth, EFinish FacePaint, EFinish EdgePaint)
    {
        for (int32 I=0; I<Outline.Num(); ++I)
        {
            const FVector2D A=Outline[I], B=Outline[(I+1)%Outline.Num()];
            const FVector2D Out=(A+B)*.5-Ridge;
            Mesh.Quad(EdgePaint,{-EdgeWidth,A.X,A.Y},{EdgeWidth,A.X,A.Y},
                {EdgeWidth,B.X,B.Y},{-EdgeWidth,B.X,B.Y},{0,Out.X,Out.Y});
            for (const double Side : {-1.,1.})
            {
                const FVector Facet[]={{EdgeWidth*Side,A.X,A.Y},
                    {EdgeWidth*Side,B.X,B.Y},{RidgeWidth*Side,Ridge.X,Ridge.Y}};
                Mesh.Face(FacePaint,Facet,{Side,0,0});
            }
        }
    }

    void CarvedBird(FShipMesh& Mesh)
    {
        Mesh.Current=Prow;
        const FVector2D Breast[]={{-410,251},{-407,316},{-394,356},{-374,356},
            {-362,329},{-339,276},{-377,287}};
        CarvedPiece(Mesh,Breast,{-387,313},7.,17.,Lagoon,Walnut);
        const FVector2D Head[]={{-404,344},{-407,374},{-396,395},{-379,402},
            {-353,383},{-373,378},{-362,365},{-382,346}};
        CarvedPiece(Mesh,Head,{-393,371},8.,18.,Flax,Saltwood);
        const FVector2D Bill[]={{-402,374},{-450,357},{-444,351},{-402,353},{-395,360}};
        CarvedPiece(Mesh,Bill,{-409,362},5.,10.,Brass,Oak);
        const FVector2D Crown[]={{-401,386},{-395,398},{-380,410},{-346,389},
            {-370,389},{-361,382},{-383,390}};
        CarvedPiece(Mesh,Crown,{-381,396},8.,14.,Feather,Lagoon);
        for (const double Side : {-1.,1.})
        {
            const FVector Eye[]={{16.2*Side,-400,372},{17.5*Side,-393,379},
                {16.6*Side,-386,373},{18.4*Side,-393,365}};
            Mesh.Face(Tar,Eye,{Side,0,0});
        }
    }

    void Hull(FShipMesh& Mesh)
    {
        constexpr double Levels[] = {-165,-112,-64,-19,23,59,90,119};
        constexpr EFinish Paints[] = {Tar,Walnut,Underpaint,Oxide,Vermilion,Vermilion,Coral};
        for (const double Side : {-1.,1.})
        {
            for (int32 Row = 0; Row < UE_ARRAY_COUNT(Paints); ++Row)
            {
                // Stagger the splintered board ends and the scarf joints on each side.
                const double Gap = 93. + ((Row*17 + (Side>0.?7:0))%26);
                const double BowJoint = -228. - 31.*(Row%3);
                const double SternJoint = 220. + 31.*((Row+1)%3);
                const double Low = Levels[Row]+1.5, High = Levels[Row+1];
                const EFinish Paint = Paints[Row];
                Mesh.Current = Side < 0. ? PortBow : StarboardBow;
                Plank(Mesh,Side,-405,BowJoint-.65,Low,High,Paint,5.,12.,Paint);
                Plank(Mesh,Side,BowJoint+.65,-Gap,Low,High,Paint,5.,12.,Paint);
                Mesh.Current = Side < 0. ? PortStern : StarboardStern;
                Plank(Mesh,Side,Gap+5.,SternJoint-.65,Low,High,Paint,5.,12.,Paint);
                Plank(Mesh,Side,SternJoint+.65,385,Low,High,Paint,5.,12.,Paint);
            }
            Mesh.Current = Side < 0. ? PortRail : StarboardRail;
            // Unbroken wale above both openings, with a narrow painted ochre line.
            Plank(Mesh,Side,-406,386,118,132,Walnut,7.,15.,Walnut);
            Plank(Mesh,Side,-407,386,131,149,Walnut,7.5,16.,Oak);
            Plank(Mesh,Side,-408,387,147,155,Saltwood,9.,18.,Endgrain);

            for (const double Y : {-329.,-255.,-108.,108.,255.,331.})
            {
                Mesh.Current = Y < 0. ? (Side < 0. ? PortBow : StarboardBow) : (Side < 0. ? PortStern : StarboardStern);
                // Unequal structural posts. Unequal split tips show
                // above the rail without narrowing the hatch or either approach.
                const bool bBroken = FMath::Abs(Y)==255. || Y==331.;
                const double TipHeight = bBroken ? (Y<0.?193.:175.) : 146.;
                const FVector Foot = Skin(Side,Y,-115,-13);
                const FVector Waist = Skin(Side,Y,46,-12);
                const FVector Tip = Skin(Side,Y+((Y>0.)?2.:-2.),TipHeight,-10);
                Mesh.Tube(Foot,Waist,22.,20.,Walnut,5);
                Mesh.Tube(Waist,Tip,20.,bBroken?13.:20.,bBroken?Oak:Walnut,5);
                if (bBroken)
                    Mesh.Tube(Tip-FVector(0,0,9),Tip+FVector(Side*1.5,2.5,14),10.,1.,Endgrain,4);
            }

            Mesh.Current = Side < 0. ? PortBow : StarboardBow;
            // A painted pilot eye on either side of the prow. The pigment follows the
            // actual shell so that even the foreshortened eye reads from the beach.
            // Lift the emblem onto the exposed strakes above the original sand ridge.
            const auto Mark = [Side](double Y, double Z, double Offset)
            {
                return Skin(Side,-318.+(Y+318.)*1.2,96.+(Z-77.)*1.22,Offset);
            };
            const FVector Eye[] = {Mark(-351,65,11),Mark(-327,92,11),Mark(-303,95,11),
                Mark(-282,83,11),Mark(-303,64,11),Mark(-329,57,11)};
            Mesh.Face(Flax,Eye,{Side,0,0});
            const FVector Iris[] = {Mark(-332,74,13),Mark(-318,91,13),Mark(-303,79,13),Mark(-315,64,13)};
            Mesh.Face(Lagoon,Iris,{Side,0,0});
            const FVector Pupil[] = {Mark(-325,75,15),Mark(-317,84,15),Mark(-311,77,15),Mark(-318,71,15)};
            Mesh.Face(Tar,Pupil,{Side,0,0});
        }

        Mesh.Current = Transom;
        // The existing transom is broad and square. The opposite end tapers into the stem.
        for (int32 Row=0; Row<UE_ARRAY_COUNT(Paints); ++Row)
        {
            const double Bottom = Levels[Row]+1.5, Top = Levels[Row+1];
            Mesh.Quad(Paints[Row], Skin(-1,385,Bottom),Skin(1,385,Bottom),Skin(1,385,Top),
                Skin(-1,385,Top),{0,1,0});
            Mesh.Quad(Walnut,Skin(-1,381,Bottom),Skin(1,381,Bottom),Skin(1,381,Top),
                Skin(-1,381,Top),{0,-1,0});
        }
        Mesh.Tube(Skin(-1,385,148),Skin(1,385,148),13.,13.,Endgrain);
        Mesh.Current = Prow;
        Mesh.Tube({0,-388,-60},{0,-404,210},26.,22.,Walnut,6);
        Mesh.Tube({0,-404,210},{0,-410,307},22.,14.,Oak,6);
        Mesh.Tube({0,-406,250},{0,-407,267},24.,22.,Brass,6);
        CarvedBird(Mesh);
    }

    void Deck(FShipMesh& Mesh)
    {
        for (const double End : {-1.,1.})
        {
            Mesh.Current = End < 0. ? ForeDeck : AftDeck;
            // Lay the deck along the keel. Its outside edges follow the hull stations,
            // and the butt joints stagger across adjacent boards.
            constexpr int32 BoardCount=15;
            for (int32 Board=0; Board<BoardCount; ++Board)
            {
                const double Start=149.+(End<0. ? (Board*7)%13 : 0.);
                const double Joint=197.+((Board*37+(End>0.?23:0))%109);
                const EFinish Paint=Board%5==0 ? Saltwood : Oak;
                const auto Point=[&](double Across,double Along)
                {
                    const double Width=Skin(1.,End*Along,150.,-13.).X;
                    return FVector(Width*Across,End*Along,154.5);
                };
                const double Left=2.*Board/BoardCount-1.+.002;
                const double Right=2.*(Board+1)/BoardCount-1.-.002;
                for (int32 Piece=0; Piece<2; ++Piece)
                {
                    const double From=Piece==0 ? Start : Joint+.30;
                    const double To=Piece==0 ? Joint-.30 : 345.;
                    TArray<double,TInlineAllocator<8>> Knots;
                    Knots.Add(From);
                    for (const FStation& Station : Stations)
                    {
                        const double Along=End*Station.Y;
                        if (Along>From && Along<To) Knots.Add(Along);
                    }
                    Knots.Add(To);
                    Knots.Sort();
                    const FVector Drop(0,0,16.);
                    for (int32 K=0; K+1<Knots.Num(); ++K)
                    {
                        const FVector A=Point(Left,Knots[K]), B=Point(Right,Knots[K]);
                        const FVector C=Point(Right,Knots[K+1]), D=Point(Left,Knots[K+1]);
                        Mesh.Quad(Paint,A,B,C,D,FVector::UpVector);
                        Mesh.Quad(Walnut,A-Drop,B-Drop,C-Drop,D-Drop,-FVector::UpVector);
                        Mesh.Quad(Walnut,A-Drop,D-Drop,D,A,{-1,0,0});
                        Mesh.Quad(Walnut,B-Drop,C-Drop,C,B,{1,0,0});
                        if (K==0) Mesh.Quad(Endgrain,A-Drop,B-Drop,B,A,{0,-End,0});
                        if (K+2==Knots.Num()) Mesh.Quad(Endgrain,D-Drop,C-Drop,C,D,{0,End,0});
                    }
                }
            }
            // A short thwart spans the two ends beyond the hatch decks. Each plank
            // is visible timber and supplies the support under the standing position.
            for (int32 Slat=0; Slat<3; ++Slat)
            {
                const double Near=350.+Slat*9.8, Far=Near+9.;
                const auto Seat = [&](double Side,double Y)
                {
                    FVector P=Skin(Side,End*Y,146.,-8.);
                    P.Z=158.+(P.Z-146.)*.45;
                    return P;
                };
                const FVector A=Seat(-1,Near),B=Seat(1,Near),C=Seat(1,Far),D=Seat(-1,Far);
                const FVector Drop(0,0,12.);
                Mesh.Quad(Slat==1?Oak:Endgrain,A,B,C,D,FVector::UpVector);
                Mesh.Quad(Walnut,A-Drop,B-Drop,C-Drop,D-Drop,-FVector::UpVector);
                Mesh.Quad(Endgrain,A-Drop,B-Drop,B,A,{0,-End,0});
                Mesh.Quad(Endgrain,D-Drop,C-Drop,C,D,{0,End,0});
                Mesh.Quad(Walnut,A-Drop,D-Drop,D,A,{-1,0,0});
                Mesh.Quad(Walnut,B-Drop,C-Drop,C,B,{1,0,0});
            }
        }
    }

    void Mast(FShipMesh& Mesh)
    {
        Mesh.Current = MastAndYard;
        const FVector Heel(38,217,153), Head(67,220,589);
        Mesh.Tube(Heel,Head,36.,19.,Oak,9);
        const auto AtHeight = [&](double Z) {return FMath::Lerp(Heel,Head,(Z-Heel.Z)/(Head.Z-Heel.Z));};
        for (const double Z : {187.,202.})
            Mesh.Tube(AtHeight(Z-4.),AtHeight(Z+4.),41.,40.,Flax,10);
        const FVector YardHeel(51,74,369), YardHead(72,307,587);
        Mesh.Tube(YardHeel,YardHead,16.,10.,Walnut,7);
        const double YardCrossing=FMath::Lerp(YardHeel.Z,YardHead.Z,(220.-YardHeel.Y)/(YardHead.Y-YardHeel.Y));
        Mesh.Tube(AtHeight(YardCrossing-12.),AtHeight(YardCrossing+12.),27.,27.,Flax,8);
        for (const double Z : {YardCrossing-7.,YardCrossing+7.,555.,563.})
            Mesh.Tube(AtHeight(Z-2.5),AtHeight(Z+2.5),Z<540.?29.:23.,Z<540.?29.:23.,Brass);

        Mesh.Current = Canvas;
        // A lateen sail caught high on one side. These stations draw two large
        // shapes with a deep tear between them and a single salt-green repair.
        struct FPanelEdge { double Along, Drop, Billow, Sweep; };
        constexpr FPanelEdge Hem[]={
            {.025,10,1,0}, {.14,57,7,4}, {.28,108,15,9}, {.43,157,26,16},
            {.51,93,20,10}, {.55,26,4,2}, {.59,31,6,4}, {.64,133,23,16},
            {.77,185,34,18}, {.89,109,22,12}, {.975,17,3,1}
        };
        for (int32 I=0; I+1<UE_ARRAY_COUNT(Hem); ++I)
        {
            const auto Point=[&](const FPanelEdge& Edge, double Down)
            {
                const FVector Top=FMath::Lerp(YardHeel,YardHead,Edge.Along);
                const double Curve=Down*(1.6-.6*Down);
                return Top+FVector(Edge.Billow*Curve,Edge.Sweep*Down,-Edge.Drop*Down);
            };
            const auto Strip=[&](double High, double Low, EFinish Finish)
            {
                const FVector A=Point(Hem[I],High), B=Point(Hem[I+1],High);
                const FVector C=Point(Hem[I+1],Low), D=Point(Hem[I],Low);
                for (double Facing : {-1.,1.})
                {
                    const FVector Upper[]={A,B,C}, Lower[]={A,C,D};
                    Mesh.Face(Finish,Upper,{Facing,0,0});
                    Mesh.Face(Finish,Lower,{Facing,0,0});
                }
            };
            const EFinish Cloth=(I==5 || I==9) ? CanvasShade : Flax;
            Strip(0.,.59,Cloth);
            Strip(.59,.76,I>=7 ? Lagoon : Cloth);
            Strip(.76,1.,Cloth);
        }

        const FVector Root=AtHeight(554);
        const FVector FlagAxis(6,-81,0);
        for (double Facing : {-1.,1.})
        {
            const FVector Normal(Facing,0,0);
            const FVector A=Root+FVector(0,0,31), B=Root+FlagAxis*.55+FVector(3,0,39);
            const FVector C=Root+FlagAxis+FVector(2,0,31), D=Root+FlagAxis*.78+FVector(3,0,18);
            const FVector E=Root+FlagAxis+FVector(2,0,2), F=Root+FlagAxis*.55+FVector(3,0,9);
            Mesh.Quad(Lagoon,A,B,F,Root,Normal);
            Mesh.Quad(Flax,B,C,D,F,Normal);
            const FVector Fork[]={D,E,F};
            Mesh.Face(Flax,Fork,Normal);
        }

        Mesh.Current = Forestay;
        const FVector Stay[]={{0,-390,247},{9,-267,268},{23,-102,317},{39,59,401},AtHeight(535)};
        for (int32 I=0; I+1<UE_ARRAY_COUNT(Stay); ++I)
            Mesh.Tube(Stay[I],Stay[I+1],4.,4.,Walnut,5);
        Mesh.Current = Backstays;
        for (double Side : {-1.,1.})
        {
            const FVector Chainplate=Skin(Side,299,152,-2);
            const FVector Middle=FMath::Lerp(Chainplate,AtHeight(474),.5)-FVector(0,0,18);
            Mesh.Tube(Chainplate,Middle,4.,4.,Walnut,5);
            Mesh.Tube(Middle,AtHeight(474),4.,4.,Walnut,5);
        }
    }
}

EIslandChipSurface ExpeditionWreckArt::GetChipFinish(FName PartName, int32 Finish, FColor& Paint, FColor& Alternate)
{
    Paint = Finishes[Finish >= 0 && Finish < FinishCount ? Finish : Walnut].Color;
    if (Finish == Brass) return EIslandChipSurface::Metal;
    if (PartName == TEXT("Wreck_Canvas") || PartName == TEXT("Wreck_Forestay") || PartName == TEXT("Wreck_Backstays"))
        return EIslandChipSurface::Canvas;
    Alternate = Finishes[Endgrain].Color;
    return EIslandChipSurface::Timber;
}

void ExpeditionWreckArt::Build(AActor& Owner, UMaterialInterface* PaintBase)
{
    FShipMesh Art;
    Hull(Art);
    Deck(Art);
    Mast(Art);

    UMaterialInterface* Paints[FinishCount]{};
    if (PaintBase)
        for (int32 Slot=0; Slot<FinishCount; ++Slot)
        {
            auto* Paint = UMaterialInstanceDynamic::Create(PaintBase,&Owner);
            const FFinish& Finish=Finishes[Slot];
            Paint->SetVectorParameterValue(TEXT("Color"),FLinearColor(Finish.Color));
            Paint->SetScalarParameterValue(TEXT("Roughness"),Finish.Roughness);
            Paint->SetScalarParameterValue(TEXT("Metallic"),Finish.Metallic);
            Paints[Slot] = Paint;
        }
    for (int32 Part=0; Part<PartCount; ++Part)
    {
        const FName Name(*FString::Printf(TEXT("Wreck_%s"),PartNames[Part]));
        auto* Mesh = NewObject<UProceduralMeshComponent>(&Owner,Name);
        Mesh->SetNetAddressable();
        Mesh->SetupAttachment(Owner.GetRootComponent());
        Mesh->SetMobility(EComponentMobility::Movable);
        Mesh->ComponentTags.Add(TEXT("IslandVoxelPart"));
        Mesh->SetCollisionResponseToAllChannels(Part < Canvas ? ECR_Block : ECR_Ignore);
        Mesh->SetCollisionResponseToChannel(ECC_Camera,ECR_Ignore);
        if (Part >= Canvas) Mesh->SetCollisionResponseToChannel(ECC_Visibility,ECR_Overlap);
        Mesh->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
        Mesh->bUseComplexAsSimpleCollision = true;
        Mesh->SetGenerateOverlapEvents(false);
        Mesh->SetCanEverAffectNavigation(false);
        Mesh->SetReceivesDecals(false);
        Mesh->PrimaryComponentTick.bCanEverTick=false;
        for (int32 Slot=0; Slot<FinishCount; ++Slot)
        {
            FProcMeshSection& Section = Art.Parts[Part].Finishes[Slot];
            if (Section.ProcIndexBuffer.IsEmpty()) continue;
            Section.bEnableCollision = true;
            Mesh->SetProcMeshSection(Slot,Section);
            if (Paints[Slot]) Mesh->SetMaterial(Slot,Paints[Slot]);
        }
        Owner.AddInstanceComponent(Mesh);
        Mesh->RegisterComponent();
    }
}
