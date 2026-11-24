#include "Voxel/VoxelCharacter.h"

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

AVoxelCharacter::AVoxelCharacter()
{
    PrimaryActorTick.bCanEverTick = true;
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

    USpringArmComponent* Boom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
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

FVector AVoxelCharacter::GetAimOrigin() const
{
    return GetActorLocation() + FVector(0, 0, AimHeight);
}

void AVoxelCharacter::MoveOnGrid(const FVector2D& Input, double ViewYaw)
{
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
    if (UAnimInstance* Anim = GetMesh()->GetAnimInstance(); Anim && SwingAnimation)
    {
        // The reused punch has forward root motion. Terrain edits leave movement to CharacterMovement.
        Anim->SetRootMotionMode(ERootMotionMode::IgnoreRootMotion);
        Anim->PlaySlotAnimationAsDynamicMontage(SwingAnimation, TEXT("DefaultSlot"), .06f, .12f,
            Action == EVoxelAction::DigDown ? 1.7f : 2.f);
    }
}

void AVoxelCharacter::ReturnToStart(const FTransform& Start)
{
    bWantsClimb = false;
    GetCharacterMovement()->StopMovementImmediately();
    GetCharacterMovement()->SetMovementMode(MOVE_Falling);
    TeleportTo(Start.GetLocation(), Start.Rotator());
    if (AController* OwnerController = GetController())
    {
        OwnerController->SetControlRotation(Start.Rotator());
    }
}
