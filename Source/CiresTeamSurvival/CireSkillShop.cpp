#include "CireSkillShop.h"
#include "CireThreat.h" // rules-conformance: threatScale hook
#include "CireScalingKits.h" // scaling-kits
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
// progression-shop: see CireSkillShop.h and Docs/Progression.md (Skill Shop).
#include "CireGame.h"
#include "CireItems.h"
#include "CireLoot.h"
#include "CireChampionProfiles.h"
#include "CireAbilityDB.h"
#include "CireWaves.h" // breather / ready-up (wave director)
#include "Dom/JsonObject.h"
#include "Engine/World.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

DEFINE_LOG_CATEGORY_STATIC(LogCireSkillShop, Log, All);

namespace CI = Cires::Items;

namespace
{
FCireSkillShopData ShopData;
bool bShopLoaded = false;

double Num(const TSharedPtr<FJsonObject>& Object, const TCHAR* Key, double Default)
{
    double Value = Default;
    return Object.IsValid() && Object->TryGetNumberField(Key, Value) && FMath::IsFinite(Value) ? Value : Default;
}
FString DataPath() { return FPaths::Combine(FPaths::ProjectContentDir(), TEXT("Data/SkillShop.json")); }
std::string Utf8(const FString& Text) { return std::string(TCHAR_TO_UTF8(*Text)); }

Cires::RoleMask HeroRoles(const ACireHero* Hero)
{
    if (!Hero) return Cires::RoleAll;
    const Cires::SkillDraftRole Primary = CireChampionProfiles::DraftRole(Hero);
    const Cires::RoleMask Mask = static_cast<Cires::RoleMask>(Cires::RoleBit(Primary) | CireChampionProfiles::SecondaryRoles(Hero));
    return Mask == Cires::RoleNone ? Cires::RoleAll : Mask;
}

FCireSkillRank* FindRank(ACireHero* Hero, const FString& Id)
{
    if (!Hero || !Hero->Inventory) return nullptr;
    return Hero->Inventory->SkillRanks.FindByPredicate([&](const FCireSkillRank& R) { return R.Id == Id; });
}

const TCHAR* KindName(CI::ShopSkillKind Kind)
{
    return Kind == CI::ShopSkillKind::Ultimate ? TEXT("ultimate") : Kind == CI::ShopSkillKind::Passive ? TEXT("passive") : TEXT("active");
}

void Feedback(ACireHero* Hero, ECireShopAction Action, bool bOk, const FString& Id, int32 Slot, int32 Gold, const FString& Message)
{
    if (Hero && Hero->Inventory) Hero->Inventory->SendFeedback(Action, bOk, FName(*Id), Slot, false, Gold, Message);
}
} // namespace

// ------------------------------------------------------------------ data
bool CireSkillShop::ParseJson(const FString& Json, FCireSkillShopData& Out, FString& Error)
{
    Out = FCireSkillShopData();
    TSharedPtr<FJsonObject> Root;
    if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Root) || !Root.IsValid()) { Error = TEXT("SkillShop.json is not valid JSON"); return false; }
    if (Num(Root, TEXT("schemaVersion"), 0) != 1) { Error = TEXT("SkillShop.json schemaVersion must be 1"); return false; }
    auto& R = Out.Rules;
    const TSharedPtr<FJsonObject>* Section = nullptr;
    if (Root->TryGetObjectField(TEXT("access"), Section))
    {
        (*Section)->TryGetBoolField(TEXT("breather"), Out.bBreather);
        (*Section)->TryGetBoolField(TEXT("prep"), Out.bPrep);
        (*Section)->TryGetBoolField(TEXT("recovery"), Out.bRecovery);
        (*Section)->TryGetBoolField(TEXT("autoOpenOnWaveClear"), Out.bAutoOpen);
    }
    if (Root->TryGetObjectField(TEXT("readyGate"), Section))
    {
        (*Section)->TryGetBoolField(TEXT("enabled"), Out.bReadyGate);
        Out.ReadyMaxSeconds = FMath::Clamp(static_cast<float>(Num(*Section, TEXT("maxSeconds"), 180)), 0.f, 3600.f);
    }
    if (Root->TryGetObjectField(TEXT("prices"), Section))
    {
        R.ActivePrice = FMath::Clamp(Num(*Section, TEXT("active"), 15), 0.5, 1000.);
        R.PassivePrice = FMath::Clamp(Num(*Section, TEXT("passive"), 30), 0.5, 1000.);
        R.UltimatePrice = FMath::Clamp(Num(*Section, TEXT("ultimate"), 60), 0.5, 1000.);
        R.ActiveOwnedGrowth = FMath::Clamp(Num(*Section, TEXT("activeOwnedGrowth"), .25), 0., 5.);
        R.LevelUpBase = FMath::Clamp(Num(*Section, TEXT("levelUpBase"), 8), .5, 1000.);
        R.LevelUpGrowth = FMath::Clamp(Num(*Section, TEXT("levelUpGrowth"), 1.35), 1., 5.);
    }
    if (Root->TryGetObjectField(TEXT("scaling"), Section))
    {
        R.EffectPerLevel = FMath::Clamp(Num(*Section, TEXT("effectPerLevel"), .08), 0., 1.);
        R.CostPerLevel = FMath::Clamp(Num(*Section, TEXT("costPerLevel"), .05), 0., 1.);
        R.CooldownPerLevel = FMath::Clamp(Num(*Section, TEXT("cooldownPerLevel"), .04), 0., .5);
        R.MinCooldownFactor = FMath::Clamp(Num(*Section, TEXT("minCooldownFactor"), .4), .05, 1.);
    }
    if (Root->TryGetObjectField(TEXT("slots"), Section))
    {
        R.ActiveSlotsStart = FMath::Clamp(static_cast<int>(Num(*Section, TEXT("activeStart"), 2)), 1, 6);
        R.ActiveSlotEveryWaves = FMath::Clamp(static_cast<int>(Num(*Section, TEXT("activeEveryWaves"), 3)), 1, 50);
        R.MaxActive = FMath::Clamp(static_cast<int>(Num(*Section, TEXT("maxActive"), 6)), 1, Cires::MaxActiveSkills);
        R.PassiveFromWave = FMath::Clamp(static_cast<int>(Num(*Section, TEXT("passiveFromWave"), 5)), 0, 500);
        R.UltimateFromWave = FMath::Clamp(static_cast<int>(Num(*Section, TEXT("ultimateFromWave"), 10)), 0, 500);
    }
    if (Root->TryGetObjectField(TEXT("bots"), Section)) Out.BotSkillShare = FMath::Clamp(static_cast<float>(Num(*Section, TEXT("skillBudgetShare"), .6)), 0.f, 1.f);
    Out.bValid = true;
    return true;
}

FString CireSkillShop::ToJson(const FCireSkillShopData& D)
{
    const auto& R = D.Rules;
    return FString::Printf(TEXT(R"({
  "schemaVersion": 1,
  "_comment": "progression-shop: the Skill Shop (Eric's playtest-2 ruling). Replaces level-up skill offers; the free opening role pick at the start stays. Prices are in mob values (LootTables.json economy), so they follow the gold players have. Edit live in F8 > Economy. See Docs/Progression.md.",
  "access": { "breather": %s, "prep": %s, "recovery": %s, "autoOpenOnWaveClear": %s },
  "readyGate": { "_comment": "Skill Shop mode: the next wave waits until every human presses READY TO CONTINUE (bots auto-ready); maxSeconds is the AFK safety cap (0 = none), counted down on screen in its last 30 s.", "enabled": %s, "maxSeconds": %g },
  "prices": { "active": %g, "passive": %g, "ultimate": %g, "activeOwnedGrowth": %g, "levelUpBase": %g, "levelUpGrowth": %g },
  "scaling": { "effectPerLevel": %g, "costPerLevel": %g, "cooldownPerLevel": %g, "minCooldownFactor": %g },
  "slots": { "activeStart": %d, "activeEveryWaves": %d, "maxActive": %d, "passiveFromWave": %d, "ultimateFromWave": %d },
  "bots": { "skillBudgetShare": %g }
}
)"), D.bBreather ? TEXT("true") : TEXT("false"), D.bPrep ? TEXT("true") : TEXT("false"), D.bRecovery ? TEXT("true") : TEXT("false"), D.bAutoOpen ? TEXT("true") : TEXT("false"),
        D.bReadyGate ? TEXT("true") : TEXT("false"), D.ReadyMaxSeconds,
        R.ActivePrice, R.PassivePrice, R.UltimatePrice, R.ActiveOwnedGrowth, R.LevelUpBase, R.LevelUpGrowth,
        R.EffectPerLevel, R.CostPerLevel, R.CooldownPerLevel, R.MinCooldownFactor,
        R.ActiveSlotsStart, R.ActiveSlotEveryWaves, R.MaxActive, R.PassiveFromWave, R.UltimateFromWave, D.BotSkillShare);
}

bool CireSkillShop::Reload()
{
    FString Json, Error;
    FCireSkillShopData Parsed;
    if (!FFileHelper::LoadFileToString(Json, *DataPath())) Error = TEXT("Cannot read Content/Data/SkillShop.json");
    else ParseJson(Json, Parsed, Error);
    bShopLoaded = true;
    if (!Parsed.bValid)
    {
        UE_LOG(LogCireSkillShop, Error, TEXT("CIRE_SKILLSHOP_DATA_ERROR %s"), *Error);
        ShopData.bValid = true; // defaults keep the shop usable
        ShopData.Error = Error;
        return false;
    }
    ShopData = Parsed;
    UE_LOG(LogCireSkillShop, Display, TEXT("CIRE_SKILLSHOP_LOADED active=%g passive=%g ultimate=%g slots=%d+1/%dw"), ShopData.Rules.ActivePrice,
        ShopData.Rules.PassivePrice, ShopData.Rules.UltimatePrice, ShopData.Rules.ActiveSlotsStart, ShopData.Rules.ActiveSlotEveryWaves);
    return true;
}

const FCireSkillShopData& CireSkillShop::Get() { if (!bShopLoaded) Reload(); return ShopData; }
FCireSkillShopData& CireSkillShop::Mutable() { if (!bShopLoaded) Reload(); return ShopData; }
bool CireSkillShop::Save(FString* Error)
{
    const bool bOk = FFileHelper::SaveStringToFile(ToJson(Get()), *DataPath(), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
    if (!bOk && Error) *Error = TEXT("Cannot write Content/Data/SkillShop.json");
    return bOk;
}

// ------------------------------------------------------------------ Ability DB
CI::ShopSkillKind CireSkillShop::KindOf(const FString& Id)
{
    if (const FCireAbilityDef* Def = CireAbilityDB::Find(Id))
        return Def->IsUltimate() ? CI::ShopSkillKind::Ultimate : Def->IsPassive() ? CI::ShopSkillKind::Passive : CI::ShopSkillKind::Active;
    return ACireHero::IsUltimate(Id) ? CI::ShopSkillKind::Ultimate : ACireHero::IsPassive(Id) ? CI::ShopSkillKind::Passive : CI::ShopSkillKind::Active;
}

FString CireSkillShop::RoleTags(const FString& Id)
{
    if (const FCireAbilityDef* Def = CireAbilityDB::Find(Id); Def && Def->Types.Num() > 0) return FString::Join(Def->Types, TEXT(" / "));
    const Cires::RoleMask Tags = Cires::SkillRoleTags(Utf8(Id));
    TArray<FString> Parts;
    if (Tags & Cires::RoleTank) Parts.Add(TEXT("TANK"));
    if (Tags & Cires::RoleDamage) Parts.Add(TEXT("DPS"));
    if (Tags & Cires::RoleSupport) Parts.Add(TEXT("HEAL"));
    return FString::Join(Parts, TEXT(" / "));
}

FString CireSkillShop::SchoolOf(const FString& Id)
{
    const FCireAbilityDef* Def = CireAbilityDB::Find(Id);
    return Def ? Def->School : FString();
}

TArray<FCireShopSkill> CireSkillShop::CatalogFor(const ACireHero* Hero)
{
    TArray<FString> Ids;
    // The champion's purchasable list from the Ability Database (implemented skills only).
    if (Hero && !Hero->ChampionProfileId.IsEmpty() && CireAbilityDB::Kit(Hero->ChampionProfileId))
        Ids = CireAbilityDB::PurchasableSkills(Hero->ChampionProfileId, true);
    // No profile (legacy archetype heroes, fixtures): the role-tagged skill pool.
    if (Ids.IsEmpty())
        for (const auto& Skill : Cires::StarterSkillPoolForRoles(HeroRoles(Hero))) Ids.Add(UTF8_TO_TCHAR(Skill.Id.c_str()));
    // scaling-kits "requires": shield skills only for shield users, ranged skills only for ranged attackers.
    Ids.RemoveAll([Hero](const FString& Id) { return Hero && !CireKits::MeetsRequirement(Hero, Id, nullptr); });
    // Owned skills (e.g. an opening pick from another list) are always listed so they can level.
    if (Hero) for (const FString& Id : Hero->Skills) Ids.AddUnique(Id);
    TArray<FCireShopSkill> Out;
    for (const FString& Id : Ids)
    {
        FCireShopSkill Entry;
        Entry.Id = Id;
        Entry.Name = ACireHero::SkillName(Id);
        Entry.Kind = KindOf(Id);
        Entry.Roles = Cires::SkillRoleTags(Utf8(Id));
        Entry.Types = RoleTags(Id);
        Entry.School = SchoolOf(Id);
        Out.Add(Entry);
    }
    Out.StableSort([](const FCireShopSkill& A, const FCireShopSkill& B) { return A.Kind != B.Kind ? A.Kind < B.Kind : A.Name < B.Name; });
    return Out;
}

// ------------------------------------------------------------------ state
int32 CireSkillShop::CurrentWave(const UWorld* World)
{
    const auto* S = World ? World->GetGameState<ACireGameState>() : nullptr;
    return S ? FMath::Max(0, S->Wave) : 0;
}

bool CireSkillShop::IsSkillShopMode(const UWorld* World)
{
    const auto* S = World ? World->GetGameState<ACireGameState>() : nullptr;
    return !S || S->ProgressionMode != 0;
}

FString CireSkillShop::ModeName(bool bSkillShop) { return bSkillShop ? TEXT("Skill Shop") : TEXT("Classic Draft"); }

FString CireSkillShop::WaitingLabel(int32 Humans, int32 Ready)
{
    Humans = FMath::Max(0, Humans); Ready = FMath::Clamp(Ready, 0, Humans);
    const int32 Waiting = Humans - Ready;
    FString Label = FString::Printf(TEXT("WAITING FOR %d %s"), Waiting, Waiting == 1 ? TEXT("PLAYER") : TEXT("PLAYERS"));
    if (Humans > 1) Label += FString::Printf(TEXT("  ·  %d / %d READY"), Ready, Humans);
    return Label;
}

void CireSkillShop::InitializeMode(ACireGameMode* Mode)
{
    auto* S = Mode ? Mode->GetGameState<ACireGameState>() : nullptr;
    if (!S) return;
    FString Value;
    if (FParse::Value(FCommandLine::Get(), TEXT("CireMode="), Value))
        S->ProgressionMode = Value.Equals(TEXT("Classic"), ESearchCase::IgnoreCase) || Value.Equals(TEXT("ClassicDraft"), ESearchCase::IgnoreCase) ? 0 : 1;
    UE_LOG(LogCireSkillShop, Display, TEXT("CIRE_PROGRESSION_MODE %s"), *ModeName(S->ProgressionMode != 0));
}

void CireSkillShop::SyncSchedule(ACireHero* Hero)
{
    if (!Hero || !Hero->HasAuthority()) return;
    const auto Wanted = IsSkillShopMode(Hero->GetWorld()) ? Cires::SkillSchedule::Shop : Cires::SkillSchedule::Draft;
    auto& P = Hero->Progression;
    if (P.Schedule == Wanted) return;
    P.Schedule = Wanted;
    // Classic Draft ties the learned count to the breakpoints again (mode changes happen before wave 1).
    if (Wanted == Cires::SkillSchedule::Draft) P.NextAugmentLevel = Cires::BreakpointForSkill(static_cast<int>(P.LearnedSkills.size()));
}

bool CireSkillShop::SetMode(ACireGameMode* Mode, bool bSkillShop, FString* Why)
{
    auto* S = Mode ? Mode->GetGameState<ACireGameState>() : nullptr;
    if (!S) return false;
    if (S->Wave > 0 || S->Phase != 0) { if (Why) *Why = TEXT("The progression mode can only change before the first wave."); return false; }
    S->ProgressionMode = bSkillShop ? 1 : 0;
    S->ForceNetUpdate();
    for (ACireHero* Hero : Mode->Heroes) if (IsValid(Hero)) SyncSchedule(Hero);
    // Heroes that already drafted keep a consistent flow: Classic re-offers pending breakpoints.
    if (!bSkillShop) for (ACireHero* Hero : Mode->Heroes) if (IsValid(Hero)) Hero->RefreshOffer();
    UE_LOG(LogCireSkillShop, Display, TEXT("CIRE_PROGRESSION_MODE %s (changed)"), *ModeName(bSkillShop));
    return true;
}

bool CireSkillShop::IsBreather(const UWorld* World)
{
    const auto* S = World ? World->GetGameState<ACireGameState>() : nullptr;
    return S && S->Phase == 0 && S->NextWaveSeconds > 0.f;
}

bool CireSkillShop::IsOpen(const ACireHero* Hero, FString* Why)
{
    const auto& D = Get();
    const auto* S = Hero && Hero->GetWorld() ? Hero->GetWorld()->GetGameState<ACireGameState>() : nullptr;
    if (!Hero || !S || !Hero->bDrafted) { if (Why) *Why = TEXT("Pick a champion first."); return false; }
    if (!IsSkillShopMode(Hero->GetWorld())) { if (Why) *Why = TEXT("Classic Draft mode: skills come from level-up offers."); return false; }
    const bool bOpen = (S->Phase == 1 && D.bPrep) || (S->Phase == 4 && D.bRecovery) || (IsBreather(Hero->GetWorld()) && D.bBreather);
    if (!bOpen && Why) *Why = TEXT("The Skill Shop opens between waves (the breather), during prep and during recovery.");
    return bOpen;
}

int32 CireSkillShop::Level(const ACireHero* Hero, const FString& Id)
{
    if (!Hero || !Hero->Skills.Contains(Id)) return 0;
    if (Hero->Inventory)
        if (const FCireSkillRank* R = Hero->Inventory->SkillRanks.FindByPredicate([&](const FCireSkillRank& Rank) { return Rank.Id == Id; })) return FMath::Max(1, R->Level);
    return 1;
}

int32 CireSkillShop::OwnedOfKind(const ACireHero* Hero, CI::ShopSkillKind Kind)
{
    int32 Count = 0;
    if (Hero) for (const FString& Id : Hero->Skills) Count += KindOf(Id) == Kind;
    return Count;
}

int32 CireSkillShop::BuyPrice(const ACireHero* Hero, const FString& Id)
{
    const CI::Economy& E = CireLoot::Get().Economy;
    return CI::SkillBuyPrice(Get().Rules, E, KindOf(Id), OwnedOfKind(Hero, CI::ShopSkillKind::Active), CurrentWave(Hero ? Hero->GetWorld() : nullptr));
}

int32 CireSkillShop::LevelPrice(const ACireHero* Hero, const FString& Id)
{
    return CI::SkillLevelPrice(Get().Rules, CireLoot::Get().Economy, FMath::Max(1, Level(Hero, Id)), CurrentWave(Hero ? Hero->GetWorld() : nullptr));
}

FString CireSkillShop::BuyBlocker(const ACireHero* Hero, const FString& Id)
{
    FString Why;
    if (!IsOpen(Hero, &Why)) return Why;
    const CI::ShopSkillKind Kind = KindOf(Id);
    const bool bAllowed = CatalogFor(Hero).ContainsByPredicate([&](const FCireShopSkill& S) { return S.Id == Id; });
    // kits-complete: a skill outside this champion's list says so first; the shield / ranged requirement explains in-list gates.
    if (bAllowed && !CireKits::MeetsRequirement(Hero, Id, &Why)) return Why; // scaling-kits: shield / ranged skills
    int32 Price = 0;
    const int32 Wave = CurrentWave(Hero->GetWorld());
    switch (CI::CheckSkillBuy(Get().Rules, CireLoot::Get().Economy, Kind, OwnedOfKind(Hero, Kind), OwnedOfKind(Hero, CI::ShopSkillKind::Active),
        Hero->Skills.Contains(Id), bAllowed, Wave, Hero->Gold, Price))
    {
    case CI::SkillShopResult::Ok: return FString();
    case CI::SkillShopResult::NotAllowed: return TEXT("Not in your champion's skill list.");
    case CI::SkillShopResult::AlreadyOwned: return TEXT("Already learned: level it up instead.");
    case CI::SkillShopResult::SlotLocked:
    {
        const int32 Next = CI::NextSlotWave(Get().Rules, Kind, Wave);
        return Next > 0 ? FString::Printf(TEXT("No free %s slot: the next one opens at wave %d."), KindName(Kind), Next)
            : FString::Printf(TEXT("All %s slots are filled."), KindName(Kind));
    }
    default: return FString::Printf(TEXT("Not enough gold: %d more needed."), Price - Hero->Gold);
    }
}

// ------------------------------------------------------------------ operations
bool CireSkillShop::Buy(ACireHero* Hero, const FString& Id, FString& Message)
{
    Message.Reset();
    if (!Hero || !Hero->HasAuthority() || !Hero->Inventory || Id.IsEmpty() || Id.Len() > 64) return false;
    Message = BuyBlocker(Hero, Id);
    if (!Message.IsEmpty()) { Hero->Notice = Message; Feedback(Hero, ECireShopAction::SkillBuy, false, Id, -1, 0, Message); return false; }
    if (Hero->Skills.Num() >= Cires::MaxSkills) { Message = TEXT("Your skill book is full."); Feedback(Hero, ECireShopAction::SkillBuy, false, Id, -1, 0, Message); return false; }
    const int32 Price = BuyPrice(Hero, Id);
    const CI::ShopSkillKind Kind = KindOf(Id);
    // Shop purchases never count against the level breakpoints (Cires::SkillSchedule::Shop).
    SyncSchedule(Hero);
    if (!Cires::AddPurchasedSkill(Hero->Progression, {Utf8(Id), Utf8(ACireHero::SkillName(Id)),
        Kind == CI::ShopSkillKind::Ultimate ? Cires::SkillKind::Ultimate : Kind == CI::ShopSkillKind::Passive ? Cires::SkillKind::Passive : Cires::SkillKind::Active}))
    {
        Message = TEXT("You cannot learn that skill now."); Hero->Notice = Message;
        Feedback(Hero, ECireShopAction::SkillBuy, false, Id, -1, 0, Message); return false;
    }
    Hero->Gold -= Price;
    Hero->Skills.Add(Id);
    Hero->Cooldowns.Add(0);
    Hero->Inventory->SkillRanks.RemoveAll([&](const FCireSkillRank& R) { return R.Id == Id; });
    FCireSkillRank Rank; Rank.Id = Id; Rank.Level = 1;
    Hero->Inventory->SkillRanks.Add(Rank);
    Hero->Inventory->EndShopVisit();
    Message = FString::Printf(TEXT("Learned %s  -%dg"), *ACireHero::SkillName(Id), Price);
    Hero->Notice = Message;
    Feedback(Hero, ECireShopAction::SkillBuy, true, Id, Hero->Skills.Num() - 1, -Price, Message);
    UE_LOG(LogCireSkillShop, Display, TEXT("CIRE_SKILLSHOP_BUY hero=%s skill=%s price=%d wave=%d"), *Hero->HeroName, *Id, Price, CurrentWave(Hero->GetWorld()));
    return true;
}

bool CireSkillShop::LevelUp(ACireHero* Hero, const FString& Id, FString& Message)
{
    Message.Reset();
    if (!Hero || !Hero->HasAuthority() || !Hero->Inventory) return false;
    if (!IsOpen(Hero, &Message)) {}
    else if (!Hero->Skills.Contains(Id)) Message = TEXT("Learn the skill before levelling it.");
    else if (Hero->Gold < LevelPrice(Hero, Id)) Message = FString::Printf(TEXT("Not enough gold: %d more needed."), LevelPrice(Hero, Id) - Hero->Gold);
    if (!Message.IsEmpty()) { Hero->Notice = Message; Feedback(Hero, ECireShopAction::SkillLevel, false, Id, -1, 0, Message); return false; }
    const int32 Price = LevelPrice(Hero, Id);
    Hero->Gold -= Price;
    FCireSkillRank* Rank = FindRank(Hero, Id);
    if (!Rank) { FCireSkillRank New; New.Id = Id; New.Level = 1; Rank = &Hero->Inventory->SkillRanks.Add_GetRef(New); }
    ++Rank->Level;
    Message = FString::Printf(TEXT("%s reached level %d  -%dg"), *ACireHero::SkillName(Id), Rank->Level, Price);
    Hero->Notice = Message;
    Feedback(Hero, ECireShopAction::SkillLevel, true, Id, Hero->Skills.IndexOfByKey(Id), -Price, Message);
    UE_LOG(LogCireSkillShop, Display, TEXT("CIRE_SKILLSHOP_LEVEL hero=%s skill=%s level=%d price=%d"), *Hero->HeroName, *Id, Rank->Level, Price);
    return true;
}

void CireSkillShop::BotShop(ACireHero* Hero)
{
    if (!Hero || !Hero->bBot || !IsOpen(Hero)) return; // also off in Classic Draft mode
    FString Message;
    // 1) Fill an open slot with a skill that matches the bot's primary role (cheapest first).
    const Cires::RoleMask Primary = Cires::RoleBit(CireChampionProfiles::DraftRole(Hero));
    // Ultimates first when their slot opens, then primary-role actives, then passives; the
    // share of gold a bot may spend on skills keeps a reserve for items.
    FString BestId;
    int32 BestScore = MAX_int32;
    const TArray<FCireShopSkill> Catalog = CatalogFor(Hero);
    for (const FCireShopSkill& Skill : Catalog)
    {
        if (!BuyBlocker(Hero, Skill.Id).IsEmpty()) continue;
        const int32 Price = BuyPrice(Hero, Skill.Id);
        if (Price > FMath::Max(1, FMath::FloorToInt(Hero->Gold * FMath::Max(.34f, Get().BotSkillShare)))) continue;
        const int32 KindRank = Skill.Kind == CI::ShopSkillKind::Ultimate ? 0 : Skill.Kind == CI::ShopSkillKind::Active ? 1 : 2;
        const int32 Score = KindRank * 100000 + ((Skill.Roles & Primary) ? 0 : 10000) + Price + static_cast<int32>(GetTypeHash(Skill.Id) % 7);
        if (Score < BestScore) { BestScore = Score; BestId = Skill.Id; }
    }
    if (!BestId.IsEmpty() && Buy(Hero, BestId, Message)) return;
    // 2) Otherwise level the lowest-ranked skill while keeping a reserve for items.
    FString Lowest;
    int32 LowestLevel = MAX_int32;
    for (const FString& Id : Hero->Skills)
        if (KindOf(Id) != CI::ShopSkillKind::Passive && Level(Hero, Id) < LowestLevel) { LowestLevel = Level(Hero, Id); Lowest = Id; }
    if (!Lowest.IsEmpty() && Hero->Gold * Get().BotSkillShare >= LevelPrice(Hero, Lowest)) LevelUp(Hero, Lowest, Message);
}

// ------------------------------------------------------------------ scaling
FCireCastScale CireSkillShop::CastScale(const ACireHero* Hero, const FString& Id)
{
    FCireCastScale Scale;
    Scale.Level = FMath::Max(1, Level(Hero, Id));
    if (Scale.Level <= 1) return Scale;
    const auto& R = Get().Rules;
    // Fallback: the SkillShop.json per-level rules (abilities the database does not know).
    Scale.Effect = static_cast<float>(CI::SkillEffectScale(R, Scale.Level));
    Scale.Cost = static_cast<float>(CI::SkillCostScale(R, Scale.Level));
    Scale.Cooldown = static_cast<float>(CI::SkillCooldownScale(R, Scale.Level));
    if (CireAbilityDB::Find(Id))
    {
        const FCireAbilityStats One = CireAbilityDB::EffectiveStats(Id, 1), Now = CireAbilityDB::EffectiveStats(Id, Scale.Level);
        if (One.Effect > 0) Scale.Effect = Now.Effect / One.Effect;
        const float BaseCost = One.ManaCost + One.EnergyCost;
        if (BaseCost > 0) Scale.Cost = (Now.ManaCost + Now.EnergyCost) / BaseCost;
        if (One.Cooldown > 0) Scale.Cooldown = Now.Cooldown / One.Cooldown;
    }
    return Scale;
}

float CireSkillShop::EffectScale(const ACireHero* Source, const FString& AbilityName)
{
    if (!Source || !Source->Inventory || Source->Inventory->SkillRanks.Num() == 0 || AbilityName.IsEmpty()) return 1.f;
    for (const FCireSkillRank& Rank : Source->Inventory->SkillRanks)
        if (Rank.Level > 1 && Source->Skills.Contains(Rank.Id) && ACireHero::SkillName(Rank.Id) == AbilityName)
            return CastScale(Source, Rank.Id).Effect;
    return 1.f;
}

// items-v2: mana costs also grow with champion level (Items.json manaEconomy); energy stays flat.
static FString GCostFailText = TEXT("Not enough mana or energy.");
void CireSkillShop::ScaledCost(const ACireHero* Hero, const FString& Id, float BaseMana, float BaseEnergy, float& OutMana, float& OutEnergy)
{
    const float Cost = Hero ? CastScale(Hero, Id).Cost : 1.f;
    OutMana = BaseMana * Cost * CireItems::ManaCostScale(Hero);
    OutEnergy = BaseEnergy * Cost;
}
FString CireSkillShop::CostFailText() { return GCostFailText; }
bool CireSkillShop::CanPayCast(const ACireHero* Hero, const FString& Id, float BaseMana, float BaseEnergy)
{
    if (!Hero) return false;
    float Mana = 0, Energy = 0;
    ScaledCost(Hero, Id, BaseMana, BaseEnergy, Mana, Energy);
    if (Hero->Mana >= Mana && Hero->Energy >= Energy) return true;
    GCostFailText = CireItems::NoteShortfall(Hero, Mana, Energy);
    return false;
}

void CireSkillShop::ApplyCastLevel(ACireHero* Hero, int32 Slot, const FString& Id, float BaseMana, float BaseEnergy)
{
    if (!Hero || !Hero->HasAuthority()) return;
    CireKits::OnSkillCast(Hero, Id); // scaling-kits: level-15 pulse bonus for non-damaging skills
    const FCireCastScale Scale = CastScale(Hero, Id);
    // items-v2: the caller already paid the base cost; charge the level-scaled remainder, then refunds/upgrades.
    float Mana = 0, Energy = 0;
    ScaledCost(Hero, Id, BaseMana, BaseEnergy, Mana, Energy);
    Hero->Mana = FMath::Max(0.f, Hero->Mana - FMath::Max(0.f, Mana - BaseMana));
    Hero->Energy = FMath::Max(0.f, Hero->Energy - FMath::Max(0.f, Energy - BaseEnergy));
    CireItems::OnAbilityCast(Hero, Id, Mana);
    // rules-conformance: the only ways to lose threat are death and an ability that explicitly says so.
    if (const FCireAbilityDef* Def = CireAbilityDB::Find(Id); Def && Def->ThreatScale < 1.f) CireThreat::ScaleAll(Hero, Def->ThreatScale);
    if (Scale.Level <= 1) return;
    if (Hero->Cooldowns.IsValidIndex(Slot)) Hero->Cooldowns[Slot] *= Scale.Cooldown;
}

// ------------------------------------------------------------------ Ready to Continue gate
bool CireSkillShop::HoldBreather(ACireGameMode* Mode, float DeltaSeconds, float& WaveTimer)
{
    static TMap<TWeakObjectPtr<ACireGameMode>, float> Elapsed;
    auto* S = Mode ? Mode->GetGameState<ACireGameState>() : nullptr;
    if (!S) return false;
    const auto& D = Get();
    auto Publish = [S](bool bHold, float Left)
    {
        if (S->bReadyGateHold != bHold || !FMath::IsNearlyEqual(S->ReadyGateLeft, Left, .25f)) { S->bReadyGateHold = bHold; S->ReadyGateLeft = Left; S->ForceNetUpdate(); }
    };
    CireWaveDirector::UpdateBreatherReady(Mode); // ready counts + per-hero flags
    if (!D.bReadyGate || !IsSkillShopMode(Mode->GetWorld()) || !CireWaveDirector::IsBreather(Mode) || S->BreatherPlayers <= 0)
    {
        Elapsed.Remove(Mode);
        Publish(false, -1.f);
        return false;
    }
    float& Waited = Elapsed.FindOrAdd(Mode);
    Waited += FMath::Max(0.f, DeltaSeconds);
    const bool bAllReady = S->BreatherReady >= S->BreatherPlayers;
    const bool bCapped = D.ReadyMaxSeconds > 0 && Waited >= D.ReadyMaxSeconds;
    const float Left = D.ReadyMaxSeconds > 0 ? FMath::Max(0.f, D.ReadyMaxSeconds - Waited) : -1.f;
    if (bAllReady || bCapped)
    {
        WaveTimer = FMath::Min(WaveTimer, 1.f);
        Publish(false, Left);
        return false;
    }
    Publish(true, Left);
    return true;
}
