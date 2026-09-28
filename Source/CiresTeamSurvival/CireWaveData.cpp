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
        SkillTier == O.SkillTier && bRare == O.bRare && Pack == O.Pack; // monster-expansion, waves-modes
}
// waves-modes
bool FCireMatchSchedule::operator==(const FCireMatchSchedule& O) const
{
    return TotalWaves == O.TotalWaves && PvpAfterWaves == O.PvpAfterWaves && Near(SuddenDeathHealth, O.SuddenDeathHealth) &&
        Near(SuddenDeathDamage, O.SuddenDeathDamage) && SuddenDeathLoop == O.SuddenDeathLoop;
}
bool FCireWaveMonsterRules::operator==(const FCireWaveMonsterRules& O) const
{
    return Near(Speed, O.Speed) && Near(ArmoredSpeed, O.ArmoredSpeed) && bArmoredSlowImmune == O.bArmoredSlowImmune &&
        Near(ArmoredStunMultiplier, O.ArmoredStunMultiplier) && Near(PackGapSeconds, O.PackGapSeconds);
}
bool FCireWaveScale::operator==(const FCireWaveScale& O) const { return Near(Health, O.Health) && Near(Damage, O.Damage) && Near(Speed, O.Speed); }
bool FCireWavePreset::operator==(const FCireWavePreset& O) const
{
    return Id == O.Id && Label == O.Label && Description == O.Description && bDefaultDamage == O.bDefaultDamage && DefaultFightBack == O.DefaultFightBack &&
        Waves == O.Waves && Scale == O.Scale && PackSizeBonus == O.PackSizeBonus && PvpAfterWaves == O.PvpAfterWaves && KitProfile == O.KitProfile;
}
bool FCireRareSpawnRules::operator==(const FCireRareSpawnRules& O) const
{
    return bEnabled == O.bEnabled && Near(Chance, O.Chance) && FromWave == O.FromWave && MaxPerCycle == O.MaxPerCycle && Near(Health, O.Health) &&
        Near(Damage, O.Damage) && Near(Size, O.Size) && Near(Bounty, O.Bounty) && Pool == O.Pool;
}
FCireBonusWaveRules::FCireBonusWaveRules() { Wave = CireWaveDirector::BonusTemplate(); }
bool FCireBonusWaveRules::operator==(const FCireBonusWaveRules& O) const
{
    return bEnabled == O.bEnabled && Near(Chance, O.Chance) && Near(ReplaceChance, O.ReplaceChance) && bEscapeTimerOnHit == O.bEscapeTimerOnHit && FromWave == O.FromWave && MaxPerCycle == O.MaxPerCycle &&
        Near(ExtraBreatherSeconds, O.ExtraBreatherSeconds) && Near(EscapeSeconds, O.EscapeSeconds) && Near(FleeRadius, O.FleeRadius) &&
        Near(Bounty, O.Bounty) && Wave == O.Wave;
}
int32 FCireWaveDef::UnitsPerLane() const { int32 N = 0; for (const auto& U : Units) N += U.Count; return N; }
bool FCireWaveDef::operator==(const FCireWaveDef& O) const
{
    return Label == O.Label && Type == O.Type && Units == O.Units && Near(SpawnInterval, O.SpawnInterval) && Near(DelayBefore, O.DelayBefore) &&
        bMustClear == O.bMustClear && Near(RewardMultiplier, O.RewardMultiplier) && Race == O.Race && // monster-races: race
        Packs == O.Packs && PackSizeMin == O.PackSizeMin && PackSizeMax == O.PackSizeMax && bDealsDamage == O.bDealsDamage && FightBackPacks == O.FightBackPacks; // waves-modes
}
bool FCireWaveConfig::operator==(const FCireWaveConfig& O) const
{
    return Near(BreatherSeconds, O.BreatherSeconds) && WavesPerCycle == O.WavesPerCycle && Cycles == O.Cycles &&
        Near(CycleHealthGrowth, O.CycleHealthGrowth) && Near(CycleDamageGrowth, O.CycleDamageGrowth) && CycleExtraUnits == O.CycleExtraUnits &&
        bStallFailsafe == O.bStallFailsafe && Near(MaxWaveSeconds, O.MaxWaveSeconds) && FailsafeAction == O.FailsafeAction &&
        Near(FailsafeGraceSeconds, O.FailsafeGraceSeconds) && Near(StuckSeconds, O.StuckSeconds) && Waves == O.Waves &&
        Near(SpawnAlongRoute, O.SpawnAlongRoute) && Near(MarchSpeedMultiplier, O.MarchSpeedMultiplier) && Near(FirstWaveDelay, O.FirstWaveDelay) && // pacing
        Near(RallySpeed, O.RallySpeed) && Near(RallyRadius, O.RallyRadius) && Near(BotHoldAt, O.BotHoldAt) && Near(MarcherSpeed, O.MarcherSpeed) && // world-scale
        Near(PrepSeconds, O.PrepSeconds) && Near(ArenaSeconds, O.ArenaSeconds) && Near(RecoverySeconds, O.RecoverySeconds) && bEarlyContinue == O.bEarlyContinue &&
        Skills == O.Skills && Campaign == O.Campaign && bCampaignOrder == O.bCampaignOrder &&
        Rare == O.Rare && Bonus == O.Bonus && // monster-races, rules-conformance, monster-expansion
        Match == O.Match && Monsters == O.Monsters && Live == O.Live && PackSizeBonus == O.PackSizeBonus && Preset == O.Preset; // waves-modes
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
    // rules-conformance: the default match is a 15-wave campaign played in order (waveOrder "campaign"): cycle 1 is the
    // tuned opening (normal, normal, armored, armored escort, boss); cycles 2 and 3 open with the Melee / Caster and
    // Ranged / Hybrid packs, and every cycle keeps its armored march, its Armored Escort and its boss.
    auto Pack = [](ECireWaveType Type, const TCHAR* Label, TArray<FCireWaveUnit> Units)
    { FCireWaveDef W = Template(Type); W.Label = Label; W.Units = MoveTemp(Units); return W; };
    // pacing (bots-only soak): threat is never dropped any more (no leash, no failsafe on fighting units), and later
    // cycles carry champion ranks, mythic bosses and more monster skills, so cycles 2 and 3 carry less health per unit
    // (Eric: tune the pace with wave HP, not spawn points).
    const FCireWaveDef Melee = Pack(ECireWaveType::MeleePack, TEXT("Shield Wall"), {Unit(TEXT("hollow_shieldbearer"), 1, .8f, 1.35f),
        Unit(TEXT("hollow_infantry"), 3, .8f, 1.35f), Unit(TEXT("ironbound_bruiser"), 3, .8f, 1.35f)});
    const FCireWaveDef Caster = Pack(ECireWaveType::CasterPack, TEXT("Hex Circle"), {Unit(TEXT("hollow_shieldbearer"), 1, .75f, 1.35f),
        Unit(TEXT("blight_caster"), 5, .75f, 1.35f)});
    const FCireWaveDef Ranged = Pack(ECireWaveType::RangedPack, TEXT("Arrow Storm"), {Unit(TEXT("hollow_shieldbearer"), 1, .7f, 1.35f),
        Unit(TEXT("barbed_hunter"), 5, .7f, 1.35f)});
    const FCireWaveDef Hybrid = Pack(ECireWaveType::HybridPack, TEXT("Warband"), {Unit(TEXT("hollow_shieldbearer"), 1, .6f, 1.4f),
        Unit(TEXT("hollow_infantry"), 2, .6f, 1.4f), Unit(TEXT("ironbound_bruiser"), 2, .6f, 1.4f), Unit(TEXT("blight_caster"), 1, .6f, 1.4f),
        Unit(TEXT("barbed_hunter"), 1, .6f, 1.4f), Unit(TEXT("grave_hound"), 2, .6f, 1.4f)});
    const FCireWaveDef Armored = Template(ECireWaveType::Armored), Escort = Template(ECireWaveType::ArmoredEscort), Boss = Template(ECireWaveType::Boss);
    FCireWaveDef MidEscort = Escort, MidBoss = Boss, LateEscort = Escort, LateBoss = Boss;
    for (auto& U : MidEscort.Units) if (!U.bEscortee) U.HealthScale = .85f;
    for (auto& U : MidBoss.Units) if (!U.bBoss) U.HealthScale = .85f;
    for (auto& U : LateEscort.Units) U.HealthScale = U.bEscortee ? 3.f : .7f;
    for (auto& U : LateBoss.Units) U.HealthScale = U.bBoss ? .2f : .7f;
    // waves-modes (playtest 6): a 25-wave match of five 5-wave cycles; every cycle keeps its armored march, its escort and
    // its boss. Cycles 4 and 5 re-run the pack waves under new names (the race rotation and the cycle growth change them).
    auto Renamed = [](FCireWaveDef W, const TCHAR* Label) { W.Label = Label; return W; };
    FCireWaveDef Armor = Armored;
    for (auto& U : Armor.Units) U.LeakCost = 1; // 25-49 armored marchers per wave: one life each (was 2 with 4 per wave)
    C.Waves = {One, Two, Armor, Escort, Boss,
               Melee, Caster, Armor, MidEscort, MidBoss,
               Ranged, Hybrid, Armor, LateEscort, LateBoss,
               Renamed(Melee, TEXT("Iron Vanguard")), Renamed(Caster, TEXT("Coven Rising")), Armor, LateEscort, LateBoss,
               Renamed(Ranged, TEXT("Storm of Arrows")), Renamed(Hybrid, TEXT("Grand Warband")), Armor, LateEscort, LateBoss};
    // waves-modes: waves 1-5 are 5 packs of 5 (25 monsters); later waves are 7 packs of 5-7 (35-49). The authored rows are
    // each pack's recipe; the pack-size modifier (packSizeBonus / the preset) grows or shrinks every pack.
    for (int32 I = 0; I < C.Waves.Num(); ++I)
    {
        FCireWaveDef& W = C.Waves[I];
        W.Packs = I < 5 ? 5 : 7; W.PackSizeMin = 5; W.PackSizeMax = I < 5 ? 5 : 7;
    }
    C.WavesPerCycle = 5;
    C.Cycles = 5; // waves-modes: 25 regular waves; Sudden Death waves follow until a team runs out of lives
    C.bCampaignOrder = true;
    // rules-conformance: the race changes every wave (campaign.rotateEvery "wave"), so a default 3-cycle match fields all
    // ten races: the hollow open the breach, Eric's favourites (Blightwood, the Drowned Deep) arrive early and return as
    // the cycle-2 and cycle-3 bosses, and the Aetheri (the construct race) show up in cycles 2 and 3.
    C.Campaign.RaceRotation = {TEXT("hollow"), TEXT("blightwood"), TEXT("ironhide"), TEXT("drowned_deep"), TEXT("hollow"),
        TEXT("stoneborn"), TEXT("aetheri"), TEXT("feral_kin"), TEXT("drakkari"), TEXT("blightwood"),
        TEXT("voidborn"), TEXT("fallen_order"), TEXT("aetheri+ironhide"), TEXT("stoneborn+feral_kin"), TEXT("drowned_deep")};
    // waves-modes: the 15-entry rotation wraps for waves 16-25 with palette variant 1 (reskinOnWrap).
    C.Campaign.bRotatePerWave = true;
    // rules-conformance: every rank is reachable in 3 cycles (veteran from cycle 2; elite and champion, alternating, in cycle 3).
    C.Campaign.VeteranFromCycle = 2; C.Campaign.EliteFromCycle = 3; C.Campaign.ChampionFromCycle = 3;
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
    C.RallySpeed = ClampF(C.RallySpeed, 1, 4, 1); // world-scale
    C.RallyRadius = ClampF(C.RallyRadius, 500, 10000, 3000);
    C.BotHoldAt = ClampF(C.BotHoldAt, .2f, .95f, .8f);
    C.MarcherSpeed = ClampF(C.MarcherSpeed, .5f, 4, 1);
    C.FirstWaveDelay = ClampF(C.FirstWaveDelay, 0, 120, 8);
    C.PrepSeconds = ClampF(C.PrepSeconds, 5, 600, 30);
    C.ArenaSeconds = ClampF(C.ArenaSeconds, 15, 900, 60);
    C.RecoverySeconds = ClampF(C.RecoverySeconds, 1, 180, 10);
    // waves-modes: match schedule, monster rules, live scale, pack-size modifier.
    {
        auto& M = C.Match;
        M.TotalWaves = FMath::Clamp(M.TotalWaves, 0, 200);
        M.PvpAfterWaves.RemoveAll([](int32 W) { return W < 1 || W > 500; });
        M.PvpAfterWaves.Sort();
        for (int32 I = M.PvpAfterWaves.Num() - 1; I > 0; --I) if (M.PvpAfterWaves[I] == M.PvpAfterWaves[I - 1]) M.PvpAfterWaves.RemoveAt(I);
        if (M.PvpAfterWaves.Num() > 20) M.PvpAfterWaves.SetNum(20);
        M.SuddenDeathHealth = ClampF(M.SuddenDeathHealth, 1, 10, 2); M.SuddenDeathDamage = ClampF(M.SuddenDeathDamage, 1, 10, 2);
        M.SuddenDeathLoop = FMath::Clamp(M.SuddenDeathLoop, 1, 20);
        auto& R = C.Monsters;
        R.Speed = ClampF(R.Speed, .2f, 2, .8f); R.ArmoredSpeed = ClampF(R.ArmoredSpeed, .1f, 2, .5f);
        R.ArmoredStunMultiplier = ClampF(R.ArmoredStunMultiplier, 0, 5, 2); R.PackGapSeconds = ClampF(R.PackGapSeconds, 0, 10, 1.5f);
        C.Live.Health = ClampF(C.Live.Health, .1f, 10, 1); C.Live.Damage = ClampF(C.Live.Damage, 0, 10, 1); C.Live.Speed = ClampF(C.Live.Speed, .2f, 3, 1);
        C.PackSizeBonus = FMath::Clamp(C.PackSizeBonus, -3, 3);
        if (C.Preset.IsNone()) C.Preset = TEXT("standard");
    }
    if (C.Waves.IsEmpty()) return Fail(TEXT("At least one wave is required."));
    if (C.Waves.Num() > 40) return Fail(TEXT("At most 40 waves are allowed.")); // waves-modes: 25-wave match
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
        // waves-modes: packs (up to 8 packs of up to 8) and the damage toggle.
        W.Packs = FMath::Clamp(W.Packs, 0, 8);
        W.PackSizeMin = FMath::Clamp(W.PackSizeMin, 1, 8); W.PackSizeMax = FMath::Clamp(W.PackSizeMax, W.PackSizeMin, 8);
        W.FightBackPacks.RemoveAll([](int32 P) { return P < 1 || P > 8; });
        if (W.Packs > 0 && !W.Units.ContainsByPredicate([](const FCireWaveUnit& U) { return !U.bBoss && !U.bEscortee; })) return Fail(FString::Printf(TEXT("Wave %d has packs but no pack recipe rows."), WI + 1));
        if (W.Packs == 0 && Total > 30) return Fail(FString::Printf(TEXT("Wave %d spawns %d units per lane; the limit is 30."), WI + 1, Total));
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
        B.Chance = ClampF(B.Chance, 0, 1, 0); B.ReplaceChance = ClampF(B.ReplaceChance, 0, 1, .08f); // bonus-loot B.FromWave = FMath::Clamp(B.FromWave, 1, 200); B.MaxPerCycle = FMath::Clamp(B.MaxPerCycle, 0, 5);
        B.ExtraBreatherSeconds = ClampF(B.ExtraBreatherSeconds, 0, 60, 6); B.EscapeSeconds = ClampF(B.EscapeSeconds, 5, 240, 52);
        B.FleeRadius = ClampF(B.FleeRadius, 0, 3000, 950); B.Bounty = ClampF(B.Bounty, 0, 100, 1);
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
    {
        FString Order; // rules-conformance: "campaign" plays Waves[] straight through the match; "cycle" replays it every cycle
        if (Root->TryGetStringField(TEXT("waveOrder"), Order))
        {
            if (Order == TEXT("campaign")) C.bCampaignOrder = true;
            else if (Order == TEXT("cycle")) C.bCampaignOrder = false;
            else { Error = TEXT("waveOrder must be \"campaign\" or \"cycle\"."); return false; }
        }
    }
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
        C.RallySpeed = static_cast<float>(Num(*Pacing, TEXT("rallySpeed"), C.RallySpeed)); // world-scale
        C.RallyRadius = static_cast<float>(Num(*Pacing, TEXT("rallyRadius"), C.RallyRadius));
        C.BotHoldAt = static_cast<float>(Num(*Pacing, TEXT("botHoldAt"), C.BotHoldAt));
        C.MarcherSpeed = static_cast<float>(Num(*Pacing, TEXT("marcherSpeed"), C.MarcherSpeed));
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
        FString Every; // rules-conformance: the rotation advances per "wave" or per "cycle"
        if ((*Campaign)->TryGetStringField(TEXT("rotateEvery"), Every))
        {
            if (Every == TEXT("wave")) K.bRotatePerWave = true;
            else if (Every == TEXT("cycle")) K.bRotatePerWave = false;
            else { Error = TEXT("campaign.rotateEvery must be \"wave\" or \"cycle\"."); return false; }
        }
    }
    // waves-modes: match schedule, monster rules, live scale, pack-size modifier, active preset (absent = defaults).
    const TSharedPtr<FJsonObject>* MatchObj = nullptr;
    if (Root->TryGetObjectField(TEXT("match"), MatchObj) && MatchObj)
    {
        auto& M = C.Match;
        M.TotalWaves = static_cast<int32>(Num(*MatchObj, TEXT("totalWaves"), M.TotalWaves));
        const TArray<TSharedPtr<FJsonValue>>* Pvp = nullptr;
        if ((*MatchObj)->TryGetArrayField(TEXT("pvpAfterWaves"), Pvp) && Pvp) { M.PvpAfterWaves.Reset(); for (const auto& V : *Pvp) M.PvpAfterWaves.Add(static_cast<int32>(V->AsNumber())); }
        const TSharedPtr<FJsonObject>* Sudden = nullptr;
        if ((*MatchObj)->TryGetObjectField(TEXT("suddenDeath"), Sudden) && Sudden)
        {
            M.SuddenDeathHealth = static_cast<float>(Num(*Sudden, TEXT("health"), M.SuddenDeathHealth));
            M.SuddenDeathDamage = static_cast<float>(Num(*Sudden, TEXT("damage"), M.SuddenDeathDamage));
            M.SuddenDeathLoop = static_cast<int32>(Num(*Sudden, TEXT("loopLastWaves"), M.SuddenDeathLoop));
        }
    }
    const TSharedPtr<FJsonObject>* MonstersObj = nullptr;
    if (Root->TryGetObjectField(TEXT("monsters"), MonstersObj) && MonstersObj)
    {
        auto& R = C.Monsters;
        R.Speed = static_cast<float>(Num(*MonstersObj, TEXT("speed"), R.Speed));
        R.ArmoredSpeed = static_cast<float>(Num(*MonstersObj, TEXT("armoredSpeed"), R.ArmoredSpeed));
        R.bArmoredSlowImmune = Flag(*MonstersObj, TEXT("armoredSlowImmune"), R.bArmoredSlowImmune);
        R.ArmoredStunMultiplier = static_cast<float>(Num(*MonstersObj, TEXT("armoredStunMultiplier"), R.ArmoredStunMultiplier));
        R.PackGapSeconds = static_cast<float>(Num(*MonstersObj, TEXT("packGapSeconds"), R.PackGapSeconds));
    }
    const TSharedPtr<FJsonObject>* LiveObj = nullptr;
    if (Root->TryGetObjectField(TEXT("liveScale"), LiveObj) && LiveObj)
    {
        C.Live.Health = static_cast<float>(Num(*LiveObj, TEXT("health"), C.Live.Health));
        C.Live.Damage = static_cast<float>(Num(*LiveObj, TEXT("damage"), C.Live.Damage));
        C.Live.Speed = static_cast<float>(Num(*LiveObj, TEXT("speed"), C.Live.Speed));
    }
    C.PackSizeBonus = static_cast<int32>(Num(Root, TEXT("packSizeBonus"), C.PackSizeBonus));
    { FString Preset; if (Root->TryGetStringField(TEXT("preset"), Preset) && !Preset.IsEmpty()) C.Preset = FName(*Preset); }
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
        // waves-modes: "packs": 7, "packSize": [5, 7], "damage": true, "fightBack": [1, 4, 7]
        W.Packs = static_cast<int32>(Num(*WO, TEXT("packs"), 0));
        {
            const TArray<TSharedPtr<FJsonValue>>* Size = nullptr;
            if ((*WO)->TryGetArrayField(TEXT("packSize"), Size) && Size && Size->Num() >= 1)
            {
                W.PackSizeMin = static_cast<int32>((*Size)[0]->AsNumber());
                W.PackSizeMax = static_cast<int32>((*Size)[Size->Num() > 1 ? 1 : 0]->AsNumber());
            }
            else if ((*WO)->HasField(TEXT("packSize"))) W.PackSizeMin = W.PackSizeMax = static_cast<int32>(Num(*WO, TEXT("packSize"), 5));
        }
        W.bDealsDamage = Flag(*WO, TEXT("damage"), true);
        {
            const TArray<TSharedPtr<FJsonValue>>* Fight = nullptr;
            if ((*WO)->TryGetArrayField(TEXT("fightBack"), Fight) && Fight) for (const auto& V : *Fight) W.FightBackPacks.Add(static_cast<int32>(V->AsNumber()));
        }
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
        B.ReplaceChance = static_cast<float>(Num(*BonusObj, TEXT("replaceChance"), B.ReplaceChance)); // bonus-loot
        B.bEscapeTimerOnHit = Flag(*BonusObj, TEXT("escapeTimerOnHit"), B.bEscapeTimerOnHit);
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
    Root->SetStringField(TEXT("waveOrder"), C.bCampaignOrder ? TEXT("campaign") : TEXT("cycle")); // rules-conformance
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
    Pacing->SetStringField(TEXT("_comment"), TEXT("breatherSeconds is the Skill Shop window after each cleared wave; earlyContinue ends it once every human is Ready. Waves appear spawnAlongRoute of the way down the road and walk marchSpeed x faster while not fighting; with no defender within rallyRadius cm they hurry at rallySpeed x (the 3x-longer town); idle bots hold at botHoldAt of the route (Docs/Waves.md)."));
    Pacing->SetNumberField(TEXT("spawnAlongRoute"), C.SpawnAlongRoute);
    Pacing->SetNumberField(TEXT("marchSpeed"), C.MarchSpeedMultiplier);
    Pacing->SetNumberField(TEXT("rallySpeed"), C.RallySpeed); // world-scale
    Pacing->SetNumberField(TEXT("rallyRadius"), C.RallyRadius);
    Pacing->SetNumberField(TEXT("botHoldAt"), C.BotHoldAt);
    Pacing->SetNumberField(TEXT("marcherSpeed"), C.MarcherSpeed);
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
    Campaign->SetStringField(TEXT("_comment"), TEXT("Race per wave or per cycle (rotateEvery; wraps; 'a+b' mixes races row by row). Rows with a slot follow the wave's race. The second lap reskins with palette variant 1, and so on."));
    TArray<TSharedPtr<FJsonValue>> Rotation;
    for (const FString& Race : C.Campaign.RaceRotation) Rotation.Add(MakeShared<FJsonValueString>(Race));
    Campaign->SetArrayField(TEXT("raceRotation"), Rotation);
    Campaign->SetBoolField(TEXT("reskinOnWrap"), C.Campaign.bReskinOnWrap);
    Campaign->SetNumberField(TEXT("veteranFromCycle"), C.Campaign.VeteranFromCycle);
    Campaign->SetNumberField(TEXT("eliteFromCycle"), C.Campaign.EliteFromCycle);
    Campaign->SetNumberField(TEXT("championFromCycle"), C.Campaign.ChampionFromCycle);
    Campaign->SetNumberField(TEXT("mythicBossFromCycle"), C.Campaign.MythicBossFromCycle);
    Campaign->SetNumberField(TEXT("promoteEvery"), C.Campaign.PromoteEvery);
    Campaign->SetStringField(TEXT("rotateEvery"), C.Campaign.bRotatePerWave ? TEXT("wave") : TEXT("cycle")); // rules-conformance
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
        if (W.Packs > 0) // waves-modes
        {
            WO->SetNumberField(TEXT("packs"), W.Packs);
            WO->SetArrayField(TEXT("packSize"), {MakeShared<FJsonValueNumber>(W.PackSizeMin), MakeShared<FJsonValueNumber>(W.PackSizeMax)});
        }
        if (!W.bDealsDamage) WO->SetBoolField(TEXT("damage"), false);
        if (!W.FightBackPacks.IsEmpty())
        {
            TArray<TSharedPtr<FJsonValue>> Fight;
            for (const int32 P : W.FightBackPacks) Fight.Add(MakeShared<FJsonValueNumber>(P));
            WO->SetArrayField(TEXT("fightBack"), Fight);
        }
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
        Bonus->SetStringField(TEXT("_comment"), TEXT("Bonus Loot Stage (playtest 6): from fromWave, `replaceChance` per non-boss wave that the stage REPLACES the wave (at most maxPerCycle). Its creatures run the route to the castle, never attack, bolt from champions closer than fleeRadius, never cost lives and escape escapeSeconds after they are first attacked (escapeTimerOnHit) or at the castle. The stage rolls a loot tier (LootTables.json bonusStage). Each creature pays `bounty` mob values. `chance` = the older breather bonus wave after a cleared wave (0 = off; extraBreatherSeconds). Docs/Items.md, Docs/MonsterExpansion.md."));
        Bonus->SetBoolField(TEXT("enabled"), C.Bonus.bEnabled);
        Bonus->SetNumberField(TEXT("chance"), C.Bonus.Chance);
        Bonus->SetNumberField(TEXT("replaceChance"), C.Bonus.ReplaceChance); // bonus-loot
        Bonus->SetBoolField(TEXT("escapeTimerOnHit"), C.Bonus.bEscapeTimerOnHit);
        Bonus->SetNumberField(TEXT("fromWave"), C.Bonus.FromWave);
        Bonus->SetNumberField(TEXT("maxPerCycle"), C.Bonus.MaxPerCycle);
        Bonus->SetNumberField(TEXT("extraBreatherSeconds"), C.Bonus.ExtraBreatherSeconds);
        Bonus->SetNumberField(TEXT("escapeSeconds"), C.Bonus.EscapeSeconds);
        Bonus->SetNumberField(TEXT("fleeRadius"), C.Bonus.FleeRadius);
        Bonus->SetNumberField(TEXT("bounty"), C.Bonus.Bounty);
        Bonus->SetObjectField(TEXT("wave"), WaveJson(C.Bonus.Wave));
        Root->SetObjectField(TEXT("bonusWave"), Bonus);
    }
    // waves-modes
    {
        auto Match = MakeShared<FJsonObject>();
        Match->SetStringField(TEXT("_comment"), TEXT("The match: totalWaves regular waves, a PvP arena round after each wave in pvpAfterWaves (feat/arena-flow reads it), then Sudden Death waves (the last loopLastWaves waves again, monster health and damage x suddenDeath) until a team runs out of lives. Docs/Waves.md."));
        Match->SetNumberField(TEXT("totalWaves"), C.Match.TotalWaves);
        TArray<TSharedPtr<FJsonValue>> Pvp;
        for (const int32 W : C.Match.PvpAfterWaves) Pvp.Add(MakeShared<FJsonValueNumber>(W));
        Match->SetArrayField(TEXT("pvpAfterWaves"), Pvp);
        auto Sudden = MakeShared<FJsonObject>();
        Sudden->SetNumberField(TEXT("health"), C.Match.SuddenDeathHealth);
        Sudden->SetNumberField(TEXT("damage"), C.Match.SuddenDeathDamage);
        Sudden->SetNumberField(TEXT("loopLastWaves"), C.Match.SuddenDeathLoop);
        Match->SetObjectField(TEXT("suddenDeath"), Sudden);
        Root->SetObjectField(TEXT("match"), Match);
        auto Mon = MakeShared<FJsonObject>();
        Mon->SetStringField(TEXT("_comment"), TEXT("Every wave monster moves at speed x (Eric: -20%). Armored marchers move at a further armoredSpeed x, cannot be slowed (armoredSlowImmune) and stay stunned armoredStunMultiplier x longer. packGapSeconds separates the packs of a pack wave."));
        Mon->SetNumberField(TEXT("speed"), C.Monsters.Speed);
        Mon->SetNumberField(TEXT("armoredSpeed"), C.Monsters.ArmoredSpeed);
        Mon->SetBoolField(TEXT("armoredSlowImmune"), C.Monsters.bArmoredSlowImmune);
        Mon->SetNumberField(TEXT("armoredStunMultiplier"), C.Monsters.ArmoredStunMultiplier);
        Mon->SetNumberField(TEXT("packGapSeconds"), C.Monsters.PackGapSeconds);
        Root->SetObjectField(TEXT("monsters"), Mon);
        auto Live = MakeShared<FJsonObject>();
        Live->SetStringField(TEXT("_comment"), TEXT("The in-game wave scale (F8 > Waves > Live scale, or cire.WaveScale <health> <damage> <speed>): multiplies every wave monster, living ones included."));
        Live->SetNumberField(TEXT("health"), C.Live.Health);
        Live->SetNumberField(TEXT("damage"), C.Live.Damage);
        Live->SetNumberField(TEXT("speed"), C.Live.Speed);
        Root->SetObjectField(TEXT("liveScale"), Live);
        Root->SetNumberField(TEXT("packSizeBonus"), C.PackSizeBonus);
        Root->SetStringField(TEXT("preset"), C.Preset.ToString());
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

// ---------------------------------------------------------------- waves-modes: presets (Content/Data/WavePresets.json)
namespace
{
TArray<FCireWavePreset> PresetCache;
bool bPresetsLoaded = false;

FName CleanPresetId(const FString& In)
{
    FString Out;
    for (const TCHAR Ch : In.ToLower()) Out.AppendChar(FChar::IsAlnum(Ch) ? Ch : TEXT('_'));
    Out = Out.Left(32);
    while (Out.StartsWith(TEXT("_"))) Out.RightChopInline(1);
    return Out.IsEmpty() ? FName(TEXT("custom")) : FName(*Out);
}
void ClampPreset(FCireWavePreset& P)
{
    P.Id = CleanPresetId(P.Id.ToString());
    P.Label = P.Label.Left(32).TrimStartAndEnd(); if (P.Label.IsEmpty()) P.Label = P.Id.ToString();
    P.Description = P.Description.Left(240);
    P.Scale.Health = ClampF(P.Scale.Health, .1f, 10, 1); P.Scale.Damage = ClampF(P.Scale.Damage, 0, 10, 1); P.Scale.Speed = ClampF(P.Scale.Speed, .2f, 3, 1);
    P.PackSizeBonus = FMath::Clamp(P.PackSizeBonus, -3, 3);
    auto Packs = [](TArray<int32>& A) { A.RemoveAll([](int32 V) { return V < 1 || V > 8; }); A.Sort(); };
    Packs(P.DefaultFightBack);
    P.Waves.RemoveAll([](const FCireWavePreset::FWave& W) { return W.Wave < 1 || W.Wave > 200; });
    for (auto& W : P.Waves) Packs(W.FightBack);
    P.PvpAfterWaves.RemoveAll([](int32 W) { return W < 1 || W > 500; }); P.PvpAfterWaves.Sort();
}
TArray<int32> IntArray(const TSharedPtr<FJsonObject>& O, const TCHAR* Key)
{
    TArray<int32> Out; const TArray<TSharedPtr<FJsonValue>>* A = nullptr;
    if (O && O->TryGetArrayField(Key, A) && A) for (const auto& V : *A) Out.Add(static_cast<int32>(V->AsNumber()));
    return Out;
}
TArray<TSharedPtr<FJsonValue>> IntValues(const TArray<int32>& A)
{
    TArray<TSharedPtr<FJsonValue>> Out; for (const int32 V : A) Out.Add(MakeShared<FJsonValueNumber>(V)); return Out;
}
}

TArray<FCireWavePreset> CireWaveDirector::BuiltInPresets()
{
    TArray<FCireWavePreset> Out;
    {
        FCireWavePreset P; P.Id = TEXT("standard"); P.Label = TEXT("Standard"); P.bBuiltIn = true; P.KitProfile = TEXT("Standard");
        P.Description = TEXT("Every wave fights the heroes, except the armored marches. 25 waves, 4 PvP rounds, then Sudden Death.");
        Out.Add(P);
    }
    {
        FCireWavePreset P; P.Id = TEXT("hero_td"); P.Label = TEXT("Hero TD / PvP"); P.bBuiltIn = true; P.bDefaultDamage = false; P.KitProfile = TEXT("Hero TD");
        P.Description = TEXT("Pure hero tower defence: waves never attack, they path to the castle like armored rounds. Stop them before the gate.");
        Out.Add(P);
    }
    {
        FCireWavePreset P; P.Id = TEXT("hybrid"); P.Label = TEXT("Hybrid"); P.bBuiltIn = true; P.bDefaultDamage = false; P.KitProfile = TEXT("Standard");
        P.DefaultFightBack = {1, 4, 7};
        P.Description = TEXT("Hero TD with teeth: waves march to the castle, but the vanguard, middle and rear-guard packs (1, 4, 7) fight back, and boss waves fight in full.");
        for (int32 W = 5; W <= 25; W += 5) { FCireWavePreset::FWave O; O.Wave = W; O.bDamage = true; P.Waves.Add(O); }
        Out.Add(P);
    }
    return Out;
}

bool CireWaveDirector::ParsePresets(const FString& Json, TArray<FCireWavePreset>& Out, FString& Error)
{
    TSharedPtr<FJsonObject> Root;
    if (Json.Len() > 256 * 1024 || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Root) || !Root) { Error = TEXT("WavePresets.json is not valid JSON."); return false; }
    const TArray<TSharedPtr<FJsonValue>>* List = nullptr;
    if (!Root->TryGetArrayField(TEXT("presets"), List) || !List) { Error = TEXT("WavePresets.json needs a \"presets\" array."); return false; }
    TArray<FCireWavePreset> Parsed;
    for (const auto& V : *List)
    {
        const TSharedPtr<FJsonObject>* PO = nullptr;
        if (!V || !V->TryGetObject(PO) || !PO) { Error = TEXT("Every preset must be an object."); return false; }
        FCireWavePreset P; FString Text;
        if (!(*PO)->TryGetStringField(TEXT("id"), Text) || Text.IsEmpty()) { Error = TEXT("Every preset needs an \"id\"."); return false; }
        P.Id = FName(*Text);
        (*PO)->TryGetStringField(TEXT("label"), P.Label);
        (*PO)->TryGetStringField(TEXT("description"), P.Description);
        P.bDefaultDamage = Flag(*PO, TEXT("defaultDamage"), true);
        P.DefaultFightBack = IntArray(*PO, TEXT("defaultFightBack"));
        P.PackSizeBonus = static_cast<int32>(Num(*PO, TEXT("packSizeBonus"), 0));
        P.PvpAfterWaves = IntArray(*PO, TEXT("pvpAfterWaves"));
        P.bBuiltIn = Flag(*PO, TEXT("builtIn"), false);
        (*PO)->TryGetStringField(TEXT("kitProfile"), P.KitProfile); // kit-editor: Hero Creator kit profile
        P.KitProfile = P.KitProfile.TrimStartAndEnd().Left(40);
        const TSharedPtr<FJsonObject>* Scale = nullptr;
        if ((*PO)->TryGetObjectField(TEXT("scale"), Scale) && Scale)
        {
            P.Scale.Health = static_cast<float>(Num(*Scale, TEXT("health"), 1)); P.Scale.Damage = static_cast<float>(Num(*Scale, TEXT("damage"), 1));
            P.Scale.Speed = static_cast<float>(Num(*Scale, TEXT("speed"), 1));
        }
        const TArray<TSharedPtr<FJsonValue>>* Waves = nullptr;
        if ((*PO)->TryGetArrayField(TEXT("waves"), Waves) && Waves)
            for (const auto& WV : *Waves)
            {
                const TSharedPtr<FJsonObject>* WO = nullptr;
                if (!WV || !WV->TryGetObject(WO) || !WO) { Error = TEXT("Preset wave overrides must be objects."); return false; }
                FCireWavePreset::FWave W; W.Wave = static_cast<int32>(Num(*WO, TEXT("wave"), 0)); W.bDamage = Flag(*WO, TEXT("damage"), true);
                W.FightBack = IntArray(*WO, TEXT("fightBack"));
                P.Waves.Add(W);
            }
        ClampPreset(P);
        if (Parsed.ContainsByPredicate([&](const FCireWavePreset& O) { return O.Id == P.Id; })) { Error = FString::Printf(TEXT("Preset id '%s' is listed twice."), *P.Id.ToString()); return false; }
        Parsed.Add(P);
    }
    if (Parsed.Num() > 32) { Error = TEXT("WavePresets.json lists at most 32 presets."); return false; }
    Out = MoveTemp(Parsed);
    Error.Reset();
    return true;
}

FString CireWaveDirector::PresetsToJson(const TArray<FCireWavePreset>& List)
{
    auto Root = MakeShared<FJsonObject>();
    Root->SetNumberField(TEXT("schemaVersion"), 1);
    Root->SetStringField(TEXT("_comment"), TEXT("Wave game-mode presets (Docs/Waves.md). Hosting lists them under GAME TYPE. defaultDamage: every wave attacks heroes (true) or only paths to the castle (false); defaultFightBack: packs (1-based) that fight back when a wave's damage is off; waves: per-wave overrides {wave, damage, fightBack}; scale: live health/damage/speed; packSizeBonus: added to every pack; pvpAfterWaves: optional PvP schedule override. Save the current F8 settings as a preset with F8 > Waves > Modes & Scale."));
    TArray<TSharedPtr<FJsonValue>> Out;
    for (const auto& P : List)
    {
        auto PO = MakeShared<FJsonObject>();
        PO->SetStringField(TEXT("id"), P.Id.ToString());
        PO->SetStringField(TEXT("label"), P.Label);
        PO->SetStringField(TEXT("description"), P.Description);
        if (P.bBuiltIn) PO->SetBoolField(TEXT("builtIn"), true);
        PO->SetBoolField(TEXT("defaultDamage"), P.bDefaultDamage);
        PO->SetArrayField(TEXT("defaultFightBack"), IntValues(P.DefaultFightBack));
        TArray<TSharedPtr<FJsonValue>> Waves;
        for (const auto& W : P.Waves)
        {
            auto WO = MakeShared<FJsonObject>();
            WO->SetNumberField(TEXT("wave"), W.Wave); WO->SetBoolField(TEXT("damage"), W.bDamage);
            if (!W.FightBack.IsEmpty()) WO->SetArrayField(TEXT("fightBack"), IntValues(W.FightBack));
            Waves.Add(MakeShared<FJsonValueObject>(WO));
        }
        PO->SetArrayField(TEXT("waves"), Waves);
        auto Scale = MakeShared<FJsonObject>();
        Scale->SetNumberField(TEXT("health"), P.Scale.Health); Scale->SetNumberField(TEXT("damage"), P.Scale.Damage); Scale->SetNumberField(TEXT("speed"), P.Scale.Speed);
        PO->SetObjectField(TEXT("scale"), Scale);
        PO->SetNumberField(TEXT("packSizeBonus"), P.PackSizeBonus);
        if (!P.PvpAfterWaves.IsEmpty()) PO->SetArrayField(TEXT("pvpAfterWaves"), IntValues(P.PvpAfterWaves));
        if (!P.KitProfile.IsEmpty()) PO->SetStringField(TEXT("kitProfile"), P.KitProfile); // kit-editor
        Out.Add(MakeShared<FJsonValueObject>(PO));
    }
    Root->SetArrayField(TEXT("presets"), Out);
    FString Text;
    auto Writer = TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create(&Text);
    FJsonSerializer::Serialize(Root, Writer);
    return Text + TEXT("\n");
}

FString CireWaveDirector::PresetsPath() { return FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir() / TEXT("Data/WavePresets.json")); }

const TArray<FCireWavePreset>& CireWaveDirector::Presets(bool bReload)
{
    if (bPresetsLoaded && !bReload) return PresetCache;
    bPresetsLoaded = true;
    PresetCache.Reset();
    FString Json, Error;
    if (FFileHelper::LoadFileToString(Json, *PresetsPath()) && !ParsePresets(Json, PresetCache, Error))
    { UE_LOG(LogTemp, Warning, TEXT("CIRE_WAVE_PRESETS %s; using the built-in presets"), *Error); PresetCache.Reset(); }
    // The shipped presets always exist (the file may override their settings, never remove them).
    const TArray<FCireWavePreset> BuiltIn = BuiltInPresets();
    for (int32 I = BuiltIn.Num() - 1; I >= 0; --I)
        if (!PresetCache.ContainsByPredicate([&](const FCireWavePreset& P) { return P.Id == BuiltIn[I].Id; })) PresetCache.Insert(BuiltIn[I], 0);
    for (auto& P : PresetCache) if (BuiltIn.ContainsByPredicate([&](const FCireWavePreset& B) { return B.Id == P.Id; })) P.bBuiltIn = true;
    return PresetCache;
}

const FCireWavePreset* CireWaveDirector::FindPreset(FName Id)
{
    return Presets().FindByPredicate([&](const FCireWavePreset& P) { return P.Id == Id; });
}

void CireWaveDirector::ApplyPreset(FCireWaveConfig& C, const FCireWavePreset& P)
{
    // Wave N of the preset is waves[N-1] (the campaign order plays waves[] straight through the match).
    for (int32 I = 0; I < C.Waves.Num(); ++I)
    {
        FCireWaveDef& W = C.Waves[I];
        const FCireWavePreset::FWave* O = P.Waves.FindByPredicate([&](const FCireWavePreset::FWave& X) { return X.Wave == I + 1; });
        W.bDealsDamage = O ? O->bDamage : P.bDefaultDamage;
        W.FightBackPacks = O ? O->FightBack : P.DefaultFightBack;
    }
    C.Live = P.Scale;
    C.PackSizeBonus = P.PackSizeBonus;
    if (!P.PvpAfterWaves.IsEmpty()) C.Match.PvpAfterWaves = P.PvpAfterWaves;
    C.Preset = P.Id;
}

FCireWavePreset CireWaveDirector::CapturePreset(const FCireWaveConfig& C, FName Id, const FString& Label, const FString& Description)
{
    FCireWavePreset P; P.Id = Id; P.Label = Label; P.Description = Description;
    P.bDefaultDamage = true;
    for (int32 I = 0; I < C.Waves.Num(); ++I)
    {
        const FCireWaveDef& W = C.Waves[I];
        if (!W.bDealsDamage || !W.FightBackPacks.IsEmpty())
        { FCireWavePreset::FWave O; O.Wave = I + 1; O.bDamage = W.bDealsDamage; O.FightBack = W.FightBackPacks; P.Waves.Add(O); }
    }
    P.Scale = C.Live; P.PackSizeBonus = C.PackSizeBonus; P.PvpAfterWaves = C.Match.PvpAfterWaves;
    ClampPreset(P);
    return P;
}

bool CireWaveDirector::SavePreset(const FCireWavePreset& In, FString* Error, const FString& Path)
{
    FCireWavePreset P = In; ClampPreset(P);
    const FString File = Path.IsEmpty() ? PresetsPath() : Path;
    TArray<FCireWavePreset> List;
    FString Json, Why;
    if (FFileHelper::LoadFileToString(Json, *File) && !ParsePresets(Json, List, Why)) { if (Error) *Error = Why; return false; }
    if (List.IsEmpty() && Path.IsEmpty()) List = BuiltInPresets();
    if (FCireWavePreset* Old = List.FindByPredicate([&](const FCireWavePreset& X) { return X.Id == P.Id; })) { P.bBuiltIn = Old->bBuiltIn; *Old = P; }
    else if (List.Num() >= 32) { if (Error) *Error = TEXT("At most 32 presets can be saved."); return false; }
    else List.Add(P);
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(File), true);
    if (!FFileHelper::SaveStringToFile(PresetsToJson(List), *File, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
    { if (Error) *Error = TEXT("WavePresets.json could not be written."); return false; }
    if (Path.IsEmpty()) Presets(true);
    if (Error) Error->Reset();
    return true;
}
