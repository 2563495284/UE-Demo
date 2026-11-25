#include "PartyVoxelArena.h"

#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/InputComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/LocalPlayer.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/SpringArmComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Net/UnrealNetwork.h"
#include "UObject/ConstructorHelpers.h"

APartyVoxelPawn::APartyVoxelPawn()
{
    PrimaryActorTick.bCanEverTick=true;
    bReplicates=true;
    bAlwaysRelevant=true;
    bUseControllerRotationYaw=true;
    BaseEyeHeight=58.;
    GetCapsuleComponent()->InitCapsuleSize(33.,88.);
    GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Visibility,ECR_Ignore);
    GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Pawn,ECR_Ignore);
    GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Camera,ECR_Ignore);
    GetCharacterMovement()->JumpZVelocity=565.;
    GetCharacterMovement()->AirControl=.55f;
    GetCharacterMovement()->MaxWalkSpeed=570.;
    GetCharacterMovement()->MaxStepHeight=36.;
    CameraBoom=CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
    CameraBoom->SetupAttachment(GetCapsuleComponent());
    CameraBoom->TargetArmLength=330.f;
    CameraBoom->TargetOffset.Z=BaseEyeHeight;
    CameraBoom->SocketOffset=FVector(0,55,17);
    CameraBoom->ProbeSize=12.f;
    CameraBoom->bUsePawnControlRotation=true;
    ShoulderCamera=CreateDefaultSubobject<UCameraComponent>(TEXT("ShoulderCamera"));
    ShoulderCamera->SetupAttachment(CameraBoom,USpringArmComponent::SocketName);
    ShoulderCamera->FieldOfView=85.f;
    Eye=CreateDefaultSubobject<UCameraComponent>(TEXT("Eyes"));
    Eye->SetupAttachment(GetCapsuleComponent());
    Eye->SetAutoActivate(false);
    Eye->SetRelativeLocation(FVector(0,0,BaseEyeHeight));
    Eye->FieldOfView=85.f;
    Eye->bUsePawnControlRotation=true;
    Eye->bEnableFirstPersonScale=true;
    Eye->FirstPersonScale=.5f;
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Orb(TEXT("/Engine/BasicShapes/Sphere"));
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> Paint(TEXT("/Engine/BasicShapes/BasicShapeMaterial"));
    BaseMaterial=Paint.Object;
    auto Part=[&](const TCHAR* Name,FVector Center,FVector Scale)
    {
        auto* M=CreateDefaultSubobject<UStaticMeshComponent>(FName(Name));
        M->SetupAttachment(GetCapsuleComponent());
        M->SetStaticMesh(Orb.Object);
        M->SetRelativeLocation(Center);
        M->SetRelativeScale3D(Scale);
        M->SetCanEverAffectNavigation(false);
        M->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        SuitParts.Add(M);
        return M;
    };
    Body=Part(TEXT("Suit"),FVector(0,0,-12),FVector(.75,.70,1.02));
    Part(TEXT("Helmet"),FVector(0,0,48),FVector(.79,.78,.70));
    Part(TEXT("Muzzle"),FVector(31,0,42),FVector(.32,.51,.22));
    Part(TEXT("EarLeft"),FVector(-3,-23,75),FVector(.24,.24,.34));
    Part(TEXT("EarRight"),FVector(-3,23,75),FVector(.24,.24,.34));
    for (int32 I=0;I<2;++I)
    {
        const float Side=I ? 1.f : -1.f;
        Gloves.Add(Part(I ? TEXT("RightGlove") : TEXT("LeftGlove"),{9.,Side*32.,-17.},{.26,.22,.32}));
        Boots.Add(Part(I ? TEXT("RightBoot") : TEXT("LeftBoot"),{7.,Side*18.,-73.},{.37,.29,.27}));
        auto* Paw=CreateDefaultSubobject<UStaticMeshComponent>(I ? TEXT("FirstPersonRightPaw") : TEXT("FirstPersonLeftPaw"));
        Paw->SetupAttachment(Eye);
        Paw->SetStaticMesh(Orb.Object);
        Paw->SetRelativeLocation({38.,Side*21.,-18.});
        Paw->SetRelativeScale3D({.23,.13,.16});
        Paw->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Paw->SetCanEverAffectNavigation(false);
        Paw->SetOnlyOwnerSee(true);
        Paw->SetFirstPersonPrimitiveType(EFirstPersonPrimitiveType::FirstPerson);
        Paw->SetCastShadow(false);
        Paw->SetVisibility(false);
        EyePaws.Add(Paw);
    }
}

void APartyVoxelPawn::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(APartyVoxelPawn,Arena);
    DOREPLIFETIME(APartyVoxelPawn,Seat);
    DOREPLIFETIME_CONDITION(APartyVoxelPawn,bObservedFirstPerson,COND_SkipOwner);
}

void APartyVoxelPawn::SetCrewTint(FLinearColor Color)
{
    // The hide-and-seek roles remain visible even under the island's anonymous identity effect.
    if (Arena && Arena->IsChaseGame() && Arena->GetRunnerTeam() != INDEX_NONE)
        Color = FLinearColor(Arena->IsRunner(Seat) ? FColor(247,174,70) : FColor(67,180,203));
    if (SuitPaint && SuitColor == Color) return;
    if (!SuitPaint)
    {
        SuitPaint = UMaterialInstanceDynamic::Create(BaseMaterial,this);
        Body->SetMaterial(0,SuitPaint);
        for (const auto& Glove : Gloves) Glove->SetMaterial(0,SuitPaint);
        for (const auto& Paw : EyePaws) Paw->SetMaterial(0,SuitPaint);
    }
    SuitColor = Color;
    SuitPaint->SetVectorParameterValue(TEXT("Color"),Color);
}

void APartyVoxelPawn::SetLocalFirstPersonView(bool bFirstPerson)
{
    if (bLocalFirstPerson==bFirstPerson) return;
    bLocalFirstPerson=bFirstPerson;
    for (const auto& Paw : EyePaws) Paw->SetVisibility(bFirstPerson);
}

void APartyVoxelPawn::ServerSetChaseView_Implementation(bool bFirstPerson)
{
    if (!Arena || !Arena->IsChaseGame() || bObservedFirstPerson == bFirstPerson) return;
    bObservedFirstPerson = bFirstPerson;
    ForceNetUpdate();
}

FRotator APartyVoxelPawn::GetViewRotation() const
{
    // Simulated teammates have no controller here. Their pawn replicates aim pitch and facing.
    if (!Controller && Arena && Arena->IsChaseGame()) return GetBaseAimRotation();
    return Super::GetViewRotation();
}

void APartyVoxelPawn::CalcCamera(float DeltaSeconds,FMinimalViewInfo& OutView)
{
    const bool bSpectated=!IsLocallyControlled() && Arena && Arena->IsChaseGame();
    const bool bFirst=bSpectated ? bObservedFirstPerson : IsInFirstPersonView();
    (bFirst ? Eye : ShoulderCamera)->GetCameraView(DeltaSeconds,OutView);
    if (bSpectated && bFirst) OutView.Rotation = GetViewRotation();
    if (bSpectated)
        for (const auto& Paw : EyePaws) Paw->SetVisibility(bFirst);
    const bool bHideSuit=bFirst || FVector::DistSquared(OutView.Location,GetPawnViewLocation())<FMath::Square(52.);
    if (bHideSuit!=bSuitHiddenFromOwner)
    {
        bSuitHiddenFromOwner=bHideSuit;
        for (const auto& Part : SuitParts) Part->SetOwnerNoSee(bHideSuit);
    }
}

void APartyVoxelPawn::FollowSurvivingTeammate(APlayerController& Player)
{
    const auto Eligible = [this](const APartyVoxelPawn* Other)
    {
        return IsValid(Other) && Other != this && Other->Arena == Arena && Other->Seat != INDEX_NONE
            && Arena->IsRunner(Other->Seat) && !Arena->IsCaught(Other->Seat)
            && Arena->GetStandings().ContainsByPredicate([Other](const auto& Row){ return Row.Seat == Other->Seat; });
    };
    if (Eligible(Cast<APartyVoxelPawn>(Player.GetViewTarget()))) return;
    APartyVoxelPawn* Next = nullptr;
    for (TActorIterator<APartyVoxelPawn> It(GetWorld()); It; ++It)
        if (Eligible(*It) && (!Next || It->Seat < Next->Seat)) Next = *It;
    AActor* Target = Next ? Next : this;
    if (Player.GetViewTarget() != Target) Player.SetViewTarget(Target);
}

void APartyVoxelPawn::UpdatePaws()
{
    const double Now=GetWorld()->GetTimeSeconds();
    const float Stride=FMath::Sin(Now*11.)*FMath::Clamp(GetVelocity().Size2D()/570.,0.,1.);
    const float Stroke=FMath::Sin(PI*FMath::Clamp((Now-ToolStrokeAt)/.22,0.,1.));
    for (int32 I=0;I<2;++I)
    {
        const double Side=I ? 1. : -1.;
        const double Swing=Side*Stride;
        const double Reach=(I==int32(bPlacingStroke)) ? Stroke : 0.;
        Gloves[I]->SetRelativeLocation({9.+10.*Swing+20.*Reach,Side*32.,-17.+7.*Reach});
        Boots[I]->SetRelativeLocation({7.-10.*Swing,Side*18.,-73.+FMath::Max(0.,Swing)*6.});
        if (bLocalFirstPerson) EyePaws[I]->SetRelativeLocation({38.+Swing+Reach*11.,Side*(21.-Reach*6.),-18.+Swing+Reach*6.});
    }
}

void APartyVoxelPawn::SetupPlayerInputComponent(UInputComponent* Input)
{
    Super::SetupPlayerInputComponent(Input);
    bInputReady=Input!=nullptr;
    if (const auto* PC=Cast<APlayerController>(Controller))
    {
        bWaitDigRelease=PC->IsInputKeyDown(EKeys::LeftMouseButton);
        bWaitPlaceRelease=PC->IsInputKeyDown(EKeys::RightMouseButton);
        bWaitJumpRelease=PC->IsInputKeyDown(EKeys::SpaceBar);
    }
}

bool APartyVoxelPawn::HasLocalPartyInput() const
{
    return bInputReady && IsLocallyControlled() && Arena && Arena->HasGrid();
}

void APartyVoxelPawn::ReleaseLocalPartyInput()
{
    bInputReady=false;
    if (Arena && Arena->IsChaseGame())
        if (auto* PC=Cast<APlayerController>(Controller); PC && PC->IsLocalController() && PC->GetPawn()==this)
            PC->SetViewTarget(this);
    StopJumping();
    SurfaceAimPoint.Reset();
    bAimingAtVenue = false;
}

FVector APartyVoxelPawn::GetToolAimDirection() const
{
    const auto* PC = Cast<APlayerController>(Controller);
    if (!PC || !PC->IsLocalController() || PC->GetViewTarget() != this)
        return GetControlRotation().Vector();

    FVector Camera;
    FRotator View;
    PC->GetPlayerViewPoint(Camera, View);
    const FVector EyePosition = GetPawnViewLocation();
    const FVector Crosshair = View.Vector();
    const double Range = APartyVoxelSession::StructureReach + FVector::Distance(Camera, EyePosition);
    const FVector End = Camera + Crosshair * Range;
    FHitResult Hit;
    const FCollisionQueryParams Query(SCENE_QUERY_STAT(PartyCrosshair), false, this);
    const bool bHit = GetWorld()->LineTraceSingleByChannel(Hit, Camera, End, ECC_Visibility, Query);
    // The camera selects the point under the reticle. The tool still reaches it from the character.
    return ((bHit ? Hit.ImpactPoint : End) - EyePosition).GetSafeNormal(UE_SMALL_NUMBER, Crosshair);
}

bool APartyVoxelPawn::AimTerrain(FHitResult& Hit) const
{
    return Arena && Arena->TraceTerrain(GetPawnViewLocation(), GetToolAimDirection(), this, Hit);
}

void APartyVoxelPawn::UseTool(bool bPlace)
{
    if (HasLocalPartyInput() && Arena->GetPhase()==EPartyVoxelPhase::Playing)
    {
        if (Arena->IsChaseGame() && (Arena->IsCaught(Seat) || (!Arena->IsRunner(Seat) && Arena->GetHeadStartRemaining()>0.f))) return;
        ToolStrokeAt = GetWorld()->GetTimeSeconds();
        bPlacingStroke = bPlace;
        ServerTool(bPlace,GetToolAimDirection());
    }
}

void APartyVoxelPawn::ServerTool_Implementation(bool bPlace,FVector_NetQuantizeNormal Direction)
{
    if (!Arena || Direction.ContainsNaN() || Direction.IsNearlyZero()) return;
    const double Time=GetWorld()->GetTimeSeconds();
    double& NextUse = bPlace ? NextServerPlace : NextServerDig;
    if (Time<NextUse) return;
    NextUse=Time+.22;
    // The server resolves the actual surface and applies the brush.
    Arena->UseTerrainTool(*this,bPlace,Direction);
}

void APartyVoxelPawn::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if (GetNetMode()!=NM_DedicatedServer) UpdatePaws();
    const bool bChase=Arena && Arena->IsChaseGame();
    const bool bCaught=bChase && Arena->IsCaught(Seat);
    if (bChase)
    {
        auto* Capsule=GetCapsuleComponent();
        const auto Response=bCaught ? ECR_Ignore : ECR_Block;
        if (Capsule->GetCollisionResponseToChannel(ECC_Visibility)!=Response)
            Capsule->SetCollisionResponseToChannel(ECC_Visibility,Response);
        if (Arena->GetRunnerTeam()!=INDEX_NONE)
            GetCharacterMovement()->MaxWalkSpeed=Arena->IsRunner(Seat) ? APartyVoxelSession::ChaseRunnerSpeed : APartyVoxelSession::ChaseHunterSpeed;
    }
    auto* PC=Cast<APlayerController>(Controller);
    const bool bLocalInput=PC && HasLocalPartyInput();
    if (bChase || bLocalInput)
    {
        const double Pitch=FRotator::NormalizeAxis(GetViewRotation().Pitch);
        CameraBoom->SocketOffset.Y=55.*FMath::GetMappedRangeValueClamped(FVector2D(-75.,-45.),FVector2D(0.,1.),Pitch);
    }
    if (!bLocalInput) return;
    if (bChase && bSentFirstPerson!=bLocalFirstPerson)
    {
        bSentFirstPerson=bLocalFirstPerson;
        ServerSetChaseView(bLocalFirstPerson);
    }
    if (bCaught)
    {
        StopJumping();
        ConsumeMovementInputVector();
        if (GetCharacterMovement()->MovementMode!=MOVE_None) GetCharacterMovement()->DisableMovement();
        FollowSurvivingTeammate(*PC);
        return;
    }
    if (PC->bShowMouseCursor)
    {
        StopJumping();
        SurfaceAimPoint.Reset();
        bAimingAtVenue = false;
        bWaitDigRelease=PC->IsInputKeyDown(EKeys::LeftMouseButton);
        bWaitPlaceRelease=PC->IsInputKeyDown(EKeys::RightMouseButton);
        bWaitJumpRelease=PC->IsInputKeyDown(EKeys::SpaceBar);
        return;
    }
    const bool Playing=Arena->GetPhase()==EPartyVoxelPhase::Playing;
    const bool bWaitingHunter=bChase && Playing && !Arena->IsRunner(Seat) && Arena->GetHeadStartRemaining()>0.f;
    const bool CanWalk=(Playing && !bWaitingHunter) || (!bChase && Arena->GetPhase()==EPartyVoxelPhase::Results);
    float MouseX=0, MouseY=0;
    PC->GetInputMouseDelta(MouseX,MouseY);
    if (!PC->IsLookInputIgnored() && (CanWalk || Arena->GetPhase()==EPartyVoxelPhase::Countdown))
    {
        FRotator Look=PC->GetControlRotation();
        Look.Yaw+=FMath::Clamp(MouseX,-90.f,90.f)*.18f*LocalLookScale.X;
        Look.Pitch=FMath::Clamp(FRotator::NormalizeAxis(Look.Pitch)-FMath::Clamp(MouseY,-90.f,90.f)*.18f*LocalLookScale.Y,-84.,80.);
        PC->SetControlRotation(Look);
    }
    const bool DigDown=PC->IsInputKeyDown(EKeys::LeftMouseButton);
    const bool PlaceDown=PC->IsInputKeyDown(EKeys::RightMouseButton);
    const bool JumpDown=PC->IsInputKeyDown(EKeys::SpaceBar);
    bWaitDigRelease &= DigDown;
    bWaitPlaceRelease &= PlaceDown;
    bWaitJumpRelease &= JumpDown;
    if (CanWalk && !PC->IsMoveInputIgnored())
    {
        const FRotationMatrix Basis(FRotator(0,PC->GetControlRotation().Yaw,0));
        const float Forward=float(PC->IsInputKeyDown(EKeys::W))-float(PC->IsInputKeyDown(EKeys::S));
        const float Side=float(PC->IsInputKeyDown(EKeys::D))-float(PC->IsInputKeyDown(EKeys::A));
        AddMovementInput(Basis.GetUnitAxis(EAxis::X),Forward);
        AddMovementInput(Basis.GetUnitAxis(EAxis::Y),Side);
        if (JumpDown && !bWaitJumpRelease) Jump(); else StopJumping();
    }
    if (Playing && !bWaitingHunter)
    {
        const bool bDig=DigDown && !bWaitDigRelease, bPlace=PlaceDown && !bWaitPlaceRelease;
        const double Time=GetWorld()->GetTimeSeconds();
        if (bDig && Time >= NextLocalDig)
        {
            UseTool(false);
            NextLocalDig = Time + (bChase ? APartyVoxelSession::ChaseToolRecovery(false) : .28);
            if (bChase) NextLocalPlace = NextLocalDig;
        }
        if (bPlace && Time >= NextLocalPlace)
        {
            UseTool(true);
            NextLocalPlace = Time + (bChase ? APartyVoxelSession::ChaseToolRecovery(true) : .28);
            if (bChase) NextLocalDig = NextLocalPlace;
        }
    }
    if (bChase) return; // Maze walls and capturable runners use a shared server/client targeting query.
    FHitResult Hit;
    const FVector Aim = Playing ? GetToolAimDirection() : FVector::ZeroVector;
    const bool Target=Playing && Arena->TraceTerrain(GetPawnViewLocation(), Aim, this, Hit);
    if (Target) SurfaceAimPoint = Hit.ImpactPoint;
    else SurfaceAimPoint.Reset();
    bAimingAtVenue = Playing && !Target && Arena->TraceVenueStructure(GetPawnViewLocation(),Aim,this) != INDEX_NONE;
}
