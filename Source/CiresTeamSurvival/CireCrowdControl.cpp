#include "CireCrowdControl.h"
#include "CireScalingKits.h" // scaling-kits
#include "CireMobility.h" // feat/camera-movement
#include "CireSkillShop.h" // progression-shop: per-level cast scaling
#include "CireRollSkills.h" // champion-draft: dodge-roll skills
#include "CireAbilityDB.h"
#include "CireBuffs.h"
#include "CireCombatEvents.h"
#include "CireGame.h"
#include "CireItems.h"
#include "CireNPCCombat.h"
#include "CireRaces.h"
#include "CireSummon.h"
#include "CireDeveloperTools.h"
#include "CireEffects.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Misc/ScopeExit.h"
#include "Rules/CiresRules.h"

DEFINE_LOG_CATEGORY_STATIC(LogCireCC,Log,All);

namespace
{
const FName StunnedId(TEXT("stunned")),SilencedId(TEXT("silenced")),NpcSilencedId(TEXT("npc_silenced")),LockedId(TEXT("interrupted")),
    HealCutId(TEXT("healing_cut")),HealCutDoneId(TEXT("heal_cut_done")),ArmorId(TEXT("armor_broken")),ExecId(TEXT("executioner_ready"));

struct FPendingCast { int32 Slot=INDEX_NONE; FString Id; TWeakObjectPtr<AActor> Target; FVector Aim=FVector::ZeroVector; bool bAim=false; };
struct FServerState
{
    TMap<TWeakObjectPtr<ACireHero>,FPendingCast> Casts;
    TMap<TWeakObjectPtr<ACireHero>,TPair<FString,float>> Lockouts;   // school, until
    TMap<TWeakObjectPtr<ACireHero>,double> ExecutionerReadyAt;
    TMap<TWeakObjectPtr<AActor>,TMap<FName,TArray<float>>> DR;       // PvP diminishing returns history
    TSet<TWeakObjectPtr<ACireHero>> Resolving;
};
FServerState& State(){static FServerState S;return S;}

float Now(const UWorld* W){return W?W->GetTimeSeconds():0.f;}
bool IsBossUnit(const AActor* A){const auto* M=Cast<ACireMonster>(A);return M&&M->IsLaneBoss();}
bool IsChampion(const AActor* A){const auto* H=Cast<ACireHero>(A);return H&&!H->IsA<ACireSummon>();}
float BuffMagnitude(const AActor* Unit,FName Id)
{
    if(!CireBuffs::IsActive(Unit,Id))return 0.f;
    const auto* S=CireBuffs::Get(Unit);const auto* E=S?S->Find(Id):nullptr;
    return E?FMath::Clamp(E->Stacks/100.f,0.f,1.f):0.f;
}
// PvP chain control: stun/silence durations diminish between champions.
float Diminish(AActor* Target,AActor* Source,FName Category,float Seconds)
{
    if(!IsChampion(Target)||!IsChampion(Source))return Seconds;
    auto& History=State().DR.FindOrAdd(Target).FindOrAdd(Category);
    const float T=Now(Target->GetWorld());
    History.RemoveAll([T](float At){return T-At>Cires::CC::DiminishingWindowSeconds;});
    const float Applied=static_cast<float>(Cires::CC::DiminishedDuration(Seconds,History.Num()));
    if(Applied>0)History.Add(T);
    return Applied;
}
template<typename Fn> void ForEachHostileNear(ACireHero* Source,FVector Center,float Radius,Fn&& Visit)
{
    auto* Mode=Source?Source->GetWorld()->GetAuthGameMode<ACireGameMode>():nullptr;if(!Mode)return;
    TArray<AActor*> Units;
    for(auto* M:Mode->Monsters)if(IsValid(M)&&M->Health>0)Units.Add(M);
    for(auto* H:Mode->Heroes)if(IsValid(H)&&!H->bDead)Units.Add(H);
    for(TActorIterator<ACireSummon> It(Source->GetWorld());It;++It)if(!It->bDead)Units.AddUnique(*It);
    for(AActor* U:Units)
    {
        if(!Source->IsHostile(U))continue;
        const FVector D=U->GetActorLocation()-Center;
        if(FMath::Abs(D.Z)<=300.f&&D.Size2D()<=Radius)Visit(U,D.Size2D());
    }
}
}

// ---------------------------------------------------------------- queries
bool CireCrowdControl::IsStunned(const AActor* U){return U&&CireBuffs::IsActive(U,StunnedId);}
bool CireCrowdControl::IsSilenced(const AActor* U){return U&&(CireBuffs::IsActive(U,SilencedId)||CireBuffs::IsActive(U,NpcSilencedId));}
float CireCrowdControl::HealingReceivedCut(const AActor* U){return BuffMagnitude(U,HealCutId);}
float CireCrowdControl::HealingDoneCut(const AActor* U){return BuffMagnitude(U,HealCutDoneId);}
float CireCrowdControl::HealingMultiplier(const AActor* Source,const AActor* Target)
{
    return static_cast<float>(Cires::CC::ApplyHealingCut(1.0,HealingReceivedCut(Target),HealingDoneCut(Source)));
}
float CireCrowdControl::ArmorMultiplier(const AActor* U){return U&&CireBuffs::IsActive(U,ArmorId)?.5f:1.f;}
bool CireCrowdControl::IsCasting(const ACireHero* H){return H&&!H->CastSkill.IsNone()&&H->CastEndTime>H->CastStartTime;}
float CireCrowdControl::CastProgress(const ACireHero* H)
{
    if(!IsCasting(H))return -1.f;
    const float T=CireBuffs::ServerNow(H->GetWorld());
    return FMath::Clamp((T-H->CastStartTime)/FMath::Max(.01f,H->CastEndTime-H->CastStartTime),0.f,1.f);
}

// ---------------------------------------------------------------- apply
float CireCrowdControl::Stun(AActor* Target,float Seconds,AActor* Source)
{
    if(!IsValid(Target)||!Target->HasAuthority()||IsBossUnit(Target)||Seconds<=0)return 0.f;
    if(CireKits::IgnoresStun(Target))return 0.f; // scaling-kits: level-15 stun-ignore aura
    Seconds*=CireItems::ControlDurationMultiplier(Source); // items-v2: Shackles of the Pale King
    const float Applied=Diminish(Target,Source,StunnedId,Seconds);if(Applied<=0)return 0.f;
    CireBuffs::Apply(Target,StunnedId,Applied,Source);
    if(auto* H=Cast<ACireHero>(Target))
    {
        CancelCast(H,TEXT("Stunned"));H->PendingAttackTarget.Reset();
        H->GetCharacterMovement()->StopMovementImmediately();H->Notice=TEXT("Stunned!");
    }
    else if(auto* M=Cast<ACireMonster>(Target)){CireNPCCombat::Interrupt(M);M->GetCharacterMovement()->StopMovementImmediately();}
    return Applied;
}
float CireCrowdControl::Silence(AActor* Target,float Seconds,AActor* Source)
{
    if(!IsValid(Target)||!Target->HasAuthority()||IsBossUnit(Target)||Seconds<=0)return 0.f;
    Seconds*=CireItems::ControlDurationMultiplier(Source); // items-v2
    const float Applied=Diminish(Target,Source,SilencedId,Seconds);if(Applied<=0)return 0.f;
    CireBuffs::Apply(Target,SilencedId,Applied,Source);
    if(auto* H=Cast<ACireHero>(Target)){CancelCast(H,TEXT("Silenced"));H->Notice=TEXT("Silenced: you cannot cast right now.");}
    else if(auto* M=Cast<ACireMonster>(Target)){if(!M->CastingAbility.IsEmpty())CireNPCCombat::Interrupt(M);}
    return Applied;
}
float CireCrowdControl::Slow(AActor* Target,float Seconds,AActor* Source)
{
    if(!IsValid(Target)||!Target->HasAuthority()||Seconds<=0)return 0.f;
    Seconds*=CireItems::ControlDurationMultiplier(Source); // items-v2
    const float Until=Now(Target->GetWorld())+Seconds;
    if(auto* H=Cast<ACireHero>(Target))H->SlowUntil=FMath::Max(H->SlowUntil,Until);
    else if(auto* M=Cast<ACireMonster>(Target))M->SlowUntil=FMath::Max(M->SlowUntil,Until);
    else return 0.f;
    return Seconds;
}
void CireCrowdControl::HealCut(AActor* Target,float Fraction,float Seconds,AActor* Source,bool bDone)
{
    if(!IsValid(Target)||!Target->HasAuthority()||Seconds<=0||Fraction<=0)return;
    const FName Id=bDone?HealCutDoneId:HealCutId;
    const int32 Percent=FMath::Clamp(FMath::RoundToInt(Fraction*100.f),1,100);
    CireBuffs::Apply(Target,Id,Seconds,Source,FMath::Max(Percent,FMath::RoundToInt(BuffMagnitude(Target,Id)*100.f)));
}
void CireCrowdControl::ArmorBreak(AActor* Target,float Seconds,AActor* Source)
{
    if(IsValid(Target)&&Target->HasAuthority()&&Seconds>0)CireBuffs::Apply(Target,ArmorId,Seconds,Source);
}
bool CireCrowdControl::Interrupt(AActor* Target,AActor* Source,float LockoutSeconds)
{
    if(!IsValid(Target)||!Target->HasAuthority())return false;
    if(auto* M=Cast<ACireMonster>(Target))return CireNPCCombat::InterruptCast(M,Cast<ACireHero>(Source));
    auto* H=Cast<ACireHero>(Target);if(!IsCasting(H))return false;
    const FCireAbilityDef* D=CireAbilityDB::Find(H->CastSkill.ToString());
    CancelCast(H,TEXT("Interrupted"));
    if(D&&LockoutSeconds>0)
    {
        State().Lockouts.Add(H,{D->School,Now(H->GetWorld())+LockoutSeconds});
        CireBuffs::Apply(H,LockedId,LockoutSeconds,Source);
        H->Notice=FString::Printf(TEXT("Interrupted: %s spells locked for %.0fs."),*D->School,LockoutSeconds);
    }
    return true;
}

// ---------------------------------------------------------------- casting
bool CireCrowdControl::GateCast(ACireHero* H,int32 Slot,const FString& Id)
{
    if(!H||State().Resolving.Contains(H))return false;
    if(ACireHero::IsPassive(Id))return false;
    if(IsStunned(H)){H->Notice=TEXT("Stunned: you cannot act.");return true;}
    if(IsSilenced(H)){H->Notice=TEXT("Silenced: you cannot cast right now.");return true;}
    const FCireAbilityDef* D=CireAbilityDB::Find(Id);
    if(const auto* Lock=State().Lockouts.Find(H);Lock&&D&&Lock->Value>Now(H->GetWorld())&&Lock->Key==D->School)
    {H->Notice=FString::Printf(TEXT("%s spells are locked out."),*D->School);return true;}
    if(IsCasting(H)){H->Notice=TEXT("Already casting.");return true;}
    if(!D||D->CastTime<=0)return false;
    // feat/camera-movement: WoW rule, data-driven per ability (Abilities.json castWhileMoving).
    if(CireMovement::BlocksCast(*H,Id)){H->Notice=TEXT("Can't cast while moving.");H->ForceNetUpdate();return true;}
    if(!CireSkillShop::CanPayCast(H,Id,D->Base.ManaCost,D->Base.EnergyCost)){H->Notice=CireSkillShop::CostFailText();return true;} // items-v2: scaled cost
    if(CireRollSkills::ConsumeInstantCast(H))return false; // champion-draft: Quickened Mind (instant after a roll)
    FPendingCast P;P.Slot=Slot;P.Id=Id;P.Target=H->Target;P.bAim=H->bHasCastAim;P.Aim=H->CastAimPoint;
    State().Casts.Add(H,P);
    const float T=Now(H->GetWorld());
    H->CastSkill=FName(*Id);H->CastStartTime=T;H->CastEndTime=T+D->CastTime;H->GlobalCooldown=FMath::Max(H->GlobalCooldown,.3f);
    H->Notice=FString::Printf(TEXT("Casting %s..."),*D->Name);H->ForceNetUpdate();
    CireCombat::PlayCue(H,H->Target,FName(*Id),H->GetActorLocation(),H->GetActorLocation(),ECireSpellCue::Cast,.7f,false);
    return true;
}
void CireCrowdControl::CancelCast(ACireHero* H,const FString& Reason)
{
    if(!H||!IsCasting(H))return;
    State().Casts.Remove(H);
    H->CastSkill=NAME_None;H->CastStartTime=H->CastEndTime=0;
    if(!Reason.IsEmpty())H->Notice=Reason+TEXT(": cast cancelled.");
    H->ForceNetUpdate();
}
void CireCrowdControl::TickHero(ACireHero* H,float Delta)
{
    RegisterCastProvider();
    if(!H||!H->HasAuthority())return;
    CireRollSkills::Tick(H,Delta); // champion-draft: roll mines, bot rolling
    const float T=Now(H->GetWorld());
    if(IsCasting(H))
    {
        auto* Mode=H->GetWorld()->GetAuthGameMode<ACireGameMode>();
        if(H->bDead||!Mode||!Mode->IsCombatPhase()){CancelCast(H,H->bDead?TEXT(""):TEXT("Phase changed"));}
        else if(CireMovement::BlocksCast(*H,H->CastSkill.ToString())){CancelCast(H,TEXT("Moved"));} // feat/camera-movement: moving cancels (WoW)
        else if(T>=H->CastEndTime)
        {
            const FPendingCast P=State().Casts.FindRef(H);State().Casts.Remove(H);
            H->CastSkill=NAME_None;H->CastStartTime=H->CastEndTime=0;H->ForceNetUpdate();
            if(H->Skills.IsValidIndex(P.Slot)&&H->Skills[P.Slot]==P.Id)
            {
                AActor* Previous=H->Target;if(P.Target.IsValid())H->Target=P.Target.Get();
                const float Gcd=H->GlobalCooldown;H->GlobalCooldown=0;
                State().Resolving.Add(H);
                if(P.bAim)H->CastAt(P.Slot,P.Aim);else H->Cast(P.Slot);
                State().Resolving.Remove(H);
                H->GlobalCooldown=FMath::Max(H->GlobalCooldown,Gcd);H->Target=Previous;
            }
        }
    }
    // Executioner: ready immediately when learned, then every five minutes.
    if(H->HasSkill(TEXT("executioner")))
    {
        double& ReadyAt=State().ExecutionerReadyAt.FindOrAdd(H,T);
        const bool bReady=T>=ReadyAt;
        if(bReady&&!CireBuffs::IsActive(H,ExecId))CireBuffs::Apply(H,ExecId,0.f,H);
        if(!bReady&&CireBuffs::IsActive(H,ExecId))CireBuffs::Remove(H,ExecId);
    }
}
bool CireCrowdControl::CompleteCastNow(ACireHero* H)
{
    if(!IsCasting(H))return false;
    H->CastEndTime=H->CastStartTime=Now(H->GetWorld())-.01f;H->CastStartTime-=.01f;
    TickHero(H,0.f);return true;
}
void CireCrowdControl::RegisterCastProvider()
{
    static bool bRegistered=false;if(bRegistered)return;bRegistered=true;
    CireCasts::RegisterHeroProvider([](const ACireHero& H,float ServerNow,FCireCastView& Out)
    {
        if(H.CastSkill.IsNone()||H.CastEndTime<=H.CastStartTime||H.CastEndTime<=ServerNow)return false;
        const FCireAbilityDef* D=CireAbilityDB::Find(H.CastSkill.ToString());
        Out.bCasting=true;Out.AbilityId=H.CastSkill;Out.Name=D?D->Name:ACireHero::SkillName(H.CastSkill.ToString());
        Out.Duration=H.CastEndTime-H.CastStartTime;Out.Remaining=H.CastEndTime-ServerNow;
        Out.Progress=FMath::Clamp(1.f-Out.Remaining/FMath::Max(.01f,Out.Duration),0.f,1.f);
        Out.bInterruptible=true;Out.bHeal=D&&D->Types.Contains(TEXT("HEAL"));
        return true;
    });
}
bool CireCrowdControl::TickMonster(ACireMonster* M,float)
{
    if(!M||!M->HasAuthority()||!IsStunned(M))return false;
    M->GetCharacterMovement()->StopMovementImmediately();
    return true;
}

// ---------------------------------------------------------------- ability effects
void CireCrowdControl::OnAbilityHit(AActor* Source,AActor* Target,const FString& AbilityName)
{
    if(!IsValid(Source)||!Source->HasAuthority()||!IsValid(Target))return;
    const FCireAbilityDef* D=CireAbilityDB::FindByName(AbilityName);if(!D)return;
    static const FName Stun(TEXT("stun")),Silence(TEXT("silence")),Interrupt(TEXT("interrupt")),HealCutT(TEXT("healCut")),HealCutDoneT(TEXT("healCutDone")),ArmorT(TEXT("armorBreak"));
    for(const FCireAbilityEffect& E:D->Effects)
    {
        if(E.Zone==TEXT("self"))continue;
        if(E.Type==Stun)CireCrowdControl::Stun(Target,E.Duration,Source);
        else if(E.Type==Silence)CireCrowdControl::Silence(Target,E.Duration,Source);
        else if(E.Type==Interrupt)CireCrowdControl::Interrupt(Target,Source,E.LockoutSeconds);
        else if(E.Type==HealCutT)HealCut(Target,E.Magnitude,E.Duration,Source,false);
        else if(E.Type==HealCutDoneT)HealCut(Target,E.Magnitude,E.Duration,Source,true);
        else if(E.Type==ArmorT)ArmorBreak(Target,E.Duration,Source);
    }
}

bool CireCrowdControl::IsExecutionerReady(const ACireHero* H){return H&&H->HasSkill(TEXT("executioner"))&&CireBuffs::IsActive(H,ExecId);}

float CireCrowdControl::ModifyOutgoingDamage(AActor* Source,AActor* Target,float Amount,const FString& AbilityName)
{
    auto* H=Cast<ACireHero>(Source);
    if(!H||!H->HasAuthority()||!IsExecutionerReady(H)||!CireItems::IsBasicAttack(Source,AbilityName)||!IsValid(Target))return Amount;
    const double* ReadyAt=State().ExecutionerReadyAt.Find(H);if(!ReadyAt||Now(H->GetWorld())<*ReadyAt)return Amount;
    if(IsBossUnit(Target))return Amount; // bosses: a normal hit, the charge is kept
    const auto* M=Cast<ACireMonster>(Target);const auto* Victim=Cast<ACireHero>(Target);
    const Cires::CC::ExecuteTarget Kind=M?Cires::CC::ExecuteTarget::Monster:Cires::CC::ExecuteTarget::Hero;
    const float Health=M?M->Health:Victim?Victim->Health:0.f,MaxHealth=M?M->MaxHealth:Victim?Victim->MaxHealth:0.f;
    float Result=static_cast<float>(Cires::CC::ExecuteDamage(Kind,Health,MaxHealth,Amount));
    if(M)Result=FMath::Max(Result,Health*20.f+1.f); // lethal through any mitigation
    State().ExecutionerReadyAt.Add(H,Now(H->GetWorld())+CireDeveloperTools::CooldownSeconds(H->GetWorld(),static_cast<float>(Cires::CC::ExecutionerIntervalSeconds)));
    CireBuffs::Remove(H,ExecId);H->Notice=TEXT("Executioner!");
    CireCombat::PlayCue(H,Target,TEXT("executioner"),H->GetActorLocation(),Target->GetActorLocation(),ECireSpellCue::Impact,1.3f,true);
    return Result;
}

bool CireCrowdControl::HandlesSkill(const FString& Id){return Id==TEXT("decimating_strike");}
FString CireCrowdControl::Description(const FString& Id)
{
    const FCireAbilityDef* D=CireAbilityDB::Find(Id);if(!D)return FString();
    return FString::Printf(TEXT("%.0f energy | %.0fs CD. %s"),D->Base.EnergyCost,D->Base.Cooldown,*D->Description.Replace(TEXT("{effect}"),*FString::Printf(TEXT("%.0f"),D->Base.Effect)));
}
bool CireCrowdControl::CastSkill(ACireHero* H,int32 Slot,const FString& Id)
{
    if(!H||!H->HasAuthority()||!H->Cooldowns.IsValidIndex(Slot))return false;
    const FCireAbilityDef* D=CireAbilityDB::Find(Id);if(!D)return false;
    auto* Mode=H->GetWorld()->GetAuthGameMode<ACireGameMode>();if(!Mode)return false;
    AActor* Target=H->Target;
    if(!H->IsHostile(Target)||!H->InRange(Target,D->Range)){H->Notice=TEXT("Select a hostile target in melee range.");return false;}
    if(!CireSkillShop::CanPayCast(H,Id,D->Base.ManaCost,D->Base.EnergyCost)){H->Notice=CireSkillShop::CostFailText();return false;} // progression-shop: Skill Shop level (Ability DB curve)
    H->Energy-=D->Base.EnergyCost;H->Mana-=D->Base.ManaCost;
    H->Cooldowns[Slot]=static_cast<float>(Cires::CooldownSeconds(CireDeveloperTools::CooldownSeconds(H->GetWorld(),D->Base.Cooldown),H->CDR));
    CireSkillShop::ApplyCastLevel(H,Slot,Id,D->Base.ManaCost,D->Base.EnergyCost); // progression-shop: Skill Shop level (Ability DB curve)
    H->GlobalCooldown=.9f;
    const auto* M=Cast<ACireMonster>(Target);const auto* Victim=Cast<ACireHero>(Target);
    const float Normal=CireKits::Amount(H,Id,D->Base.Effect,2.f)*Mode->Power(H->TeamId); // scaling-kits
    const Cires::CC::ExecuteTarget Kind=IsBossUnit(Target)?Cires::CC::ExecuteTarget::Boss:M?Cires::CC::ExecuteTarget::Monster:Cires::CC::ExecuteTarget::Hero;
    const float Health=M?M->Health:Victim?Victim->Health:0.f,MaxHealth=M?M->MaxHealth:Victim?Victim->MaxHealth:0.f;
    float Amount=static_cast<float>(Cires::CC::ExecuteDamage(Kind,Health,MaxHealth,Normal));
    if(Kind==Cires::CC::ExecuteTarget::Monster)Amount=FMath::Max(Amount,Health*20.f+1.f);
    const FVector Center=Target->GetActorLocation();
    CireCombat::PlayCue(H,Target,FName(*Id),H->GetActorLocation(),Center,ECireSpellCue::Impact,1.4f,true);
    CireCombat::ApplyDamage(H,Target,Amount,D->Name);
    const float Radius=D->Radius>0?D->Radius:500.f;
    ForEachHostileNear(H,Center,Radius,[&](AActor* U,float){ArmorBreak(U,10.f,H);});
    return true;
}

int32 CireCrowdControl::VoidBurst(ACireHero* H,FVector Center,const FString& AbilityId)
{
    const FCireAbilityDef* D=CireAbilityDB::Find(AbilityId);
    if(!H||!H->HasAuthority()||!D||!D->Void.bValid)return 0;
    const FCireVoidZone& Z=D->Void;int32 Hits=0;
    CireCombat::PlayCue(H,nullptr,TEXT("void_rift"),Center,Center,ECireSpellCue::Impact,Z.OuterRadius/250.f,true);
    ForEachHostileNear(H,Center,Z.OuterRadius,[&](AActor* U,float Distance)
    {
        const auto Zone=Cires::CC::ClassifyVoidZone(Distance,Z.InnerRadius,Z.OuterRadius);
        if(Zone==Cires::CC::VoidZone::None)return;
        ++Hits;
        if(Z.Damage>0)CireCombat::ApplyDamage(H,U,Z.Damage+.3f*H->PrimaryAttribute(),D->Name+TEXT(" (void)")); // scaling-kits: + primary
        if(Zone==Cires::CC::VoidZone::Inner)Stun(U,Z.InnerDuration,H);
        else {Slow(U,Z.OuterDuration,H);CireBuffs::Apply(U,TEXT("slowed"),Z.OuterDuration,H);}
    });
    if(Z.SelfHealMaxHealthFraction>0)CireCombat::ApplyHealing(H,H,H->MaxHealth*Z.SelfHealMaxHealthFraction,D->Name);
    return Hits;
}

#if !UE_BUILD_SHIPPING
bool CireCrowdControl::RunSmoke(ACireGameMode* Mode)
{
    if(!IsValid(Mode)||!Mode->HasAuthority())return false;
    int32 Checks=0;bool bPass=true;TArray<AActor*> Actors;
    const auto Check=[&](bool b,const TCHAR* Why){++Checks;if(!b){bPass=false;UE_LOG(LogCireCC,Error,TEXT("CIRE_CC_FAIL %s"),Why);}};
    ON_SCOPE_EXIT{for(auto* A:Actors)if(IsValid(A))A->Destroy();};
    FActorSpawnParameters P;P.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    const FVector Origin(0,-2100,4800);
    const auto Hero=[&](const TCHAR* Profile,int32 Team,FVector Offset)
    {
        auto* H=Mode->GetWorld()->SpawnActor<ACireHero>(Origin+Offset,FRotator::ZeroRotator,P);
        if(H){Actors.Add(H);H->SetActorTickEnabled(false);H->SetActorEnableCollision(false);H->TeamId=Team;H->DraftProfile(Profile);H->Offers.Reset();H->CurrentOffer={};
            H->Health=H->MaxHealth=2000;H->Mana=H->MaxMana=2000;H->Energy=100;H->CriticalChance=0;Mode->Heroes.Add(H);}
        return H;
    };
    auto* A=Hero(TEXT("knight"),0,FVector::ZeroVector);auto* B=Hero(TEXT("scholar"),1,FVector(150,0,0));auto* C=Hero(TEXT("ranger"),1,FVector(330,0,0));
    if(!A||!B||!C){Check(false,TEXT("fixtures spawned"));return false;}
    // Stun / silence with PvP diminishing returns.
    Check(FMath::IsNearlyEqual(Stun(B,2.f,A),2.f)&&IsStunned(B),TEXT("first stun full"));
    Check(FMath::IsNearlyEqual(Stun(B,2.f,A),1.f),TEXT("second stun halved"));
    Check(FMath::IsNearlyEqual(Stun(B,2.f,A),.5f),TEXT("third stun quartered"));
    Check(FMath::IsNearlyEqual(Stun(B,2.f,A),0.f),TEXT("fourth stun immune"));
    Check(FMath::IsNearlyEqual(Silence(C,1.5f,A),1.5f)&&IsSilenced(C),TEXT("silence applies"));
    // Heal cut maths through the healing pipeline multiplier.
    HealCut(C,.5f,6.f,A);Check(FMath::IsNearlyEqual(HealingMultiplier(B,C),.5f),TEXT("healing received -50%"));
    HealCut(B,.5f,6.f,A,true);Check(FMath::IsNearlyEqual(HealingMultiplier(B,C),.25f),TEXT("done and received cuts multiply"));
    ArmorBreak(C,10.f,A);Check(FMath::IsNearlyEqual(ArmorMultiplier(C),.5f)&&FMath::IsNearlyEqual(ArmorMultiplier(B),1.f),TEXT("armor break halves armor"));
    // Timed heal: cast starts, an interrupt cancels it and locks the school.
    CireBuffs::ClearAll(B);State().DR.Remove(B);
    B->Skills={TEXT("restoring_light")};B->Cooldowns={0};B->GlobalCooldown=0;B->Target=B;
    Check(GateCast(B,0,TEXT("restoring_light"))&&IsCasting(B)&&FMath::IsNearlyEqual(B->CastEndTime-B->CastStartTime,1.5f),TEXT("heal starts a 1.5s cast"));
    Check(Interrupt(B,A,2.f)&&!IsCasting(B)&&CireBuffs::IsActive(B,LockedId),TEXT("interrupt cancels the cast and locks holy"));
    Check(GateCast(B,0,TEXT("restoring_light"))&&!IsCasting(B),TEXT("locked school cannot start a cast"));
    State().Lockouts.Remove(B);CireBuffs::Remove(B,LockedId);
    Check(GateCast(B,0,TEXT("restoring_light"))&&IsCasting(B),TEXT("cast restarts after lockout"));
    Stun(B,1.f,A);Check(!IsCasting(B),TEXT("stun cancels the cast"));
    CireBuffs::ClearAll(B);State().DR.Remove(B);Silence(B,1.f,A);
    Check(GateCast(B,0,TEXT("restoring_light"))&&!IsCasting(B),TEXT("silence blocks casting"));
    // Void rift: inner circle stuns, outer ring slows (positions from the DB radii).
    const FCireAbilityDef* Step=CireAbilityDB::Find(TEXT("shadow_step"));
    Check(Step&&Step->Void.bValid,TEXT("shadow step void data"));
    const auto Monster=[&](FVector At,bool bBoss)
    {
        auto* M=Mode->GetWorld()->SpawnActor<ACireMonster>(At,FRotator::ZeroRotator,P);
        if(M){Actors.Add(M);M->SetActorTickEnabled(false);M->SetActorEnableCollision(false);M->Lane=A->TeamId;M->Health=M->MaxHealth=5000;M->bBoss=bBoss;Mode->Monsters.Add(M);}
        return M;
    };
    const bool bSurvival=Mode->Clock.Phase()==Cires::MatchPhase::Survival;
    if(Step&&bSurvival)
    {
        const FVector Center=A->GetActorLocation()+FVector(0,600,0);
        auto* Inner=Monster(Center+FVector(Step->Void.InnerRadius*.5f,0,0),false);
        auto* Outer=Monster(Center+FVector((Step->Void.InnerRadius+Step->Void.OuterRadius)*.5f,0,0),false);
        auto* Far=Monster(Center+FVector(Step->Void.OuterRadius+200.f,0,0),false);
        if(Inner&&Outer&&Far)
        {
            const float T=Now(Mode->GetWorld());
            Check(VoidBurst(A,Center,TEXT("shadow_step"))==2,TEXT("void rift hits both zones only"));
            Check(IsStunned(Inner)&&!IsStunned(Outer)&&!IsStunned(Far),TEXT("inner circle stuns"));
            Check(Outer->SlowUntil>T&&Inner->SlowUntil<=T&&Far->SlowUntil<=T,TEXT("outer ring slows"));
            // Decimating Strike: kills an ordinary monster, bosses survive; nearby armor broken.
            auto* Boss=Monster(A->GetActorLocation()+FVector(150,0,0),true);
            auto* Near=Monster(A->GetActorLocation()+FVector(200,200,0),false);
            A->Skills={TEXT("decimating_strike")};A->Cooldowns={0};A->Energy=100;A->Target=Inner;Inner->SetActorLocation(A->GetActorLocation()+FVector(120,0,0));
            Check(CastSkill(A,0,TEXT("decimating_strike"))&&(!IsValid(Inner)||Inner->Health<=0),TEXT("decimating strike executes a monster"));
            Check(A->Cooldowns[0]>=180.f,TEXT("decimating strike long cooldown"));
            Check(Near&&CireBuffs::IsActive(Near,ArmorId),TEXT("nearby enemies lose armor"));
            A->Cooldowns[0]=0;A->Energy=100;A->Target=Boss;
            Check(Boss&&CastSkill(A,0,TEXT("decimating_strike"))&&Boss->Health>0&&Boss->Health<5000,TEXT("bosses take a normal hit"));
            Check(Stun(Boss,2.f,A)==0.f,TEXT("bosses are immune to stun"));
            // Executioner: ready basic attack kills; bosses keep the charge.
            A->Skills={TEXT("executioner")};TickHero(A,.1f);
            Check(IsExecutionerReady(A),TEXT("executioner ready when learned"));
            Check(FMath::IsNearlyEqual(ModifyOutgoingDamage(A,Boss,10.f,TEXT("Basic attack")),10.f)&&IsExecutionerReady(A),TEXT("boss hit keeps the charge"));
            Check(ModifyOutgoingDamage(A,Far,10.f,TEXT("Basic attack"))>=Far->Health&&!IsExecutionerReady(A),TEXT("ready basic attack is lethal and consumes"));
            Check(FMath::IsNearlyEqual(ModifyOutgoingDamage(A,Far,10.f,TEXT("Basic attack")),10.f),TEXT("second attack is normal"));
        }
        else Check(false,TEXT("monster fixtures spawned"));
    }
    for(auto* X:Actors)if(auto* M=Cast<ACireMonster>(X))Mode->Monsters.Remove(M);
    UE_LOG(LogCireCC,Display,TEXT("CIRE_CC_%s checks=%d"),bPass?TEXT("PASS"):TEXT("FAIL"),Checks);
    for(auto* X:{A,B,C})Mode->Heroes.Remove(X);
    State().Casts.Remove(B);State().DR.Reset();
    return bPass;
}
#endif
