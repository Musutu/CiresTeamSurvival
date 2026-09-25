// balance: pylon field presentation (see CirePylonField.h).
#include "CirePylonField.h"
#include "CireAreaEffects.h"
#include "CireConstruct.h"
#include "CireAbilityVFX.h"
#include "CireSpellMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"

namespace
{
// A/B switch for reviews: 0 draws pylon fields with the generic active-zone painter (the previous look).
TAutoConsoleVariable<int32> CVarCalmPylons(TEXT("cire.PylonFieldCalm"), 1, TEXT("1: calm pylon fields (15-25% fill shared across overlaps, readable rim). 0: previous generic zone painter."));
}

bool CirePylonField::Enabled() { return CVarCalmPylons.GetValueOnAnyThread() != 0; }

bool CirePylonField::IsPylonField(const ACireAreaEffect* Area)
{
    if (!Enabled() || !IsValid(Area) || !Area->AreaSpec.bPersistent || Area->AreaSpec.Shape != ECireAreaShape::Circle) return false;
    const auto* Owner = Cast<ACireConstruct>(Area->GetOwner());
    return Owner && Owner->ConstructSpec.Kind == ECireConstructKind::Pylon;
}

int32 CirePylonField::CountOverlaps(const ACireAreaEffect* Area)
{
    if (!IsPylonField(Area) || !Area->GetWorld()) return 1;
    int32 N = 1;
    const FVector Here = Area->GetActorLocation();
    for (TActorIterator<ACireAreaEffect> It(Area->GetWorld()); It && N < MaxCountedOverlaps; ++It)
    {
        const ACireAreaEffect* Other = *It;
        if (Other == Area || Other->IsActorBeingDestroyed() || Other->IsHidden() || !IsPylonField(Other)) continue;
        const float Reach = Area->AreaSpec.Radius + Other->AreaSpec.Radius;
        if (FVector::DistSquared2D(Here, Other->GetActorLocation()) < FMath::Square(Reach) && FMath::Abs(Here.Z - Other->GetActorLocation().Z) < 300.f) ++N;
    }
    return N;
}

float CirePylonField::Intensity(const UWorld* World)
{
    // The ground-telegraph intensity slider (Options, 0.3..1, default 0.6), normalised to 0..1.
    return FMath::Clamp((CireAbilityVFX::GroundIntensity(World) - .3f) / .7f, 0.f, 1.f);
}

float CirePylonField::TargetFill(float InIntensity) { return FMath::Lerp(MinFill, MaxFill, FMath::Clamp(InIntensity, 0.f, 1.f)); }

float CirePylonField::LayerFill(float InIntensity, int32 Overlaps)
{
    const int32 N = FMath::Clamp(Overlaps, 1, MaxCountedOverlaps);
    return 1.f - FMath::Pow(1.f - TargetFill(InIntensity), 1.f / N);
}

float CirePylonField::Composite(float LayerAlpha, int32 Layers) { return 1.f - FMath::Pow(1.f - FMath::Clamp(LayerAlpha, 0.f, 1.f), static_cast<float>(FMath::Max(0, Layers))); }

float CirePylonField::EdgeAlpha(float InIntensity, int32 Overlaps)
{
    const int32 N = FMath::Clamp(Overlaps, 1, MaxCountedOverlaps);
    const float Full = FMath::Lerp(MinEdge + .15f, MaxEdge, FMath::Clamp(InIntensity, 0.f, 1.f));
    return FMath::Clamp(Full / FMath::Pow(static_cast<float>(N), .25f), MinEdge, MaxEdge);
}

int32 CirePylonField::ParticleBudget(int32 Overlaps) { return FMath::Max(3, 12 / FMath::Clamp(Overlaps, 1, MaxCountedOverlaps)); }

FLinearColor CirePylonField::CapBrightness(FLinearColor Color, float Cap)
{
    const float Peak = FMath::Max3(Color.R, Color.G, Color.B);
    if (Peak > Cap && Peak > 0) { const float K = Cap / Peak; Color.R *= K; Color.G *= K; Color.B *= K; }
    return Color;
}

int32 CirePylonField::Paint(FCireGroundMesh& G, float Radius, FLinearColor Color, float Time, float Alpha, float InIntensity, int32 Overlaps)
{
    using namespace CireSpellMesh;
    const int32 Start = G.V.Num();
    if (Radius <= 1 || Alpha <= .001f) return 0;
    Alpha = FMath::Clamp(Alpha, 0.f, 1.f);
    const FLinearColor FillColor = WithAlpha(CapBrightness(Color, FillEmissiveCap), LayerFill(InIntensity, Overlaps) * Alpha);
    const FLinearColor EdgeColor = WithAlpha(CapBrightness(Color * 1.3f, EdgeEmissiveCap), EdgeAlpha(InIntensity, Overlaps) * Alpha);
    const int32 Steps = FMath::Clamp(FMath::RoundToInt(Radius / 12.f), 40, 72);
    // Flat, even fill (no gradient band toward the rim: the band is what piled up where fields crossed).
    G.Disc(FVector2D::ZeroVector, Radius - 3.f, FillColor, FillColor, Steps);
    // Crisp rim on the true boundary, feathered inward only.
    G.Ring(FVector2D::ZeroVector, Radius - 2.f, 2.f, 6.f, EdgeColor, Steps);
    // One slow breathing ring (not a ripple train): shows the field is alive without adding fill.
    const float U = .5f + .5f * FMath::Sin(Time * 1.2f);
    G.Ring(FVector2D::ZeroVector, Radius * (.55f + .3f * U), 1.2f, 4.f, WithAlpha(EdgeColor, EdgeColor.A * .35f), Steps);
    return G.V.Num() - Start;
}

bool CirePylonField::RunTests(TArray<FString>& Failures)
{
    const int32 Before = Failures.Num();
    auto Check = [&Failures](bool bOk, const FString& What) { if (!bOk) Failures.Add(What); };
    for (float I : {0.f, .25f, .5f, .75f, 1.f})
    {
        Check(TargetFill(I) >= MinFill - 1e-4f && TargetFill(I) <= MaxFill + 1e-4f, FString::Printf(TEXT("pylon fill %.2f inside 15-25%% at intensity %.2f"), TargetFill(I), I));
        for (int32 N = 1; N <= MaxCountedOverlaps; ++N)
        {
            const float Layer = LayerFill(I, N);
            Check(FMath::IsNearlyEqual(Composite(Layer, N), TargetFill(I), 1e-3f), FString::Printf(TEXT("%d overlapping pylon fields composite to the single-field fill"), N));
            Check(Composite(Layer, N) <= MaxFill + 1e-3f, TEXT("overlap cap never exceeds 25%"));
            Check(EdgeAlpha(I, N) >= MinEdge - 1e-4f && EdgeAlpha(I, N) <= MaxEdge + 1e-4f, TEXT("pylon rim stays readable (40-70%)"));
            Check(EdgeAlpha(I, N) > Layer * 1.5f, TEXT("pylon rim reads above its fill"));
            if (N > 1) Check(Layer < LayerFill(I, N - 1), TEXT("more overlaps -> lower per-field alpha"));
            Check(ParticleBudget(N) * N <= 12 || ParticleBudget(N) == 3, TEXT("particle budget shared by the overlap group"));
        }
        if (I > 0) Check(TargetFill(I) > TargetFill(I - .25f), TEXT("intensity slider raises the fill"));
    }
    const FLinearColor Capped = CapBrightness(FLinearColor(.55f, .42f, 2.2f, 1), FillEmissiveCap);
    Check(FMath::Max3(Capped.R, Capped.G, Capped.B) <= FillEmissiveCap + 1e-4f && FMath::IsNearlyEqual(Capped.R / Capped.B, .55f / 2.2f, 1e-3f), TEXT("brightness cap preserves hue"));
    // Painted vertex alphas: fill vertices at the layer fill, rim at the edge alpha, nothing brighter than the caps.
    TArray<FVector> V; TArray<int32> Idx; TArray<FLinearColor> C;
    FCireGroundMesh G(V, Idx, C);
    const int32 Count = Paint(G, 450.f, FLinearColor(1.5f, 1.2f, 3.f, 1), 0.f, 1.f, 1.f, 4);
    Check(Count > 0 && Idx.Num() % 3 == 0, TEXT("pylon field painted"));
    float MaxA = 0; for (const auto& X : C) { MaxA = FMath::Max(MaxA, X.A); Check(FMath::Max3(X.R, X.G, X.B) <= EdgeEmissiveCap + 1e-3f, TEXT("no painted vertex above the emissive cap")); }
    Check(FMath::IsNearlyEqual(MaxA, EdgeAlpha(1.f, 4), 1e-3f), TEXT("brightest pylon vertex is the rim"));
    Check(C.Num() > 0 && FMath::IsNearlyEqual(C[0].A, LayerFill(1.f, 4), 1e-3f), TEXT("fill centre uses the overlap-shared alpha"));
    return Failures.Num() == Before;
}
