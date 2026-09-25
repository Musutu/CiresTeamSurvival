#pragma once
// scaling-kits: "Construct: Mechanical Tank" - a summoned mech built on ACireSummon (so monsters hold
// threat on it and enemy champions can target it). It protects the summoner's DPS and supports:
//   * attacks enemies that are hitting allies OTHER than the summoner (Cires::Kits::SelectMechAttackTarget);
//   * Taunt pulls an enemy that is NOT attacking the summoner (SelectMechTauntTarget);
//   * a small AoE ground slam that deals damage and generates threat; at skill level 15 the slam also
//     cuts enemy attack speed by 10% ("mech_weakened").
// Damage scales off the owner's primary stat; its attack speed and ability cooldowns inherit the owner's.
#include "CoreMinimal.h"
#include "CireSummon.h"
#include "CireMechTank.generated.h"

UCLASS()
class CIRESTEAMSURVIVAL_API ACireMechTank : public ACireSummon
{
    GENERATED_BODY()
public:
    ACireMechTank();
    virtual void Tick(float DeltaSeconds) override;
    static ACireMechTank* SpawnFor(ACireHero* Owner, FVector Point, FString* Why = nullptr);
    /** Picks and taunts now (ignores the cooldown). Returns the taunted unit or nullptr. */
    AActor* TryTaunt();
    /** Slams now (ignores the cooldown). Returns units hit. */
    int32 Slam();
    AActor* ChooseAttackTarget() const;
    /** kits-complete: DPS / Support champions are protected before other allies. */
    static bool IsProtectedRole(const ACireHero* Ally);
    float TauntCooldown = 0.f, SlamCooldown = 0.f;
    float TauntBaseCooldown = 8.f, SlamBaseCooldown = 6.f, TauntRange = 900.f, SlamRadius = 320.f, TauntSeconds = 3.f;
    UPROPERTY() TObjectPtr<AActor> LastTaunted;
    int32 SlamCount = 0;
};
