#include "CireAreaEffects.h"

#if !UE_BUILD_SHIPPING
#include "CireGame.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "Misc/ScopeExit.h"

DEFINE_LOG_CATEGORY_STATIC(LogCireAreaTests, Log, All);

namespace
{
struct FChecks
{
    int32 Count = 0;
    bool bPassed = true;
    void Check(bool bCondition, const TCHAR* Message)
    {
        ++Count;
        if (!bCondition) { bPassed = false; UE_LOG(LogCireAreaTests, Error, TEXT("CIRE_AREA_CHECK_FAIL %s"), Message); }
    }
    bool Finish(const TCHAR* Group) const
    {
        UE_LOG(LogCireAreaTests, Display, TEXT("CIRE_AREA_%s_%s checks=%d"), Group, bPassed ? TEXT("PASS") : TEXT("FAIL"), Count);
        return bPassed;
    }
};
}

bool CireAreaEffects::RunGeometrySmoke()
{
    FChecks Tests;
    FCireAreaSpec S;
    const FVector Origin(100, 200, 30);
    auto In = [&](FVector P, float Yaw = 0.f) { return ACireAreaEffect::ContainsPoint(S, Origin, FRotator(0, Yaw, 0), Origin + P); };
    Tests.Check(ACireAreaEffect::ValidateSpec(S), TEXT("default area valid"));
    S.Radius = 100;
    Tests.Check(In(FVector(100, 0, 0)) && !In(FVector(101, 0, 0)), TEXT("circle boundary inclusive and outside excluded"));
    Tests.Check(!In(FVector(0, 0, S.VerticalTolerance + 1)) && !In(FVector(0, 0, -S.VerticalTolerance - 1)), TEXT("upper and lower floors excluded"));
    S.Shape = ECireAreaShape::Cone;
    S.ConeAngleDegrees = 90;
    Tests.Check(In(FVector(80, 0, 0)) && !In(FVector(-10, 0, 0)) && !In(FVector(40, 60, 0)), TEXT("cone forward arc only"));
    Tests.Check(In(FVector(0, 80, 0), 90) && !In(FVector(80, 0, 0), 90), TEXT("cone rotates with heading"));
    S.Shape = ECireAreaShape::Line;
    S.Width = 40; S.Length = 100;
    Tests.Check(In(FVector(100, 20, 0)) && !In(FVector(-1, 0, 0)) && !In(FVector(50, 21, 0)), TEXT("line starts at origin and uses half-width"));
    Tests.Check(In(FVector(-10, 50, 0), 90), TEXT("line rotation"));
    S.Shape = ECireAreaShape::Square;
    Tests.Check(In(FVector(-20, 20, 0)) && !In(FVector(21, 0, 0)), TEXT("square centred dimensions"));
    S.Shape = ECireAreaShape::Custom;
    S.CustomPolygon = {FVector2D(0, 0), FVector2D(100, 0), FVector2D(100, 40), FVector2D(40, 40), FVector2D(40, 100), FVector2D(0, 100)};
    Tests.Check(ACireAreaEffect::ValidateSpec(S), TEXT("simple concave polygon accepted"));
    Tests.Check(In(FVector(20, 80, 0)) && !In(FVector(80, 80, 0)) && In(FVector(40, 60, 0)), TEXT("concave notch excluded and edge included"));
    Tests.Check(ACireAreaEffect::BoundaryPoints(S).Num() == 6, TEXT("custom visual boundary preserves vertices"));
    S.CustomPolygon = {FVector2D(0, 0), FVector2D(100, 100), FVector2D(0, 100), FVector2D(100, 0)};
    Tests.Check(!ACireAreaEffect::ValidateSpec(S), TEXT("self intersecting polygon rejected"));
    S.CustomPolygon.Init(FVector2D(1, 1), 33);
    Tests.Check(!ACireAreaEffect::ValidateSpec(S), TEXT("polygon vertex budget enforced"));
    S = FCireAreaSpec(); S.TickInterval = 0;
    Tests.Check(!ACireAreaEffect::ValidateSpec(S), TEXT("zero interval rejected"));
    S = FCireAreaSpec(); S.VerticalTolerance = 1000;
    Tests.Check(!ACireAreaEffect::ValidateSpec(S), TEXT("excessive through-floor tolerance rejected"));
    for (ECireAreaShape Shape : {ECireAreaShape::Circle, ECireAreaShape::Cone, ECireAreaShape::Line, ECireAreaShape::Square})
    {
        S = FCireAreaSpec(); S.Shape = Shape;
        const auto Boundary = ACireAreaEffect::BoundaryPoints(S);
        Tests.Check(Boundary.Num() >= 3 && Boundary.Num() <= 64, TEXT("standard visual boundary has bounded geometry"));
    }
    return Tests.Finish(TEXT("GEOMETRY"));
}

bool CireAreaEffects::RunLifecycleSmoke(ACireGameMode* Mode)
{
    if (!IsValid(Mode) || !Mode->HasAuthority()) return false;
    FChecks Tests;
    const Cires::MatchClock SavedClock = Mode->Clock;
    const auto SavedHeroes = Mode->Heroes;
    const auto SavedMonsters = Mode->Monsters;
    TArray<AActor*> Fixtures;
    ON_SCOPE_EXIT
    {
        for (AActor* Fixture : Fixtures)
            if (auto* Area = Cast<ACireAreaEffect>(Fixture); IsValid(Area)) Area->Destroy();
        Mode->Clock = SavedClock; Mode->Heroes = SavedHeroes; Mode->Monsters = SavedMonsters;
        for (AActor* Fixture : Fixtures) if (IsValid(Fixture)) Fixture->Destroy();
    };
    Mode->Clock = Cires::MatchClock({60, 90, 15});
    FActorSpawnParameters Params;
    Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    const FVector Ground(23000, 19000, 2000);
    const auto MakeHero = [&](int32 Team, FVector Position)
    {
        auto* Hero = Mode->GetWorld()->SpawnActor<ACireHero>(ACireHero::StaticClass(), Position, FRotator::ZeroRotator, Params);
        if (Hero) { Fixtures.Add(Hero); Hero->TeamId = Team; Hero->Draft(0); Hero->SetActorTickEnabled(false); }
        return Hero;
    };
    auto* Source = MakeHero(0, Ground + FVector(600, 0, 92));
    auto* OtherTeam = MakeHero(1, Ground + FVector(0, 0, 92));
    auto* Victim = Mode->GetWorld()->SpawnActor<ACireMonster>(ACireMonster::StaticClass(), Ground, FRotator::ZeroRotator, Params);
    if (Victim) Fixtures.Add(Victim);
    if (!Source || !OtherTeam || !Victim) { Tests.Check(false, TEXT("lifecycle fixture actors spawned")); return Tests.Finish(TEXT("LIFECYCLE")); }
    Victim->SetActorTickEnabled(false);
    const FVector Inside = Ground + FVector(0, 0, Victim->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
    Victim->SetActorLocation(Inside, false, nullptr, ETeleportType::TeleportPhysics);
    Victim->Lane = 0; Victim->Health = Victim->MaxHealth = 1000;
    Mode->Heroes = {Source, OtherTeam}; Mode->Monsters = {Victim};
    FCireAreaSpec S;
    S.WarningSeconds = 0; S.DurationSeconds = 3; S.TickInterval = .25f; S.DamagePerSecond = 40;
    auto MakeArea = [&](const FCireAreaSpec& Spec)
    {
        auto* Area = ACireAreaEffect::Spawn(Source, Spec, Ground, FRotator::ZeroRotator);
        if (Area) Fixtures.Add(Area);
        return Area;
    };
    auto* A = MakeArea(S); auto* B = MakeArea(S);
    if (!A || !B) { Tests.Check(false, TEXT("lifecycle areas spawned")); return Tests.Finish(TEXT("LIFECYCLE")); }
    Tests.Check(Victim->PoisonAreaCount == 2, TEXT("two overlapping zones add independent poison memberships immediately"));
    Tests.Check(A->CanObserve(Source) && !A->CanObserve(OtherTeam), TEXT("PvE area restricted to own realm"));
    Tests.Check(OtherTeam->PoisonAreaCount == 0, TEXT("other realm hero cannot receive poison"));
    A->Tick(.25f); B->Tick(.25f);
    Tests.Check(FMath::IsNearlyEqual(Victim->Health, 980.f) && FMath::IsNearlyEqual(Source->DamageDone, 20.f), TEXT("overlap damage is authoritative and contributes exactly to meter"));
    A->Destroy();
    Tests.Check(Victim->PoisonAreaCount == 1, TEXT("destroying one area preserves the other poison membership"));
    Victim->SetActorLocation(Inside + FVector(500, 0, 0), false, nullptr, ETeleportType::TeleportPhysics);
    B->Tick(.01f);
    Tests.Check(Victim->PoisonAreaCount == 0 && B->GetOccupantCount() == 0, TEXT("exit removes poison before next damage pulse"));
    const float AfterExit = Victim->Health;
    B->Tick(.5f);
    Tests.Check(FMath::IsNearlyEqual(AfterExit, Victim->Health), TEXT("no poison damage after exit"));
    Victim->SetActorLocation(Inside + FVector(0, 0, S.VerticalTolerance + 1), false, nullptr, ETeleportType::TeleportPhysics);
    B->Tick(.01f);
    Tests.Check(Victim->PoisonAreaCount == 0, TEXT("same XY on another floor never enters"));
    Victim->SetActorLocation(Inside, false, nullptr, ETeleportType::TeleportPhysics);
    B->Tick(.01f);
    Tests.Check(Victim->PoisonAreaCount == 1, TEXT("reentry reapplies status once"));
    B->Tick(.01f);
    Tests.Check(Victim->PoisonAreaCount == 1, TEXT("remaining inside does not stack its own membership"));
    B->Tick(3.f);
    Tests.Check(B->IsActorBeingDestroyed() && Victim->PoisonAreaCount == 0, TEXT("expiry clears status"));
    S.WarningSeconds = 1;
    auto* Warning = MakeArea(S);
    if (!Warning) { Tests.Check(false, TEXT("warning area spawned")); return Tests.Finish(TEXT("LIFECYCLE")); }
    const float BeforeWarning = Victim->Health;
    Warning->Tick(.75f);
    Tests.Check(!Warning->IsActive() && Victim->PoisonAreaCount == 0 && FMath::IsNearlyEqual(Victim->Health, BeforeWarning), TEXT("warning is harmless"));
    Warning->Tick(.25f);
    Tests.Check(Warning->IsActive() && Victim->PoisonAreaCount == 1, TEXT("warning activates poison at deadline"));
    Warning->Destroy();
    S.bPersistent = false; S.BurstDamage = 35;
    auto* Burst = MakeArea(S);
    if (!Burst) { Tests.Check(false, TEXT("burst area spawned")); return Tests.Finish(TEXT("LIFECYCLE")); }
    const float BeforeBurst = Victim->Health;
    Burst->Tick(1.f); Burst->Tick(.1f);
    Tests.Check(FMath::IsNearlyEqual(Victim->Health, BeforeBurst - 35.f) && Victim->PoisonAreaCount == 0, TEXT("delayed burst damages once without persistent status"));
    Burst->Destroy();
    S.bPersistent = true; S.WarningSeconds = 0;
    auto* PhaseArea = MakeArea(S);
    if (!PhaseArea) { Tests.Check(false, TEXT("phase area spawned")); return Tests.Finish(TEXT("LIFECYCLE")); }
    Mode->Clock.BeginIntermission();
    PhaseArea->Tick(0);
    Tests.Check(PhaseArea->IsActorBeingDestroyed() && Victim->PoisonAreaCount == 0 && !PhaseArea->CanObserve(Source), TEXT("phase change clears status and stale visuals"));
    Mode->Clock.Advance(60);
    auto* ArenaArea = MakeArea(S);
    if (!ArenaArea) { Tests.Check(false, TEXT("arena area spawned")); return Tests.Finish(TEXT("LIFECYCLE")); }
    Tests.Check(ArenaArea->CanObserve(Source) && ArenaArea->CanObserve(OtherTeam) && OtherTeam->PoisonAreaCount == 1 && Victim->PoisonAreaCount == 0,
        TEXT("arena shared visibility and hostile hero filtering"));
    ACireAreaEffect::ClearForActor(OtherTeam);
    Tests.Check(OtherTeam->PoisonAreaCount == 0, TEXT("death or teleport hook clears membership immediately"));
    ArenaArea->Tick(0);
    Source->bDead = true;
    ArenaArea->Tick(0);
    Tests.Check(ArenaArea->IsActorBeingDestroyed() && OtherTeam->PoisonAreaCount == 0, TEXT("source death cancels area and all memberships"));
    return Tests.Finish(TEXT("LIFECYCLE"));
}
#endif
