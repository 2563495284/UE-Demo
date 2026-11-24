#pragma once

#include "GameplayEffect.h"
#include "ExpeditionMudEffect.generated.h"

class AVoxelCharacter;

/** All throwers contribute to a single expiring stack on the victim. */
UCLASS()
class UExpeditionMudMarkEffect : public UGameplayEffect
{
    GENERATED_BODY()
public:
    UExpeditionMudMarkEffect();
    static int32 Count(const AVoxelCharacter& Target);
    static int32 TryHit(AVoxelCharacter& Thrower, AVoxelCharacter& Target);
    static void Clear(AVoxelCharacter& Target);
};
