#pragma once

#include "GameFramework/Actor.h"
#include "ExpeditionMudBall.generated.h"

class AVoxelCharacter;

USTRUCT()
struct FExpeditionMudFlight
{
    GENERATED_BODY()
    UPROPERTY() FVector_NetQuantize10 Start = FVector::ZeroVector;
    UPROPERTY() FVector_NetQuantize10 Velocity = FVector::ZeroVector;
    UPROPERTY() double LaunchedAt = -1.;
};

USTRUCT()
struct FExpeditionMudStop
{
    GENERATED_BODY()
    UPROPERTY() FVector_NetQuantize10 Point = FVector::ZeroVector;
    UPROPERTY() FVector_NetQuantizeNormal Normal = FVector::UpVector;
    UPROPERTY() TObjectPtr<AVoxelCharacter> MarkedCharacter;
    UPROPERTY() uint8 Marks = 0;
    UPROPERTY() double Time = -1.;
};

UCLASS(NotPlaceable)
class AExpeditionMudBall : public AActor
{
    GENERATED_BODY()
public:
    AExpeditionMudBall();
    static constexpr double ThrowSpeed = 960., ArcLift = 220., FallAcceleration = 1150., Radius = 9.;
    static FVector LaunchVelocity(const FVector& Aim);
    void Launch(const FVector& Aim);
    virtual void Tick(float DeltaSeconds) override;
    virtual void BeginPlay() override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;

private:
    double ServerNow() const;
    FVector PositionAt(double Age) const;
    bool TraceSegment(const FVector& Start, const FVector& End, FHitResult& Hit) const;
    void DrawStop(double Now);
    void ApplyLaunchPose();

    UPROPERTY(Replicated) FExpeditionMudFlight Flight;
    UPROPERTY(Replicated) FExpeditionMudStop Stop;
    UPROPERTY() TObjectPtr<UStaticMeshComponent> Clod;
    UPROPERTY() TObjectPtr<UMaterialInterface> ClayBase;
    bool bPlayedThrow = false;
    bool bPlayedMarks = false;
    double SimulatedAge = 0.;
};
