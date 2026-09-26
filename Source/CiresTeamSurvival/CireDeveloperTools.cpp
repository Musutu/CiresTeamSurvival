#include "CireDeveloperTools.h"
#include "CireGame.h"
#include "CireLanePath.h"
#include "CireAreaEffects.h"
#include "CireSkillTuning.h"
#include "CireBalanceLab.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "HAL/FileManager.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Paths.h"
#include "Misc/Guid.h"
#include <limits>

namespace
{
struct FState
{
    FCireDeveloperSettings Current;
    Cires::PhaseDurations OriginalDurations;
    float OriginalBreather=8,OriginalRecovery=15,OriginalTimeScale=1;
    int32 OriginalWaves=3;
    bool bSnapshot=false;
};
TMap<TWeakObjectPtr<UWorld>,FState> States;
const FCireDeveloperSettings Neutral;
const TCHAR* Section=TEXT("CireDeveloper.Settings");
FString TestProfileOverride;
FString Filename(){return TestProfileOverride.IsEmpty()?FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(),TEXT("Config/CireDeveloper.ini"))):TestProfileOverride;}
bool Fail(FString* Error,const TCHAR* Text){if(Error)*Error=Text;return false;}
}
bool CireDeveloperTools::CanEdit(UWorld* World)
{
#if UE_BUILD_SHIPPING
    return false;
#else
    return World&&World->GetNetMode()==NM_Standalone&&World->GetAuthGameMode<ACireGameMode>();
#endif
}
void CireDeveloperTools::Initialize(ACireGameMode* Mode)
{
    if(!Mode||!CanEdit(Mode->GetWorld()))return;
    for(auto I=States.CreateIterator();I;++I)if(!I.Key().IsValid())I.RemoveCurrent();
    FState& State=States.FindOrAdd(Mode->GetWorld());State=FState();
    // Saved values are available in the editor panel, but never auto-enable
    // overrides during an ordinary play session.
    LoadProfile(Mode->GetWorld(),State.Current);State.Current.bEnabled=false;
}
const FCireDeveloperSettings& CireDeveloperTools::Get(UWorld* World)
{
    if(!CanEdit(World))return Neutral;
    const auto* State=States.Find(World);return State?State->Current:Neutral;
}
bool CireDeveloperTools::Validate(const FCireDeveloperSettings& S,FString* Error)
{
    auto In=[](float V,float Lo,float Hi){return FMath::IsFinite(V)&&V>=Lo&&V<=Hi;};
    if(!In(S.PrepSeconds,1,600)||!In(S.ArenaSeconds,1,900)||!In(S.RecoverySeconds,1,180)||!In(S.WaveBreatherSeconds,0,120))return Fail(Error,TEXT("Phase/wave timing is outside bounds."));
    if(S.WavesPerCycle<1||S.WavesPerCycle>10||S.WaveUnitsOverride<0||S.WaveUnitsOverride>40||S.TeamLives<1||S.TeamLives>1000)return Fail(Error,TEXT("Wave count, units or team lives outside bounds."));
    if(!In(S.MonsterHealthScale,.1f,10)||!In(S.MonsterDamageScale,.05f,5)||!In(S.ProjectileSpeedScale,.1f,5)||!In(S.ProjectileCollisionScale,.1f,5))return Fail(Error,TEXT("Combat/projectile multiplier outside bounds."));
    if(!In(S.EffectDurationScale,.1f,5)||!In(S.TelegraphScale,.1f,5)||!In(S.ConstructHealthScale,.1f,10)||!In(S.SummonHealthScale,.1f,10))return Fail(Error,TEXT("Effect or object multiplier outside bounds."));
    if(!In(S.CooldownScale,.1f,5))return Fail(Error,TEXT("Cooldown multiplier outside bounds."));
    for(int32 P:{S.WorldPolicy,S.PlayerPolicy,S.MonsterPolicy,S.ProtectionPolicy,S.WallPolicy})if(P<0||P>3)return Fail(Error,TEXT("Unknown projectile collision policy."));
    if(!In(S.SpawnX,-1000,80000)||!In(S.SpawnSpread,0,600)||!In(S.SpawnSpacing,30,240)||!In(S.SpawnOffsetY,-500,500)||!In(S.SimulationSpeed,.1f,3))return Fail(Error,TEXT("Spawn geometry or simulation speed outside bounds."));
    return true;
}
bool CireDeveloperTools::Apply(ACireGameMode* Mode,const FCireDeveloperSettings& S,FString* Error)
{
    if(!Mode||!CanEdit(Mode->GetWorld()))return Fail(Error,TEXT("Developer tuning requires a development standalone game."));
    if(CireBalanceLab::IsActive(Mode))return Fail(Error,TEXT("Stop the balance lab before applying match tuning."));
    if(!Validate(S,Error))return false;
    if(!S.bEnabled)return Restore(Mode,Error);
    auto* GS=Mode->GetGameState<ACireGameState>();if(!GS)return Fail(Error,TEXT("Match state is unavailable."));
    auto& State=States.FindOrAdd(Mode->GetWorld());
    if(!State.bSnapshot)
    {
        State.OriginalDurations=Mode->Clock.GetDurations();State.OriginalBreather=Mode->WaveBreatherSeconds;
        State.OriginalRecovery=Mode->RecoverySeconds;State.OriginalWaves=GS->WavesPerCycle;
        State.OriginalTimeScale=Mode->GetWorld()->GetWorldSettings()->TimeDilation;State.bSnapshot=true;
    }
    if(!Mode->Clock.SetDurations({S.PrepSeconds,S.ArenaSeconds,S.RecoverySeconds}))return Fail(Error,TEXT("Clock rejected durations."));
    State.Current=S;Mode->WaveBreatherSeconds=S.WaveBreatherSeconds;Mode->RecoverySeconds=S.RecoverySeconds;
    GS->WavesPerCycle=S.WavesPerCycle;Mode->WaveTimer=FMath::Min(Mode->WaveTimer,S.WaveBreatherSeconds);
    Mode->GetWorld()->GetWorldSettings()->SetTimeDilation(S.SimulationSpeed);
    UE_LOG(LogTemp,Display,TEXT("CIRE_DEVELOPER_APPLIED prep=%.1f arena=%.1f recovery=%.1f waves=%d health=%.2f damage=%.2f"),S.PrepSeconds,S.ArenaSeconds,S.RecoverySeconds,S.WavesPerCycle,S.MonsterHealthScale,S.MonsterDamageScale);
    return true;
}
bool CireDeveloperTools::Restore(ACireGameMode* Mode,FString* Error)
{
    if(!Mode||!CanEdit(Mode->GetWorld()))return Fail(Error,TEXT("Restore requires a development standalone game."));
    if(CireBalanceLab::IsActive(Mode))return Fail(Error,TEXT("Stop the balance lab before restoring match tuning."));
    auto& S=States.FindOrAdd(Mode->GetWorld());
    if(S.bSnapshot)
    {
        Mode->Clock.SetDurations(S.OriginalDurations);Mode->WaveBreatherSeconds=S.OriginalBreather;Mode->RecoverySeconds=S.OriginalRecovery;
        if(auto* GS=Mode->GetGameState<ACireGameState>())GS->WavesPerCycle=S.OriginalWaves;
        Mode->GetWorld()->GetWorldSettings()->SetTimeDilation(S.OriginalTimeScale);
    }
    S.Current=Neutral;S.bSnapshot=false;return true;
}
bool CireDeveloperTools::SaveProfile(UWorld* World,const FCireDeveloperSettings& S,FString* Error)
{
    if(!CanEdit(World))return Fail(Error,TEXT("Cannot save developer settings from this session."));
    if(!Validate(S,Error))return false;
    FConfigFile C;C.SetString(Section,TEXT("Version"),TEXT("1"));
#define SAVE_FLOAT(Field) C.SetFloat(Section,TEXT(#Field),S.Field)
    SAVE_FLOAT(PrepSeconds);SAVE_FLOAT(ArenaSeconds);SAVE_FLOAT(RecoverySeconds);SAVE_FLOAT(WaveBreatherSeconds);
    SAVE_FLOAT(MonsterHealthScale);SAVE_FLOAT(MonsterDamageScale);SAVE_FLOAT(ProjectileSpeedScale);SAVE_FLOAT(ProjectileCollisionScale);
    SAVE_FLOAT(EffectDurationScale);SAVE_FLOAT(TelegraphScale);SAVE_FLOAT(ConstructHealthScale);SAVE_FLOAT(SummonHealthScale);
    SAVE_FLOAT(CooldownScale);
    SAVE_FLOAT(SpawnX);SAVE_FLOAT(SpawnSpread);SAVE_FLOAT(SpawnSpacing);SAVE_FLOAT(SpawnOffsetY);SAVE_FLOAT(SimulationSpeed);
#undef SAVE_FLOAT
    C.SetString(Section,TEXT("WavesPerCycle"),*FString::FromInt(S.WavesPerCycle));C.SetString(Section,TEXT("WaveUnitsOverride"),*FString::FromInt(S.WaveUnitsOverride));
    C.SetString(Section,TEXT("TeamLives"),*FString::FromInt(S.TeamLives));C.SetBool(Section,TEXT("bPauseWaveSpawns"),S.bPauseWaveSpawns);C.SetBool(Section,TEXT("bFreezePhaseClock"),S.bFreezePhaseClock);
    C.SetBool(Section,TEXT("bOverrideCollisionPolicies"),S.bOverrideCollisionPolicies);
#define SAVE_INT(Field) C.SetString(Section,TEXT(#Field),*FString::FromInt(S.Field))
    SAVE_INT(WorldPolicy);SAVE_INT(PlayerPolicy);SAVE_INT(MonsterPolicy);SAVE_INT(ProtectionPolicy);SAVE_INT(WallPolicy);
#undef SAVE_INT
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(Filename()),true);
    return C.Write(Filename())?true:Fail(Error,TEXT("Developer profile could not be saved."));
}
bool CireDeveloperTools::LoadProfile(UWorld* World,FCireDeveloperSettings& Out,FString* Error)
{
    if(!CanEdit(World))return Fail(Error,TEXT("Cannot load developer settings from this session."));
    FCireDeveloperSettings S;FConfigFile C;C.Read(Filename());int32 Version=1;C.GetInt(Section,TEXT("Version"),Version);
    if(Version!=1)return Fail(Error,TEXT("Unsupported developer profile version."));
#define LOAD_FLOAT(Field) C.GetFloat(Section,TEXT(#Field),S.Field)
    LOAD_FLOAT(PrepSeconds);LOAD_FLOAT(ArenaSeconds);LOAD_FLOAT(RecoverySeconds);LOAD_FLOAT(WaveBreatherSeconds);
    LOAD_FLOAT(MonsterHealthScale);LOAD_FLOAT(MonsterDamageScale);LOAD_FLOAT(ProjectileSpeedScale);LOAD_FLOAT(ProjectileCollisionScale);
    LOAD_FLOAT(EffectDurationScale);LOAD_FLOAT(TelegraphScale);LOAD_FLOAT(ConstructHealthScale);LOAD_FLOAT(SummonHealthScale);
    LOAD_FLOAT(CooldownScale);
    LOAD_FLOAT(SpawnX);LOAD_FLOAT(SpawnSpread);LOAD_FLOAT(SpawnSpacing);LOAD_FLOAT(SpawnOffsetY);LOAD_FLOAT(SimulationSpeed);
#undef LOAD_FLOAT
    C.GetInt(Section,TEXT("WavesPerCycle"),S.WavesPerCycle);C.GetInt(Section,TEXT("WaveUnitsOverride"),S.WaveUnitsOverride);C.GetInt(Section,TEXT("TeamLives"),S.TeamLives);
    C.GetBool(Section,TEXT("bPauseWaveSpawns"),S.bPauseWaveSpawns);C.GetBool(Section,TEXT("bFreezePhaseClock"),S.bFreezePhaseClock);
    C.GetBool(Section,TEXT("bOverrideCollisionPolicies"),S.bOverrideCollisionPolicies);
#define LOAD_INT(Field) C.GetInt(Section,TEXT(#Field),S.Field)
    LOAD_INT(WorldPolicy);LOAD_INT(PlayerPolicy);LOAD_INT(MonsterPolicy);LOAD_INT(ProtectionPolicy);LOAD_INT(WallPolicy);
#undef LOAD_INT
    if(!Validate(S,Error))return false;Out=S;return true;
}
bool CireDeveloperTools::AdvancePhase(ACireGameMode* Mode,FString* Error)
{
    if(!Mode||!CanEdit(Mode->GetWorld())||!Get(Mode->GetWorld()).bEnabled)return Fail(Error,TEXT("Enable developer overrides before forcing a transition."));
    if(CireBalanceLab::IsActive(Mode))return Fail(Error,TEXT("Stop the balance lab before advancing the match."));
    if(Mode->Clock.Phase()==Cires::MatchPhase::Finished)return Fail(Error,TEXT("A finished match must be restarted."));
    if(Mode->Clock.Phase()==Cires::MatchPhase::Survival)
    {
        for(int32 I=Mode->Monsters.Num()-1;I>=0;--I)if(IsValid(Mode->Monsters[I])&&Mode->Monsters[I]->PackId<0){Mode->Monsters[I]->Destroy();Mode->Monsters.RemoveAt(I);}
        if(auto* GS=Mode->GetGameState<ACireGameState>()){GS->CycleWavesDone=GS->WavesPerCycle;Mode->CycleWavesSpawned=GS->WavesPerCycle;}
        if(Mode->Clock.BeginIntermission())Mode->ChangePhase(1);return true;
    }
    if(Mode->Clock.Phase()==Cires::MatchPhase::Arena){Mode->ResolveArena();return true;}
    for(const auto& Event:Mode->Clock.Advance(Mode->Clock.RemainingSeconds()))Mode->ChangePhase(static_cast<int32>(Event.To));
    return true;
}
bool CireDeveloperTools::SetLives(ACireGameMode* Mode,int32 Lives,FString* Error)
{
    if(!Mode||!CanEdit(Mode->GetWorld())||!Get(Mode->GetWorld()).bEnabled||Lives<1||Lives>1000)return Fail(Error,TEXT("Lives require enabled standalone developer tuning and a value from 1 to 1000."));
    if(CireBalanceLab::IsActive(Mode))return Fail(Error,TEXT("Stop the balance lab before resetting match lives."));
    auto* GS=Mode->GetGameState<ACireGameState>();if(!GS)return false;GS->EmberLives=GS->DuskLives=Lives;return true;
}
FVector CireDeveloperTools::SpawnPosition(UWorld* World,int32 Team,int32 Index,FVector Default)
{
    const auto& S=Get(World);if(!S.bEnabled)return Default;
    Index=FMath::Max(0,Index);
    const float X=S.SpawnX+(Index/2)*S.SpawnSpacing;
    const float LocalY=S.SpawnOffsetY+(Index%2==0?-S.SpawnSpread:S.SpawnSpread);
    return CireLanePath::ClampToLane(World,Team,FVector(X,CireLanePath::CenterY(Team)+LocalY,Default.Z),80);
}
void CireDeveloperTools::AdjustMonster(ACireMonster* Monster)
{
    if(!IsValid(Monster)||!Monster->HasAuthority())return;const auto& S=Get(Monster->GetWorld());if(!S.bEnabled)return;
    Monster->MaxHealth=FMath::Clamp(Monster->MaxHealth*S.MonsterHealthScale,1.f,1000000.f);Monster->Health=Monster->MaxHealth;
    Monster->Damage=FMath::Clamp(Monster->Damage*S.MonsterDamageScale,0.f,10000.f);
}
void CireDeveloperTools::AdjustArea(UWorld* World,FCireAreaSpec& Spec)
{const auto& S=Get(World);if(!S.bEnabled)return;Spec.DurationSeconds=FMath::Clamp(Spec.DurationSeconds*S.EffectDurationScale,.05f,60.f);Spec.WarningSeconds=FMath::Clamp(Spec.WarningSeconds*S.TelegraphScale,0.f,10.f);}
void CireDeveloperTools::AdjustSkillshot(UWorld* World,FCireSkillshotSpec& Spec)
{const auto& S=Get(World);if(!S.bEnabled)return;Spec.Speed=FMath::Clamp(Spec.Speed*S.ProjectileSpeedScale,50.f,10000.f);Spec.Radius=FMath::Clamp(Spec.Radius*S.ProjectileCollisionScale,1.f,200.f);Spec.WarningSeconds=FMath::Clamp(Spec.WarningSeconds*S.TelegraphScale,0.f,10.f);Spec.LifetimeSeconds=FMath::Clamp(Spec.LifetimeSeconds*S.EffectDurationScale,.05f,30.f);
if(S.bOverrideCollisionPolicies){Spec.WorldCollision=static_cast<ECireProjectileCollision>(S.WorldPolicy);Spec.PlayerCollision=static_cast<ECireProjectileCollision>(S.PlayerPolicy);Spec.MonsterCollision=static_cast<ECireProjectileCollision>(S.MonsterPolicy);Spec.ProtectionCollision=static_cast<ECireProjectileCollision>(S.ProtectionPolicy);Spec.WallCollision=static_cast<ECireProjectileCollision>(S.WallPolicy);}}
void CireDeveloperTools::AdjustConstruct(UWorld* World,FCireConstructSpec& Spec)
{const auto& S=Get(World);if(!S.bEnabled)return;Spec.MaxHealth=FMath::Clamp(Spec.MaxHealth*S.ConstructHealthScale,1.f,100000.f);Spec.LifetimeSeconds=FMath::Clamp(Spec.LifetimeSeconds*S.EffectDurationScale,.1f,120.f);}
void CireDeveloperTools::AdjustSummon(UWorld* World,FCireSummonSpec& Spec)
{const auto& S=Get(World);if(!S.bEnabled)return;Spec.Health=FMath::Clamp(Spec.Health*S.SummonHealthScale,1.f,100000.f);Spec.DurationSeconds=FMath::Clamp(Spec.DurationSeconds*S.EffectDurationScale,1.f,120.f);}
float CireDeveloperTools::EffectSeconds(UWorld* World,float Seconds){const auto& S=Get(World);return S.bEnabled?FMath::Clamp(Seconds*S.EffectDurationScale,0.f,300.f):Seconds;}
float CireDeveloperTools::CooldownSeconds(UWorld* World,float Seconds){const auto& S=Get(World);return S.bEnabled?FMath::Clamp(Seconds*S.CooldownScale,0.f,300.f):Seconds;}
#if !UE_BUILD_SHIPPING
bool CireDeveloperTools::RunRuntimeSmoke(ACireGameMode* Mode)
{
    if(!Mode||!CanEdit(Mode->GetWorld())||CireBalanceLab::IsActive(Mode))return false;
    auto* World=Mode->GetWorld();auto* GS=Mode->GetGameState<ACireGameState>();if(!GS)return false;
    const FState Previous=States.FindOrAdd(World);const auto Durations=Mode->Clock.GetDurations();
    const float Breather=Mode->WaveBreatherSeconds,Recovery=Mode->RecoverySeconds,WaveTimer=Mode->WaveTimer;
    const int32 Waves=GS->WavesPerCycle;const float TimeScale=World->GetWorldSettings()->TimeDilation;
    const auto Phase=Mode->Clock.Phase();const auto Remaining=Mode->Clock.RemainingSeconds();
    const FString TestFile=FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(),TEXT("DeveloperTests"),FGuid::NewGuid().ToString()+TEXT(".ini")));
    TGuardValue<FString> ProfileGuard(TestProfileOverride,TestFile);
    int32 Checks=0;bool Pass=true;
    auto Check=[&](bool OK,const TCHAR* Name){++Checks;Pass&=OK;if(!OK)UE_LOG(LogTemp,Error,TEXT("CIRE_DEVELOPER_RUNTIME_FAIL check=%s"),Name);};
    FCireDeveloperSettings S;S.bEnabled=true;S.PrepSeconds=121;S.ArenaSeconds=145;S.RecoverySeconds=22;S.WavesPerCycle=4;S.WaveBreatherSeconds=12;
    S.MonsterHealthScale=2;S.MonsterDamageScale=.5f;S.ProjectileSpeedScale=1.4f;S.ProjectileCollisionScale=.8f;
    S.EffectDurationScale=1.5f;S.TelegraphScale=.5f;S.ConstructHealthScale=2;S.SummonHealthScale=3;S.CooldownScale=.5f;
    S.bOverrideCollisionPolicies=true;S.WorldPolicy=0;S.PlayerPolicy=2;S.MonsterPolicy=1;S.ProtectionPolicy=3;S.WallPolicy=2;S.SimulationSpeed=.75f;
    Check(SaveProfile(World,S),TEXT("save isolated profile"));FCireDeveloperSettings Loaded;
    Check(LoadProfile(World,Loaded),TEXT("load isolated profile"));
    Check(!Loaded.bEnabled&&Loaded.PrepSeconds==121&&Loaded.ArenaSeconds==145&&Loaded.RecoverySeconds==22&&Loaded.WavesPerCycle==4,TEXT("profile never auto-enables"));
    Check(Loaded.bOverrideCollisionPolicies&&Loaded.ProtectionPolicy==3&&Loaded.PlayerPolicy==2&&Loaded.CooldownScale==.5f,TEXT("profile combat roundtrip"));
    Check(Apply(Mode,S),TEXT("apply valid settings"));
    Check(Mode->Clock.GetDurations().Intermission==121&&Mode->Clock.GetDurations().Arena==145&&Mode->Clock.GetDurations().Recovery==22&&GS->WavesPerCycle==4,TEXT("real clock and cycle updated"));
    Check(Mode->Clock.Phase()==Phase&&(Phase!=Cires::MatchPhase::Survival||Mode->Clock.RemainingSeconds()==Remaining),TEXT("phase preserved"));
    Check(FMath::IsNearlyEqual(World->GetWorldSettings()->TimeDilation,.75f),TEXT("world dilation updated"));
    Check(EffectSeconds(World,4)==6&&CooldownSeconds(World,4)==2,TEXT("live duration queries"));
    FCireSkillshotSpec Shot;const float OriginalSpeed=Shot.Speed;AdjustSkillshot(World,Shot);
    Check(FMath::IsNearlyEqual(Shot.Speed,OriginalSpeed*1.4f)&&Shot.ProtectionCollision==ECireProjectileCollision::Reflect&&Shot.PlayerCollision==ECireProjectileCollision::Pierce,TEXT("live projectile policies"));
    FCireAreaSpec Area;const float AreaDuration=Area.DurationSeconds;AdjustArea(World,Area);Check(Area.DurationSeconds==AreaDuration*1.5f,TEXT("live ground duration"));
    const auto Before=Get(World);FCireDeveloperSettings Invalid=S;Invalid.PrepSeconds=-4;
    Check(!Apply(Mode,Invalid)&&Get(World).PrepSeconds==Before.PrepSeconds&&Mode->Clock.GetDurations().Intermission==121,TEXT("invalid apply atomic"));
    Check(Restore(Mode),TEXT("restore snapshot"));
    Check(!Get(World).bEnabled&&Mode->Clock.GetDurations().Intermission==Durations.Intermission&&GS->WavesPerCycle==Waves&&FMath::IsNearlyEqual(World->GetWorldSettings()->TimeDilation,TimeScale),TEXT("original runtime restored"));
    // Always restore the caller's exact developer state, including a preexisting
    // snapshot. The smoke test never advances phases or edits normal profiles.
    States.FindOrAdd(World)=Previous;Mode->Clock.SetDurations(Durations);Mode->WaveBreatherSeconds=Breather;Mode->RecoverySeconds=Recovery;Mode->WaveTimer=WaveTimer;
    GS->WavesPerCycle=Waves;World->GetWorldSettings()->SetTimeDilation(TimeScale);
    Check(IFileManager::Get().Delete(*TestFile,false,true),TEXT("isolated profile cleanup"));
    UE_LOG(LogTemp,Display,TEXT("CIRE_DEVELOPER_RUNTIME_%s checks=%d"),Pass?TEXT("PASS"):TEXT("FAIL"),Checks);return Pass;
}
bool CireDeveloperTools::RunValidationSmoke()
{
    FCireDeveloperSettings S;bool Pass=Validate(S);S.PrepSeconds=0;Pass &= !Validate(S);S=FCireDeveloperSettings();S.MonsterDamageScale=std::numeric_limits<float>::quiet_NaN();Pass &= !Validate(S);
    S=FCireDeveloperSettings();S.WaveUnitsOverride=41;Pass &= !Validate(S);S=FCireDeveloperSettings();S.SpawnOffsetY=9000;Pass &= !Validate(S);
    S=FCireDeveloperSettings();S.SimulationSpeed=4;Pass &= !Validate(S);Pass &= !CanEdit(nullptr)&&!Get(nullptr).bEnabled;
    Cires::MatchClock C;C.BeginIntermission();C.Advance(20);Pass &= C.SetDurations({80,120,30})&&C.RemainingSeconds()==60&&C.Phase()==Cires::MatchPhase::Intermission;
    Pass &= C.SetDurations({5,90,15})&&C.RemainingSeconds()==0&&C.Advance(0).size()==1&&C.Phase()==Cires::MatchPhase::Arena;
    UE_LOG(LogTemp,Display,TEXT("CIRE_DEVELOPER_VALIDATION_%s checks=10"),Pass?TEXT("PASS"):TEXT("FAIL"));return Pass;
}
#endif
