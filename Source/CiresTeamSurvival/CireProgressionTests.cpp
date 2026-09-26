// progression-shop: in-engine checks for items, shop access, stats application, consumables,
// actives, teleport, loot chests, fair distribution, challenge gating and the NPC prep pause.
// Run by -CireCombatExpansionProbe (Tools/RunExpansionChecks.py --only native) and by
// Tools/RunProgressionChecks.py. Pure rules are covered natively by Tests/ItemRulesTests.cpp.
#include "CireItems.h"
#include "CireScalingKits.h" // scaling-kits
#include "CireLoot.h"
#include "CireSkillShop.h"
#include "CireGame.h"
#include "CireNPCArchetypes.h"
#include "CireNPCCombat.h"
#include "CireNPCState.h"
#include "CireSkillTuning.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Misc/ScopeExit.h"

#if !UE_BUILD_SHIPPING
DEFINE_LOG_CATEGORY_STATIC(LogCireProgressionTests, Log, All);
namespace CI = Cires::Items;

namespace
{
struct FFixture
{
    ACireGameMode* Mode;
    Cires::MatchClock Clock;
    TArray<ACireHero*> Heroes;
    TArray<ACireMonster*> Monsters;
    TSet<int32> Rewarded;
    TArray<AActor*> Spawned;
    explicit FFixture(ACireGameMode* InMode) : Mode(InMode), Clock(InMode->Clock), Heroes(InMode->Heroes), Monsters(InMode->Monsters), Rewarded(InMode->RewardedPacks)
    {
        Mode->Clock = Cires::MatchClock();
        Mode->Heroes.Reset();
        Mode->Monsters.Reset();
    }
    ~FFixture()
    {
        for (AActor* Actor : Spawned) if (IsValid(Actor)) Actor->Destroy();
        for (TActorIterator<ACireLootDrop> It(Mode->GetWorld()); It; ++It) It->Destroy();
        for (TActorIterator<ACireLanternWard> It(Mode->GetWorld()); It; ++It) It->Destroy();
        Mode->Clock = Clock; Mode->Heroes = Heroes; Mode->Monsters = Monsters; Mode->RewardedPacks = Rewarded;
    }
    ACireHero* Hero(int32 Team, int32 Archetype, FVector Offset = FVector::ZeroVector)
    {
        FActorSpawnParameters Params;
        Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        auto* H = Mode->GetWorld()->SpawnActor<ACireHero>(Mode->BasePosition(Team) + Offset, FRotator::ZeroRotator, Params);
        if (!H) return nullptr;
        Spawned.Add(H);
        H->SetActorTickEnabled(false);
        if (H->Inventory) H->Inventory->SetComponentTickEnabled(false);
        H->TeamId = Team;
        H->Draft(Archetype);
        H->Offers.Reset();
        H->CriticalChance = 0;
        H->Gold = 0;
        Mode->Heroes.Add(H);
        return H;
    }
    ACireMonster* Monster(int32 Lane, FVector Location)
    {
        FActorSpawnParameters Params;
        Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        auto* M = Mode->GetWorld()->SpawnActor<ACireMonster>(Location, FRotator::ZeroRotator, Params);
        if (!M) return nullptr;
        Spawned.Add(M);
        M->SetActorTickEnabled(false);
        M->Lane = Lane;
        M->Health = M->MaxHealth = 5000;
        Mode->Monsters.Add(M);
        return M;
    }
    void Prep() { Mode->Clock = Cires::MatchClock(); Mode->Clock.BeginIntermission(); }
    void Survival() { Mode->Clock = Cires::MatchClock(); }
    void Recovery() { Prep(); Mode->Clock.Advance(Mode->Clock.GetDurations().Intermission); Mode->Clock.ResolveArena(); }
    void Round(int32 Target)
    {
        Mode->Clock = Cires::MatchClock();
        while (Mode->Clock.Round() < Target)
        {
            Mode->Clock.BeginIntermission();
            Mode->Clock.Advance(Mode->Clock.GetDurations().Intermission);
            Mode->Clock.ResolveArena();
            Mode->Clock.Advance(Mode->Clock.GetDurations().Recovery);
        }
    }
};

struct FChecker
{
    const TCHAR* Suite;
    int32 Count = 0;
    bool bPass = true;
    void operator()(bool bValue, const TCHAR* Label)
    {
        ++Count;
        if (!bValue) { bPass = false; UE_LOG(LogCireProgressionTests, Error, TEXT("CIRE_%s_CHECK_FAIL %s"), Suite, Label); }
    }
};

FName N(const TCHAR* Id) { return FName(Id); }
}

bool CireItems::RunSmoke(ACireGameMode* Mode)
{
    if (!Mode || !Mode->HasAuthority()) return false;
    FChecker Check{TEXT("ITEMS")};
    FFixture F(Mode);
    const auto& D = Get();
    Check(D.bValid && D.Order.Num() >= 45 && D.Order.Num() <= 60, TEXT("Items.json loads a 45-60 item catalog")); // items-v2
    int32 Tiers[4] = {0, 0, 0, 0}, Actives = 0, Uniques = 0;
    for (const auto& Item : D.Catalog.Items) { ++Tiers[static_cast<int32>(Item.Tier)]; Actives += Item.HasActive(); Uniques += Item.Unique; }
    Check(Tiers[0] >= 6 && Tiers[1] >= 10 && Tiers[2] >= 3 && Tiers[3] >= 12 && Actives >= 5 && Uniques >= 10, TEXT("catalog spans consumables, components, legendaries, actives and uniques"));
    for (const TCHAR* Role : {TEXT("tank"), TEXT("physical"), TEXT("caster"), TEXT("support")})
        Check(D.Recommended.Contains(Role) && D.Recommended[Role].Num() == 3 && D.Recommended[Role][1].Num() >= 3, TEXT("every role has a recommended build"));
    FString BadJson;
    FCireItemData Scratch;
    Check(!ParseJson(TEXT("{\"schemaVersion\":1,\"items\":[{\"id\":\"a\",\"name\":\"A\",\"tier\":\"epic\",\"cost\":5,\"components\":[\"ghost\"]}]}"), Scratch, BadJson) && BadJson.Contains(TEXT("ghost")), TEXT("unknown recipe component rejected"));
    Check(!ParseJson(TEXT("{\"schemaVersion\":1,\"items\":[{\"id\":\"a\",\"name\":\"A\",\"tier\":\"basic\",\"cost\":5,\"stats\":{\"power\":3}}]}"), Scratch, BadJson), TEXT("unknown stat key rejected"));

    ACireHero* Hero = F.Hero(0, 0, FVector(3200, 0, 0)); // far from town
    ACireHero* Ally = F.Hero(0, 2, FVector(0, 200, 0));
    if (!Hero || !Ally || !Hero->Inventory) { Check(false, TEXT("fixture heroes")); return false; }
    UCireInventory* Inv = Hero->Inventory;
    FString Message;
    // ---- shop access: prep anywhere, recovery in town, survival closed
    F.Survival();
    Hero->Gold = 5000;
    Check(!Inv->Buy(N(TEXT("bloodstone_shard")), Message) && Message.Contains(TEXT("intermission")) && Hero->Gold == 5000, TEXT("survival shopping rejected with the prep hint"));
    F.Recovery();
    Message.Reset();
    Check(!Inv->Buy(N(TEXT("bloodstone_shard")), Message) && Hero->Gold == 5000, TEXT("recovery shopping away from town rejected"));
    F.Prep();
    const float BaseHealth = Hero->MaxHealth;
    // Prices come from Items.json (rescaled with the economy), so checks read them from the catalog.
    auto Recipe = [&](const char* Id) { const auto* Def = D.Catalog.Find(Id); return Def ? Def->RecipeCost : -1; };
    auto Total = [&](const char* Id) { const auto* Def = D.Catalog.Find(Id); return Def ? Def->TotalCost : -1; };
    Message.Reset();
    Check(Inv->Buy(N(TEXT("bloodstone_shard")), Message) && Hero->Gold == 5000 - Recipe("bloodstone_shard") && Inv->Equipment[0].Id == N(TEXT("bloodstone_shard")), TEXT("prep: buy anywhere on the map"));
    // items-v2: item stats come from the catalog (primary stat + flat stats; Eric's universal-scaling ruling).
    auto StatOf = [&](const char* Id, CI::ItemStat Stat) { const auto* Def = D.Catalog.Find(Id); return Def ? static_cast<float>(Def->Stats.Get(Stat)) : -1.f; };
    const float Shard = StatOf("bloodstone_shard", CI::ItemStat::Health);
    Check(Shard > 0 && FMath::IsNearlyEqual(Hero->MaxHealth, BaseHealth + Shard), TEXT("item health applied to max health"));
    // ---- stats application
    const int32 BaseStrength = Hero->Strength;
    const float BaseAD = Hero->AttackDamage();
    const float OxPrimary = StatOf("gauntlet_of_the_ox", CI::ItemStat::Primary), OxHealth = StatOf("gauntlet_of_the_ox", CI::ItemStat::Health);
    Check(Inv->Buy(N(TEXT("gauntlet_of_the_ox")), Message) && Hero->Strength == BaseStrength + FMath::RoundToInt(OxPrimary) &&
        FMath::IsNearlyEqual(Hero->MaxHealth, BaseHealth + Shard + OxHealth + static_cast<float>(Cires::HealthPerStrength) * OxPrimary), TEXT("primary stat lands on STR with its derived health (10 per point)"));
    Check(FMath::IsNearlyEqual(Hero->AttackDamage(), BaseAD + OxPrimary), TEXT("primary attribute from items adds attack damage"));
    const float SwordPrimary = StatOf("rusted_longsword", CI::ItemStat::Primary);
    Check(Inv->Buy(N(TEXT("rusted_longsword")), Message) && FMath::IsNearlyEqual(Hero->AttackDamage(), BaseAD + OxPrimary + SwordPrimary), TEXT("adaptive primary stat applied"));
    // rules-conformance: items grant only the primary stat and flat stats (no CDR, attack speed, crit or lifesteal).
    const float CDRBefore = Hero->CDR;
    Check(Inv->Buy(N(TEXT("sandglass_charm")), Message) && FMath::IsNearlyEqual(Hero->CDR, CDRBefore) && StatOf("sandglass_charm", CI::ItemStat::Mana) > 0 &&
        FMath::IsNearlyEqual(static_cast<float>(Inv->Totals().Stats.Get(CI::ItemStat::Mana)), StatOf("sandglass_charm", CI::ItemStat::Mana)), TEXT("a hybrid component adds flat mana, never cooldown reduction"));
    Check(Inv->Buy(N(TEXT("boiled_jerkin")), Message) && FMath::IsNearlyEqual(static_cast<float>(Inv->Totals().Stats.Get(CI::ItemStat::Armor)), StatOf("boiled_jerkin", CI::ItemStat::Armor)), TEXT("flat armor applied"));
    const float BeforeDagger = Hero->AttackDamage();
    Check(FMath::IsNearlyEqual(AttackSpeedBonus(Hero), 0.f) && Inv->Buy(N(TEXT("bone_dagger")), Message) && FMath::IsNearlyEqual(AttackSpeedBonus(Hero), 0.f) &&
        FMath::IsNearlyEqual(Hero->AttackDamage(), BeforeDagger + StatOf("bone_dagger", CI::ItemStat::Primary)), TEXT("the dagger adds primary stat, not attack speed"));
    // ---- slot limit: 6/6 full
    Check(Inv->ToRules().FreeEquipment() == 0 && !Inv->Buy(N(TEXT("hexweave_cloak")), Message) && Message.Contains(TEXT("full")), TEXT("six-slot limit"));
    // ---- recipe consumes owned components and charges only the recipe
    int32 Gold = Hero->Gold;
    Check(Inv->Buy(N(TEXT("serrated_cleaver")), Message) && Hero->Gold == Gold - Recipe("serrated_cleaver") && Inv->ToRules().CountOf("rusted_longsword") == 0 && Inv->ToRules().CountOf("bone_dagger") == 0, TEXT("recipe consumes components, charges recipe cost"));
    Check(Inv->Buy(N(TEXT("rusted_longsword")), Message), TEXT("second component for the legendary"));
    Gold = Hero->Gold;
    Check(Inv->Buy(N(TEXT("nightfall_reaver")), Message) && Hero->Gold == Gold - Recipe("nightfall_reaver") && Hero->GearRank == 1, TEXT("legendary built from owned epic + component"));
    Check(FMath::IsNearlyEqual(Hero->CriticalMultiplier, CireSkillTuning::Get().CritMultiplier + .25f, .001f), TEXT("unique passive raises crit multiplier"));
    Check(StatOf("nightfall_reaver", CI::ItemStat::CritChance) == 0 && FMath::IsNearlyEqual(Hero->CriticalChance, CireSkillTuning::Get().CritChance, .001f), TEXT("items add no critical chance"));
    Check(!Inv->Buy(N(TEXT("nightfall_reaver")), Message) && Message.Contains(TEXT("unique")), TEXT("unique item cannot be bought twice"));
    // ---- sell + undo
    Gold = Hero->Gold;
    const int32 Slot = Inv->Equipment.IndexOfByPredicate([](const FCireItemSlot& S) { return S.Id == FName(TEXT("bloodstone_shard")); });
    Check(Slot != INDEX_NONE && Inv->SellSlot(Slot, false, Message) && Hero->Gold == Gold + FMath::RoundToInt(Total("bloodstone_shard") * .6f), TEXT("sell returns 60% of total cost"));
    Check(Inv->UndoLast(Message) && Hero->Gold == Gold && Inv->Equipment[Slot].Id == N(TEXT("bloodstone_shard")), TEXT("undo restores the sold item and gold"));
    Gold = Hero->Gold;
    Check(Inv->UndoLast(Message) && Hero->Gold == Gold + Recipe("nightfall_reaver") && Inv->ToRules().CountOf("nightfall_reaver") == 0 && Inv->ToRules().CountOf("serrated_cleaver") == 1 &&
        Inv->ToRules().CountOf("rusted_longsword") == 1, TEXT("undo reverts a recipe to its parts"));
    Inv->EndShopVisit();
    Check(!Inv->UndoLast(Message) && Inv->UndoDepth == 0, TEXT("closing the shop ends the undo history"));
    // ---- instant tomes and belt consumables
    const int32 Str = Hero->Strength;
    Check(Inv->Buy(N(TEXT("tome_of_ascendance")), Message) && Hero->Strength == Str + 3 && Inv->PrimaryTomePoints == 3, TEXT("primary stat tome raises the primary attribute permanently"));
    const int32 XP = Hero->Experience, Level = Hero->Level;
    Check(Inv->Buy(N(TEXT("tome_of_insight")), Message) && (Hero->Experience > XP || Hero->Level > Level), TEXT("experience tome grants XP"));
    Check(Inv->Buy(N(TEXT("vial_of_crimson")), Message) && Inv->Buy(N(TEXT("vial_of_crimson")), Message) && Inv->Belt[0].Charges == 2, TEXT("consumables stack on the belt"));
    Hero->Health = 100;
    Check(Inv->UseSlot(0, true, Message) && Inv->Belt[0].Charges == 1 && Inv->Restores.Num() == 1, TEXT("using a potion spends one charge"));
    for (int32 Step = 0; Step < 13; ++Step) Inv->TickComponent(1.f, LEVELTICK_All, nullptr);
    Check(FMath::IsNearlyEqual(Hero->Health, FMath::Min(Hero->MaxHealth, 250.f), 1.f) && Inv->Restores.Num() == 0, TEXT("potion restores 150 health over its duration"));
    Check(Inv->Buy(N(TEXT("elixir_of_wrath")), Message), TEXT("elixir purchased"));
    const float BeforeElixir = Hero->AttackDamage();
    const int32 ElixirSlot = Inv->Belt.IndexOfByPredicate([](const FCireItemSlot& S) { return S.Id == FName(TEXT("elixir_of_wrath")); });
    const float ElixirPrimary = D.Catalog.Find("elixir_of_wrath") ? static_cast<float>(D.Catalog.Find("elixir_of_wrath")->Use.Buff.Get(CI::ItemStat::Primary)) : 0.f;
    Check(ElixirSlot != INDEX_NONE && ElixirPrimary > 0 && Inv->UseSlot(ElixirSlot, true, Message) && FMath::IsNearlyEqual(Hero->AttackDamage(), BeforeElixir + ElixirPrimary) && Inv->Buffs.Num() >= 1, TEXT("elixir buff applies the primary stat"));
    // ---- active item: Void Rupture needs a hostile target, then starts its cooldown
    Hero->Gold = 5000;
    for (auto& Cell : Inv->Equipment) Cell = FCireItemSlot();
    Inv->Invalidate();
    Check(Inv->Buy(N(TEXT("voidglass_orb")), Message), TEXT("active item purchased"));
    F.Survival();
    ACireMonster* Target = F.Monster(0, Hero->GetActorLocation() + FVector(300, 0, 0));
    ACireMonster* Near = F.Monster(0, Hero->GetActorLocation() + FVector(450, 0, 0));
    const int32 Orb = Inv->Equipment.IndexOfByPredicate([](const FCireItemSlot& S) { return S.Id == FName(TEXT("voidglass_orb")); });
    Hero->Target = nullptr;
    Check(Orb != INDEX_NONE && !Inv->UseSlot(Orb, false, Message) && Inv->Equipment[Orb].ReadyAt <= 0, TEXT("active without a target fails and keeps its cooldown"));
    Hero->Target = Target;
    Check(Inv->UseSlot(Orb, false, Message) && Target->Health < 5000 && Near->Health < 5000, TEXT("Void Rupture damages the target and nearby enemies"));
    if (Orb == INDEX_NONE) { Check(false, TEXT("orb slot")); return false; }
    Check(Inv->Equipment[Orb].ReadyAt > Inv->Now() + 40 && !Inv->UseSlot(Orb, false, Message) && Message.Contains(TEXT("recharging")), TEXT("active item cooldown enforced"));
    // ---- armor / ward mitigation and lifesteal
    for (auto& Cell : Inv->Equipment) Cell = FCireItemSlot();
    Inv->Equipment[0].Id = N(TEXT("gravewarden_bulwark"));
    Inv->Invalidate();
    const float BulwarkArmor = (StatOf("gravewarden_bulwark", CI::ItemStat::Armor) + CireItems::StrengthDefense(Hero, true)) * CireKits::DefenseMultiplier(Hero), /* str-scaling: + STR armor */ /* scaling-kits: shield tanks -10% */ Block = StatOf("gravewarden_bulwark", CI::ItemStat::DamageBlock);
    Check(FMath::IsNearlyEqual(ModifyIncomingDamage(Hero, Target, TEXT("Monster attack"), 100.f), 100.f * (1.f - BulwarkArmor / (BulwarkArmor + 100.f)) - Block, .01f), TEXT("armor mitigates basic attacks, then the block"));
    Check(FMath::IsNearlyEqual(ModifyIncomingDamage(Hero, Target, TEXT("Shadow Bolt"), 100.f), CireItems::AfterStrengthDefense(Hero, 100.f, false) - Block, .01f), TEXT("armor does not mitigate spells (STR ward and the block do)"));
    // rules-conformance: HitGuard ("reduce instances of incoming damage"), Sanguine Sabre's Blood Parry.
    {
        Inv->Equipment[0] = FCireItemSlot(); Inv->Equipment[1].Id = N(TEXT("sanguine_sabre")); Inv->Invalidate(); Inv->HitGuardState = CI::ChargeState();
        const CI::ItemDef* Sabre = D.Catalog.Find("sanguine_sabre");
        const float Parry = Sabre && !Sabre->Passives.empty() ? static_cast<float>(Sabre->Passives[0].Amount) : 0.f;
        const int32 Charges = Sabre && !Sabre->Passives.empty() ? Sabre->Passives[0].Count : 0;
        const int32 Spent = Inv->HitGuardSpent;
        bool bGuarded = Parry > 0 && Charges == 2;
        for (int32 Hit = 0; Hit < Charges; ++Hit) bGuarded &= FMath::IsNearlyEqual(ModifyIncomingDamage(Hero, Target, TEXT("Shadow Bolt"), 100.f), CireItems::AfterStrengthDefense(Hero, 100.f, false) * (1.f - Parry / 100.f), .01f);
        Check(bGuarded && Inv->HitGuardSpent == Spent + Charges, TEXT("hit guard reduces the next hits by its percent"));
        Check(FMath::IsNearlyEqual(ModifyIncomingDamage(Hero, Target, TEXT("Shadow Bolt"), 100.f), CireItems::AfterStrengthDefense(Hero, 100.f, false), .01f), TEXT("hit guard runs out of charges"));
        Hero->Health = 100;
        OnDamageDealt(Hero, Target, 100.f, TEXT("sword strike"));
        Check(FMath::IsNearlyEqual(Hero->Health, 100.f), TEXT("items grant no lifesteal"));
        Inv->Equipment[1] = FCireItemSlot(); Inv->Equipment[0].Id = N(TEXT("gravewarden_bulwark")); Inv->Invalidate();
    }
    const float MonsterHealth = Target->Health;
    OnHeroDamaged(Hero, Target, TEXT("Monster attack"), 40.f);
    Check(Target->Health < MonsterHealth, TEXT("thorns reflect basic-attack damage"));
    // ---- move speed multiplier from boots
    Inv->Equipment[2].Id = N(TEXT("stalkers_treads"));
    Inv->Invalidate();
    Check(FMath::IsNearlyEqual(MoveSpeedMultiplier(Hero), 1.12f, .001f), TEXT("boots raise move speed"));
    // ---- loot grants: full bags convert to gold, belt items stack
    for (auto& Cell : Inv->Equipment) Cell.Id = N(TEXT("bloodstone_shard"));
    Inv->Invalidate();
    int32 Converted = 0;
    Gold = Hero->Gold;
    Check(!Inv->GrantItem(N(TEXT("rusted_longsword")), Converted) && Converted == Total("rusted_longsword") && Hero->Gold == Gold + Total("rusted_longsword"), TEXT("loot into a full bag converts to gold"));
    Check(Inv->HasRoomFor(N(TEXT("watchers_lantern"))) && Inv->GrantItem(N(TEXT("watchers_lantern")), Converted), TEXT("loot consumable goes to the belt"));
    UE_LOG(LogCireProgressionTests, Display, TEXT("CIRE_ITEMS_%s checks=%d"), Check.bPass ? TEXT("PASS") : TEXT("FAIL"), Check.Count);
    return RunV2Smoke(Mode) && Check.bPass; // items-v2: uniques, group actives, dodge charges, ultimate upgrades, mana economy
}

bool CireProgression::RunSmoke(ACireGameMode* Mode)
{
    if (!Mode || !Mode->HasAuthority()) return false;
    bool bGood = CireItems::RunSmoke(Mode);
    bGood = CireSkillShop::RunSmoke(Mode) && bGood; // progression-shop: economy, mode, Skill Shop, bots, builds
    FChecker Check{TEXT("PROGRESSION")};
    FFixture F(Mode);
    const auto& Loot = CireLoot::Get();
    Check(Loot.bValid && Loot.Tables.Num() >= 6 && Loot.Schedule.Bays.size() == 3, TEXT("LootTables.json loads tables and the pack schedule"));

    // ---- challenge gating: tiers unlock in later cycles and sit deeper along the route
    auto PackTiers = [&]() { TMap<int32, int32> Bays; for (ACireMonster* M : Mode->Monsters) if (IsValid(M) && M->PackId >= 0 && M->Lane == 0) Bays.Add(M->PackId % 50, M->Tier); return Bays; };
    auto ClearPacks = [&]() { for (ACireMonster* M : Mode->Monsters) if (IsValid(M)) { F.Spawned.AddUnique(M); M->Destroy(); } Mode->Monsters.Reset(); };
    F.Round(1); CireProgression::SpawnPacks(Mode, 1);
    TMap<int32, int32> Bays = PackTiers();
    Check(Bays.Num() == 1 && Bays.FindRef(1) == 1, TEXT("cycle 1 offers only the tier-1 outpost nearest town"));
    ClearPacks();
    F.Round(2); CireProgression::SpawnPacks(Mode, 1); Bays = PackTiers();
    Check(Bays.Num() == 2 && Bays.FindRef(2) == 2, TEXT("cycle 2 unlocks the tier-2 bay"));
    ClearPacks();
    F.Round(3); CireProgression::SpawnPacks(Mode, 1); Bays = PackTiers();
    Check(Bays.Num() == 2, TEXT("cycle 3 tier-3 bay waits for wave 2"));
    CireProgression::OnWaveSpawned(Mode, 2); Bays = PackTiers();
    Check(Bays.Num() == 3 && Bays.FindRef(3) == 3, TEXT("tier-3 bay appears when wave 2 spawns"));
    ClearPacks();
    F.Round(6); CireProgression::SpawnPacks(Mode, 1); Bays = PackTiers();
    Check(Bays.FindRef(1) == 3 && Bays.FindRef(2) == 4 && Bays.FindRef(3) == 5, TEXT("later cycles promote every bay"));
    const FVector Town = Mode->BasePosition(0);
    float Distances[4] = {0, 0, 0, 0};
    for (ACireMonster* M : Mode->Monsters) if (IsValid(M) && M->Lane == 0 && M->PackId >= 0) Distances[M->PackId % 50] = FVector::Dist2D(M->SpawnPosition, Town);
    Check(Distances[1] < Distances[2] && Distances[2] < Distances[3], TEXT("deeper bays sit farther from town"));
    bool bLeaders = true;
    for (int32 Bay = 1; Bay <= 3; ++Bay)
    {
        int32 Leaders = 0;
        for (ACireMonster* M : Mode->Monsters) if (IsValid(M) && M->Lane == 0 && M->PackId % 50 == Bay && M->GetNPCClassification() == ECireNPCClass::Boss) ++Leaders;
        bLeaders &= Leaders == 1;
    }
    Check(bLeaders, TEXT("every pack has one Pack Leader"));
    ClearPacks();

    // ---- NPC prep pause: timers freeze and resume exactly
    ACireMonster* M = F.Monster(0, Town + FVector(2500, 0, 0));
    if (!M || !M->NPCState) { Check(false, TEXT("pause fixture")); return false; }
    CireNPCCombat::ConfigureArchetype(M, CireNPCArchetypes::Get().PackLeader, 1, 2, 1);
    const float Now = M->GetWorld()->GetTimeSeconds();
    M->NPCState->ReadyAt.Add(TEXT("probe"), Now + 5.f);
    M->NPCState->RallyUntil = Now + 3.f;
    M->SlowUntil = Now + 2.f;
    M->NPCState->KiteReadyAt = Now - 1.f;
    CireProgression::PauseNPC(M, Now);
    Check(CireProgression::IsPaused(M), TEXT("monster paused"));
    CireProgression::ResumeNPC(M, Now + 60.);
    Check(!CireProgression::IsPaused(M) && FMath::IsNearlyEqual(M->NPCState->ReadyAt[TEXT("probe")], Now + 65.f, .01f) &&
        FMath::IsNearlyEqual(M->NPCState->RallyUntil, Now + 63.f, .01f) && FMath::IsNearlyEqual(M->SlowUntil, Now + 62.f, .01f) &&
        FMath::IsNearlyEqual(M->NPCState->KiteReadyAt, Now - 1.f, .01f), TEXT("cooldowns, buffs and slows resume with the same remaining time"));
    F.Prep();
    ACireHero* Bait = F.Hero(0, 0, FVector(2400, 0, 0));
    M->SetActorLocation(Bait->GetActorLocation() + FVector(150, 0, 0));
    M->Damage = 20;
    const float BaitHealth = Bait->Health;
    const FVector Start = M->GetActorLocation();
    for (int32 Step = 0; Step < 30; ++Step) CireNPCCombat::Tick(M, .1f);
    Check(CireProgression::IsPaused(M) && FMath::IsNearlyEqual(Bait->Health, BaitHealth) && M->CastingAbility.IsEmpty() &&
        FVector::Dist2D(Start, M->GetActorLocation()) < 1.f && M->GetCharacterMovement()->Velocity.IsNearlyZero(), TEXT("during prep NPCs neither move, attack nor cast"));
    F.Survival();
    CireNPCCombat::Tick(M, .1f);
    Check(!CireProgression::IsPaused(M), TEXT("survival resumes NPC logic"));

    // ---- loot: tables roll, chests spawn on kills, distribution is team-fair
    TArray<ACireHero*> Team;
    for (int32 Index = 0; Index < 5; ++Index) Team.Add(F.Hero(1, Index % 3, FVector(0, Index * 120 - 240, 0)));
    for (ACireHero* H : Team) if (H) H->Gold = 0;
    CI::LootBundle Bundle;
    Bundle.Gold = 50; Bundle.Experience = 100;
    Bundle.Items = {"bone_dagger", "bone_dagger", "bone_dagger", "bone_dagger", "bone_dagger"};
    Bundle.PrimaryTomes = {1};
    const TArray<FCireLootLine> Lines = CireLoot::Distribute(Mode, 1, Bundle, TEXT("Probe cache"), 99);
    int32 Daggers = 0; bool bEachOne = true;
    for (ACireHero* H : Team) { const int32 Count = H->Inventory->ToRules().CountOf("bone_dagger"); Daggers += Count; bEachOne &= Count == 1 && H->Gold == 50; }
    Check(Lines.Num() == 8 && Daggers == 5 && bEachOne, TEXT("gold to everyone; five item drops rotate to five different players"));
    int32 TomeOwners = 0;
    for (ACireHero* H : Team) TomeOwners += H->Inventory->PrimaryTomePoints > 0;
    Check(TomeOwners == 1, TEXT("a stat tome goes to exactly one player"));
    ACireMonster* Leader = F.Monster(1, Mode->BasePosition(1) + FVector(900, 0, 0));
    CireNPCCombat::ConfigureArchetype(Leader, CireNPCArchetypes::Get().PackLeader, 1, 3, 1);
    Leader->PackId = 1234;
    int32 DropsBefore = 0;
    for (TActorIterator<ACireLootDrop> It(Mode->GetWorld()); It; ++It) ++DropsBefore;
    CireLoot::OnMonsterKilled(Mode, Leader, Team[0], true);
    ACireLootDrop* Chest = nullptr;
    for (TActorIterator<ACireLootDrop> It(Mode->GetWorld()); It; ++It) if (!It->bOpened) Chest = *It;
    Check(Chest && Chest->TeamId == 1 && Chest->Tier == 3 && Chest->Bundle.Gold > 0, TEXT("pack leader + pack completion drop a tiered chest"));
    int32 GoldBefore = 0; for (ACireHero* H : Team) GoldBefore += H->Gold;
    Check(CireLoot::CollectAll(Mode) >= 1 && Chest && Chest->bOpened, TEXT("prep auto-collects unopened chests"));
    int32 GoldAfter = 0; for (ACireHero* H : Team) GoldAfter += H->Gold;
    Check(GoldAfter > GoldBefore, TEXT("opened chest pays the team"));
    const CI::LootTable* Minor = CireLoot::TableFor(Loot.PackCompletion, 1, 1), *Elite = CireLoot::TableFor(Loot.PackCompletion, 6, 1);
    Check(Minor && Elite && Minor->Id == "pack_minor" && Elite->Id == "pack_elite", TEXT("pack tier selects its loot table"));
    long long MinorGold = 0, EliteGold = 0;
    for (uint64 Seed = 1; Seed <= 400; ++Seed) { MinorGold += CI::RollLoot(*Minor, 1, 1, Seed, Loot.Scaling).Gold; EliteGold += CI::RollLoot(*Elite, 6, 1, Seed, Loot.Scaling).Gold; }
    Check(EliteGold > MinorGold * 4, TEXT("higher tiers pay much more"));

    // ---- personal loot: independent rolls, owner-only chests, eligibility, bots auto-loot, auto-collect summary
    for (TActorIterator<ACireLootDrop> It(Mode->GetWorld()); It; ++It) It->Destroy();
    for (ACireHero* H : Team) Mode->Heroes.Remove(H); // earlier fixture party must not count as nearby teammates
    Check(Loot.bPersonal && Loot.bBotsAutoLoot, TEXT("personal loot is the default distribution"));
    TArray<ACireHero*> Party;
    for (int32 Index = 0; Index < 5; ++Index) Party.Add(F.Hero(1, Index % 3, FVector(120, Index * 130 - 260, 0)));
    for (ACireHero* H : Party) { H->Gold = 0; H->Inventory->LootScore = 0; }
    Party[3]->bBot = true;                                                        // bot: auto-loots, no chest
    Party[4]->SetActorLocation(Mode->BasePosition(1) + FVector(9000, 0, 0));     // far away and never helped: ineligible
    Party[2]->SetActorLocation(Mode->BasePosition(1) + FVector(8000, 400, 0));   // far away but helped: eligible
    ACireMonster* Boss = F.Monster(1, Mode->BasePosition(1) + FVector(900, 0, 0));
    CireNPCCombat::ConfigureArchetype(Boss, CireNPCArchetypes::Get().PackLeader, 1, 4, 1);
    Boss->PackId = 4321;
    CireLoot::NoteContribution(Party[2], Boss);
    const TArray<ACireHero*> Eligible = CireLoot::EligibleFor(Mode, Boss, Boss->GetActorLocation());
    Check(Eligible.Num() == 4 && Eligible.Contains(Party[2]) && !Eligible.Contains(Party[4]), TEXT("eligible = contributed or alive within 40 m"));
    CireLoot::OnMonsterKilled(Mode, Boss, Party[0], true);
    TMap<ACireHero*, ACireLootDrop*> Chests;
    int32 ChestCount = 0;
    for (TActorIterator<ACireLootDrop> It(Mode->GetWorld()); It; ++It) { ++ChestCount; Chests.Add(It->OwnerHero, *It); }
    Check(ChestCount == 3 && Chests.Contains(Party[0]) && Chests.Contains(Party[1]) && Chests.Contains(Party[2]), TEXT("each eligible player gets their own chest"));
    Check(!Chests.Contains(Party[3]) && Party[3]->Gold > 0, TEXT("bots auto-loot their personal roll"));
    Check(!Chests.Contains(Party[4]) && Party[4]->Gold == 0, TEXT("ineligible players get nothing"));
    ACireLootDrop* Mine = Chests.FindRef(Party[0]);
    ACireLootDrop* Theirs = Chests.FindRef(Party[1]);
    if (!Mine || !Theirs) { Check(false, TEXT("personal chests")); return false; }
    Check(Mine->bOnlyRelevantToOwner && Mine->GetOwner() == Party[0] && Mine->IsNetRelevantFor(nullptr, Party[0], FVector::ZeroVector) &&
        !Mine->IsNetRelevantFor(nullptr, Party[1], FVector::ZeroVector), TEXT("a personal chest replicates only to its owner"));
    Check(!Mine->CanBeOpenedBy(Party[1]) && !Mine->Open(Party[1]) && !Mine->bOpened, TEXT("a teammate cannot open your chest"));
    bool bAllSame = true;
    for (const auto& Pair : Chests)
        bAllSame &= Pair.Value->Bundle.Gold == Mine->Bundle.Gold && Pair.Value->Bundle.Experience == Mine->Bundle.Experience && Pair.Value->Bundle.Items == Mine->Bundle.Items;
    Check(!bAllSame, TEXT("rolls are independent per player"));
    const int32 OwnGold = Party[0]->Gold;
    Check(Mine->Open(Party[0]) && Mine->bOpened && Party[0]->Gold > OwnGold && Mine->Manifest.Num() > 0, TEXT("the owner opens their chest"));
    TMap<TWeakObjectPtr<ACireHero>, FCireLootReport> Summaries;
    Check(CireLoot::CollectAll(Mode, &Summaries) == 2 && Summaries.Num() == 2, TEXT("prep auto-collects the two unopened chests"));
    const FCireLootReport* Summary = Summaries.Find(Party[1]);
    Check(Summary && Summary->bAutoCollected && Summary->Chests == 1 && Summary->Lines.Num() > 0 && Summary->Source.Contains(TEXT("Auto-collected")), TEXT("auto-collect sends a per-owner summary"));
    Cires::Items::LootBundle Tomes; Tomes.PrimaryTomes = {2};
    const int32 Strength = Party[0]->Strength;
    const FCireLootReport TomeReport = CireLoot::GrantPersonal(Party[0], Tomes, TEXT("Probe"), TEXT("why"), false);
    Check(TomeReport.Lines.Num() == 1 && TomeReport.Lines[0].Text.Contains(TEXT("+2 Strength")) && Party[0]->Strength == Strength + 2, TEXT("stat tomes state the exact stat gained"));
    const CI::LootTable* Major = Loot.Tables.Find(TEXT("pack_major"));
    const double SharedDrops = Major ? CI::ExpectedPersonalDrops(*Major, 3, 1, Loot.Scaling, 1) : 0;
    bool bSameTotals = Major != nullptr;
    for (int32 Players = 1; Players <= 5; ++Players)
        bSameTotals &= FMath::IsNearlyEqual(CI::ExpectedPersonalDrops(*Major, 3, 1, Loot.Scaling, CI::PersonalItemShare(Players, Loot.PersonalFactor)) * Players, SharedDrops, 1e-6);
    Check(bSameTotals, TEXT("team-wide expected tomes/items match one shared roll for 1-5 players"));

    // ---- teleport to base: channel, damage interrupt, completion cooldown, instant prep recall
    F.Survival();
    ACireHero* Traveler = F.Hero(0, 1, FVector(4000, 0, 0));
    UCireInventory* Inv = Traveler->Inventory;
    Inv->Teleport();
    Check(Inv->IsChanneling() && FMath::IsNearlyEqual(Inv->TeleportChannelEnd - Inv->TeleportChannelStart, 6.f, .01f), TEXT("teleport starts a 6 s channel"));
    CireItems::OnHeroDamaged(Traveler, M, TEXT("Monster attack"), 10.f);
    Check(!Inv->IsChanneling() && Inv->TeleportCooldownRemaining() <= 0, TEXT("damage interrupts without spending the cooldown"));
    Inv->Teleport();
    Traveler->SetActorLocation(Traveler->GetActorLocation() + FVector(200, 0, 0));
    Inv->TickComponent(.1f, LEVELTICK_All, nullptr);
    Check(!Inv->IsChanneling() && Inv->TeleportCooldownRemaining() <= 0, TEXT("moving interrupts the channel"));
    Inv->Teleport();
    Inv->CompleteTeleportNow();
    Check(!Inv->IsChanneling() && FVector::Dist2D(Traveler->GetActorLocation(), Mode->BasePosition(0)) < 500 && FMath::IsNearlyEqual(Inv->TeleportCooldownRemaining(), 120.f, 1.f), TEXT("completed teleport reaches town and starts the 2 min cooldown"));
    Traveler->SetActorLocation(Mode->BasePosition(0) + FVector(4000, 0, 0));
    Inv->Teleport();
    Check(!Inv->IsChanneling() && Traveler->Notice.Contains(TEXT("recharging")), TEXT("teleport on cooldown is refused"));
    F.Prep();
    Inv->Teleport();
    Check(FVector::Dist2D(Traveler->GetActorLocation(), Mode->BasePosition(0)) < 300, TEXT("prep recall is instant and ignores the cooldown"));
    Mode->Clock = Cires::MatchClock(); Mode->Clock.BeginIntermission(); Mode->Clock.Advance(Mode->Clock.GetDurations().Intermission); // arena
    Traveler->SetActorLocation(Mode->BasePosition(0) + FVector(4000, 0, 0));
    Inv->TeleportReadyAt = 0;
    Inv->Teleport();
    Check(!Inv->IsChanneling() && Traveler->Notice.Contains(TEXT("arena")), TEXT("teleport sealed during the arena"));

    UE_LOG(LogCireProgressionTests, Display, TEXT("CIRE_PROGRESSION_%s checks=%d"), Check.bPass ? TEXT("PASS") : TEXT("FAIL"), Check.Count);
    return bGood && Check.bPass;
}
#endif
