// ability-vfx: themed rune telegraphs. Each damage school gets its own glyph, edge treatment and animated
// motif (physical etched steel + dust, fire ember glyphs with flame licks, frost crystals with an icy rim,
// cracked earth runes, flowing tide waves, radiant holy script, thorny shadow sigils, void starfield,
// bubbling poison, nature leaf knots, storm bolts, arcane geometry). Healing is its own unmistakable set
// (green/gold radiant crosses). Everything is procedural ground geometry drawn inside the true boundary.
#include "CireAbilityVFX.h"
#include "CireSpellMesh.h"
#include "CireAreaEffects.h"

using namespace CireSpellMesh;
using ERuneSet = CireAbilityVFX::ERuneSet;

namespace
{
constexpr float Tau = 2 * PI;
FVector2D Rot(FVector2D P, float A) { const float C = FMath::Cos(A), S = FMath::Sin(A); return FVector2D(P.X * C - P.Y * S, P.X * S + P.Y * C); }
float Hash(int32 N) { return Fract(FMath::Sin(N * 12.9898f) * 43758.5453f); }

struct FEdgeSample { FVector2D P, T, N; float U; float Corner = 1e9f; }; // Corner: distance to the nearest sharp vertex
// Evenly spaced samples along a closed boundary with inward normals (any winding, concave ok).
TArray<FEdgeSample> SampleEdge(const TArray<FVector2D>& Loop, float Spacing, int32 MaxCount)
{
    TArray<FEdgeSample> Out; const int32 N = Loop.Num(); if (N < 3) return Out;
    double Area = 0, Length = 0;
    for (int32 J = 0; J < N; ++J) { Area += Loop[J].X * Loop[(J + 1) % N].Y - Loop[(J + 1) % N].X * Loop[J].Y; Length += FVector2D::Distance(Loop[J], Loop[(J + 1) % N]); }
    const float Sign = Area >= 0 ? 1.f : -1.f; // CCW: inward normal is left of the tangent
    const int32 Count = FMath::Clamp(FMath::RoundToInt(Length / FMath::Max(8.f, Spacing)), 3, MaxCount);
    const double Step = Length / Count; double Walked = 0; int32 Edge = 0; double EdgeStart = 0;
    for (int32 K = 0; K < Count; ++K)
    {
        const double Target = (K + .5) * Step;
        while (Edge < N - 1 && EdgeStart + FVector2D::Distance(Loop[Edge], Loop[(Edge + 1) % N]) < Target) { EdgeStart += FVector2D::Distance(Loop[Edge], Loop[(Edge + 1) % N]); ++Edge; }
        const FVector2D A = Loop[Edge], B = Loop[(Edge + 1) % N]; const double L = FMath::Max(1e-3, FVector2D::Distance(A, B));
        const FVector2D T = (B - A) / L; const FVector2D Normal = FVector2D(-T.Y, T.X) * Sign;
        Out.Add({A + T * static_cast<float>(FMath::Clamp(Target - EdgeStart, 0.0, L)), T, Normal, static_cast<float>(K) / Count});
    }
    // Sharp vertices (turn > 25 degrees): cone apex/arc ends, square and polygon corners.
    TArray<FVector2D> Sharp;
    for (int32 J = 0; J < N; ++J)
    {
        const FVector2D A = (Loop[J] - Loop[(J + N - 1) % N]).GetSafeNormal(), B = (Loop[(J + 1) % N] - Loop[J]).GetSafeNormal();
        if (FVector2D::DotProduct(A, B) < FMath::Cos(FMath::DegreesToRadians(25.f))) Sharp.Add(Loop[J]);
    }
    for (FEdgeSample& E : Out) for (const FVector2D& V : Sharp) E.Corner = FMath::Min(E.Corner, static_cast<float>(FVector2D::Distance(E.P, V)));
    (void)Walked; return Out;
}
float SizeOf(const FCireAreaSpec& S, const TArray<FVector2D>& B)
{
    switch (S.Shape)
    {
    case ECireAreaShape::Circle: case ECireAreaShape::Cone: return S.Radius;
    case ECireAreaShape::Line: return FMath::Min(S.Width, S.Length);
    case ECireAreaShape::Square: return S.Width * .5f;
    default: { const FBox2D Box(B); return FMath::Min(Box.GetSize().X, Box.GetSize().Y) * .5f; }
    }
}
} // namespace

CireAbilityVFX::ERuneSet CireAbilityVFX::RuneSetForSchool(ECireSchool School)
{
    switch (School)
    {
    case ECireSchool::Fire: return ERuneSet::Fire;
    case ECireSchool::Frost: return ERuneSet::Frost;
    case ECireSchool::Earth: return ERuneSet::Earth;
    case ECireSchool::Tide: return ERuneSet::Tide;
    case ECireSchool::Holy: return ERuneSet::Holy;
    case ECireSchool::Shadow: return ERuneSet::Shadow;
    case ECireSchool::Void: return ERuneSet::Void;
    case ECireSchool::Poison: return ERuneSet::Poison;
    case ECireSchool::Nature: return ERuneSet::Nature;
    case ECireSchool::Storm: return ERuneSet::Storm;
    case ECireSchool::Arcane: return ERuneSet::Arcane;
    case ECireSchool::Blood: return ERuneSet::Blood;
    case ECireSchool::Spirit: return ERuneSet::Spirit;
    case ECireSchool::Life: return ERuneSet::Heal;
    default: return ERuneSet::Physical;
    }
}
CireAbilityVFX::ERuneSet CireAbilityVFX::RuneSetFor(const FCireHitShape& Shape)
{
    return Shape.bHeal ? ERuneSet::Heal : RuneSetForSchool(Shape.School);
}
FLinearColor CireAbilityVFX::RuneColor(ERuneSet Set)
{
    // Emissive (unlit) colours tuned to read on dark earth and bright stone alike.
    static const FLinearColor Colors[] = {
        // Kept near 1.0-1.5 at the brightest channel: higher emissive values bloom toward white and lose the hue.
        {.95f, .78f, .55f, 1},   // physical: dusty steel / earth tan, close to the ground colour
        {1.3f, .26f, .02f, 1},   // fire: ember orange-red (stays orange after bloom)
        {.5f, .95f, 1.5f, 1},    // frost: pale ice blue
        {1.1f, .55f, .18f, 1},   // earth: ochre stone
        {.06f, .75f, 1.3f, 1},   // tide: sea blue-cyan
        {1.5f, 1.15f, .3f, 1},   // holy: radiant gold
        {.75f, .15f, 1.2f, 1},   // shadow: violet
        {.85f, .2f, 1.5f, 1},    // void: deep magenta-violet
        {.5f, 1.3f, .05f, 1},    // poison: acid green
        {.2f, 1.1f, .25f, 1},    // nature: leaf green
        {.55f, .8f, 1.6f, 1},    // storm: electric blue-white
        {.6f, .4f, 1.6f, 1},     // arcane: blue-violet
        {1.35f, .06f, .1f, 1},   // blood: crimson
        {.12f, 1.15f, 1.f, 1},   // spirit: teal
        {.25f, 1.35f, .35f, 1}}; // heal: saturated green (gold accents are added by the glyph)
    static_assert(UE_ARRAY_COUNT(Colors) == static_cast<int32>(ERuneSet::Count), "rune colours");
    return Colors[FMath::Clamp(static_cast<int32>(Set), 0, static_cast<int32>(ERuneSet::Count) - 1)];
}
FString CireAbilityVFX::RuneSetName(ERuneSet Set)
{
    static const TCHAR* Names[] = {TEXT("physical"), TEXT("fire"), TEXT("frost"), TEXT("earth"), TEXT("tide"), TEXT("holy"), TEXT("shadow"), TEXT("void"),
        TEXT("poison"), TEXT("nature"), TEXT("storm"), TEXT("arcane"), TEXT("blood"), TEXT("spirit"), TEXT("heal")};
    const int32 I = static_cast<int32>(Set); return I >= 0 && I < UE_ARRAY_COUNT(Names) ? Names[I] : TEXT("none");
}
CireAbilityVFX::FRuneTheme CireAbilityVFX::ThemeFor(const FCireHitShape& Shape)
{
    FRuneTheme T; T.Set = RuneSetFor(Shape); T.Glyph = RuneColor(T.Set);
    // Edge motifs keep the school hue; only the cold/storm/holy sets carry a white-hot rim.
    const bool bBright = T.Set == ERuneSet::Frost || T.Set == ERuneSet::Storm || T.Set == ERuneSet::Holy;
    T.Edge = bBright ? FMath::Lerp(T.Glyph, FLinearColor(1.8f, 1.8f, 1.8f, 1), .3f) : T.Glyph * 1.1f; T.Edge.A = 1;
    if (T.Set == ERuneSet::Heal) T.Edge = FLinearColor(1.55f, 1.15f, .25f, 1); // gold rim on green crosses
    T.bSharp = !Shape.bHeal && !Shape.bBuff;
    return T;
}
CireAbilityVFX::FStyle CireAbilityVFX::ThemedStyle(ETone Tone, const FCireHitShape& Shape)
{
    FRuneTheme Theme = ThemeFor(Shape);
    FStyle S = StyleFor(Tone == ETone::AimValid ? ETone::Friendly : Tone, Theme.Glyph);
    if (Tone == ETone::AimInvalid)
    {
        S = StyleFor(ETone::AimInvalid, Theme.Glyph);
        Theme.Glyph = FMath::Lerp(Theme.Glyph, FLinearColor(1.8f, .25f, .2f, 1), .55f); // can't cast here: runes dim toward red
    }
    else if (Tone == ETone::AimValid) { S.Edge = WithAlpha(FMath::Lerp(Theme.Edge, FLinearColor(2.f, 2.f, 2.f, 1), .1f), .95f); S.Fill = WithAlpha(Theme.Glyph * .55f, .2f); } // rim and fill keep the school hue
    else if (Tone == ETone::Hostile)
    {
        // Amber stays the urgency colour; the school must still read inside it: amber-adjacent schools shift
        // hue (fire -> deep red, earth -> dark umber, holy -> white-gold) and every glyph gets a heavy backing.
        if (Theme.Set == ERuneSet::Fire) Theme.Glyph = FLinearColor(1.6f, .08f, .02f, 1);
        else if (Theme.Set == ERuneSet::Earth || Theme.Set == ERuneSet::Physical) Theme.Glyph = FLinearColor(.5f, .22f, .05f, 1);
        else if (Theme.Set == ERuneSet::Holy) Theme.Glyph = FLinearColor(1.7f, 1.6f, 1.2f, 1);
        Theme.Edge = Theme.Glyph; Theme.Underlay = .7f;
    }
    S.Runes = Theme; S.bRunes = true;
    return S;
}

void CireAbilityVFX::PaintGlyph(FCireGroundMesh& G, ERuneSet Set, FVector2D C, float Size, float Angle, FLinearColor Col, float Time, int32 Seed, float Lift, float Underlay)
{
    const float S = FMath::Max(6.f, Size);
    const float W = FMath::Clamp(S * .095f, 2.2f, 10.f), F = W * .9f; // gameplay-distance legibility: thick strokes
    // Dark underlay: lifts the glyph off bright or busy ground at gameplay distance.
    if (Underlay > 0) { FLinearColor Dark = Col * .06f; Dark.A = FMath::Min(.8f, Underlay * 1.3f) * Col.A; G.Disc(C, S * 1.2f, Dark, WithAlpha(Dark, Dark.A * .15f), 14, Lift - .2f); }
    auto P = [&](float X, float Y) { return C + Rot(FVector2D(X, Y) * S, Angle); };
    auto Ln = [&](float X0, float Y0, float X1, float Y1, float Scale = 1.f) { G.Stroke(P(X0, Y0), P(X1, Y1), W * Scale, F * Scale, Col, Lift); };
    auto Arc = [&](float Cx, float Cy, float R, float A0, float A1, int32 Steps, float Scale = 1.f)
    {
        for (int32 J = 0; J < Steps; ++J)
        {
            const float U0 = A0 + (A1 - A0) * J / Steps, U1 = A0 + (A1 - A0) * (J + 1) / Steps;
            Ln(Cx + FMath::Cos(U0) * R, Cy + FMath::Sin(U0) * R, Cx + FMath::Cos(U1) * R, Cy + FMath::Sin(U1) * R, Scale);
        }
    };
    auto Dot = [&](float X, float Y, float R, FLinearColor Cc) { G.Disc(P(X, Y), R * S, Cc, WithAlpha(Cc, Cc.A * .4f), 8, Lift + .1f); };
    switch (Set)
    {
    case ERuneSet::Physical: // etched crossed blades inside a notched circle
        Arc(0, 0, .95f, 0, Tau, 10, .7f);
        Ln(-.62f, -.62f, .62f, .62f); Ln(-.62f, .62f, .62f, -.62f);
        Ln(-.5f, -.2f, -.2f, -.5f, .8f); Ln(.2f, -.5f, .5f, -.2f, .8f);
        break;
    case ERuneSet::Fire: // flame glyph with an inner tongue, flickering height
    {
        const float H = .9f + .12f * FMath::Sin(Time * 9.f + Seed);
        Ln(0, -.85f, -.52f, -.25f); Ln(-.52f, -.25f, -.35f, .35f); Ln(-.35f, .35f, 0, H);
        Ln(0, H, .22f, .38f); Ln(.22f, .38f, .52f, -.2f); Ln(.52f, -.2f, 0, -.85f);
        Ln(0, -.5f, -.16f, .02f, .8f); Ln(-.16f, .02f, .05f, .45f * H, .8f);
        break;
    }
    case ERuneSet::Frost: // six-armed crystal with branch ticks
        for (int32 K = 0; K < 6; ++K)
        {
            const float A = K * Tau / 6; const FVector2D D(FMath::Cos(A), FMath::Sin(A)), N(-D.Y, D.X);
            Ln(0, 0, D.X * .95f, D.Y * .95f);
            const FVector2D B = D * .55f; Ln(B.X, B.Y, B.X + (D.X + N.X) * .22f, B.Y + (D.Y + N.Y) * .22f, .75f); Ln(B.X, B.Y, B.X + (D.X - N.X) * .22f, B.Y + (D.Y - N.Y) * .22f, .75f);
        }
        break;
    case ERuneSet::Earth: // angular stone rune split by a crack
        Ln(0, .95f, .72f, 0); Ln(.72f, 0, 0, -.95f); Ln(0, -.95f, -.72f, 0); Ln(-.72f, 0, 0, .95f);
        Ln(-.28f, .55f, .1f, .12f, .8f); Ln(.1f, .12f, -.12f, -.18f, .8f); Ln(-.12f, -.18f, .24f, -.58f, .8f);
        break;
    case ERuneSet::Tide: // two flowing waves drifting sideways
        for (int32 Row = 0; Row < 2; ++Row)
            for (int32 K = 0; K < 8; ++K)
            {
                const float X0 = -1 + 2.f * K / 8, X1 = -1 + 2.f * (K + 1) / 8, Y = Row ? -.3f : .3f, Ph = Time * 3.f + Row * 1.6f + Seed;
                Ln(X0, Y + FMath::Sin(X0 * 3.4f + Ph) * .24f, X1, Y + FMath::Sin(X1 * 3.4f + Ph) * .24f);
            }
        break;
    case ERuneSet::Holy: // radiant script: small ring with long/short rays and a central cross
        Arc(0, 0, .32f, 0, Tau, 10, .8f);
        for (int32 K = 0; K < 8; ++K) { const float A = K * Tau / 8 + Time * .4f; Ln(FMath::Cos(A) * .45f, FMath::Sin(A) * .45f, FMath::Cos(A) * (K % 2 ? .75f : .98f), FMath::Sin(A) * (K % 2 ? .75f : .98f), .8f); }
        Ln(0, -.18f, 0, .18f, .7f); Ln(-.18f, 0, .18f, 0, .7f);
        break;
    case ERuneSet::Shadow: // thorny sigil: stem, hooked thorns, crescent crown
        Ln(0, -.95f, 0, .7f);
        for (int32 K = 0; K < 3; ++K) { const float Y = -.6f + K * .45f, Sd = K % 2 ? 1.f : -1.f; Ln(0, Y, Sd * .42f, Y + .2f, .8f); Ln(Sd * .42f, Y + .2f, Sd * .3f, Y + .36f, .6f); }
        Arc(0, .6f, .38f, PI * .15f, PI * .85f, 6, .85f);
        break;
    case ERuneSet::Void: // rift star with orbiting specks
    {
        Ln(0, -.95f, 0, .95f, .7f); Ln(-.95f, 0, .95f, 0, .7f);
        Ln(0, .38f, .38f, 0); Ln(.38f, 0, 0, -.38f); Ln(0, -.38f, -.38f, 0); Ln(-.38f, 0, 0, .38f);
        for (int32 K = 0; K < 3; ++K) { const float A = Time * 1.3f + K * Tau / 3 + Seed; const float Tw = .5f + .5f * FMath::Sin(Time * 6 + K * 2 + Seed); Dot(FMath::Cos(A) * .7f, FMath::Sin(A) * .7f, .07f, WithAlpha(Col, Col.A * Tw)); }
        break;
    }
    case ERuneSet::Poison: // bubbling sigil: three bubbles that swell and pop
        for (int32 K = 0; K < 3; ++K)
        {
            const FVector2D Cc = K == 0 ? FVector2D(-.3f, -.3f) : K == 1 ? FVector2D(.34f, .06f) : FVector2D(-.08f, .52f);
            const float Ph = Fract(Time * .7f + K * .33f + Seed * .17f); const float R = (K == 0 ? .38f : K == 1 ? .3f : .22f) * (.6f + .4f * Ph);
            Arc(Cc.X, Cc.Y, R, 0, Tau, 9, .75f * (1 - Ph * .5f));
        }
        break;
    case ERuneSet::Nature: // leaf knot: vesica, midrib, veins
        Arc(-.62f, 0, 1.05f, -.9f, .9f, 7); Arc(.62f, 0, 1.05f, PI - .9f, PI + .9f, 7);
        Ln(0, -.95f, 0, .95f, .75f);
        Ln(0, -.25f, .3f, .05f, .6f); Ln(0, -.25f, -.3f, .05f, .6f); Ln(0, .25f, .25f, .5f, .6f); Ln(0, .25f, -.25f, .5f, .6f);
        break;
    case ERuneSet::Storm: // lightning glyph with arrowed tip
        Ln(-.25f, .95f, .22f, .12f, 1.1f); Ln(.22f, .12f, -.18f, .08f, 1.1f); Ln(-.18f, .08f, .3f, -.95f, 1.1f);
        Ln(.3f, -.95f, .02f, -.72f, .7f); Ln(.3f, -.95f, .36f, -.62f, .7f);
        break;
    case ERuneSet::Arcane: // geometric: circle, triangle, counter-triangle
        Arc(0, 0, .95f, 0, Tau, 12, .7f);
        for (int32 K = 0; K < 3; ++K)
        {
            const float A0 = PI * .5f + K * Tau / 3 + Time * .5f, A1 = A0 + Tau / 3;
            Ln(FMath::Cos(A0) * .9f, FMath::Sin(A0) * .9f, FMath::Cos(A1) * .9f, FMath::Sin(A1) * .9f);
            const float B0 = -PI * .5f + K * Tau / 3 - Time * .5f, B1 = B0 + Tau / 3;
            Ln(FMath::Cos(B0) * .45f, FMath::Sin(B0) * .45f, FMath::Cos(B1) * .45f, FMath::Sin(B1) * .45f, .7f);
        }
        break;
    case ERuneSet::Blood: // blood drop with a cut
        for (int32 K = 0; K < 10; ++K)
        {
            auto Q = [](float A) { const float Pinch = FMath::Max(0.f, FMath::Sin(A)); return FVector2D(FMath::Cos(A) * .6f * (1 - Pinch * .75f), FMath::Sin(A) * .6f + Pinch * .4f - .15f); };
            const FVector2D A = Q(-PI * .5f + Tau * K / 10), B = Q(-PI * .5f + Tau * (K + 1) / 10); Ln(A.X, A.Y, B.X, B.Y);
        }
        Ln(-.3f, -.1f, .3f, .15f, .7f);
        break;
    case ERuneSet::Spirit: // spiral wisp
        for (int32 K = 0; K < 12; ++K) { const float U0 = K / 12.f, U1 = (K + 1) / 12.f; const float A0 = U0 * Tau * 1.4f + Time, A1 = U1 * Tau * 1.4f + Time;
            Ln(FMath::Cos(A0) * .9f * U0, FMath::Sin(A0) * .9f * U0, FMath::Cos(A1) * .9f * U1, FMath::Sin(A1) * .9f * U1); }
        break;
    case ERuneSet::Heal: default: // radiant "+": thick green cross, gold outline, soft rays
    {
        const FLinearColor Gold(1.55f, 1.15f, .25f, Col.A);
        Ln(0, -.72f, 0, .72f, 2.2f); Ln(-.72f, 0, .72f, 0, 2.2f);
        const FVector2D O[] = {{-.25f, .85f}, {.25f, .85f}, {.25f, .25f}, {.85f, .25f}, {.85f, -.25f}, {.25f, -.25f}, {.25f, -.85f}, {-.25f, -.85f}, {-.25f, -.25f}, {-.85f, -.25f}, {-.85f, .25f}, {-.25f, .25f}};
        for (int32 K = 0; K < 12; ++K) G.Stroke(P(O[K].X, O[K].Y), P(O[(K + 1) % 12].X, O[(K + 1) % 12].Y), W * .55f, F * .6f, Gold, Lift + .2f);
        break;
    }
    }
}

namespace
{
// Edge treatment at one boundary sample: every motif points or lies INWARD from the true edge.
void EdgeMotif(FCireGroundMesh& G, ERuneSet Set, const FEdgeSample& E, float Size, float Time, int32 K, FLinearColor Col, bool bSharp)
{
    const FVector2D P = E.P + E.N * 6.f, T = E.T, N = E.N; // inset so diagonal stroke caps never cross the edge
    const float W = FMath::Clamp(Size * .075f, 2.f, 7.f);
    auto Ln = [&](FVector2D A, FVector2D B, float Scale = 1.f, FLinearColor C = FLinearColor::Transparent)
    { G.Stroke(A, B, W * Scale, W * 1.2f * Scale, C.A > 0 ? C : Col, 1.3f); };
    const float H = Hash(K + 7);
    switch (Set)
    {
    case ERuneSet::Physical: // etched ticks with drifting dust specks
        Ln(P, P + N * Size * (K % 2 ? .35f : .6f), .8f);
        if (K % 3 == 0) { const float U = Fract(Time * .35f + H); G.Disc(P + N * Size * (.4f + U * .5f) + T * Size * .3f * FMath::Sin(Time + K), W * .9f, WithAlpha(Col, Col.A * .7f * (1 - U)), WithAlpha(Col, 0), 6, 1.4f); }
        break;
    case ERuneSet::Fire: // flame licks flickering inward
    {
        const float L = Size * (.45f + .35f * FMath::Abs(FMath::Sin(Time * 7.f + K * 1.7f)));
        G.Triangle(P - T * Size * .22f, P + N * L, P + T * Size * .22f, WithAlpha(Col, Col.A * .55f), 1.2f);
        Ln(P - T * Size * .22f, P + N * L, .6f); Ln(P + T * Size * .22f, P + N * L, .6f);
        break;
    }
    case ERuneSet::Frost: // icy crystal spikes of varying length
        Ln(P, P + N * Size * (.35f + .5f * H), bSharp ? .7f : .5f); Ln(P + T * Size * .12f, P + N * Size * .25f + T * Size * .2f, .5f);
        break;
    case ERuneSet::Earth: // jagged cracks running inward
    {
        FVector2D A = P;
        for (int32 J = 0; J < 3; ++J) { const FVector2D B = A + N * Size * .25f + T * Size * .14f * (Hash(K * 3 + J) - .5f) * 2.f; Ln(A, B, 1.f - J * .2f); A = B; }
        break;
    }
    case ERuneSet::Tide: // scalloped wave crest rolling along the rim
    {
        const float Ph = Time * 2.5f + K * .9f;
        const FVector2D A = P + N * Size * (.18f + .12f * FMath::Sin(Ph)), B = P + T * Size * .5f + N * Size * (.18f + .12f * FMath::Sin(Ph + .9f));
        Ln(A, B, .9f); Ln(A, A + N * Size * .12f - T * Size * .12f, .6f);
        break;
    }
    case ERuneSet::Holy: // alternating rays of light
        Ln(P, P + N * Size * (K % 2 ? .3f : .62f) * (.85f + .15f * FMath::Sin(Time * 2 + K)), .8f);
        break;
    case ERuneSet::Shadow: // hooked thorns
    {
        const FVector2D Tip = P + N * Size * .55f + T * Size * .18f;
        G.Triangle(P - T * Size * .14f, Tip, P + T * Size * .14f, WithAlpha(Col, Col.A * .6f), 1.2f); Ln(P, Tip, .6f);
        break;
    }
    case ERuneSet::Void: // twinkling starfield band
    {
        const float Tw = .35f + .65f * FMath::Abs(FMath::Sin(Time * 3.f + H * 20.f));
        const FVector2D S = P + N * Size * (.2f + .5f * H) + T * Size * (H - .5f) * .6f;
        Ln(S - T * Size * .09f * Tw, S + T * Size * .09f * Tw, .6f, WithAlpha(Col, Col.A * Tw)); Ln(S - N * Size * .09f * Tw, S + N * Size * .09f * Tw, .6f, WithAlpha(Col, Col.A * Tw));
        break;
    }
    case ERuneSet::Poison: // bubbles rising off the rim and popping
    {
        const float U = Fract(Time * .6f + H);
        G.Ring(P + N * Size * (.2f + .5f * U), Size * (.07f + .1f * U), W * .45f, W * .6f, WithAlpha(Col, Col.A * (1 - U)), 10, 0, Tau, 1.3f);
        break;
    }
    case ERuneSet::Nature: // curling vine with leaf buds
    {
        const FVector2D A = P + N * Size * .15f, B = P + N * Size * (.22f + .1f * FMath::Sin(K * 1.3f)) + T * Size * .5f; Ln(A, B, .7f);
        if (K % 2 == 0) { const FVector2D L = A + N * Size * .28f + T * Size * .1f; Ln(A + N * Size * .05f, L, .9f); }
        break;
    }
    case ERuneSet::Storm: // zigzag arcing along the rim, re-rolled every beat
    {
        const int32 Beat = static_cast<int32>(Time * 8.f);
        const FVector2D A = P + N * Size * (.15f + .2f * Hash(K + Beat)), B = P + T * Size * .5f + N * Size * (.15f + .2f * Hash(K + Beat + 1));
        Ln(A, B, .8f);
        break;
    }
    case ERuneSet::Arcane: // dashed rim with small diamonds
        Ln(P + N * Size * .2f - T * Size * .18f, P + N * Size * .2f + T * Size * .18f, .6f);
        if (K % 2 == 0) { const FVector2D C = P + N * Size * .42f; const float R = Size * .1f; Ln(C + N * R, C + T * R, .5f); Ln(C + T * R, C - N * R, .5f); Ln(C - N * R, C - T * R, .5f); Ln(C - T * R, C + N * R, .5f); }
        break;
    case ERuneSet::Blood: // drips pulled inward
        Ln(P, P + N * Size * (.3f + .3f * Fract(Time * .5f + H)), 1.1f);
        break;
    case ERuneSet::Spirit: // soft wisps
        Ln(P + N * Size * .1f, P + N * Size * .45f + T * Size * .2f * FMath::Sin(Time * 2 + K), .6f);
        break;
    case ERuneSet::Heal: default: // small radiant "+" with an upward-feeling shimmer
    {
        const float U = Fract(Time * .5f + H);
        const FVector2D C = P + N * Size * (.28f + .35f * U); const float R = Size * .13f;
        const FLinearColor Cc = WithAlpha(K % 2 ? FLinearColor(1.55f, 1.15f, .25f, 1) : Col, Col.A * FMath::Sin(U * PI));
        Ln(C - T * R, C + T * R, 1.1f, Cc); Ln(C - N * R, C + N * R, 1.1f, Cc);
        break;
    }
    }
}
} // namespace

int32 CireAbilityVFX::PaintRuneRing(FCireGroundMesh& G, FVector2D Center, float Radius, const FRuneTheme& Theme, float Time, float Alpha, float GlyphSize)
{
    if (Radius < 20 || Alpha <= .01f) return 0;
    const float Size = GlyphSize > 0 ? GlyphSize : FMath::Clamp(Radius * .22f, 16.f, 110.f);
    const int32 Count = FMath::Clamp(FMath::RoundToInt(Tau * Radius / (Size * 3.1f)), 4, 10); // fewer, larger glyphs
    const float Spin = Time * (Theme.bSharp ? .18f : .07f);
    FLinearColor Frame = WithAlpha(Theme.Glyph, .45f * Alpha);
    G.Ring(Center, Radius + Size * 1.15f, 1.1f, 3.f, Frame, 64, 0, Tau, 1.2f);
    G.Ring(Center, FMath::Max(4.f, Radius - Size * 1.15f), 1.1f, 3.f, Frame, 64, 0, Tau, 1.2f);
    for (int32 K = 0; K < Count; ++K)
    {
        const float A = Spin + K * Tau / Count;
        PaintGlyph(G, Theme.Set, Center + Polar2(Radius, A), Size, A + PI * .5f, WithAlpha(Theme.Glyph, .9f * Alpha), Time, K, 1.5f, Theme.Underlay);
    }
    return Count;
}

CireAbilityVFX::FRuneResult CireAbilityVFX::PaintRunes(FCireGroundMesh& G, const FCireAreaSpec& Spec, const FRuneTheme& Theme, float Time, float Alpha, bool bCenterSigil)
{
    FRuneResult R; const int32 Start = G.V.Num();
    const TArray<FVector2D> Boundary = ACireAreaEffect::BoundaryPoints(Spec);
    if (Boundary.Num() < 3 || Alpha <= .01f) return R;
    const float Dim = SizeOf(Spec, Boundary);
    const float Breath = Theme.bSharp ? .85f + .15f * FMath::Sin(Time * 5.f) : .8f + .2f * FMath::Sin(Time * 1.4f);
    const FLinearColor Glyph = WithAlpha(Theme.Glyph, .92f * Alpha * Breath), Edge = WithAlpha(Theme.Edge, .85f * Alpha);
    // 1. Edge treatment around the true boundary, pointing inward.
    const float EdgeSize = FMath::Clamp(Dim * .2f, 16.f, 90.f);
    int32 K = 0;
    for (const FEdgeSample& E : SampleEdge(Boundary, EdgeSize * 1.5f, 72))
    {
        // Near a sharp corner a motif's sideways reach would cross the other edge: leave corners clean.
        if (E.Corner < EdgeSize * 1.1f) continue;
        // Narrow places (a cone near its apex): the motif's full inward reach must still be inside.
        bool bRoom = true;
        for (const float Side : {-.65f, 0.f, .65f})
        {
            const FVector2D Q = E.P + E.N * EdgeSize * .95f + E.T * EdgeSize * Side;
            bRoom &= ACireAreaEffect::ContainsPoint(Spec, FVector::ZeroVector, FRotator::ZeroRotator, FVector(Q.X, Q.Y, 0));
        }
        if (!bRoom) continue;
        EdgeMotif(G, Theme.Set, E, EdgeSize, Time, K++, Edge, Theme.bSharp); ++R.EdgeMotifs;
    }
    // 2. Glyph band framed by thin rune rings, 3. centre sigil.
    const float Underlay = Spec.Shape == ECireAreaShape::Line ? 0.f : Theme.Underlay;
    auto Glyphs = [&](FVector2D Pos, float Size, float Angle, int32 Seed) { PaintGlyph(G, Theme.Set, Pos, Size, Angle, Glyph, Time, Seed, 1.5f, Underlay); ++R.Glyphs; };
    switch (Spec.Shape)
    {
    case ECireAreaShape::Circle:
    {
        // Bold enough to read at gameplay distance: glyphs ~13% of the radius, band at 62%.
        const float GlyphSize = FMath::Clamp(Spec.Radius * .23f, 20.f, 130.f);
        const float Band = Spec.Radius * .64f;
        R.Glyphs += PaintRuneRing(G, FVector2D::ZeroVector, Band, Theme, Time, Alpha * Breath, GlyphSize);
        if (bCenterSigil && Spec.Radius > 110.f)
        {
            Glyphs(FVector2D::ZeroVector, FMath::Clamp(Spec.Radius * .26f, 22.f, 170.f), -Time * (Theme.bSharp ? .25f : .1f), 99);
            G.Ring(FVector2D::ZeroVector, FMath::Clamp(Spec.Radius * .36f, 30.f, 230.f), 2.6f, 4.f, WithAlpha(Theme.Glyph, .75f * Alpha), 48, 0, Tau, 1.2f);
        }
        break;
    }
    case ECireAreaShape::Cone:
    {
        const float Half = FMath::DegreesToRadians(Spec.ConeAngleDegrees * .5f), Band = Spec.Radius * .66f;
        const float Size = FMath::Clamp(FMath::Min(Spec.Radius * .18f, Band * FMath::Sin(Half) * .75f), 16.f, 110.f);
        const int32 Count = FMath::Clamp(FMath::RoundToInt(2 * Half * Band / (Size * 2.7f)), 1, 6);
        const float Pad = FMath::Min(Half * .9f, Size * 1.4f / FMath::Max(Band, 1.f));
        G.Ring(FVector2D::ZeroVector, Band + Size * 1.15f, 1.1f, 3.f, WithAlpha(Theme.Glyph, .45f * Alpha), 32, -Half + Pad * .4f, 2 * (Half - Pad * .4f), 1.2f);
        G.Ring(FVector2D::ZeroVector, Band - Size * 1.15f, 1.1f, 3.f, WithAlpha(Theme.Glyph, .45f * Alpha), 32, -Half + Pad * .4f, 2 * (Half - Pad * .4f), 1.2f);
        for (int32 J = 0; J < Count; ++J)
        {
            const float A = Count == 1 ? 0.f : FMath::Lerp(-Half + Pad, Half - Pad, J / float(Count - 1));
            Glyphs(Polar2(Band, A), Size, A + PI * .5f, J);
        }
        break;
    }
    case ECireAreaShape::Line:
    {
        // Glyphs march along the lane's centre between the caster and the arrowhead, oriented along the lane.
        const float Size = FMath::Clamp(Spec.Width * .42f, 12.f, 90.f); // lanes: glyph nearly as wide as the lane (lane fill is the backing)
        const float Head = FMath::Clamp(Spec.Width * .95f, 36.f, FMath::Max(36.f, Spec.Length * .28f));
        const float From = Size * 1.4f, To = FMath::Max(From, Spec.Length - Head - Size * 1.2f);
        const int32 Count = FMath::Clamp(FMath::RoundToInt((To - From) / (Size * 3.2f)), 1, 14);
        const float Flow = Theme.bSharp ? Fract(Time * .15f) : 0.f;
        for (int32 J = 0; J < Count; ++J)
        {
            const float U = Count == 1 ? .5f : Fract((J + .5f) / Count + Flow);
            Glyphs(FVector2D(FMath::Lerp(From, To, U), 0), Size, -PI * .5f, J);
        }
        if (Spec.Width >= 90.f)
        {
            const float Y = Spec.Width * .5f - EdgeSize * 1.1f;
            G.Stroke(FVector2D(From * .5f, Y), FVector2D(To, Y), 1.1f, 3.f, WithAlpha(Theme.Glyph, .4f * Alpha), 1.2f);
            G.Stroke(FVector2D(From * .5f, -Y), FVector2D(To, -Y), 1.1f, 3.f, WithAlpha(Theme.Glyph, .4f * Alpha), 1.2f);
        }
        break;
    }
    default:
    {
        // Squares and authored polygons: a glyph band along an inset loop, sigil at the centroid.
        const float Size = FMath::Clamp(Dim * .24f, 16.f, 100.f);
        const auto Inset = Offset(Boundary, -Size * 1.9f);
        int32 J = 0;
        for (const FEdgeSample& E : SampleEdge(Inset, Size * 2.8f, 16))
        {
            const FVector2D Pos = E.P;
            if (!ACireAreaEffect::ContainsPoint(Spec, FVector::ZeroVector, FRotator::ZeroRotator, FVector(Pos.X, Pos.Y, 0))) continue;
            Glyphs(Pos, Size, FMath::Atan2(E.T.Y, E.T.X), J++);
        }
        if (bCenterSigil && Dim > 90.f) Glyphs(Centroid(Boundary), FMath::Clamp(Dim * .38f, 20.f, 150.f), -Time * .15f, 99);
        break;
    }
    }
    for (int32 J = Start; J < G.V.Num(); ++J) R.MaxExtent = FMath::Max(R.MaxExtent, static_cast<float>(FVector2D(G.V[J].X, G.V[J].Y).Size()));
    R.Vertices = G.V.Num() - Start;
    return R;
}

CireAbilityVFX::FVoidResult CireAbilityVFX::PaintVoidZone(FCireGroundMesh& G, FVector2D C, float Outer, float Inner, ETone Tone, float Time, float Alpha, bool bHeal)
{
    FVoidResult R;
    if (Outer <= 0 || Inner <= 0 || Inner >= Outer || Alpha <= .01f) return R;
    R.Outer = Outer; R.Inner = Inner;
    const bool bHostile = Tone == ETone::Hostile, bInvalid = Tone == ETone::AimInvalid;
    const FLinearColor VoidC = RuneColor(ERuneSet::Void);
    // Outer (slow) band: amber rim for enemy warnings, void violet for your own; starfield glyph ring inside.
    const FLinearColor OuterEdge = bInvalid ? FLinearColor(1.9f, .2f, .12f, 1) : bHostile ? FLinearColor(2.1f, .72f, .08f, 1) : FMath::Lerp(VoidC, FLinearColor(2, 2, 2, 1), .2f);
    const FLinearColor InnerEdge = bInvalid ? FLinearColor(2.2f, .3f, .2f, 1) : bHostile ? FLinearColor(2.4f, .35f, .06f, 1) : FLinearColor(2.2f, 1.9f, 2.6f, 1);
    TArray<FVector2D> OuterLoop, InnerLoop, MidLoop;
    for (int32 K = 0; K < 64; ++K) { const float A = K * Tau / 64; OuterLoop.Add(C + Polar2(Outer, A)); InnerLoop.Add(C + Polar2(Inner, A)); }
    // Slow band fill (annulus), denser toward the outer rim.
    G.Band(InnerLoop, OuterLoop, WithAlpha(bHostile ? FLinearColor(1.2f, .4f, .05f, 1) : VoidC * .6f, .1f * Alpha), WithAlpha(bHostile ? FLinearColor(1.25f, .42f, .04f, 1) : VoidC * .7f, .24f * Alpha), true);
    // Stun core fill: brighter, swirling toward the centre.
    G.Disc(C, Inner, WithAlpha(bHostile ? FLinearColor(2.f, .4f, .05f, 1) : VoidC, .34f * Alpha), WithAlpha(bHostile ? FLinearColor(2.f, .4f, .05f, 1) : VoidC, .18f * Alpha), 48, .4f);
    // Outer rim: dashed (slowing) ring; inner rim: solid double ring (stunning), clearly different.
    const float Dash = Time * .25f;
    for (int32 K = 0; K < 24; ++K) G.Ring(C, Outer, 2.2f, 6.f, WithAlpha(OuterEdge, .95f * Alpha), 4, Dash + K * Tau / 24, Tau / 24 * .62f, 1.5f);
    G.Ring(C, Inner, 3.2f, 9.f, WithAlpha(InnerEdge, Alpha), 56, 0, Tau, 1.6f);
    G.Ring(C, Inner * .82f, 1.4f, 4.f, WithAlpha(InnerEdge, .7f * Alpha), 48, 0, Tau, 1.6f);
    // Rune rings: void starfield glyphs in the slow band, rift swirls inside the stun circle.
    FRuneTheme Band; Band.Set = ERuneSet::Void; Band.Glyph = bInvalid ? FLinearColor(1.6f, .4f, .5f, 1) : VoidC; Band.Edge = Band.Glyph; Band.bSharp = true;
    const float BandR = (Outer + Inner) * .5f;
    const float GlyphSize = FMath::Clamp((Outer - Inner) * .3f, 14.f, 80.f);
    const int32 Glyphs = FMath::Clamp(FMath::RoundToInt(Tau * BandR / (GlyphSize * 3.4f)), 6, 12);
    for (int32 K = 0; K < Glyphs; ++K)
    {
        const float A = Time * .12f + K * Tau / Glyphs;
        if (K % 3 == 1)
        {
            // Slow icon: double down-chevron (the same mark BuffVisuals uses for "slowed").
            const FVector2D P = C + Polar2(BandR, A), Out = Polar2(1, A), Side(-Out.Y, Out.X);
            for (int32 J = 0; J < 2; ++J)
            {
                const FVector2D Row = P - Out * GlyphSize * (.35f - J * .5f);
                G.Stroke(Row + Side * GlyphSize * .55f + Out * GlyphSize * .3f, Row, 1.8f, 3.f, WithAlpha(OuterEdge, Alpha), 1.9f);
                G.Stroke(Row - Side * GlyphSize * .55f + Out * GlyphSize * .3f, Row, 1.8f, 3.f, WithAlpha(OuterEdge, Alpha), 1.9f);
            }
            ++R.SlowIcons;
        }
        else PaintGlyph(G, ERuneSet::Void, C + Polar2(BandR, A), GlyphSize, A, WithAlpha(Band.Glyph, .9f * Alpha), Time, K);
    }
    // Stun icons: small stars in orbit inside the inner circle (the "stunned" daze mark).
    const int32 Stars = Inner > 70.f ? 4 : 3;
    for (int32 K = 0; K < Stars; ++K)
    {
        const float A = -Time * .8f + K * Tau / Stars; const FVector2D P = C + Polar2(Inner * .52f, A); const float S = FMath::Clamp(Inner * .3f, 14.f, 60.f);
        for (int32 J = 0; J < 5; ++J)
        {
            const float B = A + J * Tau / 5;
            G.Stroke(P, P + Polar2(S, B), FMath::Clamp(S * .1f, 2.4f, 6.f), 3.f, WithAlpha(FLinearColor(1.7f, 1.45f, .2f, 1), Alpha), 2.f);
        }
        ++R.StunIcons;
    }
    // Rift swirl at the centre.
    for (int32 K = 0; K < 3; ++K)
        for (int32 J = 0; J < 8; ++J)
        {
            const float U0 = J / 8.f, U1 = (J + 1) / 8.f, A0 = K * Tau / 3 + U0 * 2.2f + Time * 1.2f, A1 = K * Tau / 3 + U1 * 2.2f + Time * 1.2f;
            G.Stroke(C + Polar2(Inner * .3f * U0, A0), C + Polar2(Inner * .3f * U1, A1), 1.4f, 2.5f, WithAlpha(InnerEdge, .8f * Alpha), 1.8f);
        }
    if (bHeal) PaintGlyph(G, ERuneSet::Heal, C, FMath::Clamp(Inner * .26f, 14.f, 60.f), 0, WithAlpha(RuneColor(ERuneSet::Heal), Alpha), Time, 0, 2.2f);
    R.Bounds = FBox2D(C - FVector2D(Outer, Outer), C + FVector2D(Outer, Outer));
    return R;
}
