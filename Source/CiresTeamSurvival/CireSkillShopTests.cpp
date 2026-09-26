// progression-shop: in-engine checks for the kill-gold economy, the progression mode setting,
// the Skill Shop (server-validated buy / level-up, gating, per-level Ability DB scaling on real
// casts), bots buying, the recommended item builds, and Classic Draft still offering skills.
// Run by CireProgression::RunSmoke (Tools/RunProgressionChecks.py --only native and
// Tools/RunExpansionChecks.py --only native). Pure price/slot maths: Tests/ItemRulesTests.cpp.
#include "CireSkillShop.h"
#if !UE_BUILD_SHIPPING
#include "CireAbilityDB.h"
#include "CirePolymorph.h"
#include "CireScalingKits.h"
#include "CireWaves.h"
#include "CireGame.h"
#include "CireItems.h"
#include "CireLoot.h"
#include "CireNPCArchetypes.h"
#include "CireNPCCombat.h"
#include "Engine/World.h"
#include "Misc/ScopeExit.h"

DEFINE_LOG_CATEGORY_STATIC(LogCireSkillShopTests, Log, All);
namespace CI = Cires::Items;

bool CireSkillShop::RunSmoke(ACireGameMode* Mode)
{
    auto* S = Mode ? Mode->GetGameState<ACireGameState>() : nullptr;
    if (!S || !Mode->HasAuthority()) return false;
    int32 Count = 0;
    bool bPass = true;
    auto Check = [&](bool bValue, const TCHAR* Label)
    {
        ++Count;
        if (!bValue) { bPass = false; UE_LOG(LogCireSkillShopTests, Error, TEXT("CIRE_SKILLSHOP_CHECK_FAIL %s"), Label); }
    };
    // Fixture: isolated heroes/monsters, match state restored on exit.
    const Cires::MatchClock SavedClock = Mode->Clock;
    const TArray<ACireHero*> SavedHeroes = Mode->Heroes;
    const TArray<ACireMonster*> SavedMonsters = Mode->Monsters;
    const int32 SavedWave = S->Wave, SavedPhase = S->Phase, SavedDone = S->CycleWavesDone;
    const float SavedNext = S->NextWaveSeconds;
    const uint8 SavedMode = S->ProgressionMode;
    TArray<AActor*> Spawned;
    ON_SCOPE_EXIT
    {
        for (AActor* Actor : Spawned) if (IsValid(Actor)) Actor->Destroy();
        Mode->Clock = SavedClock; Mode->Heroes = SavedHeroes; Mode->Monsters = SavedMonsters;
        S->Wave = SavedWave; S->Phase = SavedPhase; S->CycleWavesDone = SavedDone; S->NextWaveSeconds = SavedNext; S->ProgressionMode = SavedMode;
    };
    Mode->Heroes.Reset();
    Mode->Monsters.Reset();
    auto Hero = [&](int32 Team, int32 Archetype, FVector Offset)
    {
        FActorSpawnParameters Params;
        Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        auto* H = Mode->GetWorld()->SpawnActor<ACireHero>(Mode->BasePosition(Team) + Offset, FRotator::ZeroRotator, Params);
        if (!H) return H;
        Spawned.Add(H);
        H->SetActorTickEnabled(false);
        if (H->Inventory) H->Inventory->SetComponentTickEnabled(false);
        H->TeamId = Team; H->Draft(Archetype); H->Offers.Reset(); H->Gold = 0; H->CriticalChance = 0;
        Mode->Heroes.Add(H);
        return H;
    };
    auto Monster = [&](int32 Lane, FVector Location)
    {
        FActorSpawnParameters Params;
        Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        auto* M = Mode->GetWorld()->SpawnActor<ACireMonster>(Location, FRotator::ZeroRotator, Params);
        if (!M) return M;
        Spawned.Add(M);
        M->SetActorTickEnabled(false);
        M->Lane = Lane; M->Health = M->MaxHealth = 5000;
        Mode->Monsters.Add(M);
        return M;
    };
    auto Prep = [&](int32 Wave) { Mode->Clock = Cires::MatchClock(); Mode->Clock.BeginIntermission(); S->Phase = 1; S->Wave = Wave; S->NextWaveSeconds = 0; };
    auto SetRank = [](ACireHero* H, const FString& Id, int32 Level)
    {
        H->Inventory->SkillRanks.RemoveAll([&](const FCireSkillRank& R) { return R.Id == Id; });
        FCireSkillRank R; R.Id = Id; R.Level = Level; H->Inventory->SkillRanks.Add(R);
    };

    // ---- economy: Eric's kill-gold ruling
    const CI::Economy& E = CireLoot::Get().Economy;
    Check(CI::MobValue(E, 1) == 1 && CI::MobValue(E, 2) == 1 && CI::MobValue(E, 3) == 2 && CI::MobValue(E, 6) == 3 && CI::MobValue(E, 12) == 5,
        TEXT("mob value: 1 gold, +1 every 3 waves (wave 6 = 3)"));
    S->Wave = 6; S->Phase = 0;
    const FVector Town = Mode->BasePosition(0);
    ACireMonster* Mob = Monster(0, Town + FVector(300, 0, 0));
    ACireMonster* Armored = Monster(0, Town + FVector(300, 150, 0));
    ACireMonster* Boss = Monster(0, Town + FVector(300, -150, 0));
    ACireMonster* PackUnit = Monster(0, Town + FVector(400, 0, 0));
    ACireMonster* Leader = Monster(0, Town + FVector(400, 150, 0));
    ACireHero* A = Hero(0, 0, FVector(0, -120, 0));
    ACireHero* B = Hero(0, 1, FVector(0, 120, 0));
    if (!Mob || !Armored || !Boss || !PackUnit || !Leader || !A || !B || !A->Inventory || !B->Inventory) { Check(false, TEXT("fixture spawn")); return false; }
    Armored->bArmoredEscort = true;
    Boss->bBoss = true;
    PackUnit->PackId = 9901;
    Leader->PackId = 9901;
    CireNPCCombat::ConfigureArchetype(Leader, CireNPCArchetypes::Get().PackLeader, 6, 1, 1);
    Leader->PackId = 9901; Leader->Lane = 0;
    Check(CireLoot::KillBounty(Mode, Mob) == 3, TEXT("wave 6 mob pays 3 gold"));
    Check(CireLoot::KillBounty(Mode, Armored) == 6, TEXT("armored unit pays 2x the mob value"));
    Check(CireLoot::KillBounty(Mode, Boss) == 30, TEXT("boss pays 10x the mob value (wave 6 = 30)"));
    Check(CireLoot::KillBounty(Mode, PackUnit) == 30, TEXT("challenge-pack unit pays 10x the mob value"));
    Check(CireLoot::KillBounty(Mode, Leader) == 300, TEXT("Pack Leader pays another 10x (100x the mob value)"));
    S->Wave = 1;
    Check(CireLoot::KillBounty(Mode, Mob) == 1 && CireLoot::KillBounty(Mode, Boss) == 10, TEXT("wave 1: mob 1 gold, boss 10"));
    S->Wave = 6;
    A->Gold = B->Gold = 0;
    Check(CireLoot::AwardKillGold(Mode, Mob) == 3 && A->Gold == 3 && B->Gold == 3, TEXT("wave kill pays every teammate the full bounty"));
    Check(CireLoot::AwardKillGold(Mode, Leader) == 300 && A->Gold == 303 && B->Gold == 303, TEXT("Pack Leader bounty paid to eligible teammates"));
    Check(CireLoot::AwardKillGold(Mode, Mob, 1.5f) == 5, TEXT("wave reward multiplier scales the bounty"));

    // ---- progression mode: server setting, before the first wave only
    S->Wave = 0; S->Phase = 0;
    FString Why;
    Check(SetMode(Mode, false, &Why) && S->ProgressionMode == 0 && !IsSkillShopMode(Mode->GetWorld()), TEXT("mode switches to Classic Draft before the first wave"));
    {
        // Classic Draft still works: a pending breakpoint offers skills on level-up.
        ACireHero* C = Hero(0, 2, FVector(0, 300, 0));
        C->Skills = {TEXT("ember_lance")};
        C->Cooldowns.Init(0, 1);
        C->Progression.LearnedSkills = {{"ember_lance", "Ember Lance", Cires::SkillKind::Active}};
        C->Progression.Level = 3; C->Progression.NextAugmentLevel = 3;
        C->Offers.Reset();
        C->RefreshOffer();
        Check(C->Offers.Num() > 0, TEXT("Classic Draft: level-up breakpoints still offer skills"));
        FString ClosedWhy;
        Prep(0);
        Check(!IsOpen(C, &ClosedWhy) && ClosedWhy.Contains(TEXT("Classic")), TEXT("Classic Draft: the Skill Shop is closed with the reason"));
        S->Phase = 0;
        Check(SetMode(Mode, true, &Why) && IsSkillShopMode(Mode->GetWorld()), TEXT("mode switches back to Skill Shop"));
        C->Offers.Reset();
        C->RefreshOffer();
        Check(C->Offers.Num() == 0, TEXT("Skill Shop mode: level-ups offer no skills (stats only)"));
        const int32 Strength = C->Strength;
        C->GrantExperience(5000);
        Check(C->Progression.Level > 3 && C->Offers.Num() == 0 && C->Strength >= Strength, TEXT("Skill Shop mode: level-up raises stats, no skill offer"));
        S->Wave = 3;
        Why.Reset();
        Check(!SetMode(Mode, false, &Why) && !Why.IsEmpty() && S->ProgressionMode == 1, TEXT("mode is locked once waves have started"));
    }

    // ---- buy / level-up (server-validated, gold deducted, gating, no level cap)
    Prep(7);
    ACireHero* T = Hero(0, 0, FVector(0, 500, 0));
    T->Skills = {TEXT("shield_slam")};
    T->Cooldowns.Init(0, 1);
    T->Inventory->SkillRanks.Reset();
    T->Gold = 1000;
    const TArray<FCireShopSkill> Catalog = CatalogFor(T);
    Check(Catalog.Num() >= 8, TEXT("the class skill list has at least 8 skills"));
    const FCireShopSkill* NewActive = Catalog.FindByPredicate([&](const FCireShopSkill& K) { return K.Kind == CI::ShopSkillKind::Active && !T->Skills.Contains(K.Id); });
    const FCireShopSkill* Ultimate = Catalog.FindByPredicate([&](const FCireShopSkill& K) { return K.Kind == CI::ShopSkillKind::Ultimate; });
    FString Outside;
    for (const auto& Skill : Cires::StarterSkillPool())
    {
        const FString Id = UTF8_TO_TCHAR(Skill.Id.c_str());
        if (!Catalog.ContainsByPredicate([&](const FCireShopSkill& K) { return K.Id == Id; })) { Outside = Id; break; }
    }
    FString Message;
    if (NewActive)
    {
        const int32 Price = BuyPrice(T, NewActive->Id);
        Check(Price == FMath::RoundToInt(Get().Rules.ActivePrice * (1 + Get().Rules.ActiveOwnedGrowth) * CI::MobValue(E, 7)), TEXT("active price = mob values x owned-active growth"));
        Check(Buy(T, NewActive->Id, Message) && T->Gold == 1000 - Price && T->Skills.Contains(NewActive->Id) && Level(T, NewActive->Id) == 1,
            TEXT("buying a skill deducts gold, adds it at level 1"));
        Check(T->Inventory->SkillRanks.ContainsByPredicate([&](const FCireSkillRank& R) { return R.Id == NewActive->Id && R.Level == 1; }), TEXT("skill level stored in the replicated ranks"));
        const int32 Gold = T->Gold;
        Check(!Buy(T, NewActive->Id, Message) && Message.Contains(TEXT("Already")) && T->Gold == Gold, TEXT("owned skill cannot be bought twice"));
        // Level-ups: price grows, no cap.
        const int32 L1 = LevelPrice(T, NewActive->Id);
        Check(LevelUp(T, NewActive->Id, Message) && Level(T, NewActive->Id) == 2 && T->Gold == Gold - L1, TEXT("level-up deducts gold and raises the level"));
        Check(LevelPrice(T, NewActive->Id) > L1, TEXT("each level costs more than the last"));
        T->Gold = 1000000;
        bool bClimb = true;
        for (int32 Step = 0; Step < 10; ++Step) bClimb &= LevelUp(T, NewActive->Id, Message);
        Check(bClimb && Level(T, NewActive->Id) == 12, TEXT("no level cap (level 12 reached)"));
        T->Gold = 0;
        Check(!LevelUp(T, NewActive->Id, Message) && Message.Contains(TEXT("Not enough gold")), TEXT("level-up rejected without gold"));
    }
    else Check(false, TEXT("catalog has an unowned active"));
    T->Gold = 100000;
    if (Ultimate) Check(!Buy(T, Ultimate->Id, Message) && Message.Contains(TEXT("wave 10")) && !T->Skills.Contains(Ultimate->Id), TEXT("ultimate slot opens at wave 10"));
    if (!Outside.IsEmpty()) Check(!Buy(T, Outside, Message) && Message.Contains(TEXT("Not in your")), TEXT("skills outside the champion's list are rejected"));
    // Active slot gate: at wave 7, 4 actives.
    int32 Bought = 0;
    for (const FCireShopSkill& Skill : Catalog) if (Skill.Kind == CI::ShopSkillKind::Active && !T->Skills.Contains(Skill.Id) && Buy(T, Skill.Id, Message)) ++Bought;
    Check(OwnedOfKind(T, CI::ShopSkillKind::Active) == CI::SlotsAvailable(Get().Rules, CI::ShopSkillKind::Active, 7) && Message.Contains(TEXT("wave 9")),
        TEXT("active slots gate the kit (4 at wave 7, next at wave 9)"));
    {
        // Eric's ruling: Skill Shop mode still levels up. Bought skills sit outside the level
        // breakpoints (Cires::SkillSchedule::Shop) and must never stall level-ups.
        const int32 LevelBefore = T->Progression.Level, StrBefore = T->Strength, AgiBefore = T->Agility, IntBefore = T->Intelligence;
        const float HealthBefore = T->MaxHealth;
        Check(T->Progression.Schedule == Cires::SkillSchedule::Shop && T->Progression.LearnedSkills.size() >= 3 &&
            static_cast<int32>(T->Progression.LearnedSkills.size()) > LevelBefore, TEXT("Skill Shop purchases outrun the level breakpoints"));
        T->GrantExperience(20000);
        const int32 Gained = T->Progression.Level - LevelBefore;
        const bool bStr = T->PrimaryStat() == Cires::PrimaryStat::Strength;
        Check(Gained >= 5 && T->Level == T->Progression.Level, TEXT("Skill Shop champion with bought skills levels up"));
        Check(T->Strength - StrBefore == Gained * (bStr ? 2 : 1) && T->Agility - AgiBefore >= Gained && T->Intelligence - IntBefore >= Gained,
            TEXT("each Skill Shop level gives +2 primary and +1 other stats"));
        Check(FMath::IsNearlyEqual(T->MaxHealth - HealthBefore, (T->Strength - StrBefore) * static_cast<float>(Cires::HealthPerStrength), .5f),
            TEXT("Skill Shop levels add 10 health per STR point"));
        Check(T->Offers.Num() == 0, TEXT("Skill Shop levels still offer no skills"));
    }
    // Access windows: prep, breather, recovery open; mid-wave closed.
    S->Phase = 0; S->NextWaveSeconds = 0;
    Check(!IsOpen(T, &Why) && Why.Contains(TEXT("between waves")), TEXT("closed while a wave is running"));
    const int32 GoldMidWave = T->Gold;
    Check(!LevelUp(T, T->Skills[0], Message) && T->Gold == GoldMidWave, TEXT("mid-wave level-up rejected by the server"));
    S->NextWaveSeconds = 6.f;
    Check(IsOpen(T) && IsBreather(Mode->GetWorld()), TEXT("open during the breather after a cleared wave"));
    S->Phase = 4;
    Check(IsOpen(T), TEXT("open during recovery"));
    Prep(7);
    Check(IsOpen(T), TEXT("open during prep"));
    // Non-authority callers never mutate (clients go through the Server RPCs).
    Check(!Buy(nullptr, TEXT("war_cry"), Message) && !LevelUp(nullptr, TEXT("war_cry"), Message), TEXT("invalid buyer rejected"));

    // ---- per-level scaling on real casts (Ability DB EffectiveStats)
    {
        Mode->Clock = Cires::MatchClock(); S->Phase = 0; S->Wave = 7;
        ACireHero* One = Hero(0, 0, FVector(600, -300, 0));
        ACireHero* Three = Hero(0, 0, FVector(600, 300, 0));
        for (ACireHero* H : {One, Three})
        {
            H->Skills = {TEXT("war_cry"), TEXT("ember_lance")};
            H->Cooldowns.Init(0, 2);
            H->Inventory->SkillRanks.Reset();
            H->Energy = 100; H->Mana = H->MaxMana = 400; H->GlobalCooldown = 0;
        }
        SetRank(One, TEXT("war_cry"), 1); SetRank(One, TEXT("ember_lance"), 1);
        SetRank(Three, TEXT("war_cry"), 3); SetRank(Three, TEXT("ember_lance"), 3);
        One->Cast(0); Three->Cast(0);
        const float Cost1 = 100 - One->Energy, Cost3 = 100 - Three->Energy;
        const float Cd1 = One->Cooldowns[0], Cd3 = Three->Cooldowns[0];
        Check(Cost1 > 0 && Cd1 > 0, TEXT("level-1 War Cry cast pays energy and starts its cooldown"));
        Check(Cost3 > Cost1 && Cost3 <= Cost1 * 1.5f + .01f, TEXT("level-3 cast costs a bit more"));
        Check(Cd3 < Cd1, TEXT("level-3 cast has a shorter cooldown"));
        if (CireAbilityDB::Find(TEXT("war_cry")))
        {
            const FCireAbilityStats S1 = CireAbilityDB::EffectiveStats(TEXT("war_cry"), 1), S3 = CireAbilityDB::EffectiveStats(TEXT("war_cry"), 3);
            Check(FMath::IsNearlyEqual(Cd3 / Cd1, S3.Cooldown / S1.Cooldown, .02f) && FMath::IsNearlyEqual(Cost3 / Cost1, (S3.EnergyCost + S3.ManaCost) / (S1.EnergyCost + S1.ManaCost), .02f),
                TEXT("cast numbers follow CireAbilityDB::EffectiveStats"));
        }
        ACireMonster* Dummy = Monster(0, Town + FVector(900, 0, 0));
        const float Dmg1 = CireItems::ModifyOutgoingDamage(One, Dummy, 100.f, TEXT("Ember Lance"));
        const float Dmg3 = CireItems::ModifyOutgoingDamage(Three, Dummy, 100.f, TEXT("Ember Lance"));
        Check(Dmg3 > Dmg1 && CastScale(Three, TEXT("ember_lance")).Effect > 1.f, TEXT("level-3 skill deals more damage"));
        Check(CireItems::HealingMultiplier(Three, TEXT("War Cry")) >= CireItems::HealingMultiplier(One, TEXT("War Cry")), TEXT("healing/effect scales with the level"));
    }

    // ---- bots buy sensibly
    {
        Prep(7);
        ACireHero* Bot = Hero(1, 1, FVector(0, 0, 0));
        Bot->bBot = true;
        Bot->Skills = {TEXT("cleaving_strike")};
        Bot->Cooldowns.Init(0, 1);
        Bot->Inventory->SkillRanks.Reset();
        Bot->Gold = 600;
        BotShop(Bot);
        Check(Bot->Skills.Num() == 2 && Bot->Gold < 600, TEXT("bot buys a skill for an open slot"));
        const TArray<FCireShopSkill> BotList = CatalogFor(Bot);
        Check(BotList.ContainsByPredicate([&](const FCireShopSkill& K) { return K.Id == Bot->Skills.Last(); }), TEXT("bot buys from its own class list"));
        for (int32 Step = 0; Step < 12; ++Step) BotShop(Bot);
        int32 MaxLevel = 0;
        for (const FString& Id : Bot->Skills) MaxLevel = FMath::Max(MaxLevel, Level(Bot, Id));
        Check(OwnedOfKind(Bot, CI::ShopSkillKind::Active) <= CI::SlotsAvailable(Get().Rules, CI::ShopSkillKind::Active, 7) && Bot->Gold >= 0, TEXT("bot respects slots and gold"));
        Bot->Gold = 5000;
        for (int32 Step = 0; Step < 6; ++Step) BotShop(Bot);
        int32 After = 0;
        for (const FString& Id : Bot->Skills) After = FMath::Max(After, Level(Bot, Id));
        Check(After > 1, TEXT("bot levels skills once its slots are full"));
        S->Phase = 0; S->NextWaveSeconds = 0;
        const int32 Before = Bot->Gold;
        BotShop(Bot);
        Check(Bot->Gold == Before, TEXT("bots do not shop mid-wave"));
    }

    // ---- recommended item builds per role (starter -> core -> situational)
    {
        const auto& Items = CireItems::Get();
        bool bBuilds = true;
        for (const TCHAR* Role : {TEXT("tank"), TEXT("physical"), TEXT("caster"), TEXT("support")})
        {
            const auto* Lists = Items.Recommended.Find(Role);
            bBuilds &= Lists && Lists->Num() == 3 && (*Lists)[0].Num() >= 2 && (*Lists)[1].Num() >= 3 && (*Lists)[2].Num() >= 2;
            if (Lists) for (const auto& List : *Lists) for (const FName Id : List) { const auto* Item = CireItems::Find(Id); bBuilds &= Item && Item->Purchasable; }
        }
        Check(bBuilds, TEXT("every role has a starter/core/situational build of purchasable items"));
        bool bKeys = true;
        for (int32 Archetype = 0; Archetype < 3; ++Archetype) { ACireHero* H = Hero(0, Archetype, FVector(-300, Archetype * 100.f, 0)); bKeys &= H && Items.Recommended.Contains(CireItems::RoleKey(H)); }
        Check(bKeys, TEXT("each champion maps to a recommended build"));
    }

    // ---- Ready to Continue gate (Skill Shop mode): the next wave waits for every human
    {
        auto& Data = CireSkillShop::Mutable();
        const bool SavedGate = Data.bReadyGate; const float SavedCap = Data.ReadyMaxSeconds;
        Data.bReadyGate = true; Data.ReadyMaxSeconds = 180.f;
        Mode->Heroes.Reset(); Mode->Monsters.Reset();
        Mode->Clock = Cires::MatchClock(); S->Phase = 0; S->Wave = 3;
        S->WavesPerCycle = 5; S->CycleWavesDone = 1; Mode->CycleWavesSpawned = 1;
        ACireHero* H1 = Hero(0, 0, FVector(0, -600, 0));
        ACireHero* H2 = Hero(0, 1, FVector(0, -700, 0));
        ACireHero* Bot = Hero(0, 2, FVector(0, -800, 0));
        H1->bBot = H2->bBot = false; Bot->bBot = true;
        S->ProgressionMode = 0;
        float Timer = 15.f;
        Check(!HoldBreather(Mode, .5f, Timer), TEXT("Classic Draft: the breather is never held"));
        S->ProgressionMode = 1;
        Timer = 15.f;
        const bool bHeld = HoldBreather(Mode, .5f, Timer);
        Check(bHeld && Timer == 15.f && S->bReadyGateHold && S->BreatherPlayers == 2, TEXT("gate holds the countdown while humans are not ready"));
        Check(Bot->Inventory->bReadyToContinue && !H1->Inventory->bReadyToContinue, TEXT("bots auto-ready; humans start not ready"));
        CireWaveDirector::SetPlayerReady(H1, true);
        Check(HoldBreather(Mode, .5f, Timer) && H1->Inventory->bReadyToContinue && S->BreatherReady == 1, TEXT("one of two humans ready: still waiting"));
        CireWaveDirector::SetPlayerReady(H2, true);
        Check(!HoldBreather(Mode, .5f, Timer) && Timer <= 1.f && !S->bReadyGateHold, TEXT("every human ready: the next wave starts in 1 s"));
        // Safety cap (AFK): released after maxSeconds even if nobody is ready.
        CireWaveDirector::SetPlayerReady(H1, false); CireWaveDirector::SetPlayerReady(H2, false);
        S->ProgressionMode = 0; HoldBreather(Mode, 0, Timer); S->ProgressionMode = 1; // resets the wait clock
        Data.ReadyMaxSeconds = 5.f; Timer = 15.f;
        Check(HoldBreather(Mode, 3.f, Timer) && FMath::IsNearlyEqual(S->ReadyGateLeft, 2.f, .01f), TEXT("cap counts down while waiting"));
        Check(!HoldBreather(Mode, 3.f, Timer) && Timer <= 1.f, TEXT("safety cap releases the wave for AFK players"));
        Data.ReadyMaxSeconds = 0.f; S->ProgressionMode = 0; HoldBreather(Mode, 0, Timer); S->ProgressionMode = 1; Timer = 15.f;
        Check(HoldBreather(Mode, 1000.f, Timer) && S->ReadyGateLeft < 0, TEXT("cap 0 = wait for players with no limit"));
        // Bots-only matches (soaks) keep the normal breather.
        H1->bBot = H2->bBot = true; Timer = 15.f;
        Check(!HoldBreather(Mode, .5f, Timer), TEXT("bots-only match: no gate"));
        Data.bReadyGate = SavedGate; Data.ReadyMaxSeconds = SavedCap;
        S->ProgressionMode = 0; HoldBreather(Mode, 0, Timer); S->ProgressionMode = 1;
        S->CycleWavesDone = SavedDone; Mode->CycleWavesSpawned = 0;
    }

    // ---- Ability groupings ("periodic table") derived from the Ability DB
    {
        auto Is = [](const TCHAR* Id, const TCHAR* Section, const TCHAR* Tag)
        {
            const FCireAbilityDef* D = CireAbilityDB::Find(Id);
            return D && D->Section == Section && (!Tag || D->EffectTags.Contains(Tag));
        };
        Check(Is(TEXT("frost_bind"), TEXT("control"), TEXT("Slow")) && Is(TEXT("grave_line"), TEXT("control"), TEXT("Silence")) &&
            Is(TEXT("polymorph"), TEXT("control"), TEXT("Polymorph")) && Is(TEXT("shadow_step"), TEXT("control"), TEXT("Stun")), TEXT("crowd control grouped with CC tags"));
        Check(Is(TEXT("spectral_pack"), TEXT("summon"), TEXT("Summon")) && Is(TEXT("summoned_wall"), TEXT("construct"), TEXT("Construct")), TEXT("summons and constructs grouped"));
        Check(Is(TEXT("restoring_light"), TEXT("defensive"), TEXT("Heal")) && Is(TEXT("iron_guard"), TEXT("defensive"), TEXT("Guard")), TEXT("defensive skills grouped with heal/guard tags"));
        Check(Is(TEXT("ember_lance"), TEXT("spell"), nullptr) && Is(TEXT("piercing_shot"), TEXT("attack"), nullptr), TEXT("offensive split into spell and attack damage"));
        Check(Is(TEXT("stone_skin"), TEXT("passive"), nullptr) && Is(TEXT("cataclysm"), TEXT("ultimate"), nullptr), TEXT("passives and ultimates keep their own sections"));
        bool bAll = true;
        static const TSet<FString> Valid = {TEXT("spell"), TEXT("attack"), TEXT("defensive"), TEXT("control"), TEXT("summon"), TEXT("construct"), TEXT("passive"), TEXT("ultimate")};
        for (const FCireAbilityDef& D : CireAbilityDB::All()) bAll &= Valid.Contains(D.Section) && D.EffectTags.Num() <= 4;
        Check(bAll, TEXT("every ability has one primary section and at most four tags"));
        // scaling-kits "requires": the catalog never lists a skill the champion may not buy (shield skills).
        bool bGated = true;
        for (int32 Archetype = 0; Archetype < 3; ++Archetype)
        {
            ACireHero* H = Hero(0, Archetype, FVector(-500, Archetype * 100.f, 0));
            for (const FCireShopSkill& K : CatalogFor(H)) bGated &= H->Skills.Contains(K.Id) || CireKits::MeetsRequirement(H, K.Id, nullptr);
        }
        Check(bGated, TEXT("shield / ranged-only skills are listed only for champions who meet the requirement"));
    }

    bPass = CirePolymorph::RunSmoke(Mode) && bPass;
    UE_LOG(LogCireSkillShopTests, Display, TEXT("CIRE_SKILLSHOP_%s checks=%d"), bPass ? TEXT("PASS") : TEXT("FAIL"), Count);
    return bPass;
}
#endif
