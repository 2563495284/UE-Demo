#pragma once

#include "PartyMudBall.h"
#include "ExpeditionMudBall.generated.h"

class AVoxelCharacter;

UCLASS(NotPlaceable)
class AExpeditionMudBall : public APartyMudBall
{
    GENERATED_BODY()
public:
    static bool TraceContact(UWorld& World, const AVoxelCharacter& Thrower, const FVector& Start,
        const FVector& End, float SweepRadius, FHitResult& Hit);

protected:
    virtual bool SweepFlight(const FVector& Start, const FVector& End, FHitResult& Hit) const override;
    virtual void ApplyImpact(const FHitResult& Hit) override;
    virtual bool GetThrowHandLocation(FVector& OutLocation) const override;
};
