// Native checks for NPC roles, the Pack Leader boss and WoW-style threat rules.
// Invoked from CireNPCCombat::RunSmoke (part of -CireCombatExpansionProbe).
#include "CireNPCCombat.h"
#include "CireThreat.h"
#include "CireNPCState.h"
#include "CireNPCArchetypes.h"
#include "CireGame.h"
#include "CireAreaEffects.h"
#include "CireSkillshot.h"
#include "CireCombatEvents.h"
#include "CireLanePath.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Misc/ScopeExit.h"

#if !UE_BUILD_SHIPPING
DEFINE_LOG_CATEGORY_STATIC(LogCireNPCTests,Log,All);

namespace
{
struct FNPCFixture
{
    ACireGameMode* Mode=nullptr;
    Cires::MatchClock Clock;TArray<ACireHero*> Heroes;TArray<ACireMonster*> Monsters;TSet<int32> Rewarded;
    TArray<AActor*> Actors;
    FVector Ground=FVector(0,-2100,3000);
    explicit FNPCFixture(ACireGameMode* InMode):Mode(InMode),Clock(InMode->Clock),Heroes(InMode->Heroes),Monsters(InMode->Monsters),Rewarded(InMode->RewardedPacks)
    {
        Mode->Clock=Cires::MatchClock();Mode->Heroes.Reset();Mode->Monsters.Reset();
        auto* Floor=Mode->GetWorld()->SpawnActor<AActor>();
        if(Floor)
        {
            Actors.Add(Floor);auto* Box=NewObject<UBoxComponent>(Floor);Floor->SetRootComponent(Box);Floor->AddInstanceComponent(Box);
            Box->SetBoxExtent(FVector(2400,1200,50));Box->SetCollisionObjectType(ECC_WorldStatic);
            Box->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);Box->SetCollisionResponseToAllChannels(ECR_Block);
            Box->RegisterComponent();Floor->SetActorLocation(Ground-FVector(0,0,50));
        }
    }
    ~FNPCFixture()
    {
        for(auto* M:Mode->Monsters)if(IsValid(M))CireNPCCombat::Interrupt(M);
        for(int32 I=Actors.Num()-1;I>=0;--I)if(IsValid(Actors[I]))Actors[I]->Destroy();
        for(TActorIterator<ACireAreaEffect> It(Mode->GetWorld());It;++It)if(!It->GetOwner()||!IsValid(It->GetOwner())||It->GetOwner()->IsActorBeingDestroyed())It->Destroy();
        Mode->Clock=Clock;Mode->Heroes=Heroes;Mode->Monsters=Monsters;Mode->RewardedPacks=Rewarded;
    }
    ACireHero* Hero(FVector Offset,int32 Kind)
    {
        FActorSpawnParameters P;P.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        auto* H=Mode->GetWorld()->SpawnActor<ACireHero>(Ground+Offset+FVector(0,0,92),FRotator::ZeroRotator,P);
        if(H){Actors.Add(H);H->SetActorTickEnabled(false);H->TeamId=0;H->Draft(Kind);H->Health=H->MaxHealth=100000;H->CriticalChance=0;H->ShieldUntil=0;Mode->Heroes.Add(H);}
        return H;
    }
    ACireMonster* Monster(FVector Offset,FName Id,int32 Tier=0,int32 Pack=-1)
    {
        FActorSpawnParameters P;P.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        auto* M=Mode->GetWorld()->SpawnActor<ACireMonster>(Ground+Offset+FVector(0,0,95),FRotator::ZeroRotator,P);
        if(!M)return nullptr;
        Actors.Add(M);M->SetActorTickEnabled(false);M->Lane=0;M->PackId=Pack;Mode->Monsters.Add(M);
        CireNPCCombat::ConfigureArchetype(M,Id,1,Tier,1);M->SetActorLocation(Ground+Offset+FVector(0,0,95));M->SpawnPosition=M->GetActorLocation();
        return M;
    }
};
float Now(const AActor* A){return A->GetWorld()->GetTimeSeconds();}
void Ready(ACireMonster* M){if(M->NPCState)M->NPCState->ReadyAt.Reset();M->AbilityTimer=0;M->AttackTimer=10;}
void FinishCast(ACireMonster* M){M->CastEndsAt=Now(M)-.01f;CireNPCCombat::Tick(M,.01f);}
}

bool CireThreat::RunRulesSmoke(ACireGameMode* Mode)
{
    if(!IsValid(Mode)||!Mode->HasAuthority())return false;
    bool bPass=true;int32 Checks=0;
    auto Check=[&](bool V,const TCHAR* Why){++Checks;if(!V){bPass=false;UE_LOG(LogCireNPCTests,Error,TEXT("CIRE_THREAT_RULES_CHECK_FAIL %s"),Why);}};
    TArray<FCireAggroEvent> Events;
    const FDelegateHandle Handle=UCireNPCState::OnAggroChanged().AddLambda([&Events](const FCireAggroEvent& E){Events.Add(E);});
    ON_SCOPE_EXIT{UCireNPCState::OnAggroChanged().Remove(Handle);};
    {
        FNPCFixture F(Mode);
        auto* Tank=F.Hero(FVector(150,0,0),0);auto* Dps=F.Hero(FVector(-150,60,0),1);auto* Healer=F.Hero(FVector(-200,-200,0),2);
        auto* M=F.Monster(FVector::ZeroVector,TEXT("hollow_infantry"));auto* Second=F.Monster(FVector(0,600,0),TEXT("hollow_infantry"));
        if(!Tank||!Dps||!Healer||!M||!Second){Check(false,TEXT("threat fixture spawned"));return false;}
        const float TankMul=Tank->DamageThreatMultiplier(),DpsMul=Dps->DamageThreatMultiplier();
        Check(TankMul>DpsMul,TEXT("tank role has a higher damage threat multiplier"));
        Events.Reset();Damage(M,Tank,10);
        Check(M->Victim==Tank&&Events.Num()==1&&Events[0].Reason==ECireAggroReason::Acquired&&Events[0].NewTarget==Tank,TEXT("first damage acquires with an Acquired event"));
        Check(UCireNPCState::DescribeAggro(Events.Last()).Contains(TEXT("gained aggro on")),TEXT("aggro text for the UI"));
        const float Held=M->Threat.FindRef(Tank);
        // Melee pull rule: 110%.
        Damage(M,Dps,(Held*1.08f)/DpsMul);Check(M->Victim==Tank,TEXT("108% in melee does not pull"));
        Damage(M,Dps,(Held*.04f)/DpsMul);Check(M->Victim==Dps&&Events.Last().Reason==ECireAggroReason::Pulled&&Events.Last().OldTarget==Tank,TEXT("112% in melee pulls with a Pulled event"));
        // Ranged pull rule: 130%.
        Clear(M);Check(Events.Last().Reason==ECireAggroReason::Reset,TEXT("clear publishes Reset"));
        Dps->SetActorLocation(F.Ground+FVector(-900,0,92));
        Damage(M,Tank,10);const float Held2=M->Threat.FindRef(Tank);
        Damage(M,Dps,(Held2*1.25f)/DpsMul);Check(M->Victim==Tank,TEXT("125% at range does not pull"));
        Damage(M,Dps,(Held2*.07f)/DpsMul);Check(M->Victim==Dps,TEXT("132% at range pulls"));
        Check(FMath::IsNearlyEqual(PullRatio(M,Dps),1.3f)&&FMath::IsNearlyEqual(PullRatio(M,Tank),1.1f),TEXT("pull ratio depends on melee range"));
        // Taunt: top threat + forced target; after expiry the taunter keeps aggro unless outpaced by 110/130%.
        Taunt(M,Tank,3);Check(M->Victim==Tank&&Events.Last().Reason==ECireAggroReason::Taunted&&M->Threat.FindRef(Tank)>=M->Threat.FindRef(Dps),TEXT("taunt forces target and matches top threat"));
        Damage(M,Dps,(M->Threat.FindRef(Tank)*.5f)/DpsMul);Check(M->Victim==Tank,TEXT("taunt holds during its duration"));
        M->ForcedVictimUntil=0;Select(M);Check(M->Victim==Dps&&Events.Last().Reason==ECireAggroReason::TauntExpired,TEXT("expired taunt releases to a DPS above the pull threshold"));
        Taunt(M,Tank,3);M->ForcedVictimUntil=0;Select(M);Check(M->Victim==Tank,TEXT("fresh taunt without further DPS threat keeps the tank"));
        // Healing threat split across engaged monsters; overheal generates none.
        Clear(M);Clear(Second);Engage(M,Tank);Engage(Second,Tank);Tank->Health=Tank->MaxHealth-500;
        const float Applied=CireCombat::ApplyHealing(Healer,Tank,100,TEXT("Threat test heal"));
        const float Expected=Applied*CireSkillTuning::Get().HealingThreatMultiplier/2;
        Check(Applied>0&&FMath::IsNearlyEqual(M->Threat.FindRef(Healer),Expected,.01f)&&FMath::IsNearlyEqual(Second->Threat.FindRef(Healer),Expected,.01f),TEXT("healing threat splits across engaged monsters"));
        Tank->Health=Tank->MaxHealth;const float BeforeOverheal=M->Threat.FindRef(Healer);CireCombat::ApplyHealing(Healer,Tank,100,TEXT("Overheal"));
        Check(FMath::IsNearlyEqual(BeforeOverheal,M->Threat.FindRef(Healer)),TEXT("overheal adds no threat"));
        // Transfer / scale.
        Clear(M);Damage(M,Tank,100);Damage(M,Dps,100);const float T0=M->Threat.FindRef(Tank),D0=M->Threat.FindRef(Dps);
        Transfer(M,Dps,Tank,.5f);Check(FMath::IsNearlyEqual(M->Threat.FindRef(Dps),D0*.5f)&&FMath::IsNearlyEqual(M->Threat.FindRef(Tank),T0+D0*.5f),TEXT("transfer moves a fraction of threat"));
        Scale(M,Dps,.5f);Check(FMath::IsNearlyEqual(M->Threat.FindRef(Dps),D0*.25f),TEXT("scale multiplies a hero's threat"));
        // Idle decay skips the current target.
        M->NPCState->LastThreatAt.FindOrAdd(Dps)=Now(M)-60.f;M->NPCState->LastThreatAt.FindOrAdd(Tank)=Now(M)-60.f;
        const float DBefore=M->Threat.FindRef(Dps),TBefore=M->Threat.FindRef(Tank);
        Tick(M,1.f);
        Check(M->Threat.FindRef(Dps)<DBefore&&FMath::IsNearlyEqual(M->Threat.FindRef(Tank),TBefore),TEXT("idle threat decays but the current target's does not"));
        // Replicated table + UI percentages.
        M->NPCState->PublishThreat(true);
        Check(M->NPCState->ThreatTable.Num()==2&&M->NPCState->ThreatTable[0].Hero==Tank&&M->NPCState->ThreatTable[0].Threat>=M->NPCState->ThreatTable[1].Threat,TEXT("threat table is sorted for the meter"));
        Check(FMath::IsNearlyEqual(M->NPCState->ThreatPercent(Tank),100.f)&&M->NPCState->ThreatPercent(Dps)<100.f&&
            FMath::IsNearlyEqual(M->NPCState->PullPercent(Dps),M->NPCState->ThreatPercent(Dps)/1.3f,.1f),TEXT("threat percent and pull percent"));
        Check(UCireNPCState::DescribeFocus(M).Contains(TEXT("is focusing")),TEXT("focus text for the UI"));
        // Target loss.
        Tank->bDead=true;Select(M);Check(M->Victim==Dps&&Events.Last().Reason==ECireAggroReason::TargetLost,TEXT("dead target hands aggro to next highest with TargetLost"));
        Tank->bDead=false;
    }
    UE_LOG(LogCireNPCTests,Display,TEXT("CIRE_THREAT_RULES_%s checks=%d"),bPass?TEXT("PASS"):TEXT("FAIL"),Checks);
    return bPass;
}

bool CireNPCCombat::RunRolesSmoke(ACireGameMode* Mode)
{
    if(!IsValid(Mode)||!Mode->HasAuthority())return false;
    bool bPass=true;int32 Checks=0;
    auto Check=[&](bool V,const TCHAR* Why){++Checks;if(!V){bPass=false;UE_LOG(LogCireNPCTests,Error,TEXT("CIRE_NPC_ROLES_CHECK_FAIL %s"),Why);}};
    const auto& D=CireNPCArchetypes::Get();
    {
        FNPCFixture F(Mode);
        auto* Hero=F.Hero(FVector(150,0,0),1);
        if(!Hero){Check(false,TEXT("roles fixture hero"));return false;}
        // Every archetype exposes clean UI data.
        int32 Index=0;
        for(const auto& Pair:D.Archetypes)
        {
            auto* M=F.Monster(FVector(-2000,-1000+Index++*150,0),Pair.Key);if(!M){Check(false,TEXT("archetype spawned"));continue;}
            const auto Abilities=M->NPCState->Abilities();
            Check(M->GetNPCRole()==Pair.Value.Role&&M->GetNPCDisplayName()==Pair.Value.DisplayName&&!Abilities.IsEmpty()&&
                Abilities.ContainsByPredicate([](const FCireNPCAbilityInfo& I){return I.bBasic;})&&M->NPCState->ArchetypeId==Pair.Key,TEXT("archetype read API"));
            Check(M->GetNPCClassification()==Pair.Value.Classification,TEXT("classification from data"));
            Check(M->NPCState->GetIsReplicated(),TEXT("NPC state replicates"));
            CireNPCCombat::Interrupt(M);M->Destroy();Mode->Monsters.Remove(M);
        }
        // Elite promotion and lane boss.
        auto* Elite=F.Monster(FVector(-2000,800,0),TEXT("blight_caster"),2,4242);
        Check(Elite&&Elite->GetNPCClassification()==ECireNPCClass::Elite&&Elite->Tier==2,TEXT("challenge units are elite"));
        if(Elite){Elite->Destroy();Mode->Monsters.Remove(Elite);}
        auto* Boss=F.Monster(FVector(-2000,1000,0),D.WaveBoss);
        if(Boss)CireNPCCombat::ConfigureArchetype(Boss,D.WaveBoss,1,0,1,true);
        Check(Boss&&Boss->IsLaneBoss()&&Boss->LeakCostOverride==10&&Boss->GetNPCClassification()==ECireNPCClass::Boss,TEXT("wave boss leaks for 10 and shows a boss frame"));
        if(Boss){Boss->Destroy();Mode->Monsters.Remove(Boss);}

        // ---- Tank ----
        auto* Tank=F.Monster(FVector::ZeroVector,TEXT("hollow_shieldbearer"));
        auto* Ally=F.Monster(FVector(0,400,0),TEXT("hollow_infantry"));
        if(!Tank||!Ally){Check(false,TEXT("tank fixture"));return false;}
        float Before=Tank->Health;CireCombat::ApplyDamage(Hero,Tank,100,TEXT("armor test"));
        Check(FMath::IsNearlyEqual(Before-Tank->Health,75.f,.5f),TEXT("tank armor mitigates 25%"));
        CireThreat::Engage(Tank,Hero);Ready(Tank);CireNPCCombat::Tick(Tank,.01f);
        Check(Tank->CastingAbility==TEXT("npc_tank_provoke"),TEXT("tank opens with a provoking roar"));
        FinishCast(Tank);
        Check(Tank->NPCState->ProvokedUntil.FindRef(Hero)>Now(Tank)&&Tank->NPCState->HasStatus(CireNPCStatus::Provoking),TEXT("roar provokes nearby champions"));
        Before=Ally->Health;CireCombat::ApplyDamage(Hero,Ally,100,TEXT("provoke test"));
        Check(FMath::IsNearlyEqual(Before-Ally->Health,65.f,.5f),TEXT("provoked champion deals 35% less to others"));
        Before=Tank->Health;CireCombat::ApplyDamage(Hero,Tank,100,TEXT("provoke tank test"));
        Check(FMath::IsNearlyEqual(Before-Tank->Health,75.f,.5f),TEXT("provoked champion hits the tank normally"));
        Tank->NPCState->ProvokedUntil.Reset();
        Ally->Health=Ally->MaxHealth*.5f;Ready(Tank);Tank->NPCState->ReadyAt.Add(TEXT("npc_tank_provoke"),Now(Tank)+60);
        CireNPCCombat::Tick(Tank,.01f);Check(Tank->CastingAbility==TEXT("npc_tank_guard"),TEXT("tank guards an injured ally"));
        FinishCast(Tank);
        Check(Ally->NPCState->Guardian==Tank&&Ally->NPCState->HasStatus(CireNPCStatus::Guarded),TEXT("guard links the ally to its guardian"));
        const float AllyBefore=Ally->Health,TankBefore=Tank->Health;CireCombat::ApplyDamage(Hero,Ally,100,TEXT("guard test"));
        Check(FMath::IsNearlyEqual(AllyBefore-Ally->Health,60.f,.5f)&&FMath::IsNearlyEqual(TankBefore-Tank->Health,30.f,.5f),TEXT("guard redirects 40% to the armored tank"));
        Tank->Health=Tank->MaxHealth*.4f;Ready(Tank);Tank->NPCState->ReadyAt.Add(TEXT("npc_tank_provoke"),Now(Tank)+60);Tank->NPCState->ReadyAt.Add(TEXT("npc_tank_guard"),Now(Tank)+60);
        CireNPCCombat::Tick(Tank,.01f);Check(Tank->CastingAbility==TEXT("npc_tank_wall"),TEXT("tank raises shield wall when low"));
        FinishCast(Tank);Before=Tank->Health;CireCombat::ApplyDamage(Hero,Tank,100,TEXT("wall test"));
        Check(FMath::IsNearlyEqual(Before-Tank->Health,37.5f,.5f),TEXT("shield wall halves damage after armor"));
        CireNPCCombat::Interrupt(Tank);Tank->Destroy();Mode->Monsters.Remove(Tank);Ally->Destroy();Mode->Monsters.Remove(Ally);

        // ---- Caster ----
        auto* Caster=F.Monster(FVector::ZeroVector,TEXT("blight_caster"));auto* Hurt=F.Monster(FVector(0,500,0),TEXT("hollow_infantry"));
        if(!Caster||!Hurt){Check(false,TEXT("caster fixture"));return false;}
        Hero->SetActorLocation(F.Ground+FVector(200,0,92));CireThreat::Engage(Caster,Hero);Caster->AbilityTimer=10;Caster->AttackTimer=10;
        Caster->ConsumeMovementInputVector();CireNPCCombat::Tick(Caster,.01f);
        Check(Caster->NPCState->KiteUntil>Now(Caster)&&(Caster->GetPendingMovementInputVector()|(Hero->GetActorLocation()-Caster->GetActorLocation()).GetSafeNormal2D())<0,TEXT("caster backs away from melee"));
        Caster->NPCState->KiteUntil=0;Hero->SetActorLocation(F.Ground+FVector(520,0,92));
        Hurt->Health=Hurt->MaxHealth*.4f;Ready(Caster);CireNPCCombat::Tick(Caster,.01f);
        Check(Caster->CastingAbility==TEXT("npc_caster_mend")&&Caster->NPCState->bCastInterruptible&&Caster->NPCState->CastInfo().bCasting,TEXT("caster starts an interruptible heal with a cast bar"));
        Check(CireNPCCombat::InterruptCast(Caster,Hero)&&Caster->CastingAbility.IsEmpty(),TEXT("champion interrupt stops the heal"));
        Ready(Caster);CireNPCCombat::Tick(Caster,.01f);const float HurtBefore=Hurt->Health;FinishCast(Caster);
        Check(FMath::IsNearlyEqual(Hurt->Health-HurtBefore,Hurt->MaxHealth*.2f,1.f),TEXT("completed heal restores 20%"));
        CireNPCCombat::Interrupt(Caster);Caster->Destroy();Mode->Monsters.Remove(Caster);Hurt->Destroy();Mode->Monsters.Remove(Hurt);

        // ---- Ranged ----
        auto* Hunter=F.Monster(FVector::ZeroVector,TEXT("barbed_hunter"));
        if(!Hunter){Check(false,TEXT("hunter fixture"));return false;}
        Hero->SetActorLocation(F.Ground+FVector(200,0,92));CireThreat::Engage(Hunter,Hero);Ready(Hunter);CireNPCCombat::Tick(Hunter,.01f);
        Check(Hunter->NPCState->ReadyAt.FindRef(TEXT("npc_hunter_disengage"))>Now(Hunter)&&
            (Hunter->GetCharacterMovement()->PendingLaunchVelocity|(Hero->GetActorLocation()-Hunter->GetActorLocation()).GetSafeNormal2D())<0,TEXT("hunter disengages from melee"));
        Hunter->GetCharacterMovement()->PendingLaunchVelocity=FVector::ZeroVector;Hunter->AbilityTimer=10;Hunter->ConsumeMovementInputVector();CireNPCCombat::Tick(Hunter,.01f);
        Check((Hunter->GetPendingMovementInputVector()|(Hero->GetActorLocation()-Hunter->GetActorLocation()).GetSafeNormal2D())<0,TEXT("hunter kites while disengage is cooling down"));
        Hunter->NPCState->KiteUntil=0;Hunter->NPCState->KiteReadyAt=Now(Hunter)+10;Hero->SetActorLocation(F.Ground+FVector(500,0,92));Hunter->AttackTimer=0;CireNPCCombat::Tick(Hunter,.01f);
        Check(Hunter->CastingAbility==TEXT("npc_barbed_shot")&&!Hunter->NPCState->bCastInterruptible,TEXT("hunter shoots at range"));
        CireNPCCombat::Interrupt(Hunter);Hunter->Destroy();Mode->Monsters.Remove(Hunter);

        // ---- Bruiser ----
        auto* Bruiser=F.Monster(FVector::ZeroVector,TEXT("ironbound_bruiser"));
        if(!Bruiser){Check(false,TEXT("bruiser fixture"));return false;}
        Hero->SetActorLocation(F.Ground+FVector(650,0,92));CireThreat::Engage(Bruiser,Hero);Ready(Bruiser);CireNPCCombat::Tick(Bruiser,.01f);
        bool bLine=false;
        for(TActorIterator<ACireAreaEffect> It(Mode->GetWorld());It;++It)bLine|=It->GetOwner()==Bruiser&&It->AreaSpec.Shape==ECireAreaShape::Line&&!It->IsActorBeingDestroyed();
        Check(Bruiser->CastingAbility==TEXT("npc_bruiser_charge")&&bLine,TEXT("bruiser telegraphs a charge line"));
        FinishCast(Bruiser);Check(Bruiser->NPCState->DashUntil>Now(Bruiser)&&Bruiser->NPCState->HasStatus(CireNPCStatus::Charging),TEXT("charge release starts the dash"));
        Bruiser->ConsumeMovementInputVector();CireNPCCombat::Tick(Bruiser,.01f);
        Check((Bruiser->GetPendingMovementInputVector()|(Hero->GetActorLocation()-Bruiser->GetActorLocation()).GetSafeNormal2D())>.9f,TEXT("bruiser dashes toward the target"));
        CireNPCCombat::Interrupt(Bruiser);Bruiser->Destroy();Mode->Monsters.Remove(Bruiser);

        // ---- Pack Leader ----
        const int32 Pack=4343;
        TArray<ACireMonster*> PackUnits;
        for(int32 I=0;I<D.PackMembers.Num();++I)PackUnits.Add(F.Monster(FVector(-300,(I-1)*200,0),D.PackMembers[I],2,Pack));
        auto* Leader=F.Monster(FVector::ZeroVector,D.PackLeader,2,Pack);
        auto* Member=PackUnits.IsEmpty()?nullptr:PackUnits[0];
        if(!Leader||!Member){Check(false,TEXT("pack fixture"));return false;}
        Check(Leader->GetNPCClassification()==ECireNPCClass::Boss&&!Leader->IsLaneBoss()&&Leader->GetActorScale3D().X>Member->GetActorScale3D().X*1.3f&&
            Leader->MaxHealth>Member->MaxHealth*2.f,TEXT("pack leader is a larger boss-framed unit with a big health pool"));
        int32 Telegraphs=0;for(const auto& I:Leader->NPCState->Abilities())Telegraphs+=!I.bBasic&&I.CastTime>0;
        Check(Telegraphs>=3&&Leader->NPCState->Abilities().Num()>=4,TEXT("pack leader has 3-4 telegraphed abilities"));
        Hero->SetActorLocation(F.Ground+FVector(180,0,92));CireThreat::Engage(Leader,Hero);Ready(Leader);CireNPCCombat::Tick(Leader,.01f);
        Check(Leader->CastingAbility==TEXT("boss_leader_rally")&&!Leader->NPCState->bCastInterruptible,TEXT("leader roars to rally the pack"));
        Check(!CireNPCCombat::InterruptCast(Leader,Hero)&&!Leader->CastingAbility.IsEmpty(),TEXT("boss casts cannot be kicked"));
        const float MemberDamage=CireNPCCombat::EffectiveDamage(Member);FinishCast(Leader);
        Check(Member->NPCState->HasStatus(CireNPCStatus::Rallied)&&FMath::IsNearlyEqual(CireNPCCombat::EffectiveDamage(Member),MemberDamage*1.25f,.01f),TEXT("rally buffs pack damage by 25%"));
        Ready(Leader);Leader->NPCState->ReadyAt.Add(TEXT("boss_leader_rally"),Now(Leader)+60);CireNPCCombat::Tick(Leader,.01f);
        bool bCone=false;
        for(TActorIterator<ACireAreaEffect> It(Mode->GetWorld());It;++It)bCone|=It->GetOwner()==Leader&&It->AreaSpec.Shape==ECireAreaShape::Cone&&!It->IsActorBeingDestroyed();
        Check(Leader->CastingAbility==TEXT("boss_leader_cleave")&&bCone,TEXT("leader telegraphs a cleave cone"));
        CireNPCCombat::Interrupt(Leader);
        auto* Far=F.Hero(FVector(1000,150,0),2);
        if(Far){CireThreat::Engage(Leader,Far);}
        Ready(Leader);Leader->NPCState->ReadyAt.Add(TEXT("boss_leader_rally"),Now(Leader)+60);Leader->NPCState->ReadyAt.Add(TEXT("boss_leader_cleave"),Now(Leader)+60);
        CireNPCCombat::Tick(Leader,.01f);
        Check(Far&&Leader->CastingAbility==TEXT("boss_leader_charge")&&FVector::Dist2D(Leader->PendingAim,Far->GetActorLocation())<5.f,TEXT("leader charges the farthest champion"));
        CireNPCCombat::Interrupt(Leader);if(Far){CireThreat::Remove(Far);Far->Destroy();Mode->Heroes.Remove(Far);}
        Leader->NPCState->RallyUntil=0;Leader->Health=Leader->MaxHealth*.25f;Leader->AbilityTimer=10;Leader->NPCState->ReadyAt.Reset();
        CireNPCCombat::Tick(Leader,.01f);Check(Leader->CastingAbility==TEXT("boss_leader_frenzy"),TEXT("leader enrages at low health even during its global cooldown"));
        FinishCast(Leader);
        Check(Leader->NPCState->bEnraged&&Leader->NPCState->HasStatus(CireNPCStatus::Enraged)&&FMath::IsNearlyEqual(CireNPCCombat::EffectiveDamage(Leader),Leader->Damage*1.5f,.01f),TEXT("enrage adds 50% damage"));
        // Whole-pack reward rule includes the leader.
        Mode->RewardedPacks.Remove(Pack);
        for(auto* Unit:PackUnits)if(Unit){Unit->Health=1;CireCombat::ApplyDamage(Hero,Unit,1000,TEXT("pack test"));}
        Check(!Mode->RewardedPacks.Contains(Pack),TEXT("pack reward waits for the leader"));
        Leader->Health=1;CireCombat::ApplyDamage(Hero,Leader,1000,TEXT("pack test"));
        Check(Mode->RewardedPacks.Contains(Pack),TEXT("killing the leader completes the challenge reward"));
        // Route abstraction delegates to the current lane path.
        auto* Walker=F.Monster(FVector(0,-600,0),TEXT("hollow_infantry"));
        Check(Walker&&CireNPCCombat::RouteDestination(Walker).Equals(CireLanePath::NextWaypoint(Walker)),TEXT("route abstraction uses the lane path"));
    }
    UE_LOG(LogCireNPCTests,Display,TEXT("CIRE_NPC_ROLES_%s checks=%d"),bPass?TEXT("PASS"):TEXT("FAIL"),Checks);
    return bPass;
}
#endif
