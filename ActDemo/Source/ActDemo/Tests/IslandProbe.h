#pragma once

#include "GameFramework/Actor.h"
#include "Island/IslandRules.h"
#include "Tests/ExpeditionDecoyToolProof.h"
#include "IslandProbe.generated.h"

class AIslandGameMode;
class AIslandChest;
class AVoxelCharacter;
class UIslandRouteVoxelComponent;

// A repeatable wall and raised deck made from the same solid field as island scenery.
UCLASS(NotPlaceable, Transient)
class AIslandMudProbeSurface : public AActor
{
    GENERATED_BODY()
public:
    AIslandMudProbeSurface();
    void BeginPlay() override;
    UPROPERTY() TObjectPtr<UIslandRouteVoxelComponent> Field;
};
class AIslandNeutralProp;
class AIslandPlayerController;

UENUM()
enum class EIslandDecoyCheck : uint8 { None, Held, Cancelled, Started };

UCLASS()
class AIslandProbePeer : public AActor
{
    GENERATED_BODY()

public:
    AIslandProbePeer();
    void Tick(float DeltaSeconds) override;

    UFUNCTION(Client, Reliable) void ClientInspect(int32 Epoch, bool bDigest);
    UFUNCTION(Client, Reliable) void ClientOpenChest();
    bool HasReleasedChestClick() const { return AimAtChestUntil == 0. && ReleaseChestClickFrame == 0; }
    UFUNCTION(Client, Reliable) void ClientWatchHeldLid();
    UFUNCTION(Server, Reliable) void ServerHeldLidObserved();
    UFUNCTION(Client, Reliable) void ClientJoinCrew(int32 Team, bool bReady);
    UFUNCTION(Client, Reliable) void ClientCharacterInput(int32 Model, int32 Team, int32 Ready, bool bModelLast = false);
    UFUNCTION(Client, Reliable) void ClientCharacterInspect(int32 Stage, EIslandPhase Phase, FIntVector Host, FIntVector Guest, int32 Members);
    UFUNCTION(Server, Reliable) void ServerCharacterObserved(int32 Stage);
    int32 CharacterObserved = INDEX_NONE;
    int32 CharacterLocallySeen = INDEX_NONE;
    UFUNCTION(Client, Reliable) void ClientStress(int32 Stage);
    UFUNCTION(Client, Reliable) void ClientShowChart();
    UFUNCTION(Client, Reliable) void ClientStrike(FVector Target);
    UFUNCTION(Client, Reliable) void ClientMudStroke(bool bThrow, FVector Aim);
    UFUNCTION(Client, Reliable) void ClientMudHold(bool bThrow, bool bHeld, FRotator Look);
    UFUNCTION(Client, Reliable) void ClientBodyMudInspect(int32 Stage, AVoxelCharacter* Subject, FName Photo);
    UFUNCTION(Server, Reliable) void ServerBodyMudReport(int32 Stage, uint32 Digest, int32 Clods, bool bAnchored);
    int32 BodyMudStage = INDEX_NONE;
    uint32 BodyMudDigest = 0;
    int32 BodyMudClods = 0;
    bool bBodyMudAnchored = false;
    UFUNCTION(Client, Reliable) void ClientMudInspect(int32 Stage, FVector Origin);
    UFUNCTION(Server, Reliable) void ServerMudReport(int32 Stage, uint32 Sequence, uint32 Checksum, int32 Top);
    UFUNCTION(Client, Reliable) void ClientFieldAim(FVector Target);
    UFUNCTION(Client, Reliable) void ClientFieldKeepDigging(bool bKeep, FVector Target);
    UFUNCTION(Client, Reliable) void ClientFieldRally();
    UFUNCTION(Client, Reliable) void ClientFieldEchoStrike(int32 Trial);
    UFUNCTION(Client, Reliable) void ClientNeutralInspect(int32 Stage);
    UFUNCTION(Client, Reliable) void ClientSceneryInspect(int32 Stage, AActor* Subject, FName Part, FVector Eye, FVector Aim);
    UFUNCTION(Client, Reliable) void ClientSceneryCamera(FName Clip, FVector View, FVector Focus, bool bUsePlayer);
    UFUNCTION(Client, Reliable) void ClientSceneryRay(int32 Stage, FVector From, FVector To, AActor* Ignore, bool bMud);
    UFUNCTION(Server, Reliable) void ServerNeutralReport(int32 Stage, const FString& Digest);
    UFUNCTION(Client, Reliable) void ClientNeutralInput(uint8 Action, FVector Target);
    static constexpr uint8 NeutralJumpButton = 4;
    UFUNCTION(Client, Reliable) void ClientWalkToDig(FVector Target);
    UFUNCTION(Client, Reliable) void ClientInspectDig(FVector OriginalFloor, FVector ReferenceFloor, uint32 Sequence);
    UFUNCTION(Server, Reliable) void ServerReportDig(uint32 Sequence, uint32 Checksum, int32 Drop, int32 ReferenceDelta);
    UFUNCTION(Client, Reliable) void ClientPartySpace(bool bPressed, bool bCheckJump);
    UFUNCTION(Client, Reliable) void ClientInspectArenaView(int32 Round, FName Feature);
    UFUNCTION(Server, Reliable) void ServerArenaViewReady(int32 Round);
    int32 ReadyArenaViewRound = 0;
    UFUNCTION(Client, Reliable) void ClientTryArenaPunch(int32 Round, int32 Trial, FVector Target);
    UFUNCTION(Server, Reliable) void ServerTriedArenaPunch(int32 Round, int32 Trial);
    int32 TriedPunchRound = 0, TriedPunchTrial = INDEX_NONE;
    UFUNCTION(Client, Reliable) void ClientSampleArenaTerrain(int32 Round, FName Feature, int32 Stage, FVector Surface, int32 Sequence);
    UFUNCTION(Server, Reliable) void ServerSampledArenaTerrain(int32 Round, int32 Stage);
    int32 SampledTerrainRound = 0, SampledTerrainStage = INDEX_NONE;
    UFUNCTION(Client, Reliable) void ClientGameTool(FVector Target, bool bPlace,float HoldSeconds=.12f);
    UFUNCTION(Client, Reliable) void ClientCheckMaze(int32 Round, int32 Stage, FVector Midpoint, FVector Normal);
    UFUNCTION(Server, Reliable) void ServerCheckedMaze(int32 Round, int32 Stage);
    int32 MazeObservedRound = 0, MazeObservedStage = INDEX_NONE;
    UFUNCTION(Client, Reliable) void ClientInspectVenue(int32 Round, int32 ExpectedGone, int32 ExpectedParts,uint32 Digest);
    UFUNCTION(Server, Reliable) void ServerVenueReport(int32 Round, int32 Gone, int32 Pieces);
    UFUNCTION(Client, Reliable) void ClientGameWalk(FVector Destination);
    UFUNCTION(Client, Reliable) void ClientGameReceipt(int32 Round, bool bVerifyRepeatedClaim = false);
    UFUNCTION(Server, Reliable) void ServerGameReceipt(int32 Round, const FIslandClues& Clues, uint8 Outcome, uint8 Event, uint8 Effects);
    UFUNCTION(Server, Reliable) void ServerMeasure(int32 Epoch, FIslandClues Clues, int32 Boxes, int32 Winner,
        uint32 Sequence, const FString& Digest);
    UFUNCTION(Server, Reliable) void ServerStress(int32 Stage);

    TWeakObjectPtr<class AIslandProbe> Coordinator;
    int32 Seat = INDEX_NONE;
    EIslandDecoyCheck ConfirmedDecoyCheck = EIslandDecoyCheck::None;
    UFUNCTION(Client, Reliable) void ClientCheckDecoy(EIslandDecoyCheck Check, EIslandDecoyEvent Event, int32 BoostMask, double ReturnedAt);
    UFUNCTION(Server, Reliable) void ServerConfirmDecoy(EIslandDecoyCheck Check);
    UFUNCTION(Client, Reliable) void ClientObserveDecoyWorld(int32 Round, int32 Stage, EIslandDecoyEvent Event,
        FVector Target, FVector2D SpentChest, uint32 Sequence, uint32 Checksum, const TArray<FIntVector>& Sails);
    UFUNCTION(Server, Reliable) void ServerObservedDecoyWorld(int32 Round, int32 Stage);
    int32 DecoyObservedRound = 0, DecoyObservedStage = INDEX_NONE;
    bool bHeldLidObserved = false;

private:
    FName SceneryClip;
    double SceneryClipStarted = 0., NextSceneryFrame = 0.;
    TWeakObjectPtr<class ACameraActor> SceneryCamera;
    void CaptureSceneryFrame();
    int32 StressStage = 0;
    double NextRequestAt = 0.0;
    double ObserveJumpUntil = 0.0;
    void TickFieldInput();
    void TickFieldWalk(float DeltaSeconds);
    TOptional<FVector> FieldWalkTarget;
    TOptional<FVector> GameWalkTarget;
    void TickGameWalk();
    void TickGameTool(AIslandPlayerController& Player);
    TOptional<FVector> GameToolTarget;
    bool bGameToolPlaces = false;
    float GameToolHoldSeconds=.12f;
    double AimAtChestUntil = 0.;
    uint8 AimedLidFrames = 0;
    bool bWatchHeldLid = false;
    double HeldLidSince = -1.;
    uint64 ReleaseChestClickFrame = 0;
    FKey HeldArenaKey;
    double ReleaseArenaKeyAt = 0.;
    int32 TerrainSampleRound = 0, TerrainSampleStage = INDEX_NONE, TerrainSampleSequence = 0;
    FVector TerrainSampleOrigin = FVector::ZeroVector;
    FName TerrainSampleGame;
    double TerrainPhotoAt = 0.;
    bool bTerrainOriginallyFirst = false, bTerrainPhotoRequested = false;
    FString TerrainPhotoFile;
    void TickArenaTerrainSample(AIslandPlayerController& Player);
    void TickArenaPunchInput(AIslandPlayerController& Player);
    int32 ArenaPunchRound = 0, ArenaPunchTrial = INDEX_NONE;
    double ArenaPunchPressedAt = -1.;
    FVector ArenaPunchTarget = FVector::ZeroVector;
    bool bPunchViewWasFirst = false;
    void TickArenaViews(AIslandPlayerController& Player);
    enum class EViewStep : uint8 { Idle, Photo, LookAtFloor, Aim, Toggle, Return };
    EViewStep ViewStep = EViewStep::Idle;
    int32 ViewRound = 0, ViewsMeasured = 0;
    FName ViewFeature;
    double ViewNext = 0.;
    bool bViewFirst = false, bViewKeyDown = false, bSetGuestView = false;
    FRotator SceneView = FRotator::ZeroRotator;
    TOptional<FVector> AbortWalkStart;
    bool bAbortWalkConfirmed = false;
    void TickAbortRecovery(AIslandPlayerController& Player);
    void TickInteractionProof(AIslandPlayerController& Player);
    uint32 VeilRound = 0;
    bool bVeilReturn = false, bVeilObserved = false, bVeilLogged = false;
    bool bVeilBlack = false, bVeilSawSwap = false, bVeilLockedEarly = false, bVeilUnlockedBlack = false;
    bool bVeilClosingPhoto = false, bVeilBlackPhoto = false, bVeilOpeningPhoto = false, bVeilMoving = false;
    TWeakObjectPtr<APawn> VeilBlackPawn;
    FVector VeilStart = FVector::ZeroVector, VeilBlackPosition = FVector::ZeroVector;
    double VeilMoved = 0., VeilBlackDrift = 0., VeilDestinationDrift = 0., VeilBlockedInput = 0.;
    FName LastTreasureShot;
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
    static AIslandChest* PrepareFilmPrize(AIslandGameMode&, AIslandPlayerController&, FVector Center);
    static bool TryGetWalkTarget(const AIslandGameMode* Mode, int32 Index, FVector& Target, const AIslandChest*& Exposed);
    void Tick(float DeltaSeconds) override;
    void Record(int32 Seat, int32 Epoch, const FIslandClues& Clues, int32 Boxes, int32 Winner, uint32 Sequence, const FString& Digest);
    void RequestStress(int32 Seat, int32 Stage);
    void RecordNeutral(int32 Seat, int32 Stage, const FString& Digest);
    void RecordMud(int32 Seat, int32 Stage, uint32 Sequence, uint32 Checksum, int32 Top);
    void RecordDig(int32 Seat, uint32 Sequence, uint32 Checksum, int32 Drop, int32 ReferenceDelta);
    void RecordGameReceipt(int32 Seat, int32 Round, const FIslandClues& Clues, uint8 Outcome, uint8 Event, uint8 Effects);
    void RecordVenueReport(int32 Seat, int32 Round, int32 Gone, int32 Pieces);

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
    void RetryLegacyBriefingChest(double Now);
    void TickExpedition(double Now);
    void TickExpeditionGame(double Now);
    bool MeasureControlPass(AIslandPlayerController& Player, int32 Pass);
    int32 ControlPassesDone = 0;
    enum class EControlStep : uint8 { SetUp, Settle, MouseForward, MouseBack, AlignForWalk, SideStep, Jump, EyeView, Restore };
    EControlStep ControlStep = EControlStep::SetUp;
    double ControlStepAt = 0., ControlJumpBase = 0., ControlJumpHeight = 0.;
    FTransform ControlOriginalPose;
    FRotator ControlOriginalView, ControlMouseStart, ControlMouseDelta, ControlReverseDelta;
    FVector ControlCameraOffset, ControlEyeOffset;
    float ControlCameraFOV = 0.f, ControlEyeFOV = 0.f;
    bool bControlOriginalFirst = false, bReleaseControlViewKey = false;
    double ControlViewInputAt = -1.;
    double ControlSideSpeed = 0., ControlBodyTurn = 0.;
    UPROPERTY() TObjectPtr<class UBoxComponent> ControlPlatform;
    void TickCharacterSelection(double Now);
    int32 CharacterStage = INDEX_NONE;
    double CharacterStageAt = 0.;
    double NextCharacterPoll = 0.;
    void PlayExpeditionGame(class APartyVoxelSession& Arena);
    void PlayMazeChase(class APartyVoxelSession& Arena);
    int32 GameWinnerMask = 0;
    FVector MazeWallCenter = FVector::ZeroVector, MazeWallNormal = FVector::ZeroVector;
    double MazeBlockedAt = 0.;
    int32 MazeLastEdit=0;
    UPROPERTY() TObjectPtr<class ACameraActor> MazeProofCamera;
    uint64 MazePhotoFrame=0;
    int32 MazePhotoStage=INDEX_NONE;
    bool CaptureMazeStage(class APartyVoxelSession& Arena,int32 Phase,const TCHAR* Label);
    bool TickArenaTerrain(class APartyVoxelSession& Arena);
    bool TickArenaPunchTrials(APartyVoxelSession& Arena);
    bool FindArenaPunchSite(APartyVoxelSession& Arena, class APartyVoxelPawn& Pawn, FVector& Point);
    FVector TerrainSamplePoint = FVector::ZeroVector;
    TArray<FVector> PunchSites;
    FTransform PunchOriginalPose;
    bool bPunchTrialSent = false;
    int32 TerrainStage = 0, TerrainInputSequence = INDEX_NONE;
    bool bTerrainSampleRequested = false;
    TMap<int32,FTransform> TerrainObserverPoses;
    bool TickVenueProof(class APartyVoxelSession& Arena, bool bShots);
    bool CollectVenueReports(int32 Gone);
    enum class EVenueStep : uint8 { Entry, OccludedFar, CheckFar, OccludedNear, CheckNear, Strike, CheckStrike, BeforePhoto, WaitBeforePhoto, AfterPhoto, WaitAfterPhoto, Final, Restored };
    EVenueStep VenueStep = EVenueStep::Entry;
    bool bVenueProof = false, bAllVenueBuildings = false;
    int32 VenueId = 0, ExpectedVenueGone = 0;
    FIntVector VenueSpawn = FIntVector::ZeroValue, VenueWall = FIntVector::ZeroValue;
    FVector VenueTarget = FVector::ZeroVector;
    int32 VenueWallEditSequence = 0;
    TSet<int32> VenueReports;
    void StartNextGame();
    void TickChestRevisit();
    bool bTestingChestRevisit = false;
    bool bGamesScenario = false;
    bool bGamesDrop = false;
    bool bGameDropSent = false;
    bool bGameStarted = false;
    double AbortRecoveredAt = 0.;
    bool bGameOverviewShot = false;
    int32 GameCount = 0, GameAction = 0, GameWinnerSeat = 0;
    FName OnlyGame;
    uint32 GameTerrainCRC = 0;
    TMap<int32, FIslandClues> GamePreviousClues;
    TArray<FVector> GameBeforePositions;
    TSet<int32> UnsafeReturnSeats;
    void SnapshotDecoyWorld();
    bool TickDecoyWorld(EIslandDecoyEvent Event, double Now);
    uint32 DecoyTerrainSequence = 0;
    int32 DecoyWorldMask = 0, DecoyWorldStage = 0;
    double DecoyWorldInputAt = 0.;
    TArray<FVector> DecoyRampFloors;
    TOptional<FVector> DecoyRampPeak;
    TArray<FIntVector> DecoySailsBefore;
    int32 BoostMaskBeforeGame = 0;
    EIslandDecoyEvent ActiveBeforeGame{};
    double FirstDecoyReturnedAt = 0.;
    bool bCheckingDecoyHold = false;
    bool AllPeersObservedDecoy(EIslandDecoyCheck Check, EIslandDecoyEvent Event, int32 BoostMask);
    int32 CeasefireGroundSequence = INDEX_NONE;
    double CeasefireFloorBefore = 0.;
    bool bInjectedDecoyLoadFailure = false;
    bool bVerifiedDecoyLoadRollback = false;
    bool bMeasuringDecoyTools = false;
    friend struct FExpeditionDecoyToolProof;
    FExpeditionDecoyToolProof DecoyTools;
    TOptional<FIntVector> StoneProofCell;
    bool bStoneDug = false;
    double StoneHeightBefore = 0.;
    int32 FloodSealedSequence = 0, FloodScoreAfterBreach = 0;
    double FloodBreachTime = 0.;
    int32 ExpeditionTeamForSeat(int32 Seat) const;
    void StartFieldCheck(double Now);
    void StartNeutralCheck(double Now);
    void TickNeutralCheck(double Now);
    void TickWindChain(AIslandNeutralProp& Tower, double Now);
    bool bWindChainDone = false;
    void TickSceneryCheck(double Now);
    void TickSceneryReplay(double Now);
    TWeakObjectPtr<class AExpeditionLandmark> ReplayArch;
    TWeakObjectPtr<class UIslandMeshVoxels> WestPier, EastPier, ArchBeam;
    int32 ReplayHits = 0;
    FVector ReplayTarget = FVector::ZeroVector;
    FVector ReplayRayStart = FVector::ZeroVector, ReplayRayEnd = FVector::ZeroVector;
    int32 SceneryCase = 0, SceneryHits = 0, SceneryCuts = 0;
    int32 SceneryInitial = 0, SceneryAttempts = 0;
    TWeakObjectPtr<class UProceduralMeshComponent> SceneryField;
    FVector SceneryEye = FVector::ZeroVector, SceneryAim = FVector::ForwardVector;
    FVector SceneryFirstPoint = FVector::ZeroVector;
    bool bSceneryPartial = false, bSceneryDifferent = false, bSceneryRepeated = false;
    bool bSceneryInventory = false;
    void SetNeutralStep(int32 Next, double Now);
    bool NeutralAim(int32 Seat, uint8 Action, const FVector& Target, const AActor* AimActor);
    void NeutralFrame(const AIslandNeutralProp& Prop);
    bool NeutralFloorPose(int32 Seat, const FVector& Column, FVector& Foot, double TopOffset = 850.) const;
    void NeutralWalk(int32 Seat, const FVector& Start, const FVector& End);
    bool NeutralWalkArrived(int32 Seat) const;
    void NeutralDigLane(int32 Seat, const FVector& Point);
    void TickMudCheck(double Now);
    void SetMudStep(int32 Next, double Now);
    bool bMudScenario = false;
    int32 MudStep = 0, MudShots = 0;
    uint32 MudShaftSequence = 0;
    double MudBase = 0.;
    bool SharedBodyMud(int32 Stage, AVoxelCharacter& Victim, FName Photo, double Now);
    TWeakObjectPtr<AIslandMudProbeSurface> MudWall;
    FVector MudFoot = FVector::ZeroVector, MudShaft = FVector::ZeroVector;
    FVector MudExitLip = FVector::ZeroVector;
    bool bNeutralScenario = false;
    bool bEarlyPrizeCheck = false;
    bool bNeutralWaitingInput = false;
    uint8 NeutralPendingAction = 0;
    int32 NeutralPendingSeat = 0, NeutralKind = 0, NeutralStep = 0, NeutralStroke = 0;
    int32 NeutralWalkSeat = 2;
    double NeutralSendAt = 0., NeutralBase = 0., NeutralPeak = 0.;
    FVector NeutralAimPoint = FVector::ZeroVector;
    FVector NeutralWalkFrom = FVector::ZeroVector, NeutralWalkTo = FVector::ZeroVector;
    TArray<TWeakObjectPtr<AIslandNeutralProp>> NeutralProps;
    FString NeutralExpected;
    TWeakObjectPtr<class ACameraActor> NeutralCamera;
    void TickFieldCheck(double Now);
    bool BeginLockCheck(double Now);
    void PlaceFieldEchoDig(double Now);
    enum class EFieldStep : uint8 { Aim, Strike, InspectDig, WalkToDig, EchoApproach, HearEcho, Gather, Open, Assist, Defend, TakeOver, Seize, PreventReseize, Award, Cancel, Reset, Verify, AimHeldDig, HoldFirst, HoldSecond, Released, HoldThroughReveal, ChargeInterrupted, ChargeIdle, PunchTrials, CoatLeader, CoatHelper };
    void SetFieldStage(EFieldStep Stage, double Now);
    bool bFieldScenario = false, bFieldLidSighted = false;
    int32 FieldEchoTrial = 0, FieldEchoTarget = INDEX_NONE;
    uint32 FieldEchoSequence = 0;
    EFieldStep FieldStage = EFieldStep::Aim;
    FVector FieldStand = FVector::ZeroVector;
    FVector ReferenceFloor = FVector::ZeroVector;
    FVector OriginalFloor = FVector::ZeroVector;
    FVector DigLanding = FVector::ZeroVector;
    FVector SecondFieldTarget = FVector::ZeroVector;
    uint32 HoldSequence = 0;
    int32 FieldPeelInitial = 0, FieldPeelPrevious = INDEX_NONE;
    int32 FieldDrop = 0;
    int32 ReferenceDelta = 0;
    float FieldSoloRate = 0.f;
    int32 ExpeditionPlayers = 0;
    bool bOneCrew = false;
    enum class ERoomCheck : uint8 { Join, Ready, Withdraw, Depart, Dig, Open, Inspect, Return, Stress, Drain, VerifyEdits, Field, Neutral, Mud, Games };
    ERoomCheck RoomCheck = ERoomCheck::Join;
    int32 ExpeditionOpener = 0;
    int32 ChestApproachDigs = 0;
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
    bool TickFieldPunchTrials();
};
