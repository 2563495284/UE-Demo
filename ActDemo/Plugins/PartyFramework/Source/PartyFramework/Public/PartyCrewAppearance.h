#pragma once

#include "UObject/Interface.h"
#include "PartyCrewAppearance.generated.h"

class UStaticMesh;
class USkeletalMesh;

UINTERFACE(MinimalAPI)
class UPartyCrewAppearance : public UInterface
{
    GENERATED_BODY()
};

// The island owns its player models and disguise rules. Game Features obtain the
// visible model from the same replicated player state as the expedition pawn.
class PARTYFRAMEWORK_API IPartyCrewAppearance
{
    GENERATED_BODY()
public:
    virtual UStaticMesh* GetPartyFirstPersonFist() const = 0;
    virtual FVector GetCrewBadgeAnchor() const = 0;
    virtual USkeletalMesh* GetPartyCharacterRig() const = 0;
    virtual UStaticMesh* GetPartyCharacterMesh(bool& bLongPaws) const = 0;
};
