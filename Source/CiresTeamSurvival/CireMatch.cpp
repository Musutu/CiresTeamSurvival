#include "CireBalanceLab.h"
#include "CireSkillShop.h" // progression-shop
#include "CireShopFixtures.h" // progression-shop
#include "CireLoot.h" // progression-shop
#include "CireLanePath.h"
#include "CireEnvironmentGallery.h"
#include "CireBatchArtGallery.h"
#include "CireTooltipGallery.h"
#include "CireDeveloperTools.h"
#include "CireReplay.h"
#include "CireReplaySpectator.h"
#include "CireGame.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "Net/UnrealNetwork.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "CireCombatEvents.h"
#include "CireInterfaceProbe.h"
#include "CireExpansionNetProbe.h"
#include "CireTownGoal.h"
#include "CireFeedbackPreview.h"
#include "CireArtPreview.h"
#include "CireCombatArtPreview.h"
#include "CireAreaEffects.h"
#include "CireCombatFeaturesProbe.h"
#include "CireNPCCombat.h"
#include "CireThreat.h"
#include "CireSkillshot.h"
#include "CireConstruct.h"
#include "CireSummon.h"
#include "CireSpellGallery.h"
#include "CireAuraGallery.h" // aura-vfx
#include "CireAbilityVFXGallery.h" // ability-vfx
#include "CireOptionsGallery.h"
#include "CireCombatExpansionProbe.h"
#include "CireNPCArchetypes.h"
#include "CireNPCPackPreview.h"
#include "CireMonsterGallery.h" // creature-anim
#include "CireNPCNetProbe.h"
#include "CireNav.h" // nav-paths
#include "CireArenas.h" // arenas
#include "CireArenaGallery.h" // arenas
#include "CireWaves.h" // wave-director

DEFINE_LOG_CATEGORY_STATIC(LogCire, Log, All);

#if !UE_BUILD_SHIPPING
namespace {
struct FCireServerProbe {
    bool Enabled=false;
    bool ActionsVerified=false;
    bool Done=false;
    double Started=0;
    FVector MovementOrigin=FVector::ZeroVector;
    TWeakObjectPtr<ACireHero> PlayerPawn;
    TWeakObjectPtr<ACireMonster> Target;
};
FCireServerProbe ServerProbe;
int32 SmokePhaseMask = 0;
int32 SmokeClearedWaves = 0;
float SmokeWaveAge = 0.f;
int32 SmokeBossLeaks = 0;
bool SmokeCycleClearValid = false;

void TickServerProbe(ACireGameMode* Mode) {
    auto& Probe=ServerProbe;
    if(!Probe.Enabled||Probe.Done)return;
    const auto Fail=[&](const TCHAR* Reason) {
        UE_LOG(LogCire,Error,TEXT("CIRE_NET_SERVER_FAIL reason=%s"),Reason);
        Probe.Done=true;FPlatformMisc::RequestExitWithStatus(false,1);
    };
    if(FPlatformTime::Seconds()-Probe.Started>40) {Fail(TEXT("client actions or disconnect timed out"));return;}
    if(!Probe.PlayerPawn.IsValid()) {
        for(auto* Hero:Mode->Heroes) {
            if(!IsValid(Hero)||Hero->bBot||!Hero->IsPlayerControlled())continue;
            Probe.PlayerPawn=Hero;
            Probe.MovementOrigin=Hero->GetActorLocation();
            Mode->SpawnBots();
            for(auto* Bot:Mode->Heroes) if(IsValid(Bot)&&Bot->bBot) {
                Bot->GetCharacterMovement()->DisableMovement();
                Bot->bAutoAttack=false;
            }
            FActorSpawnParameters Params;
            Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
            auto* Target=Mode->GetWorld()->SpawnActor<ACireMonster>(ACireMonster::StaticClass(),Hero->GetActorLocation()+FVector(450,0,0),FRotator::ZeroRotator,Params);
            if(!Target) {Fail(TEXT("probe fixture spawn failed"));return;}
            Target->Lane=Hero->TeamId;
            Target->Health=Target->MaxHealth=1000000;
            Target->Damage=0;
            Target->MonsterName=TEXT("CIRE_NETWORK_PROBE_TARGET");
            Target->GetCharacterMovement()->DisableMovement();
            Mode->Monsters.Add(Target);
            Probe.Target=Target;
            UE_LOG(LogCire,Display,TEXT("CIRE_NET_SERVER_JOIN pawn=%s team=%d heroes=%d"),*Hero->GetName(),Hero->TeamId,Mode->Heroes.Num());
            break;
        }
        return;
    }
    auto* Hero=Probe.PlayerPawn.Get();
    if(!Probe.ActionsVerified&&Hero->bDrafted&&Hero->Target==Probe.Target.Get()&&Hero->Notice.Contains(TEXT("intermission"))) {
        const bool Valid=Hero->Archetype==2&&Hero->Gold==120&&Hero->Skills.Num()==0&&Hero->Cooldowns.Num()==0&&
            Hero->GearRank==0&&FMath::IsNearlyZero(Hero->CDR)&&Hero->Level==1&&
            FVector::Dist2D(Probe.MovementOrigin,Hero->GetActorLocation())>=100;
        if(!Valid) {Fail(TEXT("server validation state mismatch"));return;}
        Probe.ActionsVerified=true;
        UE_LOG(LogCire,Display,TEXT("CIRE_NET_SERVER_ACTIONS_PASS draft=2 gold=120 skills=0 illegal_shop_rejected=1 movement_cm=%.1f"),FVector::Dist2D(Probe.MovementOrigin,Hero->GetActorLocation()));
    }
    if(Probe.ActionsVerified&&Hero->bBot&&!Hero->IsPlayerControlled()) {
        int32 Counts[2]={0,0};
        for(auto* Member:Mode->Heroes) if(IsValid(Member)&&Member->TeamId>=0&&Member->TeamId<2)++Counts[Member->TeamId];
        const bool Valid=Mode->Heroes.Contains(Hero)&&Mode->Heroes.Num()==10&&Counts[0]==5&&Counts[1]==5&&
            Hero->bDrafted&&Hero->Archetype==2&&Hero->Level==1&&Hero->Gold==120;
        if(!Valid) {Fail(TEXT("disconnect did not preserve champion/team membership"));return;}
        UE_LOG(LogCire,Display,TEXT("CIRE_NET_SERVER_PASS heroes=%d teams=%d/%d preserved_pawn=%s level=%d bot=1"),Mode->Heroes.Num(),Counts[0],Counts[1],*Hero->GetName(),Hero->Level);
        Probe.Done=true;FPlatformMisc::RequestExitWithStatus(false,0);
    }
}
} // namespace
#endif

ACireGameState::ACireGameState() { SetNetUpdateFrequency(5); }
void ACireGameState::OnRepLaneRoutes() { CireLanePath::ReceiveState(this); }
void ACireGameState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const {
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(ACireGameState,Phase); DOREPLIFETIME(ACireGameState,SecondsLeft);
    DOREPLIFETIME(ACireGameState,Round); DOREPLIFETIME(ACireGameState,Wave);
    DOREPLIFETIME(ACireGameState,CycleWavesDone); DOREPLIFETIME(ACireGameState,WavesPerCycle);
    DOREPLIFETIME(ACireGameState,NextWaveSeconds);
    DOREPLIFETIME(ACireGameState,MonsterSkillSeed); DOREPLIFETIME(ACireGameState,WaveRace); // monster-races
    DOREPLIFETIME(ACireGameState,EmberLives); DOREPLIFETIME(ACireGameState,DuskLives);
    DOREPLIFETIME(ACireGameState,EmberWins); DOREPLIFETIME(ACireGameState,DuskWins);
    DOREPLIFETIME(ACireGameState,ArenaIndex); DOREPLIFETIME(ACireGameState,Announcement);
    DOREPLIFETIME(ACireGameState,ProgressionMode); // progression-shop
    DOREPLIFETIME(ACireGameState,WaveLabel); DOREPLIFETIME(ACireGameState,NextWaveLabel); // wave-director
    DOREPLIFETIME(ACireGameState,BreatherReady); DOREPLIFETIME(ACireGameState,BreatherPlayers); // wave-director
    DOREPLIFETIME(ACireGameState,LaneBounds); DOREPLIFETIME(ACireGameState,LanePoints0);
    DOREPLIFETIME(ACireGameState,LanePoints1); DOREPLIFETIME(ACireGameState,LaneRouteVersion);
}
ACireGameMode::ACireGameMode() {
    PrimaryActorTick.bCanEverTick=true;
    GameStateClass=ACireGameState::StaticClass();
    PlayerControllerClass=ACireController::StaticClass();
    DefaultPawnClass=ACireHero::StaticClass();
    HUDClass=ACireHUD::StaticClass();
    ReplaySpectatorPlayerControllerClass=ACireReplaySpectator::StaticClass();
}
FVector ACireGameMode::BasePosition(int32 Team) const { return FVector(-1700,Team==0?-2100:2100,110); }
FVector ACireGameMode::ArenaPosition(int32 Team,int32 Slot) const {
    return CireArenas::SpawnLocation(ArenaIndex,Team,Slot); // arenas: the picked arena's authored, mirrored spawns
}
float ACireGameMode::Power(int32 Team) const { return Team>=0&&Team<2?static_cast<float>(Rewards[Team].PowerMultiplier):1.f; }
float ACireGameMode::Loot(int32 Team) const { return Team>=0&&Team<2?static_cast<float>(Rewards[Team].LootMultiplier):1.f; }
bool ACireGameMode::IsCombatPhase() const { return Clock.Phase()==Cires::MatchPhase::Survival||Clock.Phase()==Cires::MatchPhase::Arena; }
bool ACireGameMode::CanFight(const ACireHero* A,const ACireHero* B) const {
    return A&&B&&A->bDrafted&&B->bDrafted&&!A->bDead&&!B->bDead&&A->TeamId!=B->TeamId&&Clock.Phase()==Cires::MatchPhase::Arena;
}
void ACireGameMode::BeginPlay() {
    Super::BeginPlay();
#if !UE_BUILD_SHIPPING
    ServerProbe={};
    ServerProbe.Enabled=GetNetMode()==NM_DedicatedServer&&FParse::Param(FCommandLine::Get(),TEXT("CireNetServerProbe"));
    if(ServerProbe.Enabled) {
        ServerProbe.Started=FPlatformTime::Seconds();
        BotFillTimer=60;WaveTimer=60; // leave a fresh champion for the remote draft test
    }
#endif
    bSmoke=FParse::Param(FCommandLine::Get(),TEXT("CireSmoke"));
    Clock=Cires::MatchClock({60,90,RecoverySeconds});
#if !UE_BUILD_SHIPPING
    SmokePhaseMask = 1; SmokeClearedWaves = 0; SmokeWaveAge = 0.f;
    SmokeBossLeaks = 0; SmokeCycleClearValid = false;
    if(bSmoke) {Clock=Cires::MatchClock({2,2,1}); WaveBreatherSeconds=.3f; WaveTimer=.3f; BotFillTimer=0;}
#endif
    CireDeveloperTools::Initialize(this);
    CireSkillShop::InitializeMode(this); // progression-shop: -CireMode=SkillShop|Classic
    CireLanePath::PublishState(GetGameState<ACireGameState>());
    GetWorld()->SpawnActor<ACireWorld>();
    for(int32 Team=0;Team<2;++Team) {
        auto* Goal=GetWorld()->SpawnActor<ACireTownGoal>(ACireTownGoal::StaticClass(),
            FVector(-1850,Team==0?-2100:2100,150),FRotator::ZeroRotator);
        if(Goal)Goal->TeamId=Team;
        else UE_LOG(LogCire,Error,TEXT("Town goal spawn failed for team %d"),Team);
    }
    auto* S=GetGameState<ACireGameState>();
    S->SecondsLeft=-1; S->CycleWavesDone=0; S->WavesPerCycle=FMath::Clamp(S->WavesPerCycle,1,10);
    CireWaveDirector::Initialize(this); // wave-director: Waves.json drives composition, waves per cycle, breather and phase pacing
#if !UE_BUILD_SHIPPING
    const bool bProbeTimer=ServerProbe.Enabled;
#else
    const bool bProbeTimer=false;
#endif
    if(!bSmoke&&!bProbeTimer)WaveTimer=CireWaveDirector::Config(GetWorld()).FirstWaveDelay;
    S->NextWaveSeconds=WaveTimer;
    S->Announcement=TEXT("Hold the gates. Challenge the outposts. Survive together.");
    bool bFeedbackPreview = false;
#if !UE_BUILD_SHIPPING
    bFeedbackPreview = CireTooltipGallery::Initialize(this);
    if(!bFeedbackPreview)bFeedbackPreview = CireBatchArtGallery::Initialize(this);
    if(!bFeedbackPreview)bFeedbackPreview = CireEnvironmentGallery::Initialize(this);
    if(!bFeedbackPreview)bFeedbackPreview = CireArenaGallery::Initialize(this); // arenas
    if(!bFeedbackPreview)bFeedbackPreview = CireOptionsGallery::Initialize(this);
    if(!bFeedbackPreview)bFeedbackPreview = CireSpellGallery::Initialize(this);
    if(!bFeedbackPreview)bFeedbackPreview = CireAuraGallery::Initialize(this); // aura-vfx
    if(!bFeedbackPreview)bFeedbackPreview = CireAbilityVFXGallery::Initialize(this); // ability-vfx
    if(!bFeedbackPreview)bFeedbackPreview = CireCombatArtPreview::Initialize(this);
    if(!bFeedbackPreview)bFeedbackPreview = CireArtPreview::Initialize(this);
    if(!bFeedbackPreview)bFeedbackPreview = CireFeedbackPreview::Initialize(this);
    if(!bFeedbackPreview)bFeedbackPreview = CireNPCPackPreview::Initialize(this);
    if(!bFeedbackPreview)bFeedbackPreview = CireMonsterGallery::Initialize(this); // creature-anim
    if(!bFeedbackPreview)bFeedbackPreview = CireShopFixtures::Initialize(this); // progression-shop
    CireNPCNetProbe::InitializeServer(this);
#endif
    if(!bFeedbackPreview)SpawnPacks();
    if(!bFeedbackPreview)CireBalanceLab::Initialize(this);
    if(!bFeedbackPreview)CireWaveDirector::InitializeSoak(this); // wave-director
#if !UE_BUILD_SHIPPING
    if(!bFeedbackPreview)CireNav::InitializeProbe(this); // nav-paths: -CireNavProbe march + performance probe
#endif
    UE_LOG(LogCire,Display,TEXT("CIRE MATCH READY | 5v5 | %d cleared waves / %.0fs prep / %.0fs arena / %.0fs recovery | server authority"),S->WavesPerCycle,Clock.GetDurations().Intermission,Clock.GetDurations().Arena,RecoverySeconds);
#if !UE_BUILD_SHIPPING
    if(ServerProbe.Enabled)UE_LOG(LogCire,Display,TEXT("CIRE_NET_SERVER_READY dedicated=1 timeout=40"));
    if(FParse::Param(FCommandLine::Get(),TEXT("CireCombatFeaturesProbe")))
        FPlatformMisc::RequestExitWithStatus(false,CireCombatFeatures::Run(this)?0:1);
    if(FParse::Param(FCommandLine::Get(),TEXT("CireCombatExpansionProbe")))
        FPlatformMisc::RequestExitWithStatus(false,CireCombatExpansion::Run(this)?0:1);
    if(FParse::Param(FCommandLine::Get(),TEXT("CireNavTests"))) // nav-paths: the navigation checks alone
        FPlatformMisc::RequestExitWithStatus(false,CireNav::RunTests(this)?0:1);
    if(FParse::Param(FCommandLine::Get(),TEXT("CireTelemetryProbe")))
        FPlatformMisc::RequestExitWithStatus(false,CireCombat::RunTelemetrySmoke(this)?0:1);
#endif
}
void ACireGameMode::HandleStartingNewPlayer_Implementation(APlayerController* P) {
    if(!P||P->GetPawn()) return;
    int Counts[2]={0,0};
    for(auto* H:Heroes) if(IsValid(H)&&!H->bBot) ++Counts[FMath::Clamp(H->TeamId,0,1)];
    int Team=Counts[0]<=Counts[1]?0:1;
    if(Counts[Team]>=5) { P->StartSpectatingOnly(); return; }
    ACireHero* Replaced=nullptr;
    for(auto* H:Heroes) if(IsValid(H)&&H->bBot&&H->TeamId==Team) {Replaced=H;break;}
    if(Replaced) {
        Replaced->bBot=false; P->Possess(Replaced);
        Replaced->Notice=TEXT("Joined an existing champion; level and arena state preserved.");
        return;
    }
    FActorSpawnParameters Params; Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    auto* H=GetWorld()->SpawnActor<ACireHero>(ACireHero::StaticClass(),BasePosition(Team)+FVector(0,Counts[Team]*140,0),FRotator::ZeroRotator,Params);
    H->TeamId=Team; H->HomePosition=BasePosition(Team); Heroes.Add(H); P->Possess(H);
    if(Clock.Phase()==Cires::MatchPhase::Arena) H->ReviveAt(ArenaPosition(Team,Counts[Team]));
}
void ACireGameMode::PostLogin(APlayerController* P) { Super::PostLogin(P); }
void ACireGameMode::Logout(AController* P) {
    if(auto* H=Cast<ACireHero>(P?P->GetPawn():nullptr)) { H->bBot=true; H->bAutoAttack=true; H->Draft(H->Archetype); }
    Super::Logout(P);
    bBotsFilled=false; BotFillTimer=2;
}
void ACireGameMode::SpawnBots() {
    Heroes.RemoveAll([](auto* H){return !IsValid(H);});
    for(int Team=0;Team<2;++Team) {
        int Count=0; for(auto* H:Heroes) if(H->TeamId==Team)++Count;
        for(int I=Count;I<5;++I) {
            FActorSpawnParameters Params; Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
            auto* H=GetWorld()->SpawnActor<ACireHero>(ACireHero::StaticClass(),BasePosition(Team)+FVector(250,I*150-300,0),FRotator::ZeroRotator,Params);
            H->TeamId=Team; H->bBot=true; H->HomePosition=BasePosition(Team); H->Draft(I%3);
            H->HomePosition=BasePosition(Team)+FVector(250,I*180-360,0);
            H->HeroName=FString::Printf(TEXT("%s %d"),Team==0?TEXT("Ember"):TEXT("Dusk"),I+1);
            H->bAutoAttack=true; Heroes.Add(H);
            if(Clock.Phase()==Cires::MatchPhase::Arena)H->ReviveAt(ArenaPosition(Team,I));
        }
    }
    bBotsFilled=true;
}
void ACireGameMode::SpawnWave() {
    // wave-director: composition, types, spawn pacing and scaling come from Waves.json (CireWaves.h).
    if(!CireWaveDirector::StartWave(this)) return;
    auto* S=GetGameState<ACireGameState>();
    UE_LOG(LogCire,Display,TEXT("CIRE WAVE SPAWN round=%d wave=%d cycle=%d/%d type=%s"),S->Round,S->Wave,CycleWavesSpawned,S->WavesPerCycle,*CireWaveDirector::CurrentWaveType(this));
    CireProgression::OnWaveSpawned(this,CycleWavesSpawned); // progression-shop: mid-cycle challenge unlocks
}
void ACireGameMode::SpawnPacks() {
    // progression-shop: challenge packs are progression content. Bays unlock by round/wave
    // (LootTables.json packSchedule), deeper bays hold higher tiers, and tiers rise in later cycles.
    CireProgression::SpawnPacks(this,1);
}
void ACireGameMode::AwardTeam(int32 Team,int32 XP,int32 GoldAmount) {
    for(auto* H:Heroes) if(IsValid(H)&&H->TeamId==Team) { H->GrantExperience(XP); H->Gold+=GoldAmount; }
}
void ACireGameMode::MonsterKilled(ACireMonster* M,ACireHero* Killer) {
    if(!IsValid(M)||!IsValid(Killer)||Killer->TeamId!=M->Lane) return;
    const float Reward=M->PackId<0?CireWaveDirector::RewardMultiplier(M):1.f; // wave-director: per-wave reward multiplier
    // progression-shop: gold is the playtest-2 kill bounty (CireLoot::AwardKillGold); XP unchanged.
    AwardTeam(M->Lane,FMath::RoundToInt((45+GetGameState<ACireGameState>()->Round*4)*Reward),0);
    CireLoot::AwardKillGold(this,M,Reward);
    // progression-shop: pack completion, Pack Leaders and lane bosses roll data-driven loot tables
    // into a glowing auto-pickup chest (CireLoot). The old flat stat/rare reward is replaced.
    bool bPackCompleted=false;
    if(M->PackId>=0&&!RewardedPacks.Contains(M->PackId)) {
        bool Remaining=false;
        for(auto* Other:Monsters) if(IsValid(Other)&&Other!=M&&Other->PackId==M->PackId&&Other->Health>0) {Remaining=true;break;}
        if(!Remaining) {RewardedPacks.Add(M->PackId);bPackCompleted=true;}
    }
    CireLoot::OnMonsterKilled(this,M,Killer,bPackCompleted);
    CireWaveDirector::Forget(M); // wave-director
    Monsters.Remove(M);
}
void ACireGameMode::Leak(ACireMonster* M) {
    if(!IsValid(M)||M->IsActorBeingDestroyed()||!Monsters.Contains(M)||M->PackId>=0
        ||M->Lane<0||M->Lane>1||Clock.Phase()!=Cires::MatchPhase::Survival) return;
    auto* S=GetGameState<ACireGameState>();
    if(!S) return;
    // Remove first: overlapping collision components must not debit the same creep twice.
    Monsters.Remove(M);
    CireWaveDirector::Forget(M); // wave-director
    int32& Lives=M->Lane==0?S->EmberLives:S->DuskLives;
    Lives=FMath::Max(0,Lives-(M->LeakCostOverride>0?M->LeakCostOverride:M->bBoss?10:1));
    if(M->bArmoredEscort)S->Announcement=FString::Printf(TEXT("%s GATE BREACHED | %s cost %d lives"),M->Lane==0?TEXT("EMBER"):TEXT("DUSK"),*M->GetNPCDisplayName(),FMath::Max(1,M->LeakCostOverride));
    if(M->bBoss) S->Announcement=FString::Printf(TEXT("%s GATE BREACHED | Siegebreaker cost 10 lives"),M->Lane==0?TEXT("EMBER"):TEXT("DUSK"));
    const int LosingTeam=M->Lane;
    M->Destroy();
    if(Lives==0) EndSurvival(LosingTeam==0?1:0);
}
void ACireGameMode::HeroKilled(ACireHero* H) {
    if(Clock.Phase()==Cires::MatchPhase::Survival) H->RespawnTimer=10;
    else H->RespawnTimer=0;
}
void ACireGameMode::EndSurvival(int32 Winner) {
    ReplayStopAt=GetWorld()->GetTimeSeconds()+1.f;
    Clock.Finish(); auto* S=GetGameState<ACireGameState>(); S->Phase=3;
    S->SecondsLeft=0; S->NextWaveSeconds=0;
    S->Announcement=Winner==0?TEXT("EMBER VICTORIOUS - Dusk's gate has fallen"):TEXT("DUSK VICTORIOUS - Ember's gate has fallen");
    // wave-director: a finite Waves.json cycle count ends the match on lives (-1 = drawn).
    if(bCyclesComplete)S->Announcement=Winner<0?FString(TEXT("MATCH DRAWN - both gates held to the last cycle")):FString::Printf(TEXT("%s VICTORIOUS - more lives after the final cycle"),Winner==0?TEXT("EMBER"):TEXT("DUSK"));
    UE_LOG(LogCire,Display,TEXT("CIRE MATCH COMPLETE winner=%d"),Winner);
}
void ACireGameMode::ChangePhase(int32 NewPhase) {
    ACireAreaEffect::ClearAll(GetWorld());
    ACireSkillshot::ClearAll(GetWorld());ACireConstruct::ClearAll(GetWorld());ACireSummon::ClearAll(GetWorld());
    for(auto* M:Monsters)if(IsValid(M)){CireThreat::Clear(M);CireNPCCombat::Interrupt(M);}
    for(auto* H:Heroes)if(IsValid(H))H->PendingAttackTarget.Reset();
    auto* S=GetGameState<ACireGameState>(); S->Phase=NewPhase;
    S->SecondsLeft=static_cast<float>(Clock.RemainingSeconds()); S->Round=Clock.Round();
    CireProgression::OnPhaseChanged(this,NewPhase); // progression-shop: end shop visits, cancel teleports, auto-collect loot on prep
    CireWaveDirector::OnPhaseChanged(this,NewPhase); // wave-director: clears spawn queues, publishes next wave
    S->NextWaveSeconds=0;
#if !UE_BUILD_SHIPPING
    if(bSmoke) SmokePhaseMask|=1<<NewPhase;
#endif
    if(NewPhase==1) {
#if !UE_BUILD_SHIPPING
        if(bSmoke) {
            int32 WaveAlive=0, PacksAlive=0;
            for(auto* M:Monsters) if(IsValid(M)&&M->Health>0) {if(M->PackId<0)++WaveAlive;else++PacksAlive;}
            SmokeCycleClearValid=WaveAlive==0&&PacksAlive>0&&S->CycleWavesDone==S->WavesPerCycle;
            UE_LOG(LogCire,Display,TEXT("CIRE_SMOKE_CLEAR wave_alive=%d optional_alive=%d cleared=%d"),WaveAlive,PacksAlive,S->CycleWavesDone);
        }
#endif
        S->Announcement=TEXT("THE QUIET MINUTE | Monsters are dormant. Shop anywhere: press B.");
        CireArenas::ServerPrepare(this); // arenas: pick the next arena now so every peer prebuilds it (hidden)
        int32 TownSlot[2]={0,0};
        for(auto* H:Heroes) if(IsValid(H)) {
            H->Target=nullptr;
            if(H->bDead||H->bBot) {
                H->HomePosition=BasePosition(H->TeamId)+FVector(250,TownSlot[H->TeamId]++*180-360,0);
                H->ReviveAt(H->HomePosition);
            }
        }
    } else if(NewPhase==2) {
        // arenas: random themed arena (never the previous one), built before anyone is teleported
        CireArenas::ServerBegin(this);
        S->Announcement=FString::Printf(TEXT("ARENA | %s | Defeat the opposing team for power and loot."),*CireArenas::DisplayName(ArenaIndex));
        int Slot[2]={0,0};
        for(auto* H:Heroes) if(IsValid(H)) {
            if(!H->bDrafted)H->Draft(H->Archetype);
            H->ReviveAt(ArenaPosition(H->TeamId,Slot[H->TeamId]++));
        }
    } else if(NewPhase==4) {
        for(auto* H:Heroes) if(IsValid(H)) {
            H->ReviveAt(BasePosition(H->TeamId)+FVector(300,Heroes.IndexOfByKey(H)%5*110-220,0));
            H->Target=nullptr;
        }
        S->Announcement+=TEXT(" | Recovery: regroup at your gate.");
    } else if(NewPhase==0) {
        CycleWavesSpawned=0; S->CycleWavesDone=0;
        S->Announcement=TEXT("DEFEND THE GATES | A new wave cycle begins.");
        // Completed wave creeps are gone; optional challenge packs refresh each cycle.
        for(int I=Monsters.Num()-1;I>=0;--I) if(IsValid(Monsters[I])&&Monsters[I]->PackId>=0) {Monsters[I]->Destroy();Monsters.RemoveAt(I);}
        RewardedPacks.Reset();
        SpawnPacks(); WaveTimer=0;
        // wave-director: the first wave's authored delay, and the finite cycle count.
        const auto& Waves=CireWaveDirector::Config(GetWorld());
        if(!bSmoke&&!Waves.Waves.IsEmpty())WaveTimer=CireWaveDirector::ResolveWave(Waves,0,Clock.Round()-1).DelayBefore;
        if(Waves.Cycles>0&&Clock.Round()>Waves.Cycles) {
            bCyclesComplete=true;
            EndSurvival(S->EmberLives==S->DuskLives?-1:S->EmberLives>S->DuskLives?0:1);
            return;
        }
    }
    if(NewPhase!=1&&NewPhase!=2)CireArenas::Sync(GetWorld()); // arenas: recovery/survival/finish clean the arena up
    UE_LOG(LogCire,Display,TEXT("CIRE PHASE %d ROUND %d HEROES %d"),NewPhase,Clock.Round(),Heroes.Num());
}
void ACireGameMode::ResolveArena() {
    if(GetGameState<ACireGameState>()->Phase!=2) return;
    int Alive[2]={0,0}; float Fraction[2]={0,0};
    for(auto* H:Heroes) if(IsValid(H)&&H->bDrafted&&!H->bDead) {++Alive[H->TeamId]; Fraction[H->TeamId]+=H->Health/FMath::Max(1.f,H->MaxHealth);}
    int Winner=-1;
    if(Alive[0]!=Alive[1]) Winner=Alive[0]>Alive[1]?0:1;
    else if(!FMath::IsNearlyEqual(Fraction[0],Fraction[1],.01f)) Winner=Fraction[0]>Fraction[1]?0:1;
    auto* S=GetGameState<ACireGameState>();
    if(Winner>=0) {
        Cires::AwardArenaWin(Rewards[Winner]); AwardTeam(Winner,100,80);
        S->Announcement=FString::Printf(TEXT("%s won the arena | team power %.0f%% | loot %.0f%%"),Winner==0?TEXT("EMBER"):TEXT("DUSK"),(Power(Winner)-1)*100,(Loot(Winner)-1)*100);
    } else S->Announcement=TEXT("Arena drawn | Both teams return without a victory buff.");
    S->EmberWins=Rewards[0].ArenaWins; S->DuskWins=Rewards[1].ArenaWins;
    Clock.ResolveArena(); ChangePhase(4);
}
void ACireGameMode::Tick(float Dt) {
    Super::Tick(Dt);
#if !UE_BUILD_SHIPPING
    if(CireTooltipGallery::Tick(this)) return;
    if(CireBatchArtGallery::Tick(this)) return;
    if(CireEnvironmentGallery::Tick(this)) return;
    if(CireArenaGallery::Tick(this)) return; // arenas
    if(CireBalanceLab::Tick(this,Dt)) return;
    if(CireOptionsGallery::Tick(this)) return;
    if(CireSpellGallery::Tick(this)) return;
    if(CireAuraGallery::Tick(this)) return; // aura-vfx
    if(CireAbilityVFXGallery::Tick(this)) return; // ability-vfx
    if(CireCombatArtPreview::Tick(this)) return;
    if(CireArtPreview::Tick(this)) return;
    if(CireFeedbackPreview::Tick(this)) return;
    if(CireNPCPackPreview::Tick(this)) return;
    if(CireMonsterGallery::Tick(this)) return; // creature-anim
    if(CireShopFixtures::Tick(this)) return; // progression-shop
    if(CireNPCNetProbe::TickServer(this)) return;
    if(CireExpansionNetProbe::TickServer(this)) return;
    if(CireInterfaceProbe::TickServer(this)) return;
    TickServerProbe(this);
    CireWaveDirector::TickSoak(this,Dt); // wave-director: headless soak bookkeeping
    CireWaveDirector::TickGallery(this); // wave-director: -CireWaveGallery captures
    if(CireNav::TickGallery(this)) return; // nav-paths: -CireNavGallery captures
    CireNav::TickProbe(this,Dt); // nav-paths: -CireNavProbe
#endif
    auto* S=GetGameState<ACireGameState>(); if(!S) return;
    if(!bSmoke&&GetNetMode()==NM_Standalone) {
        for(auto* H:Heroes) if(IsValid(H)&&!H->bBot&&!H->bDrafted)return;
    }
    if(!bBotsFilled) {BotFillTimer-=Dt;if(BotFillTimer<=0)SpawnBots();}
    if(Clock.Phase()==Cires::MatchPhase::Finished) {
        if(GetWorld()->GetTimeSeconds()>=ReplayStopAt)if(auto* Replay=CireReplay::Get(GetWorld()))Replay->StopRecording();
        return;
    }
    if(!bAutomaticReplayAttempted){
        bAutomaticReplayAttempted=true;
        const FString Args=FCommandLine::Get();
        const bool Test=Args.Contains(TEXT("Probe"))||Args.Contains(TEXT("Gallery"))||Args.Contains(TEXT("Preview"))||
            Args.Contains(TEXT("CireSmoke"))||Args.Contains(TEXT("CireExpansionNet"))||Args.Contains(TEXT("CireNoReplay"));
        if(!Test)if(auto* Replay=CireReplay::Get(GetWorld()))Replay->StartRecording();
    }
    const auto& Dev=CireDeveloperTools::Get(GetWorld());
    for(const auto& Event:Clock.Advance(Dev.bEnabled&&Dev.bFreezePhaseClock?0.f:Dt)) {
        if(Event.ArenaTimedOut) ResolveArena();
        else ChangePhase(static_cast<int32>(Event.To));
    }
    S->Phase=static_cast<int32>(Clock.Phase()); S->SecondsLeft=static_cast<float>(Clock.RemainingSeconds()); S->Round=Clock.Round();
    if(S->Phase==0) {
        Monsters.RemoveAll([](auto* M){return !IsValid(M);});
        // wave-director: spawn queue, stuck detection and the stall failsafe run before the
        // clear rule, so a wave can never hold the cycle forever (Docs/Waves.md).
        CireWaveDirector::TickSurvival(this,Dt);
        const bool bWaveAlive=CireWaveDirector::IsWaveActive(this);
        const bool bBlocking=CireWaveDirector::BlocksNextWave(this);
#if !UE_BUILD_SHIPPING
        // Exercise actual town-zone entry and despawn while optional packs stay alive.
        if(bSmoke&&bWaveAlive) {
            SmokeWaveAge+=Dt;
            if(SmokeWaveAge>=.5f) {
                const auto WaveMonsters=Monsters;
                for(auto* M:WaveMonsters) if(IsValid(M)&&M->PackId<0) {
                    if(M->bBoss)++SmokeBossLeaks;
                    M->SetActorLocation(FVector(-1850,M->Lane==0?-2100:2100,110),false,nullptr,ETeleportType::TeleportPhysics);
                }
                SmokeWaveAge=0;
            }
        }
#endif
        if(!bBlocking) {
            if(CycleWavesSpawned>S->CycleWavesDone) {
                S->CycleWavesDone=CycleWavesSpawned;
                // wave-director: breather plus the next wave's authored delay.
                const auto& Waves=CireWaveDirector::Config(GetWorld());
                WaveTimer=WaveBreatherSeconds+(bSmoke||Waves.Waves.IsEmpty()?0.f:CireWaveDirector::ResolveWave(Waves,CycleWavesSpawned,Clock.Round()-1).DelayBefore);
#if !UE_BUILD_SHIPPING
                if(bSmoke)++SmokeClearedWaves;
#endif
                UE_LOG(LogCire,Display,TEXT("CIRE WAVE CLEAR round=%d cleared=%d/%d"),S->Round,S->CycleWavesDone,S->WavesPerCycle);
            }
            if(S->CycleWavesDone>=S->WavesPerCycle) {
                // Prep waits for every wave unit, including non-blocking ones, to die or leak.
                if(!bWaveAlive&&Clock.BeginIntermission()) ChangePhase(1);
                else S->NextWaveSeconds=0;
            } else {
                // wave-director: every human pressed Ready in the Skill Shop window -> start in 1 s.
                if(CireWaveDirector::UpdateBreatherReady(this))WaveTimer=FMath::Min(WaveTimer,1.f);
                WaveTimer=FMath::Max(0.f,WaveTimer-Dt);
                S->NextWaveSeconds=WaveTimer;
                if(WaveTimer<=0&&!(Dev.bEnabled&&Dev.bPauseWaveSpawns))SpawnWave();
            }
        } else S->NextWaveSeconds=0;
    }
    if(S->Phase==2&&Heroes.Num()>=2) {
        int Alive[2]={0,0}; for(auto* H:Heroes) if(IsValid(H)&&H->bDrafted&&!H->bDead)++Alive[H->TeamId];
        if(Alive[0]==0||Alive[1]==0)ResolveArena();
    }
#if !UE_BUILD_SHIPPING
    if(bSmoke) {
        SmokeElapsed+=Dt;
        if(S->Round>=2||SmokeElapsed>40) { // wave-director: five authored waves per cycle
            // wave-director: expected losses come from what the director actually spawned (Waves.json).
            int32 Leak0=0,Leak1=0,Bosses=0; CireWaveDirector::SmokeCounters(this,Leak0,Leak1,Bosses);
            const bool Pass=Heroes.Num()==10&&S->Round>=2&&SmokeClearedWaves>=S->WavesPerCycle&&(SmokePhaseMask&23)==23
                &&SmokeCycleClearValid&&SmokeBossLeaks==Bosses&&Bosses>0&&S->EmberLives==FMath::Max(0,100-Leak0)&&S->DuskLives==FMath::Max(0,100-Leak1);
            UE_LOG(LogCire,Display,TEXT("CIRE_SMOKE_%s heroes=%d round=%d phase=%d cleared=%d phase_mask=%d boss_leaks=%d lives=%d/%d"),Pass?TEXT("PASS"):TEXT("FAIL"),Heroes.Num(),S->Round,S->Phase,SmokeClearedWaves,SmokePhaseMask,SmokeBossLeaks,S->EmberLives,S->DuskLives);
            FPlatformMisc::RequestExitWithStatus(false,Pass?0:1);
        }
    }
#endif
}
