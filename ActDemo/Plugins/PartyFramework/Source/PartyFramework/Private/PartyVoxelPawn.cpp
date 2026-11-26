#include "PartyVoxelArena.h"
#include "PartyCrewAppearance.h"
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
    auto Part=[&](const TCHAR* Name,FVector Center,FVector Scale, UMaterialInterface* Surface, UStaticMesh* Shape=nullptr)
    {
        auto* M=CreateDefaultSubobject<UStaticMeshComponent>(FName(Name));
        M->SetupAttachment(GetCapsuleComponent());
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
    CrewSculpture->SetStaticMesh(nullptr);
    CrewSculpture->SetVisibility(false);
    CrewBadge=Part(TEXT("CrewBadge"),{-36,0,-26},{.07,.43,.30},Canvas.Object);
    CrewBadge->SetVisibility(false);
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
        CrewBadge->SetMaterial(0,SuitPaint);
        for (const auto& Glove : Gloves) Glove->SetMaterial(0,SuitPaint);
        for (const auto& Paw : EyePaws) Paw->SetMaterial(0,SuitPaint);
    }
    SuitColor = Color;
    SuitPaint->SetVectorParameterValue(TEXT("Color"),Color);
}

void APartyVoxelPawn::UpdateCrewModel()
{
    const auto* Appearance=Cast<IPartyCrewAppearance>(GetPlayerState());
    bool bLongPaws=false;
    UStaticMesh* Mesh=Appearance ? Appearance->GetPartyCharacterMesh(bLongPaws) : nullptr;
    if (CrewSculpture->GetStaticMesh()==Mesh) return;

    CrewSculpture->SetStaticMesh(Mesh);
    for (const auto& Part : SuitParts)
    {
        if (Part==CrewSculpture || Part==CrewBadge) Part->SetVisibility(Mesh!=nullptr);
        else if (!Gloves.Contains(Part) && !Boots.Contains(Part)) Part->SetVisibility(Mesh==nullptr);
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
    if (GetNetMode()!=NM_DedicatedServer)
    {
        UpdateCrewModel();
        UpdatePaws();
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
