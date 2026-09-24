#pragma once
#include "CoreMinimal.h"

class ACharacter;
/**
 * Local cosmetic status/buff presentation for heroes and monsters.
 * aura-vfx: the old four generic shapes were replaced by the data-driven signature
 * system in CireAuraVisuals (Content/Data/BuffVisuals.json, Docs/BuffVisuals.md).
 */
namespace CireStatusVisual { CIRESTEAMSURVIVAL_API void Attach(ACharacter* Unit); }
