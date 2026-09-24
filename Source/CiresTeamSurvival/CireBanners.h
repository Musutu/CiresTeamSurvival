#pragma once
// Unified transition banners ("WAVE 4", "PREP PHASE", "VICTORY"...): a priority
// queue so banners never overlap, drawn by ACireHUD with the style kit and scaled
// with the interface scale. Local presentation only; see Docs/UIStyle.md.
#include "CoreMinimal.h"
#include "CireUIStyle.h"

enum class ECireBanner : uint8
{
    LevelUp, WaveIncoming, WaveCleared, PrepPhase, Arena, Recovery,
    ChallengeUnlocked, BossSpawned, Victory, Defeat, Custom
};

namespace CireBanners
{
    /** Queue a banner. Kicker defaults per type ("WAVE INCOMING"...). Duplicates within 1.5s are dropped. */
    CIRESTEAMSURVIVAL_API void Show(ECireBanner Type, const FString& Title, const FString& Subtitle = FString(), const FString& Kicker = FString());
    /** Draws the active banner (starting the next queued one when free). Returns true and sets
     *  OutStarted when a banner began this frame, so the caller can play its sound. */
    CIRESTEAMSURVIVAL_API bool Draw(const FCireUIPainter& Painter, float ViewW, float ViewH, ECireBanner& OutStarted, float Y = -1.f);
    CIRESTEAMSURVIVAL_API void Clear();
    CIRESTEAMSURVIVAL_API int32 QueuedCount();
    CIRESTEAMSURVIVAL_API bool IsShowing();
    /** Look of each type (colour, default kicker, priority, duration). */
    CIRESTEAMSURVIVAL_API FCireBannerSpec DefaultSpec(ECireBanner Type);
    CIRESTEAMSURVIVAL_API int32 Priority(ECireBanner Type);
}
