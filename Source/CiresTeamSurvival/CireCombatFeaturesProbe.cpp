#include "CireCombatFeaturesProbe.h"
#if !UE_BUILD_SHIPPING
#include "CireGame.h"
#include "CireAttackSystem.h"
#include "CireAbilityLibrary.h"
#include "CireAreaEffects.h"
#include "Rules/CireAttackRules.h"
#include "Components/CapsuleComponent.h"
#include "Components/BoxComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Misc/ScopeExit.h"
#include <limits>

DEFINE_LOG_CATEGORY_STATIC(LogCireFeatures,Log,All);
bool CireCombatFeatures::Run(ACireGameMode* Mode) {
    if(!IsValid(Mode)||!Mode->HasAuthority())return false;
    int32 Checks=0;bool Pass=true;
    const auto Check=[&](bool Good,const TCHAR* Label){++Checks;if(!Good){Pass=false;UE_LOG(LogCireFeatures,Error,TEXT("CIRE_COMBAT_FEATURE_CHECK_FAIL %s"),Label);}};
    for(const double Miss:{.05,.40}){
        int32 Hits=0,Misses=0,Dodges=0;
        for(int32 M=0;M<100;++M)for(int32 D=0;D<100;++D){
            const auto R=Cires::ResolveAttack(Miss,(M+.5)/100.,(D+.5)/100.);
            Hits+=R==Cires::AttackResult::Hit;Misses+=R==Cires::AttackResult::Miss;Dodges+=R==Cires::AttackResult::Dodge;
        }
        Check(Miss==.05?(Hits==9025&&Misses==500&&Dodges==475):(Hits==5700&&Misses==4000&&Dodges==300),TEXT("independent miss/dodge probability grid"));
    }
    Check(Cires::ResolveAttack(.05,.05,.05)==Cires::AttackResult::Hit,TEXT("exact threshold succeeds"));
    Check(Cires::ResolveAttack(.05,.04,0)==Cires::AttackResult::Miss,TEXT("miss precedes dodge"));
    Check(Cires::ResolveAttack(.05,.5,.049)==Cires::AttackResult::Dodge,TEXT("dodge after successful accuracy"));
    Check(Cires::ResolveAttack(.05,std::numeric_limits<double>::quiet_NaN(),.5)==Cires::AttackResult::Miss,TEXT("invalid probability fails closed"));
    Check(Cires::MissChance(true,0,10)==.05&&Cires::MissChance(true,0,10.1)==.4,TEXT("uphill tolerance boundary"));
    Check(Cires::MissChance(false,0,200)==.05&&Cires::MissChance(true,200,0)==.05,TEXT("melee and downhill retain baseline"));
    Check(CireAbilityLibrary::Count()==5,TEXT("five validated Astra ground recipes loaded"));
    const TCHAR* IDs[]={TEXT("venom_ground"),TEXT("cinder_cone"),TEXT("grave_line"),TEXT("ashen_square"),TEXT("blight_sigil")};
    for(int32 I=0;I<5;++I){const auto* A=CireAbilityLibrary::Find(IDs[I]);Check(A&&static_cast<int32>(A->Area.Shape)==I,TEXT("authored shape matches recipe"));}
    Check(CireAreaEffects::RunGeometrySmoke(),TEXT("ground geometry suite"));
    Check(CireAreaEffects::RunLifecycleSmoke(Mode),TEXT("ground lifecycle suite"));

    const auto SavedClock=Mode->Clock;const auto SavedHeroes=Mode->Heroes;const auto SavedMonsters=Mode->Monsters;
    auto* State=Mode->GetWorld()->GetGameState<ACireGameState>();const int32 SavedPhase=State->Phase;
    TArray<AActor*> Fixtures;
    ON_SCOPE_EXIT {
        for(AActor* A:Fixtures)if(IsValid(A))ACireAreaEffect::ClearForActor(A);
        Mode->Clock=SavedClock;Mode->Heroes=SavedHeroes;Mode->Monsters=SavedMonsters;State->Phase=SavedPhase;
        for(AActor* A:Fixtures)if(IsValid(A))A->Destroy();
    };
    Mode->Clock=Cires::MatchClock({60,90,15});State->Phase=0;
    FActorSpawnParameters P;P.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    UWorld* W=Mode->GetWorld();
    auto* Ranger=W->SpawnActor<ACireHero>(FVector(0,-2100,2092),FRotator::ZeroRotator,P);
    auto* Lancer=W->SpawnActor<ACireHero>(FVector(0,-1800,2092),FRotator::ZeroRotator,P);
    auto* Target=W->SpawnActor<ACireMonster>(FVector(600,-2100,2092),FRotator::ZeroRotator,P);
    for(AActor* A:{static_cast<AActor*>(Ranger),static_cast<AActor*>(Lancer),static_cast<AActor*>(Target)})if(A){Fixtures.Add(A);A->SetActorTickEnabled(false);}
    Check(Ranger&&Lancer&&Target,TEXT("combat fixture spawned"));if(!Ranger||!Lancer||!Target)return false;
    Ranger->TeamId=0;Ranger->Draft(1);Lancer->TeamId=0;Lancer->Draft(3);Ranger->CriticalChance=Lancer->CriticalChance=0;
    Target->Lane=0;Target->Health=Target->MaxHealth=1000;
    Target->SetActorLocation(FVector(600,-2100,2000+Target->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()));
    Mode->Heroes={Ranger,Lancer};Mode->Monsters={Target};
    Check(Lancer->Archetype==3&&Lancer->Agility==20&&Lancer->Strength==10&&Lancer->Intelligence==10&&FMath::IsNearlyEqual(Lancer->AttackDamage(),Ranger->AttackDamage()),TEXT("Lancer uses agility primary stats and damage"));
    Check(FMath::IsNearlyEqual(CireAttacks::MissChance(Ranger,Target,true),.05f),TEXT("equal feet height ignores different capsules"));
    Target->AddActorWorldOffset(FVector(0,0,20));
    Check(FMath::IsNearlyEqual(CireAttacks::MissChance(Ranger,Target,true),.4f),TEXT("runtime uphill ranged chance"));
    Target->AddActorWorldOffset(FVector(0,0,-20));
    CireAttacks::Resolve(Ranger,Target,50,ECireHitOutcome::Miss,TEXT("test miss"));
    CireAttacks::Resolve(Ranger,Target,50,ECireHitOutcome::Dodge,TEXT("test dodge"));
    Check(Target->Health==1000&&Ranger->DamageDone==0,TEXT("avoidance changes neither health nor damage meter"));
    CireAttacks::Resolve(Ranger,Target,50,ECireHitOutcome::Hit,TEXT("test hit"));
    Check(Target->Health==950&&Ranger->DamageDone==50,TEXT("successful attack applies actual damage once"));
    TArray<FCireCombatEvent> Events;uint32 Sequence=0;FCireCombatEvent E;E.Outcome=ECireHitOutcome::Miss;
    CireCombat::AppendReceivedEvent(Events,Sequence,E,1);E.Outcome=ECireHitOutcome::Dodge;CireCombat::AppendReceivedEvent(Events,Sequence,E,1);
    E.Outcome=ECireHitOutcome::Hit;CireCombat::AppendReceivedEvent(Events,Sequence,E,1);
    Check(Events.Num()==2&&Events[0].Outcome==ECireHitOutcome::Miss&&Events[1].Outcome==ECireHitOutcome::Dodge,TEXT("SCT accepts avoidance while rejecting zero damage hits"));
    auto* Shot=ACireTargetProjectile::Launch(Ranger,Target,70,ECireHitOutcome::Hit);
    Check(Shot&&Target->Health==950,TEXT("projectile launch does not apply instant damage"));
    if(Shot){Fixtures.Add(Shot);Shot->Tick(.01f);Check(Target->Health==950,TEXT("damage waits for projectile travel"));
        Target->AddActorWorldOffset(FVector(250,350,0));
        for(int32 I=0;I<100&&!Shot->IsActorBeingDestroyed();++I)Shot->Tick(.01f);
        Check(Shot->IsActorBeingDestroyed()&&Target->Health==880&&Ranger->DamageDone==120,TEXT("targeted arrow tracks moving target and resolves once"));}
    Shot=ACireTargetProjectile::Launch(Lancer,Target,70,ECireHitOutcome::Dodge);
    Check(Shot&&Shot->Style==3,TEXT("Lancer creates thrown lance style"));
    if(Shot){Fixtures.Add(Shot);Shot->Tick(2);Check(Shot->IsActorBeingDestroyed()&&Target->Health==880,TEXT("dodge resolves after projectile arrival without damage"));}
    Shot=ACireTargetProjectile::Launch(Ranger,Target,70,ECireHitOutcome::Hit);
    if(Shot){Fixtures.Add(Shot);State->Phase=1;Shot->Tick(.1f);Check(Shot->IsActorBeingDestroyed()&&Target->Health==880,TEXT("phase change cancels in-flight attack"));State->Phase=0;}
    else Check(false,TEXT("phase fixture shot launched"));
    Target->Lane=1;
    Check(ACireTargetProjectile::Launch(Ranger,Target,70,ECireHitOutcome::Hit)==nullptr,TEXT("cross-realm launch rejected"));
    Target->Lane=0;
    auto* Floor=W->SpawnActor<AActor>(FVector(0,-2100,1990),FRotator::ZeroRotator,P);
    if(Floor){
        Fixtures.Add(Floor);auto* Box=NewObject<UBoxComponent>(Floor);Floor->SetRootComponent(Box);
        Box->SetBoxExtent(FVector(2000,2000,10));Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
        Box->SetCollisionObjectType(ECC_WorldStatic);Box->SetCollisionResponseToAllChannels(ECR_Block);Box->RegisterComponent();
        Floor->SetActorLocation(FVector(0,-2100,1990));
        Target->SetActorLocation(FVector(600,-2100,2000+Target->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()));
        Ranger->Skills={TEXT("venom_ground")};Ranger->Cooldowns={0};Ranger->GlobalCooldown=0;Ranger->Target=Target;
        const float ManaBefore=Ranger->Mana,HealthBefore=Target->Health;
        Ranger->Cast(0);
        Check(Ranger->Mana==ManaBefore-35&&FMath::IsNearlyEqual(Ranger->Cooldowns[0],8.f),TEXT("authored skill cast pays mana and starts cooldown"));
        ACireAreaEffect* CastArea=nullptr;
        for(TActorIterator<ACireAreaEffect> It(W);It;++It)if(!It->IsActorBeingDestroyed()&&It->AreaSpec.AbilityName==TEXT("Venom Ground")){CastArea=*It;break;}
        Check(CastArea&&Target->Health==HealthBefore&&Target->PoisonAreaCount==0,TEXT("authored cast creates harmless warning before activation"));
        if(CastArea){Fixtures.Add(CastArea);CastArea->Tick(.65f);CastArea->Tick(.5f);
            Check(Target->PoisonAreaCount==1&&FMath::IsNearlyEqual(Target->Health,HealthBefore-12.f*CastArea->AreaSpec.DamagePerSecond/FMath::Max(1.f,CireAbilityLibrary::Find(TEXT("venom_ground"))->Area.DamagePerSecond),.05f),TEXT("authored poison produces configured DPS after warning")); // scaling-kits: DPS includes the primary term
            Target->AddActorWorldOffset(FVector(500,0,0));CastArea->Tick(.01f);
            Check(Target->PoisonAreaCount==0,TEXT("authored poison stops on exit"));}
        const float PaidMana=Ranger->Mana;Ranger->GlobalCooldown=0;Ranger->Cast(0);
        Check(Ranger->Mana==PaidMana,TEXT("cooldown rejects repeat authored cast"));
        Ranger->Cooldowns[0]=0;Ranger->Mana=0;Ranger->Cast(0);
        Check(Ranger->Cooldowns[0]==0,TEXT("insufficient resource starts no cooldown"));
    }else Check(false,TEXT("authored skill fixture floor spawned"));
    UE_LOG(LogCireFeatures,Display,TEXT("CIRE_COMBAT_FEATURES_%s checks=%d"),Pass?TEXT("PASS"):TEXT("FAIL"),Checks);
    return Pass;
}
#endif
