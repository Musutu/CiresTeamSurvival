// scaling-kits: native checks (run by -CireCombatExpansionProbe / Tools/RunExpansionChecks.py --only native).
// Primary-stat scaling for tank / DPS / caster / healer abilities, summon and construct damage, owner
// CDR / attack-speed inheritance, monsters targeting summons and constructs, shield block and the
// armour penalty, the Mechanical Tank's taunt choice and slam, level-15 bonuses and auras, Headshot
// 2x / 3x and Artillery (range, attack speed, bomb = accumulated damage).
#include "CireScalingKits.h"
#if !UE_BUILD_SHIPPING
#include "CireAbilityDB.h"
#include "CireBuffs.h"
#include "CireCombatEvents.h"
#include "CireConstruct.h"
#include "CireCrowdControl.h"
#include "CireGame.h"
#include "CireItems.h"
#include "CireLanePath.h"
#include "CireMechTank.h"
#include "CireSkillShop.h"
#include "CireTechConstructs.h"
#include "CireThreat.h"
#include "EngineUtils.h"
#include "Misc/ScopeExit.h"

DEFINE_LOG_CATEGORY_STATIC(LogCireKitsTests, Log, All);
namespace K = Cires::Kits;

bool CireKits::RunSmoke(ACireGameMode* Mode)
{
    if (!Mode || !Mode->HasAuthority()) return false;
    UWorld* World = Mode->GetWorld();
    const auto SavedClock = Mode->Clock; const auto SavedHeroes = Mode->Heroes; const auto SavedMonsters = Mode->Monsters;
    TArray<AActor*> Actors; Mode->Clock = Cires::MatchClock(); Mode->Heroes.Reset(); Mode->Monsters.Reset();
    auto* Sub = UCireKitsSubsystem::Get(World);
    ON_SCOPE_EXIT
    {
        for (auto* A : Actors) if (IsValid(A)) A->Destroy();
        for (TActorIterator<ACireMechTank> It(World); It; ++It) It->Destroy();
        Mode->Clock = SavedClock; Mode->Heroes = SavedHeroes; Mode->Monsters = SavedMonsters;
        if (Sub) { Sub->Dots.Reset(); Sub->Artillery.Reset(); Sub->ConstructThreat.Reset(); }
    };
    bool bPass = true; int32 Checks = 0;
    auto Check = [&](bool bOk, const TCHAR* Why) { ++Checks; if (!bOk) { bPass = false; UE_LOG(LogCireKitsTests, Error, TEXT("CIRE_KITS_FAIL %s"), Why); } };
    const FVector Origin(0, -2100, 3092);
    auto Hero = [&](const TCHAR* Profile, int32 Team, FVector Offset)
    {
        FActorSpawnParameters P; P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        auto* H = World->SpawnActor<ACireHero>(Origin + Offset, FRotator::ZeroRotator, P);
        if (H) { Actors.Add(H); H->SetActorTickEnabled(false); H->TeamId = Team; H->DraftProfile(Profile); H->Offers.Reset(); H->CurrentOffer = {};
            H->CriticalChance = 0; H->Health = H->MaxHealth = 5000; H->Mana = H->MaxMana = 1000; H->Energy = 100; Mode->Heroes.Add(H); }
        return H;
    };
    auto Monster = [&](FVector Offset)
    {
        FActorSpawnParameters P; P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        auto* M = World->SpawnActor<ACireMonster>(Origin + Offset + FVector(0, 0, -4), FRotator::ZeroRotator, P);
        if (M) { Actors.Add(M); M->SetActorTickEnabled(false); M->Lane = 0; M->Health = M->MaxHealth = 100000; Mode->Monsters.Add(M); }
        return M;
    };
    auto Learn = [&](ACireHero* H, const FString& Id, int32 Level)
    {
        if (!H->Skills.Contains(Id)) { H->Skills.Add(Id); H->Cooldowns.Add(0.f); }
        if (H->Inventory) { H->Inventory->SkillRanks.RemoveAll([&](const FCireSkillRank& R) { return R.Id == Id; }); FCireSkillRank R; R.Id = Id; R.Level = Level; H->Inventory->SkillRanks.Add(R); }
        return H->Skills.IndexOfByKey(Id);
    };
    ACireHero* Tank = Hero(TEXT("knight"), 0, FVector(0, 0, 0));
    ACireHero* Ranger = Hero(TEXT("ranger"), 0, FVector(-150, 150, 0));
    ACireHero* Wizard = Hero(TEXT("wizard"), 0, FVector(-150, -150, 0));
    ACireHero* Healer = Hero(TEXT("scholar"), 0, FVector(-300, 0, 0));
    ACireMonster* M1 = Monster(FVector(200, 0, 0));
    ACireMonster* M2 = Monster(FVector(600, 400, 0));
    if (!Tank || !Ranger || !Wizard || !Healer || !M1 || !M2) { UE_LOG(LogCireKitsTests, Error, TEXT("CIRE_KITS_FAIL fixtures")); return false; }
    Tank->Strength = 30; Ranger->Agility = 34; Wizard->Intelligence = 40; Healer->Intelligence = 36;
    const float Power = Mode->Power(0);

    // ---- 1. universal primary-stat scaling ----
    {
        const FCireAbilityDef* Bash = CireAbilityDB::Find(TEXT("shield_bash"));
        Check(Bash && Bash->ScalePrimary > 0 && FMath::IsNearlyEqual(Amount(Tank, TEXT("shield_bash")), Bash->ScaleBase + Bash->ScalePrimary * 30.f, .01f), TEXT("tank stun scales with STR"));
        const FCireAbilityDef* Shot = CireAbilityDB::Find(TEXT("piercing_shot"));
        Check(Shot && FMath::IsNearlyEqual(Amount(Ranger, TEXT("piercing_shot")), Shot->ScaleBase + Shot->ScalePrimary * 34.f, .01f), TEXT("ranger ability scales with AGI"));
        const FCireAbilityDef* Lance = CireAbilityDB::Find(TEXT("ember_lance"));
        Check(Lance && FMath::IsNearlyEqual(Amount(Wizard, TEXT("ember_lance")), Lance->ScaleBase + Lance->ScalePrimary * 40.f, .01f), TEXT("caster ability scales with INT"));
        const FCireAbilityDef* Light = CireAbilityDB::Find(TEXT("restoring_light"));
        Check(Light && Light->ScaleComponent == TEXT("heal") && FMath::IsNearlyEqual(Amount(Healer, TEXT("restoring_light")), Light->ScaleBase + Light->ScalePrimary * 36.f, .01f), TEXT("heal scales with INT"));
        // Same ability, any primary: a STR tank and an INT caster with equal primaries match.
        Tank->Strength = 40; Check(FMath::IsNearlyEqual(Amount(Tank, TEXT("cleaving_strike")), Amount(Wizard, TEXT("cleaving_strike"))), TEXT("school/role-agnostic coefficient")); Tank->Strength = 30;
        // Every damaging / healing implemented ability declares a primary coefficient; no per-stat scaling remains.
        int32 Missing = 0;
        for (const FCireAbilityDef& D : CireAbilityDB::All())
            if (D.IsImplemented() && !D.IsPassive() && (D.EffectLabel.Contains(TEXT("damage")) || D.EffectLabel.Contains(TEXT("heal"))) && !D.EffectLabel.StartsWith(TEXT("%")) && D.ScalePrimary <= 0) ++Missing;
        Check(Missing == 0, TEXT("every damage/heal ability has a primary coefficient"));
        // End to end: legacy Shield Slam through the real cast + damage pipeline.
        const int32 Slot = Learn(Tank, TEXT("shield_slam"), 1);
        Tank->Target = M1; const float Before = M1->Health; Tank->GlobalCooldown = 0;
        Tank->Cast(Slot);
        Check(FMath::IsNearlyEqual(Before - M1->Health, Amount(Tank, TEXT("shield_slam")) * Power, 1.f), TEXT("Shield Slam deals base + coef x STR"));
        const FString Tip = DescribeFor(Tank, TEXT("shield_bash"), 15);
        Check(Tip.Contains(TEXT("Primary (STR 30)")) && Tip.Contains(TEXT("Lv 15: +")), TEXT("tooltip shows the STR line and the level-15 bonus"));
    }
    // ---- 2. summons / constructs: owner primary, attack speed, CDR ----
    ACireMechTank* Mech = nullptr;
    {
        FString Why;
        Mech = ACireMechTank::SpawnFor(Tank, Origin + FVector(100, -250, 0), &Why);
        if (!Mech) UE_LOG(LogCireKitsTests, Error, TEXT("CIRE_KITS_MECH_SPAWN %s"), *Why);
        Check(Mech != nullptr, TEXT("Mechanical Tank spawns"));
        if (Mech)
        {
            Actors.Add(Mech); Mech->SetActorTickEnabled(false);
            Check(FMath::IsNearlyEqual(Mech->SummonSpec.Damage, Amount(Tank, TEXT("mechanical_tank")) * Power, .01f), TEXT("mech damage = base + coef x owner STR"));
            Tank->Agility = 60; Tank->CDR = .3f;
            const float Mult = AttackSpeedMultiplier(Tank);
            Check(Mult > 1.5f && FMath::IsNearlyEqual(InheritedInterval(Mech, 1.5f), 1.5f / Mult, .001f), TEXT("summon attack interval uses the owner's attack speed"));
            Check(FMath::IsNearlyEqual(InheritedCooldown(Mech, 8.f), 5.6f, .001f), TEXT("summon ability cooldowns use the owner's CDR"));
            Tank->Agility = 10; Tank->CDR = 0;
        }
        FCireConstructSpec Turret;
        Check(CireTechConstructs::BuildSpec(Wizard, TEXT("photon_turret"), Turret) && FMath::IsNearlyEqual(Turret.AttackDamage, Amount(Wizard, TEXT("photon_turret")) * Power, .5f),
            TEXT("turret bolt = recipe base + coef x owner INT (matches the DB)"));
        const float SpeedBefore = AttackSpeedMultiplier(Wizard);
        Wizard->Agility += 50;
        Check(AttackSpeedMultiplier(Wizard) > SpeedBefore + .45f, TEXT("owner attack speed rises with AGI"));
        Wizard->Agility -= 50;
    }
    // ---- 3. monsters target summons and constructs (threat) ----
    if (Mech)
    {
        CireThreat::Clear(M2);
        CireCombat::ApplyDamage(Mech, M2, 50.f, TEXT("Mech Slam"));
        Check(M2->Threat.Contains(Mech) && M2->Victim == Mech, TEXT("a summon's damage makes it the monster's victim"));
        // Constructs need level ground: stage the owner on the lane for the placement, then return it.
        TArray<ACireConstruct*> Built; FString DeployWhy;
        const FVector WizardHome = Wizard->GetActorLocation();
        FVector Lane = CireLanePath::PointAlongRoute(World, 0, .5f);
        FHitResult Floor; FCollisionQueryParams Q(SCENE_QUERY_STAT(CireKitsFloor), false);
        for (AActor* A : Actors) Q.AddIgnoredActor(A);
        if (World->LineTraceSingleByObjectType(Floor, Lane + FVector(0, 0, 3000), Lane - FVector(0, 0, 6000), FCollisionObjectQueryParams(ECC_WorldStatic), Q)) Lane = Floor.ImpactPoint;
        Wizard->SetActorLocation(Lane + FVector(0, 0, 100));
        for (const FVector Offset : {FVector(300, 0, 0), FVector(-300, 0, 0), FVector(0, 300, 0), FVector(0, -300, 0), FVector(200, 200, 0)})
            if (Built.IsEmpty()) Built = CireTechConstructs::Deploy(Wizard, TEXT("photon_turret"), Lane + Offset, &DeployWhy);
        Wizard->SetActorLocation(WizardHome);
        ACireConstruct* Tower = Built.Num() ? Built[0] : nullptr;
        if (!Tower) UE_LOG(LogCireKitsTests, Error, TEXT("CIRE_KITS_TURRET_DEPLOY %s"), *DeployWhy);
        Check(Tower != nullptr, TEXT("turret deploys"));
        if (Tower)
        {
            Actors.Add(Tower);
            const float Mult = AttackSpeedMultiplier(Wizard);
            Check(FMath::IsNearlyEqual(InheritedInterval(Tower, .8f), .8f / Mult, .001f), TEXT("turret fire interval uses the owner's attack speed"));
            AddConstructThreat(M2, Tower, M2->Threat.FindRef(Mech) * 3.f + 100.f);
            Check(ConstructVictim(M2) == Tower, TEXT("a construct holding more threat becomes the monster's target"));
            M2->SetActorLocation(Tower->GetActorLocation() + FVector(60, 0, 0)); M2->AttackTimer = 0;
            const float TowerBefore = Tower->Health;
            Check(MonsterPursueConstruct(M2) && Tower->Health < TowerBefore, TEXT("the monster smashes the construct"));
            M2->SetActorLocation(Origin + FVector(600, 400, -4));
        }
    }
    // ---- 4. shield block and the armour / MR penalty ----
    {
        Check(CarriesShield(Tank) && IsShieldTank(Tank) && !CarriesShield(Ranger), TEXT("knight carries a shield, ranger does not"));
        Check(FMath::IsNearlyEqual(DefenseMultiplier(Tank), .9f) && FMath::IsNearlyEqual(DefenseMultiplier(Ranger), 1.f), TEXT("shield tank -10% armour / MR"));
        int32 Blocked = 0, Odd = 0;
        for (int32 I = 0; I < 400; ++I)
        {
            const float Out = ModifyIncomingDamage(Tank, M1, TEXT("Monster attack"), 100.f);
            if (FMath::IsNearlyEqual(Out, 50.f)) ++Blocked; else if (!FMath::IsNearlyEqual(Out, 100.f)) ++Odd;
        }
        Check(Odd == 0 && Blocked >= 80 && Blocked <= 160, TEXT("~30% of physical hits are blocked for 50%"));
        int32 MagicBlocked = 0;
        for (int32 I = 0; I < 100; ++I) MagicBlocked += FMath::IsNearlyEqual(ModifyIncomingDamage(Tank, Wizard, TEXT("Ember Lance"), 100.f), 50.f) ? 1 : 0;
        Check(MagicBlocked == 0, TEXT("magic damage is never blocked"));
        FString Why;
        Check(MeetsRequirement(Tank, TEXT("shield_bash")) && !MeetsRequirement(Ranger, TEXT("shield_bash"), &Why) && !Why.IsEmpty(), TEXT("shield skills: shield users only"));
        Check(MeetsRequirement(Ranger, TEXT("artillery")) && !MeetsRequirement(Tank, TEXT("artillery")), TEXT("range skills: ranged champions only"));
    }
    // ---- 5. Mechanical Tank: taunt choice and slam ----
    if (Mech)
    {
        M1->Victim = Tank; M2->Victim = Ranger; M2->ForcedVictim.Reset(); M2->ForcedVictimUntil = 0; M1->ForcedVictim.Reset();
        CireThreat::Engage(M1, Tank); CireThreat::Engage(M2, Ranger); M2->Victim = Ranger; M1->Victim = Tank;
        Mech->SetActorLocation(Origin + FVector(150, -120, 0));
        M2->SetActorLocation(Origin + FVector(500, -120, -4));
        Check(Mech->ChooseAttackTarget() == M2, TEXT("mech attacks the enemy hitting an ally other than the summoner"));
        AActor* Taunted = Mech->TryTaunt();
        Check(Taunted == M2 && M2->ForcedVictim.Get() == Mech && M2->Victim == Mech, TEXT("taunt pulls an enemy that is not attacking the summoner"));
        M1->SetActorLocation(Mech->GetActorLocation() + FVector(120, 0, 0));
        const float Before = M1->Health;
        Check(Mech->Slam() >= 1 && M1->Health < Before && M1->Threat.FindRef(Mech) > 0, TEXT("slam hits nearby enemies and generates threat"));
        Check(!CireBuffs::IsActive(M1, TEXT("mech_weakened")), TEXT("no attack-speed cut below level 15"));
        Learn(Tank, TEXT("mechanical_tank"), 15);
        Mech->Slam();
        Check(CireBuffs::IsActive(M1, TEXT("mech_weakened")) && FMath::IsNearlyEqual(MonsterAttackRate(M1), .9f), TEXT("level-15 slam: enemy attack speed -10%"));
    }
    // ---- 6. level 15: every skill has a bonus / aura, and they apply ----
    {
        int32 NoBonus = 0, NoAura = 0;
        for (const FCireAbilityDef& D : CireAbilityDB::All())
        {
            if (D.IsPassive()) { if (D.Aura15.IsNone() && D.Level15Special.IsEmpty()) ++NoAura; }
            else if (D.Level15Bonus.IsNone() && D.Level15Special.IsEmpty()) ++NoBonus;
        }
        Check(NoBonus == 0 && NoAura == 0, TEXT("every active has a level-15 bonus and every passive an aura"));
        FCireAbilityDef Probe = *CireAbilityDB::Find(TEXT("piercing_shot"));
        ACireMonster* Dummy = Monster(FVector(900, -600, 0));
        auto Bonus = [&](const TCHAR* Id) { Probe.Level15Bonus = FName(Id); ApplyLevel15(Ranger, Probe, Dummy, 100.f); };
        const float Now = World->GetTimeSeconds();
        Bonus(TEXT("stun")); Check(CireCrowdControl::IsStunned(Dummy), TEXT("level-15 stun"));
        Bonus(TEXT("slow")); Check(Dummy->SlowUntil > Now, TEXT("level-15 slow"));
        Bonus(TEXT("healCut")); Check(CireCrowdControl::HealingReceivedCut(Dummy) > .3f, TEXT("level-15 heal cut"));
        Bonus(TEXT("damageAmp")); Check(FMath::IsNearlyEqual(ModifyOutgoingDamage(Ranger, Dummy, 100.f, TEXT("Piercing Shot")), 112.f, .01f), TEXT("level-15 damage amp +12%"));
        Bonus(TEXT("vulnerability")); Check(FMath::IsNearlyEqual(DefenseMultiplier(Dummy), .8f), TEXT("level-15 Vulnerability ignores 20% of defences"));
        CireBuffs::Apply(Dummy, TEXT("npc_bloodlust"), 10.f, Dummy);
        Bonus(TEXT("purge")); Check(!CireBuffs::IsActive(Dummy, TEXT("npc_bloodlust")), TEXT("level-15 purge strips buffs"));
        if (Sub)
        {
            Sub->Dots.Reset(); const float HpBefore = Dummy->Health;
            Bonus(TEXT("dot"));
            for (int32 I = 0; I < 5; ++I) Sub->Tick(1.f);
            Check(FMath::IsNearlyEqual(HpBefore - Dummy->Health, 40.f * 1.12f, 1.f) || FMath::IsNearlyEqual(HpBefore - Dummy->Health, 40.f, 1.f), TEXT("level-15 DoT deals 40% of the hit over 4s"));
        }
        // End to end: Shield Slam at level 15 stuns (its assigned bonus).
        CireBuffs::ClearAll(M2); CireThreat::Clear(M2); M2->SetActorLocation(Tank->GetActorLocation() + FVector(150, 0, -4));
        const int32 Slot = Learn(Tank, TEXT("shield_slam"), 15);
        Tank->Cooldowns[Slot] = 0; Tank->GlobalCooldown = 0; Tank->Energy = 100; Tank->Target = M2;
        Tank->Cast(Slot);
        Check(CireCrowdControl::IsStunned(M2), TEXT("Shield Slam level 15 adds its stun"));
        // Team auras from passives at level 15.
        Check(PartyAuras(Ranger).Armor == 0, TEXT("no aura below level 15"));
        Learn(Tank, TEXT("stone_skin"), 15); Learn(Healer, TEXT("battle_rhythm"), 15);
        const auto Totals = PartyAuras(Ranger);
        Check(FMath::IsNearlyEqual(Totals.Armor, 15.0) && FMath::IsNearlyEqual(Totals.AttackSpeed, .30), TEXT("level-15 passives grant party auras"));
        Check(FMath::IsNearlyEqual(FlatDefense(Ranger, true), 15.f) && FMath::IsNearlyEqual(AttackSpeedBonus(Ranger), .30f), TEXT("auras feed armour and attack speed"));
        if (Sub) { Sub->RefreshAuras(); Check(CireBuffs::IsActive(Ranger, AuraBuffId(K::Aura::Armor)), TEXT("aura shows in the buff registry")); }
        Tank->Skills.Remove(TEXT("stone_skin")); Healer->Skills.Remove(TEXT("battle_rhythm"));
    }
    // ---- 7. Headshot 2x / 3x ----
    {
        CireBuffs::ClearAll(M1); M1->Health = M1->MaxHealth;
        Learn(Ranger, TEXT("headshot"), 1);
        float Before = M1->Health;
        for (int32 I = 0; I < 400; ++I) CireCombat::ApplyDamage(Ranger, M1, 10.f, TEXT("bow strike"));
        float Extra = (Before - M1->Health) - 4000.f;
        int32 Procs = FMath::RoundToInt(Extra / 20.f);
        Check(Procs >= 15 && Procs <= 70 && FMath::IsNearlyEqual(Extra, Procs * 20.f, .5f), TEXT("Headshot: ~10% extra hits for 2x"));
        Learn(Ranger, TEXT("headshot"), 15); M1->Health = M1->MaxHealth; Before = M1->Health;
        for (int32 I = 0; I < 400; ++I) CireCombat::ApplyDamage(Ranger, M1, 10.f, TEXT("bow strike"));
        Extra = (Before - M1->Health) - 4000.f; Procs = FMath::RoundToInt(Extra / 30.f);
        Check(Procs >= 15 && Procs <= 70 && FMath::IsNearlyEqual(Extra, Procs * 30.f, .5f), TEXT("Headshot level 15: extra hit is 3x"));
        Ranger->Skills.Remove(TEXT("headshot"));
    }
    // ---- 8. Artillery ----
    {
        const int32 Slot = Learn(Ranger, TEXT("artillery"), 15);
        const float RangeBefore = Ranger->BasicAttackRange(), SpeedBefore = AttackSpeedMultiplier(Ranger);
        Ranger->Cooldowns[Slot] = 0; Ranger->GlobalCooldown = 0; Ranger->Energy = 100; Ranger->Target = M1;
        Check(Cast(Ranger, Slot, TEXT("artillery")) && IsArtilleryActive(Ranger), TEXT("Artillery starts"));
        Check(Ranger->BasicAttackRange() > 1.e6f && RangeBefore < 5000.f, TEXT("Artillery: unlimited basic range"));
        Check(FMath::IsNearlyEqual(AttackSpeedMultiplier(Ranger) - SpeedBefore, 1.f, .01f), TEXT("Artillery: +100% attack speed"));
        Check(BlocksCasting(Ranger, TEXT("piercing_shot")), TEXT("Artillery: basic attacks only"));
        M1->Health = M1->MaxHealth; M2->SetActorLocation(M1->GetActorLocation() + FVector(200, 0, 0)); M2->Health = M2->MaxHealth;
        for (int32 I = 0; I < 3; ++I) CireCombat::ApplyDamage(Ranger, M1, 100.f, TEXT("bow strike"));
        const float Dealt = M1->MaxHealth - M1->Health; const float M2Before = M2->Health;
        if (Sub)
        {
            Sub->LastBombDamage = 0;
            for (int32 I = 0; I < 9; ++I) Sub->Tick(1.f);
            Check(!IsArtilleryActive(Ranger), TEXT("Artillery ends after 8s"));
            Check(FMath::IsNearlyEqual(Sub->LastBombDamage, Dealt, 1.f) && Dealt >= 299.f, TEXT("level-15 bomb = the damage Artillery dealt"));
            Check(M2Before - M2->Health >= Dealt - 1.f, TEXT("the bomb hits everything in its large radius"));
        }
    }
    UE_LOG(LogCireKitsTests, Display, TEXT("CIRE_KITS_%s checks=%d"), bPass ? TEXT("PASS") : TEXT("FAIL"), Checks);
    return bPass;
}
#endif
