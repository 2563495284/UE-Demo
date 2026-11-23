#pragma once

#include "GameFramework/Actor.h"
#include "VoxelPlaygroundProbe.generated.h"

class AVoxelPlayerController;
class AVoxelTerrain;

/** Opt-in two-process acceptance scenario. Only the authority can create it, using -VoxelQA. */
UCLASS()
class AVoxelPlaygroundProbe : public AActor
{
    GENERATED_BODY()

public:
    AVoxelPlaygroundProbe();
    void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
    void Tick(float DeltaTime) override;

private:
    enum class EPhase : uint8
    {
        Waiting, Punch, Walk, RejectFill, BuildStep, Land, Climb, Dig, Recover, Stress, Done
    };

    void TickHost(AVoxelPlayerController& Host, AVoxelTerrain& Terrain, double Now);
    void TickGuest(AVoxelPlayerController& Guest, AVoxelTerrain& Terrain, double Now);
    void Advance(EPhase Next, double Now);
    void Check(bool bPassed, const TCHAR* Label);
    static void Place(AVoxelPlayerController& Player, const FVector& Position, FRotator View = FRotator::ZeroRotator);
    void QueueStressEdit(AVoxelPlayerController& Player, uint32 ExpectedSequence);
    UFUNCTION(Server, Reliable) void ServerStressFill(uint32 ExpectedSequence);

    UPROPERTY(Replicated) uint8 PhaseValue = 0;
    UPROPERTY() TObjectPtr<AVoxelPlayerController> RemotePlayer;
    double StartedAt = 0.0;
    double LastStressRequest = 0.0;
    double PreviousFrame = 0.0;
    TArray<double> FrameTimesMs;
    uint8 LastGuestPhase = 0;
    uint32 LastRequestedSequence = MAX_uint32;
    bool bDidPhaseAction = false;
    bool bCheckedLand = false;
    bool bFinished = false;
    EPhase Phase() const { return static_cast<EPhase>(PhaseValue); }
};
