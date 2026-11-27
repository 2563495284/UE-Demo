#pragma once

#include "CoreMinimal.h"

// Accessible before a Game Feature is loaded. MovieFile is relative to Content/Movies.
struct FPartyBriefing
{
    FString Title;
    FString Rules;
    FString Controls;
    FString Goal;
    FString Result;
    FString MovieFile;
};

namespace PartyBriefings
{
    PARTYFRAMEWORK_API TOptional<FPartyBriefing> Find(FName Feature);
}
