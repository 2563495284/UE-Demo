#include "Combat/ActCombatTypes.h"
namespace ActTags
{
 UE_DEFINE_GAMEPLAY_TAG(Light, "Ability.Attack.Light");
 UE_DEFINE_GAMEPLAY_TAG(Heavy, "Ability.Attack.Heavy");
 UE_DEFINE_GAMEPLAY_TAG(Dodge, "Ability.Dodge");
 UE_DEFINE_GAMEPLAY_TAG(Heal, "Ability.Heal");
 UE_DEFINE_GAMEPLAY_TAG(Busy, "State.Busy");
 UE_DEFINE_GAMEPLAY_TAG(Invulnerable, "State.Invulnerable");
 UE_DEFINE_GAMEPLAY_TAG(Dead, "State.Dead");
 UE_DEFINE_GAMEPLAY_TAG(Magnitude, "Data.Magnitude");
}
UActCombatConfig::UActCombatConfig()
{
 LightAttacks.SetNum(3);
 LightAttacks[1].Damage = 30.f;
 LightAttacks[2].Damage = 38.f;
 LightAttacks[2].Duration = 1.f;
 HeavyAttack.Damage = 65.f;
 HeavyAttack.StaminaCost = 36.f;
 HeavyAttack.Duration = 1.5f;
 HeavyAttack.ActiveStart = 0.7f;
 HeavyAttack.ActiveEnd = 0.95f;
 HeavyAttack.Reach = 220.f;
}
