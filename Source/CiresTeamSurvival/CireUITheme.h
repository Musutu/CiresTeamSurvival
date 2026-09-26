#pragma once
// ui-themes: the selectable skin of the Cire UI style kit (Docs/UIThemes.md).
// A theme is data (Content/Data/UIThemes.json): a palette written into CireUIColors, one
// texture atlas of nine-slice art pieces (frames, slots, rings, bars, banners, ornaments) plus a
// tileable panel fill, and a few glow/ornament switches. The kit painters (CireUIStyle) draw
// from the active theme, so switching themes restyles every screen that uses the kit at once.
// Art: generated for Eric via ChatGPT, sliced by Tools/BuildUIThemes.py (Content/UI/Themes/LICENSES.md).
#include "CoreMinimal.h"

class UTexture2D;
struct FCireUIPainter;

/** Art pieces a theme atlas provides (names match the JSON "pieces" keys). */
enum class ECireThemePiece : uint8
{
    Panel,        // ornate panel frame (9-slice, hollow centre)
    Card,         // light frame for buttons, rows and cards (9-slice)
    Tooltip,      // tooltip frame (9-slice)
    Slot,         // square action-button frame
    SlotPassive,  // octagonal passive frame
    SlotUltimate, // ornate ultimate frame
    Ring,         // portrait ring (hollow centre)
    BarFrame,     // health/resource bar frame (horizontal 3-slice)
    CastFrame,    // cast-bar frame (horizontal 3-slice)
    Minimap,      // minimap border (9-slice)
    Banner,       // banner ribbon (horizontal 3-slice)
    Divider,      // horizontal divider with a centre ornament (horizontal 3-slice)
    Ornament,     // top-centre crest / medallion
    BarFill,      // grayscale glossy fill, tinted per resource
    // hud-art: optional state/extra pieces (a theme without them falls back to the base piece + tint).
    SlotHover, SlotPressed, SlotCooldown, SlotDisabled,
    Button, ButtonHover, ButtonPressed, ButtonDisabled,
    BuffBorder,
    Count,
    RequiredCount = SlotHover // pieces every theme must provide
};

struct FCireThemePiece
{
    FVector2f UV0 = FVector2f::ZeroVector, UV1 = FVector2f::ZeroVector; // atlas rect (0..1)
    FVector2f SizePx = FVector2f::ZeroVector;                            // source size in pixels
    float Slice = .3f;   // corner/cap fraction of the piece (0 = stretch whole)
    float Corner = 16.f; // drawn corner/cap size in logical units
    bool bValid = false;
};

struct FCireUITheme
{
    FName Id;
    FString Name, Tagline;
    // Palette (linear). Written into CireUIColors when the theme becomes active.
    FLinearColor Trim, BrightTrim, Text, Muted, Ink, Card, Hover, Accent, Glow, TooltipBg, TooltipBorder, BarBack, Title, Filigree, PanelTint;
    FString AtlasPath, FillPath;
    FCireThemePiece Pieces[static_cast<int32>(ECireThemePiece::Count)];
    float GlowStrength = 1.f;   // hover/proc glow multiplier
    float FillScale = 256.f;    // logical size of one fill tile
    float PanelOpacity = .96f;
    bool bGem = true;           // centre gem/crest on panel tops
    bool bCornerOrnaments = true;
    // Resolved textures (loaded on first use, rooted).
    mutable UTexture2D* Atlas = nullptr;
    mutable UTexture2D* Fill = nullptr;
    mutable bool bLoaded = false;

    const FCireThemePiece& Piece(ECireThemePiece P) const { return Pieces[static_cast<int32>(P)]; }
};

namespace CireUITheme
{
    CIRESTEAMSURVIVAL_API const TCHAR* PieceName(ECireThemePiece Piece);
    /** Parses theme JSON; Errors lists every problem (empty = valid). */
    CIRESTEAMSURVIVAL_API bool ParseJson(const FString& Json, TArray<FCireUITheme>& Out, FName& OutDefault, TArray<FString>& Errors);
    /** Loads Content/Data/UIThemes.json once (safe to call often). */
    CIRESTEAMSURVIVAL_API const TArray<FCireUITheme>& All();
    CIRESTEAMSURVIVAL_API FName DefaultId();
    /** The active theme (the default until SetActive). Never null once All() is non-empty. */
    CIRESTEAMSURVIVAL_API const FCireUITheme* Active();
    CIRESTEAMSURVIVAL_API const FCireUITheme* Find(FName Id);
    /** Activates a theme (unknown ids fall back to the default) and applies its palette. Returns the id used. */
    CIRESTEAMSURVIVAL_API FName SetActive(FName Id);
    /** Loads (and roots) the atlas/fill of a theme. False if either is missing. */
    CIRESTEAMSURVIVAL_API bool LoadArt(const FCireUITheme& Theme);
    /** Asset paths of every theme's textures (hard references for cooking). */
    CIRESTEAMSURVIVAL_API const TArray<FString>& AssetPaths();

    /** Draws a piece of the active theme; false (nothing drawn) when the theme has no art for it. */
    CIRESTEAMSURVIVAL_API bool Draw(const FCireUIPainter& P, ECireThemePiece Piece, float X, float Y, float W, float H,
        FLinearColor Tint = FLinearColor::White, float CornerScale = 1.f);
    /** Tiled panel fill of the active theme; false when missing. */
    CIRESTEAMSURVIVAL_API bool DrawFill(const FCireUIPainter& P, float X, float Y, float W, float H, FLinearColor Tint);
    /** Bar fill: the piece's gloss stretched over [X, X+W*Fraction]. */
    CIRESTEAMSURVIVAL_API bool DrawBarFill(const FCireUIPainter& P, float X, float Y, float W, float H, FLinearColor Tint);

    /** Validation used by the native checks: every theme parses, has all pieces, and its art loads. */
    CIRESTEAMSURVIVAL_API bool Validate(TArray<FString>& Errors, bool bRequireArt);
    /** Native smoke (CireUIThemeTests.cpp): data, art per theme, palette, parser errors, profile persistence/migration. */
    CIRESTEAMSURVIVAL_API bool RunSmoke();
}
