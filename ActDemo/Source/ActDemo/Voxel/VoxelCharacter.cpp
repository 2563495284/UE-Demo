#include "Voxel/VoxelCharacter.h"
#include "Voxel/VoxelGameMode.h"
#include "Voxel/VoxelPlayerController.h"
#include "Abilities/ExpeditionAbility.h"
#include "AbilitySystemComponent.h"
#include "Island/ExpeditionAnimalComponent.h"

#include "Animation/AnimInstance.h"
#include "Animation/AnimSequence.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "UObject/ConstructorHelpers.h"
#include "Voxel/VoxelTerrain.h"

namespace
{
    constexpr double AimHeight = 49.0;
}

AVoxelCharacter::AVoxelCharacter(const FObjectInitializer& Initializer)
    : Super(Initializer.SetDefaultSubobjectClass<UExpeditionCharacterMovement>(ACharacter::CharacterMovementComponentName))
{
    PrimaryActorTick.bCanEverTick = true;
    AbilitySystem = CreateDefaultSubobject<UAbilitySystemComponent>(TEXT("AbilitySystem"));
    AbilitySystem->SetIsReplicated(true);
    AbilitySystem->SetReplicationMode(EGameplayEffectReplicationMode::Mixed);
    ExpeditionAnimal = CreateDefaultSubobject<UExpeditionAnimalComponent>(TEXT("ExpeditionAnimal"));
    GetCapsuleComponent()->InitCapsuleSize(40.f, 90.f);
    GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
    GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
    GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Visibility, ECR_Ignore);
    GetMesh()->SetRelativeLocation(FVector(0, 0, -90));
    GetMesh()->SetRelativeRotation(FRotator(0, -90, 0));
    GetMesh()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    // Movement, climbing and chest traces all use the authoritative capsule. Offscreen bodies
    // have no animation-driven gameplay and can resume their cosmetic pose when visible.
    GetMesh()->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::OnlyTickPoseWhenRendered;
    GetMesh()->bEnableUpdateRateOptimizations = true;
    static ConstructorHelpers::FObjectFinder<USkeletalMesh> Manny(TEXT("/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple"));
    static ConstructorHelpers::FClassFinder<UAnimInstance> Anim(TEXT("/Game/Characters/Mannequins/Anims/Unarmed/ABP_Unarmed"));
    static ConstructorHelpers::FObjectFinder<UAnimSequence> Swing(TEXT("/Game/Characters/Mannequins/Anims/Unarmed/Attack/MM_Attack_01"));
    GetMesh()->SetSkeletalMesh(Manny.Object);
    GetMesh()->SetAnimInstanceClass(Anim.Class);
    SwingAnimation = Swing.Object;

    bUseControllerRotationYaw = false;
    UCharacterMovementComponent* Movement = GetCharacterMovement();
    Movement->bOrientRotationToMovement = true;
    Movement->RotationRate = FRotator(0, 620, 0);
    Movement->MaxWalkSpeed = 370.f;
    Movement->MaxFlySpeed = 210.f;
    Movement->BrakingDecelerationFlying = 2200.f;
    Movement->BrakingDecelerationWalking = 1800.f;
    Movement->JumpZVelocity = 445.f;
    Movement->AirControl = .54f;
    Movement->MaxStepHeight = 45.f;
    Movement->GetNavAgentPropertiesRef().bCanCrouch = true;
    Movement->SetCrouchedHalfHeight(40.f);
    Movement->MaxWalkSpeedCrouched = 210.f;

    Boom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
    Boom->SetupAttachment(RootComponent);
    Boom->TargetArmLength = 370.f;
    Boom->TargetOffset.Z = AimHeight;
    Boom->SocketOffset = FVector(0, 66, 18);
    Boom->ProbeSize = 12.f;
    Boom->bUsePawnControlRotation = true;
    UCameraComponent* Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
    Camera->SetupAttachment(Boom, USpringArmComponent::SocketName);
    Camera->FieldOfView = 82.f;
}

UAbilitySystemComponent* AVoxelCharacter::GetAbilitySystemComponent() const { return AbilitySystem; }

void AVoxelCharacter::BeginPlay()
{
    Super::BeginPlay();
    AbilitySystem->InitAbilityActorInfo(this, this);
    AbilitySystem->RegisterGameplayTagEvent(ExpeditionAbilities::Stunned()).AddUObject(this, &ThisClass::MudStunChanged);
    EnsureExpeditionAbilities();
}

void AVoxelCharacter::PossessedBy(AController* NewController)
{
    Super::PossessedBy(NewController);
    AbilitySystem->InitAbilityActorInfo(this, this);
    EnsureExpeditionAbilities();
}

void AVoxelCharacter::OnRep_Controller()
{
    Super::OnRep_Controller();
    AbilitySystem->InitAbilityActorInfo(this, this);
}

void AVoxelCharacter::EnsureExpeditionAbilities()
{
    if (bGrantedExpeditionAbilities || !HasAuthority()) return;
    const auto* PC = Cast<AVoxelPlayerController>(Controller);
    if (!PC || !PC->UsesExpeditionAbilities()) return;
    ExpeditionAbilities::Grant(*AbilitySystem);
    bGrantedExpeditionAbilities = true;
}

bool AVoxelCharacter::IsMudStunned() const { return AbilitySystem && AbilitySystem->HasMatchingGameplayTag(ExpeditionAbilities::Stunned()); }
bool AVoxelCharacter::IsMudImmune() const { return AbilitySystem && AbilitySystem->HasMatchingGameplayTag(ExpeditionAbilities::Immune()); }

bool AVoxelCharacter::ApplyMudStun()
{
    if (!HasAuthority() || IsMudStunned() || IsMudImmune()) return false;
    return ExpeditionAbilities::ApplyTimedTag(*AbilitySystem, ExpeditionAbilities::Stunned(), 1.8f);
}

void AVoxelCharacter::MudStunChanged(FGameplayTag Tag, int32 Count)
{
    if (Count > 0)
    {
        if (HasAuthority()) AbilitySystem->CancelAllAbilities();
        GetCharacterMovement()->StopMovementImmediately();
        if (HasAuthority() || IsLocallyControlled())
        {
            Crouch();
            GetCharacterMovement()->Crouch(false);
        }
        if (auto* PC = Cast<AVoxelPlayerController>(Controller)) PC->OnMudStunChanged(true);
    }
    else
    {
        if (HasAuthority() && !bClearingMud)
            ExpeditionAbilities::ApplyTimedTag(*AbilitySystem, ExpeditionAbilities::Immune(), 4.f);
        if (HasAuthority() || IsLocallyControlled()) UnCrouch();
        if (auto* PC = Cast<AVoxelPlayerController>(Controller); PC && !bClearingMud) PC->OnMudStunChanged(false);
    }
    ForceNetUpdate();
}

bool AVoxelCharacter::CanJumpInternal_Implementation() const
{
    return !IsMudStunned() && Super::CanJumpInternal_Implementation();
}

FVector AVoxelCharacter::GetAimOrigin() const
{
    return GetActorLocation() + FVector(0, 0, IsMudCompact() ? 10.0 : AimHeight);
}

void AVoxelCharacter::OnJumped_Implementation()
{
    Super::OnJumped_Implementation();
    if (auto* Mode = GetWorld()->GetAuthGameMode<AVoxelGameMode>()) Mode->OnPlayerJumped(*this);
}

void AVoxelCharacter::MoveOnGrid(const FVector2D& Input, double ViewYaw)
{
    if (IsMudStunned()) return;
    const FRotationMatrix Basis(FRotator(0, ViewYaw, 0));
    AddMovementInput(IsClimbingRock() ? FVector::UpVector : Basis.GetUnitAxis(EAxis::X), Input.Y);
    AddMovementInput(Basis.GetUnitAxis(EAxis::Y), Input.X);
}

void AVoxelCharacter::SetWantsClimb(bool bEnabled)
{
    bWantsClimb = bEnabled;
    if (bEnabled && !IsClimbingRock())
    {
        // Hold a stable wall-facing direction while the stick controls vertical and lateral movement.
        ClimbDirection = FRotator(0, GetControlRotation().Yaw, 0).Vector();
    }
}

bool AVoxelCharacter::IsClimbingRock() const
{
    return GetCharacterMovement()->MovementMode == MOVE_Flying;
}

void AVoxelCharacter::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if (IsLocallyControlled())
    {
        const bool bLow = IsMudCompact();
        Boom->TargetArmLength = bLow ? 4.f : 370.f;
        Boom->TargetOffset.Z = bLow ? 10.f : AimHeight;
        const auto* PC = Cast<AVoxelPlayerController>(Controller);
        const float Shoulder = PC && PC->UsesExpeditionAbilities()
            ? FMath::GetMappedRangeValueClamped(FVector2D(-74., -55.), FVector2D(0., 66.), FRotator::NormalizeAxis(GetControlRotation().Pitch))
            : 66.f;
        Boom->SocketOffset = bLow ? FVector::ZeroVector : FVector(0, Shoulder, 18);
    }
    if (!HasAuthority())
    {
        return;
    }
    if (!Terrain.IsValid())
    {
        Terrain = AVoxelTerrain::Find(GetWorld());
    }

    UCharacterMovementComponent* Movement = GetCharacterMovement();
    FHitResult Wall;
    const FVector Center = GetActorLocation();
    const bool bNearWall = bWantsClimb && Terrain.IsValid()
        && Terrain->TraceRock(Center, Center + ClimbDirection * 105.f, this, Wall)
        && FMath::Abs(Wall.ImpactNormal.Z) < .45f;
    if (bNearWall && !IsClimbingRock())
    {
        Movement->StopMovementImmediately();
        Movement->SetMovementMode(MOVE_Flying);
    }
    else if (!bNearWall && IsClimbingRock())
    {
        Movement->SetMovementMode(MOVE_Falling);
    }
}

void AVoxelCharacter::PlayTerrainSwing(EVoxelAction Action)
{
    const auto* PC = Cast<AVoxelPlayerController>(Controller);
    if (Action == EVoxelAction::Punch && PC && PC->UsesExpeditionAbilities())
    {
        ExpeditionAnimal->PlayDig();
        return;
    }
    if (UAnimInstance* Anim = GetMesh()->GetAnimInstance(); Anim && SwingAnimation)
    {
        // The reused punch has forward root motion. Terrain edits leave movement to CharacterMovement.
        Anim->SetRootMotionMode(ERootMotionMode::IgnoreRootMotion);
        Anim->PlaySlotAnimationAsDynamicMontage(SwingAnimation, TEXT("DefaultSlot"), .06f, .12f,
            Action == EVoxelAction::DigDown ? 1.7f : 2.f);
    }
}

void AVoxelCharacter::MulticastExpeditionDig_Implementation()
{
    ExpeditionAnimal->PlayDig();
}

void AVoxelCharacter::ReturnToStart(const FTransform& Start)
{
    if (HasAuthority() && bGrantedExpeditionAbilities)
    {
        TGuardValue<bool> Clearing(bClearingMud, true);
        AbilitySystem->CancelAllAbilities();
        ExpeditionAbilities::ClearTimedTags(*AbilitySystem);
        UnCrouch();
    }
    bWantsClimb = false;
    GetCharacterMovement()->StopMovementImmediately();
    GetCharacterMovement()->SetMovementMode(MOVE_Falling);
    TeleportTo(Start.GetLocation(), Start.Rotator());
    if (AController* OwnerController = GetController())
    {
        OwnerController->SetControlRotation(Start.Rotator());
    }
}
