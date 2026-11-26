#include "PartyDensityTerrain.h"

#include "PartyVoxelArena.h"
#include "VoxelSurfaceKernel.h"
#include "Materials/MaterialInterface.h"
#include "ProceduralMeshComponent.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
    using namespace VoxelSurfaceKernel;
    constexpr int32 AuthorSize = 100;
    constexpr int32 SamplesPerAuthorCell = AuthorSize / Spacing;
    constexpr int32 WeightScale = SamplesPerAuthorCell*SamplesPerAuthorCell*SamplesPerAuthorCell;
    const FName TerrainTag(TEXT("PartyDensitySurface"));

    FIntVector FloorVector(FVector P)
    {
        return {FMath::FloorToInt(P.X),FMath::FloorToInt(P.Y),FMath::FloorToInt(P.Z)};
    }

    uint32 HashSample(int32 Index, int16 Density, uint8 Kind)
    {
        uint32 H = uint32(Index)*0x9e3779b9u ^ (uint32(uint16(Density)) << 8) ^ Kind;
        H ^= H >> 16;
        H *= 0x85ebca6bu;
        return H ^ (H >> 13);
    }
}

UPartyDensityTerrain::UPartyDensityTerrain()
{
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> Paint(TEXT("/Game/TreasureIsland/FX/M_ArenaGround.M_ArenaGround"));
    BaseMaterial = Paint.Object;
}

void UPartyDensityTerrain::Initialize(const TMap<FIntVector,EPartyBlock>& InitialCells, FColor Theme,
    TConstArrayView<FVector> Berths)
{
    using namespace VoxelSurfaceKernel;
    if (InitialCells.IsEmpty() || !Samples.IsEmpty()) return;

    FIntVector Lo(MAX_int32), Hi(MIN_int32);
    for (const auto& Cell : InitialCells)
    {
        Lo = Lo.ComponentMin(Cell.Key);
        Hi = Hi.ComponentMax(Cell.Key);
    }
    // Add an air collar around the solid geometry, including room above it to stack mud.
    Origin = Lo*AuthorSize - FIntVector(150);
    const FIntVector Ceiling = (Hi+FIntVector(1))*AuthorSize + FIntVector(150,150,800);
    constexpr int32 ChunkSize = CellsPerChunk*Spacing;
    Chunks = (Ceiling-Origin+FIntVector(ChunkSize-1))/ChunkSize;
    Dimensions = Chunks*CellsPerChunk+FIntVector(1);
    Samples.Init(Band,Dimensions.X*Dimensions.Y*Dimensions.Z);
    Kinds.Init(0,Samples.Num());
    const int32 LayerSize=Dimensions.X*Dimensions.Y;
    FoundationZ.Init(INDEX_NONE,LayerSize);
    BerthFoundation.Init(false,LayerSize);
    MeshChunks.SetNum(Chunks.X*Chunks.Y*Chunks.Z);
    Palette = {FLinearColor::Transparent, FMath::Lerp(FLinearColor(Theme), FLinearColor(FColor(100,126,119)), .34f),
        FLinearColor(FColor(171,116,78)), FLinearColor(FColor(231,194,140)), FLinearColor(FColor(255,185,46))};

    // Authors describe volumes by metre-wide cells. Interpolate their centre samples once to
    // produce continuous ledges, walls and beds. Edits never touch this authoring lattice.
    const FIntVector CoarseOrigin = Lo-FIntVector(2);
    const FIntVector CoarseSize = Hi-Lo+FIntVector(5);
    const auto CoarseIndex = [&](FIntVector P) { return P.X+CoarseSize.X*(P.Y+CoarseSize.Y*P.Z); };
    TArray<uint8> Coarse;
    Coarse.Init(0,CoarseSize.X*CoarseSize.Y*CoarseSize.Z);
    for (const auto& Cell : InitialCells) Coarse[CoarseIndex(Cell.Key-CoarseOrigin)] = uint8(Cell.Value);
    // Origin lies exactly at the centre of the first padded author cell.
    const FIntVector Last = ((Hi-Lo+FIntVector(3))*SamplesPerAuthorCell).ComponentMin(Dimensions-FIntVector(1));
    TArray<FIntVector> Undersides;
    for (int32 Z=0; Z<=Last.Z; ++Z)
        for (int32 Y=0; Y<=Last.Y; ++Y)
            for (int32 X=0; X<=Last.X; ++X)
            {
                const FIntVector P(X,Y,Z);
                const FIntVector C = P/SamplesPerAuthorCell;
                const FIntVector Frac = P-C*SamplesPerAuthorCell;
                int32 SolidWeight=0, BestWeight=0;
                uint8 Material=0;
                for (int32 Dz=0; Dz<2; ++Dz)
                    for (int32 Dy=0; Dy<2; ++Dy)
                        for (int32 Dx=0; Dx<2; ++Dx)
                        {
                            const uint8 Kind = Coarse[CoarseIndex(C+FIntVector(Dx,Dy,Dz))];
                            if (!Kind) continue;
                            const int32 Weight = (Dx ? Frac.X : SamplesPerAuthorCell-Frac.X)*(Dy ? Frac.Y : SamplesPerAuthorCell-Frac.Y)*(Dz ? Frac.Z : SamplesPerAuthorCell-Frac.Z);
                            SolidWeight += Weight;
                            if (Weight>BestWeight) { BestWeight=Weight; Material=Kind; }
                        }
                const int32 I=Index(P);
                Samples[I] = Band - (2*Band*SolidWeight + WeightScale/2)/WeightScale;
                Kinds[I] = Material;
                if (Samples[I]>=0 || (Z>0 && Samples[I-LayerSize]<0)) continue;
                const FIntVector Local=Origin+P*Spacing;
                const FIntVector Author=FloorVector(FVector(Local)/AuthorSize);
                const int32 Column=X+Dimensions.X*Y;
                // The banks start at course height zero. Recessed pier footings start
                // at the lowest authored level. Upper decks get support via their piers.
                if (FoundationZ[Column]==INDEX_NONE && (Author.Z==0 || (Lo.Z<0 && Author.Z==Lo.Z))
                    && InitialCells.Contains(Author))
                {
                    FoundationZ[Column]=Z;
                    for (const FVector& Berth : Berths)
                        if (FVector::DistSquared2D(Berth,FVector(Local))<=FMath::Square(75.))
                        {
                            BerthFoundation[Column]=true;
                            break;
                        }
                }
                else Undersides.Add(P);
            }
    // Every component has a lowest sample. Check those before the first physics cook,
    // including authored platforms separated by an air gap within the same column.
    FindUnsupported(Undersides,
        [&](FIntVector P){ return Contains(P) && Samples[Index(P)]<0; },
        [&](FIntVector P){ return FoundationZ[P.X+Dimensions.X*P.Y]==P.Z; },
        [&](FIntVector P){ Samples[Index(P)]=Band; Kinds[Index(P)]=uint8(EPartyBlock::Air); });
    for (int32 I=0; I<Samples.Num(); ++I) Digest ^= HashSample(I,Samples[I],Kinds[I]);
    const FIntVector InitialChunks = (Last/CellsPerChunk).ComponentMin(Chunks-FIntVector(1));
    for (int32 Z=0; Z<=InitialChunks.Z; ++Z)
        for (int32 Y=0; Y<=InitialChunks.Y; ++Y)
            for (int32 X=0; X<=InitialChunks.X; ++X) RebuildChunk({X,Y,Z});
}

bool UPartyDensityTerrain::Contains(FIntVector P) const
{
    return P.GetMin()>=0 && P.X<Dimensions.X && P.Y<Dimensions.Y && P.Z<Dimensions.Z;
}

float UPartyDensityTerrain::Interpolate(FVector LocalPosition, EPartyBlock* Material) const
{
    using namespace VoxelSurfaceKernel;
    const FVector P=(LocalPosition-FVector(Origin))/Spacing;
    const FIntVector Low=FloorVector(P);
    if (!Contains(Low) || !Contains(Low+FIntVector(1)))
    {
        if (Material) *Material=EPartyBlock::Air;
        return Band;
    }
    const FVector F=P-FVector(Low);
    float Result=0.f, BestWeight=-1.f;
    if (Material) *Material=EPartyBlock::Air;
    for (int32 Z=0; Z<2; ++Z)
        for (int32 Y=0; Y<2; ++Y)
            for (int32 X=0; X<2; ++X)
            {
                const int32 I=Index(Low+FIntVector(X,Y,Z));
                const float Weight=(X ? F.X : 1.-F.X)*(Y ? F.Y : 1.-F.Y)*(Z ? F.Z : 1.-F.Z);
                Result+=Samples[I]*Weight;
                if (Material && Samples[I]<0 && Kinds[I] && Weight>BestWeight)
                {
                    BestWeight=Weight;
                    *Material=EPartyBlock(Kinds[I]);
                }
            }
    return Result;
}

float UPartyDensityTerrain::DensityAt(FVector LocalPosition) const
{
    return Interpolate(LocalPosition,nullptr);
}

EPartyBlock UPartyDensityTerrain::MaterialAt(FVector LocalPosition) const
{
    EPartyBlock Material;
    const float D = Interpolate(LocalPosition,&Material);
    return D<0 ? Material : EPartyBlock::Air;
}

bool UPartyDensityTerrain::OwnsSurface(const UPrimitiveComponent* Component) const
{
    return Component && Component->GetOwner()==GetOwner() && Component->ComponentHasTag(TerrainTag);
}

bool UPartyDensityTerrain::ApplyBrush(FIntVector Center, FIntVector Normal, bool bFill,
    const TArray<FIntVector>& ProtectedMin, const TArray<FIntVector>& ProtectedMax)
{
    using namespace VoxelSurfaceKernel;
    if (Samples.IsEmpty() || ProtectedMin.Num()!=ProtectedMax.Num()) return false;
    const FIntVector Extent(bFill ? 185 : 165);
    const FIntVector Lo = FloorVector(FVector(Center-Extent-Origin)/Spacing).ComponentMax(FIntVector(1));
    const FIntVector Hi = FloorVector(FVector(Center+Extent-Origin)/Spacing).ComponentMin(Dimensions-FIntVector(2));
    FIntVector MinChanged=Dimensions, MaxChanged(-1);
    const auto SetSample = [&](FIntVector P,int16 Value,uint8 Kind)
    {
        const int32 I=Index(P);
        Digest ^= HashSample(I,Samples[I],Kinds[I]) ^ HashSample(I,Value,Kind);
        Samples[I]=Value;
        Kinds[I]=Kind;
        MinChanged=MinChanged.ComponentMin(P);
        MaxChanged=MaxChanged.ComponentMax(P);
    };
    struct FPrevious { int16 Density; uint8 Kind; };
    TMap<FIntVector,FPrevious> BeforeFill;
    TArray<FIntVector> SupportSeeds;
    for (int32 Z=Lo.Z; Z<=Hi.Z; ++Z)
        for (int32 Y=Lo.Y; Y<=Hi.Y; ++Y)
            for (int32 X=Lo.X; X<=Hi.X; ++X)
            {
                const FIntVector P(X,Y,Z);
                const FIntVector World = Origin+P*Spacing;
                int32 Distance = ToolDistance(World-Center,Normal,bFill);
                if (bFill)
                    for (int32 B=0; B<ProtectedMin.Num(); ++B)
                        Distance = FMath::Max(Distance,-FMath::Max((ProtectedMin[B]-World).GetMax(),(World-ProtectedMax[B]).GetMax()));
                const int32 I=Index(P);
                const int16 Old=Samples[I];
                const int16 Next=Blend(Old,Distance,bFill);
                if (Next==Old) continue;
                const int32 Column=X+Dimensions.X*Y;
                if (!bFill && FoundationZ[Column]==Z && BerthFoundation[Column]) continue;
                if (bFill)
                {
                    BeforeFill.Add(P,FPrevious{Old,Kinds[I]});
                    if (Old>=0 && Next<0) SupportSeeds.Add(P);
                }
                else if (Old<0 && Next>=0)
                {
                    if (FoundationZ[Column]==Z) FoundationZ[Column]=INDEX_NONE;
                    for (FIntVector Step : FaceNeighbors) SupportSeeds.Add(P+Step);
                }
                SetSample(P,Next,bFill && Old>=0 ? uint8(EPartyBlock::Sand) : Kinds[I]);
            }
    if (MaxChanged.X<0) return false;
    bool bRejectedFill=false;
    FindUnsupported(SupportSeeds,
        [&](FIntVector P){ return Contains(P) && Samples[Index(P)]<0; },
        [&](FIntVector P){ return FoundationZ[P.X+Dimensions.X*P.Y]==P.Z; },
        [&](FIntVector P)
        {
            if (bFill)
            {
                // Filling cannot detach old geometry. Restore the original air samples.
                const FPrevious& Before=BeforeFill.FindChecked(P);
                SetSample(P,Before.Density,Before.Kind);
                bRejectedFill=true;
            }
            else SetSample(P,Band,uint8(EPartyBlock::Air));
        });
    if (bFill)
    {
        bool bTouchesSurface=false;
        for (const auto& Entry : BeforeFill)
        {
            const int16 Now=Samples[Index(Entry.Key)];
            if (Now==Entry.Value.Density) continue;
            if (Now<0) { bTouchesSurface=true; break; }
            // A clipped fill may only move the air side of an existing surface.
            if (bRejectedFill) continue;
            for (int32 Z=-1; Z<=1 && !bTouchesSurface; ++Z)
                for (int32 Y=-1; Y<=1 && !bTouchesSurface; ++Y)
                    for (int32 X=-1; X<=1; ++X)
                        if (Samples[Index(Entry.Key+FIntVector(X,Y,Z))]<0) { bTouchesSurface=true; break; }
            if (bTouchesSurface) break;
        }
        if (!bTouchesSurface)
        {
            for (const auto& Entry : BeforeFill) SetSample(Entry.Key,Entry.Value.Density,Entry.Value.Kind);
            return false;
        }
    }
    const FIntVector First=((MinChanged-FIntVector(2))/CellsPerChunk).ComponentMax(FIntVector::ZeroValue);
    const FIntVector Last=((MaxChanged+FIntVector(1))/CellsPerChunk).ComponentMin(Chunks-FIntVector(1));
    for (int32 Z=First.Z; Z<=Last.Z; ++Z)
        for (int32 Y=First.Y; Y<=Last.Y; ++Y)
            for (int32 X=First.X; X<=Last.X; ++X) RebuildChunk({X,Y,Z});
    return true;
}

FLinearColor UPartyDensityTerrain::SurfaceColor(FVector LocalPosition) const
{
    using namespace VoxelSurfaceKernel;
    const FVector Grid=(LocalPosition-FVector(Origin))/Spacing;
    const FIntVector Low=FloorVector(Grid);
    if (!Contains(Low) || !Contains(Low+FIntVector(1))) return Palette[int32(EPartyBlock::Stone)];
    const FVector F=Grid-FVector(Low);
    FLinearColor Color=FLinearColor::Transparent;
    float Total=0.f;
    for (int32 Z=0; Z<2; ++Z)
        for (int32 Y=0; Y<2; ++Y)
            for (int32 X=0; X<2; ++X)
            {
                const int32 I=Index(Low+FIntVector(X,Y,Z));
                if (Kinds[I]==uint8(EPartyBlock::Air)) continue;
                const float Occupancy=FMath::Clamp((Spacing-Samples[I])/float(2*Spacing),0.f,1.f);
                const float Weight=Occupancy*(X ? F.X : 1.-F.X)*(Y ? F.Y : 1.-F.Y)*(Z ? F.Z : 1.-F.Z);
                Color+=Palette[Kinds[I]]*Weight;
                Total+=Weight;
            }
    return Total>UE_SMALL_NUMBER ? Color/Total : Palette[int32(EPartyBlock::Stone)];
}

void UPartyDensityTerrain::RebuildChunk(FIntVector Chunk)
{
    using namespace VoxelSurfaceKernel;
    const FIntVector First=Chunk*CellsPerChunk;
    FChunkSamples Buffer;
    constexpr int32 Width=FChunkSamples::Width;
    for (int32 Z=0; Z<Width; ++Z)
        for (int32 Y=0; Y<Width; ++Y)
        {
            const FIntVector P=First+FIntVector(-2,Y-2,Z-2);
            int16* Row=&Buffer.Values[Width*(Y+Width*Z)];
            if (P.X>=0 && P.X+Width<=Dimensions.X && Contains(P)) FMemory::Memcpy(Row,&Samples[Index(P)],Width*sizeof(int16));
            else for (int32 X=0; X<Width; ++X)
            {
                const FIntVector At=P+FIntVector(X,0,0);
                Row[X]=Contains(At) ? Samples[Index(At)] : Band;
            }
        }
    FProcMeshSection Geometry=BuildChunk(Buffer,FVector(Origin+First*Spacing));
    const int32 Slot=Chunk.X+Chunks.X*(Chunk.Y+Chunks.Y*Chunk.Z);
    auto& Mesh=MeshChunks[Slot];
    if (Geometry.ProcIndexBuffer.IsEmpty())
    {
        if (Mesh) Mesh->ClearAllMeshSections();
        return;
    }
    if (!Mesh)
    {
        const FName Name(*FString::Printf(TEXT("PartyGround_%d_%d_%d"),Chunk.X,Chunk.Y,Chunk.Z));
        Mesh=NewObject<UProceduralMeshComponent>(GetOwner(),Name);
        Mesh->SetNetAddressable();
        Mesh->SetupAttachment(GetOwner()->GetRootComponent());
        Mesh->ComponentTags.Add(TerrainTag);
        Mesh->SetCollisionProfileName(TEXT("BlockAll"));
        Mesh->SetCanEverAffectNavigation(false);
        Mesh->SetGenerateOverlapEvents(false);
        Mesh->bUseComplexAsSimpleCollision=true;
        Mesh->bUseAsyncCooking=false;
        Mesh->SetMaterial(0,BaseMaterial);
        Mesh->SetCastContactShadow(false);
        Mesh->RegisterComponent();
    }
    // The GPU interpolates the interior sample colours over each triangle. Geometry,
    // normals and walking collision stay on the same continuous zero surface.
    for (auto& Vertex : Geometry.ProcVertexBuffer)
        Vertex.Color=SurfaceColor(Vertex.Position-Vertex.Normal*5.).ToFColor(false);
    Mesh->SetProcMeshSection(0,Geometry);
}
