// wave-director: Waves.json data model, templates, validation and (de)serialization.
#include "CireWaves.h"
#include "CireNPCArchetypes.h"
#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Policies/PrettyJsonPrintPolicy.h"

namespace
{
const TCHAR* TypeIds[] = {TEXT("normal"), TEXT("armored"), TEXT("armored_escort"), TEXT("boss"), TEXT("caster_pack"),
    TEXT("melee_pack"), TEXT("ranged_pack"), TEXT("hybrid_pack"), TEXT("custom"), TEXT("bonus_loot")};
const TCHAR* TypeLabels[] = {TEXT("Normal"), TEXT("Armored"), TEXT("Armored Escort"), TEXT("Boss"), TEXT("Caster Pack"),
    TEXT("Melee Pack"), TEXT("Ranged Pack"), TEXT("Hybrid Pack"), TEXT("Custom"), TEXT("Bonus Loot")};
static_assert(UE_ARRAY_COUNT(TypeIds) == static_cast<int32>(ECireWaveType::Count), "wave type table");

FCireWaveUnit Unit(const TCHAR* Id, int32 Count, float Health = 1.f, float Damage = 1.f)
{
    FCireWaveUnit U; U.Archetype = Id; U.Count = Count; U.HealthScale = Health; U.DamageScale = Damage;
    // monster-races: template rows follow the wave's race through their slot (the hollow unit stays the default).
    static const TMap<FName, FName> Slots = {{TEXT("hollow_infantry"), TEXT("line")}, {TEXT("ironbound_bruiser"), TEXT("bruiser")},
        {TEXT("hollow_shieldbearer"), TEXT("tank")}, {TEXT("blight_caster"), TEXT("caster")}, {TEXT("barbed_hunter"), TEXT("ranged")},
        {TEXT("grave_hound"), TEXT("special")}, {TEXT("hollow_siegebreaker"), TEXT("boss")}, {TEXT("gravemaw_pack_leader"), TEXT("warlord")}};
    if (const FName* Slot = Slots.Find(U.Archetype)) U.Slot = *Slot;
    return U;
}
bool KnownSlot(FName Slot) { return CireRaces::SlotNames().Contains(Slot); }
bool Near(float A, float B) { return FMath::IsNearlyEqual(A, B, 1.e-4f); }
float ClampF(float V, float Lo, float Hi, float Fallback) { return FMath::IsFinite(V) ? FMath::Clamp(V, Lo, Hi) : Fallback; }
}

bool FCireWaveUnit::operator==(const FCireWaveUnit& O) const
{
    return Archetype == O.Archetype && Count == O.Count && Near(HealthScale, O.HealthScale) && Near(DamageScale, O.DamageScale) &&
        Near(SizeScale, O.SizeScale) && bElite == O.bElite && bNonAttacking == O.bNonAttacking && bEscortee == O.bEscortee &&
        bBoss == O.bBoss && LeakCost == O.LeakCost && Slot == O.Slot && Rank == O.Rank && Palette == O.Palette && SkillCount == O.SkillCount && // monster-races
        SkillTier == O.SkillTier && bRare == O.bRare; // monster-expansion
}
bool FCireRareSpawnRules::operator==(const FCireRareSpawnRules& O) const
{
    return bEnabled == O.bEnabled && Near(Chance, O.Chance) && FromWave == O.FromWave && MaxPerCycle == O.MaxPerCycle && Near(Health, O.Health) &&
        Near(Damage, O.Damage) && Near(Size, O.Size) && Near(Bounty, O.Bounty) && Pool == O.Pool;
}
FCireBonusWaveRules::FCireBonusWaveRules() { Wave = CireWaveDirector::BonusTemplate(); }
bool FCireBonusWaveRules::operator==(const FCireBonusWaveRules& O) const
{
    return bEnabled == O.bEnabled && Near(Chance, O.Chance) && FromWave == O.FromWave && MaxPerCycle == O.MaxPerCycle &&
        Near(ExtraBreatherSeconds, O.ExtraBreatherSeconds) && Near(EscapeSeconds, O.EscapeSeconds) && Near(FleeRadius, O.FleeRadius) &&
        Near(Bounty, O.Bounty) && Wave == O.Wave;
}
int32 FCireWaveDef::UnitsPerLane() const { int32 N = 0; for (const auto& U : Units) N += U.Count; return N; }
bool FCireWaveDef::operator==(const FCireWaveDef& O) const
{
    return Label == O.Label && Type == O.Type && Units == O.Units && Near(SpawnInterval, O.SpawnInterval) && Near(DelayBefore, O.DelayBefore) &&
        bMustClear == O.bMustClear && Near(RewardMultiplier, O.RewardMultiplier) && Race == O.Race; // monster-races: race
}
bool FCireWaveConfig::operator==(const FCireWaveConfig& O) const
{
    return Near(BreatherSeconds, O.BreatherSeconds) && WavesPerCycle == O.WavesPerCycle && Cycles == O.Cycles &&
        Near(CycleHealthGrowth, O.CycleHealthGrowth) && Near(CycleDamageGrowth, O.CycleDamageGrowth) && CycleExtraUnits == O.CycleExtraUnits &&
        bStallFailsafe == O.bStallFailsafe && Near(MaxWaveSeconds, O.MaxWaveSeconds) && FailsafeAction == O.FailsafeAction &&
        Near(FailsafeGraceSeconds, O.FailsafeGraceSeconds) && Near(StuckSeconds, O.StuckSeconds) && Waves == O.Waves &&
        Near(SpawnAlongRoute, O.SpawnAlongRoute) && Near(MarchSpeedMultiplier, O.MarchSpeedMultiplier) && Near(FirstWaveDelay, O.FirstWaveDelay) && // pacing
        Near(PrepSeconds, O.PrepSeconds) && Near(ArenaSeconds, O.ArenaSeconds) && Near(RecoverySeconds, O.RecoverySeconds) && bEarlyContinue == O.bEarlyContinue &&
        Skills == O.Skills && Campaign == O.Campaign && // monster-races
        Rare == O.Rare && Bonus == O.Bonus; // monster-expansion
}

const TCHAR* CireWaveDirector::TypeName(ECireWaveType Type)
{
    const int32 I = static_cast<int32>(Type);
    return I >= 0 && I < UE_ARRAY_COUNT(TypeIds) ? TypeIds[I] : TEXT("custom");
}
FString CireWaveDirector::TypeLabel(ECireWaveType Type)
{
    const int32 I = static_cast<int32>(Type);
    return I >= 0 && I < UE_ARRAY_COUNT(TypeLabels) ? TypeLabels[I] : TEXT("Custom");
}
bool CireWaveDirector::ParseType(const FString& Name, ECireWaveType& Out)
{
    for (int32 I = 0; I < UE_ARRAY_COUNT(TypeIds); ++I)
        if (Name.Equals(TypeIds[I], ESearchCase::IgnoreCase)) { Out = static_cast<ECireWaveType>(I); return true; }
    return false;
}

FCireWaveDef CireWaveDirector::Template(ECireWaveType Type)
{
    FCireWaveDef W; W.Type = Type; W.Label = TypeLabel(Type);
    switch (Type)
    {
    case ECireWaveType::Normal:
        W.Label = TEXT("Breach Vanguard");
        // balance: early waves hit harder (damage 1.25 -> 1.4; wave 2 1.3 -> 1.45 with health .95 -> .85).
        W.Units = {Unit(TEXT("hollow_infantry"), 3, .9f, 1.4f), Unit(TEXT("ironbound_bruiser"), 2, .9f, 1.4f),
                   Unit(TEXT("barbed_hunter"), 1, .9f, 1.4f), Unit(TEXT("blight_caster"), 1, .9f, 1.4f)};
        break;
    case ECireWaveType::Armored:
    {
        W.Label = TEXT("Iron Procession"); W.SpawnInterval = .8f;
        FCireWaveUnit A = Unit(TEXT("hollow_shieldbearer"), 4, 1.3f, 1.f); A.bNonAttacking = true; A.SizeScale = 1.2f; A.LeakCost = 2;
        W.Units = {A};
        break;
    }
    case ECireWaveType::ArmoredEscort:
    {
        W.Label = TEXT("Armored Escort"); W.SpawnInterval = .5f;
        FCireWaveUnit Tank = Unit(TEXT("hollow_shieldbearer"), 1, 4.f, 1.f);
        Tank.bNonAttacking = true; Tank.bEscortee = true; Tank.SizeScale = 1.45f; Tank.LeakCost = 5;
        W.Units = {Tank, Unit(TEXT("hollow_infantry"), 2, .95f, 1.2f), Unit(TEXT("ironbound_bruiser"), 1, .95f, 1.2f),
                   Unit(TEXT("barbed_hunter"), 1, .95f, 1.2f)};
        W.RewardMultiplier = 1.25f;
        break;
    }
    case ECireWaveType::Boss:
    {
        W.Label = TEXT("Siege Host"); W.SpawnInterval = .5f;
        // pacing: the boss is 30% of its archetype health (balance: 40 -> 30%, the boss wave held every cycle).
        FCireWaveUnit Boss = Unit(TEXT("hollow_siegebreaker"), 1, .3f, 1.f); Boss.bBoss = true;
        W.Units = {Unit(TEXT("hollow_infantry"), 2, .95f, 1.2f), Unit(TEXT("ironbound_bruiser"), 2, .95f, 1.2f),
                   Unit(TEXT("blight_caster"), 1, .95f, 1.2f), Boss};
        W.RewardMultiplier = 1.5f;
        break;
    }
    case ECireWaveType::CasterPack:
        W.Units = {Unit(TEXT("hollow_shieldbearer"), 1), Unit(TEXT("blight_caster"), 4)};
        break;
    case ECireWaveType::MeleePack:
        W.Units = {Unit(TEXT("hollow_shieldbearer"), 1), Unit(TEXT("hollow_infantry"), 2), Unit(TEXT("ironbound_bruiser"), 2)};
        break;
    case ECireWaveType::RangedPack:
        W.Units = {Unit(TEXT("hollow_shieldbearer"), 1), Unit(TEXT("barbed_hunter"), 4)};
        break;
    case ECireWaveType::HybridPack:
        W.Units = {Unit(TEXT("hollow_shieldbearer"), 1), Unit(TEXT("hollow_infantry"), 1), Unit(TEXT("ironbound_bruiser"), 1),
                   Unit(TEXT("blight_caster"), 1), Unit(TEXT("barbed_hunter"), 1), Unit(TEXT("grave_hound"), 2)};
        break;
    case ECireWaveType::BonusLoot: // monster-expansion
        W = CireWaveDirector::BonusTemplate();
        break;
    default:
        W.Type = ECireWaveType::Custom;
        W.Units = {Unit(TEXT("hollow_infantry"), 4)};
        break;
    }
    return W;
}

// monster-expansion: the Goblin Hoard. Treasure goblins and a gilded stag flee down the lane; they never fight back and
// never cost lives, and escape after bonusWave.escapeSeconds. Bestiary.json creatures (fallback bodies without the packs).
FCireWaveDef CireWaveDirector::BonusTemplate()
{
    FCireWaveDef W; W.Type = ECireWaveType::BonusLoot; W.Label = TEXT("Goblin Hoard"); W.SpawnInterval = .35f; W.bMustClear = false;
    FCireWaveUnit Goblin; Goblin.Archetype = TEXT("treasure_goblin"); Goblin.Count = 3; Goblin.HealthScale = .8f;
    FCireWaveUnit Stag; Stag.Archetype = TEXT("gilded_stag"); Stag.Count = 1; Stag.HealthScale = 1.1f;
    W.Units = {Goblin, Stag};
    return W;
}

FCireWaveConfig CireWaveDirector::Defaults()
{
    FCireWaveConfig C;
    FCireWaveDef One = Template(ECireWaveType::Normal);
    FCireWaveDef Two = Template(ECireWaveType::Normal);
    Two.Label = TEXT("Breach Column"); // monster-races: race-neutral (the race is appended at runtime)
    Two.Units = {Unit(TEXT("hollow_infantry"), 3, .85f, 1.45f), Unit(TEXT("ironbound_bruiser"), 2, .85f, 1.45f),
                 Unit(TEXT("barbed_hunter"), 1, .85f, 1.45f), Unit(TEXT("blight_caster"), 1, .85f, 1.45f)};
    // monster-races: wave 2 brings the race's special unit (hollow: grave hounds).
    Two.Units.Add(Unit(TEXT("grave_hound"), 2, .85f, 1.45f));
    C.Waves = {One, Two, Template(ECireWaveType::Armored), Template(ECireWaveType::ArmoredEscort), Template(ECireWaveType::Boss)};
    C.WavesPerCycle = C.Waves.Num();
    // Start on the hollow basics, bring in Eric's favourites (Blightwood, then the Drowned Deep), then the other races,
    // then mixed hosts. Each cycle ends on one of its race's two bosses (colossus on odd cycles, warlord on even).
    C.Campaign.RaceRotation = {TEXT("hollow"), TEXT("blightwood"), TEXT("drowned_deep"), TEXT("ironhide"), TEXT("hollow+blightwood"), TEXT("stoneborn"),
        TEXT("drakkari"), TEXT("drowned_deep+voidborn"), TEXT("feral_kin"), TEXT("fallen_order"), TEXT("voidborn"), TEXT("ironhide+drakkari")};
    // monster-expansion: rare creatures drawn from the purchased creature packs (Bestiary.json), and the goblin hoard.
    C.Rare.Pool = {TEXT("lich_revenant"), TEXT("storm_griffon"), TEXT("cinder_drake"), TEXT("frostfang_alpha"), TEXT("horned_brute")};
    C.Bonus.Wave = BonusTemplate();
    return C;
}

bool CireWaveDirector::Validate(FCireWaveConfig& C, FString* Error, bool bClamp)
{
    auto Fail = [&](const FString& Why) { if (Error) *Error = Why; return false; };
    FCireWaveConfig Before = C;
    C.BreatherSeconds = ClampF(C.BreatherSeconds, 0, 120, 15);
    C.WavesPerCycle = FMath::Clamp(C.WavesPerCycle, 1, 10);
    C.Cycles = FMath::Clamp(C.Cycles, 0, 50);
    C.CycleHealthGrowth = ClampF(C.CycleHealthGrowth, 0, 2, .1f);
    C.CycleDamageGrowth = ClampF(C.CycleDamageGrowth, 0, 2, .1f);
    C.CycleExtraUnits = FMath::Clamp(C.CycleExtraUnits, 0, 5);
    C.MaxWaveSeconds = ClampF(C.MaxWaveSeconds, 30, 900, 120);
    C.FailsafeGraceSeconds = ClampF(C.FailsafeGraceSeconds, 5, 300, 30);
    C.StuckSeconds = ClampF(C.StuckSeconds, 1, 30, 5);
    // pacing
    C.SpawnAlongRoute = ClampF(C.SpawnAlongRoute, 0, .7f, 0.f); // default: spawn at the rift (Eric)
    C.MarchSpeedMultiplier = ClampF(C.MarchSpeedMultiplier, .5f, 2, 1.25f);
    C.FirstWaveDelay = ClampF(C.FirstWaveDelay, 0, 120, 8);
    C.PrepSeconds = ClampF(C.PrepSeconds, 5, 600, 30);
    C.ArenaSeconds = ClampF(C.ArenaSeconds, 15, 900, 60);
    C.RecoverySeconds = ClampF(C.RecoverySeconds, 1, 180, 10);
    if (C.Waves.IsEmpty()) return Fail(TEXT("At least one wave is required."));
    if (C.Waves.Num() > 20) return Fail(TEXT("At most 20 waves are allowed."));
    for (int32 WI = 0; WI < C.Waves.Num(); ++WI)
    {
        auto& W = C.Waves[WI];
        W.Label = W.Label.Left(40).TrimStartAndEnd();
        if (W.Label.IsEmpty()) W.Label = TypeLabel(W.Type);
        if (static_cast<int32>(W.Type) >= static_cast<int32>(ECireWaveType::Count)) W.Type = ECireWaveType::Custom;
        W.SpawnInterval = ClampF(W.SpawnInterval, 0, 5, .6f);
        W.DelayBefore = ClampF(W.DelayBefore, 0, 120, 0);
        W.RewardMultiplier = ClampF(W.RewardMultiplier, 0, 10, 1);
        if (W.Units.IsEmpty()) return Fail(FString::Printf(TEXT("Wave %d has no composition rows."), WI + 1));
        if (W.Units.Num() > 8) return Fail(FString::Printf(TEXT("Wave %d has more than 8 composition rows."), WI + 1));
        int32 Total = 0, Bosses = 0, Attackers = 0;
        // monster-races: wave race, row slots, ranks and skill overrides.
        if (W.Race == TEXT("rotation")) W.Race = NAME_None;
        if (!W.Race.IsNone() && !CireRaces::FindRace(W.Race))
            return Fail(FString::Printf(TEXT("Wave %d: unknown race '%s'."), WI + 1, *W.Race.ToString()));
        for (auto& U : W.Units)
        {
            if (!U.Slot.IsNone() && !KnownSlot(U.Slot)) return Fail(FString::Printf(TEXT("Wave %d: unknown slot '%s'."), WI + 1, *U.Slot.ToString()));
            U.Rank = static_cast<ECireNPCRank>(FMath::Clamp(static_cast<int32>(U.Rank), 0, static_cast<int32>(ECireNPCRank::Mythic)));
            U.Palette = FMath::Clamp(U.Palette, -1, 15); U.SkillCount = FMath::Clamp(U.SkillCount, -1, 8); U.SkillTier = FMath::Clamp(U.SkillTier, 0, 5);
            if (U.Archetype.IsNone() || !CireNPCArchetypes::Find(U.Archetype))
                return Fail(FString::Printf(TEXT("Wave %d: unknown archetype '%s'."), WI + 1, *U.Archetype.ToString()));
            U.Count = FMath::Clamp(U.Count, 1, 20);
            U.HealthScale = ClampF(U.HealthScale, .1f, 20, 1);
            U.DamageScale = ClampF(U.DamageScale, .05f, 10, 1);
            U.SizeScale = ClampF(U.SizeScale, .5f, 3, 1);
            U.LeakCost = FMath::Clamp(U.LeakCost, 0, 100);
            if (U.bEscortee) U.bNonAttacking = true;
            if (U.bBoss) { U.bNonAttacking = false; U.bEscortee = false; Bosses += U.Count; }
            if (!U.bNonAttacking) Attackers += U.Count;
            Total += U.Count;
        }
        if (Total > 30) return Fail(FString::Printf(TEXT("Wave %d spawns %d units per lane; the limit is 30."), WI + 1, Total));
        if (Bosses > 3) return Fail(FString::Printf(TEXT("Wave %d has %d lane bosses; the limit is 3."), WI + 1, Bosses));
        (void)Attackers;
    }
    // monster-expansion: rare spawns and the bonus loot wave.
    {
        auto& Rr = C.Rare;
        Rr.Chance = ClampF(Rr.Chance, 0, 1, .3f); Rr.FromWave = FMath::Clamp(Rr.FromWave, 1, 200); Rr.MaxPerCycle = FMath::Clamp(Rr.MaxPerCycle, 0, 10);
        Rr.Health = ClampF(Rr.Health, .2f, 20, 3); Rr.Damage = ClampF(Rr.Damage, .1f, 10, 1.3f); Rr.Size = ClampF(Rr.Size, .5f, 2.5f, 1.15f);
        Rr.Bounty = ClampF(Rr.Bounty, 0, 100, 5);
        if (Rr.Pool.Num() > 16) Rr.Pool.SetNum(16);
        Rr.Pool.RemoveAll([](FName Id) { return !CireNPCArchetypes::Find(Id); }); // a missing Bestiary.json never takes the waves down
        auto& B = C.Bonus;
        B.Chance = ClampF(B.Chance, 0, 1, .4f); B.FromWave = FMath::Clamp(B.FromWave, 1, 200); B.MaxPerCycle = FMath::Clamp(B.MaxPerCycle, 0, 5);
        B.ExtraBreatherSeconds = ClampF(B.ExtraBreatherSeconds, 0, 60, 6); B.EscapeSeconds = ClampF(B.EscapeSeconds, 5, 120, 26);
        B.FleeRadius = ClampF(B.FleeRadius, 0, 3000, 950); B.Bounty = ClampF(B.Bounty, 0, 100, 4);
        B.Wave.Type = ECireWaveType::BonusLoot; B.Wave.bMustClear = false; B.Wave.Race = NAME_None;
        B.Wave.Label = B.Wave.Label.Left(40).TrimStartAndEnd(); if (B.Wave.Label.IsEmpty()) B.Wave.Label = TEXT("Bonus Loot");
        B.Wave.SpawnInterval = ClampF(B.Wave.SpawnInterval, 0, 5, .35f); B.Wave.DelayBefore = 0; B.Wave.RewardMultiplier = ClampF(B.Wave.RewardMultiplier, 0, 10, 1);
        if (B.Wave.Units.IsEmpty()) B.Wave.Units = BonusTemplate().Units; // default-constructed configs (tests, SPAWN NOW probes)
        if (B.Wave.Units.Num() > 8) return Fail(TEXT("bonusWave has more than 8 composition rows."));
        B.Wave.Units.RemoveAll([](const FCireWaveUnit& U) { return U.Archetype.IsNone() || !CireNPCArchetypes::Find(U.Archetype); });
        int32 Total = 0;
        for (auto& U : B.Wave.Units)
        {
            U.Count = FMath::Clamp(U.Count, 1, 10); U.HealthScale = ClampF(U.HealthScale, .1f, 20, 1); U.DamageScale = ClampF(U.DamageScale, .05f, 10, 1);
            U.SizeScale = ClampF(U.SizeScale, .5f, 3, 1); U.bBoss = false; U.bEscortee = false; U.bRare = false; U.LeakCost = 0; U.Slot = NAME_None;
            Total += U.Count;
        }
        if (Total > 12) return Fail(TEXT("bonusWave spawns at most 12 creatures per lane."));
    }
    // monster-races: skill schedule and campaign.
    {
        auto& S = C.Skills;
        S.FirstSkillWave = FMath::Clamp(S.FirstSkillWave, 1, 200); S.UnlockEveryWaves = FMath::Clamp(S.UnlockEveryWaves, 1, 50);
        S.MaxSkills = FMath::Clamp(S.MaxSkills, 0, 6); S.TierEveryWaves = FMath::Clamp(S.TierEveryWaves, 1, 50); S.MaxTier = FMath::Clamp(S.MaxTier, 1, 5);
        S.TierDamage = ClampF(S.TierDamage, 0, 2, .2f); S.TierCooldown = ClampF(S.TierCooldown, 0, .5f, .1f); S.TierDuration = ClampF(S.TierDuration, 0, 2, .15f);
        auto& K = C.Campaign;
        K.VeteranFromCycle = FMath::Clamp(K.VeteranFromCycle, 0, 100); K.EliteFromCycle = FMath::Clamp(K.EliteFromCycle, 0, 100);
        K.ChampionFromCycle = FMath::Clamp(K.ChampionFromCycle, 0, 100); K.MythicBossFromCycle = FMath::Clamp(K.MythicBossFromCycle, 0, 100);
        K.PromoteEvery = FMath::Clamp(K.PromoteEvery, 1, 30);
        if (K.RaceRotation.Num() > 40) return Fail(TEXT("The race rotation lists at most 40 cycles."));
        for (const FString& Entry : K.RaceRotation)
        {
            TArray<FString> Parts; Entry.ParseIntoArray(Parts, TEXT("+"), true);
            if (Parts.IsEmpty() || Parts.Num() > 3) return Fail(FString::Printf(TEXT("Race rotation entry '%s' must name 1-3 races joined by '+'."), *Entry));
            for (const FString& Part : Parts) if (!CireRaces::FindRace(FName(*Part.TrimStartAndEnd())))
                return Fail(FString::Printf(TEXT("Race rotation names unknown race '%s'."), *Part));
        }
    }
    if (!bClamp && !(Before == C)) return Fail(TEXT("Values were outside their limits."));
    if (Error) Error->Reset();
    return true;
}

// ---------------------------------------------------------------- JSON
namespace
{
double Num(const TSharedPtr<FJsonObject>& O, const TCHAR* Key, double Default)
{
    double V = Default; if (O) O->TryGetNumberField(Key, V); return V;
}
bool Flag(const TSharedPtr<FJsonObject>& O, const TCHAR* Key, bool Default)
{
    bool V = Default; if (O) O->TryGetBoolField(Key, V); return V;
}
}

bool CireWaveDirector::ParseJson(const FString& Json, FCireWaveConfig& Out, FString& Error)
{
    if (Json.Len() > 256 * 1024) { Error = TEXT("Waves.json exceeds 256 KB."); return false; }
    TSharedPtr<FJsonObject> Root;
    if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Root) || !Root) { Error = TEXT("Waves.json is not valid JSON."); return false; }
    if (static_cast<int32>(Num(Root, TEXT("schemaVersion"), 0)) != 1) { Error = TEXT("Waves.json must declare schemaVersion 1."); return false; }
    FCireWaveConfig C;
    C.BreatherSeconds = static_cast<float>(Num(Root, TEXT("breatherSeconds"), C.BreatherSeconds));
    C.WavesPerCycle = static_cast<int32>(Num(Root, TEXT("wavesPerCycle"), C.WavesPerCycle));
    C.Cycles = static_cast<int32>(Num(Root, TEXT("cycles"), C.Cycles));
    const TSharedPtr<FJsonObject>* Scaling = nullptr;
    if (Root->TryGetObjectField(TEXT("cycleScaling"), Scaling) && Scaling)
    {
        C.CycleHealthGrowth = static_cast<float>(Num(*Scaling, TEXT("healthGrowth"), C.CycleHealthGrowth));
        C.CycleDamageGrowth = static_cast<float>(Num(*Scaling, TEXT("damageGrowth"), C.CycleDamageGrowth));
        C.CycleExtraUnits = static_cast<int32>(Num(*Scaling, TEXT("extraUnits"), C.CycleExtraUnits));
    }
    const TSharedPtr<FJsonObject>* Pacing = nullptr;
    if (Root->TryGetObjectField(TEXT("pacing"), Pacing) && Pacing)
    {
        C.SpawnAlongRoute = static_cast<float>(Num(*Pacing, TEXT("spawnAlongRoute"), C.SpawnAlongRoute));
        C.MarchSpeedMultiplier = static_cast<float>(Num(*Pacing, TEXT("marchSpeed"), C.MarchSpeedMultiplier));
        C.FirstWaveDelay = static_cast<float>(Num(*Pacing, TEXT("firstWaveDelay"), C.FirstWaveDelay));
        C.PrepSeconds = static_cast<float>(Num(*Pacing, TEXT("prepSeconds"), C.PrepSeconds));
        C.ArenaSeconds = static_cast<float>(Num(*Pacing, TEXT("arenaSeconds"), C.ArenaSeconds));
        C.RecoverySeconds = static_cast<float>(Num(*Pacing, TEXT("recoverySeconds"), C.RecoverySeconds));
        C.bEarlyContinue = Flag(*Pacing, TEXT("earlyContinue"), C.bEarlyContinue);
    }
    const TSharedPtr<FJsonObject>* Failsafe = nullptr;
    if (Root->TryGetObjectField(TEXT("failsafe"), Failsafe) && Failsafe)
    {
        C.bStallFailsafe = Flag(*Failsafe, TEXT("enabled"), C.bStallFailsafe);
        C.MaxWaveSeconds = static_cast<float>(Num(*Failsafe, TEXT("maxWaveSeconds"), C.MaxWaveSeconds));
        C.FailsafeGraceSeconds = static_cast<float>(Num(*Failsafe, TEXT("graceSeconds"), C.FailsafeGraceSeconds));
        C.StuckSeconds = static_cast<float>(Num(*Failsafe, TEXT("stuckSeconds"), C.StuckSeconds));
        FString Action;
        if ((*Failsafe)->TryGetStringField(TEXT("action"), Action))
        {
            if (Action == TEXT("march")) C.FailsafeAction = ECireWaveFailsafe::March;
            else if (Action == TEXT("despawn")) C.FailsafeAction = ECireWaveFailsafe::Despawn;
            else { Error = TEXT("failsafe.action must be \"march\" or \"despawn\"."); return false; }
        }
    }
    // monster-races: skill schedule and race campaign.
    const TSharedPtr<FJsonObject>* Skills = nullptr;
    if (Root->TryGetObjectField(TEXT("skillProgression"), Skills) && Skills)
    {
        auto& S = C.Skills;
        S.FirstSkillWave = static_cast<int32>(Num(*Skills, TEXT("firstSkillWave"), S.FirstSkillWave));
        S.UnlockEveryWaves = static_cast<int32>(Num(*Skills, TEXT("unlockEveryWaves"), S.UnlockEveryWaves));
        S.MaxSkills = static_cast<int32>(Num(*Skills, TEXT("maxSkills"), S.MaxSkills));
        S.TierEveryWaves = static_cast<int32>(Num(*Skills, TEXT("tierEveryWaves"), S.TierEveryWaves));
        S.MaxTier = static_cast<int32>(Num(*Skills, TEXT("maxTier"), S.MaxTier));
        S.TierDamage = static_cast<float>(Num(*Skills, TEXT("tierDamage"), S.TierDamage));
        S.TierCooldown = static_cast<float>(Num(*Skills, TEXT("tierCooldown"), S.TierCooldown));
        S.TierDuration = static_cast<float>(Num(*Skills, TEXT("tierDuration"), S.TierDuration));
    }
    const TSharedPtr<FJsonObject>* Campaign = nullptr;
    if (Root->TryGetObjectField(TEXT("campaign"), Campaign) && Campaign)
    {
        auto& K = C.Campaign;
        const TArray<TSharedPtr<FJsonValue>>* Rotation = nullptr;
        if ((*Campaign)->TryGetArrayField(TEXT("raceRotation"), Rotation) && Rotation)
            for (const auto& V : *Rotation) { FString S; if (!V->TryGetString(S) || S.IsEmpty()) { Error = TEXT("campaign.raceRotation must list race ids."); return false; } K.RaceRotation.Add(S); }
        K.bReskinOnWrap = Flag(*Campaign, TEXT("reskinOnWrap"), K.bReskinOnWrap);
        K.VeteranFromCycle = static_cast<int32>(Num(*Campaign, TEXT("veteranFromCycle"), K.VeteranFromCycle));
        K.EliteFromCycle = static_cast<int32>(Num(*Campaign, TEXT("eliteFromCycle"), K.EliteFromCycle));
        K.ChampionFromCycle = static_cast<int32>(Num(*Campaign, TEXT("championFromCycle"), K.ChampionFromCycle));
        K.MythicBossFromCycle = static_cast<int32>(Num(*Campaign, TEXT("mythicBossFromCycle"), K.MythicBossFromCycle));
        K.PromoteEvery = static_cast<int32>(Num(*Campaign, TEXT("promoteEvery"), K.PromoteEvery));
    }
    // monster-expansion: one wave object parser shared by waves[] and bonusWave.wave.
    auto ParseWave = [&](const TSharedPtr<FJsonObject>& WObj, FCireWaveDef& W) -> bool
    {
        const TSharedPtr<FJsonObject>* WO = &WObj;
        FString TypeText = TEXT("custom");
        (*WO)->TryGetStringField(TEXT("type"), TypeText);
        if (!ParseType(TypeText, W.Type)) { Error = FString::Printf(TEXT("Unknown wave type '%s'."), *TypeText); return false; }
        (*WO)->TryGetStringField(TEXT("label"), W.Label);
        { FString Race; if ((*WO)->TryGetStringField(TEXT("race"), Race) && !Race.IsEmpty() && Race != TEXT("rotation")) W.Race = FName(*Race); } // monster-races
        W.SpawnInterval = static_cast<float>(Num(*WO, TEXT("spawnInterval"), W.SpawnInterval));
        W.DelayBefore = static_cast<float>(Num(*WO, TEXT("delayBefore"), W.DelayBefore));
        W.bMustClear = Flag(*WO, TEXT("mustClear"), W.bMustClear);
        W.RewardMultiplier = static_cast<float>(Num(*WO, TEXT("rewardMultiplier"), W.RewardMultiplier));
        const TArray<TSharedPtr<FJsonValue>>* Units = nullptr;
        if (!(*WO)->TryGetArrayField(TEXT("units"), Units) || !Units) { Error = FString::Printf(TEXT("Wave '%s' needs a \"units\" array."), *W.Label); return false; }
        for (const auto& UV : *Units)
        {
            const TSharedPtr<FJsonObject>* UO = nullptr;
            if (!UV || !UV->TryGetObject(UO) || !UO) { Error = TEXT("Every composition row must be an object."); return false; }
            FCireWaveUnit U; FString Id;
            if (!(*UO)->TryGetStringField(TEXT("archetype"), Id)) { Error = TEXT("Composition rows need an \"archetype\"."); return false; }
            U.Archetype = FName(*Id);
            U.Count = static_cast<int32>(Num(*UO, TEXT("count"), U.Count));
            U.HealthScale = static_cast<float>(Num(*UO, TEXT("health"), U.HealthScale));
            U.DamageScale = static_cast<float>(Num(*UO, TEXT("damage"), U.DamageScale));
            U.SizeScale = static_cast<float>(Num(*UO, TEXT("size"), U.SizeScale));
            U.bElite = Flag(*UO, TEXT("elite"), false);
            U.bNonAttacking = Flag(*UO, TEXT("nonAttacking"), false);
            U.bEscortee = Flag(*UO, TEXT("escortee"), false);
            U.bBoss = Flag(*UO, TEXT("boss"), false);
            U.bRare = Flag(*UO, TEXT("rare"), false); // monster-expansion
            U.LeakCost = static_cast<int32>(Num(*UO, TEXT("leakCost"), 0));
            // monster-races
            FString Text;
            if ((*UO)->TryGetStringField(TEXT("slot"), Text) && !Text.IsEmpty()) U.Slot = FName(*Text);
            if ((*UO)->TryGetStringField(TEXT("rank"), Text) && !CireRaces::ParseRank(Text, U.Rank)) { Error = FString::Printf(TEXT("Unknown rank '%s'."), *Text); return false; }
            U.Palette = static_cast<int32>(Num(*UO, TEXT("palette"), -1));
            U.SkillCount = static_cast<int32>(Num(*UO, TEXT("skills"), -1));
            U.SkillTier = static_cast<int32>(Num(*UO, TEXT("skillTier"), 0));
            W.Units.Add(U);
        }
        return true;
    };
    // monster-expansion: rare spawns and the bonus loot wave (absent = defaults, so older files keep working).
    const FCireWaveConfig Builtin = CireWaveDirector::Defaults();
    C.Rare = Builtin.Rare; C.Bonus = Builtin.Bonus;
    const TSharedPtr<FJsonObject>* RareObj = nullptr;
    if (Root->TryGetObjectField(TEXT("rareSpawn"), RareObj) && RareObj)
    {
        auto& Rr = C.Rare;
        Rr.bEnabled = Flag(*RareObj, TEXT("enabled"), Rr.bEnabled);
        Rr.Chance = static_cast<float>(Num(*RareObj, TEXT("chance"), Rr.Chance));
        Rr.FromWave = static_cast<int32>(Num(*RareObj, TEXT("fromWave"), Rr.FromWave));
        Rr.MaxPerCycle = static_cast<int32>(Num(*RareObj, TEXT("maxPerCycle"), Rr.MaxPerCycle));
        Rr.Health = static_cast<float>(Num(*RareObj, TEXT("health"), Rr.Health));
        Rr.Damage = static_cast<float>(Num(*RareObj, TEXT("damage"), Rr.Damage));
        Rr.Size = static_cast<float>(Num(*RareObj, TEXT("size"), Rr.Size));
        Rr.Bounty = static_cast<float>(Num(*RareObj, TEXT("bounty"), Rr.Bounty));
        const TArray<TSharedPtr<FJsonValue>>* Pool = nullptr;
        if ((*RareObj)->TryGetArrayField(TEXT("pool"), Pool) && Pool)
        {
            Rr.Pool.Reset();
            for (const auto& V : *Pool) { FString Id; if (V->TryGetString(Id) && !Id.IsEmpty()) Rr.Pool.Add(FName(*Id)); }
        }
    }
    const TSharedPtr<FJsonObject>* BonusObj = nullptr;
    if (Root->TryGetObjectField(TEXT("bonusWave"), BonusObj) && BonusObj)
    {
        auto& B = C.Bonus;
        B.bEnabled = Flag(*BonusObj, TEXT("enabled"), B.bEnabled);
        B.Chance = static_cast<float>(Num(*BonusObj, TEXT("chance"), B.Chance));
        B.FromWave = static_cast<int32>(Num(*BonusObj, TEXT("fromWave"), B.FromWave));
        B.MaxPerCycle = static_cast<int32>(Num(*BonusObj, TEXT("maxPerCycle"), B.MaxPerCycle));
        B.ExtraBreatherSeconds = static_cast<float>(Num(*BonusObj, TEXT("extraBreatherSeconds"), B.ExtraBreatherSeconds));
        B.EscapeSeconds = static_cast<float>(Num(*BonusObj, TEXT("escapeSeconds"), B.EscapeSeconds));
        B.FleeRadius = static_cast<float>(Num(*BonusObj, TEXT("fleeRadius"), B.FleeRadius));
        B.Bounty = static_cast<float>(Num(*BonusObj, TEXT("bounty"), B.Bounty));
        const TSharedPtr<FJsonObject>* WaveObj = nullptr;
        if ((*BonusObj)->TryGetObjectField(TEXT("wave"), WaveObj) && WaveObj) { FCireWaveDef W; if (!ParseWave(*WaveObj, W)) return false; B.Wave = W; }
    }
    const TArray<TSharedPtr<FJsonValue>>* Waves = nullptr;
    if (!Root->TryGetArrayField(TEXT("waves"), Waves) || !Waves) { Error = TEXT("Waves.json needs a \"waves\" array."); return false; }
    for (const auto& Value : *Waves)
    {
        const TSharedPtr<FJsonObject>* WO = nullptr;
        if (!Value || !Value->TryGetObject(WO) || !WO) { Error = TEXT("Every wave must be an object."); return false; }
        FCireWaveDef W;
        if (!ParseWave(*WO, W)) return false;
        C.Waves.Add(MoveTemp(W));
    }
    if (!Validate(C, &Error, true)) return false;
    Out = MoveTemp(C);
    Error.Reset();
    return true;
}

FString CireWaveDirector::ToJson(const FCireWaveConfig& C)
{
    auto Root = MakeShared<FJsonObject>();
    Root->SetNumberField(TEXT("schemaVersion"), 1);
    Root->SetStringField(TEXT("_comment"), TEXT("Wave composer data (Docs/Waves.md). Edited live with F8 > Waves; counts are per lane."));
    Root->SetNumberField(TEXT("breatherSeconds"), C.BreatherSeconds);
    Root->SetNumberField(TEXT("wavesPerCycle"), C.WavesPerCycle);
    Root->SetNumberField(TEXT("cycles"), C.Cycles);
    auto Scaling = MakeShared<FJsonObject>();
    Scaling->SetNumberField(TEXT("healthGrowth"), C.CycleHealthGrowth);
    Scaling->SetNumberField(TEXT("damageGrowth"), C.CycleDamageGrowth);
    Scaling->SetNumberField(TEXT("extraUnits"), C.CycleExtraUnits);
    Root->SetObjectField(TEXT("cycleScaling"), Scaling);
    auto Failsafe = MakeShared<FJsonObject>();
    Failsafe->SetBoolField(TEXT("enabled"), C.bStallFailsafe);
    Failsafe->SetNumberField(TEXT("maxWaveSeconds"), C.MaxWaveSeconds);
    Failsafe->SetStringField(TEXT("action"), C.FailsafeAction == ECireWaveFailsafe::Despawn ? TEXT("despawn") : TEXT("march"));
    Failsafe->SetNumberField(TEXT("graceSeconds"), C.FailsafeGraceSeconds);
    Failsafe->SetNumberField(TEXT("stuckSeconds"), C.StuckSeconds);
    Root->SetObjectField(TEXT("failsafe"), Failsafe);
    auto Pacing = MakeShared<FJsonObject>();
    Pacing->SetStringField(TEXT("_comment"), TEXT("breatherSeconds is the Skill Shop window after each cleared wave; earlyContinue ends it once every human is Ready. Waves appear spawnAlongRoute of the way down the road and walk marchSpeed x faster while not fighting (Docs/Waves.md)."));
    Pacing->SetNumberField(TEXT("spawnAlongRoute"), C.SpawnAlongRoute);
    Pacing->SetNumberField(TEXT("marchSpeed"), C.MarchSpeedMultiplier);
    Pacing->SetNumberField(TEXT("firstWaveDelay"), C.FirstWaveDelay);
    Pacing->SetBoolField(TEXT("earlyContinue"), C.bEarlyContinue);
    Pacing->SetNumberField(TEXT("prepSeconds"), C.PrepSeconds);
    Pacing->SetNumberField(TEXT("arenaSeconds"), C.ArenaSeconds);
    Pacing->SetNumberField(TEXT("recoverySeconds"), C.RecoverySeconds);
    Root->SetObjectField(TEXT("pacing"), Pacing);
    // monster-races
    auto Skills = MakeShared<FJsonObject>();
    Skills->SetStringField(TEXT("_comment"), TEXT("Monster skills: none before firstSkillWave (global wave number); then 1 skill, +1 every unlockEveryWaves up to maxSkills (+ rank bonus); tier II/III every tierEveryWaves (Docs/Races.md)."));
    Skills->SetNumberField(TEXT("firstSkillWave"), C.Skills.FirstSkillWave);
    Skills->SetNumberField(TEXT("unlockEveryWaves"), C.Skills.UnlockEveryWaves);
    Skills->SetNumberField(TEXT("maxSkills"), C.Skills.MaxSkills);
    Skills->SetNumberField(TEXT("tierEveryWaves"), C.Skills.TierEveryWaves);
    Skills->SetNumberField(TEXT("maxTier"), C.Skills.MaxTier);
    Skills->SetNumberField(TEXT("tierDamage"), C.Skills.TierDamage);
    Skills->SetNumberField(TEXT("tierCooldown"), C.Skills.TierCooldown);
    Skills->SetNumberField(TEXT("tierDuration"), C.Skills.TierDuration);
    Root->SetObjectField(TEXT("skillProgression"), Skills);
    auto Campaign = MakeShared<FJsonObject>();
    Campaign->SetStringField(TEXT("_comment"), TEXT("Race per cycle (wraps; 'a+b' mixes races row by row). Rows with a slot follow the wave's race. The second lap reskins with palette variant 1, and so on."));
    TArray<TSharedPtr<FJsonValue>> Rotation;
    for (const FString& Race : C.Campaign.RaceRotation) Rotation.Add(MakeShared<FJsonValueString>(Race));
    Campaign->SetArrayField(TEXT("raceRotation"), Rotation);
    Campaign->SetBoolField(TEXT("reskinOnWrap"), C.Campaign.bReskinOnWrap);
    Campaign->SetNumberField(TEXT("veteranFromCycle"), C.Campaign.VeteranFromCycle);
    Campaign->SetNumberField(TEXT("eliteFromCycle"), C.Campaign.EliteFromCycle);
    Campaign->SetNumberField(TEXT("championFromCycle"), C.Campaign.ChampionFromCycle);
    Campaign->SetNumberField(TEXT("mythicBossFromCycle"), C.Campaign.MythicBossFromCycle);
    Campaign->SetNumberField(TEXT("promoteEvery"), C.Campaign.PromoteEvery);
    Root->SetObjectField(TEXT("campaign"), Campaign);
    auto WaveJson = [](const FCireWaveDef& W)
    {
        auto WO = MakeShared<FJsonObject>();
        WO->SetStringField(TEXT("label"), W.Label);
        WO->SetStringField(TEXT("type"), TypeName(W.Type));
        if (!W.Race.IsNone()) WO->SetStringField(TEXT("race"), W.Race.ToString()); // monster-races
        WO->SetNumberField(TEXT("spawnInterval"), W.SpawnInterval);
        WO->SetNumberField(TEXT("delayBefore"), W.DelayBefore);
        WO->SetBoolField(TEXT("mustClear"), W.bMustClear);
        WO->SetNumberField(TEXT("rewardMultiplier"), W.RewardMultiplier);
        TArray<TSharedPtr<FJsonValue>> Units;
        for (const auto& U : W.Units)
        {
            auto UO = MakeShared<FJsonObject>();
            UO->SetStringField(TEXT("archetype"), U.Archetype.ToString());
            UO->SetNumberField(TEXT("count"), U.Count);
            UO->SetNumberField(TEXT("health"), U.HealthScale);
            UO->SetNumberField(TEXT("damage"), U.DamageScale);
            UO->SetNumberField(TEXT("size"), U.SizeScale);
            if (U.bElite) UO->SetBoolField(TEXT("elite"), true);
            if (U.bNonAttacking) UO->SetBoolField(TEXT("nonAttacking"), true);
            if (U.bEscortee) UO->SetBoolField(TEXT("escortee"), true);
            if (U.bBoss) UO->SetBoolField(TEXT("boss"), true);
            if (U.LeakCost > 0) UO->SetNumberField(TEXT("leakCost"), U.LeakCost);
            // monster-races
            if (!U.Slot.IsNone()) UO->SetStringField(TEXT("slot"), U.Slot.ToString());
            if (U.Rank != ECireNPCRank::Normal) UO->SetStringField(TEXT("rank"), CireRaces::RankId(U.Rank));
            if (U.Palette >= 0) UO->SetNumberField(TEXT("palette"), U.Palette);
            if (U.SkillCount >= 0) UO->SetNumberField(TEXT("skills"), U.SkillCount);
            if (U.SkillTier > 0) UO->SetNumberField(TEXT("skillTier"), U.SkillTier);
            if (U.bRare) UO->SetBoolField(TEXT("rare"), true); // monster-expansion
            Units.Add(MakeShared<FJsonValueObject>(UO));
        }
        WO->SetArrayField(TEXT("units"), Units);
        return WO;
    };
    // monster-expansion: rare spawns and the bonus loot wave.
    {
        auto Rare = MakeShared<FJsonObject>();
        Rare->SetStringField(TEXT("_comment"), TEXT("Rare Spawn: from fromWave, each normal/pack wave has `chance` to add one rare creature from `pool` (both lanes, at most maxPerCycle per cycle). It glows, wears a 'Rare' plate, is tougher (health/damage/size x) and pays `bounty` mob values plus a personal rare chest (LootTables.json sources.rareSpawn). Docs/MonsterExpansion.md."));
        Rare->SetBoolField(TEXT("enabled"), C.Rare.bEnabled);
        Rare->SetNumberField(TEXT("chance"), C.Rare.Chance);
        Rare->SetNumberField(TEXT("fromWave"), C.Rare.FromWave);
        Rare->SetNumberField(TEXT("maxPerCycle"), C.Rare.MaxPerCycle);
        Rare->SetNumberField(TEXT("health"), C.Rare.Health);
        Rare->SetNumberField(TEXT("damage"), C.Rare.Damage);
        Rare->SetNumberField(TEXT("size"), C.Rare.Size);
        Rare->SetNumberField(TEXT("bounty"), C.Rare.Bounty);
        TArray<TSharedPtr<FJsonValue>> Pool;
        for (const FName Id : C.Rare.Pool) Pool.Add(MakeShared<FJsonValueString>(Id.ToString()));
        Rare->SetArrayField(TEXT("pool"), Pool);
        Root->SetObjectField(TEXT("rareSpawn"), Rare);
        auto Bonus = MakeShared<FJsonObject>();
        Bonus->SetStringField(TEXT("_comment"), TEXT("Bonus Loot Wave: after a cleared wave (never the cycle's last), from fromWave, `chance` to run `wave` during the breather (at most maxPerCycle). Its creatures flee champions closer than fleeRadius, never attack, never cost lives and escape after escapeSeconds. Each pays `bounty` mob values plus a personal chest (LootTables.json sources.bonusWave). The breather grows by extraBreatherSeconds only when it runs. Docs/MonsterExpansion.md."));
        Bonus->SetBoolField(TEXT("enabled"), C.Bonus.bEnabled);
        Bonus->SetNumberField(TEXT("chance"), C.Bonus.Chance);
        Bonus->SetNumberField(TEXT("fromWave"), C.Bonus.FromWave);
        Bonus->SetNumberField(TEXT("maxPerCycle"), C.Bonus.MaxPerCycle);
        Bonus->SetNumberField(TEXT("extraBreatherSeconds"), C.Bonus.ExtraBreatherSeconds);
        Bonus->SetNumberField(TEXT("escapeSeconds"), C.Bonus.EscapeSeconds);
        Bonus->SetNumberField(TEXT("fleeRadius"), C.Bonus.FleeRadius);
        Bonus->SetNumberField(TEXT("bounty"), C.Bonus.Bounty);
        Bonus->SetObjectField(TEXT("wave"), WaveJson(C.Bonus.Wave));
        Root->SetObjectField(TEXT("bonusWave"), Bonus);
    }
    TArray<TSharedPtr<FJsonValue>> Waves;
    for (const auto& W : C.Waves) Waves.Add(MakeShared<FJsonValueObject>(WaveJson(W)));
    Root->SetArrayField(TEXT("waves"), Waves);
    FString Out;
    auto Writer = TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create(&Out);
    FJsonSerializer::Serialize(Root, Writer);
    return Out + TEXT("\n");
}

// monster-races: F8 editor operations (also used by the native tests).
void CireWaveDirector::CycleWaveRace(FCireWaveDef& W)
{
    const TArray<FName>& Order = CireRaces::Get().Order;
    if (Order.IsEmpty()) { W.Race = NAME_None; return; }
    const int32 Index = W.Race.IsNone() ? -1 : Order.IndexOfByKey(W.Race);
    W.Race = Index + 1 >= Order.Num() ? NAME_None : Order[Index + 1];
}
void CireWaveDirector::CycleRowUnit(FCireWaveUnit& U, FName Race)
{
    const FCireRace* R = CireRaces::FindRace(Race.IsNone() ? FName(TEXT("hollow")) : Race);
    const TArray<FName>& Slots = CireRaces::SlotNames();
    const FName Hollow(TEXT("hollow"));
    auto SetSlot = [&](FName Slot)
    {
        U.Slot = Slot;
        const FName Id = CireRaces::UnitFor(R ? R->Id : Hollow, Slot, 0);
        if (!Id.IsNone()) U.Archetype = Id;
        U.bBoss = Slot == TEXT("warlord") || Slot == TEXT("colossus") || Slot == TEXT("boss");
        if (U.bBoss) { U.bNonAttacking = false; U.bEscortee = false; }
    };
    if (!U.Slot.IsNone())
    {
        const int32 Index = Slots.IndexOfByKey(U.Slot);
        if (Index + 1 < Slots.Num()) { SetSlot(Slots[Index + 1]); return; }
        // After the slots: explicit units of the race, starting with its first unit.
        U.Slot = NAME_None;
        if (R && !R->Units.IsEmpty()) { U.Archetype = R->Units[0]; U.bBoss = CireNPCArchetypes::Find(U.Archetype) && CireNPCArchetypes::Find(U.Archetype)->Classification == ECireNPCClass::Boss; }
        return;
    }
    const int32 Index = R ? R->Units.IndexOfByKey(U.Archetype) : INDEX_NONE;
    if (R && Index != INDEX_NONE && Index + 1 < R->Units.Num())
    {
        U.Archetype = R->Units[Index + 1];
        const auto* A = CireNPCArchetypes::Find(U.Archetype);
        U.bBoss = A && A->Classification == ECireNPCClass::Boss;
        if (U.bBoss) { U.bNonAttacking = false; U.bEscortee = false; }
        return;
    }
    SetSlot(Slots[0]);
}
void CireWaveDirector::CycleRowRank(FCireWaveUnit& U)
{
    U.Rank = static_cast<ECireNPCRank>((static_cast<int32>(U.Rank) + 1) % static_cast<int32>(ECireNPCRank::Count));
    U.bElite = false; // the rank replaces the legacy flag
}
FString CireWaveDirector::RowUnitLabel(const FCireWaveUnit& U, FName Race, int32 Cycle)
{
    const FName Id = U.Slot.IsNone() ? U.Archetype : CireRaces::UnitFor(Race.IsNone() ? FName(TEXT("hollow")) : Race, U.Slot, Cycle);
    const auto* A = CireNPCArchetypes::Find(Id.IsNone() ? U.Archetype : Id);
    FString Name = A ? A->DisplayName : U.Archetype.ToString();
    Name.ReplaceInline(TEXT(", Pack Leader"), TEXT(""));
    if (U.Slot.IsNone()) return Name;
    FString Slot = U.Slot.ToString(); Slot[0] = FChar::ToUpper(Slot[0]);
    return Slot + TEXT(": ") + Name;
}

FString CireWaveDirector::DataPath() { return FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir() / TEXT("Data/Waves.json")); }

bool CireWaveDirector::LoadFile(FCireWaveConfig& Out, FString* Error, const FString& Path)
{
    FString Json, Why;
    const FString File = Path.IsEmpty() ? DataPath() : Path;
    if (!FFileHelper::LoadFileToString(Json, *File)) { if (Error) *Error = FString::Printf(TEXT("%s could not be read."), *FPaths::GetCleanFilename(File)); return false; }
    if (!ParseJson(Json, Out, Why)) { if (Error) *Error = Why; return false; }
    if (Error) Error->Reset();
    return true;
}

bool CireWaveDirector::SaveFile(const FCireWaveConfig& Config, FString* Error, const FString& Path)
{
    FCireWaveConfig Copy = Config;
    if (!Validate(Copy, Error, true)) return false;
    const FString File = Path.IsEmpty() ? DataPath() : Path;
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(File), true);
    if (!FFileHelper::SaveStringToFile(ToJson(Copy), *File, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
    { if (Error) *Error = TEXT("Waves.json could not be written."); return false; }
    if (Error) Error->Reset();
    return true;
}
