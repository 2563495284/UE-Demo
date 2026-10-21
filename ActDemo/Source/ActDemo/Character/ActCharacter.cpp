#include "Character/ActCharacter.h"
#include "Combat/ActAttributeSet.h"
#include "Combat/ActCombatAbility.h"
#include "AI/ActAIController.h"
#include "UI/ActHUDWidget.h"
#include "World/ActGameMode.h"
#include "AbilitySystemComponent.h"
#include "Animation/AnimInstance.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/WidgetComponent.h"
#include "DrawDebugHelpers.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "Kismet/GameplayStatics.h"
#include "NiagaraFunctionLibrary.h"
#include "Perception/AISense_Hearing.h"
#include "Perception/AIPerceptionStimuliSourceComponent.h"
#include "Perception/AISense_Sight.h"

AActCharacter::AActCharacter()
{
 PrimaryActorTick.bCanEverTick = true;
 GetCapsuleComponent()->InitCapsuleSize(40.f, 90.f);
 GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
 GetMesh()->SetRelativeLocation(FVector(0, 0, -90));
 GetMesh()->SetRelativeRotation(FRotator(0, -90, 0));
 GetMesh()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
 GetMesh()->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
 static ConstructorHelpers::FObjectFinder<USkeletalMesh> Mesh(TEXT("/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple"));
 static ConstructorHelpers::FClassFinder<UAnimInstance> Anim(TEXT("/Game/Characters/Mannequins/Anims/Unarmed/ABP_Unarmed"));
 if (Mesh.Succeeded()) GetMesh()->SetSkeletalMesh(Mesh.Object);
 if (Anim.Succeeded()) GetMesh()->SetAnimInstanceClass(Anim.Class);
 GetCharacterMovement()->MaxWalkSpeed = 420.f;
 GetCharacterMovement()->bOrientRotationToMovement = true;
 GetCharacterMovement()->RotationRate = FRotator(0, 600, 0);
 GetCharacterMovement()->BrakingDecelerationWalking = 1800.f;
 bUseControllerRotationYaw = false;
 Abilities = CreateDefaultSubobject<UAbilitySystemComponent>(TEXT("Abilities"));
 Attributes = CreateDefaultSubobject<UActAttributeSet>(TEXT("Attributes"));
 CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
 CameraBoom->SetupAttachment(RootComponent);
 CameraBoom->TargetArmLength = 440.f;
 CameraBoom->SocketOffset = FVector(0, 55, 70);
 CameraBoom->bUsePawnControlRotation = true;
 Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
 Camera->SetupAttachment(CameraBoom);
 Camera->FieldOfView = 78.f;
 Weapon = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("TrainingBlade"));
 Weapon->SetupAttachment(GetMesh(), TEXT("hand_r"));
 static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube"));
 Weapon->SetStaticMesh(Cube.Object);
 Weapon->SetRelativeLocation(FVector(0, 0, -38));
 Weapon->SetRelativeScale3D(FVector(.055, .12, .85));
 Weapon->SetCollisionEnabled(ECollisionEnabled::NoCollision);
 LifeBar = CreateDefaultSubobject<UWidgetComponent>(TEXT("LifeBar"));
 LifeBar->SetupAttachment(RootComponent);
 LifeBar->SetRelativeLocation(FVector(0,0,125));
 LifeBar->SetWidgetSpace(EWidgetSpace::Screen);
 LifeBar->SetDrawSize(FVector2D(180, 45));
 LifeBar->SetWidgetClass(UActLifeBarWidget::StaticClass());
 CreateDefaultSubobject<UAIPerceptionStimuliSourceComponent>(TEXT("Stimuli"));
 AIControllerClass = AActAIController::StaticClass();
 AutoPossessAI = EAutoPossessAI::Disabled;
}
void AActCharacter::BeginPlay()
{
 Super::BeginPlay();
 // Registration needs a live world and perception system, which constructors lack.
 if (auto* Source=FindComponentByClass<UAIPerceptionStimuliSourceComponent>()) Source->RegisterForSense(UAISense_Sight::StaticClass());
 InitializeCombat();
 const TCHAR* MaterialName = Faction == EActFaction::Player ? TEXT("M_Player") : EnemyKind == EActEnemyKind::Boss ? TEXT("M_Boss") : EnemyKind == EActEnemyKind::Brute ? TEXT("M_Brute") : TEXT("M_Scout");
 if ((!Profile || !Profile->Mesh)) if (auto* Material = LoadObject<UMaterialInterface>(nullptr, *FString::Printf(TEXT("/Game/ActDemo/Materials/%s.%s"),MaterialName,MaterialName)))
  for (int32 I=0; I<GetMesh()->GetNumMaterials(); ++I) GetMesh()->SetMaterial(I,Material);
 if (auto* Material = LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/ActDemo/Materials/M_Amber.M_Amber"))) Weapon->SetMaterial(0,Material);
 LifeBar->SetVisibility(Faction != EActFaction::Player);
 if (UActLifeBarWidget* Bar = Cast<UActLifeBarWidget>(LifeBar->GetUserWidgetObject())) Bar->Observe(this);
}
void AActCharacter::InitializeCombat()
{
 if (bInitialized) return;
 bInitialized = true;
 if (!Profile && Faction == EActFaction::Player) Profile=LoadObject<UActCharacterProfile>(nullptr,TEXT("/Game/ActDemo/Data/DA_Player.DA_Player"));
 if (Profile)
 {
  InitialHealth=Profile->Health; DamageScale=Profile->DamageScale; DisplayName=Profile->DisplayName;
  if (Profile->Combat) CombatConfig=Profile->Combat;
  if (Profile->Mesh) GetMesh()->SetSkeletalMesh(Profile->Mesh);
  if (Profile->AnimClass) GetMesh()->SetAnimInstanceClass(Profile->AnimClass);
  Weapon->SetVisibility(Profile->WeaponTipSocket.IsNone());
 }
 if (!CombatConfig) CombatConfig = LoadObject<UActCombatConfig>(nullptr, TEXT("/Game/ActDemo/Data/DA_Combat.DA_Combat"));
 if (!CombatConfig) CombatConfig = NewObject<UActCombatConfig>(this);
 HealingCharges = CombatConfig->HealCharges;
 Abilities->InitAbilityActorInfo(this, this);
 Attributes->InitMaxHealth(InitialHealth);
 Attributes->InitHealth(InitialHealth);
 Attributes->InitMaxStamina(100.f);
 Attributes->InitStamina(100.f);
 Abilities->GiveAbility(FGameplayAbilitySpec(UActLightAbility::StaticClass()));
 Abilities->GiveAbility(FGameplayAbilitySpec(UActHeavyAbility::StaticClass()));
 Abilities->GiveAbility(FGameplayAbilitySpec(UActDodgeAbility::StaticClass()));
 Abilities->GiveAbility(FGameplayAbilitySpec(UActHealAbility::StaticClass()));
 Abilities->GetGameplayAttributeValueChangeDelegate(UActAttributeSet::GetHealthAttribute()).AddUObject(this, &AActCharacter::HealthChanged);
 Abilities->GetGameplayAttributeValueChangeDelegate(UActAttributeSet::GetStaminaAttribute()).AddWeakLambda(this, [this](const FOnAttributeChangeData&){ OnChanged.Broadcast(); });
 OnChanged.Broadcast();
}
void AActCharacter::EndPlay(const EEndPlayReason::Type Reason)
{
 ClearInput();
 Abilities->CancelAllAbilities();
 Abilities->GetGameplayAttributeValueChangeDelegate(UActAttributeSet::GetHealthAttribute()).RemoveAll(this);
 Abilities->GetGameplayAttributeValueChangeDelegate(UActAttributeSet::GetStaminaAttribute()).RemoveAll(this);
 OnChanged.Clear();
 OnActionEnded.Clear();
 Super::EndPlay(Reason);
}
UAbilitySystemComponent* AActCharacter::GetAbilitySystemComponent() const { return Abilities; }
bool AActCharacter::IsAlive() const { return Attributes->GetHealth() > 0 && !Abilities->HasMatchingGameplayTag(ActTags::Dead); }
bool AActCharacter::IsBusy() const { return CurrentAction != EActAction::None || GetWorld()->GetTimeSeconds() < StaggerUntil; }
float AActCharacter::HealthFraction() const { return Attributes->GetHealth() / FMath::Max(1.f, Attributes->GetMaxHealth()); }
float AActCharacter::StaminaFraction() const { return Attributes->GetStamina() / FMath::Max(1.f, Attributes->GetMaxStamina()); }
bool AActCharacter::IsHostileTo(const AActCharacter* Other) const { return Other && Other != this && Faction != Other->Faction; }
FActAttack AActCharacter::GetAttack() const
{
 if (CurrentAction == EActAction::Heavy) return CombatConfig->HeavyAttack;
 return CombatConfig->LightAttacks.IsValidIndex(ComboIndex) ? CombatConfig->LightAttacks[ComboIndex] : FActAttack();
}
float AActCharacter::ActionCost(EActAction Action) const
{
 if (Action == EActAction::Dodge) return CombatConfig->DodgeCost;
 if (Action == EActAction::Heavy) return CombatConfig->HeavyAttack.StaminaCost;
 if (Action == EActAction::Light) return CombatConfig->LightAttacks.IsValidIndex(ComboIndex) ? CombatConfig->LightAttacks[ComboIndex].StaminaCost : 18.f;
 return 0.f;
}
bool AActCharacter::CanStartAction(EActAction Action) const
{
 return CombatConfig && IsAlive() && !IsBusy() && !GetWorld()->IsPaused() && !GetCharacterMovement()->IsFalling()
 && Attributes->GetStamina() >= ActionCost(Action)
 && (Action != EActAction::Heal || (HealingCharges > 0 && HealthFraction() < 1.f));
}
bool AActCharacter::RequestAbility(FGameplayTag Tag, bool bBuffer)
{
 if (!IsAlive() || GetWorld()->IsPaused()) return false;
 if (IsBusy())
 {
  if (bBuffer && Faction == EActFaction::Player) { BufferedInput = Tag; BufferTime = GetWorld()->GetTimeSeconds(); }
  return false;
 }
 return Abilities->TryActivateAbilitiesByTag(FGameplayTagContainer(Tag));
}
bool AActCharacter::BeginAction(EActAction Action, UActCombatAbility* Ability)
{
 if (!CanStartAction(Action)) return false;
 if (Action != EActAction::Light || GetWorld()->GetTimeSeconds() > ComboExpireTime) ComboIndex = 0;
 ActiveAbility = Ability;
 CurrentAction = Action;
 ActionStart = GetWorld()->GetTimeSeconds();
 bSprintHeld = false;
 bHealApplied = false;
 HitActors.Reset();
 bComboWindow = bDamageWindow = false;
 Abilities->AddLooseGameplayTag(ActTags::Busy);
 ApplyResource(UActStaminaEffect::StaticClass(), -ActionCost(Action));
 LastStaminaUse = ActionStart;
 UAnimMontage* Montage = nullptr;
 if (Action == EActAction::Light || Action == EActAction::Heavy)
 {
  const FActAttack Attack = GetAttack();
  Montage = Attack.Montage;
  ActionDuration = Attack.Duration;
  if (bBossPhaseTwo) ActionDuration *= .8f;
  if (LockTarget) { FRotator Face = (LockTarget->GetActorLocation() - GetActorLocation()).Rotation(); Face.Pitch=0; Face.Roll=0; SetActorRotation(Face); }
  UAISense_Hearing::ReportNoiseEvent(GetWorld(), GetActorLocation(), 1.f, this, 1400.f, TEXT("Attack"));
  if (CombatConfig->AttackSound) UGameplayStatics::PlaySoundAtLocation(this, CombatConfig->AttackSound, GetActorLocation(), .35f);
 }
 else if (Action == EActAction::Dodge)
 {
  Montage = CombatConfig->DodgeMontage;
  ActionDuration = CombatConfig->DodgeDuration;
  DodgeDirection = DesiredMove.IsNearlyZero() ? GetActorForwardVector() : DesiredMove.GetSafeNormal2D();
 }
 else { ActionDuration = CombatConfig->HealDuration; Montage = CombatConfig->HealMontage; }
 GetCharacterMovement()->StopMovementImmediately();
 bActionHasMontage = Montage != nullptr;
 if (Montage && GetMesh()->GetAnimInstance())
 {
  const float Result = GetMesh()->GetAnimInstance()->Montage_Play(Montage, Montage->GetPlayLength() / FMath::Max(.1f, ActionDuration));
  bActionHasMontage = Result > 0.f;
 }
 OnChanged.Broadcast();
 return true;
}
void AActCharacter::EndAction(UActCombatAbility* Ability, bool bCancelled)
{
 if (ActiveAbility != Ability) return;
 const bool bLight = CurrentAction == EActAction::Light;
 ActiveAbility = nullptr;
 CurrentAction = EActAction::None;
 bDamageWindow = bComboWindow = false;
 HitActors.Reset();
 Abilities->SetLooseGameplayTagCount(ActTags::Busy, 0);
 Abilities->SetLooseGameplayTagCount(ActTags::Invulnerable, 0);
 GetCharacterMovement()->StopMovementImmediately();
 if (GetMesh()->GetAnimInstance()) GetMesh()->GetAnimInstance()->Montage_Stop(.12f);
 if (bCancelled) { BufferedInput = FGameplayTag(); ComboIndex = 0; }
 else if (bLight) { ComboIndex = (ComboIndex + 1) % FMath::Max(1, CombatConfig->LightAttacks.Num()); ComboExpireTime = GetWorld()->GetTimeSeconds() + .35f; }
 OnActionEnded.Broadcast(bCancelled);
 OnChanged.Broadcast();
}
void AActCharacter::SetCombatWindow(uint8 Window, bool bEnabled)
{
 if (!ActiveAbility) return;
 if (Window == 0)
 {
  if (bEnabled && !bDamageWindow) GetWeaponPoints(PreviousBase, PreviousTip);
  bDamageWindow = bEnabled;
 }
 if (Window == 1) Abilities->SetLooseGameplayTagCount(ActTags::Invulnerable, bEnabled ? 1 : 0);
 if (Window == 2) bComboWindow = bEnabled;
}
void AActCharacter::Tick(float Dt)
{
 Super::Tick(Dt);
 if (!bInitialized || !IsAlive()) return;
 const float Now = GetWorld()->GetTimeSeconds();
 CameraBoom->SocketOffset.Z = 70.f + (Now < CameraImpactUntil ? FMath::Sin(Now*90.f) * 5.f * (CameraImpactUntil-Now)/.16f : 0.f);
 UpdateLock(Dt);
 if (Faction != EActFaction::Player)
 {
  auto* PC=UGameplayStatics::GetPlayerController(this,0);
  APawn* Hero=PC ? PC->GetPawn() : nullptr;
  const bool bNearby=Hero && FVector::DistSquared(Hero->GetActorLocation(),GetActorLocation())<FMath::Square(1400.f);
  LifeBar->SetVisibility(bNearby && PC->LineOfSightTo(this));
 }
 if (ActiveAbility)
 {
  const float T = Now - ActionStart;
  if (!bActionHasMontage)
  {
   if (CurrentAction == EActAction::Light || CurrentAction == EActAction::Heavy)
   { const auto Attack = GetAttack(); SetCombatWindow(0, T >= Attack.ActiveStart && T <= Attack.ActiveEnd); bComboWindow = T >= Attack.ComboStart; }
   if (CurrentAction == EActAction::Dodge) SetCombatWindow(1, T >= CombatConfig->InvulnerableStart && T <= CombatConfig->InvulnerableEnd);
  }
  if (bDamageWindow) TraceWeapon();
  if (CurrentAction == EActAction::Dodge && T < ActionDuration * .75f)
   GetCharacterMovement()->Velocity = DodgeDirection * CombatConfig->DodgeSpeed;
  if (CurrentAction == EActAction::Heal && !bHealApplied && T >= ActionDuration * .8f)
  { bHealApplied = true; --HealingCharges; ApplyResource(UActHealthEffect::StaticClass(), CombatConfig->HealAmount); OnChanged.Broadcast(); }
  const bool bBufferedCombo = CurrentAction == EActAction::Light && bComboWindow && BufferedInput == ActTags::Light && Now - BufferTime <= CombatConfig->InputBufferSeconds;
  if (T >= ActionDuration || bBufferedCombo) ActiveAbility->Finish();
 }
 if (!ActiveAbility && BufferedInput.IsValid())
 {
  const FGameplayTag Input = BufferedInput;
  BufferedInput = FGameplayTag();
  if (Now - BufferTime <= CombatConfig->InputBufferSeconds) RequestAbility(Input, false);
 }
 const bool bSprint = bSprintHeld && !IsBusy() && Attributes->GetStamina() > 0.f && GetVelocity().SizeSquared2D() > 100.f;
 const float WalkSpeed = Profile ? Profile->MoveSpeed : (Faction==EActFaction::Player ? 420.f : 340.f);
 GetCharacterMovement()->MaxWalkSpeed = CurrentAction == EActAction::Dodge ? CombatConfig->DodgeSpeed : (IsBusy() ? 0.f : (bSprint ? WalkSpeed*1.57f : WalkSpeed));
 ResourceAccumulator += Dt;
 if (ResourceAccumulator >= .1f)
 {
  if (bSprint) { ApplyResource(UActStaminaEffect::StaticClass(), -14.f * ResourceAccumulator); LastStaminaUse = Now; UAISense_Hearing::ReportNoiseEvent(GetWorld(), GetActorLocation(), .6f, this, 650.f); }
  else if (!IsBusy() && Now - LastStaminaUse > CombatConfig->RegenDelay && Attributes->GetStamina() < Attributes->GetMaxStamina()) ApplyResource(UActStaminaEffect::StaticClass(), CombatConfig->StaminaRegen * ResourceAccumulator);
  if (Attributes->GetStamina() <= 0.f) bSprintHeld = false;
  ResourceAccumulator = 0.f;
 }
 if (bDebugCombat)
 {
  DrawDebugString(GetWorld(), FVector(0,0,165), FString::Printf(TEXT("%s  HP %.0f  ST %.0f  %s"), *AIState, Attributes->GetHealth(), Attributes->GetStamina(), *UEnum::GetValueAsString(CurrentAction)), this, FColor::Yellow, 0.f, true);
  if (Faction == EActFaction::Enemy) DrawDebugCircle(GetWorld(),GetActorLocation()-FVector(0,0,85),1200.f,48,FColor::Cyan,false,0.f,0,1.f,FVector(1,0,0),FVector(0,1,0),false);
 }
}
void AActCharacter::GetWeaponPoints(FVector& Base, FVector& Tip) const
{
 if (Profile && !Profile->WeaponTipSocket.IsNone())
 {
  Base=GetMesh()->GetSocketLocation(Profile->WeaponBaseSocket);
  Tip=GetMesh()->GetSocketLocation(Profile->WeaponTipSocket);
 }
 else
 {
  Base=Weapon->GetComponentLocation()-Weapon->GetUpVector()*40.f;
  Tip=Weapon->GetComponentLocation()+Weapon->GetUpVector()*45.f;
 }
}
void AActCharacter::TraceWeapon()
{
 FVector Base, Tip; GetWeaponPoints(Base,Tip);
 const FActAttack Attack = GetAttack();
 FCollisionObjectQueryParams Objects(ECC_Pawn);
 FCollisionQueryParams Params(SCENE_QUERY_STAT(ActWeapon), false, this);
 auto Sweep = [&](FVector Start, FVector End, float Radius)
 {
  TArray<FHitResult> Hits;
  GetWorld()->SweepMultiByObjectType(Hits, Start, End, FQuat::Identity, Objects, FCollisionShape::MakeSphere(Radius), Params);
  if (bDebugCombat) DrawDebugLine(GetWorld(), Start, End, FColor::Orange, false, .12f, 0, 3);
  for (const auto& Hit : Hits)
  {
   auto* Target = Cast<AActCharacter>(Hit.GetActor());
   if (!IsHostileTo(Target) || !Target->IsAlive() || HitActors.Contains(Target)) continue;
   if (FVector::Dist2D(GetActorLocation(), Target->GetActorLocation()) > Attack.Reach + 55.f) continue;
   FHitResult Wall;
   if (GetWorld()->LineTraceSingleByChannel(Wall, GetActorLocation(), Target->GetActorLocation(), ECC_Visibility, Params) && Wall.GetActor() != Target) continue;
   HitActors.Add(Target);
   Target->ReceiveCombatHit(this, Attack.Damage * DamageScale * (bBossPhaseTwo ? 1.15f : 1.f), Hit.ImpactPoint);
  }
 };
 if (Profile && !Profile->WeaponTipSocket.IsNone())
 {
  // Cover the swept blade, including its middle, between animation samples.
  const int32 Steps=FMath::Clamp(FMath::CeilToInt(FMath::Max(FVector::Dist(PreviousTip,Tip),FVector::Dist(PreviousBase,Base))/20.f),1,12);
  for(int32 I=0;I<=Steps;++I)
   Sweep(FMath::Lerp(PreviousBase,Base,float(I)/Steps),FMath::Lerp(PreviousTip,Tip,float(I)/Steps),Profile->WeaponTraceRadius);
 }
 else
 {
  Sweep(PreviousTip, Tip, 28.f); Sweep(Base, Tip, 28.f);
  // Retained only for the mannequin's unarmed graybox attacks.
  Sweep(GetActorLocation()+GetActorForwardVector()*40.f,GetActorLocation()+GetActorForwardVector()*Attack.Reach,35.f);
 }
 PreviousTip = Tip; PreviousBase = Base;
}
void AActCharacter::ApplyResource(TSubclassOf<UGameplayEffect> Effect, float Amount)
{
 FGameplayEffectSpecHandle Spec = Abilities->MakeOutgoingSpec(Effect, 1.f, Abilities->MakeEffectContext());
 if (Spec.IsValid()) { Spec.Data->SetSetByCallerMagnitude(ActTags::Magnitude, Amount); Abilities->ApplyGameplayEffectSpecToSelf(*Spec.Data.Get()); }
}
void AActCharacter::ReceiveCombatHit(AActCharacter* Source, float Damage, FVector Impact)
{
 if (!IsAlive() || !Source || !Source->IsHostileTo(this) || Abilities->HasMatchingGameplayTag(ActTags::Invulnerable)) return;
 ApplyResource(UActHealthEffect::StaticClass(), -FMath::Max(0.f, Damage));
 CameraImpactUntil = GetWorld()->GetTimeSeconds() + .16f;
 Source->CameraImpactUntil = CameraImpactUntil;
 if (CombatConfig->HitEffect) UNiagaraFunctionLibrary::SpawnSystemAtLocation(this, CombatConfig->HitEffect, Impact);
 if (CombatConfig->HitSound) UGameplayStatics::PlaySoundAtLocation(this, CombatConfig->HitSound, Impact, .65f);
 if (auto* AI = Cast<AActAIController>(Controller)) AI->NotifyDamage(Source);
 if (IsAlive() && EnemyKind != EActEnemyKind::Boss)
 {
  Abilities->CancelAllAbilities();
  ClearInput();
  StaggerUntil = GetWorld()->GetTimeSeconds() + .3f;
  if (CombatConfig->HitMontage) PlayAnimMontage(CombatConfig->HitMontage);
 }
 if (Faction == EActFaction::Training) { ApplyResource(UActHealthEffect::StaticClass(), InitialHealth); }
}
void AActCharacter::HealthChanged(const FOnAttributeChangeData& Change)
{
 if (Change.NewValue <= 0 && !Abilities->HasMatchingGameplayTag(ActTags::Dead)) Die();
 if (Faction == EActFaction::Enemy && EnemyKind == EActEnemyKind::Boss && HealthFraction() <= .5f && !bBossPhaseTwo) { bBossPhaseTwo = true; OnChanged.Broadcast(); }
 OnChanged.Broadcast();
}
void AActCharacter::Die()
{
 Abilities->AddLooseGameplayTag(ActTags::Dead);
 ClearInput(); SetLock(nullptr);
 Abilities->CancelAllAbilities();
 GetCharacterMovement()->StopMovementImmediately();
 GetCharacterMovement()->DisableMovement();
 GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
 if (auto* AI = Cast<AActAIController>(Controller)) AI->StopCombat();
 if (CombatConfig->DeathMontage) PlayAnimMontage(CombatConfig->DeathMontage);
 LifeBar->SetVisibility(false);
 if (auto* Mode = GetWorld()->GetAuthGameMode<AActGameMode>()) Mode->CharacterDied(this);
}
void AActCharacter::ClearInput() { BufferedInput = FGameplayTag(); bSprintHeld = false; DesiredMove = FVector::ZeroVector; }
void AActCharacter::SetSprint(bool Enabled) { bSprintHeld = Enabled; }
void AActCharacter::RestoreAtCheckpoint()
{
 if (!IsAlive()) return;
 Abilities->CancelAllAbilities(); ClearInput();
 ApplyResource(UActHealthEffect::StaticClass(), InitialHealth);
 ApplyResource(UActStaminaEffect::StaticClass(), 100.f);
 HealingCharges = CombatConfig->HealCharges;
 OnChanged.Broadcast();
}
void AActCharacter::SetLock(AActCharacter* Target)
{
 LockTarget = Target; LostLockTime = 0.f;
 GetCharacterMovement()->bOrientRotationToMovement = !Target;
 OnChanged.Broadcast();
}
void AActCharacter::ToggleLock() { if (LockTarget) SetLock(nullptr); else SwitchLock(); }
void AActCharacter::SwitchLock()
{
 if (!Controller || !IsAlive()) return;
 AActCharacter* Best = nullptr; float BestScore = TNumericLimits<float>::Max();
 for (TActorIterator<AActCharacter> It(GetWorld()); It; ++It)
 {
  if (!IsHostileTo(*It) || !It->IsAlive() || *It == LockTarget) continue;
  const FVector Delta = It->GetActorLocation() - GetActorLocation();
  if (Delta.Size() > 1800.f || !Controller->LineOfSightTo(*It)) continue;
  const float Facing = FVector::DotProduct(Controller->GetControlRotation().Vector(), Delta.GetSafeNormal());
  if (Facing < .1f) continue;
  const float Score = Delta.Size() + (1.f-Facing)*800.f;
  if (Score < BestScore) { Best = *It; BestScore = Score; }
 }
 if (Best) SetLock(Best);
}
void AActCharacter::UpdateLock(float Dt)
{
 if (!LockTarget || !Controller) return;
 if (!IsValid(LockTarget) || !LockTarget->IsAlive() || FVector::DistSquared(GetActorLocation(), LockTarget->GetActorLocation()) > FMath::Square(2200.f)) { SetLock(nullptr); return; }
 LostLockTime = Controller->LineOfSightTo(LockTarget) ? 0.f : LostLockTime + Dt;
 if (LostLockTime > .8f) { SetLock(nullptr); return; }
 FRotator Facing = (LockTarget->GetActorLocation() - GetActorLocation()).Rotation(); Facing.Pitch = 0.f;
 if (!IsBusy()) SetActorRotation(FMath::RInterpTo(GetActorRotation(), Facing, Dt, 12.f));
 FRotator Aim = (LockTarget->GetActorLocation() - Camera->GetComponentLocation()).Rotation();
 Aim.Pitch = FMath::Clamp(Aim.Pitch, -30.f, 20.f);
 Controller->SetControlRotation(FMath::RInterpTo(Controller->GetControlRotation(), Aim, Dt, 7.f));
}
