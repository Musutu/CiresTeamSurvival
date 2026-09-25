#include "CireCombatExpansionProbe.h"
#include "CireWaves.h" // wave-director
#include "CireClassTraits.h"
#include "CireAbilityDB.h"
#include "CireCrowdControl.h"
#include "CireArenas.h" // arenas
#include "CireAudio.h" // audio:
#include "CireLoot.h" // progression-shop
#include "CireLanePath.h"
#include "CireChampionRoster.h"
#include "CireChampionProfiles.h"
#include "CireDeveloperTools.h"
#include "CireReplay.h"
#include "CireGame.h"
#include "CireThreat.h"
#include "CireNPCCombat.h"
#include "CireSkillTuning.h"
#include "CireSkillshot.h"
#include "CireConstruct.h"
#include "CireSummon.h"
#include "CireSkillCasting.h"
#include "CireSpellPresentation.h"
#include "CireCombatEvents.h"
#include "CireUISettings.h"
#include "CireMobility.h"
#include "CireRoleSkills.h"
#include "CireWeaponPresentation.h"
#include "CireTargeting.h"
#include "Engine/World.h"
#include "Misc/ScopeExit.h"
#include "CireAuraVisuals.h" // aura-vfx
#include "CireNav.h" // nav-paths
#include "CireAbilityVFX.h" // ability-vfx

#if !UE_BUILD_SHIPPING
DEFINE_LOG_CATEGORY_STATIC(LogCireExpansion,Log,All);
bool CireThreat::RunSmoke(ACireGameMode* Mode){
    if(!Mode||!Mode->HasAuthority())return false;
    const auto SavedClock=Mode->Clock;const auto SavedHeroes=Mode->Heroes;const auto SavedMonsters=Mode->Monsters;
    TArray<AActor*> Actors;Mode->Clock=Cires::MatchClock();Mode->Heroes.Reset();Mode->Monsters.Reset();
    ON_SCOPE_EXIT{for(auto* A:Actors)if(IsValid(A))A->Destroy();Mode->Clock=SavedClock;Mode->Heroes=SavedHeroes;Mode->Monsters=SavedMonsters;};
    bool Passed=true;int32 Checks=0;
    auto Check=[&](bool Value,const TCHAR* Label){++Checks;if(!Value){Passed=false;UE_LOG(LogCireExpansion,Error,TEXT("CIRE_THREAT_CHECK_FAIL %s"),Label);}};
    auto Hero=[&](int32 Class){FActorSpawnParameters P;P.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        auto* H=Mode->GetWorld()->SpawnActor<ACireHero>(FVector(0,-2100,3092),FRotator::ZeroRotator,P);
        if(H){Actors.Add(H);H->SetActorTickEnabled(false);H->TeamId=0;H->Draft(Class);H->Health=H->MaxHealth=1000;H->CriticalChance=0;Mode->Heroes.Add(H);}return H;};
    auto Monster=[&](){FActorSpawnParameters P;P.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        auto* M=Mode->GetWorld()->SpawnActor<ACireMonster>(FVector(250,-2100,3088),FRotator::ZeroRotator,P);
        if(M){Actors.Add(M);M->SetActorTickEnabled(false);M->Lane=0;M->Health=M->MaxHealth=1000;Mode->Monsters.Add(M);}return M;};
    auto* Tank=Hero(0);auto* DPS=Hero(1);auto* Healer=Hero(2);auto* Enemy=Monster();auto* Second=Monster();
    if(!Tank||!DPS||!Healer||!Enemy||!Second)return false;
    const auto& T=CireSkillTuning::Get();
    Damage(Enemy,Tank,10);Check(Enemy->Victim==Tank&&FMath::IsNearlyEqual(Enemy->Threat.FindRef(Tank),10*T.TankDamageThreatMultiplier),TEXT("tank damage threat multiplier"));
    Damage(Enemy,DPS,40);Check(Enemy->Victim==Tank,TEXT("DPS stays below tank threat"));
    Damage(Enemy,DPS,11);Check(Enemy->Victim==Tank,TEXT("DPS above 100% but under the 110% melee pull threshold keeps the tank"));
    Damage(Enemy,DPS,5);Check(Enemy->Victim==DPS,TEXT("DPS overtakes tank past 110% in melee"));
    Taunt(Enemy,Tank,3);Damage(Enemy,DPS,100);Check(Enemy->Victim==Tank,TEXT("active taunt forces target"));
    Enemy->ForcedVictimUntil=0;Check(Select(Enemy)==DPS,TEXT("expired taunt returns to highest threat"));
    Engage(Second,Tank);Tank->Health=500;
    Check(FMath::IsNearlyEqual(CireCombat::ApplyHealing(Healer,Tank,100,TEXT("Test heal")),100.f),TEXT("effective heal applied"));
    Check(FMath::IsNearlyEqual(Enemy->Threat.FindRef(Healer),100*T.HealingThreatMultiplier/2)&&FMath::IsNearlyEqual(Second->Threat.FindRef(Healer),100*T.HealingThreatMultiplier/2),TEXT("healing threat split among engaged enemies"));
    Tank->Health=Tank->MaxHealth;const float Before=Enemy->Threat.FindRef(Healer);CireCombat::ApplyHealing(Healer,Tank,100,TEXT("Overheal"));
    Check(FMath::IsNearlyEqual(Before,Enemy->Threat.FindRef(Healer)),TEXT("overheal gives no threat"));
    DPS->bDead=true;Check(Select(Enemy)!=DPS&&!Enemy->Threat.Contains(DPS),TEXT("dead targets removed"));
    Remove(Tank);Check(!Enemy->Threat.Contains(Tank)&&Enemy->Victim==Healer,TEXT("despawn removes tank threat"));
    Clear(Enemy);Check(Enemy->Threat.IsEmpty()&&!Enemy->Victim,TEXT("phase clear removes all threat"));
    DPS->bDead=false;DPS->CriticalChance=1;DPS->CriticalMultiplier=1.5f;DPS->DamageDone=0;Enemy->Health=1000;
    Check(FMath::IsNearlyEqual(CireCombat::ApplyStrike(DPS,Enemy,100,TEXT("Critical test")),150.f)&&FMath::IsNearlyEqual(DPS->DamageDone,150.f),TEXT("guaranteed critical applies 150 percent and meter actual damage"));
    DPS->CriticalChance=0;Check(FMath::IsNearlyEqual(CireCombat::ApplyStrike(DPS,Enemy,100,TEXT("Normal test")),100.f),TEXT("zero crit chance stays normal"));
    Enemy->Health=10;DPS->CriticalChance=1;Check(FMath::IsNearlyEqual(CireCombat::ApplyStrike(DPS,Enemy,100,TEXT("Lethal crit")),10.f),TEXT("critical effective damage excludes overkill"));
    UE_LOG(LogCireExpansion,Display,TEXT("CIRE_THREAT_%s checks=%d"),Passed?TEXT("PASS"):TEXT("FAIL"),Checks);return Passed;
}
bool CireCombatExpansion::Run(ACireGameMode* Mode){
    bool Good=true;
    Good=CireChampionRoster::RunValidationSmoke()&&Good;
    Good=CireChampionProfiles::RunSmoke(Mode)&&Good;
    Good=CireClassTraits::RunSmoke(Mode)&&Good; // champion-draft: class traits
    Good=CireAbilityDB::RunSmoke()&&Good; // champion-draft: ability database
    Good=CireCrowdControl::RunSmoke(Mode)&&Good; // champion-draft: crowd control, casts, execute skills
    Good=CireWeapons::RunValidationSmoke()&&Good;
    Good=CireMovement::RunSmoke(Mode)&&Good;
    Good=CireRoleSkills::RunSmoke(Mode)&&Good;
    Good=CireTargeting::RunDescriptorSmoke()&&Good;
    Good=CireTargeting::RunRuntimeSmoke(Mode)&&Good;
    Good=CireLanePath::RunSmoke(Mode)&&Good;
    Good=CireSkillTuning::RunValidationSmoke()&&Good;
    Good=CireThreat::RunSmoke(Mode)&&Good;
    Good=CireSkillshots::RunSkillshotSmoke(Mode)&&Good;
    Good=CireConstructs::RunConstructSmoke(Mode)&&Good;
    Good=CireSummons::RunSummonSmoke(Mode)&&Good;
    Good=CireSkillCasting::RunCastSmoke(Mode)&&Good;
    Good=CireNPCCombat::RunSmoke(Mode)&&Good;
    Good=CireOptions::RunSettingsSmoke()&&Good;
    Good=CireDeveloperTools::RunValidationSmoke()&&Good;
    Good=CireReplay::RunReplaySmoke(Mode->GetWorld())&&Good;
    Good=CireSpellPresentation::RunSmoke(Mode->GetWorld())&&Good;
    Good=CireAudio::RunAudioSmoke(Mode->GetWorld())&&Good; // audio: settings, buses, data, armour classes, music, cadence
    Good=CireAuraVisuals::RunSmoke(Mode)&&Good; // aura-vfx
    Good=CireProgression::RunSmoke(Mode)&&Good; // progression-shop: items, shop, loot, gating, teleport, NPC pause
    Good=CireArenas::RunSmoke(Mode)&&Good; // arenas: data, symmetry, paths, random no-repeat pick, build and cleanup
    Good=CireWaveDirector::RunTests(Mode)&&Good; // wave-director: data, templates, live edits, escort, stuck/failsafe, neutral packs, bots
    Good=CireNav::RunTests(Mode)&&Good; // nav-paths: navmesh coverage, paths, prop carving, arenas, path editor
    Good=CireAbilityVFX::RunTests(Mode)&&Good; // ability-vfx: shape-true telegraphs, line indicators, lifecycles, release sync
    UE_LOG(LogCireExpansion,Display,TEXT("CIRE_COMBAT_EXPANSION_%s"),Good?TEXT("PASS"):TEXT("FAIL"));return Good;
}
#endif
