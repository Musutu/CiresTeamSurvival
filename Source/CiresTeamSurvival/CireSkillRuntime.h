#pragma once
#include "CireGame.h"
#include "Engine/World.h"
#include "CireLanePath.h"

// Shared scope rules for transient spell actors. They never cross a match phase.
namespace CireSkillRuntime
{
inline int32 Phase(const UWorld* World)
{
    if (!World) return INDEX_NONE;
    if (const auto* Mode = World->GetAuthGameMode<ACireGameMode>()) return static_cast<int32>(Mode->Clock.Phase());
    if (const auto* State = World->GetGameState<ACireGameState>()) return State->Phase;
    return INDEX_NONE;
}
inline int32 Team(const AActor* Actor)
{
    if (const auto* Hero = Cast<ACireHero>(Actor)) return Hero->TeamId;
    if (const auto* Monster = Cast<ACireMonster>(Actor)) return Monster->Lane;
    return INDEX_NONE;
}
inline bool Alive(const AActor* Actor)
{
    if (!IsValid(Actor) || Actor->IsActorBeingDestroyed()) return false;
    if (const auto* Hero = Cast<ACireHero>(Actor)) return Hero->bDrafted && !Hero->bDead && Hero->Health > 0;
    if (const auto* Monster = Cast<ACireMonster>(Actor)) return Monster->Health > 0;
    return false;
}
inline bool CanObserve(const AActor* Observer, const UWorld* World, int32 TeamId, int32 OriginPhase)
{
    if (World && World->IsPlayingReplay()) return true;
    const auto* Hero = Cast<ACireHero>(Observer);
    if (const auto* Controller = Cast<AController>(Observer)) Hero = Cast<ACireHero>(Controller->GetPawn());
    return Hero && Hero->TeamId >= 0 && Hero->bDrafted && Phase(World) == OriginPhase &&
        (OriginPhase == static_cast<int32>(Cires::MatchPhase::Arena) || Hero->TeamId == TeamId);
}
inline bool InRealmBounds(const ACireGameMode* Mode, int32 TeamId, const FVector& Point, float Margin = 0)
{
    if (!Mode || Point.ContainsNaN() || TeamId < 0 || TeamId > 1) return false;
    if (Mode->Clock.Phase() == Cires::MatchPhase::Arena)
    {
        const float ArenaY = Mode->ArenaPosition(0, 2).Y;
        return FMath::Abs(Point.X) <= 2100.f - Margin && FMath::Abs(Point.Y - ArenaY) <= 1450.f - Margin;
    }
    return CireLanePath::Contains(Mode->GetWorld(),TeamId,Point,Margin);
}
}
