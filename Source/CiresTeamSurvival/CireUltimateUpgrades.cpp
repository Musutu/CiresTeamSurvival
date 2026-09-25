// items-v2: ultimate upgrades (see CireUltimateUpgrades.h, Tools/UltimateUpgrades.py, Docs/Items.md).
#include "CireUltimateUpgrades.h"
#include "CireAbilityDB.h"
#include "CireBuffs.h"
#include "CireCombatEvents.h"
#include "CireCrowdControl.h"
#include "CireGame.h"
#include "CireItems.h"
#include "Engine/World.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogCireUltimateUpgrades, Log, All);

float CireUltimateUpgrades::PrimaryValue(const ACireHero* Hero)
{
    if (!Hero) return 0.f;
    switch (Hero->PrimaryStat())
    {
    case Cires::PrimaryStat::Strength: return static_cast<float>(Hero->Strength);
    case Cires::PrimaryStat::Agility: return static_cast<float>(Hero->Agility);
    default: return static_cast<float>(Hero->Intelligence);
    }
}

FVector CireUltimateUpgrades::CenterFor(const ACireHero* Hero, const FCireAbilityDef& Def)
{
    if (!Hero) return FVector::ZeroVector;
    if (!Def.Upgrade.bAtTarget) return Hero->GetActorLocation();
    if (Hero->bHasCastAim && !Hero->CastAimPoint.ContainsNaN()) return Hero->CastAimPoint;
    if (IsValid(Hero->Target) && const_cast<ACireHero*>(Hero)->IsHostile(Hero->Target)) return Hero->Target->GetActorLocation();
    return Hero->GetActorLocation();
}

bool CireUltimateUpgrades::Trigger(ACireHero* Hero, const FCireAbilityDef& Def)
{
    if (!IsValid(Hero) || !Hero->HasAuthority() || !Def.Upgrade.bValid) return false;
    const FVector Center = CenterFor(Hero, Def);
    Hero->Notice = FString::Printf(TEXT("Apotheosis: %s"), *Def.Upgrade.Name);
    CireBuffs::Apply(Hero, TEXT("apotheosis"), 3.f + Def.Upgrade.Delay, Hero);
    if (Def.Upgrade.Delay <= 0.f || !Hero->GetWorld()) { Apply(Hero, Def, Center); return true; }
    const FString Id = Def.Id;
    TWeakObjectPtr<ACireHero> Weak(Hero);
    FTimerHandle Handle;
    Hero->GetWorld()->GetTimerManager().SetTimer(Handle, FTimerDelegate::CreateLambda([Weak, Id, Center]()
    {
        if (ACireHero* Caster = Weak.Get(); Caster && !Caster->bDead)
            if (const FCireAbilityDef* Found = CireAbilityDB::Find(Id)) Apply(Caster, *Found, Center);
    }), Def.Upgrade.Delay, false);
    return true;
}

int32 CireUltimateUpgrades::Apply(ACireHero* Hero, const FCireAbilityDef& Def, FVector Center)
{
    if (!IsValid(Hero) || !Hero->HasAuthority() || !Def.Upgrade.bValid) return 0;
    ACireGameMode* Mode = Hero->GetWorld() ? Hero->GetWorld()->GetAuthGameMode<ACireGameMode>() : nullptr;
    if (!Mode) return 0;
    const FString Label = Def.Upgrade.Name;
    const float Primary = PrimaryValue(Hero);
    int32 Touched = 0;
    for (const FCireUpgradeEffect& E : Def.Upgrade.Effects)
    {
        const FVector At = E.CenterOverride == 1 ? Hero->GetActorLocation() : E.CenterOverride == 2 ? CenterFor(Hero, Def) : Center;
        const float Amount = E.Amount + E.Scaling * Primary + E.PrimaryScaling * Primary + E.HealthScaling * Hero->MaxHealth;
        const FName Type = E.Type;
        if (Type == TEXT("partyBuff"))
            for (ACireHero* Ally : CireItems::AlliesNear(Hero, At, E.Radius)) { CireItems::GrantStatBuff(Ally, FName(*(TEXT("ult:") + Def.Id)), E.Duration, Hero); ++Touched; }
        else if (Type == TEXT("barrier"))
            for (ACireHero* Ally : CireItems::AlliesNear(Hero, At, E.Radius)) { if (CireItems::GrantBarrier(Ally, Amount, E.Duration, Hero) > 0) ++Touched; }
        else if (Type == TEXT("heal"))
            for (ACireHero* Ally : CireItems::AlliesNear(Hero, At, E.Radius))
            {
                if (Mode->IsCombatPhase()) CireCombat::ApplyHealing(Hero, Ally, Amount, Label);
                else Ally->Health = FMath::Min(Ally->MaxHealth, Ally->Health + Amount);
                ++Touched;
            }
        else if (Type == TEXT("restore"))
            for (ACireHero* Ally : CireItems::AlliesNear(Hero, At, E.Radius))
            {
                Ally->Mana = FMath::Min(Ally->MaxMana, Ally->Mana + Ally->MaxMana * E.Magnitude);
                Ally->Energy = FMath::Min(100.f, Ally->Energy + 100.f * E.Magnitude);
                ++Touched;
            }
        else if (Type == TEXT("cleanse"))
            for (ACireHero* Ally : CireItems::AlliesNear(Hero, At, E.Radius)) { Ally->SlowUntil = 0; ++Touched; }
        else if (Type == TEXT("cooldownRefund"))
        {
            for (int32 Index = 0; Index < Hero->Cooldowns.Num(); ++Index)
                if (!Hero->Skills.IsValidIndex(Index) || Hero->Skills[Index] != Def.Id) Hero->Cooldowns[Index] *= 1.f - FMath::Clamp(E.Magnitude, 0.f, 1.f);
            ++Touched;
        }
        else
            for (AActor* Enemy : CireItems::EnemiesNear(Hero, At, E.Radius))
            {
                float Applied = 0.f;
                if (Type == TEXT("stun")) Applied = CireCrowdControl::Stun(Enemy, E.Duration, Hero);
                else if (Type == TEXT("silence")) Applied = CireCrowdControl::Silence(Enemy, E.Duration, Hero);
                else if (Type == TEXT("slow")) Applied = CireCrowdControl::Slow(Enemy, E.Duration, Hero);
                else if (Type == TEXT("armorBreak")) { CireCrowdControl::ArmorBreak(Enemy, E.Duration, Hero); Applied = E.Duration; }
                else if (Type == TEXT("damage")) Applied = CireCombat::ApplyDamage(Hero, Enemy, Amount, Label);
                if (Applied > 0) ++Touched;
            }
        if (E.Radius > 0 && Type != TEXT("cooldownRefund"))
            CireCombat::PlayCue(Hero, Hero, FName(*Def.Id), Hero->GetActorLocation(), At, ECireSpellCue::Impact, FMath::Clamp(E.Radius / 550.f, .4f, 2.f), false);
    }
    UE_LOG(LogCireUltimateUpgrades, Log, TEXT("CIRE_ULT_UPGRADE %s %s touched=%d"), *Def.Id, *Label, Touched);
    return Touched;
}
