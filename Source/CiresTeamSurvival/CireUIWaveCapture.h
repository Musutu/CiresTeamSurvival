#pragma once
// ui-themes: HUD screenshots during a real wave fight (see CireUIWaveCapture.cpp).
#include "CoreMinimal.h"

class ACireGameMode;

namespace CireUIWaveCapture
{
    /** Called every game-mode tick while the wave soak runs; no-op without -CireUIWaveCapture. */
    void Tick(ACireGameMode* Mode);
}
