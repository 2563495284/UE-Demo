#include "PartyCrewMotion.h"
#include "PartyPunch.h"

namespace
{
    float Smooth(float T)
    {
        T=FMath::Clamp(T,0.f,1.f);
        return T*T*(3.f-2.f*T);
    }

    float Beat(double Now,double At,float Duration)
    {
        if (At<0. || Now<At || Now>=At+Duration) return 0.f;
        return FMath::Sin(PI*(Now-At)/Duration);
    }
}

void FPartyCrewMotion::Charge(double Now,bool bPressed,float Elapsed)
{
    ChargeAt=bPressed ? Now-FMath::Max(Elapsed,0.f) : -1.;
    if (bPressed) StrikeAt=-1.;
}

void FPartyCrewMotion::Strike(double Now,float Strength)
{
    StrikeAt=Now;
    StrikeStrength=FMath::Clamp(Strength,0.f,1.f);
    ChargeAt=-1.;
}

void FPartyCrewMotion::Throw(double Now) { TossAt=Now; }

FPartyCrewPose FPartyCrewMotion::Sample(double Now,float Delta,float Speed,float UpSpeed,bool bAirborne,
    bool bStunned,bool bCompact,bool bPrying)
{
    FPartyCrewPose P;
    const float Elapsed=FMath::Max(.001f,Delta);
    if (bWasAirborne && !bAirborne) LandedAt=Now;
    if ((PreviousSpeed-Speed)/Elapsed>1.4f && PreviousSpeed>.22f && Now-BrakedAt>.36) BrakedAt=Now;
    PreviousSpeed=Speed;
    bWasAirborne=bAirborne;
    Step+=Elapsed*(5.5f+Speed*7.f)*FMath::Min(1.f,Speed*6.f);
    P.Speed=Speed;
    P.Gait=FMath::Sin(Step)*Speed;
    P.Air=bAirborne ? FMath::Clamp(UpSpeed/450.f,-1.f,1.f) : 0.f;
    P.Landing=Beat(Now,LandedAt,.28f);
    P.Brake=Beat(Now,BrakedAt,.34f)*(1.f-Speed);
    P.Idle=FMath::Sin(3.f*Now);
    P.bStunned=bStunned;
    P.Pry=bPrying && !bStunned ? .83f+.17f*FMath::Sin(8.f*Now) : 0.f;
    if (!bStunned && !bPrying)
    {
        if (TossAt>=0.)
        {
            const float Age=Now-TossAt;
            // Projectiles leave on press. The elbow drives forward first,
            // then the palm rolls out and the arm falls back into guard.
            P.Toss=Smooth(Age/.065f)*(1.f-Smooth((Age-.085f)/.19f));
            P.TossFollow=Smooth((Age-.045f)/.09f)*(1.f-Smooth((Age-.165f)/.22f));
        }
        if (ChargeAt>=0.)
        {
            const float Alpha=FMath::Clamp(float((Now-ChargeAt)/PartyPunch::ChargeSeconds),0.f,1.f);
            P.Load=Smooth(Alpha);
        }
        else if (StrikeAt>=0.)
        {
            const float S=StrikeStrength;
            const float Age=Now-StrikeAt, Contact=.072f+S*.043f;
            const float PunchEnd=Contact+.026f+S*.019f, Return=.12f+S*.055f;
            P.Strength=S;
            P.Load=(1.f-Smooth(Age/Contact))*S;
            P.Extension=Smooth(Age/Contact)*(1.f-Smooth((Age-PunchEnd)/Return));
            P.Recoil=Smooth((Age-PunchEnd)/Return)*(1.f-Smooth((Age-PunchEnd-Return)/(.12f+.05f*S)))
                *(.38f+.62f*S);
        }
    }
    else
    {
        ChargeAt=-1.;
        StrikeAt=-1.;
        TossAt=-1.;
    }
    const float Wind=P.Load, Hit=P.Extension, Bounce=FMath::Abs(P.Gait);
    const float Punch=FMath::Max(Wind,Hit);
    const bool Relax=Speed<.12f && !bAirborne && !bStunned && !bCompact && !bPrying
        && ChargeAt<0. && (StrikeAt<0. || Now-StrikeAt>.56) && (TossAt<0. || Now-TossAt>.44);
    if (Relax)
    {
        RestClock+=Elapsed;
        P.Relaxed=Smooth(RestClock/.26f)*(1.f-Smooth(Speed/.12f));
        // Once after stopping, the surveyor checks the ground just ahead of
        // the working hand, then returns to a steady stance.
        if (bWalkedOrWorked)
            P.WorkLook=Smooth((RestClock-.28f)/.44f)*(1.f-Smooth((RestClock-1.02f)/.65f))*P.Relaxed;
        if (RestClock>1.7f) bWalkedOrWorked=false;
        const float Blink=FMath::Fmod(float(Now),5.7f)-2.4f;
        P.EyeClose=Smooth(Blink/.07f)*(1.f-Smooth((Blink-.085f)/.11f))*P.Relaxed;
    }
    else
    {
        RestClock=0.f;
        bWalkedOrWorked=Speed>=.12f || bPrying || ChargeAt>=0. || P.Toss>0.f || P.Extension>0.f || bWalkedOrWorked;
    }
    // Ears yield for longer than the stiffer felt brim; both settle after contact.
    const float Impact=Hit*(.25f+.75f*P.Strength);
    const float EarTarget=14.f*P.Brake-16.f*P.Landing+8.f*P.Air-20.f*Impact+5.f*Wind;
    const float BrimTarget=9.f*P.Brake-11.f*P.Landing+3.f*P.Air-12.f*Impact+3.f*Wind+Bounce*2.f;
    FMath::SpringDamper(EarPitch,EarRate,EarTarget,0.f,Elapsed,2.9f,.46f);
    FMath::SpringDamper(BrimPitch,BrimRate,BrimTarget,0.f,Elapsed,4.1f,.52f);
    P.EarPitch=EarPitch;
    P.BrimPitch=BrimPitch;
    P.BodyOffset={-5.f*Wind+7.f*Hit-2.5f*P.Recoil+1.6f*P.Toss, -4.f*Wind+3.f*Hit,
        Bounce*3.f+.8f*P.Idle*(1.f-Speed)-6.f*Wind-8.f*P.Landing-5.f*P.Pry-1.5f*P.Recoil};
    P.BodyRot={-6.f*Speed+10.f*Wind-11.f*Hit+11.f*P.Brake-10.f*P.Pry,
        35.f*Wind-28.f*Hit+7.f*P.Recoil+6.f*P.Toss,3.f*P.Gait-6.f*Wind+4.f*Hit-2.f*P.TossFollow};
    if (bStunned) { P.BodyRot.Pitch-=24.f; P.BodyRot.Roll+=15.f; }
    for (int32 I=0;I<2;++I)
    {
        const float Side=I ? 1.f : -1.f, LegGait=Side*P.Gait;
        const float G=LegGait*(1.f-Punch)*(I ? 1.f : 1.f-P.Toss);
        const bool Lead=I==1;
        P.Hands[I]={11.f+G*13.f,Side*39.f,61.f+G*4.f+Bounce*2.f};
        P.Hands[I].Z+=FMath::Abs(P.Air)*11.f;
        if (Lead)
        {
            P.Hands[I]+=FVector(-44.f*Wind+(39.f+17.f*P.Strength)*Hit,12.f*Wind-24.f*Hit,
                13.f*Wind+21.f*Hit);
            P.Hands[I]+=FVector(-9.f,4.f,7.f)*P.Recoil;
        }
        else
        {
            P.Hands[I]+=FVector(19.f*Wind+4.f*Hit,12.f*Wind+6.f*Hit,22.f*Wind+12.f*Hit)*(1.f-P.Toss);
            P.Hands[I]+=FVector(43.f,15.f,25.f)*P.Toss+FVector(3.f,-7.f,-5.f)*P.TossFollow;
        }
        P.Hands[I]=FMath::Lerp(P.Hands[I],FVector(43.f,Side*23.f,48.f),P.Pry);
        if (bStunned) P.Hands[I]={8.,Side*40.,45.};
        P.HandRot[I]={-11.f*G+(Lead ? -30.f*Wind+60.f*Hit : 22.f*Wind),-Side*8.f,-Side*(5.f+7.f*Punch)};
        if (!Lead) P.HandRot[I]+=FRotator(35.f*P.Toss,-9.f*P.TossFollow,-32.f*P.TossFollow);
        P.Feet[I]={7.f-LegGait*14.f-2.f*P.Brake-Side*(6.f*Wind+3.f*Hit),Side*(18.f+Wind*4.f),
            13.f+FMath::Max(0.f,-LegGait)*11.f+FMath::Abs(P.Air)*(I ? 9.f : 17.f)+(Lead ? 4.f*Hit : 0.f)};
        if (bCompact) P.Feet[I].X-=4.f;
        P.EyeHands[I]={39.f+G*1.8f,Side*20.f,-17.f+G};
        if (Lead) P.EyeHands[I]+=FVector(-8.f*Wind+(15.f+6.f*P.Strength)*Hit-3.f*P.Recoil,
            4.f*Wind-16.f*Hit+2.f*P.Recoil,2.f*Wind+13.f*Hit+3.f*P.Recoil);
        else P.EyeHands[I]+=FVector(6.f,7.f,6.f)*Wind*(1.f-P.Toss)
            +FVector(17.f,11.f,12.f)*P.Toss+FVector(2.f,-3.f,-2.f)*P.TossFollow;
        P.EyeHands[I]=FMath::Lerp(P.EyeHands[I],FVector(48.f,Side*14.f,-12.f),P.Pry);
        P.EyeRot[I]={17.f+(Lead ? -24.f*Wind+33.f*Hit : 24.f*Wind),-Side*12.f,Side*8.f*Punch};
        if (Lead) P.EyeRot[I].Pitch-=12.f*P.Recoil;
        else P.EyeRot[I]+=FRotator(27.f*P.Toss,-8.f*P.TossFollow,-26.f*P.TossFollow);
    }
    return P;
}
