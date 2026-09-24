#pragma once
// Cire UI style kit: the one visual language of the WoW-style interface.
// Canvas-based and draw-only: callers do their own hit testing and pass the
// resulting state (hover/pressed/...) in. Coordinates are logical HUD units
// (1280x720 reference, before the interface scale); see Docs/UIStyle.md.
// API is kept stable for other HUD code (draft screen, shop, toasts, banners).
#include "CoreMinimal.h"

class UCanvas;
class UFont;
class UFontFace;
class UTexture2D;
class USoundBase;
class UWorld;

/** Font hierarchy. Auto picks by text content: digits -> Numbers, ALL CAPS -> Heading, >=14 -> Bold, else Body. */
enum class ECireFont : uint8 { Auto, Body, Heading, Bold, Numbers };

/** Palette. */
namespace CireUIColors
{
    inline const FLinearColor Ink(.014f,.020f,.026f,.95f);
    inline const FLinearColor Card(.034f,.046f,.055f,.98f);
    inline const FLinearColor Hover(.075f,.106f,.116f,1.f);
    inline const FLinearColor Gold(.77f,.61f,.34f,1.f);       // UI trim, captions
    inline const FLinearColor BrightGold(1.f,.82f,.0f,1.f);   // elite, WoW quest gold
    inline const FLinearColor Parchment(.91f,.90f,.83f,1.f);  // body text
    inline const FLinearColor Muted(.50f,.57f,.59f,1.f);      // secondary text
    inline const FLinearColor Teal(.20f,.71f,.59f,1.f);
    inline const FLinearColor Red(.75f,.20f,.23f,1.f);
    inline const FLinearColor Blue(.23f,.46f,.80f,1.f);
    inline const FLinearColor Purple(.66f,.46f,.83f,1.f);
    inline const FLinearColor Hostile(.95f,.20f,.16f,1.f);    // reaction colours
    inline const FLinearColor Neutral(1.f,.86f,.18f,1.f);
    inline const FLinearColor Friendly(.16f,.92f,.24f,1.f);
    inline const FLinearColor Health(.10f,.74f,.12f,1.f);
    inline const FLinearColor Mana(.12f,.36f,.95f,1.f);
    inline const FLinearColor Energy(.95f,.78f,.18f,1.f);
    inline const FLinearColor Cast(1.f,.70f,.05f,1.f);        // interruptible cast
    inline const FLinearColor CastLocked(.40f,.42f,.48f,1.f); // uninterruptible cast
    inline const FLinearColor Silver(.76f,.80f,.86f,1.f);
    inline const FLinearColor Orange(1.f,.55f,.10f,1.f);
}

/**
 * Canvas painter in logical units. Origin/Stretch map a panel's design space
 * onto its current layout rectangle; Alpha fades everything drawn.
 */
struct CIRESTEAMSURVIVAL_API FCireUIPainter
{
    UCanvas* Canvas = nullptr;
    float Scale = 1.f;                         // logical -> pixels
    FVector2D Origin = FVector2D::ZeroVector;  // logical
    FVector2D Stretch = FVector2D(1, 1);
    float Alpha = 1.f;

    FVector2D ToScreen(float X, float Y) const { return FVector2D((Origin.X + X * Stretch.X) * Scale, (Origin.Y + Y * Stretch.Y) * Scale); }
    float K() const { return Scale * FMath::Min(Stretch.X, Stretch.Y); }
    FLinearColor Fade(FLinearColor C) const { C.A *= Alpha; return C; }

    void Rect(float X, float Y, float W, float H, FLinearColor Color) const;
    void Line(float X1, float Y1, float X2, float Y2, FLinearColor Color, float Width = 1.f) const;
    void Disc(float X, float Y, float R, FLinearColor Color, int32 Sides = 28) const;
    void Circle(float X, float Y, float R, FLinearColor Color, float Width = 1.f, int32 Sides = 36) const;
    void Tri(FVector2D A, FVector2D B, FVector2D C, FLinearColor Color) const;
    void Tex(UTexture2D* Texture, float X, float Y, float W, float H, FLinearColor Color,
        float U0 = 0.f, float V0 = 0.f, float U1 = 1.f, float V1 = 1.f, bool bAdditive = false) const;
    /** 9-slice: Corner is the logical corner size; SourceCorner the corner fraction of the texture. */
    void NineSlice(UTexture2D* Texture, float X, float Y, float W, float H, float Corner, FLinearColor Color, float SourceCorner = .25f) const;
    /** Crisp TTF text rasterized at its final pixel size, optional 1px outline and drop shadow. */
    void Text(const FString& Text, float X, float Y, float Size, FLinearColor Color, ECireFont Font = ECireFont::Auto,
        bool bOutline = false, bool bShadow = true) const;
    float TextWidth(const FString& Text, float Size, ECireFont Font = ECireFont::Auto) const;
    /** Text shortened with ".." so it fits MaxWidth (logical units). */
    FString Fit(const FString& Text, float Size, float MaxWidth, ECireFont Font = ECireFont::Auto) const;
    /** Word-wrapped text; returns the number of lines drawn. */
    int32 Wrapped(const FString& Text, float X, float Y, float Width, float Size, FLinearColor Color, int32 MaxLines,
        ECireFont Font = ECireFont::Body, float LineGap = 4.f) const;
};

/** Frame looks. */
enum class ECireFrame : uint8 { Panel, Inset, Card, Tooltip, Unit };
enum class ECireButtonState : uint8 { Normal, Hover, Pressed, Disabled, Selected };
enum class ECireSlotKind : uint8 { Normal, Passive, Ultimate, Attack };

/** Everything an action-bar style icon slot can show. */
struct FCireIconSlot
{
    FString IconId;                         // sigil / ability id
    UTexture2D* IconTexture = nullptr;      // optional painted icon (CireUIStyle::FindAbilityIcon)
    FLinearColor Tint = CireUIColors::Teal; // school/role colour of the sigil and backdrop
    ECireSlotKind Kind = ECireSlotKind::Normal;
    FString KeyLabel;                       // "1", "S-2", "R"
    float CooldownFraction = 0.f;           // remaining fraction 0..1 (radial sweep)
    float CooldownRemaining = 0.f;          // seconds (countdown number)
    int32 Charges = 0;                      // shown when > 1
    bool bEmpty = false, bHover = false, bPressed = false;
    bool bNoResource = false;               // blue tint
    bool bOutOfRange = false;               // red tint
    bool bGlow = false;                     // proc / ready: animated marching border
    float Flash = 0.f;                      // press / ready flash 0..1
};

/** Animated health/resource bar memory ("damage chunk" trail). Owned by the caller, one per bar. */
struct FCireBarTrail
{
    float Shown = -1.f, Trail = -1.f;
    double DropTime = 0.0;
};

/** A transition banner (see CireBanners for the queue). */
struct FCireBannerSpec
{
    FString Kicker, Title, Subtitle;
    FLinearColor Color = CireUIColors::BrightGold;
    float Duration = 3.4f;
};

namespace CireUIStyle
{
    struct FAssets
    {
        UFont* Fonts[4] = {};        // Body, Heading, Bold, Numbers
        float Calibration[4] = {1, 1, 1, 1};
        UTexture2D *Panel = nullptr, *Border = nullptr, *Button = nullptr, *ButtonUlt = nullptr, *ButtonPassive = nullptr;
        UTexture2D *Glow = nullptr, *Gloss = nullptr, *IconBg = nullptr, *Gem = nullptr, *Header = nullptr;
        bool bFonts = false, bTextures = false;
    };
    /** Lazily loads and roots the kit's fonts and textures (safe to call every frame). */
    CIRESTEAMSURVIVAL_API const FAssets& Assets();
    CIRESTEAMSURVIVAL_API UFont* ResolveFont(ECireFont Font, const FString& Text, float Size, int32* OutIndex = nullptr);
    /** Asset paths, for hard references (cooking) in HUD constructors. */
    CIRESTEAMSURVIVAL_API const TArray<FString>& AssetPaths();

    /** Ornate framed panel. Accent colours the top trim and gem. */
    CIRESTEAMSURVIVAL_API void Frame(const FCireUIPainter& P, float X, float Y, float W, float H,
        FLinearColor Accent = CireUIColors::Gold, ECireFrame Kind = ECireFrame::Panel);
    /** Header strip with a gold filigree underline and a caption. */
    /** Slider track + gem knob (Fraction 0..1). Caller handles the drag. */
    CIRESTEAMSURVIVAL_API void Slider(const FCireUIPainter& P, float X, float Y, float W, float Fraction, bool bEnabled = true, bool bHover = false);
    /** Small collapse chevron (pointing down when expanded, right when collapsed). */
    CIRESTEAMSURVIVAL_API void Chevron(const FCireUIPainter& P, float X, float Y, float Size, bool bCollapsed, FLinearColor Color = CireUIColors::Gold);
    CIRESTEAMSURVIVAL_API void Header(const FCireUIPainter& P, float X, float Y, float W, const FString& Caption,
        FLinearColor Color = CireUIColors::Gold, float Size = 10.f);
    CIRESTEAMSURVIVAL_API void Button(const FCireUIPainter& P, float X, float Y, float W, float H, const FString& Label,
        ECireButtonState State, FLinearColor Accent = CireUIColors::Gold, float TextSize = 11.f);
    /** Soft additive glow (hover, proc, selection). */
    CIRESTEAMSURVIVAL_API void Glow(const FCireUIPainter& P, float X, float Y, float W, float H, FLinearColor Color);
    /** Action-bar icon slot with every WoW state. Time drives the glow animation. */
    CIRESTEAMSURVIVAL_API void IconSlot(const FCireUIPainter& P, float X, float Y, float Size, const FCireIconSlot& Slot, double Time);
    /** Draws an ability sigil (procedural vector icon) for an id. */
    CIRESTEAMSURVIVAL_API void Sigil(const FCireUIPainter& P, const FString& Id, float X, float Y, float Size, FLinearColor Color);
    /** Shared ability-icon lookup: a painted texture if one exists (/Game/UI/Abilities/T_<Id>), else null. */
    CIRESTEAMSURVIVAL_API UTexture2D* FindAbilityIcon(const FString& Id);
    /** Radial clockwise cooldown sweep over a square (Remaining 0..1). */
    CIRESTEAMSURVIVAL_API void CooldownSweep(const FCireUIPainter& P, float X, float Y, float Size, float Remaining);
    /** Glossy bar with a trailing "damage chunk" and smooth fill. Text is centred when set. */
    CIRESTEAMSURVIVAL_API void Bar(const FCireUIPainter& P, float X, float Y, float W, float H, float Value, FLinearColor Color,
        FCireBarTrail* Trail, double Now, const FString& Text = FString(), float TextSize = 0.f);
    /** WoW tooltip backdrop. */
    CIRESTEAMSURVIVAL_API void TooltipFrame(const FCireUIPainter& P, float X, float Y, float W, float H,
        FLinearColor Border = FLinearColor(.55f, .58f, .64f, 1), float Opacity = .94f);
    /** Title + wrapped body tooltip; returns its height. Pass bDraw=false to measure. */
    CIRESTEAMSURVIVAL_API float Tooltip(const FCireUIPainter& P, float X, float Y, float W, const FString& Title, const FString& Body,
        float Scale = 1.f, float Opacity = .94f, bool bDraw = true);
    /** Toast card that slides in from the right and fades (Age/Life in seconds). */
    CIRESTEAMSURVIVAL_API void Toast(const FCireUIPainter& P, float X, float Y, float W, const FString& IconId, const FString& Title,
        const FString& Body, float Age, float Life, FLinearColor Accent = CireUIColors::Gold);
    /** Big animated transition banner centred horizontally at Y. */
    CIRESTEAMSURVIVAL_API void Banner(const FCireUIPainter& P, float ViewW, float Y, const FCireBannerSpec& Spec, float Age);
}
