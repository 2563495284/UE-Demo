#pragma once

#include "CoreMinimal.h"

struct FIslandEchoAudio
{
    FVector Position;
    float Volume;
    float Pitch;
    float AnswerPitch;
    float AnswerDelay;

    static FIslandEchoAudio ForListener(FVector Ear, FVector Forward, FVector Direction, float Proximity)
    {
        // Place the cue relative to the active camera's ear. The bearing comes from
        // the excavation, even when the third-person camera is metres behind it.
        const FVector Bearing = Direction.IsNearlyZero() ? Forward : Direction;
        const float Ahead = FVector::DotProduct(Direction, Forward.GetSafeNormal2D());
        const float Pitch = FMath::Lerp(.84f, 1.28f, Proximity);
        return {
            Ear + Bearing * 300.,
            FMath::Lerp(.20f, 1.f, Proximity),
            Pitch,
            Pitch * FMath::Exp2(.5f * float(Direction.Z)),
            FMath::Lerp(.24f, .105f, .5f * (1.f + Ahead))
        };
    }
};
