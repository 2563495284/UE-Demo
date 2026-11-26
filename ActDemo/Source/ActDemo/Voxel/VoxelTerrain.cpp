#include "Voxel/VoxelTerrain.h"
#if !UE_BUILD_SHIPPING
#include "Tests/ExpeditionDecoyToolProof.h"
#endif
#include "Voxel/ExpeditionSurface.h"
#include "Voxel/IslandRouteVoxel.h"
#include "Island/IslandSceneryVoxels.h"
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
#include "VoxelSurfaceKernel.h"
#include "EngineUtils.h"
#include "Engine/StaticMesh.h"
#include "Misc/App.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "ProceduralMeshComponent.h"
#include "Serialization/CustomVersion.h"
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
    const FGuid SearchCircleVersion(0x606858E0, 0x8F94453B, 0xA477256A, 0xBDF22F55);
    constexpr int32 WideExpeditionCircles = 1;
    const FCustomVersionRegistration RegisterSearchCircleVersion(SearchCircleVersion, WideExpeditionCircles, TEXT("ExpeditionSearchCircles"));

    TSharedPtr<FVoxelDensity, ESPMode::ThreadSafe> BuildDensity(EVoxelLayout Layout, TConstArrayView<FVoxelBrushStamp> Stamps)
    {
        auto Result = MakeShared<FVoxelDensity, ESPMode::ThreadSafe>(Layout);
        for (const FVoxelBrushStamp& Stamp : Stamps)
        {
            Result->Apply(Result->PrepareBrush(Stamp));
        }
        return Result;
    }

    FProcMeshSection MeshChunk(const FVoxelDensity& Density, int32 Index, const FVoxelDensityPatch* Patch)
    {
        using namespace VoxelPlayground;
        constexpr int32 Side = CellsPerChunk;
        constexpr int32 Width = VoxelSurfaceKernel::FChunkSamples::Width;
        static_assert(Side == VoxelSurfaceKernel::CellsPerChunk && Spacing == VoxelSurfaceKernel::Spacing);
        const FIntVector First = Density.Grid.ChunkCoordinates(Index) * Side;
        const FVector Start(Density.Grid.Origin + First * Spacing);
        VoxelSurfaceKernel::FChunkSamples Samples;
        for (int32 Z = 0; Z < Width; ++Z)
        {
            for (int32 Y = 0; Y < Width; ++Y)
            {
                Density.CopySampleRow(First + FIntVector(-2, Y - 2, Z - 2), Width, &Samples.Values[Width * (Y + Width * Z)]);
            }
        }
        // A detached structure can span many chunks. Limit each chunk's overlay
        // pass to the smaller of its sample window and the sparse edit.
        if (Patch && Patch->Samples.Num() > Samples.Values.Num())
        {
            const FIntVector StartPoint = First - FIntVector(2);
            const FIntVector Low = StartPoint.ComponentMax(FIntVector::ZeroValue);
            const FIntVector High = (StartPoint + FIntVector(Width - 1)).ComponentMin(Density.Grid.Cells);
            for (int32 Z = Low.Z; Z <= High.Z; ++Z)
                for (int32 Y = Low.Y; Y <= High.Y; ++Y)
                {
                    const int32 RowStart = Density.Grid.SampleIndex({Low.X, Y, Z});
                    int16* Dest = &Samples.Values[Low.X - StartPoint.X + Width * (Y - StartPoint.Y + Width * (Z - StartPoint.Z))];
                    for (int32 X = 0; X <= High.X - Low.X; ++X)
                        if (const int16* Value = Patch->Samples.Find(RowStart + X)) Dest[X] = *Value;
                }
        }
        else if (Patch)
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

        FProcMeshSection Section = VoxelSurfaceKernel::BuildChunk(Samples, Start);
        if (Density.Layout == EVoxelLayout::Expedition)
        {
            for (FProcMeshVertex& Vertex : Section.ProcVertexBuffer)
            {
                const FVector& P = Vertex.Position;
                const int32 Surface = BaseHeight(Density.Layout, FMath::RoundToInt(P.X), FMath::RoundToInt(P.Y));
                // Zero on original ground, positive into a cut, negative on a newly built mound.
                Vertex.UV1.X = Surface - P.Z;
            }
        }
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
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> Ground(TEXT("/Game/TreasureIsland/FX/M_IslandGround"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Clod(TEXT("/Game/TreasureIsland/Art/Neutral/SM_ReefStone"));
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> ClodPigment(TEXT("/Game/TreasureIsland/Art/Neutral/M_ClodPigment"));
    IslandGround = Ground.Object;
    ExpeditionClodMesh = Clod.Object;
    ExpeditionClodPigment = ClodPigment.Object;

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
    if (Layout == EVoxelLayout::Expedition) Chunk.SetMaterial(0,IslandGround);
    Chunk.SetGenerateOverlapEvents(false);
    Chunk.SetCanEverAffectNavigation(false);
    Chunk.bUseComplexAsSimpleCollision = true;
    // Keep the visible surface and walking collision on the same committed version.
    Chunk.bUseAsyncCooking = false;
}

void AVoxelTerrain::ApplyBakedGroundMaterial(UProceduralMeshComponent& Tile) const
{
    // Old maps saved the whitebox grid as a per-component override. Keep other authored surfaces.
    const UMaterialInterface* Current = Tile.GetMaterial(0);
    if (!Tile.GetNumOverrideMaterials() || !Current || Current->GetPathName() ==
        TEXT("/Game/LevelPrototyping/Materials/MI_PrototypeGrid_Gray.MI_PrototypeGrid_Gray"))
        Tile.SetMaterial(0,IslandGround);
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
    // All layouts replay the same grounded edit protocol on every peer.
    constexpr uint32 TerrainEditProtocol = 6;
    uint32 Hash = HashCombineFast(GetChecksum(), TerrainEditProtocol);
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
        ApplyBakedGroundMaterial(*BakedTiles[Index]);
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

void AVoxelTerrain::Serialize(FArchive& Ar)
{
    Super::Serialize(Ar);
    Ar.UsingCustomVersion(SearchCircleVersion);
}

void AVoxelTerrain::PostLoad()
{
    Super::PostLoad();
    if (Layout == EVoxelLayout::Expedition && GetLinkerCustomVersion(SearchCircleVersion) < WideExpeditionCircles)
    {
        for (FIslandSearchCircle& Circle : SearchCircles)
            if (Circle.Radius == 410) Circle.Radius = IslandRules::ExpeditionSearchRadius;
    }
    // The editor temporarily hides coarse tiles underneath its transient sculpt preview.
    // A level saved during a stroke must reload its original baked collision and visibility.
    for (UProceduralMeshComponent* Tile : BakedTiles)
    {
        if (!Tile) continue;
        ApplyBakedGroundMaterial(*Tile);
        Tile->SetVisibility(true);
        Tile->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
    }
}

void AVoxelTerrain::BeginPlay()
{
    Super::BeginPlay();
#if !UE_BUILD_SHIPPING
    if (Layout == EVoxelLayout::Expedition && HasAuthority() && (FParse::Param(FCommandLine::Get(),TEXT("ExpeditionWalkQA"))
        || (GetNetMode() == NM_Standalone && FParse::Param(FCommandLine::Get(),TEXT("WreckPreview")))))
        GetWorld()->SpawnActor<AExpeditionWalk>();
#endif
    if (Layout == EVoxelLayout::Expedition)
    {
        for (UProceduralMeshComponent* Chunk : MeshChunks) if (Chunk) Chunk->SetMaterial(0,IslandGround);
        for (UProceduralMeshComponent* Tile : BakedTiles) if (Tile) ApplyBakedGroundMaterial(*Tile);
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
#if !UE_BUILD_SHIPPING
                    static const bool bCheckDecoyTools = FParse::Param(FCommandLine::Get(), TEXT("ExpeditionDecoyQA"));
                    if (bCheckDecoyTools) FExpeditionDecoyToolProof::ObserveCommitted(*Terrain, *Edit);
#endif
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
    FBox LostSupport(ForceInit);
    if (Layout == EVoxelLayout::Expedition && HasAuthority())
    {
        // Include remote material removed by grounding as well as the brush itself.
        // A fill that only adds material cannot sever any existing attachment.
        for (const auto& Change : Build.Patch.Samples)
        {
            const FIntVector Cell = Density->Grid.SampleCoordinates(Change.Key);
            if (Change.Value > Density->Sample(Cell))
                LostSupport += FVector(Density->Grid.Origin + Cell * VoxelPlayground::Spacing);
        }
    }
    Density->Apply(Build.Patch);
    if (LostSupport.IsValid)
    {
        const FBox Changed = LostSupport.ExpandBy(VoxelPlayground::Spacing);
        for (TActorIterator<AIslandRouteVoxel> It(GetWorld()); It; ++It)
            if (It->Field()->Bounds.GetBox().ExpandBy(VoxelPlayground::Spacing).Intersect(Changed))
                It->RefreshGrounding(*this);
        IslandSceneryVoxels::CheckFoundations(*GetWorld(), Changed);
    }
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
    const bool bIsland = Layout == EVoxelLayout::Expedition && ExpeditionClodMesh && ExpeditionClodPigment;
    if (!Debris)
    {
        Debris = NewObject<UInstancedStaticMeshComponent>(this);
        Debris->SetupAttachment(RootComponent);
        Debris->SetStaticMesh(bIsland ? ExpeditionClodMesh.Get() : FragmentMesh.Get());
        if (bIsland)
        {
            Debris->SetNumCustomDataFloats(5);
            Debris->SetMaterial(0, ExpeditionClodPigment);
        }
        Debris->SetMobility(EComponentMobility::Movable);
        Debris->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Debris->SetCanEverAffectNavigation(false);
        Debris->SetCastShadow(false);
        Debris->RegisterComponent();
    }
    const float Depth = bIsland ? VoxelPlayground::BaseHeight(Layout, Edit.Center.X, Edit.Center.Y) - Edit.Center.Z : 0.f;
    for (int32 Index = 0; Index < 7; ++Index)
    {
        FTransform Transform(FMath::VRand().Rotation(), FVector(Edit.Center) + FMath::VRand() * 40., FVector(FMath::FRandRange(.055f, .13f)));
        // The original strike supplies the material sample while the clod tumbles through the air.
        const int32 Slot = Fragments.Add({Transform, FMath::VRand() * 210. + FVector(0, 0, 180), 0.f,
            {float(Edit.Center.X), float(Edit.Center.Y), float(Edit.Center.Z), Depth, Edit.SurfaceNormal.Z / 1000.f}});
        if (Fragments.Num() > Debris->GetInstanceCount()) Debris->AddInstance(Transform, true);
        if (bIsland) Debris->SetCustomData(Slot, MakeArrayView(Fragments[Slot].SoilSample));
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
            if (Debris->NumCustomDataFloats && Fragments.IsValidIndex(Index))
                Debris->SetCustomData(Index, MakeArrayView(Fragments[Index].SoilSample));
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
