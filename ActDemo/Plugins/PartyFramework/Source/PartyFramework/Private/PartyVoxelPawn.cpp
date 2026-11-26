#include "PartyVoxelArena.h"
#include "PartyPunch.h"
#include "GameFramework/GameStateBase.h"
#include "PartyCrewAppearance.h"
#include "PartyCrewRig.h"
#include "Engine/SkeletalMesh.h"
#include "GameFramework/PlayerState.h"

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
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Orb(TEXT("/Game/TreasureIsland/Art/Crew/SM_CarvedRound"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Mitten(TEXT("/Game/TreasureIsland/Art/Crew/SM_Mitten"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Block(TEXT("/Game/TreasureIsland/Art/Neutral/SM_HewnBlock"));
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> Canvas(TEXT("/Game/TreasureIsland/Art/Crew/MI_Canvas"));
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> Brass(TEXT("/Game/TreasureIsland/Art/Crew/MI_Brass"));
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> Iron(TEXT("/Game/TreasureIsland/Art/Crew/MI_Iron"));
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> Teal(TEXT("/Game/TreasureIsland/Art/Crew/MI_LagoonTrim"));
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> Paint(TEXT("/Engine/BasicShapes/BasicShapeMaterial"));
    BaseMaterial=Paint.Object;
    UnpaintedFist=Mitten.Object;
    OutfitRoot=CreateDefaultSubobject<USceneComponent>(TEXT("OutfitRoot"));
    OutfitRoot->SetupAttachment(GetCapsuleComponent());
    auto Part=[&](const TCHAR* Name,FVector Center,FVector Scale, UMaterialInterface* Surface, UStaticMesh* Shape=nullptr)
    {
        auto* M=CreateDefaultSubobject<UStaticMeshComponent>(FName(Name));
        M->SetupAttachment(OutfitRoot);
        M->SetStaticMesh(Shape ? Shape : Orb.Object.Get());
        M->SetMaterial(0,Surface);
        M->SetReceivesDecals(false);
        M->SetRelativeLocation(Center);
        M->SetRelativeScale3D(Scale);
        M->SetCanEverAffectNavigation(false);
        M->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        SuitParts.Add(M);
        return M;
    };
    // A compact island diver: oilskin jacket, brass collar and panelled faceplate.
    Body=Part(TEXT("Suit"),{0,0,-19},{.74,.76,.82},Canvas.Object);
    Part(TEXT("Helmet"),{2,0,40},{.80,.86,.71},Canvas.Object);
    Part(TEXT("Collar"),{0,0,9},{.75,.82,.18},Brass.Object);
    Part(TEXT("VisorFrame"),{32,0,42},{.26,.68,.41},Brass.Object);
    Part(TEXT("Glass"),{39,0,42},{.17,.59,.33},Teal.Object);
    Part(TEXT("Belt"),{-1,0,-37},{.72,.76,.14},Iron.Object);
    Part(TEXT("AirTank"),{-31,0,-13},{.27,.48,.65},Teal.Object,Block.Object);
    Part(TEXT("TankBand"),{-33,0,-10},{.29,.52,.12},Brass.Object,Block.Object);
    for (int32 I=0;I<2;++I)
    {
        const float Side=I ? 1.f : -1.f;
        Part(I ? TEXT("LeftBolt") : TEXT("RightBolt"),{0.,Side*39.,41.},{.22,.11,.24},Brass.Object);
        Gloves.Add(Part(I ? TEXT("RightGlove") : TEXT("LeftGlove"),{9.,Side*32.,-17.},{.26,Side*.22,.32},Iron.Object,Mitten.Object));
        Boots.Add(Part(I ? TEXT("RightBoot") : TEXT("LeftBoot"),{7.,Side*18.,-73.},{.37,.29,.27},Iron.Object));
        auto* Paw=CreateDefaultSubobject<UStaticMeshComponent>(I ? TEXT("FirstPersonRightPaw") : TEXT("FirstPersonLeftPaw"));
        Paw->SetupAttachment(Eye);
        Paw->SetStaticMesh(Mitten.Object);
        Paw->SetMaterial(0,Iron.Object);
        Paw->SetReceivesDecals(false);
        Paw->SetRelativeLocation({38.,Side*21.,-18.});
        Paw->SetRelativeScale3D({.23,Side*.14,.16});
        Paw->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Paw->SetCanEverAffectNavigation(false);
        Paw->SetOnlyOwnerSee(true);
        Paw->SetFirstPersonPrimitiveType(EFirstPersonPrimitiveType::FirstPerson);
        Paw->SetCastShadow(false);
        Paw->SetVisibility(false);
        EyePaws.Add(Paw);
    }
    CrewSculpture=Part(TEXT("CrewSculpture"),{0,0,-88},FVector::OneVector,nullptr);
    CrewRig=CreateDefaultSubobject<UPartyCrewRig>(TEXT("CrewRig"));
    CrewRig->SetupAttachment(OutfitRoot);
    CrewRig->SetRelativeLocation({0.,0.,-88.});
    CrewRig->SetVisibility(false);
    CrewSculpture->SetStaticMesh(nullptr);
    CrewSculpture->SetVisibility(false);
    CrewBadge=Part(TEXT("CrewBadge"),{-36,0,-26},{.07,.43,.30},Canvas.Object);
    CrewBadge->SetVisibility(false);
}

void APartyVoxelPawn::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(APartyVoxelPawn,Arena);
    DOREPLIFETIME_CONDITION(APartyVoxelPawn,ServerPunchStart,COND_SkipOwner);
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
        CrewBadge->SetMaterial(0,SuitPaint);
        for (const auto& Glove : Gloves) Glove->SetMaterial(0,SuitPaint);
        for (const auto& Paw : EyePaws) if (Paw->GetStaticMesh()==UnpaintedFist) Paw->SetMaterial(0,SuitPaint);
    }
    SuitColor = Color;
    SuitPaint->SetVectorParameterValue(TEXT("Color"),Color);
}

void APartyVoxelPawn::UpdateCrewModel()
{
    const auto* Appearance=Cast<IPartyCrewAppearance>(GetPlayerState());
    bool bLongPaws=false;
    UStaticMesh* Mesh=Appearance ? Appearance->GetPartyCharacterMesh(bLongPaws) : nullptr;
    USkeletalMesh* Skin=Appearance ? Appearance->GetPartyCharacterRig() : nullptr;
    if (CrewSculpture->GetStaticMesh()==Mesh && CrewRig->GetSkinnedAsset()==Skin) return;
    CrewRig->SetSkinnedAssetAndUpdate(Skin);
    CrewRig->SetVisibility(Skin!=nullptr);
    CrewBadge->SetRelativeLocationAndRotation(Skin ? Appearance->GetCrewBadgeAnchor()+FVector(-2.2,0.,-87.65) : FVector(-36.,0.,-26.),
        Skin ? FRotator(-10.,0.,0.) : FRotator::ZeroRotator);
    CrewBadge->SetRelativeScale3D(Skin ? FVector(.024,.20,.12) : FVector(.07,.43,.30));
    UStaticMesh* Fist=Appearance ? Appearance->GetPartyFirstPersonFist() : nullptr;
    for (int32 I=0;I<EyePaws.Num();++I)
    {
        EyePaws[I]->SetStaticMesh(Fist ? Fist : UnpaintedFist.Get());
        EyePaws[I]->SetMaterial(0,Fist ? nullptr : SuitPaint.Get());
        EyePaws[I]->SetRelativeScale3D(Fist ? FVector(.20,I ? .20 : -.20,.20) : FVector(.22,I ? .14 : -.14,.17));
    }

    CrewSculpture->SetStaticMesh(Mesh);
    for (const auto& Part : SuitParts)
    {
        if (Part==CrewSculpture) Part->SetVisibility(Mesh && !Skin);
        else if (Part==CrewBadge) Part->SetVisibility(Mesh || Skin);
        else if (Gloves.Contains(Part) || Boots.Contains(Part)) Part->SetVisibility(!Skin);
        else Part->SetVisibility(!Mesh && !Skin);
    }
    for (const auto& Glove : Gloves)
    {
        FVector Scale=Glove->GetRelativeScale3D();
        Scale.Z=bLongPaws ? .46 : .32;
        Glove->SetRelativeScale3D(Scale);
    }
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
        CrewRig->SetOwnerNoSee(bHideSuit);
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

void APartyVoxelPawn::SetPunchCharge(bool bHeld,float Elapsed)
{
    CrewMotion.Charge(GetWorld()->GetTimeSeconds(),bHeld,Elapsed);
}

void APartyVoxelPawn::PlayPunch(float Strength)
{
    CrewMotion.Strike(GetWorld()->GetTimeSeconds(),Strength);
}

void APartyVoxelPawn::UpdatePaws(float DeltaSeconds)
{
    const double Now=GetWorld()->GetTimeSeconds();
    if (bPlacingStroke && LastRenderedThrow!=ToolStrokeAt)
    {
        LastRenderedThrow=ToolStrokeAt;
        CrewMotion.Throw(ToolStrokeAt);
    }
    const FPartyCrewPose Pose=CrewMotion.Sample(Now,DeltaSeconds,FMath::Clamp(float(GetVelocity().Size2D())/570.f,0.f,1.f),
        GetVelocity().Z,GetCharacterMovement()->IsFalling());
    // Spin around the soles, keeping the camera, collision and aim on their original axes.
    const FVector Sole(0,0,-88.);
    OutfitRoot->SetRelativeLocationAndRotation(Pose.BodyOffset+Sole-Pose.BodyRot.RotateVector(Sole),Pose.BodyRot);
    CrewRig->Apply(Pose);
    for (int32 I=0;I<2;++I)
    {
        Gloves[I]->SetRelativeLocationAndRotation(Sole+Pose.Hands[I],Pose.HandRot[I]);
        Boots[I]->SetRelativeLocation(Sole+Pose.Feet[I]);
        if (bLocalFirstPerson) EyePaws[I]->SetRelativeLocationAndRotation(Pose.EyeHands[I],Pose.EyeRot[I]);
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
    CancelPunchInput();
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
    const double Range = APartyVoxelSession::FillReach + FVector::Distance(Camera, EyePosition);
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

bool APartyVoxelPawn::CanPunch() const
{
    return Arena && Arena->GetPhase()==EPartyVoxelPhase::Playing && !Arena->IsCaught(Seat)
        && (!Arena->IsChaseGame() || Arena->IsRunner(Seat) || Arena->GetHeadStartRemaining()<=0.);
}

float APartyVoxelPawn::GetPunchChargeAlpha() const
{
    return LocalPunchStart < 0. ? 0.f : FMath::Clamp(float(FPlatformTime::Seconds()-LocalPunchStart)/PartyPunch::ChargeSeconds,0.f,1.f);
}

void APartyVoxelPawn::BeginPunchInput()
{
    if (!HasLocalPartyInput() || !CanPunch() || LocalPunchStart>=0.) return;
    LocalPunchStart=FPlatformTime::Seconds();
    SetPunchCharge(true);
    ServerBeginPunch();
}

void APartyVoxelPawn::ReleasePunchInput()
{
    if (LocalPunchStart<0.) return;
    const float Held=float(FPlatformTime::Seconds()-LocalPunchStart);
    LocalPunchStart=-1.;
    SetPunchCharge(false);
    ServerEndPunch(!CanPunch(),GetToolAimDirection(),Held);
}

void APartyVoxelPawn::CancelPunchInput()
{
    if (LocalPunchStart<0.) return;
    LocalPunchStart=-1.;
    bWaitDigRelease=true;
    SetPunchCharge(false);
    ServerEndPunch(true,FVector::ForwardVector,0.f);
}

void APartyVoxelPawn::OnRep_PunchStart()
{
    if (IsLocallyControlled()) return;
    const auto* GS=GetWorld()->GetGameState();
    SetPunchCharge(ServerPunchStart>=0.,GS && ServerPunchStart>=0. ?
        FMath::Max(0.,GS->GetServerWorldTimeSeconds()-ServerPunchStart) : 0.);
}

void APartyVoxelPawn::ServerBeginPunch_Implementation()
{
    if (!CanPunch() || ServerPunchStart>=0.) return;
    ServerPunchRealStart=FPlatformTime::Seconds();
    // Replicated poses use the server world clock shared with remote GameStates.
    ServerPunchStart=GetWorld()->GetTimeSeconds();
    OnRep_PunchStart();
    ForceNetUpdate();
}

void APartyVoxelPawn::ServerEndPunch_Implementation(bool bCancel,FVector_NetQuantizeNormal Direction,float ReportedHoldSeconds)
{
    if (ServerPunchStart<0.) return;
    const double Now=GetWorld()->GetTimeSeconds();
    const double Held=FPlatformTime::Seconds()-ServerPunchRealStart;
    ServerPunchStart=-1.;
    OnRep_PunchStart();
    ForceNetUpdate();
    if (bCancel || !CanPunch() || Direction.ContainsNaN() || Direction.IsNearlyZero() || Now<NextServerDig) return;
    const auto* PS=GetPlayerState();
    const float Ping=PS && !IsLocallyControlled() ? PS->GetPingInMilliseconds() : -1.f;
    const uint8 Power=PartyPunch::ResolvePower(Held,ReportedHoldSeconds,Ping);
#if !UE_BUILD_SHIPPING
    if (FParse::Param(FCommandLine::Get(),TEXT("IslandQA")))
        UE_LOG(LogTemp,Display,TEXT("PARTY_PUNCH_RELEASE seat=%d authority_seconds=%.3f client_seconds=%.3f ping_ms=%.1f power=%u"),
            Seat,Held,ReportedHoldSeconds,Ping,Power);
#endif
    NextServerDig=Now+.5;
    Arena->UseTerrainTool(*this,false,Direction,Power);
    MulticastPunch(Power);
}

void APartyVoxelPawn::MulticastPunch_Implementation(uint8 Power)
{
    PlayPunch(PartyPunch::Alpha(Power));
}

void APartyVoxelPawn::UseTool(bool bPlace)
{
    if (!HasLocalPartyInput() || !CanPunch()) return;
    if (!bPlace) { BeginPunchInput(); ReleasePunchInput(); return; }
    ToolStrokeAt=GetWorld()->GetTimeSeconds();
    bPlacingStroke=true;
    ServerTool(true,GetToolAimDirection());
}

void APartyVoxelPawn::ServerTool_Implementation(bool bPlace,FVector_NetQuantizeNormal Direction)
{
    if (!bPlace || !CanPunch() || Direction.ContainsNaN() || Direction.IsNearlyZero()) return;
    const double Time=GetWorld()->GetTimeSeconds();
    if (Time<NextServerPlace) return;
    NextServerPlace=Time+.22;
    Arena->UseTerrainTool(*this,true,Direction);
}

void APartyVoxelPawn::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if (GetNetMode()!=NM_DedicatedServer)
    {
        UpdateCrewModel();
        UpdatePaws(DeltaSeconds);
    }
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
    if (HasAuthority() && ServerPunchStart>=0. && !CanPunch()) { ServerPunchStart=-1.; OnRep_PunchStart(); ForceNetUpdate(); }
    if (!bLocalInput) return;
    if (!CanPunch() || PC->bShowMouseCursor) CancelPunchInput();
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
        const bool bDig=DigDown && !bWaitDigRelease;
        const bool bPlace=!bWaitPlaceRelease && (PlaceDown || PC->WasInputKeyJustPressed(EKeys::RightMouseButton));
        const double Time=GetWorld()->GetTimeSeconds();
        if (!bWaitDigRelease && (bDig || PC->WasInputKeyJustPressed(EKeys::LeftMouseButton))) BeginPunchInput();
        if (!bDig && LocalPunchStart>=0.) ReleasePunchInput();
        if (bPlace && Time >= NextLocalPlace)
        {
            UseTool(true);
            NextLocalPlace = Time + (bChase ? APartyVoxelSession::ChaseToolRecovery(true) : .28);
        }
    }
    if (bChase) return; // Maze walls and capturable runners use a shared server/client targeting query.
    FHitResult Hit;
    const FVector Aim = Playing ? GetToolAimDirection() : FVector::ZeroVector;
    const bool Target=Playing && Arena->TraceTerrain(GetPawnViewLocation(), Aim, this, Hit);
    if (Target && (LocalPunchStart<0. || Hit.Distance<=PartyPunch::Reach)) SurfaceAimPoint = Hit.ImpactPoint;
    else SurfaceAimPoint.Reset();
    bAimingAtVenue = Playing && !Target && Arena->TraceVenueStructure(GetPawnViewLocation(),Aim,this) != INDEX_NONE;
}
