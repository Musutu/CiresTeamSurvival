#include "CireThreat.h"
#include "CireGame.h"
#include "CireNPCState.h"
#include "CireSkillTuning.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "CireBuffs.h" // aura-vfx
#include "CirePets.h" // pets: owner threat share

namespace {
const FCireNPCThreatRules& Rules(){return CireNPCArchetypes::Get().Threat;}
bool Eligible(const ACireMonster* M,const ACireHero* H) {
    return IsValid(M)&&!M->bArmoredEscort&&M->Health>0&&IsValid(H)&&!H->bDead&&H->bDrafted&&H->TeamId==M->Lane&&
        FVector::DistSquared2D(M->GetActorLocation(),H->GetActorLocation())<=FMath::Square(2200.f);
}
// Holding threat has no range limit: threat is only lost when a unit dies or an ability explicitly drops it.
bool CanHold(const ACireMonster* M,const ACireHero* H) {
    return IsValid(M)&&!M->bArmoredEscort&&M->Health>0&&IsValid(H)&&!H->bDead&&H->bDrafted&&H->TeamId==M->Lane;
}
void Touch(ACireMonster* M,ACireHero* H){
    if(M->NPCState&&M->GetWorld())M->NPCState->LastThreatAt.FindOrAdd(H)=M->GetWorld()->GetTimeSeconds();
}
void Add(ACireMonster* M,ACireHero* H,float Amount){
    if(!M||!M->HasAuthority()||!Eligible(M,H)||!FMath::IsFinite(Amount)||Amount<=0)return;
    float& Threat=M->Threat.FindOrAdd(H);Threat=FMath::Min(1.e9f,Threat+Amount);M->bEngaged=true;Touch(M,H);
}
float Highest(const ACireMonster* M){float Top=0;for(const auto& Pair:M->Threat)Top=FMath::Max(Top,Pair.Value);return Top;}
}
void CireThreat::Engage(ACireMonster* M,ACireHero* H){Add(M,H,1.f);}
void CireThreat::AddRaw(ACireMonster* M,ACireHero* H,float Amount){Add(M,H,Amount);Select(M);}
void CireThreat::Damage(ACireMonster* M,ACireHero* H,float Amount){
    if(!H)return;
    float Threat=Amount*H->DamageThreatMultiplier();
    Threat-=CirePets::ShareOwnerThreat(M,H,Threat); // pets: part of the owner's threat lands on its engaged companion
    Add(M,H,Threat);
    Select(M);
}
void CireThreat::Healing(ACireHero* Source,ACireHero* Target,float Amount){
    if(!IsValid(Source)||!Source->HasAuthority()||!IsValid(Target)||!FMath::IsFinite(Amount)||Amount<=0)return;
    auto* Mode=Source->GetWorld()->GetAuthGameMode<ACireGameMode>();
    if(!Mode||Mode->Clock.Phase()!=Cires::MatchPhase::Survival)return;
    TArray<ACireMonster*> Engaged;
    for(auto* M:Mode->Monsters)if(Eligible(M,Source)&&M->bEngaged&&!M->Threat.IsEmpty()&&
        FVector::DistSquared2D(M->GetActorLocation(),Target->GetActorLocation())<=FMath::Square(2200.f))Engaged.Add(M);
    if(Engaged.IsEmpty())return;
    const float PerEnemy=Amount*CireSkillTuning::Get().HealingThreatMultiplier/Engaged.Num();
    for(auto* M:Engaged){Add(M,Source,PerEnemy);Select(M);}
}
void CireThreat::Taunt(ACireMonster* M,ACireHero* H,float Seconds){
    if(!M||!M->HasAuthority()||!Eligible(M,H)||!FMath::IsFinite(Seconds)||Seconds<=0)return;
    // WoW taunt: match the current top threat and force the target for the duration.
    M->Threat.FindOrAdd(H)=FMath::Max(M->Threat.FindRef(H),Highest(M)+1.f);Touch(M,H);
    ACireHero* Old=M->Victim;
    M->ForcedVictim=H;M->ForcedVictimUntil=M->GetWorld()->GetTimeSeconds()+FMath::Min(Seconds,Rules().TauntMaxSeconds);
    M->Victim=H;M->bEngaged=true;M->ForceNetUpdate();
    CireBuffs::Apply(M,TEXT("taunted"),M->ForcedVictimUntil-M->GetWorld()->GetTimeSeconds(),H); // aura-vfx: overhead mark + tether to the taunter
    if(M->NPCState){M->NPCState->bForcedLastSelect=true;if(Old!=H)M->NPCState->SetAggro(H,Old,ECireAggroReason::Taunted);else M->NPCState->PublishThreat(true);}
}
void CireThreat::Transfer(ACireMonster* M,ACireHero* From,ACireHero* To,float Fraction){
    if(!M||!M->HasAuthority()||!From||!FMath::IsFinite(Fraction))return;
    float* Source=M->Threat.Find(From);if(!Source||*Source<=0)return;
    const float Moved=*Source*FMath::Clamp(Fraction,0.f,1.f);*Source-=Moved;
    if(Eligible(M,To))Add(M,To,Moved);
    Select(M);
}
void CireThreat::Scale(ACireMonster* M,ACireHero* H,float Multiplier){
    if(!M||!M->HasAuthority()||!FMath::IsFinite(Multiplier)||Multiplier<0)return;
    if(float* Value=M->Threat.Find(H))*Value=FMath::Min(1.e9f,*Value*Multiplier);
    Select(M);
}
void CireThreat::ScaleAll(ACireHero* H,float Multiplier){
    if(!H||!H->HasAuthority()||!H->GetWorld()||!FMath::IsFinite(Multiplier)||Multiplier<0)return;
    for(TActorIterator<ACireMonster> It(H->GetWorld());It;++It)
    {
        ACireMonster* M=*It;float* Value=M->Threat.Find(H);if(!Value)continue;
        if(Multiplier<=0.f){M->Threat.Remove(H);if(M->ForcedVictim==H)M->ForcedVictim.Reset();}
        else *Value=FMath::Min(1.e9f,*Value*Multiplier);
        Select(M);
    }
}
float CireThreat::PullRatio(const ACireMonster* M,const ACireHero* H){
    if(!M||!H)return Rules().RangedPullRatio;
    return FVector::DistSquared2D(M->GetActorLocation(),H->GetActorLocation())<=FMath::Square(Rules().MeleeRangeCm)?Rules().MeleePullRatio:Rules().RangedPullRatio;
}
void CireThreat::Tick(ACireMonster* M,float Delta){
    if(!M||!M->HasAuthority()||!M->GetWorld()||!FMath::IsFinite(Delta)||Delta<=0)return;
    // Design ruling: threat never decays with time or distance. It is lost only when the
    // monster or the hero dies, or through an explicit ability (Transfer/Scale).
    if(M->NPCState)M->NPCState->PublishThreat(false);
}
ACireHero* CireThreat::Select(ACireMonster* M){
    if(!M||!M->HasAuthority())return nullptr;
    for(auto It=M->Threat.CreateIterator();It;++It)if(!CanHold(M,It.Key().Get()))It.RemoveCurrent();
    ACireHero* Old=M->Victim;
    const bool bWasForced=M->NPCState&&M->NPCState->bForcedLastSelect;
    // The current target keeps aggro at any distance; the 22 m range only gates gaining threat.
    ACireHero* Current=CanHold(M,M->Victim)&&M->Threat.Contains(M->Victim)?M->Victim:nullptr;
    ACireHero* Best=Current;bool bForced=false;
    if(Eligible(M,M->ForcedVictim.Get())&&M->ForcedVictimUntil>M->GetWorld()->GetTimeSeconds()){Best=M->ForcedVictim.Get();bForced=true;}
    else {
        M->ForcedVictim.Reset();
        if(Current)
        {
            // WoW pull rule: overtaking the current target needs 110% in melee, 130% at range.
            const float Held=M->Threat.FindRef(Current);float BestValue=Held;
            for(const auto& Pair:M->Threat)
            {
                ACireHero* H=Pair.Key.Get();if(H==Current)continue;
                if(Pair.Value>Held*PullRatio(M,H)&&Pair.Value>BestValue){Best=H;BestValue=Pair.Value;}
            }
        }
        else
        {
            float Top=0;
            for(const auto& Pair:M->Threat)if(Pair.Value>Top){Best=Pair.Key.Get();Top=Pair.Value;}
        }
    }
    if(M->NPCState)M->NPCState->bForcedLastSelect=bForced;
    if(Old!=Best)
    {
        M->Victim=Best;M->ForceNetUpdate();
        if(M->NPCState)
        {
            const ECireAggroReason Reason=bForced?ECireAggroReason::Taunted:!Best?ECireAggroReason::Reset:!Old?ECireAggroReason::Acquired:
                bWasForced?ECireAggroReason::TauntExpired:!Current?ECireAggroReason::TargetLost:ECireAggroReason::Pulled;
            M->NPCState->SetAggro(Best,Old,Reason);
        }
    }
    return Best;
}
void CireThreat::Clear(ACireMonster* M){
    if(!M)return;
    ACireHero* Old=M->Victim;const bool bHad=Old||!M->Threat.IsEmpty();
    M->Threat.Reset();M->ForcedVictim.Reset();M->ForcedVictimUntil=0;M->Victim=nullptr;
    if(M->NPCState&&M->HasAuthority())
    {
        M->NPCState->LastThreatAt.Reset();M->NPCState->bForcedLastSelect=false;
        if(bHad)M->NPCState->SetAggro(nullptr,Old,ECireAggroReason::Reset);
    }
}
void CireThreat::Remove(ACireHero* H){
    if(!H||!H->HasAuthority())return;
    for(TActorIterator<ACireMonster> It(H->GetWorld());It;++It){It->Threat.Remove(H);if(It->ForcedVictim==H)It->ForcedVictim.Reset();Select(*It);}
}
