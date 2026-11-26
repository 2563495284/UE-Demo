#if WITH_DEV_AUTOMATION_TESTS

#include "Island/IslandRules.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FIslandDecoyDrawTest, "ActDemo.Island.DecoyEventDraw",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FIslandDecoyDrawTest::RunTest(const FString& Parameters)
{
    using Event = EIslandDecoyEvent;
    const auto VerifyPool = [this](int32 Players, int32 Teams, int32 Winners, TArray<Event> Expected, int32 WorldMask = 0)
    {
        TMap<Event, int32> Counts;
        bool bEqualNeighbors = false, bDifferentNeighbors = false;
        for (int32 Seed = 1; Seed <= 1024; ++Seed)
        {
            const Event Draw = IslandRules::DrawDecoyEvent(Seed, 5, Players, Teams, Winners, WorldMask);
            const Event Retry = IslandRules::DrawDecoyEvent(Seed, 5, Players, Teams, Winners, WorldMask);
            const Event Next = IslandRules::DrawDecoyEvent(Seed, 6, Players, Teams, Winners, WorldMask);
            if (!Expected.Contains(Draw) || !Expected.Contains(Next) || Draw != Retry)
            {
                AddError(FString::Printf(TEXT("Invalid or unstable event at seed %d, %d players, teams %d, winners %d"), Seed, Players, Teams, Winners));
                return;
            }
            int32& Count = Counts.FindOrAdd(Draw);
            if (!Count && Players == 3 && Teams == 3 && Winners == 1 && WorldMask == (64 | 128 | 256))
                UE_LOG(LogTemp, Display, TEXT("DECOY_DRAW_SAMPLE first_site=5 seed=%d event=%d"), Seed, int32(Draw));
            ++Count;
            bEqualNeighbors |= Draw == Next;
            bDifferentNeighbors |= Draw != Next;
        }
        TestEqual(TEXT("Every eligible event can be drawn"), Counts.Num(), Expected.Num());
        TestTrue(TEXT("Separate chests can repeat an event or draw different events"), bEqualNeighbors && bDifferentNeighbors);
        for (const auto& Entry : Counts)
        {
            const double ExpectedCount = 1024. / Expected.Num();
            TestTrue(TEXT("Eligible event frequencies stay near an equal draw across seeds"),
                FMath::Abs(Entry.Value - ExpectedCount) < ExpectedCount * .30);
        }
    };
    VerifyPool(3, 3, 1, {Event::WideTools, Event::Scatter, Event::Disguise, Event::SpringTide, Event::Ceasefire});
    VerifyPool(1, 1, 1, {Event::WideTools, Event::SpringTide});
    VerifyPool(1, 1, 0, {Event::Scatter, Event::SpringTide});
    VerifyPool(2, 1, 1, {Event::WideTools, Event::Disguise, Event::SpringTide});
    VerifyPool(3, 3, 0, {Event::Scatter, Event::Disguise, Event::SpringTide, Event::Ceasefire});
    VerifyPool(6, 7, 6, {Event::WideTools, Event::Scatter, Event::Disguise, Event::SpringTide, Event::Ceasefire});
    const int32 Rally = 1 << int32(Event::CrewRally);
    const int32 Ramp = 1 << int32(Event::SandRamp);
    const int32 Sail = 1 << int32(Event::SailGust);
    VerifyPool(3, 3, 1, {Event::WideTools, Event::Scatter, Event::Disguise, Event::SpringTide, Event::Ceasefire,
        Event::CrewRally, Event::SandRamp, Event::SailGust}, Rally | Ramp | Sail);
    VerifyPool(2, 1, 1, {Event::WideTools, Event::Disguise, Event::SpringTide, Event::CrewRally}, Rally);
    VerifyPool(1, 1, 0, {Event::Scatter, Event::SpringTide, Event::SandRamp}, Ramp);
    VerifyPool(2, 3, 1, {Event::WideTools, Event::Scatter, Event::Disguise, Event::SpringTide, Event::Ceasefire,
        Event::SailGust}, Rally | Sail);
    TestTrue(TEXT("Empty rooms never draw, including when a terrain or sail target exists"),
        IslandRules::DrawDecoyEvent(1, 5, 0, 0, 0, Ramp | Sail) == Event::None);
    return !HasAnyErrors();
}

#endif
