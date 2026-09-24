#pragma once
#include "CoreMinimal.h"

// Authored draft examples, never automatic learned skills. Planned entries are
// design records and must not be offered/cast until their runtime exists.
struct FCireChampionSkill
{
    FString Id,DisplayName,Status,Mechanic,VfxFamily,Delivery;
    bool IsImplemented() const { return Status==TEXT("implemented"); }
};
struct FCireChampionProfile
{
    FString Id,DisplayName,FamilyId,Variant,PrimaryStat,AttackStyle,ThreatRole;
    FString Description,ArtFamily,ArtStatus,ArtProvenance;
    int32 RuntimeArchetype=0,Strength=20,Agility=10,Intelligence=10;
    float BasicAttackRange=220,AttackSeconds=1.5f;
    TArray<FString> Roles;
    TArray<FCireChampionSkill> Actives;
    FCireChampionSkill Passive,Ultimate;
};
namespace CireChampionRoster
{
    const TArray<FCireChampionProfile>& All();
    const FCireChampionProfile* Find(const FString& Id);
    const FCireChampionProfile* FindByIndex(int32 Index);
    int32 Count();
    // Transactional: malformed input leaves Out/current data unchanged.
    bool ParseJson(const FString& Json,TArray<FCireChampionProfile>& Out,FString& Error);
    bool Reload();
    bool RunValidationSmoke();
}
