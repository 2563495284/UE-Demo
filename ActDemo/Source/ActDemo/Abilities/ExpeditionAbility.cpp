#include "Abilities/ExpeditionAbility.h"

#include "AbilitySystemComponent.h"
#include "Algo/MaxElement.h"
#include "EngineUtils.h"
#include "NativeGameplayTags.h"
#include "TimerManager.h"
#include "Island/IslandGameMode.h"
#include "Island/ExpeditionBodyMud.h"
#include "Island/IslandNeutralProp.h"
#include "Island/IslandPlayerController.h"
#include "Island/IslandState.h"
#include "Island/IslandWorld.h"
#include "Voxel/VoxelCharacter.h"

namespace ExpeditionTags
{
    UE_DEFINE_GAMEPLAY_TAG_STATIC(Dig, "Expedition.Action.Dig");
    UE_DEFINE_GAMEPLAY_TAG_STATIC(Throw, "Expedition.Action.Throw");
    UE_DEFINE_GAMEPLAY_TAG_STATIC(Jump, "Expedition.Action.Jump");
    UE_DEFINE_GAMEPLAY_TAG_STATIC(Interact, "Expedition.Action.Interact");
    UE_DEFINE_GAMEPLAY_TAG_STATIC(Rally, "Expedition.Action.Rally");
    UE_DEFINE_GAMEPLAY_TAG_STATIC(Recall, "Expedition.Action.Recall");
    UE_DEFINE_GAMEPLAY_TAG_STATIC(DigCooldown, "Expedition.Cooldown.Dig");
    UE_DEFINE_GAMEPLAY_TAG_STATIC(ThrowCooldown, "Expedition.Cooldown.Throw");
}

FGameplayTag ExpeditionAbilities::ActionTag(EExpeditionAction Action)
{
    switch (Action)
    {
    case EExpeditionAction::Dig: return ExpeditionTags::Dig;
    case EExpeditionAction::Throw: return ExpeditionTags::Throw;
    case EExpeditionAction::Jump: return ExpeditionTags::Jump;
    case EExpeditionAction::Interact: return ExpeditionTags::Interact;
    case EExpeditionAction::Rally: return ExpeditionTags::Rally;
    case EExpeditionAction::Recall: return ExpeditionTags::Recall;
    }
    return FGameplayTag();
}


UExpeditionTimedEffect::UExpeditionTimedEffect()
{
    DurationPolicy = EGameplayEffectDurationType::HasDuration;
    DurationMagnitude = FScalableFloat(1.f);
}

bool ExpeditionAbilities::ApplyTimedTag(UAbilitySystemComponent& ASC, FGameplayTag Tag, float Seconds)
{
    FGameplayEffectContextHandle Context = ASC.MakeEffectContext();
    FGameplayEffectSpecHandle Spec = ASC.MakeOutgoingSpec(UExpeditionTimedEffect::StaticClass(), 1.f, Context);
    if (!Spec.IsValid()) return false;
    Spec.Data->SetDuration(Seconds, true);
    Spec.Data->DynamicGrantedTags.AddTag(Tag);
    return ASC.ApplyGameplayEffectSpecToSelf(*Spec.Data.Get()).IsValid();
}

void ExpeditionAbilities::ClearTimedTags(UAbilitySystemComponent& ASC)
{
    FGameplayTagContainer Tags;
    Tags.AddTag(ExpeditionTags::DigCooldown);
    Tags.AddTag(ExpeditionTags::ThrowCooldown);
    ASC.RemoveActiveEffects(FGameplayEffectQuery::MakeQuery_MatchAnyOwningTags(Tags));
}

void ExpeditionAbilities::Grant(UAbilitySystemComponent& ASC)
{
    const TSubclassOf<UGameplayAbility> Classes[] = {UExpeditionDigAbility::StaticClass(), UExpeditionThrowAbility::StaticClass(),
        UExpeditionJumpAbility::StaticClass(), UExpeditionInteractAbility::StaticClass(), UExpeditionRallyAbility::StaticClass(),
        UExpeditionRecallAbility::StaticClass()};
    for (const auto& Class : Classes)
        if (!ASC.FindAbilitySpecFromClass(Class)) ASC.GiveAbility(FGameplayAbilitySpec(Class, 1));
}

UExpeditionAbility::UExpeditionAbility()
{
    InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
    NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::ServerOnly;
    NetSecurityPolicy = EGameplayAbilityNetSecurityPolicy::ServerOnly;
}

void UExpeditionAbility::SetAction(EExpeditionAction InAction)
{
    Action = InAction;
    FAbilityTriggerData Data;
    Data.TriggerTag = ExpeditionAbilities::ActionTag(InAction);
    Data.TriggerSource = EGameplayAbilityTriggerSource::GameplayEvent;
    AbilityTriggers.Add(Data);
    SetAssetTags(FGameplayTagContainer(Data.TriggerTag));
}

UExpeditionDigAbility::UExpeditionDigAbility()
{
    SetAction(EExpeditionAction::Dig);
    Cooldown = ExpeditionTags::DigCooldown;
    Interval = .50f;
}
UExpeditionThrowAbility::UExpeditionThrowAbility()
{
    SetAction(EExpeditionAction::Throw);
    Cooldown = ExpeditionTags::ThrowCooldown;
    Interval = .60f;
}
UExpeditionInteractAbility::UExpeditionInteractAbility() { SetAction(EExpeditionAction::Interact); }
UExpeditionRallyAbility::UExpeditionRallyAbility() { SetAction(EExpeditionAction::Rally); }
UExpeditionRecallAbility::UExpeditionRecallAbility() { SetAction(EExpeditionAction::Recall); }
UExpeditionJumpAbility::UExpeditionJumpAbility()
{
    SetAction(EExpeditionAction::Jump);
    NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::LocalPredicted;
    NetSecurityPolicy = EGameplayAbilityNetSecurityPolicy::ClientOrServer;
}

bool UExpeditionAbility::CanActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* Info,
    const FGameplayTagContainer* SourceTags, const FGameplayTagContainer* TargetTags, FGameplayTagContainer* OptionalRelevantTags) const
{
    if (!Info || !Super::CanActivateAbility(Handle, Info, SourceTags, TargetTags, OptionalRelevantTags)) return false;
    const auto* Hero = Cast<AVoxelCharacter>(Info->AvatarActor.Get());
    const auto* PC = Hero ? Cast<AIslandPlayerController>(Hero->GetController()) : nullptr;
    if (!PC || !PC->UsesExpeditionAbilities()) return false;
    if (Hero->HasAuthority())
    {
        const auto* Mode = Hero->GetWorld()->GetAuthGameMode<AIslandGameMode>();
        return Mode && (Mode->CanPlayerAct(*PC) || (Action == EExpeditionAction::Dig && Mode->CanPlayerPunch(*PC)));
    }
    return Action == EExpeditionAction::Jump && !Hero->IsMudAnchored() && PC->IsPlaying() && PC->IsVoxelInputEnabled();
}

void UExpeditionAbility::ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* Info,
    const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* Payload)
{
    auto* Hero = CastChecked<AVoxelCharacter>(Info->AvatarActor.Get());
    auto* PC = CastChecked<AIslandPlayerController>(Hero->GetController());
    if (!CommitAbility(Handle, Info, ActivationInfo))
    {
        EndAbility(Handle, Info, ActivationInfo, true, true);
        return;
    }
#if !UE_BUILD_SHIPPING
    if (FParse::Param(FCommandLine::Get(), TEXT("IslandQA")))
        UE_LOG(LogTemp, Display, TEXT("EXPEDITION_GAS_ACTIVATE action=%s player=%s held=%d"),
            *ExpeditionAbilities::ActionTag(Action).ToString(), *PC->GetName(), Payload && Payload->EventMagnitude > 0.f);
#endif
    Perform(*Hero, *PC, Payload);
}

void UExpeditionJumpAbility::Perform(AVoxelCharacter& Hero, AIslandPlayerController& PC, const FGameplayEventData* Payload)
{
    Hero.Jump();
    K2_EndAbility();
}

void UExpeditionInteractAbility::Perform(AVoxelCharacter& Hero, AIslandPlayerController& PC, const FGameplayEventData* Payload)
{
    if (const auto* Chest = Payload ? Cast<AIslandChest>(Payload->Target.Get()) : nullptr)
        Hero.GetWorld()->GetAuthGameMode<AIslandGameMode>()->TryOpen(PC, *Chest);
    K2_EndAbility();
}

void UExpeditionRallyAbility::Perform(AVoxelCharacter& Hero, AIslandPlayerController& PC, const FGameplayEventData* Payload)
{
    Hero.GetWorld()->GetAuthGameMode<AIslandGameMode>()->RallyCrew(PC);
    K2_EndAbility();
}

void UExpeditionDigAbility::UseTool(AVoxelCharacter& Hero, AIslandPlayerController& PC, AIslandGameMode& Mode, const FVector& Aim)
{
    Hero.MulticastExpeditionDig(PC.GetReleasedPunchPower());
    if (Hero.IsMudAnchored()) Hero.GetBodyMud()->Peel();
    else Mode.RequestEdit(PC, EVoxelAction::Punch, Aim, PC.GetReleasedPunchPower(), PC.IsPunchRelease());
}

void UExpeditionThrowAbility::UseTool(AVoxelCharacter& Hero, AIslandPlayerController& PC, AIslandGameMode& Mode, const FVector& Aim)
{
    Mode.ThrowExpeditionMud(PC, Aim);
}

void UExpeditionToolAbility::Perform(AVoxelCharacter& Hero, AIslandPlayerController& PC, const FGameplayEventData* Payload)
{
    const bool bHeld = Payload && Payload->EventMagnitude > 0.f;
    // A held fist winds up without spending its cooldown. Its release is a single activation.
    if (Action == EExpeditionAction::Dig && bHeld) return;
    bRepeat = bHeld;
    Swing();
}

void UExpeditionToolAbility::Swing()
{
    auto* Hero = Cast<AVoxelCharacter>(GetAvatarActorFromActorInfo());
    auto* PC = Hero ? Cast<AIslandPlayerController>(Hero->GetController()) : nullptr;
    auto* Mode = Hero ? Hero->GetWorld()->GetAuthGameMode<AIslandGameMode>() : nullptr;
    if (!PC || !Mode || !(Action == EExpeditionAction::Dig ? Mode->CanPlayerPunch(*PC) : Mode->CanPlayerAct(*PC)))
    {
        K2_EndAbility();
        return;
    }
    UAbilitySystemComponent& ASC = *Hero->GetAbilitySystemComponent();
    float Delay = Interval;
    if (ASC.HasMatchingGameplayTag(Cooldown))
    {
        const FGameplayEffectQuery Query = FGameplayEffectQuery::MakeQuery_MatchAnyOwningTags(FGameplayTagContainer(Cooldown));
        const TArray<float> Remaining = ASC.GetActiveEffectsTimeRemaining(Query);
        if (const float* Latest = Algo::MaxElement(Remaining))
        {
            // Incoming RPCs can execute before this frame's GAS expiry timer. An
            // elapsed effect must not eat a legal tap or delay the held cadence.
            if (*Latest <= 0.f)
                ASC.RemoveActiveEffects(Query);
            else
                Delay = FMath::Max(.005f, *Latest);
        }
    }
    if (!ASC.HasMatchingGameplayTag(Cooldown))
    {
        ExpeditionAbilities::ApplyTimedTag(ASC, Cooldown, Interval);
#if !UE_BUILD_SHIPPING
        if (FParse::Param(FCommandLine::Get(), TEXT("IslandQA")))
            UE_LOG(LogTemp, Display, TEXT("EXPEDITION_GAS_USE action=%s player=%s time=%.3f aim=%s"),
                *ExpeditionAbilities::ActionTag(Action).ToString(), *PC->GetName(), GetWorld()->GetTimeSeconds(),
                *PC->GetExpeditionActionAim(Action).ToCompactString());
#endif
        UseTool(*Hero, *PC, *Mode, PC->GetExpeditionActionAim(Action));
    }
#if !UE_BUILD_SHIPPING
    else if (!bRepeat && FParse::Param(FCommandLine::Get(), TEXT("IslandQA")))
        UE_LOG(LogTemp, Display, TEXT("EXPEDITION_GAS_COOLDOWN action=%s remaining=%.3f world=%.3f"),
            *ExpeditionAbilities::ActionTag(Action).ToString(), Delay, GetWorld()->GetTimeSeconds());
#endif
    if (bRepeat)
        GetWorld()->GetTimerManager().SetTimer(Timer, this, &ThisClass::Swing, Delay, false);
    else
        K2_EndAbility();
}

void UExpeditionRecallAbility::Perform(AVoxelCharacter& Hero, AIslandPlayerController& PC, const FGameplayEventData* Payload)
{
    GetWorld()->GetTimerManager().SetTimer(Timer, this, &ThisClass::CompleteRecall, 2.f, false);
}

void UExpeditionRecallAbility::CompleteRecall()
{
    auto* Hero = Cast<AVoxelCharacter>(GetAvatarActorFromActorInfo());
    auto* PC = Hero ? Cast<AIslandPlayerController>(Hero->GetController()) : nullptr;
    auto* Mode = Hero ? Hero->GetWorld()->GetAuthGameMode<AIslandGameMode>() : nullptr;
    if (Mode && PC && Mode->CanPlayerAct(*PC)) Mode->RecoverPlayer(*PC);
    if (IsActive()) K2_EndAbility();
}

void UExpeditionAbility::EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* Info,
    const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled)
{
    if (GetWorld()) GetWorld()->GetTimerManager().ClearTimer(Timer);
    Super::EndAbility(Handle, Info, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

float UExpeditionCharacterMovement::GetIslandJumpZVelocity() const
{
    const auto* State = GetWorld() ? GetWorld()->GetGameState<AIslandGameState>() : nullptr;
    return State && State->IsDecoyEventActive(EIslandDecoyEvent::SpringTide)
        ? JumpZVelocity * IslandRules::SpringTideJumpScale : JumpZVelocity;
}

bool UExpeditionCharacterMovement::DoJump(bool bReplayingMoves, float DeltaTime)
{
    const bool bFromGround = IsMovingOnGround();
    TGuardValue<float> JumpSpeed(JumpZVelocity, GetIslandJumpZVelocity());
    if (!Super::DoJump(bReplayingMoves, DeltaTime)) return false;
    const auto* Hero = Cast<AVoxelCharacter>(CharacterOwner);
    const auto* Round = GetWorld()->GetGameState<AIslandGameState>();
    if (!bFromGround || !Hero || !Round || !Round->bRoomLobby || Round->Phase!=EIslandPhase::Hunting) return true;
    // Apply the same takeoff on the autonomous client, its saved-move replays and the server.
    // An authority-only, next-frame launch otherwise competes with a replayed ordinary jump.
    for (TActorIterator<AIslandNeutralProp> It(GetWorld()); It; ++It)
    {
        if (const auto Launch = It->WindJumpVelocity(*Hero))
        {
            Velocity = *Launch;
#if !UE_BUILD_SHIPPING
            if (FParse::Param(FCommandLine::Get(),TEXT("ExpeditionNeutralQA")))
                UE_LOG(LogTemp,Display,TEXT("ROUTE_QA_TAKEOFF authority=%d replay=%d at=%s launch=%s"),
                    Hero->HasAuthority(),bReplayingMoves,*Hero->GetActorLocation().ToCompactString(),*Velocity.ToCompactString());
#endif
            break;
        }
    }
    return true;
}

float UExpeditionCharacterMovement::GetMaxSpeed() const
{
    const auto* Hero = Cast<AVoxelCharacter>(CharacterOwner);
    if (Hero && Hero->IsMudAnchored()) return 0.f;
    const float Base = Super::GetMaxSpeed();
    return Hero && IsMovingOnGround() && !Hero->bIsCrouched
        ? AIslandNeutralProp::CurrentWalkSpeed(*Hero, GetCurrentAcceleration(), Base) : Base;
}
