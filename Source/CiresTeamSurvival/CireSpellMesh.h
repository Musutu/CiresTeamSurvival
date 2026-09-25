#pragma once
// ability-vfx: bounded procedural builders shared by the spell renderer (CireSpellPresentation),
// the ability telegraphs (CireAbilityVFX) and the cursor aim preview (CireTargeting).
// Every builder silently stops at its vertex budget, so a cosmetic can never grow unbounded.
#include "CoreMinimal.h"

namespace CireSpellMesh
{
    constexpr int32 MaxCoreVertices = 6144, MaxSoftVertices = 2048, MaxGroundVertices = 6144; // ability-vfx: themed rune telegraphs
    inline float Fract(float N) { return N - FMath::FloorToFloat(N); }
    inline FVector Polar(float R, float A, float Z = 0) { return FVector(FMath::Cos(A) * R, FMath::Sin(A) * R, Z); }
    inline FVector2D Polar2(float R, float A) { return FVector2D(FMath::Cos(A) * R, FMath::Sin(A) * R); }
    inline FLinearColor WithAlpha(FLinearColor C, float A) { C.A = A; return C; }
}

// Modeled core section: unindexed-style triangles with vertex colours (alpha = coverage).
struct FCireSpellMesh
{
    TArray<FVector>& V; TArray<int32>& I; TArray<FLinearColor>& C;
    FCireSpellMesh(TArray<FVector>& Vertices, TArray<int32>& Indices, TArray<FLinearColor>& Colors) : V(Vertices), I(Indices), C(Colors)
    { V.Reset(); I.Reset(); C.Reset(); V.Reserve(4096); I.Reserve(6144); C.Reserve(4096); }
    void Tri(FVector A, FVector B, FVector D, FLinearColor Color)
    {
        if (V.Num() + 3 > CireSpellMesh::MaxCoreVertices) return;
        const int32 N = V.Num(); V.Append({A, B, D}); C.Append({Color, Color, Color}); I.Append({N, N + 1, N + 2});
    }
    void Quad(FVector A, FVector B, FVector D, FVector E, FLinearColor Color) { Tri(A, B, D, Color); Tri(A, D, E, Color); }
    void Tube(FVector A, FVector B, float Radius, FLinearColor Color, int32 Sides = 5)
    {
        FVector Along = (B - A).GetSafeNormal(), U, Vv; Along.FindBestAxisVectors(U, Vv);
        for (int32 J = 0; J < Sides; ++J)
        {
            const float X = 2 * PI * J / Sides, Y = 2 * PI * (J + 1) / Sides;
            const FVector P = (U * FMath::Cos(X) + Vv * FMath::Sin(X)) * Radius;
            const FVector Q = (U * FMath::Cos(Y) + Vv * FMath::Sin(Y)) * Radius;
            Quad(A + P, B + P, B + Q, A + Q, Color);
        }
    }
    void Ring(float R, float W, float Z, FLinearColor Color, float Begin = 0, float Span = 2 * PI, int32 Steps = 48)
    {
        using namespace CireSpellMesh;
        for (int32 J = 0; J < Steps; ++J)
        {
            const float A = Begin + Span * J / Steps, B = Begin + Span * (J + 1) / Steps;
            const float Edge = FMath::Min(1.f, FMath::Min((J + 1) * .25f, (Steps - J) * .25f));
            FLinearColor T = Color; T.A *= Edge;
            Quad(Polar(R, A, Z), Polar(R, B, Z), Polar(R - W, B, Z), Polar(R - W, A, Z), T);
        }
    }
    void Shard(FVector P, float R, float H, FLinearColor Color, float Twist = 0)
    {
        using namespace CireSpellMesh;
        const FVector Top = P + FVector(0, 0, H), Bottom = P - FVector(0, 0, H * .2f);
        for (int32 J = 0; J < 4; ++J)
        {
            const FVector A = P + Polar(R, Twist + PI * .5f * J), B = P + Polar(R, Twist + PI * .5f * (J + 1));
            FLinearColor Shade = Color; Shade *= J % 2 ? .58f : 1.f; Shade.A = Color.A;
            Tri(A, B, Top, Shade); Tri(B, A, Bottom, Shade);
        }
    }
    // Shard pointing along an arbitrary direction (spears, debris, spikes).
    void Spike(FVector Base, FVector Tip, float R, FLinearColor Color, float Twist = 0)
    {
        FVector Along = (Tip - Base).GetSafeNormal(), U, Vv; Along.FindBestAxisVectors(U, Vv);
        const FVector Back = Base - Along * (Tip - Base).Size() * .18f;
        for (int32 J = 0; J < 4; ++J)
        {
            const float X = Twist + PI * .5f * J, Y = X + PI * .5f;
            const FVector A = Base + (U * FMath::Cos(X) + Vv * FMath::Sin(X)) * R, B = Base + (U * FMath::Cos(Y) + Vv * FMath::Sin(Y)) * R;
            FLinearColor Shade = Color; Shade *= J % 2 ? .62f : 1.f; Shade.A = Color.A;
            Tri(A, B, Tip, Shade); Tri(B, A, Back, Shade);
        }
    }
    void Rune(FVector P, float Scale, float Angle, FLinearColor Color)
    {
        using namespace CireSpellMesh;
        const FVector X = Polar(Scale, Angle), Y = Polar(Scale * .55f, Angle + PI * .5f);
        Tube(P - X, P + X, 1.2f, Color, 3); Tube(P - X, P + Y, 1.2f, Color, 3);
        Tube(P + Y, P + X, 1.2f, Color, 3); Tube(P - X * .3f - Y, P + X * .3f + Y, 1.2f, Color, 3);
    }
    void Helix(float R, float H, float T, FLinearColor Color, int32 Strands = 2)
    {
        using namespace CireSpellMesh;
        for (int32 S = 0; S < Strands; ++S)
            for (int32 J = 0; J < 22; ++J)
            {
                const float U = J / 22.f, Vv = (J + 1) / 22.f;
                const float A = S * 2 * PI / Strands + U * PI * 1.6f + T, B = S * 2 * PI / Strands + Vv * PI * 1.6f + T;
                FLinearColor Fade = Color; Fade.A *= FMath::Sin(U * PI) * .8f;
                Tube(Polar(R * (1 - U * .35f), A, H * U), Polar(R * (1 - Vv * .35f), B, H * Vv), 1.8f, Fade, 4);
            }
    }
    void Star(FVector P, float Radius, FLinearColor Color, float Angle = 0)
    {
        using namespace CireSpellMesh;
        for (int32 J = 0; J < 8; ++J)
        {
            const float A = Angle + J * PI * .25f, B = A + PI * .25f;
            Tri(P, P + Polar(J % 2 ? Radius * .28f : Radius, A), P + Polar(J % 2 ? Radius : Radius * .28f, B), Color);
        }
    }
    void Sigil(float R, float Z, float Spin, FLinearColor Color)
    {
        using namespace CireSpellMesh;
        Ring(R, 1.1f, Z, Color, Spin, 2 * PI, 48); Ring(R * .79f, .7f, Z + .3f, Color, -Spin, 2 * PI, 48);
        for (int32 Mark = 0; Mark < 8; ++Mark)
        {
            const float A = Spin + Mark * PI * .25f;
            Tube(Polar(R * .84f, A - .025f, Z), Polar(R * .96f, A + .025f, Z), .6f, Color, 3);
            if (Mark % 2 == 0) Rune(Polar(R * .63f, A, Z), R * .08f, A, Color);
        }
        for (int32 Corner = 0; Corner < 3; ++Corner) Tube(Polar(R * .4f, Spin + Corner * PI * 2 / 3, Z), Polar(R * .4f, Spin + (Corner + 1) * PI * 2 / 3, Z), .65f, Color, 3);
    }
    // Jagged lightning between two points (deterministic by Seed), with optional thin fork.
    void Bolt(FVector A, FVector B, float Radius, FLinearColor Color, int32 Seed, float Jitter = 22.f, int32 Steps = 12)
    {
        FVector Prev = A; const FVector Along = (B - A).GetSafeNormal(); FVector U, Vv; Along.FindBestAxisVectors(U, Vv);
        for (int32 J = 1; J <= Steps; ++J)
        {
            const float T = J / float(Steps);
            FVector P = FMath::Lerp(A, B, T);
            if (J < Steps) P += U * FMath::Sin(J * 7.21f + Seed * 2.3f) * Jitter + Vv * FMath::Cos(J * 4.91f + Seed * 1.7f) * Jitter * .8f;
            Tube(Prev, P, Radius * (1.1f - T * .4f), Color, 4); Prev = P;
        }
    }
};

// Camera-facing soft section (M_SpellSoft radial falloff through UVs).
struct FCireSoftMesh
{
    TArray<FVector>& V; TArray<int32>& I; TArray<FLinearColor>& C; TArray<FVector2D>& UV;
    FVector Right, Up, Look;
    FCireSoftMesh(TArray<FVector>& InV, TArray<int32>& InI, TArray<FLinearColor>& InC, TArray<FVector2D>& InUV, FVector R, FVector U)
        : V(InV), I(InI), C(InC), UV(InUV), Right(R), Up(U), Look(FVector::CrossProduct(R, U))
    { V.Reset(); I.Reset(); C.Reset(); UV.Reset(); V.Reserve(1024); I.Reserve(1024); C.Reserve(1024); UV.Reserve(1024); }
    void Quad(FVector A, FVector B, FVector D, FVector E, FLinearColor Color)
    {
        if (V.Num() + 4 > CireSpellMesh::MaxSoftVertices) return; const int32 N = V.Num();
        V.Append({A, B, D, E}); C.Append({Color, Color, Color, Color}); UV.Append({FVector2D(0, 0), FVector2D(1, 0), FVector2D(1, 1), FVector2D(0, 1)});
        I.Append({N, N + 1, N + 2, N, N + 2, N + 3});
    }
    void Glow(FVector P, float Radius, FLinearColor Color)
    { Quad(P - Right * Radius - Up * Radius, P + Right * Radius - Up * Radius, P + Right * Radius + Up * Radius, P - Right * Radius + Up * Radius, Color); }
    void Ribbon(FVector A, FVector B, float Width, FLinearColor Color)
    {
        const FVector Side = FVector::CrossProduct(B - A, Look).GetSafeNormal() * Width;
        Quad(A - Side, A + Side, B + Side, B - Side, Color);
    }
};

// Flat ground section (M_GroundArea: unlit translucent, emissive = vertex RGB, opacity = vertex A).
// Vertex alpha interpolates across triangles, which is how edges are feathered without textures.
struct FCireGroundMesh
{
    TArray<FVector>& V; TArray<int32>& I; TArray<FLinearColor>& C;
    float Z = 5.f;
    FCireGroundMesh(TArray<FVector>& Vertices, TArray<int32>& Indices, TArray<FLinearColor>& Colors) : V(Vertices), I(Indices), C(Colors)
    { V.Reset(); I.Reset(); C.Reset(); V.Reserve(1024); I.Reserve(3072); C.Reserve(1024); }
    bool Room(int32 N) const { return V.Num() + N <= CireSpellMesh::MaxGroundVertices; }
    int32 Add(FVector2D P, FLinearColor Color, float Lift = 0) { V.Add(FVector(P.X, P.Y, Z + Lift)); C.Add(Color); return V.Num() - 1; }
    void Tri(int32 A, int32 B, int32 D) { I.Append({A, B, D}); }
    // Strip between two loops of equal length (closed or open).
    void Band(const TArray<FVector2D>& Inner, const TArray<FVector2D>& Outer, FLinearColor InnerColor, FLinearColor OuterColor, bool bClosed, float Lift = 0)
    {
        const int32 N = FMath::Min(Inner.Num(), Outer.Num()); if (N < 2 || !Room(N * 2)) return;
        const int32 Base = V.Num();
        for (int32 J = 0; J < N; ++J) { Add(Inner[J], InnerColor, Lift); Add(Outer[J], OuterColor, Lift); }
        const int32 Segments = bClosed ? N : N - 1;
        for (int32 J = 0; J < Segments; ++J)
        {
            const int32 A = Base + J * 2, B = Base + ((J + 1) % N) * 2;
            Tri(A, A + 1, B + 1); Tri(A, B + 1, B);
        }
    }
    // Fan fill from a pivot: pivot colour at the pivot, edge colour on the loop (radial gradient).
    void Fan(FVector2D Pivot, const TArray<FVector2D>& Loop, FLinearColor PivotColor, FLinearColor EdgeColor, bool bClosed = true, float Lift = 0)
    {
        const int32 N = Loop.Num(); if (N < 2 || !Room(N + 1)) return;
        const int32 Center = Add(Pivot, PivotColor, Lift), Base = V.Num();
        for (const FVector2D& P : Loop) Add(P, EdgeColor, Lift);
        for (int32 J = 0; J < (bClosed ? N : N - 1); ++J) Tri(Center, Base + J, Base + (J + 1) % N);
    }
    void Quad2(FVector2D A, FVector2D B, FVector2D D, FVector2D E, FLinearColor Color, float Lift = 0)
    {
        if (!Room(4)) return; const int32 N = Add(A, Color, Lift); Add(B, Color, Lift); Add(D, Color, Lift); Add(E, Color, Lift);
        Tri(N, N + 1, N + 2); Tri(N, N + 2, N + 3);
    }
    void Triangle(FVector2D A, FVector2D B, FVector2D D, FLinearColor Color, float Lift = 0)
    { if (!Room(3)) return; const int32 N = Add(A, Color, Lift); Add(B, Color, Lift); Add(D, Color, Lift); Tri(N, N + 1, N + 2); }
    // Thick segment with feathered sides (solid core, alpha fading to zero at Feather distance).
    void Stroke(FVector2D A, FVector2D B, float HalfWidth, float Feather, FLinearColor Color, float Lift = 0)
    {
        const FVector2D D = (B - A).GetSafeNormal(); const FVector2D N(-D.Y, D.X);
        const FLinearColor Clear = CireSpellMesh::WithAlpha(Color, 0);
        Quad2(A - N * HalfWidth, B - N * HalfWidth, B + N * HalfWidth, A + N * HalfWidth, Color, Lift);
        if (Feather > 0)
        {
            if (!Room(8)) return;
            const int32 S = V.Num();
            Add(A + N * HalfWidth, Color, Lift); Add(B + N * HalfWidth, Color, Lift); Add(B + N * (HalfWidth + Feather), Clear, Lift); Add(A + N * (HalfWidth + Feather), Clear, Lift);
            Tri(S, S + 1, S + 2); Tri(S, S + 2, S + 3);
            Add(A - N * HalfWidth, Color, Lift); Add(B - N * HalfWidth, Color, Lift); Add(B - N * (HalfWidth + Feather), Clear, Lift); Add(A - N * (HalfWidth + Feather), Clear, Lift);
            Tri(S + 4, S + 6, S + 5); Tri(S + 4, S + 7, S + 6);
        }
    }
    void Ring(FVector2D Center, float Radius, float HalfWidth, float Feather, FLinearColor Color, int32 Steps = 56, float Begin = 0, float Span = 2 * PI, float Lift = 0)
    {
        using namespace CireSpellMesh;
        TArray<FVector2D> In, Mid, Out, Mid2; const bool bClosed = Span >= 2 * PI - .001f; const int32 Count = bClosed ? Steps : Steps + 1;
        for (int32 J = 0; J < Count; ++J)
        {
            const float A = Begin + Span * J / Steps;
            In.Add(Center + Polar2(FMath::Max(0.f, Radius - HalfWidth - Feather), A)); Mid.Add(Center + Polar2(FMath::Max(0.f, Radius - HalfWidth), A));
            Mid2.Add(Center + Polar2(Radius + HalfWidth, A)); Out.Add(Center + Polar2(Radius + HalfWidth + Feather, A));
        }
        const FLinearColor Clear = WithAlpha(Color, 0);
        if (Feather > 0) Band(In, Mid, Clear, Color, bClosed, Lift);
        Band(Mid, Mid2, Color, Color, bClosed, Lift);
        if (Feather > 0) Band(Mid2, Out, Color, Clear, bClosed, Lift);
    }
    void Disc(FVector2D Center, float Radius, FLinearColor CenterColor, FLinearColor EdgeColor, int32 Steps = 40, float Lift = 0)
    {
        TArray<FVector2D> Loop; for (int32 J = 0; J < Steps; ++J) Loop.Add(Center + CireSpellMesh::Polar2(Radius, 2 * PI * J / Steps));
        Fan(Center, Loop, CenterColor, EdgeColor, true, Lift);
    }
    // Open chevron ("^" pointing along Dir), feathered.
    void Chevron(FVector2D Center, FVector2D Dir, float Size, float HalfWidth, FLinearColor Color, float Lift = 0)
    {
        const FVector2D D = Dir.GetSafeNormal(), N(-D.Y, D.X);
        const FVector2D Tip = Center + D * Size * .5f, L = Center - D * Size * .5f + N * Size * .75f, R = Center - D * Size * .5f - N * Size * .75f;
        Stroke(L, Tip, HalfWidth, HalfWidth * 1.4f, Color, Lift); Stroke(R, Tip, HalfWidth, HalfWidth * 1.4f, Color, Lift);
    }
};

namespace CireSpellMesh
{
    // Offset a simple polygon by Distance along its outward vertex normals (negative = inward).
    // Miter length is clamped, so sharp cone apexes and concave notches stay bounded.
    inline TArray<FVector2D> Offset(const TArray<FVector2D>& P, float Distance)
    {
        TArray<FVector2D> Out; const int32 N = P.Num(); if (N < 3) return P;
        double Area = 0; for (int32 J = 0; J < N; ++J) Area += P[J].X * P[(J + 1) % N].Y - P[(J + 1) % N].X * P[J].Y;
        const float Sign = Area >= 0 ? 1.f : -1.f; // CCW: outward normal of edge d is (d.Y, -d.X)
        for (int32 J = 0; J < N; ++J)
        {
            const FVector2D A = P[(J + N - 1) % N], B = P[J], D = P[(J + 1) % N];
            const FVector2D E1 = (B - A).GetSafeNormal(), E2 = (D - B).GetSafeNormal();
            const FVector2D N1 = FVector2D(E1.Y, -E1.X) * Sign, N2 = FVector2D(E2.Y, -E2.X) * Sign;
            FVector2D M = (N1 + N2).GetSafeNormal(); if (M.IsNearlyZero()) M = N1;
            const float Cos = FMath::Max(.35f, float(FVector2D::DotProduct(M, N1)));
            Out.Add(B + M * (Distance / Cos));
        }
        return Out;
    }
    inline FVector2D Centroid(const TArray<FVector2D>& P)
    {
        FVector2D Sum = FVector2D::ZeroVector; for (const auto& X : P) Sum += X; return P.Num() ? Sum / P.Num() : Sum;
    }
    inline TArray<FVector2D> Scaled(const TArray<FVector2D>& P, FVector2D Pivot, float S)
    { TArray<FVector2D> Out; for (const auto& X : P) Out.Add(Pivot + (X - Pivot) * S); return Out; }
}
