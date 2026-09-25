#pragma once
// progression-shop: the ornate "Skills" framing language of Eric's target image
// (Saved/Reference/skills-target.png): near-black panel with thin gold filigree, corner
// ornaments, diamond-studded rules, wide-spaced serif caps, and the three scroll tiers
// (Golden = active, Plain parchment = passive, Prismatic = ultimate). Stateless drawing helpers
// shared by the Skill Shop and the Armory (item shop). Art: Content/UI/Shop/Scrolls (LICENSES.md).
#include "CoreMinimal.h"
#include "CireUIStyle.h"

class UTexture2D;

namespace CireShopArt
{
    enum class EScroll : uint8 { Golden, Plain, Prismatic };

    struct FRectF { float X = 0, Y = 0, W = 0, H = 0; };

    CIRESTEAMSURVIVAL_API UTexture2D* ScrollTexture(EScroll Tier);
    CIRESTEAMSURVIVAL_API UTexture2D* CrestTexture(EScroll Tier);

    // Ink colours for text on parchment.
    // Linear colours (the canvas is sRGB): these read as deep brown / red ink.
    inline const FLinearColor Ink(.022f, .011f, .004f, 1.f);
    inline const FLinearColor InkSoft(.055f, .03f, .011f, 1.f);
    inline const FLinearColor InkRed(.22f, .018f, .01f, 1.f);
    inline FLinearColor& Filigree = CireUIColors::ThemeFiligree; // ui-themes: follows the active theme

    // Near-black panel, double thin gold border, corner ornaments; the top edge rises into a
    // plate behind the title (TitleWidth 0 = flat top).
    CIRESTEAMSURVIVAL_API void Panel(const FCireUIPainter& P, float X, float Y, float W, float H, float TitleWidth = 0.f);
    // Letter-spaced text (Tracking in ems). Centered: X is the centre. Returns the drawn width.
    CIRESTEAMSURVIVAL_API float Spaced(const FCireUIPainter& P, const FString& Text, float X, float Y, float Size, float Tracking,
        FLinearColor Color, ECireFont Font = ECireFont::Display, bool bCentered = true, bool bShadow = true);
    CIRESTEAMSURVIVAL_API float SpacedWidth(const FCireUIPainter& P, const FString& Text, float Size, float Tracking, ECireFont Font = ECireFont::Display);
    CIRESTEAMSURVIVAL_API void Diamond(const FCireUIPainter& P, float X, float Y, float R, FLinearColor Color, bool bFilled = true);
    // Horizontal rule with diamond ends (and an optional centre diamond).
    CIRESTEAMSURVIVAL_API void Rule(const FCireUIPainter& P, float X1, float X2, float Y, FLinearColor Color, bool bCentreStud = false);
    // Vertical divider with diamond studs at the ends and in the middle.
    CIRESTEAMSURVIVAL_API void Divider(const FCireUIPainter& P, float X, float Y1, float Y2, FLinearColor Color);
    CIRESTEAMSURVIVAL_API void CompassStar(const FCireUIPainter& P, float X, float Y, float R, FLinearColor Color);
    // Title block: big spaced title flanked by rules, spaced subtitle below.
    CIRESTEAMSURVIVAL_API void Title(const FCireUIPainter& P, float CX, float Y, const FString& Title, const FString& Subtitle, float Size = 30.f);
    // Double gold ring with a dark centre (skill icon or category crest goes inside).
    CIRESTEAMSURVIVAL_API void CrestRing(const FCireUIPainter& P, float CX, float CY, float R, FLinearColor Accent, float Time = 0.f);
    // A category crest (sword / shield / star) in its ring, flanked by short rules.
    CIRESTEAMSURVIVAL_API void Crest(const FCireUIPainter& P, EScroll Tier, float CX, float CY, float R, float RuleHalf);

    // The scroll card, 3-sliced vertically so the rolls keep their shape. Returns the parchment
    // interior (where text is written). Dim darkens it (unaffordable), Lift raises its glow.
    CIRESTEAMSURVIVAL_API FRectF Scroll(const FCireUIPainter& P, EScroll Tier, float X, float Y, float W, float H, float Time,
        uint32 Seed, float Dim = 0.f, float Lift = 0.f);
    // Height a scroll has at width W without stretching the parchment.
    CIRESTEAMSURVIVAL_API float NaturalHeight(EScroll Tier, float W);
    // Red wax seal stamped on a purchase; Stamp 0..1 drives the drop, flash and settle.
    CIRESTEAMSURVIVAL_API void WaxSeal(const FCireUIPainter& P, float CX, float CY, float R, float Stamp, const FString& Glyph);
    // Soft additive glow (T_Glow).
    CIRESTEAMSURVIVAL_API void Glow(const FCireUIPainter& P, float X, float Y, float W, float H, FLinearColor Color);
}
