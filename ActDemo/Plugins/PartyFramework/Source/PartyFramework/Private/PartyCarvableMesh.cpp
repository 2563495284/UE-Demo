#include "PartyCarvableMesh.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"

UPartyCarvableMesh* UPartyCarvableMesh::Create(UStaticMeshComponent& Original,double MaxPitch)
{
    auto Data=FIslandMeshVoxelData::Create();
    if (!ReadIslandVoxelSource(Original,Data->Source)) return nullptr;
    Data->Build(Original.GetComponentScale(),MaxPitch);
    if (!Data->Remaining) return nullptr;
    // CharacterMovement replicates the floor component. The level actor and authored
    // component have stable names, so remote peers resolve the same carved floor.
    const FName Name(*(Original.GetName()+TEXT("_Carved")));
    auto* Result=NewObject<UPartyCarvableMesh>(Original.GetOwner(),Name);
    Result->SetNetAddressable();
    Result->Authored=&Original;
    Result->Grid=Data;
    Result->SetupAttachment(&Original);
    Result->SetMobility(Original.Mobility);
    Result->ComponentTags=Original.ComponentTags;
    Result->SetCanEverAffectNavigation(false);
    Result->bUseComplexAsSimpleCollision=true;
    Result->bUseAsyncCooking=false;
    Result->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Result->SetCollisionResponseToAllChannels(ECR_Block);
    Original.GetOwner()->AddInstanceComponent(Result);
    Result->RegisterComponent();
    for (int32 I=0;I<Original.GetNumMaterials();++I) Result->SetMaterial(I,Original.GetMaterial(I));
    return Result;
}

bool UPartyCarvableMesh::IsSurface(const UPrimitiveComponent* Surface) const
{
    return Surface==this || Surface==Authored;
}

FBox UPartyCarvableMesh::CellBounds(int32 ID) const
{
    return FBox::BuildAABB(Grid->Center(ID),Grid->Step*.5).TransformBy(GetComponentTransform());
}

void UPartyCarvableMesh::SelectBrush(FVector Point,FVector Normal,double Radius,double Depth,bool bRestore,TArray<int32>& Out) const
{
    const FTransform ToWorld=GetComponentTransform();
    const FBox Search=FBox::BuildAABB(Point,FVector(Radius+Depth+10.)).TransformBy(ToWorld.Inverse());
    Grid->VisitBox(Search,[&](int32 I)
    {
        if (!Grid->Filled[I] || Grid->Missing[I]!=bRestore) return;
        const FVector Offset=ToWorld.TransformPosition(Grid->Center(I))-Point;
        const double Along=Offset.Dot(Normal);
        if (Along < -Depth || Along > (bRestore ? Depth : 8.)) return;
        if ((Offset-Normal*Along).SizeSquared()<=Radius*Radius) Out.Add(I);
    });
}

bool UPartyCarvableMesh::SetCells(TConstArrayView<int32> Cells,bool bRestore)
{
    int32 Changed=0;
    for (int32 I:Cells)
    {
        if (!Grid->Filled.IsValidIndex(I) || !Grid->Filled[I] || Grid->Missing[I]!=bRestore) continue;
        Grid->Missing[I]=!bRestore;
        Grid->Remaining+=bRestore ? 1 : -1;
        ++Changed;
    }
    if (Changed) Rebuild();
    return Changed>0;
}

void UPartyCarvableMesh::Rebuild()
{
    Authored->SetVisibility(false);
    Authored->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    TArray<FProcMeshSection> Sections;
    Grid->Mesh(Sections);
    ClearAllMeshSections();
    for (int32 I=0;I<Sections.Num();++I)
        if (Sections[I].ProcIndexBuffer.Num())
        {
            Sections[I].bEnableCollision=true;
            SetProcMeshSection(I,Sections[I]);
        }
    SetVisibility(Grid->Remaining>0);
    SetCollisionEnabled(Grid->Remaining ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);
}

bool UPartyCarvableMesh::FirstMissing(FVector From,FVector Direction,double Range,FVector& Point,double& Distance) const
{
    if (Grid->Remaining==Grid->Total) return false;
    const FTransform& Transform=GetComponentTransform();
    const FVector Start=Transform.InverseTransformPosition(From);
    const FVector Delta=Transform.InverseTransformVector(Direction)*Range;
    FVector Enter,Normal;
    float Fraction=0.f;
    if (!Grid->Box.IsInsideOrOn(Start) &&
        !FMath::LineExtentBoxIntersection(Grid->Box,Start,Start+Delta,FVector::ZeroVector,Enter,Normal,Fraction)) return false;
    for (double T=FMath::Max(0.,Range*Fraction+.1); T<=Range; T+=2.)
    {
        const FVector P=Start+Delta*(T/Range);
        if (!Grid->Box.IsInsideOrOn(P)) break;
        const int32 ID=Grid->Index(Grid->Cell(P));
        if (!Grid->Filled[ID]) continue;
        if (!Grid->Missing[ID]) return false;
        Distance=T; Point=From+Direction*T; return true;
    }
    return false;
}

void UPartyCarvableMesh::DropUnsupported(TConstArrayView<UPartyCarvableMesh*> Parts,TFunctionRef<bool(FVector)> IsGround)
{
    TArray<TBitArray<>> Kept;
    TArray<FIntPoint> Queue;
    TArray<FBox> Boxes;
    for (int32 P=0;P<Parts.Num();++P)
    {
        auto& Field=*Parts[P];
        auto& Data=Field.Data();
        auto& Seen=Kept.AddDefaulted_GetRef();
        Seen.Init(false,Data.Filled.Num());
        Boxes.Add(Data.Box.TransformBy(Field.GetComponentTransform()).ExpandBy(2.5));
        for (TConstSetBitIterator<> Bit(Data.Filled);Bit;++Bit)
        {
            const int32 I=Bit.GetIndex();
            if (!Data.Live(I)) continue;
            const FBox B=Field.CellBounds(I);
            const FVector Center=B.GetCenter();
            if (IsGround(FVector(Center.X,Center.Y,B.Min.Z-3.))) { Seen[I]=true; Queue.Add({P,I}); }
        }
    }
    static const FIntVector Directions[]={{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}};
    for (int32 H=0;H<Queue.Num();++H)
    {
        const FIntPoint At=Queue[H];
        auto& Field=*Parts[At.X];
        const auto& Data=Field.Data();
        const FIntVector C=Data.XYZ(At.Y);
        auto Visit=[&](int32 P,int32 ID)
        {
            if (!Kept[P][ID] && Parts[P]->Data().Live(ID)) { Kept[P][ID]=true; Queue.Add({P,ID}); }
        };
        for (FIntVector D:Directions) if (Data.Inside(C+D)) Visit(At.X,Data.Index(C+D));
        const FBox Bounds=Field.CellBounds(At.Y).ExpandBy(2.5);
        for (int32 P=0;P<Parts.Num();++P)
        {
            if (P==At.X || !Boxes[P].Intersect(Bounds)) continue;
            Parts[P]->Data().VisitBox(Bounds.TransformBy(Parts[P]->GetComponentTransform().Inverse()),[&](int32 ID)
            {
                if (Parts[P]->CellBounds(ID).Intersect(Bounds)) Visit(P,ID);
            });
        }
    }
    for (int32 P=0;P<Parts.Num();++P)
    {
        auto& Data=Parts[P]->Data();
        TArray<int32> Loose;
        for (TConstSetBitIterator<> I(Data.Filled);I;++I)
            if (Data.Live(I.GetIndex()) && !Kept[P][I.GetIndex()]) Loose.Add(I.GetIndex());
        Parts[P]->SetCells(Loose,false);
    }
}
