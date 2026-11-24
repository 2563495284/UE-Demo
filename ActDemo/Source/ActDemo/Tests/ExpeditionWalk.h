#pragma once

#include "GameFramework/Actor.h"
#include "ExpeditionWalk.generated.h"

class AIslandPlayerController;
class AVoxelTerrain;

// Opt-in single-player QA driver. Movement always passes through the normal player controller.
UCLASS(NotPlaceable)
class AExpeditionWalk : public AActor
{
    GENERATED_BODY()
public:
    AExpeditionWalk();
    void Tick(float DeltaSeconds) override;

private:
    enum class EStage { Lobby, Landing, Walking, Viewing, Digging, Escaping, Filling, Returning, Complete };
    void ChangeStage(EStage Next);
    void TickTreasure(float DeltaSeconds);
    enum class EHuntStep { AwaitPlay, ReadChart, Approach, Excavate, Open, Recall, Done };
    EHuntStep HuntStep = EHuntStep::AwaitPlay;
    void SetHuntStep(EHuntStep Next);
    int32 TreasureIndex = 0;
    FVector ChestTarget = FVector::ZeroVector;
    bool bTreasureRun = false, bChartHeld = false, bRecallHeld = false;
    int32 DigAttempts = 0;
    void RecordView(const FString& Name) const;
    void StopRun(const FString& Reason);
    void Turn(AIslandPlayerController& Player, FRotator Desired, float DeltaSeconds) const;
    bool WalkTo(AIslandPlayerController& Player, FVector2D Dest, float DeltaSeconds, double Radius=74.) const;

    EStage Stage = EStage::Lobby;
    TArray<FVector2D> Route;
    TArray<double> Frames;
    FVector Pit = FVector::ZeroVector;
    FVector2D EscapeDirection = FVector2D(1,0);
    FVector LastPosition = FVector::ZeroVector;
    double WalkStarted = 0., StageStarted = 0., LastInput = -5., Meters = 0., LastFrame = 0.;
    int32 Destination = 0;
    uint32 BaselineSequence = 0;
    double LastCarve = -5.;
    bool bStarted = false, bPassed = false, bClimbing = false;
};
