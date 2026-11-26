#pragma once

#include "PartyMinigame.h"
#include "Net/Serialization/FastArraySerializer.h"
#include "PartyVoxelArena.generated.h"

class APartyVoxelSession;
class UCameraComponent;
class UPartyDensityTerrain;
class ULevel;
class UMaterialInterface;
class USpringArmComponent;
class UStaticMesh;
class UStaticMeshComponent;

UENUM()
enum class EPartyBlock : uint8 { Air, Stone, Clay, Sand, Gold };

enum class EPartyChaseAim : uint8 { None, Wall, Gap, Runner };

UENUM()
enum class EPartyVoxelPhase : uint8 { Waiting, Countdown, Playing, Results };

USTRUCT()
struct PARTYFRAMEWORK_API FPartyVoxelStanding
{
    GENERATED_BODY()
    UPROPERTY() int32 Seat = INDEX_NONE;
    UPROPERTY() FString PlayerName;
    UPROPERTY() int32 Points = 0;
};

USTRUCT()
struct FPartyVoxelChange : public FFastArraySerializerItem
{
    GENERATED_BODY()
    UPROPERTY() int32 Sequence = 0;
    UPROPERTY() FIntVector Center = FIntVector::ZeroValue;
    UPROPERTY() FIntVector Normal = FIntVector(0,0,1000);
    UPROPERTY() bool bFill = false;
    UPROPERTY() TArray<FIntVector> ProtectedMin;
    UPROPERTY() TArray<FIntVector> ProtectedMax;
};

USTRUCT()
struct FPartyVoxelChanges : public FFastArraySerializer
{
    GENERATED_BODY()
    UPROPERTY() TArray<FPartyVoxelChange> Items;
    APartyVoxelSession* Arena = nullptr;
    bool NetDeltaSerialize(FNetDeltaSerializeInfo& Info)
    {
        return FFastArraySerializer::FastArrayDeltaSerialize<FPartyVoxelChange,FPartyVoxelChanges>(Items,Info,*this);
    }
    void PostReplicatedAdd(TArrayView<int32 const> Indices, int32 Size);
    void PostReplicatedChange(TArrayView<int32 const> Indices, int32 Size);
};
template<> struct TStructOpsTypeTraits<FPartyVoxelChanges> : TStructOpsTypeTraitsBase2<FPartyVoxelChanges>
{
    enum { WithNetDeltaSerializer = true };
};

USTRUCT()
struct FPartyVoxelBeacon
{
    GENERATED_BODY()
    UPROPERTY() FIntVector Cell = FIntVector::ZeroValue;
    UPROPERTY() FColor Color = FColor::White;
};

UCLASS()
class PARTYFRAMEWORK_API APartyVoxelPawn : public APartyPawn
{
    GENERATED_BODY()
public:
    APartyVoxelPawn();
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Props) const override;
    virtual bool HasLocalPartyInput() const override;
    virtual void ReleaseLocalPartyInput() override;
    void SetCrewTint(FLinearColor Color) override;
    void DrawPartyHUD(AHUD& HUD,const FVector2D& ViewportSize, bool bAnonymous) const override;
    APartyVoxelSession* GetSession() const { return Arena; }
    int32 GetSeat() const { return Seat; }
    void SetLocalLookScale(FVector2D Scale) { LocalLookScale = Scale; }
    void SetLocalFirstPersonView(bool bFirstPerson);
    bool IsInFirstPersonView() const { return bLocalFirstPerson && IsLocallyControlled(); }
    FRotator GetViewRotation() const override;
    void CalcCamera(float DeltaSeconds, FMinimalViewInfo& OutView) override;
    FVector GetToolAimDirection() const;
    bool AimTerrain(FHitResult& Hit) const;
    void UseTool(bool bPlace);
protected:
    virtual void SetupPlayerInputComponent(UInputComponent* Input) override;
    virtual void Tick(float DeltaSeconds) override;
private:
    friend class APartyVoxelSession;
    void UpdatePaws();
    void UpdateCrewModel();
    void FollowSurvivingTeammate(APlayerController& Player);
    UFUNCTION(Server,Reliable) void ServerSetChaseView(bool bFirstPerson);
    UFUNCTION(Server,Reliable) void ServerTool(bool bPlace,FVector_NetQuantizeNormal Direction);
    UPROPERTY(Replicated) TObjectPtr<APartyVoxelSession> Arena;
    UPROPERTY(Replicated) int32 Seat = INDEX_NONE;
    UPROPERTY() TObjectPtr<UCameraComponent> Eye;
    UPROPERTY() TObjectPtr<USpringArmComponent> CameraBoom;
    UPROPERTY() TObjectPtr<UCameraComponent> ShoulderCamera;
    UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> SuitParts;
    UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> Gloves;
    UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> Boots;
    UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> EyePaws;
    UPROPERTY() TObjectPtr<UStaticMeshComponent> Body;
    UPROPERTY() TObjectPtr<UStaticMeshComponent> CrewSculpture;
    UPROPERTY() TObjectPtr<UStaticMeshComponent> CrewBadge;
    UPROPERTY() TObjectPtr<UMaterialInterface> BaseMaterial;
    UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> SuitPaint;
    FLinearColor SuitColor = FLinearColor::Transparent;
    TOptional<FVector> SurfaceAimPoint;
    bool bAimingAtVenue = false;
    bool bInputReady = false;
    bool bLocalFirstPerson = false;
    UPROPERTY(Replicated) bool bObservedFirstPerson = false;
    bool bSentFirstPerson = false;
    bool bSuitHiddenFromOwner = false;
    bool bPlacingStroke = false;
    double ToolStrokeAt = -1.;
    FVector2D LocalLookScale = FVector2D(1., 1.);
    bool bWaitDigRelease = false;
    bool bWaitPlaceRelease = false;
    bool bWaitJumpRelease = false;
    double NextLocalDig = 0.;
    double NextLocalPlace = 0.;
    double NextServerDig = 0.;
    double NextServerPlace = 0.;
};

UCLASS(Abstract)
class PARTYFRAMEWORK_API APartyVoxelSession : public APartySession
{
    GENERATED_BODY()
public:
    APartyVoxelSession();
    static constexpr double StructureReach = 12500.;
    static constexpr float ChaseRunnerSpeed = 570.f;
    static constexpr float ChaseHunterSpeed = 615.f;
    static constexpr double ChaseToolRecovery(bool bPlace) { return bPlace ? .85 : .55; }
    static FVector ArenaOrigin() { return FVector(150000.,150000.,18000.); }
    void SetArenaSeed(int32 Value) { Seed=Value; }
    int32 ArenaSeed() const { return Seed; }
    virtual APartyPawn* CreateParticipant(int32 Seat) override;
    virtual void StartParty() override;
    virtual void RemoveParticipant(int32 Seat) override;
    virtual int32 GetWinningSeat() const override { return Winner; }
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Props) const override;
    EPartyVoxelPhase GetPhase() const { return Phase; }
    virtual bool IsChaseGame() const { return false; }
    virtual bool IsRunner(int32 Seat) const { return false; }
    virtual bool IsCaught(int32 Seat) const { return false; }
    virtual int32 GetRunnerTeam() const { return INDEX_NONE; }
    virtual int32 GetRunnerCount() const { return 0; }
    virtual int32 GetRemainingRunners() const { return 0; }
    virtual float GetHeadStartRemaining() const { return 0.f; }
    virtual EPartyChaseAim GetChaseAim(const APartyVoxelPawn& Pawn, FVector From, FVector Direction) const { return EPartyChaseAim::None; }
    bool HasGrid() const { return bBuilt; }
#if !UE_BUILD_SHIPPING
    uint32 GridDigest() const;
#endif
    float SecondsLeft() const;
    const TArray<FPartyVoxelStanding>& GetStandings() const { return Standings; }
    int32 Score(int32 Seat) const;
    EPartyBlock BlockAt(FIntVector Cell) const;
    float TerrainDensityAt(FVector WorldPoint) const;
    int32 GetAppliedEditCount() const { return AppliedEdits; }
    bool TraceTerrain(FVector From, FVector Direction, const AActor* Ignore, FHitResult& Hit) const;
    FIntVector FeetCell(int32 Seat) const;
    FVector FeetLocation(int32 Seat) const;
    bool IsGrounded(int32 Seat) const;
    FVector CellCenter(FIntVector Cell) const;
    FIntVector CellAt(FVector WorldPoint) const;
    void BindVenueLevel(ULevel& Level);
    int32 GetVenueStructureCount() const { return VenueStructures.Num(); }
    void GetVenueStructureParts(int32 Id, TArray<UStaticMeshComponent*>& Out) const;
    bool IsVenueStructureDestroyed(int32 Id) const { return DestroyedVenueStructures.Contains(Id); }
    int32 TraceVenueStructure(FVector From, FVector Direction, const AActor* Ignore) const;
    UPROPERTY(EditDefaultsOnly) FString Title;
    UPROPERTY(EditDefaultsOnly) FString Objective;
    UPROPERTY(EditDefaultsOnly) FString EnglishTitle;
    UPROPERTY(EditDefaultsOnly) FString EnglishObjective;
    UPROPERTY(EditDefaultsOnly) FColor Theme = FColor(95,179,167);
    UPROPERTY(EditDefaultsOnly) float RoundSeconds = 55.f;
protected:
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;
    virtual void BuildArena() PURE_VIRTUAL(APartyVoxelSession::BuildArena, );
    virtual float PreparationSeconds() const { return 0.f; }
    virtual void OnArenaStarted() {}
    virtual void CheckParticipantBounds(APartyVoxelPawn& Pawn);
    virtual bool UseTerrainTool(APartyVoxelPawn& Pawn,bool bPlace,FVector Direction);
    APartyVoxelPawn* Participant(int32 Seat) const { return Participants.FindRef(Seat).Get(); }
    double Clock() const;
    void Results();
    virtual void OnArenaStep(float DeltaSeconds) {}
    virtual void OnTerrainEdited(int32 Seat, const FBox& WorldBounds, bool bPlaced) {}

    void Block(FIntVector Cell,EPartyBlock Kind);
    void Box(FIntVector Min,FIntVector Max,EPartyBlock Kind);
    void Marker(FIntVector Cell,FColor Color);
    void SetBeacon(FIntVector Cell,FColor Color);
    void SetStart(int32 Seat,FIntVector Feet,float Yaw=0.f);
    TArray<int32> PlayingSeats() const;
    void SetScore(int32 Seat,int32 Points);
    void Finish(int32 Seat);
private:
    friend class APartyVoxelPawn;
    friend struct FPartyVoxelChanges;
    friend class FPartyArenaSupportTest;
    void EnsureGrid();
    void ReplayBrushes(TArrayView<const int32> Indices);
    bool CommitBrush(FPartyVoxelChange& Brush);
    void ProtectParticipants(FPartyVoxelChange& Brush, TMap<APartyVoxelPawn*,FVector>& Lifts) const;
    void ApplyBeacon(FIntVector Cell,FColor Color);
    UFUNCTION() void OnRep_Beacons();
    UFUNCTION() void OnRep_VenueStructures();
    void ApplyVenueDestruction(bool bShowDebris);
    TWeakObjectPtr<ULevel> BoundVenue;
    TArray<TArray<TWeakObjectPtr<UStaticMeshComponent>>> VenueStructures;
    TMap<TWeakObjectPtr<UStaticMeshComponent>, int32> VenuePartIds;
    UPROPERTY(ReplicatedUsing=OnRep_VenueStructures) TArray<int32> DestroyedVenueStructures;
    void Respawn(APartyVoxelPawn& Pawn);
    UPROPERTY(Replicated) int32 Seed = 1;
    UPROPERTY(Replicated) EPartyVoxelPhase Phase = EPartyVoxelPhase::Waiting;
    UPROPERTY(Replicated) int32 Winner = INDEX_NONE;
    UPROPERTY(Replicated) double Deadline = 0.;
    UPROPERTY(Replicated) TArray<FPartyVoxelStanding> Standings;
    UPROPERTY(Replicated) FPartyVoxelChanges Changes;
    UPROPERTY(ReplicatedUsing=OnRep_Beacons) TArray<FPartyVoxelBeacon> Beacons;
    UPROPERTY() TObjectPtr<UPartyDensityTerrain> Terrain;
    UPROPERTY() TObjectPtr<UStaticMesh> BeaconMesh;
    UPROPERTY() TObjectPtr<UMaterialInterface> Material;
    UPROPERTY() TMap<FIntVector,TObjectPtr<UStaticMeshComponent>> BeaconMeshes;
    TMap<FIntVector,FColor> InitialBeacons;
    TMap<FIntVector,EPartyBlock> Blocks;
    TMap<int32,int32> PendingBrushes;
    int32 AppliedEdits = 0;
    TMap<int32,FTransform> Starts;
    TMap<int32,TWeakObjectPtr<APartyVoxelPawn>> Participants;
    TMap<int32,uint64> ScoreFrames;
    TSet<int32> Completed;
    FIntVector MinCell = FIntVector(0);
    FIntVector MaxCell = FIntVector(0);
    bool bBuilt = false;
    bool bBuildStarted = false;
    bool bReturned = false;
#if !UE_BUILD_SHIPPING
    bool bQAViews = false;
    uint32 InitialGridDigest = 0;
    double NextProofAt = 0.;
#endif
};
