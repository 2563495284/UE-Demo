#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ActSmokeProbe.generated.h"
class AActCharacter;
/** Opt-in functional smoke test; spawned only with -ActDemoSmoke in a non-shipping build. */
UCLASS(NotBlueprintable)
class AActSmokeProbe : public AActor
{
 GENERATED_BODY()
public:
 AActSmokeProbe();
 virtual void Tick(float Dt) override;
private:
 void Check(bool bCondition,const TCHAR* Message);
 void Next(float Delay);
 void StopEnemies();
 TWeakObjectPtr<AActCharacter> Hero;
 TWeakObjectPtr<AActCharacter> Enemy;
 int32 Stage=0;
 int32 Failures=0;
 int32 Checks=0;
 float Until=2.f;
 float PreviousHealth=0.f;
};
