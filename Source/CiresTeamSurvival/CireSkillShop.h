#pragma once
// progression-shop: the Skill Shop (Eric's playtest-2 ruling). After every cleared wave (the
// breather), during prep and recovery, a champion buys new skills from its class list or levels
// owned ones (no cap). Replaces level-up skill offers; the free opening role pick stays.
// Data: Content/Data/SkillShop.json (+ LootTables.json economy). Rules: Rules/CireItemRules.*.
// The skill list and per-level numbers come from the Ability Database (CireAbilityDB:
// PurchasableSkills per champion profile, EffectiveStats per level); heroes without a profile
// fall back to the role-tagged skill pool. Levels live on UCireInventory::SkillRanks (replicated).
#include "CoreMinimal.h"
#include "Rules/CiresRules.h"
#include "Rules/CireItemRules.h"

class ACireHero;
class ACireGameMode;
class UWorld;

struct CIRESTEAMSURVIVAL_API FCireShopSkill
{
    FString Id;
    FString Name;
    Cires::Items::ShopSkillKind Kind = Cires::Items::ShopSkillKind::Active;
    Cires::RoleMask Roles = Cires::RoleNone;
    FString Types;    // "DPS / TANK" (Ability DB types)
    FString School;   // "Fire", "Holy"... (Ability DB)
};

// A cast's multipliers at the caster's Skill Shop level (1 at level 1).
struct CIRESTEAMSURVIVAL_API FCireCastScale
{
    int32 Level = 1;
    float Effect = 1.f, Cost = 1.f, Cooldown = 1.f;
};

struct CIRESTEAMSURVIVAL_API FCireSkillShopData
{
    Cires::Items::SkillShopRules Rules;
    bool bBreather = true, bPrep = true, bRecovery = true, bAutoOpen = true;
    // Ready to Continue gate (Skill Shop mode): the post-wave shop phase waits for every human.
    bool bReadyGate = true;
    float ReadyMaxSeconds = 180.f;   // safety cap for AFK players; 0 = none
    float BotSkillShare = .6f;
    FString Error;
    bool bValid = false;
};

namespace CireSkillShop
{
    CIRESTEAMSURVIVAL_API const FCireSkillShopData& Get();
    CIRESTEAMSURVIVAL_API FCireSkillShopData& Mutable();      // F8 > Economy live edits
    CIRESTEAMSURVIVAL_API bool Reload();
    CIRESTEAMSURVIVAL_API bool Save(FString* Error = nullptr);
    CIRESTEAMSURVIVAL_API bool ParseJson(const FString& Json, FCireSkillShopData& Out, FString& Error);
    CIRESTEAMSURVIVAL_API FString ToJson(const FCireSkillShopData& Data);

    // ---- Ability DB ----
    CIRESTEAMSURVIVAL_API TArray<FCireShopSkill> CatalogFor(const ACireHero* Hero);
    CIRESTEAMSURVIVAL_API Cires::Items::ShopSkillKind KindOf(const FString& Id);
    CIRESTEAMSURVIVAL_API FString RoleTags(const FString& Id);  // "TANK / DPS"
    CIRESTEAMSURVIVAL_API FString SchoolOf(const FString& Id);

    // ---- game mode (replicated on ACireGameState::ProgressionMode) ----
    // "Skill Shop" (default): skills are bought/levelled in the shop after every cleared wave and
    // level-ups only raise stats. "Classic Draft": level-up skill offers as before.
    CIRESTEAMSURVIVAL_API bool IsSkillShopMode(const UWorld* World);
    CIRESTEAMSURVIVAL_API void InitializeMode(ACireGameMode* Mode);          // command line -CireMode=SkillShop|Classic
    // Server: switch before the first wave (host / standalone). Returns false when not allowed.
    CIRESTEAMSURVIVAL_API bool SetMode(ACireGameMode* Mode, bool bSkillShop, FString* Why = nullptr);
    CIRESTEAMSURVIVAL_API FString ModeName(bool bSkillShop);
    // Server: match the hero's native skill schedule to the mode (Cires::SkillSchedule). Shop skills
    // never block level-ups; switching back to Classic re-derives the next breakpoint.
    CIRESTEAMSURVIVAL_API void SyncSchedule(ACireHero* Hero);
    // Ready gate banner: "WAITING FOR 1 PLAYER" / "WAITING FOR 2 PLAYERS  ·  3 / 5 READY".
    CIRESTEAMSURVIVAL_API FString WaitingLabel(int32 Humans, int32 Ready);

    // ---- state (server and clients) ----
    CIRESTEAMSURVIVAL_API int32 CurrentWave(const UWorld* World);
    CIRESTEAMSURVIVAL_API bool IsOpen(const ACireHero* Hero, FString* Why = nullptr);
    CIRESTEAMSURVIVAL_API bool IsBreather(const UWorld* World);
    CIRESTEAMSURVIVAL_API int32 Level(const ACireHero* Hero, const FString& Id);   // 0 = not owned
    CIRESTEAMSURVIVAL_API int32 OwnedOfKind(const ACireHero* Hero, Cires::Items::ShopSkillKind Kind);
    CIRESTEAMSURVIVAL_API int32 BuyPrice(const ACireHero* Hero, const FString& Id);
    CIRESTEAMSURVIVAL_API int32 LevelPrice(const ACireHero* Hero, const FString& Id);
    // Why a buy would fail right now (empty when it would succeed).
    CIRESTEAMSURVIVAL_API FString BuyBlocker(const ACireHero* Hero, const FString& Id);

    // ---- authoritative operations ----
    CIRESTEAMSURVIVAL_API bool Buy(ACireHero* Hero, const FString& Id, FString& Message);
    CIRESTEAMSURVIVAL_API bool LevelUp(ACireHero* Hero, const FString& Id, FString& Message);
    CIRESTEAMSURVIVAL_API void BotShop(ACireHero* Hero);
    // Server, from the match tick during the breather: true = hold the countdown (not every human is
    // ready and the safety cap has not run out). Clamps WaveTimer to 1 s when everyone is ready or the
    // cap expires. Classic Draft mode and bots-only matches never hold.
    CIRESTEAMSURVIVAL_API bool HoldBreather(ACireGameMode* Mode, float DeltaSeconds, float& WaveTimer);

    // ---- per-level scaling (CireAbilityDB::EffectiveStats(Id, Level) relative to level 1) ----
    CIRESTEAMSURVIVAL_API FCireCastScale CastScale(const ACireHero* Hero, const FString& Id);
    // Damage/healing multiplier for an ability by display name (combat events carry names).
    CIRESTEAMSURVIVAL_API float EffectScale(const ACireHero* Source, const FString& AbilityName);
    // Cast path hook, called right after a cast paid its level-1 cost and started its cooldown:
    // charges the level's extra mana/energy and scales the cooldown. No-op at level 1.
    CIRESTEAMSURVIVAL_API void ApplyCastLevel(ACireHero* Hero, int32 Slot, const FString& Id, float BaseMana, float BaseEnergy);
    // Resource check with the level's cost (casts refuse when the scaled cost is unaffordable).
    CIRESTEAMSURVIVAL_API bool CanPayCast(const ACireHero* Hero, const FString& Id, float BaseMana, float BaseEnergy);
    /** items-v2: the mana/energy a cast really costs (Skill Shop level x champion-level mana scaling). */
    CIRESTEAMSURVIVAL_API void ScaledCost(const ACireHero* Hero, const FString& Id, float BaseMana, float BaseEnergy, float& OutMana, float& OutEnergy);
    /** items-v2: the message for the last failed CanPayCast ("Not enough mana (32 / 48)."). */
    CIRESTEAMSURVIVAL_API FString CostFailText();
#if !UE_BUILD_SHIPPING
    // In-engine checks (CireSkillShopTests.cpp): economy, mode, buy/level, cast scaling, bots, builds.
    CIRESTEAMSURVIVAL_API bool RunSmoke(ACireGameMode* Mode);
#endif
}
