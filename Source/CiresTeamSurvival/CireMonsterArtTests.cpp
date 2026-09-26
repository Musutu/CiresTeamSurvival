// Native checks for the animated Tripo monster bodies (Docs/MonsterArt.md).
// Invoked from CireNPCCombat::RunSmoke, so -CireCombatExpansionProbe and Tools/RunNPCChecks.py run them.
#include "CireMonsterArt.h"

#if !UE_BUILD_SHIPPING
#include "CireMonsterAnim.h"
#include "CireChampionActions.h"
#include "CireGame.h"
#include "CireNPCArchetypes.h"
#include "CireNPCCombat.h"
#include "CireNPCState.h"
#include "CireThreat.h"
#include "CireFootsteps.h"
#include "Materials/MaterialInterface.h"
#include "Animation/AnimSequence.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/DamageEvents.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "Misc/ScopeExit.h"

DEFINE_LOG_CATEGORY_STATIC(LogCireMonsterArtTests, Log, All);

namespace
{
struct FPoseStats
{
    bool bFinite = true;
    float FeetZ = 0.f, HeadZ = 0.f, MaxDistance = 0.f;
    FVector2D PelvisOffset = FVector2D::ZeroVector;
};

FPoseStats Measure(const ACireMonster& Monster)
{
    FPoseStats Stats;
    const USkeletalMeshComponent* Mesh = Monster.GetMesh();
    const FVector Origin = Monster.GetActorLocation();
    float Feet = TNumericLimits<float>::Max();
    for (const TCHAR* Bone : {TEXT("foot_l"), TEXT("foot_r"), TEXT("ball_l"), TEXT("ball_r")})
        Feet = FMath::Min(Feet, static_cast<float>(Mesh->GetSocketLocation(Bone).Z));
    Stats.FeetZ = Feet;
    Stats.HeadZ = static_cast<float>(Mesh->GetSocketLocation(TEXT("head")).Z);
    Stats.PelvisOffset = FVector2D(Mesh->GetSocketLocation(TEXT("pelvis")) - Origin);
    for (int32 Bone = 0; Bone < Mesh->GetNumBones(); ++Bone)
    {
        const FVector P = Mesh->GetBoneLocation(Mesh->GetBoneName(Bone));
        if (P.ContainsNaN()) { Stats.bFinite = false; continue; }
        Stats.MaxDistance = FMath::Max(Stats.MaxDistance, static_cast<float>(FVector::Dist(P, Origin)));
    }
    return Stats;
}
}

bool CireMonsterArt::RunSmoke(ACireGameMode* Mode)
{
    if (!IsValid(Mode) || !Mode->HasAuthority()) return false;
    if (Mode->GetNetMode() == NM_DedicatedServer)
    {
        UE_LOG(LogCireMonsterArtTests, Display, TEXT("CIRE_MONSTER_ART_SKIP dedicated server draws no bodies"));
        return true;
    }
    bool bPass = true;
    int32 Checks = 0;
    auto Check = [&](bool bValue, const FString& Why)
    {
        ++Checks;
        if (!bValue) { bPass = false; UE_LOG(LogCireMonsterArtTests, Error, TEXT("CIRE_MONSTER_ART_CHECK_FAIL %s"), *Why); }
    };
    const FData& Art = Data(true);
    Check(Art.bValid, TEXT("NPCMeshes.tripo.json + MonsterArt.json load"));
    for (const auto& Pair : CireNPCArchetypes::Get().Archetypes)
        Check(Find(Pair.Key) != nullptr, TEXT("Tripo art for archetype ") + Pair.Key.ToString());

    UWorld* World = Mode->GetWorld();
    const auto Clock = Mode->Clock; const auto Heroes = Mode->Heroes; const auto Monsters = Mode->Monsters;
    TArray<AActor*> Actors;
    IConsoleVariable* TripoBodies = IConsoleManager::Get().FindConsoleVariable(TEXT("cire.Monsters.TripoBodies"));
    ON_SCOPE_EXIT
    {
        if (TripoBodies) TripoBodies->Set(1, ECVF_SetByCode);
        for (auto* M : Mode->Monsters) if (IsValid(M)) CireNPCCombat::Interrupt(M);
        for (int32 I = Actors.Num() - 1; I >= 0; --I) if (IsValid(Actors[I])) Actors[I]->Destroy();
        Mode->Clock = Clock; Mode->Heroes = Heroes; Mode->Monsters = Monsters;
    };
    Mode->Clock = Cires::MatchClock(); Mode->Heroes.Reset(); Mode->Monsters.Reset();
    const FVector Ground(0, -2100, 3000); // lane 0 realm, as the other NPC fixtures
    if (auto* Floor = World->SpawnActor<AActor>())
    {
        Actors.Add(Floor);
        auto* Box = NewObject<UBoxComponent>(Floor); Floor->SetRootComponent(Box); Floor->AddInstanceComponent(Box);
        Box->SetBoxExtent(FVector(2400, 1200, 50)); Box->SetCollisionObjectType(ECC_WorldStatic);
        Box->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics); Box->SetCollisionResponseToAllChannels(ECR_Block);
        Box->RegisterComponent(); Floor->SetActorLocation(Ground - FVector(0, 0, 50));
    }
    FActorSpawnParameters Params; Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    auto Spawn = [&](FName Id, int32 Variant, FVector Offset) -> ACireMonster*
    {
        auto* M = World->SpawnActor<ACireMonster>(Ground + Offset + FVector(0, 0, 100), FRotator::ZeroRotator, Params);
        if (!M) return nullptr;
        Actors.Add(M); M->SetActorTickEnabled(false); M->Lane = 0; Mode->Monsters.Add(M);
        if (M->MonsterArt) M->MonsterArt->ForceVariant(Variant);
        CireNPCCombat::ConfigureArchetype(M, Id, 1, 0, 1);
        M->SetActorLocation(Ground + Offset + FVector(0, 0, M->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()));
        M->GetMesh()->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
        return M;
    };

    TMap<FName, float> IdleHead;
    int32 Bodies = 0, Poses = 0;
    float X = -2000.f;
    for (const auto& Pair : Art.Archetypes)
    {
        const FCireNPCArchetype* Archetype = CireNPCArchetypes::Find(Pair.Key);
        if (!Archetype) continue;
        for (int32 Variant = 0; Variant < Pair.Value.Bodies.Num(); ++Variant)
        {
            const FBody& Body = Pair.Value.Bodies[Variant];
            const FString Tag = Pair.Key.ToString() + TEXT("/") + Body.Variant;
            ACireMonster* M = Spawn(Pair.Key, Variant, FVector(X, 0, 0));
            X += 400.f;
            if (!M || !M->MonsterArt) { Check(false, Tag + TEXT(" spawned")); continue; }
            UCireMonsterArt* Presentation = M->MonsterArt;
            ++Bodies;
            Check(Presentation->IsTripoApplied() && Presentation->GetAppliedVariant() == Body.Variant, Tag + TEXT(" resolves its Tripo body"));
            Check(M->GetMesh()->GetSkeletalMeshAsset() && (M->GetMesh()->GetSkeletalMeshAsset()->GetPathName() == Body.MeshPath || M->GetMesh()->GetSkeletalMeshAsset()->GetPathName() == Body.MeshOverride), Tag + TEXT(" mesh asset"));
            if (!Body.MeshOverride.IsEmpty()) Check(M->GetMesh()->GetSkeletalMeshAsset()->GetPathName() == Body.MeshOverride, Tag + TEXT(" uses its re-skinned weapon copy"));
            Check(Presentation->GetMonsterAnim() != nullptr, Tag + TEXT(" native anim instance (no mannequin AnimBP)"));
            for (const TCHAR* Role : {TEXT("idle"), TEXT("walk"), TEXT("run"), TEXT("attack"), TEXT("hit"), TEXT("death")})
                Check(Presentation->HasRoleClip(Role), Tag + TEXT(" clip ") + Role);
            if (!Presentation->IsTripoApplied()) continue;
            const float Scale = static_cast<float>(M->GetActorScale3D().X);
            const float Height = Body.HeightCm * Scale;
            const float Bottom = static_cast<float>(M->GetActorLocation().Z - M->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
            // Props: baked weapons are not duplicated; unarmed hands keep theirs at human scale.
            for (const FCireNPCProp& Prop : Archetype->Props)
            {
                const UStaticMeshComponent* Found = nullptr;
                for (const auto& Part : M->NPCState->VisualParts) if (Part && (Part->GetAttachSocketName() == Prop.Bone || Part->ComponentHasTag(FName(*(TEXT("CireGripHand_") + Prop.Bone.ToString()))))) Found = Part;
                if (Body.DropPropBones.Contains(Prop.Bone)) Check(Found == nullptr, Tag + TEXT(" drops duplicate prop on ") + Prop.Bone.ToString());
                else
                {
                    Check(Found != nullptr, Tag + TEXT(" keeps prop on ") + Prop.Bone.ToString());
                    const float Want = Prop.Scale * (Body.PropScale.Contains(Prop.Bone) ? Body.PropScale[Prop.Bone] : 1.f) * Scale;
                    if (Found) Check(FMath::IsNearlyEqual(Found->GetComponentScale().X, Want, .05f * Want),
                        FString::Printf(TEXT("%s prop %s world scale %.2f (want %.2f)"), *Tag, *Prop.Bone.ToString(), Found->GetComponentScale().X, Want));
                }
            }
            // Aura sockets, footstep feet and the armour class resolve on the animated body.
            for (const TCHAR* Bone : {TEXT("hand_l"), TEXT("hand_r"), TEXT("spine_03"), TEXT("head"), TEXT("foot_l"), TEXT("foot_r")})
                Check(M->GetMesh()->GetBoneIndex(Bone) != INDEX_NONE || (Body.Rig == TEXT("quadruped") && M->GetMesh()->DoesSocketExist(Bone)), // world-dressing: animal sockets
                    Tag + TEXT(" has aura/footstep bone ") + Bone);
            Check(CireFootsteps::ForCharacter(M).Class == CireFootsteps::ForMonster(Pair.Key.ToString()).Class, Tag + TEXT(" footstep armour class"));
            // Selection highlight: an overlay swapped in and out leaves the body's own overlay (elite/boss rim) in place.
            {
                UMaterialInterface* Before = M->GetMesh()->GetOverlayMaterial();
                UMaterialInterface* Highlight = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Art/Materials/M_SelectionEdge.M_SelectionEdge"));
                M->GetMesh()->SetOverlayMaterial(Highlight);
                const bool bShown = M->GetMesh()->GetOverlayMaterial() == Highlight;
                M->GetMesh()->SetOverlayMaterial(Before);
                Check(bShown && M->GetMesh()->GetOverlayMaterial() == Before, Tag + TEXT(" selection overlay round trip"));
            }
            // Idle: feet on the capsule bottom, head at the authored height.
            Check(Presentation->PoseForTest(TEXT("idle"), .3f), Tag + TEXT(" idle pose evaluates"));
            FPoseStats Idle = Measure(*M); ++Poses;
            const float Sole = Body.SoleCm * Scale, Air = Body.AirborneCm * Scale; // fab-integration: hooves, galloping suspension
            Idle.FeetZ -= Sole;
            Check(Idle.bFinite && Idle.FeetZ - Bottom > -4.f * Scale && Idle.FeetZ - Bottom < 22.f * Scale,
                FString::Printf(TEXT("%s idle feet %.1fcm above capsule bottom"), *Tag, Idle.FeetZ - Bottom));
            // tripo-races: race bodies carry drums, caps and crowns above the head bone (drummer .65, sporeling .735), so the floor is .62.
            Check(Idle.HeadZ - Bottom > .62f * Height && Idle.HeadZ - Bottom < 1.02f * Height,
                FString::Printf(TEXT("%s idle head %.1fcm for %.0fcm body"), *Tag, Idle.HeadZ - Bottom, Height));
            if (Variant == 0) IdleHead.Add(Pair.Key, (Idle.HeadZ - Bottom) / FMath::Max(1.f, Body.HeightCm));
            // Every clip stays finite, compact and grounded; locomotion stays in place over the capsule.
            for (const TCHAR* Role : {TEXT("walk"), TEXT("run"), TEXT("attack"), TEXT("hit"), TEXT("death")})
                for (int32 Step = 0; Step <= 4; ++Step)
                {
                    const float T = Step / 4.f;
                    const bool bEvaluated = Presentation->PoseForTest(Role, T);
                    FPoseStats P = Measure(*M); ++Poses; if (FCString::Strcmp(Role, TEXT("death")) != 0) P.FeetZ -= Sole; // a lying body rests on its side, not its hooves
                    const FString Where = FString::Printf(TEXT("%s %s@%.2f"), *Tag, Role, T);
                    const float Reach = FMath::Max(FMath::Max(1.6f * Height, 130.f), Body.ReachCm * Scale); // world-dressing: long-bodied animals; tripo-races: small swarm bodies sprawl ~125cm when they die
                    Check(bEvaluated && P.bFinite && P.MaxDistance < Reach, Where + FString::Printf(TEXT(" compact (max %.0fcm)"), P.MaxDistance));
                    Check(P.FeetZ - Bottom > -10.f * Scale, Where + FString::Printf(TEXT(" feet above ground (%.1f)"), P.FeetZ - Bottom));
                    if (FCString::Strcmp(Role, TEXT("death")) != 0)
                    {
                        Check(P.FeetZ - Bottom < 40.f * Scale + (FCString::Strcmp(Role, TEXT("run")) == 0 ? Air : 0.f), Where + FString::Printf(TEXT(" one foot planted (%.1f)"), P.FeetZ - Bottom));
                        // tripo-races: the Tripo slash is a deep overhead chop; hunched race bodies (bears, trolls, mammoth) dip to ~.5 of their height.
                        const float Upright = FCString::Strcmp(Role, TEXT("attack")) == 0 ? .45f : .5f;
                        Check(P.HeadZ - Bottom > Upright * Height, Where + FString::Printf(TEXT(" upright (head %.0f)"), P.HeadZ - Bottom));
                    }
                    if (FCString::Strcmp(Role, TEXT("walk")) == 0 || FCString::Strcmp(Role, TEXT("run")) == 0)
                        Check((P.PelvisOffset - Idle.PelvisOffset).Size() < 45.f * Scale, // fab-integration: relative to the idle stance anchor // tripo-races: winged/tailed bodies fit their run drift less tightly (up to ~40cm at scale 1)
                             Where + FString::Printf(TEXT(" in place (pelvis %.1fcm off)"), P.PelvisOffset.Size()));
                    if (FCString::Strcmp(Role, TEXT("death")) == 0 && Step == 4 && !Body.bSpectral) // monster-expansion: spirits rise and sink away
                        Check(P.HeadZ - Bottom < .45f * Height, Where + FString::Printf(TEXT(" lies down (head %.0f)"), P.HeadZ - Bottom));
                }
            const FVector2D Speeds = Presentation->GroundSpeeds();
            Check(Speeds.X > 40.f * Scale && Speeds.X < 350.f * Scale && Speeds.Y > Speeds.X && Speeds.Y < 1000.f * Scale,
                FString::Printf(TEXT("%s stride speeds walk %.0f run %.0f cm/s"), *Tag, Speeds.X, Speeds.Y));
            Presentation->bFrozen = false;
            UE_LOG(LogCireMonsterArtTests, Display, TEXT("CIRE_MONSTER_ART_BODY %s scale=%.2f feet=%.1f head=%.1f walk=%.0f run=%.0f props=%d"),
                *Tag, Scale, Idle.FeetZ - Bottom, Idle.HeadZ - Bottom, Speeds.X, Speeds.Y, M->NPCState->VisualParts.Num());
        }
    }
    // The Pack Leader is drawn 1.7x taller than its authored 190 cm body.
    if (const auto* Leader = CireNPCArchetypes::Find(TEXT("gravemaw_pack_leader")); Leader && IdleHead.Contains(TEXT("gravemaw_pack_leader")) && IdleHead.Contains(TEXT("hollow_infantry")))
    {
        const float Ratio = IdleHead[TEXT("gravemaw_pack_leader")] / FMath::Max(.01f, IdleHead[TEXT("hollow_infantry")]);
        Check(FMath::IsNearlyEqual(Ratio, Leader->Scale, .12f * Leader->Scale), FString::Printf(TEXT("pack leader height ratio %.2f (archetype scale %.2f)"), Ratio, Leader->Scale));
    }

    // Melee blows land on the contact frame, not the first frame of the swing.
    {
        auto* Hero = World->SpawnActor<ACireHero>(Ground + FVector(150, 600, 92), FRotator::ZeroRotator, Params);
        ACireMonster* M = Spawn(TEXT("hollow_infantry"), 0, FVector(0, 600, 0));
        if (Hero && M)
        {
            Actors.Add(Hero); Hero->SetActorTickEnabled(false); Hero->TeamId = 0; Hero->Draft(0);
            Hero->Health = Hero->MaxHealth = 100000; Hero->ShieldUntil = 0; Mode->Heroes.Add(Hero);
            bool bLanded = false, bDeferred = true;
            for (int32 Try = 0; Try < 8 && !bLanded; ++Try)
            {
                CireThreat::Engage(M, Hero); M->AttackTimer = 0; M->AbilityTimer = 10;
                const uint8 Serial = M->MonsterArt->SwingSerial; const float Before = Hero->Health;
                CireNPCCombat::Tick(M, .01f);
                bDeferred &= M->MonsterArt->HasPendingSwing() && M->MonsterArt->SwingSerial != Serial && Hero->Health == Before && M->MonsterArt->SwingWindup > .2f;
                M->AttackTimer = 10; CireNPCCombat::Tick(M, .01f);
                bDeferred &= M->MonsterArt->HasPendingSwing() && Hero->Health == Before;
                M->MonsterArt->PendingReleaseAt = World->GetTimeSeconds() - .01f;
                CireNPCCombat::Tick(M, .01f);
                bDeferred &= !M->MonsterArt->HasPendingSwing();
                bLanded = Hero->Health < Before;
                UE_LOG(LogCireMonsterArtTests, Display, TEXT("CIRE_MONSTER_ART_SWING try=%d landed=%d before=%.0f after=%.0f dist=%.0f team=%d lane=%d dead=%d drafted=%d victim=%d"),
                    Try, bLanded, Before, Hero->Health, FVector::Dist2D(M->GetActorLocation(), Hero->GetActorLocation()), Hero->TeamId, M->Lane, Hero->bDead, Hero->bDrafted, M->Victim == Hero);
            }
            Check(bDeferred, TEXT("melee swing is scheduled, then released once"));
            Check(bLanded, TEXT("released melee swing damages its victim"));
            CireThreat::Engage(M, Hero); M->AttackTimer = 0; CireNPCCombat::Tick(M, .01f);
            CireNPCCombat::Interrupt(M);
            Check(!M->MonsterArt->HasPendingSwing(), TEXT("interrupt cancels a pending swing"));
            // Death: the actor goes, a local corpse plays the fall, holds, sinks and despawns.
            const int32 CorpsesBefore = ACireMonsterCorpse::LiveCount();
            M->TakeDamage(1.e7f, FDamageEvent(), nullptr, Hero);
            Check(M->IsActorBeingDestroyed() && !Mode->Monsters.Contains(M), TEXT("killed monster is removed"));
            ACireMonsterCorpse* Corpse = nullptr;
            for (TActorIterator<ACireMonsterCorpse> It(World); It; ++It) if (!It->IsActorBeingDestroyed()) Corpse = *It;
            Check(ACireMonsterCorpse::LiveCount() == CorpsesBefore + 1 && Corpse != nullptr, TEXT("death leaves one local corpse"));
            if (Corpse)
            {
                for (int32 Step = 0; Step < 8; ++Step) Corpse->Tick(.5f);
                Corpse->Body->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
                Corpse->Body->TickAnimation(0.f, false); Corpse->Body->RefreshBoneTransforms();
                const float Head = static_cast<float>(Corpse->Body->GetSocketLocation(TEXT("head")).Z - Corpse->StartLocation.Z);
                Check(Head < 80.f && Head > -60.f, FString::Printf(TEXT("corpse holds the fallen pose (head %.0fcm)"), Head));
                for (int32 Step = 0; Step < 12 && !Corpse->IsActorBeingDestroyed(); ++Step) Corpse->Tick(.5f);
                Check(Corpse->IsActorBeingDestroyed(), TEXT("corpse despawns after hold and sink"));
            }
        }
        else Check(false, TEXT("swing fixture spawned"));
    }

    // Missing art keeps the mannequin body.
    if (TripoBodies)
    {
        TripoBodies->Set(0, ECVF_SetByCode);
        ACireMonster* M = Spawn(TEXT("hollow_infantry"), 0, FVector(0, -600, 0));
        Check(M && M->MonsterArt && !M->MonsterArt->IsTripoApplied() && M->GetMesh()->GetSkeletalMeshAsset() &&
            M->GetMesh()->GetSkeletalMeshAsset()->GetName() == TEXT("SKM_Manny_Simple") && M->NPCState->VisualParts.Num() > 0,
            TEXT("fallback keeps the mannequin body and props"));
        TripoBodies->Set(1, ECVF_SetByCode);
        if (M && M->MonsterArt) M->MonsterArt->ForceVariant(0);
        Check(M && M->MonsterArt && M->MonsterArt->IsTripoApplied(), TEXT("Tripo body re-applies over the fallback"));
    }
    UE_LOG(LogCireMonsterArtTests, Display, TEXT("CIRE_MONSTER_ART_%s checks=%d bodies=%d poses=%d"), bPass ? TEXT("PASS") : TEXT("FAIL"), Checks, Bodies, Poses);
    bPass = CireChampionActions::RunSmoke(Mode) && bPass;
    bPass = CireGrip::RunSmoke(Mode) && bPass;
    return bPass;
}
#endif
