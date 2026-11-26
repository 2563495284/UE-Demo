#pragma once

#include "GameFramework/PlayerController.h"
#include "Voxel/VoxelDensity.h"
#include "Abilities/ExpeditionAbility.h"
#include "VoxelPlayerController.generated.h"

class AVoxelCharacter;
class AVoxelTerrain;
class UVoxelInputConfig;
struct FInputActionValue;

UCLASS()
class ACTDEMO_API AVoxelPlayerController : public APlayerController
{
    GENERATED_BODY()

public:
    AVoxelCharacter* GetHero() const;
    FVector GetExcavationAim(const AVoxelCharacter& Hero) const;
    bool UsesExpeditionAbilities() const;
    FVector GetExpeditionThrowAim(const AVoxelCharacter& Hero) const;
    FVector GetExpeditionActionAim(EExpeditionAction Action) const;
    void SubmitExpeditionThrow(const FVector& Aim);
    void OnMudStunChanged(bool bStunned);
    void SetExpeditionDigHeld(bool bHeld);
    float GetPunchChargeAlpha() const;
    uint8 GetReleasedPunchPower() const { return ReleasedPunchPower; }
    bool IsPunchRelease() const { return bResolvingPunchRelease; }
    void CancelPunchCharge();
    bool IsHoldingExpeditionDig() const { return HasAuthority() ? bServerDigHeld : bDigHeld; }
    void SetExpeditionThrowHeld(bool bHeld);
    bool IsPlaying() const { return bPlaying; }
    virtual bool IsVoxelInputEnabled() const { return bVoxelInputEnabled; }
    bool WantsFirstPersonView() const { return bFirstPersonView && bPlaying && IsLocalController(); }
    void SetVoxelInputEnabled(bool bEnabled);
    void SetIslandUIInput(bool bOpen);
    double GetRecoverProgress() const;
    const FString& GetNotice() const { return Notice; }
    FString GetDisplayNotice() const;
    bool IsNoticeVisible() const;

    // The same input-to-RPC entry points are exercised by the multiplayer acceptance probe.
    void SubmitEdit(EVoxelAction Action);
    void SubmitEdit(EVoxelAction Action, const FVector& Aim);
    void ApplyMove(const FVector2D& Input);
    void StartClimb();
    void StopClimb();
    void StartRecover();
    void StopRecover();
    UFUNCTION(Exec) void VoxelStatus();

    UFUNCTION(Client, Reliable) void ClientSetRoundActive(bool bActive, const FString& Message);
    UFUNCTION(Client, Reliable) void ClientReceiveEdit(const FVoxelEdit& Edit);
    UFUNCTION(Client, Reliable) void ClientEditRejected(const FString& Message);
    UFUNCTION(Client, Reliable) void ClientResetView(FRotator View);

    // Used by the authoritative game mode for a controller's seat only.
    TOptional<uint32> InitialTerrainChecksum;
    FTransform StartTransform;
    double LastRequestAt = -1e9;

protected:
    virtual void Punch();
    void TriggerExpeditionEvent(EExpeditionAction Action, bool bHeld, AActor* Target = nullptr);
    FVector2D GetLocalLookScale() const;
    void BeginPlay() override;
    void EndPlay(const EEndPlayReason::Type Reason) override;
    void DispatchExpeditionAction(EExpeditionAction Action, bool bHeld = false, FVector Aim = FVector::ZeroVector);
    void ReleaseHeldExpeditionInputs();
    void SetupInputComponent() override;
    void PlayerTick(float DeltaTime) override;
    void FlushPressedKeys() override;
    void SetPawn(APawn* InPawn) override;

private:
    struct FIncomingEdit
    {
        FVoxelEdit Edit;
        double ReceivedAt;
    };

    void Move(const FInputActionValue& Value);
    void Look(const FInputActionValue& Value);
    void Jump();
    void SwitchPerspective();
    void DigDown();
    void StopDig();
    void StopThrow();
    void SendHeldExpeditionAim();
    UFUNCTION(Server, Reliable) void ServerExpeditionAction(EExpeditionAction Action, bool bHeld, FVector_NetQuantizeNormal Aim);
    UFUNCTION(Server, Reliable) void ServerReleaseExpeditionAction(EExpeditionAction Action);
    UFUNCTION(Server, Unreliable) void ServerUpdateExpeditionAim(EExpeditionAction Action, FVector_NetQuantizeNormal Aim);
    UFUNCTION(Server, Reliable) void ServerFinishPunch(bool bStrike, FVector_NetQuantizeNormal Aim,float ReportedHoldSeconds);
    void ClearServerPunch();
    double LocalPunchStarted = 0.;
    double ServerPunchStarted = 0.;
    uint8 ReleasedPunchPower = 0;
    bool bResolvingPunchRelease = false;
    bool bDigHeld = false;
    bool bThrowHeld = false;
    bool bServerDigHeld = false;
    bool bServerThrowHeld = false;
    FVector DigAim = FVector::ForwardVector;
    FVector ThrowAim = FVector::ForwardVector;
    double NextAimUpdate = 0.;
    void Fill();
    void ApplyNextEdit();
    void UpdateVoxelMapping();
    void TerrainProtocolError();

    UFUNCTION(Server, Reliable) void ServerTerrainReady(uint32 Checksum);
    UFUNCTION(Server, Reliable) void ServerRequestEdit(EVoxelAction Action, FVector_NetQuantizeNormal Aim);
    UFUNCTION(Server, Reliable) void ServerSetClimb(bool bEnabled);
    UFUNCTION(Server, Reliable) void ServerSetRecover(bool bEnabled);

    UPROPERTY(Transient) TObjectPtr<UVoxelInputConfig> InputConfig;
    TWeakObjectPtr<AVoxelTerrain> Terrain;
    TArray<FIncomingEdit> Incoming;
    bool bApplyingEdits = false;
    uint32 LastReceivedSequence = 0;
    FTimerHandle RecoverTimer;
    double RecoverStartedAt = -1.0;
    double NoticeUntil = -1.0;
    bool bPlaying = false;
    bool bFirstPersonView = false;
    bool bVoxelInputEnabled = true;
    bool bSentReady = false;
    FString Notice = TEXT("Waiting for both players");
};
