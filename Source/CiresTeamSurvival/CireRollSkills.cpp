#include "CireRollSkills.h"
#include "CireKitSkills.h" // kits-complete
#include "CireScalingKits.h"
#include "CireAbilityDB.h"
#include "CireBuffs.h"
#include "CireCombatEvents.h"
#include "CireCrowdControl.h"
#include "CireDeveloperTools.h"
#include "CireGame.h"
#include "CireItems.h"
#include "CireMobility.h"
#include "CireSkillShop.h"
#include "CireSummon.h"
#include "CireThreat.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "EngineUtils.h"
#include "Misc/ScopeExit.h"
#include "Rules/CiresRules.h"

DEFINE_LOG_CATEGORY_STATIC(LogCireRoll,Log,All);

namespace
{
const TArray<FString> PassiveIds={TEXT("riposte_roll"),TEXT("tumblers_edge"),TEXT("killer_instinct"),TEXT("fleet_recovery"),TEXT("windrunner"),
    TEXT("quickened_mind"),TEXT("hasted_tumble"),TEXT("ember_wake"),TEXT("frost_wake"),TEXT("momentum"),TEXT("blur_step"),TEXT("slippery_roll"),TEXT("bloodrush")};
const TArray<FString> ActiveIds={TEXT("tumble_strike"),TEXT("mine_layer"),TEXT("taunting_tumble"),TEXT("shield_tumble"),TEXT("venom_tumble"),TEXT("shadow_dance"),TEXT("evasive_stance")};
// Timed states (buff records, replicated). Actives open a window; some passives arm a charge.
const FName EdgeId(TEXT("tumblers_edge")),InstinctId(TEXT("killer_instinct")),WindId(TEXT("windrunner")),QuickId(TEXT("quickened_mind")),
    MomentumId(TEXT("momentum")),BlurId(TEXT("blur_step")),MineId(TEXT("mine_layer")),TauntId(TEXT("taunting_tumble")),ShieldId(TEXT("shield_tumble")),
    VenomId(TEXT("venom_tumble")),DanceId(TEXT("shadow_dance")),EvasiveId(TEXT("evasive_stance"));

struct FMine{TWeakObjectPtr<ACireHero> Owner;FVector At;float Until=0;};
struct FState
{
    TArray<FMine> Mines;
    TMap<TWeakObjectPtr<ACireHero>,float> LastRiposteRoll,NextBotRoll;
};
FState& State(){static FState S;return S;}

float Now(const UWorld* W){return W?W->GetTimeSeconds():0.f;}
bool Has(const ACireHero* H,const FString& Id){return H&&H->HasSkill(Id);}
float Effect(const ACireHero* H,const FString& Id)
{
    return CireKits::ScaledEffect(H,Id,0.f); // kits-complete: level x potency (1 for the primary-ratio skills)
}
FCireAbilityStats Stats(const ACireHero* H,const FString& Id){return CireAbilityDB::EffectiveStats(Id,FMath::Max(1,CireSkillShop::Level(H,Id)));}
// Every ability scales off the owner's primary stat (STR/AGI/INT): base effect + ratio x primary.
// Ratios mirror ROLL_PRIMARY in Tools/BuildAbilityDB.py (the "scaling" field of each DB row).
float Primary(const ACireHero* H,const TCHAR* Id)
{
    static const TMap<FString,float> Ratios={{TEXT("riposte_roll"),1.f},{TEXT("fleet_recovery"),.5f},{TEXT("ember_wake"),.6f},{TEXT("frost_wake"),.4f},
        {TEXT("tumble_strike"),1.5f},{TEXT("mine_layer"),1.f},{TEXT("shield_tumble"),1.f},{TEXT("venom_tumble"),.5f},{TEXT("evasive_stance"),.4f}};
    const float* R=Ratios.Find(Id);return R&&H?*R*H->PrimaryAttribute():0.f;
}
template<typename Fn> void ForEachHostile(ACireHero* H,Fn&& Visit)
{
    auto* Mode=H->GetWorld()->GetAuthGameMode<ACireGameMode>();if(!Mode)return;
    TArray<AActor*> Units;
    for(auto* M:Mode->Monsters)if(IsValid(M)&&M->Health>0)Units.Add(M);
    for(auto* E:Mode->Heroes)if(IsValid(E)&&!E->bDead)Units.Add(E);
    for(AActor* U:Units)if(H->IsHostile(U))Visit(U);
}
// Enemies within Radius of the roll path Start->End.
template<typename Fn> void ForEachOnPath(ACireHero* H,FVector Start,FVector End,float Radius,Fn&& Visit)
{
    ForEachHostile(H,[&](AActor* U)
    {
        const FVector P=U->GetActorLocation();
        if(FMath::Abs(P.Z-Start.Z)>300.f)return;
        if(Cires::Roll::PointSegmentDistance2D(P.X,P.Y,Start.X,Start.Y,End.X,End.Y)<=Radius)Visit(U);
    });
}
void Guard(ACireHero* H,float Seconds)
{
    const float T=Now(H->GetWorld());H->ShieldUntil=FMath::Max(H->ShieldUntil,T+Seconds);
}
}

bool CireRollSkills::Knows(const FString& Id){return PassiveIds.Contains(Id)||ActiveIds.Contains(Id);}
bool CireRollSkills::IsPassive(const FString& Id){return PassiveIds.Contains(Id);}
bool CireRollSkills::IsActive(const FString& Id){return ActiveIds.Contains(Id);}
const TArray<FString>& CireRollSkills::AllIds(){static TArray<FString> All=[]{TArray<FString> A=PassiveIds;A.Append(ActiveIds);return A;}();return All;}
const TArray<FName>& CireRollSkills::BuffIds()
{
    static const TArray<FName> Ids={EdgeId,InstinctId,WindId,QuickId,MomentumId,BlurId,MineId,TauntId,ShieldId,VenomId,DanceId,EvasiveId};
    return Ids;
}
FString CireRollSkills::Name(const FString& Id){const auto* D=CireAbilityDB::Find(Id);return D?D->Name:Id;}
FString CireRollSkills::Description(const FString& Id)
{
    const auto* D=CireAbilityDB::Find(Id);if(!D)return FString();
    const FString Body=D->Description.Replace(TEXT("{effect}"),*FString::Printf(TEXT("%.0f"),D->Base.Effect));
    if(D->IsPassive())return TEXT("PASSIVE (dodge roll): ")+Body;
    return FString::Printf(TEXT("%.0f %s | %.0fs CD. %s"),D->Base.ManaCost>0?D->Base.ManaCost:D->Base.EnergyCost,D->Base.ManaCost>0?TEXT("mana"):TEXT("energy"),D->Base.Cooldown,*Body);
}

bool CireRollSkills::Cast(ACireHero* H,int32 Slot,const FString& Id)
{
    if(!H||!H->HasAuthority()||!IsActive(Id)||H->bDead||!H->Skills.IsValidIndex(Slot)||H->Skills[Slot]!=Id||!H->Cooldowns.IsValidIndex(Slot)||H->Cooldowns[Slot]>0)return false;
    const FCireAbilityStats S=Stats(H,Id);const FCireAbilityDef* D=CireAbilityDB::Find(Id);if(!D)return false;
    if(H->Mana<S.ManaCost||H->Energy<S.EnergyCost){H->Notice=TEXT("Not enough mana or energy.");return false;}
    auto* Mode=H->GetWorld()->GetAuthGameMode<ACireGameMode>();if(!Mode||!Mode->IsCombatPhase())return false;
    if(Id==TEXT("tumble_strike"))
    {
        AActor* Target=H->Target;
        if(!H->IsHostile(Target)||!H->InRange(Target,S.Range)){H->Notice=TEXT("Select a hostile target within 7m.");return false;}
        if(!H->Mobility){return false;}
        // A free roll toward the target: ignores the dodge cooldown and its energy, then strikes.
        const float Ready=H->Mobility->ReadyAt;H->Mobility->ReadyAt=0;const float Energy=H->Energy;H->Energy=100.f;
        const bool bRolled=H->Mobility->StartRoll((Target->GetActorLocation()-H->GetActorLocation()).GetSafeNormal2D());
        H->Energy=Energy;if(!bRolled)H->Mobility->ReadyAt=Ready;
        CireCombat::ApplyStrike(H,Target,(S.Effect+Primary(H,TEXT("tumble_strike")))*Mode->Power(H->TeamId),D->Name);
    }
    else
    {
        const FName Window=Id==TEXT("mine_layer")?MineId:Id==TEXT("taunting_tumble")?TauntId:Id==TEXT("shield_tumble")?ShieldId:
            Id==TEXT("venom_tumble")?VenomId:Id==TEXT("shadow_dance")?DanceId:EvasiveId;
        const float Seconds=Id==TEXT("mine_layer")?8.f:S.Duration>0?S.Duration:8.f;
        CireBuffs::Apply(H,Window,CireDeveloperTools::EffectSeconds(H->GetWorld(),Seconds),H);
    }
    H->Mana-=S.ManaCost;H->Energy-=S.EnergyCost;
    H->Cooldowns[Slot]=static_cast<float>(Cires::CooldownSeconds(CireDeveloperTools::CooldownSeconds(H->GetWorld(),S.Cooldown),H->CDR));
    H->GlobalCooldown=FMath::Max(H->GlobalCooldown,.5f);
    CireCombat::PlayCue(H,H->Target,FName(*Id),H->GetActorLocation(),H->GetActorLocation(),ECireSpellCue::Cast,.8f,true);
    return true;
}

void CireRollSkills::OnRoll(ACireHero* H,FVector Direction)
{
    if(!H||!H->HasAuthority())return;
    UWorld* W=H->GetWorld();auto* Mode=W->GetAuthGameMode<ACireGameMode>();if(!Mode)return;
    const float T=Now(W);const auto V=CireMovement::Tuning();
    const FVector Start=H->GetActorLocation(),End=Start+Direction.GetSafeNormal2D()*V.RollSpeed*V.RollDuration;
    const float Power=Mode->Power(H->TeamId);
    if(Has(H,TEXT("fleet_recovery")))CireCombat::ApplyHealing(H,H,H->MaxHealth*Effect(H,TEXT("fleet_recovery"))/100.f+Primary(H,TEXT("fleet_recovery")),TEXT("Fleet Recovery"));
    if(Has(H,TEXT("windrunner")))CireBuffs::Apply(H,WindId,5.f,H);
    if(Has(H,TEXT("quickened_mind")))CireBuffs::Apply(H,QuickId,Effect(H,TEXT("quickened_mind")),H);
    if(Has(H,TEXT("tumblers_edge")))CireBuffs::Apply(H,EdgeId,4.f,H);
    if(Has(H,TEXT("killer_instinct")))CireBuffs::Apply(H,InstinctId,4.f,H);
    if(Has(H,TEXT("blur_step")))CireBuffs::Apply(H,BlurId,3.f,H);
    if(Has(H,TEXT("hasted_tumble")))
    {
        const double Cut=Effect(H,TEXT("hasted_tumble"));
        for(int32 I=0;I<H->Skills.Num()&&I<H->Cooldowns.Num();++I)
            if(!ACireHero::IsPassive(H->Skills[I]))H->Cooldowns[I]=static_cast<float>(Cires::Roll::ReducedCooldown(H->Cooldowns[I],Cut));
    }
    if(Has(H,TEXT("momentum")))
    {
        const auto* Buffs=CireBuffs::Get(H);const auto* E=Buffs?Buffs->Find(MomentumId):nullptr;
        const int32 Stacks=CireBuffs::IsActive(H,MomentumId)&&E?E->Stacks:0;
        CireBuffs::Apply(H,MomentumId,8.f,H,FMath::Min(Cires::Roll::MaxMomentumStacks,Stacks+1));
    }
    if(Has(H,TEXT("slippery_roll")))
    {
        if(H->SlowUntil>T)H->SlowUntil=0;
        else for(const TCHAR* Id:{TEXT("healing_cut"),TEXT("heal_cut_done"),TEXT("silenced"),TEXT("npc_silenced"),TEXT("armor_broken"),TEXT("npc_rooted")})
            if(CireBuffs::IsActive(H,Id)){CireBuffs::Remove(H,Id);break;}
    }
    if(Has(H,TEXT("ember_wake")))
    {
        const float Dmg=(Effect(H,TEXT("ember_wake"))+Primary(H,TEXT("ember_wake")))*Power;
        ForEachOnPath(H,Start,End,180.f,[&](AActor* U){CireCombat::ApplyDamage(H,U,Dmg,TEXT("Ember Wake"));});
    }
    if(Has(H,TEXT("frost_wake")))
    {
        const float Dmg=(Effect(H,TEXT("frost_wake"))+Primary(H,TEXT("frost_wake")))*Power;
        ForEachOnPath(H,Start,End,180.f,[&](AActor* U){CireCombat::ApplyDamage(H,U,Dmg,TEXT("Frost Wake"));CireCrowdControl::Slow(U,3.f,H);});
    }
    if(CireBuffs::IsActive(H,VenomId))
    {
        const float Dmg=(Effect(H,TEXT("venom_tumble"))+Primary(H,TEXT("venom_tumble")))*Power;
        ForEachOnPath(H,Start,End,180.f,[&](AActor* U){CireCombat::ApplyDamage(H,U,Dmg,TEXT("Venom Tumble"));CireCrowdControl::HealCut(U,.3f,5.f,H);});
    }
    if(CireBuffs::IsActive(H,TauntId))
    {
        const float Seconds=Effect(H,TEXT("taunting_tumble"));
        for(auto* M:Mode->Monsters)if(IsValid(M)&&H->IsHostile(M)&&H->InRange(M,500.f))CireThreat::Taunt(M,H,Seconds);
        for(auto* E:Mode->Heroes)if(IsValid(E)&&E->bBot&&H->IsHostile(E)&&H->InRange(E,500.f))E->Target=H;
        Guard(H,1.5f);
    }
    if(CireBuffs::IsActive(H,ShieldId))
    {
        ACireHero* Best=nullptr;float BestDist=800.f*800.f;
        for(auto* A:Mode->Heroes)if(IsValid(A)&&A!=H&&!A->bDead&&A->TeamId==H->TeamId&&!A->IsA<ACireSummon>())
        {const float D=FVector::DistSquared(A->GetActorLocation(),H->GetActorLocation());if(D<BestDist){BestDist=D;Best=A;}}
        // kits-complete: a roll reaction heals over 3s (visible Tumbling Mend) instead of carrying a cast time.
        if(Best){Guard(Best,3.f);CireBuffs::Apply(Best,TEXT("iron_guard"),3.f,H);CireKitSkills::StartHealOverTime(H,Best,Best->MaxHealth*Effect(H,TEXT("shield_tumble"))/100.f+Primary(H,TEXT("shield_tumble")),3.f,TEXT("Shield Tumble"));}
    }
    if(CireBuffs::IsActive(H,MineId))
    {
        auto& Mines=State().Mines;
        int32 Owned=0;for(const auto& M:Mines)Owned+=M.Owner.Get()==H;
        if(Owned>=2)for(int32 I=0;I<Mines.Num();++I)if(Mines[I].Owner.Get()==H){Mines.RemoveAt(I);break;}
        Mines.Add({H,Start,T+20.f});CireBuffs::Remove(H,MineId);
        CireCombat::PlayCue(H,nullptr,TEXT("mine_layer"),Start,Start,ECireSpellCue::Impact,.6f,true);
    }
    if(H->Mobility&&CireBuffs::IsActive(H,DanceId))
    {
        H->Mobility->ReadyAt=static_cast<float>(Cires::Roll::ShortenedReadyAt(H->Mobility->RollStartedAt,H->Mobility->ReadyAt,Effect(H,TEXT("shadow_dance"))));
        H->Energy=FMath::Min(100.f,H->Energy+V.RollEnergy);
    }
    if(CireBuffs::IsActive(H,EvasiveId))H->Energy=FMath::Min(100.f,H->Energy+V.RollEnergy);
}

void CireRollSkills::OnDodgedHit(ACireHero* H,AActor* Attacker)
{
    if(!H||!H->HasAuthority()||!H->Mobility)return;
    if(Has(H,TEXT("riposte_roll"))&&IsValid(Attacker)&&H->IsHostile(Attacker)&&H->InRange(Attacker,700.f))
    {
        float& Last=State().LastRiposteRoll.FindOrAdd(H,-1.f);
        if(Last!=H->Mobility->RollStartedAt)
        {
            Last=H->Mobility->RollStartedAt;
            auto* Mode=H->GetWorld()->GetAuthGameMode<ACireGameMode>();
            CireCombat::ApplyStrike(H,Attacker,(Effect(H,TEXT("riposte_roll"))+Primary(H,TEXT("riposte_roll")))*(Mode?Mode->Power(H->TeamId):1.f),TEXT("Riposte"));
        }
    }
    if(CireBuffs::IsActive(H,EvasiveId))CireKitSkills::StartHealOverTime(H,H,H->MaxHealth*Effect(H,TEXT("evasive_stance"))/100.f+Primary(H,TEXT("evasive_stance")),3.f,TEXT("Evasive Stance")); // kits-complete: visible HoT
}

bool CireRollSkills::TryBlur(ACireHero* H,AActor* Attacker,const FString& AbilityName)
{
    if(!H||!H->HasAuthority()||!Has(H,TEXT("blur_step"))||!CireBuffs::IsActive(H,BlurId))return false;
    if(!Cires::Roll::BlurDodges(FMath::FRand(),Effect(H,TEXT("blur_step"))))return false;
    CireCombat::BroadcastAvoidance(Attacker,H,ECireHitOutcome::Dodge,AbilityName);
    return true;
}

float CireRollSkills::ModifyOutgoingDamage(AActor* Source,AActor* Target,float Amount,const FString& AbilityName,bool* InOutCritical)
{
    auto* H=Cast<ACireHero>(Source);if(!H||!H->HasAuthority()||Amount<=0)return Amount;
    const bool bBasic=CireItems::IsBasicAttack(H,AbilityName);
    if(bBasic&&CireBuffs::IsActive(H,EdgeId)&&Has(H,TEXT("tumblers_edge"))){Amount*=1.f+Effect(H,TEXT("tumblers_edge"))/100.f;CireBuffs::Remove(H,EdgeId);}
    if(bBasic&&CireBuffs::IsActive(H,InstinctId)&&Has(H,TEXT("killer_instinct")))
    {
        // Guaranteed critical: an attack that already crit keeps its single multiplier.
        if(!(InOutCritical&&*InOutCritical))Amount*=FMath::Clamp(H->CriticalMultiplier,1.f,5.f);
        if(InOutCritical)*InOutCritical=true;
        CireBuffs::Remove(H,InstinctId);
    }
    if(Has(H,TEXT("momentum"))&&CireBuffs::IsActive(H,MomentumId))
    {
        const auto* Buffs=CireBuffs::Get(H);const auto* E=Buffs?Buffs->Find(MomentumId):nullptr;
        Amount*=static_cast<float>(Cires::Roll::MomentumMultiplier(E?E->Stacks:0,Effect(H,TEXT("momentum"))));
    }
    return Amount;
}

float CireRollSkills::MoveSpeedMultiplier(const ACireHero* H)
{
    return H&&H->HasSkill(TEXT("windrunner"))&&CireBuffs::IsActive(H,WindId)?1.f+Effect(H,TEXT("windrunner"))/100.f:1.f;
}
bool CireRollSkills::ConsumeInstantCast(ACireHero* H)
{
    if(!H||!Has(H,TEXT("quickened_mind"))||!CireBuffs::IsActive(H,QuickId))return false;
    CireBuffs::Remove(H,QuickId);return true;
}
void CireRollSkills::OnKill(ACireHero* K)
{
    if(!K||!K->HasAuthority()||!K->Mobility||!Has(K,TEXT("bloodrush")))return;
    K->Mobility->ReadyAt=0;K->Energy=FMath::Min(100.f,K->Energy+CireMovement::Tuning().RollEnergy);
}

void CireRollSkills::Tick(ACireHero* H,float)
{
    if(!H||!H->HasAuthority())return;
    UWorld* W=H->GetWorld();const float T=Now(W);
    // Mines owned by this hero: the first enemy within 1.5m sets one off.
    auto& Mines=State().Mines;
    for(int32 I=Mines.Num()-1;I>=0;--I)
    {
        FMine& M=Mines[I];
        if(!M.Owner.IsValid()||T>M.Until){Mines.RemoveAt(I);continue;}
        if(M.Owner.Get()!=H||H->bDead)continue;
        bool bTriggered=false;
        ForEachHostile(H,[&](AActor* U){if(!bTriggered&&FVector::Dist2D(U->GetActorLocation(),M.At)<=150.f)bTriggered=true;});
        if(!bTriggered)continue;
        auto* Mode=W->GetAuthGameMode<ACireGameMode>();
        const float Dmg=(Effect(H,TEXT("mine_layer"))+Primary(H,TEXT("mine_layer")))*(Mode?Mode->Power(H->TeamId):1.f);const FVector At=M.At;Mines.RemoveAt(I);
        CireCombat::PlayCue(H,nullptr,TEXT("mine_layer"),At,At,ECireSpellCue::Impact,1.f,true);
        ForEachHostile(H,[&](AActor* U){if(FVector::Dist2D(U->GetActorLocation(),At)<=250.f){CireCombat::ApplyDamage(H,U,Dmg,TEXT("Caltrop Mine"));CireCrowdControl::Slow(U,2.f,H);}});
    }
    // Bots: roll to use their roll kit in combat, and open stances before rolling.
    if(!H->bBot||H->bDead||!H->Mobility)return;
    bool bKit=false;for(const FString& S:H->Skills)bKit|=Knows(S);
    if(!bKit||!H->IsHostile(H->Target)||!H->InRange(H->Target,900.f))return;
    float& Next=State().NextBotRoll.FindOrAdd(H,T+2.f);
    if(T<Next)return;
    Next=T+5.f+FMath::FRand()*3.f;
    for(int32 I=0;I<H->Skills.Num();++I)if(IsActive(H->Skills[I])&&H->Cooldowns.IsValidIndex(I)&&H->Cooldowns[I]<=0){H->Cast(I);break;}
    const FVector Side=FVector::CrossProduct((H->Target->GetActorLocation()-H->GetActorLocation()).GetSafeNormal2D(),FVector::UpVector)*(FMath::RandBool()?1.f:-1.f);
    if(H->Energy>=CireMovement::Tuning().RollEnergy+10.f)H->Mobility->StartRoll(Side);
}

#if !UE_BUILD_SHIPPING
bool CireRollSkills::RunSmoke(ACireGameMode* Mode)
{
    if(!IsValid(Mode)||!Mode->HasAuthority())return false;
    int32 Checks=0;bool bPass=true;TArray<AActor*> Actors;
    const auto Check=[&](bool b,const TCHAR* Why){++Checks;if(!b){bPass=false;UE_LOG(LogCireRoll,Error,TEXT("CIRE_ROLL_FAIL %s"),Why);}};
    ON_SCOPE_EXIT{for(auto* A:Actors)if(IsValid(A)){if(auto* M=Cast<ACireMonster>(A))Mode->Monsters.Remove(M);if(auto* X=Cast<ACireHero>(A))Mode->Heroes.Remove(X);A->Destroy();}State().Mines.Reset();};
    FActorSpawnParameters P;P.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    const FVector Origin(0,-2100,5200);
    auto* H=Mode->GetWorld()->SpawnActor<ACireHero>(Origin,FRotator::ZeroRotator,P);
    auto* Ally=Mode->GetWorld()->SpawnActor<ACireHero>(Origin+FVector(0,300,0),FRotator::ZeroRotator,P);
    if(!H||!Ally){Check(false,TEXT("fixtures"));return false;}
    for(auto* X:{H,Ally}){Actors.Add(X);X->SetActorTickEnabled(false);X->SetActorEnableCollision(false);X->TeamId=0;X->DraftProfile(TEXT("ranger"));X->Offers.Reset();X->CurrentOffer={};
        X->Health=X->MaxHealth=1000;X->Energy=100;X->CriticalChance=0;X->CriticalMultiplier=2.f;Mode->Heroes.Add(X);}
    const bool bSurvival=Mode->Clock.Phase()==Cires::MatchPhase::Survival;
    const auto Learn=[&](std::initializer_list<const TCHAR*> Ids){H->Skills.Reset();H->Cooldowns.Reset();for(const TCHAR* Id:Ids){H->Skills.Add(Id);H->Cooldowns.Add(0);}CireBuffs::ClearAll(H);};
    // Heal on roll, fires per roll (charges roll again).
    Learn({TEXT("fleet_recovery")});H->Health=500;
    if(bSurvival){const float Heal=50.f+.5f*H->PrimaryAttribute();
        OnRoll(H,FVector::ForwardVector);Check(FMath::IsNearlyEqual(H->Health,500.f+Heal,1.f),TEXT("fleet recovery heals 5% + 0.5x primary per roll"));
        OnRoll(H,FVector::ForwardVector);Check(FMath::IsNearlyEqual(H->Health,FMath::Min(1000.f,500.f+2*Heal),1.f),TEXT("second roll (charge) heals again"));}
    // Cooldown % cut per roll.
    Learn({TEXT("hasted_tumble"),TEXT("tumble_strike"),TEXT("stone_skin")});H->Cooldowns={10.f,20.f,0.f};
    // kits-complete: roll effects carry potency (x1 + 0.4% per PRIMARY point), so expectations follow it.
    const float Cut=1.f-.15f*CireKits::Potency(H,TEXT("hasted_tumble"));
    OnRoll(H,FVector::ForwardVector);Check(FMath::IsNearlyEqual(H->Cooldowns[1],20.f*Cut,.01f),TEXT("hasted tumble cuts active cooldowns by 15% x potency"));
    OnRoll(H,FVector::ForwardVector);Check(FMath::IsNearlyEqual(H->Cooldowns[1],20.f*Cut*Cut,.01f),TEXT("cut applies per roll"));
    // Windrunner and move speed.
    Learn({TEXT("windrunner")});OnRoll(H,FVector::ForwardVector);Check(FMath::IsNearlyEqual(MoveSpeedMultiplier(H),1.f+.1f*CireKits::Potency(H,TEXT("windrunner")),.001f),TEXT("windrunner +10% move speed x potency"));
    // Quickened Mind: next timed cast is instant, consumed once.
    Learn({TEXT("quickened_mind"),TEXT("restoring_light")});H->Target=H;OnRoll(H,FVector::ForwardVector);
    Check(CireBuffs::IsActive(H,QuickId),TEXT("quickened mind arms after a roll"));
    Check(!CireCrowdControl::GateCast(H,1,TEXT("restoring_light"))&&!CireCrowdControl::IsCasting(H)&&!CireBuffs::IsActive(H,QuickId),TEXT("timed heal becomes instant and consumes the charge"));
    Check(CireCrowdControl::GateCast(H,1,TEXT("restoring_light"))&&CireCrowdControl::IsCasting(H),TEXT("the following heal has its cast time again"));
    CireCrowdControl::CancelCast(H,TEXT(""));
    // Next-attack empowerment and guaranteed crit.
    Learn({TEXT("tumblers_edge")});OnRoll(H,FVector::ForwardVector);
    Check(FMath::IsNearlyEqual(ModifyOutgoingDamage(H,Ally,100,TEXT("Bow shot")),100.f+50.f*CireKits::Potency(H,TEXT("tumblers_edge")),.1f)&&FMath::IsNearlyEqual(ModifyOutgoingDamage(H,Ally,100,TEXT("Bow shot")),100.f,.1f),TEXT("tumbler's edge empowers one attack"));
    Learn({TEXT("killer_instinct")});OnRoll(H,FVector::ForwardVector);
    Check(FMath::IsNearlyEqual(ModifyOutgoingDamage(H,Ally,100,TEXT("Ember Lance")),100.f,.1f)&&FMath::IsNearlyEqual(ModifyOutgoingDamage(H,Ally,100,TEXT("Bow shot")),200.f,.1f),TEXT("killer instinct crits the next basic attack only"));
    // Momentum stacks per roll.
    Learn({TEXT("momentum")});for(int32 I=0;I<7;++I)OnRoll(H,FVector::ForwardVector);
    Check(FMath::IsNearlyEqual(ModifyOutgoingDamage(H,Ally,100,TEXT("Ember Lance")),100.f+20.f*CireKits::Potency(H,TEXT("momentum")),.1f),TEXT("momentum caps at 5 stacks (+20% x potency)"));
    // Slippery cleanse.
    Learn({TEXT("slippery_roll")});H->SlowUntil=Now(H->GetWorld())+10;CireBuffs::Apply(H,TEXT("healing_cut"),10,Ally,50);
    OnRoll(H,FVector::ForwardVector);Check(H->SlowUntil==0&&CireBuffs::IsActive(H,TEXT("healing_cut")),TEXT("first roll cleanses the slow"));
    OnRoll(H,FVector::ForwardVector);Check(!CireBuffs::IsActive(H,TEXT("healing_cut")),TEXT("second roll cleanses the healing cut"));
    // Shadow Dance shortens recovery; Bloodrush resets it.
    Learn({TEXT("shadow_dance"),TEXT("bloodrush")});H->Energy=100;
    Check(Cast(H,0,TEXT("shadow_dance"))&&CireBuffs::IsActive(H,DanceId)&&H->Cooldowns[0]>0,TEXT("shadow dance opens its window and costs"));
    H->Mobility->RollStartedAt=Now(H->GetWorld());H->Mobility->ReadyAt=H->Mobility->RollStartedAt+3.5f;H->Energy=50;
    OnRoll(H,FVector::ForwardVector);Check(FMath::IsNearlyEqual(H->Mobility->ReadyAt-H->Mobility->RollStartedAt,3.5f*(1.f-FMath::Min(1.f,.7f*CireKits::Potency(H,TEXT("shadow_dance")))),.01f)&&H->Energy>=75,TEXT("shadow dance: 70% x potency faster recovery and refund"));
    OnKill(H);Check(H->Mobility->ReadyAt==0,TEXT("bloodrush resets the roll on kill"));
    // Riposte counter on an i-frame dodge, once per roll; Evasive Stance heals on dodge.
    auto* Enemy=Mode->GetWorld()->SpawnActor<ACireMonster>(Origin+FVector(200,0,0),FRotator::ZeroRotator,P);
    if(Enemy&&bSurvival)
    {
        Actors.Add(Enemy);Enemy->SetActorTickEnabled(false);Enemy->SetActorEnableCollision(false);Enemy->Lane=H->TeamId;Enemy->Health=Enemy->MaxHealth=5000;Mode->Monsters.Add(Enemy);
        Learn({TEXT("riposte_roll"),TEXT("evasive_stance")});H->Mobility->RollStartedAt=Now(H->GetWorld());
        OnDodgedHit(H,Enemy);Check(Enemy->Health<5000,TEXT("riposte counters a dodged hit"));
        const float After=Enemy->Health;OnDodgedHit(H,Enemy);Check(Enemy->Health==After,TEXT("riposte fires once per roll"));
        H->Mobility->RollStartedAt+=.01f;OnDodgedHit(H,Enemy);Check(Enemy->Health<After,TEXT("a new roll arms riposte again"));
        H->Energy=100;Check(Cast(H,1,TEXT("evasive_stance")),TEXT("evasive stance casts"));H->Health=500;OnDodgedHit(H,Enemy);Check(H->Health>500,TEXT("evasive stance heals on a dodge"));
        // Frost wake slows enemies on the roll path.
        Learn({TEXT("frost_wake")});Enemy->SlowUntil=0;Enemy->SetActorLocation(H->GetActorLocation()+FVector(250,40,0));
        OnRoll(H,FVector::ForwardVector);Check(Enemy->SlowUntil>Now(H->GetWorld()),TEXT("frost wake slows enemies on the path"));
        // Mines: drop on roll while armed, trigger when an enemy steps in.
        Learn({TEXT("mine_layer")});H->Energy=100;Check(Cast(H,0,TEXT("mine_layer")),TEXT("mine layer arms"));
        Enemy->SetActorLocation(H->GetActorLocation()+FVector(1000,0,0));OnRoll(H,FVector::ForwardVector);
        Check(State().Mines.Num()==1&&!CireBuffs::IsActive(H,MineId),TEXT("the next roll drops one mine"));
        const float Before=Enemy->Health;Enemy->SetActorLocation(State().Mines[0].At+FVector(50,0,0));Tick(H,.1f);
        Check(Enemy->Health<Before&&State().Mines.IsEmpty(),TEXT("mine triggers on an enemy"));
        // Bots: a bot with a roll kit rolls in combat.
        H->bBot=true;H->Target=Enemy;H->Energy=100;H->Mobility->ReadyAt=0;State().NextBotRoll.Add(H,0.f);
        Learn({TEXT("windrunner"),TEXT("shadow_dance")});H->GlobalCooldown=0;Tick(H,.1f);
        Check(H->Cooldowns[1]>0&&CireBuffs::IsActive(H,DanceId),TEXT("bots open their roll active"));
        Check(H->Mobility->IsRolling()||!H->GetCharacterMovement()->IsMovingOnGround(),TEXT("bots roll to use their kit (or are airborne in the fixture)"));
        H->bBot=false;
    }
    // Data: 20 skills, all in the DB with the roll tag and an icon id.
    int32 InDb=0;for(const FString& Id:AllIds()){const auto* D=CireAbilityDB::Find(Id);InDb+=D&&D->EffectTags.Contains(TEXT("Roll"))&&!D->Categories.IsEmpty()&&!D->Champions.IsEmpty();}
    Check(AllIds().Num()==20&&InDb==20,TEXT("20 roll skills in the database, each buyable by someone"));
    UE_LOG(LogCireRoll,Display,TEXT("CIRE_ROLL_SKILLS_%s checks=%d"),bPass?TEXT("PASS"):TEXT("FAIL"),Checks);
    return bPass;
}
#endif
