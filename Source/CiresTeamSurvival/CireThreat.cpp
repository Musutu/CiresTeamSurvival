#include "CireThreat.h"
#include "CireGame.h"
#include "CireSkillTuning.h"
#include "Engine/World.h"
#include "EngineUtils.h"

namespace {
bool Eligible(const ACireMonster* M,const ACireHero* H) {
    return IsValid(M)&&!M->bArmoredEscort&&M->Health>0&&IsValid(H)&&!H->bDead&&H->bDrafted&&H->TeamId==M->Lane&&
        FVector::DistSquared2D(M->GetActorLocation(),H->GetActorLocation())<=FMath::Square(2200.f);
}
void Add(ACireMonster* M,ACireHero* H,float Amount){
    if(!M||!M->HasAuthority()||!Eligible(M,H)||!FMath::IsFinite(Amount)||Amount<=0)return;
    float& Threat=M->Threat.FindOrAdd(H);Threat=FMath::Min(1.e9f,Threat+Amount);M->bEngaged=true;
}
}
void CireThreat::Engage(ACireMonster* M,ACireHero* H){Add(M,H,1.f);}
void CireThreat::Damage(ACireMonster* M,ACireHero* H,float Amount){
    if(!H)return;
    Add(M,H,Amount*H->DamageThreatMultiplier());
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
    float Highest=0;for(const auto& Pair:M->Threat)Highest=FMath::Max(Highest,Pair.Value);
    M->Threat.FindOrAdd(H)=FMath::Max(M->Threat.FindRef(H),Highest+1.f);
    M->ForcedVictim=H;M->ForcedVictimUntil=M->GetWorld()->GetTimeSeconds()+FMath::Min(Seconds,10.f);
    M->Victim=H;M->bEngaged=true;M->ForceNetUpdate();
}
ACireHero* CireThreat::Select(ACireMonster* M){
    if(!M||!M->HasAuthority())return nullptr;
    for(auto It=M->Threat.CreateIterator();It;++It)if(!Eligible(M,It.Key().Get()))It.RemoveCurrent();
    ACireHero* Best=Eligible(M,M->Victim)?M->Victim:nullptr;
    float Highest=Best?M->Threat.FindRef(Best):0.f;
    if(Eligible(M,M->ForcedVictim.Get())&&M->ForcedVictimUntil>M->GetWorld()->GetTimeSeconds())Best=M->ForcedVictim.Get();
    else {
        M->ForcedVictim.Reset();
        for(const auto& Pair:M->Threat)if(Pair.Value>Highest){Best=Pair.Key.Get();Highest=Pair.Value;}
    }
    if(M->Victim!=Best){M->Victim=Best;M->ForceNetUpdate();}
    return Best;
}
void CireThreat::Clear(ACireMonster* M){if(M){M->Threat.Reset();M->ForcedVictim.Reset();M->ForcedVictimUntil=0;M->Victim=nullptr;}}
void CireThreat::Remove(ACireHero* H){
    if(!H||!H->HasAuthority())return;
    for(TActorIterator<ACireMonster> It(H->GetWorld());It;++It){It->Threat.Remove(H);if(It->ForcedVictim==H)It->ForcedVictim.Reset();if(It->Victim==H)It->Victim=nullptr;Select(*It);}
}
