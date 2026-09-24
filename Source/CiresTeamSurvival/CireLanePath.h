#pragma once
#include "CoreMinimal.h"

class ACireMonster;
class ACireGameMode;
class ACireGameState;
class UWorld;

struct CIRESTEAMSURVIVAL_API FCireBattlefieldRoutes
{
    float MinX = -2350, MaxX = 13000, HalfWidth = 1120;
    TArray<FVector2D> LocalPoints[2];
    int32 EscortEveryWaves = 4, EscortCount = 1, EscortLeakCost = 1;
    float EscortHealthMultiplier = 6, EscortMoveSpeed = 170;
};

namespace CireLanePath
{
    CIRESTEAMSURVIVAL_API const FCireBattlefieldRoutes& Get(const UWorld* World = nullptr);
    CIRESTEAMSURVIVAL_API bool ParseJson(const FString& Json, FCireBattlefieldRoutes& Out, FString& Error);
    CIRESTEAMSURVIVAL_API bool Reload(FString* Error = nullptr);
    CIRESTEAMSURVIVAL_API bool Reload(UWorld* World, FString* Error = nullptr);
    CIRESTEAMSURVIVAL_API void PublishState(ACireGameState* State);
    CIRESTEAMSURVIVAL_API void ReceiveState(ACireGameState* State);
    CIRESTEAMSURVIVAL_API uint32 Revision(const UWorld* World = nullptr);
    CIRESTEAMSURVIVAL_API float CenterY(int32 Team);
    CIRESTEAMSURVIVAL_API bool Contains(int32 Team, const FVector& Point, float Margin = 0);
    CIRESTEAMSURVIVAL_API bool Contains(const UWorld* World, int32 Team, const FVector& Point, float Margin = 0);
    CIRESTEAMSURVIVAL_API FVector ClampToLane(int32 Team, FVector Point, float Margin = 0);
    CIRESTEAMSURVIVAL_API FVector ClampToLane(const UWorld* World, int32 Team, FVector Point, float Margin = 0);
    CIRESTEAMSURVIVAL_API FVector SpawnPosition(int32 Team, float Z = 110);
    CIRESTEAMSURVIVAL_API FVector SpawnPosition(const UWorld* World, int32 Team, float Z = 110);
    CIRESTEAMSURVIVAL_API FVector ChallengePosition(const UWorld* World, int32 Team, int32 Tier, float Z = 110);
    CIRESTEAMSURVIVAL_API void InitializeProgress(ACireMonster* Monster);
    CIRESTEAMSURVIVAL_API FVector NextWaypoint(ACireMonster* Monster);
    CIRESTEAMSURVIVAL_API bool ShouldSpawnEscort(int32 Wave);
    CIRESTEAMSURVIVAL_API bool ShouldSpawnEscort(const UWorld* World, int32 Wave);
    CIRESTEAMSURVIVAL_API void ConfigureEscort(ACireMonster* Monster);
    CIRESTEAMSURVIVAL_API void RefreshEscortCollision(ACireMonster* Monster);
#if !UE_BUILD_SHIPPING
    CIRESTEAMSURVIVAL_API bool RunSmoke(ACireGameMode* Mode);
#endif
}
