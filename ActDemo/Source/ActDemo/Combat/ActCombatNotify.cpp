#include "Combat/ActCombatNotify.h"
#include "Character/ActCharacter.h"
void UActCombatNotify::NotifyBegin(USkeletalMeshComponent* Mesh, UAnimSequenceBase*, float, const FAnimNotifyEventReference&)
{ if (auto* Character = Cast<AActCharacter>(Mesh->GetOwner())) Character->SetCombatWindow(Window, true); }
void UActCombatNotify::NotifyEnd(USkeletalMeshComponent* Mesh, UAnimSequenceBase*, const FAnimNotifyEventReference&)
{ if (auto* Character = Cast<AActCharacter>(Mesh->GetOwner())) Character->SetCombatWindow(Window, false); }
