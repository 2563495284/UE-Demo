#pragma once

#include "GameFeatureData.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"
#include "PartyMinigame.generated.h"

class AHUD;

UCLASS(Abstract)
class PARTYFRAMEWORK_API APartyPawn : public APawn
{
    GENERATED_BODY()

public:
    virtual bool HasLocalPartyInput() const { return false; }
    virtual void ReleaseLocalPartyInput() {}
    virtual void DrawPartyHUD(AHUD& HUD, const FVector2D& ViewportSize) const {}

};

UCLASS(Abstract)
class PARTYFRAMEWORK_API APartySession : public AActor
{
    GENERATED_BODY()

public:
    APartySession();

    virtual APartyPawn* CreateParticipant(int32 Seat) { return nullptr; }
    virtual void RemoveParticipant(int32 Seat) {}
    virtual void StartParty() {}

    FSimpleDelegate OnPartyFinished;
};

UCLASS()
class PARTYFRAMEWORK_API UPartyMinigameFeatureData : public UGameFeatureData
{
    GENERATED_BODY()

public:
    UPROPERTY(EditAnywhere, Category = "Party")
    TSoftClassPtr<APartySession> SessionClass;
};
