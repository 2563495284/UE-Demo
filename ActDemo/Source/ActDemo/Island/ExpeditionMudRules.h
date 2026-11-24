#pragma once

#include "CoreMinimal.h"

struct FVoxelDensity;

/** The shell stays at the original knockdown site even after its occupant moves. */
struct FExpeditionBurialSite
{
    FVector Foot = FVector::ZeroVector;
    int32 Layers = 0;
    bool bPending = false;
    bool bAwaitingLanding = false;
    uint32 Generation = 0;

    void Begin(const FVoxelDensity& Density, const FVector& Position);
    bool WasAir(const FVector& Position) const;

private:
    static constexpr int32 Across = 15;
    static constexpr int32 High = 11;
    FIntVector First = FIntVector::ZeroValue;
    FIntVector Origin = FIntVector::ZeroValue;
    TArray<int16> Before;
};
