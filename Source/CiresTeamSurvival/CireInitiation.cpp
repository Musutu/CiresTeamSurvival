// initiation: Set-up team synergy and the Blink Dagger (see CireInitiation.h).
#include "CireInitiation.h"
#include "CireAbilityDB.h"
#include "CireBuffs.h"
#include "CireCombatEvents.h"
#include "CireConstruct.h"
#include "CireGame.h"
#include "CireItems.h"
#include "CireScalingKits.h"
#include "CireSkillRuntime.h"
#include "CireSummon.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"

DEFINE_LOG_CATEGORY_STATIC(LogCireInitiation, Log, All);

const FName CireInitiation::SetUpId(TEXT("xp_setup"));
const FName CireInitiation::BlinkLockedId(TEXT("xp_blink_locked"));

namespace CireInitDetail
{
constexpr float SetUpSeconds = 3.5f, SetUpTeamBonus = .15f, SetUpAreaBonus = .25f;
TMap<TWeakObjectPtr<AActor>, float>& LastCallout() { static TMap<TWeakObjectPtr<AActor>, float> M; return M; }
TMap<TWeakObjectPtr<ACireHero>, FVector>& PendingAim() { static TMap<TWeakObjectPtr<ACireHero>, FVector> M; return M; }
const Cires::Items::Effect* BlinkUse(const ACireHero* H)
{
    if (!H || !H->Inventory) return nullptr;
    for (const FCireItemSlot& Slot : H->Inventory->Equipment)
        if (const auto* Item = CireItems::Find(Slot.Id); Item && Item->Use.Kind == Cires::Items::EffectKind::Blink) return &Item->Use;
    return nullptr;
}
bool GroundBelow(ACireHero* H, FVector& P)
{
    FCollisionQueryParams Q(SCENE_QUERY_STAT(CireBlinkGround), false, H);
    FHitResult Hit;
    if (!H->GetWorld()->LineTraceSingleByObjectType(Hit, P + FVector(0, 0, 400), P - FVector(0, 0, 800), FCollisionObjectQueryParams(ECC_WorldStatic), Q) || Hit.ImpactNormal.Z < .7f) return false;
    P = Hit.ImpactPoint; return true;
}
}
using namespace CireInitDetail;

void CireInitiation::ApplySetUp(ACireHero* Source, AActor* Target, const FString& AbilityName)
{
    if (!IsValid(Source) || !Source->HasAuthority() || !CireCombat::IsAlive(Target) || !CireCombat::AreHostile(Source, Target)) return;
    const float Seconds = SetUpSeconds * CireKits::ControlScale(Source);
    CireBuffs::Apply(Target, SetUpId, Seconds, Source, FMath::RoundToInt(SetUpTeamBonus * 100.f));
    const float Now = Source->GetWorld()->GetTimeSeconds();
    float& Last = LastCallout().FindOrAdd(Target);
    if (Now - Last >= 1.f || Last > Now) { Last = Now; CireCombat::BroadcastAvoidance(Source, Target, ECireHitOutcome::SetUp, AbilityName); }
}

float CireInitiation::ModifyOutgoingDamage(AActor* Source, AActor* Target, float Amount)
{
    if (!Source || !IsValid(Target) || Amount <= 0 || !CireBuffs::IsActive(Target, SetUpId)) return Amount;
    const auto* State = CireBuffs::Get(Target);
    const FCireBuffEntry* E = State ? State->Find(SetUpId) : nullptr;
    const AActor* Initiator = E ? E->Source.Get() : nullptr;
    const int32 Team = Initiator ? CireCombat::TeamOf(Initiator) : E ? E->SourceTeam : -1;
    if (Team < 0 || Team != CireCombat::TeamOf(Source)) return Amount;
    return Amount * (1.f + (FCireAreaDamageScope::Active() ? SetUpAreaBonus : SetUpTeamBonus));
}

void CireInitiation::OnDamageDealt(AActor* Source, AActor* Target, float Applied)
{
    auto* Victim = Cast<ACireHero>(Target);
    if (Applied <= 0 || !Victim || Victim->IsA<ACireSummon>() || !Victim->HasAuthority()) return;
    const ACireHero* Attacker = Source ? CireKits::OwnerOf(Source) : nullptr;
    if (!Attacker || Attacker->IsA<ACireSummon>() || Attacker == Victim || !CireCombat::AreHostile(const_cast<ACireHero*>(Attacker), Victim)) return;
    if (const auto* Use = BlinkUse(Victim)) CireBuffs::Apply(Victim, BlinkLockedId, FMath::Clamp(static_cast<float>(Use->Duration), .5f, 10.f), const_cast<ACireHero*>(Attacker));
}

bool CireInitiation::CarriesBlink(const ACireHero* Hero) { return BlinkUse(Hero) != nullptr; }
bool CireInitiation::CanBlink(const ACireHero* Hero, FString& Why)
{
    if (!Hero) return false;
    if (CireBuffs::IsActive(Hero, BlinkLockedId))
    {
        const auto* State = CireBuffs::Get(Hero); const FCireBuffEntry* E = State ? State->Find(BlinkLockedId) : nullptr;
        const float Left = E ? FMath::Max(0.f, E->EndTime - CireBuffs::ServerNow(Hero->GetWorld())) : 0.f;
        Why = FString::Printf(TEXT("Blink disrupted by champion damage (%.1fs)."), Left);
        return false;
    }
    return true;
}
void CireInitiation::SetUseAim(ACireHero* Hero, FVector Aim) { if (Hero && !Aim.ContainsNaN()) PendingAim().Add(Hero, Aim); }

bool CireInitiation::Blink(ACireHero* H, float Range, FString& Message)
{
    if (!IsValid(H) || !H->HasAuthority() || H->bDead) return false;
    UWorld* World = H->GetWorld();
    auto* Mode = World->GetAuthGameMode<ACireGameMode>();
    FVector Aim;
    if (const FVector* P = PendingAim().Find(H)) { Aim = *P; PendingAim().Remove(H); }
    else Aim = IsValid(H->Target) ? H->Target->GetActorLocation() : H->GetActorLocation() + H->GetActorForwardVector() * Range;
    const FVector Origin = H->GetActorLocation();
    FVector Dir = (Aim - Origin).GetSafeNormal2D();
    if (Dir.IsNearlyZero()) Dir = H->GetActorForwardVector().GetSafeNormal2D();
    const float Want = FMath::Clamp(static_cast<float>(FVector::Dist2D(Origin, Aim)), 150.f, FMath::Max(150.f, Range));
    const float Half = H->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
    // Walk back from the wanted point until a standable, in-realm spot accepts the capsule (blinks pass walls, never into them).
    for (float D = Want; D >= 100.f; D -= 60.f)
    {
        FVector P = Origin + Dir * D;
        if (Mode && !CireSkillRuntime::InRealmBounds(Mode, H->TeamId, P)) continue;
        if (!GroundBelow(H, P) || ACireConstruct::FindBlockingConstruct(H, P)) continue;
        FVector Dest = P + FVector(0, 0, Half + 2.f);
        if (!H->TeleportTo(Dest, Dir.Rotation(), false, true)) continue;
        H->GetCharacterMovement()->StopMovementImmediately();
        CireCombat::PlayCue(H, nullptr, TEXT("blink_dagger"), Origin, Origin, ECireSpellCue::Impact, 1.f, true);
        CireCombat::PlayCue(H, nullptr, TEXT("blink_dagger"), H->GetActorLocation(), H->GetActorLocation(), ECireSpellCue::Cast, 1.f, true);
        H->ForceNetUpdate();
        Message = FString::Printf(TEXT("Blink (%.0f m)"), FVector::Dist2D(Origin, H->GetActorLocation()) / 100.f);
        UE_LOG(LogCireInitiation, Verbose, TEXT("CIRE_BLINK %s %.0f cm"), *H->HeroName, FVector::Dist2D(Origin, H->GetActorLocation()));
        return true;
    }
    Message = TEXT("No room to blink there.");
    return false;
}

const TArray<FName>& CireInitiation::BuffIds() { static const TArray<FName> Ids = {SetUpId, BlinkLockedId}; return Ids; }
