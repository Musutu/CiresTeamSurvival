// Native grip checks (Docs/MonsterArt.md, "Grips"): every weapon class held by a Tripo champion or monster,
// in idle and mid-attack. Run from CireMonsterArt::RunSmoke (RunNPCChecks / RunExpansionChecks native).
#include "CireGrip.h"

#if !UE_BUILD_SHIPPING
#include "CireChampionActions.h"
#include "CireChampionArt.h"
#include "CireGame.h"
#include "CireMonsterArt.h"
#include "CireMonsterAnim.h"
#include "CireNPCCombat.h"
#include "CireNPCState.h"
#include "CireWeaponPresentation.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Misc/ScopeExit.h"

DEFINE_LOG_CATEGORY_STATIC(LogCireGripTests, Log, All);


namespace
{
double LineDistance(const FVector& Point, const FVector& Origin, const FVector& Dir)
{
    const FVector D = Point - Origin;
    return (D - Dir * FVector::DotProduct(D, Dir)).Size();
}

struct FChecker
{
    bool bPass = true; int32 Checks = 0; int32 Grips = 0;
    void Check(bool bValue, const FString& Why)
    {
        ++Checks;
        if (!bValue) { bPass = false; UE_LOG(LogCireGripTests, Error, TEXT("CIRE_GRIP_CHECK_FAIL %s"), *Why); }
    }
};

void Refresh(USkeletalMeshComponent* Mesh)
{
    Mesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
    Mesh->TickAnimation(0.f, false);
    Mesh->RefreshBoneTransforms();
    Mesh->UpdateChildTransforms();
}

/** Measures every held prop of Body against its hand pose. */
void MeasureBody(FChecker& C, const FString& Tag, USkeletalMeshComponent* Body, const TArray<UStaticMeshComponent*>& Parts,
    const CireGrip::FHands& Hands, bool bTwoHandExpected)
{
    bool bAllFinite = true;
    for (int32 I = 0; I < Body->GetNumBones(); ++I) bAllFinite &= !Body->GetBoneTransform(I).ContainsNaN();
    C.Check(bAllFinite, Tag + TEXT(" pose finite"));
    const float BodyScale = static_cast<float>(Body->GetComponentScale().X / FMath::Max(1.e-3, Body->GetRelativeScale3D().X));
    for (UStaticMeshComponent* Part : Parts)
    {
        const CireGrip::FWeapon* W = Part ? CireGrip::FindWeapon(Part->GetStaticMesh()) : nullptr;
        if (!W || W->bAmmo) continue;
        const FString Where = Tag + TEXT(" ") + Part->GetStaticMesh()->GetName();
        const FTransform T = Part->GetComponentTransform();
        C.Check(!T.ContainsNaN(), Where + TEXT(" transform finite"));
        const float Scale = static_cast<float>(T.GetScale3D().X);
        const float R = W->RadiusCm * Scale;
        const FVector P = T.TransformPosition(W->Handle);
        const FVector D = T.TransformVectorNoScale(W->Axis).GetSafeNormal();
        const FName Socket = Part->GetAttachSocketName();
        // Only hand-held and forearm-strapped props (a belt dagger on the pelvis is not gripped).
        if (!Socket.ToString().StartsWith(TEXT("hand_")) && !Socket.ToString().StartsWith(TEXT("lowerarm_"))) continue;
        if (W->bShield)
        {
            // On the outside of the forearm: centre between elbow and wrist, a few centimetres off the arm.
            const TCHAR* Side = Socket.ToString().EndsWith(TEXT("_l")) ? TEXT("_l") : TEXT("_r");
            const FVector Elbow = Body->GetSocketLocation(FName(FString(TEXT("lowerarm")) + Side));
            const FVector Wrist = Body->GetSocketLocation(FName(FString(TEXT("hand")) + Side));
            const FVector Centre = T.TransformPosition(W->Handle);
            const double Along = FVector::DotProduct(Centre - Elbow, (Wrist - Elbow).GetSafeNormal()) / FMath::Max(1., (Wrist - Elbow).Size());
            const double Off = LineDistance(Centre, Elbow, (Wrist - Elbow).GetSafeNormal());
            C.Check(Along > .2 && Along < .9 && Off > 2.0 * BodyScale && Off < 16.0 * BodyScale,
                FString::Printf(TEXT("%s strapped on the forearm (along %.2f, off %.1fcm)"), *Where, Along, Off));
            ++C.Grips;
            continue;
        }
        const int32 SideIndex = Socket == TEXT("hand_r") ? 1 : 0;
        const CireGrip::FHandPose& Pose = Hands.Pose[SideIndex];
        C.Check(Hands.Weight[SideIndex] > 0.f && Pose.bValid && Pose.Type == CireGrip::EHand::Power, Where + TEXT(" hand closes in a power grip"));
        if (!Pose.bValid) continue;
        const TCHAR* Side = SideIndex ? TEXT("_r") : TEXT("_l");
        const FTransform Hand = Body->GetSocketTransform(FName(FString(TEXT("hand")) + Side), RTS_World);
        const FTransform Grip = Pose.GripInHand * Hand;
        const double Centre = LineDistance(Grip.GetLocation(), P, D);
        const double Along = FMath::Abs(FVector::DotProduct(Grip.GetLocation() - P, D));
        const double Angle = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct(D, Grip.GetRotation().GetAxisZ()), -1.0, 1.0)));
        C.Check(Centre <= .35 * R + 1.2 * BodyScale, FString::Printf(TEXT("%s handle axis through the palm centre (%.2fcm, r %.2f)"), *Where, Centre, R));
        C.Check(Along <= 2.5 * BodyScale, FString::Printf(TEXT("%s handle point in the fist (%.2fcm along)"), *Where, Along));
        C.Check(FMath::Abs(Angle - FMath::Abs(W->TiltDeg)) <= 6.0, FString::Printf(TEXT("%s handle orientation %.1f deg (tilt %.0f)"), *Where, Angle, W->TiltDeg));
        for (const TCHAR* Finger : {TEXT("index"), TEXT("middle"), TEXT("ring"), TEXT("pinky")})
        {
            const FName Joint(FString::Printf(TEXT("%s_02%s"), Finger, Side));
            if (Body->GetBoneIndex(Joint) == INDEX_NONE) continue;
            const double F = LineDistance(Body->GetSocketLocation(Joint), P, D);
            // Handles thicker than a fist can close around (the Behemoth's 10 cm totem pole) are held loosely.
            const double Loose = R > 4.0 ? .5 * R : 0.0;
            C.Check(F >= .6 * R && F <= R + 5.5 * BodyScale + Loose, FString::Printf(TEXT("%s %s wraps the handle without piercing (%.2fcm, r %.2f)"), *Where, Finger, F, R));
        }
        for (const TCHAR* Knuckle : {TEXT("hand"), TEXT("index_01"), TEXT("middle_01"), TEXT("pinky_01")})
        {
            const double K = LineDistance(Body->GetSocketLocation(FName(FString(Knuckle) + Side)), P, D);
            C.Check(K >= .8 * R, FString::Printf(TEXT("%s handle clear of the %s (%.2fcm, r %.2f)"), *Where, Knuckle, K, R));
        }
        if (bTwoHandExpected && W->bTwoHand && Hands.bTwoHand)
        {
            const TCHAR* Off = SideIndex ? TEXT("_l") : TEXT("_r");
            const CireGrip::FHandPose& OffPose = Hands.Pose[1 - SideIndex];
            const FTransform OffGrip = OffPose.GripInHand * Body->GetSocketTransform(FName(FString(TEXT("hand")) + Off), RTS_World);
            // The second grip has its own handle line (the crossbow stock runs across the main grip).
            const double OffDistance = LineDistance(OffGrip.GetLocation(), T.TransformPosition(W->OffHand), T.TransformVectorNoScale(W->OffAxis).GetSafeNormal());
            const FVector Shoulder = Body->GetSocketLocation(FName(FString(TEXT("upperarm")) + Off));
            const double ArmLength = (Body->GetSocketLocation(FName(FString(TEXT("lowerarm")) + Off)) - Shoulder).Size() +
                (Body->GetSocketLocation(FName(FString(TEXT("hand")) + Off)) - Body->GetSocketLocation(FName(FString(TEXT("lowerarm")) + Off))).Size();
            const FTransform Target = Hands.OffHandInMain * Hand;
            C.Check(OffPose.bValid && OffDistance <= R + 4.0 * BodyScale, FString::Printf(TEXT("%s second hand on the handle (%.2fcm; target %.1fcm from shoulder, arm %.1fcm, hand %.1fcm from target)"),
                *Where, OffDistance, (Target.GetLocation() - Shoulder).Size(), ArmLength,
                (Body->GetSocketLocation(FName(FString(TEXT("hand")) + Off)) - Target.GetLocation()).Size()));
        }
        if (bTwoHandExpected && W->bCarry) C.Check(D.Z > .75, FString::Printf(TEXT("%s carried upright (axis z %.2f)"), *Where, D.Z));
        ++C.Grips;
    }
}
}

bool CireGrip::RunSmoke(ACireGameMode* Mode)
{
    if (!Mode || Mode->GetNetMode() == NM_DedicatedServer) return true;
    FChecker C;
    UWorld* World = Mode->GetWorld();
    TArray<AActor*> Actors;
    const bool bForced = GCireForceTripoChampionArt;
    const auto Clock = Mode->Clock; const auto Heroes = Mode->Heroes; const auto Monsters = Mode->Monsters;
    ON_SCOPE_EXIT
    {
        GCireForceTripoChampionArt = bForced;
        for (int32 I = Actors.Num() - 1; I >= 0; --I) if (IsValid(Actors[I])) Actors[I]->Destroy();
        Mode->Clock = Clock; Mode->Heroes = Heroes; Mode->Monsters = Monsters;
    };
    GCireForceTripoChampionArt = true;
    Mode->Heroes.Reset(); Mode->Monsters.Reset();
    const FVector Ground(0, -2100, 3000);
    if (auto* Floor = World->SpawnActor<AActor>())
    {
        Actors.Add(Floor);
        auto* Box = NewObject<UBoxComponent>(Floor); Floor->SetRootComponent(Box); Floor->AddInstanceComponent(Box);
        Box->SetBoxExtent(FVector(2400, 1200, 50)); Box->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
        Box->SetCollisionResponseToAllChannels(ECR_Block); Box->RegisterComponent(); Floor->SetActorLocation(Ground - FVector(0, 0, 50));
    }
    FActorSpawnParameters Params; Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    struct FCase { const TCHAR* Profile; const TCHAR* Preset; const TCHAR* Class; };
    const FCase Cases[] = {
        {TEXT("knight"), TEXT("warden"), TEXT("sword+shield")}, {TEXT("knight"), TEXT("hammer_shield"), TEXT("hammer+shield")},
        {TEXT("ranger"), TEXT("ranger"), TEXT("bow")}, {TEXT("ranger"), TEXT("ranger_crossbow"), TEXT("crossbow")},
        {TEXT("scholar"), TEXT("scholar"), TEXT("staff")}, {TEXT("summoner"), TEXT("summoner"), TEXT("staff+dagger")},
        {TEXT("keeper_of_light"), TEXT("keeper"), TEXT("lantern staff")}, {TEXT("orc_chieftain"), TEXT("chieftain"), TEXT("axe")},
        {TEXT("troll_berserker_melee"), TEXT("troll_melee"), TEXT("throwing axes")}, {TEXT("troll_berserker_melee"), TEXT("dual_daggers"), TEXT("daggers")},
        {TEXT("paladin_holy"), TEXT("paladin"), TEXT("flail+shield")}, {TEXT("dwarf_miner"), TEXT("miner"), TEXT("pick")},
        {TEXT("totemic_behemoth"), TEXT("behemoth"), TEXT("totem")}, {TEXT("lancer"), TEXT("lancer"), TEXT("lance")}};
    float Y = -1000.f;
    for (const FCase& Case : Cases)
    {
        const FString Tag = FString::Printf(TEXT("%s/%s"), Case.Profile, Case.Class);
        auto* H = World->SpawnActor<ACireHero>(Ground + FVector(0, Y, 100), FRotator::ZeroRotator, Params);
        Y += 150.f;
        if (!H) { C.Check(false, Tag + TEXT(" spawned")); continue; }
        Actors.Add(H); H->SetActorTickEnabled(false); H->TeamId = 0;
        C.Check(H->DraftProfile(Case.Profile), Tag + TEXT(" drafted"));
        H->GetCharacterMovement()->DisableMovement();
        H->ChampionArt->UpdateVisuals(*H, 0.f);
        auto* Weapons = H->FindComponentByClass<UCireWeaponPresentation>();
        for (int32 Try = 0; Weapons && Try < 4 && Weapons->GetEquippedLoadout() != Case.Preset; ++Try) { FString Message; Weapons->CyclePreview(*H, Message); }
        if (!Weapons || Weapons->GetEquippedLoadout() != Case.Preset || !H->ChampionArt->IsApplied()) { C.Check(false, Tag + TEXT(" Tripo body and preset")); continue; }
        TArray<UStaticMeshComponent*> Parts;
        for (const auto& Part : Weapons->GetParts()) if (Part) Parts.Add(Part.Get());
        USkeletalMeshComponent* Mesh = H->GetMesh();
        // Idle.
        H->ChampionArt->UpdateVisuals(*H, 0.f); Refresh(Mesh);
        auto* Anim = Cast<UCireCombatAnimInstance>(Mesh->GetAnimInstance());
        C.Check(Anim != nullptr, Tag + TEXT(" champion anim instance"));
        if (!Anim) continue;
        MeasureBody(C, Tag + TEXT(" idle"), Mesh, Parts, Anim->Hands, true);
        // Mid-attack: the style's clip held at contact.
        const FString Clip = CireChampionActions::ClipName(*H, TEXT("attack"));
        C.Check(CireChampionActions::Hold(*H, Clip, 1.f), Tag + TEXT(" holds ") + Clip);
        H->ChampionArt->UpdateVisuals(*H, 0.f); Refresh(Mesh);
        MeasureBody(C, Tag + TEXT(" attack"), Mesh, Parts, Anim->Hands, false);
        C.Check(Anim->AttackWeight > .9f, Tag + TEXT(" attack pose applied"));
    }
    // Monsters: the props they keep.
    const TCHAR* Ids[] = {TEXT("hollow_infantry"), TEXT("ironbound_bruiser"), TEXT("hollow_shieldbearer"), TEXT("barbed_hunter")};
    for (const TCHAR* Id : Ids)
    {
        auto* M = World->SpawnActor<ACireMonster>(Ground + FVector(400, Y, 100), FRotator::ZeroRotator, Params);
        Y += 150.f;
        if (!M) continue;
        Actors.Add(M); M->SetActorTickEnabled(false); M->Lane = 0; Mode->Monsters.Add(M);
        if (M->MonsterArt) M->MonsterArt->ForceVariant(0);
        CireNPCCombat::ConfigureArchetype(M, FName(Id), 1, 0, 1);
        if (!M->MonsterArt || !M->MonsterArt->IsTripoApplied()) { C.Check(false, FString(Id) + TEXT(" Tripo body")); continue; }
        TArray<UStaticMeshComponent*> Parts;
        for (const auto& Part : M->NPCState->VisualParts) if (Part) Parts.Add(Part.Get());
        for (const TCHAR* Role : {TEXT("idle"), TEXT("attack")})
        {
            C.Check(M->MonsterArt->PoseForTest(Role, FCString::Strcmp(Role, TEXT("idle")) == 0 ? .3f : .55f), FString(Id) + TEXT(" pose ") + Role);
            M->GetMesh()->UpdateChildTransforms();
            const auto* Anim = M->MonsterArt->GetMonsterAnim();
            if (Anim) MeasureBody(C, FString(Id) + TEXT(" ") + Role, M->GetMesh(), Parts, Anim->Hands, false);
        }
    }
    UE_LOG(LogCireGripTests, Display, TEXT("CIRE_GRIP_%s checks=%d grips=%d"), C.bPass ? TEXT("PASS") : TEXT("FAIL"), C.Checks, C.Grips);
    return C.bPass;
}
#endif
