#pragma once
#include "Components/PoseableMeshComponent.h"
#include "PartyCrewMotion.h"
#include "PartyCrewRig.generated.h"

// Skin and face share a single component. All joints are visual, the capsule owns motion.
UCLASS()
class PARTYFRAMEWORK_API UPartyCrewRig : public UPoseableMeshComponent
{
    GENERATED_BODY()
public:
    UPartyCrewRig(const FObjectInitializer& Init);
    void Apply(const FPartyCrewPose& Pose);
private:
    TArray<FTransform> Components;
    TWeakObjectPtr<USkinnedAsset> RestAsset;
    FVector HandRest[2], FootRest[2];
    int32 Bone(FName Name) const;
    void Turn(FName Name,FRotator Local);
    void UpdateComponents();
    void AimLimb(FName Upper,FName Lower,FName End,const FVector& Target,const FVector& Pole);
};
