#if WITH_DEV_AUTOMATION_TESTS

#include "PartyCrewMotion.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPartyCrewHandActionsTest, "ActDemo.Crew.Motion.HandActions",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPartyCrewHandActionsTest::RunTest(const FString&)
{
    FPartyCrewMotion Motion;
    auto PoseAt = [&](double Now) { return Motion.Sample(Now, 1.f/60.f, 0.f, 0.f, false); };

    Motion.Charge(1., true);
    TestTrue(TEXT("Half charge has half the wind-up"), FMath::IsNearlyEqual(PoseAt(1.6).Load, .5f, UE_KINDA_SMALL_NUMBER));
    const FPartyCrewPose Held = PoseAt(3.);
    TestEqual(TEXT("Holding past the cap keeps a steady fist"), Held.Load, 1.f);
    Motion.Throw(3.);
    const FPartyCrewPose Throw = PoseAt(3.07);
    TestEqual(TEXT("Throwing preserves the lead fist"), Throw.Hands[1], Held.Hands[1]);
    TestTrue(TEXT("The off hand sends soil forward in both cameras"),
        Throw.Hands[0].X > Held.Hands[0].X && Throw.EyeHands[0].X > Held.EyeHands[0].X);
    Motion.Charge(3.08, false);
    Motion.Charge(3.09, true);
    TestTrue(TEXT("Starting a charge preserves toss follow-through"), PoseAt(3.15).TossFollow > .95f);

    Motion.Strike(4., 1.f);
    TestTrue(TEXT("A heavy punch reaches extension"), PoseAt(4.12).Extension > .99f);
    const FPartyCrewPose Recoil = PoseAt(4.34);
    TestTrue(TEXT("The lead arm recoils after contact"), Recoil.Recoil > .95f && Recoil.Extension < .02f);
    TestEqual(TEXT("The arm has settled"), PoseAt(4.52).Recoil, 0.f);

    Motion.Throw(5.);
    Motion.Charge(5., true, 1.f);
    const FPartyCrewPose Pry = Motion.Sample(5.07, .016f, 0.f, 0.f, false, false, false, true);
    TestTrue(TEXT("Both hands work the lid"), Pry.Pry > .5f && Pry.Load == 0.f && Pry.Toss == 0.f);
    Motion.Throw(6.);
    const FPartyCrewPose Stunned = Motion.Sample(6.07, .016f, 0.f, 0.f, false, true, false, true);
    TestTrue(TEXT("Stun interrupts both hands"), Stunned.Pry == 0.f && Stunned.Toss == 0.f);
    TestEqual(TEXT("The interrupted throw cannot resume"), PoseAt(6.16).TossFollow, 0.f);
    return !HasAnyErrors();
}

#endif
