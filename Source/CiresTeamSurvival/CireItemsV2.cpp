// items-v2: path-defining uniques, group actives, absorb shields, stat buffs, dodge charges and the
// mana economy hooks. Declared in CireItems.h; rules in Rules/CireItemRules.*; see Docs/Items.md.
#include "CireItems.h"
#include "CireAbilityDB.h"
#include "CireBuffs.h"
#include "CireCombatEvents.h"
#include "CireConstruct.h"
#include "CireCrowdControl.h"
#include "CireGame.h"
#include "CireSummon.h"
#include "CireUltimateUpgrades.h"
#include "Engine/World.h"

using namespace Cires::Items;

namespace
{
ACireGameMode* ModeFor(const AActor* Actor)
{
    return Actor && Actor->GetWorld() ? Actor->GetWorld()->GetAuthGameMode<ACireGameMode>() : nullptr;
}

const Totals* TotalsFor(const AActor* Unit)
{
    const UCireInventory* Inventory = CireItems::InventoryOf(CireItems::OwningHero(Unit));
    return Inventory ? &Inventory->Totals() : nullptr;
}

float WorldNow(const AActor* Actor) { return Actor ? CireBuffs::ServerNow(Actor->GetWorld()) : 0.f; }
} // namespace

ACireHero* CireItems::OwningHero(const AActor* Unit)
{
    for (int32 Guard = 0; IsValid(Unit) && Guard < 4; ++Guard)
    {
        if (const auto* Summon = Cast<ACireSummon>(Unit)) { Unit = Summon->GetOwnerHero(); continue; }
        if (const auto* Hero = Cast<ACireHero>(Unit)) return const_cast<ACireHero*>(Hero);
        if (const auto* Construct = Cast<ACireConstruct>(Unit)) { Unit = Construct->GetSourceActor(); continue; }
        const AActor* Next = Unit->GetOwner() ? Unit->GetOwner() : Unit->GetInstigator();
        if (Next == Unit) break;
        Unit = Next;
    }
    return nullptr;
}

// ------------------------------------------------------------------ mana economy
float CireItems::BaseManaRegen(const ACireHero* Hero, float RegenMultiplier)
{
    if (!Hero) return 0.f;
    return static_cast<float>(ManaRegenPerSecond(Get().Mana, Hero->MaxMana, 0., RegenMultiplier));
}

float CireItems::ManaCostScale(const ACireHero* Hero)
{
    // Summons and monsters never pay mana through the champion cast path.
    return Hero ? static_cast<float>(Cires::Items::ManaCostScale(Get().Mana, Hero->Level)) : 1.f;
}

FString CireItems::NoteShortfall(const ACireHero* Hero, float NeedMana, float NeedEnergy)
{
    if (!Hero) return TEXT("Not enough mana or energy.");
    uint8 Kind = 0;
    float Need = 0;
    FString Text;
    if (NeedMana > 0 && Hero->Mana < NeedMana)
    { Kind = 1; Need = NeedMana; Text = FString::Printf(TEXT("Not enough mana (%.0f / %.0f)."), FMath::FloorToFloat(Hero->Mana), FMath::CeilToFloat(NeedMana)); }
    else if (NeedEnergy > 0 && Hero->Energy < NeedEnergy)
    { Kind = 2; Need = NeedEnergy; Text = FString::Printf(TEXT("Not enough energy (%.0f / %.0f)."), FMath::FloorToFloat(Hero->Energy), FMath::CeilToFloat(NeedEnergy)); }
    else Text = TEXT("Not enough mana or energy.");
    if (UCireInventory* Inventory = InventoryOf(Hero); Inventory && Hero->HasAuthority())
    {
        ++Inventory->ResourceFailSerial;
        Inventory->ResourceFailKind = Kind;
        Inventory->ResourceFailNeed = Need;
    }
    return Text;
}

void CireItems::OnAbilityCast(ACireHero* Hero, const FString& Id, float ManaSpent)
{
    UCireInventory* Inventory = InventoryOf(Hero);
    if (!Inventory || !Hero->HasAuthority()) return;
    const Totals& T = Inventory->Totals();
    if (T.ManaRefund > 0 && ManaSpent > 0)
        Hero->Mana = FMath::Min(Hero->MaxMana, Hero->Mana + ManaSpent * static_cast<float>(FMath::Min(T.ManaRefund, 90.) / 100.));
    if (T.UltimateUpgrade)
        if (const FCireAbilityDef* Def = CireAbilityDB::Find(Id); Def && Def->IsUltimate() && Def->Upgrade.bValid)
            CireUltimateUpgrades::Trigger(Hero, *Def);
}

// ------------------------------------------------------------------ path uniques
int32 CireItems::ConstructLimitBonus(const AActor* Source)
{
    const Totals* T = TotalsFor(Source);
    return T ? FMath::Clamp(T->ConstructLimitBonus, 0, 4) : 0;
}

float CireItems::ConstructHealthMultiplier(const AActor* Source)
{
    const Totals* T = TotalsFor(Source);
    return T ? 1.f + FMath::Clamp(static_cast<float>(T->ConstructHealth), 0.f, 200.f) / 100.f : 1.f;
}

float CireItems::ConstructShieldFraction(const AActor* Source)
{
    const Totals* T = TotalsFor(Source);
    return T ? FMath::Clamp(static_cast<float>(T->ConstructShield), 0.f, 100.f) / 100.f : 0.f;
}

float CireItems::SummonMultiplier(const AActor* OwnerOrSummon)
{
    const Totals* T = TotalsFor(OwnerOrSummon);
    return T ? 1.f + FMath::Clamp(static_cast<float>(T->SummonPower), 0.f, 200.f) / 100.f : 1.f;
}

float CireItems::AreaRadiusMultiplier(const AActor* Source)
{
    const Totals* T = TotalsFor(Source);
    return T ? 1.f + FMath::Clamp(static_cast<float>(T->AreaRadius), 0.f, 100.f) / 100.f : 1.f;
}

float CireItems::ControlDurationMultiplier(const AActor* Source)
{
    const Totals* T = TotalsFor(Source);
    return T ? 1.f + FMath::Clamp(static_cast<float>(T->ControlDuration), 0.f, 100.f) / 100.f : 1.f;
}

bool CireItems::IsAreaAbility(const FString& AbilityName)
{
    const FCireAbilityDef* Def = CireAbilityDB::FindByName(AbilityName);
    if (!Def) Def = CireAbilityDB::Find(AbilityName);
    return Def && (Def->Radius > 0 || Def->Void.bValid);
}

bool CireItems::IsControlled(const AActor* Unit)
{
    if (!IsValid(Unit)) return false;
    if (CireCrowdControl::IsStunned(Unit) || CireCrowdControl::IsSilenced(Unit)) return true;
    const float Now = WorldNow(Unit);
    if (const auto* Hero = Cast<ACireHero>(Unit)) return Hero->SlowUntil > Now;
    if (const auto* Monster = Cast<ACireMonster>(Unit)) return Monster->SlowUntil > Now;
    return false;
}

// ------------------------------------------------------------------ boots
int32 CireItems::MaxDodgeCharges(const ACireHero* Hero)
{
    const UCireInventory* Inventory = InventoryOf(Hero);
    return 1 + (Inventory ? FMath::Clamp(Inventory->Totals().DodgeCharges, 0, 3) : 0);
}

void CireItems::OnDodgeRoll(ACireHero* Hero)
{
    UCireInventory* Inventory = InventoryOf(Hero);
    if (!Inventory || !Hero->HasAuthority() || Inventory->Totals().RollHaste <= 0) return;
    for (const FCireItemSlot& Slot : Inventory->Equipment)
        if (const ItemDef* Item = Find(Slot.Id))
            for (const Passive& P : Item->Passives)
                if (P.Kind == PassiveKind::RollHaste)
                {
                    const float Now = WorldNow(Hero);
                    Inventory->Buffs.RemoveAll([&](const FCireTimedBuff& Buff) { return Buff.Id == Slot.Id || Buff.EndsAt <= Now; });
                    FCireTimedBuff Buff; Buff.Id = Slot.Id; Buff.Duration = static_cast<float>(P.Duration); Buff.EndsAt = Now + Buff.Duration;
                    Inventory->Buffs.Add(Buff);
                    Inventory->Invalidate();
                    return;
                }
}

// ------------------------------------------------------------------ shields and stat buffs
float CireItems::GrantBarrier(ACireHero* Hero, float Amount, float Duration, AActor* Source)
{
    UCireInventory* Inventory = InventoryOf(Hero);
    if (!Inventory || !Hero->HasAuthority() || Hero->bDead || !FMath::IsFinite(Amount) || Amount <= 0 || Duration <= 0) return 0.f;
    const float Now = WorldNow(Hero);
    if (Inventory->BarrierEndsAt <= Now) Inventory->BarrierHP = 0;
    // Shields never stack: the stronger one wins and the longer duration is kept.
    Inventory->BarrierHP = FMath::Min(FMath::Max(Inventory->BarrierHP, Amount), 50000.f);
    Inventory->BarrierMax = FMath::Max(Inventory->BarrierHP, Inventory->BarrierEndsAt > Now ? Inventory->BarrierMax : 0.f);
    Inventory->BarrierEndsAt = FMath::Max(Inventory->BarrierEndsAt, Now + Duration);
    CireBuffs::Apply(Hero, TEXT("party_barrier"), Inventory->BarrierEndsAt - Now, Source);
    Hero->ForceNetUpdate();
    return Inventory->BarrierHP;
}

bool CireItems::BuffStats(FName BuffId, StatBlock& Out)
{
    Out = StatBlock();
    if (const ItemDef* Item = Find(BuffId))
    {
        if (Item->Use.Kind != EffectKind::Elixir && Item->Use.Kind != EffectKind::PartyBuff) return false;
        Out = Item->Use.Buff;
        return true;
    }
    const FString Text = BuffId.ToString();
    if (!Text.StartsWith(TEXT("ult:"))) return false;
    const FCireAbilityDef* Def = CireAbilityDB::Find(Text.Mid(4));
    if (!Def || !Def->Upgrade.bValid) return false;
    for (const FCireUpgradeEffect& Effect : Def->Upgrade.Effects)
        if (Effect.Type == TEXT("partyBuff"))
            for (const auto& Pair : Effect.Stats)
            {
                ItemStat Stat;
                if (ParseStatKey(std::string(TCHAR_TO_UTF8(*Pair.Key)), Stat)) Out[Stat] += Pair.Value;
            }
    return !Out.IsZero();
}

void CireItems::GrantStatBuff(ACireHero* Hero, FName BuffId, float Duration, AActor* Source)
{
    UCireInventory* Inventory = InventoryOf(Hero);
    if (!Inventory || !Hero->HasAuthority() || Hero->bDead || Duration <= 0) return;
    StatBlock Probe;
    if (!BuffStats(BuffId, Probe)) return;
    const float Now = WorldNow(Hero);
    Inventory->Buffs.RemoveAll([&](const FCireTimedBuff& Buff) { return Buff.Id == BuffId || Buff.EndsAt <= Now; });
    FCireTimedBuff Buff; Buff.Id = BuffId; Buff.Duration = Duration; Buff.EndsAt = Now + Duration;
    Inventory->Buffs.Add(Buff);
    Inventory->Invalidate();
    if (Hero->bDrafted) Hero->Recalculate(false);
    if (BuffId.ToString().StartsWith(TEXT("ult:"))) CireBuffs::Apply(Hero, TEXT("apotheosis"), Duration, Source);
    else CireBuffs::Apply(Hero, TEXT("vigil_banner"), Duration, Source);
    Hero->ForceNetUpdate();
}

TArray<ACireHero*> CireItems::AlliesNear(const ACireHero* Source, FVector Center, float Radius)
{
    TArray<ACireHero*> Out;
    const ACireGameMode* Mode = ModeFor(Source);
    if (!Source || !Mode) return Out;
    if (Radius <= 0) { if (!Source->bDead) Out.Add(const_cast<ACireHero*>(Source)); return Out; }
    for (ACireHero* Ally : Mode->Heroes)
        if (IsValid(Ally) && !Ally->IsA<ACireSummon>() && !Ally->bDead && Ally->bDrafted && Ally->TeamId == Source->TeamId &&
            FVector::DistSquared2D(Center, Ally->GetActorLocation()) <= FMath::Square(Radius))
            Out.Add(Ally);
    return Out;
}

TArray<AActor*> CireItems::EnemiesNear(const ACireHero* Source, FVector Center, float Radius)
{
    TArray<AActor*> Out;
    const ACireGameMode* Mode = ModeFor(Source);
    if (!Source || !Mode || Radius <= 0) return Out;
    ACireHero* Self = const_cast<ACireHero*>(Source);
    for (ACireMonster* Monster : Mode->Monsters)
        if (IsValid(Monster) && CireCombat::IsAlive(Monster) && Self->IsHostile(Monster) &&
            FVector::DistSquared2D(Center, Monster->GetActorLocation()) <= FMath::Square(Radius))
            Out.Add(Monster);
    for (ACireHero* Enemy : Mode->Heroes)
        if (IsValid(Enemy) && !Enemy->bDead && Self->IsHostile(Enemy) &&
            FVector::DistSquared2D(Center, Enemy->GetActorLocation()) <= FMath::Square(Radius))
            Out.Add(Enemy);
    return Out;
}
