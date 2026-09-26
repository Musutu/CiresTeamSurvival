#include "CireAreaEffects.h"
#include "CireActorIterator.h" // town-perf: fast actor iteration in editor-binary -game
#include "CireScalingKits.h" // scaling-kits
#include "CireItems.h" // items-v2
#include "CireDeveloperTools.h"
#include "CireGame.h"
#include "CireCombatEvents.h"
#include "CireSpellPresentation.h"
#include "Algo/Reverse.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Materials/MaterialInterface.h"
#include "CireAbilityVFX.h" // telegraphs: shared ground brightness
#include "Net/UnrealNetwork.h"
#include "ProceduralMeshComponent.h"

namespace
{
constexpr int32 MaxPolygonVertices = 32;
constexpr float MaxDimension = 2000.f;

double Cross(FVector2D A, FVector2D B) { return A.X * B.Y - A.Y * B.X; }
double SignedArea(const TArray<FVector2D>& Points)
{
    double Area = 0;
    for (int32 I = 0; I < Points.Num(); ++I) Area += Cross(Points[I], Points[(I + 1) % Points.Num()]);
    return Area * .5;
}
bool OnSegment(FVector2D A, FVector2D B, FVector2D P)
{
    return FMath::Abs(Cross(B - A, P - A)) <= .01 &&
        FVector2D::DotProduct(P - A, P - B) <= .01;
}
bool SegmentsIntersect(FVector2D A, FVector2D B, FVector2D C, FVector2D D)
{
    const double AB_C = Cross(B - A, C - A), AB_D = Cross(B - A, D - A);
    const double CD_A = Cross(D - C, A - C), CD_B = Cross(D - C, B - C);
    return (AB_C * AB_D < 0 && CD_A * CD_B < 0) ||
        OnSegment(A, B, C) || OnSegment(A, B, D) || OnSegment(C, D, A) || OnSegment(C, D, B);
}
bool PointInPolygon(const TArray<FVector2D>& Points, FVector2D Point)
{
    bool bInside = false;
    for (int32 I = 0, J = Points.Num() - 1; I < Points.Num(); J = I++)
    {
        const FVector2D A = Points[J], B = Points[I];
        if (OnSegment(A, B, Point)) return true;
        if ((A.Y > Point.Y) != (B.Y > Point.Y) &&
            Point.X < (B.X - A.X) * (Point.Y - A.Y) / (B.Y - A.Y) + A.X) bInside = !bInside;
    }
    return bInside;
}
TArray<int32> Triangulate(const TArray<FVector2D>& Points)
{
    TArray<int32> Remaining, Triangles;
    for (int32 I = 0; I < Points.Num(); ++I) Remaining.Add(I);
    if (SignedArea(Points) < 0) Algo::Reverse(Remaining);
    for (int32 Budget = Points.Num() * Points.Num(); Remaining.Num() > 2 && Budget > 0; --Budget)
    {
        bool bClipped = false;
        for (int32 I = 0; I < Remaining.Num(); ++I)
        {
            const int32 A = Remaining[(I + Remaining.Num() - 1) % Remaining.Num()];
            const int32 B = Remaining[I], C = Remaining[(I + 1) % Remaining.Num()];
            if (Cross(Points[B] - Points[A], Points[C] - Points[B]) <= .001) continue;
            bool bOccupied = false;
            for (int32 Candidate : Remaining)
            {
                if (Candidate == A || Candidate == B || Candidate == C) continue;
                const FVector2D P = Points[Candidate];
                if (Cross(Points[B] - Points[A], P - Points[A]) >= -.001 &&
                    Cross(Points[C] - Points[B], P - Points[B]) >= -.001 &&
                    Cross(Points[A] - Points[C], P - Points[C]) >= -.001) { bOccupied = true; break; }
            }
            if (bOccupied) continue;
            Triangles.Append({A, B, C});
            Remaining.RemoveAt(I);
            bClipped = true;
            break;
        }
        if (!bClipped) break;
    }
    return Triangles;
}
int32 Phase(const UWorld* World)
{
    if (!World) return INDEX_NONE;
    if (const auto* Mode = World->GetAuthGameMode<ACireGameMode>()) return static_cast<int32>(Mode->Clock.Phase());
    if (const auto* State = World->GetGameState<ACireGameState>()) return State->Phase;
    return INDEX_NONE;
}
FVector Feet(const AActor* Actor)
{
    FVector Result = Actor->GetActorLocation();
    if (const auto* Character = Cast<ACharacter>(Actor)) Result.Z -= Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
    return Result;
}
void ChangePoison(AActor* Actor, int32 Delta)
{
    float End=0;
    if(Actor)for(TCireActorIterator<ACireAreaEffect> It(Actor->GetWorld());It;++It)
        if(!It->IsActorBeingDestroyed()&&It->AreaSpec.bPoison&&It->AreaSpec.bPersistent&&It->HasOccupant(Actor))
            End=FMath::Max(End,It->StartServerTime+It->AreaSpec.WarningSeconds+It->AreaSpec.DurationSeconds);
    if (auto* Hero = Cast<ACireHero>(Actor))
    {
        Hero->PoisonAreaCount = FMath::Max(0, Hero->PoisonAreaCount + Delta);
        Hero->PoisonEndsAt=Hero->PoisonAreaCount>0?End:0;
        Hero->ForceNetUpdate();
    }
    else if (auto* Monster = Cast<ACireMonster>(Actor))
    {
        Monster->PoisonAreaCount = FMath::Max(0, Monster->PoisonAreaCount + Delta);
        Monster->PoisonEndsAt=Monster->PoisonAreaCount>0?End:0;
        Monster->ForceNetUpdate();
    }
}
} // namespace

bool ACireAreaEffect::ValidateSpec(const FCireAreaSpec& Spec, FString* Error)
{
    auto Fail = [Error](const TCHAR* Text) { if (Error) *Error = Text; return false; };
    auto Bounded = [](float Value, float Minimum, float Maximum) { return FMath::IsFinite(Value) && Value >= Minimum && Value <= Maximum; };
    if (static_cast<uint8>(Spec.Shape) > static_cast<uint8>(ECireAreaShape::Custom)) return Fail(TEXT("Unknown area shape"));
    if (!Bounded(Spec.Radius, 1, MaxDimension) || !Bounded(Spec.Length, 1, MaxDimension) ||
        !Bounded(Spec.Width, 1, MaxDimension) || !Bounded(Spec.ConeAngleDegrees, 1, 179)) return Fail(TEXT("Invalid area dimensions"));
    if (!Bounded(Spec.WarningSeconds, 0, 10) || !Bounded(Spec.DurationSeconds, .05f, 60) ||
        !Bounded(Spec.TickInterval, .05f, 5) || !Bounded(Spec.DamagePerSecond, 0, 10000) ||
        !Bounded(Spec.BurstDamage, 0, 10000) || !Bounded(Spec.VerticalTolerance, 1, 150)) return Fail(TEXT("Invalid area timing, damage, or height tolerance"));
    if (!Bounded(Spec.Color.R, 0, 8) || !Bounded(Spec.Color.G, 0, 8) || !Bounded(Spec.Color.B, 0, 8) ||
        !Bounded(Spec.Color.A, .03f, 1) || Spec.AbilityName.Len() > 80) return Fail(TEXT("Invalid area appearance or name"));
    if (Spec.CustomPolygon.Num() > MaxPolygonVertices) return Fail(TEXT("Custom shape exceeds 32 vertices"));
    if (Spec.Shape != ECireAreaShape::Custom) return true;
    if (Spec.CustomPolygon.Num() < 3) return Fail(TEXT("Custom shape requires three vertices"));
    const auto& P = Spec.CustomPolygon;
    for (int32 I = 0; I < P.Num(); ++I)
    {
        if (P[I].ContainsNaN() || P[I].SizeSquared() > FMath::Square(MaxDimension) ||
            FVector2D::DistSquared(P[I], P[(I + 1) % P.Num()]) < 1) return Fail(TEXT("Invalid custom shape vertex"));
        for (int32 J = I + 1; J < P.Num(); ++J)
        {
            if (J == I + 1 || (I == 0 && J == P.Num() - 1)) continue;
            if (SegmentsIntersect(P[I], P[(I + 1) % P.Num()], P[J], P[(J + 1) % P.Num()]))
                return Fail(TEXT("Custom shape edges intersect"));
        }
    }
    if (FMath::Abs(SignedArea(P)) < 1 || Triangulate(P).Num() != (P.Num() - 2) * 3)
        return Fail(TEXT("Custom shape is degenerate"));
    return true;
}

bool ACireAreaEffect::ContainsPoint(const FCireAreaSpec& Spec, FVector Center, FRotator Heading, FVector Point)
{
    if (Center.ContainsNaN() || Point.ContainsNaN() || Heading.ContainsNaN() ||
        FMath::Abs(Point.Z - Center.Z) > Spec.VerticalTolerance) return false;
    const FVector Local = FRotator(0, Heading.Yaw, 0).UnrotateVector(Point - Center);
    const FVector2D P(Local.X, Local.Y);
    switch (Spec.Shape)
    {
    case ECireAreaShape::Circle: return P.SizeSquared() <= FMath::Square(Spec.Radius);
    case ECireAreaShape::Cone:
        return P.SizeSquared() <= FMath::Square(Spec.Radius) &&
            (P.IsNearlyZero() || FMath::Abs(FMath::Atan2(P.Y, P.X)) <= FMath::DegreesToRadians(Spec.ConeAngleDegrees * .5f));
    case ECireAreaShape::Line: return P.X >= 0 && P.X <= Spec.Length && FMath::Abs(P.Y) <= Spec.Width * .5f;
    case ECireAreaShape::Square: return FMath::Abs(P.X) <= Spec.Width * .5f && FMath::Abs(P.Y) <= Spec.Width * .5f;
    case ECireAreaShape::Custom: return PointInPolygon(Spec.CustomPolygon, P);
    default: return false;
    }
}

TArray<FVector2D> ACireAreaEffect::BoundaryPoints(const FCireAreaSpec& Spec)
{
    TArray<FVector2D> Result;
    if (Spec.Shape == ECireAreaShape::Custom) return Spec.CustomPolygon;
    if (Spec.Shape == ECireAreaShape::Square)
    {
        const float H = Spec.Width * .5f;
        return {FVector2D(-H, -H), FVector2D(H, -H), FVector2D(H, H), FVector2D(-H, H)};
    }
    if (Spec.Shape == ECireAreaShape::Line)
    {
        const float H = Spec.Width * .5f;
        return {FVector2D(0, -H), FVector2D(Spec.Length, -H), FVector2D(Spec.Length, H), FVector2D(0, H)};
    }
    const bool bCone = Spec.Shape == ECireAreaShape::Cone;
    if (bCone) Result.Add(FVector2D::ZeroVector);
    const int32 Segments = bCone ? 24 : 64;
    for (int32 I = 0; I < Segments + (bCone ? 1 : 0); ++I)
    {
        const double Angle = bCone ? FMath::DegreesToRadians(-Spec.ConeAngleDegrees * .5 + Spec.ConeAngleDegrees * I / Segments) : 2 * PI * I / Segments;
        Result.Add(FVector2D(FMath::Cos(Angle), FMath::Sin(Angle)) * Spec.Radius);
    }
    return Result;
}

ACireAreaEffect::ACireAreaEffect()
{
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.TickGroup = TG_PostPhysics;
    bReplicates = true;
    SetReplicateMovement(true);
    SetNetUpdateFrequency(10);
    GroundMesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("GroundArea"));
    SetRootComponent(GroundMesh);
    GroundMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    GroundMesh->SetGenerateOverlapEvents(false);
    GroundMesh->SetCanEverAffectNavigation(false);
    GroundMesh->SetCastShadow(false);
    GroundMesh->bUseAsyncCooking = false;
}

ACireAreaEffect* ACireAreaEffect::Spawn(AActor* Source, const FCireAreaSpec& InputSpec, FVector GroundCenter, FRotator Heading)
{
    FCireAreaSpec Spec=InputSpec;if(Source)CireDeveloperTools::AdjustArea(Source->GetWorld(),Spec);
    if(Source){const float Wide=CireItems::AreaRadiusMultiplier(Source);Spec.Radius=FMath::Min(Spec.Radius*Wide,static_cast<float>(MaxDimension));Spec.Length=FMath::Min(Spec.Length*Wide,static_cast<float>(MaxDimension));Spec.Width=FMath::Min(Spec.Width*Wide,static_cast<float>(MaxDimension));} // items-v2: Heart of the Cataclysm
    if (!CireCombat::IsAlive(Source) || !Source->HasAuthority() ||
        !ValidateSpec(Spec) || GroundCenter.ContainsNaN() || Heading.ContainsNaN()) return nullptr;
    UWorld* World = Source->GetWorld();
    const auto* Mode = World ? World->GetAuthGameMode<ACireGameMode>() : nullptr;
    if (!Mode || !Mode->IsCombatPhase() || CireCombat::TeamOf(Source) < 0 || CireCombat::TeamOf(Source) > 1) return nullptr;
    int32 SourceAreas = 0, TotalAreas = 0;
    for (TCireActorIterator<ACireAreaEffect> It(World); It; ++It)
    {
        if (It->IsActorBeingDestroyed()) continue;
        ++TotalAreas;
        if (It->SourceActor == Source) ++SourceAreas;
    }
    if (SourceAreas >= 16 || TotalAreas >= 128) return nullptr;
    const FTransform Transform(FRotator(0, Heading.Yaw, 0), GroundCenter);
    auto* Area = World->SpawnActorDeferred<ACireAreaEffect>(StaticClass(), Transform, Source, Cast<APawn>(Source),
        ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
    if (!Area) return nullptr;
    Area->AreaSpec = Spec;
    Area->SourceActor = Source;
    Area->OriginTeam = CireCombat::TeamOf(Source);
    Area->OriginPhase = Phase(World);
    Area->StartServerTime = World->GetTimeSeconds();
    Area->FinishSpawning(Transform);
    Area->Tick(0); // Zero-delay zones apply status immediately on authoritative spawn.
    return Area;
}

void ACireAreaEffect::BeginPlay()
{
    Super::BeginPlay();
    RebuildVisual();
    CireSpellPresentation::FollowArea(this);
}

void ACireAreaEffect::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(ACireAreaEffect, AreaSpec);
    DOREPLIFETIME(ACireAreaEffect, bActive);
    DOREPLIFETIME(ACireAreaEffect, OriginTeam);
    DOREPLIFETIME(ACireAreaEffect, OriginPhase);
    DOREPLIFETIME(ACireAreaEffect, StartServerTime);
}

bool ACireAreaEffect::CanObserve(const AActor* Observer) const
{
    if (GetWorld() && GetWorld()->IsPlayingReplay()) return true;
    const auto* Hero = Cast<ACireHero>(Observer);
    if (const auto* Controller = Cast<AController>(Observer)) Hero = Cast<ACireHero>(Controller->GetPawn());
    return Hero && Hero->TeamId >= 0 && Hero->bDrafted && Phase(GetWorld()) == OriginPhase &&
        (OriginPhase == static_cast<int32>(Cires::MatchPhase::Arena) || Hero->TeamId == OriginTeam);
}

bool ACireAreaEffect::IsNetRelevantFor(const AActor* RealViewer, const AActor* ViewTarget, const FVector&) const
{
    return CanObserve(RealViewer ? RealViewer : ViewTarget);
}

void ACireAreaEffect::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if (HasAuthority())
    {
        const auto* Mode = GetWorld()->GetAuthGameMode<ACireGameMode>();
        if (!Mode || !Mode->IsCombatPhase() || Phase(GetWorld()) != OriginPhase || !IsValid(SourceActor) ||
            !CireCombat::IsAlive(SourceActor) || CireCombat::TeamOf(SourceActor) != OriginTeam)
        { ClearOccupants(); Destroy(); return; }
        const float PreviousAge = Age;
        Age += FMath::Max(0.f, DeltaSeconds);
        if (Age >= AreaSpec.WarningSeconds)
        {
            if (!bActive) { bActive = true; RebuildVisual(); ForceNetUpdate(); }
            const float ActiveDelta = FMath::Max(0.f, FMath::Min(Age, AreaSpec.WarningSeconds + AreaSpec.DurationSeconds) -
                FMath::Max(PreviousAge, AreaSpec.WarningSeconds));
            RefreshOccupants(ActiveDelta);
            if (!AreaSpec.bPersistent && !bBurstApplied)
            {
                bBurstApplied = true;
                TArray<TWeakObjectPtr<AActor>> Victims;
                Occupants.GetKeys(Victims);
                for (auto Victim : Victims)
                    if (IsValid(Victim.Get()) && CireCombat::AreHostile(SourceActor,Victim.Get()))
                        { FCireAreaDamageScope AreaScope; CireCombat::ApplyDamage(SourceActor, Victim.Get(), AreaSpec.BurstDamage, AreaSpec.AbilityName); } // scaling-kits: AoE-resist aura
                ClearOccupants();
            }
            if (AreaSpec.bPersistent)
            {
                TickElapsed += ActiveDelta;
                if (TickElapsed >= AreaSpec.TickInterval || Age >= AreaSpec.WarningSeconds + AreaSpec.DurationSeconds)
                { DealAccumulatedDamage(); TickElapsed = 0; }
            }
            if (Age >= AreaSpec.WarningSeconds + (AreaSpec.bPersistent ? AreaSpec.DurationSeconds : .22f))
            { ClearOccupants(); Destroy(); return; }
        }
    }
    if (GetNetMode() != NM_DedicatedServer)
    {
        const auto* Local = GetWorld()->GetFirstPlayerController();
        GroundMesh->SetVisibility(Local && Local->IsLocalController() && CanObserve(Local));
        // ability-vfx: the following spell visual paints the animated telegraph of this exact boundary;
        // the flat mesh keeps its observable state but is not drawn underneath it.
        GroundMesh->SetHiddenInGame(bPresentationOwnsGround);
    }
}

void ACireAreaEffect::RefreshOccupants(float ActiveDelta)
{
    if (!AreaSpec.bPersistent && bBurstApplied) return;
    TSet<TWeakObjectPtr<AActor>> Inside;
    for (TCireActorIterator<ACharacter> It(GetWorld()); It; ++It)
    {
        AActor* Actor = *It;
        if (IsValid(Actor) && CireCombat::AreHostile(SourceActor,Actor) &&
            ContainsPoint(AreaSpec, GetActorLocation(), GetActorRotation(), Feet(Actor)))
        {
            Inside.Add(Actor);
            if (float* Accrued = Occupants.Find(Actor)) *Accrued += ActiveDelta;
            else
            {
                // New arrivals never inherit damage accrued before they entered.
                Occupants.Add(Actor, 0.f);
                if (AreaSpec.bPersistent && AreaSpec.bPoison) ChangePoison(Actor, 1);
            }
        }
    }
    TArray<TWeakObjectPtr<AActor>> Previous;
    Occupants.GetKeys(Previous);
    for (auto Actor : Previous)
        if (!Inside.Contains(Actor)) RemoveOccupant(Actor.Get());
    // Destroyed actors have invalid weak keys; erase them without touching any
    // other area's count or retaining damage for a future actor at that address.
    for (auto It = Occupants.CreateIterator(); It; ++It) if (!It.Key().IsValid()) It.RemoveCurrent();
}

void ACireAreaEffect::RemoveOccupant(AActor* Actor)
{
    if (Actor && Occupants.Remove(Actor) && AreaSpec.bPersistent && AreaSpec.bPoison) ChangePoison(Actor, -1);
}

void ACireAreaEffect::ClearOccupants()
{
    TArray<TWeakObjectPtr<AActor>> Previous;
    Occupants.GetKeys(Previous);
    Occupants.Reset();
    if (AreaSpec.bPersistent && AreaSpec.bPoison)
        for (const auto& Actor : Previous) if (Actor.IsValid()) ChangePoison(Actor.Get(), -1);
}

void ACireAreaEffect::DealAccumulatedDamage()
{
    TArray<TWeakObjectPtr<AActor>> Victims;
    Occupants.GetKeys(Victims);
    for (auto Victim : Victims)
    {
        float* Accrued = Occupants.Find(Victim);
        if (!Accrued) continue;
        const float Damage = *Accrued * AreaSpec.DamagePerSecond;
        *Accrued = 0;
        // Membership is refreshed before every pulse; damage is never queued on
        // the target, so leaving cancels poison at the next post-physics update.
        if (IsValid(Victim.Get()) && IsValid(SourceActor) && CireCombat::AreHostile(SourceActor,Victim.Get()) &&
            ContainsPoint(AreaSpec, GetActorLocation(), GetActorRotation(), Feet(Victim.Get())))
            { FCireAreaDamageScope AreaScope; CireCombat::ApplyDamage(SourceActor, Victim.Get(), Damage, AreaSpec.AbilityName); } // scaling-kits
        if (!IsValid(Victim.Get()) || !IsValid(SourceActor) || !CireCombat::AreHostile(SourceActor,Victim.Get())) RemoveOccupant(Victim.Get());
    }
}

void ACireAreaEffect::ClearAll(UWorld* World)
{
    if (!World || World->GetNetMode() == NM_Client) return;
    for (TCireActorIterator<ACireAreaEffect> It(World); It; ++It) { It->ClearOccupants(); It->Destroy(); }
}

void ACireAreaEffect::ClearForActor(AActor* Actor)
{
    if (!Actor || !Actor->HasAuthority() || !Actor->GetWorld()) return;
    for (TCireActorIterator<ACireAreaEffect> It(Actor->GetWorld()); It; ++It)
    {
        if (It->SourceActor == Actor) { It->ClearOccupants(); It->Destroy(); }
        else It->RemoveOccupant(Actor);
    }
}

void ACireAreaEffect::EndPlay(const EEndPlayReason::Type Reason)
{
    if (HasAuthority()) ClearOccupants();
    Super::EndPlay(Reason);
}

void ACireAreaEffect::OnRep_Appearance() { RebuildVisual(); }

void ACireAreaEffect::RebuildVisual()
{
    if (GetNetMode() == NM_DedicatedServer || !GroundMesh || !ValidateSpec(AreaSpec)) return;
    GroundMesh->ClearAllMeshSections();
    const TArray<FVector2D> Boundary = BoundaryPoints(AreaSpec);
    const TArray<int32> FillTriangles = Triangulate(Boundary);
    if (FillTriangles.IsEmpty()) return;
    auto* Material = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Art/Materials/M_GroundArea.M_GroundArea"));
    if (!Material) Material = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/EngineDebugMaterials/VertexColorMaterial.VertexColorMaterial"));
    const FLinearColor Base = bActive ? AreaSpec.Color : FLinearColor(1.f, .62f, .08f, AreaSpec.Color.A);
    TArray<FVector> Vertices, Normals;
    TArray<FVector2D> UV;
    TArray<FLinearColor> Colors;
    for (FVector2D P : Boundary)
    {
        Vertices.Add(FVector(P.X, P.Y, 4)); Normals.Add(FVector::UpVector);
        UV.Add(P / MaxDimension); Colors.Add(FLinearColor(Base.R, Base.G, Base.B, Base.A * (bActive ? .8f : .5f)));
    }
    // telegraphs: the fallback flat mesh (drawn when the presentation cap is reached) follows the same brightness slider.
    const float Intensity = CireAbilityVFX::GroundIntensity(GetWorld());
    CireAbilityVFX::Temper(Colors, 0, Intensity, 1.f);
    GroundMesh->CreateMeshSection_LinearColor(0, Vertices, FillTriangles, Normals, UV, Colors, TArray<FProcMeshTangent>(), false);
    GroundMesh->SetMaterial(0, Material);
    // Independent edge quads work for convex and concave outlines, including the
    // cone's straight sides. Meshes never collide, overlap, or affect navigation.
    Vertices.Reset(); Normals.Reset(); UV.Reset(); Colors.Reset();
    TArray<int32> RimTriangles;
    for (int32 I = 0; I < Boundary.Num(); ++I)
    {
        const FVector2D A = Boundary[I], B = Boundary[(I + 1) % Boundary.Num()];
        const FVector2D Direction = (B - A).GetSafeNormal();
        const FVector2D Offset(-Direction.Y * 3, Direction.X * 3);
        const int32 First = Vertices.Num();
        for (FVector2D P : {A - Offset, B - Offset, B + Offset, A + Offset})
        {
            Vertices.Add(FVector(P.X, P.Y, 5)); Normals.Add(FVector::UpVector);
            UV.Add(P / MaxDimension); Colors.Add(FLinearColor(Base.R * 1.5f, Base.G * 1.5f, Base.B * 1.5f, .92f));
        }
        RimTriangles.Append({First, First + 1, First + 2, First, First + 2, First + 3});
    }
    CireAbilityVFX::Temper(Colors, 0, Intensity, 1.f);
    GroundMesh->CreateMeshSection_LinearColor(1, Vertices, RimTriangles, Normals, UV, Colors, TArray<FProcMeshTangent>(), false);
    GroundMesh->SetMaterial(1, Material);
}

