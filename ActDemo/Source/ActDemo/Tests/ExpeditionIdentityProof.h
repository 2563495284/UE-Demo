#pragma once

#include "CoreMinimal.h"

class AIslandGameMode;
class AVoxelPlayerController;

// The rendered Games QA host calls this between a real chest settlement and the next dig.
bool CaptureExpeditionIdentity(AIslandGameMode& Mode, const TArray<TObjectPtr<AVoxelPlayerController>>& Players);
