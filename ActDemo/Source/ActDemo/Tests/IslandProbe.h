#pragma once

#include "GameFramework/Actor.h"
#include "Island/IslandRules.h"
#include "IslandProbe.generated.h"

class AIslandGameMode;
class AIslandNeutralProp;
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
    UFUNCTION(Client, Reliable) void ClientJoinCrew(int32 Team, bool bReady);
    UFUNCTION(Client, Reliable) void ClientStress(int32 Stage);
    UFUNCTION(Client, Reliable) void ClientShowChart();
    UFUNCTION(Client, Reliable) void ClientStrike(FVector Target);
    UFUNCTION(Client, Reliable) void ClientMudStroke(bool bThrow, FVector Aim);
    UFUNCTION(Client, Reliable) void ClientMudHold(bool bThrow, bool bHeld, FRotator Look);
    UFUNCTION(Client, Reliable) void ClientMudInspect(int32 Stage, FVector Origin);
    UFUNCTION(Server, Reliable) void ServerMudReport(int32 Stage, uint32 Sequence, uint32 Checksum, int32 Top);
    UFUNCTION(Client, Reliable) void ClientFieldAim(FVector Target);
    UFUNCTION(Client, Reliable) void ClientFieldKeepDigging(bool bKeep, FVector Target);
    UFUNCTION(Client, Reliable) void ClientFieldRally();
    UFUNCTION(Client, Reliable) void ClientNeutralInspect(int32 Stage);
    UFUNCTION(Server, Reliable) void ServerNeutralReport(int32 Stage, const FString& Digest);
    UFUNCTION(Client, Reliable) void ClientNeutralInput(uint8 Action, FVector Target);
    UFUNCTION(Client, Reliable) void ClientWalkToDig(FVector Target);
    UFUNCTION(Client, Reliable) void ClientInspectDig(FVector OriginalFloor, FVector ReferenceFloor, uint32 Sequence);
    UFUNCTION(Server, Reliable) void ServerReportDig(uint32 Sequence, uint32 Checksum, int32 Drop, int32 ReferenceDelta);
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
    void TickFieldInput();
    void TickFieldWalk(float DeltaSeconds);
    TOptional<FVector> FieldWalkTarget;
    TOptional<FVector> FieldAim;
    bool bFieldMouseDown = false;
    bool bKeepFieldDig = false;
    int32 LastFieldView = INDEX_NONE;
};

UCLASS()
class AIslandProbe : public AActor
{
    GENERATED_BODY()

public:
    AIslandProbe();
    static bool TryGetWalkTarget(const AIslandGameMode* Mode, int32 Index, FVector& Target, bool& Exposed);
    void Tick(float DeltaSeconds) override;
    void Record(int32 Seat, int32 Epoch, const FIslandClues& Clues, int32 Boxes, int32 Winner, uint32 Sequence, const FString& Digest);
    void RequestStress(int32 Seat, int32 Stage);
    void RecordNeutral(int32 Seat, int32 Stage, const FString& Digest);
    void RecordMud(int32 Seat, int32 Stage, uint32 Sequence, uint32 Checksum, int32 Top);
    void RecordDig(int32 Seat, uint32 Sequence, uint32 Checksum, int32 Drop, int32 ReferenceDelta);

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
    void TickExpedition(double Now);
    int32 ExpeditionTeamForSeat(int32 Seat) const;
    void StartFieldCheck(double Now);
    void StartNeutralCheck(double Now);
    void TickNeutralCheck(double Now);
    void SetNeutralStep(int32 Next, double Now);
    bool NeutralAim(int32 Seat, uint8 Action, const FVector& Target, const AActor* AimActor);
    void NeutralFrame(const AIslandNeutralProp& Prop);
    bool NeutralFloorPose(int32 Seat, const FVector& Column, FVector& Foot, double TopOffset = 850.) const;
    void NeutralWalk(int32 Seat, const FVector& Start, const FVector& End);
    bool NeutralWalkArrived(int32 Seat) const;
    void NeutralDigLane(int32 Seat, const FVector& Point);
    bool NeutralRampPair(const AIslandNeutralProp& Prop, FVector& Start, FVector& End) const;
    void TickMudCheck(double Now);
    void SetMudStep(int32 Next, double Now);
    bool bMudScenario = false;
    int32 MudStep = 0, MudShots = 0;
    double MudKnockTime = 0., MudBase = 0.;
    FVector MudFoot = FVector::ZeroVector, MudShaft = FVector::ZeroVector;
    bool bNeutralScenario = false;
    bool bNeutralWaitingInput = false;
    uint8 NeutralPendingAction = 0;
    int32 NeutralPendingSeat = 0, NeutralKind = 0, NeutralStep = 0, NeutralStroke = 0;
    int32 NeutralWalkSeat = 2;
    double NeutralSendAt = 0., NeutralBase = 0., NeutralPeak = 0.;
    FVector NeutralAimPoint = FVector::ZeroVector;
    FVector NeutralWalkFrom = FVector::ZeroVector, NeutralWalkTo = FVector::ZeroVector;
    TArray<FVector> NeutralSampleLocations;
    TArray<double> NeutralSurfaceBefore;
    TArray<TWeakObjectPtr<AIslandNeutralProp>> NeutralProps;
    FString NeutralExpected;
    TWeakObjectPtr<class ACameraActor> NeutralCamera;
    void TickFieldCheck(double Now);
    void BeginLockCheck(double Now);
    enum class EFieldStep : uint8 { Aim, Strike, InspectDig, WalkToDig, EchoApproach, HearEcho, Gather, Open, Assist, Defend, TakeOver, Seize, PreventReseize, Award, Cancel, Reset, Verify, AimHeldDig, HoldFirst, HoldSecond, Released };
    void SetFieldStage(EFieldStep Stage, double Now);
    bool bFieldScenario = false;
    EFieldStep FieldStage = EFieldStep::Aim;
    FVector FieldStand = FVector::ZeroVector;
    FVector ReferenceFloor = FVector::ZeroVector;
    FVector OriginalFloor = FVector::ZeroVector;
    FVector DigLanding = FVector::ZeroVector;
    FVector SecondFieldTarget = FVector::ZeroVector;
    uint32 HoldSequence = 0;
    int32 FieldDrop = 0;
    int32 ReferenceDelta = 0;
    float FieldSoloRate = 0.f;
    int32 ExpeditionPlayers = 0;
    bool bOneCrew = false;
    enum class ERoomCheck : uint8 { Join, Ready, Withdraw, Depart, Dig, Open, Inspect, Return, Stress, Drain, VerifyEdits, Field, Neutral, Mud };
    ERoomCheck RoomCheck = ERoomCheck::Join;
    int32 ExpeditionOpener = 0;
    TMap<FGuid, FTransform> DepartureBerths;
    TArray<int32> DepartureTeams;
    bool OnPrize() const { return NextDig == IslandRules::PrizeIndex; }

    UPROPERTY() TObjectPtr<AIslandGameMode> Mode;
    UPROPERTY() TArray<TObjectPtr<AIslandProbePeer>> Peers;

    EStep Step = EStep::Waiting;
    EStep AfterInspection = EStep::Waiting;
    TSet<int32> Reports;
    TArray<double> FrameTimes;
    enum class EProfileClock : uint8 { Game, Render, GPU, GameWait, RenderWait, Present, Count };
    TArray<double> ProfileFrames[static_cast<int32>(EProfileClock::Count)];
    bool bProfileFrames = false;
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
