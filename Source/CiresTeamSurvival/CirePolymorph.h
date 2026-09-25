#pragma once
// progression-shop: Polymorph (WoW "Polymorph: Sheep" style). The target becomes a harmless critter
// (Chicken, Piglet or Frog, picked at random per cast) for the ability's duration: it cannot attack
// or cast, forgets its threat table and wanders slowly. Any damage breaks it. Lane bosses and Pack
// Leaders are immune; elites get half the duration; champions (PvP) get at most 3 s with the crowd-
// control diminishing returns. State = the replicated "polymorphed" buff record (CireBuffs); its
// Stacks field carries the critter (1..3), so every client draws the same critter. Ability data:
// Abilities.json "polymorph" (Tools/BuildAbilityDB.py). Critter art: Art/Creatures/Free/PROVENANCE_Critters.md.
#include "CoreMinimal.h"

class AActor;
class ACireHero;
class ACireMonster;
class ACireGameMode;

namespace CirePolymorph
{
    enum class ECritter : uint8 { Chicken = 0, Piglet = 1, Frog = 2, Count = 3 };
    constexpr float ChampionMaxSeconds = 3.f;
    constexpr float WanderSpeed = 110.f;

    CIRESTEAMSURVIVAL_API FName BuffId();
    CIRESTEAMSURVIVAL_API bool IsPolymorphed(const AActor* Unit);
    CIRESTEAMSURVIVAL_API int32 CritterOf(const AActor* Unit);        // -1 when not polymorphed
    CIRESTEAMSURVIVAL_API FString CritterName(int32 Critter);
    CIRESTEAMSURVIVAL_API bool IsImmune(const AActor* Unit);           // lane bosses, Pack Leaders
    // Server. Returns the applied duration (0 = immune/invalid). Critter < 0 picks one at random.
    CIRESTEAMSURVIVAL_API float Apply(AActor* Target, float Seconds, AActor* Source, int32 Critter = -1);
    // Server: ends it (damage, cleanse). Returns true when the unit was polymorphed.
    CIRESTEAMSURVIVAL_API bool Break(AActor* Unit);
    // Server, from ACireMonster::Tick: wander + no AI while polymorphed. True = skip the AI this frame.
    CIRESTEAMSURVIVAL_API bool TickMonster(ACireMonster* Monster, float DeltaSeconds);
    // Every machine with a view: swaps the body for the critter mesh while polymorphed.
    CIRESTEAMSURVIVAL_API void TickVisual(AActor* Unit);
    CIRESTEAMSURVIVAL_API bool HasCritterVisual(const AActor* Unit);
    // The "polymorph" skill (Hero::Cast route after the cast time).
    CIRESTEAMSURVIVAL_API bool Handles(const FString& Id);
    CIRESTEAMSURVIVAL_API bool CastSkill(ACireHero* Hero, int32 Slot, const FString& Id);
#if !UE_BUILD_SHIPPING
    CIRESTEAMSURVIVAL_API bool RunSmoke(ACireGameMode* Mode);
#endif
}
