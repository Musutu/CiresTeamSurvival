#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CireTownGoal.generated.h"

class UBoxComponent;
class UPrimitiveComponent;

/** Authoritative town entry trigger. Its visible landmark is built by ACireWorld. */
UCLASS()
class CIRESTEAMSURVIVAL_API ACireTownGoal : public AActor
{
    GENERATED_BODY()
public:
    ACireTownGoal();
    virtual void BeginPlay() override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UBoxComponent> GoalVolume;
    UPROPERTY(Replicated) int32 TeamId = 0;
    bool ContainsLocation(const FVector& Location) const;
private:
    UFUNCTION() void OnGoalOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
        UPrimitiveComponent* OtherComponent, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);
};
