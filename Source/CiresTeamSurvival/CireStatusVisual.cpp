#include "CireStatusVisual.h"
#include "CireAuraVisuals.h"
#include "GameFramework/Character.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

void CireStatusVisual::Attach(ACharacter* Unit)
{
    // Driven by the same replicated expiry values and buff records the HUD reads.
    // -CireNoAuras disables the presentation entirely (performance A/B, diagnostics).
    static const bool bDisabled=FParse::Param(FCommandLine::Get(),TEXT("CireNoAuras"));
    if(!bDisabled)CireAuraVisuals::Attach(Unit);
}
