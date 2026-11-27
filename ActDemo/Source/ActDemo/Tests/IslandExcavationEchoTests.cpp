#if WITH_DEV_AUTOMATION_TESTS
#include "Island/IslandExcavationEcho.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FIslandExcavationEchoTest, "ActDemo.Island.ExcavationEcho.NearestAndDistance",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FIslandExcavationEchoTest::RunTest(const FString&)
{
    const FVector Dig(1200., -600., 180.);
    const FVector TrueBox = Dig + FVector(300., 0., -25.);
    const FVector Decoy = Dig + FVector(0., -120., -185.);
    const FVector TowardsDecoy(0., -.6, -.8);
    FIslandExcavationEcho FromDig(Dig);
    TestFalse(TEXT("An empty search stays silent"), FromDig.IsAudible());
    FromDig.Consider(TrueBox);
    FromDig.Consider(Decoy);
    TestEqual(TEXT("The nearer decoy is 200 cm from impact in three dimensions"), FromDig.GetDistanceCm(), 200.f);
    TestTrue(TEXT("The echo leads left and down to that decoy"), FromDig.GetDirection().Equals(TowardsDecoy, .0001));
    FIslandExcavationEcho Reversed(Dig);
    Reversed.Consider(Decoy);
    Reversed.Consider(TrueBox);
    TestTrue(TEXT("A farther true box cannot overwrite the nearer decoy"),
        Reversed.GetDirection().Equals(FromDig.GetDirection(), .0001));
    FIslandExcavationEcho Moved(Dig + FVector(250., 0., 0.));
    Moved.Consider(Decoy);
    Moved.Consider(TrueBox);
    TestEqual(TEXT("The nearer target switches to the true lid when the dig moves"), Moved.GetDistanceCm(), 50.f);
    TestTrue(TEXT("Direction switches with the selected distance"), Moved.GetDirection().Equals(FVector::ForwardVector));

    const float Range = FIslandExcavationEcho::HearingRangeCm;
    FIslandExcavationEcho Edge(Dig);
    Edge.Consider(Dig + FVector(Range, 0., -25.));
    TestFalse(TEXT("The hearing boundary is silent"), Edge.IsAudible());
    Edge.Consider(Dig + FVector(Range - 1., 0., -25.));
    TestTrue(TEXT("A point one centimetre inside the range is audible"), Edge.IsAudible());
    TestEqual(TEXT("Contact has maximum proximity"), FIslandExcavationEcho::Proximity(0.f), 1.f);
    TestEqual(TEXT("The hearing boundary has zero proximity"), FIslandExcavationEcho::Proximity(Range), 0.f);
    float Previous = 0.f;
    for (int32 Step = 1; Step <= 20; ++Step)
    {
        const float Current = FIslandExcavationEcho::Proximity(Range * (20 - Step) / 20.f);
        TestTrue(TEXT("Proximity grows with every step towards the lid"), Current > Previous && Current <= 1.f);
        Previous = Current;
    }
    return !HasAnyErrors();
}
#endif
