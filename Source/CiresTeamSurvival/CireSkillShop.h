#pragma once
// progression-shop: the Skill Shop (Eric's playtest-2 ruling). After every cleared wave (the
// breather), during prep and recovery, a champion buys new skills from its class list or levels
// owned ones (no cap). Replaces level-up skill offers; the free opening role pick stays.
// Data: Content/Data/SkillShop.json (+ LootTables.json economy). Rules: Rules/CireItemRules.*.
// The skill list comes from a thin adapter over the current role-tagged skill pool; swap
// CatalogFor/Scaling to CireAbilityDB when the champion-draft Ability Database lands.
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
};

struct CIRESTEAMSURVIVAL_API FCireSkillShopData
{
    Cires::Items::SkillShopRules Rules;
    bool bBreather = true, bPrep = true, bRecovery = true, bAutoOpen = true;
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

    // ---- Ability DB adapter ----
    CIRESTEAMSURVIVAL_API TArray<FCireShopSkill> CatalogFor(const ACireHero* Hero);
    CIRESTEAMSURVIVAL_API Cires::Items::ShopSkillKind KindOf(const FString& Id);
    CIRESTEAMSURVIVAL_API FString RoleTags(const FString& Id);  // "TANK / DPS"

    // ---- game mode (replicated on ACireGameState::ProgressionMode) ----
    // "Skill Shop" (default): skills are bought/levelled in the shop after every cleared wave and
    // level-ups only raise stats. "Classic Draft": level-up skill offers as before.
    CIRESTEAMSURVIVAL_API bool IsSkillShopMode(const UWorld* World);
    CIRESTEAMSURVIVAL_API void InitializeMode(ACireGameMode* Mode);          // command line -CireMode=SkillShop|Classic
    // Server: switch before the first wave (host / standalone). Returns false when not allowed.
    CIRESTEAMSURVIVAL_API bool SetMode(ACireGameMode* Mode, bool bSkillShop, FString* Why = nullptr);
    CIRESTEAMSURVIVAL_API FString ModeName(bool bSkillShop);

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

    // ---- per-level scaling hooks ----
    CIRESTEAMSURVIVAL_API float EffectScale(const ACireHero* Source, const FString& AbilityName);
    // Server: after a cast starts a cooldown, trims it and charges the extra resource cost.
    CIRESTEAMSURVIVAL_API void TickCastScaling(ACireHero* Hero);
}
