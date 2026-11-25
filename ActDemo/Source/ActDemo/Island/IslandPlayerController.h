#pragma once

#include "Island/IslandRules.h"
#include "Island/IslandRoomSettings.h"
#include "Voxel/VoxelPlayerController.h"
#include "IslandPlayerController.generated.h"

class APartySession;
class ULevelStreamingDynamic;
class AIslandChest;
class AIslandNeutralProp;
class USoundWave;
struct FStreamableHandle;

enum class EIslandPartyVeil : uint8 { Clear, Closing, Black, Opening };

struct FIslandPartyTransitionView
{
    EIslandPartyVeil Phase = EIslandPartyVeil::Clear;
    float Coverage = 0.f;
    bool bReturning = false;
    uint32 Round = 0;
};

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
    bool IsVoxelInputEnabled() const override { return !bMapOpen && Super::IsVoxelInputEnabled(); }
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
    FIslandPartyTransitionView GetPartyTransitionView() const;
    bool IsPartyTravelLocked() const { return bPartyTravelLocked; }
    bool IsMoveInputIgnored() const override { return bPartyTravelLocked || Super::IsMoveInputIgnored(); }
    bool IsLookInputIgnored() const override { return bPartyTravelLocked || Super::IsLookInputIgnored(); }
    UFUNCTION(Client, Reliable) void ClientPrepareParty(uint32 RoundId, FName Feature, bool bAnimate);
    UFUNCTION(Client, Reliable) void ClientBeginPartyReturn(uint32 RoundId);
    UFUNCTION(Client, Reliable) void ClientReturnFromParty(uint32 RoundId);
    UFUNCTION(Client, Reliable) void ClientRevealParty(uint32 RoundId, bool bReturning);

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
    UFUNCTION(Server, Reliable) void ServerPartyScreenBlack(uint32 RoundId, bool bReturning);
    UFUNCTION(Server, Reliable) void ServerPartyScreenClear(uint32 RoundId, bool bReturning);
    UFUNCTION(Server, Reliable) void ServerPartyPawnReady(uint32 RoundId);
    UFUNCTION(Server, Reliable) void ServerPartyUnloaded(uint32 RoundId, bool bSuccess);

    void LoadPartyAssets(uint32 RoundId);
    void ReportPartyAssets(uint32 RoundId);
    void UpdatePartyClient();
    void StartPartyVeil(bool bReturning);
    void TickPartyVeil();
    void SetPartyTravelLocked(bool bLocked);
    void FinishPartyUnload(uint32 RoundId, bool bSuccess);
    void ReleasePartyAssets();

    TSoftClassPtr<APartySession> PartySessionClass;
    UPROPERTY(Transient) TObjectPtr<ULevelStreamingDynamic> PartyVenue;
    TSharedPtr<FStreamableHandle> PartyAssets;
    FString PartyPluginURL;
    uint32 LocalPartyRound = 0;
    EIslandPartyVeil PartyVeil = EIslandPartyVeil::Clear;
    double VeilStart = 0.;
    uint64 BlackAtFrame = 0;
    bool bAnimateParty = false;
    bool bPartyTravelLocked = false;
    bool bBlackReported = false;
    bool bRevealRequested = false;
    bool bCanUnloadParty = false;
    bool bPartyActivating = false;
    bool bWaitingForVenue = false;
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
