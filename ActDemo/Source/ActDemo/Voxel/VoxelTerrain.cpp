#include "Voxel/VoxelTerrain.h"

#include "Async/Async.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Containers/StaticArray.h"
#include "EngineUtils.h"
#include "Generators/MarchingCubes.h"
#include "Misc/App.h"
#include "ProceduralMeshComponent.h"
#include "UObject/ConstructorHelpers.h"

struct FVoxelMeshBuild
{
    TSharedPtr<FVoxelDensity, ESPMode::ThreadSafe> Density;
    FVoxelDensityPatch Patch;
    TArray<TPair<int32, FProcMeshSection>> Chunks;
};

namespace
{
    TSharedPtr<FVoxelDensity, ESPMode::ThreadSafe> BuildDensity(EVoxelLayout Layout, TConstArrayView<FVoxelBrushStamp> Stamps)
    {
        auto Result = MakeShared<FVoxelDensity, ESPMode::ThreadSafe>(Layout);
        for (const FVoxelBrushStamp& Stamp : Stamps)
        {
            Result->Apply(Result->PrepareBrush(Stamp));
        }
        return Result;
    }

    struct FChunkSamples
    {
        static constexpr int32 Width = VoxelPlayground::CellsPerChunk + 5;
        TStaticArray<int16, Width * Width * Width> Values;

        int16 Sample(int32 X, int32 Y, int32 Z) const
        {
            return Values[(X + 2) + Width * ((Y + 2) + Width * (Z + 2))];
        }

        double Interpolate(const FVector& Position) const
        {
            const int32 X = FMath::FloorToInt(Position.X);
            const int32 Y = FMath::FloorToInt(Position.Y);
            const int32 Z = FMath::FloorToInt(Position.Z);
            const FVector Alpha = Position - FVector(X, Y, Z);
            const auto Row = [&](int32 DY, int32 DZ)
            {
                return FMath::Lerp<double>(Sample(X, Y + DY, Z + DZ), Sample(X + 1, Y + DY, Z + DZ), Alpha.X);
            };
            return FMath::Lerp(FMath::Lerp(Row(0, 0), Row(1, 0), Alpha.Y), FMath::Lerp(Row(0, 1), Row(1, 1), Alpha.Y), Alpha.Z);
        }

        FVector Normal(const FVector& Position) const
        {
            const auto Derivative = [&](const FVector& Offset) { return Interpolate(Position + Offset) - Interpolate(Position - Offset); };
            return FVector(Derivative({1, 0, 0}), Derivative({0, 1, 0}), Derivative({0, 0, 1})).GetSafeNormal(UE_SMALL_NUMBER, FVector::UpVector);
        }
    };

    FProcMeshSection MeshChunk(const FVoxelDensity& Density, int32 Index, const FVoxelDensityPatch* Patch)
    {
        using namespace VoxelPlayground;
        using namespace UE::Geometry;
        const FIntVector First = Density.Grid.ChunkCoordinates(Index) * CellsPerChunk;
        const FVector Start(Density.Grid.Origin + First * Spacing);
        FChunkSamples Samples;
        int32 SampleIndex = 0;
        bool bSolid = false;
        bool bAir = false;
        for (int32 Z = -2; Z <= CellsPerChunk + 2; ++Z)
        {
            for (int32 Y = -2; Y <= CellsPerChunk + 2; ++Y)
            {
                for (int32 X = -2; X <= CellsPerChunk + 2; ++X)
                {
                    const int16 Value = Density.Sample(First + FIntVector(X, Y, Z), Patch);
                    Samples.Values[SampleIndex++] = Value;
                    if (X >= 0 && X <= CellsPerChunk && Y >= 0 && Y <= CellsPerChunk && Z >= 0 && Z <= CellsPerChunk)
                    {
                        bSolid |= Value < 0;
                        bAir |= Value >= 0;
                    }
                }
            }
        }
        if (!bSolid || !bAir)
        {
            return {};
        }
        // FMarchingCubes covers one more cell than the bounding-box width / spacing.
        FMarchingCubes Generator;
        Generator.Bounds = FAxisAlignedBox3d(Start, Start + FVector((CellsPerChunk - 1) * Spacing));
        Generator.CubeSize = Spacing;
        Generator.bParallelCompute = false;
        Generator.Implicit = [&](const FVector& Position)
        {
            const FVector Offset = (Position - Start) / Spacing;
            const FIntVector Corner(FMath::RoundToInt(Offset.X), FMath::RoundToInt(Offset.Y), FMath::RoundToInt(Offset.Z));
            const int16 Value = Samples.Sample(Corner.X, Corner.Y, Corner.Z);
            // Give a sample exactly on the surface a common sign at every chunk boundary.
            return Value == 0 ? 0.01 : static_cast<double>(Value);
        };
        Generator.Generate();

        FProcMeshSection Section;
        Section.ProcVertexBuffer.SetNum(Generator.Vertices.Num());
        for (int32 VertexIndex = 0; VertexIndex < Generator.Vertices.Num(); ++VertexIndex)
        {
            FProcMeshVertex& Vertex = Section.ProcVertexBuffer[VertexIndex];
            Vertex.Position = Generator.Vertices[VertexIndex];
            Vertex.Normal = Samples.Normal((Vertex.Position - Start) / Spacing);
            FVector Tangent, Bitangent;
            Vertex.Normal.FindBestAxisVectors(Tangent, Bitangent);
            Vertex.Tangent = FProcMeshTangent(Tangent, false);
            Vertex.UV0 = FVector2D(Vertex.Position.X, Vertex.Position.Y) / 100.0;
            Section.SectionLocalBox += Vertex.Position;
        }
        Section.ProcIndexBuffer.Reserve(Generator.Triangles.Num() * 3);
        for (const FIndex3i& Triangle : Generator.Triangles)
        {
            const FProcMeshVertex& A = Section.ProcVertexBuffer[Triangle.A];
            const FProcMeshVertex& B = Section.ProcVertexBuffer[Triangle.B];
            const FProcMeshVertex& C = Section.ProcVertexBuffer[Triangle.C];
            const FVector Face = FVector::CrossProduct(B.Position - A.Position, C.Position - A.Position);
            if (Face.SizeSquared() <= UE_SMALL_NUMBER)
            {
                continue;
            }
            // ProceduralMeshComponent expects clockwise winding from the visible side.
            const bool bReverse = FVector::DotProduct(Face, A.Normal + B.Normal + C.Normal) > 0.0;
            Section.ProcIndexBuffer.Add(Triangle.A);
            Section.ProcIndexBuffer.Add(bReverse ? Triangle.C : Triangle.B);
            Section.ProcIndexBuffer.Add(bReverse ? Triangle.B : Triangle.C);
        }
        Section.bEnableCollision = !Section.ProcIndexBuffer.IsEmpty();
        return Section;
    }
}

AVoxelTerrain::AVoxelTerrain()
    : SearchCircles(IslandRules::DefaultSearchCircles())
{
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.bStartWithTickEnabled = false;
    SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Origin")));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube"));
    FragmentMesh = Cube.Object;

    for (int32 Index = 0; Index < VoxelPlayground::ChunkCount; ++Index)
    {
        UProceduralMeshComponent* Chunk = CreateDefaultSubobject<UProceduralMeshComponent>(*FString::Printf(TEXT("Chunk%02d"), Index));
        ConfigureChunk(*Chunk);
        MeshChunks.Add(Chunk);
    }
}

void AVoxelTerrain::ConfigureChunk(UProceduralMeshComponent& Chunk)
{
    Chunk.SetupAttachment(RootComponent);
    Chunk.SetCollisionProfileName(TEXT("BlockAll"));
    Chunk.SetGenerateOverlapEvents(false);
    Chunk.SetCanEverAffectNavigation(false);
    Chunk.bUseComplexAsSimpleCollision = true;
    // Keep the visible surface and walking collision on the same committed version.
    Chunk.bUseAsyncCooking = false;
}

UProceduralMeshComponent* AVoxelTerrain::CreateChunk(int32 Index)
{
    UProceduralMeshComponent* Mesh = NewObject<UProceduralMeshComponent>(this);
    ConfigureChunk(*Mesh);
    MeshChunks[Index] = Mesh;
    Mesh->RegisterComponent();
    return Mesh;
}

AVoxelTerrain* AVoxelTerrain::Find(UWorld* World)
{
    const TActorIterator<AVoxelTerrain> Terrain(World);
    return Terrain ? *Terrain : nullptr;
}

uint32 AVoxelTerrain::GetInitialChecksum() const
{
    uint32 Hash = GetChecksum();
    if (Layout == EVoxelLayout::Island)
    {
        for (const FIslandSearchCircle& Circle : SearchCircles)
        {
            Hash = (Hash ^ static_cast<uint32>(Circle.Center.X)) * 16777619u;
            Hash = (Hash ^ static_cast<uint32>(Circle.Center.Y)) * 16777619u;
            Hash = (Hash ^ static_cast<uint32>(Circle.Radius)) * 16777619u;
        }
    }
    return Hash;
}

#if WITH_EDITOR
void AVoxelTerrain::RebuildEditorPreview()
{
    ClearEditorPreview();
    PreviewDensity = BuildDensity(Layout, AuthoredStamps);
    PreviewChunks.SetNum(PreviewDensity->Grid.NumChunks());
    for (int32 Index = 0; Index < PreviewChunks.Num(); ++Index)
    {
        RefreshPreviewChunk(Index);
    }
}

void AVoxelTerrain::ClearEditorPreview()
{
    for (UProceduralMeshComponent* Chunk : PreviewChunks)
    {
        if (Chunk)
        {
            Chunk->DestroyComponent();
        }
    }
    PreviewChunks.Reset();
    PreviewDensity.Reset();
}

void AVoxelTerrain::RefreshPreviewChunk(int32 Index)
{
    const FProcMeshSection Section = MeshChunk(*PreviewDensity, Index, nullptr);
    UProceduralMeshComponent* Chunk = PreviewChunks[Index];
    if (!Section.bEnableCollision)
    {
        if (Chunk)
        {
            Chunk->ClearAllMeshSections();
        }
        return;
    }
    if (!Chunk)
    {
        Chunk = NewObject<UProceduralMeshComponent>(this, NAME_None, RF_Transient | RF_DuplicateTransient);
        ConfigureChunk(*Chunk);
        Chunk->SetHiddenInGame(true);
        Chunk->RegisterComponent();
        PreviewChunks[Index] = Chunk;
    }
    Chunk->SetProcMeshSection(0, Section);
}

bool AVoxelTerrain::TraceEditorPreview(const FVector& Start, const FVector& End, FHitResult& Hit) const
{
    bool bFound = false;
    const FCollisionQueryParams Query(SCENE_QUERY_STAT(VoxelEditor), true);
    for (UProceduralMeshComponent* Chunk : PreviewChunks)
    {
        if (!Chunk || Chunk->GetNumSections() == 0 || !FMath::LineBoxIntersection(Chunk->Bounds.GetBox(), Start, End, End - Start))
        {
            continue;
        }
        FHitResult Candidate;
        if (Chunk->LineTraceComponent(Candidate, Start, End, Query) && (!bFound || Candidate.Time < Hit.Time))
        {
            Hit = Candidate;
            bFound = true;
        }
    }
    return bFound;
}

bool AVoxelTerrain::ApplyEditorStamps(TConstArrayView<FVoxelBrushStamp> Stamps)
{
    if (!PreviewDensity)
    {
        return false;
    }
    TSet<int32> DirtyChunks;
    for (const FVoxelBrushStamp& Stamp : Stamps)
    {
        const FVoxelDensityPatch Patch = PreviewDensity->PrepareBrush(Stamp);
        if (!Patch.Samples.IsEmpty())
        {
            PreviewDensity->Apply(Patch);
            AuthoredStamps.Add(Stamp);
            DirtyChunks.Append(Patch.Chunks);
        }
    }
    for (int32 Index : DirtyChunks)
    {
        RefreshPreviewChunk(Index);
    }
    return !DirtyChunks.IsEmpty();
}

void AVoxelTerrain::SetSearchCircle(int32 Index, FIslandSearchCircle Circle)
{
    SearchCircles[Index] = Circle;
    MarkPackageDirty();
}
#endif

void AVoxelTerrain::BeginPlay()
{
    Super::BeginPlay();
    MeshChunks.SetNum(FVoxelGrid::ForLayout(Layout).NumChunks());
    StartBuild({}, {}, {}, {});
}

bool AVoxelTerrain::TraceRock(const FVector& Start, const FVector& End, const AActor* Ignore, FHitResult& Hit) const
{
    FCollisionQueryParams Query(SCENE_QUERY_STAT(VoxelRock), false, Ignore);
    return bReady && GetWorld()->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility, Query) && Hit.GetActor() == this;
}

bool AVoxelTerrain::BuildEdit(const FVoxelEdit& Edit, TFunction<bool()> CanCommit, TFunction<void(bool)> Completion)
{
    if (!bReady || bBuilding || Edit.Sequence != AppliedSequence + 1 || !IsValidEdit(Edit))
    {
        return false;
    }
    FVoxelDensityPatch Patch = Density->PrepareEdit(Edit);
    if (Patch.Samples.IsEmpty())
    {
        return false;
    }
    StartBuild(MoveTemp(Patch), Edit, MoveTemp(CanCommit), MoveTemp(Completion));
    return true;
}

void AVoxelTerrain::StartBuild(FVoxelDensityPatch Patch, TOptional<FVoxelEdit> Edit,
    TFunction<bool()> CanCommit, TFunction<void(bool)> Completion)
{
    bBuilding = true;
    const TWeakObjectPtr<AVoxelTerrain> WeakTerrain(this);
    TArray<FVoxelBrushStamp> Stamps = Density ? TArray<FVoxelBrushStamp>() : AuthoredStamps;
    Async(EAsyncExecution::ThreadPool, [WeakTerrain, Layout = Layout, Source = Density, Stamps = MoveTemp(Stamps), Edit, Patch = MoveTemp(Patch),
        CanCommit = MoveTemp(CanCommit), Completion = MoveTemp(Completion)]() mutable
    {
        FVoxelMeshBuild Build;
        // The owning terrain never mutates the source until this worker returns to the game thread.
        Build.Density = MoveTemp(Source);
        if (!Build.Density)
        {
            Build.Density = BuildDensity(Layout, Stamps);
        }
        Build.Patch = MoveTemp(Patch);
        if (Edit.IsSet())
        {
            for (int32 Index : Build.Patch.Chunks)
            {
                Build.Chunks.Emplace(Index, MeshChunk(*Build.Density, Index, &Build.Patch));
            }
        }
        else
        {
            for (int32 Index = 0; Index < Build.Density->Grid.NumChunks(); ++Index)
            {
                FProcMeshSection Section = MeshChunk(*Build.Density, Index, nullptr);
                if (Section.bEnableCollision)
                {
                    Build.Chunks.Emplace(Index, MoveTemp(Section));
                }
            }
        }
        AsyncTask(ENamedThreads::GameThread, [WeakTerrain, Edit, Build = MoveTemp(Build),
            CanCommit = MoveTemp(CanCommit), Completion = MoveTemp(Completion)]() mutable
        {
            AVoxelTerrain* Terrain = WeakTerrain.Get();
            if (!Terrain || !Terrain->HasActorBegunPlay() || Terrain->IsActorBeingDestroyed())
            {
                return;
            }
            const bool bCommit = !CanCommit || CanCommit();
            if (bCommit)
            {
                Terrain->Commit(Build);
                if (Edit.IsSet())
                {
                    Terrain->AppliedSequence = Edit->Sequence;
                    Terrain->SpawnFragments(Edit.GetValue());
                }
                else
                {
                    Terrain->bReady = true;
                    UE_LOG(LogTemp, Display, TEXT("VOXEL_INITIAL crc=%08x map=%08x"), Terrain->GetChecksum(), Terrain->GetInitialChecksum());
                }
            }
            Terrain->bBuilding = false;
            if (Completion)
            {
                Completion(bCommit);
            }
        });
    });
}

void AVoxelTerrain::Commit(FVoxelMeshBuild& Build)
{
    for (const TPair<int32, FProcMeshSection>& Chunk : Build.Chunks)
    {
        UProceduralMeshComponent* Mesh = MeshChunks[Chunk.Key];
        if (Chunk.Value.bEnableCollision)
        {
            (Mesh ? Mesh : CreateChunk(Chunk.Key))->SetProcMeshSection(0, Chunk.Value);
        }
        else if (Mesh && Mesh->GetNumSections() > 0)
        {
            Mesh->ClearAllMeshSections();
        }
    }
    Density = MoveTemp(Build.Density);
    Density->Apply(Build.Patch);
}

void AVoxelTerrain::SpawnFragments(const FVoxelEdit& Edit)
{
    if (!FragmentMesh || Edit.Action == EVoxelAction::Fill || !FApp::CanEverRender() || !GetWorld()->GetFirstLocalPlayerFromController())
    {
        return;
    }
    for (int32 Index = 0; Index < 7; ++Index)
    {
        UStaticMeshComponent* Mesh = NewObject<UStaticMeshComponent>(this);
        Mesh->SetStaticMesh(FragmentMesh);
        Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Mesh->SetCanEverAffectNavigation(false);
        Mesh->SetCastShadow(false);
        Mesh->RegisterComponent();
        Mesh->SetWorldLocation(FVector(Edit.Center) + FMath::VRand() * 65.0);
        Mesh->SetWorldScale3D(FVector(FMath::FRandRange(.055f, .13f)));
        Mesh->SetWorldRotation(FMath::VRand().Rotation());
        Fragments.Add({Mesh, FMath::VRand() * 210.0 + FVector(0, 0, 180), 0.f});
    }
    SetActorTickEnabled(true);
}

void AVoxelTerrain::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    for (int32 Index = Fragments.Num() - 1; Index >= 0; --Index)
    {
        FFragment& Fragment = Fragments[Index];
        Fragment.Age += DeltaSeconds;
        if (UStaticMeshComponent* Mesh = Fragment.Mesh.Get(); Mesh && Fragment.Age < .45f)
        {
            Fragment.Velocity.Z -= 630.f * DeltaSeconds;
            Mesh->AddWorldOffset(Fragment.Velocity * DeltaSeconds);
            Mesh->AddLocalRotation(FRotator(200.f * DeltaSeconds, 110.f * DeltaSeconds, 0));
        }
        else
        {
            if (Mesh)
            {
                Mesh->DestroyComponent();
            }
            Fragments.RemoveAtSwap(Index, 1, EAllowShrinking::No);
        }
    }
    if (Fragments.IsEmpty())
    {
        SetActorTickEnabled(false);
    }
}
