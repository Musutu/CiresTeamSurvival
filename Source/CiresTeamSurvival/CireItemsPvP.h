#pragma once
// bonus-loot: PvP uniques (Content/Data/PvPUniques.json). Ten completed, mid-value items the shop never sells: they drop
// only from a Mid-tier Bonus Loot Stage (CireBonusStage) and each carries one effect that works ONLY against enemy
// champions (the arenas). They use the LoL-style item model (Items.json schema: stats + unique + effect line); their
// catalog entries are merged into CireItems at load (purchasable false), their PvP effects live here. Docs/Items.md.
#include "CoreMinimal.h"

class ACireHero;
class UCireInventory;
struct FCireItemData;

enum class ECirePvPEffect : uint8
{
    None,
    ChampionDamage,    // +Amount % damage to enemy champions
    ChampionGuard,     // -Amount % damage taken from enemy champions
    MortalWounds,      // hits on champions cut their healing received by Amount % for Duration s
    SilenceStrike,     // your first hit on a champion every Cooldown s silences it for Duration s (no lifesteal on items:
                       // Eric's universal-scaling ruling)
    Execute,           // +Amount % damage to champions below Threshold % health
    Spellbreaker,      // the first champion ability hit every Cooldown s is reduced by Amount %
    LastStand,         // a champion hit that drops you below Threshold % health grants a barrier of Amount % max health
                       // for Duration s (Cooldown s)
    Vengeance,         // reflect Amount % of the damage champions deal to you
    Frostbite,         // hits on a champion slow it for Duration s (once per Cooldown s per target)
    Pursuit            // damaging a champion grants +Amount % move speed for Duration s
};

struct FCirePvPEffectDef
{
    ECirePvPEffect Kind = ECirePvPEffect::None;
    float Amount = 0, Threshold = 0, Duration = 0, Cooldown = 0;
    FString Text;   // tooltip / Docs line
};

namespace CireItemsPvP
{
    /** Merges PvPUniques.json into the loaded catalog (CireItems::Reload). False + Error when the file is invalid;
     *  a missing file is not an error (no PvP uniques). */
    CIRESTEAMSURVIVAL_API bool MergeInto(FCireItemData& Data, FString& Error);
    /** Parses the file text (tests). */
    CIRESTEAMSURVIVAL_API bool MergeJson(const FString& Json, FCireItemData& Data, FString& Error);
    /** Every PvP unique id, in file order. */
    CIRESTEAMSURVIVAL_API const TArray<FName>& Ids();
    CIRESTEAMSURVIVAL_API bool IsPvPUnique(FName ItemId);
    CIRESTEAMSURVIVAL_API const FCirePvPEffectDef* EffectOf(FName ItemId);
    CIRESTEAMSURVIVAL_API ECirePvPEffect ParseKind(const FString& Key);
    /** Champion vs champion: both are drafted champions (not summons) and hostile to each other. */
    CIRESTEAMSURVIVAL_API bool IsChampionDuel(const AActor* Source, const AActor* Target);

    // ---- combat hooks (called from CireItems' hooks; all no-ops outside champion-vs-champion damage) ----
    CIRESTEAMSURVIVAL_API float ModifyOutgoing(ACireHero* Hero, AActor* Target, float Amount, const FString& AbilityName);
    CIRESTEAMSURVIVAL_API void OnDamageDealt(ACireHero* Hero, AActor* Target, float Applied, const FString& AbilityName);
    CIRESTEAMSURVIVAL_API float ModifyIncoming(ACireHero* Hero, AActor* Causer, const FString& AbilityName, float Amount);
    CIRESTEAMSURVIVAL_API void OnHeroDamaged(ACireHero* Hero, AActor* Causer, const FString& AbilityName, float Taken);
    /** Pursuit: move speed fraction from active PvP buffs (replicated inventory buffs, so clients agree). */
    CIRESTEAMSURVIVAL_API float MoveSpeedBonus(const UCireInventory* Inventory);
}
