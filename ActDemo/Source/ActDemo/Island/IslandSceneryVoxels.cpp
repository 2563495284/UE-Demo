#include "Island/IslandSceneryVoxels.h"
#include "Island/IslandMeshVoxelData.h"
#include "Island/IslandScenerySupport.h"
#include "Island/IslandBreakable.h"
#include "Island/ExpeditionDock.h"
#include "Island/IslandNeutralProp.h"
#include "Island/IslandWorld.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/App.h"
#include "Net/UnrealNetwork.h"
#include "UObject/ConstructorHelpers.h"
#include "Voxel/VoxelTerrain.h"

namespace
{
    UIslandMeshVoxels* FieldFor(UPrimitiveComponent* Part)
    {
        if (!Part) return nullptr;
        if (auto* Grid=Cast<UIslandMeshVoxels>(Part)) return Grid;
        for (USceneComponent* Child:Part->GetAttachChildren())
            if (auto* Grid=Cast<UIslandMeshVoxels>(Child)) return Grid;
        return nullptr;
    }

    AIslandVoxelEdits* Edits(UWorld* World)
    {
        if (!World) return nullptr;
        TActorIterator<AIslandVoxelEdits> It(World);
        return It ? *It : nullptr;
    }

    bool IsMainIsland(const UWorld* World)
    {
        return World && World->GetMapName().EndsWith(TEXT("L_ExpeditionIsland"));
    }

    void UpdateSign(AActor& Actor)
    {
        TInlineComponentArray<UIslandMeshVoxels*> Fields(&Actor);
        if (Fields.IsEmpty() || Fields.ContainsByPredicate([](auto* F){return F->RemainingCount()>0;})) return;
        TInlineComponentArray<UTextRenderComponent*> Signs(&Actor);
        for (auto* Sign:Signs) Sign->SetVisibility(false);
    }
}

UIslandMeshVoxels::UIslandMeshVoxels(const FObjectInitializer& Initializer):Super(Initializer)
{
    SetCanEverAffectNavigation(false);
    SetGenerateOverlapEvents(false);
    bUseAsyncCooking=false;
    bUseComplexAsSimpleCollision=true;
}

bool UIslandMeshVoxels::Initialize(UPrimitiveComponent& Source)
{
    Field=FIslandMeshVoxelData::Create();
    if (!ReadIslandVoxelSource(Source,Field->Source))
    {
        UE_LOG(LogTemp,Error,TEXT("SCENERY_VOXEL_SOURCE_MISSING owner=%s source=%s"),*GetNameSafe(Source.GetOwner()),*Source.GetName());
        return false;
    }
    FVector ReferenceScale=Source.GetComponentScale();
    if(Source.ComponentHasTag(TEXT("IslandSailPanel"))) ReferenceScale.Z=2.51*Source.GetOwner()->GetActorScale3D().Z;
    Field->Build(ReferenceScale);
    if (!Field->Remaining) return false;
    Original=&Source;
    SourceCollision=Source.GetCollisionEnabled();
    SourceResponses=Source.GetCollisionResponseToChannels();
    SetCollisionObjectType(Source.GetCollisionObjectType());
    SetupAttachment(&Source);
    SetMobility(Source.Mobility);
    SetNetAddressable();
    SetCastShadow(Source.CastShadow);
    RegisterComponent();
    Rebuild();
    Source.SetRenderInMainPass(false);
    Source.SetRenderInDepthPass(false);
    Source.SetVisibleInRayTracing(false);
    Source.SetCastShadow(false);
    RefreshSource();
    return true;
}

void UIslandMeshVoxels::RefreshSource()
{
    if (!Original || !Field) return;
    // Animated props may have just changed their real collision profile. A parked
    // source mesh normally has no collision; the voxel surface owns the queries.
    if (Original->GetCollisionEnabled()!=ECollisionEnabled::NoCollision)
    {
        SourceCollision=Original->GetCollisionEnabled();
        SourceResponses=Original->GetCollisionResponseToChannels();
        Original->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    }
    for (int32 I=0;I<Original->GetNumMaterials();++I)
        if (GetMaterial(I)!=Original->GetMaterial(I)) SetMaterial(I,Original->GetMaterial(I));
    const bool Visible=Original->GetVisibleFlag() && !Original->bHiddenInGame && RemainingCount()>0;
    SetVisibility(Visible);
    SetCollisionResponseToChannels(SourceResponses);
    if (SourceCollision==ECollisionEnabled::NoCollision || GetCollisionResponseToChannel(ECC_Visibility)==ECR_Ignore)
    {
        if (SourceCollision==ECollisionEnabled::NoCollision) SetCollisionResponseToAllChannels(ECR_Ignore);
        SetCollisionResponseToChannel(ECC_Visibility,GetOwner()->IsA<AIslandChest>() ? ECR_Block : ECR_Overlap);
    }
    SetCollisionEnabled(Visible ? ECollisionEnabled::QueryOnly : ECollisionEnabled::NoCollision);
}

int32 UIslandMeshVoxels::RemainingCount() const {return Field ? Field->Remaining : 0;}
int32 UIslandMeshVoxels::InitialCount() const {return Field ? Field->Total : 0;}

uint32 UIslandMeshVoxels::Digest() const
{
    if (!Field) return 0;
    uint32 Hash=HashCombineFast(Field->Total,Field->Remaining);
    for(TConstSetBitIterator<> I(Field->Missing);I;++I) Hash=HashCombineFast(Hash,I.GetIndex()+1);
    return Hash;
}

bool UIslandMeshVoxels::IsSolidWorld(FVector World) const
{
    if (!Field) return false;
    const FVector Local=GetComponentTransform().InverseTransformPosition(World);
    return Field->Box.IsInsideOrOn(Local) && Field->Live(Field->Index(Field->Cell(Local)));
}

void UIslandMeshVoxels::GetCellAxes(FVector& A,FVector& B,FVector& C) const
{
    const FVector H=Field ? Field->Step*.5 : FVector::ZeroVector;
    A=GetComponentTransform().TransformVector({H.X,0,0});
    B=GetComponentTransform().TransformVector({0,H.Y,0});
    C=GetComponentTransform().TransformVector({0,0,H.Z});
}

double UIslandMeshVoxels::CellRadius() const
{
    return Field ? (Field->Step*GetComponentScale()).Size()*.5 : 0.;
}

void UIslandMeshVoxels::ForEachSolid(TFunctionRef<void(int32,FVector)> Visit) const
{
    if (!Field) return;
    for(TConstSetBitIterator<> I(Field->Filled);I;++I)
        if (!Field->Missing[I.GetIndex()]) Visit(I.GetIndex(),GetComponentTransform().TransformPosition(Field->Center(I.GetIndex())));
}

void UIslandMeshVoxels::GetCellNeighbors(int32 I,TArray<int32>& Out) const
{
    Out.Reset();
    if (!Field || !Field->Live(I)) return;
    const FIntVector P=Field->XYZ(I);
    for(int32 Axis=0;Axis<3;++Axis) for(int32 Sign:{-1,1})
    {
        auto Q=P; Q[Axis]+=Sign;
        if(Field->Inside(Q) && Field->Live(Field->Index(Q))) Out.Add(Field->Index(Q));
    }
}

void UIslandMeshVoxels::FindOccupiedNear(FVector World,double Radius,TArray<int32>& Out) const
{
    Out.Reset();
    if (!Field) return;
    const auto& Xform=GetComponentTransform();
    const FVector Local=Xform.InverseTransformPosition(World);
    const FVector Scale=Xform.GetScale3D().GetAbs().ComponentMax(FVector(.00001));
    Field->VisitBox(FBox::BuildAABB(Local,FVector(Radius)/Scale),[&](int32 I)
    {
        if(Field->Live(I) && ((Field->Center(I)-Local)*Scale).SizeSquared()<=Radius*Radius) Out.Add(I);
    });
}

void UIslandMeshVoxels::Rebuild()
{
    if (!Field) return;
    TArray<FProcMeshSection> Sections;
    Field->Mesh(Sections);
    if(Sections.IsEmpty()) return;
    const int32 Last=Sections.Num()-1;
    if(GetNumSections()<Sections.Num()) SetProcMeshSection(Last,FProcMeshSection());
    for(int32 I=0;I<Last;++I)
    {
        Sections[I].bEnableCollision=true;
        *GetProcMeshSection(I)=MoveTemp(Sections[I]);
    }
    Sections[Last].bEnableCollision=true;
    SetProcMeshSection(Last,Sections[Last]); // publishes bounds, render data and a single physics cook
}

bool UIslandMeshVoxels::RemoveCells(TConstArrayView<int32> Ids)
{
    if (!Field) return false;
    const int32 Prev=Field->Remaining;
    for(int32 I:Ids) if(Field->Live(I)) { Field->Missing[I]=true; --Field->Remaining; }
    if(Prev==Field->Remaining) return false;
    Rebuild();
    RefreshSource();
    return true;
}

bool UIslandMeshVoxels::Cut(FVector World,FVector Normal,bool bWide)
{
    if (!RemainingCount()) return false;
    const FVector Probe=World-Normal.GetSafeNormal()*1.;
    const double Rad=bWide?90.:57.;
    const double Depth=bWide?70.:43.;
    const FVector Local=GetComponentTransform().InverseTransformPosition(Probe);
    const FVector Scale=GetComponentScale().GetAbs().ComponentMax(FVector(.00001));
    const double Half=CellRadius();
    TArray<int32> Cells;
    Field->VisitBox(FBox::BuildAABB(Local,FVector(Rad+Depth+Half)/Scale),[&](int32 I)
    {
        if(!Field->Live(I)) return;
        const FVector D=GetComponentTransform().TransformPosition(Field->Center(I))-Probe;
        const double Along=FVector::DotProduct(D,Normal);
        if(Along>=-Depth-Half && Along<=Half+2. && (D-Normal*Along).SizeSquared()<=FMath::Square(Rad)) Cells.Add(I);
    });
    return RemoveCells(Cells);
}

int32 UIslandMeshVoxels::GetImpactPaint(const FHitResult& Hit, FColor& VertexColor)
{
    VertexColor=FColor::White;
    int32 Face=Hit.FaceIndex;
    if (Face>=0)
        for (int32 Slot=0; Slot<GetNumSections(); ++Slot)
        {
            const FProcMeshSection* Section=GetProcMeshSection(Slot);
            if (!Section) continue;
            const int32 Triangles=Section->ProcIndexBuffer.Num()/3;
            if (Face<Triangles)
            {
                VertexColor=Section->ProcVertexBuffer[Section->ProcIndexBuffer[3*Face]].Color;
                return Slot;
            }
            Face-=Triangles;
        }
    if (!Field) return INDEX_NONE;
    const FVector Local=GetComponentTransform().InverseTransformPosition(Hit.ImpactPoint-Hit.ImpactNormal);
    const int32 Cell=Field->Index(Field->Cell(Local));
    const int32 Paint=Field->CellPaint[Cell];
    if (!Field->Skin.IsValidIndex(Paint)) return INDEX_NONE;
    VertexColor=Field->Skin[Paint].V[0].Color;
    return Field->Skin[Paint].Material;
}

bool UIslandMeshVoxels::GetSurfaceTarget(FVector& Point,FVector& Normal) const
{
    if (!Field || !Field->Remaining || !IsVisible()) return false;
    // Prefer a side or a top face and confirm the physical hit on this remaining shape.
    for(int32 J=0;J<Field->Skin.Num();++J)
    {
        if (!Field->Live(Field->SkinCells[J])) continue;
        const auto& T=Field->Skin[J];
        const auto& Xform=GetComponentTransform();
        const FVector A=Xform.TransformPosition(T.V[0].Position), B=Xform.TransformPosition(T.V[1].Position), C=Xform.TransformPosition(T.V[2].Position);
        Point=(A+B+C)/3.;
        Normal=Xform.TransformVectorNoScale(T.V[0].Normal).GetSafeNormal();
        if(Normal.Z<-.3) continue;
        return true;
    }
    for(int32 S=0;S<GetNumSections();++S)
    {
        const auto* Sec=const_cast<UIslandMeshVoxels*>(this)->GetProcMeshSection(S);
        if(!Sec || Sec->ProcIndexBuffer.Num()<3) continue;
        const auto& V=Sec->ProcVertexBuffer[Sec->ProcIndexBuffer[0]];
        Point=GetComponentTransform().TransformPosition(V.Position);
        Normal=GetComponentTransform().TransformVectorNoScale(V.Normal).GetSafeNormal();
        return true;
    }
    return false;
}

void UIslandMeshVoxels::PackRemoved(TArray<uint8>& Data) const
{
    Data.Reset();
    if(!Field) return;
    // Complete loss is one byte; partially damaged grids use contiguous occupied-index runs.
    if(!RemainingCount()) { Data.Add(1); return; }
    Data.Add(0);
    auto Put=[&](uint32 V){while(V>=128){Data.Add((V&127)|128); V>>=7;} Data.Add(uint8(V));};
    int32 Last=0, First=INDEX_NONE, End=0;
    auto Flush=[&](){if(First!=INDEX_NONE){Put(First-Last);Put(End-First); Last=End;}};
    for(TConstSetBitIterator<> It(Field->Missing);It;++It)
    {
        const int32 I=It.GetIndex();
        if(I!=End || First==INDEX_NONE){ Flush(); First=I; }
        End=I+1;
    }
    Flush();
}

void UIslandMeshVoxels::ApplyRemoved(const TArray<uint8>& Data)
{
    if(!Field || Data.IsEmpty()) return;
    TArray<int32> Ids;
    if(Data[0]==1)
    {
        for(TConstSetBitIterator<> I(Field->Filled);I;++I) if(Field->Live(I.GetIndex())) Ids.Add(I.GetIndex());
    }
    else
    {
        int32 Pos=1, Prev=0;
        auto Get=[&](uint32& V)
        {
            V=0;
            for(int32 Shift=0;Shift<=28 && Pos<Data.Num();Shift+=7)
            {
                const uint8 B=Data[Pos++]; V|=uint32(B&127)<<Shift; if(!(B&128)) return true;
            }
            return false;
        };
        while(Pos<Data.Num())
        {
            uint32 Gap,Len;
            if(!Get(Gap)||!Get(Len)||int64(Prev)+Gap+Len>Field->Filled.Num()) return;
            const int32 Start=Prev+Gap; Prev=Start+Len;
            for(int32 I=Start;I<Prev;++I) if(Field->Live(I)) Ids.Add(I);
        }
    }
    RemoveCells(Ids);
}

void IslandSceneryVoxels::Register(AActor& Actor)
{
    if (!IsMainIsland(Actor.GetWorld())) return;
    const bool Neutral=Actor.IsA<AIslandNeutralProp>();
    TInlineComponentArray<UPrimitiveComponent*> Primitives(&Actor);
    int32 Added=0;
    for(auto* Part:Primitives)
    {
        if(Part->IsA<UIslandMeshVoxels>() || FieldFor(Part) || (!Part->IsA<UStaticMeshComponent>() && !Part->IsA<UProceduralMeshComponent>())) continue;
        if(Neutral && !Part->ComponentHasTag(TEXT("IslandVoxelPart"))) continue;
        const auto* Static=Cast<UStaticMeshComponent>(Part);
        if(Static && !Static->GetStaticMesh()) continue;
        if(!Neutral && !Part->GetVisibleFlag()) continue; // excludes the chest's interaction helper
        auto* Field=NewObject<UIslandMeshVoxels>(&Actor,FName(*(TEXT("Volume_")+Part->GetName())));
        if(!Field->Initialize(*Part)) {Field->DestroyComponent();continue;}
        Actor.AddInstanceComponent(Field);
        AIslandVoxelEdits::Restore(*Field);
        ++Added;
    }
    if(Added) UE_LOG(LogTemp,Verbose,TEXT("SCENERY_VOXEL_BIND actor=%s parts=%d"),*Actor.GetName(),Added);
}

UPrimitiveComponent* IslandSceneryVoxels::SourceFor(UPrimitiveComponent* Surface)
{
    auto* Grid=Cast<UIslandMeshVoxels>(Surface);
    return Grid?Grid->SourcePart():Surface;
}

UPrimitiveComponent* IslandSceneryVoxels::SurfaceFor(UPrimitiveComponent* Source)
{
    if(auto* Grid=FieldFor(Source)) return Grid;
    return Source;
}

bool IslandSceneryVoxels::IsExhausted(UPrimitiveComponent* Source)
{
    const auto* Grid=FieldFor(Source);
    return Grid && !Grid->RemainingCount();
}

void IslandSceneryVoxels::RefreshPart(UPrimitiveComponent& Source)
{
    if(auto* Grid=FieldFor(&Source)) Grid->RefreshSource();
}

namespace
{
    EIslandChipSurface ImpactFinish(const AActor& Owner,const UPrimitiveComponent& Source, int32 Slot, FColor& Paint)
    {
        using enum EIslandChipSurface;
        // Harbour vertex paint stores display RGB, with the physical finish in alpha.
        // Coffer, raft and neutral pieces instead multiply linear vertex pigment by a material tint.
        const auto* Material=Slot>=0 ? Source.GetMaterial(Slot) : nullptr;
        FLinearColor Tint;
        const uint8 FinishCode=Paint.A;
        if (Material && Material->GetVectorParameterValue(FMaterialParameterInfo(TEXT("Color")),Tint))
        {
            Paint=(Tint*Paint.ReinterpretAsLinear()).ToFColor(true);
            float Metalness=0.f;
            Material->GetScalarParameterValue(FMaterialParameterInfo(TEXT("Metallic")),Metalness);
            if (Metalness>.32f || FinishCode<=115) return Metal;
            if (Source.ComponentHasTag(TEXT("IslandChipCanvas"))) return Canvas;
            if (Source.ComponentHasTag(TEXT("IslandChipLeaf"))) return Leaf;
            if (Source.ComponentHasTag(TEXT("IslandChipRock"))) return Rock;
            if (Source.ComponentHasTag(TEXT("IslandChipSand"))) return Sand;
            if (Owner.IsA<AIslandChest>() && Source.GetFName()!=TEXT("Body") && Source.GetFName()!=TEXT("Lid")) return Metal;
            return Timber;
        }
        if (FinishCode<=125) return Metal;
        if (FinishCode>=241 && FinishCode<254) return Canvas;
        if (Owner.IsA<AExpeditionDock>() || (FinishCode>=199 && FinishCode<=221)) return Timber;
        const auto* Landmark=Cast<AExpeditionLandmark>(&Owner);
        if (Landmark && Landmark->ClueIndex==2 && FinishCode<180) return Timber; // painted quarry derrick
        return Rock;
    }

    void GroundActor(AActor& Actor,const AVoxelTerrain& Terrain)
    {
        if(!Terrain.GetDensity()) return;
        TInlineComponentArray<UIslandMeshVoxels*> Fields(&Actor);
        if(Fields.IsEmpty()) return;
        TMap<UIslandMeshVoxels*,TArray<int32>> Removed;
        IslandScenerySupport::InspectActor(Actor,Terrain,Removed);
        for(auto& Pair:Removed)
            if(Pair.Key->RemoveCells(Pair.Value)) AIslandVoxelEdits::Record(*Pair.Key);
        if(auto* Prop=Cast<AIslandNeutralProp>(&Actor)) Prop->RefreshVoxelMechanism();
        UpdateSign(Actor);
    }
}

void IslandSceneryVoxels::CheckFoundations(UWorld& World,const FBox& Area)
{
    const auto* Terrain=AVoxelTerrain::Find(&World);
    if(!Terrain) return;
    for(TActorIterator<AActor> It(&World);It;++It)
    {
        if(It->GetLevel()!=World.PersistentLevel || !It->FindComponentByClass<UIslandMeshVoxels>()) continue;
        if(Area.Intersect(It->GetComponentsBoundingBox(true))) GroundActor(**It,*Terrain);
    }
}

bool IslandSceneryVoxels::Strike(const FHitResult& Hit,bool Wide)
{
    auto* Grid=FieldFor(Hit.GetComponent());
    AActor* Owner=Grid ? Grid->GetOwner():nullptr;
    if(!Owner || !Owner->HasAuthority()) return false;
    auto* Prop=Cast<AIslandNeutralProp>(Owner);
    const bool Wreck=Prop && Prop->GetKind()==EIslandNeutralKind::Ship;
    // Cutting rebuilds the collision sections and changes their face indices.
    FColor Pigment;
    const int32 Finish=Grid->GetImpactPaint(Hit,Pigment);
    if(!Grid->Cut(Hit.ImpactPoint,Hit.ImpactNormal,Wide)) return false;
    AIslandVoxelEdits::Record(*Grid);
    if(auto* Terrain=AVoxelTerrain::Find(Owner->GetWorld())) GroundActor(*Owner,*Terrain);
    UpdateSign(*Owner);
    if (Wreck && Grid->SourcePart())
        Prop->MulticastWreckChips(Grid->SourcePart()->GetFName(),Finish,Hit.ImpactPoint,Hit.ImpactNormal);
    else if (Grid->SourcePart())
    {
        const EIslandChipSurface Surface=ImpactFinish(*Owner,*Grid->SourcePart(),Finish,Pigment);
        AIslandVoxelEdits::EmitChips(*Owner->GetWorld(),Hit,Surface,Pigment);
    }
    return true;
}

AIslandVoxelEdits::AIslandVoxelEdits()
{
    bReplicates=true; bAlwaysRelevant=true;
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Rock(TEXT("/Game/TreasureIsland/Art/Neutral/SM_ReefStone"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Wood(TEXT("/Game/TreasureIsland/Art/Neutral/SM_Spray"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Cloth(TEXT("/Game/TreasureIsland/Art/Neutral/SM_WindCanvas"));
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> Finish(TEXT("/Game/TreasureIsland/Art/Neutral/M_WreckFinish"));
    StoneChip=Rock.Object;
    Splinter=Wood.Object;
    Scrap=Cloth.Object;
    ChipFinish=Finish.Object;
}

void AIslandVoxelEdits::EmitChips(UWorld& World,const FHitResult& Hit,EIslandChipSurface Surface,FColor Color)
{
    if (auto* Journal=Edits(&World); Journal && Journal->HasAuthority())
        Journal->MulticastChips(Hit.ImpactPoint,Hit.ImpactNormal,Surface,Color);
}

void AIslandVoxelEdits::MulticastChips_Implementation(FVector_NetQuantize Point,FVector_NetQuantizeNormal Normal,
    EIslandChipSurface Surface,FColor Color)
{
    if (FParse::Param(FCommandLine::Get(),TEXT("NeutralDemolitionQA")))
        UE_LOG(LogTemp, Display, TEXT("SCENERY_CHIPS_RECEIVED surface=%d tint=%s"), int32(Surface), *Color.ToHex());
    if (GetNetMode()==NM_DedicatedServer || !FApp::CanEverRender() || !ChipFinish) return;
    UStaticMesh* Shape=StoneChip;
    FVector Scale(.105,.082,.115);
    float Roughness=.87f,Metallic=0.f;
    switch (Surface)
    {
        case EIslandChipSurface::Timber: Shape=Splinter; Scale={.043,.035,.18}; break;
        case EIslandChipSurface::Rock: break;
        case EIslandChipSurface::Metal: Shape=Splinter; Scale={.070,.025,.108}; Roughness=.40f; Metallic=.65f; break;
        case EIslandChipSurface::Canvas: Shape=Scrap; Scale={.016,.066,.13}; Roughness=.96f; break;
        case EIslandChipSurface::Sand: Scale={.065,.060,.073}; Roughness=.96f; break;
        case EIslandChipSurface::Leaf: Shape=Splinter; Scale={.085,.022,.15}; Roughness=.64f; break;
    }
    if (!Shape) return;
    auto* Paint=UMaterialInstanceDynamic::Create(ChipFinish,this);
    Paint->SetVectorParameterValue(TEXT("Color"),FLinearColor(Color));
    Paint->SetScalarParameterValue(TEXT("Roughness"),Roughness);
    Paint->SetScalarParameterValue(TEXT("Metallic"),Metallic);
    AIslandSceneryDamage::PlayDebris(*GetWorld(),Point,Normal,Paint,Shape,Scale);
}

void AIslandVoxelEdits::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(AIslandVoxelEdits,Losses);
}

void AIslandVoxelEdits::BeginPlay()
{
    Super::BeginPlay();
    for(TActorIterator<AActor> It(GetWorld());It;++It)
    {
        if(It->GetLevel()!=GetWorld()->PersistentLevel) continue;
        if(It->IsA<AStaticMeshActor>() || It->IsA<AExpeditionLandmark>() || It->IsA<AIslandBreakable>())
            IslandSceneryVoxels::Register(**It);
    }
    OnRep_Losses();
}

void AIslandVoxelEdits::Record(UIslandMeshVoxels& Mesh)
{
    auto* Log=Edits(Mesh.GetWorld());
    if(!Log || !Log->HasAuthority()) return;
    auto* Row=Log->Losses.FindByPredicate([&](const auto& R){return R.Owner==Mesh.GetOwner() && R.SourceName==Mesh.SourcePart()->GetFName();});
    if(!Row)
    {
        Row=&Log->Losses.AddDefaulted_GetRef();
        Row->Owner=Mesh.GetOwner(); Row->SourceName=Mesh.SourcePart()->GetFName();
    }
    Mesh.PackRemoved(Row->Cells); ++Row->Revision;
    Log->ForceNetUpdate();
}

void AIslandVoxelEdits::Apply(UIslandMeshVoxels& Mesh,const FIslandVoxelLoss& Loss)
{
    if(Applied.FindRef(&Mesh)==Loss.Revision) return;
    Mesh.ApplyRemoved(Loss.Cells);
    Applied.Add(&Mesh,Loss.Revision);
    UpdateSign(*Mesh.GetOwner());
}

void AIslandVoxelEdits::OnRep_Losses()
{
    if(HasAuthority()) return;
    for(const auto& Row:Losses)
    {
        if(!Row.Owner) continue;
        TInlineComponentArray<UIslandMeshVoxels*> Fields(Row.Owner.Get());
        for(auto* Grid:Fields) if(Grid->SourcePart() && Grid->SourcePart()->GetFName()==Row.SourceName) {Apply(*Grid,Row);break;}
    }
}

void AIslandVoxelEdits::Restore(UIslandMeshVoxels& Mesh)
{
    auto* Log=Edits(Mesh.GetWorld());
    if(!Log || Log->HasAuthority()) return;
    for(const auto& Loss:Log->Losses)
        if(Loss.Owner==Mesh.GetOwner() && Loss.SourceName==Mesh.SourcePart()->GetFName()) {Log->Apply(Mesh,Loss);return;}
}
