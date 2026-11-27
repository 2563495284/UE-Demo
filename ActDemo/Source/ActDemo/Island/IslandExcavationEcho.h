#pragma once

#include "Island/IslandRules.h"

// One reading per committed dig. Ties keep the first site in the seeded layout.
struct FIslandExcavationEcho
{
    static constexpr float HearingRangeCm = 1000.f;

    explicit FIslandExcavationEcho(FVector InDigPoint) : DigPoint(InDigPoint) {}

    void Consider(FVector ChestCenter)
    {
        const FVector Offset = ChestCenter + IslandRules::ClosedLidCenter - DigPoint;
        const double DistanceSq = Offset.SizeSquared();
        if (DistanceSq < ClosestSq)
        {
            ClosestSq = DistanceSq;
            ClosestOffset = Offset;
        }
    }

    bool IsAudible() const { return ClosestSq < FMath::Square(HearingRangeCm); }
    float GetDistanceCm() const { return IsAudible() ? float(FMath::Sqrt(ClosestSq)) : -1.f; }
    FVector GetDirection() const { return ClosestOffset.GetSafeNormal(); }
    static float Proximity(float DistanceCm)
    {
        return DistanceCm < 0.f ? 0.f : FMath::Clamp(1.f - DistanceCm / HearingRangeCm, 0.f, 1.f);
    }

private:
    FVector DigPoint;
    double ClosestSq = FMath::Square(HearingRangeCm);
    FVector ClosestOffset = FVector::ZeroVector;
};
