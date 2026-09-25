#include "CireShopArt.h"
// progression-shop: see CireShopArt.h. Everything here is drawn procedurally except the scroll
// and crest textures cut from Eric's reference image (Tools/CutSkillScrolls.py).
#include "Engine/Texture2D.h"

using namespace CireUIColors;

namespace
{
UTexture2D* LoadShopTexture(const TCHAR* Name)
{
    UTexture2D* Texture = LoadObject<UTexture2D>(nullptr, *FString::Printf(TEXT("/Game/UI/Shop/Scrolls/%s.%s"), Name, Name), nullptr, LOAD_NoWarn | LOAD_Quiet);
    if (Texture) Texture->AddToRoot();
    return Texture;
}

struct FShopArtAssets
{
    UTexture2D* Scrolls[3] = {};
    UTexture2D* Crests[3] = {};
    UTexture2D* Glow = nullptr;
    bool bTried = false;
};
FShopArtAssets GArt;

const FShopArtAssets& Art()
{
    if (!GArt.bTried)
    {
        GArt.bTried = true;
        const TCHAR* ScrollNames[] = {TEXT("T_Scroll_Golden"), TEXT("T_Scroll_Plain"), TEXT("T_Scroll_Prismatic")};
        const TCHAR* CrestNames[] = {TEXT("T_Crest_Active"), TEXT("T_Crest_Passive"), TEXT("T_Crest_Ultimate")};
        for (int32 Index = 0; Index < 3; ++Index) { GArt.Scrolls[Index] = LoadShopTexture(ScrollNames[Index]); GArt.Crests[Index] = LoadShopTexture(CrestNames[Index]); }
        GArt.Glow = LoadShopTexture(TEXT("T_ShopGlow"));
    }
    return GArt;
}

float Hash(uint32 A, uint32 B)
{
    uint32 H = A * 747796405u + B * 2891336453u + 0x9E3779B9u;
    H ^= H >> 16; H *= 0x7feb352du; H ^= H >> 15; H *= 0x846ca68bu; H ^= H >> 16;
    return (H & 0xFFFFFF) / float(0x1000000);
}

// Slices of the scroll art (fractions of the texture height): top roll, parchment, bottom roll.
constexpr float TopCut = .215f, BottomCut = .795f;

void Corner(const FCireUIPainter& P, float X, float Y, float SX, float SY, FLinearColor C)
{
    // L-bracket with a doubled inner line, a curl and diamond studs (SX/SY = +-1 point inward).
    const float L = 46.f, L2 = 30.f;
    P.Line(X, Y, X + SX * L, Y, C, 1.3f);
    P.Line(X, Y, X, Y + SY * L, C, 1.3f);
    P.Line(X + SX * 5, Y + SY * 5, X + SX * L2, Y + SY * 5, C * FLinearColor(1, 1, 1, .6f), .8f);
    P.Line(X + SX * 5, Y + SY * 5, X + SX * 5, Y + SY * L2, C * FLinearColor(1, 1, 1, .6f), .8f);
    for (int32 I = 0; I < 6; ++I)
    {
        const float A = I * HALF_PI / 6.f, B = (I + 1) * HALF_PI / 6.f;
        P.Line(X + SX * (5 + FMath::Cos(A) * 9), Y + SY * (5 + FMath::Sin(A) * 9), X + SX * (5 + FMath::Cos(B) * 9), Y + SY * (5 + FMath::Sin(B) * 9), C * FLinearColor(1, 1, 1, .7f), .8f);
    }
    CireShopArt::Diamond(P, X, Y, 3.2f, C);
    CireShopArt::Diamond(P, X + SX * (L + 4), Y, 2.2f, C);
    CireShopArt::Diamond(P, X, Y + SY * (L + 4), 2.2f, C);
}
} // namespace

UTexture2D* CireShopArt::ScrollTexture(EScroll Tier) { return Art().Scrolls[static_cast<int32>(Tier)]; }
UTexture2D* CireShopArt::CrestTexture(EScroll Tier) { return Art().Crests[static_cast<int32>(Tier)]; }

void CireShopArt::Glow(const FCireUIPainter& P, float X, float Y, float W, float H, FLinearColor Color)
{
    if (UTexture2D* G = Art().Glow) P.Tex(G, X, Y, W, H, Color, 0, 0, 1, 1, true);
}

void CireShopArt::Diamond(const FCireUIPainter& P, float X, float Y, float R, FLinearColor Color, bool bFilled)
{
    const FVector2D T(X, Y - R), Rt(X + R, Y), B(X, Y + R), L(X - R, Y);
    if (bFilled) { P.Tri(T, Rt, B, Color); P.Tri(T, B, L, Color); }
    else { P.Line(T.X, T.Y, Rt.X, Rt.Y, Color, 1.f); P.Line(Rt.X, Rt.Y, B.X, B.Y, Color, 1.f); P.Line(B.X, B.Y, L.X, L.Y, Color, 1.f); P.Line(L.X, L.Y, T.X, T.Y, Color, 1.f); }
}

void CireShopArt::Rule(const FCireUIPainter& P, float X1, float X2, float Y, FLinearColor Color, bool bCentreStud)
{
    P.Line(X1 + 4, Y, X2 - 4, Y, Color, 1.f);
    Diamond(P, X1, Y, 2.4f, Color);
    Diamond(P, X2, Y, 2.4f, Color);
    if (bCentreStud) { const float C = (X1 + X2) * .5f; Diamond(P, C, Y, 4.2f, Color); Diamond(P, C, Y, 2.f, FLinearColor(.02f, .02f, .02f, 1)); }
}

void CireShopArt::Divider(const FCireUIPainter& P, float X, float Y1, float Y2, FLinearColor Color)
{
    const float M = (Y1 + Y2) * .5f;
    P.Line(X, Y1, X, M - 9, Color, .9f);
    P.Line(X, M + 9, X, Y2, Color, .9f);
    Diamond(P, X, M, 6.f, Color, false);
    Diamond(P, X, M, 2.6f, Color);
    Diamond(P, X, Y1, 2.f, Color);
    Diamond(P, X, Y2, 2.f, Color);
}

void CireShopArt::CompassStar(const FCireUIPainter& P, float X, float Y, float R, FLinearColor Color)
{
    for (int32 I = 0; I < 8; ++I)
    {
        const float A = I * PI / 4.f, Len = I % 2 == 0 ? R : R * .55f, Side = R * .16f;
        const FVector2D Tip(X + FMath::Cos(A) * Len, Y + FMath::Sin(A) * Len);
        const FVector2D L(X + FMath::Cos(A + HALF_PI) * Side, Y + FMath::Sin(A + HALF_PI) * Side), Rr(X + FMath::Cos(A - HALF_PI) * Side, Y + FMath::Sin(A - HALF_PI) * Side);
        P.Tri(Tip, L, FVector2D(X, Y), Color);
        P.Tri(Tip, Rr, FVector2D(X, Y), Color * FLinearColor(.7f, .7f, .7f, 1));
    }
    P.Circle(X, Y, R * .42f, Color * FLinearColor(1, 1, 1, .6f), .7f, 20);
}

float CireShopArt::SpacedWidth(const FCireUIPainter& P, const FString& Text, float Size, float Tracking, ECireFont Font)
{
    float W = 0;
    for (int32 I = 0; I < Text.Len(); ++I)
    {
        W += P.TextWidth(Text.Mid(I, 1), Size, Font);
        if (I + 1 < Text.Len()) W += Tracking * Size;
    }
    return W;
}

float CireShopArt::Spaced(const FCireUIPainter& P, const FString& Text, float X, float Y, float Size, float Tracking, FLinearColor Color, ECireFont Font, bool bCentered, bool bShadow)
{
    const float W = SpacedWidth(P, Text, Size, Tracking, Font);
    float CX = bCentered ? X - W * .5f : X;
    for (int32 I = 0; I < Text.Len(); ++I)
    {
        const FString Ch = Text.Mid(I, 1);
        if (Ch != TEXT(" ")) P.Text(Ch, CX, Y, Size, Color, Font, false, bShadow);
        CX += P.TextWidth(Ch, Size, Font) + Tracking * Size;
    }
    return W;
}

void CireShopArt::Panel(const FCireUIPainter& P, float X, float Y, float W, float H, float TitleWidth)
{
    // Ground: near-black with a faint warm light behind the title and darker edges.
    P.Rect(X, Y, W, H, FLinearColor(.018f, .017f, .018f, .975f));
    if (UTexture2D* Stone = CireUIStyle::Assets().Panel) P.Tex(Stone, X, Y, W, H, FLinearColor(.16f, .14f, .12f, .22f));
    Glow(P, X + W * .2f, Y - H * .25f, W * .6f, H * .7f, FLinearColor(.045f, .034f, .018f, 1));
    Glow(P, X + W * .05f, Y + H * .2f, W * .9f, H * .7f, FLinearColor(.018f, .015f, .012f, 1));
    const FLinearColor Outer = Filigree * FLinearColor(1, 1, 1, .85f), Inner = Filigree * FLinearColor(1, 1, 1, .38f);
    const float I1 = 5, I2 = 10;
    const float Top = Y + (TitleWidth > 0 ? 16.f : I1);
    // Outer border, raised into a title plate in the middle of the top edge.
    if (TitleWidth > 0)
    {
        const float CX = X + W * .5f, Half = TitleWidth * .5f;
        P.Line(X + I1, Top, CX - Half - 18, Top, Outer, 1.2f);
        P.Line(CX - Half - 18, Top, CX - Half, Y + I1, Outer, 1.2f);
        P.Line(CX - Half, Y + I1, CX + Half, Y + I1, Outer, 1.2f);
        P.Line(CX + Half, Y + I1, CX + Half + 18, Top, Outer, 1.2f);
        P.Line(CX + Half + 18, Top, X + W - I1, Top, Outer, 1.2f);
        P.Line(X + I2, Top + 5, CX - Half - 20, Top + 5, Inner, .8f);
        P.Line(CX + Half + 20, Top + 5, X + W - I2, Top + 5, Inner, .8f);
        Diamond(P, CX - Half - 18, Top, 2.2f, Outer);
        Diamond(P, CX + Half + 18, Top, 2.2f, Outer);
    }
    else
    {
        P.Line(X + I1, Top, X + W - I1, Top, Outer, 1.2f);
        P.Line(X + I2, Top + 5, X + W - I2, Top + 5, Inner, .8f);
    }
    P.Line(X + I1, Y + H - I1, X + W - I1, Y + H - I1, Outer, 1.2f);
    P.Line(X + I1, Top, X + I1, Y + H - I1, Outer, 1.2f);
    P.Line(X + W - I1, Top, X + W - I1, Y + H - I1, Outer, 1.2f);
    P.Line(X + I2, Y + H - I2, X + W - I2, Y + H - I2, Inner, .8f);
    P.Line(X + I2, Top + 5, X + I2, Y + H - I2, Inner, .8f);
    P.Line(X + W - I2, Top + 5, X + W - I2, Y + H - I2, Inner, .8f);
    Corner(P, X + I1, Top, 1, 1, Outer);
    Corner(P, X + W - I1, Top, -1, 1, Outer);
    Corner(P, X + I1, Y + H - I1, 1, -1, Outer);
    Corner(P, X + W - I1, Y + H - I1, -1, -1, Outer);
    Diamond(P, X + W * .5f, Y + H - I1, 4.f, Outer);
}

void CireShopArt::Title(const FCireUIPainter& P, float CX, float Y, const FString& Text, const FString& Subtitle, float Size)
{
    const FLinearColor TitleColor(.95f, .83f, .58f, 1.f);
    const float TW = Spaced(P, Text, CX, Y, Size, .2f, TitleColor, ECireFont::Display, true, true);
    const float RY = Y + Size * .62f;
    // Flanking rules with a double diamond near the title.
    for (int32 Side = -1; Side <= 1; Side += 2)
    {
        const float Near = CX + Side * (TW * .5f + 18), Far = CX + Side * (TW * .5f + 104);
        P.Line(FMath::Min(Near, Far), RY, FMath::Max(Near, Far), RY, Filigree * FLinearColor(1, 1, 1, .8f), 1.f);
        Diamond(P, Near, RY, 3.f, Filigree);
        Diamond(P, Near + Side * 8, RY, 1.8f, Filigree);
        Diamond(P, Far, RY, 2.f, Filigree);
    }
    if (Subtitle.IsEmpty()) return;
    const float SS = FMath::Max(7.5f, Size * .3f), SY = Y + Size * 1.28f;
    const float SW = Spaced(P, Subtitle, CX, SY, SS, .5f, Filigree * FLinearColor(.95f, .95f, .95f, 1), ECireFont::Display, true, false);
    for (int32 Side = -1; Side <= 1; Side += 2)
    {
        const float Near = CX + Side * (SW * .5f + 10), Far = CX + Side * (SW * .5f + 56);
        P.Line(FMath::Min(Near, Far), SY + SS * .55f, FMath::Max(Near, Far), SY + SS * .55f, Filigree * FLinearColor(1, 1, 1, .55f), .8f);
        Diamond(P, Far, SY + SS * .55f, 1.8f, Filigree);
    }
}

void CireShopArt::CrestRing(const FCireUIPainter& P, float CX, float CY, float R, FLinearColor Accent, float Time)
{
    P.Disc(CX, CY, R + 2.5f, FLinearColor(0, 0, 0, .55f), 32);
    P.Disc(CX, CY, R, FLinearColor(.035f, .03f, .026f, 1), 32);
    P.Circle(CX, CY, R, Filigree, 1.5f, 40);
    P.Circle(CX, CY, R - 3.f, Filigree * FLinearColor(1, 1, 1, .45f), .8f, 40);
    if (Accent.A > 0) Glow(P, CX - R * 1.6f, CY - R * 1.6f, R * 3.2f, R * 3.2f, Accent * FLinearColor(.25f, .25f, .25f, 1) * (.8f + .2f * FMath::Sin(Time * 2.f)));
    Diamond(P, CX - R - 3, CY, 2.f, Filigree);
    Diamond(P, CX + R + 3, CY, 2.f, Filigree);
}

void CireShopArt::Crest(const FCireUIPainter& P, EScroll Tier, float CX, float CY, float R, float RuleHalf)
{
    for (int32 Side = -1; Side <= 1; Side += 2)
    {
        const float Near = CX + Side * (R + 8), Far = CX + Side * (R + 8 + RuleHalf);
        P.Line(FMath::Min(Near, Far), CY, FMath::Max(Near, Far), CY, Filigree * FLinearColor(1, 1, 1, .7f), 1.f);
        Diamond(P, Far, CY, 2.2f, Filigree);
        Diamond(P, Near + Side * RuleHalf * .35f, CY, 1.6f, Filigree);
    }
    if (UTexture2D* T = CrestTexture(Tier)) P.Tex(T, CX - R * 1.08f, CY - R * 1.08f, R * 2.16f, R * 2.16f, FLinearColor::White);
    else CrestRing(P, CX, CY, R, FLinearColor(0, 0, 0, 0));
}

float CireShopArt::NaturalHeight(EScroll Tier, float W)
{
    UTexture2D* T = ScrollTexture(Tier);
    const float Aspect = T && T->GetSizeX() > 0 ? static_cast<float>(T->GetSizeY()) / T->GetSizeX() : 1.f;
    return W * Aspect;
}

CireShopArt::FRectF CireShopArt::Scroll(const FCireUIPainter& P, EScroll Tier, float X, float Y, float W, float H, float Time, uint32 Seed, float Dim, float Lift)
{
    UTexture2D* T = ScrollTexture(Tier);
    const float Natural = NaturalHeight(Tier, W);
    float TopH = Natural * TopCut, BottomH = Natural * (1.f - BottomCut);
    float MidH = H - TopH - BottomH;
    if (MidH < Natural * .1f) { const float K = H / Natural; TopH *= K; BottomH *= K; MidH = H - TopH - BottomH; }
    const float Live = 1.f - Dim * .75f;
    // Behind the scroll: tier light.
    if (Tier == EScroll::Golden)
    {
        const float Pulse = .85f + .15f * FMath::Sin(Time * 1.7f + Seed % 7);
        Glow(P, X - W * .22f, Y - H * .12f, W * 1.44f, H * 1.24f, FLinearColor(.55f, .30f, .06f, 1) * ((.34f + .3f * Lift) * Pulse * Live));
    }
    else if (Tier == EScroll::Prismatic)
    {
        const float Hue = FMath::Fmod(Time * .06f + (Seed % 13) * .07f, 1.f);
        const FLinearColor A = FLinearColor::MakeFromHSV8(static_cast<uint8>(Hue * 255), 170, 255);
        Glow(P, X - W * .26f, Y - H * .14f, W * 1.52f, H * 1.28f, FLinearColor(.34f, .12f, .55f, 1) * ((.42f + .3f * Lift) * Live));
        Glow(P, X - W * .1f, Y + H * .1f, W * 1.2f, H * .8f, A * ((.16f + .12f * Lift) * Live));
    }
    else Glow(P, X - W * .12f, Y + H * .04f, W * 1.24f, H * 1.12f, FLinearColor(.14f, .12f, .09f, 1) * ((.25f + .25f * Lift) * Live));
    // Drop shadow (lifted cards cast a softer, longer one).
    P.Rect(X + W * .16f, Y + H - 3 + Lift * 5, W * .68f, 4 + Lift * 3, FLinearColor(0, 0, 0, .35f));
    const FLinearColor Tint = FMath::Lerp(FLinearColor::White, FLinearColor(.58f, .55f, .54f, 1), Dim);
    if (T)
    {
        P.Tex(T, X, Y, W, TopH, Tint, 0, 0, 1, TopCut);
        P.Tex(T, X, Y + TopH, W, MidH, Tint, 0, TopCut, 1, BottomCut);
        P.Tex(T, X, Y + TopH + MidH, W, BottomH, Tint, 0, BottomCut, 1, 1);
    }
    else
    {
        const FLinearColor Paper = Tier == EScroll::Golden ? FLinearColor(.86f, .64f, .28f, 1) : Tier == EScroll::Plain ? FLinearColor(.84f, .76f, .6f, 1) : FLinearColor(.78f, .7f, .92f, 1);
        P.Rect(X + W * .14f, Y + TopH * .5f, W * .72f, H - TopH * .5f - BottomH * .5f, Paper * Tint);
        P.Rect(X + W * .06f, Y, W * .88f, TopH * .8f, Paper * Tint * .8f);
        P.Rect(X + W * .06f, Y + H - BottomH * .8f, W * .88f, BottomH * .8f, Paper * Tint * .8f);
    }
    FRectF Inner{X + W * .215f, Y + TopH + 1, W * .57f, MidH - 2};
    // Tier effects in front: sparks (golden), sheen + wisps (prismatic).
    if (Dim < .5f && Tier == EScroll::Golden)
        for (int32 I = 0; I < 9; ++I)
        {
            const float Phase = FMath::Fmod(Time * (.22f + Hash(Seed, I) * .2f) + Hash(Seed, I + 40), 1.f);
            const float SX = X + W * (-.06f + 1.12f * Hash(Seed, I + 80)), SY = Y + H * (1.02f - Phase * 1.1f);
            const float A = FMath::Sin(Phase * PI) * (.55f + .45f * Lift);
            P.Disc(SX + FMath::Sin(Time * 2.f + I) * 2.f, SY, .7f + Hash(Seed, I + 120) * 1.1f, FLinearColor(1.f, .78f, .35f, A), 6);
        }
    if (Dim < .5f && Tier == EScroll::Prismatic)
    {
        const float Band = FMath::Fmod(Time * .18f + (Seed % 5) * .2f, 1.4f) - .2f;
        Glow(P, Inner.X + Inner.W * (Band - .25f), Inner.Y - 6, Inner.W * .5f, Inner.H + 12, FLinearColor(.16f, .12f, .22f, 1));
        for (int32 Wisp = 0; Wisp < 3; ++Wisp)
        {
            const FLinearColor C = Wisp == 1 ? FLinearColor(.35f, .75f, 1.f, 1) : FLinearColor(.72f, .42f, 1.f, 1);
            FVector2D Prev = FVector2D::ZeroVector;
            for (int32 S = 0; S <= 22; ++S)
            {
                const float U = S / 22.f;
                const float Ang = U * PI * 1.25f + Time * (.35f + Wisp * .12f) + Wisp * 2.1f + (Seed % 11);
                const float RX = W * (.56f + .06f * FMath::Sin(Time + Wisp)), RY = H * (.52f + .05f * FMath::Cos(Time * .8f + Wisp));
                const FVector2D Pt(X + W * .5f + FMath::Cos(Ang) * RX + FMath::Sin(U * 9.f + Time * 2.f) * 3.f, Y + H * .5f + FMath::Sin(Ang) * RY);
                if (S > 0) P.Line(Prev.X, Prev.Y, Pt.X, Pt.Y, C * FLinearColor(1, 1, 1, FMath::Sin(U * PI) * (.45f + .3f * Lift)), 1.4f);
                Prev = Pt;
            }
        }
    }
    return Inner;
}

void CireShopArt::WaxSeal(const FCireUIPainter& P, float CX, float CY, float R, float Stamp, const FString& Glyph)
{
    if (Stamp <= 0.f || Stamp >= 1.f) return;
    const float Drop = FMath::Clamp(Stamp / .12f, 0.f, 1.f);
    const float Fade = Stamp > .78f ? 1.f - (Stamp - .78f) / .22f : 1.f;
    const float Scale = FMath::Lerp(1.9f, 1.f, 1.f - FMath::Pow(1.f - Drop, 3.f));
    const float Alpha = Drop * Fade;
    const float Rr = R * Scale;
    FCireUIPainter Q = P; Q.Alpha *= Alpha;
    // Impact flash and ring.
    if (Stamp > .1f && Stamp < .45f)
    {
        const float K = (Stamp - .1f) / .35f;
        Glow(Q, CX - R * 3.2f, CY - R * 3.2f, R * 6.4f, R * 6.4f, FLinearColor(1.f, .6f, .3f, 1) * (1.f - K) * .9f);
        Q.Circle(CX, CY, R * (1.1f + K * 1.6f), FLinearColor(1.f, .75f, .45f, 1.f - K), 2.f, 40);
    }
    const FLinearColor Wax(.58f, .07f, .06f, 1), Deep(.40f, .04f, .03f, 1), Rim(.86f, .32f, .22f, .9f);
    Q.Disc(CX + 2, CY + 3, Rr * 1.02f, FLinearColor(0, 0, 0, .35f), 28);
    for (int32 I = 0; I < 13; ++I)
    {
        const float A = I * 2 * PI / 13 + .3f;
        Q.Disc(CX + FMath::Cos(A) * Rr * .86f, CY + FMath::Sin(A) * Rr * .86f, Rr * (.2f + .06f * FMath::Sin(I * 2.7f)), Wax, 10);
    }
    Q.Disc(CX, CY, Rr * .9f, Wax, 28);
    Q.Disc(CX, CY, Rr * .68f, Deep, 28);
    Q.Circle(CX, CY, Rr * .7f, Rim, 1.4f, 32);
    Q.Disc(CX - Rr * .32f, CY - Rr * .36f, Rr * .2f, FLinearColor(1.f, .7f, .6f, .22f), 12);
    if (!Glyph.IsEmpty())
    {
        const float S = Rr * .78f;
        Q.Text(Glyph, CX - Q.TextWidth(Glyph, S, ECireFont::Display) * .5f, CY - S * .62f, S, FLinearColor(.92f, .45f, .33f, 1), ECireFont::Display, false, false);
    }
}
