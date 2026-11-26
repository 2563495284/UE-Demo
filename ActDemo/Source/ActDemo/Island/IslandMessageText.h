#pragma once

#include "Containers/UnrealString.h"

// Localize game and network notices when displayed. User-defined player and room names never pass here.
namespace IslandMessageText
{
    FString Display(const FString& Message);
}
