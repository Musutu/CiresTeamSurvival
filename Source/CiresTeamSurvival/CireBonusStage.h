#pragma once
// bonus-loot (playtest 6): Bonus Loot Stages. A stage REPLACES a non-boss wave (CireWaveDirector::RollBonusStage,
// Waves.json bonusWave.replaceChance): its treasure creatures run the route to the castle, never attack, bolt from
// champions and escape escapeSeconds after their first hit (CireMonsterExpansion). The stage rolls one loot tier
// (LootTables.json bonusStage): Low / Mid / Rare. When a lane's hoard is all caught or escaped, every player of that
// lane rolls his personal reward in the tier and gets a chest only he can see. Docs/Items.md.
#include "CoreMinimal.h"
#include "Rules/CireItemRules.h"

class ACireGameMode;
class ACireHero;
class ACireMonster;
struct FCireWaveDef;
struct FCireBonusStageRules;

enum class ECireBonusTier : uint8 { None, Low, Mid, Rare };
enum class ECireBonusOutcome : uint8 { None, Gold, Components, Consumables, ShopItem, PvPUnique, Chests };

namespace CireBonusStage
{
    CIRESTEAMSURVIVAL_API const TCHAR* TierName(ECireBonusTier Tier);        // "Low", "Mid", "Rare"
    CIRESTEAMSURVIVAL_API const TCHAR* OutcomeName(ECireBonusOutcome Outcome);
    /** Deterministic tier roll by the rules' weights. */
    CIRESTEAMSURVIVAL_API ECireBonusTier RollTier(const FCireBonusStageRules& Rules, uint64 Seed);
    /** One player's outcome inside a tier (Rare is always chests). */
    CIRESTEAMSURVIVAL_API ECireBonusOutcome RollOutcome(const FCireBonusStageRules& Rules, ECireBonusTier Tier, uint64 Seed);
    /** Gold one player would have earned from the wave's kills (the wave's "value"). */
    CIRESTEAMSURVIVAL_API int32 WaveGoldValue(const FCireWaveDef& Wave, int32 GlobalWave);
    /** One player's reward: one bundle per chest (a single chest except Rare). Caught = share of the lane's hoard
     *  caught (0..1); Hero (optional) avoids PvP uniques he already owns. Empty when nothing was caught. */
    CIRESTEAMSURVIVAL_API TArray<Cires::Items::LootBundle> BuildReward(const FCireBonusStageRules& Rules, ECireBonusTier Tier,
        ECireBonusOutcome Outcome, int32 WaveValue, float Caught, uint64 Seed, const ACireHero* Hero = nullptr);
    /** Item pools the tiers draw from (catalog driven). */
    CIRESTEAMSURVIVAL_API TArray<FName> ComponentPool();
    CIRESTEAMSURVIVAL_API TArray<FName> ConsumablePool();
    CIRESTEAMSURVIVAL_API TArray<FName> ShopItemPool(int32 MinCost, int32 MaxCost);

    // ---- server runtime (wave director / loot / monster-expansion hooks) ----
    /** A stage starts (it replaced wave WaveNumber). ForcedTier 1..3 skips the tier roll. */
    CIRESTEAMSURVIVAL_API void Begin(ACireGameMode* Mode, int32 WaveNumber, int32 WaveValue, int32 CreaturesPerLane, int32 ForcedTier, const FString& ReplacedLabel);
    /** The wave announcement for the running stage ("BONUS LOOT STAGE | MID TIER | ..."). */
    CIRESTEAMSURVIVAL_API FString Announcement(ACireGameMode* Mode, int32 WaveNumber, int32 WaveInCycle, int32 WavesPerCycle);
    /** A bonus creature spawned (CireMonsterExpansion::ApplyBonus): it joins the newest open stage of the world. */
    CIRESTEAMSURVIVAL_API void Register(ACireMonster* Monster);
    /** A bonus creature died. True when it belongs to a stage (the stage pays; no per-creature chest). */
    CIRESTEAMSURVIVAL_API bool OnCaught(ACireMonster* Monster, ACireHero* Killer);
    /** Pays every lane whose hoard is finished (caught or escaped). Called from the wave director's tick. */
    CIRESTEAMSURVIVAL_API void Tick(ACireGameMode* Mode);
    /** Client: the tier of the stage the replicated wave announcement names (None when no stage is announced). */
    CIRESTEAMSURVIVAL_API ECireBonusTier AnnouncedTier(const UWorld* World);

    struct FStageInfo
    {
        int32 Id = 0, WaveNumber = 0, WaveValue = 0, Expected = 0;
        ECireBonusTier Tier = ECireBonusTier::None;
        int32 Spawned[2] = {0, 0}, Caught[2] = {0, 0};
        bool bPaid[2] = {false, false};
        int32 Chests[2] = {0, 0}; // personal chests (or auto-loots) handed out per lane
    };
    /** Newest stage of the world (tests, F8 readout). */
    CIRESTEAMSURVIVAL_API bool LatestStage(const UWorld* World, FStageInfo& Out);
    CIRESTEAMSURVIVAL_API int32 StageCount(const UWorld* World);
#if !UE_BUILD_SHIPPING
    CIRESTEAMSURVIVAL_API bool RunSmoke(ACireGameMode* Mode);
#endif
}
