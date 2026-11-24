#pragma once

#include "GameFramework/PlayerController.h"
#include "Voxel/VoxelDensity.h"
#include "VoxelPlayerController.generated.h"

class AVoxelCharacter;
class AVoxelTerrain;
class UActInputConfig;
struct FInputActionValue;

UCLASS()
class ACTDEMO_API AVoxelPlayerController : public APlayerController
{
    GENERATED_BODY()

public:
    AVoxelPlayerController();
    AVoxelCharacter* GetHero() const;
    bool IsPlaying() const { return bPlaying; }
    bool IsVoxelInputEnabled() const { return bVoxelInputEnabled; }
    void SetVoxelInputEnabled(bool bEnabled);
    double GetRecoverProgress() const;
    const FString& GetNotice() const { return Notice; }
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
    void BeginPlay() override;
    void SetupInputComponent() override;
    void PlayerTick(float DeltaTime) override;

private:
    struct FIncomingEdit
    {
        FVoxelEdit Edit;
        double ReceivedAt;
    };

    void Move(const FInputActionValue& Value);
    void Look(const FInputActionValue& Value);
    void Jump();
    void Punch();
    void DigDown();
    void Fill();
    void ApplyNextEdit();
    void UpdateVoxelMapping();
    void TerrainProtocolError();

    UFUNCTION(Server, Reliable) void ServerTerrainReady(uint32 Checksum);
    UFUNCTION(Server, Reliable) void ServerRequestEdit(EVoxelAction Action, FVector_NetQuantizeNormal Aim);
    UFUNCTION(Server, Reliable) void ServerSetClimb(bool bEnabled);
    UFUNCTION(Server, Reliable) void ServerSetRecover(bool bEnabled);

    UPROPERTY() TObjectPtr<UActInputConfig> InputConfig;
    TWeakObjectPtr<AVoxelTerrain> Terrain;
    TArray<FIncomingEdit> Incoming;
    uint32 LastReceivedSequence = 0;
    FTimerHandle RecoverTimer;
    double RecoverStartedAt = -1.0;
    double NoticeUntil = -1.0;
    bool bPlaying = false;
    bool bVoxelInputEnabled = true;
    bool bSentReady = false;
    FString Notice = TEXT("Waiting for both players");
};
