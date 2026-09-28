#pragma once
// ability-tuner: the replicated carrier of the match's ability override set (CireAbilityTuner.h). The server spawns
// one per match; every client rebuilds the same Ability DB rows from TuningJson (late joiners included).
#include "CoreMinimal.h"
#include "GameFramework/Info.h"
#include "CireAbilityTunerState.generated.h"

UCLASS(NotPlaceable)
class CIRESTEAMSURVIVAL_API ACireAbilityTunerState : public AInfo
{
    GENERATED_BODY()
public:
    ACireAbilityTunerState();
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;

    /** The whole override set (CireAbilityTuner::SerializeSet). */
    UPROPERTY(ReplicatedUsing=OnRep_Tuning) FString TuningJson;
    /** Bumped with every server change (forces OnRep even for an identical string after a reset). */
    UPROPERTY(ReplicatedUsing=OnRep_Tuning) int32 Revision = 0;
    /** The "Allow ability tuning" game option. */
    UPROPERTY(Replicated) bool bAllowTuning = false;
    /** A mode preset fixed the option (ranked / standard). */
    UPROPERTY(Replicated) bool bAllowLocked = false;
    UPROPERTY(Replicated) FString ProfileName;

    UFUNCTION() void OnRep_Tuning();
    /** Server: publish the process's active set. */
    void Publish(const FString& InProfileName);

    static ACireAbilityTunerState* Get(const UWorld* World);
};
