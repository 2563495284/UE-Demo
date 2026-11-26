#pragma once

#include "CoreMinimal.h"

// Local-space field for the expedition's diggable roadside plants and stone.
// XY and Z are centimetres; the root lies at the original ground contact.
namespace IslandRouteFlora
{
    enum class EShape : uint8 { FanPalm, Saltbush, SplitReef, CoralSpur };

    struct FSample
    {
        float Distance;   // Negative in occupied material.
        uint8 Pigment;
    };

    constexpr double SampleSpacing(EShape Shape) { return Shape == EShape::SplitReef ? 9. : 8.; }

    ACTDEMO_API FSample Sample(EShape Shape, const FVector& Local, int32 Variation);
    ACTDEMO_API FBox Bounds(EShape Shape);
    ACTDEMO_API float FootprintRadius(EShape Shape);
    ACTDEMO_API FColor PigmentColor(uint8 Pigment);
    ACTDEMO_API bool IsWoodyPigment(uint8 Pigment);
}
