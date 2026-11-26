#pragma once

class UWorld;

namespace ExpeditionDaylight
{
    enum class ELocation { Harbour, Island };
    void Apply(UWorld& World, ELocation Location);
}
