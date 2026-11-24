#include "PartyMinigame.h"
#include "Modules/ModuleManager.h"

IMPLEMENT_MODULE(FDefaultModuleImpl, PartyFramework)

APartySession::APartySession()
{
    bReplicates = true;
    bAlwaysRelevant = true;
}
