#include "Island/IslandMeshVoxelData.h"
#include "Island/IslandVoxelSourceArchive.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "StaticMeshResources.h"

namespace
{
    using FPolygon = TArray<FProcMeshVertex,TInlineAllocator<10>>;

    FProcMeshVertex Between(const FProcMeshVertex& A,const FProcMeshVertex& B,double T)
    {
        FProcMeshVertex V;
        V.Position = FMath::Lerp(A.Position,B.Position,T);
        V.Normal = FMath::Lerp(A.Normal,B.Normal,T).GetSafeNormal();
        V.Tangent = FProcMeshTangent(FMath::Lerp(A.Tangent.TangentX,B.Tangent.TangentX,T).GetSafeNormal(),A.Tangent.bFlipTangentY);
        V.UV0 = FMath::Lerp(A.UV0,B.UV0,T);
        V.Color = FMath::Lerp(A.Color.ReinterpretAsLinear(),B.Color.ReinterpretAsLinear(),T).ToFColor(false);
        return V;
    }

    // Split only where a polygon crosses a cell boundary. This also keeps thin,
    // one-sided canvas and the actual open spaces between a ship's ropes.
    void Slice(FIslandMeshVoxelData& Field,const FPolygon& Poly,int32 Material,int32 Axis)
    {
        if (Poly.Num()<3) return;
        if (Axis==3)
        {
            FVector Center(0);
            for (const auto& V : Poly) Center+=V.Position;
            const int32 I=Field.Index(Field.Cell(Center/Poly.Num()));
            for (int32 J=1; J+1<Poly.Num(); ++J)
            {
                if (FVector::CrossProduct(Poly[J].Position-Poly[0].Position,Poly[J+1].Position-Poly[0].Position).SizeSquared()<1.e-12) continue;
                Field.Skin.Add({{Poly[0],Poly[J],Poly[J+1]},Material});
                Field.SkinCells.Add(I);
                Field.Filled[I]=true;
            }
            return;
        }
        double Lo=Poly[0].Position[Axis], Hi=Lo;
        for (const auto& V:Poly) { Lo=FMath::Min(Lo,V.Position[Axis]); Hi=FMath::Max(Hi,V.Position[Axis]); }
        const double Base=Field.Box.Min[Axis], Spacing=Field.Step[Axis];
        const int32 First=FMath::FloorToInt((Lo-Base)/Spacing+1.e-7);
        const int32 Last=FMath::FloorToInt((Hi-Base)/Spacing-1.e-7);
        if (Last<=First) { Slice(Field,Poly,Material,Axis+1); return; }
        const double Plane=Base+((First+Last+1)/2)*Spacing;
        FPolygon Low,High;
        for (int32 J=0; J<Poly.Num(); ++J)
        {
            const auto& A=Poly[J]; const auto& B=Poly[(J+1)%Poly.Num()];
            const double Da=A.Position[Axis]-Plane, Db=B.Position[Axis]-Plane;
            if (Da<=0.) Low.Add(A);
            if (Da>=0.) High.Add(A);
            if ((Da<0. && Db>0.) || (Da>0. && Db<0.))
            {
                auto V=Between(A,B,Da/(Da-Db));
                V.Position[Axis]=Plane;
                Low.Add(V); High.Add(V);
            }
        }
        Slice(Field,Low,Material,Axis); Slice(Field,High,Material,Axis);
    }

    void Emit(FProcMeshSection& Section,const FIslandVoxelTri& Tri)
    {
        const int32 First=Section.ProcVertexBuffer.Num();
        for (const auto& V:Tri.V) { Section.ProcVertexBuffer.Add(V); Section.SectionLocalBox+=V.Position; }
        Section.ProcIndexBuffer.Append({uint32(First),uint32(First+1),uint32(First+2)});
    }

    // The only engine shapes used by uncooked-at-runtime scene props are the rescue
    // deck and the points of the final compass. Keep them available on cooked servers.
    bool StockShape(const UStaticMesh& Mesh,TArray<FIslandVoxelTri>& Out)
    {
        const bool Cube=Mesh.GetPathName()==TEXT("/Engine/BasicShapes/Cube.Cube");
        const bool Cone=Mesh.GetPathName()==TEXT("/Engine/BasicShapes/Cone.Cone");
        if (!Cube && !Cone) return false;
        const FBox Bounds=Mesh.GetBoundingBox();
        auto Face=[&](FVector A,FVector B,FVector C)
        {
            FIslandVoxelTri T;
            const FVector Normal=FVector::CrossProduct(C-A,B-A).GetSafeNormal();
            T.V[0].Position=A; T.V[1].Position=B; T.V[2].Position=C;
            for (auto& V:T.V) { V.Normal=Normal; V.Color=FColor::White; }
            Out.Add(T);
        };
        if (Cube)
        {
            FVector V[8];
            for (int32 I=0;I<8;++I) V[I]={I&1?Bounds.Max.X:Bounds.Min.X,I&2?Bounds.Max.Y:Bounds.Min.Y,I&4?Bounds.Max.Z:Bounds.Min.Z};
            const int32 Faces[][4]={{0,1,3,2},{4,6,7,5},{0,4,5,1},{2,3,7,6},{0,2,6,4},{1,5,7,3}};
            for(const auto& Q:Faces) { Face(V[Q[0]],V[Q[1]],V[Q[2]]); Face(V[Q[0]],V[Q[2]],V[Q[3]]); }
        }
        else
        {
            const FVector C=Bounds.GetCenter(), E=Bounds.GetExtent(), Base(C.X,C.Y,Bounds.Min.Z), Tip(C.X,C.Y,Bounds.Max.Z);
            for (int32 J=0;J<24;++J)
            {
                const double A=UE_TWO_PI*J/24., B=UE_TWO_PI*(J+1)/24.;
                const FVector P=Base+FVector(E.X*FMath::Cos(A),E.Y*FMath::Sin(A),0), Q=Base+FVector(E.X*FMath::Cos(B),E.Y*FMath::Sin(B),0);
                Face(Q,P,Tip); Face(Base,P,Q);
            }
        }
        return true;
    }
}

bool ReadIslandVoxelSource(UPrimitiveComponent& Source,TArray<FIslandVoxelTri>& Triangles)
{
    if (auto* Proc=Cast<UProceduralMeshComponent>(&Source))
    {
        for (int32 Mat=0;Mat<Proc->GetNumSections();++Mat)
        {
            const auto* S=Proc->GetProcMeshSection(Mat);
            if (!S) continue;
            for (int32 I=0;I+2<S->ProcIndexBuffer.Num();I+=3)
                Triangles.Add({{S->ProcVertexBuffer[S->ProcIndexBuffer[I]],S->ProcVertexBuffer[S->ProcIndexBuffer[I+1]],S->ProcVertexBuffer[S->ProcIndexBuffer[I+2]]},Mat});
        }
        return !Triangles.IsEmpty();
    }
    const auto* Part=Cast<UStaticMeshComponent>(&Source);
    const UStaticMesh* Asset=Part ? Part->GetStaticMesh().Get() : nullptr;
    if (!Asset) return false;
    if (StockShape(*Asset,Triangles)) return true;
    if (FPlatformProperties::RequiresCookedData() && ReadIslandVoxelArchive(*Asset,Triangles)) return true;
    return ReadIslandVoxelRenderSource(*Asset,Triangles);
}

bool ReadIslandVoxelRenderSource(const UStaticMesh& Asset,TArray<FIslandVoxelTri>& Triangles)
{
    const auto* Render=Asset.GetRenderData();
    if (!Render || Render->LODResources.IsEmpty() || (!GIsEditor && !Asset.bAllowCPUAccess)) return false;
    const auto& Lod=Render->LODResources[0];
    const auto Indices=Lod.IndexBuffer.GetArrayView();
    const auto& Buffers=Lod.VertexBuffers;
    if (Indices.Num()==0) return false;
    for (const auto& S:Lod.Sections)
    {
        for (uint32 J=0;J<S.NumTriangles;++J)
        {
            FIslandVoxelTri T;
            T.Material=S.MaterialIndex;
            for (int32 K=0;K<3;++K)
            {
                const uint32 V=Indices[S.FirstIndex+J*3+K];
                auto& D=T.V[K];
                D.Position=FVector(Buffers.PositionVertexBuffer.VertexPosition(V));
                D.Normal=FVector(Buffers.StaticMeshVertexBuffer.VertexTangentZ(V));
                D.Tangent=FProcMeshTangent(FVector(Buffers.StaticMeshVertexBuffer.VertexTangentX(V)),false);
                if (Buffers.StaticMeshVertexBuffer.GetNumTexCoords()) D.UV0=FVector2D(Buffers.StaticMeshVertexBuffer.GetVertexUV(V,0));
                D.Color=Buffers.ColorVertexBuffer.GetNumVertices()>V ? Buffers.ColorVertexBuffer.VertexColor(V) : FColor::White;
            }
            Triangles.Add(T);
        }
    }
    return !Triangles.IsEmpty();
}

TSharedRef<FIslandMeshVoxelData> FIslandMeshVoxelData::Create()
{
    return MakeShared<FIslandMeshVoxelData>();
}

FIntVector FIslandMeshVoxelData::Cell(FVector P) const
{
    const FVector Q=(P-Box.Min)/Step;
    return FIntVector(FMath::FloorToInt(Q.X),FMath::FloorToInt(Q.Y),FMath::FloorToInt(Q.Z)).ComponentMax(FIntVector::ZeroValue).ComponentMin(Size-FIntVector(1));
}

void FIslandMeshVoxelData::VisitBox(FBox Local,TFunctionRef<void(int32)> Visit) const
{
    if (!Box.Intersect(Local)) return;
    const auto A=Cell(Local.Min), B=Cell(Local.Max);
    for (int32 Z=A.Z;Z<=B.Z;++Z) for (int32 Y=A.Y;Y<=B.Y;++Y) for (int32 X=A.X;X<=B.X;++X)
        Visit(Index({X,Y,Z}));
}

void FIslandMeshVoxelData::Build(FVector WorldScale)
{
    for (const auto& T:Source) for (const auto& V:T.V) Box+=V.Position;
    Box=Box.ExpandBy(.003);
    const FVector Extent=Box.GetSize(), WorldExtent=Extent*WorldScale.GetAbs();
    // Small hardware has its own grid. Large beams, rocks and decks use cells up to 25 cm.
    const double Pitch=FMath::Clamp(WorldExtent.GetMax()/12.,3.,25.);
    for (int32 Axis=0;Axis<3;++Axis) Size[Axis]=FMath::Clamp(FMath::CeilToInt(WorldExtent[Axis]/Pitch),1,144);
    Step=Extent/FVector(Size);
    Filled.Init(false,Size.X*Size.Y*Size.Z);
    Missing.Init(false,Filled.Num());
    // Intersections along X can be paired for closed solids. The skin itself also
    // occupies cells, so thin sheets remain diggable without inflating their surface.
    TArray<TArray<float>> Crossings;
    Crossings.SetNum(Size.Y*Size.Z);
    for (const auto& T:Source)
    {
        FaceStarts.Add(Skin.Num());
        FPolygon Polygon={T.V[0],T.V[1],T.V[2]};
        Slice(*this,Polygon,T.Material,0);
        const FVector A=T.V[0].Position, U=T.V[1].Position-A, V=T.V[2].Position-A;
        const double Det=U.Y*V.Z-U.Z*V.Y;
        if (FMath::Abs(Det)<1.e-9) continue;
        FBox TriBox(ForceInit); for(const auto& P:T.V) TriBox+=P.Position;
        const auto Low=Cell(TriBox.Min), High=Cell(TriBox.Max);
        for(int32 Z=Low.Z;Z<=High.Z;++Z) for(int32 Y=Low.Y;Y<=High.Y;++Y)
        {
            const double Dy=Box.Min.Y+(Y+.500017)*Step.Y-A.Y, Dz=Box.Min.Z+(Z+.500013)*Step.Z-A.Z;
            const double S=(Dy*V.Z-Dz*V.Y)/Det, R=(U.Y*Dz-U.Z*Dy)/Det;
            if(S>=0. && R>=0. && S+R<1.) Crossings[Y+Z*Size.Y].Add(float(A.X+S*U.X+R*V.X));
        }
    }
    FaceStarts.Add(Skin.Num());
    for(int32 Z=0;Z<Size.Z;++Z) for(int32 Y=0;Y<Size.Y;++Y)
    {
        auto& Hits=Crossings[Y+Z*Size.Y]; Hits.Sort();
        if (Hits.Num()%2) continue; // open paint and rope shells have only their sampled skin
        for(int32 J=0;J+1<Hits.Num();J+=2)
        {
            const int32 Lo=FMath::Max(0,FMath::CeilToInt((Hits[J]-Box.Min.X)/Step.X-.5));
            const int32 Hi=FMath::Min(Size.X-1,FMath::FloorToInt((Hits[J+1]-Box.Min.X)/Step.X-.5));
            for(int32 X=Lo;X<=Hi;++X) Filled[Index({X,Y,Z})]=true;
        }
    }
    Total=Remaining=Filled.CountSetBits();
    CellPaint.Init(INDEX_NONE,Filled.Num());
    TArray<int32> Queue;
    for(int32 J=0;J<SkinCells.Num();++J)
    {
        const int32 I=SkinCells[J];
        if(CellPaint[I]==INDEX_NONE) { CellPaint[I]=J; Queue.Add(I); }
    }
    // Propagate the boundary pigment into the solid only once. Subsequent edits can
    // cap thousands of exposed cells without rescanning a ship panel's triangles.
    for(int32 J=0;J<Queue.Num();++J)
    {
        const int32 I=Queue[J];
        for(int32 Axis=0;Axis<3;++Axis) for(int32 Sign:{-1,1})
        {
            auto Q=XYZ(I); Q[Axis]+=Sign;
            if(!Inside(Q)) continue;
            const int32 K=Index(Q);
            if(Filled[K] && CellPaint[K]==INDEX_NONE) { CellPaint[K]=CellPaint[I]; Queue.Add(K); }
        }
    }
}

void FIslandMeshVoxelData::Mesh(TArray<FProcMeshSection>& Sections) const
{
    for (const auto& T:Source) if(Sections.Num()<=T.Material) Sections.SetNum(T.Material+1);
    if (Total==Remaining) { for(const auto& T:Source) Emit(Sections[T.Material],T); return; }
    if (!Remaining) return;
    for(int32 Face=0;Face<Source.Num();++Face)
    {
        const int32 First=FaceStarts[Face], End=FaceStarts[Face+1];
        bool Whole=true;
        for(int32 J=First;J<End && Whole;++J) Whole=Live(SkinCells[J]);
        if(Whole) Emit(Sections[Source[Face].Material],Source[Face]);
        else for(int32 J=First;J<End;++J) if(Live(SkinCells[J])) Emit(Sections[Skin[J].Material],Skin[J]);
    }
    const FIntVector Axes[]={{1,0,0},{0,1,0},{0,0,1}};
    for (TConstSetBitIterator<> Removed(Missing);Removed;++Removed)
    {
        const auto CellPos=XYZ(Removed.GetIndex());
        for(int32 A=0;A<3;++A) for(int32 S:{-1,1})
        {
            const auto Neighbour=CellPos+Axes[A]*S;
            if(!Inside(Neighbour) || !Live(Index(Neighbour))) continue;
            const FVector N=FVector(Axes[A])*(-S);
            const FVector C=Center(Index(Neighbour))+N*Step*.5;
            const int32 B=(A+1)%3, D=(A+2)%3;
            const FVector U=FVector(Axes[B])*Step[B]*.5, V=FVector(Axes[D])*Step[D]*.5;
            FVector Corners[]={C-U-V,C+U-V,C+U+V,C-U+V};
            FIslandVoxelTri T;
            const int32 Paint=CellPaint[Index(Neighbour)];
            if(Skin.IsValidIndex(Paint))
            {
                T.Material=Skin[Paint].Material;
                for(auto& Vertex:T.V) Vertex.Color=Skin[Paint].V[0].Color;
            }
            for(int32 J=0;J<2;++J)
            {
                const int32 Order[]={0,J+1,J+2};
                for(int32 K=0;K<3;++K)
                {
                    auto& P=T.V[K];
                    P.Position=Corners[Order[S<0 ? 2-K : K]]; P.Normal=N;
                    P.Tangent=FProcMeshTangent(U.GetSafeNormal(),false); P.UV0={P.Position[B]*.01,P.Position[D]*.01};
                }
                Emit(Sections[T.Material],T);
            }
        }
    }
}
