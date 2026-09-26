// kits-complete: the 63 signature skills of the thirteen roster champions whose kits were "planned".
#include "CireKitSkills.h"
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
#include "CireMobility.h"
#include "CireNPCCombat.h"
#include "CireScalingKits.h"
#include "CireSkillRuntime.h"
#include "CireSkillShop.h"
#include "CireSkillshot.h"
#include "CireSkillTuning.h"
#include "CireSummon.h"
#include "CireTechConstructs.h"
#include "CireThreat.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogCireKitSkills, Log, All);

namespace
{
// Delivery recipes. Numbers live in the Ability Database; this table only says how each skill lands.
enum class EKit : uint8
{
    Passive,
    Strike,      // targeted melee / short-range hit (maul, pickfall, censer)
    Cone,        // ground cone from the caster (warning, burst)
    Circle,      // aimed ground circle (warning, burst)
    Line,        // ground line from the caster (warning, burst)
    Burst,       // circle around the caster (ultimate slams, shouts)
    Charge,      // rush down a lane (bear: stop at first enemy; behemoth: through everyone)
    Leap,        // jump to the aimed spot, cleave on landing
    Hook,        // chained axe: pull the first enemy on the line
    Channel,     // stationary self heal (Ironroot Slumber)
    SelfBuff,    // timed state on the caster
    AllyBarrier, // barrier on an ally (Relic Vow, Living Granite)
    DelayedHeal, // seed that blooms after a delay
    AllyOrb,     // slow orb / mote down a line: heals the first ally it reaches
    HealZone,    // ground zone that heals allies inside every second
    Tether,      // ally link that heals per second while in range
    Motes,       // trail of consumable healing motes
    Construct,   // tech-construct recipe (lantern, banner, ward)
    Wall,        // Totem Bulwark barricade
    Summon,      // Herd Call stag
    Frenzy,      // five rapid hits on one target
    TwinThrow,   // two forked skillshots
    Returning,   // out-and-back axe lane
    Javelin,     // skillshot that grows a healing bloom where it hits
    Beam,        // line that heals allies and burns enemies
    Beacon,      // ground zone: haste + regeneration
    Trail,       // ground lane: allies move faster
    Transform,   // Dragon Oath: empowered basic attacks, then a fireball
    Worldstone,  // role-dependent golem ultimate
    MovingAura,  // Spring March: heal aura that travels with the caster
    Constellation, // links to allies, pulsing heals
    Sunrise      // party heal over time + cleanse
};
struct FKit
{
    const TCHAR* Id;
    EKit D;
    float Angle = 0;      // cone degrees
    float Warning = 0;    // telegraph seconds before the hit resolves
};
const FKit Kits[] = {
    // Gravewood Bear
    {TEXT("bear_maul"), EKit::Strike}, {TEXT("bear_roar"), EKit::Cone, 90, .15f}, {TEXT("bear_charge"), EKit::Charge, 0, .1f},
    {TEXT("bear_hibernate"), EKit::Channel}, {TEXT("bear_ancient_hide"), EKit::Passive}, {TEXT("bear_colossus"), EKit::Burst, 0, .1f},
    // Paladins
    {TEXT("paladin_righteous_flail"), EKit::Cone, 100, .2f}, {TEXT("paladin_relic_vow"), EKit::AllyBarrier},
    {TEXT("paladin_holy_flail"), EKit::Strike}, {TEXT("paladin_pilgrim_light"), EKit::AllyOrb},
    // Dwarf Miner
    {TEXT("miner_pickfall"), EKit::Strike}, {TEXT("miner_faultline"), EKit::Line, 0, .8f}, {TEXT("miner_lantern"), EKit::Construct},
    {TEXT("miner_orehide"), EKit::Passive}, {TEXT("miner_mountain"), EKit::Circle, 0, 1.f},
    // Ether Golems
    {TEXT("golem_granite_fist"), EKit::Cone, 70, .35f}, {TEXT("golem_ether_anchor"), EKit::Circle, 0, .5f}, {TEXT("golem_construct_core"), EKit::Passive},
    {TEXT("golem_worldstone"), EKit::Worldstone, 0, .1f}, {TEXT("golem_moss_bloom"), EKit::HealZone}, {TEXT("golem_living_granite"), EKit::AllyBarrier},
    {TEXT("golem_fel_fist"), EKit::Cone, 60, .1f}, {TEXT("golem_ether_furnace"), EKit::SelfBuff},
    // Orc Chieftain
    {TEXT("chieftain_axe_hook"), EKit::Hook}, {TEXT("chieftain_banner"), EKit::Construct}, {TEXT("chieftain_courage"), EKit::Passive},
    {TEXT("chieftain_earthshout"), EKit::Burst, 0, .8f},
    // Totemic Behemoth
    {TEXT("behemoth_totem_sweep"), EKit::Cone, 120, .3f}, {TEXT("behemoth_tusk_line"), EKit::Charge, 0, .1f}, {TEXT("behemoth_totem_bulwark"), EKit::Wall},
    {TEXT("behemoth_ancestral_weight"), EKit::Passive}, {TEXT("behemoth_stampede"), EKit::Line, 0, 1.f},
    // Drakish Footman
    {TEXT("drakish_dragon_oath"), EKit::Transform}, {TEXT("drakish_scale_guard"), EKit::SelfBuff}, {TEXT("drakish_wing_rebuke"), EKit::Cone, 120, .3f},
    {TEXT("drakish_ember_memory"), EKit::Passive}, {TEXT("drakish_ancient_pact"), EKit::Burst, 0, .6f},
    // Troll Berserkers
    {TEXT("troll_axe_frenzy"), EKit::Frenzy}, {TEXT("troll_blood_leap"), EKit::Leap, 0, .45f}, {TEXT("troll_hunger"), EKit::Passive},
    {TEXT("troll_red_moon"), EKit::SelfBuff}, {TEXT("troll_twin_throw"), EKit::TwinThrow}, {TEXT("troll_returning_axes"), EKit::Returning, 0, .1f},
    // Dryad
    {TEXT("dryad_root_snare"), EKit::Circle, 0, .6f}, {TEXT("dryad_seed_mend"), EKit::DelayedHeal}, {TEXT("dryad_thorn_line"), EKit::Line, 0, .3f},
    {TEXT("dryad_green_covenant"), EKit::Passive}, {TEXT("dryad_grove_renewal"), EKit::HealZone},
    // Whisp
    {TEXT("whisp_guiding_mote"), EKit::AllyOrb}, {TEXT("whisp_spirit_tether"), EKit::Tether}, {TEXT("whisp_fey_trail"), EKit::Motes},
    {TEXT("whisp_lantern_soul"), EKit::Passive}, {TEXT("whisp_constellation"), EKit::Constellation},
    // Evergrove Centaur
    {TEXT("centaur_grove_javelin"), EKit::Javelin}, {TEXT("centaur_trailblaze"), EKit::Trail, 0, .2f}, {TEXT("centaur_herd_call"), EKit::Summon},
    {TEXT("centaur_steady_gait"), EKit::Passive}, {TEXT("centaur_spring_march"), EKit::MovingAura},
    // Keeper of Light
    {TEXT("keeper_dawn_beam"), EKit::Beam}, {TEXT("keeper_lantern_ward"), EKit::Construct}, {TEXT("keeper_beacon"), EKit::Beacon},
    {TEXT("keeper_last_light"), EKit::Passive}, {TEXT("keeper_sunrise"), EKit::Sunrise},
};
const FKit* FindKit(const FString& Id) { for (const FKit& K : Kits) if (Id == K.Id) return &K; return nullptr; }

// Buff records (identity for visuals + the magnitude in Stacks).
const FName HibernateId(TEXT("bear_hibernate")), HideId(TEXT("ancient_hide")), ColossusId(TEXT("bear_colossus")), CowedId(TEXT("bear_cowed")),
    RelicMarkId(TEXT("relic_mark")), RelicVowId(TEXT("relic_vow")), MountainId(TEXT("mountain_heart")), CoreId(TEXT("construct_core")),
    WorldTankId(TEXT("worldstone_tank")), WorldBruiserId(TEXT("worldstone_bruiser")), GraniteId(TEXT("living_granite")), FurnaceId(TEXT("ether_furnace")),
    RallyId(TEXT("blood_oath_banner")), ResistId(TEXT("deep_lantern")), WeightId(TEXT("ancestral_weight")), DragonId(TEXT("dragon_form")),
    ScaleId(TEXT("scale_guard")), PactId(TEXT("ancient_pact")), RedMoonId(TEXT("red_moon")), SeedId(TEXT("seed_mend")), GroveId(TEXT("grove_renewal")),
    TetherId(TEXT("spirit_tether")), LinkId(TEXT("kindred_link")), TrailId(TEXT("evergrove_trail")), GaitId(TEXT("steady_gait")), MarchId(TEXT("spring_march")),
    BeaconId(TEXT("beacon_of_return")), SunriseId(TEXT("sunrise_vigil")), BurnId(TEXT("kit_burning")), RootedId(TEXT("npc_rooted")),
    VulnerableId(TEXT("l15_vulnerable")), FrenzyId(TEXT("axe_frenzy")), MendId(TEXT("tumbling_mend"));

// Riders authored per skill (beyond the Ability DB effect list handled by CireCrowdControl / CireSignatureSkills).
const TSet<FString> TripleThreat = {TEXT("bear_maul"), TEXT("golem_granite_fist")};
const TMap<FString, float> Knockback = {{TEXT("behemoth_totem_sweep"), 200.f}, {TEXT("drakish_wing_rebuke"), 300.f}, {TEXT("behemoth_stampede"), 250.f}};
const TMap<FString, float> BurnFraction = {{TEXT("golem_fel_fist"), .3f}};
const TSet<FString> EmberSources = {TEXT("drakish_dragon_oath"), TEXT("drakish_wing_rebuke"), TEXT("drakish_ancient_pact")};

ACireGameMode* ModeOf(const AActor* A) { return A && A->GetWorld() ? A->GetWorld()->GetAuthGameMode<ACireGameMode>() : nullptr; }
float Now(const UWorld* W) { return W ? W->GetTimeSeconds() : 0.f; }
bool IsBoss(const AActor* U) { const auto* M = Cast<ACireMonster>(U); return M && (M->IsLaneBoss() || M->bBoss); }
float Body(const AActor* U) { const auto* C = Cast<ACharacter>(U); return C ? C->GetCapsuleComponent()->GetScaledCapsuleRadius() : 30.f; }
FVector FeetOf(const AActor* U) { const auto* C = Cast<ACharacter>(U); return U->GetActorLocation() - FVector(0, 0, C ? C->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() : 0.f); }
float HealthFraction(const ACireHero* H) { return H ? H->Health / FMath::Max(1.f, H->MaxHealth) : 1.f; }
const FCireBuffEntry* ActiveBuff(const AActor* Unit, FName Id)
{
    if (!Unit || !CireBuffs::IsActive(Unit, Id)) return nullptr;
    const auto* State = CireBuffs::Get(Unit); return State ? State->Find(Id) : nullptr;
}
float BuffFraction(const AActor* Unit, FName Id) { const auto* E = ActiveBuff(Unit, Id); return E ? E->Stacks / 100.f : 0.f; }
uint8 Percent(float Value) { return static_cast<uint8>(FMath::Clamp(FMath::RoundToInt(Value), 1, 250)); }
UCireKitSkillsSubsystem* Sub(const UWorld* W) { return UCireKitSkillsSubsystem::Get(W); }

bool Sight(const AActor* From, const AActor* To)
{
    if (!IsValid(From) || !IsValid(To)) return false;
    FHitResult Hit; FCollisionQueryParams Q(SCENE_QUERY_STAT(CireKitSight), false, From);
    const bool bBlocked = From->GetWorld()->LineTraceSingleByChannel(Hit, From->GetActorLocation() + FVector(0, 0, 45), To->GetActorLocation() + FVector(0, 0, 35), ECC_Visibility, Q);
    return !bBlocked || Hit.GetActor() == To || Cast<ACharacter>(Hit.GetActor()) != nullptr;
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
    for (TActorIterator<ACireHero> It(Hero->GetWorld()); It; ++It) Consider(*It);
    return Out;
}
TArray<ACireHero*> Allies(const ACireHero* Hero, FVector Center, float Radius, bool bSelf = true)
{
    TArray<ACireHero*> Out;
    if (!Hero) return Out;
    for (TActorIterator<ACireHero> It(Hero->GetWorld()); It; ++It)
    {
        ACireHero* A = *It;
        if (A->IsA<ACireSummon>() || !A->bDrafted || A->bDead || A->TeamId != Hero->TeamId || (!bSelf && A == Hero)) continue;
        if (FVector::DistSquared2D(Center, A->GetActorLocation()) <= FMath::Square(Radius + Body(A)) && FMath::Abs(A->GetActorLocation().Z - Center.Z) < 400.f) Out.Add(A);
    }
    return Out;
}
// Distance from P to the segment A-B in the ground plane, and how far along it P projects (0..1).
float SegmentDistance(FVector P, FVector A, FVector B, float* OutAlong = nullptr)
{
    const FVector2D AB(B.X - A.X, B.Y - A.Y), AP(P.X - A.X, P.Y - A.Y);
    const float Len2 = FMath::Max(1.f, static_cast<float>(AB.SizeSquared()));
    const float T = FMath::Clamp(static_cast<float>(FVector2D::DotProduct(AP, AB)) / Len2, 0.f, 1.f);
    if (OutAlong) *OutAlong = T;
    return static_cast<float>((AP - AB * T).Size());
}
bool GroundAt(ACireHero* Hero, FVector& Aim)
{
    FCollisionQueryParams Query(SCENE_QUERY_STAT(CireKitGround), false, Hero);
    FHitResult Hit;
    if (!Hero->GetWorld()->LineTraceSingleByObjectType(Hit, Aim + FVector(0, 0, 300), Aim - FVector(0, 0, 600), FCollisionObjectQueryParams(ECC_WorldStatic), Query) || Hit.ImpactNormal.Z < .7f) return false;
    Aim = Hit.ImpactPoint; return true;
}
bool GroundSight(ACireHero* Hero, FVector Ground)
{
    FCollisionQueryParams Query(SCENE_QUERY_STAT(CireKitPlacement), false, Hero);
    FHitResult Hit;
    return !Hero->GetWorld()->LineTraceSingleByObjectType(Hit, Hero->GetActorLocation() + FVector(0, 0, 35), Ground + FVector(0, 0, 60), FCollisionObjectQueryParams(ECC_WorldStatic), Query) &&
        !ACireConstruct::FindBlockingConstruct(Hero, Ground);
}
// Furthest point along Dir (up to Distance) the caster's capsule can travel before a wall or blocking construct.
float FreeTravel(ACireHero* Hero, FVector Dir, float Distance)
{
    const FVector From = Hero->GetActorLocation();
    FCollisionQueryParams Q(SCENE_QUERY_STAT(CireKitCharge), false, Hero);
    FHitResult Hit;
    const float Radius = Body(Hero) * .8f;
    float Free = Distance;
    if (Hero->GetWorld()->SweepSingleByObjectType(Hit, From, From + Dir * Distance, FQuat::Identity, FCollisionObjectQueryParams(ECC_WorldStatic), FCollisionShape::MakeSphere(Radius), Q))
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
    if (bMoved) { if (!Dir.IsNearlyZero()) Hero->SetActorRotation(Dir.Rotation()); ACireAreaEffect::ClearForActor(Hero); }
    return bMoved;
}
void Later(UWorld* World, float Seconds, TFunction<void()> Work)
{
    if (!World) return;
    FTimerHandle Handle;
    World->GetTimerManager().SetTimer(Handle, FTimerDelegate::CreateLambda(MoveTemp(Work)), FMath::Max(.01f, Seconds), false);
}
void Push(AActor* Unit, FVector Away, float Distance)
{
    if (IsBoss(Unit) || Distance <= 0) return;
    if (auto* C = Cast<ACharacter>(Unit)) C->LaunchCharacter(Away.GetSafeNormal2D() * Distance * 2.4f + FVector(0, 0, 260), true, true);
}
void PullTo(AActor* Unit, FVector Anchor, float Keep)
{
    auto* C = Cast<ACharacter>(Unit);
    if (!C || IsBoss(Unit)) return;
    const FVector Back = Anchor - Unit->GetActorLocation();
    const float Dist = static_cast<float>(Back.Size2D());
    if (Dist > Keep + 20.f) C->LaunchCharacter(Back.GetSafeNormal2D() * FMath::Clamp((Dist - Keep) * 2.4f, 500.f, 3200.f) + FVector(0, 0, 220), true, true);
}
void Root(AActor* Unit, float Seconds, AActor* Source)
{
    if (IsBoss(Unit) || Seconds <= 0) return;
    CireBuffs::Apply(Unit, RootedId, Seconds, Source);
    if (auto* C = Cast<ACharacter>(Unit)) C->GetCharacterMovement()->StopMovementImmediately();
}
void Taunt(ACireHero* H, AActor* Unit, float Seconds)
{
    if (auto* M = Cast<ACireMonster>(Unit)) CireThreat::Taunt(M, H, Seconds);
    else if (auto* E = Cast<ACireHero>(Unit)) { if (E->bBot) E->Target = H; CireBuffs::Apply(E, TEXT("taunted"), Seconds, H); }
}
FLinearColor Tint(const FCireAbilityDef& D, bool bHeal)
{
    if (bHeal) return FLinearColor(.35f, 1.f, .45f, .26f);
    ECireSchool S = ECireSchool::Arcane; CireAbilityShapes::ParseSchool(D.School, S);
    const FLinearColor C = CireAbilityShapes::SchoolColor(S);
    return FLinearColor(FMath::Min(C.R, 1.f), FMath::Min(C.G, 1.f), FMath::Min(C.B, 1.f), .36f);
}
FCireAreaSpec AreaFor(const FCireAbilityDef& D, ECireAreaShape Shape, float Warning, float Burst)
{
    FCireAreaSpec A; A.Shape = Shape; A.Radius = FMath::Max(10.f, D.Radius); A.Length = FMath::Max(10.f, D.Range); A.Width = FMath::Max(20.f, D.Radius * 2.f);
    A.ConeAngleDegrees = 60; A.WarningSeconds = Warning; A.DurationSeconds = .1f; A.TickInterval = .5f; A.DamagePerSecond = 0; A.BurstDamage = Burst;
    A.bPersistent = false; A.bPoison = false; A.VerticalTolerance = 90; A.Color = Tint(D, false); A.AbilityName = D.Name; return A;
}
// A harmless persistent ground zone: the visible field for heal zones, beacons, trails (replication + realm privacy for free).
ACireAreaEffect* Field(ACireHero* H, const FCireAbilityDef& D, ECireAreaShape Shape, FVector Ground, FRotator Heading, float Radius, float Length, float Width,
    float Seconds, float Warning, bool bHeal)
{
    FCireAreaSpec A; A.Shape = Shape; A.Radius = FMath::Max(10.f, Radius); A.Length = FMath::Max(10.f, Length); A.Width = FMath::Max(20.f, Width);
    A.WarningSeconds = Warning; A.DurationSeconds = FMath::Clamp(Seconds, .1f, 60.f); A.TickInterval = 1.f; A.DamagePerSecond = 0; A.BurstDamage = 0;
    A.bPersistent = true; A.bPoison = false; A.VerticalTolerance = 120; A.Color = Tint(D, bHeal); A.Color.A = .2f; A.AbilityName = D.Name;
    return ACireAreaEffect::Spawn(H, A, Ground, Heading);
}
// Smart cast: the selected ally, else the most wounded ally in range (you included when allowed).
ACireHero* PickAlly(ACireHero* H, float Range, bool bSelfOk)
{
    if (auto* A = Cast<ACireHero>(H->Target); A && !A->IsA<ACireSummon>() && A->TeamId == H->TeamId && !A->bDead && A->bDrafted && (bSelfOk || A != H) &&
        H->InRange(A, Range + 40.f) && (A == H || Sight(H, A))) return A;
    ACireHero* Best = nullptr; float Lowest = 2.f;
    for (ACireHero* A : Allies(H, H->GetActorLocation(), Range, bSelfOk))
        if ((A == H || Sight(H, A)) && HealthFraction(A) < Lowest) { Lowest = HealthFraction(A); Best = A; }
    return Best;
}
float PotencyOf(const ACireHero* H, const FString& Id, float Fallback) { return CireKits::ScaledEffect(H, Id, Fallback); }
float Seconds(UWorld* W, float S) { return CireDeveloperTools::EffectSeconds(W, S); }
bool IsKitName(const FString& Name, const FCireAbilityDef*& Out) { Out = CireAbilityDB::FindByName(Name); return Out && FindKit(Out->Id) != nullptr; }
float ControlSeconds(AActor* Source, float Base) { return Base * CireKits::ControlScale(Source); }
}

// ============================================================================================ identity
bool CireKitSkills::Knows(const FString& Id) { return FindKit(Id) != nullptr; }
bool CireKitSkills::Handles(const FString& Id) { const FKit* K = FindKit(Id); return K && K->D != EKit::Passive; }
bool CireKitSkills::IsPassive(const FString& Id) { const FKit* K = FindKit(Id); return K && K->D == EKit::Passive; }
const TArray<FString>& CireKitSkills::AllIds() { static TArray<FString> Ids = [] { TArray<FString> Out; for (const FKit& K : Kits) Out.Add(K.Id); return Out; }(); return Ids; }
const TArray<FName>& CireKitSkills::BuffIds()
{
    static const TArray<FName> Ids = {HibernateId, HideId, ColossusId, CowedId, RelicMarkId, RelicVowId, MountainId, CoreId, WorldTankId, WorldBruiserId,
        GraniteId, FurnaceId, RallyId, ResistId, WeightId, DragonId, ScaleId, PactId, RedMoonId, SeedId, GroveId, TetherId, LinkId, TrailId, GaitId, MarchId,
        BeaconId, SunriseId, BurnId, FrenzyId, MendId};
    return Ids;
}

// ============================================================================================ casting
bool CireKitSkills::Cast(ACireHero* Hero, int32 Slot, const FString& Id)
{
    const FKit* Kit = FindKit(Id);
    const FCireAbilityDef* Def = CireAbilityDB::Find(Id);
    if (!IsValid(Hero) || !Hero->HasAuthority() || !Kit || !Def || Kit->D == EKit::Passive || !CireSkillRuntime::Alive(Hero) ||
        !Hero->Skills.IsValidIndex(Slot) || Hero->Skills[Slot] != Id || !Hero->Cooldowns.IsValidIndex(Slot) || Hero->Cooldowns[Slot] > 0 || Hero->GlobalCooldown > 0) return false;
    UWorld* World = Hero->GetWorld();
    auto* Mode = World->GetAuthGameMode<ACireGameMode>();
    if (!Mode || !Mode->IsCombatPhase()) return false;
    auto Fail = [&](const FString& Message) { Hero->Notice = Message; return false; };
    const float Mana = Def->Base.ManaCost, Energy = Def->Base.EnergyCost;
    if (!CireSkillShop::CanPayCast(Hero, Id, Mana, Energy)) return Fail(*CireSkillShop::CostFailText());
    const float Range = Def->Range > 0 ? Def->Range : 900.f;
    const float Power = Mode->Power(Hero->TeamId);
    const float Amount = FMath::Min(10000.f, CireKits::Amount(Hero, Id, Def->Base.Effect) * Power); // base + coef x PRIMARY (Ability DB)
    const float Magnitude = PotencyOf(Hero, Id, Def->Base.Effect);                                   // utility: level x potency
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
    // Heal zones and allied fields smart-cast onto the most wounded ally when nothing is aimed.
    auto AllyAim = [&](float Reach)
    {
        if (Hero->bHasCastAim) return;
        if (ACireHero* A = PickAlly(Hero, Reach, true)) Aim = A->GetActorLocation();
        else Aim = Origin;
    };
    auto* S = Sub(World);
    bool bCue = true;
    FName CueTarget = NAME_None;
    const FString Name = Def->Name;
    TWeakObjectPtr<ACireHero> Weak(Hero);
    switch (Kit->D)
    {
    case EKit::Strike:
    {
        const float Reach = Def->Range > 0 ? Def->Range : 280.f;
        if (!NeedEnemy(Reach)) return Fail(TEXT("Select a hostile target in range and line of sight."));
        const float Applied = CireCombat::ApplyStrike(Hero, Target, Amount, Name);
        if (Id == TEXT("paladin_holy_flail") && Applied > 0)
            for (ACireHero* A : Allies(Hero, Target->GetActorLocation(), Def->Radius > 0 ? Def->Radius : 450.f))
                CireCombat::ApplyHealing(Hero, A, Applied * .15f, Name); // 15% of the censer hit (balance lab)
        Aim = Target->GetActorLocation(); break;
    }
    case EKit::Cone:
    {
        FCireAreaSpec A = AreaFor(*Def, ECireAreaShape::Cone, Kit->Warning, Amount); A.ConeAngleDegrees = FMath::Clamp(Kit->Angle, 10.f, 170.f);
        if (!ACireAreaEffect::Spawn(Hero, A, Feet, Direction.Rotation())) return Fail(TEXT("Cannot unleash that here."));
        Hero->SetActorRotation(Direction.Rotation()); Aim = Origin + Direction * Def->Radius; break;
    }
    case EKit::Circle:
    {
        if (!NeedGround(true)) return false;
        FCireAreaSpec A = AreaFor(*Def, ECireAreaShape::Circle, Kit->Warning, Amount);
        if (Id == TEXT("miner_mountain")) { A.bPersistent = true; A.DurationSeconds = FMath::Max(1.f, Def->Duration); A.TickInterval = 1.f; }
        ACireAreaEffect* Area = ACireAreaEffect::Spawn(Hero, A, Aim, FRotator::ZeroRotator);
        if (!Area) return Fail(TEXT("Cannot create that area here."));
        if (Id == TEXT("miner_mountain") && S)
        {
            UCireKitSkillsSubsystem::FZone Z; Z.Owner = Hero; Z.Area = Area; Z.Id = Id; Z.Name = Name; Z.Kind = UCireKitSkillsSubsystem::EZone::Mountain;
            Z.Center = Aim; Z.Radius = Def->Radius; Z.Magnitude = .25f; Z.StartsAt = Now(World) + Kit->Warning; Z.EndsAt = Z.StartsAt + Seconds(World, Def->Duration);
            Z.Interval = .5f; S->Zones.Add(Z);
        }
        break;
    }
    case EKit::Line:
    {
        FCireAreaSpec A = AreaFor(*Def, ECireAreaShape::Line, Kit->Warning, Amount);
        if (Id == TEXT("dryad_thorn_line"))
        {
            A.BurstDamage = 0; A.bPersistent = true; A.DurationSeconds = FMath::Max(1.f, Seconds(World, Def->Duration)); A.TickInterval = .5f;
            A.DamagePerSecond = Amount;
        }
        if (!ACireAreaEffect::Spawn(Hero, A, Feet, Direction.Rotation())) return Fail(TEXT("Cannot unleash that here."));
        Hero->SetActorRotation(Direction.Rotation()); Aim = Origin + Direction * Def->Range; break;
    }
    case EKit::Burst:
    {
        const float Radius = Def->Radius > 0 ? Def->Radius : 450.f;
        if (Id == TEXT("chieftain_earthshout"))
        {
            // The shout lands after its warning: enemies taunted and silenced, allies shielded.
            Field(Hero, *Def, ECireAreaShape::Circle, Feet, FRotator::ZeroRotator, Radius, 0, 0, Kit->Warning + .2f, Kit->Warning, false);
            const float Barrier = Amount * FMath::Max(1.f, CireSkillShop::CastScale(Hero, Id).Effect); // barriers: level scale by hand
            Later(World, Kit->Warning, [Weak, Radius, Barrier, Name, Def]()
            {
                ACireHero* H = Weak.Get(); if (!CireSkillRuntime::Alive(H)) return;
                for (AActor* U : Enemies(H, H->GetActorLocation(), Radius))
                {
                    Taunt(H, U, ControlSeconds(H, 4.f));
                    CireCrowdControl::Silence(U, ControlSeconds(H, 1.5f), H);
                }
                for (ACireHero* A : Allies(H, H->GetActorLocation(), Radius)) CireItems::GrantBarrier(A, Barrier, Seconds(H->GetWorld(), Def->Duration > 0 ? Def->Duration : 8.f), H);
                CireCombat::PlayCue(H, nullptr, FName(*Def->Id), H->GetActorLocation(), H->GetActorLocation(), ECireSpellCue::Impact, Radius / 400.f, true);
            });
            Aim = Feet; break;
        }
        FCireAreaSpec A = AreaFor(*Def, ECireAreaShape::Circle, Kit->Warning, Amount);
        if (!ACireAreaEffect::Spawn(Hero, A, Feet, FRotator::ZeroRotator)) return Fail(TEXT("Cannot unleash that here."));
        const float Duration = Seconds(World, Def->Duration > 0 ? Def->Duration : 8.f);
        if (Id == TEXT("bear_colossus"))
        {
            CireBuffs::Apply(Hero, ColossusId, Duration, Hero, 30);
            for (AActor* U : Enemies(Hero, Origin, Radius)) Taunt(Hero, U, ControlSeconds(Hero, 4.f));
        }
        else if (Id == TEXT("drakish_ancient_pact"))
        {
            for (ACireHero* Ally : Allies(Hero, Origin, Radius)) CireBuffs::Apply(Ally, PactId, Duration, Hero, 20);
            if (S) { auto& Dr = S->Dragons.FindOrAdd(Hero); Dr.Charges = 99; Dr.EndsAt = Now(World) + Duration; Dr.bPact = true; }
            CireBuffs::Apply(Hero, DragonId, Duration, Hero, 99);
        }
        Aim = Feet; break;
    }
    case EKit::Charge:
    {
        const bool bThrough = Id == TEXT("behemoth_tusk_line");
        const float Free = FreeTravel(Hero, Direction, Range);
        const float Width = FMath::Max(40.f, Def->Radius);
        FVector Stop = Origin + Direction * Free;
        AActor* First = nullptr; float FirstAlong = 2.f;
        TArray<AActor*> Lane;
        for (AActor* U : Enemies(Hero, Origin + Direction * Free * .5f, Free * .5f + Width + 100.f))
        {
            float Along = 0; const float Off = SegmentDistance(U->GetActorLocation(), Origin, Origin + Direction * Free, &Along);
            if (Off > Width + Body(U)) continue;
            Lane.Add(U);
            if (Along < FirstAlong) { FirstAlong = Along; First = U; }
        }
        if (!bThrough && First) Stop = First->GetActorLocation() - Direction * (Body(First) + Body(Hero) + 20.f);
        if (FVector::DistSquared2D(Stop, Origin) > FMath::Square(40.f) && !MoveTo(Hero, Stop)) return Fail(TEXT("The path is blocked."));
        if (bThrough) for (AActor* U : Lane) CireCombat::ApplyDamage(Hero, U, Amount, Name);
        else if (First) CireCombat::ApplyDamage(Hero, First, Amount, Name);
        if (Def->Void.bValid) CireCrowdControl::VoidBurst(Hero, FeetOf(Hero), Id);
        Aim = Origin + Direction * Free; break;
    }
    case EKit::Leap:
    {
        if (!NeedGround(false)) return false;
        const FVector Dir = (Aim - Origin).GetSafeNormal2D();
        const float Free = FreeTravel(Hero, Dir.IsNearlyZero() ? Direction : Dir, FMath::Min(Range, static_cast<float>(FVector::Dist2D(Origin, Aim))));
        if (!MoveTo(Hero, Origin + (Dir.IsNearlyZero() ? Direction : Dir) * Free)) return Fail(TEXT("The path is blocked."));
        FCireAreaSpec A = AreaFor(*Def, ECireAreaShape::Circle, 0.f, Amount);
        ACireAreaEffect::Spawn(Hero, A, FeetOf(Hero), FRotator::ZeroRotator);
        if (Def->Void.bValid) CireCrowdControl::VoidBurst(Hero, FeetOf(Hero), Id);
        Aim = FeetOf(Hero); break;
    }
    case EKit::Hook:
    {
        // The first enemy in the chain's corridor (with sight) is struck and dragged toward you.
        AActor* Hooked = nullptr; float Best = 2.f;
        const FVector End = Origin + Direction * Range;
        for (AActor* U : Enemies(Hero, Origin + Direction * Range * .5f, Range * .5f + 200.f))
        {
            float Along = 0; const float Off = SegmentDistance(U->GetActorLocation(), Origin, End, &Along);
            if (Off <= FMath::Max(40.f, Def->Radius) + Body(U) && Along < Best && Sight(Hero, U)) { Best = Along; Hooked = U; }
        }
        CireCombat::PlayCue(Hero, Hooked, FName(*Id), Origin, Hooked ? Hooked->GetActorLocation() : End, ECireSpellCue::Launch, .9f, true);
        if (Hooked)
        {
            CireCombat::ApplyStrike(Hero, Hooked, Amount, Name);
            PullTo(Hooked, Origin + Direction * (Body(Hero) + 120.f), Body(Hero) + 120.f);
        }
        Aim = End; break;
    }
    case EKit::Channel:
    {
        const float Duration = Seconds(World, Def->Duration > 0 ? Def->Duration : 4.f);
        CireBuffs::Apply(Hero, HibernateId, Duration, Hero, 20);
        if (S)
        {
            S->Channels.RemoveAll([Hero](const UCireKitSkillsSubsystem::FChannel& C) { return C.Hero.Get() == Hero; });
            UCireKitSkillsSubsystem::FChannel C; C.Hero = Hero; C.Start = Origin; C.PerTick = Amount * .5f; C.EndsAt = Now(World) + Duration; C.Name = Name;
            S->Channels.Add(C);
        }
        Aim = Feet; break;
    }
    case EKit::SelfBuff:
    {
        const float Duration = Seconds(World, Def->Duration > 0 ? Def->Duration : 6.f);
        if (Id == TEXT("golem_ether_furnace")) { CireBuffs::Apply(Hero, FurnaceId, Duration, Hero, Percent(Magnitude)); if (S) S->FurnaceCharges.Add(Hero, 4); }
        else if (Id == TEXT("drakish_scale_guard")) CireBuffs::Apply(Hero, ScaleId, Duration, Hero, Percent(FMath::Min(Magnitude, 80.f)));
        else if (Id == TEXT("troll_red_moon")) CireBuffs::Apply(Hero, RedMoonId, Duration, Hero, Percent(Magnitude));
        Aim = Feet; break;
    }
    case EKit::AllyBarrier:
    {
        ACireHero* Ally = PickAlly(Hero, Range, true);
        if (!Ally) return Fail(TEXT("No ally in range."));
        const float Duration = Seconds(World, Def->Duration > 0 ? Def->Duration : 8.f);
        CireItems::GrantBarrier(Ally, Amount * FMath::Max(1.f, CireSkillShop::CastScale(Hero, Id).Effect), Duration, Hero); // barriers: level scale by hand
        CireBuffs::Apply(Ally, Id == TEXT("paladin_relic_vow") ? RelicVowId : GraniteId, Duration, Hero, 30);
        if (Id == TEXT("golem_living_granite") && S)
        {
            // 2% of max health per second while the barrier holds (checked by the tick).
            UCireKitSkillsSubsystem::FHot X; X.Source = Hero; X.Target = Ally; X.Name = Name; X.PerTick = Ally->MaxHealth * .02f; // balance lab: 2%/s X.Ticks = FMath::RoundToInt(Duration);
            X.Buff = GraniteId; S->Hots.Add(X);
        }
        CireCombat::PlayCue(Hero, Ally, FName(*Id), Origin, Ally->GetActorLocation(), ECireSpellCue::Impact, 1.f, false);
        Aim = Ally->GetActorLocation(); break;
    }
    case EKit::DelayedHeal:
    {
        ACireHero* Ally = PickAlly(Hero, Range, true);
        if (!Ally) return Fail(TEXT("No ally in range."));
        const float Delay = FMath::Max(.5f, Def->Duration);
        CireBuffs::Apply(Ally, SeedId, Delay, Hero);
        TWeakObjectPtr<ACireHero> WeakAlly(Ally);
        Later(World, Delay, [Weak, WeakAlly, Amount, Name, Id]()
        {
            ACireHero* H = Weak.Get(); ACireHero* A = WeakAlly.Get();
            if (!CireSkillRuntime::Alive(H) || !CireSkillRuntime::Alive(A)) return;
            CireCombat::ApplyHealing(H, A, Amount * (HealthFraction(A) < .4f ? 1.5f : 1.f), Name);
            CireBuffs::Remove(A, SeedId);
        });
        Aim = Ally->GetActorLocation(); break;
    }
    case EKit::AllyOrb:
    {
        // Smart cast: a selected ally sets the direction; otherwise the aim (or facing) does.
        if (auto* Sel = Cast<ACireHero>(Target); Sel && Sel != Hero && Sel->TeamId == Hero->TeamId && !Sel->bDead && !Hero->bHasCastAim)
            Direction = (Sel->GetActorLocation() - Origin).GetSafeNormal2D();
        else if (!Hero->bHasCastAim) if (ACireHero* A = PickAlly(Hero, Range, false)) Direction = (A->GetActorLocation() - Origin).GetSafeNormal2D();
        if (Direction.IsNearlyZero()) Direction = Hero->GetActorForwardVector().GetSafeNormal2D();
        const float Speed = Id == TEXT("whisp_guiding_mote") ? 1400.f : 900.f;
        const FVector End = Origin + Direction * Range;
        ACireHero* First = nullptr; float Best = 2.f;
        for (ACireHero* A : Allies(Hero, Origin + Direction * Range * .5f, Range * .5f + 150.f, false))
        {
            float Along = 0; const float Off = SegmentDistance(A->GetActorLocation(), Origin, End, &Along);
            if (Off <= FMath::Max(40.f, Def->Radius) + Body(A) && Along < Best && Sight(Hero, A)) { Best = Along; First = A; }
        }
        const FVector Land = First ? First->GetActorLocation() : End;
        const float Travel = static_cast<float>(FVector::Dist2D(Origin, Land)) / Speed;
        CireCombat::PlayCue(Hero, First, FName(*Id), Origin, Land, ECireSpellCue::Launch, .9f, true);
        TWeakObjectPtr<ACireHero> WeakFirst(First);
        const bool bHasAlly = First != nullptr;
        Later(World, Travel, [Weak, WeakFirst, bHasAlly, Land, Amount, Name, Id]()
        {
            ACireHero* H = Weak.Get(); if (!CireSkillRuntime::Alive(H)) return;
            if (bHasAlly) { if (ACireHero* A = WeakFirst.Get(); CireSkillRuntime::Alive(A)) CireCombat::ApplyHealing(H, A, Amount, Name); return; }
            for (ACireHero* A : Allies(H, Land, 200.f)) CireCombat::ApplyHealing(H, A, Amount, Name);
            CireCombat::PlayCue(H, nullptr, FName(*Id), Land, Land, ECireSpellCue::Impact, .8f, false);
        });
        Aim = End; bCue = false; break;
    }
    case EKit::HealZone:
    {
        AllyAim(Range);
        if (!NeedGround(false)) return false;
        const float Duration = Seconds(World, Def->Duration > 0 ? Def->Duration : 6.f);
        ACireAreaEffect* Area = Field(Hero, *Def, ECireAreaShape::Circle, Aim, FRotator::ZeroRotator, Def->Radius, 0, 0, Duration, 0.f, true);
        if (!Area) return Fail(TEXT("Cannot grow that here."));
        if (S)
        {
            UCireKitSkillsSubsystem::FZone Z; Z.Owner = Hero; Z.Area = Area; Z.Id = Id; Z.Name = Name; Z.Kind = UCireKitSkillsSubsystem::EZone::Heal;
            Z.Center = Aim; Z.Radius = Def->Radius; Z.PerSecond = Amount; Z.StartsAt = Now(World); Z.EndsAt = Z.StartsAt + Duration; Z.Interval = 1.f;
            Z.Magnitude = Id == TEXT("dryad_grove_renewal") ? .15f : 0.f; S->Zones.Add(Z);
        }
        break;
    }
    case EKit::Tether:
    {
        ACireHero* Ally = PickAlly(Hero, Range, false);
        if (!Ally) Ally = PickAlly(Hero, Range, true);
        if (!Ally) return Fail(TEXT("No ally in range."));
        const float Duration = Seconds(World, Def->Duration > 0 ? Def->Duration : 6.f);
        CireBuffs::Apply(Ally, TetherId, Duration, Hero);
        if (S)
        {
            UCireKitSkillsSubsystem::FHot X; X.Source = Hero; X.Target = Ally; X.Name = Name; X.PerTick = Amount; X.Ticks = FMath::RoundToInt(Duration);
            X.MaxDistance = 900.f; X.Buff = TetherId; X.Timer = .25f; S->Hots.Add(X);
        }
        Aim = Ally->GetActorLocation(); break;
    }
    case EKit::Motes:
    {
        const float Duration = Seconds(World, Def->Duration > 0 ? Def->Duration : 6.f);
        Field(Hero, *Def, ECireAreaShape::Line, Feet, Direction.Rotation(), 0, Range, FMath::Max(120.f, Def->Radius * 2.f), Duration, 0.f, true);
        if (S) for (int32 I = 0; I < 5; ++I)
        {
            UCireKitSkillsSubsystem::FMote M; M.Owner = Hero; M.At = Feet + Direction * (Range * (I + .5f) / 5.f); M.Amount = Amount; M.EndsAt = Now(World) + Duration; M.Name = Name;
            S->Motes.Add(M);
        }
        Aim = Origin + Direction * Range; break;
    }
    case EKit::Construct:
    {
        if (!NeedGround(true)) return false;
        FString Why;
        if (CireTechConstructs::Deploy(Hero, FName(*Id), Aim, &Why).IsEmpty()) return Fail(Why);
        break;
    }
    case EKit::Wall:
    {
        if (!NeedGround(true)) return false;
        for (TActorIterator<ACireConstruct> It(World); It; ++It) if (It->GetSourceActor() == Hero && It->GetDisplayName() == Name) It->Destroy(); // one per owner
        FCireConstructSpec W; W.Kind = ECireConstructKind::Wall; W.MaxHealth = FMath::Min(20000.f, Amount); W.LifetimeSeconds = Seconds(World, Def->Duration > 0 ? Def->Duration : 8.f);
        W.Width = FMath::Max(120.f, Def->Radius * 2.f); W.Depth = 60.f; W.Height = 230.f; W.ManaCost = 0; W.EnergyCost = Energy; W.CooldownSeconds = Def->Base.Cooldown; W.CastRange = Range + 60.f;
        W.bBlockMovement = true; W.bBlockProjectiles = true; W.bDestructible = true; W.bBlockFriendly = false; W.Color = FLinearColor(.55f, .42f, .26f, .95f);
        const FVector Dir = (Aim - Origin).GetSafeNormal2D();
        if (!ACireConstruct::Spawn(Hero, W, Aim, Dir.IsNearlyZero() ? Hero->GetActorRotation() : Dir.Rotation(), Name)) return Fail(TEXT("The totem cannot be braced there."));
        break;
    }
    case EKit::Summon:
    {
        AActor* Prey = bHostile ? Target : nullptr;
        if (!Prey) { float Best = FMath::Square(1400.f); for (AActor* U : Enemies(Hero, Origin, 1400.f)) { const float D = FVector::DistSquared2D(Origin, U->GetActorLocation()); if (D < Best) { Best = D; Prey = U; } } }
        if (!Prey) return Fail(TEXT("No enemy for the stag to hunt."));
        const FCireSummonSpec* Base = CireSkillTuning::FindSummon(TEXT("spectral_pack"));
        FCireSummonSpec Spec = Base ? *Base : FCireSummonSpec();
        Spec.Count = 1; Spec.Damage = Amount; Spec.Health = FMath::Min(20000.f, 300.f + 8.f * Hero->PrimaryAttribute());
        Spec.DurationSeconds = Def->Duration > 0 ? Def->Duration : 15.f; Spec.ManaCost = 0; Spec.EnergyCost = 0; Spec.CooldownSeconds = 0; Spec.bCommandable = false;
        CireDeveloperTools::AdjustSummon(World, Spec);
        FVector At = Aim; if (!Hero->bHasCastAim) At = Origin + Direction * 150.f;
        const auto Units = ACireSummon::SpawnGroup(Hero, Spec, Prey, At);
        if (Units.IsEmpty()) return Fail(TEXT("No room for the stag there."));
        Aim = At; break;
    }
    case EKit::Frenzy:
    {
        const float Reach = Def->Range > 0 ? Def->Range : 280.f;
        if (!NeedEnemy(Reach)) return Fail(TEXT("Select a hostile target in range and line of sight."));
        const float Commit = FMath::Max(.5f, Def->Duration);
        CireBuffs::Apply(Hero, FrenzyId, Commit, Hero);
        Hero->SlowUntil = FMath::Max(Hero->SlowUntil, Now(World) + Commit);
        TWeakObjectPtr<AActor> Victim(Target);
        for (int32 I = 0; I < 5; ++I)
            Later(World, .02f + I * Commit / 5.f, [Weak, Victim, Amount, Name, Reach]()
            {
                ACireHero* H = Weak.Get(); AActor* V = Victim.Get();
                if (!CireSkillRuntime::Alive(H) || !CireCombat::IsAlive(V) || !H->InRange(V, Reach + 120.f + Body(V))) return;
                CireCombat::ApplyStrike(H, V, Amount, Name);
            });
        Aim = Target->GetActorLocation(); break;
    }
    case EKit::TwinThrow:
    case EKit::Javelin:
    {
        const int32 Count = Kit->D == EKit::TwinThrow ? 2 : 1;
        bool bAny = false;
        for (int32 I = 0; I < Count; ++I)
        {
            FCireSkillshotSpec Shot;
            Shot.Speed = Kit->D == EKit::TwinThrow ? 2400.f : 2600.f; Shot.Radius = FMath::Clamp(Def->Radius > 0 ? Def->Radius : 30.f, 12.f, 80.f);
            Shot.MaxRange = Range; Shot.LifetimeSeconds = Range / Shot.Speed + .3f; Shot.Damage = Amount; Shot.WarningSeconds = .05f; Shot.CastRange = Range;
            Shot.HitLimit = 1; Shot.PlayerCollision = Shot.MonsterCollision = ECireProjectileCollision::Stop;
            Shot.VisualStyle = Kit->D == EKit::TwinThrow ? TEXT("arrow") : TEXT("nature"); Shot.Color = Tint(*Def, false); Shot.Color.A = .9f; Shot.AbilityName = Name;
            const float Yaw = Count > 1 ? (I == 0 ? -3.f : 3.f) : 0.f;
            const FVector Dir = Direction.RotateAngleAxis(Yaw, FVector::UpVector);
            bAny |= ACireSkillshot::Spawn(Hero, Shot, Origin + Dir * Range, Name) != nullptr;
        }
        if (!bAny) return Fail(TEXT("Cannot throw from here."));
        Aim = Origin + Direction * Range; break;
    }
    case EKit::Returning:
    {
        FCireAreaSpec Out = AreaFor(*Def, ECireAreaShape::Line, Kit->Warning, Amount);
        if (!ACireAreaEffect::Spawn(Hero, Out, Feet, Direction.Rotation())) return Fail(TEXT("Cannot throw from here."));
        const FVector End = Feet + Direction * Range;
        CireCombat::PlayCue(Hero, nullptr, FName(*Id), Origin, End, ECireSpellCue::Launch, .8f, false);
        const FCireAbilityDef* D = Def;
        Later(World, Kit->Warning + Range / 2200.f, [Weak, End, Amount, D, Id]()
        {
            ACireHero* H = Weak.Get(); if (!CireSkillRuntime::Alive(H)) return;
            const FVector Home = FeetOf(H), Back = (Home - End).GetSafeNormal2D();
            const float Length = FMath::Max(50.f, static_cast<float>(FVector::Dist2D(Home, End)));
            FCireAreaSpec Ret = AreaFor(*D, ECireAreaShape::Line, 0.f, Amount); Ret.Length = Length;
            ACireAreaEffect::Spawn(H, Ret, End, Back.IsNearlyZero() ? FRotator::ZeroRotator : Back.Rotation());
            CireCombat::PlayCue(H, nullptr, FName(*Id), End + FVector(0, 0, 80), H->GetActorLocation(), ECireSpellCue::Launch, .8f, true);
        });
        Hero->SetActorRotation(Direction.Rotation()); Aim = End; break;
    }
    case EKit::Beam:
    {
        const FVector End = Origin + Direction * Range;
        const float Half = FMath::Max(50.f, Def->Radius);
        for (ACireHero* A : Allies(Hero, Origin + Direction * Range * .5f, Range * .5f + Half))
            if (A == Hero || SegmentDistance(A->GetActorLocation(), Origin, End) <= Half + Body(A)) CireCombat::ApplyHealing(Hero, A, Amount, Name);
        for (AActor* U : Enemies(Hero, Origin + Direction * Range * .5f, Range * .5f + Half))
            if (SegmentDistance(U->GetActorLocation(), Origin, End) <= Half + Body(U)) CireCombat::ApplyDamage(Hero, U, Amount * .4f, Name);
        Field(Hero, *Def, ECireAreaShape::Line, Feet, Direction.Rotation(), 0, Range, Half * 2.f, .6f, 0.f, true);
        Hero->SetActorRotation(Direction.Rotation()); Aim = End; break;
    }
    case EKit::Beacon:
    case EKit::Trail:
    {
        const bool bTrail = Kit->D == EKit::Trail;
        if (!bTrail) { AllyAim(Range); if (!NeedGround(false)) return false; }
        const float Duration = Seconds(World, Def->Duration > 0 ? Def->Duration : 6.f);
        ACireAreaEffect* Area = bTrail ? Field(Hero, *Def, ECireAreaShape::Line, Feet, Direction.Rotation(), 0, Range, Def->Radius * 2.f, Duration, Kit->Warning, false)
                                       : Field(Hero, *Def, ECireAreaShape::Circle, Aim, FRotator::ZeroRotator, Def->Radius, 0, 0, Duration, 0.f, false);
        if (!Area) return Fail(TEXT("Cannot place that here."));
        if (S)
        {
            UCireKitSkillsSubsystem::FZone Z; Z.Owner = Hero; Z.Area = Area; Z.Id = Id; Z.Name = Name; Z.Kind = bTrail ? UCireKitSkillsSubsystem::EZone::Haste : UCireKitSkillsSubsystem::EZone::Beacon;
            Z.Center = bTrail ? Feet : Aim; Z.Direction = Direction; Z.Radius = Def->Radius; Z.Length = bTrail ? Range : 0.f; Z.Width = Def->Radius * 2.f;
            Z.Magnitude = Magnitude; Z.StartsAt = Now(World) + (bTrail ? Kit->Warning : 0.f); Z.EndsAt = Now(World) + Duration; Z.Interval = .5f; S->Zones.Add(Z);
        }
        if (bTrail) { Hero->SetActorRotation(Direction.Rotation()); Aim = Origin + Direction * Range; }
        break;
    }
    case EKit::Transform:
    {
        const float Duration = Seconds(World, Def->Duration > 0 ? Def->Duration : 6.f);
        CireBuffs::Apply(Hero, DragonId, Duration, Hero, 2);
        if (S) { auto& Dr = S->Dragons.FindOrAdd(Hero); Dr.Charges = 2; Dr.EndsAt = Now(World) + Duration; Dr.bPact = false; }
        Aim = Feet; break;
    }
    case EKit::Worldstone:
    {
        const float Radius = Def->Radius > 0 ? Def->Radius : 600.f;
        const float Duration = Seconds(World, Def->Duration > 0 ? Def->Duration : 10.f);
        const FString Profile = Hero->ChampionProfileId;
        const bool bSupport = Profile == TEXT("ether_golem_support") || (!Profile.Contains(TEXT("golem")) && Hero->HasChampionRole(TEXT("healer")));
        const bool bBruiser = Profile == TEXT("ether_golem_bruiser");
        if (bSupport)
        {
            for (ACireHero* A : Allies(Hero, Origin, Radius)) CireCombat::ApplyHealing(Hero, A, Amount, Name);
            if (S)
            {
                ACireAreaEffect* Area = Field(Hero, *Def, ECireAreaShape::Circle, Feet, FRotator::ZeroRotator, Radius, 0, 0, Duration, 0.f, true);
                UCireKitSkillsSubsystem::FZone Z; Z.Owner = Hero; Z.Area = Area; Z.Id = Id; Z.Name = Name; Z.Kind = UCireKitSkillsSubsystem::EZone::Worldstone;
                Z.Center = Feet; Z.Radius = Radius; Z.PerSecond = Amount; Z.StartsAt = Now(World); Z.EndsAt = Z.StartsAt + Duration; Z.Interval = 2.f; Z.Timer = 2.f;
                Z.bFollowOwner = true; S->Zones.Add(Z);
            }
        }
        else
        {
            FCireAreaSpec A = AreaFor(*Def, ECireAreaShape::Circle, Kit->Warning, Amount);
            ACireAreaEffect::Spawn(Hero, A, Feet, FRotator::ZeroRotator);
            if (bBruiser) CireBuffs::Apply(Hero, WorldBruiserId, Duration, Hero, 30);
            else
            {
                CireBuffs::Apply(Hero, WorldTankId, Duration, Hero, 35);
                for (AActor* U : Enemies(Hero, Origin, Radius)) Taunt(Hero, U, ControlSeconds(Hero, 4.f));
            }
        }
        Aim = Feet; break;
    }
    case EKit::MovingAura:
    case EKit::Constellation:
    case EKit::Sunrise:
    {
        const float Radius = Def->Radius > 0 ? Def->Radius : 600.f;
        const float Duration = Seconds(World, Def->Duration > 0 ? Def->Duration : 8.f);
        if (Kit->D == EKit::MovingAura && S)
        {
            ACireAreaEffect* Area = Field(Hero, *Def, ECireAreaShape::Circle, Feet, FRotator::ZeroRotator, Radius, 0, 0, Duration, 0.f, true);
            UCireKitSkillsSubsystem::FZone Z; Z.Owner = Hero; Z.Area = Area; Z.Id = Id; Z.Name = Name; Z.Kind = UCireKitSkillsSubsystem::EZone::Aura;
            Z.Center = Feet; Z.Radius = Radius; Z.PerSecond = Amount; Z.Magnitude = 15.f; Z.StartsAt = Now(World); Z.EndsAt = Z.StartsAt + Duration; Z.Interval = 1.f;
            Z.bFollowOwner = true; S->Zones.Add(Z);
        }
        else if (S)
        {
            const bool bLinks = Kit->D == EKit::Constellation;
            for (ACireHero* A : Allies(Hero, Origin, Radius))
            {
                UCireKitSkillsSubsystem::FHot X; X.Source = Hero; X.Target = A; X.Name = Name; X.PerTick = Amount;
                X.Interval = bLinks ? 2.f : 1.f; X.Ticks = FMath::Max(1, FMath::RoundToInt(Duration / X.Interval)); X.Timer = bLinks ? .05f : 1.f;
                X.MaxDistance = bLinks ? 1200.f : 0.f; X.Buff = bLinks ? LinkId : SunriseId;
                CireBuffs::Apply(A, X.Buff, Duration, Hero);
                if (!bLinks) A->SlowUntil = 0.f; // Sunrise clears slows
                S->Hots.Add(X);
            }
        }
        Aim = Feet; break;
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
    UE_LOG(LogCireKitSkills, Verbose, TEXT("CIRE_KIT_CAST %s by %s"), *Id, *Hero->HeroName);
    return true;
}

// ============================================================================================ shapes
bool CireKitSkills::DescribeShape(const FString& Id, FCireHitShape& R)
{
    const FKit* Kit = FindKit(Id);
    const FCireAbilityDef* Def = CireAbilityDB::Find(Id);
    if (!Kit || !Def) return false;
    const float Range = Def->Range > 0 ? Def->Range : 900.f;
    const float Radius = Def->Radius > 0 ? Def->Radius : 300.f;
    auto Circle = [&](float Rad, bool bSelf) { R.Kind = ECireHitShape::Circle; R.Radius = FMath::Max(40.f, Rad); R.bFromCaster = bSelf; R.bAtTarget = !bSelf; };
    auto Lane = [&](float Length, float Width) { R.Kind = ECireHitShape::Line; R.bFromCaster = true; R.bGroundAim = true; R.Length = Length; R.Width = FMath::Max(24.f, Width); };
    R.LingerSeconds = .6f; R.WarningSeconds = Kit->Warning;
    switch (Kit->D)
    {
    case EKit::Passive: R.Kind = ECireHitShape::None; break;
    case EKit::Strike: case EKit::Frenzy: R.Kind = ECireHitShape::Unit; break;
    case EKit::Cone: R.Kind = ECireHitShape::Cone; R.bFromCaster = true; R.bGroundAim = true; R.Radius = Radius; R.Angle = Kit->Angle; break;
    case EKit::Circle:
        Circle(Radius, false); R.bGroundAim = true; R.LingerSeconds = Id == TEXT("miner_mountain") ? Def->Duration : .4f; break;
    case EKit::Line:
        Lane(Range, Radius * 2.f); R.LingerSeconds = Id == TEXT("dryad_thorn_line") ? Def->Duration : .4f; break;
    case EKit::Returning: Lane(Range, Radius * 2.f); R.LingerSeconds = .8f; break;
    case EKit::Burst: Circle(Radius, true); break;
    case EKit::Worldstone: Circle(Radius, true); R.bHostileOnly = false; R.LingerSeconds = 1.f; break;
    case EKit::Charge: Lane(Range, FMath::Max(40.f, Def->Radius) * 2.f); break;
    case EKit::Leap: Circle(Radius, false); R.bGroundAim = true; break;
    case EKit::Hook: Lane(Range, FMath::Max(40.f, Def->Radius) * 2.f); R.bProjectile = true; R.Speed = 2400.f; break;
    case EKit::TwinThrow:
        // Two axes 6 degrees apart: one corridor wide enough to hold both paths at full range.
        Lane(Range, FMath::Max(12.f, Def->Radius) * 2.f + 2.f * Range * FMath::Tan(FMath::DegreesToRadians(3.f))); R.bProjectile = true; R.Speed = 2400.f; break;
    case EKit::Javelin: Lane(Range, FMath::Max(12.f, Def->Radius) * 2.f); R.bProjectile = true; R.Speed = 2600.f; break;
    case EKit::AllyOrb:
        Lane(Range, FMath::Max(40.f, Def->Radius) * 2.f); R.bHostileOnly = false; R.bHeal = true; R.Speed = Id == TEXT("whisp_guiding_mote") ? 1400.f : 900.f; break;
    case EKit::Beam: Lane(Range, FMath::Max(50.f, Def->Radius) * 2.f); R.bHostileOnly = false; R.bHeal = true; break;
    case EKit::Motes: Lane(Range, FMath::Max(120.f, Def->Radius * 2.f)); R.bHostileOnly = false; R.bHeal = true; R.LingerSeconds = Def->Duration; break;
    case EKit::Trail: Lane(Range, Radius * 2.f); R.bHostileOnly = false; R.LingerSeconds = Def->Duration; break;
    case EKit::HealZone: Circle(Radius, false); R.bGroundAim = true; R.bHostileOnly = false; R.bHeal = true; R.LingerSeconds = Def->Duration; break;
    case EKit::Beacon: Circle(Radius, false); R.bGroundAim = true; R.bHostileOnly = false; R.LingerSeconds = Def->Duration; break;
    case EKit::MovingAura: case EKit::Constellation: case EKit::Sunrise:
        Circle(Radius, true); R.bHostileOnly = false; R.bHeal = true; R.LingerSeconds = FMath::Min(2.f, Def->Duration); break;
    case EKit::Channel: R.Kind = ECireHitShape::Self; R.bHostileOnly = false; R.bHeal = true; R.LingerSeconds = Def->Duration; break;
    case EKit::SelfBuff: case EKit::Transform: R.Kind = ECireHitShape::Self; R.bHostileOnly = false; R.LingerSeconds = 1.f; break;
    case EKit::AllyBarrier: case EKit::Tether: R.Kind = ECireHitShape::Unit; R.bHostileOnly = false; R.LingerSeconds = .8f; break;
    case EKit::DelayedHeal: R.Kind = ECireHitShape::Unit; R.bHostileOnly = false; R.bHeal = true; R.LingerSeconds = FMath::Max(.8f, Def->Duration); break;
    case EKit::Construct:
    {
        const FCireTechRecipe* X = CireTechConstructs::FindRecipe(FName(*Id));
        Circle(X ? X->Radius : Radius, false); R.bGroundAim = true; R.bHostileOnly = false; R.LingerSeconds = FMath::Min(2.f, X ? X->Lifetime : 2.f); break;
    }
    case EKit::Wall:
        // The barricade's true footprint: a Width x 60 cm plank across the aim direction.
        R.Kind = ECireHitShape::Custom; R.bGroundAim = true; R.bHostileOnly = false; R.LingerSeconds = 1.f;
        R.Polygon = {{-30.f, -Radius}, {30.f, -Radius}, {30.f, Radius}, {-30.f, Radius}}; break;
    case EKit::Summon: Circle(60.f, false); R.bGroundAim = true; break;
    default: R.Kind = ECireHitShape::Unit; break;
    }
    return true;
}

// ============================================================================================ hooks
float CireKitSkills::ModifyOutgoingDamage(AActor* Source, AActor* Target, float Amount, const FString& AbilityName)
{
    if (!IsValid(Target) || Amount <= 0 || !Target->HasAuthority()) return Amount;
    UWorld* World = Target->GetWorld();
    auto* S = Sub(World);
    const float T = Now(World);
    float M = 1.f;
    // ---- attacker side
    if (auto* H = Cast<ACireHero>(Source))
    {
        if (const auto* E = ActiveBuff(Target, RelicMarkId); E && E->Source == H) M *= 1.1f;
        if (const auto* E = ActiveBuff(H, CoreId); E && H->HasSkill(TEXT("golem_construct_core"))) M *= 1.f + E->Stacks * PotencyOf(H, TEXT("golem_construct_core"), 2.f) / 100.f;
        if (CireBuffs::IsActive(H, WorldBruiserId)) M *= 1.3f;
        if (const auto* E = ActiveBuff(H, RallyId)) M *= 1.f + E->Stacks / 100.f;
        if (auto* Mon = Cast<ACireMonster>(Target))
        {
            if (CireBuffs::IsActive(H, ColossusId)) CireThreat::AddRaw(Mon, H, Amount * 5.f);        // double threat (tank multiplier 5x)
            if (CireBuffs::IsActive(H, WeightId)) CireThreat::AddRaw(Mon, H, Amount * 2.5f);          // +50% threat while rooted in place
        }
        if (CireBuffs::IsActive(H, RedMoonId) && CireItems::IsBasicAttack(H, AbilityName) && !H->bDead)
            CireCombat::ApplyHealing(H, H, Amount * .15f, TEXT("Red Moon Frenzy"));
    }
    if (const auto* E = ActiveBuff(Source, CowedId)) M *= 1.f - FMath::Clamp(E->Stacks / 100.f, 0.f, .6f); // Deepwood Roar weakens attacks
    // ---- defender side (champions)
    auto* Victim = Cast<ACireHero>(Target);
    if (!Victim || Victim->IsA<ACireSummon>()) return Amount * M;
    float Reduction = 0.f;
    auto Take = [&](float Fraction) { Reduction = 1.f - (1.f - Reduction) * (1.f - FMath::Clamp(Fraction, 0.f, .8f)); };
    Take(BuffFraction(Victim, HibernateId));
    Take(BuffFraction(Victim, HideId));
    Take(BuffFraction(Victim, ColossusId));
    Take(BuffFraction(Victim, MountainId));
    Take(BuffFraction(Victim, WorldTankId));
    Take(BuffFraction(Victim, ResistId));
    Take(BuffFraction(Victim, WeightId));
    Take(BuffFraction(Victim, ScaleId));
    Take(BuffFraction(Victim, PactId));
    Take(BuffFraction(Victim, GroveId));
    if (const auto* E = ActiveBuff(Victim, CoreId); E && Victim->HasSkill(TEXT("golem_construct_core"))) Take(E->Stacks * .01f);
    if (Victim->HasSkill(TEXT("chieftain_courage")))
    {
        const int32 Near = FMath::Min(3, Allies(Victim, Victim->GetActorLocation(), 800.f, false).Num());
        Take(Near * PotencyOf(Victim, TEXT("chieftain_courage"), 5.f) / 100.f);
    }
    // Totem Bulwark cover: an allied barricade within 3m that stands between the champion and the attacker.
    if (IsValid(Source))
        for (TActorIterator<ACireConstruct> It(World); It; ++It)
        {
            if (It->IsActorBeingDestroyed() || It->OriginTeam != Victim->TeamId || !It->IsWall() || It->GetDisplayName() != TEXT("Totem Bulwark")) continue;
            const FVector P = It->GetActorLocation();
            if (FVector::DistSquared2D(P, Victim->GetActorLocation()) > FMath::Square(300.f)) continue;
            if (FVector::DotProduct((Source->GetActorLocation() - Victim->GetActorLocation()).GetSafeNormal2D(), (P - Victim->GetActorLocation()).GetSafeNormal2D()) > .2f) { Take(.2f); break; }
        }
    float Out = Amount * M * (1.f - Reduction);
    // Relic Vow: 30% of the damage is redirected to the paladin who bound this ally.
    if (S && S->RedirectDepth == 0)
        if (const auto* E = ActiveBuff(Victim, RelicVowId))
            if (auto* Paladin = Cast<ACireHero>(E->Source.Get()); Paladin && Paladin != Victim && CireSkillRuntime::Alive(Paladin) && IsValid(Source))
            {
                const float Shared = Out * .3f; Out -= Shared;
                ++S->RedirectDepth; CireCombat::ApplyDamage(Source, Paladin, Shared, TEXT("Relic Vow")); --S->RedirectDepth;
            }
    // Scale Guard: melee attackers are scorched.
    if (S && S->ThornDepth == 0 && CireBuffs::IsActive(Victim, ScaleId) && IsValid(Source) && Source != Victim &&
        FVector::DistSquared2D(Source->GetActorLocation(), Victim->GetActorLocation()) <= FMath::Square(380.f))
    {
        ++S->ThornDepth; CireCombat::ApplyDamage(Victim, Source, 15.f + .3f * Victim->PrimaryAttribute(), TEXT("Scale Guard")); --S->ThornDepth;
    }
    // ---- passive trackers (hits taken)
    if (S)
    {
        S->LastHitAt.Add(Victim, T);
        if (Victim->HasSkill(TEXT("bear_ancient_hide")) && S->HideReadyAt.FindRef(Victim) <= T)
        {
            TArray<float>& Hits = S->HideHits.FindOrAdd(Victim);
            Hits.RemoveAll([T](float At) { return T - At > 4.f; }); Hits.Add(T);
            if (Hits.Num() >= 5)
            {
                CireBuffs::Apply(Victim, HideId, Seconds(World, 4.f), Victim, Percent(FMath::Min(PotencyOf(Victim, TEXT("bear_ancient_hide"), 25.f), 60.f)));
                S->HideReadyAt.Add(Victim, T + 12.f); Hits.Reset();
            }
        }
        if (Victim->HasSkill(TEXT("miner_orehide")))
        {
            int32& N = S->OreHits.FindOrAdd(Victim);
            if (++N >= 4)
            {
                N = 0;
                const float Power = ModeOf(Victim) ? ModeOf(Victim)->Power(Victim->TeamId) : 1.f;
                const float Level = FMath::Max(1.f, CireSkillShop::CastScale(Victim, TEXT("miner_orehide")).Effect);
                CireItems::GrantBarrier(Victim, CireKits::Amount(Victim, TEXT("miner_orehide"), 30.f, 1.f) * Power * Level, Seconds(World, 6.f), Victim);
            }
        }
        if (Victim->HasSkill(TEXT("golem_construct_core")))
        {
            float& Stored = S->CoreDamage.FindOrAdd(Victim); Stored += Out;
            const int32 Charges = FMath::Clamp(FMath::FloorToInt(Stored / 150.f), 0, 10);
            if (Charges > 0) CireBuffs::Apply(Victim, CoreId, 6.f, Victim, Charges);
        }
        // Ironroot Slumber breaks on a heavy hit.
        if (CireBuffs::IsActive(Victim, HibernateId) && Out > Victim->MaxHealth * .1f)
        {
            CireBuffs::Remove(Victim, HibernateId);
            S->Channels.RemoveAll([Victim](const UCireKitSkillsSubsystem::FChannel& C) { return C.Hero.Get() == Victim; });
            Victim->Notice = TEXT("Ironroot Slumber broken!");
        }
        S->MovingSince.Remove(Victim);
    }
    return Out;
}

void CireKitSkills::OnAbilityHit(AActor* Source, AActor* Target, const FString& AbilityName, float Applied)
{
    if (!IsValid(Source) || !Source->HasAuthority() || !IsValid(Target) || Applied <= 0) return;
    const FCireAbilityDef* D = nullptr;
    if (!IsKitName(AbilityName, D)) return;
    auto* H = Cast<ACireHero>(Source);
    if (!H) return;
    for (const FCireAbilityEffect& E : D->Effects)
    {
        if (E.Zone == TEXT("self")) continue;
        if (E.Type == TEXT("taunt")) Taunt(H, Target, ControlSeconds(H, E.Duration));
        else if (E.Type == TEXT("root")) Root(Target, ControlSeconds(H, E.Duration), H);
        else if (E.Type == TEXT("weaken")) CireBuffs::Apply(Target, CowedId, ControlSeconds(H, E.Duration), H, Percent(E.Magnitude * 100.f * CireKits::Potency(H, D->Id)));
        else if (E.Type == TEXT("mark") && D->Id == TEXT("miner_pickfall")) CireBuffs::Apply(Target, VulnerableId, ControlSeconds(H, E.Duration), H);
        else if (E.Type == TEXT("mark")) CireBuffs::Apply(Target, RelicMarkId, ControlSeconds(H, E.Duration), H, Percent(E.Magnitude * 100.f));
    }
    if (TripleThreat.Contains(D->Id)) if (auto* M = Cast<ACireMonster>(Target)) CireThreat::AddRaw(M, H, Applied * 10.f);
    if (const float* Push2 = Knockback.Find(D->Id))
    {
        FVector Away = (Target->GetActorLocation() - H->GetActorLocation()).GetSafeNormal2D();
        if (D->Id == TEXT("behemoth_stampede")) { const FVector F = H->GetActorForwardVector().GetSafeNormal2D(); const FVector Side(-F.Y, F.X, 0); Away = FVector::DotProduct(Away, Side) >= 0 ? Side : -Side; }
        Push(Target, Away, *Push2);
    }
    auto* S = Sub(H->GetWorld());
    auto AddBurn = [&](float Fraction)
    {
        if (!S || Fraction <= 0) return;
        UCireKitSkillsSubsystem::FBurn B; B.Source = H; B.Target = Target; B.Name = D->Name + TEXT(" (burn)"); B.Ticks = 3; B.PerTick = Applied * Fraction / 3.f;
        S->Burns.Add(B); CireBuffs::Apply(Target, BurnId, 3.2f, H);
    };
    if (const float* F = BurnFraction.Find(D->Id)) AddBurn(*F);
    if (EmberSources.Contains(D->Id) && H->HasSkill(TEXT("drakish_ember_memory"))) AddBurn(PotencyOf(H, TEXT("drakish_ember_memory"), 20.f) / 100.f);
    if (D->Id == TEXT("centaur_grove_javelin") && S)
    {
        // A healing bloom grows where the javelin struck: 50% of the damage over 3 s to allies inside.
        const FVector At = FeetOf(Target);
        const float Duration = Seconds(H->GetWorld(), D->Duration > 0 ? D->Duration : 3.f);
        ACireAreaEffect* Area = Field(H, *D, ECireAreaShape::Circle, At, FRotator::ZeroRotator, 250.f, 0, 0, Duration, 0.f, true);
        UCireKitSkillsSubsystem::FZone Z; Z.Owner = H; Z.Area = Area; Z.Id = D->Id; Z.Name = D->Name; Z.Kind = UCireKitSkillsSubsystem::EZone::Bloom;
        Z.Center = At; Z.Radius = 250.f; Z.PerSecond = Applied / FMath::Max(1.f, Duration); // 100% of the javelin hit over the bloom (balance lab) Z.StartsAt = Now(H->GetWorld()); Z.EndsAt = Z.StartsAt + Duration; Z.Interval = 1.f;
        S->Zones.Add(Z);
    }
}

float CireKitSkills::ModifyBasicAttack(ACireHero* Hero, AActor* Target, float Damage, FString& Name)
{
    if (!Hero || !Hero->HasAuthority() || !IsValid(Target)) return Damage;
    auto* S = Sub(Hero->GetWorld());
    if (!S) return Damage;
    // Ether Furnace: four empowered swings, half of each splashing around the target.
    if (int32* Charges = S->FurnaceCharges.Find(Hero); Charges && *Charges > 0 && CireBuffs::IsActive(Hero, FurnaceId))
    {
        const float Bonus = BuffFraction(Hero, FurnaceId);
        Damage *= 1.f + Bonus;
        for (AActor* U : Enemies(Hero, Target->GetActorLocation(), 150.f)) if (U != Target) CireCombat::ApplyDamage(Hero, U, Damage * .5f, TEXT("Ether Furnace"));
        if (--*Charges <= 0) { CireBuffs::Remove(Hero, FurnaceId); S->FurnaceCharges.Remove(Hero); }
    }
    // Dragon Oath: the swing becomes a 120-degree cleave; the last one is followed by the threat fireball.
    if (auto* Dr = S->Dragons.Find(Hero); Dr && Dr->Charges > 0 && CireBuffs::IsActive(Hero, DragonId))
    {
        const FCireAbilityDef* D = CireAbilityDB::Find(TEXT("drakish_dragon_oath"));
        const float Power = ModeOf(Hero) ? ModeOf(Hero)->Power(Hero->TeamId) : 1.f;
        const float Slash = CireKits::Amount(Hero, TEXT("drakish_dragon_oath"), 60.f) * Power;
        const float Reach = D && D->Radius > 0 ? D->Radius : 350.f;
        const FVector Fwd = (Target->GetActorLocation() - Hero->GetActorLocation()).GetSafeNormal2D();
        for (AActor* U : Enemies(Hero, Hero->GetActorLocation(), Reach))
            if (U != Target && FVector::DotProduct((U->GetActorLocation() - Hero->GetActorLocation()).GetSafeNormal2D(), Fwd) >= FMath::Cos(FMath::DegreesToRadians(60.f)))
                CireCombat::ApplyStrike(Hero, U, Slash, D ? D->Name : TEXT("Dragon Oath"));
        CireCombat::PlayCue(Hero, Target, TEXT("drakish_dragon_oath"), Hero->GetActorLocation(), Hero->GetActorLocation() + Fwd * Reach, ECireSpellCue::Impact, 1.f, true);
        Damage = FMath::Max(Damage, Slash); Name = D ? D->Name : TEXT("Dragon Oath");
        if (!Dr->bPact && --Dr->Charges <= 0)
        {
            // Threat fireball at the furthest enemy within 12 m, then back to human form.
            AActor* Far = nullptr; float Best = 0;
            for (AActor* U : Enemies(Hero, Hero->GetActorLocation(), D && D->Range > 0 ? D->Range : 1200.f))
            { const float Dist = static_cast<float>(FVector::DistSquared2D(U->GetActorLocation(), Hero->GetActorLocation())); if (Dist > Best && Sight(Hero, U)) { Best = Dist; Far = U; } }
            if (Far)
            {
                CireCombat::PlayCue(Hero, Far, TEXT("drakish_dragon_oath"), Hero->GetActorLocation(), Far->GetActorLocation(), ECireSpellCue::Launch, 1.f, true);
                CireCombat::ApplyDamage(Hero, Far, Slash * .4f, D ? D->Name : TEXT("Dragon Oath"));
                Taunt(Hero, Far, ControlSeconds(Hero, 2.f));
            }
            CireBuffs::Remove(Hero, DragonId); S->Dragons.Remove(Hero);
        }
    }
    // Ember Memory: every third basic attack ignites the target.
    if (Hero->HasSkill(TEXT("drakish_ember_memory")))
    {
        int32& N = S->BasicCount.FindOrAdd(Hero);
        if (++N >= 3)
        {
            N = 0;
            UCireKitSkillsSubsystem::FBurn B; B.Source = Hero; B.Target = Target; B.Name = TEXT("Ember Memory"); B.Ticks = 3;
            B.PerTick = Damage * PotencyOf(Hero, TEXT("drakish_ember_memory"), 20.f) / 100.f / 3.f; S->Burns.Add(B); CireBuffs::Apply(Target, BurnId, 3.2f, Hero);
        }
    }
    // Red Moon Frenzy: every swing or throw also strikes a second enemy for half damage.
    if (CireBuffs::IsActive(Hero, RedMoonId))
    {
        AActor* Second = nullptr; float Best = FMath::Square(250.f + 200.f);
        for (AActor* U : Enemies(Hero, Target->GetActorLocation(), 250.f))
            if (U != Target) { const float D2 = static_cast<float>(FVector::DistSquared2D(U->GetActorLocation(), Target->GetActorLocation())); if (D2 < Best) { Best = D2; Second = U; } }
        if (Second) CireCombat::ApplyDamage(Hero, Second, Damage * .5f, TEXT("Red Moon Frenzy"));
    }
    return Damage;
}

float CireKitSkills::ModifyHealing(ACireHero* Source, ACireHero*, float Amount, const FString& AbilityName)
{
    if (!Source || !Source->HasAuthority() || Amount <= 0) return Amount;
    if (Source->HasSkill(TEXT("centaur_steady_gait")) && CireBuffs::IsActive(Source, GaitId) && AbilityName != TEXT("Green Covenant"))
    {
        CireBuffs::Remove(Source, GaitId);
        return Amount * (1.f + PotencyOf(Source, TEXT("centaur_steady_gait"), 30.f) / 100.f);
    }
    return Amount;
}

void CireKitSkills::OnHealingDone(ACireHero* Source, ACireHero*, float Applied, const FString& AbilityName)
{
    if (!Source || !Source->HasAuthority() || Applied <= 0 || AbilityName == TEXT("Green Covenant") || !Source->HasSkill(TEXT("dryad_green_covenant"))) return;
    if (auto* S = Sub(Source->GetWorld()))
    {
        const float Cap = CireKits::Amount(Source, TEXT("dryad_green_covenant"), 60.f, 2.f);
        float& R = S->Reserve.FindOrAdd(Source); R = FMath::Min(Cap, R + Applied * .2f);
    }
}

float CireKitSkills::MoveSpeedMultiplier(const ACireHero* Hero)
{
    if (!Hero) return 1.f;
    float M = 1.f;
    M *= 1.f + BuffFraction(Hero, TrailId);
    M *= 1.f + BuffFraction(Hero, BeaconId);
    M *= 1.f + BuffFraction(Hero, MarchId);
    return M;
}
float CireKitSkills::AttackSpeedBonus(const ACireHero* Hero)
{
    if (!Hero) return 0.f;
    float Bonus = 0.f;
    if (CireBuffs::IsActive(Hero, RallyId)) Bonus += .10f;
    Bonus += BuffFraction(Hero, RedMoonId);
    if (Hero->HasSkill(TEXT("troll_hunger")))
    {
        const float Missing = FMath::Clamp(1.f - HealthFraction(Hero), 0.f, 1.f);
        Bonus += FMath::Min(.4f, FMath::FloorToFloat(Missing * 10.f) * PotencyOf(Hero, TEXT("troll_hunger"), 5.f) / 100.f);
    }
    return Bonus;
}
float CireKitSkills::BasicRangeBonus(const ACireHero* Hero) { return Hero && CireBuffs::IsActive(Hero, ColossusId) ? 150.f : 0.f; }
float CireKitSkills::ResourceRegenMultiplier(const ACireHero* Hero)
{
    if (!Hero || !Hero->HasSkill(TEXT("whisp_lantern_soul"))) return 1.f;
    for (ACireHero* A : Allies(Hero, Hero->GetActorLocation(), 600.f, false)) if (HealthFraction(A) < .6f) return 1.f + PotencyOf(Hero, TEXT("whisp_lantern_soul"), 40.f) / 100.f;
    return 1.f;
}
float CireKitSkills::BodyScaleMultiplier(const ACireHero* Hero) { return Hero && CireBuffs::IsActive(Hero, ColossusId) ? 1.3f : 1.f; }

bool CireKitSkills::BotWantsCast(ACireHero* Hero, const FString& Id)
{
    const FKit* Kit = FindKit(Id);
    const FCireAbilityDef* Def = CireAbilityDB::Find(Id);
    if (!Hero || !Kit || !Def) return true;
    const float Range = FMath::Max(600.f, Def->Range > 0 ? Def->Range : 900.f);
    auto Wounded = [&](float Below, float Reach, bool bSelf) { int32 N = 0; for (ACireHero* A : Allies(Hero, Hero->GetActorLocation(), Reach, bSelf)) N += HealthFraction(A) < Below; return N; };
    switch (Kit->D)
    {
    case EKit::Channel: return HealthFraction(Hero) < .6f;
    case EKit::AllyOrb: case EKit::DelayedHeal: case EKit::Tether: case EKit::HealZone: case EKit::AllyBarrier:
        return Wounded(.8f, Range, true) > 0;
    case EKit::Beam: return Wounded(.85f, Range, true) > 0;
    case EKit::Motes: case EKit::Beacon: return Wounded(.75f, Range, true) > 0;
    case EKit::MovingAura: case EKit::Constellation: case EKit::Sunrise:
        return Wounded(.7f, Def->Radius > 0 ? Def->Radius : 700.f, true) >= 2 || Wounded(.45f, Def->Radius > 0 ? Def->Radius : 700.f, true) > 0;
    case EKit::Worldstone:
        if (Hero->ChampionProfileId == TEXT("ether_golem_support")) return Wounded(.7f, 600.f, true) >= 1;
        return IsValid(Hero->Target) && Hero->InRange(Hero->Target, 600.f);
    case EKit::Burst: case EKit::Cone: case EKit::Charge: case EKit::Hook:
        return IsValid(Hero->Target) && Hero->InRange(Hero->Target, FMath::Max(Def->Radius, Def->Range) + 100.f);
    case EKit::Trail: return false; // movement utility: players only
    default: return true;
    }
}

bool CireKitSkills::PylonPulse(ACireConstruct* C, AActor* Unit, bool bAlly, bool, float Hold)
{
    if (!C || !IsValid(Unit)) return false;
    const FName Effect = C->ConstructSpec.Effect;
    const uint8 Stacks = Percent(C->ConstructSpec.EffectMagnitude * 100.f);
    if (Effect == TEXT("rally")) { if (bAlly) CireBuffs::Apply(Unit, RallyId, Hold, C->GetSourceActor(), Stacks); return true; }
    if (Effect == TEXT("resist")) { if (bAlly) CireBuffs::Apply(Unit, ResistId, Hold, C->GetSourceActor(), Stacks); return true; }
    if (Effect == TEXT("barrier"))
    {
        auto* Owner = Cast<ACireHero>(C->GetSourceActor());
        auto* Ally = Cast<ACireHero>(Unit);
        if (bAlly && Owner && Ally && !Ally->IsA<ACireSummon>())
        {
            const float Power = ModeOf(Owner) ? ModeOf(Owner)->Power(Owner->TeamId) : 1.f;
            const float Level = FMath::Max(1.f, CireSkillShop::CastScale(Owner, C->ConstructSpec.Recipe.ToString()).Effect);
            CireItems::GrantBarrier(Ally, CireKits::Amount(Owner, C->ConstructSpec.Recipe.ToString(), 40.f, 1.f) * Power * Level, 3.5f, Owner);
        }
        return true;
    }
    return false;
}

void CireKitSkills::StartHealOverTime(ACireHero* Source, ACireHero* Target, float Total, float Secs, const FString& Name)
{
    if (!Source || !Target || !Source->HasAuthority() || Total <= 0) return;
    auto* S = Sub(Source->GetWorld()); if (!S) return;
    const int32 Ticks = FMath::Clamp(FMath::RoundToInt(Secs), 1, 20);
    UCireKitSkillsSubsystem::FHot X; X.Source = Source; X.Target = Target; X.Name = Name; X.PerTick = Total / Ticks; X.Ticks = Ticks - 1; X.Timer = 1.f; X.Buff = MendId;
    CireBuffs::Apply(Target, MendId, Ticks + .2f, Source);
    CireCombat::ApplyHealing(Source, Target, X.PerTick, Name); // the first pulse lands at once, the rest each second
    if (X.Ticks > 0) S->Hots.Add(X);
}

// ============================================================================================ subsystem
UCireKitSkillsSubsystem* UCireKitSkillsSubsystem::Get(const UWorld* World) { return World ? World->GetSubsystem<UCireKitSkillsSubsystem>() : nullptr; }

void UCireKitSkillsSubsystem::Tick(float Dt)
{
    UWorld* W = GetWorld();
    if (!W || W->GetNetMode() == NM_Client || !FMath::IsFinite(Dt) || Dt <= 0) return;
    const float T = W->GetTimeSeconds();
    auto* Mode = W->GetAuthGameMode<ACireGameMode>();
    const bool bCombat = Mode && Mode->IsCombatPhase();
    // ---- zones
    for (int32 I = Zones.Num() - 1; I >= 0; --I)
    {
        FZone& Z = Zones[I];
        ACireHero* H = Z.Owner.Get();
        const bool bAreaGone = Z.Area.IsValid() == false && Z.Kind != EZone::Mountain;
        if (!bCombat || !CireSkillRuntime::Alive(H) || T >= Z.EndsAt || (bAreaGone && Z.Kind != EZone::Aura && Z.Kind != EZone::Worldstone && Z.Kind != EZone::Bloom)) { Zones.RemoveAtSwap(I); continue; }
        if (T < Z.StartsAt) continue;
        if (Z.bFollowOwner) { Z.Center = FeetOf(H); if (ACireAreaEffect* A = Z.Area.Get()) A->SetActorLocation(Z.Center); }
        Z.Timer -= Dt; if (Z.Timer > 0) continue; Z.Timer += Z.Interval;
        const float Hold = Z.Interval + .35f;
        auto Inside = [&](const AActor* U)
        {
            if (Z.Length > 0) return SegmentDistance(U->GetActorLocation(), Z.Center, Z.Center + Z.Direction * Z.Length) <= Z.Width * .5f + Body(U);
            return FVector::DistSquared2D(U->GetActorLocation(), Z.Center) <= FMath::Square(Z.Radius + Body(U));
        };
        const float Reach = Z.Length > 0 ? Z.Length + Z.Width : Z.Radius;
        const FVector Mid = Z.Length > 0 ? Z.Center + Z.Direction * Z.Length * .5f : Z.Center;
        for (ACireHero* A : Allies(H, Mid, Reach))
        {
            if (!Inside(A)) continue;
            switch (Z.Kind)
            {
            case EZone::Heal: case EZone::Bloom: case EZone::Worldstone:
                CireCombat::ApplyHealing(H, A, Z.PerSecond * Z.Interval, Z.Name);
                if (Z.Magnitude > 0) CireBuffs::Apply(A, GroveId, Hold, H, Percent(Z.Magnitude * 100.f));
                break;
            case EZone::Aura:
                CireCombat::ApplyHealing(H, A, Z.PerSecond * Z.Interval, Z.Name);
                CireBuffs::Apply(A, MarchId, Hold, H, Percent(Z.Magnitude));
                break;
            case EZone::Mountain: CireBuffs::Apply(A, MountainId, Hold, H, Percent(Z.Magnitude * 100.f)); break;
            case EZone::Haste: CireBuffs::Apply(A, TrailId, Hold, H, Percent(Z.Magnitude)); break;
            case EZone::Beacon:
                CireBuffs::Apply(A, BeaconId, Hold, H, Percent(Z.Magnitude));
                CireCombat::ApplyHealing(H, A, A->MaxHealth * .015f * Z.Interval, Z.Name); // 1.5% max health per second (balance lab)
                break;
            default: break;
            }
        }
        if (Z.Kind == EZone::Mountain) for (AActor* U : Enemies(H, Z.Center, Z.Radius)) CireCrowdControl::Slow(U, Hold, H);
    }
    // ---- heals over time (tethers, links, sunrise, living granite, tumbling mends)
    for (int32 I = Hots.Num() - 1; I >= 0; --I)
    {
        FHot& X = Hots[I];
        ACireHero* H = X.Source.Get(); ACireHero* A = X.Target.Get();
        if (!bCombat || !CireSkillRuntime::Alive(H) || !CireSkillRuntime::Alive(A) || X.Ticks <= 0) { if (A && !X.Buff.IsNone()) CireBuffs::Remove(A, X.Buff); Hots.RemoveAtSwap(I); continue; }
        if (X.MaxDistance > 0 && FVector::DistSquared2D(H->GetActorLocation(), A->GetActorLocation()) > FMath::Square(X.MaxDistance))
        { if (!X.Buff.IsNone()) CireBuffs::Remove(A, X.Buff); if (X.Buff == TetherId) H->Notice = TEXT("Spirit Tether snapped."); Hots.RemoveAtSwap(I); continue; }
        if (X.Buff == GraniteId)
        {
            const auto* Inv = CireItems::InventoryOf(A);
            if (!Inv || Inv->BarrierHP <= 0 || Inv->BarrierEndsAt <= T) { CireBuffs::Remove(A, GraniteId); Hots.RemoveAtSwap(I); continue; }
        }
        X.Timer -= Dt; if (X.Timer > 0) continue; X.Timer += X.Interval; --X.Ticks;
        CireCombat::ApplyHealing(H, A, X.PerTick, X.Name);
        if (X.Buff == TetherId || X.Buff == LinkId)
            CireCombat::PlayCue(H, A, X.Buff == TetherId ? FName(TEXT("whisp_spirit_tether")) : FName(TEXT("whisp_constellation")), H->GetActorLocation(), A->GetActorLocation(), ECireSpellCue::Launch, .6f, false);
    }
    // ---- burns
    for (int32 I = Burns.Num() - 1; I >= 0; --I)
    {
        FBurn& B = Burns[I];
        if (!B.Source.IsValid() || !CireCombat::IsAlive(B.Target.Get()) || B.Ticks <= 0) { Burns.RemoveAtSwap(I); continue; }
        B.Timer -= Dt; if (B.Timer > 0) continue; B.Timer += 1.f; --B.Ticks;
        CireCombat::ApplyDamage(B.Source.Get(), B.Target.Get(), B.PerTick, B.Name);
    }
    // ---- motes
    for (int32 I = Motes.Num() - 1; I >= 0; --I)
    {
        FMote& M = Motes[I];
        ACireHero* H = M.Owner.Get();
        if (!bCombat || !CireSkillRuntime::Alive(H) || T >= M.EndsAt) { Motes.RemoveAtSwap(I); continue; }
        for (ACireHero* A : Allies(H, M.At, 120.f))
            if (A->Health < A->MaxHealth)
            {
                CireCombat::ApplyHealing(H, A, M.Amount, M.Name);
                CireCombat::PlayCue(H, A, TEXT("whisp_fey_trail"), M.At, A->GetActorLocation(), ECireSpellCue::Impact, .6f, true);
                Motes.RemoveAtSwap(I); break;
            }
    }
    // ---- channels (Ironroot Slumber)
    for (int32 I = Channels.Num() - 1; I >= 0; --I)
    {
        FChannel& C = Channels[I];
        ACireHero* H = C.Hero.Get();
        if (!bCombat || !CireSkillRuntime::Alive(H) || T >= C.EndsAt || !CireBuffs::IsActive(H, HibernateId)) { if (H) CireBuffs::Remove(H, HibernateId); Channels.RemoveAtSwap(I); continue; }
        if (!H->bBot && FVector::DistSquared2D(H->GetActorLocation(), C.Start) > FMath::Square(60.f))
        { CireBuffs::Remove(H, HibernateId); H->Notice = TEXT("Ironroot Slumber ended: you moved."); Channels.RemoveAtSwap(I); continue; }
        C.Timer -= Dt; if (C.Timer > 0) continue; C.Timer += .5f;
        CireCombat::ApplyHealing(H, H, C.PerTick, C.Name);
    }
    // ---- dragon form expiry: back to human form when the window closes
    for (auto It = Dragons.CreateIterator(); It; ++It)
    {
        ACireHero* H = It.Key().Get();
        if (!CireSkillRuntime::Alive(H)) { It.RemoveCurrent(); continue; }
        if (T < It.Value().EndsAt && CireBuffs::IsActive(H, DragonId)) continue;
        CireBuffs::Remove(H, DragonId); It.RemoveCurrent();
    }
    // ---- passive trackers (4x a second)
    PassiveTimer -= Dt;
    if (PassiveTimer > 0 || !Mode) return;
    PassiveTimer = .25f;
    for (ACireHero* H : Mode->Heroes)
    {
        if (!CireSkillRuntime::Alive(H)) continue;
        const FVector P = H->GetActorLocation();
        const FVector* Last = LastPosition.Find(H);
        const bool bMoving = Last && FVector::DistSquared2D(*Last, P) > FMath::Square(8.f);
        LastPosition.Add(H, P);
        // Ancestral Weight: still for 1.5 s.
        if (H->HasSkill(TEXT("behemoth_ancestral_weight")))
        {
            if (bMoving) { StillSince.Add(H, T); CireBuffs::Remove(H, WeightId); }
            else if (T - StillSince.FindOrAdd(H, T) >= 1.5f) CireBuffs::Apply(H, WeightId, .6f, H, Percent(FMath::Min(PotencyOf(H, TEXT("behemoth_ancestral_weight"), 15.f), 50.f)));
        }
        // Steady Gait: 2 s of moving without being hit readies the next heal.
        if (H->HasSkill(TEXT("centaur_steady_gait")) && !CireBuffs::IsActive(H, GaitId))
        {
            if (!bMoving || T - LastHitAt.FindRef(H) < 2.f) MovingSince.Add(H, T);
            else if (T - MovingSince.FindOrAdd(H, T) >= 2.f) CireBuffs::Apply(H, GaitId, 0.f, H, Percent(PotencyOf(H, TEXT("centaur_steady_gait"), 30.f)));
        }
        // Construct Core charges fade 6 s after the last hit.
        if (H->HasSkill(TEXT("golem_construct_core")) && T - LastHitAt.FindRef(H) > 6.f) CoreDamage.Remove(H);
        // Green Covenant: the banked reserve heals the most wounded ally every 2 s.
        if (float* R = Reserve.Find(H); R && *R > 1.f)
        {
            float& Timer = ReserveTimer.FindOrAdd(H); Timer -= .25f;
            if (Timer <= 0)
            {
                Timer = 2.f;
                ACireHero* Low = nullptr; float Lowest = .999f;
                for (ACireHero* A : Allies(H, P, 1200.f)) if (HealthFraction(A) < Lowest) { Lowest = HealthFraction(A); Low = A; }
                if (Low) { const float Heal = FMath::Min(*R, Low->MaxHealth - Low->Health); *R -= CireCombat::ApplyHealing(H, Low, Heal, TEXT("Green Covenant")); *R = FMath::Max(0.f, *R); }
            }
        }
        // Last Light: an emergency barrier for an ally dropping below 30%.
        if (H->HasSkill(TEXT("keeper_last_light")))
            for (ACireHero* A : Allies(H, P, 1200.f))
            {
                const FString Key = FString::Printf(TEXT("%s/%s"), *H->GetName(), *A->GetName());
                if (HealthFraction(A) >= .3f || LastLightReadyAt.FindRef(Key) > T) continue;
                const float Power = Mode->Power(H->TeamId);
                const float Level = FMath::Max(1.f, CireSkillShop::CastScale(H, TEXT("keeper_last_light")).Effect);
                CireItems::GrantBarrier(A, CireKits::Amount(H, TEXT("keeper_last_light"), 80.f, 2.f) * Power * Level, 6.f, H);
                CireCombat::PlayCue(H, A, TEXT("keeper_last_light"), P, A->GetActorLocation(), ECireSpellCue::Impact, 1.f, true);
                LastLightReadyAt.Add(Key, T + 20.f);
            }
    }
}
