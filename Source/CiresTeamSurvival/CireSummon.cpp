#include "CireSummon.h"
#include "CireDeveloperTools.h"
#include "CireAreaEffects.h"
#include "CireConstruct.h"
#include "CireSkillshot.h"
#include "CireSkillRuntime.h"
#include "CireCombatEvents.h"
#include "CireThreat.h"
#include "Components/CapsuleComponent.h"
#include "Engine/OverlapResult.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Net/UnrealNetwork.h"

ACireSummon::ACireSummon() { bBot = false; bAutoAttack = false; Gold = 0; }
bool ACireSummon::ValidateSpec(const FCireSummonSpec& S)
{
    auto In = [](float V, float Min, float Max) { return FMath::IsFinite(V) && V >= Min && V <= Max; };
    return S.Count >= 1 && S.Count <= 3 && S.ArchetypeVisual >= 0 && S.ArchetypeVisual <= 3 &&
        In(S.Health, 1, 100000) && In(S.Damage, 0, 10000) && In(S.DurationSeconds, .1f, 300) &&
        In(S.ManaCost, 0, 10000) && In(S.EnergyCost, 0, 100) && In(S.CooldownSeconds, 0, 300) &&
        In(S.CastRange, 0, 3000) && In(S.LeashRange, 100, 5000) && In(S.MoveSpeed, 50, 1500) && In(S.AttackRange, 50, 2000);
}
TArray<ACireSummon*> ACireSummon::SpawnGroup(ACireHero* Source, const FCireSummonSpec& InputSpec, AActor* TargetActor, FVector Point)
{
    FCireSummonSpec Spec=InputSpec;if(Source)CireDeveloperTools::AdjustSummon(Source->GetWorld(),Spec);
    TArray<ACireSummon*> Result;
    if (!CireSkillRuntime::Alive(Source) || !Source->HasAuthority() || !ValidateSpec(Spec) || Point.ContainsNaN() ||
        (!Spec.bCommandable && !CireCombat::AreHostile(Source, TargetActor))) return Result;
    UWorld* World = Source->GetWorld();
    auto* Mode = World->GetAuthGameMode<ACireGameMode>();
    if (!Mode || !Mode->IsCombatPhase() || FVector::DistSquared2D(Source->GetActorLocation(), Point) > FMath::Square(Spec.CastRange)) return Result;
    int32 Owned = 0, Total = 0;
    for (TActorIterator<ACireSummon> It(World); It; ++It)
        if (!It->IsActorBeingDestroyed()) { ++Total; if (It->OwnerHero == Source) ++Owned; }
    if (Owned + Spec.Count > 6 || Total + Spec.Count > 48) return Result;
    TArray<FVector> Positions;
    FCollisionQueryParams Params(SCENE_QUERY_STAT(CireSummonPlacement), false);
    FCollisionObjectQueryParams GroundObjects(ECC_WorldStatic), Objects;
    Objects.AddObjectTypesToQuery(ECC_WorldStatic); Objects.AddObjectTypesToQuery(ECC_WorldDynamic); Objects.AddObjectTypesToQuery(ECC_Pawn);
    // Resolve every position first, so a blocked pack cast creates no partial pack.
    for (int32 Attempt = 0; Attempt < 25 && Positions.Num() < Spec.Count; ++Attempt)
    {
        const float Angle = Attempt * 2.399963f;
        const float Radius = Attempt == 0 ? 0 : 100.f + 25.f * FMath::Sqrt(static_cast<float>(Attempt));
        FVector P = Point + FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0) * Radius;
        if (!CireSkillRuntime::InRealmBounds(Mode, Source->TeamId, P, 45) || FVector::DistSquared2D(Source->GetActorLocation(), P) > FMath::Square(Spec.CastRange + 250.f)) continue;
        FHitResult Floor;
        if (!World->LineTraceSingleByObjectType(Floor, P + FVector(0, 0, 250), P - FVector(0, 0, 500), GroundObjects, Params) || Floor.ImpactNormal.Z < .8f) continue;
        P.Z = Floor.ImpactPoint.Z + 94.f;
        bool bBlocked = false;
        for (FVector Other : Positions) if (FVector::DistSquared2D(P, Other) < FMath::Square(90.f)) bBlocked = true;
        TArray<FOverlapResult> Overlaps;
        World->OverlapMultiByObjectType(Overlaps, P, FQuat::Identity, Objects, FCollisionShape::MakeCapsule(40.f, 91.f), Params);
        for (const auto& Hit : Overlaps)
            if (::Cast<ACharacter>(Hit.GetActor()) || ::Cast<ACireConstruct>(Hit.GetActor()) ||
                (Hit.GetComponent() && Hit.GetComponent()->GetCollisionResponseToChannel(ECC_Pawn) == ECR_Block)) bBlocked = true;
        if (!bBlocked) Positions.Add(P);
    }
    if (Positions.Num() != Spec.Count) return Result;
    for (FVector P : Positions)
    {
        const FTransform Transform(Source->GetActorRotation(), P);
        auto* Unit = World->SpawnActorDeferred<ACireSummon>(StaticClass(), Transform, Source, Source, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        if (!Unit)
        {
            for (auto* Created : Result) Created->Destroy();
            Result.Reset(); return Result;
        }
        Unit->OwnerHero = Source; Unit->TeamId = Source->TeamId; Unit->SummonSpec = Spec; Unit->bCommandable = Spec.bCommandable;
        Unit->OriginPhase = CireSkillRuntime::Phase(World); Unit->ExpiresServerTime = World->GetTimeSeconds() + Spec.DurationSeconds;
        Unit->Draft(Spec.ArchetypeVisual);
        Unit->HeroName = Spec.bCommandable ? TEXT("Oathbound Guardian") : TEXT("Spectral Companion");
        Unit->MaxHealth = Unit->Health = Spec.Health;
        Unit->Gold = 0; Unit->Skills.Reset(); Unit->Offers.Reset(); Unit->Cooldowns.Reset();
        Unit->Target = CireCombat::AreHostile(Source, TargetActor) ? TargetActor : nullptr;
        Unit->CurrentCommand = Unit->Target ? ECireSummonCommand::Attack : ECireSummonCommand::Follow;
        Unit->MoveDestination = Point;
        Unit->FinishSpawning(Transform);
        Result.Add(Unit);
    }
    return Result;
}
bool ACireSummon::Command(ECireSummonCommand NewCommand, FVector Point, AActor* NewTarget)
{
    if (!HasAuthority() || !bCommandable || bDead || !CireSkillRuntime::Alive(OwnerHero) || Point.ContainsNaN() ||
        static_cast<uint8>(NewCommand) > static_cast<uint8>(ECireSummonCommand::Hold)) return false;
    auto* Mode = GetWorld()->GetAuthGameMode<ACireGameMode>();
    if (!Mode || !Mode->IsCombatPhase() || CireSkillRuntime::Phase(GetWorld()) != OriginPhase) return false;
    if (NewCommand == ECireSummonCommand::Attack)
    {
        if (!CireCombat::AreHostile(this, NewTarget) || FVector::DistSquared2D(OwnerHero->GetActorLocation(), NewTarget->GetActorLocation()) > FMath::Square(SummonSpec.LeashRange)) return false;
    }
    if (NewCommand == ECireSummonCommand::Move && (!CireSkillRuntime::InRealmBounds(Mode, TeamId, Point, 45) ||
        FVector::DistSquared2D(OwnerHero->GetActorLocation(), Point) > FMath::Square(SummonSpec.LeashRange))) return false;
    CurrentCommand = NewCommand; Target = NewCommand == ECireSummonCommand::Attack ? NewTarget : nullptr;
    MoveDestination = Point; bAutoAttack = false; PendingAttackTarget.Reset();
    GetCharacterMovement()->StopMovementImmediately(); ForceNetUpdate(); return true;
}
void ACireSummon::Tick(float Delta)
{
    if (HasAuthority())
    {
        Age += FMath::Max(0.f, Delta);
        if (bDead || !CireSkillRuntime::Alive(OwnerHero) || OwnerHero->TeamId != TeamId || CireSkillRuntime::Phase(GetWorld()) != OriginPhase || Age >= SummonSpec.DurationSeconds)
        { Destroy(); return; }
    }
    // Parent updates presentation and pending attack release, but never controls
    // this unit's decisions or a dead summon (which must not enter hero revival).
    bBot = false; bAutoAttack = false;
    Super::Tick(Delta);
    GetCharacterMovement()->MaxWalkSpeed = SummonSpec.MoveSpeed * (SlowUntil > GetWorld()->GetTimeSeconds() ? .65f : 1.f);
    if (!HasAuthority()) return;
    if (FVector::DistSquared2D(GetActorLocation(), OwnerHero->GetActorLocation()) > FMath::Square(SummonSpec.LeashRange))
    { CurrentCommand = ECireSummonCommand::Follow; Target = nullptr; PendingAttackTarget.Reset(); }
    if (CurrentCommand == ECireSummonCommand::Attack && (!CireCombat::AreHostile(this, Target) ||
        FVector::DistSquared2D(OwnerHero->GetActorLocation(), Target->GetActorLocation()) > FMath::Square(SummonSpec.LeashRange)))
    { CurrentCommand = ECireSummonCommand::Follow; Target = nullptr; PendingAttackTarget.Reset(); }
    if (!bCommandable && CurrentCommand != ECireSummonCommand::Attack && CireCombat::AreHostile(this, OwnerHero->Target) &&
        FVector::DistSquared2D(OwnerHero->GetActorLocation(), OwnerHero->Target->GetActorLocation()) <= FMath::Square(SummonSpec.LeashRange))
    { CurrentCommand = ECireSummonCommand::Attack; Target = OwnerHero->Target; }
    FVector Destination = GetActorLocation(); float StopRange = 70.f;
    if (CurrentCommand == ECireSummonCommand::Follow) { Destination = OwnerHero->GetActorLocation(); StopRange = 170.f; }
    else if (CurrentCommand == ECireSummonCommand::Move) Destination = MoveDestination;
    else if (CurrentCommand == ECireSummonCommand::Attack && IsValid(Target)) { Destination = Target->GetActorLocation(); StopRange = SummonSpec.AttackRange * .85f; }
    if (auto* Wall = ACireConstruct::FindBlockingConstruct(this, Destination); Wall && Wall->CanBeDamagedBy(this))
    { Target = Wall; CurrentCommand = ECireSummonCommand::Attack; Destination = Wall->GetActorLocation(); StopRange = SummonSpec.AttackRange * .85f; }
    const FVector Direction = (Destination - GetActorLocation()).GetSafeNormal2D();
    if (FVector::DistSquared2D(Destination, GetActorLocation()) > FMath::Square(StopRange)) AddMovementInput(Direction);
    else
    {
        GetCharacterMovement()->StopMovementImmediately();
        if (CurrentCommand == ECireSummonCommand::Move) CurrentCommand = ECireSummonCommand::Hold;
        if (CurrentCommand == ECireSummonCommand::Attack && IsValid(Target))
        {
            SetActorRotation(Direction.Rotation());
            const uint32 PreviousSerial = AttackSerial;
            BasicAttack();
            if (AttackSerial != PreviousSerial) PendingAttackDamage = SummonSpec.Damage;
        }
    }
}
float ACireSummon::TakeDamage(float Amount, const FDamageEvent& Event, AController*, AActor* Causer)
{
    if (!HasAuthority() || bDead || !FMath::IsFinite(Amount) || Amount <= 0 || !CireCombat::AreHostile(Causer, this)) return 0;
    if (ShieldUntil > GetWorld()->GetTimeSeconds()) Amount *= .6f;
    const float Applied = FMath::Min(Health, Amount); Health -= Applied;
    CireCombat::BroadcastDamage(Causer, this, Applied, Event); ForceNetUpdate();
    if (Health <= 0)
    {
        bDead = true; PendingAttackTarget.Reset(); Target = nullptr; bAutoAttack = false;
        GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Destroy();
    }
    return Applied;
}
void ACireSummon::FellOutOfWorld(const UDamageType&) { if (HasAuthority()) Destroy(); }
void ACireSummon::EndPlay(const EEndPlayReason::Type Reason)
{
    PendingAttackTarget.Reset();
    if (HasAuthority()) { CireThreat::Remove(this); ACireAreaEffect::ClearForActor(this); ACireSkillshot::ClearForActor(this); ACireConstruct::ClearForActor(this); }
    Super::EndPlay(Reason);
}
void ACireSummon::ClearAll(UWorld* World) { if (World) for (TActorIterator<ACireSummon> It(World); It; ++It) if (It->HasAuthority()) It->Destroy(); }
void ACireSummon::ClearForActor(AActor* Actor) { if (IsValid(Actor)) for (TActorIterator<ACireSummon> It(Actor->GetWorld()); It; ++It) if (It->HasAuthority() && It->OwnerHero == Actor) It->Destroy(); }
void ACireSummon::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(ACireSummon, OwnerHero); DOREPLIFETIME(ACireSummon, SummonSpec); DOREPLIFETIME(ACireSummon, bCommandable);
    DOREPLIFETIME(ACireSummon, CurrentCommand); DOREPLIFETIME(ACireSummon, MoveDestination); DOREPLIFETIME(ACireSummon, ExpiresServerTime); DOREPLIFETIME(ACireSummon, OriginPhase);
}
