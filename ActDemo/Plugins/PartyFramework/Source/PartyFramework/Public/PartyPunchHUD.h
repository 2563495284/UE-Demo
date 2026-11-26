#pragma once
#include "GameFramework/HUD.h"

namespace PartyPunch
{
    inline void DrawChargeRing(AHUD& HUD,float X,float Y,float UIScale,float Alpha)
    {
        if (Alpha<=0.f) return;
        const float Radius=14.f*UIScale;
        const FLinearColor Active=Alpha>=.999f ? FLinearColor(1.f,.75f,.22f) : FLinearColor(.88f,.97f,.92f);
        for (int32 I=0;I<32;++I)
        {
            const float A=UE_TWO_PI*I/32.f-UE_HALF_PI, B=A+UE_TWO_PI/32.f;
            HUD.DrawLine(X+Radius*FMath::Cos(A),Y+Radius*FMath::Sin(A),X+Radius*FMath::Cos(B),Y+Radius*FMath::Sin(B),
                I<Alpha*32.f ? Active : FLinearColor(.09f,.14f,.16f,.70f),2.f*UIScale);
        }
    }
}
