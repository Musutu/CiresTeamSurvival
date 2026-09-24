#pragma once
#include "CoreMinimal.h"

class UWorld;
class ACireGameMode;
class ACireMonster;
struct FCireAreaSpec;
struct FCireSkillshotSpec;
struct FCireConstructSpec;
struct FCireSummonSpec;

// Neutral by default. Editable only in development standalone worlds; this is
// not a client RPC or a shipping cheat surface. Authored combat data is untouched.
struct CIRESTEAMSURVIVAL_API FCireDeveloperSettings
{
    bool bEnabled=false;
    float PrepSeconds=60,ArenaSeconds=90,RecoverySeconds=15,WaveBreatherSeconds=8;
    int32 WavesPerCycle=3,WaveUnitsOverride=0,TeamLives=100;
    bool bPauseWaveSpawns=false,bFreezePhaseClock=false;
    float MonsterHealthScale=1,MonsterDamageScale=1;
    float ProjectileSpeedScale=1,ProjectileCollisionScale=1;
    float EffectDurationScale=1,TelegraphScale=1,ConstructHealthScale=1,SummonHealthScale=1;
    float CooldownScale=1;
    bool bOverrideCollisionPolicies=false;
    int32 WorldPolicy=1,PlayerPolicy=1,MonsterPolicy=1,ProtectionPolicy=1,WallPolicy=1;
    float SpawnX=12000,SpawnSpread=180,SpawnSpacing=100,SpawnOffsetY=0;
    float SimulationSpeed=1;
};
namespace CireDeveloperTools
{
    CIRESTEAMSURVIVAL_API bool CanEdit(UWorld* World);
    CIRESTEAMSURVIVAL_API void Initialize(ACireGameMode* Mode);
    CIRESTEAMSURVIVAL_API const FCireDeveloperSettings& Get(UWorld* World);
    CIRESTEAMSURVIVAL_API bool Validate(const FCireDeveloperSettings& Settings,FString* Error=nullptr);
    CIRESTEAMSURVIVAL_API bool Apply(ACireGameMode* Mode,const FCireDeveloperSettings& Settings,FString* Error=nullptr);
    CIRESTEAMSURVIVAL_API bool Restore(ACireGameMode* Mode,FString* Error=nullptr);
    CIRESTEAMSURVIVAL_API bool SaveProfile(UWorld* World,const FCireDeveloperSettings& Settings,FString* Error=nullptr);
    CIRESTEAMSURVIVAL_API bool LoadProfile(UWorld* World,FCireDeveloperSettings& Settings,FString* Error=nullptr);
    CIRESTEAMSURVIVAL_API bool AdvancePhase(ACireGameMode* Mode,FString* Error=nullptr);
    CIRESTEAMSURVIVAL_API bool SetLives(ACireGameMode* Mode,int32 Lives,FString* Error=nullptr);
    CIRESTEAMSURVIVAL_API FVector SpawnPosition(UWorld* World,int32 Team,int32 Index,FVector Default);
    CIRESTEAMSURVIVAL_API void AdjustMonster(ACireMonster* Monster);
    CIRESTEAMSURVIVAL_API void AdjustArea(UWorld* World,FCireAreaSpec& Spec);
    CIRESTEAMSURVIVAL_API void AdjustSkillshot(UWorld* World,FCireSkillshotSpec& Spec);
    CIRESTEAMSURVIVAL_API void AdjustConstruct(UWorld* World,FCireConstructSpec& Spec);
    CIRESTEAMSURVIVAL_API void AdjustSummon(UWorld* World,FCireSummonSpec& Spec);
    CIRESTEAMSURVIVAL_API float EffectSeconds(UWorld* World,float Seconds);
    CIRESTEAMSURVIVAL_API float CooldownSeconds(UWorld* World,float Seconds);
#if !UE_BUILD_SHIPPING
    CIRESTEAMSURVIVAL_API bool RunValidationSmoke();
    CIRESTEAMSURVIVAL_API bool RunRuntimeSmoke(ACireGameMode* Mode);
#endif
}
