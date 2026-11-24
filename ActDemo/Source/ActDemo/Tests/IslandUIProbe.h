#pragma once
#include "CoreMinimal.h"
class AIslandPlayerController;
class SIslandRoomPanel;
#if !UE_BUILD_SHIPPING
void TickIslandUIProbe(AIslandPlayerController& PC, const TSharedRef<SIslandRoomPanel>& Panel);
#endif
