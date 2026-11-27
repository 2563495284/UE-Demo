#include "Voxel/VoxelCharacter.h"
#include "Voxel/VoxelGameMode.h"
#include "Voxel/VoxelPlayerController.h"
#include "Abilities/ExpeditionAbility.h"
#include "AbilitySystemComponent.h"
#include "PartyCharacterControl.h"
#include "PartyPunch.h"
#include "Net/UnrealNetwork.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerState.h"
#include "Island/ExpeditionAnimalComponent.h"
#include "Island/ExpeditionBodyMud.h"
#include "Island/IslandGameMode.h"

#include "Animation/AnimInstance.h"
#include "Camera/CameraComponent.h"
#include "PartyCharacterCamera.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "UObject/ConstructorHelpers.h"
#include "Voxel/VoxelTerrain.h"

namespace
{
    constexpr float ToolStrokeDuration = .32f;
}

AVoxelCharacter::AVoxelCharacter(const FObjectInitializer& Initializer)
    : Super(Initializer.SetDefaultSubobjectClass<UExpeditionCharacterMovement>(ACharacter::CharacterMovementComponentName))
{
    PrimaryActorTick.bCanEverTick = true;
    AbilitySystem = CreateDefaultSubobject<UAbilitySystemComponent>(TEXT("AbilitySystem"));
    AbilitySystem->SetIsReplicated(true);
    AbilitySystem->SetReplicationMode(EGameplayEffectReplicationMode::Mixed);
    BodyMud = CreateDefaultSubobject<UExpeditionBodyMudComponent>(TEXT("BodyMud"));
    ExpeditionAnimal = CreateDefaultSubobject<UExpeditionAnimalComponent>(TEXT("ExpeditionAnimal"));
    PartyCharacterControl::ConfigureMovement(*this);
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
    GetMesh()->SetSkeletalMesh(Manny.Object);
    GetMesh()->SetAnimInstanceClass(Anim.Class);

    UCharacterMovementComponent* Movement = GetCharacterMovement();
    Movement->MaxFlySpeed = 210.f;
    Movement->BrakingDecelerationFlying = 2200.f;
    Movement->GetNavAgentPropertiesRef().bCanCrouch = true;
    Movement->SetCrouchedHalfHeight(40.f);
    Movement->MaxWalkSpeedCrouched = 210.f;

    Boom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
    Boom->SetupAttachment(RootComponent);
    UCameraComponent* Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
    Camera->SetupAttachment(Boom, USpringArmComponent::SocketName);

    EyeCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("EyeCamera"));
    EyeCamera->SetupAttachment(GetCapsuleComponent());
    PartyCharacterCamera::Configure(*Boom, *Camera, *EyeCamera);
}

UAbilitySystemComponent* AVoxelCharacter::GetAbilitySystemComponent() const { return AbilitySystem; }

void AVoxelCharacter::BeginPlay()
{
    Super::BeginPlay();
    AbilitySystem->InitAbilityActorInfo(this, this);
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

void AVoxelCharacter::SetMudAnchored(bool bAnchored)
{
    if (bMudAnchored == bAnchored) return;
    auto* Mode = GetWorld()->GetAuthGameMode<AIslandGameMode>();
    if (Mode && bAnchored)
    {
        // Settle work earned before this contact. Finishing a chest can change venue and clear the mud.
        Mode->RefreshMudChestLocks();
        if (!BodyMud->IsAnchored()) return;
    }
    bMudAnchored = bAnchored;
    if (bAnchored)
    {
        if (HasAuthority()) AbilitySystem->CancelAllAbilities();
        if (auto* PC = Cast<AVoxelPlayerController>(Controller)) PC->CancelMudInputs();
        if (HasAuthority() || IsLocallyControlled())
        {
            GetCharacterMovement()->StopMovementImmediately();
            GetCharacterMovement()->DisableMovement();
        }
        if (Mode) Mode->RefreshMudChestLocks();
    }
    else if ((HasAuthority() || IsLocallyControlled()) && GetCharacterMovement()->MovementMode == MOVE_None)
    {
        GetCharacterMovement()->SetMovementMode(MOVE_Falling);
    }
    if (HasAuthority()) ForceNetUpdate();
}

bool AVoxelCharacter::CanJumpInternal_Implementation() const
{
    return !IsMudAnchored() && Super::CanJumpInternal_Implementation();
}

FVector AVoxelCharacter::GetAimOrigin() const
{
    return GetActorLocation() + FVector(0, 0, PartyCharacterCamera::EyeHeight(IsMudCompact()));
}

bool AVoxelCharacter::IsInFirstPersonView() const
{
    const auto* PC = Cast<AVoxelPlayerController>(Controller);
    return PC && PC->WantsFirstPersonView() && PC->GetViewTarget() == this;
}

FRotator AVoxelCharacter::GetViewRotationForTarget(FVector Target, FVector Stand) const
{
    const FVector FromEye = Target - (Stand + GetAimOrigin() - GetActorLocation());
    if (IsInFirstPersonView()) return FromEye.Rotation();
    if (FromEye.Z < 0. && FromEye.Size2D() < 35.) return FRotator(-82., FromEye.Rotation().Yaw, 0.);
    // A retracted boom still points along the shoulder ray. Solve for its rotated socket offset.
    const FVector Pivot = Stand + Boom->GetComponentLocation() - GetActorLocation() + Boom->TargetOffset;
    const FVector ToTarget = Target - Pivot;
    FRotator View = ToTarget.Rotation();
    for (int32 Step = 0; Step < 6; ++Step)
        View = (ToTarget - View.RotateVector(Boom->SocketOffset)).Rotation();
    return View;
}

void AVoxelCharacter::CalcCamera(float DeltaTime, FMinimalViewInfo& OutResult)
{
    if (IsInFirstPersonView())
    {
        EyeCamera->SetWorldLocation(GetAimOrigin());
        EyeCamera->GetCameraView(DeltaTime, OutResult);
        return;
    }
    Super::CalcCamera(DeltaTime, OutResult);
}

void AVoxelCharacter::OnJumped_Implementation()
{
    Super::OnJumped_Implementation();
    if (auto* Mode = GetWorld()->GetAuthGameMode<AVoxelGameMode>()) Mode->OnPlayerJumped(*this);
}

void AVoxelCharacter::MoveOnGrid(const FVector2D& Input, double ViewYaw)
{
    if (IsMudAnchored()) return;
    PartyCharacterControl::Move(*this, Input, ViewYaw, IsClimbingRock());
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
    if (ToolStrokeRemaining > 0.f)
    {
        ToolStrokeRemaining = FMath::Max(0.f, ToolStrokeRemaining - DeltaSeconds);
        const float Arc = FMath::Sin(PI * ToolStrokeRemaining / ToolStrokeDuration);
        const FQuat Lean(FVector::RightVector, FMath::DegreesToRadians(ToolStrokeDegrees * Arc));
        GetMesh()->SetRelativeRotation(Lean * GetBaseRotationOffset());
    }
    if (IsLocallyControlled())
    {
        const auto* PC = Cast<AVoxelPlayerController>(Controller);
        PartyCharacterCamera::UpdateBoom(*Boom, GetControlRotation().Pitch, PC && PC->UsesExpeditionAbilities(), IsMudCompact());
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
        // The accepted strike is played once by MulticastExpeditionDig, including on the host.
        return;
    }
    // Sandbox and classic crews use the locomotion mesh. Pitch the body toward the
    // tool contact while leaving its walking animation and movement capsule in control.
    ToolStrokeDegrees = Action == EVoxelAction::Fill ? -9.f : Action == EVoxelAction::DigDown ? 25.f : 16.f;
    ToolStrokeRemaining = ToolStrokeDuration;
}

void AVoxelCharacter::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME_CONDITION(AVoxelCharacter,PunchHeldAt,COND_SkipOwner);
}

void AVoxelCharacter::SetPunchChargePresentation(bool bHeld)
{
    if (HasAuthority())
    {
        const bool WasHeld=PunchHeldAt>=0.;
        if (WasHeld==bHeld) return;
        const auto* Time=GetWorld()->GetGameState();
        PunchHeldAt=bHeld ? (Time ? Time->GetServerWorldTimeSeconds() : GetWorld()->GetTimeSeconds()) : -1.;
        ForceNetUpdate();
    }
    if (GetNetMode()!=NM_DedicatedServer) ExpeditionAnimal->SetPunchCharge(bHeld);
}

void AVoxelCharacter::OnRep_PunchHeldAt()
{
    const auto* Time=GetWorld()->GetGameState();
    const float Elapsed=PunchHeldAt>=0. && Time ? FMath::Max(0.,Time->GetServerWorldTimeSeconds()-PunchHeldAt) : 0.;
    ExpeditionAnimal->SetPunchCharge(PunchHeldAt>=0.,Elapsed);
}

void AVoxelCharacter::MulticastExpeditionDig_Implementation(uint8 Power)
{
    if (GetNetMode()!=NM_DedicatedServer) ExpeditionAnimal->PlayPunch(PartyPunch::Alpha(Power));
}

void AVoxelCharacter::MulticastExpeditionThrow_Implementation()
{
    if (GetNetMode() == NM_DedicatedServer) return;
    ExpeditionAnimal->PlayThrow();
#if !UE_BUILD_SHIPPING
    if (FParse::Param(FCommandLine::Get(), TEXT("ExpeditionMudQA")))
        UE_LOG(LogTemp, Display, TEXT("MUD_QA_THROW_POSE player=%d local=%d"), GetPlayerState() ? GetPlayerState()->GetPlayerId() : INDEX_NONE, IsLocallyControlled());
#endif
}

void AVoxelCharacter::ReturnToStart(const FTransform& Start)
{
    SetPunchChargePresentation(false);
    if (HasAuthority() && bGrantedExpeditionAbilities)
    {
        BodyMud->Clear();
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
