#pragma once
#include "CoreMinimal.h"
class ACireGameMode;
// -CireNPCPackPreview: renders an elite challenge pack with its Pack Leader, role
// labels and a telegraphed cleave, then exits. Run via Tools/RunNPCPackPreview.py.
namespace CireNPCPackPreview
{
#if !UE_BUILD_SHIPPING
    bool Initialize(ACireGameMode* Mode);
    bool Tick(ACireGameMode* Mode);
#endif
}
