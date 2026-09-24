#pragma once
#include "CoreMinimal.h"
class ACireWorld;
namespace CireEnvironmentProps
{
    void Build(ACireWorld* WorldActor);
    void Refresh(ACireWorld* WorldActor);
    int32 InstanceCount(const ACireWorld* WorldActor);
    bool HasSafeClearance(const ACireWorld* WorldActor);
}
