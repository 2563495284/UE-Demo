#pragma once

#include "GameFramework/Actor.h"
#include "Island/IslandRules.h"
#include "IslandProbe.generated.h"

class AIslandGameMode;
class AIslandPlayerController;

UCLASS()
class AIslandProbePeer : public AActor
{
    GENERATED_BODY()

public:
    AIslandProbePeer();
    void Tick(float DeltaSeconds) override;

    UFUNCTION(Client, Reliable) void ClientInspect(int32 Epoch, bool bDigest);
    UFUNCTION(Client, Reliable) void ClientOpenChest();
    UFUNCTION(Client, Reliable) void ClientStress(int32 Stage);
    UFUNCTION(Client, Reliable) void ClientShowChart();
    UFUNCTION(Client, Reliable) void ClientStrike(FVector Target);
    UFUNCTION(Client, Reliable) void ClientPartySpace(bool bPressed, bool bCheckJump);
    UFUNCTION(Server, Reliable) void ServerMeasure(int32 Epoch, FIslandClues Clues, int32 Boxes, int32 Winner,
        uint32 Sequence, const FString& Digest);
    UFUNCTION(Server, Reliable) void ServerStress(int32 Stage);

    TWeakObjectPtr<class AIslandProbe> Coordinator;
    int32 Seat = INDEX_NONE;

private:
    int32 StressStage = 0;
    double NextRequestAt = 0.0;
    double ObserveJumpUntil = 0.0;
};

UCLASS()
class AIslandProbe : public AActor
{
    GENERATED_BODY()

public:
    AIslandProbe();
    void Tick(float DeltaSeconds) override;
    void Record(int32 Seat, int32 Epoch, const FIslandClues& Clues, int32 Boxes, int32 Winner, uint32 Sequence, const FString& Digest);
    void RequestStress(int32 Seat, int32 Stage);

private:
    enum class EStep : uint8 { Waiting, Inspect, Dig, Open, ExpectDenied, Drown, BreakTree, BreakDock, ReturnAfterBreak, StressShared, StressApart, Drain, Race, Restart, PartyOpen, PartyPlay, PartyJump, PartyDone, Done };

    void Advance(EStep Step);
    void Require(bool bSuccess, const TCHAR* Message);
    void InspectThen(EStep After, bool bDigest = false);
    void Place(int32 Seat, const FVector& Center);
    void PrepareDig(int32 Index);
    void PrepareOpen(int32 Team);
    void StartStress(int32 Stage);
    void TickParty(double Now);
    bool OnPrize() const { return NextDig == IslandRules::PrizeIndex; }

    UPROPERTY() TObjectPtr<AIslandGameMode> Mode;
    UPROPERTY() TArray<TObjectPtr<AIslandProbePeer>> Peers;

    EStep Step = EStep::Waiting;
    EStep AfterInspection = EStep::Waiting;
    TSet<int32> Reports;
    TArray<double> FrameTimes;
    FString ExpectedDigest;
    FString BeforePartyDigest;
    TArray<TWeakObjectPtr<APawn>> OriginalPawns;
    double PartyStartedAt = 0.0;
    int32 PartyAttempts = 0;
    bool bPartyScenario = false;
    bool bPartyTimeout = false;
    FVector PrizePose = FVector::ZeroVector;
    int32 Epoch = 0;
    int32 NextDig = 0;
    int32 OpeningTeam = 0;
    uint32 StressStart = 0, GroupStart = 0;
    uint32 DrownSequence = 0;
    double StepAt = 0.0, LastAction = 0.0, LastFrame = 0.0;
    bool bIssued = false, bSawDrown = false, bReopened = false, bFailed = false;
};
