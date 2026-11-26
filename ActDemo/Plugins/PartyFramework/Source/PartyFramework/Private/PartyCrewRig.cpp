#include "PartyCrewRig.h"
#include "Engine/SkinnedAsset.h"

UPartyCrewRig::UPartyCrewRig(const FObjectInitializer& Init):Super(Init)
{
    SetCollisionEnabled(ECollisionEnabled::NoCollision);
    SetGenerateOverlapEvents(false);
    SetCanEverAffectNavigation(false);
    SetReceivesDecals(false);
    bPerBoneMotionBlur=false;
}

int32 UPartyCrewRig::Bone(FName Name) const { return GetSkinnedAsset()->GetRefSkeleton().FindBoneIndex(Name); }

void UPartyCrewRig::Turn(FName Name,FRotator Local)
{
    const int32 Index=Bone(Name);
    if (BoneSpaceTransforms.IsValidIndex(Index))
        BoneSpaceTransforms[Index].SetRotation(BoneSpaceTransforms[Index].GetRotation()*Local.Quaternion());
}

void UPartyCrewRig::UpdateComponents()
{
    const auto& Ref=GetSkinnedAsset()->GetRefSkeleton();
    Components.SetNum(BoneSpaceTransforms.Num());
    for (int32 I=0;I<Components.Num();++I)
    {
        const int32 Parent=Ref.GetParentIndex(I);
        Components[I]=Parent==INDEX_NONE ? BoneSpaceTransforms[I] : BoneSpaceTransforms[I]*Components[Parent];
    }
}

void UPartyCrewRig::AimLimb(FName Upper,FName Lower,FName End,const FVector& Target,const FVector& Pole)
{
    const int32 A=Bone(Upper), B=Bone(Lower), C=Bone(End);
    if (!Components.IsValidIndex(A) || !Components.IsValidIndex(B) || !Components.IsValidIndex(C)) return;
    const FVector Start=Components[A].GetLocation();
    const FVector RestUpper=Components[B].GetLocation()-Start, RestLower=Components[C].GetLocation()-Components[B].GetLocation();
    const double L1=RestUpper.Size(), L2=RestLower.Size();
    if (L1<.2 || L2<.2) return;
    const FVector Along=(Target-Start).GetSafeNormal();
    const double D=FMath::Clamp((Target-Start).Size(),FMath::Abs(L1-L2)+.1,(L1+L2)*1.32);
    const double Stretch=FMath::Max(1.,D/((L1+L2)*.995));
    const double X=FMath::Clamp((D*D+FMath::Square(L1*Stretch)-FMath::Square(L2*Stretch))/(2.*D),0.,D);
    const FVector Bend=FVector::VectorPlaneProject(Pole,Along).GetSafeNormal();
    const FVector First=Along*X+Bend*FMath::Sqrt(FMath::Max(0.,FMath::Square(L1*Stretch)-X*X));
    const FVector Second=Along*D-First;
    const FQuat RotA=FQuat::FindBetweenNormals(RestUpper.GetSafeNormal(),First.GetSafeNormal())*Components[A].GetRotation();
    const FQuat RotB=FQuat::FindBetweenNormals(RestLower.GetSafeNormal(),Second.GetSafeNormal())*Components[B].GetRotation();
    const int32 Parent=GetSkinnedAsset()->GetRefSkeleton().GetParentIndex(A);
    BoneSpaceTransforms[A].SetRotation((Parent==INDEX_NONE ? RotA : Components[Parent].GetRotation().Inverse()*RotA).GetNormalized());
    BoneSpaceTransforms[B].SetRotation((RotA.Inverse()*RotB).GetNormalized());
    BoneSpaceTransforms[B].SetTranslation(BoneSpaceTransforms[B].GetTranslation()*Stretch);
    BoneSpaceTransforms[C].SetTranslation(BoneSpaceTransforms[C].GetTranslation()*Stretch);
}

void UPartyCrewRig::Apply(const FPartyCrewPose& P)
{
    if (!GetSkinnedAsset() || !IsVisible()) return;
    const auto& Rest=GetSkinnedAsset()->GetRefSkeleton().GetRefBonePose();
    if (Rest.Num()==0 || BoneSpaceTransforms.Num()!=Rest.Num()) return;
    if (RestAsset.Get()!=GetSkinnedAsset())
    {
        const auto& Skeleton=GetSkinnedAsset()->GetRefSkeleton();
        for (int32 I=0; I<2; ++I)
        {
            const int32 Wrist=Bone(I ? TEXT("hand_r") : TEXT("hand_l"));
            const int32 Ankle=Bone(I ? TEXT("foot_r") : TEXT("foot_l"));
            if (Wrist==INDEX_NONE || Ankle==INDEX_NONE) return;
            HandRest[I]=Skeleton.GetBoneAbsoluteTransform(Wrist).GetLocation();
            FootRest[I]=Skeleton.GetBoneAbsoluteTransform(Ankle).GetLocation();
        }
        RestAsset=GetSkinnedAsset();
    }
    BoneSpaceTransforms=Rest;
    const float Hit=P.Extension, W=P.Load, G=P.Gait, Bob=P.Idle;
    Turn(TEXT("spine"),{-3.f*P.Speed-10.f*P.Pry+3.f*W,7.f*W-10.f*Hit,2.f*G});
    Turn(TEXT("chest"),{-3.f*P.Landing+.65f*Bob,12.f*W-15.f*Hit,-4.f*W+3.f*Hit});
    Turn(TEXT("neck"),{4.f*P.Speed+7.f*P.Pry,-12.f*W+15.f*Hit,2.f*G});
    Turn(TEXT("head"),{.5f*Bob+4.f*P.Brake+2.f*P.Air-4.3f*P.WorkLook,-16.f*W+17.f*Hit+6.8f*P.WorkLook,
        1.5f*G+.65f*P.WorkLook});
    Turn(TEXT("hat_brim"),{P.BrimPitch*.7f,0.,0.});
    for(int32 I=0;I<2;++I)
    {
        const float S=I ? 1.f : -1.f;
        Turn(I ? TEXT("ear_r") : TEXT("ear_l"),{3.f*G*S+.62f*P.EarPitch,0.,S*(.65f*Bob+4.f*W+3.f*P.Landing)});
        const int32 Eye=Bone(I ? TEXT("eyelid_r") : TEXT("eyelid_l"));
        if (BoneSpaceTransforms.IsValidIndex(Eye))
            BoneSpaceTransforms[Eye].SetScale3D(BoneSpaceTransforms[Eye].GetScale3D()*FVector(1.,1.,1.-.72*P.EyeClose));
    }
    Turn(TEXT("jaw"),{-.4f-.25f*Bob-3.f*Hit+.8f*W,0.,0.});
    Turn(TEXT("tail_01"),{1.5f*Bob+5.f*P.Brake,8.f*G-7.f*W+6.f*Hit,0.});
    Turn(TEXT("tail_02"),{1.f*Bob,6.f*G-4.f*W,0.});
    UpdateComponents();
    for (int32 I=0;I<2;++I)
    {
        const double Side=I ? 1. : -1.;
        // Keep each sculpt's natural reach while all four use the same chest grip.
        const FVector HandOffset=HandRest[I]-FVector(11.,Side*39.,60.);
        FVector Hand=P.Hands[I]+FMath::Lerp(HandOffset,FVector(-2.,Side*6.,-3.),double(P.Pry));
        Hand.Z-=P.Relaxed;
        Hand.Z+=I ? 14.f*Hit+10.f*W : 7.f*Hit+10.f*W;
        AimLimb(I ? TEXT("upperarm_r") : TEXT("upperarm_l"),I ? TEXT("lowerarm_r") : TEXT("lowerarm_l"),
            I ? TEXT("hand_r") : TEXT("hand_l"),Hand,FVector(-.75,Side*.8,-.65));
        Turn(I ? TEXT("hand_r") : TEXT("hand_l"),{I ? -8.f*W+19.f*Hit : 8.f*W,-Side*3.f,0.});
        AimLimb(I ? TEXT("thigh_r") : TEXT("thigh_l"),I ? TEXT("calf_r") : TEXT("calf_l"),
            I ? TEXT("foot_r") : TEXT("foot_l"),P.Feet[I]+FootRest[I]-FVector(7.,Side*18.,13.),FVector(1.,Side*.12,0.));
    }
    MarkRefreshTransformDirty();
}
