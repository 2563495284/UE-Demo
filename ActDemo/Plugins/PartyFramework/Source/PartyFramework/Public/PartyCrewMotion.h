#pragma once

#include "CoreMinimal.h"

// Shared world-body and near-camera acting for both expedition and arena pawns.
struct PARTYFRAMEWORK_API FPartyCrewPose
{
    FVector Hands[2], Feet[2], EyeHands[2];
    FRotator HandRot[2], EyeRot[2];
    FVector BodyOffset = FVector::ZeroVector;
    FRotator BodyRot = FRotator::ZeroRotator;
    float Gait = 0.f, Speed = 0.f, Load = 0.f, Extension = 0.f, Strength = 0.f;
    float Air = 0.f, Landing = 0.f, Brake = 0.f, Idle = 0.f, Pry = 0.f, Toss = 0.f;
    float EarPitch = 0.f, BrimPitch = 0.f;
    float WorkLook = 0.f, EyeClose = 0.f, Relaxed = 0.f;
    bool bStunned = false;
};

struct PARTYFRAMEWORK_API FPartyCrewMotion
{
    void Charge(double Now, bool bPressed, float Elapsed = 0.f);
    void Strike(double Now, float Strength);
    void Throw(double Now);
    FPartyCrewPose Sample(double Now, float Delta, float Speed, float UpSpeed, bool bAirborne,
        bool bStunned = false, bool bCompact = false, bool bPrying = false);
private:
    double ChargeAt = -1., StrikeAt = -1., TossAt = -1., LandedAt = -1., BrakedAt = -1.;
    float StrikeStrength = 0.f, Step = 0.f, PreviousSpeed = 0.f;
    float EarPitch = 0.f, EarRate = 0.f, BrimPitch = 0.f, BrimRate = 0.f;
    float RestClock = 0.f;
    bool bWasAirborne = false, bWalkedOrWorked = false;
};
