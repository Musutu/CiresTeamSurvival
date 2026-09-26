#include "CireConstruct.h"
#include "CireItems.h" // items-v2
#include "CireDeveloperTools.h"
#include "CireCombatEvents.h"
#include "CireSkillRuntime.h"
#include "CireSpellPresentation.h"
#include "CireTechConstructs.h" // new-champions
#include "CireAreaEffects.h" // new-champions: pylon fields
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/OverlapResult.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Net/UnrealNetwork.h"

namespace
{
FVector Extents(const FCireConstructSpec& Spec) { return FVector(Spec.Depth, Spec.Width, Spec.Height) * .5f; }
bool Bounded(float Value, float Min, float Max) { return FMath::IsFinite(Value) && Value >= Min && Value <= Max; }
bool IsTown(const FVector& P, int32 Team)
{
    return P.X >= -2300 && P.X <= -1400 && FMath::Abs(P.Y - (Team == 0 ? -2100.f : 2100.f)) <= 900;
}
}

ACireConstruct::ACireConstruct()
{
    PrimaryActorTick.bCanEverTick = true;
    bReplicates = true;
    SetReplicateMovement(true);
    SetNetUpdateFrequency(10);
    CollisionBox = CreateDefaultSubobject<UBoxComponent>(TEXT("ConstructCollision"));
    SetRootComponent(CollisionBox);
    CollisionBox->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
    CollisionBox->SetCollisionObjectType(ECC_WorldDynamic);
    CollisionBox->SetCollisionResponseToAllChannels(ECR_Ignore);
    CollisionBox->SetGenerateOverlapEvents(false);
    CollisionBox->SetCanEverAffectNavigation(false);
    BodyMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ConstructBody"));
    BodyMesh->SetupAttachment(CollisionBox);
    BodyMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    BodyMesh->SetCanEverAffectNavigation(false);
    BodyMesh->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
    // new-champions: energy core and crown for tech constructs (hidden for walls/protection).
    CoreMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("TechCore"));
    CrownMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("TechCrown"));
    for (UStaticMeshComponent* Part : {CoreMesh.Get(), CrownMesh.Get()})
    {
        Part->SetupAttachment(CollisionBox);
        Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Part->SetCanEverAffectNavigation(false);
        Part->SetVisibility(false);
    }
}

bool ACireConstruct::ValidateSpec(const FCireConstructSpec& S, FString* Error)
{
    auto Fail = [&](const TCHAR* Message) { if (Error) *Error = Message; return false; };
    if (static_cast<uint8>(S.Kind) > static_cast<uint8>(ECireConstructKind::Skitter) ||
        static_cast<uint8>(S.ProtectionResponse) > static_cast<uint8>(ECireProjectileCollision::Reflect)) return Fail(TEXT("Unknown construct kind or response"));
    if (!Bounded(S.MaxHealth, 1, 100000) || !Bounded(S.LifetimeSeconds, .1f, 120) ||
        !Bounded(S.Width, 20, 2000) || !Bounded(S.Depth, 10, 1000) || !Bounded(S.Height, 20, 2000)) return Fail(TEXT("Construct dimensions, health, or lifetime out of bounds"));
    if (!Bounded(S.ManaCost, 0, 10000) || !Bounded(S.EnergyCost, 0, 100) || !Bounded(S.CooldownSeconds, 0, 300) || !Bounded(S.CastRange, 0, 5000)) return Fail(TEXT("Construct costs or cast range out of bounds"));
    if (!Bounded(S.Color.R, 0, 8) || !Bounded(S.Color.G, 0, 8) || !Bounded(S.Color.B, 0, 8) || !Bounded(S.Color.A, .03f, 1)) return Fail(TEXT("Invalid construct color"));
    // new-champions: tech construct numbers.
    if (!Bounded(S.AttackRange, 0, 3000) || !Bounded(S.AttackInterval, .1f, 10) || !Bounded(S.AttackDamage, 0, 10000) || !Bounded(S.TriggerRadius, 0, 1000) ||
        !Bounded(S.EffectRadius, 0, 2000) || !Bounded(S.EffectMagnitude, 0, 10) || !Bounded(S.MoveSpeed, 0, 2000) || S.OwnerLimit < 0 || S.OwnerLimit > 12 ||
        !Bounded(S.SplashRadius, 0, 1000)) return Fail(TEXT("Tech construct numbers out of bounds"));
    if (S.IsTech() && (S.Recipe.IsNone() || (S.Kind == ECireConstructKind::Turret && S.AttackRange < 100) ||
        ((S.Kind == ECireConstructKind::Trap || S.Kind == ECireConstructKind::Skitter) && S.TriggerRadius < 20) ||
        (S.Kind == ECireConstructKind::Pylon && S.EffectRadius < 100) || (S.Kind == ECireConstructKind::Skitter && S.MoveSpeed < 50)))
        return Fail(TEXT("Tech construct needs a recipe and its kind's range, trigger, field or speed"));
    return true;
}

bool ACireConstruct::ValidatePlacement(ACireHero* Source, const FCireConstructSpec& Spec, FVector& Ground, FRotator Heading, FString* Error)
{
    return ValidatePlacementFor(Source, Spec, Ground, Heading, Error);
}

bool ACireConstruct::ValidatePlacementFor(AActor* Source, const FCireConstructSpec& Spec, FVector& Ground, FRotator Heading, FString* Error)
{
    auto Fail = [&](const TCHAR* Message) { if (Error) *Error = Message; return false; };
    if (!IsValid(Source) || !Source->HasAuthority() || !CireSkillRuntime::Alive(Source) || !ValidateSpec(Spec, Error) || Ground.ContainsNaN() || Heading.ContainsNaN()) return false;
    const int32 SourceTeam = CireSkillRuntime::Team(Source);
    if (SourceTeam < 0 || SourceTeam > 1) return Fail(TEXT("Construct owner has no team"));
    auto* Mode = Source->GetWorld()->GetAuthGameMode<ACireGameMode>();
    if (!Mode || !Mode->IsCombatPhase() || FVector::DistSquared2D(Source->GetActorLocation(), Ground) > FMath::Square(Spec.CastRange)) return Fail(TEXT("Construct is outside casting range"));
    const FQuat Rotation = FRotator(0, Heading.Yaw, 0).Quaternion();
    const FVector Half = Extents(Spec);
    // Test the oriented footprint, including its edges, against town and realm.
    // Sampling edges catches a wall spanning town while its corners lie outside.
    for (int32 X = -1; X <= 1; ++X)
        for (int32 Y = -1; Y <= 1; ++Y)
        {
            const FVector P = Ground + Rotation.RotateVector(FVector(X * Half.X, Y * Half.Y, 0));
            if (!CireSkillRuntime::InRealmBounds(Mode, SourceTeam, P, 10)) return Fail(TEXT("Construct footprint crosses the realm boundary"));
        }
    if (Mode->Clock.Phase() != Cires::MatchPhase::Arena)
    {
        // Separating-axis rectangle test prevents thin rotated walls clipping town.
        const FVector TownCenter(-1850, SourceTeam == 0 ? -2100.f : 2100.f, Ground.Z);
        const FVector Offset = TownCenter - Ground;
        const FVector XAxis = Rotation.GetAxisX(), YAxis = Rotation.GetAxisY();
        const bool bSeparated = FMath::Abs(Offset.X) > 450 + FMath::Abs(XAxis.X) * Half.X + FMath::Abs(YAxis.X) * Half.Y ||
            FMath::Abs(Offset.Y) > 900 + FMath::Abs(XAxis.Y) * Half.X + FMath::Abs(YAxis.Y) * Half.Y ||
            FMath::Abs(FVector::DotProduct(Offset, XAxis)) > Half.X + 450 * FMath::Abs(XAxis.X) + 900 * FMath::Abs(XAxis.Y) ||
            FMath::Abs(FVector::DotProduct(Offset, YAxis)) > Half.Y + 450 * FMath::Abs(YAxis.X) + 900 * FMath::Abs(YAxis.Y);
        if (!bSeparated || IsTown(Ground, SourceTeam)) return Fail(TEXT("Construct overlaps town"));
    }
    FCollisionQueryParams Params(SCENE_QUERY_STAT(CireConstructPlacement), false);
    FCollisionObjectQueryParams GroundObjects(ECC_WorldStatic);
    FHitResult Floor;
    if (!Source->GetWorld()->LineTraceSingleByObjectType(Floor, Ground + FVector(0, 0, 300), Ground - FVector(0, 0, 500), GroundObjects, Params) || Floor.ImpactNormal.Z < .9f)
        return Fail(TEXT("Construct requires level supporting ground"));
    Ground.Z = Floor.ImpactPoint.Z + 3.f;
    // All four corners must have support at the same height; no hovering over ledges.
    for (int32 X : {-1, 1}) for (int32 Y : {-1, 1})
    {
        const FVector P = Ground + Rotation.RotateVector(FVector(X * Half.X, Y * Half.Y, 0));
        FHitResult Support;
        if (!Source->GetWorld()->LineTraceSingleByObjectType(Support, P + FVector(0, 0, 40), P - FVector(0, 0, 50), GroundObjects, Params) ||
            Support.ImpactNormal.Z < .9f || FMath::Abs(Support.ImpactPoint.Z + 3.f - Ground.Z) > 12.f) return Fail(TEXT("Construct footprint lacks level ground"));
    }
    FCollisionObjectQueryParams Objects;
    Objects.AddObjectTypesToQuery(ECC_WorldStatic); Objects.AddObjectTypesToQuery(ECC_WorldDynamic); Objects.AddObjectTypesToQuery(ECC_Pawn);
    TArray<FOverlapResult> Overlaps;
    Source->GetWorld()->OverlapMultiByObjectType(Overlaps, Ground + FVector(0, 0, Half.Z), Rotation, Objects, FCollisionShape::MakeBox(Half), Params);
    for (const auto& Overlap : Overlaps)
    {
        if (!Overlap.GetComponent()) continue;
        // new-champions: tech constructs do not block units, so only solid world geometry, walls and other tech rule them out.
        if (Spec.IsTech())
        {
            if (const auto* Other = Cast<ACireConstruct>(Overlap.GetActor()))
            {
                if (Spec.Kind != ECireConstructKind::Skitter && Other->ConstructSpec.Kind != ECireConstructKind::Skitter) return Fail(TEXT("Construct overlaps another construct"));
                continue;
            }
            if (Cast<ACharacter>(Overlap.GetActor())) continue;
        }
        if (Cast<ACharacter>(Overlap.GetActor()) || Cast<ACireConstruct>(Overlap.GetActor()) ||
            Overlap.GetComponent()->GetCollisionResponseToChannel(ECC_Pawn) == ECR_Block) return Fail(TEXT("Construct overlaps a unit, wall, or world object"));
    }
    return true;
}

ACireConstruct* ACireConstruct::Spawn(ACireHero* Source, const FCireConstructSpec& InputSpec, FVector Ground, FRotator Heading, const FString& Name)
{
    return SpawnFor(Source, InputSpec, Ground, Heading, Name);
}

ACireConstruct* ACireConstruct::SpawnFor(AActor* Source, const FCireConstructSpec& InputSpec, FVector Ground, FRotator Heading, const FString& Name)
{
    FCireConstructSpec Spec=InputSpec;if(Source)CireDeveloperTools::AdjustConstruct(Source->GetWorld(),Spec);
    if (Name.Len() > 80 || !ValidatePlacementFor(Source, Spec, Ground, Heading)) return nullptr;
    int32 Total = 0, Owned = 0, OwnedTech = 0;
    TArray<ACireConstruct*> SameRecipe;
    for (TActorIterator<ACireConstruct> It(Source->GetWorld()); It; ++It)
        if (!It->IsActorBeingDestroyed())
        {
            ++Total;
            if (It->SourceUnit != Source) continue;
            if (It->IsTech()) ++OwnedTech; else ++Owned;
            if (Spec.IsTech() && It->ConstructSpec.Recipe == Spec.Recipe) SameRecipe.Add(*It);
        }
    if (Spec.IsTech())
    {
        // new-champions: at the recipe's limit the oldest one collapses to make room; the owner budget stays bounded.
        SameRecipe.Sort([](const ACireConstruct& A, const ACireConstruct& B) { return A.Age > B.Age; });
        const int32 Limit = (Spec.OwnerLimit > 0 ? Spec.OwnerLimit : 4) + CireItems::ConstructLimitBonus(Source); // items-v2: Heartforge
        int32 Removed = 0;
        for (int32 Index = 0; Index + Limit <= SameRecipe.Num(); ++Index) { SameRecipe[Index]->Destroy(); ++Removed; }
        OwnedTech -= Removed; Total -= Removed;
        if (Total >= 96 || OwnedTech >= 16) return nullptr;
    }
    else if (Total >= 48 || Owned >= 6) return nullptr;
    const FTransform Transform(FRotator(0, Heading.Yaw, 0), Ground + FVector(0, 0, Spec.Height * .5f));
    auto* Result = Source->GetWorld()->SpawnActorDeferred<ACireConstruct>(StaticClass(), Transform, Source, Cast<APawn>(Source), ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
    if (!Result) return nullptr;
    Result->SourceUnit = Source; Result->OriginTeam = CireSkillRuntime::Team(Source); Result->bMonsterOwned = Source->IsA<ACireMonster>();
    Result->OriginPhase = CireSkillRuntime::Phase(Source->GetWorld());
    Result->ConstructSpec = Spec; Result->Health = Result->MaxHealth = Spec.MaxHealth * CireItems::ConstructHealthMultiplier(Source); // items-v2
    Result->ItemShield = Result->MaxHealth * CireItems::ConstructShieldFraction(Source);
    Result->AbilityName = Name.IsEmpty() ? (Spec.Kind == ECireConstructKind::Wall ? TEXT("Summoned Wall") : Spec.IsTech() ? Spec.Recipe.ToString() : TEXT("Protection")) : Name;
    Result->ExpiresServerTime = Source->GetWorld()->GetTimeSeconds() + Spec.LifetimeSeconds; // fix/summons: summons-bar timer
    if (Spec.Kind == ECireConstructKind::Skitter) Result->SetNetUpdateFrequency(30);
    Result->FinishSpawning(Transform);
    if (Spec.IsTech()) CireTechConstructs::OnSpawned(Result); // new-champions: pylon fields, placement cue
    return Result;
}

void ACireConstruct::BeginPlay() { Super::BeginPlay(); OnRep_Appearance(); RefreshMovementExceptions(); }
void ACireConstruct::OnRep_Appearance()
{
    if (GetNetMode() != NM_DedicatedServer)
    {
        const auto* Local = GetWorld()->GetFirstPlayerController();
        SetActorHiddenInGame(!Local || !CanObserve(Local));
    }
    CollisionBox->SetBoxExtent(Extents(ConstructSpec));
    CollisionBox->SetCollisionResponseToChannel(ECC_Pawn, ConstructSpec.bBlockMovement ? ECR_Block : ECR_Ignore);
    CollisionBox->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
    CollisionBox->SetCollisionResponseToChannel(ECC_GameTraceChannel1, ECR_Block);
    BodyMesh->SetRelativeScale3D(FVector(ConstructSpec.Depth, ConstructSpec.Width, ConstructSpec.Height) / 100.f);
    if (IsTech())
    {
        // new-champions: tech constructs are drawn by CireTechConstructs (energy palette, per-kind silhouette).
        CireTechConstructs::ApplyAppearance(this);
        return;
    }
    if (auto* Material = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Art/Effects/CireSpell/M_Runestone.M_Runestone")))
    {
        auto* Dynamic = UMaterialInstanceDynamic::Create(Material, this);
        Dynamic->SetVectorParameterValue(TEXT("Tint"), ConstructSpec.Color);
        BodyMesh->SetMaterial(0, Dynamic);
    }
    if (GetNetMode() != NM_DedicatedServer && !IsValid(Presentation))
        Presentation = CireSpellPresentation::AttachConstruct(this, IsProtection(), Extents(ConstructSpec));
    if (auto* Visual = Cast<ACireSpellVisual>(Presentation)) Visual->SetTint(ConstructSpec.Color);
    BodyMesh->SetVisibility(!IsProtection() || !IsValid(Presentation));
}
AActor* ACireConstruct::GetSourceActor() const { return SourceUnit; }
bool ACireConstruct::CanObserve(const AActor* Observer) const { return CireSkillRuntime::CanObserve(Observer, GetWorld(), OriginTeam, OriginPhase); }
bool ACireConstruct::IsNetRelevantFor(const AActor* Viewer, const AActor* Target, const FVector&) const { return CanObserve(Viewer ? Viewer : Target); }
bool ACireConstruct::CanBeDamagedBy(AActor* Source) const
{
    if (!ConstructSpec.bDestructible || Health <= 0 || !CireSkillRuntime::Alive(Source) || CireSkillRuntime::Phase(GetWorld()) != OriginPhase) return false;
    // new-champions: monster-owned constructs are hostile to the champions (and their summons) of their lane.
    if (bMonsterOwned)
    {
        if (const auto* Hero = Cast<ACireHero>(Source)) return OriginPhase == static_cast<int32>(Cires::MatchPhase::Survival) && Hero->TeamId == OriginTeam;
        return false;
    }
    if (const auto* Hero = Cast<ACireHero>(Source)) return OriginPhase == static_cast<int32>(Cires::MatchPhase::Arena) && Hero->TeamId != OriginTeam;
    if (const auto* Monster = Cast<ACireMonster>(Source)) return OriginPhase == static_cast<int32>(Cires::MatchPhase::Survival) && Monster->Lane == OriginTeam;
    return false;
}
bool ACireConstruct::BlocksProjectilesFrom(AActor* Source) const
{
    if (!ConstructSpec.bBlockProjectiles || Health <= 0 || CireSkillRuntime::Phase(GetWorld()) != OriginPhase || !CireSkillRuntime::Alive(Source)) return false;
    if (const auto* Hero = Cast<ACireHero>(Source)) return OriginPhase == static_cast<int32>(Cires::MatchPhase::Arena) && Hero->TeamId != OriginTeam;
    if (const auto* Monster = Cast<ACireMonster>(Source)) return OriginPhase == static_cast<int32>(Cires::MatchPhase::Survival) && Monster->Lane == OriginTeam;
    return false;
}
bool ACireConstruct::BlocksMovementOf(AActor* Mover) const
{
    if (!ConstructSpec.bBlockMovement || Health <= 0 || CireSkillRuntime::Phase(GetWorld()) != OriginPhase) return false;
    if (const auto* Hero = Cast<ACireHero>(Mover))
        return (OriginPhase == static_cast<int32>(Cires::MatchPhase::Arena) || Hero->TeamId == OriginTeam) && (ConstructSpec.bBlockFriendly || Hero->TeamId != OriginTeam);
    if (const auto* Monster = Cast<ACireMonster>(Mover)) return OriginPhase == static_cast<int32>(Cires::MatchPhase::Survival) && Monster->Lane == OriginTeam;
    return false;
}
void ACireConstruct::RefreshMovementExceptions()
{
    for (TActorIterator<ACharacter> It(GetWorld()); It; ++It)
    {
        ACharacter* Character = *It;
        const bool bIgnore = !BlocksMovementOf(Character);
        Character->GetCapsuleComponent()->IgnoreActorWhenMoving(this, bIgnore);
        if (bIgnore) IgnoringCharacters.Add(Character); else IgnoringCharacters.Remove(Character);
    }
}
float ACireConstruct::TakeDamage(float Amount, const FDamageEvent& Event, AController*, AActor* Causer)
{
    if (!HasAuthority() || !FMath::IsFinite(Amount) || Amount <= 0 || !CanBeDamagedBy(Causer)) return 0;
    if (IsTech()) Amount = CireTechConstructs::ModifyIncomingDamage(this, Causer, Amount); // new-champions
    if (ItemShield > 0) { const float Soak = FMath::Min(ItemShield, Amount); ItemShield -= Soak; Amount -= Soak; if (Amount <= 0) return 0; } // items-v2: Aegis Plating
    const float Applied = FMath::Min(Health, Amount);
    Health -= Applied;
    CireCombat::BroadcastDamage(Causer, this, Applied, Event);
    ForceNetUpdate();
    if (Health <= 0) { CollisionBox->SetCollisionEnabled(ECollisionEnabled::NoCollision); Destroy(); }
    return Applied;
}
void ACireConstruct::Tick(float Delta)
{
    Super::Tick(Delta);
    if (HasAuthority())
    {
        Age += FMath::Max(0.f, Delta);
        if (!CireSkillRuntime::Alive(SourceUnit) || CireSkillRuntime::Team(SourceUnit) != OriginTeam || CireSkillRuntime::Phase(GetWorld()) != OriginPhase || Age >= ConstructSpec.LifetimeSeconds)
        { if (IsTech()) CireTechConstructs::OnExpired(this); CollisionBox->SetCollisionEnabled(ECollisionEnabled::NoCollision); Destroy(); return; }
        if (IsTech() && !CireTechConstructs::TickConstruct(this, FMath::Max(0.f, Delta))) return; // new-champions: fire, trigger, pulse, seek
    }
    if (IsTech()) UpdateTechVisual(Delta);
    RefreshMovementExceptions();
    if (GetNetMode() != NM_DedicatedServer)
    {
        auto* Local = GetWorld()->GetFirstPlayerController();
        SetActorHiddenInGame(!Local || !CanObserve(Local));
        BodyMesh->SetVisibility(!IsProtection() || !IsValid(Presentation));
    }
}
void ACireConstruct::EndPlay(const EEndPlayReason::Type Reason)
{
    CollisionBox->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    if (HasAuthority() && IsTech()) ACireAreaEffect::ClearForActor(this); // new-champions: a pylon's field goes with it
    for (auto Character : IgnoringCharacters) if (Character.IsValid()) Character->GetCapsuleComponent()->IgnoreActorWhenMoving(this, false);
    IgnoringCharacters.Reset(); Super::EndPlay(Reason);
}
ACireConstruct* ACireConstruct::FindBlockingConstruct(AActor* Mover, AActor* Destination)
{
    return IsValid(Destination) ? FindBlockingConstruct(Mover, Destination->GetActorLocation()) : nullptr;
}
ACireConstruct* ACireConstruct::FindBlockingConstruct(AActor* Mover, FVector Destination)
{
    if (!IsValid(Mover) || Destination.ContainsNaN()) return nullptr;
    float Radius = 35.f;
    if (auto* Character = Cast<ACharacter>(Mover)) Radius = Character->GetCapsuleComponent()->GetScaledCapsuleRadius();
    ACireConstruct* Nearest = nullptr; float NearestT = 2.f;
    for (TActorIterator<ACireConstruct> It(Mover->GetWorld()); It; ++It)
    {
        if (It->IsActorBeingDestroyed() || !It->BlocksMovementOf(Mover)) continue;
        const FTransform T = It->GetActorTransform();
        const FVector A = T.InverseTransformPosition(Mover->GetActorLocation()), B = T.InverseTransformPosition(Destination);
        const FVector Half = Extents(It->ConstructSpec) + FVector(Radius, Radius, 100.f);
        const FVector Direction = B - A;
        float Entry = 0, Exit = 1;
        bool bIntersects = true;
        for (int32 Axis = 0; Axis < 3; ++Axis)
        {
            if (FMath::Abs(Direction[Axis]) < KINDA_SMALL_NUMBER) { if (FMath::Abs(A[Axis]) > Half[Axis]) { bIntersects = false; break; } }
            else
            {
                float T0 = (-Half[Axis] - A[Axis]) / Direction[Axis], T1 = (Half[Axis] - A[Axis]) / Direction[Axis];
                if (T0 > T1) Swap(T0, T1);
                Entry = FMath::Max(Entry, T0); Exit = FMath::Min(Exit, T1);
                if (Entry > Exit) { bIntersects = false; break; }
            }
        }
        if (bIntersects && Entry < NearestT) { NearestT = Entry; Nearest = *It; }
    }
    return Nearest;
}
void ACireConstruct::ClearAll(UWorld* World) { if (World) for (TActorIterator<ACireConstruct> It(World); It; ++It) if (It->HasAuthority()) It->Destroy(); }
void ACireConstruct::ClearForActor(AActor* Actor) { if (IsValid(Actor)) for (TActorIterator<ACireConstruct> It(Actor->GetWorld()); It; ++It) if (It->HasAuthority() && It->SourceUnit == Actor) It->Destroy(); }
void ACireConstruct::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(ACireConstruct, ConstructSpec); DOREPLIFETIME(ACireConstruct, Health); DOREPLIFETIME(ACireConstruct, MaxHealth);
    DOREPLIFETIME(ACireConstruct, OriginTeam); DOREPLIFETIME(ACireConstruct, OriginPhase); DOREPLIFETIME(ACireConstruct, AbilityName);
    DOREPLIFETIME(ACireConstruct, bMonsterOwned); DOREPLIFETIME(ACireConstruct, OverchargedUntil); DOREPLIFETIME(ACireConstruct, ShotSerial); // new-champions
    DOREPLIFETIME(ACireConstruct, ExpiresServerTime); // fix/summons
}

void ACireConstruct::UpdateTechVisual(float DeltaSeconds)
{
    if (GetNetMode() == NM_DedicatedServer) return;
    VisualTime += FMath::Clamp(DeltaSeconds, 0.f, .25f);
    CireTechConstructs::AnimateAppearance(this, VisualTime);
}
