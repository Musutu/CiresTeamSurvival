// items-v2: in-engine checks for the reworked catalog (primary-stat items, path-defining uniques,
// group actives, unique boots), dodge-roll charges, ultimate upgrades and the mana economy.
// Run from CireProgression::RunSmoke (Tools/RunProgressionChecks.py, RunExpansionChecks --only native).
#include "CireItems.h"
#include "CireScalingKits.h" // scaling-kits
#include "CireAbilityDB.h"
#include "CireBuffs.h"
#include "CireCrowdControl.h"
#include "CireGame.h"
#include "CireMobility.h"
#include "CireSkillShop.h"
#include "CireUltimateUpgrades.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Engine/World.h"

#if !UE_BUILD_SHIPPING
DEFINE_LOG_CATEGORY_STATIC(LogCireItemsV2Tests, Log, All);
namespace CI = Cires::Items;

namespace
{
struct FV2Check
{
    int32 Count = 0;
    bool bPass = true;
    void operator()(bool bValue, const FString& Label)
    {
        ++Count;
        if (!bValue) { bPass = false; UE_LOG(LogCireItemsV2Tests, Error, TEXT("CIRE_ITEMS_V2_CHECK_FAIL %s"), *Label); }
    }
};

struct FV2Fixture
{
    ACireGameMode* Mode;
    Cires::MatchClock Clock;
    TArray<ACireHero*> Heroes;
    TArray<ACireMonster*> Monsters;
    TArray<AActor*> Spawned;
    explicit FV2Fixture(ACireGameMode* InMode) : Mode(InMode), Clock(InMode->Clock), Heroes(InMode->Heroes), Monsters(InMode->Monsters)
    {
        Mode->Clock = Cires::MatchClock();   // survival: combat phase
        Mode->Heroes.Reset();
        Mode->Monsters.Reset();
    }
    ~FV2Fixture()
    {
        for (AActor* Actor : Spawned) if (IsValid(Actor)) Actor->Destroy();
        Mode->Clock = Clock; Mode->Heroes = Heroes; Mode->Monsters = Monsters;
    }
    ACireHero* Hero(int32 Archetype, FVector Offset)
    {
        FActorSpawnParameters Params;
        Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        auto* H = Mode->GetWorld()->SpawnActor<ACireHero>(Mode->BasePosition(0) + FVector(2600, 0, 0) + Offset, FRotator::ZeroRotator, Params);
        if (!H) return nullptr;
        Spawned.Add(H);
        H->SetActorTickEnabled(false);
        if (H->Inventory) H->Inventory->SetComponentTickEnabled(false);
        H->TeamId = 0;
        H->Draft(Archetype);
        H->Offers.Reset();
        H->CriticalChance = 0;
        Mode->Heroes.Add(H);
        return H;
    }
    ACireMonster* Monster(FVector Location)
    {
        FActorSpawnParameters Params;
        Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        auto* M = Mode->GetWorld()->SpawnActor<ACireMonster>(Location, FRotator::ZeroRotator, Params);
        if (!M) return nullptr;
        Spawned.Add(M);
        M->SetActorTickEnabled(false);
        M->Lane = 0;
        M->Health = M->MaxHealth = 50000;
        Mode->Monsters.Add(M);
        return M;
    }
};

void Give(ACireHero* Hero, std::initializer_list<const TCHAR*> Ids)
{
    for (auto& Cell : Hero->Inventory->Equipment) Cell = FCireItemSlot();
    int32 Index = 0;
    for (const TCHAR* Id : Ids) Hero->Inventory->Equipment[Index++].Id = FName(Id);
    Hero->Inventory->Invalidate();
    Hero->Recalculate(false);
}

int32 SlotOf(const ACireHero* Hero, const TCHAR* Id)
{
    return Hero->Inventory->Equipment.IndexOfByPredicate([Id](const FCireItemSlot& S) { return S.Id == FName(Id); });
}

bool HasBuff(const ACireHero* Hero, const FString& Id)
{
    return Hero->Inventory->Buffs.ContainsByPredicate([&](const FCireTimedBuff& B) { return B.Id == FName(*Id); });
}
} // namespace

bool CireItems::RunV2Smoke(ACireGameMode* Mode)
{
    if (!Mode || !Mode->HasAuthority()) return false;
    FV2Check Check;
    FV2Fixture F(Mode);
    const auto& D = Get();
    FString Message;

    // ---------------- catalog shape (Eric: 45-60 items, a unique per build path, one-line effects)
    Check(D.bValid && D.Order.Num() >= 45 && D.Order.Num() <= 60, FString::Printf(TEXT("catalog has 45-60 items (%d)"), D.Order.Num()));
    Check(CI::ValidateStatPolicy(D.Catalog).empty(), TEXT("items grant the primary stat and flat stats only"));
    TMap<int32, int32> PathKinds;
    int32 PathItems = 0;
    TSet<int32> GroupUses;
    bool bDodgeBoots = false, bSpeedBoots = false;
    for (const auto& Item : D.Catalog.Items)
    {
        const FName Id(UTF8_TO_TCHAR(Item.Id.c_str()));
        Check(D.EffectLine.Contains(Id) && !D.EffectLine[Id].IsEmpty(), TEXT("one-line effect: ") + Id.ToString());
        if (Item.UniqueGroup == "path")
        {
            ++PathItems;
            Check(Item.Unique && Item.Tier == CI::ItemTier::Legendary, TEXT("path uniques are unique legendaries"));
            for (const auto& P : Item.Passives) PathKinds.FindOrAdd(static_cast<int32>(P.Kind))++;
        }
        if (Item.UniqueGroup == "boots")
            for (const auto& P : Item.Passives)
            { bDodgeBoots |= P.Kind == CI::PassiveKind::DodgeCharges; bSpeedBoots |= P.Kind == CI::PassiveKind::RollHaste && Item.Stats.Get(CI::ItemStat::MoveSpeed) >= 18; }
        if (Item.Use.Kind == CI::EffectKind::PartyBarrier && Item.Use.Radius > 0) GroupUses.Add(0);
        if (Item.Use.Kind == CI::EffectKind::PartyBuff && FMath::IsNearlyEqual(Item.Use.Buff.Get(CI::ItemStat::Armor), 250.) && FMath::IsNearlyEqual(Item.Use.Duration, 10.)) GroupUses.Add(1);
        if (Item.Use.Kind == CI::EffectKind::HealTarget && FMath::IsNearlyEqual(Item.Use.Amount, 1000.)) GroupUses.Add(2);
        if (Item.Use.Kind == CI::EffectKind::HealAllies && FMath::IsNearlyEqual(Item.Use.Amount, 200.) && Item.Use.Radius <= 500) GroupUses.Add(3);
    }
    Check(PathItems == 6, TEXT("six path-defining uniques"));
    for (CI::PassiveKind Kind : {CI::PassiveKind::ConstructLimit, CI::PassiveKind::SummonPower, CI::PassiveKind::AreaAmp, CI::PassiveKind::ControlAmp,
                                 CI::PassiveKind::UltimateUpgrade, CI::PassiveKind::AttackSplash})
        Check(PathKinds.FindRef(static_cast<int32>(Kind)) == 1, FString::Printf(TEXT("exactly one path unique of kind %d"), static_cast<int32>(Kind)));
    Check(GroupUses.Num() == 4, TEXT("party shield, +250 armor aura, 1000 target heal and 200 area heal items exist"));
    Check(bDodgeBoots && bSpeedBoots, TEXT("double-dash boots and speed boots exist"));
    for (const TCHAR* Role : {TEXT("tank"), TEXT("physical"), TEXT("caster"), TEXT("support"), TEXT("summoner"), TEXT("constructor")})
    {
        const auto* Lists = D.Recommended.Find(Role);
        Check(Lists && Lists->Num() == 3 && (*Lists)[1].Num() == 6, FString::Printf(TEXT("%s has a six-item core build"), Role));
        if (!Lists) continue;
        std::vector<std::string> Core;
        for (const FName& Id : (*Lists)[1]) Core.push_back(TCHAR_TO_UTF8(*Id.ToString()));
        Check(CI::ValidateBuild(D.Catalog, Core).empty(), FString::Printf(TEXT("%s core build is carryable"), Role));
        for (const auto& Group : *Lists) for (const FName& Id : Group) Check(Find(Id) && Find(Id)->Purchasable, TEXT("recommended item is sold: ") + Id.ToString());
    }
    FCireItemData Scratch;
    FString BadJson;
    Check(!ParseJson(TEXT("{\"schemaVersion\":1,\"items\":[{\"id\":\"a\",\"name\":\"A\",\"tier\":\"basic\",\"cost\":5,\"stats\":{\"attackDamage\":3}}]}"), Scratch, BadJson) && BadJson.Contains(TEXT("attackDamage")),
        TEXT("attack damage on an item is rejected"));

    // ---------------- adaptive primary stat
    ACireHero* Tank = F.Hero(0, FVector(0, 0, 0));
    ACireHero* Mage = F.Hero(2, FVector(0, 250, 0));
    ACireHero* Far = F.Hero(1, FVector(0, 2400, 0));
    ACireMonster* Mob = F.Monster(Tank->GetActorLocation() + FVector(300, 0, 0));
    ACireMonster* Mob2 = F.Monster(Tank->GetActorLocation() + FVector(420, 120, 0));
    if (!Tank || !Mage || !Far || !Mob || !Mob2 || !Tank->Mobility) { Check(false, TEXT("fixture")); return false; }
    auto Primary = [](const ACireHero* H) { return CireUltimateUpgrades::PrimaryValue(H); };
    const float TankBefore = Primary(Tank), MageBefore = Primary(Mage);
    const int32 TankInt = Mage->Intelligence;
    Give(Tank, {TEXT("rusted_longsword")});
    Give(Mage, {TEXT("rusted_longsword")});
    Check(FMath::IsNearlyEqual(Primary(Tank), TankBefore + 5) && Tank->PrimaryStat() == Cires::PrimaryStat::Strength, TEXT("+5 primary stat becomes STR for a strength champion"));
    Check(FMath::IsNearlyEqual(Primary(Mage), MageBefore + 5) && Mage->Intelligence == TankInt + 5 && Mage->PrimaryStat() == Cires::PrimaryStat::Intelligence, TEXT("+5 primary stat becomes INT for an intelligence champion"));
    Give(Tank, {TEXT("gravewarden_bulwark"), TEXT("stoneheart")});
    const float Armor = static_cast<float>(Tank->Inventory->Totals().Stats.Get(CI::ItemStat::Armor)) * CireKits::DefenseMultiplier(Tank); // scaling-kits: shield tanks -10% armour
    const float Expected = static_cast<float>(CI::ApplyItemMitigation(100. * (1. - CI::Mitigation(Armor)), 6, 12));
    Check(FMath::IsNearlyEqual(ModifyIncomingDamage(Tank, Mob, TEXT("Monster attack"), 100.f), Expected, .05f), TEXT("completed-item mitigation: 6% reduction and a 12 block per hit"));

    // ---------------- uniques: one path-defining unique, one boots, loot respects the groups
    Give(Tank, {});
    Tank->Gold = 20000;
    Mode->Clock.BeginIntermission();   // prep: the shop is open anywhere
    Check(Tank->Inventory->Buy(FName(TEXT("heart_of_cataclysm")), Message), TEXT("buy a path unique"));
    Message.Reset();
    Check(!Tank->Inventory->Buy(FName(TEXT("sigil_of_apotheosis")), Message) && Message.Contains(TEXT("path-defining")), TEXT("a second path unique is refused"));
    Check(!Tank->Inventory->HasRoomFor(FName(TEXT("stormhowl_ravager"))), TEXT("loot never grants a second path unique"));
    Check(Tank->Inventory->Buy(FName(TEXT("twinstep_treads")), Message) && !Tank->Inventory->Buy(FName(TEXT("windrunner_boots")), Message), TEXT("one pair of boots"));
    Check(Tank->Inventory->Buy(FName(TEXT("aegis_of_the_last_oath")), Message) && !Tank->Inventory->Buy(FName(TEXT("aegis_of_the_last_oath")), Message), TEXT("group actives are unique"));
    const int32 HeartSlot = SlotOf(Tank, TEXT("heart_of_cataclysm"));
    Check(HeartSlot != INDEX_NONE && Tank->Inventory->SellSlot(HeartSlot, false, Message) && Tank->Inventory->Buy(FName(TEXT("sigil_of_apotheosis")), Message), TEXT("selling the path unique frees the choice"));
    Tank->Inventory->EndShopVisit();
    Mode->Clock = Cires::MatchClock();

    // ---------------- group on-use actives
    ACireHero* Ally = Mage;
    Give(Tank, {TEXT("aegis_of_the_last_oath"), TEXT("banner_of_the_vigil"), TEXT("lifebinders_reliquary"), TEXT("chalice_of_mercy")});
    Give(Ally, {});
    Give(Far, {});
    Check(Tank->Inventory->UseSlot(SlotOf(Tank, TEXT("aegis_of_the_last_oath")), false, Message), TEXT("party shield used"));
    const float Shield = 180.f + 2.f * Primary(Tank);
    Check(FMath::IsNearlyEqual(Ally->Inventory->BarrierHP, Shield, 1.f) && FMath::IsNearlyEqual(Tank->Inventory->BarrierHP, Shield, 1.f) && Far->Inventory->BarrierHP <= 0,
        TEXT("party shield covers allies in the radius only"));
    const float Through = ModifyIncomingDamage(Ally, Mob, TEXT("Shadow Bolt"), 50.f);
    Check(FMath::IsNearlyEqual(Through, 0.f) && Ally->Inventory->BarrierHP < Shield, TEXT("the shield absorbs damage first"));
    Check(Tank->Inventory->UseSlot(SlotOf(Tank, TEXT("banner_of_the_vigil")), false, Message), TEXT("armor banner used"));
    Check(Ally->Inventory->Totals().Stats.Get(CI::ItemStat::Armor) >= 250 && HasBuff(Ally, TEXT("banner_of_the_vigil")) && !HasBuff(Far, TEXT("banner_of_the_vigil")),
        TEXT("allies in range gain +250 armor"));
    const float Armored = ModifyIncomingDamage(Ally, Mob, TEXT("Monster attack"), 100.f);
    Check(Armored < 100.f * (1.f - 250.f / 350.f) + .5f, TEXT("+250 armor mitigates basic attacks"));
    Ally->Inventory->Buffs.RemoveAll([](const FCireTimedBuff& B) { return B.Id == FName(TEXT("banner_of_the_vigil")); });
    Ally->Inventory->Invalidate();
    Ally->Health = 100; Ally->MaxHealth = 5000; Tank->Target = Ally;
    Check(Tank->Inventory->UseSlot(SlotOf(Tank, TEXT("lifebinders_reliquary")), false, Message) && Ally->Health >= 1100.f - 1.f, TEXT("target heal restores at least 1,000"));
    Ally->Recalculate(false); Tank->Recalculate(false);
    Ally->Health = FMath::Max(1.f, Ally->MaxHealth - 300.f); Tank->Health = FMath::Max(1.f, Tank->MaxHealth - 300.f); Far->Health = 100;
    const float AllyBefore = Ally->Health, TankHealBefore = Tank->Health;
    const bool bChalice = Tank->Inventory->UseSlot(SlotOf(Tank, TEXT("chalice_of_mercy")), false, Message);
    const FString ChaliceLabel = FString::Printf(TEXT("area heal restores 200 to allies in the small radius (used %d ally %.0f tank %.0f far %.0f: %s)"), bChalice, Ally->Health, Tank->Health, Far->Health, *Message);
    Check(bChalice && Ally->Health >= AllyBefore + 199.f && Tank->Health >= TankHealBefore + 199.f && FMath::IsNearlyEqual(Far->Health, 100.f), ChaliceLabel);

    // ---------------- dodge-roll charges (double dash)
    auto Roll = [&](ACireHero* H) { const bool bOk = H->Mobility->StartRoll(FVector::ForwardVector); H->Mobility->CancelRoll(); return bOk; };
    for (ACireHero* H : {Tank, Far})
    {
        auto* Move = H->GetCharacterMovement();
        Move->bRunPhysicsWithNoController = true; Move->SetMovementMode(MOVE_Walking);
        H->Energy = 100; H->Mobility->RollCharges = 1; H->Mobility->ReadyAt = 0; H->GlobalCooldown = 0;
    }
    Give(Far, {});
    Check(Roll(Far) && !Roll(Far), TEXT("one dodge charge without the boots"));
    Give(Tank, {TEXT("twinstep_treads")});
    Tank->Energy = 100;
    Check(CireItems::MaxDodgeCharges(Tank) == 2 && Roll(Tank) && Roll(Tank) && !Roll(Tank), TEXT("Galeborn Twinstep: two rolls back to back, then recovering"));
    Check(Tank->Mobility->AvailableCharges() == 0 && Tank->Mobility->NextChargeIn() > 0, TEXT("charges refill one at a time"));
    Give(Tank, {TEXT("windrunner_boots")});
    Tank->Energy = 100; Tank->Mobility->RollCharges = 1; Tank->Mobility->ReadyAt = 0;
    const float Before = MoveSpeedMultiplier(Tank);
    Check(Roll(Tank) && MoveSpeedMultiplier(Tank) > Before + .29f, TEXT("Windrunner Boots: a roll grants +30% move speed"));

    // ---------------- ultimate upgrades: every ultimate fires its added effect
    Give(Tank, {TEXT("sigil_of_apotheosis")});
    Check(TotalsOf(Tank).UltimateUpgrade, TEXT("Sigil of Apotheosis enables ultimate upgrades"));
    int32 Upgrades = 0;
    for (const FCireAbilityDef& Def : CireAbilityDB::All())
    {
        if (!Def.IsUltimate()) continue;
        Check(Def.Upgrade.bValid, TEXT("ultimate has an upgrade: ") + Def.Id);
        if (!Def.Upgrade.bValid) continue;
        ++Upgrades;
        Tank->Inventory->Buffs.Reset(); Tank->Inventory->BarrierHP = 0; Ally->Inventory->Buffs.Reset(); Ally->Inventory->BarrierHP = 0;
        Tank->Inventory->Invalidate(); Ally->Inventory->Invalidate();
        Tank->Health = Ally->Health = 100; Tank->Mana = Ally->Mana = 0; Tank->Energy = Ally->Energy = 0; Ally->SlowUntil = 1.e6f;
        Tank->Skills = {TEXT("iron_guard"), Def.Id}; Tank->Cooldowns = {10.f, 80.f};
        Tank->Target = Mob; Tank->bHasCastAim = false;
        for (ACireMonster* M : {Mob, Mob2}) { M->SlowUntil = 0; CireBuffs::ClearAll(M); }
        const int32 Touched = CireUltimateUpgrades::Apply(Tank, Def, CireUltimateUpgrades::CenterFor(Tank, Def));
        bool bEvidence = Touched > 0;
        for (const FCireUpgradeEffect& E : Def.Upgrade.Effects)
        {
            const FString T = E.Type.ToString();
            if (T == TEXT("partyBuff")) bEvidence &= HasBuff(Tank, TEXT("ult:") + Def.Id);
            else if (T == TEXT("barrier")) bEvidence &= Tank->Inventory->BarrierHP > 0;
            else if (T == TEXT("heal")) bEvidence &= Tank->Health > 100.f;
            else if (T == TEXT("restore")) bEvidence &= Tank->Mana > 0.f;
            else if (T == TEXT("cleanse")) bEvidence &= Ally->SlowUntil <= 0.f;
            else if (T == TEXT("stun")) bEvidence &= CireCrowdControl::IsStunned(Mob);
            else if (T == TEXT("silence")) bEvidence &= CireCrowdControl::IsSilenced(Mob);
            else if (T == TEXT("slow")) bEvidence &= Mob->SlowUntil > 0.f;
            else if (T == TEXT("armorBreak")) bEvidence &= CireCrowdControl::ArmorMultiplier(Mob) < 1.f;
            else if (T == TEXT("cooldownRefund")) bEvidence &= Tank->Cooldowns[0] < 10.f && FMath::IsNearlyEqual(Tank->Cooldowns[1], 80.f);
        }
        Check(bEvidence, TEXT("ultimate upgrade applies its added effect: ") + Def.Id);
    }
    Check(Upgrades >= 16, TEXT("every ultimate carries an upgrade"));
    // Real cast path: Bastion of Dawn with the Sigil also shields nearby allies (Dawnward).
    Tank->Inventory->Buffs.Reset(); Tank->Inventory->BarrierHP = 0; Ally->Inventory->BarrierHP = 0;
    Tank->Skills = {TEXT("bastion_of_dawn")}; Tank->Cooldowns = {0.f}; Tank->GlobalCooldown = 0; Tank->Energy = 100; Tank->Mana = Tank->MaxMana;
    Tank->Cast(0);
    Check(Tank->Cooldowns[0] > 0 && Ally->Inventory->BarrierHP > 0 && CireBuffs::IsActive(Tank, TEXT("apotheosis")), TEXT("casting an ultimate triggers its upgrade (Dawnward)"));
    Give(Tank, {});
    Ally->Inventory->BarrierHP = 0;
    Tank->Cooldowns = {0.f}; Tank->GlobalCooldown = 0; Tank->Energy = 100;
    Tank->Cast(0);
    Check(Ally->Inventory->BarrierHP <= 0, TEXT("no upgrade without the Sigil"));

    // ---------------- mana economy: spam runs dry, regen matters, energy stays flat
    ACireHero* Caster = Mage;
    Give(Caster, {});
    for (int32 Guard = 0; Caster->Level < 10 && Guard < 200; ++Guard) Caster->GrantExperience(250);
    Caster->Offers.Reset();
    Caster->Recalculate(true);
    const float Regen = BaseManaRegen(Caster);
    Check(FMath::IsNearlyEqual(Regen, 2.f + .008f * Caster->MaxMana, .01f), TEXT("mana regen is 2 + 0.8% of max mana per second"));
    const float Scale = ManaCostScale(Caster), Expect = 1.f + .05f * (Caster->Level - 1);
    Check(Caster->Level >= 10 && FMath::IsNearlyEqual(Scale, Expect, .001f), FString::Printf(TEXT("mana costs grow 5%% per level (level %d, x%.3f)"), Caster->Level, Scale));
    float CostMana = 0, CostEnergy = 0;
    CireSkillShop::ScaledCost(Caster, TEXT("cleaving_strike"), 0, 30, CostMana, CostEnergy);
    Check(FMath::IsNearlyEqual(CostEnergy, 30.f), TEXT("energy costs stay flat (snappy energy classes)"));
    // Restoring Light: a plain 45-mana cast through the champion cast path (no placement rules).
    const float LightCost = 45.f * Scale;
    auto CastLight = [&]() { Caster->Skills = {TEXT("restoring_light")}; Caster->Cooldowns = {0.f}; Caster->GlobalCooldown = 0; Caster->Target = nullptr; Caster->Cast(0); CireCrowdControl::CompleteCastNow(Caster); };
    auto SecondsToDry = [&](float ItemRegen)
    {
        Caster->Mana = Caster->MaxMana;
        const int32 Serial = Caster->Inventory->ResourceFailSerial;
        for (float T = 0; T < 180.f; T += 2.f)   // spam: a 45-mana spell every 2 s (a full rotation of several skills)
        {
            Caster->Mana = FMath::Min(Caster->MaxMana, Caster->Mana + 2.f * (BaseManaRegen(Caster) + ItemRegen));
            CastLight();
            if (Caster->Inventory->ResourceFailSerial != Serial) return T;
        }
        return 180.f;
    };
    Caster->Mana = Caster->MaxMana;
    const float Full = Caster->Mana;
    CastLight();
    const float Spent = Full - Caster->Mana;
    Check(FMath::IsNearlyEqual(Spent, LightCost, .5f), FString::Printf(TEXT("a cast pays the level-scaled cost (%.1f of %.1f; %s)"), Spent, LightCost, *Caster->Notice));
    const float Dry = SecondsToDry(0.f);
    const FString Notice = Caster->Notice;
    const uint8 Kind = Caster->Inventory->ResourceFailKind;
    Check(Dry < 90.f, FString::Printf(TEXT("spamming runs a caster dry (%.0f s)"), Dry));
    Check(Notice.StartsWith(TEXT("Not enough mana (")) && Kind == 1, FString::Printf(TEXT("clear 'Not enough mana (x / y)' feedback and HUD flash (%s, kind %d)"), *Notice, Kind));
    const float DryWithItems = SecondsToDry(8.f);
    Check(DryWithItems > Dry * 1.3f, FString::Printf(TEXT("mana regen items matter (%.0f s -> %.0f s)"), Dry, DryWithItems));
    Give(Caster, {TEXT("moonwell_codex")});
    Caster->Mana = Caster->MaxMana;
    const float Pool = Caster->Mana;
    CastLight();
    const float Refunded = Pool - Caster->Mana;
    Check(FMath::IsNearlyEqual(Refunded, LightCost * .8f, .5f), FString::Printf(TEXT("Moonwell Codex refunds 20%% of the mana cost (spent %.1f of %.1f)"), Refunded, LightCost));

    UE_LOG(LogCireItemsV2Tests, Display, TEXT("CIRE_ITEMS_V2_%s checks=%d"), Check.bPass ? TEXT("PASS") : TEXT("FAIL"), Check.Count);
    return Check.bPass;
}
#endif
