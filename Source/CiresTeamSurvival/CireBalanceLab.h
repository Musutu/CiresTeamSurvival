#pragma once
#include "CoreMinimal.h"
class ACireGameMode;

struct FCireBalanceSnapshot
{
    bool bActive=false;
    FString Scenario,Result,ReportPath;
    float ElapsedSeconds=0,LimitSeconds=60;
    float Damage=0,Healing=0,DPS=0,HPS=0;
    float EnemyInitialHealth=0,EnemyHealthRemaining=0;
    int32 AlliesAlive=0,EnemiesAlive=0,VictimSwitches=0;
    float TankTargetShare=0,ThreatLeadRatio=0;
};

// Explicit development sandbox. Normal match actors and the player pawn are
// suspended, never repurposed; Stop restores them. Fixture actors use actual
// hero bot, NPC, skillshot, healing, threat, and damage systems.
namespace CireBalanceLab
{
    void Initialize(ACireGameMode* Mode);
    bool Tick(ACireGameMode* Mode,float DeltaSeconds); // true => skip normal match orchestration this frame
    bool Start(ACireGameMode* Mode,int32 Wave=1,int32 EnemyKind=-1,bool bPlayerMode=false,int32 TeamSize=5,int32 EnemyCount=5,float Seconds=60);
    bool StartArena(ACireGameMode* Mode,bool bPlayerMode=false,int32 TeamSize=5,float Seconds=60);
    bool StartLoadout(ACireGameMode* Mode,const FString& Preset,int32 Wave=10,int32 EnemyKind=-1,
        int32 TeamSize=5,int32 EnemyCount=5,float Seconds=90);
    void Stop(ACireGameMode* Mode);
    bool IsActive(const ACireGameMode* Mode);
    FCireBalanceSnapshot Snapshot(const ACireGameMode* Mode);
    FString Summary(const ACireGameMode* Mode);
}
