#pragma once

#include "CoreMinimal.h"

class ACireGameMode;

namespace CireFeedbackPreview
{
#if !UE_BUILD_SHIPPING
    // Development-only, opt-in visual fixture; neither function affects normal play.
    bool Initialize(ACireGameMode* Mode);
    bool Tick(ACireGameMode* Mode);
#endif
}
