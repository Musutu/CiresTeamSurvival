// bonus-loot: PvP uniques. See CireItemsPvP.h and Docs/Items.md ("PvP uniques").
#include "CireItemsPvP.h"
#include "CireBuffs.h"
#include "CireCombatEvents.h"
#include "CireCrowdControl.h"
#include "CireGame.h"
#include "CireItems.h"
#include "CireSummon.h"
#include "Dom/JsonObject.h"
#include "Engine/World.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

DEFINE_LOG_CATEGORY_STATIC(LogCireItemsPvP, Log, All);

namespace
{
TArray<FName> GPvPIds;
TMap<FName, FCirePvPEffectDef> GPvPEffects;

// Per-champion server state for the cooldown effects.
struct FPvPState
{
    float SpellbreakerReadyAt = 0, LastStandReadyAt = 0, SilenceReadyAt = 0;
    TMap<TWeakObjectPtr<AActor>, float> FrostbiteReadyAt;
};
TMap<TWeakObjectPtr<const ACireHero>, FPvPState> GPvPStates;

const TCHAR* const VengeanceName = TEXT("Vengeance Sigil");

float PvPNow(const AActor* Actor) { return Actor ? CireBuffs::ServerNow(Actor->GetWorld()) : 0.f; }

bool IsChampion(const AActor* Actor)
{
    const auto* Hero = Cast<ACireHero>(Actor);
    return Hero && !Hero->IsA<ACireSummon>() && Hero->bDrafted;
}

// Effects the champion carries (a PvP unique is unique, so each kind appears at most once per item; two different
// items never share a kind in the shipped table, and the strongest wins if a data edit makes them).
template <typename Fn>
void ForEachEffect(const ACireHero* Hero, Fn&& Visit)
{
    const UCireInventory* Inventory = CireItems::InventoryOf(Hero);
    if (!Inventory) return;
    for (const FCireItemSlot& Slot : Inventory->Equipment)
        if (const FCirePvPEffectDef* Def = GPvPEffects.Find(Slot.Id)) Visit(*Def, Slot.Id);
}

const FCirePvPEffectDef* Strongest(const ACireHero* Hero, ECirePvPEffect Kind, FName* OutItem = nullptr)
{
    const FCirePvPEffectDef* Best = nullptr;
    ForEachEffect(Hero, [&](const FCirePvPEffectDef& Def, FName Id)
    {
        if (Def.Kind == Kind && (!Best || Def.Amount > Best->Amount)) { Best = &Def; if (OutItem) *OutItem = Id; }
    });
    return Best;
}

float HealthFraction(const AActor* Actor)
{
    const auto* Hero = Cast<ACireHero>(Actor);
    return Hero && Hero->MaxHealth > 0 ? Hero->Health / Hero->MaxHealth : 1.f;
}

bool IsBasic(const AActor* Source, const FString& AbilityName) { return CireItems::IsBasicAttack(Source, AbilityName); }
} // namespace

// ------------------------------------------------------------------ data
ECirePvPEffect CireItemsPvP::ParseKind(const FString& Key)
{
    static const TMap<FString, ECirePvPEffect> Kinds = {
        {TEXT("championDamage"), ECirePvPEffect::ChampionDamage}, {TEXT("championGuard"), ECirePvPEffect::ChampionGuard},
        {TEXT("mortalWounds"), ECirePvPEffect::MortalWounds}, {TEXT("silenceStrike"), ECirePvPEffect::SilenceStrike},
        {TEXT("execute"), ECirePvPEffect::Execute}, {TEXT("spellbreaker"), ECirePvPEffect::Spellbreaker},
        {TEXT("lastStand"), ECirePvPEffect::LastStand}, {TEXT("vengeance"), ECirePvPEffect::Vengeance},
        {TEXT("frostbite"), ECirePvPEffect::Frostbite}, {TEXT("pursuit"), ECirePvPEffect::Pursuit}};
    const ECirePvPEffect* Found = Kinds.Find(Key);
    return Found ? *Found : ECirePvPEffect::None;
}

bool CireItemsPvP::MergeJson(const FString& Json, FCireItemData& Data, FString& Error)
{
    // The item entries use the Items.json schema, so the catalog parser reads them; the "pvp" blocks are ours.
    FCireItemData Parsed;
    if (!CireItems::ParseJson(Json, Parsed, Error)) { Error = TEXT("PvPUniques.json: ") + Error; return false; }
    TSharedPtr<FJsonObject> Root;
    FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Root);
    const TArray<TSharedPtr<FJsonValue>>* Items = nullptr;
    if (!Root.IsValid() || !Root->TryGetArrayField(TEXT("items"), Items)) { Error = TEXT("PvPUniques.json has no items"); return false; }
    TArray<FName> Ids;
    TMap<FName, FCirePvPEffectDef> Effects;
    for (const auto& Value : *Items)
    {
        const TSharedPtr<FJsonObject> Object = Value.IsValid() ? Value->AsObject() : nullptr;
        if (!Object.IsValid()) continue;
        const FName Id(*Object->GetStringField(TEXT("id")));
        const Cires::Items::ItemDef* Item = Parsed.Catalog.Find(TCHAR_TO_UTF8(*Id.ToString()));
        if (!Item || Item->Purchasable || !Item->Unique || Item->Tier != Cires::Items::ItemTier::Legendary)
        { Error = FString::Printf(TEXT("PvPUniques.json: %s must be a unique, non-purchasable legendary item"), *Id.ToString()); return false; }
        if (Data.Catalog.Find(TCHAR_TO_UTF8(*Id.ToString()))) { Error = FString::Printf(TEXT("PvPUniques.json: %s is already in Items.json"), *Id.ToString()); return false; }
        const TSharedPtr<FJsonObject>* Pvp = nullptr;
        if (!Object->TryGetObjectField(TEXT("pvp"), Pvp)) { Error = Id.ToString() + TEXT(" has no pvp effect"); return false; }
        FCirePvPEffectDef Def;
        Def.Kind = ParseKind((*Pvp)->GetStringField(TEXT("kind")));
        if (Def.Kind == ECirePvPEffect::None) { Error = Id.ToString() + TEXT(" has an unknown pvp kind"); return false; }
        double Number = 0;
        if ((*Pvp)->TryGetNumberField(TEXT("amount"), Number)) Def.Amount = FMath::Clamp(static_cast<float>(Number), 0.f, 100.f);
        if ((*Pvp)->TryGetNumberField(TEXT("threshold"), Number)) Def.Threshold = FMath::Clamp(static_cast<float>(Number), 0.f, 100.f);
        if ((*Pvp)->TryGetNumberField(TEXT("duration"), Number)) Def.Duration = FMath::Clamp(static_cast<float>(Number), 0.f, 30.f);
        if ((*Pvp)->TryGetNumberField(TEXT("cooldown"), Number)) Def.Cooldown = FMath::Clamp(static_cast<float>(Number), 0.f, 300.f);
        Def.Text = Object->GetStringField(TEXT("effect"));
        Ids.Add(Id);
        Effects.Add(Id, Def);
    }
    // Merge: catalog entries, shop order (they stay out of the shop: purchasable false), texts.
    // Work on a copy so a bad file leaves the shop catalog untouched.
    Cires::Items::Catalog Catalog = Data.Catalog;
    for (const Cires::Items::ItemDef& Item : Parsed.Catalog.Items) Catalog.Items.push_back(Item);
    const std::string CatalogError = Catalog.Finalize();
    if (!CatalogError.empty()) { Error = TEXT("PvPUniques.json: ") + FString(UTF8_TO_TCHAR(CatalogError.c_str())); return false; }
    const std::string PolicyError = Cires::Items::ValidateStatPolicy(Catalog);
    if (!PolicyError.empty()) { Error = TEXT("PvPUniques.json: ") + FString(UTF8_TO_TCHAR(PolicyError.c_str())); return false; }
    Data.Catalog = MoveTemp(Catalog);
    Data.Order.Append(Parsed.Order);
    Data.EffectLine.Append(Parsed.EffectLine);
    Data.UseText.Append(Parsed.UseText);
    Data.PassiveText.Append(Parsed.PassiveText);
    GPvPIds = MoveTemp(Ids);
    GPvPEffects = MoveTemp(Effects);
    return true;
}

bool CireItemsPvP::MergeInto(FCireItemData& Data, FString& Error)
{
    const FString Path = FPaths::Combine(FPaths::ProjectContentDir(), TEXT("Data/PvPUniques.json"));
    FString Json;
    if (!FFileHelper::LoadFileToString(Json, *Path)) { GPvPIds.Reset(); GPvPEffects.Reset(); return true; }
    if (!MergeJson(Json, Data, Error)) { UE_LOG(LogCireItemsPvP, Error, TEXT("CIRE_PVP_UNIQUES_ERROR %s"), *Error); return false; }
    UE_LOG(LogCireItemsPvP, Display, TEXT("CIRE_PVP_UNIQUES_LOADED items=%d"), GPvPIds.Num());
    return true;
}

const TArray<FName>& CireItemsPvP::Ids() { CireItems::Get(); return GPvPIds; }
bool CireItemsPvP::IsPvPUnique(FName ItemId) { CireItems::Get(); return GPvPEffects.Contains(ItemId); }
const FCirePvPEffectDef* CireItemsPvP::EffectOf(FName ItemId) { CireItems::Get(); return GPvPEffects.Find(ItemId); }

bool CireItemsPvP::IsChampionDuel(const AActor* Source, const AActor* Target)
{
    if (!IsChampion(Source) || !IsChampion(Target)) return false;
    return const_cast<ACireHero*>(Cast<ACireHero>(Source))->IsHostile(const_cast<AActor*>(Target));
}

// ------------------------------------------------------------------ combat hooks
float CireItemsPvP::ModifyOutgoing(ACireHero* Hero, AActor* Target, float Amount, const FString& AbilityName)
{
    if (GPvPEffects.IsEmpty() || !FMath::IsFinite(Amount) || Amount <= 0 || !IsChampionDuel(Hero, Target)) return Amount;
    if (const FCirePvPEffectDef* Def = Strongest(Hero, ECirePvPEffect::ChampionDamage)) Amount *= 1.f + Def->Amount / 100.f;
    if (const FCirePvPEffectDef* Def = Strongest(Hero, ECirePvPEffect::Execute); Def && HealthFraction(Target) * 100.f < Def->Threshold)
        Amount *= 1.f + Def->Amount / 100.f;
    return Amount;
}

void CireItemsPvP::OnDamageDealt(ACireHero* Hero, AActor* Target, float Applied, const FString& AbilityName)
{
    if (GPvPEffects.IsEmpty() || Applied <= 0 || !IsValid(Hero) || Hero->bDead || !IsChampion(Target) || !IsChampion(Hero)) return;
    // Hostility was checked on the way in; the target may already be dead (a killing blow still counts).
    if (Cast<ACireHero>(Target)->TeamId == Hero->TeamId) return;
    const float Now = PvPNow(Hero);
    if (const FCirePvPEffectDef* Def = Strongest(Hero, ECirePvPEffect::SilenceStrike); Def && !Cast<ACireHero>(Target)->bDead)
    {
        float& ReadyAt = GPvPStates.FindOrAdd(Hero).SilenceReadyAt;
        if (Now >= ReadyAt) { CireCrowdControl::Silence(Target, FMath::Max(.25f, Def->Duration), Hero); ReadyAt = Now + Def->Cooldown; }
    }
    if (const FCirePvPEffectDef* Def = Strongest(Hero, ECirePvPEffect::MortalWounds))
        CireCrowdControl::HealCut(Target, Def->Amount / 100.f, FMath::Max(.5f, Def->Duration), Hero);
    if (const FCirePvPEffectDef* Def = Strongest(Hero, ECirePvPEffect::Frostbite))
    {
        float& ReadyAt = GPvPStates.FindOrAdd(Hero).FrostbiteReadyAt.FindOrAdd(Target);
        if (Now >= ReadyAt && !Cast<ACireHero>(Target)->bDead)
        {
            CireCrowdControl::Slow(Target, FMath::Max(.25f, Def->Duration), Hero);
            ReadyAt = Now + Def->Cooldown;
        }
    }
    FName PursuitItem;
    if (const FCirePvPEffectDef* Def = Strongest(Hero, ECirePvPEffect::Pursuit, &PursuitItem))
        if (UCireInventory* Inventory = CireItems::InventoryOf(Hero))
        {
            Inventory->Buffs.RemoveAll([&](const FCireTimedBuff& Buff) { return Buff.Id == PursuitItem || Buff.EndsAt <= Now; });
            FCireTimedBuff Buff; Buff.Id = PursuitItem; Buff.Duration = FMath::Max(.25f, Def->Duration); Buff.EndsAt = Now + Buff.Duration;
            Inventory->Buffs.Add(Buff);
            Inventory->Invalidate();
        }
}

float CireItemsPvP::ModifyIncoming(ACireHero* Hero, AActor* Causer, const FString& AbilityName, float Amount)
{
    if (GPvPEffects.IsEmpty() || !FMath::IsFinite(Amount) || Amount <= 0) return Amount;
    const ACireHero* Attacker = CireItems::OwningHero(Causer);
    if (!Attacker || Attacker == Hero || !IsChampion(Hero) || Attacker->TeamId == Hero->TeamId || !IsChampionDuel(Attacker, Hero)) return Amount;
    if (const FCirePvPEffectDef* Def = Strongest(Hero, ECirePvPEffect::ChampionGuard)) Amount *= 1.f - FMath::Clamp(Def->Amount, 0.f, 90.f) / 100.f;
    if (const FCirePvPEffectDef* Def = Strongest(Hero, ECirePvPEffect::Spellbreaker); Def && !IsBasic(Causer, AbilityName) && AbilityName != VengeanceName)
    {
        FPvPState& State = GPvPStates.FindOrAdd(Hero);
        const float Now = PvPNow(Hero);
        if (Now >= State.SpellbreakerReadyAt)
        {
            Amount *= 1.f - FMath::Clamp(Def->Amount, 0.f, 90.f) / 100.f;
            State.SpellbreakerReadyAt = Now + Def->Cooldown;
            Hero->Notice = TEXT("Spellbreaker Veil: the spell is broken on your ward.");
        }
    }
    return Amount;
}

void CireItemsPvP::OnHeroDamaged(ACireHero* Hero, AActor* Causer, const FString& AbilityName, float Taken)
{
    if (GPvPEffects.IsEmpty() || Taken <= 0 || !IsValid(Hero) || !IsChampion(Hero)) return;
    ACireHero* Attacker = CireItems::OwningHero(Causer);
    if (!Attacker || Attacker == Hero || Attacker->TeamId == Hero->TeamId || !IsChampion(Attacker)) return;
    // Vengeance never answers a reflection (two sigils cannot ping-pong).
    if (const FCirePvPEffectDef* Def = Strongest(Hero, ECirePvPEffect::Vengeance); Def && AbilityName != VengeanceName && !Attacker->bDead)
        CireCombat::ApplyDamage(Hero, Attacker, Taken * Def->Amount / 100.f, VengeanceName);
    if (const FCirePvPEffectDef* Def = Strongest(Hero, ECirePvPEffect::LastStand); Def && !Hero->bDead && Hero->MaxHealth > 0)
    {
        FPvPState& State = GPvPStates.FindOrAdd(Hero);
        const float Now = PvPNow(Hero);
        const float After = Hero->Health / Hero->MaxHealth * 100.f, Before = (Hero->Health + Taken) / Hero->MaxHealth * 100.f;
        if (After < Def->Threshold && Before >= Def->Threshold && Now >= State.LastStandReadyAt)
        {
            CireItems::GrantBarrier(Hero, Hero->MaxHealth * Def->Amount / 100.f, FMath::Max(.5f, Def->Duration), Hero);
            State.LastStandReadyAt = Now + Def->Cooldown;
            Hero->Notice = TEXT("Last Stand: a barrier flares as you fall.");
        }
    }
}

float CireItemsPvP::MoveSpeedBonus(const UCireInventory* Inventory)
{
    if (!Inventory || GPvPEffects.IsEmpty()) return 0.f;
    const double Now = Inventory->Now();
    float Bonus = 0.f;
    for (const FCireTimedBuff& Buff : Inventory->Buffs)
        if (Buff.EndsAt > Now)
            if (const FCirePvPEffectDef* Def = GPvPEffects.Find(Buff.Id); Def && Def->Kind == ECirePvPEffect::Pursuit)
                Bonus = FMath::Max(Bonus, Def->Amount / 100.f);
    return Bonus;
}
