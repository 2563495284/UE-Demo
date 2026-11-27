#pragma once

#include "GameFramework/Actor.h"
#include "PartyMudBall.generated.h"

class UMaterialInterface;
class UStaticMeshComponent;

USTRUCT()
struct FPartyMudFlight
{
    GENERATED_BODY()
    UPROPERTY() FVector_NetQuantize10 Start = FVector::ZeroVector;
    UPROPERTY() FVector_NetQuantize10 Velocity = FVector::ZeroVector;
    UPROPERTY() double LaunchedAt = -1.;
};

USTRUCT()
struct FPartyMudStop
{
    GENERATED_BODY()
    UPROPERTY() FVector_NetQuantize10 Point = FVector::ZeroVector;
    UPROPERTY() FVector_NetQuantizeNormal Normal = FVector::UpVector;
    UPROPERTY() double Time = -1.;
};

UCLASS(NotPlaceable)
class PARTYFRAMEWORK_API APartyMudBall : public AActor
{
    GENERATED_BODY()
public:
    APartyMudBall();
    static constexpr double ThrowSpeed = 960., ArcLift = 220., FallAcceleration = 1150., Radius = 9.;
    static FVector LaunchVelocity(const FVector& Aim);
    static FVector AimAtPoint(const FVector& From, const FVector& BallCenter);
    static FVector AimAtSurface(const FVector& From, const FVector& SightDirection, const FHitResult& Hit);
    void Launch(const FVector& Aim);
    virtual void Tick(float DeltaSeconds) override;
    virtual void BeginPlay() override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;

protected:
    virtual bool SweepFlight(const FVector& Start, const FVector& End, FHitResult& Hit) const;
    virtual void ApplyImpact(const FHitResult& Hit);
    virtual bool GetThrowHandLocation(FVector& OutLocation) const;

private:
    double ServerNow() const;
    FVector PositionAt(double Age) const;
    void DrawStop(double Now);
    void UpdateFlyingClod(double Age);

    UPROPERTY(Replicated) FPartyMudFlight Flight;
    UPROPERTY(Replicated) FPartyMudStop Stop;
    UPROPERTY() TObjectPtr<UStaticMeshComponent> Clod;
    UPROPERTY() TObjectPtr<UMaterialInterface> ClayBase;
    TOptional<FVector> HandOffset;
    double SimulatedAge = 0.;
};
