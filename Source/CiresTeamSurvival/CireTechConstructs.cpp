// new-champions: Aetheri Constructs (see CireTechConstructs.h, Docs/NewChampions.md).
#include "CireTechConstructs.h"
#include "CireScalingKits.h" // scaling-kits
#include "CireConstruct.h"
#include "CireGame.h"
#include "CireAbilityDB.h"
#include "CireAreaEffects.h"
#include "CireBuffs.h"
#include "CireCombatEvents.h"
#include "CireCrowdControl.h"
#include "CireNPCCombat.h"
#include "CireSkillRuntime.h"
#include "CireSkillShop.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Materials/MaterialInstanceDynamic.h"

DEFINE_LOG_CATEGORY_STATIC(LogCireTech, Log, All);

namespace
{
using K = ECireConstructKind;
// Energy palette: Aetheri gold/white alloy with blue-violet energy; each field reads by hue.
const FLinearColor Violet(.55f, .42f, 2.2f, 1), Cyan(.25f, 1.2f, 2.0f, 1), Gold(2.0f, 1.45f, .45f, 1), Rose(1.9f, .35f, 1.4f, 1),
    Ice(.35f, .75f, 2.4f, 1), White(1.8f, 1.8f, 2.1f, 1), Spirit(.3f, .6f, 2.6f, 1), Crimson(2.0f, .3f, .35f, 1);

FCireTechRecipe R(const TCHAR* Id, const TCHAR* Name, K Kind, const TCHAR* Effect, float Health, float Life, float Foot, float Height, FLinearColor Color)
{
    FCireTechRecipe X; X.Id = Id; X.Name = Name; X.Kind = Kind; X.Effect = Effect ? FName(Effect) : NAME_None;
    X.Health = Health; X.Lifetime = Life; X.Footprint = Foot; X.Height = Height; X.Color = Color; return X;
}
TArray<FCireTechRecipe> BuildRecipes()
{
    TArray<FCireTechRecipe> Out;
    // ---- Aetheri Artificer ----
    { auto X = R(TEXT("photon_turret"), TEXT("Photon Turret"), K::Turret, nullptr, 240, 25, 70, 150, Violet);
      X.Range = 950; X.Interval = .8f; X.Damage = 18; X.Scaling = .35f; X.Limit = 2; Out.Add(X); }
    { auto X = R(TEXT("skitter_swarm"), TEXT("Skitter Swarm"), K::Skitter, TEXT("mine"), 30, 12, 40, 40, Cyan);
      X.Range = 1400; X.Damage = 70; X.Scaling = .8f; X.Trigger = 110; X.Radius = 200; X.Speed = 520; X.Limit = 6; X.Count = 3; Out.Add(X); }
    { auto X = R(TEXT("arc_mine"), TEXT("Arc Mine"), K::Trap, TEXT("mine"), 60, 30, 50, 20, Cyan);
      X.Damage = 110; X.Scaling = 1.2f; X.Trigger = 150; X.Radius = 240; X.Limit = 3; Out.Add(X); }
    { auto X = R(TEXT("disruption_pylon"), TEXT("Disruption Pylon"), K::Pylon, TEXT("weaken"), 200, 15, 60, 200, Rose);
      X.Interval = .5f; X.Radius = 450; X.Magnitude = .25f; X.Limit = 1; Out.Add(X); }
    { auto X = R(TEXT("warp_obelisk"), TEXT("Warp Obelisk"), K::Turret, nullptr, 600, 15, 110, 320, White);
      X.Range = 1300; X.Interval = 1.2f; X.Damage = 60; X.Scaling = 1.f; X.Splash = 200; X.Limit = 1; Out.Add(X); }
    // ---- Witch Slayer (a trap drawn from the same system) ----
    { auto X = R(TEXT("spirit_lantern"), TEXT("Spirit Lantern"), K::Trap, TEXT("silence"), 60, 20, 40, 80, Spirit);
      X.Damage = 60; X.Scaling = 1.f; X.Trigger = 150; X.Radius = 260; X.Magnitude = 2.f; X.Limit = 2; Out.Add(X); }
    // ---- Aetheri Warden ----
    { auto X = R(TEXT("aegis_pylon"), TEXT("Aegis Pylon"), K::Pylon, TEXT("shield"), 220, 15, 60, 200, Cyan);
      X.Interval = .5f; X.Radius = 450; X.Magnitude = .02f; X.Limit = 1; Out.Add(X); }
    { auto X = R(TEXT("haste_pylon"), TEXT("Haste Pylon"), K::Pylon, TEXT("haste"), 200, 12, 60, 200, Gold);
      X.Interval = .5f; X.Radius = 450; X.Magnitude = .25f; X.Limit = 1; Out.Add(X); }
    { auto X = R(TEXT("gravity_pylon"), TEXT("Gravity Pylon"), K::Pylon, TEXT("slow"), 200, 12, 60, 200, Ice);
      X.Interval = .5f; X.Radius = 450; X.Magnitude = .35f; X.Limit = 1; Out.Add(X); }
    { auto X = R(TEXT("stasis_snare"), TEXT("Stasis Snare"), K::Trap, TEXT("stasis"), 60, 30, 50, 20, Violet);
      X.Damage = 20; X.Scaling = .3f; X.Trigger = 150; X.Radius = 150; X.Magnitude = 1.5f; X.Limit = 3; Out.Add(X); }
    { auto X = R(TEXT("aether_nexus"), TEXT("Aether Nexus"), K::Pylon, TEXT("nexus"), 500, 10, 90, 280, White);
      X.Interval = .5f; X.Radius = 650; X.Magnitude = .05f; X.Limit = 1; Out.Add(X); }
    // ---- Aetheri monster race (damage = unit damage x the ability's damageMultiplier) ----
    { auto X = R(TEXT("npc_photon_turret"), TEXT("Warp Turret"), K::Turret, nullptr, 180, 20, 70, 150, Crimson);
      X.Range = 900; X.Interval = 1.2f; X.Damage = 1; X.Limit = 2; X.bMonster = true; Out.Add(X); }
    { auto X = R(TEXT("npc_skitter"), TEXT("Skitter Bomb"), K::Skitter, TEXT("mine"), 30, 14, 40, 40, Crimson);
      X.Range = 1400; X.Damage = 1; X.Trigger = 110; X.Radius = 200; X.Speed = 480; X.Limit = 6; X.Count = 3; X.bMonster = true; Out.Add(X); }
    { auto X = R(TEXT("npc_gravity_pylon"), TEXT("Gravity Pylon"), K::Pylon, TEXT("slow"), 200, 14, 60, 200, Ice);
      X.Interval = .5f; X.Radius = 450; X.Magnitude = .35f; X.Limit = 1; X.bMonster = true; Out.Add(X); }
    { auto X = R(TEXT("npc_empower_pylon"), TEXT("Empowering Pylon"), K::Pylon, TEXT("empower"), 260, 16, 60, 200, Crimson);
      X.Interval = .5f; X.Radius = 500; X.Magnitude = .25f; X.Limit = 1; X.bMonster = true; Out.Add(X); }
    { auto X = R(TEXT("npc_stasis_mine"), TEXT("Stasis Mine"), K::Trap, TEXT("stasis"), 60, 25, 50, 20, Violet);
      X.Damage = 1; X.Trigger = 150; X.Radius = 170; X.Magnitude = 1.2f; X.Limit = 3; X.bMonster = true; Out.Add(X); }
    return Out;
}

const FName AegisId(TEXT("aether_aegis")), HasteId(TEXT("aether_haste")), WeakenedId(TEXT("aether_weakened")),
    NexusId(TEXT("aether_nexus")), EmpoweredId(TEXT("npc_aether_empowered")), RootedId(TEXT("npc_rooted"));

ACireGameMode* ModeOf(const AActor* A) { return A && A->GetWorld() ? A->GetWorld()->GetAuthGameMode<ACireGameMode>() : nullptr; }
float Feet(const AActor* A)
{
    const auto* C = Cast<ACharacter>(A);
    return static_cast<float>(A->GetActorLocation().Z) - (C ? C->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() : 0.f);
}
bool Near(const AActor* From, const AActor* Unit, float Radius, float Vertical = 220.f)
{
    if (!IsValid(Unit)) return false;
    const auto* C = Cast<ACharacter>(Unit);
    const float Body = C ? C->GetCapsuleComponent()->GetScaledCapsuleRadius() : 0.f;
    return FVector::DistSquared2D(From->GetActorLocation(), Unit->GetActorLocation()) <= FMath::Square(Radius + Body) &&
        FMath::Abs(Feet(Unit) - Feet(From)) <= Vertical;
}
// Every combat unit a construct could affect: monsters, champions and their summons, other constructs.
void Units(UWorld* World, TArray<AActor*>& Out, bool bConstructs)
{
    Out.Reset();
    if (auto* Mode = World->GetAuthGameMode<ACireGameMode>()) for (auto* M : Mode->Monsters) if (CireCombat::IsAlive(M)) Out.Add(M);
    for (TActorIterator<ACireHero> It(World); It; ++It) if (CireCombat::IsAlive(*It)) Out.Add(*It);
    if (bConstructs) for (TActorIterator<ACireConstruct> It(World); It; ++It) if (CireCombat::IsAlive(*It) && !It->IsActorBeingDestroyed()) Out.Add(*It);
}
bool IsAlly(const ACireConstruct* C, const AActor* Unit)
{
    if (!CireCombat::IsAlive(Unit)) return false;
    if (C->bMonsterOwned) { const auto* M = Cast<ACireMonster>(Unit); return M && M->Lane == C->OriginTeam; }
    const auto* H = Cast<ACireHero>(Unit); return H && H->bDrafted && H->TeamId == C->OriginTeam;
}
bool IsEnemy(ACireConstruct* C, AActor* Unit) { return CireCombat::AreHostile(C->GetSourceActor(), Unit); }
ACireHero* OwnerHero(const ACireConstruct* C) { return Cast<ACireHero>(C->GetSourceActor()); }
bool ClearLine(ACireConstruct* C, const AActor* Target, float HeadZ)
{
    FHitResult Hit; FCollisionQueryParams Q(SCENE_QUERY_STAT(CireTechSight), false, C);
    Q.AddIgnoredActor(C->GetSourceActor());
    const bool bBlocked = C->GetWorld()->LineTraceSingleByChannel(Hit, C->GetActorLocation() + FVector(0, 0, HeadZ), Target->GetActorLocation() + FVector(0, 0, 30), ECC_Visibility, Q);
    return !bBlocked || Hit.GetActor() == Target || Cast<ACharacter>(Hit.GetActor()) != nullptr;
}
AActor* NearestEnemy(ACireConstruct* C, float Range, bool bSight, bool bIncludeConstructs)
{
    TArray<AActor*> List; Units(C->GetWorld(), List, bIncludeConstructs);
    AActor* Best = nullptr; double BestD = TNumericLimits<double>::Max();
    for (AActor* U : List)
    {
        if (U == C || !IsEnemy(C, U) || !Near(C, U, Range, 400.f)) continue;
        // Skitters and traps are not worth a bolt while a real unit is in range.
        const double D = FVector::DistSquared2D(C->GetActorLocation(), U->GetActorLocation()) * (Cast<ACireConstruct>(U) ? 4.0 : 1.0);
        if (D < BestD && (!bSight || ClearLine(C, U, C->ConstructSpec.Height * .4f))) { Best = U; BestD = D; }
    }
    return Best;
}
float LevelScale(const AActor* Owner, FName Recipe)
{
    const auto* H = Cast<ACireHero>(Owner);
    return H ? FMath::Max(1.f, CireSkillShop::CastScale(H, Recipe.ToString()).Effect) : 1.f;
}
void Cue(ACireConstruct* C, AActor* Target, FVector From, FVector To, ECireSpellCue Kind, float Scale = 1.f)
{
    CireCombat::PlayCue(C, Target, C->ConstructSpec.Recipe, From, To, Kind, Scale, true);
}
UMaterialInterface* AlloyMaterial()
{
    static TWeakObjectPtr<UMaterialInterface> Cached;
    if (!Cached.IsValid())
    {
        UMaterialInterface* M = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Art/NewChampions01/Materials/M_AetherAlloy.M_AetherAlloy"), nullptr, LOAD_Quiet | LOAD_NoWarn);
        if (!M) M = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Art/Materials/M_Gold.M_Gold"), nullptr, LOAD_Quiet | LOAD_NoWarn);
        Cached = M;
    }
    return Cached.Get();
}
UMaterialInterface* EnergyMaterial()
{
    static TWeakObjectPtr<UMaterialInterface> Cached;
    if (!Cached.IsValid())
    {
        UMaterialInterface* M = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Art/NewChampions01/Materials/M_AetherEnergy.M_AetherEnergy"), nullptr, LOAD_Quiet | LOAD_NoWarn);
        if (!M) M = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Art/Effects/CireSpellPolish01/M_SpellCore.M_SpellCore"), nullptr, LOAD_Quiet | LOAD_NoWarn);
        if (!M) M = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Art/Materials/M_Ember.M_Ember"), nullptr, LOAD_Quiet | LOAD_NoWarn);
        Cached = M;
    }
    return Cached.Get();
}
UStaticMesh* Shape(const TCHAR* Name)
{
    return LoadObject<UStaticMesh>(nullptr, *FString::Printf(TEXT("/Engine/BasicShapes/%s.%s"), Name, Name));
}
UStaticMesh* Authored(FName Kind)
{
    // Optional authored silhouettes (Tools/BuildNewChampionContent.py); basic shapes stand in until they exist.
    const FString Path = FString::Printf(TEXT("/Game/Art/NewChampions01/Constructs/SM_%s.SM_%s"), *Kind.ToString(), *Kind.ToString());
    return LoadObject<UStaticMesh>(nullptr, *Path, nullptr, LOAD_Quiet | LOAD_NoWarn);
}
void Energize(UStaticMeshComponent* Part, FLinearColor Color, float Glow)
{
    if (!Part) return;
    if (UMaterialInterface* Base = EnergyMaterial())
    {
        auto* MID = Cast<UMaterialInstanceDynamic>(Part->GetMaterial(0));
        if (!MID || MID->Parent != Base) { MID = UMaterialInstanceDynamic::Create(Base, Part); Part->SetMaterial(0, MID); }
        MID->SetVectorParameterValue(TEXT("Tint"), Color * Glow);
        MID->SetScalarParameterValue(TEXT("Glow"), Glow);
    }
}
void Alloy(UStaticMeshComponent* Part, FLinearColor Trim)
{
    if (!Part) return;
    if (UMaterialInterface* Base = AlloyMaterial())
    {
        auto* MID = UMaterialInstanceDynamic::Create(Base, Part); Part->SetMaterial(0, MID);
        MID->SetVectorParameterValue(TEXT("Trim"), Trim);
    }
}
}

const TArray<FCireTechRecipe>& CireTechConstructs::Recipes() { static const TArray<FCireTechRecipe> All = BuildRecipes(); return All; }
const FCireTechRecipe* CireTechConstructs::FindRecipe(FName Id) { for (const auto& X : Recipes()) if (X.Id == Id) return &X; return nullptr; }
bool CireTechConstructs::IsConstructSkill(const FString& Id) { const auto* X = FindRecipe(FName(*Id)); return X && !X->bMonster; }
const TArray<FName>& CireTechConstructs::FieldBuffIds() { static const TArray<FName> Ids = {AegisId, HasteId, WeakenedId, NexusId, EmpoweredId}; return Ids; }

bool CireTechConstructs::BuildSpec(AActor* Owner, FName RecipeId, FCireConstructSpec& S, float MonsterMultiplier)
{
    const FCireTechRecipe* X = FindRecipe(RecipeId);
    if (!X || !IsValid(Owner)) return false;
    S = FCireConstructSpec();
    S.Kind = X->Kind; S.Recipe = X->Id; S.Effect = X->Effect; S.Color = X->Color; S.Color.A = 1.f;
    S.Width = S.Depth = X->Footprint; S.Height = X->Height;
    S.bBlockMovement = false; S.bBlockProjectiles = false; S.bBlockFriendly = false; S.bDestructible = true;
    S.AttackRange = X->Range; S.AttackInterval = X->Interval; S.TriggerRadius = X->Trigger; S.EffectRadius = X->Radius;
    S.EffectMagnitude = X->Magnitude; S.MoveSpeed = X->Speed; S.OwnerLimit = X->Limit; S.SplashRadius = X->Splash;
    S.MaxHealth = X->Health; S.LifetimeSeconds = X->Lifetime; S.ManaCost = S.EnergyCost = S.CooldownSeconds = 0;
    const auto* Mode = ModeOf(Owner);
    const float Power = Mode ? Mode->Power(CireSkillRuntime::Team(Owner)) : 1.f;
    if (const auto* H = Cast<ACireHero>(Owner))
    {
        const auto* Def = CireAbilityDB::Find(X->Id.ToString());
        S.CastRange = Def && Def->Range > 0 ? Def->Range + 60.f : 900.f;
        S.AttackDamage = FMath::Min(10000.f, (X->Damage + X->Scaling * H->PrimaryAttribute()) * Power);
        S.MaxHealth *= LevelScale(H, X->Id) * (H->HasSkill(TEXT("aether_engineering")) ? 1.25f : 1.f);
        if (H->HasSkill(TEXT("aether_engineering"))) S.LifetimeSeconds *= 1.2f;
        if (X->Kind == K::Pylon && H->HasSkill(TEXT("resonant_lattice"))) S.LifetimeSeconds *= 1.2f;
        if (X->Kind == K::Pylon) S.EffectMagnitude *= FMath::Min(1.6f, LevelScale(H, X->Id));
    }
    else if (const auto* M = Cast<ACireMonster>(Owner))
    {
        S.CastRange = 5000.f;
        S.AttackDamage = FMath::Min(10000.f, CireNPCCombat::EffectiveDamage(M) * FMath::Max(0.f, MonsterMultiplier) * X->Damage);
        S.MaxHealth *= FMath::Clamp(M->MaxHealth / 500.f, .6f, 4.f);
    }
    else return false;
    S.LifetimeSeconds = FMath::Clamp(S.LifetimeSeconds, .5f, 120.f);
    return ACireConstruct::ValidateSpec(S);
}

TArray<ACireConstruct*> CireTechConstructs::Deploy(AActor* Owner, FName RecipeId, FVector Aim, FString* Why, float MonsterMultiplier)
{
    TArray<ACireConstruct*> Out;
    const FCireTechRecipe* X = FindRecipe(RecipeId);
    FCireConstructSpec Spec;
    if (!X || !BuildSpec(Owner, RecipeId, Spec, MonsterMultiplier)) { if (Why) *Why = TEXT("This construct recipe is unavailable."); return Out; }
    const FString Name = [&] { const auto* Def = CireAbilityDB::Find(X->Id.ToString()); return Def && !X->bMonster ? Def->Name : X->Name; }();
    const FRotator Heading = (Aim - Owner->GetActorLocation()).GetSafeNormal2D().IsNearlyZero() ? Owner->GetActorRotation() : (Aim - Owner->GetActorLocation()).GetSafeNormal2D().Rotation();
    FString Error;
    for (int32 Index = 0; Index < X->Count; ++Index)
    {
        // A small ring around the aim point for multi-unit deploys (skitters), probing a few spots each.
        bool bPlaced = false;
        for (int32 Try = 0; Try < 6 && !bPlaced; ++Try)
        {
            const float Angle = (Index * 2.1f + Try * 1.1f) + Heading.Yaw * PI / 180.f;
            const float Radius = X->Count > 1 ? 70.f + 30.f * Try : 45.f * Try;
            const FVector Point = Aim + FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0) * Radius;
            if (auto* C = ACireConstruct::SpawnFor(Owner, Spec, Point, Heading, Name)) { Out.Add(C); bPlaced = true; }
            else { FVector Probe = Point; ACireConstruct::ValidatePlacementFor(Owner, Spec, Probe, Heading, &Error); }
        }
    }
    if (Out.IsEmpty() && Why) *Why = Error.IsEmpty() ? TEXT("No room to place this construct here.") : Error;
    return Out;
}

int32 CireTechConstructs::CountOwned(const AActor* Owner, FName Recipe)
{
    int32 N = 0;
    if (Owner) for (TActorIterator<ACireConstruct> It(Owner->GetWorld()); It; ++It)
        if (!It->IsActorBeingDestroyed() && It->GetSourceActor() == Owner && It->ConstructSpec.Recipe == Recipe) ++N;
    return N;
}

int32 CireTechConstructs::Overcharge(AActor* Owner, FVector Center, float Radius, float Seconds, float RepairFraction)
{
    int32 N = 0;
    if (!Owner || !Owner->HasAuthority()) return 0;
    const float Now = Owner->GetWorld()->GetTimeSeconds();
    for (TActorIterator<ACireConstruct> It(Owner->GetWorld()); It; ++It)
    {
        if (It->IsActorBeingDestroyed() || It->GetSourceActor() != Owner || !It->IsTech() || FVector::DistSquared2D(Center, It->GetActorLocation()) > FMath::Square(Radius)) continue;
        It->OverchargedUntil = FMath::Max(It->OverchargedUntil, Now + Seconds);
        It->Health = FMath::Min(It->MaxHealth, It->Health + It->MaxHealth * RepairFraction);
        It->ForceNetUpdate(); ++N;
    }
    return N;
}

void CireTechConstructs::OnSpawned(ACireConstruct* C)
{
    if (!C || !C->HasAuthority()) return;
    const FVector Ground = C->GetActorLocation() - FVector(0, 0, C->ConstructSpec.Height * .5f);
    // Warp-in cue at the spot (arcane runes: the energy school).
    Cue(C, nullptr, Ground, Ground, ECireSpellCue::Cast, FMath::Clamp(C->ConstructSpec.EffectRadius / 300.f, .5f, 2.f));
    if (C->ConstructSpec.Kind == K::Pylon)
    {
        // The field is a harmless persistent ground circle: school runes, replication and realm privacy come with it.
        FCireAreaSpec Field; Field.Shape = ECireAreaShape::Circle; Field.Radius = C->ConstructSpec.EffectRadius;
        Field.WarningSeconds = 0; Field.DurationSeconds = FMath::Min(60.f, C->ConstructSpec.LifetimeSeconds);
        Field.TickInterval = 1.f; Field.DamagePerSecond = 0; Field.BurstDamage = 0; Field.bPersistent = true; Field.bPoison = false;
        FLinearColor Tint = C->ConstructSpec.Color; Field.Color = FLinearColor(FMath::Min(Tint.R, 1.f), FMath::Min(Tint.G, 1.f), FMath::Min(Tint.B, 1.f), .18f);
        Field.AbilityName = C->AbilityName; Field.VerticalTolerance = 120;
        ACireAreaEffect::Spawn(C, Field, Ground, FRotator::ZeroRotator);
    }
    if (C->ConstructSpec.Kind == K::Turret) C->TechTimer = .35f;
    if (C->ConstructSpec.Kind == K::Pylon) C->TechTimer = 0.f;
}

void CireTechConstructs::OnExpired(ACireConstruct* C)
{
    if (!C || !C->HasAuthority()) return;
    // A skitter that runs out of time with an enemy beside it still goes off.
    if (C->ConstructSpec.Kind == K::Skitter && NearestEnemy(C, C->ConstructSpec.EffectRadius, false, false)) Detonate(C);
}

float CireTechConstructs::ModifyIncomingDamage(ACireConstruct* C, AActor*, float Amount)
{
    // Pylons are warded crystal (15% less damage); everything else takes full damage.
    return C && C->ConstructSpec.Kind == K::Pylon ? Amount * .85f : Amount;
}

int32 CireTechConstructs::Detonate(ACireConstruct* C)
{
    if (!C || !C->HasAuthority() || C->bTriggered) return 0;
    C->bTriggered = true;
    const FCireConstructSpec& S = C->ConstructSpec;
    AActor* Owner = C->GetSourceActor();
    const bool bDatabase = CireAbilityDB::FindByName(C->AbilityName) != nullptr; // champion recipes: CC comes from the Ability DB effects
    TArray<AActor*> List; Units(C->GetWorld(), List, true);
    int32 Hit = 0;
    for (AActor* U : List)
    {
        if (U == C || !IsEnemy(C, U) || !Near(C, U, FMath::Max(S.EffectRadius, S.TriggerRadius))) continue;
        const float Applied = S.AttackDamage > 0 ? CireCombat::ApplyDamage(Owner, U, S.AttackDamage, C->AbilityName) : 0.f;
        if (auto* M = Cast<ACireMonster>(U)) CireKits::AddConstructThreat(M, C, FMath::Max(Applied, 1.f)); // scaling-kits: monsters hunt the construct
        ++Hit;
        const float Seconds = S.EffectMagnitude;
        const bool bBoss = [&] { const auto* M = Cast<ACireMonster>(U); return M && M->IsLaneBoss(); }();
        if (S.Effect == TEXT("stasis"))
        {
            if (bBoss) CireCrowdControl::Slow(U, Seconds + 1.f, Owner);
            else if (!bDatabase || Applied <= 0)
            {
                if (Cast<ACireHero>(U) && C->bMonsterOwned) CireBuffs::Apply(U, RootedId, Seconds, Owner); // monster stasis mines root champions
                else CireCrowdControl::Stun(U, Seconds, Owner);
            }
        }
        else if (S.Effect == TEXT("silence") && (!bDatabase || Applied <= 0)) CireCrowdControl::Silence(U, Seconds, Owner);
    }
    const FVector At = C->GetActorLocation() - FVector(0, 0, S.Height * .5f);
    Cue(C, nullptr, At, At, ECireSpellCue::Impact, FMath::Clamp(S.EffectRadius / 180.f, .6f, 2.5f));
    UE_LOG(LogCireTech, Verbose, TEXT("CIRE_TECH_DETONATE %s hit=%d"), *C->AbilityName, Hit);
    C->CollisionBox->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    C->Destroy();
    return Hit;
}

bool CireTechConstructs::TickConstruct(ACireConstruct* C, float Dt)
{
    if (!C || C->IsActorBeingDestroyed()) return false;
    const FCireConstructSpec& S = C->ConstructSpec;
    AActor* Owner = C->GetSourceActor();
    UWorld* World = C->GetWorld();
    const float Now = World->GetTimeSeconds();
    switch (S.Kind)
    {
    case K::Turret:
    {
        C->TechTimer -= Dt * (C->OverchargedUntil > Now ? 2.f : 1.f);
        if (C->TechTimer > 0) return true;
        AActor* Target = NearestEnemy(C, S.AttackRange, true, true);
        if (!Target) { C->TechTimer = .2f; return true; }
        C->TechTimer = CireKits::InheritedInterval(C, S.AttackInterval); // scaling-kits: the owner's attack speed
        const FVector Dir = (Target->GetActorLocation() - C->GetActorLocation()).GetSafeNormal2D();
        if (!Dir.IsNearlyZero()) C->SetActorRotation(Dir.Rotation());
        const FVector Head = C->GetActorLocation() + FVector(0, 0, S.Height * .4f);
        Cue(C, Target, Head, Target->GetActorLocation(), ECireSpellCue::Launch, S.SplashRadius > 0 ? 1.4f : .8f);
        if (S.AttackDamage > 0)
        {
            const float Dealt = CireCombat::ApplyStrike(Owner, Target, S.AttackDamage, C->AbilityName);
            if (auto* M = Cast<ACireMonster>(Target)) CireKits::AddConstructThreat(M, C, FMath::Max(Dealt, 1.f) * 2.f); // scaling-kits: turret threat
            if (S.SplashRadius > 0)
            {
                TArray<AActor*> List; Units(World, List, true);
                for (AActor* U : List) if (U != Target && U != C && IsEnemy(C, U) && Near(Target, U, S.SplashRadius))
                    CireCombat::ApplyDamage(Owner, U, S.AttackDamage * .5f, C->AbilityName);
            }
        }
        ++C->ShotSerial; C->ForceNetUpdate();
        return !C->IsActorBeingDestroyed();
    }
    case K::Trap:
    {
        if (C->GetAge() < .6f) return true; // arming
        C->TechTimer -= Dt; if (C->TechTimer > 0) return true; C->TechTimer = .1f;
        if (NearestEnemy(C, S.TriggerRadius, false, false)) { Detonate(C); return false; }
        return true;
    }
    case K::Skitter:
    {
        AActor* Target = C->SeekTarget.Get();
        if (!IsValid(Target) || !IsEnemy(C, Target) || !Near(C, Target, S.AttackRange * 1.5f, 400.f))
        {
            C->TechTimer -= Dt;
            if (C->TechTimer <= 0) { C->TechTimer = .25f; Target = NearestEnemy(C, S.AttackRange, false, false); C->SeekTarget = Target; }
            else Target = nullptr;
        }
        if (!IsValid(Target)) return true;
        if (Near(C, Target, S.TriggerRadius, 250.f)) { Detonate(C); return false; }
        const FVector To = (Target->GetActorLocation() - C->GetActorLocation()).GetSafeNormal2D();
        FVector Next = C->GetActorLocation() + To * S.MoveSpeed * Dt;
        FHitResult Floor; FCollisionQueryParams Q(SCENE_QUERY_STAT(CireSkitterFloor), false, C);
        if (World->LineTraceSingleByObjectType(Floor, Next + FVector(0, 0, 120), Next - FVector(0, 0, 300), FCollisionObjectQueryParams(ECC_WorldStatic), Q) && Floor.ImpactNormal.Z > .6f)
        {
            Next.Z = Floor.ImpactPoint.Z + S.Height * .5f + 2.f;
            // Solid walls and summoned walls stop the runner (it waits beside them).
            if (!ACireConstruct::FindBlockingConstruct(C, Next)) C->SetActorLocationAndRotation(Next, To.Rotation(), false);
        }
        return true;
    }
    case K::Pylon:
    {
        C->TechTimer -= Dt; if (C->TechTimer > 0) return true; C->TechTimer = FMath::Max(.2f, S.AttackInterval);
        const float Hold = C->TechTimer + .35f;
        const uint8 Stacks = static_cast<uint8>(FMath::Clamp(FMath::RoundToInt(S.EffectMagnitude * 100.f), 1, 250));
        TArray<AActor*> List; Units(World, List, false);
        ACireHero* HeroOwner = OwnerHero(C);
        for (AActor* U : List)
        {
            if (!Near(C, U, S.EffectRadius, 250.f)) continue;
            const bool bAlly = IsAlly(C, U), bEnemy = !bAlly && IsEnemy(C, U);
            if (S.Effect == TEXT("shield") || S.Effect == TEXT("nexus"))
            {
                if (bAlly)
                {
                    const float Max = [&] { if (const auto* H = Cast<ACireHero>(U)) return H->MaxHealth; if (const auto* M = Cast<ACireMonster>(U)) return M->MaxHealth; return 0.f; }();
                    const float Amount = Max * S.EffectMagnitude * C->TechTimer;
                    if (auto* H = Cast<ACireHero>(U); H && HeroOwner) CireCombat::ApplyHealing(HeroOwner, H, Amount, C->AbilityName);
                    else if (auto* M = Cast<ACireMonster>(U)) { M->Health = FMath::Min(M->MaxHealth, M->Health + Amount); }
                    // balance: the damage reduction is Ability DB data (aether_nexus "guard", aegis_pylon "shield" magnitude).
                    static const auto Reduction = [](const TCHAR* Skill, const TCHAR* Type, float Fallback)
                    {
                        if (const FCireAbilityDef* D = CireAbilityDB::Find(Skill)) for (const FCireAbilityEffect& E : D->Effects) if (E.Type == Type && E.Magnitude > 0) return E.Magnitude;
                        return Fallback;
                    };
                    const bool bNexus = S.Effect == TEXT("nexus");
                    const float Percent = 100.f * (bNexus ? Reduction(TEXT("aether_nexus"), TEXT("guard"), .4f) : Reduction(TEXT("aegis_pylon"), TEXT("shield"), .15f));
                    CireBuffs::Apply(U, bNexus ? NexusId : AegisId, Hold, Owner, static_cast<uint8>(FMath::Clamp(FMath::RoundToInt(Percent), 1, 80)));
                }
                else if (bEnemy && S.Effect == TEXT("nexus")) CireCrowdControl::Slow(U, Hold, Owner);
            }
            else if (S.Effect == TEXT("haste") && bAlly) CireBuffs::Apply(U, HasteId, Hold, Owner, Stacks);
            else if (S.Effect == TEXT("weaken") && bEnemy) CireBuffs::Apply(U, WeakenedId, Hold, Owner, Stacks);
            else if (S.Effect == TEXT("slow") && bEnemy) CireCrowdControl::Slow(U, Hold, Owner);
            else if (S.Effect == TEXT("empower") && bAlly) CireBuffs::Apply(U, EmpoweredId, Hold, Owner, Stacks);
        }
        return true;
    }
    default: return true;
    }
}

// ------------------------------------------------------------------------------------------ monsters
bool CireTechConstructs::MonsterHandleConstructs(ACireMonster* M, bool bHasVictimInReach)
{
    if (!M || !M->HasAuthority() || bHasVictimInReach) return false;
    ACireConstruct* Best = nullptr; double BestD = FMath::Square(650.f);
    for (TActorIterator<ACireConstruct> It(M->GetWorld()); It; ++It)
    {
        if (It->IsActorBeingDestroyed() || !It->IsTech() || It->ConstructSpec.Kind == K::Trap || It->ConstructSpec.Kind == K::Skitter || !It->CanBeDamagedBy(M)) continue;
        const double D = FVector::DistSquared2D(M->GetActorLocation(), It->GetActorLocation());
        if (D < BestD) { Best = *It; BestD = D; }
    }
    if (!Best) return false;
    const FVector Dir = (Best->GetActorLocation() - M->GetActorLocation()).GetSafeNormal2D();
    const float Reach = M->GetCapsuleComponent()->GetScaledCapsuleRadius() + Best->ConstructSpec.Width * .5f + 90.f;
    if (BestD > FMath::Square(Reach)) { M->AddMovementInput(Dir); return true; }
    M->GetCharacterMovement()->StopMovementImmediately();
    if (!Dir.IsNearlyZero()) M->SetActorRotation(Dir.Rotation());
    if (M->AttackTimer <= 0)
    {
        M->AttackTimer = 1.8f;
        CireCombat::ApplyDamage(M, Best, CireNPCCombat::EffectiveDamage(M), TEXT("Smash construct"));
        CireCombat::PlayCue(M, Best, TEXT("npc_wall_strike"), M->GetActorLocation(), Best->GetActorLocation(), ECireSpellCue::Impact, .8f, true);
    }
    return true;
}

// ------------------------------------------------------------------------------------------ presentation
void CireTechConstructs::ApplyAppearance(ACireConstruct* C)
{
    if (!C || C->GetNetMode() == NM_DedicatedServer) return;
    const FCireConstructSpec& S = C->ConstructSpec;
    UStaticMeshComponent* Body = C->BodyMesh; UStaticMeshComponent* Core = C->CoreMesh; UStaticMeshComponent* Crown = C->CrownMesh;
    const float HalfH = S.Height * .5f, Foot = S.Width;
    Core->SetVisibility(true); Crown->SetVisibility(true);
    Body->SetRelativeRotation(FRotator::ZeroRotator); Core->SetRelativeRotation(FRotator::ZeroRotator); Crown->SetRelativeRotation(FRotator::ZeroRotator);
    const FName Kind = S.Kind == K::Turret ? FName(S.Recipe == TEXT("warp_obelisk") ? TEXT("AetherObelisk") : TEXT("AetherTurret")) : S.Kind == K::Pylon ? FName(TEXT("AetherPylon")) :
        S.Kind == K::Skitter ? FName(TEXT("SkitterBomb")) : S.Effect == TEXT("silence") ? FName(TEXT("SpiritLantern")) : FName(TEXT("AetherTrap"));
    if (UStaticMesh* Mesh = Authored(Kind))
    {
        // Authored body: pivot at the ground, sized to the footprint height; the crystal/core stays a tinted part.
        Body->SetStaticMesh(Mesh);
        const FBoxSphereBounds B = Mesh->GetBounds();
        const float Scale = S.Height / FMath::Max(1.f, static_cast<float>(B.BoxExtent.Z * 2));
        Body->SetRelativeScale3D(FVector(Scale)); Body->SetRelativeLocation(FVector(0, 0, -HalfH - (B.Origin.Z - B.BoxExtent.Z) * Scale));
    }
    else if (S.Kind == K::Turret)
    {
        Body->SetStaticMesh(Shape(TEXT("Cylinder"))); Body->SetRelativeScale3D(FVector(Foot / 100.f, Foot / 100.f, S.Height * .55f / 100.f));
        Body->SetRelativeLocation(FVector(0, 0, -HalfH + S.Height * .275f)); Alloy(Body, S.Color);
    }
    else if (S.Kind == K::Pylon)
    {
        Body->SetStaticMesh(Shape(TEXT("Cylinder"))); Body->SetRelativeScale3D(FVector(Foot * .9f / 100.f, Foot * .9f / 100.f, S.Height * .22f / 100.f));
        Body->SetRelativeLocation(FVector(0, 0, -HalfH + S.Height * .11f)); Alloy(Body, S.Color);
    }
    else if (S.Kind == K::Skitter)
    {
        Body->SetStaticMesh(Shape(TEXT("Sphere"))); Body->SetRelativeScale3D(FVector(Foot * 1.1f / 100.f, Foot * .9f / 100.f, S.Height * .7f / 100.f));
        Body->SetRelativeLocation(FVector(0, 0, -HalfH * .2f)); Alloy(Body, S.Color);
    }
    else
    {
        const bool bLantern = S.Effect == TEXT("silence");
        Body->SetStaticMesh(Shape(bLantern ? TEXT("Cube") : TEXT("Cylinder")));
        Body->SetRelativeScale3D(bLantern ? FVector(.28f, .28f, S.Height * .8f / 100.f) : FVector(Foot / 100.f, Foot / 100.f, .08f));
        Body->SetRelativeLocation(bLantern ? FVector(0, 0, -HalfH + S.Height * .4f) : FVector(0, 0, -HalfH + 4.f)); Alloy(Body, S.Color);
    }
    // Energy core (turret eye, pylon crystal, trap glyph, skitter eye) and crown (barrel, halo, legs).
    if (S.Kind == K::Turret)
    {
        Core->SetStaticMesh(Shape(TEXT("Sphere"))); Core->SetRelativeScale3D(FVector(Foot * .7f / 100.f)); Core->SetRelativeLocation(FVector(0, 0, HalfH - Foot * .35f));
        Crown->SetStaticMesh(Shape(TEXT("Cylinder"))); Crown->SetRelativeScale3D(FVector(.12f, .12f, Foot * .9f / 100.f));
        Crown->SetRelativeLocation(FVector(Foot * .45f, 0, HalfH - Foot * .35f)); Crown->SetRelativeRotation(FRotator(-90, 0, 0)); Alloy(Crown, S.Color);
    }
    else if (S.Kind == K::Pylon)
    {
        Core->SetStaticMesh(Shape(TEXT("Cone"))); Core->SetRelativeScale3D(FVector(Foot * .55f / 100.f, Foot * .55f / 100.f, S.Height * .55f / 100.f));
        Core->SetRelativeLocation(FVector(0, 0, HalfH * .25f));
        Crown->SetStaticMesh(Shape(TEXT("Cylinder"))); Crown->SetRelativeScale3D(FVector(Foot * 1.5f / 100.f, Foot * 1.5f / 100.f, .025f));
        Crown->SetRelativeLocation(FVector(0, 0, HalfH * .1f)); Energize(Crown, S.Color, .6f);
    }
    else if (S.Kind == K::Skitter)
    {
        Core->SetStaticMesh(Shape(TEXT("Sphere"))); Core->SetRelativeScale3D(FVector(.14f)); Core->SetRelativeLocation(FVector(Foot * .45f, 0, 2));
        Crown->SetStaticMesh(Shape(TEXT("Cube"))); Crown->SetRelativeScale3D(FVector(Foot * .5f / 100.f, Foot * 1.5f / 100.f, .04f));
        Crown->SetRelativeLocation(FVector(0, 0, -HalfH * .55f)); Alloy(Crown, S.Color);
    }
    else
    {
        const bool bLantern = S.Effect == TEXT("silence");
        Core->SetStaticMesh(Shape(TEXT("Sphere"))); Core->SetRelativeScale3D(FVector(bLantern ? .2f : .16f));
        Core->SetRelativeLocation(FVector(0, 0, bLantern ? -HalfH + S.Height * .45f : -HalfH + 10.f));
        Crown->SetStaticMesh(Shape(TEXT("Cylinder"))); Crown->SetRelativeScale3D(FVector(S.TriggerRadius * 2 / 100.f, S.TriggerRadius * 2 / 100.f, .01f));
        Crown->SetRelativeLocation(FVector(0, 0, -HalfH + 1.f)); Energize(Crown, S.Color, .18f);
    }
    Energize(Core, S.Color, 1.f);
    for (UStaticMeshComponent* Part : {Body, Core, Crown}) { Part->SetCastShadow(Part != Crown); }
}

void CireTechConstructs::AnimateAppearance(ACireConstruct* C, float Time)
{
    if (!C) return;
    const FCireConstructSpec& S = C->ConstructSpec;
    const float HalfH = S.Height * .5f;
    if (C->ShotSerial != C->SeenShotSerial) { C->SeenShotSerial = C->ShotSerial; C->Recoil = 1.f; }
    C->Recoil = FMath::Max(0.f, C->Recoil - .06f);
    const bool bOver = C->OverchargedUntil > (C->GetWorld() ? C->GetWorld()->GetTimeSeconds() : 0.f);
    if (S.Kind == K::Turret)
    {
        const float Pulse = 1.f + .08f * FMath::Sin(Time * (bOver ? 14.f : 5.f)) + .25f * C->Recoil;
        C->CoreMesh->SetRelativeScale3D(FVector(S.Width * .7f / 100.f * Pulse));
        C->CrownMesh->SetRelativeLocation(FVector(S.Width * .45f - 10.f * C->Recoil, 0, HalfH - S.Width * .35f));
    }
    else if (S.Kind == K::Pylon)
    {
        C->CoreMesh->SetRelativeLocation(FVector(0, 0, HalfH * .25f + 8.f * FMath::Sin(Time * 1.7f)));
        C->CoreMesh->SetRelativeRotation(FRotator(0, Time * 40.f, 0));
        C->CrownMesh->SetRelativeRotation(FRotator(0, -Time * 25.f, 0));
        const float Breath = 1.f + .06f * FMath::Sin(Time * 2.3f);
        C->CrownMesh->SetRelativeScale3D(FVector(S.Width * 1.5f / 100.f * Breath, S.Width * 1.5f / 100.f * Breath, .025f));
    }
    else if (S.Kind == K::Skitter)
    {
        C->CrownMesh->SetRelativeRotation(FRotator(0, 0, 12.f * FMath::Sin(Time * 26.f)));
        C->CoreMesh->SetRelativeScale3D(FVector(.14f * (1.f + .3f * FMath::Abs(FMath::Sin(Time * 9.f)))));
    }
    else
    {
        const float Blink = Time < .6f ? .5f : 1.f + .35f * FMath::Sin(Time * 4.f);
        C->CoreMesh->SetRelativeScale3D(FVector((S.Effect == TEXT("silence") ? .2f : .16f) * Blink));
    }
}
