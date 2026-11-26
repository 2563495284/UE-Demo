#pragma once

#include "PartyVoxelArena.h"
#include "ReefRunSession.generated.h"

UCLASS()
class AReefRunSession final : public APartyVoxelSession
{
    GENERATED_BODY()

public:
    AReefRunSession();

private:
    void BuildArena() override;
    void OnArenaStep(float DeltaSeconds) override;
};
