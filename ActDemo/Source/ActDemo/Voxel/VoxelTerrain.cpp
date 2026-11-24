#include "Voxel/VoxelTerrain.h"
#include "Voxel/ExpeditionSurface.h"
#include "Voxel/ExpeditionImpact.h"
#if !UE_BUILD_SHIPPING
#include "Tests/ExpeditionWalk.h"
#endif

#include "Async/Async.h"
#include "Components/SceneComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "GameFramework/PlayerController.h"
#include "Components/StaticMeshComponent.h"
#include "Containers/StaticArray.h"
#include "EngineUtils.h"
#include "Generators/MarchingCubes.h"
#include "Misc/App.h"
#include "Materials/MaterialInterface.h"
#include "ProceduralMeshComponent.h"
#include "UObject/ConstructorHelpers.h"

struct FVoxelMeshBuild
{
    TSharedPtr<FVoxelDensity, ESPMode::ThreadSafe> Density;
    FVoxelDensityPatch Patch;
    TArray<TPair<int32, FProcMeshSection>> Chunks;
    TArray<TPair<int32, FProcMeshSection>> Tiles;
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

        FVector Gradient(const FIntVector& P) const
        {
            const int16* At = &Values[P.X + 2 + Width * (P.Y + 2 + Width * (P.Z + 2))];
            return FVector(At[1] - At[-1], At[Width] - At[-Width], At[Width*Width] - At[-Width*Width]);
        }
    };

    // Use GeometryCore's topology with direct edge indices for our fixed, dense 16-cell chunks.
    // Adjacent cells share the same edge slot, including when their corner order is reversed.
    struct FChunkTopology : UE::Geometry::FMarchingCubes
    {
        using FMarchingCubes::EdgeIndices;
        using FMarchingCubes::TriTable;
    };

    FProcMeshSection MeshChunk(const FVoxelDensity& Density, int32 Index, const FVoxelDensityPatch* Patch)
    {
        using namespace VoxelPlayground;
        constexpr int32 Side = CellsPerChunk;
        constexpr int32 Width = FChunkSamples::Width;
        constexpr int32 EdgeWidth = Side + 1;
        const FIntVector First = Density.Grid.ChunkCoordinates(Index) * Side;
        const FVector Start(Density.Grid.Origin + First * Spacing);
        FChunkSamples Samples;
        for (int32 Z = 0; Z < Width; ++Z)
        {
            for (int32 Y = 0; Y < Width; ++Y)
            {
                Density.CopySampleRow(First + FIntVector(-2, Y - 2, Z - 2), Width, &Samples.Values[Width * (Y + Width * Z)]);
            }
        }
        if (Patch)
        {
            const int32 Row = Density.Grid.Cells.X + 1;
            const int32 Plane = Row * (Density.Grid.Cells.Y + 1);
            for (const auto& Pair : Patch->Samples)
            {
                const FIntVector Point(Pair.Key % Row, (Pair.Key % Plane) / Row, Pair.Key / Plane);
                const FIntVector Local = Point - First + FIntVector(2);
                if (Local.GetMin() >= 0 && Local.GetMax() < Width)
                    Samples.Values[Local.X + Width * (Local.Y + Width * Local.Z)] = Pair.Value;
            }
        }

        static const FIntVector Corners[] = {{0,0,0},{1,0,0},{1,0,1},{0,0,1},{0,1,0},{1,1,0},{1,1,1},{0,1,1}};
        static constexpr int32 CornerOffsets[] = {0,1, 1+Width*Width,Width*Width, Width,Width+1,Width+1+Width*Width,Width+Width*Width};
        TStaticArray<uint16, EdgeWidth * EdgeWidth * EdgeWidth * 3> EdgeVertices;
        FMemory::Memset(EdgeVertices.GetData(), 0xff, sizeof(EdgeVertices));
        uint32 Inside[EdgeWidth][EdgeWidth];
        for (int32 Z = 0; Z < EdgeWidth; ++Z)
            for (int32 Y = 0; Y < EdgeWidth; ++Y)
            {
                uint32 Bits = 0;
                for (int32 X = 0; X < EdgeWidth; ++X) Bits |= uint32(Samples.Sample(X,Y,Z) < 0) << X;
                Inside[Z][Y] = Bits;
            }
        FProcMeshSection Section;
        for (int32 Z = 0; Z < Side; ++Z)
        {
            for (int32 Y = 0; Y < Side; ++Y)
            {
                // Eight equal corner signs cannot emit a triangle. Skip whole spans of air or solid rock.
                const uint32 AnySolid = Inside[Z][Y] | Inside[Z+1][Y] | Inside[Z][Y+1] | Inside[Z+1][Y+1];
                const uint32 AllSolid = Inside[Z][Y] & Inside[Z+1][Y] & Inside[Z][Y+1] & Inside[Z+1][Y+1];
                uint32 Crossings = (AnySolid | (AnySolid >> 1)) & ~(AllSolid & (AllSolid >> 1)) & ((1u << Side) - 1);
                while (Crossings)
                {
                    const int32 X = FMath::CountTrailingZeros(Crossings);
                    Crossings &= Crossings - 1;
                    const int16* Cell = &Samples.Values[X + 2 + Width * (Y + 2 + Width * (Z + 2))];
                    int32 Mask = 0;
                    for (int32 C = 0; C < 8; ++C) Mask |= int32(Cell[CornerOffsets[C]] < 0) << C;
                    const FIntVector Origin(X,Y,Z);
                    const auto GetVertex = [&](int32 Edge) -> int32
                    {
                        const int32 A = FChunkTopology::EdgeIndices[Edge][0], B = FChunkTopology::EdgeIndices[Edge][1];
                        const FIntVector Delta = Corners[B] - Corners[A];
                        const FIntVector Lower = Origin + Corners[A].ComponentMin(Corners[B]);
                        const int32 Axis = Delta.X != 0 ? 0 : (Delta.Y != 0 ? 1 : 2);
                        uint16& Cached = EdgeVertices[Axis + 3 * (Lower.X + EdgeWidth * (Lower.Y + EdgeWidth * Lower.Z))];
                        if (Cached != MAX_uint16) return Cached;
                        const double VA = Cell[CornerOffsets[A]] == 0 ? .01 : double(Cell[CornerOffsets[A]]);
                        const double VB = Cell[CornerOffsets[B]] == 0 ? .01 : double(Cell[CornerOffsets[B]]);
                        const double Alpha = VA / (VA - VB);
                        const FVector Local = FVector(Origin + Corners[A]) + FVector(Delta) * Alpha;
                        Cached = Section.ProcVertexBuffer.Num();
                        FProcMeshVertex& Vertex = Section.ProcVertexBuffer.AddDefaulted_GetRef();
                        Vertex.Position = Start + Local * Spacing;
                        // A marching-cubes vertex lies on a grid edge, so its trilinear gradient
                        // is the linear interpolation of the two endpoint gradients.
                        Vertex.Normal = FMath::Lerp(Samples.Gradient(Origin + Corners[A]), Samples.Gradient(Origin + Corners[B]), Alpha)
                            .GetSafeNormal(UE_SMALL_NUMBER, FVector::UpVector);
                        FVector Tangent, Bitangent;
                        Vertex.Normal.FindBestAxisVectors(Tangent, Bitangent);
                        Vertex.Tangent = FProcMeshTangent(Tangent, false);
                        Vertex.UV0 = FVector2D(Vertex.Position) / 100.;
                        Section.SectionLocalBox += Vertex.Position;
                        return Cached;
                    };
                    for (uint64 Triangles = FChunkTopology::TriTable[Mask]; Triangles; Triangles >>= 12)
                    {
                        const int32 A = GetVertex(Triangles & 15);
                        const int32 B = GetVertex((Triangles >> 4) & 15);
                        const int32 C = GetVertex((Triangles >> 8) & 15);
                        const auto& VA = Section.ProcVertexBuffer[A];
                        const auto& VB = Section.ProcVertexBuffer[B];
                        const auto& VC = Section.ProcVertexBuffer[C];
                        const FVector Face = FVector::CrossProduct(VB.Position - VA.Position, VC.Position - VA.Position);
                        if (Face.SizeSquared() <= UE_SMALL_NUMBER) continue;
                        const bool bReverse = FVector::DotProduct(Face, VA.Normal + VB.Normal + VC.Normal) > 0.;
                        Section.ProcIndexBuffer.Append({A, bReverse ? C : B, bReverse ? B : C});
                    }
                }
            }
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
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> Rock(TEXT("/Game/LevelPrototyping/Materials/MI_PrototypeGrid_Gray"));
    ExpeditionRockMaterial = Rock.Object;

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
    if (Layout == EVoxelLayout::Expedition) Chunk.SetMaterial(0,ExpeditionRockMaterial);
    Chunk.SetGenerateOverlapEvents(false);
    Chunk.SetCanEverAffectNavigation(false);
    Chunk.bUseComplexAsSimpleCollision = true;
    // Keep the visible surface and walking collision on the same committed version.
    Chunk.bUseAsyncCooking = false;
}

UProceduralMeshComponent* AVoxelTerrain::CreateChunk(int32 Index)
{
    // A guest can stand on this chunk as soon as a sand spill or dig is committed.
    // Give CharacterMovement the same base name even when peers build chunks in a different order.
    const FName Name(*FString::Printf(TEXT("VoxelCollision_%d"), Index));
    UProceduralMeshComponent* Mesh = NewObject<UProceduralMeshComponent>(this, Name);
    Mesh->SetNetAddressable();
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
    // Include the terrain edit protocol revision in the expedition room readiness check.
    constexpr uint32 ExpeditionEditProtocol = 4;
    if (Layout == EVoxelLayout::Expedition) Hash = HashCombineFast(Hash, ExpeditionEditProtocol);
    if (Layout != EVoxelLayout::Playground)
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

UProceduralMeshComponent* AVoxelTerrain::MakeSurfaceComponent(bool bPersistent)
{
    UProceduralMeshComponent* Mesh = NewObject<UProceduralMeshComponent>(this, NAME_None, bPersistent ? RF_Transactional : RF_Transient);
    ConfigureChunk(*Mesh);
    if (bPersistent) AddInstanceComponent(Mesh);
    Mesh->RegisterComponent();
    return Mesh;
}

#if WITH_EDITOR
void AVoxelTerrain::BakeExpeditionSurface()
{
    check(Layout == EVoxelLayout::Expedition);
    const auto Grid = FVoxelGrid::ForLayout(Layout);
    BakedTiles.SetNum(ExpeditionSurface::TileCount(Grid));
    for (int32 Index=0; Index<BakedTiles.Num(); ++Index)
    {
        FProcMeshSection Section = ExpeditionSurface::BuildTile(Grid,Index,{});
        if (!Section.bEnableCollision)
        {
            if (BakedTiles[Index]) BakedTiles[Index]->ClearAllMeshSections();
            continue;
        }
        if (!BakedTiles[Index]) BakedTiles[Index]=MakeSurfaceComponent(true);
        if (!BakedTiles[Index]->GetNumOverrideMaterials()) BakedTiles[Index]->SetMaterial(0,ExpeditionRockMaterial);
        BakedTiles[Index]->SetProcMeshSection(0,Section);
    }
}

void AVoxelTerrain::UpdatePreviewTile(int32 Tile)
{
    if (!BakedTiles.IsValidIndex(Tile) || !BakedTiles[Tile]) return;
    UProceduralMeshComponent* Base = BakedTiles[Tile];
    if (!PreviewTiles[Tile])
    {
        PreviewTiles[Tile] = MakeSurfaceComponent(false);
        PreviewTiles[Tile]->SetHiddenInGame(true);
    }
    Base->SetVisibility(false);
    Base->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    PreviewTiles[Tile]->SetProcMeshSection(0, ExpeditionSurface::BuildTile(PreviewDensity->Grid, Tile, PreviewDensity->EditedColumns()));
}

void AVoxelTerrain::RebuildEditorPreview()
{
    ClearEditorPreview();
    PreviewDensity = BuildDensity(Layout, AuthoredStamps);
    PreviewChunks.SetNum(PreviewDensity->Grid.NumChunks());
    if (Layout == EVoxelLayout::Expedition)
    {
        PreviewTiles.SetNum(BakedTiles.Num());
        const auto Count = PreviewDensity->Grid.Chunks();
        TSet<int32> Tiles;
        for (int32 Column : PreviewDensity->EditedColumns())
        {
            for (int32 Z=0; Z<Count.Z; ++Z) RefreshPreviewChunk(Column + Z*Count.X*Count.Y);
            Tiles.Add(ExpeditionSurface::TileForColumn(PreviewDensity->Grid,Column));
        }
        for (int32 Tile : Tiles) UpdatePreviewTile(Tile);
        return;
    }
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
    for (int32 Index=0; Index<PreviewTiles.Num(); ++Index)
    {
        if (!PreviewTiles[Index]) continue;
        PreviewTiles[Index]->DestroyComponent();
        if (BakedTiles.IsValidIndex(Index) && BakedTiles[Index])
        {
            BakedTiles[Index]->SetVisibility(true);
            BakedTiles[Index]->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
        }
    }
    PreviewTiles.Reset();
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
    if (Layout == EVoxelLayout::Expedition)
    {
        const auto Trace = [&](UProceduralMeshComponent* Mesh)
        {
            if (!Mesh || !Mesh->IsVisible() || !FMath::LineBoxIntersection(Mesh->Bounds.GetBox(),Start,End,End-Start)) return;
            FHitResult Candidate;
            if (Mesh->LineTraceComponent(Candidate,Start,End,Query) && (!bFound || Candidate.Time < Hit.Time))
            {
                Hit=Candidate;
                bFound=true;
            }
        };
        for (UProceduralMeshComponent* Tile : BakedTiles) Trace(Tile);
        for (UProceduralMeshComponent* Tile : PreviewTiles) Trace(Tile);
    }
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
    TSet<int32> DirtyTiles;
    for (const FVoxelBrushStamp& Stamp : Stamps)
    {
        const FVoxelDensityPatch Patch = PreviewDensity->PrepareBrush(Stamp);
        if (!Patch.Samples.IsEmpty())
        {
            if (Layout == EVoxelLayout::Expedition)
            {
                TArray<int32> Expanded;
                TSet<int32> Columns;
                ExpeditionSurface::ExpandFirstEdit(*PreviewDensity,Patch,Expanded,Columns);
                DirtyChunks.Append(Expanded);
                for (int32 Column : Columns.Difference(PreviewDensity->EditedColumns()))
                    DirtyTiles.Add(ExpeditionSurface::TileForColumn(PreviewDensity->Grid,Column));
            }
            PreviewDensity->Apply(Patch);
            AuthoredStamps.Add(Stamp);
            DirtyChunks.Append(Patch.Chunks);
        }
    }
    for (int32 Index : DirtyChunks)
    {
        RefreshPreviewChunk(Index);
    }
    for (int32 Tile : DirtyTiles) UpdatePreviewTile(Tile);
    return !DirtyChunks.IsEmpty();
}

void AVoxelTerrain::SetSearchCircle(int32 Index, FIslandSearchCircle Circle)
{
    SearchCircles[Index] = Circle;
    MarkPackageDirty();
}
#endif

void AVoxelTerrain::PostLoad()
{
    Super::PostLoad();
    // The editor temporarily hides coarse tiles underneath its transient sculpt preview.
    // A level saved during a stroke must reload its original baked collision and visibility.
    for (UProceduralMeshComponent* Tile : BakedTiles)
    {
        if (!Tile) continue;
        if (!Tile->GetNumOverrideMaterials()) Tile->SetMaterial(0,ExpeditionRockMaterial);
        Tile->SetVisibility(true);
        Tile->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
    }
}

void AVoxelTerrain::BeginPlay()
{
    Super::BeginPlay();
#if !UE_BUILD_SHIPPING
    if (Layout == EVoxelLayout::Expedition && HasAuthority() && FParse::Param(FCommandLine::Get(),TEXT("ExpeditionWalkQA")))
        GetWorld()->SpawnActor<AExpeditionWalk>();
#endif
    if (Layout == EVoxelLayout::Expedition)
    {
        for (UProceduralMeshComponent* Chunk : MeshChunks) if (Chunk) Chunk->SetMaterial(0,ExpeditionRockMaterial);
        for (UProceduralMeshComponent* Tile : BakedTiles) if (Tile && !Tile->GetNumOverrideMaterials()) Tile->SetMaterial(0,ExpeditionRockMaterial);
    }
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
    const double SubmittedAt = FPlatformTime::Seconds();
    const TWeakObjectPtr<AVoxelTerrain> WeakTerrain(this);
    TArray<FVoxelBrushStamp> Stamps = Density ? TArray<FVoxelBrushStamp>() : AuthoredStamps;
    if (ImmediateFrame != GFrameCounter)
    {
        ImmediateFrame = GFrameCounter;
        ImmediateMs = 0.;
    }
    // Re-editing a small, already materialized area can finish before the next frame.
    // Cap work per frame and leave first-time tile promotion and larger patches on a worker.
    bool bImmediate = Edit.IsSet() && Layout == EVoxelLayout::Expedition && Density
        && Patch.Chunks.Num() <= 8 && ImmediateMs < 4.;
    if (bImmediate)
    {
        const FIntVector Count = Density->Grid.Chunks();
        const int32 Stride = Count.X * Count.Y;
        for (int32 Index : Patch.Chunks)
        {
            if (!Density->EditedColumns().Contains(Index % Stride)) { bImmediate = false; break; }
        }
    }
    auto BuildMesh = [WeakTerrain, SubmittedAt, bImmediate, Layout = Layout, Source = Density, Stamps = MoveTemp(Stamps), Edit, Patch = MoveTemp(Patch),
        CanCommit = MoveTemp(CanCommit), Completion = MoveTemp(Completion)]() mutable
    {
        const double WorkerAt = FPlatformTime::Seconds();
        FVoxelMeshBuild Build;
        // The owning terrain never mutates the source until this worker returns to the game thread.
        Build.Density = MoveTemp(Source);
        if (!Build.Density)
        {
            Build.Density = BuildDensity(Layout, Stamps);
        }
        Build.Patch = MoveTemp(Patch);
        if (Layout == EVoxelLayout::Expedition)
        {
            const auto& D = *Build.Density;
            const FIntVector Count = D.Grid.Chunks();
            TArray<int32> Sections;
            TSet<int32> Columns;
            TSet<int32> ChangedTiles;
            if (Edit.IsSet())
            {
                ExpeditionSurface::ExpandFirstEdit(D, Build.Patch, Sections, Columns);
                for (int32 Column : Columns.Difference(D.EditedColumns())) ChangedTiles.Add(ExpeditionSurface::TileForColumn(D.Grid, Column));
            }
            else
            {
                Columns = D.EditedColumns();
                for (int32 Column : Columns)
                {
                    ChangedTiles.Add(ExpeditionSurface::TileForColumn(D.Grid,Column));
                    for (int32 Z=0; Z<Count.Z; ++Z) Sections.Add(Column + Z*Count.X*Count.Y);
                }
            }
            for (int32 Tile : ChangedTiles) Build.Tiles.Emplace(Tile,ExpeditionSurface::BuildTile(D.Grid,Tile,Columns));
            for (int32 Index : Sections) Build.Chunks.Emplace(Index,MeshChunk(D,Index,&Build.Patch));
        }
        else if (Edit.IsSet())
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
        auto Finish = [WeakTerrain, Edit, bImmediate, SubmittedAt, WorkerAt, BuiltAt = FPlatformTime::Seconds(), Build = MoveTemp(Build),
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
                const double CommitAt = FPlatformTime::Seconds();
                Terrain->Commit(Build);
                if (Edit.IsSet())
                {
                    Terrain->AppliedSequence = Edit->Sequence;
                    if (Terrain->CanSeeExcavation(Edit.GetValue()))
                    {
                        Terrain->SpawnFragments(Edit.GetValue());
                        if (Terrain->Layout == EVoxelLayout::Expedition)
                            AExpeditionImpactCamera::Play(*Terrain, Edit.GetValue());
                    }
#if !UE_BUILD_SHIPPING
                    static const bool bProfile = FParse::Param(FCommandLine::Get(), TEXT("VoxelProfile"));
                    if (bProfile) UE_LOG(LogTemp, Display, TEXT("VOXEL_BUILD seq=%u chunks=%d tiles=%d worker_wait=%.2f mesh=%.2f game_wait=%.2f commit=%.2f"),
                        Edit->Sequence, Build.Chunks.Num(), Build.Tiles.Num(), (WorkerAt-SubmittedAt)*1000., (BuiltAt-WorkerAt)*1000.,
                        (CommitAt-BuiltAt)*1000., (FPlatformTime::Seconds()-CommitAt)*1000.);
#endif
                }
                else
                {
                    Terrain->bReady = true;
                    UE_LOG(LogTemp, Display, TEXT("VOXEL_INITIAL crc=%08x map=%08x"), Terrain->GetChecksum(), Terrain->GetInitialChecksum());
                }
            }
            Terrain->bBuilding = false;
            if (bImmediate) Terrain->ImmediateMs += (FPlatformTime::Seconds() - SubmittedAt) * 1000.;
            if (Completion)
            {
                Completion(bCommit);
            }
        };
        if (bImmediate) Finish();
        else AsyncTask(ENamedThreads::GameThread, MoveTemp(Finish));
    };
    if (bImmediate) BuildMesh();
    else if (Edit.IsSet()) AsyncTask(ENamedThreads::AnyHiPriThreadNormalTask, MoveTemp(BuildMesh));
    else Async(EAsyncExecution::ThreadPool, MoveTemp(BuildMesh));
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
    for (const auto& Tile : Build.Tiles)
    {
        if (BakedTiles.IsValidIndex(Tile.Key) && BakedTiles[Tile.Key])
        {
            if (Tile.Value.bEnableCollision) BakedTiles[Tile.Key]->SetProcMeshSection(0,Tile.Value);
            else BakedTiles[Tile.Key]->ClearAllMeshSections();
        }
    }
    Density = MoveTemp(Build.Density);
    Density->Apply(Build.Patch);
}

bool AVoxelTerrain::CanSeeExcavation(const FVoxelEdit& Edit) const
{
    if (Edit.BrushId != VoxelPlayground::ShallowCut) return true;
    const auto* Player = GetWorld()->GetFirstPlayerController();
    if (!Player || !Player->IsLocalController() || !Player->PlayerCameraManager) return false;
    const FVector Start = Player->PlayerCameraManager->GetCameraLocation();
    const FVector End(Edit.Center);
    FCollisionQueryParams Query(SCENE_QUERY_STAT(MudImpactSight), false, Player->GetPawn());
    FHitResult Hit;
    return !GetWorld()->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility, Query)
        || FVector::DistSquared(Hit.ImpactPoint, End) <= 9.;
}

void AVoxelTerrain::SpawnFragments(const FVoxelEdit& Edit)
{
    if (!FragmentMesh || Edit.Action == EVoxelAction::Fill || !FApp::CanEverRender()) return;
    const APlayerController* Local = GetWorld()->GetFirstPlayerController();
    if (!Local || !Local->IsLocalController() || !Local->PlayerCameraManager) return;
    // The fist-sized stones are local feedback around a strike.
    if (FVector::DistSquared(Local->PlayerCameraManager->GetCameraLocation(), FVector(Edit.Center)) > FMath::Square(2600.)) return;
    if (!Debris)
    {
        Debris = NewObject<UInstancedStaticMeshComponent>(this);
        Debris->SetupAttachment(RootComponent);
        Debris->SetStaticMesh(FragmentMesh);
        Debris->SetMobility(EComponentMobility::Movable);
        Debris->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Debris->SetCanEverAffectNavigation(false);
        Debris->SetCastShadow(false);
        Debris->RegisterComponent();
    }
    for (int32 Index = 0; Index < 7; ++Index)
    {
        FTransform Transform(FMath::VRand().Rotation(), FVector(Edit.Center) + FMath::VRand() * 40., FVector(FMath::FRandRange(.055f, .13f)));
        Fragments.Add({Transform, FMath::VRand() * 210. + FVector(0, 0, 180), 0.f});
        if (Fragments.Num() > Debris->GetInstanceCount()) Debris->AddInstance(Transform, true);
    }
    SetActorTickEnabled(true);
}

void AVoxelTerrain::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    const FQuat Spin(FRotator(200.f * DeltaSeconds, 110.f * DeltaSeconds, 0));
    for (int32 Index = Fragments.Num() - 1; Index >= 0; --Index)
    {
        FFragment& Fragment = Fragments[Index];
        Fragment.Age += DeltaSeconds;
        if (Fragment.Age >= .45f)
        {
            Fragments.RemoveAtSwap(Index, 1, EAllowShrinking::No);
            continue;
        }
        Fragment.Velocity.Z -= 630.f * DeltaSeconds;
        Fragment.Transform.AddToTranslation(Fragment.Velocity * DeltaSeconds);
        Fragment.Transform.ConcatenateRotation(Spin);
    }
    DebrisTransforms.Reset(Debris->GetInstanceCount());
    const FTransform Hidden(FQuat::Identity, FVector::ZeroVector, FVector::ZeroVector);
    for (const FFragment& Fragment : Fragments) DebrisTransforms.Add(Fragment.Transform);
    while (DebrisTransforms.Num() < Debris->GetInstanceCount()) DebrisTransforms.Add(Hidden);
    Debris->BatchUpdateInstancesTransforms(0, DebrisTransforms, true, true, true);
    SetActorTickEnabled(!Fragments.IsEmpty());
}
