#pragma once
// fix/summons: the summons bar (Docs/Pets.md "Summons bar"). One compact entry per active summon and
// construct a champion owns (Oathbound Guardian, Spectral Pack/Hunt, Mechanical Tank, turrets, obelisk,
// pylons, skitters, mines and traps, walls, Pavise). The persistent companion (Ashfang) keeps its own
// frame above it. The data is collected here (testable without a canvas); CireHUDSummons.cpp draws it.
// Works on every client: summons replicate OwnerHero, constructs replicate their actor Owner.
#include "CoreMinimal.h"

class AActor;
class ACireHero;
class ACireSummon;
class ACireConstruct;

enum class ECireSummonBarKind : uint8 { Summon, Construct };

struct CIRESTEAMSURVIVAL_API FCireSummonBarEntry
{
    TWeakObjectPtr<AActor> Unit;
    ECireSummonBarKind Kind = ECireSummonBarKind::Summon;
    FString Name;              // display name ("Oathbound Guardian", "Photon Turret")
    FString IconId;            // Ability DB id for the painted icon / sigil
    float Health = 0.f, MaxHealth = 0.f;
    float Remaining = -1.f;    // seconds left; < 0 = no timer
    float Duration = 0.f;      // full lifetime (timer fraction)
    bool bCommandable = false; // accepts attack / follow / stay orders
    bool bFights = false;      // deals damage (turret, summon, skitter, trap)
    FString State;             // "ATTACKING", "FOLLOWING", "HOLDING", "MOVING", "GUARDING", "ARMED", "FIELD"
};

namespace CireSummonsBar
{
    /** Tiles shown before the "+N" overflow badge. */
    constexpr int32 MaxShown = 10;
    /** Up to this many entries draw as named rows; more switch to compact icon tiles. */
    constexpr int32 MaxRows = 3;
    /** Owner's live summons (not its companion pet) then constructs, commandable first. ServerNow drives the timers. */
    CIRESTEAMSURVIVAL_API TArray<FCireSummonBarEntry> Collect(const ACireHero* Owner, float ServerNow);
    /** True when the owner has at least one live commandable summon (the pet keys also order it). */
    CIRESTEAMSURVIVAL_API bool HasCommandable(const ACireHero* Owner);
    CIRESTEAMSURVIVAL_API FString IconFor(const ACireSummon* Summon);
    CIRESTEAMSURVIVAL_API FString IconFor(const ACireConstruct* Construct);
    CIRESTEAMSURVIVAL_API FString NameFor(const ACireConstruct* Construct);
    /** "12s" / "1:05" timer text. */
    CIRESTEAMSURVIVAL_API FString TimerText(float Seconds);
}
