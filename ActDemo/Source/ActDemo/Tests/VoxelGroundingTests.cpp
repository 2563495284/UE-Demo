#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Voxel/VoxelDensity.h"

namespace
{
    FIntVector GridPoint(const FVoxelDensity& Field, FIntVector World)
    {
        return (World - Field.Grid.Origin) / VoxelPlayground::Spacing;
    }

    bool Solid(const FVoxelDensity& Field, FIntVector World, const FVoxelDensityPatch* Patch = nullptr)
    {
        return Field.Sample(GridPoint(Field, World), Patch) < 0;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FExpeditionBridgeSupportTest, "ActDemo.Voxel.GroundedTerrain.Bridge",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FExpeditionBridgeSupportTest::RunTest(const FString& Parameters)
{
    FVoxelDensity Server(EVoxelLayout::Expedition);
    FVoxelDensity Replay(EVoxelLayout::Expedition);

    const FIntVector Deck(3300, 0, 750);
    const FIntVector Loose(3700, 300, 925);
    // The edit arrives as one sand spill. Its early lobes meet the island through later lobes.
    FVoxelEdit Bridge;
    Bridge.Sequence = 1;
    Bridge.Action = EVoxelAction::Fill;
    Bridge.BrushId = VoxelPlayground::BrushFor(Bridge.Action);
    Bridge.Center = Deck;
    for (int32 X = 3400; X <= 4300; X += 100)
        Bridge.ExtraFillCenters.Add({X, 0, 750});
    for (int32 Z = 675; Z >= 375; Z -= 75)
    {
        Bridge.ExtraFillCenters.Add({3300, 0, Z});
        Bridge.ExtraFillCenters.Add({4300, 0, Z});
    }
    Bridge.ExtraFillCenters.Add(Loose);
    TestTrue(TEXT("Bridge edit fits normal network input"), Server.Grid.IsValidEdit(Bridge));
    Server.Apply(Server.PrepareEdit(Bridge));
    TestTrue(TEXT("Connected spill keeps the entire bridge deck"), Solid(Server, Deck) && Solid(Server, {3800, 0, 750}));
    TestFalse(TEXT("Disconnected lobe of the same spill creates no floating material"), Solid(Server, Loose));

    FVoxelEdit LeftCut;
    LeftCut.Sequence = 2;
    LeftCut.Center = {3300, 0, 525};
    Server.Apply(Server.PrepareEdit(LeftCut));
    TestFalse(TEXT("The left pillar is severed"), Solid(Server, LeftCut.Center));
    TestTrue(TEXT("The far pillar still supports the overhang"), Solid(Server, Deck));

    FVoxelEdit RightCut = LeftCut;
    RightCut.Sequence = 3;
    RightCut.Center.X = 4300;
    const FVoxelDensityPatch Cut = Server.PrepareEdit(RightCut);
    TestTrue(TEXT("Preparing a cut leaves the committed bridge intact"), Solid(Server, Deck));
    TestFalse(TEXT("Cutting the last support clears the distant end in the pending patch"), Solid(Server, Deck, &Cut));
    const FIntVector Chunk = GridPoint(Server, Deck) / VoxelPlayground::CellsPerChunk;
    const FIntVector Counts = Server.Grid.Chunks();
    const int32 DistantChunk = Chunk.X + Counts.X * (Chunk.Y + Counts.Y * Chunk.Z);
    TestTrue(TEXT("The distant chunk is scheduled to rebuild its mesh and collision"), Cut.Chunks.Contains(DistantChunk));
    Server.Apply(Cut);
    TestFalse(TEXT("The central deck is gone"), Solid(Server, {3800, 0, 750}));
    TestTrue(TEXT("Ground below the severed support remains solid"), Solid(Server, {4300, 0, 325}));

    Replay.Apply(Replay.PrepareEdit(Bridge));
    Replay.Apply(Replay.PrepareEdit(LeftCut));
    const FVoxelDensityPatch ReplayCut = Replay.PrepareEdit(RightCut);
    TArray<int32> ExpectedChunks = Cut.Chunks;
    TArray<int32> ActualChunks = ReplayCut.Chunks;
    ExpectedChunks.Sort();
    ActualChunks.Sort();
    TestTrue(TEXT("Replayed cuts dirty the same chunks"), ActualChunks == ExpectedChunks);
    Replay.Apply(ReplayCut);
    TestEqual(TEXT("Replaying the brush journal produces the same density"), Replay.Checksum(), Server.Checksum());
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelAuthoredSupportTest, "ActDemo.Voxel.GroundedTerrain.Authored",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVoxelAuthoredSupportTest::RunTest(const FString& Parameters)
{
    FVoxelDensity Field(EVoxelLayout::Playground);
    const FIntVector Air(650, 300, 300);
    const FVoxelBrushStamp FloatingStamp{Air, 50, true};
    const FVoxelDensityPatch Floating = Field.PrepareBrush(FloatingStamp);
    TestTrue(TEXT("A floating add stamp changes no density samples"), Floating.Samples.IsEmpty());
    TestTrue(TEXT("A floating add stamp dirties no chunk"), Floating.Chunks.IsEmpty());
    Field.Apply(Floating);
    TestFalse(TEXT("An isolated authored add stamp leaves the sky clear"), Solid(Field, Air));

    const FIntVector Cap(1025, 175, 275);
    const FIntVector OtherTower(1025, 500, 275);
    TestTrue(TEXT("Both original rock towers start solid"), Solid(Field, Cap) && Solid(Field, OtherTower));
    // This old-format editor stamp spans the built-in tower at its waist, clear of its top.
    Field.Apply(Field.PrepareBrush({{1025, 175, 100}, 160, false}));
    TestFalse(TEXT("Undercutting original terrain removes its detached cap"), Solid(Field, Cap));
    TestTrue(TEXT("The neighbouring grounded tower is preserved"), Solid(Field, OtherTower));
    return !HasAnyErrors();
}

#endif
