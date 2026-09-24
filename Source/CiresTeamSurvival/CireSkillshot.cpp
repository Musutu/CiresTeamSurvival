#include "CireSkillshot.h"
#include "CireDeveloperTools.h"
#include "CireConstruct.h"
#include "CireCombatEvents.h"
#include "CireSkillRuntime.h"
#include "CireSpellPresentation.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Net/UnrealNetwork.h"

ACireSkillshot::ACireSkillshot()
{
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.TickGroup = TG_PostPhysics;
    bReplicates = true;
    SetReplicateMovement(true);
    SetNetUpdateFrequency(40);
    SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("SkillshotRoot")));
    FlightMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("SkillshotVisual"));
    FlightMesh->SetupAttachment(RootComponent);
    FlightMesh->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere")));
    FlightMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    FlightMesh->SetCanEverAffectNavigation(false);
    FlightMesh->SetCastShadow(false);
}
bool ACireSkillshot::ValidateSpec(const FCireSkillshotSpec& S, FString* Error)
{
    auto Fail = [&](const TCHAR* Message) { if (Error) *Error = Message; return false; };
    auto In = [](float V, float Min, float Max) { return FMath::IsFinite(V) && V >= Min && V <= Max; };
    if (!In(S.Speed, 50, 10000) || !In(S.Radius, 1, 200) || !In(S.MaxRange, 50, 5000) || !In(S.LifetimeSeconds, .05f, 30) ||
        !In(S.WarningSeconds, 0, 10) || !In(S.Damage, 0, 10000)) return Fail(TEXT("Invalid projectile speed, dimensions, timing, or damage"));
    if (S.HitLimit < 1 || S.HitLimit > 32 || S.ReflectionLimit < 0 || S.ReflectionLimit > 8) return Fail(TEXT("Invalid projectile hit or reflection budget"));
    if (!In(S.ManaCost, 0, 10000) || !In(S.EnergyCost, 0, 100) || !In(S.CooldownSeconds, 0, 300) || !In(S.CastRange, 0, 5000)) return Fail(TEXT("Invalid projectile costs or cast range"));
    for (auto Policy : {S.WorldCollision, S.PlayerCollision, S.MonsterCollision, S.ProtectionCollision, S.WallCollision})
        if (static_cast<uint8>(Policy) > static_cast<uint8>(ECireProjectileCollision::Reflect)) return Fail(TEXT("Unknown projectile collision response"));
    if (!In(S.Color.R, 0, 8) || !In(S.Color.G, 0, 8) || !In(S.Color.B, 0, 8) || !In(S.Color.A, .03f, 1) || S.AbilityName.Len() > 80 || S.VisualStyle.Len() > 80)
        return Fail(TEXT("Invalid projectile presentation"));
    return true;
}
ACireSkillshot* ACireSkillshot::Spawn(AActor* Source, const FCireSkillshotSpec& InputSpec, FVector AimPoint, const FString& Name)
{
    FCireSkillshotSpec Spec=InputSpec;if(Source)CireDeveloperTools::AdjustSkillshot(Source->GetWorld(),Spec);
    if (!CireSkillRuntime::Alive(Source) || !Source->HasAuthority() || !ValidateSpec(Spec) || AimPoint.ContainsNaN() || Name.Len() > 80) return nullptr;
    UWorld* World = Source->GetWorld();
    auto* Mode = World->GetAuthGameMode<ACireGameMode>();
    const int32 Team = CireSkillRuntime::Team(Source);
    if (!Mode || !Mode->IsCombatPhase() || Team < 0 || Team > 1 || (Cast<ACireMonster>(Source) && Mode->Clock.Phase() != Cires::MatchPhase::Survival)) return nullptr;
    const FVector Origin = Source->GetActorLocation();
    if (!CireSkillRuntime::InRealmBounds(Mode, Team, Origin)) return nullptr;
    FVector Direction = AimPoint - Origin; Direction.Z = 0;
    if (!Direction.Normalize()) return nullptr;
    int32 Total = 0, Owned = 0;
    for (TActorIterator<ACireSkillshot> It(World); It; ++It)
        if (!It->IsActorBeingDestroyed()) { ++Total; if (It->SourceActor == Source) ++Owned; }
    if (Total >= 256 || Owned >= 16) return nullptr;
    const FTransform Transform(Direction.Rotation(), Origin);
    auto* Result = World->SpawnActorDeferred<ACireSkillshot>(StaticClass(), Transform, Source, Cast<APawn>(Source), ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
    if (!Result) return nullptr;
    Result->ShotSpec = Spec; Result->SourceActor = Source; Result->Velocity = Direction * Spec.Speed;
    Result->OriginTeam = Team; Result->OriginPhase = CireSkillRuntime::Phase(World);
    Result->AbilityName = !Name.IsEmpty() ? Name : !Spec.AbilityName.IsEmpty() ? Spec.AbilityName : TEXT("Skillshot");
    Result->StartServerTime = World->GetTimeSeconds();
    Result->bReleased = Spec.WarningSeconds <= 0;
    Result->FinishSpawning(Transform);
    return Result;
}
void ACireSkillshot::BeginPlay() { Super::BeginPlay(); OnRep_Appearance(); }
void ACireSkillshot::OnRep_Appearance()
{
    if (GetNetMode() != NM_DedicatedServer)
    {
        const auto* Local = GetWorld()->GetFirstPlayerController();
        SetActorHiddenInGame(!Local || !CanObserve(Local));
    }
    FlightMesh->SetRelativeScale3D(FVector(ShotSpec.Radius * 2.f / 100.f));
    if (auto* Material = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Art/Materials/M_Ember.M_Ember")))
    {
        auto* Dynamic = UMaterialInstanceDynamic::Create(Material, this);
        Dynamic->SetVectorParameterValue(TEXT("Tint"), ShotSpec.Color);
        FlightMesh->SetMaterial(0, Dynamic);
    }
    if (GetNetMode() != NM_DedicatedServer && !IsValid(Presentation))
        Presentation = CireSpellPresentation::AttachProjectile(this, FName(*ShotSpec.VisualStyle), ShotSpec.Radius);
    if (auto* Visual = Cast<ACireSpellVisual>(Presentation)) Visual->SetTint(ShotSpec.Color);
    FlightMesh->SetVisibility(!IsValid(Presentation));
}
bool ACireSkillshot::CanObserve(const AActor* Observer) const { return CireSkillRuntime::CanObserve(Observer, GetWorld(), OriginTeam, OriginPhase); }
bool ACireSkillshot::IsNetRelevantFor(const AActor* Viewer, const AActor* Target, const FVector&) const { return CanObserve(Viewer ? Viewer : Target); }
void ACireSkillshot::Tick(float Delta)
{
    Super::Tick(Delta);
    if (HasAuthority())
    {
        if (!CireSkillRuntime::Alive(SourceActor) || CireSkillRuntime::Phase(GetWorld()) != OriginPhase || CireSkillRuntime::Team(SourceActor) != OriginTeam)
        { Destroy(); return; }
        if (!FMath::IsFinite(Delta) || Delta < 0) return;
        const float PreviousAge = Age;
        Age += Delta;
        const float End = ShotSpec.WarningSeconds + ShotSpec.LifetimeSeconds;
        const float ActiveDelta = FMath::Max(0.f, FMath::Min(Age, End) - FMath::Max(PreviousAge, ShotSpec.WarningSeconds));
        if (Age >= ShotSpec.WarningSeconds && !bReleased) { bReleased = true; OnRep_Appearance(); ForceNetUpdate(); }
        if (ActiveDelta > 0) Travel(FMath::Min(ShotSpec.Speed * ActiveDelta, ShotSpec.MaxRange - DistanceTravelled));
        if (!IsActorBeingDestroyed() && (Age >= End || DistanceTravelled >= ShotSpec.MaxRange - .01f)) Destroy();
    }
    if (GetNetMode() != NM_DedicatedServer)
    {
        auto* Local = GetWorld()->GetFirstPlayerController();
        SetActorHiddenInGame(!Local || !CanObserve(Local));
        FlightMesh->SetVisibility(!IsValid(Presentation));
    }
}

void ACireSkillshot::Travel(float Distance)
{
    auto* Mode = GetWorld()->GetAuthGameMode<ACireGameMode>();
    FCollisionObjectQueryParams Objects;
    Objects.AddObjectTypesToQuery(ECC_WorldStatic); Objects.AddObjectTypesToQuery(ECC_WorldDynamic); Objects.AddObjectTypesToQuery(ECC_Pawn);
    TSet<TWeakObjectPtr<AActor>> IgnoredThisStep;
    TSet<TWeakObjectPtr<UPrimitiveComponent>> IgnoredComponents;
    // Each sweep covers the complete travelled distance, so fast projectiles cannot
    // tunnel between frames. Repeated sweeps resolve ordered pierces/reflections.
    for (int32 Iteration = 0; Distance > .01f && Iteration < 64 && !IsActorBeingDestroyed(); ++Iteration)
    {
        const FVector Start = GetActorLocation();
        const FVector Direction = FVector(Velocity).GetSafeNormal();
        FVector End = Start + Direction * Distance;
        bool bReachedBoundary = false;
        if (!CireSkillRuntime::InRealmBounds(Mode, OriginTeam, End, ShotSpec.Radius))
        {
            float Low = 0, High = Distance;
            for (int32 I = 0; I < 16; ++I)
            {
                const float Mid = (Low + High) * .5f;
                if (CireSkillRuntime::InRealmBounds(Mode, OriginTeam, Start + Direction * Mid, ShotSpec.Radius)) Low = Mid; else High = Mid;
            }
            Distance = Low; End = Start + Direction * Distance; bReachedBoundary = true;
        }
        FCollisionQueryParams Params(SCENE_QUERY_STAT(CireSkillshotSweep), false);
        Params.AddIgnoredActor(this); Params.AddIgnoredActor(SourceActor);
        for (auto Actor : IgnoredThisStep) if (Actor.IsValid()) Params.AddIgnoredActor(Actor.Get());
        for (auto Component : PiercedWorldComponents) if (Component.IsValid()) Params.AddIgnoredComponent(Component.Get());
        for (auto Component : IgnoredComponents) if (Component.IsValid()) Params.AddIgnoredComponent(Component.Get());
        for (const auto& Pair : HitGeneration)
            if (Pair.Key.IsValid() && (!ShotSpec.bHitSameTargetAgain || Pair.Value == ReflectionCount)) Params.AddIgnoredActor(Pair.Key.Get());
        FHitResult Hit;
        if (!GetWorld()->SweepSingleByObjectType(Hit, Start, End, FQuat::Identity, Objects, FCollisionShape::MakeSphere(ShotSpec.Radius), Params))
        {
            SetActorLocation(End); DistanceTravelled += Distance;
            if (bReachedBoundary) Destroy();
            return;
        }
        const float Travelled = Distance * FMath::Clamp(Hit.Time, 0.f, 1.f);
        DistanceTravelled += Travelled; Distance -= Travelled;
        SetActorLocation(Start + Direction * Travelled);
        AActor* Target = Hit.GetActor();
        auto* Construct = Cast<ACireConstruct>(Target);
        ECireProjectileCollision Policy = ShotSpec.WorldCollision;
        bool bDamageable = false;
        if (Construct)
        {
            Policy = Construct->IsProtection() ? ShotSpec.ProtectionCollision : ShotSpec.WallCollision;
            if (!Construct->BlocksProjectilesFrom(SourceActor)) Policy = ECireProjectileCollision::Ignore;
            // Explicit projectile Ignore/Pierce bypasses protection; a stopped shot
            // takes the protection's absorb/reflect policy.
            else if (Construct->IsProtection() && Policy == ECireProjectileCollision::Stop) Policy = Construct->ConstructSpec.ProtectionResponse;
            bDamageable = Construct->CanBeDamagedBy(SourceActor);
        }
        else if (Cast<ACireHero>(Target)) { Policy = ShotSpec.PlayerCollision; bDamageable = CireCombat::AreHostile(SourceActor, Target); if (!bDamageable) Policy = ECireProjectileCollision::Ignore; }
        else if (Cast<ACireMonster>(Target)) { Policy = ShotSpec.MonsterCollision; bDamageable = CireCombat::AreHostile(SourceActor, Target); if (!bDamageable) Policy = ECireProjectileCollision::Ignore; }
        else if (Hit.GetComponent() && Hit.GetComponent()->GetCollisionResponseToChannel(ECC_Pawn) != ECR_Block) Policy = ECireProjectileCollision::Ignore;

        if (Policy == ECireProjectileCollision::Reflect)
        {
            if (ReflectionCount >= ShotSpec.ReflectionLimit) { Destroy(); return; }
            FVector Normal = Hit.ImpactNormal.GetSafeNormal(); Normal.Z = 0;
            if (!Normal.Normalize()) Normal = -Direction;
            FVector Reflected = Direction - 2.f * FVector::DotProduct(Direction, Normal) * Normal;
            Reflected.Z = 0; Reflected.Normalize();
            Velocity = Reflected * ShotSpec.Speed; SetActorRotation(Reflected.Rotation()); ++ReflectionCount;
            if (Construct && CireSkillRuntime::Alive(Construct->GetSourceActor()))
            {
                SourceActor = Construct->GetSourceActor(); OriginTeam = CireSkillRuntime::Team(SourceActor);
                SetOwner(SourceActor); SetInstigator(Cast<APawn>(SourceActor));
            }
            // Move the swept sphere off this surface. Do not ignore the complete
            // component: an instanced world component can contain many walls.
            const float Nudge = FMath::Min(2.f, Distance);
            SetActorLocation(GetActorLocation() + Reflected * Nudge); Distance -= Nudge; DistanceTravelled += Nudge;
            ForceNetUpdate();
            continue;
        }
        if (Policy != ECireProjectileCollision::Ignore && bDamageable)
        {
            const int32* PreviousHit = HitGeneration.Find(Target);
            if (!PreviousHit || (ShotSpec.bHitSameTargetAgain && *PreviousHit != ReflectionCount))
            {
                HitGeneration.Add(Target, ReflectionCount); ++HitCount;
                const float Dealt=CireCombat::ApplyStrike(SourceActor, Target, ShotSpec.Damage, AbilityName, ShotSpec.bCanCrit);
                if(Dealt>0&&CireCombat::IsAlive(Target)&&AbilityName==TEXT("Frost Bind")){
                    const float Until=GetWorld()->GetTimeSeconds()+CireDeveloperTools::EffectSeconds(GetWorld(),4.f);
                    if(auto* Hero=Cast<ACireHero>(Target)){Hero->SlowUntil=FMath::Max(Hero->SlowUntil,Until);Hero->ForceNetUpdate();}
                    if(auto* Monster=Cast<ACireMonster>(Target)){Monster->SlowUntil=FMath::Max(Monster->SlowUntil,Until);Monster->ForceNetUpdate();}
                }
                if (HitCount >= ShotSpec.HitLimit) { Destroy(); return; }
            }
        }
        if (Policy == ECireProjectileCollision::Stop) { Destroy(); return; }
        if (Cast<ACharacter>(Target) || Construct) IgnoredThisStep.Add(Target);
        else if (Hit.GetComponent())
        {
            IgnoredComponents.Add(Hit.GetComponent());
            if (Policy == ECireProjectileCollision::Pierce) PiercedWorldComponents.Add(Hit.GetComponent());
        }
        else { Destroy(); return; }
    }
    // Malformed geometry or too many collisions must not stall or create an
    // unbounded server loop; excess travel is terminated conservatively.
    if (Distance > .01f && !IsActorBeingDestroyed()) Destroy();
}
void ACireSkillshot::ClearAll(UWorld* World) { if (World) for (TActorIterator<ACireSkillshot> It(World); It; ++It) if (It->HasAuthority()) It->Destroy(); }
void ACireSkillshot::ClearForActor(AActor* Actor) { if (IsValid(Actor)) for (TActorIterator<ACireSkillshot> It(Actor->GetWorld()); It; ++It) if (It->HasAuthority() && It->SourceActor == Actor) It->Destroy(); }
void ACireSkillshot::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(ACireSkillshot, ShotSpec); DOREPLIFETIME(ACireSkillshot, Velocity); DOREPLIFETIME(ACireSkillshot, OriginTeam);
    DOREPLIFETIME(ACireSkillshot, OriginPhase); DOREPLIFETIME(ACireSkillshot, AbilityName); DOREPLIFETIME(ACireSkillshot, StartServerTime); DOREPLIFETIME(ACireSkillshot, bReleased);
}
