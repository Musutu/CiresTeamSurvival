#include "CireNPCCombat.h"
#include "CireLoot.h" // progression-shop: NPC pause
#include "CireGame.h"
#include "CireThreat.h"
#include "CireNPCState.h"
#include "CireNPCArchetypes.h"
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
#include "CireBuffs.h" // aura-vfx
#include "CireMonsterArt.h" // creature-anim
#include "CireWaves.h" // wave-director

DEFINE_LOG_CATEGORY_STATIC(LogCireNPCCombat,Log,All);

namespace
{
UCireNPCState* St(const ACireMonster* M){return M?M->NPCState.Get():nullptr;}
const FCireNPCArchetype* Arch(const ACireMonster* M){const auto* S=St(M);return S?S->Archetype():nullptr;}
float NowOf(const ACireMonster* M){return M->GetWorld()->GetTimeSeconds();}
float HealthFraction(const ACireMonster* M){return M->MaxHealth>0?M->Health/M->MaxHealth:0.f;}
bool Alive(const ACireMonster* M){return IsValid(M)&&!M->IsActorBeingDestroyed()&&M->Health>0;}
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
// Heroes and monsters of this lane only (same rule as the threat table).
bool HeroTargetable(const ACireMonster* M,const ACireHero* H)
{
    return IsValid(H)&&!H->bDead&&H->bDrafted&&H->Health>0&&H->TeamId==M->Lane&&CireRealm::CanObserve(H,M);
}
bool Friendly(const ACireMonster* M,const ACireMonster* Other)
{
    return Alive(Other)&&Other!=M&&Other->Lane==M->Lane&&!Other->bArmoredEscort&&(M->PackId<0?Other->PackId<0:Other->PackId==M->PackId);
}
void ClearCast(ACireMonster* M)
{
    M->CastingAbility.Reset();M->CastStartedAt=0;M->CastEndsAt=0;M->bPendingSkillshot=false;
    if(auto* S=St(M)){S->CastAbilityId=NAME_None;S->bCastInterruptible=false;}
    M->ForceNetUpdate();
}
void BeginCast(ACireMonster* M,const FCireNPCAbility& A,FVector Aim,bool bProjectile)
{
    const float Now=NowOf(M);
    M->CastingAbility=A.Id.ToString();M->CastStartedAt=Now;M->CastEndsAt=Now+FMath::Max(A.CastTime,.05f);
    M->PendingAim=Aim;M->bPendingSkillshot=bProjectile;
    if(auto* S=St(M)){S->CastAbilityId=A.Id;S->bCastInterruptible=A.bInterruptible;}
    M->GetCharacterMovement()->StopMovementImmediately();
    const FVector Facing=(Aim-M->GetActorLocation()).GetSafeNormal2D();
    if(!Facing.IsNearlyZero())M->SetActorRotation(Facing.Rotation());
    M->ForceNetUpdate();
    CireCombat::PlayCue(M,M->Victim,A.Id,M->GetActorLocation(),Aim,ECireSpellCue::Cast,1.f,true);
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
            if(M->MonsterArt)M->MonsterArt->PresentInstantStrike(); // creature-anim: show the blow
            CireCombat::PlayCue(M,Wall,TEXT("npc_wall_strike"),M->GetActorLocation(),Near,ECireSpellCue::Impact,.8f,true);
        }
    }
    return true;
}
void MarchLane(ACireMonster* M,ACireGameMode* Mode)
{
    if(CireNPCCombat::ReachedGoal(M)){Mode->Leak(M);return;}
    const FVector Destination=CireNPCCombat::RouteDestination(M);
    if(!HandleWall(M,Destination))M->AddMovementInput((Destination-M->GetActorLocation()).GetSafeNormal2D());
}
float AttackPeriod(const ACireMonster* M,const FCireNPCArchetype* A)
{
    const auto* S=St(M);float Period=A?A->AttackInterval:1.8f;
    if(S&&S->RallyUntil>NowOf(M))Period/=1.f+S->RallyBonus;
    if(S&&S->bEnraged)Period*=.75f;
    return FMath::Max(.3f,Period);
}
bool SpawnArea(ACireMonster* M,const FCireNPCAbility& A,ECireAreaShape Shape,FVector Ground,FRotator Heading,float Length=0)
{
    FCireAreaSpec S;
    S.Shape=Shape;S.Radius=A.Radius;S.ConeAngleDegrees=FMath::Clamp(A.Angle,1.f,179.f);S.Length=Length>0?Length:A.Length;S.Width=A.Width;
    S.WarningSeconds=A.CastTime;S.TickInterval=.5f;
    const bool bPool=A.DamagePerSecond>0&&A.Duration>0;
    S.bPersistent=bPool;S.bPoison=bPool;S.DurationSeconds=bPool?A.Duration:.3f;
    S.DamagePerSecond=bPool?A.DamagePerSecond:0.f;
    S.BurstDamage=bPool?0.f:FMath::Min(CireNPCCombat::EffectiveDamage(M)*A.DamageMultiplier,10000.f);
    S.Color=A.Color;S.AbilityName=A.Name.Left(80);
    return ACireAreaEffect::Spawn(M,S,Ground,Heading)!=nullptr;
}
ACireHero* FarthestHero(ACireMonster* M,const FCireNPCAbility& A)
{
    ACireHero* Best=nullptr;double BestDistance=-1;
    for(TActorIterator<ACireHero> It(M->GetWorld());It;++It)
    {
        auto* H=*It;if(!HeroTargetable(M,H))continue;
        const double D=FVector::DistSquared2D(M->GetActorLocation(),H->GetActorLocation());
        if(D<FMath::Square(A.MinRange)||D>FMath::Square(A.Range)||D<=BestDistance||!ClearSight(M,H))continue;
        Best=H;BestDistance=D;
    }
    return Best;
}
ACireMonster* LowestAlly(ACireMonster* M,ACireGameMode* Mode,float Radius,float BelowFraction,bool bNotGuarded)
{
    ACireMonster* Best=nullptr;float BestFraction=BelowFraction;
    for(auto* Other:Mode->Monsters)
    {
        if(!Friendly(M,Other)||FVector::DistSquared2D(M->GetActorLocation(),Other->GetActorLocation())>FMath::Square(Radius))continue;
        const auto* OS=St(Other);
        if(bNotGuarded&&OS&&OS->GuardUntil>NowOf(M)&&OS->Guardian.IsValid()&&Alive(OS->Guardian.Get()))continue;
        const float F=HealthFraction(Other);if(F<BestFraction){Best=Other;BestFraction=F;}
    }
    return Best;
}
// Pending ally for guard/heal is stored as the cast aim location; resolve the nearest friendly there.
ACireMonster* AllyAt(ACireMonster* M,ACireGameMode* Mode,FVector Point)
{
    ACireMonster* Best=nullptr;double BestDistance=FMath::Square(250.f);
    for(auto* Other:Mode->Monsters)if(Friendly(M,Other))
    {
        const double D=FVector::DistSquared2D(Other->GetActorLocation(),Point);if(D<BestDistance){Best=Other;BestDistance=D;}
    }
    return Best;
}
void Committed(ACireMonster* M,const FCireNPCAbility& A)
{
    auto* S=St(M);const float Now=NowOf(M);
    if(S)S->ReadyAt.FindOrAdd(A.Id)=Now+A.CastTime+A.Cooldown;
    M->AbilityTimer=A.CastTime+1.5f;M->AttackTimer=FMath::Max(M->AttackTimer,A.CastTime+.7f);
}
bool StartAbility(ACireMonster* M,ACireGameMode* Mode,const FCireNPCAbility& A,ACireHero* Victim,float Distance,bool bSight)
{
    auto* S=St(M);if(!S)return false;
    const float Now=NowOf(M);
    const bool bInRange=Victim&&Distance>=A.MinRange&&Distance<=A.Range;
    switch(A.Kind)
    {
    case ECireNPCAbilityKind::Projectile:
        if(!bInRange||!bSight||!CireSkillTuning::FindSkillshot(A.Skillshot))return false;
        BeginCast(M,A,Victim->GetActorLocation(),true);break;
    case ECireNPCAbilityKind::Cone:
        if(!bInRange||!bSight||!SpawnArea(M,A,ECireAreaShape::Cone,Feet(M),(Victim->GetActorLocation()-M->GetActorLocation()).GetSafeNormal2D().Rotation()))return false;
        BeginCast(M,A,Victim->GetActorLocation(),false);break;
    case ECireNPCAbilityKind::TargetCircle:
        if(!bInRange||!bSight||!SpawnArea(M,A,ECireAreaShape::Circle,Feet(Victim),FRotator::ZeroRotator))return false;
        BeginCast(M,A,Victim->GetActorLocation(),false);break;
    case ECireNPCAbilityKind::SelfCircle:
        if(!bInRange||!SpawnArea(M,A,ECireAreaShape::Circle,Feet(M),FRotator::ZeroRotator))return false;
        BeginCast(M,A,Victim->GetActorLocation(),false);break;
    case ECireNPCAbilityKind::Charge:
    {
        ACireHero* Target=A.Targeting==TEXT("farthest")?FarthestHero(M,A):(bInRange&&bSight?Victim:nullptr);
        if(!Target)return false;
        const FVector From=M->GetActorLocation(),Direction=(Target->GetActorLocation()-From).GetSafeNormal2D();
        const float Length=FMath::Min(A.Length,static_cast<float>(FVector::Dist2D(From,Target->GetActorLocation()))+150.f);
        if(Direction.IsNearlyZero()||!SpawnArea(M,A,ECireAreaShape::Line,Feet(M),Direction.Rotation(),Length))return false;
        S->DashTarget=From+Direction*FMath::Max(50.f,Length-80.f);S->DashAbility=A.Id;
        BeginCast(M,A,Target->GetActorLocation(),false);break;
    }
    case ECireNPCAbilityKind::Guard:
    {
        auto* Ally=LowestAlly(M,Mode,A.Radius,.95f,true);if(!Ally)return false;
        BeginCast(M,A,Ally->GetActorLocation(),false);break;
    }
    case ECireNPCAbilityKind::HealAlly:
    {
        auto* Ally=LowestAlly(M,Mode,A.Radius,A.HealthThreshold,false);if(!Ally)return false;
        BeginCast(M,A,Ally->GetActorLocation(),false);break;
    }
    case ECireNPCAbilityKind::Provoke:
        if(!Victim||Distance>A.Radius)return false;
        BeginCast(M,A,Victim->GetActorLocation(),false);break;
    case ECireNPCAbilityKind::Rally:
    {
        bool bAnyAlly=false;
        for(auto* Other:Mode->Monsters)if(Friendly(M,Other)&&FVector::DistSquared2D(M->GetActorLocation(),Other->GetActorLocation())<=FMath::Square(A.Radius)){bAnyAlly=true;break;}
        if(!bAnyAlly||!Victim)return false;
        // Harmless gold ring shows the buff radius while the roar winds up.
        FCireNPCAbility Ring=A;Ring.DamageMultiplier=0;Ring.DamagePerSecond=0;
        SpawnArea(M,Ring,ECireAreaShape::Circle,Feet(M),FRotator::ZeroRotator);
        BeginCast(M,A,Victim->GetActorLocation(),false);break;
    }
    case ECireNPCAbilityKind::Enrage:
        if(S->bEnraged||HealthFraction(M)>A.HealthThreshold)return false;
        BeginCast(M,A,Victim?Victim->GetActorLocation():M->GetActorLocation()+M->GetActorForwardVector()*100,false);break;
    case ECireNPCAbilityKind::ShieldWall:
        if(HealthFraction(M)>A.HealthThreshold||S->ShieldWallUntil>Now)return false;
        BeginCast(M,A,Victim?Victim->GetActorLocation():M->GetActorLocation()+M->GetActorForwardVector()*100,false);break;
    case ECireNPCAbilityKind::Disengage:
    {
        if(!Victim||Distance>A.Range)return false;
        const FVector Away=(M->GetActorLocation()-Victim->GetActorLocation()).GetSafeNormal2D();
        if(Away.IsNearlyZero())return false;
        M->LaunchCharacter(Away*A.Length*1.5f+FVector(0,0,380),true,true);
        CireCombat::PlayCue(M,Victim,A.Id,M->GetActorLocation(),M->GetActorLocation()+Away*A.Length,ECireSpellCue::Cast,.8f,true);
        S->ReadyAt.FindOrAdd(A.Id)=Now+A.Cooldown;M->AbilityTimer=FMath::Max(M->AbilityTimer,.8f);return true;
    }
    default:return false;
    }
    Committed(M,A);return true;
}
// Priority order is the authored order. Enrage ignores the ability global cooldown.
bool TryAbilities(ACireMonster* M,ACireGameMode* Mode,ACireHero* Victim,float Distance,bool bSight)
{
    const auto* A=Arch(M);auto* S=St(M);if(!A||!S)return false;
    const float Now=NowOf(M);
    for(const auto& Ability:A->Abilities)
    {
        if(Ability.bBasic||S->ReadyAt.FindRef(Ability.Id)>Now)continue;
        if(M->AbilityTimer>0&&Ability.Kind!=ECireNPCAbilityKind::Enrage)continue;
        if(StartAbility(M,Mode,Ability,Victim,Distance,bSight))return true;
    }
    return false;
}
void ReleaseProjectile(ACireMonster* M,const FCireNPCAbility* A)
{
    const FString Id=M->CastingAbility;
    const FString SkillshotId=A?A->Skillshot:Id;
    if(const auto* Authored=CireSkillTuning::FindSkillshot(SkillshotId))
    {
        auto S=*Authored;S.Damage=CireNPCCombat::EffectiveDamage(M)*(A?A->DamageMultiplier:1.f);S.bCanCrit=false;S.WarningSeconds=0;
        ACireSkillshot::Spawn(M,S,M->PendingAim,A?A->Name:Id);
    }
    // The cast bar was the warning. Do not delay the spawned projectile a second
    // time, and do not update its recorded direction to chase a target.
    M->AttackTimer=AttackPeriod(M,Arch(M));ClearCast(M);
}
void ReleaseCast(ACireMonster* M,ACireGameMode* Mode)
{
    auto* S=St(M);const auto* Archetype=Arch(M);
    const FCireNPCAbility* A=Archetype&&S?Archetype->FindAbility(S->CastAbilityId):nullptr;
    if(M->bPendingSkillshot){ReleaseProjectile(M,A);return;}
    const float Now=NowOf(M);
    if(A&&S)switch(A->Kind)
    {
    case ECireNPCAbilityKind::Charge:
        S->DashUntil=Now+FMath::Clamp(static_cast<float>(FVector::Dist2D(M->GetActorLocation(),S->DashTarget))/1500.f,.1f,.9f);S->DashSpeed=1500.f;break;
    case ECireNPCAbilityKind::Guard:
        if(auto* Ally=AllyAt(M,Mode,M->PendingAim))if(auto* AS=St(Ally))
        {AS->Guardian=M;AS->GuardUntil=Now+A->Duration;AS->GuardFraction=A->Magnitude;AS->RefreshStatusFlags(Now);
         CireCombat::PlayCue(M,nullptr,A->Id,M->GetActorLocation(),Ally->GetActorLocation(),ECireSpellCue::Impact,.8f,true);}
        break;
    case ECireNPCAbilityKind::HealAlly:
        if(auto* Ally=AllyAt(M,Mode,M->PendingAim))
        {Ally->Health=FMath::Min(Ally->MaxHealth,Ally->Health+Ally->MaxHealth*A->Magnitude);Ally->ForceNetUpdate();
         CireCombat::PlayCue(M,nullptr,A->Id,M->GetActorLocation(),Ally->GetActorLocation(),ECireSpellCue::Impact,.8f,true);}
        break;
    case ECireNPCAbilityKind::Provoke:
        for(TActorIterator<ACireHero> It(M->GetWorld());It;++It)
        {
            auto* H=*It;if(!HeroTargetable(M,H)||FVector::DistSquared2D(M->GetActorLocation(),H->GetActorLocation())>FMath::Square(A->Radius))continue;
            S->ProvokedUntil.FindOrAdd(H)=Now+A->Duration; CireBuffs::Apply(H,TEXT("npc_tank_provoke_debuff"),A->Duration,M); // aura-vfx
            if(H->bBot)H->Target=M; // bots obey the taunt; human input is never forced
        }
        break;
    case ECireNPCAbilityKind::Rally:
        for(auto* Other:Mode->Monsters)
        {
            if(!(Other==M||Friendly(M,Other))||FVector::DistSquared2D(M->GetActorLocation(),Other->GetActorLocation())>FMath::Square(A->Radius))continue;
            if(auto* OS=St(Other)){OS->RallyUntil=Now+A->Duration;OS->RallyBonus=A->Magnitude;OS->RefreshStatusFlags(Now);Other->ForceNetUpdate();}
        }
        break;
    case ECireNPCAbilityKind::Enrage:
        S->bEnraged=true;S->EnrageBonus=A->Magnitude;
        M->BaseMoveSpeed*=1.25f;
        UE_LOG(LogCireNPCCombat,Display,TEXT("CIRE_NPC_ENRAGE %s"),*M->GetNPCDisplayName());break;
    case ECireNPCAbilityKind::ShieldWall:
        S->ShieldWallUntil=Now+A->Duration;S->ShieldWallReduction=A->Magnitude;break;
    default:break;
    }
    if(S)S->RefreshStatusFlags(Now);
    ClearCast(M);
}
float DesiredScale(const ACireMonster* M)
{
    const auto* A=Arch(M);const auto* S=St(M);
    // wave-director: director-spawned units use their archetype scale times the wave row's size.
    if(const float Size=CireWaveDirector::SizeScale(M);Size>0)return (A?A->Scale:1.f)*Size*(S&&S->bEnraged?1.08f:1.f);
    if(M->bArmoredEscort)return 1.45f;
    if(!A)return M->bBoss?1.35f:M->Tier>0?1.08f+M->Tier*.09f:1.f;
    float Scale=A->Scale;
    if(M->Tier>0&&A->Classification!=ECireNPCClass::Boss)Scale*=1.f+M->Tier*.06f;
    if(S&&S->bEnraged)Scale*=1.08f;
    return Scale;
}
bool UsesProjectiles(const FCireNPCArchetype* A){return A&&(A->Role==ECireNPCRole::Caster||A->Role==ECireNPCRole::Ranged);}
}

float CireNPCCombat::EffectiveDamage(const ACireMonster* M)
{
    if(!M)return 0.f;
    const auto* S=St(M);float Damage=M->Damage;
    if(S&&M->GetWorld()&&S->RallyUntil>NowOf(M))Damage*=1.f+S->RallyBonus;
    if(S&&S->bEnraged)Damage*=1.f+S->EnrageBonus;
    return Damage;
}

FVector CireNPCCombat::RouteDestination(ACireMonster* M){return CireLanePath::NextWaypoint(M);}
bool CireNPCCombat::ReachedGoal(ACireMonster* M)
{
    if(!IsValid(M))return false;
    for(TActorIterator<ACireTownGoal> It(M->GetWorld());It;++It)
        if(It->TeamId==M->Lane&&It->ContainsLocation(M->GetActorLocation()))return true;
    return false;
}

void CireNPCCombat::Configure(ACireMonster* M,int32 Kind,int32 Wave,bool bBoss,int32 Tier,int32 Round)
{
    Kind=FMath::Clamp(Kind,0,3);
    const FName Id=bBoss?CireNPCArchetypes::Get().WaveBoss:CireNPCArchetypes::LegacyKind(Kind);
    if(ConfigureArchetype(M,Id,Wave,Tier,Round,bBoss)&&!bBoss){M->CombatArchetype=Kind;M->ForceNetUpdate();}
}

bool CireNPCCombat::ConfigureArchetype(ACireMonster* M,FName Id,int32 Wave,int32 Tier,int32 Round,bool bLaneBoss)
{
    if(!IsValid(M)||!M->HasAuthority())return false;
    const FCireNPCArchetype* A=CireNPCArchetypes::Find(Id);
    if(!A){UE_LOG(LogCireNPCCombat,Warning,TEXT("Unknown NPC archetype %s; using legacy infantry"),*Id.ToString());A=CireNPCArchetypes::Find(CireNPCArchetypes::LegacyKind(0));}
    if(!A)return false;
    const auto& T=CireSkillTuning::Get();Tier=FMath::Clamp(Tier,0,10);
    if(M->bArmoredEscort)M->GetCapsuleComponent()->ClearMoveIgnoreActors();
    M->bArmoredEscort=false;M->LeakCostOverride=bLaneBoss?A->LeakCost:0;
    M->bBoss=bLaneBoss;M->Tier=Tier;
    M->CombatArchetype=A->Role==ECireNPCRole::Caster?2:A->Role==ECireNPCRole::Ranged?3:A->Role==ECireNPCRole::Bruiser?1:0;
    // Legacy archetypes read their health factor and damage from CombatTuning.json
    // so the existing balance knobs stay the single source of truth.
    const float TuningHealth[]={T.BasicHealthMultiplier,T.BruiserHealthMultiplier,T.CasterHealthMultiplier,T.RangedHealthMultiplier};
    const float TuningDamage[]={T.NormalMonsterDamage,T.BruiserMonsterDamage,T.CasterMonsterDamage,T.RangedMonsterDamage};
    const bool bTuned=A->TuningKind>=0&&A->TuningKind<4;
    const float HealthFactor=(bTuned?TuningHealth[A->TuningKind]:A->HealthMultiplier)*(bLaneBoss?T.BossHealthMultiplier:1.f);
    const double Base=Tier>0?T.ChallengeHealthBase*Cires::ChallengeHealthMultiplier(Tier,FMath::Clamp(Round,1,100)):
        T.WaveHealthBase+T.WaveHealthPerWave*FMath::Clamp(Wave,1,10000);
    M->MaxHealth=M->Health=static_cast<float>(FMath::Clamp(Base*HealthFactor,1.,1.e9));
    const bool bBossClass=A->Classification==ECireNPCClass::Boss;
    M->Damage=bLaneBoss?T.BossMonsterDamage:bBossClass?A->Damage:Tier>0?T.ChallengeMonsterDamage*A->EliteDamageMultiplier:
        bTuned?TuningDamage[A->TuningKind]:A->Damage;
    M->BaseMoveSpeed=A->MoveSpeed;M->GetCharacterMovement()->MaxWalkSpeed=M->BaseMoveSpeed;
    const ECireNPCClass Class=bBossClass?ECireNPCClass::Boss:Tier>0?ECireNPCClass::Elite:A->Classification;
    M->MonsterName=Class==ECireNPCClass::Boss?FString::Printf(TEXT("BOSS | %s"),*A->DisplayName):
        Tier>0?FString::Printf(TEXT("Elite %d | %s"),Tier,*A->DisplayName):A->DisplayName;
    M->AbilityTimer=4.f;M->AttackTimer=1.f;M->LeashTimer=0;M->bEngaged=false;
    CireThreat::Clear(M);ClearCast(M);M->SpawnPosition=M->GetActorLocation();
    if(auto* S=St(M))
    {
        S->ResetRuntime();S->ArchetypeId=A->Id;S->Role=A->Role;S->Classification=Class;
        const float Now=NowOf(M);
        for(const auto& Ability:A->Abilities)if(Ability.InitialCooldown>0)S->ReadyAt.Add(Ability.Id,Now+Ability.InitialCooldown);
        S->ApplyVisuals();
    }
    M->SetActorScale3D(FVector(DesiredScale(M)));
    CireLanePath::InitializeProgress(M);
    CireDeveloperTools::AdjustMonster(M);M->ForceNetUpdate();
    return true;
}

void CireNPCCombat::Interrupt(ACireMonster* M)
{
    if(!IsValid(M)||!M->HasAuthority())return;
    ClearCast(M);M->AbilityTimer=FMath::Max(M->AbilityTimer,3.f);M->AttackTimer=FMath::Max(M->AttackTimer,1.f);
    if(M->MonsterArt)M->MonsterArt->CancelSwing(); // creature-anim: an interrupted swing never lands
    if(auto* S=St(M)){S->DashUntil=0;S->KiteUntil=0;}
    // Interrupt owns only this caster's spells. Clearing every membership here
    // would also forgive accumulated poison from hostile player-owned areas.
    for(TActorIterator<ACireAreaEffect> It(M->GetWorld());It;++It)if(It->GetOwner()==M)It->Destroy();
    ACireSkillshot::ClearForActor(M);
}

bool CireNPCCombat::InterruptCast(ACireMonster* M,ACireHero* Source)
{
    if(!IsValid(M)||!M->HasAuthority()||M->CastingAbility.IsEmpty())return false;
    const auto* S=St(M);
    // Units without data (fixtures) keep the old always-interruptible behaviour.
    if(S&&!S->ArchetypeId.IsNone()&&!S->bCastInterruptible)return false;
    const FString Name=S&&Arch(M)&&Arch(M)->FindAbility(S->CastAbilityId)?Arch(M)->FindAbility(S->CastAbilityId)->Name:M->CastingAbility;
    Interrupt(M);
    if(Source)CireCombat::PlayCue(Source,M,TEXT("npc_interrupted"),Source->GetActorLocation(),M->GetActorLocation(),ECireSpellCue::Impact,.8f,true);
    UE_LOG(LogCireNPCCombat,Verbose,TEXT("%s interrupted %s"),Source?*Source->HeroName:TEXT("?"),*Name);
    return true;
}

float CireNPCCombat::ModifyIncomingDamage(ACireMonster* M,ACireHero* Attacker,float Amount)
{
    static int32 Depth=0;
    if(!IsValid(M)||!M->HasAuthority()||!FMath::IsFinite(Amount)||Amount<=0)return Amount;
    auto* S=St(M);const auto* A=Arch(M);const float Now=NowOf(M);
    if(A)Amount*=1.f-A->Armor;
    if(S&&S->ShieldWallUntil>Now)Amount*=1.f-S->ShieldWallReduction;
    if(Attacker)if(auto* Mode=M->GetWorld()->GetAuthGameMode<ACireGameMode>())
        for(auto* Other:Mode->Monsters)
        {
            if(Other==M||!Alive(Other)||!Other->NPCState)continue;
            const float* Until=Other->NPCState->ProvokedUntil.Find(Attacker);
            if(!Until||*Until<=Now)continue;
            const auto* OA=Other->NPCState->Archetype();float Reduction=.35f;
            if(OA)for(const auto& Ab:OA->Abilities)if(Ab.Kind==ECireNPCAbilityKind::Provoke){Reduction=Ab.Magnitude;break;}
            Amount*=1.f-Reduction;break;
        }
    if(S&&Depth==0&&S->GuardUntil>Now&&S->Guardian.IsValid()&&S->Guardian.Get()!=M&&Alive(S->Guardian.Get())&&Attacker)
    {
        const float Redirect=Amount*S->GuardFraction;Amount-=Redirect;
        ++Depth;CireCombat::ApplyDamage(Attacker,S->Guardian.Get(),Redirect,TEXT("Guardian's Oath"));--Depth;
    }
    return Amount;
}

void CireNPCCombat::Tick(ACireMonster* M,float Delta)
{
    if(!IsValid(M))return;
    CireRealm::UpdateVisibility(M);
    const float Scale=DesiredScale(M);
    if(!M->GetActorScale3D().Equals(FVector(Scale)))M->SetActorScale3D(FVector(Scale));
    auto* Mode=M->GetWorld()->GetAuthGameMode<ACireGameMode>();
    if(!M->HasAuthority()||!Mode||M->Health<=0)return;
    auto* Movement=M->GetCharacterMovement();
    auto* S=St(M);const auto* A=Arch(M);
    // progression-shop: outside the survival phase every NPC is paused: no movement, attacks or
    // casts, and its cooldown/buff/slow/cast timers freeze and resume exactly (CireProgression).
    if(Mode->Clock.Phase()!=Cires::MatchPhase::Survival||M->Damage<=0)
    {
        CireProgression::PauseNPC(M,M->GetWorld()->GetTimeSeconds());
        if(M->MonsterArt)M->MonsterArt->CancelSwing(); // creature-anim
        Movement->StopMovementImmediately();return;
    }
    CireProgression::ResumeNPC(M,M->GetWorld()->GetTimeSeconds());
    if(!FMath::IsFinite(Delta)||Delta<0)return;
    const float Now=M->GetWorld()->GetTimeSeconds();
    if(M->BaseMoveSpeed<=0)M->BaseMoveSpeed=Movement->MaxWalkSpeed;
    Movement->MaxWalkSpeed=M->BaseMoveSpeed*(M->SlowUntil>Now?.65f:1.f)*(S&&S->RallyUntil>Now?1.1f:1.f);
    M->AttackTimer=FMath::Max(0.f,M->AttackTimer-Delta);M->AbilityTimer=FMath::Max(0.f,M->AbilityTimer-Delta);
    if(M->MonsterArt)M->MonsterArt->ReleaseSwing(Now); // creature-anim: a committed swing lands on its contact frame
    if(S)S->RefreshStatusFlags(Now);
    // wave-director: neutral challenge packs stand at their camp and never pick a fight;
    // a player's attack (CireWaveDirector::AllowDamage) turns the whole pack hostile.
    if(M->bNeutral)
    {
        if(!M->Threat.IsEmpty()||M->Victim)CireThreat::Clear(M);
        M->bEngaged=false;M->LeashTimer=0;
        if(FVector::DistSquared2D(M->GetActorLocation(),M->SpawnPosition)>FMath::Square(90.f))M->AddMovementInput((M->SpawnPosition-M->GetActorLocation()).GetSafeNormal2D());
        else Movement->StopMovementImmediately();
        return;
    }
    // wave-director: the stall failsafe's forced march behaves like an armored marcher.
    if(M->bArmoredEscort||CireWaveDirector::IsForcedMarch(M))
    {
        if(!M->CastingAbility.IsEmpty())Interrupt(M);
        if(!M->Threat.IsEmpty()||M->Victim)CireThreat::Clear(M);
        M->bEngaged=false;
        CireLanePath::RefreshEscortCollision(M);MarchLane(M,Mode);return;
    }
    // No distance leash: packs keep their threat and chase until they or every threat holder dies.
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
        else
        {
            M->LeashTimer=0;M->Health=M->MaxHealth;Movement->StopMovementImmediately();
            CireWaveDirector::OnPackReset(M); // wave-director: a reset pack is neutral again
            if(S){const FName Id=S->ArchetypeId;const ECireNPCRole Role=S->Role;const ECireNPCClass Class=S->Classification;
                S->ResetRuntime();S->ArchetypeId=Id;S->Role=Role;S->Classification=Class;}
            if(A)M->BaseMoveSpeed=A->MoveSpeed;
            M->ForceNetUpdate();
        }
        return;
    }
    // Charge dash in progress: nothing else happens until it lands.
    if(S&&S->DashUntil>Now)
    {
        Movement->MaxWalkSpeed=S->DashSpeed;
        const FVector ToTarget=S->DashTarget-M->GetActorLocation();
        if(ToTarget.Size2D()<=60.f)S->DashUntil=0;else M->AddMovementInput(ToTarget.GetSafeNormal2D());
        return;
    }
    CireThreat::Tick(M,Delta);
    CireThreat::Select(M);
    if(!M->Victim&&M->Threat.IsEmpty()&&!CireWaveDirector::AggroSuppressed(M)) // wave-director: dropped/unreachable targets
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
        if(!IsValid(M->Victim)&&M->bPendingSkillshot){Interrupt(M);return;}
        if(Now>=M->CastEndsAt)ReleaseCast(M,Mode);
        return;
    }
    if(auto* Victim=M->Victim)
    {
        M->bEngaged=true;
        if(HandleWall(M,Victim->GetActorLocation()))return;
        const float Distance=static_cast<float>(FVector::Dist2D(M->GetActorLocation(),Victim->GetActorLocation()));
        const FVector Direction=(Victim->GetActorLocation()-M->GetActorLocation()).GetSafeNormal2D();
        const bool bSight=ClearSight(M,Victim);
        // creature-anim: finish the committed swing before choosing the next action.
        if(M->MonsterArt&&M->MonsterArt->HasPendingSwing()){Movement->StopMovementImmediately();M->SetActorRotation(Direction.Rotation());return;}
        if(TryAbilities(M,Mode,Victim,Distance,bSight))return;
        const bool bRanged=A?UsesProjectiles(A):M->CombatArchetype>=2;
        const float Reach=A?A->AttackRange:bRanged?650.f:170.f;
        if(bRanged&&S&&A)
        {
            // Casters and hunters hold their distance: back off from melee, then shoot.
            if(Distance<A->KiteRange&&S->KiteReadyAt<=Now&&bSight)
            {S->KiteUntil=Now+(A->Role==ECireNPCRole::Caster?.8f:1.f);S->KiteReadyAt=Now+(A->Role==ECireNPCRole::Caster?4.f:3.f);}
            if(S->KiteUntil>Now){M->AddMovementInput(-Direction);M->SetActorRotation(Direction.Rotation());return;}
        }
        if(Distance>Reach||!bSight){M->AddMovementInput(Direction);return;}
        Movement->StopMovementImmediately();M->SetActorRotation(Direction.Rotation());
        if(M->AttackTimer<=0)
        {
            const FCireNPCAbility* Basic=A?A->BasicAttack():nullptr;
            if(bRanged)
            {
                if(Basic&&Basic->Kind==ECireNPCAbilityKind::Projectile&&CireSkillTuning::FindSkillshot(Basic->Skillshot))BeginCast(M,*Basic,Victim->GetActorLocation(),true);
                else M->AttackTimer=2.2f;
            }
            else
            {
                M->AttackTimer=AttackPeriod(M,A);
                const float Amount=EffectiveDamage(M)*(Basic?Basic->DamageMultiplier:1.f);
                // creature-anim: the blow lands on the swing's contact frame (UCireMonsterArt::ReleaseSwing).
                if(M->MonsterArt&&M->MonsterArt->StartSwing(Victim,Amount,Basic?Basic->Name:TEXT("Monster attack"),Reach,M->AttackTimer))return;
                CireAttacks::Resolve(M,Victim,Amount,CireAttacks::Roll(M,Victim,false),Basic?Basic->Name:TEXT("Monster attack"));
                CireCombat::PlayCue(M,Victim,TEXT("npc_melee"),M->GetActorLocation(),Victim->GetActorLocation(),ECireSpellCue::Impact,.8f,true);
            }
        }
        return;
    }
    if(M->PackId>=0){if(M->bEngaged)StartLeash(M);return;}
    // wave-director: escort guards walk beside their escortee instead of racing ahead.
    if(const ACireMonster* Charge=CireWaveDirector::EscortCharge(M))
    {
        const FVector Offset=FVector(0,(M->GetUniqueID()%2?1.f:-1.f)*170.f,0);
        const FVector Beside=Charge->GetActorLocation()+Offset;
        if(FVector::DistSquared2D(M->GetActorLocation(),Beside)>FMath::Square(260.f)){M->AddMovementInput((Beside-M->GetActorLocation()).GetSafeNormal2D());return;}
        const UWorld* World=M->GetWorld();
        if(CireLanePath::RouteProgress(World,M->Lane,M->GetActorLocation())>CireLanePath::RouteProgress(World,Charge->Lane,Charge->GetActorLocation())+.01f)
        {Movement->StopMovementImmediately();return;}
    }
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
    {
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
    Check(M->Victim==Tank&&M->Threat.Contains(Tank)&&M->LeashTimer==0,TEXT("pack keeps threat and chases at any distance"));
    // Park the other fixture hero out of proximity-aggro range so only the threat rule is tested.
    Dps->SetActorLocation(M->GetActorLocation()+FVector(0,2000,0));M->Health=1;Tick(M,.01f);
    Check(M->Victim==Tank&&M->Threat.Contains(Tank),TEXT("threat survives idle ticks and distance"));
    Tank->bDead=true;CireThreat::Remove(Tank);Tick(M,.01f);
    Check(M->Victim==nullptr&&M->Threat.IsEmpty()&&M->LeashTimer>0,TEXT("pack returns home once every threat holder is dead"));
    M->SetActorLocation(M->SpawnPosition);Tick(M,.01f);Check(M->Health==M->MaxHealth&&M->LeashTimer==0,TEXT("pack heals only on return home"));Tank->bDead=false;
    }
    UE_LOG(LogCireNPCCombat,Display,TEXT("CIRE_NPC_SMOKE_%s checks=%d"),bPassed?TEXT("PASS"):TEXT("FAIL"),Checks);
    // Role, boss and WoW threat-rule suites run with the legacy NPC smoke so the
    // existing expansion probe picks them up.
    bPassed=CireNPCArchetypes::RunValidationSmoke()&&bPassed;
    bPassed=CireThreat::RunRulesSmoke(Mode)&&bPassed;
    bPassed=RunRolesSmoke(Mode)&&bPassed;
    bPassed=CireMonsterArt::RunSmoke(Mode)&&bPassed; // creature-anim
    return bPassed;
}
#endif
