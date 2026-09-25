#include "CireCombatEvents.h"
#include "CireSignatureSkills.h" // new-champions
#include "CireRollSkills.h" // champion-draft: dodge-roll skills
#include "CireCrowdControl.h" // champion-draft: crowd control, timed casts, execute skills
#include "CireClassTraits.h"
#include "CireItems.h" // progression-shop
#include "CireGame.h"
#include "CireThreat.h"
#include "CireConstruct.h"
#include "CireSummon.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "Misc/ScopeExit.h"
#include "CireScalingKits.h" // scaling-kits

DEFINE_LOG_CATEGORY_STATIC(LogCireCombat, Log, All);

namespace
{
int32 HeroTeam(const AActor* Actor)
{
    if (const auto* Hero = Cast<ACireHero>(Actor)) return Hero->TeamId;
    if (const auto* Monster = Cast<ACireMonster>(Actor)) return Monster->Lane;
    if (const auto* Construct = Cast<ACireConstruct>(Actor)) return Construct->OriginTeam;
    return INDEX_NONE;
}

FString CombatName(const AActor* Actor)
{
    if (const auto* Hero = Cast<ACireHero>(Actor)) return Hero->HeroName;
    if (const auto* Monster = Cast<ACireMonster>(Actor)) return Monster->MonsterName;
    if (const auto* Construct = Cast<ACireConstruct>(Actor)) return Construct->GetDisplayName();
    return TEXT("Unknown");
}

FCireCombatEvent MakeEvent(AActor* Source, AActor* Target, float Amount, const FString& AbilityName, bool bHealing)
{
    FCireCombatEvent Event;
    Event.Source = Source;
    Event.Target = Target;
    Event.SourceName = CombatName(Source);
    Event.TargetName = CombatName(Target);
    Event.AbilityName = AbilityName;
    Event.SourceId = Source->GetFName();
    Event.TargetId = Target->GetFName();
    Event.Amount = Amount;
    Event.bHealing = bHealing;
    if (const auto* Character = Cast<ACharacter>(Target))
        Event.Location = Target->GetActorLocation() + FVector(0, 0, Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 20);
    else
    {
        FVector Origin, Extent;
        Target->GetActorBounds(false, Origin, Extent);
        Event.Location = Origin + FVector(0, 0, Extent.Z + 20);
    }
    Event.SourceTeam = HeroTeam(Source);
    Event.TargetTeam = HeroTeam(Target);
    Event.ServerTime = Source->GetWorld()->GetTimeSeconds();
    return Event;
}

bool CanObserve(const FCireCombatEvent& Event, int32 ObserverTeam, Cires::MatchPhase Phase)
{
    if (ObserverTeam < 0 || ObserverTeam > 1) return false;
    if (Phase == Cires::MatchPhase::Arena)
        return Event.SourceTeam != INDEX_NONE && Event.TargetTeam != INDEX_NONE;
    // Do not disclose opposing PvE engagements, including actor references/positions.
    return Event.SourceTeam == ObserverTeam || Event.TargetTeam == ObserverTeam;
}

bool ForRecipient(const FCireCombatEvent& Event, const ACireHero* Observer, Cires::MatchPhase Phase, FCireCombatEvent& Result)
{
    if (!IsValid(Observer) || !Observer->bDrafted || !CanObserve(Event, Observer->TeamId, Phase)) return false;
    Result = Event;
    const auto* Summon=Cast<ACireSummon>(Event.Source);
    Result.bLocalSource = Event.Source == Observer || (Summon && Summon->GetOwnerHero()==Observer);
    Result.bLocalTarget = Event.Target == Observer;
    // Even a living target may be destroyed before this RPC reaches its client.
    // Stable identity and the impact snapshot are sufficient for both SCT views.
    Result.Source = nullptr;
    Result.Target = nullptr;
    return true;
}

void Broadcast(const FCireCombatEvent& Event, UWorld* World)
{
    auto* Mode = World ? World->GetAuthGameMode<ACireGameMode>() : nullptr;
    if (!Mode) return;
    for (auto It = World->GetPlayerControllerIterator(); It; ++It)
    {
        auto* Controller = Cast<ACireController>(It->Get());
        const auto* Observer = Controller ? Cast<ACireHero>(Controller->GetPawn()) : nullptr;
        FCireCombatEvent RecipientEvent;
        if (ForRecipient(Event, Observer, Mode->Clock.Phase(), RecipientEvent))
            Controller->ClientCombatEvent(RecipientEvent);
    }
}
} // namespace

void CireCombat::AppendReceivedEvent(TArray<FCireCombatEvent>& Buffer, uint32& Sequence, const FCireCombatEvent& Event, float Now)
{
    if (!FMath::IsFinite(Now) || !FMath::IsFinite(Event.Amount) || (Event.Amount <= 0 && Event.Outcome == ECireHitOutcome::Hit) || Event.Amount < 0 || Event.Location.ContainsNaN()) return;
    constexpr int32 MaxEvents = 256;
    constexpr float RetentionSeconds = 8.f;
    Buffer.RemoveAll([Now, RetentionSeconds](const FCireCombatEvent& Previous) { return Now - Previous.TimeSeconds > RetentionSeconds || Previous.TimeSeconds > Now; });
    const bool bPersonal = Event.bLocalSource || Event.bLocalTarget;
    while (Buffer.Num() >= MaxEvents)
    {
        const int32 TeamEvent = Buffer.IndexOfByPredicate([](const FCireCombatEvent& Previous) { return !Previous.bLocalSource && !Previous.bLocalTarget; });
        if (TeamEvent != INDEX_NONE) Buffer.RemoveAt(TeamEvent);
        else if (bPersonal) Buffer.RemoveAt(0);
        else return; // Team traffic cannot evict a buffer full of personal hits.
    }
    FCireCombatEvent Entry = Event;
    Entry.Source = nullptr;
    Entry.Target = nullptr;
    Entry.TimeSeconds = Now;
    if (++Sequence == 0) ++Sequence;
    Entry.Sequence = Sequence;
    Buffer.Add(MoveTemp(Entry));
}

int32 CireCombat::TeamOf(const AActor* Actor){return HeroTeam(Actor);}
bool CireCombat::IsAlive(const AActor* Actor){
    if(!IsValid(Actor)||Actor->IsActorBeingDestroyed())return false;
    if(const auto* H=Cast<ACireHero>(Actor))return H->bDrafted&&!H->bDead&&H->Health>0;
    if(const auto* M=Cast<ACireMonster>(Actor))return M->Health>0;
    if(const auto* C=Cast<ACireConstruct>(Actor))return C->Health>0;
    return false;
}
bool CireCombat::AreHostile(AActor* Source,AActor* Target){
    if(Source==Target||!IsAlive(Source)||!IsAlive(Target))return false;
    if(auto* C=Cast<ACireConstruct>(Target))return C->CanBeDamagedBy(Source);
    if(auto* C=Cast<ACireConstruct>(Source))return AreHostile(C->GetSourceActor(),Target);
    if(auto* H=Cast<ACireHero>(Source))return H->IsHostile(Target);
    if(auto* M=Cast<ACireMonster>(Source)){
        const auto* H=Cast<ACireHero>(Target);const auto* Mode=Source->GetWorld()->GetAuthGameMode<ACireGameMode>();
        const auto* State=Source->GetWorld()->GetGameState<ACireGameState>();
        const bool Survival=Mode?Mode->Clock.Phase()==Cires::MatchPhase::Survival:State&&State->Phase==0;
        return H&&Survival&&M->Lane==H->TeamId;
    }return false;
}
float CireCombat::ApplyStrike(AActor* Source,AActor* Target,float Amount,const FString& AbilityName,bool bCanCrit){
    if(!IsValid(Source)||!Source->HasAuthority()||!AreHostile(Source,Target))return 0;
    const auto* H=Cast<ACireHero>(Source);
    const bool Critical=bCanCrit&&H&&FMath::FRand()<FMath::Clamp(H->CriticalChance+CireKits::CritBonus(H),0.f,1.f); // scaling-kits: crit aura
    return ApplyDamage(Source,Target,Amount*(Critical?FMath::Clamp(H->CriticalMultiplier,1.f,5.f):1.f),AbilityName,Critical);
}
void CireCombat::PlayCue(AActor* Source,AActor* Target,FName SkillId,FVector From,FVector To,ECireSpellCue Cue,float Scale,bool bSound){
    if(!IsValid(Source)||!Source->HasAuthority()||From.ContainsNaN()||To.ContainsNaN()||!FMath::IsFinite(Scale))return;
    auto* W=Source->GetWorld();const auto* Mode=W?W->GetAuthGameMode<ACireGameMode>():nullptr;if(!Mode)return;
    for(auto It=W->GetPlayerControllerIterator();It;++It){
        auto* C=Cast<ACireController>(It->Get());const auto* H=C?Cast<ACireHero>(C->GetPawn()):nullptr;
        if(!H||!H->bDrafted||H->TeamId<0)continue;
        if(Mode->Clock.Phase()==Cires::MatchPhase::Arena||TeamOf(Source)==H->TeamId||(Target&&TeamOf(Target)==H->TeamId))
            C->ClientSpellEffect(SkillId,From,To,Cue,FMath::Clamp(Scale,.1f,4.f),bSound);
    }
}
float CireCombat::ApplyDamage(AActor* Source, AActor* Target, float Amount, const FString& AbilityName,bool bCritical)
{
    if (!IsValid(Source) || !Source->HasAuthority() || !IsValid(Target) ||
        !FMath::IsFinite(Amount) || Amount <= 0) return 0;
    const float OriginalAmount = Amount; // scaling-kits: Headshot / double attack repeat the original hit
    // progression-shop: spell power, execute, every-Nth-hit and lantern marks scale outgoing damage.
    Amount = CireItems::ModifyOutgoingDamage(Source, Target, Amount, AbilityName);
    Amount = CireClassTraits::ModifyOutgoingDamage(Source, Amount); // champion-draft: Support -20% damage
    Amount = CireCrowdControl::ModifyOutgoingDamage(Source, Target, Amount, AbilityName); // champion-draft: Executioner
    Amount = CireSignatureSkills::ModifyOutgoingDamage(Source, Target, Amount, AbilityName); // new-champions: marks, fields, silver, banishment
    Amount = CireRollSkills::ModifyOutgoingDamage(Source, Target, Amount, AbilityName, &bCritical); // champion-draft: roll empowerment, crit, Momentum
    Amount = CireKits::ModifyOutgoingDamage(Source, Target, Amount, AbilityName); // scaling-kits: level-15 amp, Longshot, ranged-damage aura
    if (Amount <= 0) return 0;
    const FCireDamageEvent Event(AbilityName,bCritical);
    const float Applied = Target->TakeDamage(Amount, Event, Source->GetInstigatorController(), Source);
    CireItems::OnDamageDealt(Source, Target, Applied, AbilityName); // progression-shop: lifesteal
    CireClassTraits::OnDamageDealt(Source, Target, Applied); // champion-draft: Support Mending Strikes
    if (Applied > 0) CireCrowdControl::OnAbilityHit(Source, Target, AbilityName); // champion-draft: ability CC from Abilities.json
    if (Applied > 0) CireSignatureSkills::OnAbilityHit(Source, Target, AbilityName, Applied); // new-champions: slows, purges
    if (Applied > 0) CireKits::OnDamageDealt(Source, Target, OriginalAmount, Applied, AbilityName); // scaling-kits: level 15, auras, Headshot, Artillery
    return Applied;
}

float CireCombat::ApplyHealing(ACireHero* Source, ACireHero* Target, float Amount, const FString& AbilityName)
{
    if (!IsValid(Source) || !Source->HasAuthority() || Source->bDead || !Source->bDrafted ||
        !IsValid(Target) || Target->bDead || !Target->bDrafted || Target->TeamId != Source->TeamId ||
        !FMath::IsFinite(Amount) || Amount <= 0) return 0;
    auto* Mode = Source->GetWorld()->GetAuthGameMode<ACireGameMode>();
    if (!Mode || !Mode->IsCombatPhase()) return 0;
    const float Multiplier = (Source->HasSkill(TEXT("soul_conduit")) ? 1.25f : 1.f) * CireItems::HealingMultiplier(Source, AbilityName) // progression-shop: + Skill Shop level
        * CireCrowdControl::HealingMultiplier(Source, Target); // champion-draft: healing cuts
    const float Before = Target->Health;
    Target->Health = FMath::Min(Target->MaxHealth, Before + Amount * Multiplier);
    const float Applied = FMath::Max(0.f, Target->Health - Before);
    BroadcastHealing(Source, Target, Applied, AbilityName);
    CireThreat::Healing(Source,Target,Applied);
    if(Applied>0)PlayCue(Source,Target,FName(*AbilityName),Source->GetActorLocation(),Target->GetActorLocation(),ECireSpellCue::Impact);
    return Applied;
}

void CireCombat::BroadcastDamage(AActor* Source, AActor* Target, float AppliedAmount, const FDamageEvent& DamageEvent)
{
    if (!IsValid(Source) || !Source->HasAuthority() || !IsValid(Target) ||
        !FMath::IsFinite(AppliedAmount) || AppliedAmount <= 0) return;
    if (auto* Hero = Cast<ACireHero>(Source)) {
        if(auto* Summon=Cast<ACireSummon>(Hero);Summon&&Summon->GetOwnerHero())Summon->GetOwnerHero()->DamageDone+=AppliedAmount;
        else Hero->DamageDone += AppliedAmount;
    }
    const FString AbilityName = DamageEvent.IsOfType(FCireDamageEvent::CireClassID)
        ? static_cast<const FCireDamageEvent&>(DamageEvent).AbilityName : TEXT("Basic attack");
    auto Event=MakeEvent(Source,Target,AppliedAmount,AbilityName,false);
    Event.bCritical=DamageEvent.IsOfType(FCireDamageEvent::CireClassID)&&static_cast<const FCireDamageEvent&>(DamageEvent).bCritical;
    Broadcast(Event,Source->GetWorld());
    PlayCue(Source,Target,FName(*AbilityName),Source->GetActorLocation(),Target->GetActorLocation(),Event.bCritical?ECireSpellCue::Critical:ECireSpellCue::Impact);
}

FString CireCombat::OutcomeText(ECireHitOutcome O)
{
    return O==ECireHitOutcome::Miss?TEXT("Miss"):O==ECireHitOutcome::Block?TEXT("Block"):O==ECireHitOutcome::Resist?TEXT("Resist"):TEXT("Dodge");
}

void CireCombat::BroadcastAvoidance(AActor* Source, AActor* Target, ECireHitOutcome Outcome, const FString& AbilityName, float PreventedAmount)
{
    if (!IsValid(Source) || !Source->HasAuthority() || !IsValid(Target) || Outcome == ECireHitOutcome::Hit) return;
    FCireCombatEvent Event = MakeEvent(Source, Target, FMath::IsFinite(PreventedAmount) ? FMath::Max(0.f, PreventedAmount) : 0.f, AbilityName, false);
    Event.Outcome = Outcome;
    Broadcast(Event, Source->GetWorld());
}

void CireCombat::BroadcastHealing(ACireHero* Source, ACireHero* Target, float AppliedAmount, const FString& AbilityName)
{
    if (!IsValid(Source) || !Source->HasAuthority() || !IsValid(Target) ||
        !FMath::IsFinite(AppliedAmount) || AppliedAmount <= 0) return;
    Source->HealingDone += AppliedAmount;
    Broadcast(MakeEvent(Source, Target, AppliedAmount, AbilityName, true), Source->GetWorld());
}

#if !UE_BUILD_SHIPPING
bool CireCombat::RunTelemetrySmoke(ACireGameMode* Mode)
{
    if (!IsValid(Mode) || !Mode->HasAuthority()) return false;
    const Cires::MatchClock SavedClock = Mode->Clock;
    const auto SavedHeroes = Mode->Heroes;
    const auto SavedMonsters = Mode->Monsters;
    TArray<AActor*> Fixtures;
    CireKits::SetRandomProcs(false); // scaling-kits: deterministic telemetry (no shield blocks / Headshot rolls)
    ON_SCOPE_EXIT
    {
        CireKits::SetRandomProcs(true);
        Mode->Clock = SavedClock;
        Mode->Heroes = SavedHeroes;
        Mode->Monsters = SavedMonsters;
        for (auto* Fixture : Fixtures) if (IsValid(Fixture)) Fixture->Destroy();
    };
    FActorSpawnParameters Params;
    Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    const auto SpawnHero = [&](int32 Team)
    {
        auto* Hero = Mode->GetWorld()->SpawnActor<ACireHero>(ACireHero::StaticClass(),
            FVector(20000, Fixtures.Num() * 200, 1000), FRotator::ZeroRotator, Params);
        if (Hero)
        {
            Fixtures.Add(Hero);
            Hero->TeamId = Team;
            Hero->Draft(0); Hero->CriticalChance=0;
            Hero->HeroName = FString::Printf(TEXT("Telemetry fixture %d"), Fixtures.Num());
        }
        return Hero;
    };
    auto* Source = SpawnHero(0);
    auto* Ally = SpawnHero(0);
    auto* Enemy = SpawnHero(1);
    auto* Monster = Mode->GetWorld()->SpawnActor<ACireMonster>(ACireMonster::StaticClass(),
        FVector(20500, 0, 1000), FRotator::ZeroRotator, Params);
    if (Monster) Fixtures.Add(Monster);
    if (!Source || !Ally || !Enemy || !Monster)
    {
        UE_LOG(LogCireCombat, Error, TEXT("CIRE_TELEMETRY_FAIL fixture spawn"));
        return false;
    }
    int32 Assertions = 0;
    bool bPassed = true;
    const auto Check = [&](bool bCondition, const TCHAR* Label)
    {
        ++Assertions;
        if (!bCondition)
        {
            bPassed = false;
            UE_LOG(LogCireCombat, Error, TEXT("CIRE_TELEMETRY_FAIL %s"), Label);
        }
    };
    const auto Near = [](float A, float B) { return FMath::IsNearlyEqual(A, B, 0.01f); };
    // champion-draft class traits apply to every PvP hit: the fixture heroes are drafted Iron Wardens
    // (Tank, Natural Defense: flat reduction after other mitigation) and the source's outgoing class
    // multiplier. Expected values are derived from the same rules, not hard-coded around them.
    const auto Hit = [&](const ACireHero* Victim, float Raw)
    {
        const double Out = Raw * Cires::Traits::OutgoingDamageMultiplier(CireClassTraits::Role(Source));
        return static_cast<float>(Cires::Traits::ApplyIncomingFlatReduction(Out, CireClassTraits::Role(Victim)));
    };
    Mode->Clock = Cires::MatchClock({60, 90, 15});
    Check(Near(ApplyDamage(Source, Enemy, 30, TEXT("probe")), 0), TEXT("enemy PvE damage rejected"));
    Check(Near(ApplyDamage(Source, Ally, 30, TEXT("probe")), 0), TEXT("friendly damage rejected"));
    Monster->Lane = 0;
    Monster->Health = Monster->MaxHealth = 100;
    Check(Near(ApplyDamage(Source, Monster, 25, TEXT("probe strike")), 25), TEXT("monster applied damage"));
    Check(Near(Source->DamageDone, 25) && Near(Monster->Health, 75), TEXT("damage meter matches health removed"));
    Monster->Lane = 1;
    Check(Near(ApplyDamage(Source, Monster, 10, TEXT("probe")), 0), TEXT("other lane monster damage rejected"));
    Monster->Lane = 0;
    Mode->Clock.BeginIntermission();
    Check(Near(ApplyDamage(Source, Enemy, 30, TEXT("probe")), 0), TEXT("enemy town damage rejected"));
    Check(Near(ApplyDamage(Source, Monster, 30, TEXT("probe")), 0), TEXT("monster town damage rejected"));
    Mode->Clock.Advance(60);
    const float ArenaHit = Hit(Enemy, 50);
    Check(ArenaHit > 0 && Near(ApplyDamage(Source, Enemy, 50, TEXT("probe strike")), ArenaHit), TEXT("arena enemy damage permitted"));
    Enemy->Skills.Add(TEXT("stone_skin"));
    Enemy->ShieldUntil = Mode->GetWorld()->GetTimeSeconds() + 10;
    const float MitigatedHit = Hit(Enemy, 100 * .90f * .60f); // Stone Skin, then guard, then class trait
    Check(Near(ApplyDamage(Source, Enemy, 100, TEXT("probe strike")), MitigatedHit), TEXT("mitigated amount recorded"));
    Enemy->Health = 20;
    Check(Near(ApplyDamage(Source, Enemy, 100, TEXT("probe strike")), 20), TEXT("overkill excluded"));
    Check(Near(Source->DamageDone, 25 + ArenaHit + MitigatedHit + 20), TEXT("authoritative cumulative damage"));
    Check(Near(ApplyDamage(Source, Enemy, 100, TEXT("probe strike")), 0), TEXT("dead target damage rejected"));
    Source->Skills.Add(TEXT("soul_conduit"));
    Ally->Health = Ally->MaxHealth - 40;
    Check(Near(ApplyHealing(Source, Ally, 100, TEXT("probe heal")), 40), TEXT("effective healing excludes overheal"));
    Check(Near(ApplyHealing(Source, Ally, 100, TEXT("probe heal")), 0), TEXT("full health grants no healing credit"));
    Ally->Health -= 30;
    Check(Near(ApplyHealing(Source, Ally, 20, TEXT("probe heal")), 25), TEXT("healing passive included"));
    Check(Near(Source->HealingDone, 65), TEXT("authoritative cumulative healing"));
    Enemy->bDead = false;
    Enemy->Health = 20;
    Check(Near(ApplyHealing(Source, Enemy, 100, TEXT("probe heal")), 0), TEXT("enemy healing rejected"));
    Check(Near(ApplyDamage(Source, Ally, -10, TEXT("probe")), 0), TEXT("negative damage rejected"));
    Check(Near(ApplyHealing(Source, Ally, -10, TEXT("probe")), 0), TEXT("negative healing rejected"));
    const FCireCombatEvent DamageSample = MakeEvent(Source, Monster, 25, TEXT("probe strike"), false);
    Check(DamageSample.SourceName == Source->HeroName && DamageSample.TargetName == Monster->MonsterName &&
        DamageSample.AbilityName == TEXT("probe strike") && Near(DamageSample.Amount, 25) && !DamageSample.bHealing,
        TEXT("stable event attribution"));
    Check(CanObserve(DamageSample, 0, Cires::MatchPhase::Survival), TEXT("own lane event visible"));
    Check(!CanObserve(DamageSample, 1, Cires::MatchPhase::Survival), TEXT("opposing lane event private"));
    Check(!CanObserve(DamageSample, INDEX_NONE, Cires::MatchPhase::Survival), TEXT("unassigned observer private"));
    const FCireCombatEvent HealSample = MakeEvent(Source, Ally, 25, TEXT("probe heal"), true);
    Check(CanObserve(HealSample, 1, Cires::MatchPhase::Arena), TEXT("arena healing event visible"));
    Check(!CanObserve(HealSample, 1, Cires::MatchPhase::Survival), TEXT("opposing PvE healing event private"));
    const float CoreDamage = Source->DamageDone, CoreHealing = Source->HealingDone;

    // Ultimate casts use actual server combat paths with an isolated roster. The
    // fixture assigns learned IDs directly so draft-policy changes cannot mask a
    // combat, resource, cooldown, or slot-mapping regression.
    auto* NearEnemy = SpawnHero(1);
    auto* FarEnemy = SpawnHero(1);
    if (!NearEnemy || !FarEnemy)
    {
        UE_LOG(LogCireCombat, Error, TEXT("CIRE_TELEMETRY_FAIL ultimate fixture spawn"));
        return false;
    }
    Mode->Heroes = {Source, Ally, Enemy, NearEnemy, FarEnemy};
    Mode->Monsters.Empty();
    NearEnemy->SetActorLocation(Source->GetActorLocation() + FVector(200, 500, 0));
    FarEnemy->SetActorLocation(Source->GetActorLocation() + FVector(1500, 500, 0));
    Ally->Health = Ally->MaxHealth;
    for (auto* Victim : {Enemy, NearEnemy, FarEnemy})
    {
        Victim->Skills.Empty();
        Victim->bDead = false;
        Victim->Health = Victim->MaxHealth = 1000;
        Victim->ShieldUntil = 0;
    }
    const auto PrepareUltimate = [&](const TCHAR* Id)
    {
        Source->Skills = {FString(Id)};
        Source->Cooldowns.Init(0, 1);
        Source->GlobalCooldown = 0;
        Source->Mana = Source->MaxMana;
        Source->Energy = 100;
        Source->CDR = 0;
        Source->Target = Enemy;
    };
    const float Now = Mode->GetWorld()->GetTimeSeconds();
    PrepareUltimate(TEXT("bastion_of_dawn"));
    Source->Health = Source->MaxHealth - 200;
    float PreviousHealing = Source->HealingDone;
    // scaling-kits: 30% max health + the DB primary term, capped by the missing 200.
    const float BastionHeal = FMath::Min(200.f, (Source->MaxHealth * .30f + CireKits::Amount(Source, TEXT("bastion_of_dawn"))) * Mode->Power(Source->TeamId));
    const float BastionBefore = Source->Health;
    Source->Cast(0);
    Check(Near(Source->Health, BastionBefore + BastionHeal) && Source->ShieldUntil > Now && Ally->ShieldUntil > Now && Enemy->ShieldUntil == 0,
        TEXT("bastion self healing and allied guard only"));
    Check(Near(Source->Energy, 55) && Near(Source->Cooldowns[0], 75), TEXT("bastion energy and cooldown"));
    Check(Near(Source->HealingDone - PreviousHealing, BastionHeal), TEXT("bastion meter counts healing not shields"));
    Source->GlobalCooldown = 0;
    Source->Cast(0);
    Check(Near(Source->Energy, 55) && Near(Source->HealingDone - PreviousHealing, BastionHeal), TEXT("ultimate cooldown rejects repeated cast"));

    PrepareUltimate(TEXT("cataclysm"));
    Source->CDR = 0.25f;
    float PreviousDamage = Source->DamageDone;
    const float CataclysmHit = Hit(Enemy, CireKits::Amount(Source, TEXT("cataclysm")) * Mode->Power(Source->TeamId)); // scaling-kits: DB base + coef x PRIMARY
    Source->Cast(0);
    Check(Near(Enemy->Health, 1000 - CataclysmHit) && Near(NearEnemy->Health, 1000 - CataclysmHit) && Near(FarEnemy->Health, 1000) && Near(Ally->Health, Ally->MaxHealth),
        TEXT("cataclysm hits nearby enemies only"));
    Check(Near(Source->Mana, 150) && Near(Source->Cooldowns[0], 60), TEXT("cataclysm mana and pure cooldown reduction"));
    Check(Near(Source->DamageDone - PreviousDamage, 2 * CataclysmHit), TEXT("cataclysm effective damage attribution"));
    PrepareUltimate(TEXT("cataclysm"));
    Mode->Clock = Cires::MatchClock({60, 90, 15});
    Source->Cast(0);
    Check(Near(Source->Mana, Source->MaxMana) && Near(Source->Cooldowns[0], 0) && Near(Enemy->Health, 1000 - CataclysmHit), TEXT("ultimate cannot attack other team during PvE"));
    Mode->Clock.BeginIntermission();
    Source->Cast(0);
    Check(Near(Source->Mana, Source->MaxMana) && Near(Source->Cooldowns[0], 0), TEXT("ultimate blocked during preparation"));
    Mode->Clock.Advance(60);

    PrepareUltimate(TEXT("executioners_verdict"));
    Source->Target = FarEnemy;
    Source->Cast(0);
    Check(Near(Source->Energy, 100) && Near(Source->Cooldowns[0], 0) && Near(FarEnemy->Health, 1000), TEXT("ultimate range failure consumes no resource"));
    Source->Target = Enemy;
    Enemy->MaxHealth = 5000;
    Enemy->Health = 3000;
    PreviousDamage = Source->DamageDone;
    // 100 + 3 x primary + missing-health bonus capped at 300 (2000 missing -> 300).
    const float VerdictHit = Hit(Enemy, (CireKits::Amount(Source, TEXT("executioners_verdict")) + 300.f) * Mode->Power(Source->TeamId)); // scaling-kits
    Source->Cast(0);
    Check(Near(Enemy->Health, 3000 - VerdictHit) && Near(Source->DamageDone - PreviousDamage, VerdictHit), TEXT("executioner missing health bonus capped"));
    Check(Near(Source->Energy, 40) && Near(Source->Cooldowns[0], 60), TEXT("executioner energy and cooldown"));

    PrepareUltimate(TEXT("renewal"));
    Source->Health = Source->MaxHealth - 60;
    Ally->Health = Ally->MaxHealth - 80;
    Source->SlowUntil = Ally->SlowUntil = Enemy->SlowUntil = Now + 20;
    PreviousHealing = Source->HealingDone;
    Source->Cast(0); CireCrowdControl::CompleteCastNow(Source); // champion-draft: Renewal has a 2.5s cast
    Check(Near(Source->Health, Source->MaxHealth) && Near(Ally->Health, Ally->MaxHealth) && Near(Enemy->Health, 3000 - VerdictHit) &&
        Source->SlowUntil == 0 && Ally->SlowUntil == 0 && Enemy->SlowUntil > Now, TEXT("renewal heals and cleanses allies only"));
    Check(Near(Source->HealingDone - PreviousHealing, 140), TEXT("renewal overhealing excluded from meter"));
    Check(Near(Source->Mana, 160) && Near(Source->Cooldowns[0], 90), TEXT("renewal mana and cooldown"));
    PrepareUltimate(TEXT("renewal"));
    Ally->bDead = true;
    Ally->Health = 0;
    PreviousHealing = Source->HealingDone;
    Source->Cast(0);
    Check(Ally->bDead && Ally->Health == 0 && Near(Source->HealingDone, PreviousHealing), TEXT("renewal cannot revive fallen allies"));

    Source->Skills = {TEXT("stone_skin"), TEXT("shield_slam"), TEXT("renewal"), TEXT("ember_lance"),
        TEXT("frost_bind"), TEXT("piercing_shot"), TEXT("shadow_step"), TEXT("sanctuary")};
    Check(Source->ActiveSkillSlot(0) == 1 && Source->ActiveSkillSlot(1) == 3 && Source->ActiveSkillSlot(5) == 7 && Source->UltimateSkillSlot() == 2,
        TEXT("six active slots and ultimate map independently of learn order"));
    Check(Source->ActiveSkillSlot(-1) == INDEX_NONE && Source->ActiveSkillSlot(6) == INDEX_NONE, TEXT("active ordinal bounds reject invalid slots"));

    Monster->Health = 0;
    Monster->SetActorScale3D(FVector(1.4f));
    const FName DeadMonsterId = Monster->GetFName();
    const FString DeadMonsterName = Monster->MonsterName;
    const FVector ExpectedHead = Monster->GetActorLocation() + FVector(0, 0, Monster->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 20);
    const FCireCombatEvent Lethal = MakeEvent(Source, Monster, 75, TEXT("lethal fixture"), false);
    FCireCombatEvent PersonalLethal, TeamLethal, Rejected;
    Check(ForRecipient(Lethal, Source, Cires::MatchPhase::Survival, PersonalLethal) && PersonalLethal.bLocalSource && !PersonalLethal.bLocalTarget,
        TEXT("lethal outgoing identity is recipient stable"));
    Check(!PersonalLethal.Source && !PersonalLethal.Target && PersonalLethal.TargetId == DeadMonsterId && PersonalLethal.Location.Equals(ExpectedHead, 0.01),
        TEXT("wire event uses scaled head snapshot without actor NetGUIDs"));
    Check(ForRecipient(Lethal, Ally, Cires::MatchPhase::Survival, TeamLethal) && !TeamLethal.bLocalSource && !TeamLethal.bLocalTarget,
        TEXT("teammate log event is not mislabeled personal"));
    Check(!ForRecipient(Lethal, Enemy, Cires::MatchPhase::Survival, Rejected), TEXT("lethal event cannot leak to opposing PvE team"));
    Ally->bDrafted = false;
    Check(!ForRecipient(Lethal, Ally, Cires::MatchPhase::Survival, Rejected), TEXT("undrafted observer receives no combat event"));
    Ally->bDrafted = true;
    FCireCombatEvent PersonalIncoming;
    const FCireCombatEvent Incoming = MakeEvent(Monster, Ally, 25, TEXT("incoming lethal fixture"), false);
    Check(ForRecipient(Incoming, Ally, Cires::MatchPhase::Survival, PersonalIncoming) && !PersonalIncoming.bLocalSource && PersonalIncoming.bLocalTarget,
        TEXT("fallen local target retains incoming identity"));
    Monster->Destroy();
    Check(PersonalLethal.TargetId == DeadMonsterId && PersonalLethal.TargetName == DeadMonsterName && Near(PersonalLethal.Amount, 75) && PersonalLethal.Location.Equals(ExpectedHead, 0.01),
        TEXT("lethal identity and position survive immediate actor destruction"));

    TArray<FCireCombatEvent> Received;
    uint32 ReceiveSequence = 0;
    AppendReceivedEvent(Received, ReceiveSequence, PersonalLethal, 10.f);
    AppendReceivedEvent(Received, ReceiveSequence, PersonalIncoming, 10.1f);
    const uint32 FirstSequence = Received[0].Sequence;
    Check(FirstSequence == 1 && Received[1].Sequence == 2, TEXT("receiver assigns monotonic event identity"));
    for (int32 Index = 0; Index < 300; ++Index) AppendReceivedEvent(Received, ReceiveSequence, TeamLethal, 10.2f);
    Check(Received.Num() == 256 && Received.ContainsByPredicate([FirstSequence](const FCireCombatEvent& Event) { return Event.Sequence == FirstSequence && Event.bLocalSource; }),
        TEXT("team traffic cannot evict recent personal combat text"));
    const uint32 BeforePersonal = ReceiveSequence;
    AppendReceivedEvent(Received, ReceiveSequence, PersonalLethal, 10.3f);
    Check(Received.Num() == 256 && Received.Last().Sequence == BeforePersonal + 1 && Received[0].Sequence == FirstSequence,
        TEXT("stable SCT identity survives buffer trimming"));
    TArray<FCireCombatEvent> PersonalOnly;
    uint32 PersonalSequence = 0;
    for (int32 Index = 0; Index < 256; ++Index) AppendReceivedEvent(PersonalOnly, PersonalSequence, PersonalLethal, 1.f);
    AppendReceivedEvent(PersonalOnly, PersonalSequence, TeamLethal, 1.1f);
    Check(PersonalOnly.Num() == 256 && PersonalSequence == 256, TEXT("full personal buffer rejects team spam"));
    AppendReceivedEvent(PersonalOnly, PersonalSequence, PersonalLethal, 9.2f);
    Check(PersonalOnly.Num() == 1 && PersonalOnly[0].Sequence == 257, TEXT("old events expire without resetting sequence"));
    FCireCombatEvent Invalid = PersonalLethal;
    Invalid.Amount = 0;
    AppendReceivedEvent(PersonalOnly, PersonalSequence, Invalid, 9.3f);
    Check(PersonalOnly.Num() == 1 && PersonalSequence == 257, TEXT("receiver rejects empty combat amounts"));
    UE_LOG(LogCireCombat, Display, TEXT("CIRE_TELEMETRY_%s assertions=%d damage=%.0f healing=%.0f"),
        bPassed ? TEXT("PASS") : TEXT("FAIL"), Assertions, CoreDamage, CoreHealing);
    return bPassed;
}
#endif
