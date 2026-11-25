#pragma once

#include "GameFeatureData.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Character.h"
#include "PartyMinigame.generated.h"

class AHUD;

UCLASS(Abstract)
class PARTYFRAMEWORK_API APartyPawn : public ACharacter
{
    GENERATED_BODY()

public:
    virtual bool HasLocalPartyInput() const { return false; }
    virtual void ReleaseLocalPartyInput() {}
    virtual void SetCrewTint(FLinearColor Color) {}
    virtual void DrawPartyHUD(AHUD& HUD, const FVector2D& ViewportSize, bool bAnonymous) const {}

};

UCLASS(Abstract)
class PARTYFRAMEWORK_API APartySession : public AActor
{
    GENERATED_BODY()

public:
    APartySession();

    virtual APartyPawn* CreateParticipant(int32 Seat) { return nullptr; }
    virtual void SetParticipantTeam(int32 Seat, int32 Team) {}
    virtual void RemoveParticipant(int32 Seat) {}
    virtual void StartParty() {}
    virtual int32 GetWinningSeat() const { return INDEX_NONE; }
    virtual int32 GetWinningTeamMask() const { return 0; }

    FSimpleDelegate OnPartyFinished;
};

UCLASS()
class PARTYFRAMEWORK_API UPartyMinigameFeatureData : public UGameFeatureData
{
    GENERATED_BODY()

public:
    UPROPERTY(EditAnywhere, Category = "Party")
    TSoftClassPtr<APartySession> SessionClass;

    UPROPERTY(EditAnywhere, Category = "Party")
    TSoftObjectPtr<UWorld> ArenaMap;
};
