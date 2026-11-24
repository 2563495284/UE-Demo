#pragma once

#include "PartyMinigame.h"
#include "PartyButtonChallenge.generated.h"

class APartyButtonSession;
class UCameraComponent;
class UEnhancedInputComponent;
class UEnhancedInputLocalPlayerSubsystem;
class UInputAction;
class UInputMappingContext;
class UStaticMeshComponent;

UENUM()
enum class EPartyButtonPhase : uint8
{
    Waiting,
    Countdown,
    Playing,
    Results
};

USTRUCT()
struct FPartyButtonState
{
    GENERATED_BODY()

    UPROPERTY() EPartyButtonPhase Phase = EPartyButtonPhase::Waiting;
    UPROPERTY() double Deadline = 0.0;
    UPROPERTY() int32 Players = 0;
    UPROPERTY() int32 Confirmed = 0;
};

UCLASS()
class APartyButtonPawn : public APartyPawn
{
    GENERATED_BODY()

public:
    APartyButtonPawn();
    void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
    bool HasLocalPartyInput() const override;
    void ReleaseLocalPartyInput() override;
    void DrawPartyHUD(AHUD& HUD, const FVector2D& ViewportSize) const override;

protected:
    void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;
    void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
    friend class APartyButtonSession;
    void Confirm();
    UFUNCTION(Server, Reliable) void ServerConfirm();

    UPROPERTY(Replicated) TObjectPtr<APartyButtonSession> Session;
    UPROPERTY(Replicated) bool bConfirmed = false;
    UPROPERTY() TObjectPtr<UInputAction> ConfirmAction;
    UPROPERTY() TObjectPtr<UInputMappingContext> Mapping;
    UPROPERTY() TObjectPtr<UCameraComponent> Camera;
    UPROPERTY() TObjectPtr<UStaticMeshComponent> Podium;
    TWeakObjectPtr<UEnhancedInputComponent> BoundInput;
    TWeakObjectPtr<UEnhancedInputLocalPlayerSubsystem> BoundSubsystem;
    uint32 ConfirmBinding = 0;
};

UCLASS()
class APartyButtonSession : public APartySession
{
    GENERATED_BODY()

public:
    APartyButtonSession();
    void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
    APartyPawn* CreateParticipant(int32 Seat) override;
    void RemoveParticipant(int32 Seat) override;
    void StartParty() override;
    void TryConfirm(APartyButtonPawn& Pawn);
    const FPartyButtonState& GetState() const { return State; }

protected:
    void Tick(float DeltaSeconds) override;

private:
    void EnterResults();
    void UpdateCounts();

    UPROPERTY() TObjectPtr<UStaticMeshComponent> Floor;
    UPROPERTY(Replicated) FPartyButtonState State;
    TMap<int32, TWeakObjectPtr<APartyButtonPawn>> Participants;
    bool bFinished = false;
};
