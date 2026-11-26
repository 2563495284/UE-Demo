#include "Voxel/IslandRouteVoxel.h"
#include "Island/IslandSceneryVoxels.h"

#include "Engine/World.h"
#include "Materials/MaterialInterface.h"
#include "Misc/Crc.h"
#include "Net/UnrealNetwork.h"
#include "UObject/ConstructorHelpers.h"
#include "Voxel/VoxelTerrain.h"
#include "VoxelSurfaceKernel.h"

namespace
{
    constexpr int32 Band = VoxelSurfaceKernel::Band;
    constexpr int32 ChunkSide = VoxelSurfaceKernel::CellsPerChunk;

    FIntVector Rounded(FVector V)
    {
        return {FMath::RoundToInt(V.X), FMath::RoundToInt(V.Y), FMath::RoundToInt(V.Z)};
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
    return Inside(P) ? Samples[Index(P)] : int8(Band);
}

FBox UIslandRouteVoxelComponent::GetFieldLocalBounds() const
{
    return Samples.IsEmpty() ? FBox(ForceInit) : FBox(GridStart, GridStart + FVector(Size - FIntVector(1)) * Step);
}

bool UIslandRouteVoxelComponent::InitializeField(const FBox& LocalBounds, double Spacing,
    TFunctionRef<FRouteVoxelSample(FVector)> SampleShape)
{
    if (!LocalBounds.IsValid || !FMath::IsFinite(Spacing) || Spacing <= 0.) return false;
    const FVector Span = LocalBounds.GetSize() / Spacing;
    if (Span.GetMax() > 2048.) return false;
    const FIntVector Points(FMath::CeilToInt(Span.X) + 5, FMath::CeilToInt(Span.Y) + 5, FMath::CeilToInt(Span.Z) + 5);
    const int64 Num = int64(Points.X) * Points.Y * Points.Z;
    if (Num > 2000000) return false;
    Size = Points;
    Step = Spacing;
    GridStart = LocalBounds.Min - FVector(2. * Step);
    Samples.SetNumUninitialized(Num);
    Pigments.SetNumUninitialized(Num);
    SolidCount = 0;
    for (int32 I = 0; I < Samples.Num(); ++I)
    {
        const FRouteVoxelSample Value = SampleShape(GridStart + FVector(Coordinates(I)) * Step);
        const double D = Value.Distance * VoxelSurfaceKernel::Spacing / Step;
        int32 Signed = FMath::RoundToInt(FMath::Clamp(D, -double(Band), double(Band)));
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
    if (Step <= 0.) return false;
    const FVector P = (Local - GridStart) / Step;
    const FIntVector Low(FMath::FloorToInt(P.X), FMath::FloorToInt(P.Y), FMath::FloorToInt(P.Z));
    if (!Inside(Low)) return false;
    const FVector T = P - FVector(Low);
    const auto Plane = [&](int32 Z)
    {
        return FMath::Lerp(FMath::Lerp<double>(At({Low.X, Low.Y, Z}), At({Low.X+1, Low.Y, Z}), T.X),
            FMath::Lerp<double>(At({Low.X, Low.Y+1, Z}), At({Low.X+1, Low.Y+1, Z}), T.X), T.Y);
    };
    return FMath::Lerp(Plane(Low.Z), Plane(Low.Z+1), T.Z) < 0.;
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
    if (SolidCount == 0)
    {
        ClearAllMeshSections();
        return;
    }
    FProcMeshSection Combined;
    const double Scale = Step / VoxelSurfaceKernel::Spacing;
    for (int32 Z = 0; Z < Size.Z - 1; Z += ChunkSide)
    for (int32 Y = 0; Y < Size.Y - 1; Y += ChunkSide)
    for (int32 X = 0; X < Size.X - 1; X += ChunkSide)
    {
        const FIntVector Origin(X, Y, Z);
        VoxelSurfaceKernel::FChunkSamples Chunk;
        constexpr int32 Width = VoxelSurfaceKernel::FChunkSamples::Width;
        for (int32 K = 0; K < Width; ++K)
        for (int32 J = 0; J < Width; ++J)
        for (int32 I = 0; I < Width; ++I)
            Chunk.Values[I + Width * (J + Width * K)] = At(Origin + FIntVector(I-2, J-2, K-2));
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
    SetProcMeshSection(0, Combined);
}

bool UIslandRouteVoxelComponent::ApplyWorldCut(FVector Point, FVector OutwardNormal, double Radius)
{
    if (!SolidCount || Radius <= 0.) return false;
    const FTransform& Transform = GetComponentTransform();
    const FVector N = OutwardNormal.GetSafeNormal(UE_SMALL_NUMBER, FVector::UpVector);
    const FVector Center = Point - N * 12.;
    const double Depth = .69 * Radius;
    const FBox Bounds = FBox::BuildAABB(Center, FVector(Radius + Depth + Step * 2.)).TransformBy(Transform.Inverse());
    const FIntVector Low = Rounded((Bounds.Min - GridStart)/Step).ComponentMax(FIntVector(0));
    const FIntVector High = Rounded((Bounds.Max - GridStart)/Step).ComponentMin(Size - FIntVector(1));
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
        const int8 Value = FMath::RoundToInt(FMath::Clamp(-Tool * VoxelSurfaceKernel::Spacing / Step, -double(Band), double(Band)));
        if (Value <= Previous) continue;
        if (Previous < 0 && Value >= 0) --SolidCount;
        Previous = Value;
        bChanged = true;
    }
    if (bChanged) UpdateSurface();
    return bChanged;
}

bool UIslandRouteVoxelComponent::CullUnsupported(TFunctionRef<bool(FVector)> IsLocalAnchor)
{
    if (!SolidCount) return false;
    TBitArray<> Grounded(false, Samples.Num());
    TArray<int32> Reachable;
    for (int32 I = 0; I < Samples.Num(); ++I)
    {
        if (Samples[I] >= 0 || !IsLocalAnchor(GridStart + FVector(Coordinates(I)) * Step)) continue;
        Reachable.Add(I);
        Grounded[I] = true;
    }
    for (int32 Head = 0; Head < Reachable.Num(); ++Head)
    {
        const FIntVector P = Coordinates(Reachable[Head]);
        for (FIntVector Direction : VoxelSurfaceKernel::FaceNeighbors)
        {
            const FIntVector Next = P + Direction;
            if (!Inside(Next)) continue;
            const int32 I = Index(Next);
            if (Samples[I] >= 0 || Grounded[I]) continue;
            Grounded[I] = true;
            Reachable.Add(I);
        }
    }
    if (Reachable.Num() == SolidCount) return false;
    for (int32 I = 0; I < Samples.Num(); ++I)
        if (Samples[I] < 0 && !Grounded[I]) Samples[I] = Band;
    SolidCount = Reachable.Num();
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

bool AIslandRouteVoxel::Strike(const FHitResult& Hit, bool bWide)
{
    if (!HasAuthority() || Hit.GetComponent() != Voxels || !Voxels->ApplyWorldCut(Hit.ImpactPoint, Hit.ImpactNormal, bWide ? 124. : 62.)) return false;
    const auto* Terrain = AVoxelTerrain::Find(GetWorld());
    if (!Terrain || !RefreshGrounding(*Terrain)) Publish();
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
    if (!Voxels->CullUnsupported([&](FVector Local)
    {
        const FVector World = Transform.TransformPosition(Local) - FVector(0., 0., Contact);
        return Ground->SignedDistanceAt(World) <= 0.;
    })) return false;
    Publish();
    return true;
}
