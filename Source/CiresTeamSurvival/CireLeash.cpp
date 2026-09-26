// layout-wiring: the wave leash / snap-back. See CireLeash.h.
#include "CireLeash.h"
#include "CireActorIterator.h" // town-perf: fast actor iteration in editor-binary -game
#include "CireGame.h"
#include "CireLanePath.h"
#include "CireNav.h"
#include "CireNPCCombat.h"
#include "CireNPCState.h"
#include "CireRaces.h"
#include "CireThreat.h"
#include "CireWaves.h"
#include "Components/CapsuleComponent.h"
#include "Dom/JsonObject.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HAL/IConsoleManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

DEFINE_LOG_CATEGORY_STATIC(LogCireLeash, Log, All);

namespace
{
FCireLeashRules GRules;
bool bRulesLoaded = false;
int32 GReturns = 0, GArrivals = 0, GRescues = 0;
FString DataPath() { return FPaths::ProjectContentDir() / TEXT("Data/MonsterLeash.json"); }
float NowOf(const UObject* O) { return O && O->GetWorld() ? O->GetWorld()->GetTimeSeconds() : 0.f; }
FVector2D LocalOf(const ACireMonster* M) { return CireLanePath::ToLocal(FMath::Clamp(M->Lane, 0, 1), M->GetActorLocation()); }
FVector WorldAt(const ACireMonster* M, const FVector2D& Local)
{
    const FVector2D O = CireLanePath::RealmOrigin(FMath::Clamp(M->Lane, 0, 1));
    return FVector(Local.X + O.X, Local.Y + O.Y, M->GetActorLocation().Z);
}
void SetState(ACireMonster* M, ECireLeashState S)
{
    if (M->LeashState != static_cast<uint8>(S)) { M->LeashState = static_cast<uint8>(S); M->ForceNetUpdate(); }
}
void BeginReturn(ACireMonster* M, const FCireLeashRules& R, float PathDistance, const TCHAR* Why)
{
    const FVector2D Here = LocalOf(M);
    const FVector2D Nearest = CireLanePath::NearestOnPolyline(CireLanePath::UnitPath(M), Here);
    M->LeashReturnPoint = R.bReturnToAnchor && M->bLeashAnchored ? M->LeashAnchor : Nearest;
    M->LeashReturnStarted = NowOf(M);
    M->LeashReturnBest = static_cast<float>(FVector2D::Distance(Here, M->LeashReturnPoint)); M->LeashReturnBestAt = M->LeashReturnStarted;
    ++M->LeashReturns; ++GReturns;
    // Disengage: drop the current target and any cast, keep the threat table (threat is only lost on death).
    CireNPCCombat::Interrupt(M);
    if (M->Victim) { M->Victim = nullptr; }
    M->bEngaged = false;
    CireNav::Forget(M);
    if (R.bUntargetableWhileReturning)
        for (TCireActorIterator<ACireHero> It(M->GetWorld()); It; ++It) if (It->Target == M) It->Target = nullptr;
    SetState(M, ECireLeashState::Return);
    UE_LOG(LogCireLeash, Display, TEXT("CIRE_LEASH_RETURN %s lane=%d path=%d why=%s off_path=%.0f radius=%.0f to=(%.0f,%.0f) threat_holders=%d"), *M->GetNPCDisplayName(), M->Lane,
        M->LanePath, Why, PathDistance, CireLeash::RadiusFor(R, M), M->LeashReturnPoint.X, M->LeashReturnPoint.Y, M->Threat.Num());
}
void EndReturn(ACireMonster* M, const FCireLeashRules& R, const TCHAR* How)
{
    if (R.bHealOnArrive) M->Health = M->MaxHealth;
    M->bLeashAnchored = false;
    M->LeashReengageAt = NowOf(M) + R.ReengageSeconds;
    SetState(M, ECireLeashState::March);
    ++GArrivals;
    CireNav::Forget(M);
    CireLanePath::InitializeProgress(M); // resume from the return point (progress never moves backward)
    M->ForceNetUpdate();
    UE_LOG(LogCireLeash, Display, TEXT("CIRE_LEASH_RESUMED %s lane=%d path=%d how=%s at=(%.0f,%.0f) health=%.0f/%.0f threat_holders=%d"), *M->GetNPCDisplayName(), M->Lane, M->LanePath, How,
        LocalOf(M).X, LocalOf(M).Y, M->Health, M->MaxHealth, M->Threat.Num());
}
bool Num(const TSharedPtr<FJsonObject>& O, const TCHAR* Key, float& Out, float Min, float Max, FString& Error)
{
    if (!O->HasField(Key)) return true;
    double V = 0;
    if (!O->TryGetNumberField(Key, V) || !FMath::IsFinite(V) || V < Min || V > Max) { Error = FString::Printf(TEXT("%s must be %g..%g"), Key, Min, Max); return false; }
    Out = static_cast<float>(V); return true;
}
bool Flag(const TSharedPtr<FJsonObject>& O, const TCHAR* Key, bool& Out, FString& Error)
{
    if (!O->HasField(Key)) return true;
    if (!O->TryGetBoolField(Key, Out)) { Error = FString::Printf(TEXT("%s must be true or false"), Key); return false; }
    return true;
}
}

bool CireLeash::ParseJson(const FString& Json, FCireLeashRules& Out, FString& Error)
{
    TSharedPtr<FJsonObject> Root;
    if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Root) || !Root) { Error = TEXT("MonsterLeash.json is not valid JSON"); return false; }
    double Schema = 0;
    if (!Root->TryGetNumberField(TEXT("schemaVersion"), Schema) || Schema != 1) { Error = TEXT("MonsterLeash.json needs schemaVersion 1"); return false; }
    FCireLeashRules R;
    if (!Flag(Root, TEXT("enabled"), R.bEnabled, Error)) return false;
    if (const TSharedPtr<FJsonObject>* Radius = nullptr; Root->TryGetObjectField(TEXT("radius"), Radius) && Radius)
    {
        if (!Num(*Radius, TEXT("normal"), R.RadiusNormal, 500, 20000, Error) || !Num(*Radius, TEXT("elite"), R.RadiusElite, 500, 20000, Error) ||
            !Num(*Radius, TEXT("boss"), R.RadiusBoss, 500, 20000, Error)) return false;
    }
    else if (Root->HasField(TEXT("radius"))) { Error = TEXT("radius is { normal, elite, boss } in cm"); return false; }
    FString ReturnTo;
    if (Root->TryGetStringField(TEXT("returnTo"), ReturnTo))
    {
        if (ReturnTo != TEXT("anchor") && ReturnTo != TEXT("nearest")) { Error = TEXT("returnTo must be anchor or nearest"); return false; }
        R.bReturnToAnchor = ReturnTo == TEXT("anchor");
    }
    if (!Num(Root, TEXT("pursuitMargin"), R.PursuitMargin, 0, 5000, Error) || !Num(Root, TEXT("disengageDistance"), R.DisengageDistance, 100, 20000, Error) ||
        !Num(Root, TEXT("returnSpeedMultiplier"), R.ReturnSpeedMultiplier, .5f, 5.f, Error) || !Num(Root, TEXT("regenPerSecond"), R.RegenPerSecond, 0, 10, Error) ||
        !Num(Root, TEXT("resumeRadius"), R.ResumeRadius, 50, 2000, Error) || !Num(Root, TEXT("reengageSeconds"), R.ReengageSeconds, 0, 30, Error) ||
        !Num(Root, TEXT("maxReturnSeconds"), R.MaxReturnSeconds, 2, 120, Error) || !Num(Root, TEXT("stuckReturnSeconds"), R.StuckReturnSeconds, 1, 60, Error) ||
        !Flag(Root, TEXT("immuneWhileReturning"), R.bImmuneWhileReturning, Error) || !Flag(Root, TEXT("untargetableWhileReturning"), R.bUntargetableWhileReturning, Error) ||
        !Flag(Root, TEXT("healOnArrive"), R.bHealOnArrive, Error)) return false;
    if (R.PursuitMargin >= FMath::Min3(R.RadiusNormal, R.RadiusElite, R.RadiusBoss)) { Error = TEXT("pursuitMargin must be smaller than every leash radius"); return false; }
    if (R.DisengageDistance > R.RadiusNormal) { Error = TEXT("disengageDistance must not exceed the normal leash radius"); return false; }
    if (R.bUntargetableWhileReturning) R.bImmuneWhileReturning = true;
    Out = R; Error.Reset(); return true;
}
bool CireLeash::Reload(FString* Error)
{
    bRulesLoaded = true;
    FString Json, Why; FCireLeashRules Parsed;
    if (!FFileHelper::LoadFileToString(Json, *DataPath())) { GRules = FCireLeashRules(); if (Error) *Error = TEXT("MonsterLeash.json is missing; built-in defaults"); return false; }
    if (!ParseJson(Json, Parsed, Why)) { UE_LOG(LogCireLeash, Warning, TEXT("CIRE_LEASH_DEFAULTS %s"), *Why); GRules = FCireLeashRules(); if (Error) *Error = Why; return false; }
    GRules = Parsed;
    UE_LOG(LogCireLeash, Display, TEXT("CIRE_LEASH_READY enabled=%d radius=%.0f/%.0f/%.0f return=%s x%.2f immune=%d regen=%.2f/s"), GRules.bEnabled ? 1 : 0, GRules.RadiusNormal,
        GRules.RadiusElite, GRules.RadiusBoss, GRules.bReturnToAnchor ? TEXT("anchor") : TEXT("nearest"), GRules.ReturnSpeedMultiplier, GRules.bImmuneWhileReturning ? 1 : 0, GRules.RegenPerSecond);
    if (Error) Error->Reset();
    return true;
}
const FCireLeashRules& CireLeash::Rules() { if (!bRulesLoaded) Reload(); return GRules; }

ECireLeashState CireLeash::Step(const FCireLeashRules& R, const FCireLeashInput& In)
{
    switch (In.State)
    {
    case ECireLeashState::March:
        // A marching unit without a target is never leashed, however far off its path it stands: that is a stuck or
        // displaced unit and the director's rescue (nudge / repath) owns it.
        return In.bHasTarget && In.bTargetInZone ? ECireLeashState::Chase : ECireLeashState::March;
    case ECireLeashState::Chase:
        if (In.PathDistance > In.Radius) return ECireLeashState::Return; // kited past the leash
        if (!In.bHasTarget || !In.bTargetInZone) return In.PathDistance > R.DisengageDistance ? ECireLeashState::Return : ECireLeashState::March;
        return ECireLeashState::Chase; // still fighting inside the zone (stuck or not: the repath rescue owns a stuck chaser)
    case ECireLeashState::Return:
        return In.DistanceToReturn <= R.ResumeRadius ? ECireLeashState::March : ECireLeashState::Return;
    }
    return ECireLeashState::March;
}

bool CireLeash::Applies(const ACireMonster* M)
{
    return IsValid(M) && M->bPathLeash && Rules().bEnabled && M->PackId < 0 && M->Lane >= 0 && M->Lane < 2 && !M->bArmoredEscort && M->SpecialSpawn != 2 &&
        !CireWaveDirector::IsForcedMarch(M) && M->Health > 0;
}
float CireLeash::RadiusFor(const FCireLeashRules& R, const ACireMonster* M)
{
    if (!M) return R.RadiusNormal;
    const ECireNPCRank Rank = CireRaces::RankOf(M);
    if (M->bBoss || M->GetNPCClassification() == ECireNPCClass::Boss || Rank >= ECireNPCRank::Warlord) return R.RadiusBoss;
    if (Rank >= ECireNPCRank::Elite || M->GetNPCClassification() == ECireNPCClass::Elite) return R.RadiusElite;
    return R.RadiusNormal;
}
bool CireLeash::CanPursue(const ACireMonster* M, const ACireHero* H)
{
    if (!Applies(M) || !H) return true;
    if (M->LeashState == static_cast<uint8>(ECireLeashState::Return) || NowOf(M) < M->LeashReengageAt) return false;
    const FCireLeashRules& R = Rules();
    return CireLanePath::DistanceToUnitPath(M, H->GetActorLocation()) <= RadiusFor(R, M) - R.PursuitMargin;
}
bool CireLeash::IsReturning(const ACireMonster* M) { return IsValid(M) && M->LeashState == static_cast<uint8>(ECireLeashState::Return); }
bool CireLeash::AllowDamage(const ACireMonster* M) { return !(IsReturning(M) && Rules().bImmuneWhileReturning); }

bool CireLeash::Tick(ACireMonster* M, float Delta)
{
    if (!IsValid(M) || !M->HasAuthority()) return false;
    if (!Applies(M))
    {
        if (M->LeashState != 0) { M->bLeashAnchored = false; SetState(M, ECireLeashState::March); }
        return false;
    }
    const FCireLeashRules& R = Rules();
    const float Now = NowOf(M);
    const FVector2D Here = LocalOf(M);
    double PathDistance = 0;
    const FVector2D OnPath = CireLanePath::NearestOnPolyline(CireLanePath::UnitPath(M), Here, &PathDistance);
    FCireLeashInput In;
    In.State = static_cast<ECireLeashState>(FMath::Min<uint8>(M->LeashState, 2));
    In.PathDistance = static_cast<float>(PathDistance);
    In.bHasTarget = IsValid(M->Victim) && !M->Victim->bDead && M->Victim->Health > 0;
    In.bTargetInZone = In.bHasTarget && CireLanePath::DistanceToUnitPath(M, M->Victim->GetActorLocation()) <= RadiusFor(R, M) - R.PursuitMargin;
    In.Radius = RadiusFor(R, M);
    In.DistanceToReturn = static_cast<float>(FVector2D::Distance(Here, M->LeashReturnPoint));
    const ECireLeashState Next = Step(R, In);
    if (In.State == ECireLeashState::March && Next == ECireLeashState::Chase)
    {
        // The anchor: where the unit left its path to fight.
        M->LeashAnchor = OnPath; M->bLeashAnchored = true;
        SetState(M, ECireLeashState::Chase);
        return false;
    }
    if (In.State == ECireLeashState::Chase && Next == ECireLeashState::March) { M->bLeashAnchored = false; SetState(M, ECireLeashState::March); return false; }
    if (In.State == ECireLeashState::Chase && Next == ECireLeashState::Return)
        BeginReturn(M, R, In.PathDistance, In.PathDistance > In.Radius ? TEXT("kited") : TEXT("target_left_zone"));
    else if (In.State == ECireLeashState::Return && Next == ECireLeashState::March) { EndReturn(M, R, TEXT("walked")); return false; }
    if (M->LeashState != static_cast<uint8>(ECireLeashState::Return)) return false;
    // Returning: walk home fast, evading (no targets, optional immunity), regenerating.
    if (M->Victim) M->Victim = nullptr;
    M->bEngaged = false;
    if (auto* Movement = M->GetCharacterMovement()) Movement->MaxWalkSpeed = FMath::Max(M->BaseMoveSpeed, 1.f) * R.ReturnSpeedMultiplier;
    if (R.RegenPerSecond > 0 && M->Health < M->MaxHealth && FMath::IsFinite(Delta) && Delta > 0)
        M->Health = FMath::Min(M->MaxHealth, M->Health + M->MaxHealth * R.RegenPerSecond * Delta);
    const float Distance = static_cast<float>(FVector2D::Distance(Here, M->LeashReturnPoint));
    if (Distance < M->LeashReturnBest - 50.f) { M->LeashReturnBest = Distance; M->LeashReturnBestAt = Now; }
    if (Now - M->LeashReturnStarted > R.MaxReturnSeconds || Now - M->LeashReturnBestAt > R.StuckReturnSeconds) { Rescue(M); return true; }
    M->AddMovementInput(CireNav::Steer(M, WorldAt(M, M->LeashReturnPoint)));
    return true;
}
void CireLeash::Rescue(ACireMonster* M)
{
    if (!IsValid(M) || !M->HasAuthority() || !IsReturning(M)) return;
    // The existing stuck rescue, applied to a return: set the unit down at its return point, on the ground.
    FVector Target = CireLanePath::ToWorld(FMath::Clamp(M->Lane, 0, 1), M->LeashReturnPoint, 0.f);
    Target.Z += M->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 10.f;
    if (Target.Z < M->GetActorLocation().Z - 5000.f || !FMath::IsFinite(Target.Z)) Target.Z = M->GetActorLocation().Z;
    M->GetCharacterMovement()->StopMovementImmediately();
    M->SetActorLocation(Target, false, nullptr, ETeleportType::TeleportPhysics);
    ++GRescues;
    UE_LOG(LogCireLeash, Display, TEXT("CIRE_LEASH_RESCUE %s lane=%d (stuck on the way back) set down at (%.0f,%.0f)"), *M->GetNPCDisplayName(), M->Lane, M->LeashReturnPoint.X, M->LeashReturnPoint.Y);
    EndReturn(M, Rules(), TEXT("rescued"));
}
void CireLeash::Counts(int32& Returns, int32& Arrivals, int32& Rescues) { Returns = GReturns; Arrivals = GArrivals; Rescues = GRescues; }

#if !UE_BUILD_SHIPPING
static FAutoConsoleCommand LeashReloadCommand(TEXT("cire.Leash"), TEXT("cire.Leash reload: re-read Content/Data/MonsterLeash.json."),
    FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args) { FString Error; CireLeash::Reload(&Error); if (!Error.IsEmpty()) UE_LOG(LogCireLeash, Warning, TEXT("%s"), *Error); }));

bool CireLeash::RunTests(ACireGameMode* Mode)
{
    if (!IsValid(Mode) || !Mode->HasAuthority()) return false;
    bool bPass = true; int32 Checks = 0;
    auto Check = [&](bool bValue, const FString& Why) { ++Checks; if (!bValue) { bPass = false; UE_LOG(LogCireLeash, Error, TEXT("CIRE_LEASH_CHECK_FAIL %s"), *Why); } };
    // ---- the state machine (pure) ----
    const FCireLeashRules R;
    FCireLeashInput In; In.Radius = 1800;
    In.State = ECireLeashState::March; In.PathDistance = 5000; In.bHasTarget = false;
    Check(Step(R, In) == ECireLeashState::March, TEXT("stuck != kited: a marcher far off its path without a target is never leashed (the stuck rescue owns it)"));
    In.PathDistance = 100; In.bHasTarget = true; In.bTargetInZone = true;
    Check(Step(R, In) == ECireLeashState::Chase, TEXT("a target inside the leash zone starts a chase"));
    In.bTargetInZone = false; Check(Step(R, In) == ECireLeashState::March, TEXT("a target outside the zone is not chased"));
    In.State = ECireLeashState::Chase; In.bTargetInZone = true; In.PathDistance = 1000;
    Check(Step(R, In) == ECireLeashState::Chase, TEXT("a chaser inside the radius keeps fighting, stuck or not (the repath rescue owns it)"));
    In.PathDistance = 1801; Check(Step(R, In) == ECireLeashState::Return, TEXT("kited past the radius: the unit gives up and returns"));
    In.PathDistance = 1000; In.bTargetInZone = false; Check(Step(R, In) == ECireLeashState::Return, TEXT("target left the zone while the unit stands off the path: it returns (evading)"));
    In.PathDistance = 300; Check(Step(R, In) == ECireLeashState::March, TEXT("target left the zone while the unit stands on its path: it simply marches on"));
    In.bHasTarget = false; In.PathDistance = 300; Check(Step(R, In) == ECireLeashState::March, TEXT("a chaser whose target died next to the path marches on"));
    In.State = ECireLeashState::Return; In.DistanceToReturn = 900; In.bHasTarget = true; In.bTargetInZone = true;
    Check(Step(R, In) == ECireLeashState::Return, TEXT("a returning unit ignores targets until it is home"));
    In.DistanceToReturn = 150; Check(Step(R, In) == ECireLeashState::March, TEXT("back at its return point it resumes the march"));
    // ---- data ----
    FCireLeashRules Parsed; FString Error;
    Check(ParseJson(TEXT("{\"schemaVersion\":1,\"radius\":{\"normal\":1500,\"elite\":2000,\"boss\":2600},\"returnTo\":\"nearest\",\"returnSpeedMultiplier\":2,\"immuneWhileReturning\":false,\"regenPerSecond\":0.5}"), Parsed, Error) &&
        Parsed.RadiusNormal == 1500 && Parsed.RadiusBoss == 2600 && !Parsed.bReturnToAnchor && Parsed.ReturnSpeedMultiplier == 2 && !Parsed.bImmuneWhileReturning && Parsed.RegenPerSecond == .5f,
        TEXT("MonsterLeash.json parses: ") + Error);
    Check(!ParseJson(TEXT("{\"schemaVersion\":1,\"radius\":{\"normal\":100}}"), Parsed, Error), TEXT("a radius under 5 m is rejected"));
    Check(!ParseJson(TEXT("{\"schemaVersion\":1,\"returnTo\":\"spawn\"}"), Parsed, Error), TEXT("an unknown return mode is rejected"));
    Check(!ParseJson(TEXT("{\"schemaVersion\":1,\"pursuitMargin\":1900}"), Parsed, Error), TEXT("a pursuit margin wider than a radius is rejected"));
    {
        FString Json; FCireLeashRules Shipped;
        Check(FFileHelper::LoadFileToString(Json, *DataPath()) && ParseJson(Json, Shipped, Error), TEXT("the shipped MonsterLeash.json parses: ") + Error);
    }
    // ---- in the world: a wave unit on the primary route of realm 0 ----
    UWorld* World = Mode->GetWorld();
    const FCireLeashRules Saved = GRules; const bool bSavedLoaded = bRulesLoaded;
    GRules = FCireLeashRules(); bRulesLoaded = true; // the built-in defaults for the fixture
    const TArray<FVector2D>& Path = CireLanePath::PathPoints(CireLanePath::Get(World), 0, 0);
    FActorSpawnParameters P; P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    const FVector2D A = Path.Num() > 2 ? Path[1] : FVector2D::ZeroVector, B = Path.Num() > 2 ? Path[2] : FVector2D(100, 0);
    const FVector2D Along = (B - A).GetSafeNormal(), Side(-Along.Y, Along.X);
    const FVector2D Mid = (A + B) * .5;
    auto World0 = [&](const FVector2D& L, float Z) { const FVector2D O = CireLanePath::RealmOrigin(0); return FVector(L.X + O.X, L.Y + O.Y, Z); };
    const float Z = CireLanePath::ToWorld(0, Mid, 0).Z + 120.f;
    auto* M = World->SpawnActor<ACireMonster>(World0(Mid, Z), FRotator::ZeroRotator, P);
    auto* Near = World->SpawnActor<ACireHero>(World0(Mid + Side * 900, Z), FRotator::ZeroRotator, P);
    auto* Far = World->SpawnActor<ACireHero>(World0(Mid + Side * 1950, Z), FRotator::ZeroRotator, P); // outside the zone, inside threat range
    if (!M || !Near || !Far) { Check(false, TEXT("leash fixture actors spawn")); }
    else
    {
        M->Lane = 0; M->SetActorTickEnabled(false); CireNPCCombat::ConfigureArchetype(M, TEXT("hollow_infantry"), 1, 0, 1, false);
        M->bPathLeash = true; M->LanePath = 0; CireLanePath::InitializeProgress(M);
        for (ACireHero* H : {Near, Far}) { H->TeamId = 0; H->Draft(0); H->SetActorTickEnabled(false); H->Health = H->MaxHealth = 1000; }
        Check(Applies(M) && RadiusFor(GRules, M) == GRules.RadiusNormal, TEXT("a director wave unit on a path is leashed with its rank's radius"));
        Check(CanPursue(M, Near) && !CanPursue(M, Far), TEXT("only heroes inside radius - margin of the path can be pursued"));
        // Threat is kept for a hero outside the zone, but the unit does not select (pursue) him.
        CireThreat::Engage(M, Far); CireThreat::Select(M);
        Check(M->Threat.Contains(Far) && M->Victim == nullptr, TEXT("a threat holder outside the zone keeps his threat but is not chased"));
        CireThreat::Engage(M, Near); CireThreat::Select(M);
        Check(M->Victim == Near, TEXT("a holder inside the zone is chased"));
        Tick(M, .05f);
        Check(M->LeashState == uint8(ECireLeashState::Chase) && M->bLeashAnchored && M->LeashAnchor.Equals(Mid, 5.), TEXT("the chase anchors at the path point it left"));
        // Kite: the unit is dragged past its radius.
        M->SetActorLocation(World0(Mid + Side * 2000, Z));
        Tick(M, .05f);
        Check(IsReturning(M) && M->Victim == nullptr && M->Threat.Contains(Near) && M->Threat.Contains(Far), TEXT("kited past the radius: it disengages and returns, threat table intact"));
        Check(M->LeashReturnPoint.Equals(Mid, 5.), TEXT("it returns to the point it left"));
        Check(!AllowDamage(M), TEXT("an evading unit is immune"));
        Check(!CanPursue(M, Near), TEXT("a returning unit pursues no one"));
        CireThreat::Select(M);
        Check(M->Victim == nullptr, TEXT("threat selection skips targets while returning"));
        M->Health = M->MaxHealth * .5f;
        const float Before = M->Health;
        Tick(M, 1.f);
        Check(M->Health > Before, TEXT("it regenerates on the way back"));
        Check(M->GetCharacterMovement()->MaxWalkSpeed >= M->BaseMoveSpeed * GRules.ReturnSpeedMultiplier - 1.f, TEXT("it returns at the return speed multiplier"));
        // Back home: resume the march; re-engage delay.
        M->SetActorLocation(World0(Mid + Side * 60, Z));
        Tick(M, .05f);
        Check(M->LeashState == uint8(ECireLeashState::March) && AllowDamage(M) && !CanPursue(M, Near), TEXT("home again: it marches on and ignores targets for the re-engage delay"));
        M->LeashReengageAt = 0;
        Check(CanPursue(M, Near) && M->Threat.Contains(Near), TEXT("after the delay the kiter's threat is still there to re-engage"));
        // Stuck != kited in the world: far off its path WITHOUT a target, the unit stays in March (the stuck rescue owns it).
        CireThreat::Clear(M); M->Victim = nullptr;
        M->SetActorLocation(World0(Mid + Side * 3000, Z));
        Tick(M, .05f);
        Check(M->LeashState == uint8(ECireLeashState::March), TEXT("a unit displaced far off its path without a target is not leashed"));
        // A stuck return is rescued: set down at its return point.
        M->SetActorLocation(World0(Mid, Z)); CireThreat::Engage(M, Near); CireThreat::Select(M); Tick(M, .05f);
        M->SetActorLocation(World0(Mid + Side * 2100, Z)); Tick(M, .05f);
        Check(IsReturning(M), TEXT("fixture: returning again"));
        M->LeashReturnBestAt = NowOf(M) - GRules.StuckReturnSeconds - 1.f;
        Tick(M, .05f);
        Check(!IsReturning(M) && FVector2D::Distance(LocalOf(M), Mid) < 50., TEXT("a return that makes no progress is rescued at its return point"));
        // Not leashed: packs, escorts and fixtures without the flag keep the old unlimited chase.
        M->bPathLeash = false; Check(!Applies(M) && CanPursue(M, Far), TEXT("units the director did not put on a path keep chasing (no leash)"));
        M->bPathLeash = true; M->PackId = 7; Check(!Applies(M), TEXT("challenge packs are not path-leashed")); M->PackId = -1;
    }
    for (AActor* Actor : TArray<AActor*>{M, Near, Far}) if (IsValid(Actor)) Actor->Destroy();
    GRules = Saved; bRulesLoaded = bSavedLoaded;
    UE_LOG(LogCireLeash, Display, TEXT("CIRE_LEASH_TESTS_%s checks=%d"), bPass ? TEXT("PASS") : TEXT("FAIL"), Checks);
    return bPass;
}
#endif
