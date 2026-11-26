#pragma once
#include "CoreMinimal.h"

namespace PartyPunch
{
    constexpr float ChargeSeconds = 1.2f;
    constexpr float Reach = 235.f;
    inline uint8 Quantize(float Seconds)
    {
        return uint8(FMath::RoundToInt(255.f*FMath::Clamp(Seconds/ChargeSeconds,0.f,1.f)));
    }
    // Reliable press and release packets can arrive closer together (or farther apart)
    // than the inputs. The server bounds compensation by the measured link round-trip time.
    inline uint8 ResolvePower(double ServerSeconds,float ReportedSeconds,float PingMilliseconds)
    {
        const float Elapsed=FMath::Max(0.f,float(ServerSeconds));
        if (!FMath::IsFinite(ReportedSeconds) || ReportedSeconds<0.f || PingMilliseconds<0.f)
            return Quantize(Elapsed);
        const float Jitter=FMath::Clamp(.035f+.002f*PingMilliseconds,.035f,.20f);
        return Quantize(FMath::Clamp(ReportedSeconds,Elapsed-Jitter,Elapsed+Jitter));
    }
    inline float Alpha(uint8 Power) { return float(Power)/255.f; }
    inline float RadiusScale(uint8 Power,bool bBoost=false) { return (bBoost ? 2.f : 1.f)+Alpha(Power); }
}
