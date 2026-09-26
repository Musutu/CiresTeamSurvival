#pragma once
// layout-wiring: the wave LEASH / SNAP-BACK (Eric 2026-09-26: "the placements need to define how the waves will come and
// path, and snap back to if kited too far"). Docs/MapLayout.md "Leash".
//
// A wave unit marches its path (March). When it takes a target it chases (Chase) and remembers the path point it left
// (the anchor). If it is pulled farther from its path than its rank's leash radius, or its target leaves the leash zone
// while the unit stands well off the path, it gives up: it disengages and walks back (Return) to the anchor (or the
// nearest point of the path), faster than it marches, immune to damage (the WoW evade) and regenerating health, then
// resumes the march from there.
//
// Threat (Eric's ruling: only death drops threat) is never removed by the leash. The leash limits PURSUIT: a unit only
// selects threat holders standing inside its leash zone (within radius - pursuitMargin of its path), and never while
// returning. The table keeps every holder and its value; a kiter who comes back into the zone is the target again with
// all the threat he built.
//
// Kited is not stuck: the leash measures the distance from the unit's PATH, and only a chasing unit can be leashed. A unit
// jammed at a market stall stands on its path (a few metres off at most), so it never leashes; the wave director's stuck
// rescue (nudge along the path, repath while chasing) keeps handling it. A returning unit that stops making progress is
// rescued the same way: it is set down at its return point.
//
// Data: Content/Data/MonsterLeash.json (built-in defaults below). Server authoritative; LeashState replicates.
#include "CoreMinimal.h"

class ACireMonster;
class ACireHero;
class ACireGameMode;

enum class ECireLeashState : uint8 { March = 0, Chase = 1, Return = 2 };

struct CIRESTEAMSURVIVAL_API FCireLeashRules
{
    bool bEnabled = true;
    /** Leash radius (cm from the unit's path) by rank: normal (normal / veteran), elite (elite / champion), boss (warlord,
     *  mythic, bosses and lane bosses). */
    float RadiusNormal = 1800.f, RadiusElite = 2400.f, RadiusBoss = 3200.f;
    /** A target is pursued only inside radius - PursuitMargin of the path, so the unit that reaches it stays inside. */
    float PursuitMargin = 300.f;
    /** A chaser whose target leaves the zone walks back (evading) when it stands farther than this from its path. */
    float DisengageDistance = 700.f;
    /** Return to the anchor (the path point it left, WoW "home") or the nearest path point. */
    bool bReturnToAnchor = true;
    float ReturnSpeedMultiplier = 1.6f;
    /** Immune to damage while returning (the WoW evade). Untargetable also drops heroes' target on it. */
    bool bImmuneWhileReturning = true, bUntargetableWhileReturning = false;
    /** Fraction of max health regenerated per second while returning; HealOnArrive restores it fully on arrival. */
    float RegenPerSecond = .2f;
    bool bHealOnArrive = false;
    /** Arrival: within this of the return point. */
    float ResumeRadius = 200.f;
    /** After returning, the unit ignores targets for this long (it resumes the march first). */
    float ReengageSeconds = 2.f;
    /** A return that takes longer is a stuck return: the unit is set down at its return point. */
    float MaxReturnSeconds = 15.f;
    /** A return that makes no progress for this long is stuck too. */
    float StuckReturnSeconds = 4.f;
};

/** Inputs of one leash step (pure: the unit tests drive it directly). */
struct CIRESTEAMSURVIVAL_API FCireLeashInput
{
    ECireLeashState State = ECireLeashState::March;
    /** cm from the unit to its path. */
    float PathDistance = 0.f;
    /** It has a live target, and that target stands inside its leash zone. */
    bool bHasTarget = false, bTargetInZone = false;
    float Radius = 1800.f;
    /** cm from the unit to its return point (Return only). */
    float DistanceToReturn = 0.f;
};

namespace CireLeash
{
    CIRESTEAMSURVIVAL_API const FCireLeashRules& Rules();
    CIRESTEAMSURVIVAL_API bool Reload(FString* Error = nullptr);
    CIRESTEAMSURVIVAL_API bool ParseJson(const FString& Json, FCireLeashRules& Out, FString& Error);
    /** The state machine: March -> Chase (a target in the zone) -> Return (kited past the radius, or the target left and the
     *  unit stands off the path) -> March (back at the return point). Stuck units without a target stay in March. */
    CIRESTEAMSURVIVAL_API ECireLeashState Step(const FCireLeashRules& Rules, const FCireLeashInput& In);
    /** The unit is leashed at all (a director wave unit on a path; not packs, escorts, forced marchers or bonus creatures). */
    CIRESTEAMSURVIVAL_API bool Applies(const ACireMonster* Monster);
    CIRESTEAMSURVIVAL_API float RadiusFor(const FCireLeashRules& Rules, const ACireMonster* Monster);
    /** May this unit pursue this hero now (inside the zone, not returning, re-engage delay over)? True when not leashed. */
    CIRESTEAMSURVIVAL_API bool CanPursue(const ACireMonster* Monster, const ACireHero* Hero);
    CIRESTEAMSURVIVAL_API bool IsReturning(const ACireMonster* Monster);
    /** Damage allowed (false while an immune unit evades home). */
    CIRESTEAMSURVIVAL_API bool AllowDamage(const ACireMonster* Monster);
    /** Server NPC tick hook: runs the state machine; true while returning (the leash owns the unit this tick). */
    CIRESTEAMSURVIVAL_API bool Tick(ACireMonster* Monster, float DeltaSeconds);
    /** Stuck while returning (or a return past its time cap): set down at the return point and resume the march. */
    CIRESTEAMSURVIVAL_API void Rescue(ACireMonster* Monster);
    /** Leash returns and rescues this match (probes). */
    CIRESTEAMSURVIVAL_API void Counts(int32& Returns, int32& Arrivals, int32& Rescues);
#if !UE_BUILD_SHIPPING
    CIRESTEAMSURVIVAL_API bool RunTests(ACireGameMode* Mode);
#endif
}
