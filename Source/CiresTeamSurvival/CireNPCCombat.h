#pragma once
#include "CoreMinimal.h"
class ACireMonster;
class ACireHero;
class ACireGameMode;
namespace CireNPCCombat {
// Legacy numeric kinds 0..3 (infantry, bruiser, caster, ranged) map through
// NPCArchetypes.json "legacyKinds". bBoss configures the data-driven wave boss.
void Configure(ACireMonster* Monster,int32 Kind,int32 Wave,bool bBoss=false,int32 ChallengeTier=0,int32 Round=1);
// Data-driven configuration. Tier>0 means a challenge-pack unit (elite unless the
// archetype is already boss-classified). bLaneBoss marks a unit that leaks for its leakCost.
bool ConfigureArchetype(ACireMonster* Monster,FName ArchetypeId,int32 Wave,int32 ChallengeTier=0,int32 Round=1,bool bLaneBoss=false);
void Tick(ACireMonster* Monster,float Delta);
// Cancels current windup and owned combat actors; preserves threat. Always succeeds
// (phase changes, leash, death, escort conversion).
void Interrupt(ACireMonster* Monster);
// Player interrupt ("kick"): only cancels interruptible casts. Returns true when a cast was stopped.
bool InterruptCast(ACireMonster* Monster,ACireHero* Source);
// Server hook from ACireMonster::TakeDamage: armor, shield wall, guard redirect, provoke.
float ModifyIncomingDamage(ACireMonster* Monster,ACireHero* Attacker,float Amount);
// Current outgoing damage after rally/enrage buffs.
float EffectiveDamage(const ACireMonster* Monster);
// Route abstraction: the next point the unit walks toward on its lane, and whether it
// is inside its town goal. Wraps CireLanePath/ACireTownGoal so a new layout plugs in here.
FVector RouteDestination(ACireMonster* Monster);
bool ReachedGoal(ACireMonster* Monster);
#if !UE_BUILD_SHIPPING
bool RunSmoke(ACireGameMode* Mode);
bool RunRolesSmoke(ACireGameMode* Mode);
// ability-vfx: galleries/tests start one authored ability through the real StartAbility path
// (same range, sight, area and cast-bar rules), ignoring cooldown/loadout gating. False if refused.
bool DebugStartAbility(ACireMonster* Monster,FName AbilityId,ACireHero* Victim);
#endif
}
