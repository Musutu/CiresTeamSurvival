// balance: how bot owners command their companions (CirePets). Called from ACireHero::BotThink.
//   - Attack: the pet fights the bot's own target (re-ordered when the bot switches targets).
//   - Special (the Huntress's Dread Roar): used when a monster near the pet is hitting an ally who is not a tank,
//     or any ally under 50% health, so the roar's taunt peels it onto the cat.
//   - Revive: a fallen companion is revived as soon as the cooldown and range allow.
#pragma once
#include "CoreMinimal.h"

class ACireHero;
class ACireMonster;

namespace CireBotPets
{
    CIRESTEAMSURVIVAL_API void Think(ACireHero* Bot, float DeltaSeconds);
    /** The monster the roar should peel (hitting a threatened ally within Radius of the pet), or null. */
    CIRESTEAMSURVIVAL_API ACireMonster* RoarTarget(const ACireHero* Bot, float Radius);
}
