// ability-expansion: generic delivery recipes for the expansion ability pool (Content/Data/AbilitiesExpansion.json).
#include "CireAbilityExpansion.h"
#include "CireActorIterator.h"
#include "CireAbilityDB.h"
#include "CireAbilityShapes.h"
#include "CireAreaEffects.h"
#include "CireBuffs.h"
#include "CireCombatEvents.h"
#include "CireConstruct.h"
#include "CireCrowdControl.h"
#include "CireDeveloperTools.h"
#include "CireGame.h"
#include "CireItems.h"
#include "CireKitSkills.h"
#include "CireInitiation.h" // initiation: Set-up synergy, blink lockout
#include "CireSkillCasting.h" // casting-rules: PlacementAim
#include "CireScalingKits.h"
#include "CireSkillRuntime.h"
#include "CireSkillShop.h"
#include "CireSkillshot.h"
#include "CireSkillTuning.h"
#include "CireSummon.h"
#include "CireTechConstructs.h"
#include "CireThreat.h"
#include "Components/CapsuleComponent.h"
#include "Dom/JsonObject.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogCireExpansion, Log, All);

namespace CireXpDetail
{
enum class EDel : uint8
{
    Passive, Bolt, Pierce, Line, Cone, Circle, Zone, Nova, Chain, Strike, Leap, Dash, Heal, HealZone, Barrier,
    SelfBuff, PartyBuff, Summon, Construct, Wall, Barrage,
    Vacuum, Charge, Hook, Cage // initiation: pull-together, gap-close slam, hook, trapping wall ring
};
struct FDelName { const TCHAR* Name; EDel D; };
const FDelName DelNames[] = {
    {TEXT("passive"), EDel::Passive}, {TEXT("bolt"), EDel::Bolt}, {TEXT("pierce"), EDel::Pierce}, {TEXT("line"), EDel::Line},
    {TEXT("cone"), EDel::Cone}, {TEXT("circle"), EDel::Circle}, {TEXT("zone"), EDel::Zone}, {TEXT("nova"), EDel::Nova},
    {TEXT("chain"), EDel::Chain}, {TEXT("strike"), EDel::Strike}, {TEXT("leap"), EDel::Leap}, {TEXT("dash"), EDel::Dash},
    {TEXT("heal"), EDel::Heal}, {TEXT("healZone"), EDel::HealZone}, {TEXT("barrier"), EDel::Barrier}, {TEXT("selfBuff"), EDel::SelfBuff},
    {TEXT("partyBuff"), EDel::PartyBuff}, {TEXT("summon"), EDel::Summon}, {TEXT("construct"), EDel::Construct}, {TEXT("wall"), EDel::Wall},
    {TEXT("barrage"), EDel::Barrage}, {TEXT("vacuum"), EDel::Vacuum}, {TEXT("charge"), EDel::Charge}, {TEXT("hook"), EDel::Hook}, {TEXT("cage"), EDel::Cage}};

/** One expansion row's delivery recipe (everything else is the Ability DB row). */
struct FRecipe
{
    FString Id, DeliveryName;
    EDel D = EDel::Passive;
    float Angle = 60, Warning = 0, Speed = 0;
    int32 Hits = 1;
    // Riders on every hit of this skill.
    float Knockback = 0, Pull = 0, Bleed = 0, Lifesteal = 0, ExecuteBelow = 0, ExecuteBonus = 0;
    // Buffs (selfBuff / partyBuff / zones): record + value (%); value 0 = the skill's scaled headline effect.
    FName Buff; float BuffValue = 0, BuffSeconds = 0;
    float BarrierFraction = 0;       // heals / party buffs: also grant a barrier of this fraction of the amount
    bool bCleanse = false;           // party buff / heal: clears slows and roots
    // Summons.
    int32 Count = 1, Visual = 0; float SummonHealth = 300, SummonHealthPrimary = 6, MoveSpeed = 430, AttackRange = 180;
    bool bCommandable = false; FString SummonName;
    // Barrage (ultimates): waves of small circles over the aimed area.
    int32 Waves = 5; float Interval = .45f, SubRadius = 220;
    // Passives.
    FName Hook; float Chance = 0, Threshold = 0;
    // initiation: engage riders. PullCenter = fraction of the distance to the cast centre pulled per hit; Knockup = launch
    // speed (cm/s); Echo = extra damage per additional enemy caught by a nova; Cage = trapping wall ring; Setup = Set-up debuff.
    float PullCenter = 0, Knockup = 0, Echo = 0, CageSeconds = 3.5f; int32 CageSegments = 10; bool bSetup = false;
    // Constructs: the tech recipe row (kept as JSON; parsed by AppendConstructRecipes).
    TSharedPtr<FJsonObject> Construct;
};

struct FTable { TMap<FString, FRecipe> Rows; TArray<FString> Ids; bool bLoaded = false; };
FTable& Table() { static FTable T; return T; }

float Num(const TSharedPtr<FJsonObject>& J, const TCHAR* Key, float Default = 0) { double V = Default; return J && J->TryGetNumberField(Key, V) && FMath::IsFinite(V) ? static_cast<float>(V) : Default; }
FString Str(const TSharedPtr<FJsonObject>& J, const TCHAR* Key) { FString V; if (J) J->TryGetStringField(Key, V); return V; }

void Load()
{
    FTable& T = Table(); T = FTable(); T.bLoaded = true;
    FString Text; TSharedPtr<FJsonObject> Root;
    if (!FFileHelper::LoadFileToString(Text, *FPaths::Combine(FPaths::ProjectContentDir(), TEXT("Data/AbilitiesExpansion.json"))) ||
        !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) || !Root.IsValid()) return;
    const TSharedPtr<FJsonObject>* Abilities = nullptr;
    if (!Root->TryGetObjectField(TEXT("abilities"), Abilities)) return;
    for (const auto& Pair : (*Abilities)->Values)
    {
        const TSharedPtr<FJsonObject>* Row = nullptr; const TSharedPtr<FJsonObject>* Rec = nullptr;
        if (!Pair.Value->TryGetObject(Row) || !(*Row)->TryGetObjectField(TEXT("recipe"), Rec)) continue;
        const TSharedPtr<FJsonObject>& J = *Rec;
        FRecipe R; R.Id = FString(Pair.Key); R.DeliveryName = Str(J, TEXT("delivery"));
        bool bKnown = false;
        for (const FDelName& N : DelNames) if (R.DeliveryName == N.Name) { R.D = N.D; bKnown = true; }
        if (!bKnown) { UE_LOG(LogCireExpansion, Warning, TEXT("Unknown delivery %s for %s"), *R.DeliveryName, *R.Id); continue; }
        R.Angle = FMath::Clamp(Num(J, TEXT("angle"), 60), 10.f, 170.f); R.Warning = FMath::Clamp(Num(J, TEXT("warning")), 0.f, 3.f);
        R.Speed = FMath::Clamp(Num(J, TEXT("speed")), 0.f, 6000.f); R.Hits = FMath::Clamp(static_cast<int32>(Num(J, TEXT("hits"), 1)), 1, 12);
        R.Knockback = FMath::Clamp(Num(J, TEXT("knockback")), 0.f, 800.f); R.Pull = FMath::Clamp(Num(J, TEXT("pull")), 0.f, 1.f);
        R.Bleed = FMath::Clamp(Num(J, TEXT("bleed")), 0.f, 2.f); R.Lifesteal = FMath::Clamp(Num(J, TEXT("lifesteal")), 0.f, 1.f);
        R.ExecuteBelow = FMath::Clamp(Num(J, TEXT("executeBelow")), 0.f, .5f); R.ExecuteBonus = FMath::Clamp(Num(J, TEXT("executeBonus")), 0.f, 3.f);
        const FString Buff = Str(J, TEXT("buff")); R.Buff = Buff.IsEmpty() ? NAME_None : FName(*Buff);
        R.BuffValue = FMath::Clamp(Num(J, TEXT("buffValue")), 0.f, 200.f); R.BuffSeconds = FMath::Clamp(Num(J, TEXT("buffSeconds")), 0.f, 60.f);
        R.BarrierFraction = FMath::Clamp(Num(J, TEXT("barrier")), 0.f, 3.f);
        J->TryGetBoolField(TEXT("cleanse"), R.bCleanse);
        R.Count = FMath::Clamp(static_cast<int32>(Num(J, TEXT("count"), 1)), 1, 6); R.Visual = FMath::Clamp(static_cast<int32>(Num(J, TEXT("visual"))), 0, 3);
        R.SummonHealth = FMath::Clamp(Num(J, TEXT("health"), 300), 10.f, 20000.f); R.SummonHealthPrimary = FMath::Clamp(Num(J, TEXT("healthPrimary"), 6), 0.f, 100.f);
        R.MoveSpeed = FMath::Clamp(Num(J, TEXT("moveSpeed"), 430), 100.f, 1200.f); R.AttackRange = FMath::Clamp(Num(J, TEXT("attackRange"), 180), 80.f, 1400.f);
        J->TryGetBoolField(TEXT("commandable"), R.bCommandable); R.SummonName = Str(J, TEXT("summonName"));
        R.Waves = FMath::Clamp(static_cast<int32>(Num(J, TEXT("waves"), 5)), 1, 20); R.Interval = FMath::Clamp(Num(J, TEXT("interval"), .45f), .1f, 3.f);
        R.SubRadius = FMath::Clamp(Num(J, TEXT("subRadius"), 220), 60.f, 800.f);
        const FString Hook = Str(J, TEXT("hook")); R.Hook = Hook.IsEmpty() ? NAME_None : FName(*Hook);
        R.Chance = FMath::Clamp(Num(J, TEXT("chance")), 0.f, 1.f); R.Threshold = FMath::Clamp(Num(J, TEXT("threshold")), 0.f, 1.f);
        R.PullCenter = FMath::Clamp(Num(J, TEXT("pullCenter")), 0.f, 1.f); R.Knockup = FMath::Clamp(Num(J, TEXT("knockup")), 0.f, 1600.f);
        R.Echo = FMath::Clamp(Num(J, TEXT("echo")), 0.f, 1.f); R.CageSeconds = FMath::Clamp(Num(J, TEXT("cageSeconds"), 3.5f), .5f, 10.f);
        R.CageSegments = FMath::Clamp(static_cast<int32>(Num(J, TEXT("cageSegments"), 10)), 4, 20); J->TryGetBoolField(TEXT("setup"), R.bSetup);
        const TSharedPtr<FJsonObject>* C = nullptr; if (J->TryGetObjectField(TEXT("construct"), C)) R.Construct = *C;
        T.Ids.Add(R.Id); T.Rows.Add(R.Id, MoveTemp(R));
    }
    T.Ids.Sort();
    UE_LOG(LogCireExpansion, Display, TEXT("CIRE_ABILITY_EXPANSION_LOADED recipes=%d"), T.Ids.Num());
}
const FTable& Loaded() { FTable& T = Table(); if (!T.bLoaded) Load(); return T; }
const FRecipe* Find(const FString& Id) { return Loaded().Rows.Find(Id); }

const FName EmpoweredId(TEXT("xp_empowered")), FortifiedId(TEXT("xp_fortified")), HastenedId(TEXT("xp_hastened")), ExposedId(TEXT("xp_exposed")),
    BleedingId(TEXT("xp_bleeding")), RootedId(TEXT("npc_rooted")), CowedId(TEXT("bear_cowed")), TauntedId(TEXT("taunted"));

ACireGameMode* ModeOf(const AActor* A) { return A && A->GetWorld() ? A->GetWorld()->GetAuthGameMode<ACireGameMode>() : nullptr; }
float Now(const UWorld* W) { return W ? W->GetTimeSeconds() : 0.f; }
bool IsBoss(const AActor* U) { const auto* M = Cast<ACireMonster>(U); return M && (M->IsLaneBoss() || M->bBoss); }
float Body(const AActor* U) { const auto* C = Cast<ACharacter>(U); return C ? C->GetCapsuleComponent()->GetScaledCapsuleRadius() : 30.f; }
FVector FeetOf(const AActor* U) { const auto* C = Cast<ACharacter>(U); return U->GetActorLocation() - FVector(0, 0, C ? C->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() : 0.f); }
float HealthFraction(const AActor* U)
{
    if (const auto* H = Cast<ACireHero>(U)) return H->Health / FMath::Max(1.f, H->MaxHealth);
    if (const auto* M = Cast<ACireMonster>(U)) return M->Health / FMath::Max(1.f, M->MaxHealth);
    return 1.f;
}
const FCireBuffEntry* ActiveBuff(const AActor* Unit, FName Id)
{
    if (!Unit || !CireBuffs::IsActive(Unit, Id)) return nullptr;
    const auto* State = CireBuffs::Get(Unit); return State ? State->Find(Id) : nullptr;
}
float BuffFraction(const AActor* Unit, FName Id) { const auto* E = ActiveBuff(Unit, Id); return E ? E->Stacks / 100.f : 0.f; }
int32 Percent(float Value) { return FMath::Clamp(FMath::RoundToInt(Value), 1, 250); }
bool Sight(const AActor* From, const AActor* To)
{
    if (!IsValid(From) || !IsValid(To)) return false;
    FHitResult Hit; FCollisionQueryParams Q(SCENE_QUERY_STAT(CireExpansionSight), false, From);
    const bool bBlocked = From->GetWorld()->LineTraceSingleByChannel(Hit, From->GetActorLocation() + FVector(0, 0, 45), To->GetActorLocation() + FVector(0, 0, 35), ECC_Visibility, Q);
    return !bBlocked || Hit.GetActor() == To;
}
TArray<AActor*> Enemies(ACireHero* Hero, FVector Center, float Radius)
{
    TArray<AActor*> Out;
    auto Consider = [&](AActor* U)
    {
        if (!U || U == Hero || !CireCombat::AreHostile(Hero, U) || !CireCombat::IsAlive(U)) return;
        if (FVector::DistSquared2D(Center, U->GetActorLocation()) <= FMath::Square(Radius + Body(U)) && FMath::Abs(U->GetActorLocation().Z - Center.Z) < 400.f) Out.Add(U);
    };
    if (auto* Mode = ModeOf(Hero)) for (auto* M : Mode->Monsters) if (IsValid(M)) Consider(M);
    for (TCireActorIterator<ACireHero> It(Hero->GetWorld()); It; ++It) Consider(*It);
    return Out;
}
TArray<ACireHero*> Allies(const ACireHero* Hero, FVector Center, float Radius, bool bSelf = true)
{
    TArray<ACireHero*> Out;
    if (!Hero) return Out;
    for (TCireActorIterator<ACireHero> It(Hero->GetWorld()); It; ++It)
    {
        ACireHero* A = *It;
        if (A->IsA<ACireSummon>() || !A->bDrafted || A->bDead || A->TeamId != Hero->TeamId || (!bSelf && A == Hero)) continue;
        if (FVector::DistSquared2D(Center, A->GetActorLocation()) <= FMath::Square(Radius + Body(A)) && FMath::Abs(A->GetActorLocation().Z - Center.Z) < 400.f) Out.Add(A);
    }
    return Out;
}
float SegmentDistance(FVector P, FVector A, FVector B)
{
    const FVector2D AB(B.X - A.X, B.Y - A.Y), AP(P.X - A.X, P.Y - A.Y);
    const float Len2 = FMath::Max(1.f, static_cast<float>(AB.SizeSquared()));
    const float T = FMath::Clamp(static_cast<float>(FVector2D::DotProduct(AP, AB)) / Len2, 0.f, 1.f);
    return static_cast<float>((AP - AB * T).Size());
}
bool GroundAt(ACireHero* Hero, FVector& Aim)
{
    FCollisionQueryParams Query(SCENE_QUERY_STAT(CireExpansionGround), false, Hero);
    FHitResult Hit;
    if (!Hero->GetWorld()->LineTraceSingleByObjectType(Hit, Aim + FVector(0, 0, 300), Aim - FVector(0, 0, 600), FCollisionObjectQueryParams(ECC_WorldStatic), Query) || Hit.ImpactNormal.Z < .7f) return false;
    Aim = Hit.ImpactPoint; return true;
}
bool GroundSight(ACireHero* Hero, FVector Ground)
{
    FCollisionQueryParams Query(SCENE_QUERY_STAT(CireExpansionPlacement), false, Hero);
    FHitResult Hit;
    return !Hero->GetWorld()->LineTraceSingleByObjectType(Hit, Hero->GetActorLocation() + FVector(0, 0, 35), Ground + FVector(0, 0, 60), FCollisionObjectQueryParams(ECC_WorldStatic), Query) &&
        !ACireConstruct::FindBlockingConstruct(Hero, Ground);
}
float FreeTravel(ACireHero* Hero, FVector Dir, float Distance)
{
    const FVector From = Hero->GetActorLocation();
    FCollisionQueryParams Q(SCENE_QUERY_STAT(CireExpansionDash), false, Hero);
    FHitResult Hit;
    float Free = Distance;
    if (Hero->GetWorld()->SweepSingleByObjectType(Hit, From, From + Dir * Distance, FQuat::Identity, FCollisionObjectQueryParams(ECC_WorldStatic), FCollisionShape::MakeSphere(Body(Hero) * .8f), Q))
        Free = FMath::Max(0.f, static_cast<float>(Hit.Distance) - 10.f);
    for (float Step = 60.f; Step <= Free; Step += 60.f)
        if (ACireConstruct::FindBlockingConstruct(Hero, From + Dir * Step)) { Free = FMath::Max(0.f, Step - 70.f); break; }
    return Free;
}
bool MoveTo(ACireHero* Hero, FVector To)
{
    const FVector From = Hero->GetActorLocation();
    To.Z = From.Z;
    FVector Ground = To; if (GroundAt(Hero, Ground)) To.Z = Ground.Z + Hero->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 2.f;
    const bool bMoved = Hero->SetActorLocation(To, true, nullptr, ETeleportType::TeleportPhysics);
    const FVector Dir = (To - From).GetSafeNormal2D();
    if (bMoved && !Dir.IsNearlyZero()) Hero->SetActorRotation(Dir.Rotation());
    return bMoved;
}
void Later(UWorld* World, float Seconds, TFunction<void()> Work)
{
    if (!World) return;
    FTimerHandle Handle;
    World->GetTimerManager().SetTimer(Handle, FTimerDelegate::CreateLambda(MoveTemp(Work)), FMath::Max(.01f, Seconds), false);
}
FLinearColor Tint(const FCireAbilityDef& D, bool bHeal)
{
    if (bHeal) return FLinearColor(.35f, 1.f, .45f, .26f);
    ECireSchool S = ECireSchool::Arcane; CireAbilityShapes::ParseSchool(D.School, S);
    const FLinearColor C = CireAbilityShapes::SchoolColor(S);
    return FLinearColor(FMath::Min(C.R, 1.f), FMath::Min(C.G, 1.f), FMath::Min(C.B, 1.f), .36f);
}
FCireAreaSpec AreaFor(const FCireAbilityDef& D, ECireAreaShape Shape, float Radius, float Warning, float Burst)
{
    FCireAreaSpec A; A.Shape = Shape; A.Radius = FMath::Max(10.f, Radius); A.Length = FMath::Max(10.f, D.Range); A.Width = FMath::Max(20.f, D.Radius * 2.f);
    A.ConeAngleDegrees = 60; A.WarningSeconds = Warning; A.DurationSeconds = .1f; A.TickInterval = .5f; A.DamagePerSecond = 0; A.BurstDamage = Burst;
    A.bPersistent = false; A.bPoison = false; A.VerticalTolerance = 90; A.Color = Tint(D, false); A.AbilityName = D.Name; return A;
}
ACireAreaEffect* Field(ACireHero* H, const FCireAbilityDef& D, FVector Ground, float Radius, float Seconds, bool bHeal)
{
    FCireAreaSpec A; A.Shape = ECireAreaShape::Circle; A.Radius = FMath::Max(10.f, Radius);
    A.WarningSeconds = 0; A.DurationSeconds = FMath::Clamp(Seconds, .1f, 60.f); A.TickInterval = 1.f; A.DamagePerSecond = 0; A.BurstDamage = 0;
    A.bPersistent = true; A.bPoison = false; A.VerticalTolerance = 120; A.Color = Tint(D, bHeal); A.Color.A = .2f; A.AbilityName = D.Name;
    return ACireAreaEffect::Spawn(H, A, Ground, FRotator::ZeroRotator);
}
ACireHero* PickAlly(ACireHero* H, float Range)
{
    if (auto* A = Cast<ACireHero>(H->Target); A && !A->IsA<ACireSummon>() && A->TeamId == H->TeamId && !A->bDead && A->bDrafted &&
        H->InRange(A, Range + 40.f) && (A == H || Sight(H, A))) return A;
    ACireHero* Best = H; float Lowest = HealthFraction(H);
    for (ACireHero* A : Allies(H, H->GetActorLocation(), Range, false))
        if (Sight(H, A) && HealthFraction(A) < Lowest) { Lowest = HealthFraction(A); Best = A; }
    return Best;
}
AActor* NearestEnemy(ACireHero* H, FVector From, float Radius)
{
    AActor* Best = nullptr; double BestD = FMath::Square(static_cast<double>(Radius));
    for (AActor* U : Enemies(H, From, Radius)) { const double D = FVector::DistSquared2D(From, U->GetActorLocation()); if (D < BestD) { BestD = D; Best = U; } }
    return Best;
}
float Seconds(UWorld* W, float S) { return CireDeveloperTools::EffectSeconds(W, S); }
// initiation: the last cast centre per caster and skill (pull-to-centre riders resolve against it).
TMap<FString, FVector>& Centers() { static TMap<FString, FVector> M; return M; }
FString CenterKey(const AActor* H, const FString& Id) { return FString::Printf(TEXT("%u/%s"), H ? H->GetUniqueID() : 0u, *Id); }
void SetCenter(const AActor* H, const FString& Id, FVector P) { Centers().Add(CenterKey(H, Id), P); }
const FVector* CenterOf(const AActor* H, const FString& Id) { return Centers().Find(CenterKey(H, Id)); }
float ControlSeconds(AActor* Source, float Base) { return Base * CireKits::ControlScale(Source); }
void Push(AActor* Unit, FVector Away, float Distance)
{
    if (IsBoss(Unit) || Distance <= 0) return;
    if (auto* C = Cast<ACharacter>(Unit)) C->LaunchCharacter(Away.GetSafeNormal2D() * Distance * 2.4f + FVector(0, 0, 260), true, true);
}
void PullTo(AActor* Unit, FVector Anchor, float Fraction)
{
    auto* C = Cast<ACharacter>(Unit);
    if (!C || IsBoss(Unit) || Fraction <= 0) return;
    const FVector Back = Anchor - Unit->GetActorLocation();
    const float Dist = static_cast<float>(Back.Size2D()) * Fraction;
    if (Dist > 40.f) C->LaunchCharacter(Back.GetSafeNormal2D() * FMath::Clamp(Dist * 2.4f, 400.f, 2600.f) + FVector(0, 0, 200), true, true);
}
void AddBleed(ACireHero* H, AActor* Target, float Total, const FString& Name)
{
    auto* S = UCireKitSkillsSubsystem::Get(H->GetWorld());
    if (!S || Total <= 0 || !IsValid(Target)) return;
    UCireKitSkillsSubsystem::FBurn B; B.Source = H; B.Target = Target; B.Name = Name + TEXT(" (bleed)"); B.Ticks = 3; B.PerTick = Total / 3.f;
    S->Burns.Add(B); CireBuffs::Apply(Target, BleedingId, 3.2f, H);
}
/** The expansion passives a unit owns (hero skills only; summons/constructs resolve to their owner by the caller). */
template<class Fn> void ForPassives(const ACireHero* H, Fn&& Visit)
{
    if (!H) return;
    for (const FString& Id : H->Skills)
        if (const FRecipe* R = Find(Id); R && R->D == EDel::Passive && !R->Hook.IsNone()) Visit(*R, Id);
}
float PassiveValue(const ACireHero* H, const FString& Id)
{
    const FCireAbilityDef* D = CireAbilityDB::Find(Id);
    return CireKits::ScaledEffect(H, Id, D ? D->Base.Effect : 10.f) / 100.f;
}
bool IsControlled(const AActor* U)
{
    if (CireCrowdControl::IsStunned(U) || CireBuffs::IsActive(U, RootedId)) return true;
    const float T = Now(U->GetWorld());
    if (const auto* M = Cast<ACireMonster>(U)) return M->SlowUntil > T;
    if (const auto* H = Cast<ACireHero>(U)) return H->SlowUntil > T;
    return false;
}
}
using namespace CireXpDetail;

// ============================================================================================ identity
void CireAbilityExpansion::Reload() { Table().bLoaded = false; Load(); }
const TArray<FString>& CireAbilityExpansion::AllIds() { return Loaded().Ids; }
bool CireAbilityExpansion::Knows(const FString& Id) { return Find(Id) != nullptr; }
bool CireAbilityExpansion::Handles(const FString& Id) { const FRecipe* R = Find(Id); return R && R->D != EDel::Passive; }
bool CireAbilityExpansion::IsPassive(const FString& Id) { const FRecipe* R = Find(Id); return R && R->D == EDel::Passive; }
FString CireAbilityExpansion::Delivery(const FString& Id) { const FRecipe* R = Find(Id); return R ? R->DeliveryName : FString(); }
const TArray<FName>& CireAbilityExpansion::BuffIds()
{
    static const TArray<FName> Ids = {EmpoweredId, FortifiedId, HastenedId, ExposedId, BleedingId};
    return Ids;
}

void CireAbilityExpansion::AppendConstructRecipes(TArray<FCireTechRecipe>& Out)
{
    static const TMap<FString, ECireConstructKind> Kinds = {{TEXT("turret"), ECireConstructKind::Turret}, {TEXT("trap"), ECireConstructKind::Trap},
        {TEXT("pylon"), ECireConstructKind::Pylon}, {TEXT("skitter"), ECireConstructKind::Skitter}};
    for (const FString& Id : Loaded().Ids)
    {
        const FRecipe& R = Loaded().Rows[Id];
        if (R.D != EDel::Construct || !R.Construct.IsValid()) continue;
        const ECireConstructKind* Kind = Kinds.Find(Str(R.Construct, TEXT("kind")));
        const FCireAbilityDef* Def = CireAbilityDB::Find(Id);
        if (!Kind) { UE_LOG(LogCireExpansion, Warning, TEXT("Construct %s has no valid kind"), *Id); continue; }
        FCireTechRecipe X; X.Id = FName(*Id); X.Name = Def ? Def->Name : Id; X.Kind = *Kind;
        const FString Effect = Str(R.Construct, TEXT("effect")); X.Effect = Effect.IsEmpty() ? NAME_None : FName(*Effect);
        const TSharedPtr<FJsonObject>& J = R.Construct;
        X.Health = Num(J, TEXT("health"), 200); X.Lifetime = Num(J, TEXT("lifetime"), 20); X.Footprint = Num(J, TEXT("footprint"), 60); X.Height = Num(J, TEXT("height"), 180);
        X.Range = Num(J, TEXT("range")); X.Interval = FMath::Max(.2f, Num(J, TEXT("interval"), 1)); X.Damage = Num(J, TEXT("damage"));
        X.Scaling = Def ? Def->ScalePrimary : Num(J, TEXT("scaling")); // universal scaling: the Ability DB coefficient
        X.Trigger = Num(J, TEXT("trigger")); X.Radius = Num(J, TEXT("radius")); X.Magnitude = Num(J, TEXT("magnitude")); X.Speed = Num(J, TEXT("speed"));
        X.Splash = Num(J, TEXT("splash")); X.Limit = FMath::Clamp(static_cast<int32>(Num(J, TEXT("limit"), 1)), 1, 6); X.Count = FMath::Clamp(static_cast<int32>(Num(J, TEXT("count"), 1)), 1, 6);
        const FString Hex = Str(J, TEXT("color"));
        if (Hex.Len() == 7 && Hex[0] == '#') { const FLinearColor C = FLinearColor::FromSRGBColor(FColor::FromHex(Hex)); X.Color = FLinearColor(C.R * 2.f, C.G * 2.f, C.B * 2.f, 1.f); }
        Out.Add(X);
    }
}

// ============================================================================================ casting
bool CireAbilityExpansion::Cast(ACireHero* Hero, int32 Slot, const FString& Id)
{
    const FRecipe* Rec = Find(Id);
    const FCireAbilityDef* Def = CireAbilityDB::Find(Id);
    if (!IsValid(Hero) || !Hero->HasAuthority() || !Rec || !Def || Rec->D == EDel::Passive || !CireSkillRuntime::Alive(Hero) ||
        !Hero->Skills.IsValidIndex(Slot) || Hero->Skills[Slot] != Id || !Hero->Cooldowns.IsValidIndex(Slot) || Hero->Cooldowns[Slot] > 0 || Hero->GlobalCooldown > 0) return false;
    UWorld* World = Hero->GetWorld();
    auto* Mode = World->GetAuthGameMode<ACireGameMode>();
    if (!Mode || !Mode->IsCombatPhase()) return false;
    auto Fail = [&](const FString& Message) { Hero->Notice = Message; return false; };
    const float Mana = Def->Base.ManaCost, Energy = Def->Base.EnergyCost;
    if (!CireSkillShop::CanPayCast(Hero, Id, Mana, Energy)) return Fail(*CireSkillShop::CostFailText());
    const float Range = Def->Range > 0 ? Def->Range : 900.f;
    const float Radius = Def->Radius > 0 ? Def->Radius : 300.f;
    const float Power = Mode->Power(Hero->TeamId);
    const float Amount = FMath::Min(10000.f, CireKits::Amount(Hero, Id, Def->Base.Effect) * Power); // base + coef x PRIMARY (Ability DB)
    const float Magnitude = CireKits::ScaledEffect(Hero, Id, Def->Base.Effect);                      // utility: level x potency
    const float Duration = Seconds(World, Def->Duration > 0 ? Def->Duration : 6.f);
    AActor* Target = Hero->Target;
    const bool bHostile = CireCombat::AreHostile(Hero, Target);
    const FVector Origin = Hero->GetActorLocation();
    const FVector Feet = FeetOf(Hero);
    FVector Aim = Hero->bHasCastAim ? Hero->CastAimPoint : bHostile ? Target->GetActorLocation() : Origin + Hero->GetActorForwardVector().GetSafeNormal2D() * FMath::Min(500.f, Range);
    if (Aim.ContainsNaN()) return Fail(TEXT("Invalid aim."));
    FVector Direction = (Aim - Origin).GetSafeNormal2D();
    if (Direction.IsNearlyZero()) Direction = Hero->GetActorForwardVector().GetSafeNormal2D();
    auto NeedEnemy = [&](float Reach) { return bHostile && Hero->InRange(Target, Reach + 40.f + Body(Target)) && Sight(Hero, Target); };
    auto NeedGround = [&](bool bSight) -> bool
    {
        if (!CireSkillRuntime::InRealmBounds(Mode, Hero->TeamId, Aim) || FVector::DistSquared2D(Origin, Aim) > FMath::Square(Range + 60.f)) { Hero->Notice = TEXT("Aim within your realm and casting range."); return false; }
        if (!GroundAt(Hero, Aim)) { Hero->Notice = TEXT("Aim at supported battlefield ground."); return false; }
        if (bSight && !GroundSight(Hero, Aim)) { Hero->Notice = TEXT("A wall or world object blocks that spot."); return false; }
        return true;
    };
    const FString Name = Def->Name;
    const float BuffPercent = Rec->BuffValue > 0 ? Rec->BuffValue * CireKits::Potency(Hero, Id) : Magnitude;
    const float BuffSeconds = Seconds(World, Rec->BuffSeconds > 0 ? Rec->BuffSeconds : Def->Duration > 0 ? Def->Duration : 6.f);
    TWeakObjectPtr<ACireHero> Weak(Hero);
    bool bCue = true;
    switch (Rec->D)
    {
    case EDel::Bolt:
    case EDel::Pierce:
    {
        FCireSkillshotSpec S;
        S.Speed = Rec->Speed > 0 ? Rec->Speed : Rec->D == EDel::Pierce ? 2200.f : 2600.f; S.Radius = FMath::Clamp(Def->Radius > 0 ? Def->Radius : 30.f, 12.f, 80.f);
        S.MaxRange = Range; S.LifetimeSeconds = Range / S.Speed + .3f; S.Damage = Amount; S.WarningSeconds = .05f; S.CastRange = Range;
        S.HitLimit = Rec->D == EDel::Pierce ? FMath::Max(2, Rec->Hits) : 1;
        S.PlayerCollision = S.MonsterCollision = Rec->D == EDel::Pierce ? ECireProjectileCollision::Pierce : ECireProjectileCollision::Stop;
        S.VisualStyle = Id; S.Color = Tint(*Def, false); S.Color.A = .9f; S.AbilityName = Name;
        const FVector ShotAim = Origin + Direction * Range;
        if (!ACireSkillshot::Spawn(Hero, S, ShotAim, Name)) return Fail(TEXT("Cannot fire from here."));
        Hero->SetActorRotation(Direction.Rotation()); Aim = ShotAim; break;
    }
    case EDel::Line:
    {
        FCireAreaSpec A = AreaFor(*Def, ECireAreaShape::Line, Radius, Rec->Warning, Amount);
        if (!ACireAreaEffect::Spawn(Hero, A, Feet, Direction.Rotation())) return Fail(TEXT("Cannot unleash that here."));
        Hero->SetActorRotation(Direction.Rotation()); Aim = Origin + Direction * Range; break;
    }
    case EDel::Cone:
    {
        FCireAreaSpec A = AreaFor(*Def, ECireAreaShape::Cone, Radius, Rec->Warning, Amount); A.ConeAngleDegrees = Rec->Angle;
        if (!ACireAreaEffect::Spawn(Hero, A, Feet, Direction.Rotation())) return Fail(TEXT("Cannot unleash that here."));
        Hero->SetActorRotation(Direction.Rotation()); Aim = Origin + Direction * Radius; break;
    }
    case EDel::Circle:
    {
        if (!NeedGround(true)) return false;
        if (!ACireAreaEffect::Spawn(Hero, AreaFor(*Def, ECireAreaShape::Circle, Radius, Rec->Warning, Amount), Aim, FRotator::ZeroRotator)) return Fail(TEXT("Cannot create that area here."));
        SetCenter(Hero, Id, Aim);
        break;
    }
    case EDel::Zone:
    {
        if (!NeedGround(true)) return false;
        FCireAreaSpec A = AreaFor(*Def, ECireAreaShape::Circle, Radius, Rec->Warning, 0.f);
        A.bPersistent = true; A.DurationSeconds = FMath::Clamp(Duration, .5f, 20.f); A.TickInterval = .5f;
        A.DamagePerSecond = Amount; // Amount is damage per second (base + coef x PRIMARY)
        if (!ACireAreaEffect::Spawn(Hero, A, Aim, FRotator::ZeroRotator)) return Fail(TEXT("Cannot create that area here."));
        SetCenter(Hero, Id, Aim);
        break;
    }
    case EDel::Nova:
    {
        float Damage = Amount;
        if (Rec->Echo > 0) Damage *= 1.f + Rec->Echo * FMath::Max(0, Enemies(Hero, Origin, Radius).Num() - 1); // initiation: Echo Slam
        if (!ACireAreaEffect::Spawn(Hero, AreaFor(*Def, ECireAreaShape::Circle, Radius, Rec->Warning, Damage), Feet, FRotator::ZeroRotator)) return Fail(TEXT("Cannot unleash that here."));
        SetCenter(Hero, Id, Feet);
        Aim = Feet; break;
    }
    case EDel::Vacuum: // initiation: everything in the circle is dragged to its centre when it resolves
    {
        if (!NeedGround(true)) return false;
        if (!ACireAreaEffect::Spawn(Hero, AreaFor(*Def, ECireAreaShape::Circle, Radius, Rec->Warning, Amount), Aim, FRotator::ZeroRotator)) return Fail(TEXT("Cannot create that area here."));
        SetCenter(Hero, Id, Aim);
        break;
    }
    case EDel::Charge: // initiation: rush to the selected enemy and slam around the landing spot
    {
        if (!NeedEnemy(Range)) return Fail(TEXT("Select a hostile target in range and line of sight."));
        const FVector Toward = (Target->GetActorLocation() - Origin).GetSafeNormal2D();
        const float Gap = FMath::Max(0.f, static_cast<float>(FVector::Dist2D(Origin, Target->GetActorLocation())) - Body(Target) - Body(Hero) - 20.f);
        const float Free = FreeTravel(Hero, Toward, Gap);
        if (Free > 40.f && !MoveTo(Hero, Origin + Toward * Free)) return Fail(TEXT("The path is blocked."));
        SetCenter(Hero, Id, FeetOf(Hero));
        ACireAreaEffect::Spawn(Hero, AreaFor(*Def, ECireAreaShape::Circle, Radius, 0.f, Amount), FeetOf(Hero), FRotator::ZeroRotator);
        Aim = Target->GetActorLocation(); break;
    }
    case EDel::Hook: // initiation: the first enemy in the chain lane is struck and dragged to you
    {
        const float Width = FMath::Max(40.f, Def->Radius);
        AActor* First = nullptr; float Best = TNumericLimits<float>::Max();
        for (AActor* U : Enemies(Hero, Origin + Direction * Range * .5f, Range * .5f + Width + 100.f))
        {
            const float Along = static_cast<float>(FVector::DotProduct(U->GetActorLocation() - Origin, Direction));
            if (Along < 0 || Along > Range || SegmentDistance(U->GetActorLocation(), Origin, Origin + Direction * Range) > Width + Body(U) || !Sight(Hero, U)) continue;
            if (Along < Best) { Best = Along; First = U; }
        }
        CireCombat::PlayCue(Hero, First, FName(*Id), Origin, First ? First->GetActorLocation() : Origin + Direction * Range, ECireSpellCue::Launch, 1.f, true);
        if (First)
        {
            SetCenter(Hero, Id, Origin + Direction * (Body(Hero) + 80.f));
            CireCombat::ApplyDamage(Hero, First, Amount, Name);
            if (!IsBoss(First)) PullTo(First, Origin + Direction * (Body(Hero) + 80.f), 1.f);
        }
        Hero->SetActorRotation(Direction.Rotation()); Aim = Origin + Direction * Range; bCue = false; break;
    }
    case EDel::Cage: // initiation: a ring of walls traps whoever is inside (allies walk through)
    {
        if (!CireSkillCasting::PlacementAim(Hero, Aim, Range)) return false; // casting-rules: placement ignores clipping
        SetCenter(Hero, Id, Aim);
        ACireAreaEffect::Spawn(Hero, AreaFor(*Def, ECireAreaShape::Circle, Radius, Rec->Warning, Amount), Aim, FRotator::ZeroRotator);
        const int32 N = Rec->CageSegments; const float Seg = 2.f * PI * Radius / N * 1.08f;
        for (int32 I = 0; I < N; ++I)
        {
            const float A = 2.f * PI * I / N; const FVector Out(FMath::Cos(A), FMath::Sin(A), 0);
            FCireConstructSpec W; W.Kind = ECireConstructKind::Wall; W.MaxHealth = FMath::Min(20000.f, Amount * 4.f); W.LifetimeSeconds = Seconds(World, Rec->CageSeconds);
            W.Width = FMath::Max(60.f, Seg); W.Depth = 40.f; W.Height = 220.f; W.ManaCost = 0; W.EnergyCost = 0; W.CooldownSeconds = 0; W.CastRange = Range + Radius + 200.f;
            W.bBlockMovement = true; W.bBlockProjectiles = false; W.bDestructible = true; W.bBlockFriendly = false; W.Color = Tint(*Def, false); W.Color.A = .9f;
            FVector P = Aim + Out * Radius; GroundAt(Hero, P);
            ACireConstruct::Spawn(Hero, W, P, Out.Rotation(), Name);
        }
        break;
    }
    case EDel::Chain:
    {
        if (!NeedEnemy(Range)) return Fail(TEXT("Select a hostile target in range and line of sight."));
        TArray<AActor*> Hit; AActor* Current = Target; FVector From = Origin; float Damage = Amount;
        for (int32 Bounce = 0; Bounce < FMath::Max(1, Rec->Hits) && IsValid(Current); ++Bounce)
        {
            CireCombat::PlayCue(Hero, Current, FName(*Id), From, Current->GetActorLocation(), ECireSpellCue::Launch, .9f, Bounce == 0);
            CireCombat::ApplyStrike(Hero, Current, Damage, Name);
            Hit.Add(Current); From = Current->GetActorLocation(); Damage *= .8f;
            AActor* Next = nullptr; double Best = TNumericLimits<double>::Max();
            for (AActor* U : Enemies(Hero, From, Radius))
            {
                if (Hit.Contains(U) || !Sight(Current, U)) continue;
                const double D = FVector::DistSquared2D(From, U->GetActorLocation()); if (D < Best) { Best = D; Next = U; }
            }
            Current = Next;
        }
        Aim = Target->GetActorLocation(); bCue = false; break;
    }
    case EDel::Strike:
    {
        if (!NeedEnemy(Range)) return Fail(TEXT("Select a hostile target in range and line of sight."));
        float Damage = Amount;
        if (Rec->ExecuteBelow > 0 && !IsBoss(Target) && HealthFraction(Target) < Rec->ExecuteBelow) Damage *= 1.f + Rec->ExecuteBonus;
        CireCombat::ApplyStrike(Hero, Target, Damage, Name);
        Aim = Target->GetActorLocation(); break;
    }
    case EDel::Leap:
    {
        if (!NeedGround(false)) return false;
        const FVector Dir = (Aim - Origin).GetSafeNormal2D().IsNearlyZero() ? Direction : (Aim - Origin).GetSafeNormal2D();
        const float Free = FreeTravel(Hero, Dir, FMath::Min(Range, static_cast<float>(FVector::Dist2D(Origin, Aim))));
        if (!MoveTo(Hero, Origin + Dir * Free)) return Fail(TEXT("The path is blocked."));
        ACireAreaEffect::Spawn(Hero, AreaFor(*Def, ECireAreaShape::Circle, Radius, 0.f, Amount), FeetOf(Hero), FRotator::ZeroRotator);
        Aim = FeetOf(Hero); break;
    }
    case EDel::Dash:
    {
        const float Free = FreeTravel(Hero, Direction, Range);
        const float Width = FMath::Max(50.f, Def->Radius);
        TArray<AActor*> Lane;
        for (AActor* U : Enemies(Hero, Origin + Direction * Free * .5f, Free * .5f + Width + 100.f))
            if (SegmentDistance(U->GetActorLocation(), Origin, Origin + Direction * Free) <= Width + Body(U)) Lane.Add(U);
        if (Free > 40.f && !MoveTo(Hero, Origin + Direction * Free)) return Fail(TEXT("The path is blocked."));
        for (AActor* U : Lane) CireCombat::ApplyDamage(Hero, U, Amount, Name);
        Aim = Origin + Direction * Free; break;
    }
    case EDel::Heal:
    {
        ACireHero* Ally = PickAlly(Hero, Range);
        if (!Ally) return Fail(TEXT("Ally is unavailable or out of range."));
        CireCombat::ApplyHealing(Hero, Ally, Amount, Name);
        if (Rec->BarrierFraction > 0) CireItems::GrantBarrier(Ally, Amount * Rec->BarrierFraction, Seconds(World, 6.f), Hero);
        if (Rec->bCleanse) { Ally->SlowUntil = 0; CireBuffs::Remove(Ally, RootedId); }
        Aim = Ally->GetActorLocation(); break;
    }
    case EDel::HealZone:
    {
        if (!Hero->bHasCastAim) { ACireHero* A = PickAlly(Hero, Range); Aim = A ? A->GetActorLocation() : Origin; }
        if (!NeedGround(false)) return false;
        auto* S = UCireKitSkillsSubsystem::Get(World);
        ACireAreaEffect* Area = Field(Hero, *Def, Aim, Radius, Duration, true);
        if (!S || !Area) return Fail(TEXT("Cannot create that area here."));
        UCireKitSkillsSubsystem::FZone Z; Z.Owner = Hero; Z.Area = Area; Z.Id = Id; Z.Name = Name; Z.Kind = UCireKitSkillsSubsystem::EZone::Heal;
        Z.Center = Aim; Z.Radius = Radius; Z.PerSecond = Amount; Z.Magnitude = 0; Z.StartsAt = Now(World); Z.EndsAt = Z.StartsAt + Duration; Z.Interval = 1.f; Z.Timer = .05f;
        S->Zones.Add(Z);
        break;
    }
    case EDel::Barrier:
    {
        ACireHero* Ally = PickAlly(Hero, Range);
        if (!Ally) return Fail(TEXT("Ally is unavailable or out of range."));
        CireItems::GrantBarrier(Ally, Amount, Duration, Hero);
        if (!Rec->Buff.IsNone()) CireBuffs::Apply(Ally, Rec->Buff, BuffSeconds, Hero, Percent(BuffPercent));
        Aim = Ally->GetActorLocation(); break;
    }
    case EDel::SelfBuff:
    {
        CireBuffs::Apply(Hero, Rec->Buff.IsNone() ? EmpoweredId : Rec->Buff, BuffSeconds, Hero, Percent(BuffPercent));
        if (Rec->BarrierFraction > 0) CireItems::GrantBarrier(Hero, Amount * Rec->BarrierFraction, BuffSeconds, Hero);
        if (Rec->bCleanse) { Hero->SlowUntil = 0; CireBuffs::Remove(Hero, RootedId); }
        Aim = Feet; break;
    }
    case EDel::PartyBuff:
    {
        for (ACireHero* A : Allies(Hero, Origin, Radius))
        {
            if (!Rec->Buff.IsNone()) CireBuffs::Apply(A, Rec->Buff, BuffSeconds, Hero, Percent(BuffPercent));
            if (Rec->BarrierFraction > 0) CireItems::GrantBarrier(A, Amount * Rec->BarrierFraction, BuffSeconds, Hero);
            if (Rec->bCleanse) { A->SlowUntil = 0; CireBuffs::Remove(A, RootedId); }
        }
        // Enemy-facing riders (taunt / slow / weaken effects) land on every enemy in the same circle.
        bool bEnemyEffects = false; for (const FCireAbilityEffect& E : Def->Effects) bEnemyEffects |= E.Zone != TEXT("self");
        if (bEnemyEffects) for (AActor* U : Enemies(Hero, Origin, Radius)) OnAbilityHit(Hero, U, Name, 1.f);
        Aim = Feet; break;
    }
    case EDel::Summon:
    {
        AActor* Prey = bHostile ? Target : NearestEnemy(Hero, Origin, 1600.f);
        if (!Rec->bCommandable && !Prey) return Fail(TEXT("No enemy for your summons to hunt."));
        const FCireSummonSpec* Base = CireSkillTuning::FindSummon(Rec->bCommandable ? TEXT("oathbound_guardian") : TEXT("spectral_pack"));
        FCireSummonSpec Spec = Base ? *Base : FCireSummonSpec();
        Spec.Count = Rec->Count; Spec.Damage = Amount; Spec.Health = FMath::Min(20000.f, Rec->SummonHealth + Rec->SummonHealthPrimary * Hero->PrimaryAttribute());
        Spec.DurationSeconds = Duration; Spec.ManaCost = 0; Spec.EnergyCost = 0; Spec.CooldownSeconds = 0; Spec.bCommandable = Rec->bCommandable;
        Spec.CastRange = Range + 250.f; Spec.MoveSpeed = Rec->MoveSpeed; Spec.AttackRange = Rec->AttackRange; Spec.ArchetypeVisual = Rec->Visual;
        FVector At = Hero->bHasCastAim ? Aim : Origin + Direction * 180.f;
        if (!CireSkillCasting::PlacementAim(Hero, At, Range)) return false; // casting-rules: placement ignores clipping
        // SpawnGroup places at most 3 per call: larger packs arrive in groups of up to 3 side by side.
        TArray<ACireSummon*> Units;
        for (int32 Left = Rec->Count, Group = 0; Left > 0; ++Group)
        {
            FCireSummonSpec Part = Spec; Part.Count = FMath::Min(3, Left); Left -= Part.Count;
            FVector P = At + FVector(-Direction.Y, Direction.X, 0) * (Group * 220.f);
            if (Group > 0 && !GroundAt(Hero, P)) P = At;
            Units.Append(ACireSummon::SpawnGroup(Hero, Part, Prey, P));
        }
        if (Units.IsEmpty()) return Fail(TEXT("No room for your summons there (at most 6 at a time)."));
        for (ACireSummon* U : Units)
        {
            U->HeroName = Rec->SummonName.IsEmpty() ? Name : Rec->SummonName;
            U->SourceSkill = FName(*Id); U->ForceNetUpdate();
        }
        Aim = At; break;
    }
    case EDel::Construct:
    {
        if (!CireSkillCasting::PlacementAim(Hero, Aim, Range)) return false; // casting-rules: placement ignores clipping
        FString Why;
        if (CireTechConstructs::Deploy(Hero, FName(*Id), Aim, &Why).IsEmpty()) return Fail(Why.IsEmpty() ? FString(TEXT("Cannot build there.")) : Why);
        break;
    }
    case EDel::Wall:
    {
        if (!CireSkillCasting::PlacementAim(Hero, Aim, Range)) return false; // casting-rules: placement ignores clipping
        for (TCireActorIterator<ACireConstruct> It(World); It; ++It) if (It->GetSourceActor() == Hero && It->GetDisplayName() == Name) It->Destroy(); // one per owner
        FCireConstructSpec W; W.Kind = ECireConstructKind::Wall; W.MaxHealth = FMath::Min(20000.f, Amount); W.LifetimeSeconds = Duration;
        W.Width = FMath::Max(120.f, Radius * 2.f); W.Depth = 60.f; W.Height = 230.f; W.ManaCost = 0; W.EnergyCost = 0; W.CooldownSeconds = Def->Base.Cooldown; W.CastRange = Range + 60.f;
        W.bBlockMovement = true; W.bBlockProjectiles = true; W.bDestructible = true; W.bBlockFriendly = false; W.Color = Tint(*Def, false); W.Color.A = .95f;
        const FVector Dir = (Aim - Origin).GetSafeNormal2D();
        if (!ACireConstruct::Spawn(Hero, W, Aim, Dir.IsNearlyZero() ? Hero->GetActorRotation() : Dir.Rotation(), Name)) return Fail(TEXT("The wall cannot rise there."));
        break;
    }
    case EDel::Barrage:
    {
        if (!NeedGround(false)) return false;
        const FVector Center = Aim; const float Sub = FMath::Min(Rec->SubRadius, Radius); const float Warning = FMath::Max(.3f, Rec->Warning);
        const int32 Waves = Rec->Waves; const float Interval = Rec->Interval; const int32 PerWave = FMath::Max(1, Rec->Hits);
        // The whole footprint is shown first; each wave then drops PerWave circles (spread by a golden-angle spiral).
        Field(Hero, *Def, Center, Radius, Waves * Interval + Warning + .3f, false);
        const float Each = Amount;
        const FCireAbilityDef* DefPtr = Def;
        for (int32 W = 0; W < Waves; ++W)
            Later(World, .05f + W * Interval, [Weak, Center, Radius, Sub, Warning, Each, W, PerWave, DefPtr]()
            {
                ACireHero* H = Weak.Get(); if (!CireSkillRuntime::Alive(H)) return;
                for (int32 K = 0; K < PerWave; ++K)
                {
                    const int32 I = W * PerWave + K;
                    const float Angle = I * 2.399963f, Dist = (Radius - Sub) * FMath::Sqrt(FMath::Frac(I * .618034f + .1f));
                    FVector P = Center + FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0) * Dist;
                    if (!GroundAt(H, P)) P = Center;
                    ACireAreaEffect::Spawn(H, AreaFor(*DefPtr, ECireAreaShape::Circle, Sub, Warning, Each), P, FRotator::ZeroRotator);
                }
            });
        break;
    }
    default: return false;
    }
    Hero->Mana -= Mana; Hero->Energy -= Energy;
    Hero->Cooldowns[Slot] = static_cast<float>(Cires::CooldownSeconds(CireDeveloperTools::CooldownSeconds(World, Def->Base.Cooldown), Hero->CDR));
    CireSkillShop::ApplyCastLevel(Hero, Slot, Id, Mana, Energy);
    Hero->GlobalCooldown = .9f;
    Hero->Notice = Def->Name;
    Hero->ForceNetUpdate();
    if (bCue) CireCombat::PlayCue(Hero, bHostile ? Target : nullptr, FName(*Id), Origin, Aim, ECireSpellCue::Cast);
    UE_LOG(LogCireExpansion, Verbose, TEXT("CIRE_EXPANSION_CAST %s by %s"), *Id, *Hero->HeroName);
    return true;
}

// ============================================================================================ shapes
bool CireAbilityExpansion::DescribeShape(const FString& Id, FCireHitShape& R)
{
    const FRecipe* Rec = Find(Id);
    const FCireAbilityDef* Def = CireAbilityDB::Find(Id);
    if (!Rec || !Def) return false;
    const float Range = Def->Range > 0 ? Def->Range : 900.f;
    const float Radius = Def->Radius > 0 ? Def->Radius : 300.f;
    auto Circle = [&](float Rad, bool bSelf) { R.Kind = ECireHitShape::Circle; R.Radius = FMath::Max(40.f, Rad); R.bFromCaster = bSelf; R.bAtTarget = !bSelf; };
    auto Lane = [&](float Length, float Width) { R.Kind = ECireHitShape::Line; R.bFromCaster = true; R.bGroundAim = true; R.Length = Length; R.Width = FMath::Max(24.f, Width); };
    R.LingerSeconds = .6f; R.WarningSeconds = Rec->Warning;
    switch (Rec->D)
    {
    case EDel::Passive: R.Kind = ECireHitShape::None; break;
    case EDel::Bolt: case EDel::Pierce:
        Lane(Range, FMath::Clamp(Def->Radius > 0 ? Def->Radius : 30.f, 12.f, 80.f) * 2.f); R.bProjectile = true;
        R.Speed = Rec->Speed > 0 ? Rec->Speed : Rec->D == EDel::Pierce ? 2200.f : 2600.f; R.WarningSeconds = .05f; R.LingerSeconds = .4f; break;
    case EDel::Line: Lane(Range, Radius * 2.f); R.LingerSeconds = .4f; break;
    case EDel::Dash: Lane(Range, FMath::Max(50.f, Def->Radius) * 2.f); break;
    case EDel::Cone: R.Kind = ECireHitShape::Cone; R.bFromCaster = true; R.bGroundAim = true; R.Radius = Radius; R.Angle = Rec->Angle; break;
    case EDel::Circle: Circle(Radius, false); R.bGroundAim = true; R.LingerSeconds = .4f; break;
    case EDel::Zone: Circle(Radius, false); R.bGroundAim = true; R.LingerSeconds = Def->Duration; break;
    case EDel::Barrage: Circle(Radius, false); R.bGroundAim = true; R.LingerSeconds = FMath::Max(1.f, Rec->Waves * Rec->Interval); break;
    case EDel::Nova: Circle(Radius, true); break;
    case EDel::Vacuum: Circle(Radius, false); R.bGroundAim = true; R.LingerSeconds = .5f; break;
    case EDel::Charge: Circle(Radius, false); R.bAtTarget = true; break;
    case EDel::Hook: Lane(Range, FMath::Max(40.f, Def->Radius) * 2.f); R.LingerSeconds = .5f; break;
    case EDel::Cage: Circle(Radius, false); R.bGroundAim = true; R.LingerSeconds = Rec->CageSeconds; break;
    case EDel::Leap: Circle(Radius, false); R.bGroundAim = true; break;
    case EDel::Chain: R.Kind = ECireHitShape::Chain; R.Radius = Radius; R.bAtTarget = true; break;
    case EDel::Strike: R.Kind = ECireHitShape::Unit; break;
    case EDel::Heal: R.Kind = ECireHitShape::Unit; R.bHostileOnly = false; R.bHeal = true; R.LingerSeconds = .8f; break;
    case EDel::Barrier: R.Kind = ECireHitShape::Unit; R.bHostileOnly = false; R.bBuff = true; R.LingerSeconds = .8f; break;
    case EDel::HealZone: Circle(Radius, false); R.bGroundAim = true; R.bHostileOnly = false; R.bHeal = true; R.LingerSeconds = Def->Duration; break;
    case EDel::SelfBuff: R.Kind = ECireHitShape::Self; R.bHostileOnly = false; R.bBuff = true; R.LingerSeconds = 1.f; break;
    case EDel::PartyBuff: Circle(Radius, true); R.bHostileOnly = false; R.bBuff = true; R.LingerSeconds = 1.f; break;
    case EDel::Summon: Circle(60.f, false); R.bGroundAim = true; break;
    case EDel::Construct:
    {
        const FCireTechRecipe* X = CireTechConstructs::FindRecipe(FName(*Id));
        const float Rad = !X ? 80.f : X->Kind == ECireConstructKind::Pylon ? X->Radius : X->Kind == ECireConstructKind::Turret ? X->Range :
            X->Kind == ECireConstructKind::Skitter ? 180.f : FMath::Max(X->Trigger, X->Radius);
        Circle(Rad, false); R.bGroundAim = true;
        R.bHostileOnly = X && X->Kind == ECireConstructKind::Pylon ? (X->Effect == TEXT("weaken") || X->Effect == TEXT("slow")) : true;
        R.LingerSeconds = FMath::Min(2.f, X ? X->Lifetime : 2.f); break;
    }
    case EDel::Wall:
        R.Kind = ECireHitShape::Custom; R.bGroundAim = true; R.bHostileOnly = false; R.LingerSeconds = 1.f;
        R.Polygon = {{-30.f, -Radius}, {30.f, -Radius}, {30.f, Radius}, {-30.f, Radius}}; break;
    default: R.Kind = ECireHitShape::Unit; break;
    }
    return true;
}

// ============================================================================================ hooks
float CireAbilityExpansion::ModifyOutgoingDamage(AActor* Source, AActor* Target, float Amount, const FString& AbilityName)
{
    Amount = CireInitiation::ModifyOutgoingDamage(Source, Target, Amount); // initiation: Set-up follow-ups
    if (!IsValid(Target) || Amount <= 0 || Loaded().Ids.IsEmpty()) return Amount;
    float M = 1.f;
    if (const auto* E = ActiveBuff(Target, FortifiedId)) M *= 1.f - FMath::Clamp(E->Stacks / 100.f, 0.f, .7f);
    if (const auto* E = ActiveBuff(Target, ExposedId)) M *= 1.f + FMath::Clamp(E->Stacks / 100.f, 0.f, 1.f);
    if (Source) if (const auto* E = ActiveBuff(Source, EmpoweredId)) M *= 1.f + FMath::Clamp(E->Stacks / 100.f, 0.f, 1.f);
    // Passives of the attacking champion (a summon / construct counts for its owner through Summoner's Bond only).
    const ACireHero* Owner = Source ? CireKits::OwnerOf(Source) : nullptr;
    const bool bOwnUnit = Owner && Owner != Source;
    ForPassives(Owner, [&](const FRecipe& R, const FString& Id)
    {
        const float V = PassiveValue(Owner, Id);
        if (bOwnUnit) { if (R.Hook == TEXT("bond")) M *= 1.f + V; return; }
        if (R.Hook == TEXT("executioner") && HealthFraction(Target) < FMath::Max(.05f, R.Threshold)) M *= 1.f + V;
        else if (R.Hook == TEXT("firstStrike") && HealthFraction(Target) > FMath::Max(.5f, R.Threshold)) M *= 1.f + V;
        else if (R.Hook == TEXT("opportunist") && IsControlled(Target)) M *= 1.f + V;
    });
    // Passives of the defending champion.
    if (const auto* Defender = Cast<ACireHero>(Target); Defender && !Defender->IsA<ACireSummon>())
        ForPassives(Defender, [&](const FRecipe& R, const FString& Id)
        {
            if (R.Hook == TEXT("thickSkin")) M *= 1.f - FMath::Clamp(PassiveValue(Defender, Id), 0.f, .4f);
            else if (R.Hook == TEXT("lastBastion") && HealthFraction(Defender) < FMath::Max(.1f, R.Threshold)) M *= 1.f - FMath::Clamp(PassiveValue(Defender, Id), 0.f, .6f);
        });
    return Amount * M;
}

void CireAbilityExpansion::OnAbilityHit(AActor* Source, AActor* Target, const FString& AbilityName, float Applied)
{
    CireInitiation::OnDamageDealt(Source, Target, Applied); // initiation: champion damage disrupts a Blink Dagger
    if (!IsValid(Source) || !Source->HasAuthority() || !IsValid(Target) || Applied <= 0 || Loaded().Ids.IsEmpty()) return;
    auto* H = Cast<ACireHero>(Source);
    if (!H || H->IsA<ACireSummon>()) return;
    const FCireAbilityDef* D = CireAbilityDB::FindByName(AbilityName);
    // ---- expansion skill riders
    if (const FRecipe* Rec = D ? Find(D->Id) : nullptr)
    {
        for (const FCireAbilityEffect& E : D->Effects)
        {
            if (E.Zone == TEXT("self")) continue;
            if (E.Type == TEXT("root") && !IsBoss(Target)) { CireBuffs::Apply(Target, RootedId, ControlSeconds(H, E.Duration), H); if (auto* C = Cast<ACharacter>(Target)) C->GetCharacterMovement()->StopMovementImmediately(); }
            else if (E.Type == TEXT("taunt"))
            {
                if (auto* M = Cast<ACireMonster>(Target)) CireThreat::Taunt(M, H, ControlSeconds(H, E.Duration));
                else if (auto* Foe = Cast<ACireHero>(Target)) { if (Foe->bBot) Foe->Target = H; CireBuffs::Apply(Foe, TauntedId, ControlSeconds(H, E.Duration), H); }
            }
            else if (E.Type == TEXT("weaken")) CireBuffs::Apply(Target, CowedId, ControlSeconds(H, E.Duration), H, Percent(E.Magnitude * 100.f * CireKits::Potency(H, D->Id)));
            else if (E.Type == TEXT("mark")) CireBuffs::Apply(Target, ExposedId, ControlSeconds(H, E.Duration), H, Percent(E.Magnitude * 100.f * CireKits::Potency(H, D->Id)));
        }
        if (Rec->Knockback > 0) Push(Target, Target->GetActorLocation() - H->GetActorLocation(), Rec->Knockback);
        if (Rec->Pull > 0) PullTo(Target, H->GetActorLocation(), Rec->Pull);
        if (Rec->Bleed > 0) AddBleed(H, Target, Applied * Rec->Bleed, D->Name);
        if (Rec->bSetup) CireInitiation::ApplySetUp(H, Target, D->Name); // initiation
        if (Rec->Knockup > 0 && !IsBoss(Target)) if (auto* C = Cast<ACharacter>(Target)) C->LaunchCharacter(FVector(0, 0, Rec->Knockup), false, true);
        if (Rec->PullCenter > 0) if (const FVector* Center = CenterOf(H, D->Id)) PullTo(Target, *Center, Rec->PullCenter);
        if (Rec->Lifesteal > 0) CireCombat::ApplyHealing(H, H, Applied * Rec->Lifesteal, D->Name);
    }
    // ---- on-hit passives: skill damage only (the name resolves to an Ability DB row; echoes / bleeds never chain)
    if (!D) return;
    ForPassives(H, [&](const FRecipe& R, const FString& Id)
    {
        const float V = PassiveValue(H, Id);
        if (R.Hook == TEXT("vampiric")) CireCombat::ApplyHealing(H, H, Applied * FMath::Clamp(V, 0.f, .5f), AbilityName);
        else if (R.Hook == TEXT("bleedEdge") && FMath::FRand() < R.Chance) AddBleed(H, Target, Applied * V, AbilityName);
        else if (R.Hook == TEXT("echo") && FMath::FRand() < R.Chance && CireCombat::IsAlive(Target)) CireCombat::ApplyDamage(H, Target, Applied * FMath::Clamp(V, 0.f, 1.f), AbilityName + TEXT(" (echo)"));
        else if (R.Hook == TEXT("frostbite") && FMath::FRand() < FMath::Clamp(V, 0.f, .6f)) CireCrowdControl::Slow(Target, ControlSeconds(H, 2.f), H);
    });
}

void CireAbilityExpansion::OnMonsterKilled(ACireMonster* Monster, ACireHero* Killer)
{
    if (!IsValid(Monster) || !Killer || Killer->IsA<ACireSummon>() || Loaded().Ids.IsEmpty()) return;
    ForPassives(Killer, [&](const FRecipe& R, const FString& Id)
    {
        const float V = PassiveValue(Killer, Id);
        if (R.Hook == TEXT("manaFont")) Killer->Mana = FMath::Min(Killer->MaxMana, Killer->Mana + Killer->MaxMana * FMath::Clamp(V, 0.f, .5f));
        else if (R.Hook == TEXT("soulHarvest") && !Killer->bDead) CireCombat::ApplyHealing(Killer, Killer, Killer->MaxHealth * FMath::Clamp(V, 0.f, .5f), CireAbilityDB::Find(Id) ? CireAbilityDB::Find(Id)->Name : Id);
    });
    Killer->ForceNetUpdate();
}

float CireAbilityExpansion::MoveSpeedMultiplier(const ACireHero* Hero)
{
    if (!Hero) return 1.f;
    float M = 1.f + FMath::Clamp(BuffFraction(Hero, HastenedId), 0.f, 1.f);
    ForPassives(Hero, [&](const FRecipe& R, const FString& Id) { if (R.Hook == TEXT("fleet")) M *= 1.f + FMath::Clamp(PassiveValue(Hero, Id), 0.f, .4f); });
    return M;
}
float CireAbilityExpansion::AttackSpeedBonus(const ACireHero* Hero)
{
    if (!Hero) return 0.f;
    float Bonus = FMath::Clamp(BuffFraction(Hero, HastenedId), 0.f, 1.f);
    ForPassives(Hero, [&](const FRecipe& R, const FString& Id) { if (R.Hook == TEXT("trance")) Bonus += FMath::Clamp(PassiveValue(Hero, Id), 0.f, .6f); });
    return Bonus;
}

bool CireAbilityExpansion::BotWantsCast(ACireHero* Hero, const FString& Id)
{
    const FRecipe* R = Find(Id);
    const FCireAbilityDef* D = CireAbilityDB::Find(Id);
    if (!R || !D || !Hero) return false;
    const float Range = D->Range > 0 ? D->Range : 900.f;
    switch (R->D)
    {
    case EDel::Heal: case EDel::HealZone:
        for (ACireHero* A : Allies(Hero, Hero->GetActorLocation(), Range)) if (HealthFraction(A) < .8f) return true;
        return false;
    case EDel::Barrier: case EDel::SelfBuff: case EDel::PartyBuff: case EDel::Summon: case EDel::Construct: case EDel::Wall:
        return NearestEnemy(Hero, Hero->GetActorLocation(), 1400.f) != nullptr;
    case EDel::Passive: return false;
    default:
        if (R->bSetup) return Enemies(Hero, Hero->GetActorLocation(), Range + FMath::Max(300.f, D->Radius)).Num() >= 2; // initiation: engage a group, not one straggler
        return true;
    }
}
