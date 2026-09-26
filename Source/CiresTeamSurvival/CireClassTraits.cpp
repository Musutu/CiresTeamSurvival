#include "CireClassTraits.h"
#include "CireActorIterator.h" // town-perf: fast actor iteration in editor-binary -game
#include "CireChampionProfiles.h"
#include "CireCombatEvents.h"
#include "CireGame.h"
#include "CireSummon.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "Misc/ScopeExit.h"

DEFINE_LOG_CATEGORY_STATIC(LogCireClassTraits,Log,All);

Cires::SkillDraftRole CireClassTraits::Role(const AActor* Actor)
{
    const auto* Hero=Cast<ACireHero>(Actor);
    if(!Hero||Hero->IsA<ACireSummon>()||!Hero->bDrafted)return Cires::SkillDraftRole::Any;
    return CireChampionProfiles::DraftRole(Hero);
}

FCireClassTrait CireClassTraits::Info(Cires::SkillDraftRole Role)
{
    FCireClassTrait T;
    switch(Role)
    {
    case Cires::SkillDraftRole::Support:
        T.Id=TEXT("trait_mending_strikes");T.Name=TEXT("Mending Strikes");T.Color=FLinearColor::FromSRGBColor(FColor(88,198,126));
        T.Summary=TEXT("50% of damage dealt heals the lowest-health ally  |  -20% damage  |  +10% attack speed");
        T.Tooltip=TEXT("SUPPORT CLASS TRAIT (always active). 50% of the damage you deal heals the living party member with the lowest health percentage, you included. Your damage to enemies is reduced by 20% and you attack 10% faster than other classes.");
        break;
    case Cires::SkillDraftRole::Tank:
        T.Id=TEXT("trait_natural_defense");T.Name=TEXT("Natural Defense");T.Color=FLinearColor::FromSRGBColor(FColor(92,148,228));
        T.Summary=TEXT("Every hit you take is reduced by 10 (after armor)");
        T.Tooltip=TEXT("TANK CLASS TRAIT (always active). Every incoming damage instance, basic or ability, is reduced by a flat 10 after guard, Stone Skin, armor and spell ward. Damage cannot drop below 0.");
        break;
    case Cires::SkillDraftRole::Damage:
        T.Id=TEXT("trait_keen_edge");T.Name=TEXT("Keen Edge");T.Color=FLinearColor::FromSRGBColor(FColor(216,80,64));
        T.Summary=TEXT("10% base critical-strike chance");
        T.Tooltip=TEXT("DPS CLASS TRAIT (always active). The baseline damage dealer: your base critical-strike chance is 10% instead of 5%. Items and effects add on top.");
        break;
    default: break;
    }
    return T;
}

float CireClassTraits::CriticalChance(const ACireHero* Hero,float TuningBase)
{
    return static_cast<float>(Cires::Traits::BaseCriticalChance(Role(Hero),TuningBase));
}
float CireClassTraits::AttackSpeedBonus(const ACireHero* Hero)
{
    return static_cast<float>(Cires::Traits::AttackSpeedBonus(Role(Hero)));
}
float CireClassTraits::ModifyOutgoingDamage(const AActor* Source,float Amount)
{
    return static_cast<float>(Amount*Cires::Traits::OutgoingDamageMultiplier(Role(Source)));
}
float CireClassTraits::ModifyIncomingDamage(const ACireHero* Hero,float Amount)
{
    return static_cast<float>(Cires::Traits::ApplyIncomingFlatReduction(Amount,Role(Hero)));
}

ACireHero* CireClassTraits::MendingTarget(const ACireHero* Source)
{
    if(!Source||!Source->GetWorld())return nullptr;
    TArray<ACireHero*> Party;std::vector<Cires::Traits::PartyMember> Members;
    for(TCireActorIterator<ACireHero> It(Source->GetWorld());It;++It)
    {
        ACireHero* H=*It;
        if(H->IsA<ACireSummon>()||!H->bDrafted||H->TeamId!=Source->TeamId||H->TeamId<0)continue;
        Members.push_back({static_cast<int>(H->GetUniqueID()),H->Health,H->MaxHealth,!H->bDead});
        Party.Add(H);
    }
    const int Index=Cires::Traits::SelectMendingTarget(Members);
    return Party.IsValidIndex(Index)?Party[Index]:nullptr;
}

float CireClassTraits::OnDamageDealt(AActor* Source,AActor* Target,float Applied)
{
    auto* Healer=Cast<ACireHero>(Source);
    const double Heal=Cires::Traits::MendingHealAmount(Applied,Role(Healer));
    if(!Healer||Heal<=0||Healer->bDead||!CireCombat::AreHostile(Source,Target))return 0;
    ACireHero* Ally=MendingTarget(Healer);
    return Ally?CireCombat::ApplyHealing(Healer,Ally,static_cast<float>(Heal),TEXT("Mending Strikes")):0.f;
}

#if !UE_BUILD_SHIPPING
bool CireClassTraits::RunSmoke(ACireGameMode* Mode)
{
    if(!IsValid(Mode)||!Mode->HasAuthority())return false;
    int32 Checks=0;bool bPass=true;TArray<AActor*> Actors;
    const auto Check=[&](bool bValue,const TCHAR* Why){++Checks;if(!bValue){bPass=false;UE_LOG(LogCireClassTraits,Error,TEXT("CIRE_CLASS_TRAITS_FAIL %s"),Why);}};
    ON_SCOPE_EXIT{for(auto* A:Actors)if(IsValid(A))A->Destroy();};
    FActorSpawnParameters P;P.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    const FVector Origin(0,-2100,4600);
    const auto Make=[&](const TCHAR* Profile,int32 Team)
    {
        auto* H=Mode->GetWorld()->SpawnActor<ACireHero>(Origin+FVector(Actors.Num()*120.f,0,0),FRotator::ZeroRotator,P);
        if(H){Actors.Add(H);H->SetActorTickEnabled(false);H->SetActorEnableCollision(false);H->TeamId=Team;H->DraftProfile(Profile);H->Offers.Reset();H->CurrentOffer={};}
        return H;
    };
    auto* Support=Make(TEXT("scholar"),0);auto* Tank=Make(TEXT("knight"),0);auto* Dps=Make(TEXT("ranger"),0);auto* Enemy=Make(TEXT("lancer"),1);
    if(!Support||!Tank||!Dps||!Enemy){Check(false,TEXT("fixture heroes spawned"));return false;}
    Check(Role(Support)==Cires::SkillDraftRole::Support&&Role(Tank)==Cires::SkillDraftRole::Tank&&Role(Dps)==Cires::SkillDraftRole::Damage,TEXT("main roles resolve"));
    Check(FMath::IsNearlyEqual(Dps->CriticalChance,FMath::Max(.10f,Dps->CriticalChance))&&Dps->CriticalChance>=.10f-1e-4f,TEXT("DPS base crit is at least 10%"));
    Check(Tank->CriticalChance<.10f-1e-4f,TEXT("tank keeps the tuning crit"));
    Check(FMath::IsNearlyEqual(AttackSpeedBonus(Support),.10f)&&FMath::IsNearlyEqual(AttackSpeedBonus(Tank),0.f),TEXT("support +10% attack speed"));
    Check(FMath::IsNearlyEqual(ModifyOutgoingDamage(Support,100.f),80.f)&&FMath::IsNearlyEqual(ModifyOutgoingDamage(Dps,100.f),100.f),TEXT("support -20% outgoing"));
    Check(FMath::IsNearlyEqual(ModifyIncomingDamage(Tank,25.f),15.f)&&FMath::IsNearlyEqual(ModifyIncomingDamage(Tank,6.f),0.f)&&FMath::IsNearlyEqual(ModifyIncomingDamage(Dps,25.f),25.f),TEXT("tank flat 10 reduction, floor 0"));
    // Mending target: lowest % health living teammate, support included; dead members skipped.
    Support->Health=Support->MaxHealth*.9f;Tank->Health=Tank->MaxHealth*.4f;Dps->Health=Dps->MaxHealth*.3f;Dps->bDead=true;
    Check(MendingTarget(Support)==Tank,TEXT("lowest living ally selected, dead skipped"));
    Dps->bDead=false;Check(MendingTarget(Support)==Dps,TEXT("revived lowest ally selected"));
    Dps->Health=Dps->MaxHealth;Tank->Health=Tank->MaxHealth;Support->Health=Support->MaxHealth*.5f;
    Check(MendingTarget(Support)==Support,TEXT("support heals itself when lowest"));
    // End-to-end through the real damage pipeline in PvE (monster target) is covered by
    // the telemetry/expansion probes; here the arena path heals via ApplyHealing.
    const bool bArena=Mode->IsCombatPhase();
    if(bArena)
    {
        Support->Health=Support->MaxHealth*.5f;const float Before=Support->Health;
        const float Healed=OnDamageDealt(Support,Enemy,40.f);
        Check(Healed<=20.f+1e-3f&&Support->Health>=Before,TEXT("mending heals at most half the damage"));
    }
    UE_LOG(LogCireClassTraits,Display,TEXT("CIRE_CLASS_TRAITS_%s checks=%d"),bPass?TEXT("PASS"):TEXT("FAIL"),Checks);
    return bPass;
}
#endif
