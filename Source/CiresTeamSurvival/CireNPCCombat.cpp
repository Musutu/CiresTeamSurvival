#include "CireNPCCombat.h"
#include "CireGame.h"
#include "CireThreat.h"
#include "CireSkillTuning.h"
#include "CireSkillshot.h"
#include "CireConstruct.h"
#include "CireAreaEffects.h"
#include "CireAttackSystem.h"
#include "CireCombatEvents.h"
#include "CireRealm.h"
#include "CireTownGoal.h"
#include "CireDeveloperTools.h"
#include "CireLanePath.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"

DEFINE_LOG_CATEGORY_STATIC(LogCireNPCCombat,Log,All);

namespace
{
bool ClearSight(const AActor* From,const AActor* To)
{
    if(!IsValid(From)||!IsValid(To))return false;
    FHitResult Hit;FCollisionQueryParams Query(SCENE_QUERY_STAT(CireNPCSight),false,From);
    const bool bBlocked=From->GetWorld()->LineTraceSingleByChannel(Hit,From->GetActorLocation()+FVector(0,0,35),
        To->GetActorLocation()+FVector(0,0,35),ECC_Visibility,Query);
    return !bBlocked||Hit.GetActor()==To;
}
FVector Feet(const ACharacter* Actor)
{
    return Actor->GetActorLocation()-FVector(0,0,Actor->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
}
void ClearCast(ACireMonster* M)
{
    M->CastingAbility.Reset();M->CastStartedAt=0;M->CastEndsAt=0;M->bPendingSkillshot=false;M->ForceNetUpdate();
}
void BeginCast(ACireMonster* M,const TCHAR* Id,float Duration,FVector Aim,bool bProjectile)
{
    const float Now=M->GetWorld()->GetTimeSeconds();
    M->CastingAbility=Id;M->CastStartedAt=Now;M->CastEndsAt=Now+Duration;
    M->PendingAim=Aim;M->bPendingSkillshot=bProjectile;
    M->GetCharacterMovement()->StopMovementImmediately();
    M->SetActorRotation((Aim-M->GetActorLocation()).GetSafeNormal2D().Rotation());M->ForceNetUpdate();
    CireCombat::PlayCue(M,M->Victim,FName(Id),M->GetActorLocation(),Aim,ECireSpellCue::Cast,1.f,true);
}
void StartLeash(ACireMonster* M)
{
    CireNPCCombat::Interrupt(M);CireThreat::Clear(M);M->bEngaged=false;M->LeashTimer=8.f;
}
// Find the near surface in the wall's local frame; a long or rotated wall must
// not grant a monster a center-based attack from hundreds of units away.
FVector WallSurface(const ACireMonster* M,const ACireConstruct* Wall)
{
    const FTransform T=Wall->GetActorTransform();FVector Local=T.InverseTransformPosition(M->GetActorLocation());
    Local.X=FMath::Clamp(Local.X,-Wall->ConstructSpec.Depth*.5f,Wall->ConstructSpec.Depth*.5f);
    Local.Y=FMath::Clamp(Local.Y,-Wall->ConstructSpec.Width*.5f,Wall->ConstructSpec.Width*.5f);
    return T.TransformPosition(Local);
}
bool HandleWall(ACireMonster* M,FVector Destination)
{
    auto* Wall=ACireConstruct::FindBlockingConstruct(M,Destination);if(!Wall)return false;
    const FVector Near=WallSurface(M,Wall),Direction=(Near-M->GetActorLocation()).GetSafeNormal2D();
    const float Reach=M->GetCapsuleComponent()->GetScaledCapsuleRadius()+105.f;
    if(FVector::DistSquared2D(M->GetActorLocation(),Near)>FMath::Square(Reach))M->AddMovementInput(Direction);
    else
    {
        M->GetCharacterMovement()->StopMovementImmediately();
        M->SetActorRotation((Wall->GetActorLocation()-M->GetActorLocation()).GetSafeNormal2D().Rotation());
        if(M->AttackTimer<=0&&Wall->CanBeDamagedBy(M))
        {
            M->AttackTimer=1.8f;CireCombat::ApplyDamage(M,Wall,M->Damage,TEXT("Breach wall"));
            CireCombat::PlayCue(M,Wall,TEXT("npc_wall_strike"),M->GetActorLocation(),Near,ECireSpellCue::Impact,.8f,true);
        }
    }
    return true;
}
void MarchLane(ACireMonster* M,ACireGameMode* Mode)
{
    for(TActorIterator<ACireTownGoal> It(M->GetWorld());It;++It)
        if(It->TeamId==M->Lane&&It->ContainsLocation(M->GetActorLocation())){Mode->Leak(M);return;}
    const FVector Destination=CireLanePath::NextWaypoint(M);
    if(!HandleWall(M,Destination))M->AddMovementInput((Destination-M->GetActorLocation()).GetSafeNormal2D());
}
bool StartArea(ACireMonster* M,bool bPool)
{
    FCireAreaSpec S;
    S.Shape=bPool?ECireAreaShape::Circle:ECireAreaShape::Cone;
    S.Radius=bPool?220.f:360.f;S.ConeAngleDegrees=80.f;S.WarningSeconds=bPool?1.2f:1.1f;
    S.DurationSeconds=bPool?5.f:.3f;S.TickInterval=.5f;
    S.bPersistent=bPool;S.bPoison=bPool;S.DamagePerSecond=bPool?12.f:0.f;
    S.BurstDamage=bPool?0.f:FMath::Min(M->Damage*2.5f,10000.f);
    S.Color=bPool?FLinearColor(.28f,.52f,.08f,.35f):FLinearColor(.8f,.24f,.06f,.35f);
    S.AbilityName=bPool?TEXT("Blight Pool"):TEXT("Bruiser Slam");
    const FVector Ground=bPool?Feet(M->Victim):Feet(M);
    const FRotator Heading=(M->Victim->GetActorLocation()-M->GetActorLocation()).GetSafeNormal2D().Rotation();
    if(!ACireAreaEffect::Spawn(M,S,Ground,Heading))return false;
    BeginCast(M,bPool?TEXT("npc_blight_pool"):TEXT("npc_bruiser_slam"),S.WarningSeconds,M->Victim->GetActorLocation(),false);
    M->AbilityTimer=bPool?12.f:8.f;M->AttackTimer=FMath::Max(M->AttackTimer,S.WarningSeconds+.7f);return true;
}
void ReleaseProjectile(ACireMonster* M)
{
    const FString Id=M->CastingAbility;
    if(const auto* Authored=CireSkillTuning::FindSkillshot(Id))
    {
        auto S=*Authored;S.Damage=M->Damage;S.bCanCrit=false;S.WarningSeconds=0;
        ACireSkillshot::Spawn(M,S,M->PendingAim,Id==TEXT("npc_shadow_bolt")?TEXT("Shadow Bolt"):TEXT("Barbed Shot"));
    }
    // The one-second cast was the warning. Do not delay the spawned projectile
    // a second time, and do not update its recorded direction to chase a target.
    M->AttackTimer=2.2f;ClearCast(M);
}
}

void CireNPCCombat::Configure(ACireMonster* M,int32 Kind,int32 Wave,bool bBoss,int32 Tier,int32 Round)
{
    if(!IsValid(M)||!M->HasAuthority())return;
    const auto& T=CireSkillTuning::Get();Kind=FMath::Clamp(Kind,0,3);Tier=FMath::Clamp(Tier,0,10);
    const float HealthFactors[]={T.BasicHealthMultiplier,T.BruiserHealthMultiplier,T.CasterHealthMultiplier,T.RangedHealthMultiplier};
    const float Damages[]={T.NormalMonsterDamage,T.BruiserMonsterDamage,T.CasterMonsterDamage,T.RangedMonsterDamage};
    const float Speeds[]={210.f,185.f,195.f,205.f};
    const TCHAR* Names[]={TEXT("Hollow Infantry"),TEXT("Ironbound Bruiser"),TEXT("Blight Caster"),TEXT("Barbed Hunter")};
    if(M->bArmoredEscort)M->GetCapsuleComponent()->ClearMoveIgnoreActors();
    M->bArmoredEscort=false;M->LeakCostOverride=0;
    M->CombatArchetype=Kind;M->bBoss=bBoss;M->Tier=Tier;
    const double Base=Tier>0?T.ChallengeHealthBase*Cires::ChallengeHealthMultiplier(Tier,FMath::Clamp(Round,1,100)):
        T.WaveHealthBase+T.WaveHealthPerWave*FMath::Clamp(Wave,1,10000);
    M->MaxHealth=M->Health=static_cast<float>(FMath::Clamp(Base*HealthFactors[Kind]*(bBoss?T.BossHealthMultiplier:1.f),1.,1.e9));
    M->Damage=bBoss?T.BossMonsterDamage:Tier>0?T.ChallengeMonsterDamage:Damages[Kind];
    M->BaseMoveSpeed=Speeds[Kind]*(bBoss?.8f:1.f);M->GetCharacterMovement()->MaxWalkSpeed=M->BaseMoveSpeed;
    M->MonsterName=bBoss?FString::Printf(TEXT("Siegebreaker | %s"),Names[Kind]):Tier>0?FString::Printf(TEXT("Elite %d | %s"),Tier,Names[Kind]):FString(Names[Kind]);
    M->AbilityTimer=4.f;M->AttackTimer=1.f;M->LeashTimer=0;M->bEngaged=false;
    CireThreat::Clear(M);ClearCast(M);M->SpawnPosition=M->GetActorLocation();
    CireLanePath::InitializeProgress(M);
    CireDeveloperTools::AdjustMonster(M);M->ForceNetUpdate();
}

void CireNPCCombat::Interrupt(ACireMonster* M)
{
    if(!IsValid(M)||!M->HasAuthority())return;
    ClearCast(M);M->AbilityTimer=FMath::Max(M->AbilityTimer,3.f);M->AttackTimer=FMath::Max(M->AttackTimer,1.f);
    // Interrupt owns only this caster's spells. Clearing every membership here
    // would also forgive accumulated poison from hostile player-owned areas.
    for(TActorIterator<ACireAreaEffect> It(M->GetWorld());It;++It)if(It->GetOwner()==M)It->Destroy();
    ACireSkillshot::ClearForActor(M);
}

void CireNPCCombat::Tick(ACireMonster* M,float Delta)
{
    if(!IsValid(M))return;
    CireRealm::UpdateVisibility(M);
    const float Scale=M->bArmoredEscort?1.45f:M->bBoss?1.35f:M->Tier>0?1.08f+M->Tier*.09f:1.f;
    if(!M->GetActorScale3D().Equals(FVector(Scale)))M->SetActorScale3D(FVector(Scale));
    auto* Mode=M->GetWorld()->GetAuthGameMode<ACireGameMode>();
    if(!M->HasAuthority()||!Mode||M->Health<=0)return;
    auto* Movement=M->GetCharacterMovement();
    if(Mode->Clock.Phase()!=Cires::MatchPhase::Survival||M->Damage<=0)
    {
        if(!M->CastingAbility.IsEmpty())Interrupt(M);
        Movement->StopMovementImmediately();return;
    }
    if(!FMath::IsFinite(Delta)||Delta<0)return;
    const float Now=M->GetWorld()->GetTimeSeconds();
    if(M->BaseMoveSpeed<=0)M->BaseMoveSpeed=Movement->MaxWalkSpeed;
    Movement->MaxWalkSpeed=M->BaseMoveSpeed*(M->SlowUntil>Now?.65f:1.f);
    M->AttackTimer=FMath::Max(0.f,M->AttackTimer-Delta);M->AbilityTimer=FMath::Max(0.f,M->AbilityTimer-Delta);
    if(M->bArmoredEscort)
    {
        if(!M->CastingAbility.IsEmpty())Interrupt(M);
        CireThreat::Clear(M);M->bEngaged=false;
        CireLanePath::RefreshEscortCollision(M);MarchLane(M,Mode);return;
    }
    if(M->PackId>=0&&M->LeashTimer<=0&&FVector::DistSquared2D(M->GetActorLocation(),M->SpawnPosition)>FMath::Square(1700.f))StartLeash(M);
    if(M->LeashTimer>0)
    {
        CireThreat::Clear(M);
        if(FVector::DistSquared2D(M->GetActorLocation(),M->SpawnPosition)>FMath::Square(90.f))
        {
            M->LeashTimer=FMath::Max(.05f,M->LeashTimer-Delta);
            // Resetting packs cannot acquire fresh victims; solid summoned walls
            // still need to be breached on the route back to their spawn.
            if(!HandleWall(M,M->SpawnPosition))M->AddMovementInput((M->SpawnPosition-M->GetActorLocation()).GetSafeNormal2D());
        }
        else {M->LeashTimer=0;M->Health=M->MaxHealth;Movement->StopMovementImmediately();M->ForceNetUpdate();}
        return;
    }
    CireThreat::Select(M);
    if(!M->Victim&&M->Threat.IsEmpty())
    {
        ACireHero* Closest=nullptr;double Best=FMath::Square(700.f);
        for(TActorIterator<ACireHero> It(M->GetWorld());It;++It)
        {
            auto* H=*It;if(H->bDead||!H->bDrafted||H->Health<=0||H->TeamId!=M->Lane||!CireRealm::CanObserve(H,M))continue;
            const double Distance=FVector::DistSquared2D(M->GetActorLocation(),H->GetActorLocation());
            if(Distance<Best&&ClearSight(M,H)){Closest=H;Best=Distance;}
        }
        if(Closest){CireThreat::Engage(M,Closest);CireThreat::Select(M);}
    }
    if(!M->CastingAbility.IsEmpty())
    {
        Movement->StopMovementImmediately();
        if(!IsValid(M->Victim)){Interrupt(M);return;}
        if(Now>=M->CastEndsAt){if(M->bPendingSkillshot)ReleaseProjectile(M);else ClearCast(M);}
        return;
    }
    if(auto* Victim=M->Victim)
    {
        M->bEngaged=true;
        if(HandleWall(M,Victim->GetActorLocation()))return;
        const float Distance=static_cast<float>(FVector::Dist2D(M->GetActorLocation(),Victim->GetActorLocation()));
        const FVector Direction=(Victim->GetActorLocation()-M->GetActorLocation()).GetSafeNormal2D();
        const bool bRanged=M->CombatArchetype>=2;
        const bool bSight=ClearSight(M,Victim);
        if(bSight&&M->AbilityTimer<=0&&M->CombatArchetype==1&&Distance<=360.f&&StartArea(M,false))return;
        if(bSight&&M->AbilityTimer<=0&&M->CombatArchetype==2&&Distance<=650.f&&StartArea(M,true))return;
        if(Distance>(bRanged?650.f:170.f)||!bSight){M->AddMovementInput(Direction);return;}
        Movement->StopMovementImmediately();M->SetActorRotation(Direction.Rotation());
        if(M->AttackTimer<=0)
        {
            if(bRanged)
            {
                const TCHAR* Id=M->CombatArchetype==2?TEXT("npc_shadow_bolt"):TEXT("npc_barbed_shot");
                if(CireSkillTuning::FindSkillshot(Id))BeginCast(M,Id,1.f,Victim->GetActorLocation(),true);
                else M->AttackTimer=2.2f;
            }
            else
            {
                M->AttackTimer=1.8f;
                CireAttacks::Resolve(M,Victim,M->Damage,CireAttacks::Roll(M,Victim,false),TEXT("Monster attack"));
                CireCombat::PlayCue(M,Victim,TEXT("npc_melee"),M->GetActorLocation(),Victim->GetActorLocation(),ECireSpellCue::Impact,.8f,true);
            }
        }
        return;
    }
    if(M->PackId>=0){if(M->bEngaged)StartLeash(M);return;}
    MarchLane(M,Mode);
}

#if !UE_BUILD_SHIPPING
#include "Components/BoxComponent.h"
#include "Misc/ScopeExit.h"

bool CireNPCCombat::RunSmoke(ACireGameMode* Mode)
{
    if(!IsValid(Mode)||!Mode->HasAuthority())return false;
    bool bPassed=true;int32 Checks=0;
    auto Check=[&](bool Value,const TCHAR* Why){++Checks;if(!Value){bPassed=false;UE_LOG(LogCireNPCCombat,Error,TEXT("CIRE_NPC_CHECK_FAIL %s"),Why);}};
    const auto Clock=Mode->Clock;const auto Heroes=Mode->Heroes;const auto Monsters=Mode->Monsters;
    TArray<AActor*> Actors;
    ON_SCOPE_EXIT
    {
        for(auto* M:Mode->Monsters)if(IsValid(M))Interrupt(M);
        for(int32 I=Actors.Num()-1;I>=0;--I)if(IsValid(Actors[I]))Actors[I]->Destroy();
        Mode->Clock=Clock;Mode->Heroes=Heroes;Mode->Monsters=Monsters;
    };
    Mode->Clock=Cires::MatchClock();Mode->Heroes.Reset();Mode->Monsters.Reset();
    const FVector Ground(0,-2100,3000);
    auto* Floor=Mode->GetWorld()->SpawnActor<AActor>();
    if(Floor)
    {
        Actors.Add(Floor);auto* Box=NewObject<UBoxComponent>(Floor);Floor->SetRootComponent(Box);Floor->AddInstanceComponent(Box);
        Box->SetBoxExtent(FVector(2200,900,50));Box->SetCollisionObjectType(ECC_WorldStatic);
        Box->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);Box->SetCollisionResponseToAllChannels(ECR_Block);
        Box->RegisterComponent();Floor->SetActorLocation(Ground-FVector(0,0,50));
    }
    FActorSpawnParameters Params;Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    auto MakeHero=[&](FVector Offset,int32 Kind)
    {
        auto* H=Mode->GetWorld()->SpawnActor<ACireHero>(Ground+Offset+FVector(0,0,92),FRotator::ZeroRotator,Params);
        if(H){Actors.Add(H);H->SetActorTickEnabled(false);H->TeamId=0;H->Draft(Kind);H->Health=H->MaxHealth=10000;Mode->Heroes.Add(H);}return H;
    };
    auto* Tank=MakeHero(FVector(500,0,0),0);auto* Dps=MakeHero(FVector(80,100,0),2);
    auto* M=Mode->GetWorld()->SpawnActor<ACireMonster>(Ground+FVector(0,0,88),FRotator::ZeroRotator,Params);
    if(M){Actors.Add(M);M->SetActorTickEnabled(false);M->Lane=0;Mode->Monsters.Add(M);}
    if(!Floor||!Tank||!Dps||!M){Check(false,TEXT("fixture actors spawned"));return false;}
    Configure(M,0,1);const float FirstHealth=M->Health,FirstDamage=M->Damage;
    Configure(M,0,12);Check(M->Health>FirstHealth&&M->Damage==FirstDamage,TEXT("wave health increases while damage remains fixed"));
    Configure(M,2,1,false,1,1);const float EliteHealth=M->Health,EliteDamage=M->Damage;
    Configure(M,2,12,false,3,5);Check(M->Health>EliteHealth&&M->Damage==EliteDamage,TEXT("elite tier/round scales health only"));
    Configure(M,0,1);CireThreat::Damage(M,Tank,100);CireThreat::Damage(M,Dps,10);M->AttackTimer=10;M->AbilityTimer=10;
    Tick(M,.01f);Check(M->Victim==Tank,TEXT("nearer DPS cannot steal higher tank threat"));
    Configure(M,1,1);Tank->SetActorLocation(Ground+FVector(150,0,92));Dps->SetActorLocation(Ground+FVector(1500,0,92));
    CireThreat::Engage(M,Tank);M->AttackTimer=0;M->AbilityTimer=0;const float Before=Tank->Health;
    Tick(M,.01f);Check(M->CastingAbility==TEXT("npc_bruiser_slam"),TEXT("bruiser begins a telegraphed cone"));
    ACireAreaEffect* Warning=nullptr;
    for(TActorIterator<ACireAreaEffect> It(Mode->GetWorld());It;++It)if(It->GetOwner()==M&&!It->IsActorBeingDestroyed()){Warning=*It;Actors.Add(*It);break;}
    if(Warning)Warning->Tick(.5f);
    Check(Warning&&!Warning->IsActive()&&Tank->Health==Before,TEXT("warning remains harmless before impact"));
    Interrupt(M);Check(M->CastingAbility.IsEmpty()&&Warning&&Warning->IsActorBeingDestroyed(),TEXT("interrupt cancels windup and its warning"));
    Configure(M,2,1);Tank->SetActorLocation(Ground+FVector(500,0,92));CireThreat::Engage(M,Tank);M->AttackTimer=0;M->AbilityTimer=10;
    Tick(M,.01f);const FVector LockedAim=M->PendingAim;
    Check(M->bPendingSkillshot&&M->CastingAbility==TEXT("npc_shadow_bolt")&&M->CastEndsAt>M->CastStartedAt&&Tank->Health==Before,TEXT("caster winds up without direct damage"));
    Tank->SetActorLocation(Ground+FVector(500,200,92));M->CastEndsAt=Mode->GetWorld()->GetTimeSeconds()-.01f;Tick(M,.01f);
    ACireSkillshot* Shot=nullptr;
    for(TActorIterator<ACireSkillshot> It(Mode->GetWorld());It;++It)if(It->GetSourceActor()==M&&!It->IsActorBeingDestroyed()){Shot=*It;Actors.Add(*It);break;}
    Check(Shot&&Shot->ShotSpec.WarningSeconds==0&&Shot->ShotSpec.Damage==M->Damage&&Shot->Velocity.GetSafeNormal2D().Equals((LockedAim-M->GetActorLocation()).GetSafeNormal2D(),.01f),TEXT("cast releases fixed-direction projectile with fixed damage"));
    Interrupt(M);Tank->SetActorLocation(Ground+FVector(500,0,92));CireThreat::Engage(M,Tank);M->AbilityTimer=0;M->AttackTimer=10;Tick(M,.01f);
    Check(M->CastingAbility==TEXT("npc_blight_pool"),TEXT("caster secondary starts warned poison pool"));Interrupt(M);
    Configure(M,0,1);CireThreat::Engage(M,Tank);M->AttackTimer=0;FCireConstructSpec WallSpec;WallSpec.MaxHealth=1000;
    auto* Wall=ACireConstruct::Spawn(Tank,WallSpec,Ground+FVector(150,0,0),FRotator::ZeroRotator,TEXT("NPC fixture wall"));
    if(Wall)Actors.Add(Wall);const float HeroBefore=Tank->Health;Tick(M,.01f);
    Check(Wall&&Wall->Health<Wall->MaxHealth&&Tank->Health==HeroBefore,TEXT("blocked monster damages wall before victim"));
    M->PackId=999;M->Health=1;M->SetActorLocation(M->SpawnPosition+FVector(1800,0,0));Tick(M,.01f);
    Check(M->Victim==nullptr&&M->Threat.IsEmpty()&&M->LeashTimer>0,TEXT("pack leash clears threat"));
    M->SetActorLocation(M->SpawnPosition);Tick(M,.01f);Check(M->Health==M->MaxHealth&&M->LeashTimer==0,TEXT("pack heals only on return home"));
    UE_LOG(LogCireNPCCombat,Display,TEXT("CIRE_NPC_SMOKE_%s checks=%d"),bPassed?TEXT("PASS"):TEXT("FAIL"),Checks);return bPassed;
}
#endif
