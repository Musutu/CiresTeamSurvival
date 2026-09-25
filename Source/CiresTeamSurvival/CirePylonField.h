// balance: calm ground presentation for construct pylon fields (Aegis, Haste, Gravity, Disruption, Aether Nexus and the
// Aetheri monster pylons). A pylon field is a harmless persistent zone that often overlaps others; drawn with the generic
// active-zone painter, 3-4 stacked pylons turned the ground near-opaque. Rules (ground-telegraph rules):
//   - fill 15-25% opacity, set by the ground-telegraph intensity slider (0.3 -> 15%, 1 -> 25%, default 0.6 -> 19%);
//   - overlap cap: N overlapping fields share that budget, so the stacked fill never exceeds the single-field fill;
//   - a readable rim (never below 40% alpha) and no runes, ripples or detonation on the fill;
//   - rising particles are budgeted per overlap group.
#pragma once
#include "CoreMinimal.h"

class ACireAreaEffect;
struct FCireGroundMesh;

namespace CirePylonField
{
    constexpr float MinFill = .15f, MaxFill = .25f;       // single-field fill opacity range (intensity 0..1)
    constexpr float MinEdge = .4f, MaxEdge = .7f;         // rim opacity range
    constexpr float FillEmissiveCap = .9f, EdgeEmissiveCap = 1.2f; // hue-preserving brightness caps (no bloom white-out)
    constexpr int32 MaxCountedOverlaps = 8;

    /** cire.PylonFieldCalm (default 1); 0 restores the generic zone painter for A/B captures. */
    CIRESTEAMSURVIVAL_API bool Enabled();
    /** True (while Enabled) for a persistent field owned by a construct pylon (champion or monster). */
    CIRESTEAMSURVIVAL_API bool IsPylonField(const ACireAreaEffect* Area);
    /** This field plus every other visible pylon field whose circle intersects it (1..MaxCountedOverlaps). */
    CIRESTEAMSURVIVAL_API int32 CountOverlaps(const ACireAreaEffect* Area);
    /** The ground-telegraph intensity slider (CireAbilityVFX::GroundIntensity) normalised to 0..1. */
    CIRESTEAMSURVIVAL_API float Intensity(const UWorld* World);

    /** Target opacity of the field fill (and the cap for any number of overlapping fields). */
    CIRESTEAMSURVIVAL_API float TargetFill(float Intensity);
    /** Per-field fill so that Overlaps stacked layers composite to exactly TargetFill: 1-(1-F)^(1/N). */
    CIRESTEAMSURVIVAL_API float LayerFill(float Intensity, int32 Overlaps);
    /** Alpha-over composite of Layers identical layers. */
    CIRESTEAMSURVIVAL_API float Composite(float LayerAlpha, int32 Layers);
    /** Rim opacity: readable at any overlap, dimmer when many rims cross. */
    CIRESTEAMSURVIVAL_API float EdgeAlpha(float Intensity, int32 Overlaps);
    /** Rising particles per field. */
    CIRESTEAMSURVIVAL_API int32 ParticleBudget(int32 Overlaps);
    /** Scales RGB so the brightest channel is at most Cap (hue preserved). */
    CIRESTEAMSURVIVAL_API FLinearColor CapBrightness(FLinearColor Color, float Cap);

    /** Paints the field (fill + rim + one slow breathing ring). Returns vertices added. */
    CIRESTEAMSURVIVAL_API int32 Paint(FCireGroundMesh& Ground, float Radius, FLinearColor Color, float Time, float Alpha, float Intensity, int32 Overlaps);

    /** Pure checks of the rules above (fill band, overlap cap, readable edge, slider monotonicity). */
    CIRESTEAMSURVIVAL_API bool RunTests(TArray<FString>& Failures);
}
