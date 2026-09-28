// bosses-spacing: native checks for boss body size, capsule/nav sizing, melee reach, separation and the raid-bar
// phases. Invoked from CireNPCCombat::RunSmoke (-CireCombatExpansionProbe, RunExpansionChecks --only native).
#include "CireUnitSpacing.h"

#if !UE_BUILD_SHIPPING
#include "CireGame.h"
#include "CireHUD.h"
#include "CireNav.h"
#include "CireNPCArchetypes.h"
#include "CireNPCCombat.h"
#include "CireNPCState.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"

DEFINE_LOG_CATEGORY_STATIC(LogCireUnitSpacingTests, Log, All);

bool CireUnitSpacing::RunSmoke(ACireGameMode* Mode)
{
    if (!IsValid(Mode) || !Mode->HasAuthority()) return false;
    bool bPass = true; int32 Checks = 0;
    auto Check = [&](bool V, const FString& Why) { ++Checks; if (!V) { bPass = false; UE_LOG(LogCireUnitSpacingTests, Error, TEXT("CIRE_UNIT_SPACING_CHECK_FAIL %s"), *Why); } };
    // Data: the shipped JSON parses and holds the defaults the rest of the checks assume.
    {
        FString Json, Error; FCireUnitSpacing S;
        Check(FFileHelper::LoadFileToString(Json, *(FPaths::ProjectContentDir() / TEXT("Data/UnitSpacing.json"))) && Parse(Json, S, Error), TEXT("UnitSpacing.json parses: ") + Error);
        Check(FMath::IsNearlyEqual(S.BossSizeMultiplier, 5.f) && S.BossCapsuleRadiusMax <= 72.f && S.BossCapsuleHalfHeightMax <= 150.f, TEXT("bosses are 5x with a Large-agent capsule"));
        Check(S.MonsterCapsuleRadius > 38.f && S.MonsterCapsuleRadius <= 48.f && S.MeleeReachBonus > 0.f && S.bSeparation, TEXT("units are slightly wider (fits the Hero nav agent), reach further and separate"));
        FCireUnitSpacing Bad; Check(!Parse(TEXT("{\"boss\":{\"sizeMultiplier\":50}}"), Bad, Error), TEXT("an out-of-range size is rejected"));
    }
    const FCireUnitSpacing Saved = Get();
    const Cires::MatchClock Clock = Mode->Clock; const TArray<ACireHero*> Heroes = Mode->Heroes; const TArray<ACireMonster*> Monsters = Mode->Monsters;
    TArray<AActor*> Actors;
    ON_SCOPE_EXIT
    {
        Set(Saved);
        for (int32 I = Actors.Num() - 1; I >= 0; --I) if (IsValid(Actors[I])) Actors[I]->Destroy();
        Mode->Clock = Clock; Mode->Heroes = Heroes; Mode->Monsters = Monsters;
    };
    FCireUnitSpacing Now; Set(Now); // the built-in defaults == the shipped JSON
    Mode->Clock = Cires::MatchClock(); Mode->Heroes.Reset(); Mode->Monsters.Reset();
    const FVector Ground(0, 2600, 3000);
    if (AActor* Floor = Mode->GetWorld()->SpawnActor<AActor>())
    {
        Actors.Add(Floor); auto* Box = NewObject<UBoxComponent>(Floor); Floor->SetRootComponent(Box); Floor->AddInstanceComponent(Box);
        Box->SetBoxExtent(FVector(2400, 1200, 50)); Box->SetCollisionObjectType(ECC_WorldStatic);
        Box->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics); Box->SetCollisionResponseToAllChannels(ECR_Block);
        Box->RegisterComponent(); Floor->SetActorLocation(Ground - FVector(0, 0, 50));
    }
    FActorSpawnParameters Params; Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    auto Spawn = [&](FVector Offset, FName Id, bool bLaneBoss) -> ACireMonster*
    {
        auto* M = Mode->GetWorld()->SpawnActor<ACireMonster>(Ground + Offset + FVector(0, 0, 88), FRotator::ZeroRotator, Params);
        if (!M) return nullptr;
        Actors.Add(M); M->SetActorTickEnabled(false); M->Lane = 0; Mode->Monsters.Add(M);
        CireNPCCombat::ConfigureArchetype(M, Id, 1, 0, 1, bLaneBoss);
        return M;
    };
    const FCireNPCDatabase& D = CireNPCArchetypes::Get();
    FName Normal = D.LegacyKinds.IsValidIndex(0) ? D.LegacyKinds[0] : NAME_None;
    ACireMonster* Boss = Spawn(FVector::ZeroVector, D.WaveBoss, true);
    ACireMonster* Leader = D.PackLeader.IsNone() ? nullptr : Spawn(FVector(0, 900, 0), D.PackLeader, false);
    ACireMonster* A = Spawn(FVector(-1500, -600, 0), Normal, false);
    ACireMonster* B = Spawn(FVector(-1500, -540, 0), Normal, false);
    auto* Hero = Mode->GetWorld()->SpawnActor<ACireHero>(Ground + FVector(1500, 0, 92), FRotator::ZeroRotator, Params);
    if (Hero) { Actors.Add(Hero); Hero->SetActorTickEnabled(false); Hero->TeamId = 0; Hero->Draft(0); Mode->Heroes.Add(Hero); }
    if (!Boss || !A || !B || !Hero) { Check(false, TEXT("fixture spawned")); UE_LOG(LogCireUnitSpacingTests, Display, TEXT("CIRE_UNIT_SPACING_TESTS_FAIL checks=%d"), Checks); return false; }

    // Boss body: 5x the legacy size, capsule capped to the Large nav agent, feet on the floor, body hung from the capsule bottom.
    Set(FCireUnitSpacing::Legacy()); CireNPCCombat::Tick(Boss, 0.f);
    const float LegacyScale = static_cast<float>(Boss->GetActorScale3D().X);
    Set(Now); CireNPCCombat::Tick(Boss, 0.f);
    const float Scale = static_cast<float>(Boss->GetActorScale3D().X);
    UCapsuleComponent* Cap = Boss->GetCapsuleComponent();
    Check(IsBossBody(Boss) && FMath::IsNearlyEqual(Scale, LegacyScale * 5.f, .01f), FString::Printf(TEXT("wave boss drawn 5x (%.2f vs legacy %.2f)"), Scale, LegacyScale));
    Check(Cap->GetScaledCapsuleRadius() <= 72.5f && Cap->GetScaledCapsuleHalfHeight() <= 150.5f && Cap->GetScaledCapsuleHalfHeight() >= Cap->GetScaledCapsuleRadius(),
        FString::Printf(TEXT("boss collision is the Large nav agent (r %.0f, half %.0f)"), Cap->GetScaledCapsuleRadius(), Cap->GetScaledCapsuleHalfHeight()));
    Check(CireNav::AgentRadius(Boss) <= 78.f, TEXT("boss paths on the Large navmesh (radius within its tolerance)"));
    const float MeshZ = static_cast<float>(Boss->GetMesh()->GetRelativeLocation().Z);
    Check(MeshZ <= -Cap->GetUnscaledCapsuleHalfHeight() + 20.f && MeshZ >= -Cap->GetUnscaledCapsuleHalfHeight() - 20.f,
        FString::Printf(TEXT("boss body hangs from the capsule bottom (mesh z %.1f, unscaled half %.1f)"), MeshZ, Cap->GetUnscaledCapsuleHalfHeight()));
    const float Feet = static_cast<float>(Boss->GetActorLocation().Z - Cap->GetScaledCapsuleHalfHeight());
    Check(FMath::Abs(Feet - Ground.Z) < 6.f, FString::Printf(TEXT("boss feet stay on the floor after growing (%.1f vs %.1f)"), Feet, Ground.Z));
    Check(PlateLift(Boss) > 700.f, TEXT("boss nameplate sits over the giant head"));
    Check(BodyReachBonus(Boss) > 100.f && MeleeReach(Boss, 200.f) > 200.f + 100.f, TEXT("boss melee reaches from its body's edge"));
    Hero->SetActorLocation(Boss->GetActorLocation() + FVector(200.f + BodyReachBonus(Boss) - 10.f, 0, 0));
    Check(Hero->InRange(Boss, 200.f), TEXT("a hero reaches a giant boss from its drawn edge"));
    Hero->SetActorLocation(Ground + FVector(1500, 0, 92));
    if (Leader) Check(IsBossBody(Leader) && Leader->GetActorScale3D().X > A->GetActorScale3D().X * 4.f && Leader->GetCapsuleComponent()->GetScaledCapsuleRadius() <= 72.5f, TEXT("boss pack leader is a giant too"));
    // Normal units: wider capsule, same height; no body bonus; slightly longer melee reach.
    const float AS = static_cast<float>(A->GetActorScale3D().X);
    Check(!IsBossBody(A) && FMath::IsNearlyEqual(A->GetCapsuleComponent()->GetScaledCapsuleRadius(), Now.MonsterCapsuleRadius * AS, .1f) &&
        FMath::IsNearlyEqual(A->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight(), BaseHalfHeight, .1f), TEXT("normal unit capsule is wider, same height"));
    Check(FMath::IsNearlyEqual(MeleeReach(A, 170.f), 170.f + Now.MeleeReachBonus, .1f) && BodyReachBonus(A) == 0.f, TEXT("normal melee reach = attackRange + bonus"));
    // Separation: crowding lane-mates push apart; units with room do not; legacy has none.
    const FVector Push = Separation(A, Mode);
    Check(!Push.IsNearlyZero() && Push.Y < 0.f, TEXT("crowded melee unit sidesteps away from its neighbour"));
    B->SetActorLocation(A->GetActorLocation() + FVector(0, 600, 0));
    Check(Separation(A, Mode).IsNearlyZero(), TEXT("a unit with room does not move"));
    B->SetActorLocation(A->GetActorLocation() + FVector(0, 40, 0));
    Set(FCireUnitSpacing::Legacy()); Check(Separation(A, Mode).IsNearlyZero(), TEXT("legacy spacing has no separation")); Set(Now);
    // Crowd metric the probe uses.
    const FCrowd C = Measure({A, B});
    Check(C.Units == 2 && C.OverlapPairs == 1 && C.MeanNearest < 60.f, TEXT("crowd metric counts an overlapping pair"));
    // Raid bar phases: sorted descending, inside (0,1), default quarter marks without authored thresholds.
    const TArray<float> Phases = ACireHUD::RaidBossPhases(Boss);
    bool bSorted = !Phases.IsEmpty();
    for (int32 I = 0; I < Phases.Num(); ++I) bSorted &= Phases[I] > 0.f && Phases[I] < 1.f && (I == 0 || Phases[I] < Phases[I - 1]);
    Check(bSorted, TEXT("raid bar phase ticks are sorted health thresholds"));
    Check(ACireHUD::RaidBossPhases(nullptr).Num() == 3, TEXT("raid bar falls back to quarter marks"));
    UE_LOG(LogCireUnitSpacingTests, Display, TEXT("CIRE_UNIT_SPACING_TESTS_%s checks=%d boss_scale=%.2f capsule=%.0f/%.0f reach_bonus=%.0f"), bPass ? TEXT("PASS") : TEXT("FAIL"), Checks,
        Scale, Cap->GetScaledCapsuleRadius(), Cap->GetScaledCapsuleHalfHeight(), BodyReachBonus(Boss));
    return bPass;
}
#endif
