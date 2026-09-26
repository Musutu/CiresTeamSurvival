#include "CireRoleSkills.h"
#include "CireScalingKits.h" // scaling-kits
#include "CireSkillShop.h" // progression-shop: per-level cast scaling
#include "CireGame.h"
#include "CireSkillTuning.h"
#include "CireSkillRuntime.h"
#include "CireDeveloperTools.h"
#include "CireAreaEffects.h"
#include "CireCombatEvents.h"
#include "CireSummon.h"
#include "CireThreat.h"
#include "CireNPCCombat.h"
#include "CireTargeting.h"
#include "EngineUtils.h"
#include "Components/PrimitiveComponent.h"
#include "CireBuffs.h" // aura-vfx

namespace
{
bool ClearSight(ACireHero* Source,AActor* Target)
{
    if(!IsValid(Source)||!IsValid(Target))return false;
    if(Source==Target)return true;
    if(FMath::Abs(Source->GetActorLocation().Z-Target->GetActorLocation().Z)>180.f)return false;
    FCollisionQueryParams Query(SCENE_QUERY_STAT(CireRoleSkillSight),false,Source);Query.AddIgnoredActor(Target);
    FCollisionObjectQueryParams Objects;Objects.AddObjectTypesToQuery(ECC_WorldStatic);Objects.AddObjectTypesToQuery(ECC_WorldDynamic);
    TArray<FHitResult> Hits;
    Source->GetWorld()->LineTraceMultiByObjectType(Hits,Source->GetActorLocation(),Target->GetActorLocation(),Objects,Query);
    for(const auto& Hit:Hits)if(Hit.GetComponent()&&Hit.GetComponent()->GetCollisionResponseToChannel(ECC_Pawn)==ECR_Block)return false;
    return true;
}
bool Friendly(ACireHero* Source,ACireHero* Target,float Range)
{
    return CireSkillRuntime::Alive(Target)&&Target->TeamId==Source->TeamId&&
        Source->InRange(Target,Range)&&ClearSight(Source,Target);
}
bool Ground(ACireHero* Hero,FVector& Point)
{
    FHitResult Hit;FCollisionQueryParams Query(SCENE_QUERY_STAT(CireRoleSkillGround),false,Hero);
    if(!Hero->GetWorld()->LineTraceSingleByObjectType(Hit,Point+FVector(0,0,180),Point-FVector(0,0,400),
        FCollisionObjectQueryParams(ECC_WorldStatic),Query)||Hit.ImpactNormal.Z<.8f)return false;
    Point=Hit.ImpactPoint;return true;
}
}

bool CireRoleSkills::Handles(const FString& Id)
{
    return Id==TEXT("second_wind")||Id==TEXT("last_stand")||Id==TEXT("challenge_of_iron")||
        Id==TEXT("seismic_reprisal")||Id==TEXT("starfall")||Id==TEXT("spectral_hunt")||
        Id==TEXT("mass_aegis")||Id==TEXT("wellspring");
}
bool CireRoleSkills::IsUltimate(const FString& Id){return Handles(Id)&&Id!=TEXT("second_wind");}
FString CireRoleSkills::Name(const FString& Id)
{
    for(const auto& Skill:Cires::StarterSkillPool())if(Id==UTF8_TO_TCHAR(Skill.Id.c_str()))return UTF8_TO_TCHAR(Skill.Name.c_str());
    return Id;
}
FString CireRoleSkills::Targeting(const FString& Id)
{
    if(Id==TEXT("second_wind")||Id==TEXT("last_stand"))return TEXT("Self");
    if(Id==TEXT("challenge_of_iron")||Id==TEXT("seismic_reprisal"))return TEXT("Hostile area / centered on self");
    if(Id==TEXT("mass_aegis"))return TEXT("Friendly area / centered on self");
    if(Id==TEXT("wellspring"))return TEXT("Friendly / self fallback");
    if(Id==TEXT("spectral_hunt"))return TEXT("Hostile / summoned hunters");
    return TEXT("Ground / aimed location");
}
FString CireRoleSkills::Description(const FString& Id)
{
    const auto* S=CireSkillTuning::FindRoleSkill(Id);if(!S)return TEXT("Role recipe unavailable.");
    FString Effect;
    if(Id==TEXT("second_wind"))Effect=FString::Printf(TEXT("Heal only yourself for %.0f%% maximum health."),S->MaxHealthFraction*100);
    else if(Id==TEXT("last_stand"))Effect=FString::Printf(TEXT("Clear your slow and heal only yourself for %.0f%% maximum health."),S->MaxHealthFraction*100);
    else if(Id==TEXT("challenge_of_iron"))Effect=FString::Printf(TEXT("Taunt nearby monsters for up to %.1fs; take 40%% less damage for %.1fs. Does not heal allies."),FMath::Min(S->DurationSeconds,10.f),S->DurationSeconds);
    else if(Id==TEXT("seismic_reprisal"))Effect=FString::Printf(TEXT("Warn the ground around you for %.1fs, then burst for %.0f + %.1fx primary damage in %.0f cm."),S->WarningSeconds,S->FlatPower,S->PrimaryScaling,S->Radius);
    else if(Id==TEXT("starfall"))Effect=FString::Printf(TEXT("Aim a %.0f cm ground circle. After %.1fs, deal %.0f + %.1fx primary damage."),S->Radius,S->WarningSeconds,S->FlatPower,S->PrimaryScaling);
    else if(Id==TEXT("spectral_hunt"))Effect=FString::Printf(TEXT("Summon three hunters for %.1fs, each dealing %.0f + %.1fx primary damage per attack. Requires a hostile target."),S->DurationSeconds,S->FlatPower,S->PrimaryScaling);
    else if(Id==TEXT("mass_aegis"))Effect=FString::Printf(TEXT("Clear nearby allies' slows and grant 40%% damage reduction for %.1fs. Does not stack with another guard."),S->DurationSeconds);
    else Effect=FString::Printf(TEXT("Heal the selected ally (or self) for %.0f + %.1fx primary; grant 40%% damage reduction for %.1fs."),S->FlatPower,S->PrimaryScaling,S->DurationSeconds);
    return (IsUltimate(Id)?TEXT("ULTIMATE | "):TEXT(""))+Targeting(Id)+TEXT(". ")+Effect+
        FString::Printf(TEXT(" %.0f mana / %.0f energy | %.1fs CD | %.0f cm range."),S->ManaCost,S->EnergyCost,S->CooldownSeconds,S->CastRange);
}

bool CireRoleSkills::Cast(ACireHero* Hero,int32 Slot,const FString& Id)
{
    if(!CireSkillRuntime::Alive(Hero)||!Hero->HasAuthority()||!Handles(Id)||!Hero->Skills.IsValidIndex(Slot)||
        Hero->Skills[Slot]!=Id||!Hero->Cooldowns.IsValidIndex(Slot)||Hero->Cooldowns[Slot]>0||Hero->GlobalCooldown>0)return false;
    auto* Mode=Hero->GetWorld()->GetAuthGameMode<ACireGameMode>();
    const auto* Recipe=CireSkillTuning::FindRoleSkill(Id);
    if(!Mode||!Mode->IsCombatPhase()||!Recipe)return false;
    const auto S=*Recipe;
    auto Fail=[&](const TCHAR* Why){Hero->Notice=Why;return false;};
    if(!CireSkillShop::CanPayCast(Hero,Id,S.ManaCost,S.EnergyCost))return Fail(*CireSkillShop::CostFailText()); // progression-shop: Skill Shop level (Ability DB curve)
    const float Now=Hero->GetWorld()->GetTimeSeconds(),Duration=CireDeveloperTools::EffectSeconds(Hero->GetWorld(),S.DurationSeconds)*CireKits::Potency(Hero,Id); // kits-complete: guard/taunt seconds x potency (1 for damage/heal skills)
    const float Power=Mode->Power(Hero->TeamId),Amount=FMath::Min(10000.f,CireKits::Amount(Hero,Id,S.FlatPower,S.PrimaryScaling)*Power); // scaling-kits: DB base + coef x PRIMARY
    FVector Aim=Hero->GetActorLocation();ACireHero* Ally=Hero;
    if(Id==TEXT("wellspring"))
    {
        if(auto* Selected=::Cast<ACireHero>(Hero->Target);Selected&&Selected->TeamId==Hero->TeamId)Ally=Selected;
        if(!Friendly(Hero,Ally,S.CastRange))return Fail(TEXT("Select a living ally in range and line of sight."));
        Aim=Ally->GetActorLocation();
    }
    if(Id==TEXT("spectral_hunt"))
    {
        if(!Hero->IsHostile(Hero->Target)||!Hero->InRange(Hero->Target,S.CastRange)||!ClearSight(Hero,Hero->Target))
            return Fail(TEXT("Select a hostile target in range and line of sight."));
        const auto* Base=CireSkillTuning::FindSummon(TEXT("spectral_pack"));if(!Base)return Fail(TEXT("Spectral Pack recipe is unavailable."));
        // SpawnGroup applies developer overrides once to the authored duration.
        auto Spec=*Base;Spec.Count=3;Spec.bCommandable=false;Spec.Damage=Amount;Spec.DurationSeconds=FMath::Max(.1f,S.DurationSeconds);Spec.CastRange=S.CastRange;
        Aim=Hero->GetActorLocation()+Hero->GetActorForwardVector().GetSafeNormal2D()*180;
        const auto Units=ACireSummon::SpawnGroup(Hero,Spec,Hero->Target,Aim);
        if(Units.Num()!=Spec.Count){for(auto* Unit:Units)if(IsValid(Unit))Unit->Destroy();return Fail(TEXT("No room for hunters or summon limit reached."));}
        for(auto* Unit:Units)Unit->SourceSkill=TEXT("spectral_hunt"); // fix/summons: summons-bar icon
    }
    else if(Id==TEXT("starfall")||Id==TEXT("seismic_reprisal"))
    {
        if(Id==TEXT("starfall"))
            Aim=Hero->bHasCastAim?Hero->CastAimPoint:Hero->IsHostile(Hero->Target)?Hero->Target->GetActorLocation():Hero->GetActorLocation()+Hero->GetActorForwardVector()*FMath::Min(500.f,S.CastRange);
        if(Aim.ContainsNaN()||!CireSkillRuntime::InRealmBounds(Mode,Hero->TeamId,Aim)||
            FVector::DistSquared2D(Hero->GetActorLocation(),Aim)>FMath::Square(Id==TEXT("starfall")?S.CastRange:1.f)||!Ground(Hero,Aim))
            return Fail(TEXT("Aim at supported ground in your realm and casting range."));
        if(Id==TEXT("starfall"))
        {
            FVector Center;FRotator Heading;FString Reason;
            if(!CireTargeting::ValidateGround(Hero,Id,Aim,Center,Heading,Reason))return Fail(*Reason);
            Aim=Center;
        }
        FCireAreaSpec Area;Area.Radius=S.Radius;Area.WarningSeconds=S.WarningSeconds;
        Area.bPersistent=false;Area.bPoison=false;Area.BurstDamage=Amount;Area.DamagePerSecond=0;Area.AbilityName=Name(Id);
        Area.Color=Id==TEXT("starfall")?FLinearColor(.45f,.35f,1,.4f):FLinearColor(.8f,.52f,.17f,.4f);
        if(!ACireAreaEffect::Spawn(Hero,Area,Aim,FRotator::ZeroRotator))return Fail(TEXT("Cannot create this ground warning here."));
    }
    else if(Id==TEXT("second_wind")||Id==TEXT("last_stand"))
    {
        if(Id==TEXT("last_stand"))Hero->SlowUntil=0;
        CireCombat::ApplyHealing(Hero,Hero,(Hero->MaxHealth*S.MaxHealthFraction+CireKits::Amount(Hero,Id))*Power,Name(Id)); // scaling-kits: + primary
    }
    else if(Id==TEXT("challenge_of_iron"))
    {
        Hero->ShieldUntil=FMath::Max(Hero->ShieldUntil,Now+Duration);Hero->TauntUntil=FMath::Max(Hero->TauntUntil,Now+Duration); CireBuffs::Apply(Hero,TEXT("challenge_of_iron"),Duration,Hero); // aura-vfx
        for(TActorIterator<ACireMonster> It(Hero->GetWorld());It;++It)
            if(Hero->IsHostile(*It)&&Hero->InRange(*It,S.Radius)&&ClearSight(Hero,*It))CireThreat::Taunt(*It,Hero,Duration);
    }
    else if(Id==TEXT("mass_aegis"))
    {
        for(TActorIterator<ACireHero> It(Hero->GetWorld());It;++It)if(Friendly(Hero,*It,S.Radius))
        {It->SlowUntil=0;It->ShieldUntil=FMath::Max(It->ShieldUntil,Now+Duration);It->ForceNetUpdate();CireBuffs::Apply(*It,TEXT("mass_aegis"),Duration,Hero);} // aura-vfx
    }
    else if(Id==TEXT("wellspring"))
    {
        CireCombat::ApplyHealing(Hero,Ally,Amount,Name(Id));Ally->ShieldUntil=FMath::Max(Ally->ShieldUntil,Now+Duration);Ally->ForceNetUpdate(); CireBuffs::Apply(Ally,TEXT("wellspring"),Duration,Hero); // aura-vfx
    }
    Hero->Mana-=S.ManaCost;Hero->Energy-=S.EnergyCost;
    Hero->Cooldowns[Slot]=static_cast<float>(Cires::CooldownSeconds(CireDeveloperTools::CooldownSeconds(Hero->GetWorld(),S.CooldownSeconds),Hero->CDR));
    CireSkillShop::ApplyCastLevel(Hero,Slot,Id,S.ManaCost,S.EnergyCost); // progression-shop: Skill Shop level (Ability DB curve)
    Hero->GlobalCooldown=.9f;Hero->Notice=Name(Id);Hero->ForceNetUpdate();
    CireCombat::PlayCue(Hero,Id==TEXT("wellspring")?Ally:Hero,FName(*Id),Hero->GetActorLocation(),Aim,ECireSpellCue::Cast);
    return true;
}

#if !UE_BUILD_SHIPPING
#include "Misc/ScopeExit.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Components/BoxComponent.h"

bool CireRoleSkills::RunSmoke(ACireGameMode* Mode)
{
    if(!IsValid(Mode)||!Mode->HasAuthority()||!Mode->IsCombatPhase())return false;
    bool Pass=true;int32 Checks=0;TArray<AActor*> Actors;
    auto Check=[&](bool Value,const TCHAR* Why){++Checks;if(!Value){Pass=false;UE_LOG(LogTemp,Error,TEXT("CIRE_ROLE_SKILLS_FAIL %s"),Why);}};
    ON_SCOPE_EXIT {for(auto* Actor:Actors)if(IsValid(Actor)){ACireSummon::ClearForActor(Actor);ACireAreaEffect::ClearForActor(Actor);Actor->Destroy();}};
    FActorSpawnParameters P;P.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    const FVector Origin(0,-2100,4502);
    auto* Platform=Mode->GetWorld()->SpawnActor<AActor>(FVector(0,-2100,4400),FRotator::ZeroRotator,P);
    if(!Platform)return false;Actors.Add(Platform);
    auto* Floor=NewObject<UStaticMeshComponent>(Platform);Platform->SetRootComponent(Floor);Floor->RegisterComponent();
    Floor->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube")));
    Floor->SetWorldScale3D(FVector(30,30,.2f));Floor->SetWorldLocation(FVector(0,-2100,4400));
    Floor->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);Floor->SetCollisionObjectType(ECC_WorldStatic);Floor->SetCollisionResponseToAllChannels(ECR_Block);
    auto Make=[&](FVector Point,int32 Team,int32 Class)
    {
        auto* H=Mode->GetWorld()->SpawnActor<ACireHero>(Point,FRotator::ZeroRotator,P);
        if(H){Actors.Add(H);H->TeamId=Team;H->Draft(Class);H->SetActorTickEnabled(false);H->SetActorEnableCollision(false);H->SetActorHiddenInGame(true);H->Health=100;H->Mana=1000;H->Energy=100;}
        return H;
    };
    auto* Tank=Make(Origin,0,0);auto* Ally=Make(Origin+FVector(300,0,0),0,1);auto* Support=Make(Origin+FVector(0,300,0),0,2);
    auto* M=Mode->GetWorld()->SpawnActor<ACireMonster>(Origin+FVector(220,0,0),FRotator::ZeroRotator,P);
    if(!Tank||!Ally||!Support||!M)return false;Actors.Add(M);M->Lane=0;M->Health=M->MaxHealth=5000;M->SetActorTickEnabled(false);M->SetActorEnableCollision(false);M->SetActorHiddenInGame(true);
    auto Ready=[&](ACireHero* H,const TCHAR* Id){H->Skills={Id};H->Cooldowns={0};H->GlobalCooldown=0;H->Energy=100;H->Mana=1000;};
    for(const TCHAR* Id:{TEXT("second_wind"),TEXT("last_stand"),TEXT("challenge_of_iron"),TEXT("seismic_reprisal"),TEXT("starfall"),TEXT("spectral_hunt"),TEXT("mass_aegis"),TEXT("wellspring")})
        Check(Handles(Id)&&CireSkillTuning::FindRoleSkill(Id)!=nullptr,TEXT("implemented role skill has a validated tuning recipe"));
    Ready(Tank,TEXT("second_wind"));Tank->Target=Ally;const float AllyBefore=Ally->Health;
    Check(Cast(Tank,0,TEXT("second_wind"))&&Tank->Health>100&&Ally->Health==AllyBefore,TEXT("self sustain never heals selected ally"));
    const float After=Tank->Health,Energy=Tank->Energy;
    Check(!Cast(Tank,0,TEXT("second_wind"))&&Tank->Health==After&&Tank->Energy==Energy,TEXT("cooldown replay cannot heal or charge again"));
    Ready(Tank,TEXT("last_stand"));Tank->SlowUntil=Mode->GetWorld()->GetTimeSeconds()+10;
    Check(Cast(Tank,0,TEXT("last_stand"))&&Tank->SlowUntil==0&&Tank->Health>After&&Ally->Health==AllyBefore,TEXT("last stand cleanses and heals only caster"));
    Ready(Tank,TEXT("challenge_of_iron"));
    Check(Cast(Tank,0,TEXT("challenge_of_iron"))&&M->ForcedVictim==Tank&&Tank->ShieldUntil>Mode->GetWorld()->GetTimeSeconds(),TEXT("challenge applies guard and authoritative monster taunt"));
    Ready(Support,TEXT("wellspring"));Support->Target=Ally;
    Check(Cast(Support,0,TEXT("wellspring"))&&Ally->Health>AllyBefore&&Ally->ShieldUntil>Mode->GetWorld()->GetTimeSeconds(),TEXT("wellspring heals and guards the selected ally"));
    Ready(Support,TEXT("wellspring"));Ally->SetActorLocation(Origin+FVector(2500,0,0));const float Mana=Support->Mana;
    Check(!Cast(Support,0,TEXT("wellspring"))&&Support->Mana==Mana&&Support->Cooldowns[0]==0,TEXT("out-of-range ally is rejected without payment"));
    Ally->SetActorLocation(Origin+FVector(300,0,0));Ally->SlowUntil=Mode->GetWorld()->GetTimeSeconds()+10;Ally->Health=100;
    Ready(Support,TEXT("mass_aegis"));
    Check(Cast(Support,0,TEXT("mass_aegis"))&&Ally->SlowUntil==0&&Ally->ShieldUntil>Mode->GetWorld()->GetTimeSeconds()&&Ally->Health==100,TEXT("mass aegis is guard and cleanse rather than duplicate group healing"));
    Ready(Tank,TEXT("seismic_reprisal"));const float MonsterBefore=M->Health;
    Check(Cast(Tank,0,TEXT("seismic_reprisal"))&&M->Health==MonsterBefore,TEXT("seismic warning is harmless at cast time"));
    ACireAreaEffect* Area=nullptr;
    for(TActorIterator<ACireAreaEffect> It(Mode->GetWorld());It;++It)if(It->AreaSpec.AbilityName==Name(TEXT("seismic_reprisal"))&&FVector::DistSquared2D(It->GetActorLocation(),Origin)<1)Area=*It;
    Check(Area!=nullptr,TEXT("seismic uses native replicated area actor"));
    if(Area)
    {
        Actors.Add(Area);Area->Tick(Area->AreaSpec.WarningSeconds*.5f);Check(M->Health==MonsterBefore,TEXT("telegraph remains harmless before warning expires"));
        Area->Tick(Area->AreaSpec.WarningSeconds);Check(M->Health<MonsterBefore,TEXT("seismic applies confirmed damage after warning"));
    }
    // Exercise the bot/native Cast path, which does not pass through a controller RPC.
    auto* Wall=Mode->GetWorld()->SpawnActor<AActor>(Origin+FVector(110,0,0),FRotator::ZeroRotator,P);
    Check(Wall!=nullptr,TEXT("starfall obstruction fixture spawned"));
    if(Wall)
    {
        Actors.Add(Wall);auto* Collision=NewObject<UBoxComponent>(Wall);Wall->SetRootComponent(Collision);Wall->AddInstanceComponent(Collision);
        Collision->SetBoxExtent(FVector(15,160,180));Collision->SetCollisionObjectType(ECC_WorldStatic);
        Collision->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);Collision->SetCollisionResponseToAllChannels(ECR_Block);
        Collision->RegisterComponent();Wall->SetActorLocation(Origin+FVector(110,0,0));
        Ready(Tank,TEXT("starfall"));Tank->Target=M;const float BeforeMana=Tank->Mana;
        Check(!Cast(Tank,0,TEXT("starfall"))&&Tank->Mana==BeforeMana&&Tank->Cooldowns[0]==0&&Tank->Notice.Contains(TEXT("wall")),TEXT("native starfall rejects blocked ground without payment"));
        Wall->SetActorEnableCollision(false);
        Check(Cast(Tank,0,TEXT("starfall"))&&Tank->Mana<BeforeMana&&Tank->Cooldowns[0]>0,TEXT("same starfall aim succeeds once obstruction is removed"));
    }
    if(CireDeveloperTools::CanEdit(Mode->GetWorld()))
    {
        const auto SavedSettings=CireDeveloperTools::Get(Mode->GetWorld());
        ON_SCOPE_EXIT {if(SavedSettings.bEnabled)CireDeveloperTools::Apply(Mode,SavedSettings);else CireDeveloperTools::Restore(Mode);};
        FCireDeveloperSettings Settings;Settings.bEnabled=true;Settings.EffectDurationScale=2;
        Check(CireDeveloperTools::Apply(Mode,Settings),TEXT("isolated duration override enabled"));
        Ready(Tank,TEXT("spectral_hunt"));Tank->Target=M;
        Check(Cast(Tank,0,TEXT("spectral_hunt")),TEXT("spectral hunt casts with duration override"));
        int32 Count=0;const auto* Recipe=CireSkillTuning::FindRoleSkill(TEXT("spectral_hunt"));
        for(TActorIterator<ACireSummon> It(Mode->GetWorld());It;++It)if(It->GetOwnerHero()==Tank&&!It->IsActorBeingDestroyed())
        {
            ++Count;Actors.Add(*It);
            Check(Recipe&&FMath::IsNearlyEqual(It->SummonSpec.DurationSeconds,FMath::Clamp(Recipe->DurationSeconds*2.f,1.f,120.f))&&
                FMath::IsNearlyEqual(It->ExpiresServerTime-Mode->GetWorld()->GetTimeSeconds(),It->SummonSpec.DurationSeconds,.01f),TEXT("hunter lifetime uses one duration multiplier and matches replicated expiry"));
        }
        Check(Count==3,TEXT("duration regression observes all three actual hunters"));
    }
    UE_LOG(LogTemp,Display,TEXT("CIRE_ROLE_SKILLS_SMOKE_%s checks=%d"),Pass?TEXT("PASS"):TEXT("FAIL"),Checks);return Pass;
}
#endif
