// bosses-spacing: boss body size, capsule/nav sizing, melee reach and crowd separation (CireUnitSpacing.h).
#include "CireUnitSpacing.h"
#include "CireGame.h"
#include "CireNPCArchetypes.h"
#include "CireNPCState.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Dom/JsonObject.h"
#include "HAL/IConsoleManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

DEFINE_LOG_CATEGORY_STATIC(LogCireUnitSpacing, Log, All);

namespace
{
FCireUnitSpacing GSpacing;
bool GSpacingLoaded = false;
FString SpacingPath() { return FPaths::ProjectContentDir() / TEXT("Data/UnitSpacing.json"); }
bool SpacingNum(const TSharedPtr<FJsonObject>& O, const TCHAR* Key, float& Value, float Min, float Max, FString& Error)
{
    double V = 0;
    if (!O->HasField(Key)) return true;
    if (!O->TryGetNumberField(Key, V) || !FMath::IsFinite(V) || V < Min || V > Max) { Error = FString::Printf(TEXT("UnitSpacing.json: %s must be %.0f..%.0f"), Key, Min, Max); return false; }
    Value = static_cast<float>(V); return true;
}
void EnsureLoaded() { if (!GSpacingLoaded) { GSpacingLoaded = true; FString Error; CireUnitSpacing::Reload(&Error); } }
}

bool CireUnitSpacing::Parse(const FString& Json, FCireUnitSpacing& Out, FString& Error)
{
    TSharedPtr<FJsonObject> Root;
    if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Root) || !Root) { Error = TEXT("UnitSpacing.json is not valid JSON"); return false; }
    FCireUnitSpacing S;
    const TSharedPtr<FJsonObject>* Boss = nullptr; const TSharedPtr<FJsonObject>* Units = nullptr;
    if (Root->TryGetObjectField(TEXT("boss"), Boss) && Boss)
    {
        if (!SpacingNum(*Boss, TEXT("sizeMultiplier"), S.BossSizeMultiplier, 1, 10, Error) || !SpacingNum(*Boss, TEXT("capsuleRadiusMax"), S.BossCapsuleRadiusMax, 30, 400, Error) ||
            !SpacingNum(*Boss, TEXT("capsuleHalfHeightMax"), S.BossCapsuleHalfHeightMax, 60, 2000, Error)) return false;
    }
    if (Root->TryGetObjectField(TEXT("units"), Units) && Units)
    {
        if (!SpacingNum(*Units, TEXT("capsuleRadius"), S.MonsterCapsuleRadius, 20, 80, Error) || !SpacingNum(*Units, TEXT("meleeReachBonus"), S.MeleeReachBonus, 0, 200, Error) ||
            !SpacingNum(*Units, TEXT("separationPadding"), S.SeparationPadding, 0, 200, Error) || !SpacingNum(*Units, TEXT("separationStrength"), S.SeparationStrength, 0, 1, Error)) return false;
        bool bOn = S.bSeparation; if ((*Units)->TryGetBoolField(TEXT("separation"), bOn)) S.bSeparation = bOn;
    }
    Out = S; return true;
}

const FCireUnitSpacing& CireUnitSpacing::Get() { EnsureLoaded(); return GSpacing; }
void CireUnitSpacing::Set(const FCireUnitSpacing& Spacing) { GSpacingLoaded = true; GSpacing = Spacing; }
bool CireUnitSpacing::Reload(FString* Error)
{
    GSpacingLoaded = true;
    FString Json, Why;
    if (!FFileHelper::LoadFileToString(Json, *SpacingPath())) { GSpacing = FCireUnitSpacing(); if (Error) *Error = TEXT("UnitSpacing.json is missing; built-in defaults"); return false; }
    FCireUnitSpacing S;
    if (!Parse(Json, S, Why)) { GSpacing = FCireUnitSpacing(); if (Error) *Error = Why; UE_LOG(LogCireUnitSpacing, Warning, TEXT("%s (built-in defaults)"), *Why); return false; }
    GSpacing = S;
    UE_LOG(LogCireUnitSpacing, Display, TEXT("CIRE_UNIT_SPACING boss_size=%.2f boss_capsule=%.0f/%.0f radius=%.0f melee_bonus=%.0f separation=%d pad=%.0f"), S.BossSizeMultiplier,
        S.BossCapsuleRadiusMax, S.BossCapsuleHalfHeightMax, S.MonsterCapsuleRadius, S.MeleeReachBonus, S.bSeparation ? 1 : 0, S.SeparationPadding);
    return true;
}

bool CireUnitSpacing::IsBossBody(const ACireMonster* M)
{
    return IsValid(M) && (M->IsLaneBoss() || M->GetNPCClassification() == ECireNPCClass::Boss);
}

void CireUnitSpacing::ApplyBody(ACireMonster* M, float Scale)
{
    if (!IsValid(M) || !FMath::IsFinite(Scale) || Scale <= 0.f) return;
    UCapsuleComponent* Capsule = M->GetCapsuleComponent();
    if (!Capsule) return;
    const FCireUnitSpacing& S = Get();
    float Radius = S.MonsterCapsuleRadius, Half = BaseHalfHeight;
    if (IsBossBody(M))
    {
        // The body is huge; the collision stays the Large nav agent so the boss walks the streets it paths on.
        Radius = FMath::Min(Radius, S.BossCapsuleRadiusMax / Scale);
        Half = FMath::Min(Half, S.BossCapsuleHalfHeightMax / Scale);
    }
    Half = FMath::Max(Half, Radius);
    const bool bScale = !M->GetActorScale3D().Equals(FVector(Scale), 1.e-4);
    const float OldHalf = Capsule->GetUnscaledCapsuleHalfHeight();
    const bool bCapsule = !FMath::IsNearlyEqual(Capsule->GetUnscaledCapsuleRadius(), Radius, .01f) || !FMath::IsNearlyEqual(OldHalf, Half, .01f);
    if (!bScale && !bCapsule) return;
    if (bCapsule)
    {
        // Keep the feet on the ground: the capsule bottom (actor Z - scaled half height) must not move, and the
        // body mesh hangs from the capsule bottom (-unscaled half height + lift, CireMonsterArt).
        const float OldScaledHalf = Capsule->GetScaledCapsuleHalfHeight();
        Capsule->SetCapsuleSize(Radius, Half, true);
        if (USkeletalMeshComponent* Mesh = M->GetMesh())
        {
            Mesh->SetRelativeLocation(Mesh->GetRelativeLocation() + FVector(0, 0, OldHalf - Half));
            M->CacheInitialMeshOffset(Mesh->GetRelativeLocation(), Mesh->GetRelativeRotation());
        }
        if (!bScale && M->HasAuthority()) M->AddActorWorldOffset(FVector(0, 0, Capsule->GetScaledCapsuleHalfHeight() - OldScaledHalf));
    }
    if (bScale)
    {
        const float OldScaledHalf = static_cast<float>(M->GetActorScale3D().Z) * (bCapsule ? OldHalf : Capsule->GetUnscaledCapsuleHalfHeight());
        M->SetActorScale3D(FVector(Scale));
        if (M->HasAuthority()) M->AddActorWorldOffset(FVector(0, 0, Capsule->GetScaledCapsuleHalfHeight() - OldScaledHalf));
    }
}

float CireUnitSpacing::VisualRadius(const ACireMonster* M)
{
    return IsValid(M) ? BodyRadius * static_cast<float>(M->GetActorScale3D().X) : BodyRadius;
}

float CireUnitSpacing::BodyReachBonus(const AActor* Actor)
{
    const auto* M = Cast<ACireMonster>(Actor);
    if (!IsValid(M) || !M->GetCapsuleComponent()) return 0.f;
    return FMath::Max(0.f, VisualRadius(M) - M->GetCapsuleComponent()->GetScaledCapsuleRadius());
}

float CireUnitSpacing::MeleeReach(const ACireMonster* M, float AttackRange)
{
    return AttackRange + Get().MeleeReachBonus + BodyReachBonus(M);
}

FVector CireUnitSpacing::Separation(const ACireMonster* M, const ACireGameMode* Mode)
{
    const FCireUnitSpacing& S = Get();
    if (!S.bSeparation || !IsValid(M) || !Mode || S.SeparationStrength <= 0.f) return FVector::ZeroVector;
    const FVector Me = M->GetActorLocation();
    const float MyR = M->GetCapsuleComponent()->GetScaledCapsuleRadius();
    FVector Push = FVector::ZeroVector;
    for (const ACireMonster* O : Mode->Monsters)
    {
        if (O == M || !IsValid(O) || O->Health <= 0 || O->Lane != M->Lane || IsBossBody(O)) continue;
        const float Want = MyR + O->GetCapsuleComponent()->GetScaledCapsuleRadius() + S.SeparationPadding;
        const FVector Delta = Me - O->GetActorLocation();
        if (FMath::Abs(Delta.Z) > 250. || Delta.SizeSquared2D() >= FMath::Square(Want)) continue;
        const float D = static_cast<float>(Delta.Size2D());
        // Coincident units split by a stable per-actor side instead of a zero vector.
        const FVector Away = D > 1.f ? Delta.GetSafeNormal2D() : FVector(M->GetUniqueID() > O->GetUniqueID() ? 1 : -1, 0, 0);
        Push += Away * (1.f - D / Want);
    }
    if (Push.SizeSquared2D() < .0025) return FVector::ZeroVector;
    return Push.GetClampedToMaxSize2D(1.f) * S.SeparationStrength;
}

float CireUnitSpacing::PlateLift(const ACireMonster* M)
{
    if (!IsValid(M) || !M->GetCapsuleComponent()) return 100.f;
    // The drawn body is BaseHalfHeight*2 x scale tall from the capsule bottom; the plate sits above its head.
    const float Scale = static_cast<float>(M->GetActorScale3D().Z);
    const float Top = BaseHalfHeight * 2.f * Scale - M->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
    return FMath::Max(100.f, Top + 12.f);
}

CireUnitSpacing::FCrowd CireUnitSpacing::Measure(const TArray<ACireMonster*>& Units)
{
    FCrowd C; double Nearest = 0, Depth = 0;
    TArray<const ACireMonster*> Live;
    for (const ACireMonster* M : Units) if (IsValid(M) && M->Health > 0) Live.Add(M);
    C.Units = Live.Num();
    // Footprint: the drawn body's half-width (arms and weapons included), independent of the capsule setting,
    // so before/after runs compare the same thing.
    const auto Foot = [](const ACireMonster* M) { return 1.3f * VisualRadius(M); };
    for (int32 I = 0; I < Live.Num(); ++I)
    {
        float Best = MAX_flt;
        for (int32 J = 0; J < Live.Num(); ++J)
        {
            if (I == J) continue;
            const float D = static_cast<float>(FVector::Dist2D(Live[I]->GetActorLocation(), Live[J]->GetActorLocation()));
            Best = FMath::Min(Best, D);
            const float Overlap = Foot(Live[I]) + Foot(Live[J]) - D;
            if (J > I && Overlap > 0) { ++C.OverlapPairs; Depth += Overlap; }
        }
        if (Best < MAX_flt) Nearest += Best;
    }
    C.MeanNearest = Live.Num() > 1 ? static_cast<float>(Nearest / Live.Num()) : 0.f;
    C.MeanOverlapDepth = C.OverlapPairs > 0 ? static_cast<float>(Depth / C.OverlapPairs) : 0.f;
    return C;
}

#if !UE_BUILD_SHIPPING
static FAutoConsoleCommand UnitSpacingCommand(TEXT("cire.Spacing"),
    TEXT("cire.Spacing reload | legacy | <bossSize|bossRadius|bossHalfHeight|radius|meleeBonus|pad|strength|separation> <value>: live unit spacing (Content/Data/UnitSpacing.json)."),
    FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
    {
        if (Args.Num() == 0 || Args[0] == TEXT("reload")) { FString Error; CireUnitSpacing::Reload(&Error); if (!Error.IsEmpty()) UE_LOG(LogCireUnitSpacing, Warning, TEXT("%s"), *Error); return; }
        if (Args[0] == TEXT("legacy")) { CireUnitSpacing::Set(FCireUnitSpacing::Legacy()); UE_LOG(LogCireUnitSpacing, Display, TEXT("legacy spacing (before bosses-spacing)")); return; }
        if (Args.Num() < 2) return;
        FCireUnitSpacing S = CireUnitSpacing::Get(); const float V = FCString::Atof(*Args[1]);
        const FString& K = Args[0];
        if (K == TEXT("bossSize")) S.BossSizeMultiplier = FMath::Clamp(V, 1.f, 10.f);
        else if (K == TEXT("bossRadius")) S.BossCapsuleRadiusMax = FMath::Clamp(V, 30.f, 400.f);
        else if (K == TEXT("bossHalfHeight")) S.BossCapsuleHalfHeightMax = FMath::Clamp(V, 60.f, 2000.f);
        else if (K == TEXT("radius")) S.MonsterCapsuleRadius = FMath::Clamp(V, 20.f, 80.f);
        else if (K == TEXT("meleeBonus")) S.MeleeReachBonus = FMath::Clamp(V, 0.f, 200.f);
        else if (K == TEXT("pad")) S.SeparationPadding = FMath::Clamp(V, 0.f, 200.f);
        else if (K == TEXT("strength")) S.SeparationStrength = FMath::Clamp(V, 0.f, 1.f);
        else if (K == TEXT("separation")) S.bSeparation = V != 0.f;
        else { UE_LOG(LogCireUnitSpacing, Warning, TEXT("cire.Spacing: unknown key %s"), *K); return; }
        CireUnitSpacing::Set(S);
    }));
#endif
