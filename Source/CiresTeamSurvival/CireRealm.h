#pragma once
#include "CoreMinimal.h"
class AActor;
namespace CireRealm {
    // Shared visibility rule for replication and listen-server rendering.
    bool CanObserve(const AActor* Observer, const AActor* Subject);
    void UpdateVisibility(AActor* Subject);
}
