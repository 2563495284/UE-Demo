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
    static FVector AimAtPoint(const FVector& From, const FVector& BallCenter);
    static bool TraceContact(UWorld& World, const AVoxelCharacter& Thrower, const FVector& Start,
        const FVector& End, float SweepRadius, FHitResult& Hit);
    void Launch(const FVector& Aim);
    virtual void Tick(float DeltaSeconds) override;
    virtual void BeginPlay() override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;

private:
    double ServerNow() const;
    FVector PositionAt(double Age) const;
    void DrawStop(double Now);
    void UpdateFlyingClod(double Age);

    UPROPERTY(Replicated) FExpeditionMudFlight Flight;
    UPROPERTY(Replicated) FExpeditionMudStop Stop;
    UPROPERTY() TObjectPtr<UStaticMeshComponent> Clod;
    UPROPERTY() TObjectPtr<UMaterialInterface> ClayBase;
    TOptional<FVector> HandOffset;
    double SimulatedAge = 0.;
};
