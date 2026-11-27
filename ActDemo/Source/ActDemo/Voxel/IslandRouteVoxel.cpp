#include "Voxel/IslandRouteVoxel.h"
#include "Island/ExpeditionBodyMud.h"
#include "Island/IslandSceneryVoxels.h"
#include "Island/IslandFallingScenery.h"
#include "Island/IslandAttachedMud.h"
#include "Misc/Compression.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"

#include "Engine/World.h"
#include "Materials/MaterialInterface.h"
#include "Misc/Crc.h"
#include "Net/UnrealNetwork.h"
#include "UObject/ConstructorHelpers.h"
#include "Voxel/VoxelTerrain.h"
#include "VoxelSurfaceKernel.h"

namespace
{
    constexpr int32 ChunkSide = VoxelSurfaceKernel::CellsPerChunk;

    FIntVector Rounded(FVector V)
    {
        return {FMath::RoundToInt32(V.X), FMath::RoundToInt32(V.Y), FMath::RoundToInt32(V.Z)};
    }

    void WriteVarUInt(TArray<uint8>& Out, uint32 N)
    {
        while (N >= 128) { Out.Add(static_cast<uint8>(N) | 128); N >>= 7; }
        Out.Add(static_cast<uint8>(N));
    }

    bool ReadVarUInt(const TArray<uint8>& Data, int32& Cursor, uint32& Result)
    {
        Result = 0;
        for (int32 Shift = 0; Shift <= 28 && Cursor < Data.Num(); Shift += 7)
        {
            const uint8 Byte = Data[Cursor++];
            if (Shift == 28 && Byte > 15) return false;
            Result |= uint32(Byte & 127) << Shift;
            if (!(Byte & 128)) return true;
        }
        return false;
    }
}

UIslandRouteVoxelComponent::UIslandRouteVoxelComponent(const FObjectInitializer& Initializer)
    : Super(Initializer)
{
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> Paint(TEXT("/Game/TreasureIsland/FX/M_RoutePigment.M_RoutePigment"));
    SetMaterial(0, Paint.Object);
    SetCollisionProfileName(TEXT("BlockAll"));
    SetGenerateOverlapEvents(false);
    SetCanEverAffectNavigation(false);
    bUseComplexAsSimpleCollision = true;
    bUseAsyncCooking = false;
}

FIntVector UIslandRouteVoxelComponent::Coordinates(int32 I) const
{
    return {I % Size.X, (I / Size.X) % Size.Y, I / (Size.X * Size.Y)};
}

bool UIslandRouteVoxelComponent::Inside(FIntVector P) const
{
    return P.GetMin() >= 0 && P.X < Size.X && P.Y < Size.Y && P.Z < Size.Z;
}

int8 UIslandRouteVoxelComponent::At(FIntVector P) const
{
    return Inside(P) ? Samples[Index(P)] : int8(VoxelSurfaceKernel::Band);
}

FBox UIslandRouteVoxelComponent::GetFieldLocalBounds() const
{
    return Samples.IsEmpty() ? FBox(ForceInit) : FBox(GridStart, GridStart + FVector(Size - FIntVector(1)) * Step);
}

bool UIslandRouteVoxelComponent::InitializeField(const FBox& FieldBounds, double Spacing,
    TFunctionRef<FRouteVoxelSample(FVector)> SampleShape)
{
    if (!FieldBounds.IsValid || !FMath::IsFinite(Spacing) || Spacing <= 0.) return false;
    const FVector Span = FieldBounds.GetSize() / Spacing;
    if (Span.GetMax() > 2048.) return false;
    const FIntVector Points(FMath::CeilToInt(Span.X) + 5, FMath::CeilToInt(Span.Y) + 5, FMath::CeilToInt(Span.Z) + 5);
    const int64 Num = int64(Points.X) * Points.Y * Points.Z;
    if (Num > 2000000) return false;
    Size = Points;
    Step = Spacing;
    GridStart = FieldBounds.Min - FVector(2. * Step);
    Samples.SetNumUninitialized(Num);
    Pigments.SetNumUninitialized(Num);
    SolidCount = 0;
    for (int32 I = 0; I < Samples.Num(); ++I)
    {
        const FRouteVoxelSample Value = SampleShape(GridStart + FVector(Coordinates(I)) * Step);
        const double D = Value.Distance * VoxelSurfaceKernel::Spacing / Step;
        int32 Signed = FMath::RoundToInt(FMath::Clamp(D, -double(VoxelSurfaceKernel::Band), double(VoxelSurfaceKernel::Band)));
        if (Value.Distance < 0. && Signed == 0) Signed = -1;
        Samples[I] = Signed;
        // Procedural mesh vertex colors are uploaded as unorm bytes and read as linear pigment.
        Pigments[I] = FLinearColor(Value.Color).ToFColor(false);
        SolidCount += Signed < 0;
    }
    Original = Samples;
    InitialSolidCount = SolidCount;
    UpdateSurface();
    return SolidCount > 0;
}

bool UIslandRouteVoxelComponent::IsSolidLocal(FVector Local) const
{
    return SignedDistanceLocal(Local) < 0.;
}

double UIslandRouteVoxelComponent::SignedDistanceLocal(FVector Local) const
{
    if (Step <= 0.) return BIG_NUMBER;
    const FVector P = (Local - GridStart) / Step;
    const FIntVector Low(FMath::FloorToInt(P.X), FMath::FloorToInt(P.Y), FMath::FloorToInt(P.Z));
    if (!Inside(Low)) return double(VoxelSurfaceKernel::Band)*Step/VoxelSurfaceKernel::Spacing;
    const FVector T = P - FVector(Low);
    const auto Plane = [&](int32 Z)
    {
        return FMath::Lerp(FMath::Lerp<double>(At({Low.X, Low.Y, Z}), At({Low.X+1, Low.Y, Z}), T.X),
            FMath::Lerp<double>(At({Low.X, Low.Y+1, Z}), At({Low.X+1, Low.Y+1, Z}), T.X), T.Y);
    };
    return FMath::Lerp(Plane(Low.Z), Plane(Low.Z+1), T.Z) * Step / VoxelSurfaceKernel::Spacing;
}

bool UIslandRouteVoxelComponent::IsSolidWorld(FVector World) const
{
    return IsSolidLocal(GetComponentTransform().InverseTransformPosition(World));
}

uint32 UIslandRouteVoxelComponent::Digest() const
{
    return FCrc::MemCrc32(Samples.GetData(), Samples.Num());
}

FColor UIslandRouteVoxelComponent::SurfaceColor(FVector Local) const
{
    const FIntVector P = Rounded((Local - GridStart) / Step).ComponentMax(FIntVector(0)).ComponentMin(Size-FIntVector(1));
    return Pigments[Index(P)];
}

void UIslandRouteVoxelComponent::UpdateSurface()
{
    UExpeditionBodyMudComponent::NotifySceneChanged(GetWorld());
    if (SolidCount == 0)
    {
        ClearAllMeshSections();
        return;
    }
    FProcMeshSection Combined;
    BuildSurface(FIntVector(0), Size - FIntVector(1), [&](FIntVector P){return At(P);}, Combined);
    SetProcMeshSection(0, Combined);
}

void UIslandRouteVoxelComponent::BuildSurface(FIntVector Low, FIntVector High,
    TFunctionRef<int8(FIntVector)> SampleAt, FProcMeshSection& Combined) const
{
    const double Scale = Step / VoxelSurfaceKernel::Spacing;
    for (int32 Z = Low.Z; Z < High.Z; Z += ChunkSide)
    for (int32 Y = Low.Y; Y < High.Y; Y += ChunkSide)
    for (int32 X = Low.X; X < High.X; X += ChunkSide)
    {
        const FIntVector Origin(X, Y, Z);
        VoxelSurfaceKernel::FChunkSamples Chunk;
        constexpr int32 Width = VoxelSurfaceKernel::FChunkSamples::Width;
        for (int32 K = 0; K < Width; ++K)
        for (int32 J = 0; J < Width; ++J)
        for (int32 I = 0; I < Width; ++I)
            Chunk.Values[I + Width * (J + Width * K)] = SampleAt(Origin + FIntVector(I-2, J-2, K-2));
        auto Part = VoxelSurfaceKernel::BuildChunk(Chunk, FVector(Origin) * VoxelSurfaceKernel::Spacing);
        if (!Part.bEnableCollision) continue;
        const int32 Offset = Combined.ProcVertexBuffer.Num();
        for (auto& Vertex : Part.ProcVertexBuffer)
        {
            Vertex.Position = GridStart + Vertex.Position * Scale;
            Vertex.Color = SurfaceColor(Vertex.Position - Vertex.Normal * (.3 * Step));
            Combined.SectionLocalBox += Vertex.Position;
        }
        Combined.ProcVertexBuffer.Append(MoveTemp(Part.ProcVertexBuffer));
        for (uint32 Id : Part.ProcIndexBuffer) Combined.ProcIndexBuffer.Add(Offset + Id);
    }
    Combined.bEnableCollision = !Combined.ProcIndexBuffer.IsEmpty();
}

bool UIslandRouteVoxelComponent::ApplyWorldCut(FVector Point, FVector OutwardNormal, double Radius, float DepthScale)
{
    if (!SolidCount || Radius <= 0.) return false;
    const FTransform& Transform = GetComponentTransform();
    const FVector N = OutwardNormal.GetSafeNormal(UE_SMALL_NUMBER, FVector::UpVector);
    // Keep the brush's outer face in place as the fist reaches farther into the surface.
    const double LightHalfDepth = .69 * FMath::Min(Radius,62.);
    constexpr double LightInset = 12.;
    const double ExtraDepth = (LightHalfDepth + LightInset) * (DepthScale - 1.f);
    const FVector Center = Point - N * (LightInset + .5 * ExtraDepth);
    const double Depth = LightHalfDepth + .5 * ExtraDepth;
    const FBox EditBounds = FBox::BuildAABB(Center, FVector(Radius + Depth + Step * 2.)).TransformBy(Transform.Inverse());
    const FIntVector Low = Rounded((EditBounds.Min - GridStart)/Step).ComponentMax(FIntVector(0));
    const FIntVector High = Rounded((EditBounds.Max - GridStart)/Step).ComponentMin(Size - FIntVector(1));
    bool bChanged = false;
    for (int32 Z = Low.Z; Z <= High.Z; ++Z)
    for (int32 Y = Low.Y; Y <= High.Y; ++Y)
    for (int32 X = Low.X; X <= High.X; ++X)
    {
        const FIntVector P(X, Y, Z);
        const FVector Delta = Transform.TransformPosition(GridStart + FVector(P) * Step) - Center;
        const double Along = FVector::DotProduct(Delta, N);
        const double Across = (Delta - N * Along).Size();
        const double Tool = FMath::Max(Across - Radius, FMath::Abs(Along) - Depth);
        int8& Previous = Samples[Index(P)];
        const int8 Value = FMath::RoundToInt(FMath::Clamp(-Tool * VoxelSurfaceKernel::Spacing / Step, -double(VoxelSurfaceKernel::Band), double(VoxelSurfaceKernel::Band)));
        if (Value <= Previous) continue;
        if (Previous < 0 && Value >= 0) --SolidCount;
        Previous = Value;
        bChanged = true;
    }
    if (bChanged) UpdateSurface();
    return bChanged;
}

void UIslandRouteVoxelComponent::CaptureLoose(TConstArrayView<int32> Cells, FIslandLooseSamples& Out) const
{
    FIntVector Lo = Size, Hi(0);
    for (int32 I : Cells)
    {
        const FIntVector P = Coordinates(I);
        Lo = Lo.ComponentMin(P);
        Hi = Hi.ComponentMax(P);
        Out.Feet.Add(GetComponentTransform().TransformPosition(GridStart + FVector(P)*Step) - FVector::UpVector*Step*.5);
    }
    Lo = (Lo-FIntVector(3)).ComponentMax(FIntVector(0));
    Hi = (Hi+FIntVector(3)).ComponentMin(Size-FIntVector(1));
    FIntVector Span = Hi-Lo+FIntVector(1);
    const auto Offset = [&](FIntVector P){ P-=Lo; return P.X+Span.X*(P.Y+Span.Y*P.Z); };
    TArray<int8> Patch;
    Patch.Reserve(Span.X*Span.Y*Span.Z);
    for (int32 Z=Lo.Z; Z<=Hi.Z; ++Z)
        for (int32 Y=Lo.Y; Y<=Hi.Y; ++Y)
            for (int32 X=Lo.X; X<=Hi.X; ++X)
            {
                const int8 Sample=At({X,Y,Z});
                Patch.Add(Sample>=0 ? Sample : int8(VoxelSurfaceKernel::Band));
            }
    for (int32 I : Cells) Patch[Offset(Coordinates(I))] = Samples[I];

    FMemoryWriter Writer(Out.Shape);
    Writer << Lo << Span;
    int64 CompressedSize = 0;
    if (!FCompression::CompressMemoryBound(NAME_Zlib, CompressedSize, int64(Patch.Num()))) { Out.Shape.Reset(); return; }
    TArray<uint8> Compressed;
    Compressed.SetNumUninitialized(CompressedSize);
    if (FCompression::CompressMemory(NAME_Zlib, Compressed.GetData(), CompressedSize, Patch.GetData(), Patch.Num()))
        Writer.Serialize(Compressed.GetData(), CompressedSize);
    else Out.Shape.Reset();
}

bool UIslandRouteVoxelComponent::FallingSection(const TArray<uint8>& Shape, FProcMeshSection& Out) const
{
    if (Shape.Num() <= 6*int32(sizeof(int32)) || !Original.Num()) return false;
    FMemoryReader Reader(Shape);
    FIntVector Lo, Span;
    Reader << Lo << Span;
    if (Lo.GetMin()<0 || Span.GetMin()<=0 || Span.X>Size.X-Lo.X || Span.Y>Size.Y-Lo.Y || Span.Z>Size.Z-Lo.Z) return false;
    TArray<int8> Patch;
    Patch.SetNumUninitialized(Span.X*Span.Y*Span.Z);
    if (!FCompression::UncompressMemory(NAME_Zlib, Patch.GetData(), Patch.Num(),
        Shape.GetData()+Reader.Tell(), Shape.Num()-Reader.Tell())) return false;
    BuildSurface(Lo, Lo+Span-FIntVector(1), [&](FIntVector P) -> int8
    {
        P-=Lo;
        return P.GetMin()>=0 && P.X<Span.X && P.Y<Span.Y && P.Z<Span.Z ? Patch[P.X+Span.X*(P.Y+Span.Y*P.Z)] : int8(VoxelSurfaceKernel::Band);
    }, Out);
    Out.bEnableCollision = false;
    return !Out.ProcIndexBuffer.IsEmpty();
}

bool UIslandRouteVoxelComponent::CullUnsupported(TFunctionRef<bool(FVector)> IsLocalAnchor, TArray<FIslandLooseSamples>* Loose)
{
    if (!SolidCount) return false;
    TBitArray<> Visited(false, Samples.Num());
    TArray<int32> Group;
    const int32 Before = SolidCount;
    for (int32 Seed=0; Seed<Samples.Num(); ++Seed)
    {
        if (Visited[Seed] || Samples[Seed]>=0) continue;
        Group.Reset();
        Group.Add(Seed);
        Visited[Seed]=true;
        bool bAnchored=false;
        for (int32 Head=0; Head<Group.Num(); ++Head)
        {
            const FIntVector P=Coordinates(Group[Head]);
            if (!bAnchored) bAnchored=IsLocalAnchor(GridStart+FVector(P)*Step);
            for (const auto Direction : VoxelSurfaceKernel::FaceNeighbors)
            {
                const FIntVector Next=P+Direction;
                if (!Inside(Next)) continue;
                const int32 N=Index(Next);
                if (Samples[N]<0 && !Visited[N]) { Visited[N]=true; Group.Add(N); }
            }
        }
        if (bAnchored) continue;
        if (Loose) CaptureLoose(Group, Loose->AddDefaulted_GetRef());
        for (int32 I : Group) Samples[I]=VoxelSurfaceKernel::Band;
        SolidCount-=Group.Num();
    }
    if (Before==SolidCount) return false;
    UpdateSurface();
    return true;
}

void UIslandRouteVoxelComponent::WriteChanges(TArray<uint8>& Out) const
{
    Out.Reset();
    int32 PreviousEnd = 0;
    for (int32 I = 0; I < Samples.Num();)
    {
        if (Samples[I] == Original[I]) { ++I; continue; }
        const int32 First = I++;
        while (I < Samples.Num() && Samples[I] != Original[I]) ++I;
        WriteVarUInt(Out, First - PreviousEnd);
        WriteVarUInt(Out, I - First);
        Out.Append(reinterpret_cast<const uint8*>(Samples.GetData() + First), I - First);
        PreviousEnd = I;
    }
}

void UIslandRouteVoxelComponent::ReadChanges(const TArray<uint8>& Changes)
{
    if (Original.IsEmpty()) return;
    TArray<int8> Updated = Original;
    int32 Cursor = 0, End = 0;
    while (Cursor < Changes.Num())
    {
        uint32 Gap, Count;
        if (!ReadVarUInt(Changes, Cursor, Gap) || !ReadVarUInt(Changes, Cursor, Count) || Count == 0 ||
            uint64(End) + Gap + Count > uint32(Updated.Num()) || Count > uint32(Changes.Num() - Cursor)) return;
        End += Gap;
        FMemory::Memcpy(Updated.GetData() + End, Changes.GetData() + Cursor, Count);
        End += Count;
        Cursor += Count;
    }
    if (Samples == Updated) return;
    Samples = MoveTemp(Updated);
    SolidCount = 0;
    for (int8 Sample : Samples) SolidCount += Sample < 0;
    UpdateSurface();
}

AIslandRouteVoxel::AIslandRouteVoxel()
{
    bReplicates = true;
    bAlwaysRelevant = true;
    bNetLoadOnClient = false;
    Voxels = CreateDefaultSubobject<UIslandRouteVoxelComponent>(TEXT("LivingVolume"));
    SetRootComponent(Voxels);
}

AIslandRouteVoxel* AIslandRouteVoxel::Spawn(UWorld& World, IslandRouteFlora::EShape Kind, FVector Location,
    int32 Yaw, int32 Variant)
{
    const FTransform Transform(FRotator(0., Yaw, 0.), Location);
    auto* Actor = World.SpawnActorDeferred<AIslandRouteVoxel>(StaticClass(), Transform, nullptr, nullptr,
        ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
    if (Actor)
    {
        Actor->State.Kind = static_cast<uint8>(Kind);
        Actor->State.Variant = Variant;
        Actor->FinishSpawning(Transform);
    }
    return Actor;
}

void AIslandRouteVoxel::BeginPlay()
{
    Super::BeginPlay();
    OnRep_State();
}

void AIslandRouteVoxel::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(AIslandRouteVoxel, State);
}

void AIslandRouteVoxel::OnRep_State()
{
    if (State.Kind > uint8(IslandRouteFlora::EShape::CoralSpur)) return;
    if (Voxels->InitialCount() == 0)
    {
        const auto Kind = GetKind();
        Voxels->InitializeField(IslandRouteFlora::Bounds(Kind), IslandRouteFlora::SampleSpacing(Kind), [Kind, this](FVector P)
        {
            const auto Sample = IslandRouteFlora::Sample(Kind, P, State.Variant);
            return FRouteVoxelSample{Sample.Distance, IslandRouteFlora::PigmentColor(Sample.Pigment)};
        });
    }
    if (!State.Changes.IsEmpty()) Voxels->ReadChanges(State.Changes);
}

void AIslandRouteVoxel::Publish()
{
    Voxels->WriteChanges(State.Changes);
    ForceNetUpdate();
}

bool AIslandRouteVoxel::Strike(const FHitResult& Hit, float RadiusScale, float DepthScale)
{
    if (!HasAuthority() || Hit.GetComponent() != Voxels || !Voxels->ApplyWorldCut(Hit.ImpactPoint, Hit.ImpactNormal, 62.*RadiusScale, DepthScale)) return false;
    const auto* Terrain = AVoxelTerrain::Find(GetWorld());
    if (!Terrain || !RefreshGrounding(*Terrain))
    {
        Publish();
        AIslandAttachedMud::RefreshFor(*this);
    }
    const auto Kind=GetKind();
    const FVector Local=Voxels->GetComponentTransform().InverseTransformPosition(Hit.ImpactPoint-Hit.ImpactNormal);
    const uint8 Pigment=IslandRouteFlora::Sample(Kind,Local,State.Variant).Pigment;
    const bool bRock=Kind==IslandRouteFlora::EShape::SplitReef || Kind==IslandRouteFlora::EShape::CoralSpur;
    const auto Surface=bRock ? EIslandChipSurface::Rock :
        (IslandRouteFlora::IsWoodyPigment(Pigment) ? EIslandChipSurface::Timber : EIslandChipSurface::Leaf);
    AIslandVoxelEdits::EmitChips(*GetWorld(),Hit,Surface,IslandRouteFlora::PigmentColor(Pigment));
    return true;
}

bool AIslandRouteVoxel::RefreshGrounding(const AVoxelTerrain& Terrain)
{
    const FVoxelDensity* Ground = Terrain.GetDensity();
    if (!HasAuthority() || !Ground || !Voxels->RemainingCount()) return false;
    const FTransform& Transform = Voxels->GetComponentTransform();
    const double Contact = Voxels->CellSpacing() * 1.1;
    TArray<FIslandLooseSamples> Loose;
    if (!Voxels->CullUnsupported([&](FVector Local)
    {
        const FVector World = Transform.TransformPosition(Local) - FVector(0., 0., Contact);
        return Ground->SignedDistanceAt(World) <= 0.;
    }, &Loose)) return false;
    const auto Kind = GetKind();
    const bool bRock = Kind==IslandRouteFlora::EShape::SplitReef || Kind==IslandRouteFlora::EShape::CoralSpur;
    AIslandFallingScenery::Release(*Voxels, Loose, bRock ? EIslandChipSurface::Rock : EIslandChipSurface::Leaf);
    Publish();
    AIslandAttachedMud::RefreshFor(*this);
    return true;
}
