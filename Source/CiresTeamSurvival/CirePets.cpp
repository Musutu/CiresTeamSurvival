// pets: persistent companions (Docs/Pets.md).
#include "CirePets.h"
#include "CireActorIterator.h" // town-perf: fast actor iteration in editor-binary -game
#include "CireScalingKits.h" // scaling-kits
#include "CireAbilityDB.h"
#include "CireAreaEffects.h"
#include "CireCombatEvents.h"
#include "CireConstruct.h"
#include "CireCrowdControl.h"
#include "CireGame.h"
#include "CireNav.h"
#include "CireRealm.h"
#include "CireSkillRuntime.h"
#include "CireSkillshot.h"
#include "CireThreat.h"
#include "CireItems.h"
#include "CireClassTraits.h"
#include "CireSignatureSkills.h"
#include "Components/CapsuleComponent.h"
#include "Dom/JsonValue.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Net/UnrealNetwork.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

DEFINE_LOG_CATEGORY_STATIC(LogCirePets, Log, All);

#define LOCTEXT_NAMESPACE "CirePets"

namespace
{
struct FPetData
{
    FCirePetRules Rules;
    TArray<FCirePetDef> Pets;
    TMap<FString, FName> Owners;
    bool bLoaded = false;
};
FPetData& Data() { static FPetData D; return D; }

float Num(const TSharedPtr<FJsonObject>& O, const TCHAR* Key, float Default)
{
    double V = Default;
    return O.IsValid() && O->TryGetNumberField(Key, V) && FMath::IsFinite(V) ? static_cast<float>(V) : Default;
}
FString Str(const TSharedPtr<FJsonObject>& O, const TCHAR* Key)
{
    FString V; if (O.IsValid()) O->TryGetStringField(Key, V); return V;
}
TSharedPtr<FJsonObject> ReadJson(const FString& RelativePath)
{
    FString Text; TSharedPtr<FJsonObject> Root;
    const FString Path = FPaths::Combine(FPaths::ProjectDir(), RelativePath);
    if (!FFileHelper::LoadFileToString(Text, *Path)) return nullptr;
    const auto Reader = TJsonReaderFactory<>::Create(Text);
    return FJsonSerializer::Deserialize(Reader, Root) ? Root : nullptr;
}

/** PetArt.tripo.json (written by the 3D collector once the sabercat has clips): a ready row overrides the body. */
TSharedPtr<FJsonObject> ClipOverride(const FString& File, FName PetId)
{
    if (File.IsEmpty()) return nullptr;
    const TSharedPtr<FJsonObject> Root = ReadJson(File);
    if (!Root) return nullptr;
    TArray<TSharedPtr<FJsonObject>> Rows;
    const TArray<TSharedPtr<FJsonValue>>* List = nullptr;
    if (Root->TryGetArrayField(TEXT("pets"), List)) for (const auto& V : *List) if (V->Type == EJson::Object) Rows.Add(V->AsObject());
    const TSharedPtr<FJsonObject>* Direct = nullptr;
    if (Root->TryGetObjectField(PetId.ToString(), Direct)) Rows.Add(*Direct);
    for (const auto& Row : Rows)
    {
        FString Id = Str(Row, TEXT("petId")); if (Id.IsEmpty()) Id = Str(Row, TEXT("id"));
        if (!Id.IsEmpty() && Id != PetId.ToString()) continue;
        const FString Status = Str(Row, TEXT("status"));
        const TSharedPtr<FJsonObject>* Anims = nullptr;
        FString Idle;
        if ((!Status.IsEmpty() && Status != TEXT("ready")) || !Row->TryGetObjectField(TEXT("animations"), Anims) || !(*Anims)->TryGetStringField(TEXT("idle"), Idle)) continue;
        return Row;
    }
    return nullptr;
}

bool Parse(FPetData& Out, FString& Error)
{
    const TSharedPtr<FJsonObject> Root = ReadJson(TEXT("Content/Data/Pets.json"));
    if (!Root) { Error = TEXT("Content/Data/Pets.json missing or invalid"); return false; }
    const TSharedPtr<FJsonObject>* R = nullptr;
    if (Root->TryGetObjectField(TEXT("rules"), R))
    {
        auto& X = Out.Rules;
        X.FollowDistance = Num(*R, TEXT("followDistanceCm"), X.FollowDistance); X.Leash = Num(*R, TEXT("leashCm"), X.Leash);
        X.Teleport = Num(*R, TEXT("teleportCm"), X.Teleport); X.AssistRange = Num(*R, TEXT("assistRangeCm"), X.AssistRange);
        X.DefendRadius = Num(*R, TEXT("defendRadiusCm"), X.DefendRadius); X.AggressiveRadius = Num(*R, TEXT("aggressiveRadiusCm"), X.AggressiveRadius);
        X.ReviveRange = Num(*R, TEXT("reviveRangeCm"), X.ReviveRange); X.ReviveHealthFraction = FMath::Clamp(Num(*R, TEXT("reviveHealthFraction"), X.ReviveHealthFraction), .05f, 1.f);
        X.ReviveCooldown = Num(*R, TEXT("reviveCooldownSeconds"), X.ReviveCooldown); X.ResummonCooldown = Num(*R, TEXT("resummonCooldownSeconds"), X.ResummonCooldown);
        X.OrderedAbilitySeconds = Num(*R, TEXT("orderedAbilitySeconds"), X.OrderedAbilitySeconds);
        X.OwnerThreatShare = FMath::Clamp(Num(*R, TEXT("ownerThreatShare"), X.OwnerThreatShare), 0.f, .9f);
    }
    const TSharedPtr<FJsonObject>* Owners = nullptr;
    if (Root->TryGetObjectField(TEXT("owners"), Owners))
        for (const auto& Pair : (*Owners)->Values) { FString Profile(Pair.Key); FString Pet = Pair.Value.IsValid() ? Pair.Value->AsString() : FString(); Out.Owners.Add(Profile, FName(*Pet)); }
    const TArray<TSharedPtr<FJsonValue>>* Pets = nullptr;
    if (!Root->TryGetArrayField(TEXT("pets"), Pets) || Pets->IsEmpty()) { Error = TEXT("Pets.json has no pets"); return false; }
    for (const auto& V : *Pets)
    {
        const TSharedPtr<FJsonObject> O = V->AsObject();
        FCirePetDef D;
        D.Id = FName(*Str(O, TEXT("id"))); D.DisplayName = Str(O, TEXT("displayName")); D.Family = Str(O, TEXT("family")); D.Description = Str(O, TEXT("description"));
        if (D.Id.IsNone() || D.DisplayName.IsEmpty()) { Error = TEXT("pet without id or displayName"); return false; }
        const TSharedPtr<FJsonObject>* S = nullptr;
        if (O->TryGetObjectField(TEXT("stats"), S))
        {
            D.Health = Num(*S, TEXT("health"), D.Health); D.HealthPerLevel = Num(*S, TEXT("healthPerLevel"), D.HealthPerLevel);
            D.OwnerHealthShare = Num(*S, TEXT("ownerHealthShare"), D.OwnerHealthShare); D.Damage = Num(*S, TEXT("damage"), D.Damage);
            D.DamagePerLevel = Num(*S, TEXT("damagePerLevel"), D.DamagePerLevel); D.OwnerPrimaryScale = Num(*S, TEXT("ownerPrimaryScale"), D.OwnerPrimaryScale);
            D.AttackSeconds = Num(*S, TEXT("attackSeconds"), D.AttackSeconds); D.AttackRange = Num(*S, TEXT("attackRangeCm"), D.AttackRange);
            D.MoveSpeed = Num(*S, TEXT("moveSpeed"), D.MoveSpeed); D.ThreatMultiplier = Num(*S, TEXT("threatMultiplier"), D.ThreatMultiplier);
            D.DamageTakenMultiplier = Num(*S, TEXT("damageTakenMultiplier"), D.DamageTakenMultiplier);
        }
        const bool bSane = D.Health >= 1 && D.Health <= 100000 && D.Damage >= 0 && D.Damage <= 10000 && D.AttackSeconds >= .3f && D.AttackSeconds <= 10 &&
            D.AttackRange >= 80 && D.AttackRange <= 2000 && D.MoveSpeed >= 100 && D.MoveSpeed <= 1500 && D.ThreatMultiplier >= 0 && D.ThreatMultiplier <= 20 &&
            D.DamageTakenMultiplier > 0 && D.DamageTakenMultiplier <= 4;
        if (!bSane) { Error = D.Id.ToString() + TEXT(": stats out of range"); return false; }
        const TArray<TSharedPtr<FJsonValue>>* Abilities = nullptr;
        if (O->TryGetArrayField(TEXT("abilities"), Abilities))
            for (const auto& AV : *Abilities)
            {
                const TSharedPtr<FJsonObject> A = AV->AsObject();
                FCirePetAbility Ab; Ab.Id = Str(A, TEXT("id")); Ab.Kind = Str(A, TEXT("kind")); Ab.Scaling = Num(A, TEXT("scaling"), 0);
                A->TryGetBoolField(TEXT("autocast"), Ab.bAutocast); A->TryGetBoolField(TEXT("special"), Ab.bSpecial);
                Ab.MinRange = Num(A, TEXT("minRangeCm"), 0); Ab.ThreatBonus = Num(A, TEXT("threatBonus"), 1); Ab.TauntSeconds = Num(A, TEXT("tauntSeconds"), 0);
                if (Ab.Id.IsEmpty() || (Ab.Kind != TEXT("leap") && Ab.Kind != TEXT("strike") && Ab.Kind != TEXT("roar"))) { Error = D.Id.ToString() + TEXT(": ability needs an id and kind leap|strike|roar"); return false; }
                D.Abilities.Add(Ab);
            }
        const TSharedPtr<FJsonObject>* Art = nullptr;
        if (O->TryGetObjectField(TEXT("art"), Art))
        {
            D.Portrait = Str(*Art, TEXT("portrait"));
            D.CapsuleRadius = FMath::Clamp(Num(*Art, TEXT("capsuleRadius"), D.CapsuleRadius), 20.f, 72.f);
            D.CapsuleHalfHeight = FMath::Clamp(Num(*Art, TEXT("capsuleHalfHeight"), D.CapsuleHalfHeight), D.CapsuleRadius, 120.f);
            // Tripo clips (PetArt.tripo.json) override the procedural body; the procedural body becomes the fallback.
            if (const TSharedPtr<FJsonObject> Clips = ClipOverride(Str(*Art, TEXT("clipsFile")), D.Id))
            {
                const TSharedPtr<FJsonObject> Bound = MakeShared<FJsonObject>(*Clips);
                Bound->SetStringField(TEXT("motion"), TEXT("monster_native"));
                if (!Bound->HasField(TEXT("mesh"))) Bound->SetStringField(TEXT("mesh"), Str(*Art, TEXT("mesh")));
                if (!Bound->HasField(TEXT("heightCm"))) Bound->SetNumberField(TEXT("heightCm"), Num(*Art, TEXT("heightCm"), 118));
                if (!Bound->HasField(TEXT("yaw"))) Bound->SetNumberField(TEXT("yaw"), Num(*Art, TEXT("yaw"), -90));
                Bound->SetBoolField(TEXT("lockRoot"), true);
                const TSharedPtr<FJsonObject> Procedural = MakeShared<FJsonObject>(**Art);
                Bound->SetObjectField(TEXT("fallback"), Procedural);
                D.Art = Bound;
            }
            else D.Art = *Art;
        }
        Out.Pets.Add(MoveTemp(D));
    }
    for (const auto& Pair : Out.Owners)
        if (!Out.Pets.ContainsByPredicate([&](const FCirePetDef& P) { return P.Id == Pair.Value; })) { Error = Pair.Key + TEXT(" owns an unknown pet"); return false; }
    return true;
}
const FPetData& Loaded()
{
    FPetData& D = Data();
    if (!D.bLoaded) { FString Error; FPetData Fresh; if (!Parse(Fresh, Error)) { UE_LOG(LogCirePets, Error, TEXT("CIRE_PETS_DATA_INVALID %s"), *Error); } else { D = MoveTemp(Fresh); } D.bLoaded = true; }
    return D;
}

float Dist2D(const AActor* A, const AActor* B) { return A && B ? static_cast<float>(FVector::Dist2D(A->GetActorLocation(), B->GetActorLocation())) : TNumericLimits<float>::Max(); }
bool Sight(const AActor* From, const AActor* To)
{
    if (!IsValid(From) || !IsValid(To)) return false;
    FHitResult Hit; FCollisionQueryParams Q(SCENE_QUERY_STAT(CirePetSight), false, From);
    const bool bBlocked = From->GetWorld()->LineTraceSingleByChannel(Hit, From->GetActorLocation() + FVector(0, 0, 30), To->GetActorLocation() + FVector(0, 0, 30), ECC_Visibility, Q);
    return !bBlocked || Hit.GetActor() == To;
}
TArray<AActor*> HostilesNear(ACireHero* Source, FVector Center, float Radius)
{
    TArray<AActor*> Out;
    auto Consider = [&](AActor* U)
    {
        if (!U || U == Source || !CireCombat::AreHostile(Source, U) || !CireCombat::IsAlive(U)) return;
        const auto* C = ::Cast<ACharacter>(U);
        const float Body = C ? C->GetCapsuleComponent()->GetScaledCapsuleRadius() : 30.f;
        if (FVector::DistSquared2D(Center, U->GetActorLocation()) <= FMath::Square(Radius + Body) && FMath::Abs(U->GetActorLocation().Z - Center.Z) < 400.f) Out.Add(U);
    };
    if (auto* Mode = Source->GetWorld()->GetAuthGameMode<ACireGameMode>()) for (auto* M : Mode->Monsters) if (IsValid(M)) Consider(M);
    for (TCireActorIterator<ACireHero> It(Source->GetWorld()); It; ++It) Consider(*It);
    return Out;
}
FVector FollowSpot(const ACireHero* Owner, float Distance)
{
    const FVector Forward = Owner->GetActorForwardVector().GetSafeNormal2D();
    const FVector Right = FVector::CrossProduct(FVector::UpVector, Forward);
    return Owner->GetActorLocation() - Forward * Distance + Right * Distance * .45f;
}
}

// ============================================================================================ data
int32 FCirePetDef::SpecialIndex() const { return Abilities.IndexOfByPredicate([](const FCirePetAbility& A) { return A.bSpecial; }); }
int32 FCirePetDef::AbilityIndex(const FString& AbilityId) const { return Abilities.IndexOfByPredicate([&](const FCirePetAbility& A) { return A.Id == AbilityId; }); }
const FCirePetRules& CirePets::Rules() { return Loaded().Rules; }
const TArray<FCirePetDef>& CirePets::All() { return Loaded().Pets; }
const FCirePetDef* CirePets::Find(FName PetId) { return Loaded().Pets.FindByPredicate([&](const FCirePetDef& P) { return P.Id == PetId; }); }
const FCirePetDef* CirePets::ForOwner(const ACireHero* Owner)
{
    if (!Owner || Owner->IsA<ACireSummon>()) return nullptr;
    if (!Owner->PetGrant.IsNone()) return Find(Owner->PetGrant); // a pet talent / skill on any champion
    if (Owner->ChampionProfileId.IsEmpty()) return nullptr;
    const FName* Pet = Loaded().Owners.Find(Owner->ChampionProfileId);
    return Pet ? Find(*Pet) : nullptr;
}
bool CirePets::Reload(FString* Error)
{
    FString Why; FPetData Fresh;
    if (!Parse(Fresh, Why)) { if (Error) *Error = Why; return false; }
    Fresh.bLoaded = true; Data() = MoveTemp(Fresh); return true;
}
bool CirePets::IsPetSkill(const FString& SkillId)
{
    for (const auto& P : All()) if (P.AbilityIndex(SkillId) != INDEX_NONE) return true;
    return false;
}
FString CirePets::StanceName(ECirePetStance S)
{
    return S == ECirePetStance::Aggressive ? TEXT("Aggressive") : S == ECirePetStance::Passive ? TEXT("Passive") : TEXT("Defensive");
}
FName CirePets::CommandAction(ECirePetCommand C)
{
    static const FName Ids[] = {TEXT("PetAttack"), TEXT("PetFollow"), TEXT("PetStay"), TEXT("PetSpecial"), TEXT("PetRevive"),
        TEXT("PetStanceAggressive"), TEXT("PetStanceDefensive"), TEXT("PetStancePassive")};
    return static_cast<uint8>(C) < UE_ARRAY_COUNT(Ids) ? Ids[static_cast<uint8>(C)] : NAME_None;
}
FText CirePets::CommandName(ECirePetCommand C)
{
    switch (C)
    {
    case ECirePetCommand::Attack: return LOCTEXT("PetAttack", "Pet: attack my target");
    case ECirePetCommand::Follow: return LOCTEXT("PetFollow", "Pet: follow");
    case ECirePetCommand::Stay: return LOCTEXT("PetStay", "Pet: stay");
    case ECirePetCommand::Special: return LOCTEXT("PetSpecial", "Pet: special ability");
    case ECirePetCommand::Revive: return LOCTEXT("PetRevive", "Pet: revive / call");
    case ECirePetCommand::StanceAggressive: return LOCTEXT("PetAggressive", "Pet stance: aggressive");
    case ECirePetCommand::StanceDefensive: return LOCTEXT("PetDefensive", "Pet stance: defensive");
    case ECirePetCommand::StancePassive: return LOCTEXT("PetPassive", "Pet stance: passive");
    default: return FText::GetEmpty();
    }
}

// ============================================================================================ actor
ACirePet::ACirePet()
{
    bBot = false; bAutoAttack = false; bCommandable = false;
    // The owner's view of its pet must stay current; a pet is as relevant as a hero.
    SetNetUpdateFrequency(30.f);
}
const FCirePetDef* ACirePet::Def() const { return CirePets::Find(PetId); }
float ACirePet::ServerNow() const
{
    const UWorld* World = GetWorld();
    if (!World) return 0.f;
    if (HasAuthority()) return World->GetTimeSeconds();
    const auto* State = World->GetGameState<ACireGameState>();
    return State ? static_cast<float>(State->GetServerWorldTimeSeconds()) : World->GetTimeSeconds();
}
float ACirePet::PetThreatMultiplier() const { const auto* D = Def(); return D ? D->ThreatMultiplier : 1.f; }

void ACirePet::Rescale(bool bFill)
{
    const FCirePetDef* D = Def();
    if (!HasAuthority() || !D || !IsValid(OwnerHero)) return;
    const int32 L = FMath::Clamp(OwnerHero->Level, 1, 100);
    const auto* Mode = GetWorld()->GetAuthGameMode<ACireGameMode>();
    const float Power = Mode ? FMath::Clamp(Mode->Power(TeamId), .1f, 10.f) : 1.f;
    const float NewMax = FMath::Clamp(D->Health + D->HealthPerLevel * (L - 1) + D->OwnerHealthShare * OwnerHero->MaxHealth, 1.f, 100000.f);
    const float Fraction = bFill || MaxHealth <= 0 ? 1.f : FMath::Clamp(Health / MaxHealth, 0.f, 1.f);
    Level = L; MaxHealth = NewMax;
    if (!bDead) Health = FMath::Clamp(NewMax * Fraction, bFill ? NewMax : 0.f, NewMax);
    BasicDamage = FMath::Clamp((D->Damage + D->DamagePerLevel * (L - 1) + D->OwnerPrimaryScale * CirePets::OwnerPrimaryScale(OwnerHero)) * Power, 0.f, 10000.f);
    SummonSpec.Health = NewMax; SummonSpec.Damage = BasicDamage; SummonSpec.AttackRange = D->AttackRange; SummonSpec.MoveSpeed = D->MoveSpeed;
    SummonSpec.LeashRange = CirePets::Rules().Leash;
}

float ACirePet::AbilityAmount(const FCirePetAbility& A) const
{
    const FCireAbilityDef* D = CireAbilityDB::Find(A.Id);
    if (!D || !IsValid(OwnerHero)) return 0.f;
    const auto* Mode = GetWorld()->GetAuthGameMode<ACireGameMode>();
    const float Power = Mode ? Mode->Power(TeamId) : 1.f;
    return FMath::Min(10000.f, (D->Base.Effect + A.Scaling * CirePets::OwnerPrimaryScale(OwnerHero)) * Power);
}

void ACirePet::SetStance(ECirePetStance NewStance)
{
    if (!HasAuthority() || static_cast<uint8>(NewStance) > static_cast<uint8>(ECirePetStance::Passive)) return;
    Stance = NewStance;
    if (Stance == ECirePetStance::Passive && Order != ECirePetOrder::Attack) { Target = nullptr; PendingAttackTarget.Reset(); }
    ForceNetUpdate();
}
bool ACirePet::Order_Attack(AActor* NewTarget, FString& Why)
{
    const FCirePetDef* D = Def();
    if (!HasAuthority() || bDead || !D) { Why = TEXT("Your companion cannot fight now."); return false; }
    if (!CireCombat::AreHostile(this, NewTarget) || !CireCombat::IsAlive(NewTarget)) { Why = TEXT("Select a hostile target for your companion."); return false; }
    if (!CireRealm::CanObserve(OwnerHero, NewTarget) || Dist2D(OwnerHero, NewTarget) > CirePets::Rules().Leash) { Why = TEXT("That target is too far from you."); return false; }
    Order = ECirePetOrder::Attack; Target = NewTarget; PendingAttackTarget.Reset(); ForceNetUpdate(); return true;
}
void ACirePet::Order_Follow()
{
    if (!HasAuthority() || bDead) return;
    Order = ECirePetOrder::Follow; Target = nullptr; PendingAttackTarget.Reset(); PendingAbility = INDEX_NONE; ForceNetUpdate();
}
void ACirePet::Order_Stay()
{
    if (!HasAuthority() || bDead) return;
    Order = ECirePetOrder::Stay; StayPoint = GetActorLocation(); Target = nullptr; PendingAttackTarget.Reset(); PendingAbility = INDEX_NONE;
    GetCharacterMovement()->StopMovementImmediately(); ForceNetUpdate();
}

bool ACirePet::QueueAbility(int32 Index, AActor* AbilityTarget, float Amount, bool bIgnoreCooldown, FString& Why)
{
    const FCirePetDef* D = Def();
    if (!HasAuthority() || bDead || !D || !D->Abilities.IsValidIndex(Index)) { Why = TEXT("Your companion cannot do that now."); return false; }
    const FCirePetAbility& A = D->Abilities[Index];
    const FCireAbilityDef* Row = CireAbilityDB::Find(A.Id);
    const FString Name = Row ? Row->Name : A.Id;
    if (!Row) { Why = Name + TEXT(" is unknown."); return false; }
    if (CireCrowdControl::IsStunned(this)) { Why = D->DisplayName + TEXT(" is stunned."); return false; }
    if (!bIgnoreCooldown && AbilityReadyAt.IsValidIndex(Index) && AbilityReadyAt[Index] > ServerNow())
    { Why = FString::Printf(TEXT("%s is not ready (%.0fs)."), *Name, AbilityReadyAt[Index] - ServerNow()); return false; }
    if (A.Kind == TEXT("roar"))
    {
        if (HostilesNear(this, GetActorLocation(), Row->Radius > 0 ? Row->Radius : 450.f).IsEmpty()) { Why = FString::Printf(TEXT("No enemies near %s."), *D->DisplayName); return false; }
        return Use(Index, nullptr, Amount);
    }
    if (!CireCombat::AreHostile(this, AbilityTarget) || !CireCombat::IsAlive(AbilityTarget)) { Why = FString::Printf(TEXT("%s needs a hostile target."), *Name); return false; }
    if (!CireRealm::CanObserve(OwnerHero, AbilityTarget) || Dist2D(OwnerHero, AbilityTarget) > CirePets::Rules().Leash) { Why = TEXT("That target is too far from you."); return false; }
    PendingAbility = Index; PendingTarget = AbilityTarget; PendingAmount = Amount; PendingUntil = ServerNow() + CirePets::Rules().OrderedAbilitySeconds;
    // The pet commits to the target as if ordered to attack it.
    Order = ECirePetOrder::Attack; Target = AbilityTarget; ForceNetUpdate();
    return true;
}

bool ACirePet::Use(int32 Index, AActor* AbilityTarget, float Amount)
{
    const FCirePetDef* D = Def();
    if (!D || !D->Abilities.IsValidIndex(Index)) return false;
    const FCirePetAbility& A = D->Abilities[Index];
    const FCireAbilityDef* Row = CireAbilityDB::Find(A.Id);
    if (!Row) return false;
    const float Now = ServerNow();
    if (A.Kind == TEXT("leap"))
    {
        if (!IsValid(AbilityTarget)) return false;
        const FVector From = GetActorLocation();
        const FVector Toward = (AbilityTarget->GetActorLocation() - From).GetSafeNormal2D();
        const float Reach = FMath::Min(static_cast<float>(FVector::Dist2D(From, AbilityTarget->GetActorLocation())) - 70.f, Row->Range > 0 ? Row->Range : 700.f);
        const FVector Land = From + Toward * FMath::Max(0.f, Reach);
        const float Flight = .45f;
        const float Gravity = FMath::Abs(GetCharacterMovement()->GetGravityZ());
        FVector Velocity = (Land - From) / Flight; Velocity.Z = .5f * Gravity * Flight;
        SetActorRotation(Toward.Rotation());
        LaunchCharacter(Velocity, true, true);
        bLeaping = true; LeapLandAt = Now + Flight + .35f; LeapAmount = Amount; LeapAbility = Index; LeapTarget = AbilityTarget;
        CireCombat::PlayCue(this, AbilityTarget, FName(*A.Id), From, Land, ECireSpellCue::Cast, .9f, true);
    }
    else if (A.Kind == TEXT("strike"))
    {
        if (!IsValid(AbilityTarget)) return false;
        SetActorRotation((AbilityTarget->GetActorLocation() - GetActorLocation()).GetSafeNormal2D().Rotation());
        const float Applied = CireCombat::ApplyStrike(this, AbilityTarget, Amount, Row->Name);
        if (auto* M = ::Cast<ACireMonster>(AbilityTarget); M && Applied > 0 && A.ThreatBonus > 1.f)
            CireThreat::AddRaw(M, this, Applied * (A.ThreatBonus - 1.f) * PetThreatMultiplier());
        CireCombat::PlayCue(this, AbilityTarget, FName(*A.Id), GetActorLocation(), AbilityTarget->GetActorLocation(), ECireSpellCue::Impact, .9f, true);
    }
    else // roar
    {
        const float Radius = Row->Radius > 0 ? Row->Radius : 450.f;
        const float Seconds = FMath::Max(.5f, Row->Duration);
        for (AActor* U : HostilesNear(this, GetActorLocation(), Radius))
        {
            if (Amount > 0) CireCombat::ApplyDamage(this, U, Amount, Row->Name);
            CireCrowdControl::Slow(U, Seconds, this);
            if (auto* M = ::Cast<ACireMonster>(U); M && A.TauntSeconds > 0) CireThreat::Taunt(M, this, A.TauntSeconds);
        }
        CireCombat::PlayCue(this, nullptr, FName(*A.Id), GetActorLocation(), GetActorLocation(), ECireSpellCue::Cast, 1.f, true);
    }
    if (AbilityReadyAt.Num() != D->Abilities.Num()) AbilityReadyAt.SetNumZeroed(D->Abilities.Num());
    AbilityReadyAt[Index] = Now + CirePets::OwnerCooldown(OwnerHero, FMath::Max(1.f, Row->Base.Cooldown)); // the owner's CDR applies
    LastAbility = Index; ++AbilitySerial;
    if (++AttackSerial == 0) ++AttackSerial; // the body's attack pose
    AttackStartedServerTime = Now;
    UE_LOG(LogCirePets, Verbose, TEXT("CIRE_PET_ABILITY %s %s amount=%.0f"), *HeroName, *A.Id, Amount);
    ForceNetUpdate();
    return true;
}

void ACirePet::Land()
{
    bLeaping = false;
    const FCirePetDef* D = Def();
    if (!D || !D->Abilities.IsValidIndex(LeapAbility)) return;
    const FCireAbilityDef* Row = CireAbilityDB::Find(D->Abilities[LeapAbility].Id);
    if (!Row) return;
    for (AActor* U : HostilesNear(this, GetActorLocation(), Row->Radius > 0 ? Row->Radius : 260.f))
        CireCombat::ApplyDamage(this, U, LeapAmount, Row->Name);
    CireCombat::PlayCue(this, nullptr, FName(*D->Abilities[LeapAbility].Id), GetActorLocation(), GetActorLocation(), ECireSpellCue::Impact, 1.f, true);
    if (CireCombat::AreHostile(this, LeapTarget.Get())) Target = LeapTarget.Get();
    LeapAbility = INDEX_NONE; LeapTarget.Reset();
}

AActor* ACirePet::ChooseTarget()
{
    const FCirePetRules& R = CirePets::Rules();
    auto Valid = [&](AActor* A)
    {
        return CireCombat::AreHostile(this, A) && CireCombat::IsAlive(A) && CireRealm::CanObserve(OwnerHero, A) &&
            Dist2D(OwnerHero, A) <= R.Leash;
    };
    if (Order == ECirePetOrder::Attack)
    {
        if (Valid(Target)) return Target;
        Order = ECirePetOrder::Follow; Target = nullptr; PendingAttackTarget.Reset();
    }
    if (Stance == ECirePetStance::Passive) return nullptr;
    const FCirePetDef* D = Def();
    const float Reach = (D ? D->AttackRange : 190.f) + 60.f;
    const bool bStay = Order == ECirePetOrder::Stay;
    auto InReach = [&](AActor* A) { return !bStay || Dist2D(this, A) <= Reach; };
    // Keep fighting the current target while it is valid.
    if (Valid(Target) && InReach(Target)) return Target;
    // Assist: whatever the owner is fighting.
    AActor* OwnerTarget = OwnerHero->Target;
    const auto* OwnerMonster = ::Cast<ACireMonster>(OwnerTarget);
    const bool bOwnerFighting = OwnerHero->bAutoAttack || (OwnerMonster && (OwnerMonster->Victim == OwnerHero || OwnerMonster->Threat.Contains(OwnerHero))) ||
        (::Cast<ACireHero>(OwnerTarget) && OwnerHero->AttackStartedServerTime > ServerNow() - 4.f);
    if (bOwnerFighting && Valid(OwnerTarget) && Dist2D(OwnerHero, OwnerTarget) <= R.AssistRange && InReach(OwnerTarget)) return OwnerTarget;
    // Defend: the nearest unit attacking the owner or the pet.
    AActor* Best = nullptr; float BestDistance = TNumericLimits<float>::Max();
    auto Consider = [&](AActor* A, float Radius, const AActor* From)
    {
        if (!Valid(A) || !InReach(A)) return;
        const float Distance = Dist2D(From, A);
        if (Distance <= Radius && Distance < BestDistance) { Best = A; BestDistance = Distance; }
    };
    auto* Mode = GetWorld()->GetAuthGameMode<ACireGameMode>();
    if (Mode) for (auto* M : Mode->Monsters) if (IsValid(M) && (M->Victim == OwnerHero || M->Victim == this)) Consider(M, R.DefendRadius, OwnerHero);
    for (TCireActorIterator<ACireHero> It(GetWorld()); It; ++It)
        if (It->Target == OwnerHero || It->Target == this) Consider(*It, R.DefendRadius, OwnerHero);
    if (Best) return Best;
    // Aggressive: anything hostile near the pet (never opens on a neutral challenge pack).
    if (Stance == ECirePetStance::Aggressive)
    {
        if (Mode) for (auto* M : Mode->Monsters) if (IsValid(M) && !M->bNeutral) Consider(M, R.AggressiveRadius, this);
        for (TCireActorIterator<ACireHero> It(GetWorld()); It; ++It) Consider(*It, R.AggressiveRadius, this);
    }
    return Best;
}

void ACirePet::Tick(float Delta)
{
    // The companion leaves with its owner: owner gone, dead, re-teamed, or no longer entitled to this pet.
    if (HasAuthority() && (!IsValid(OwnerHero) || OwnerHero->IsActorBeingDestroyed() || OwnerHero->TeamId != TeamId || !OwnerHero->bDrafted ||
        OwnerHero->bDead || !CirePets::ForOwner(OwnerHero) || CirePets::ForOwner(OwnerHero)->Id != PetId))
    { Destroy(); return; }
    // Hero presentation, realm visibility, cooldowns and attack release; never the timed-summon lifetime
    // of ACireSummon::Tick and never hero revival (a corpse keeps RespawnTimer out of reach).
    bBot = false; bAutoAttack = false;
    ACireHero::Tick(Delta);
    const FCirePetDef* D = Def();
    const bool bSlowed = SlowUntil > ServerNow();
    GetCharacterMovement()->MaxWalkSpeed = (D ? D->MoveSpeed : 600.f) * (bSlowed ? .65f : 1.f) * (CireCrowdControl::IsStunned(this) ? 0.f : 1.f);
    if (!HasAuthority() || !D) return;
    if (bDead) { GetCharacterMovement()->StopMovementImmediately(); return; }
    auto* Mode = GetWorld()->GetAuthGameMode<ACireGameMode>();
    if (!Mode) return;
    RescaleTimer -= Delta;
    if (RescaleTimer <= 0) { RescaleTimer = 1.f; Rescale(false); }
    const float Now = ServerNow();
    const FCirePetRules& R = CirePets::Rules();
    if (bLeaping)
    {
        if (Now >= LeapLandAt || (GetCharacterMovement()->IsMovingOnGround() && Now >= LeapLandAt - .3f)) Land();
        return;
    }
    // Too far or outside the owner's realm (teleports, recall, arena): come back to the owner's side.
    if (Dist2D(this, OwnerHero) > R.Teleport || !CireSkillRuntime::InRealmBounds(Mode, TeamId, GetActorLocation(), 200.f) != !CireSkillRuntime::InRealmBounds(Mode, TeamId, OwnerHero->GetActorLocation(), 200.f))
    {
        FVector Spot = FollowSpot(OwnerHero, R.FollowDistance); FVector OnNav;
        if (CireNav::Project(GetWorld(), Spot, OnNav, FVector(120, 120, 400), D->CapsuleRadius)) Spot = OnNav + FVector(0, 0, D->CapsuleHalfHeight);
        else Spot.Z = OwnerHero->GetActorLocation().Z;
        SetActorLocation(Spot, false, nullptr, ETeleportType::TeleportPhysics);
        CireNav::Forget(this); Target = nullptr; Order = ECirePetOrder::Follow; PendingAbility = INDEX_NONE; PendingAttackTarget.Reset();
        return;
    }
    if (CireCrowdControl::IsStunned(this)) return;
    // An ordered ability (owner skill or special command) waits for the pet to close in.
    if (PendingAbility != INDEX_NONE && (Now > PendingUntil || !CireCombat::AreHostile(this, PendingTarget.Get()) || !CireCombat::IsAlive(PendingTarget.Get())))
        PendingAbility = INDEX_NONE;
    AActor* Fight = ChooseTarget();
    Target = Fight;
    if (PendingAbility != INDEX_NONE && D->Abilities.IsValidIndex(PendingAbility))
    {
        AActor* T = PendingTarget.Get();
        const FCirePetAbility& A = D->Abilities[PendingAbility];
        const FCireAbilityDef* Row = CireAbilityDB::Find(A.Id);
        const float Reach = A.Kind == TEXT("leap") ? (Row && Row->Range > 0 ? Row->Range : 700.f) : D->AttackRange + 40.f;
        if (Dist2D(this, T) <= Reach && Sight(this, T))
        {
            GetCharacterMovement()->StopMovementImmediately();
            const int32 Index = PendingAbility; PendingAbility = INDEX_NONE;
            Use(Index, T, PendingAmount);
            return;
        }
        AddMovementInput(CireNav::Steer(this, T->GetActorLocation()));
        return;
    }
    // Autocast: the pet uses its own abilities on what it fights (not in Passive).
    if (IsValid(Fight) && Stance != ECirePetStance::Passive && Mode->IsCombatPhase())
        for (int32 I = 0; I < D->Abilities.Num(); ++I)
        {
            const FCirePetAbility& A = D->Abilities[I];
            if (!A.bAutocast || (AbilityReadyAt.IsValidIndex(I) && AbilityReadyAt[I] > Now)) continue;
            const FCireAbilityDef* Row = CireAbilityDB::Find(A.Id);
            if (!Row) continue;
            const float Distance = Dist2D(this, Fight);
            const bool bReady = A.Kind == TEXT("leap") ? Distance >= A.MinRange && Distance <= (Row->Range > 0 ? Row->Range : 700.f) && Sight(this, Fight) && Order != ECirePetOrder::Stay :
                A.Kind == TEXT("strike") ? Distance <= D->AttackRange + 40.f : false;
            // Autocast uses 60% of the commanded strength: the owner's skill is the strong version.
            if (bReady && Use(I, Fight, AbilityAmount(A) * .6f)) return;
        }
    FVector Goal; float Stop;
    if (IsValid(Fight)) { Goal = Fight->GetActorLocation(); Stop = D->AttackRange * .85f; }
    else if (Order == ECirePetOrder::Stay) { Goal = StayPoint; Stop = 60.f; }
    else { Goal = FollowSpot(OwnerHero, R.FollowDistance); Stop = 90.f; }
    if (IsValid(Fight) && Order == ECirePetOrder::Stay && Dist2D(this, Fight) > D->AttackRange + 60.f) { Goal = StayPoint; Stop = 60.f; }
    if (FVector::DistSquared2D(Goal, GetActorLocation()) > FMath::Square(Stop))
    {
        if (auto* Wall = ACireConstruct::FindBlockingConstruct(this, Goal); Wall && Wall->CanBeDamagedBy(this) && IsValid(Fight)) Goal = Wall->GetActorLocation();
        AddMovementInput(CireNav::Steer(this, Goal));
    }
    else
    {
        GetCharacterMovement()->StopMovementImmediately();
        if (IsValid(Fight))
        {
            SetActorRotation((Fight->GetActorLocation() - GetActorLocation()).GetSafeNormal2D().Rotation());
            const uint32 Previous = AttackSerial;
            BasicAttack();
            if (AttackSerial != Previous)
            {
                // The owner's attack speed drives the pet's swing timer (the pet has no attributes of its own).
                PendingAttackDamage = BasicDamage;
                BasicTimer = D->AttackSeconds / CirePets::OwnerAttackSpeed(OwnerHero);
                AttackDuration = FMath::Min(.65f, BasicTimer); AttackReleaseTimer = AttackDuration * (.25f / .65f);
            }
        }
        else if (Order == ECirePetOrder::Follow)
        {
            // Idle beside the owner, facing the same way.
            const FRotator Want(0, OwnerHero->GetActorRotation().Yaw, 0);
            SetActorRotation(FMath::RInterpTo(GetActorRotation(), Want, Delta, 4.f));
        }
    }
}

float ACirePet::TakeDamage(float Amount, const FDamageEvent& Event, AController*, AActor* Causer)
{
    if (!HasAuthority() || bDead || !FMath::IsFinite(Amount) || Amount <= 0 || !CireCombat::AreHostile(Causer, this)) return 0;
    if (const FCirePetDef* D = Def()) Amount *= D->DamageTakenMultiplier;
    if (ShieldUntil > GetWorld()->GetTimeSeconds()) Amount *= .6f;
    const float Applied = FMath::Min(Health, Amount); Health -= Applied;
    CireCombat::BroadcastDamage(Causer, this, Applied, Event); ForceNetUpdate();
    if (Health <= 0) Die(Causer);
    return Applied;
}
void ACirePet::Die(AActor*)
{
    const FCirePetDef* D = Def();
    bDead = true; Health = 0; DiedAt = ServerNow(); Target = nullptr; Order = ECirePetOrder::Follow;
    PendingAttackTarget.Reset(); PendingAbility = INDEX_NONE; bLeaping = false; bAutoAttack = false;
    RespawnTimer = 1.e9f; // ACireHero::Tick must never revive a pet at the team base
    GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    GetCharacterMovement()->StopMovementImmediately();
    CireThreat::Remove(this);
    if (IsValid(OwnerHero))
    {
        OwnerHero->PetResummonAt = DiedAt + CirePets::Rules().ResummonCooldown;
        OwnerHero->Notice = FString::Printf(TEXT("%s has fallen. Revive it, or it returns in %.0fs."), D ? *D->DisplayName : TEXT("Your companion"), CirePets::Rules().ResummonCooldown);
        OwnerHero->ForceNetUpdate();
    }
    UE_LOG(LogCirePets, Display, TEXT("CIRE_PET_DIED %s owner=%s"), *HeroName, IsValid(OwnerHero) ? *OwnerHero->HeroName : TEXT("-"));
    ForceNetUpdate();
}
bool ACirePet::Revive(float Fraction)
{
    if (!HasAuthority() || !bDead) return false;
    bDead = false; RespawnTimer = 0; DiedAt = 0;
    Rescale(false);
    Health = FMath::Clamp(MaxHealth * Fraction, 1.f, MaxHealth);
    GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
    GetCharacterMovement()->SetMovementMode(MOVE_Walking);
    if (IsValid(OwnerHero)) { OwnerHero->PetResummonAt = 0; OwnerHero->ForceNetUpdate(); }
    ForceNetUpdate();
    return true;
}
void ACirePet::FellOutOfWorld(const UDamageType&) { if (HasAuthority()) Destroy(); }
void ACirePet::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(ACirePet, PetId); DOREPLIFETIME(ACirePet, Stance); DOREPLIFETIME(ACirePet, Order); DOREPLIFETIME(ACirePet, StayPoint);
    DOREPLIFETIME(ACirePet, AbilityReadyAt); DOREPLIFETIME(ACirePet, DiedAt); DOREPLIFETIME(ACirePet, AbilitySerial); DOREPLIFETIME(ACirePet, LastAbility);
}

// ============================================================================================ owner API
ACirePet* CirePets::PetOf(const ACireHero* Owner)
{
    if (!IsValid(Owner) || !Owner->GetWorld()) return nullptr;
    for (TCireActorIterator<ACirePet> It(Owner->GetWorld()); It; ++It)
        if (It->OwnerHero == Owner && !It->IsActorBeingDestroyed()) return *It;
    return nullptr;
}

ACirePet* CirePets::Summon(ACireHero* Owner)
{
    const FCirePetDef* D = ForOwner(Owner);
    if (!D || !IsValid(Owner) || !Owner->HasAuthority() || !Owner->bDrafted || Owner->bDead) return nullptr;
    UWorld* World = Owner->GetWorld();
    if (ACirePet* Old = PetOf(Owner)) Old->Destroy();
    FVector Spot = FollowSpot(Owner, Rules().FollowDistance);
    FVector OnNav;
    if (CireNav::Project(World, Spot, OnNav, FVector(150, 150, 400), D->CapsuleRadius)) Spot = OnNav;
    else
    {
        FHitResult Floor; FCollisionQueryParams Q(SCENE_QUERY_STAT(CirePetSummon), false, Owner);
        Spot.Z = Owner->GetActorLocation().Z;
        if (World->LineTraceSingleByObjectType(Floor, Spot + FVector(0, 0, 250), Spot - FVector(0, 0, 600), FCollisionObjectQueryParams(ECC_WorldStatic), Q)) Spot = Floor.ImpactPoint;
        else Spot = Owner->GetActorLocation() - FVector(0, 0, Owner->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
    }
    Spot.Z += D->CapsuleHalfHeight + 4.f;
    const FTransform Transform(FRotator(0, Owner->GetActorRotation().Yaw, 0), Spot);
    auto* Pet = World->SpawnActorDeferred<ACirePet>(ACirePet::StaticClass(), Transform, Owner, Owner, ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn);
    if (!Pet) return nullptr;
    Pet->OwnerHero = Owner; Pet->TeamId = Owner->TeamId; Pet->PetId = D->Id; Pet->bCommandable = false;
    Pet->OriginPhase = CireSkillRuntime::Phase(World); Pet->ExpiresServerTime = 0;
    Pet->SummonSpec.Count = 1; Pet->SummonSpec.DurationSeconds = 300; Pet->SummonSpec.ArchetypeVisual = 1;
    Pet->GetCapsuleComponent()->SetCapsuleSize(D->CapsuleRadius, D->CapsuleHalfHeight);
    Pet->Stance = static_cast<ECirePetStance>(FMath::Min<uint8>(Owner->PetStance, static_cast<uint8>(ECirePetStance::Passive)));
    Pet->Order = ECirePetOrder::Follow;
    Pet->AbilityReadyAt.SetNumZeroed(D->Abilities.Num());
    Pet->Draft(1);
    // A companion is not a champion: its own name, body key, attack and no skills or economy.
    Pet->ChampionProfileId = FString(TEXT("pet:")) + D->Id.ToString();
    Pet->HeroName = D->DisplayName;
    Pet->ProfileAttackSeconds = D->AttackSeconds; Pet->ProfileBasicAttackRange = D->AttackRange;
    Pet->ProfileAttackStyle = TEXT("claw"); Pet->ProfileThreatRole = TEXT("pet"); Pet->ProfileRoles = {TEXT("pet")};
    Pet->Strength = Pet->Agility = Pet->Intelligence = 0; Pet->Gold = 0; Pet->Mana = Pet->MaxMana = 0; Pet->Energy = 0;
    Pet->Skills.Reset(); Pet->Offers.Reset(); Pet->Cooldowns.Reset();
    Pet->FinishSpawning(Transform);
    Pet->Rescale(true);
    Owner->PetResummonAt = 0; Owner->ForceNetUpdate();
    UE_LOG(LogCirePets, Display, TEXT("CIRE_PET_SUMMONED %s owner=%s team=%d hp=%.0f dmg=%.0f"), *Pet->HeroName, *Owner->HeroName, Owner->TeamId, Pet->MaxHealth, Pet->CurrentDamage());
    return Pet;
}

void CirePets::TickOwner(ACireHero* Owner, float)
{
    if (!IsValid(Owner) || !Owner->HasAuthority() || Owner->IsA<ACireSummon>() || !Owner->bDrafted || Owner->bDead) return;
    const FCirePetDef* D = ForOwner(Owner);
    if (!D) return;
    auto* Mode = Owner->GetWorld()->GetAuthGameMode<ACireGameMode>();
    if (!Mode || Mode->Clock.Phase() == Cires::MatchPhase::Finished) return;
    const ACirePet* Pet = PetOf(Owner);
    if (Pet && !Pet->bDead) return;
    if (Owner->GetWorld()->GetTimeSeconds() >= Owner->PetResummonAt) Summon(Owner);
}

bool CirePets::Command(ACireHero* Owner, ECirePetCommand C, AActor* Target)
{
    if (!IsValid(Owner) || !Owner->HasAuthority() || static_cast<uint8>(C) >= static_cast<uint8>(ECirePetCommand::Count)) return false;
    const FCirePetDef* D = ForOwner(Owner);
    if (!D) { Owner->Notice = TEXT("You have no companion."); return false; }
    if (!Owner->bDrafted || Owner->bDead) return false;
    const float Now = Owner->GetWorld()->GetTimeSeconds();
    ACirePet* Pet = PetOf(Owner);
    FString Why;
    const bool bAlive = Pet && !Pet->bDead;
    auto Fail = [&](const FString& Message) { Owner->Notice = Message; Owner->ForceNetUpdate(); return false; };
    auto Done = [&](const FString& Message) { Owner->Notice = Message; Owner->ForceNetUpdate(); return true; };
    switch (C)
    {
    case ECirePetCommand::StanceAggressive: case ECirePetCommand::StanceDefensive: case ECirePetCommand::StancePassive:
    {
        const ECirePetStance S = C == ECirePetCommand::StanceAggressive ? ECirePetStance::Aggressive : C == ECirePetCommand::StancePassive ? ECirePetStance::Passive : ECirePetStance::Defensive;
        Owner->PetStance = static_cast<uint8>(S);
        if (Pet) Pet->SetStance(S);
        return Done(D->DisplayName + TEXT(": ") + StanceName(S) + TEXT("."));
    }
    case ECirePetCommand::Revive:
    {
        if (bAlive)
        {
            // Call: a living pet that strayed comes straight back.
            if (Dist2D(Pet, Owner) > Rules().FollowDistance * 3.f) { Pet->Order_Follow(); Pet->SetActorLocation(Owner->GetActorLocation() + (FollowSpot(Owner, Rules().FollowDistance) - Owner->GetActorLocation()), false, nullptr, ETeleportType::TeleportPhysics); CireNav::Forget(Pet); }
            else Pet->Order_Follow();
            return Done(D->DisplayName + TEXT(" returns to your side."));
        }
        if (Pet && Pet->bDead)
        {
            if (Now < Owner->PetReviveReadyAt) return Fail(FString::Printf(TEXT("Revive is not ready (%.0fs)."), Owner->PetReviveReadyAt - Now));
            if (Dist2D(Pet, Owner) > Rules().ReviveRange) return Fail(FString::Printf(TEXT("Move closer to %s to revive it."), *D->DisplayName));
            Pet->Revive(Rules().ReviveHealthFraction);
            Owner->PetReviveReadyAt = Now + Rules().ReviveCooldown;
            return Done(D->DisplayName + TEXT(" is revived."));
        }
        if (Now < Owner->PetResummonAt) return Fail(FString::Printf(TEXT("%s returns in %.0fs."), *D->DisplayName, Owner->PetResummonAt - Now));
        return Summon(Owner) ? Done(D->DisplayName + TEXT(" answers your call.")) : Fail(TEXT("Your companion cannot come here."));
    }
    default: break;
    }
    if (!bAlive) return Fail(D->DisplayName + TEXT(" is not at your side."));
    switch (C)
    {
    case ECirePetCommand::Attack:
        if (!Pet->Order_Attack(Target, Why)) return Fail(Why);
        return Done(D->DisplayName + TEXT(": attack."));
    case ECirePetCommand::Follow: Pet->Order_Follow(); return Done(D->DisplayName + TEXT(": follow."));
    case ECirePetCommand::Stay: Pet->Order_Stay(); return Done(D->DisplayName + TEXT(": stay."));
    case ECirePetCommand::Special:
    {
        const int32 Index = D->SpecialIndex();
        if (Index == INDEX_NONE) return Fail(D->DisplayName + TEXT(" has no special ability."));
        const FCirePetAbility& A = D->Abilities[Index];
        const FCireAbilityDef* Row = CireAbilityDB::Find(A.Id);
        const auto* Mode = Owner->GetWorld()->GetAuthGameMode<ACireGameMode>();
        const float Strength = Row ? (Row->Base.Effect + A.Scaling * OwnerPrimaryScale(Owner)) * (Mode ? Mode->Power(Owner->TeamId) : 1.f) : 0.f;
        if (!Pet->QueueAbility(Index, Target, FMath::Min(10000.f, Strength), false, Why)) return Fail(Why);
        return Done(Row ? Row->Name : A.Id);
    }
    default: return false;
    }
}

bool CirePets::OwnerCast(ACireHero* Owner, const FString& SkillId, AActor* Target, float Amount, FString& Why)
{
    const FCirePetDef* D = ForOwner(Owner);
    const int32 Index = D ? D->AbilityIndex(SkillId) : INDEX_NONE;
    if (Index == INDEX_NONE) { Why = TEXT("You have no companion to command."); return false; }
    ACirePet* Pet = PetOf(Owner);
    if (!Pet || Pet->bDead) { Why = D->DisplayName + TEXT(" is not at your side."); return false; }
    // The owner's skill is the commanded version: it ignores the pet's own cooldown.
    return Pet->QueueAbility(Index, Target, Amount, true, Why);
}

float CirePets::ShareOwnerThreat(ACireMonster* M, ACireHero* Owner, float Threat)
{
    if (!IsValid(M) || !IsValid(Owner) || Owner->IsA<ACireSummon>() || Threat <= 0 || !FMath::IsFinite(Threat)) return 0.f;
    ACirePet* Pet = PetOf(Owner);
    if (!Pet || Pet->bDead || Pet->Stance == ECirePetStance::Passive || !M->Threat.Contains(Pet)) return 0.f;
    const float Moved = Threat * Rules().OwnerThreatShare;
    if (Moved <= 0) return 0.f;
    const float Before = M->Threat.FindRef(Pet);
    CireThreat::AddRaw(M, Pet, Moved);
    return M->Threat.FindRef(Pet) > Before ? Moved : 0.f;
}

#undef LOCTEXT_NAMESPACE

// scaling-kits: one shared owner-inheritance implementation for pets, summons and constructs (CireScalingKits).
float CirePets::OwnerPrimaryScale(const ACireHero* Owner) { return IsValid(Owner) ? static_cast<float>(FMath::Max(0, CireKits::PrimaryOf(Owner))) : 0.f; }
float CirePets::OwnerAttackSpeed(const ACireHero* Owner) { return IsValid(Owner) ? CireKits::AttackSpeedMultiplier(Owner) : 1.f; }
float CirePets::OwnerCooldown(const ACireHero* Owner, float BaseSeconds) { return CireKits::OwnerCooldown(Owner, BaseSeconds); }
