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
// Display: Cinzel wide serif caps for ornate titles (Skill Shop / Armory); falls back to Heading.
enum class ECireFont : uint8 { Auto, Body, Heading, Bold, Numbers, Display };

/**
 * Palette. The first block is themed: CireUITheme::SetActive writes the active theme's
 * colours here (Content/Data/UIThemes.json), so every screen using these names follows the
 * selected UI theme. The values below are the pre-theme defaults. The second block is
 * semantic (reaction, resource, school colours) and never changes with the theme.
 */
namespace CireUIColors
{
    inline FLinearColor Ink(.014f,.020f,.026f,.95f);
    inline FLinearColor Card(.034f,.046f,.055f,.98f);
    inline FLinearColor Hover(.075f,.106f,.116f,1.f);
    inline FLinearColor Gold(.77f,.61f,.34f,1.f);       // UI trim, captions (theme trim)
    inline FLinearColor BrightGold(1.f,.82f,.0f,1.f);   // elite, WoW quest gold (theme bright trim)
    inline FLinearColor Parchment(.91f,.90f,.83f,1.f);  // body text
    inline FLinearColor Muted(.50f,.57f,.59f,1.f);      // secondary text
    inline FLinearColor ThemeAccent(.77f,.61f,.34f,1.f); // selection / highlight accent
    inline FLinearColor ThemeGlow(1.f,.85f,.5f,1.f);     // hover, proc and selection glow
    inline FLinearColor TooltipBg(.02f,.025f,.06f,1.f);
    inline FLinearColor TooltipBorder(.55f,.58f,.64f,1.f);
    inline FLinearColor BarBack(0.f,0.f,0.f,.85f);       // empty part of bars
    inline FLinearColor TitleText(1.f,.86f,.3f,1.f);     // tooltip / window titles
    inline FLinearColor ThemeFiligree(.83f,.66f,.36f,1.f); // ornate shop framing (CireShopArt::Filigree)
    inline FLinearColor PanelTint(1.f,1.f,1.f,1.f);      // multiplies the panel fill
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
 * readability: every canvas text size goes through ReadableSize: a global boost on the design size
 * plus a floor, so small labels stay at or above ~12.75 px and body text (9+) at or above ~15.5 px
 * at 1080p (100% interface scale). Both scale with the resolution and the Options UI-scale slider.
 * Painter::Text / TextWidth / Fit / Wrapped apply it; layouts that step lines use ReadableSize too.
 */
namespace CireUIStyle
{
    inline constexpr float TextBoost = 1.15f;
    inline constexpr float MinTextSize = 8.5f;
    inline float ReadableSize(float Size) { return FMath::Max(Size * TextBoost, MinTextSize); }
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
    /** Texture clipped to a disc (triangle fan): round portraits without square corners. */
    void TexDisc(UTexture2D* Texture, float CX, float CY, float R, FLinearColor Color, float U0 = 0, float V0 = 0, float U1 = 1, float V1 = 1, int32 Sides = 40) const;
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

/** readability: one row of a rich (WoW-style) tooltip. */
struct FCireTooltipRow
{
    enum class EKind : uint8 { Text, Pair, Header, Stat, Divider, Bar, Gap };
    EKind Kind = EKind::Text;
    FString Left, Right;
    FLinearColor LeftColor = FLinearColor(.90f, .90f, .87f, 1.f), RightColor = FLinearColor(.90f, .90f, .87f, 1.f);
    float Size = 0.f;      // design size before the tooltip scale (0 = body size)
    float Fraction = 0.f;  // Bar fill
    ECireFont Font = ECireFont::Body;
};

/**
 * readability: a WoW-style tooltip: icon (painted art, sigil or champion portrait), name in its
 * rarity / role / reaction colour, a tag ("Ultimate", "Legendary") and a type line, then rows split
 * by ornamental dividers: text, left/right pairs (cost | range), section headers, symbol stat lines
 * ("DEF +20%"), a health bar. Drawn by CireUIStyle::RichTooltip in the theme's tooltip frame.
 */
struct CIRESTEAMSURVIVAL_API FCireTooltipSpec
{
    UTexture2D* Icon = nullptr;
    FString Sigil;                                  // procedural sigil when there is no painted icon
    FLinearColor IconTint = CireUIColors::Gold;
    ECireSlotKind IconKind = ECireSlotKind::Normal; // icon frame (ultimate / passive shapes)
    FString PortraitId;                             // champion portrait (round) instead of an icon
    FString Title;
    FLinearColor TitleColor = FLinearColor(1.f, .86f, .3f, 1.f);
    FString Tag;
    FLinearColor TagColor = FLinearColor(.72f, .74f, .78f, 1.f);
    FString Subtitle;
    FLinearColor SubtitleColor = FLinearColor(.78f, .80f, .82f, 1.f);
    FLinearColor Accent = FLinearColor(.55f, .58f, .64f, 1.f); // frame + title band tint
    TArray<FCireTooltipRow> Rows;
    FString Footer;

    FCireTooltipSpec& Text(const FString& Text, FLinearColor Color = FLinearColor(.90f, .90f, .87f, 1.f), float Size = 0.f, ECireFont Font = ECireFont::Body);
    FCireTooltipSpec& Pair(const FString& Left, const FString& Right, FLinearColor LeftColor = FLinearColor::White, FLinearColor RightColor = FLinearColor::White);
    FCireTooltipSpec& Header(const FString& Text, FLinearColor Color);
    FCireTooltipSpec& Stat(const FString& Text, FLinearColor Color = FLinearColor(.42f, 1.f, .48f, 1.f));
    FCireTooltipSpec& Divider();
    FCireTooltipSpec& Bar(float Fraction, const FString& Label, FLinearColor Color);
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
        UFont* Fonts[5] = {};        // Body, Heading, Bold, Numbers, Display (optional)
        float Calibration[5] = {1, 1, 1, 1, 1};
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

    // ui-themes: themed pieces (CireUITheme). Each falls back to procedural drawing without theme art.
    /** Ornate portrait ring around a circle of radius R (drawn over the portrait). */
    CIRESTEAMSURVIVAL_API void PortraitRing(const FCireUIPainter& P, float CX, float CY, float R, FLinearColor Tint = FLinearColor::White);
    /** Painted champion portrait (/Game/UI/Draft/Portraits/T_Portrait_<id>), cached; null when missing. */
    /** hud-art: how far (logical units) the active theme's panel corner ornament reaches into a Panel/Unit
     *  frame of this size; captions in a corner start past it (0 without theme art). */
    CIRESTEAMSURVIVAL_API float FrameCornerClear(float W, float H);
    CIRESTEAMSURVIVAL_API UTexture2D* ChampionPortrait(const FString& ProfileId);
    /** hud-art: a champion portrait inside the theme's portrait ring (face crop, round, crisp); false = no art. */
    CIRESTEAMSURVIVAL_API bool PortraitFace(const FCireUIPainter& P, const FString& ProfileId, float CX, float CY, float R, bool bDead = false);
    /** hud-art: small role badge (the theme's ring as a medallion with the role emblem) on a portrait. */
    CIRESTEAMSURVIVAL_API void RoleBadge(const FCireUIPainter& P, float CX, float CY, float R, const FString& SigilId, FLinearColor Tint, const FString& PaintedIcon = FString());
    /** Small round medallion (level / tier badge) with centred text. */
    CIRESTEAMSURVIVAL_API void Medallion(const FCireUIPainter& P, float CX, float CY, float R, const FString& Text, FLinearColor TextColor);
    /** Minimap border (drawn over the map area). */
    CIRESTEAMSURVIVAL_API void MinimapFrame(const FCireUIPainter& P, float X, float Y, float W, float H);
    /** Ornamental horizontal divider. */
    CIRESTEAMSURVIVAL_API void Divider(const FCireUIPainter& P, float X, float Y, float W, FLinearColor Tint = FLinearColor::White);
    /** Top-centre crest ornament centred at CX, Y (a panel's top edge). */
    CIRESTEAMSURVIVAL_API void Ornament(const FCireUIPainter& P, float CX, float Y, float Height, FLinearColor Tint = FLinearColor::White);
    /** Cast bar: themed frame, fill, spell name (left) and time (right). */
    CIRESTEAMSURVIVAL_API void CastBar(const FCireUIPainter& P, float X, float Y, float W, float H, float Progress, FLinearColor Color,
        const FString& Name, const FString& Time, float TextSize = 0.f);
    /** Frame around an existing bar (bars drawn with Bar() get it automatically when tall enough). */
    CIRESTEAMSURVIVAL_API void BarFrame(const FCireUIPainter& P, float X, float Y, float W, float H);
    /** Rounded (capsule) shape clipped to Fraction of its width, 3-sliced so the ends stay round.
     *  Layer 0 = body with a soft vertical gradient, 1 = glossy sheen. False when the texture is missing. */
    CIRESTEAMSURVIVAL_API bool Capsule(const FCireUIPainter& P, float X, float Y, float W, float H, float Fraction, FLinearColor Color, int32 Layer = 0);
    /** WoW-style rounded bar: faint theme trim, thin dark border, dark back, pale trailing chunk, rounded
     *  fill that follows the ends, sheen. Trail < 0 = no chunk. */
    CIRESTEAMSURVIVAL_API void RoundBar(const FCireUIPainter& P, float X, float Y, float W, float H, float Fraction, FLinearColor Color, float Trail = -1.f);
    /** True when the active theme has art loaded (painters use it). */
    CIRESTEAMSURVIVAL_API bool HasThemeArt();

    // readability: rich tooltips, symbol stat lines and bevelled cards.
    /** Draws (or measures, bDraw=false) a rich tooltip at X,Y of width W; returns its height. Scale multiplies
     *  every size; MaxHeight (0 = none) drops trailing lines to fit; OutLines = text lines drawn. */
    CIRESTEAMSURVIVAL_API float RichTooltip(const FCireUIPainter& P, float X, float Y, float W, const FCireTooltipSpec& Spec,
        float Scale = 1.f, float Opacity = .94f, bool bDraw = true, float MaxHeight = 0.f, int32* OutLines = nullptr);
    /** A plain title + body tooltip as a rich spec: stat lines ("+20 Attack", "DEF +20%") become coloured symbol
     *  rows, "UNIQUE PASSIVE" / "ACTIVE" / "USE" paragraphs get section headers, blank lines dividers. */
    CIRESTEAMSURVIVAL_API FCireTooltipSpec TooltipFromText(const FString& Title, const FString& Body);
    CIRESTEAMSURVIVAL_API bool IsStatLine(const FString& Line);
    /** Green for gains ("+"), red for losses ("-N"), gold otherwise. */
    CIRESTEAMSURVIVAL_API FLinearColor StatColor(const FString& Line);
    /** Filled rectangle with chamfered (bevelled) corners of size C, and its outline. */
    CIRESTEAMSURVIVAL_API void Bevel(const FCireUIPainter& P, float X, float Y, float W, float H, float C, FLinearColor Color);
    CIRESTEAMSURVIVAL_API void BevelOutline(const FCireUIPainter& P, float X, float Y, float W, float H, float C, FLinearColor Color, float Width = 1.f);
    /** Bevelled item card: rarity glow, rarity metal rim, dark body washed in the rarity colour. */
    CIRESTEAMSURVIVAL_API void BevelCard(const FCireUIPainter& P, float X, float Y, float W, float H, FLinearColor Rarity,
        bool bHover = false, bool bSelected = false, bool bDim = false);
}
