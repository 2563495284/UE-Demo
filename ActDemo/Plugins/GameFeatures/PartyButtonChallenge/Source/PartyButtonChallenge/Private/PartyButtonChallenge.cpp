#include "PartyButtonChallenge.h"
#include "ExpeditionText.h"

#include "Camera/CameraComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/Engine.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/HUD.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "Modules/ModuleManager.h"
#include "Net/UnrealNetwork.h"
#include "UObject/ConstructorHelpers.h"

IMPLEMENT_MODULE(FDefaultModuleImpl, PartyButtonChallenge)

APartyButtonPawn::APartyButtonPawn()
{
    bReplicates = true;
    bAlwaysRelevant = true;
    Podium = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Podium"));
    GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    GetCharacterMovement()->DefaultLandMovementMode = MOVE_None;
    GetCharacterMovement()->SetMovementMode(MOVE_None);
    Podium->SetupAttachment(GetRootComponent());
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube"));
    Podium->SetStaticMesh(Cube.Object);
    Podium->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Podium->SetRelativeScale3D(FVector(.75, .75, 1.05));
    Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
    Camera->SetupAttachment(GetRootComponent());
    Camera->SetRelativeLocation(FVector(-270, 0, 160));
    Camera->SetRelativeRotation(FRotator(-19, 0, 0));
    Camera->bUsePawnControlRotation = false;
}

void APartyButtonPawn::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(APartyButtonPawn, Session);
    DOREPLIFETIME_CONDITION(APartyButtonPawn, bConfirmed, COND_OwnerOnly);
}

void APartyButtonPawn::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
    Super::SetupPlayerInputComponent(PlayerInputComponent);
    ReleaseLocalPartyInput();
    APlayerController* Player = Cast<APlayerController>(GetController());
    UEnhancedInputComponent* Input = Cast<UEnhancedInputComponent>(PlayerInputComponent);
    UEnhancedInputLocalPlayerSubsystem* Subsystem = Player
        ? ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(Player->GetLocalPlayer()) : nullptr;
    if (!Input || !Subsystem)
    {
        return;
    }
    ConfirmAction = NewObject<UInputAction>(this);
    ConfirmAction->ValueType = EInputActionValueType::Boolean;
    Mapping = NewObject<UInputMappingContext>(this);
    Mapping->MapKey(ConfirmAction, EKeys::SpaceBar);
    ConfirmBinding = Input->BindAction(ConfirmAction, ETriggerEvent::Started, this, &APartyButtonPawn::Confirm).GetHandle();
    Subsystem->AddMappingContext(Mapping, 10);
    BoundInput = Input;
    BoundSubsystem = Subsystem;
    UE_LOG(LogTemp, Display, TEXT("PARTY_BUTTON_INPUT attached=1"));
}

bool APartyButtonPawn::HasLocalPartyInput() const
{
    return BoundSubsystem.IsValid() && Session != nullptr;
}

void APartyButtonPawn::ReleaseLocalPartyInput()
{
    if (UEnhancedInputLocalPlayerSubsystem* Subsystem = BoundSubsystem.Get())
    {
        Subsystem->RemoveMappingContext(Mapping);
        UE_LOG(LogTemp, Display, TEXT("PARTY_BUTTON_INPUT attached=0"));
    }
    if (UEnhancedInputComponent* Input = BoundInput.Get())
    {
        Input->RemoveBindingByHandle(ConfirmBinding);
    }
    BoundSubsystem.Reset();
    BoundInput.Reset();
    Mapping = nullptr;
    ConfirmAction = nullptr;
}

void APartyButtonPawn::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    ReleaseLocalPartyInput();
    Super::EndPlay(EndPlayReason);
}

void APartyButtonPawn::Confirm()
{
    if (Session && Session->GetState().Phase == EPartyButtonPhase::Playing && !bConfirmed)
    {
        ServerConfirm();
    }
}


void APartyButtonPawn::ServerConfirm_Implementation()
{
    if (Session)
    {
        Session->TryConfirm(*this);
    }
}

void APartyButtonPawn::DrawPartyHUD(AHUD& HUD, const FVector2D& ViewportSize, bool bAnonymous) const
{
    if (!Session)
    {
        return;
    }
    const FPartyButtonState& State = Session->GetState();
    const AGameStateBase* GameState = GetWorld()->GetGameState();
    const int32 Seconds = GameState ? FMath::Max(0, FMath::CeilToInt(State.Deadline - GameState->GetServerWorldTimeSeconds())) : 0;
    FString Prompt;
    switch (State.Phase)
    {
    case EPartyButtonPhase::Countdown:
        Prompt = FString::Format(EXPEDITION_TEXT("准备  {0}", "Get ready  {0}"), {Seconds});
        break;
    case EPartyButtonPhase::Playing:
        Prompt = bConfirmed ? EXPEDITION_TEXT("已确认", "Confirmed") : FString::Format(EXPEDITION_TEXT("按空格确认  {0}", "Press Space  {0}"), {Seconds});
        break;
    case EPartyButtonPhase::Results:
        Prompt = FString::Format(EXPEDITION_TEXT("{0} / {1} 人已确认", "{0} / {1} confirmed"), {State.Confirmed, State.Players});
        break;
    default:
        return;
    }

    float Width, Height;
    UFont* Font = GEngine->GetSmallFont();
    HUD.GetTextSize(Prompt, Width, Height, Font, 1.5f);
    const float X = (ViewportSize.X - Width) * .5f;
    const float Y = ViewportSize.Y * .32f;
    HUD.DrawRect(FLinearColor(.025f, .04f, .05f, .94f), X - 20, Y - 10, Width + 40, Height + 20);
    HUD.DrawText(Prompt, bConfirmed ? FLinearColor(.58f, .9f, .69f) : FLinearColor::White, X, Y, Font, 1.5f);
}

APartyButtonSession::APartyButtonSession()
{
    PrimaryActorTick.bCanEverTick = true;
    Floor = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Arena"));
    SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Stage")));
    Floor->SetupAttachment(GetRootComponent());
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube"));
    Floor->SetStaticMesh(Cube.Object);
    Floor->SetRelativeScale3D(FVector(15, 13, .4));
}

void APartyButtonSession::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(APartyButtonSession, State);
}

APartyPawn* APartyButtonSession::CreateParticipant(int32 Seat)
{
    const double Angle = Seat * UE_TWO_PI / 6.0;
    const FVector Offset(420 * FMath::Cos(Angle), 420 * FMath::Sin(Angle), 78);
    const FTransform Start((-Offset.GetSafeNormal2D()).Rotation(), GetActorLocation() + Offset);
    if (APartyButtonPawn* Pawn = GetWorld()->SpawnActorDeferred<APartyButtonPawn>(APartyButtonPawn::StaticClass(), Start,
        nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn))
    {
        Pawn->Session = this;
        Pawn->FinishSpawning(Start);
        Participants.Add(Seat, Pawn);
        return Pawn;
    }
    return nullptr;
}

void APartyButtonSession::RemoveParticipant(int32 Seat)
{
    Participants.Remove(Seat);
    UpdateCounts();
}

void APartyButtonSession::StartParty()
{
    UpdateCounts();
    State.Phase = EPartyButtonPhase::Countdown;
    State.Deadline = GetWorld()->GetTimeSeconds() + 3.0;
    ForceNetUpdate();
}

void APartyButtonSession::TryConfirm(APartyButtonPawn& Pawn)
{
    if (State.Phase == EPartyButtonPhase::Playing && GetWorld()->GetTimeSeconds() <= State.Deadline
        && Participants.FindKey(&Pawn) && Pawn.GetController() && !Pawn.bConfirmed)
    {
        Pawn.bConfirmed = true;
        Pawn.ForceNetUpdate();
        UpdateCounts();
    }
}

void APartyButtonSession::UpdateCounts()
{
    State.Players = 0;
    State.Confirmed = 0;
    for (const auto& Entry : Participants)
    {
        if (const APartyButtonPawn* Pawn = Entry.Value.Get())
        {
            ++State.Players;
            State.Confirmed += Pawn->bConfirmed;
        }
    }
    ForceNetUpdate();
}

void APartyButtonSession::EnterResults()
{
    State.Phase = EPartyButtonPhase::Results;
    State.Deadline = GetWorld()->GetTimeSeconds() + 2.0;
    ForceNetUpdate();
    UE_LOG(LogTemp, Display, TEXT("PARTY_BUTTON_RESULT confirmed=%d players=%d"), State.Confirmed, State.Players);
}

void APartyButtonSession::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if (!HasAuthority() || bFinished)
    {
        return;
    }
    const double Now = GetWorld()->GetTimeSeconds();
    if (State.Phase == EPartyButtonPhase::Countdown && Now >= State.Deadline)
    {
        State.Phase = EPartyButtonPhase::Playing;
        State.Deadline = Now + 8.0;
        ForceNetUpdate();
        UE_LOG(LogTemp, Display, TEXT("PARTY_BUTTON_OPEN players=%d"), State.Players);
    }
    else if (State.Phase == EPartyButtonPhase::Playing && (State.Confirmed == State.Players || Now >= State.Deadline))
    {
        EnterResults();
    }
    else if (State.Phase == EPartyButtonPhase::Results && Now >= State.Deadline)
    {
        bFinished = true;
        OnPartyFinished.ExecuteIfBound();
    }
}
