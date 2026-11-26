#pragma once

#include "PartyVoxelArena.h"
#include "MazeChaseSession.generated.h"

UCLASS()
class AMazeChaseSession final : public APartyVoxelSession
{
    GENERATED_BODY()
public:
    AMazeChaseSession();
    void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Props) const override;
    void SetParticipantTeam(int32 Seat, int32 Team) override;
    void StartParty() override;
    void RemoveParticipant(int32 Seat) override;
    bool IsChaseGame() const override { return true; }
    bool IsRunner(int32 Seat) const override;
    bool IsCaught(int32 Seat) const override;
    int32 GetRunnerTeam() const override { return RunnerTeam; }
    int32 GetRunnerCount() const override;
    int32 GetRemainingRunners() const override;
    float GetHeadStartRemaining() const override;
    int32 GetWinningTeamMask() const override { return WinningTeams; }
    int32 GetWinningSeat() const override { return INDEX_NONE; }
    EPartyChaseAim GetChaseAim(const APartyVoxelPawn& Pawn, FVector From, FVector Direction) const override;
private:
    void BuildArena() override;
    float PreparationSeconds() const override { return 8.f; }
    void OnArenaStarted() override;
    void OnArenaStep(float DeltaSeconds) override;
    void CheckParticipantBounds(APartyVoxelPawn& Pawn) override;
    bool UseTerrainTool(APartyVoxelPawn& Pawn, bool bPlace, FVector Direction) override;

    void AssignSides(const TArray<int32>& Seats);
    void DefineBerths(const TArray<int32>& Seats, bool bMove);
    void CheckOutcome();
    UStaticMeshComponent* MakeBlock(FName Tag, const FBox& LocalBounds, UMaterialInterface* Paint, bool bSolid=true);
    void BuildWall(FIntPoint Room, bool bBetweenColumns);
    bool WallIsOccupied(int32 WallIndex) const;
    EPartyChaseAim FindTarget(const APartyVoxelPawn& Pawn, FVector From, FVector Direction, int32& WallIndex, int32& TargetSeat, bool bCheckGaps=true) const;
    UFUNCTION() void OnRep_OpenWalls();

    UPROPERTY() TObjectPtr<UStaticMesh> Cube;
    UPROPERTY() TObjectPtr<UStaticMesh> HewnWall;
    UPROPERTY() TObjectPtr<UMaterialInterface> BasePaint;
    UPROPERTY() TArray<TObjectPtr<UMaterialInstanceDynamic>> Paints;
    UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> Walls;
    TArray<FBox> LocalWallBounds;
    int32 NextPartId = 0;
    TMap<int32,int32> SeatTeams;
    TMap<int32,FVector> Berths;
    TMap<int32,FVector> LastGround;
    TMap<int32,double> NextToolTime;
    TMap<int32,double> RefillAfter;
    UPROPERTY(Replicated) int32 RunnerTeam = INDEX_NONE;
    UPROPERTY(Replicated) int32 RunnerSeats = 0;
    UPROPERTY(Replicated) int32 ActiveSeats = 0;
    UPROPERTY(Replicated) int32 CaughtSeats = 0;
    UPROPERTY(Replicated) int32 WinningTeams = 0;
    UPROPERTY(Replicated) double HuntersReleasedAt = 0.;
    UPROPERTY(ReplicatedUsing=OnRep_OpenWalls) TArray<uint8> OpenWalls;
    bool bReleasedHunters = false;
};
