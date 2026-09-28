#pragma once
// ability-expansion: the data-driven ability pool expansion (Docs/Abilities.md "Expansion pool").
//
// Every row lives in Content/Data/AbilitiesExpansion.json (written by Tools/BuildAbilityExpansion.py) and is merged
// additively into the Ability Database by CireAbilityDB::Reload (same row format as Abilities.json, plus a "recipe"
// block). This module reads the recipes and holds the generic delivery code: bolts, piercing shots, lines, cones,
// circles, lingering zones, novas, chains, strikes, leaps, dashes, heals, heal zones, barriers, self / party buffs,
// summons, constructs (appended to CireTechConstructs), walls, barrages (ultimates) and passives.
//
// Same contract as CireSignatureSkills / CireKitSkills (which route here): every number (cost, cooldown, effect, range,
// radius, duration, crowd control) comes from the Ability Database; damage / healing / barrier / summon hits scale off
// the caster's PRIMARY stat (Ability DB "scaling"), utility magnitudes through potency; the level-15 bonus and aura are
// the generic CireKits ones. Server-authoritative; clients see replicated areas, skillshots, buffs, constructs,
// summons and combat events. Telegraphs come from DescribeShape (the true hit shape) and the per-ability Fab Niagara
// overlay (Content/Data/FabVFX.expansion.json).
#include "CoreMinimal.h"

class AActor;
class ACireHero;
class ACireMonster;
class ACireGameMode;
struct FCireHitShape;
struct FCireTechRecipe;

namespace CireAbilityExpansion
{
    /** Any expansion ability (active, passive, ultimate). */
    CIRESTEAMSURVIVAL_API bool Knows(const FString& Id);
    /** Castable actives and ultimates. */
    CIRESTEAMSURVIVAL_API bool Handles(const FString& Id);
    CIRESTEAMSURVIVAL_API bool IsPassive(const FString& Id);
    CIRESTEAMSURVIVAL_API const TArray<FString>& AllIds();
    /** The delivery recipe name of an id ("bolt", "zone", "summon"...), empty when unknown. */
    CIRESTEAMSURVIVAL_API FString Delivery(const FString& Id);
    CIRESTEAMSURVIVAL_API bool Cast(ACireHero* Hero, int32 Slot, const FString& Id);
    CIRESTEAMSURVIVAL_API bool DescribeShape(const FString& Id, FCireHitShape& Out);
    /** Construct recipes of the expansion (called once by CireTechConstructs when it builds its table). */
    CIRESTEAMSURVIVAL_API void AppendConstructRecipes(TArray<FCireTechRecipe>& Out);

    // ---- combat hooks (server; called from CireSignatureSkills) ----
    /** Self / party buffs, exposed marks, passives (executioner, opportunist, thick skin, summoner's bond). */
    CIRESTEAMSURVIVAL_API float ModifyOutgoingDamage(AActor* Source, AActor* Target, float Amount, const FString& AbilityName);
    /** Root / taunt / weaken / mark / knockback / bleed / lifesteal riders of expansion skills, and on-hit passives. */
    CIRESTEAMSURVIVAL_API void OnAbilityHit(AActor* Source, AActor* Target, const FString& AbilityName, float Applied);
    /** Kill passives (mana font, soul harvest). */
    CIRESTEAMSURVIVAL_API void OnMonsterKilled(ACireMonster* Monster, ACireHero* Killer);
    CIRESTEAMSURVIVAL_API float MoveSpeedMultiplier(const ACireHero* Hero);
    CIRESTEAMSURVIVAL_API float AttackSpeedBonus(const ACireHero* Hero);
    /** Bot rotation: false while casting this now would be wasted (heal with nobody hurt, buff with no enemy near...). */
    CIRESTEAMSURVIVAL_API bool BotWantsCast(ACireHero* Hero, const FString& Id);
    /** Buff record ids applied by these skills (BuffVisuals.json / Ability DB buffModifiers rows exist for each). */
    CIRESTEAMSURVIVAL_API const TArray<FName>& BuffIds();
    /** Re-read the recipes (tests / live data edits; the Ability DB rows reload with CireAbilityDB::Reload). */
    CIRESTEAMSURVIVAL_API void Reload();
#if !UE_BUILD_SHIPPING
    CIRESTEAMSURVIVAL_API bool RunSmoke(ACireGameMode* Mode);
#endif
}
