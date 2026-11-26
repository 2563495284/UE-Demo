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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FExpeditionFistRadiusTest, "ActDemo.Voxel.ChargedPunch.RadiusAndDepth",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FExpeditionFistRadiusTest::RunTest(const FString&)
{
    using namespace VoxelPlayground;
    FVoxelEdit Punch;
    Punch.BrushId=ShallowCut;
    const uint8 Half=PartyPunch::Quantize(.6f);
    TestEqual(TEXT("Overcharging clamps on the server"), PartyPunch::Quantize(4.f), PartyPunch::Quantize(1.2f));
    TestEqual(TEXT("Reliable delivery jitter can restore a real half charge"),PartyPunch::ResolvePower(.49,.6f,80.f),Half);
    TestEqual(TEXT("A local host uses its own elapsed clock"),PartyPunch::ResolvePower(.6,12.f,-1.f),Half);
    TestEqual(TEXT("Even a very slow connection cannot turn a tap into a full charge"),
        PartyPunch::ResolvePower(.02,50.f,900.f),PartyPunch::Quantize(.22f));
    TestEqual(TEXT("The full charge ceiling also applies after compensation"),
        PartyPunch::ResolvePower(1.12,2.1f,80.f),uint8(255));
    for (bool bBoost : {false,true})
    {
        Punch.bWideFootprint=bBoost;
        for (uint8 Power : {uint8(0),Half,uint8(255)})
        {
            Punch.PunchPower=Power;
            const int32 Radius=(bBoost ? 124 : 62) + (Power==0 ? 0 : Power==Half ? 31 : 62);
            TestEqual(TEXT("Light / half / full use linear radii"),Punch.PunchRadius(),Radius);
            Punch.SurfaceNormal={0,0,1000};
            TestEqual(TEXT("Floor edge matches the recorded radius"),PartyBrushDistance(Punch,{0,Radius,0}),0);
            TestTrue(TEXT("A full footprint never cuts deeper than a light fist"),PartyBrushDistance(Punch,{0,0,-41})>0);
            Punch.SurfaceNormal={1000,0,0};
            TestTrue(TEXT("Sideways punch carves same footprint on wall"),PartyBrushDistance(Punch,{-15,0,Radius-3})<0);
            TestTrue(TEXT("Material just outside wall footprint remains"),PartyBrushDistance(Punch,{-15,0,Radius+3})>0);
        }
    }
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FExpeditionDecoyToolFootprintTest, "ActDemo.Voxel.DecoyToolFootprint",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FExpeditionDecoyToolFootprintTest::RunTest(const FString& Parameters)
{
    using namespace VoxelPlayground;
    FVoxelDensity Field(EVoxelLayout::Expedition);
    FVoxelEdit Edit;
    // This shore shelf is constant along Y. Its 338 cm surface cuts between grid samples.
    Edit.Center = {6600, 0, 340};
    const int32 Lateral = 100;
    const FIntVector SideBelow(6600, Lateral, 325), SideAbove(6600, Lateral, 350);
    Edit.BrushId = ShallowCut;
    TestTrue(TEXT("The edge sample starts underground"), Solid(Field, SideBelow));
    auto Patch = Field.PrepareEdit(Edit);
    TestTrue(TEXT("A normal cut leaves the edge alone"), Solid(Field, SideBelow, &Patch));
    Edit.bWideFootprint = true;
    Patch = Field.PrepareEdit(Edit);
    TestFalse(TEXT("The winning crew cuts through the same edge"), Solid(Field, SideBelow, &Patch));
    TestTrue(TEXT("Wider excavation preserves material below its normal depth"), Solid(Field, {6600, 0, 275}, &Patch));
    for (const auto& Normal : {FIntVector(0,0,1000), FIntVector(1000,0,0)})
    {
        Edit.SurfaceNormal = Normal;
        for (bool bWide : {false,true})
        {
            Edit.bWideFootprint = bWide;
            const FIntVector Rim = Edit.Center + FIntVector(0,62 * Edit.FootprintScale(),0);
            TestEqual(TEXT("Cut radius is exact on the floor and the wall"), PartyBrushDistance(Edit,Rim), 0);
            TestEqual(TEXT("Cut depth stays 40 cm along the surface normal"), PartyBrushDistance(Edit,Edit.Center - Normal*40/1000), 0);
        }
    }

    Edit.Action = EVoxelAction::Fill;
    Edit.BrushId = MudPad;
    Edit.SurfaceNormal = {0,0,1000};
    Edit.bWideFootprint = false;
    Patch = Field.PrepareEdit(Edit);
    TestFalse(TEXT("The ordinary mud pad leaves the side open"), Solid(Field, SideAbove, &Patch));
    Edit.bWideFootprint = true;
    TestTrue(TEXT("A wide mud pad can travel through the normal edit stream"), Field.Grid.IsValidEdit(Edit));
    Patch = Field.PrepareEdit(Edit);
    TestTrue(TEXT("The wide pad adds solid ground at the side"), Solid(Field, SideAbove, &Patch));
    TestFalse(TEXT("The wider pad stays one layer high"), Solid(Field, {6600,0,400}, &Patch));
    TestEqual(TEXT("The sloping pad at half height has double the original radius"),
        PartyBrushDistance(Edit,Edit.Center + FIntVector(0,140,20)), 0);
    FVoxelProtectedBox& Lid = Edit.ProtectedBoxes.AddDefaulted_GetRef();
    Lid.Low = SideAbove - FIntVector(10);
    Lid.High = SideAbove + FIntVector(10);
    Patch = Field.PrepareEdit(Edit);
    TestFalse(TEXT("The wide rim still respects an exposed chest"), Solid(Field, SideAbove, &Patch));
    TestTrue(TEXT("The other side of the same wide pad remains solid"), Solid(Field, {6600,-Lateral,350}, &Patch));
    FVoxelDensity Replay(EVoxelLayout::Expedition);
    Field.Apply(Patch);
    Replay.Apply(Replay.PrepareEdit(Edit));
    TestEqual(TEXT("Replaying the authoritative wide edit reproduces the protected terrain"), Replay.Checksum(), Field.Checksum());
    Edit.BrushId = FirstBurialLayer;
    TestFalse(TEXT("A mud burial can never use the tool reward"), Field.Grid.IsValidEdit(Edit));
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
