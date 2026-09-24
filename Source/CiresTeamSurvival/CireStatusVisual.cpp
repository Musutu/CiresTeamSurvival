#include "CireStatusVisual.h"
#include "CireAuraVisuals.h"
#include "GameFramework/Character.h"

void CireStatusVisual::Attach(ACharacter* Unit)
{
    // Driven by the same replicated expiry values and buff records the HUD reads.
    CireAuraVisuals::Attach(Unit);
}
