#pragma once

#include "Island/IslandRules.h"
#include "Island/IslandRoomSettings.h"
#include "Voxel/VoxelPlayerController.h"
#include "IslandPlayerController.generated.h"

class APartySession;
class AIslandChest;
class AIslandNeutralProp;
class USoundWave;
struct FStreamableHandle;

UCLASS()
class AIslandPlayerController : public AVoxelPlayerController
{
    GENERATED_BODY()

public:
    AIslandPlayerController();
    int32 GetTeam() const;
    UFUNCTION(Server, Reliable) void ServerChooseTeam(int32 Team);
    UFUNCTION(Server, Reliable) void ServerSetRoomReady(bool bReady);
    UFUNCTION(Server, Reliable) void ServerStartExpedition();
    UFUNCTION(Server, Reliable) void ServerConfigureRoom(const FIslandRoomSettings& Settings);
    UFUNCTION(Server, Reliable) void ServerReturnToRoom();
    bool IsMapOpen() const { return bMapOpen; }
    const FIslandClues& GetLocalClues() const { return LocalClues; }
    FString WaterStatus() const;
    UFUNCTION(Client, Reliable) void ClientExcavationEcho(bool bStrong);
    UFUNCTION(Client, Reliable) void ClientCrewRally(FVector_NetQuantize Position, bool bAtChest, double ExpiresAt);
    bool GetCrewRally(FVector& Position, bool& bAtChest) const;
    double GetEchoAge() const { return FPlatformTime::Seconds() - LastEchoAt; }
    bool HasStrongEcho() const { return bStrongEcho; }
    bool HasChestClue(const AIslandChest& Chest) const;

    void Interact();
    void RestartIsland();

    UClass* GetPartySessionClass() const;
    UFUNCTION(Client, Reliable) void ClientPrepareParty(uint32 RoundId, FName Feature);
    UFUNCTION(Client, Reliable) void ClientReturnFromParty(uint32 RoundId);

    UFUNCTION(Client, Reliable) void ClientUpdateClues(const FIslandClues& Snapshot);
    UFUNCTION(Client, Reliable) void ClientWaterState(uint8 State);

protected:
    void SetupInputComponent() override;
    void PlayerTick(float DeltaTime) override;
    void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
    friend class AIslandProbePeer;
    void OpenMap();
    void Rally();
    UFUNCTION(Server, Reliable) void ServerRally();
    void CloseMap();
    UFUNCTION(Server, Reliable) void ServerInteract();
    UFUNCTION(Server, Reliable) void ServerRestartIsland();
    UFUNCTION(Server, Reliable) void ServerPartyAssetsReady(uint32 RoundId, bool bSuccess);
    UFUNCTION(Server, Reliable) void ServerPartyPawnReady(uint32 RoundId);
    UFUNCTION(Server, Reliable) void ServerPartyUnloaded(uint32 RoundId, bool bSuccess);

    void LoadPartyAssets(uint32 RoundId);
    void ReportPartyAssets(uint32 RoundId);
    void UpdatePartyClient();
    void FinishPartyUnload(uint32 RoundId, bool bSuccess);
    void ReleasePartyAssets();

    TSoftClassPtr<APartySession> PartySessionClass;
    TSharedPtr<FStreamableHandle> PartyAssets;
    FString PartyPluginURL;
    uint32 LocalPartyRound = 0;
    bool bPartyActivating = false;
    bool bPartyPawnReadySent = false;
    bool bPartyReturning = false;
    bool bPartyUnloadStarted = false;

    FIslandClues LocalClues;
    UPROPERTY() TObjectPtr<USoundWave> EchoSound;
    double LastEchoAt = -1e9;
    bool bStrongEcho = false;
    FVector RallyPosition = FVector::ZeroVector;
    bool bRallyAtChest = false;
    double RallyExpiresAt = 0.;
    double WaterChangedAt = 0.0;
    uint8 WaterState = 0;
    bool bMapOpen = false;
    bool bHelpOpen = false;
    void ToggleMenu();
    void OpenHelp();
    void CloseHelp();
    bool CanUseIslandWorld() const;
public:
    AIslandChest* GetReachableChest() const;
    AIslandNeutralProp* GetReachableSail() const;
    FString GetNeutralHint() const;
    void ClearHeldIslandActions();
    bool ShowControlHelp() const { return bHelpOpen; }

};
