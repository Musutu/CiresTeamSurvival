// new-champions: review gallery for the new champions and the Aetheri Constructs (see CireNewChampionsGallery.h).
#include "CireNewChampionsGallery.h"

#include "CireGame.h"
#include "CireChampionArt.h"
#include "CireChampionRoster.h"
#include "CireConstruct.h"
#include "CireLanePath.h"
#include "CireNPCArchetypes.h"
#include "CireNPCCombat.h"
#include "CireNPCState.h"
#include "CireRaces.h"
#include "CireSignatureSkills.h"
#include "CireTechConstructs.h"
#include "CirePets.h" // pets
#include "CireCreatureArt.h"
#include "GameFramework/HUD.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/TextRenderActor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HAL/FileManager.h"
#include "Misc/CommandLine.h"
#include "Misc/DateTime.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "ShaderCompiler.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Animation/AnimSequence.h"
#include "Animation/BlendSpace.h"
#include "Engine/SkeletalMesh.h"
#include "UnrealClient.h"

DEFINE_LOG_CATEGORY_STATIC(LogCireNewChampionsGallery, Log, All);

namespace
{
struct FStage { FString Name; float Settle = 2.f; bool bKeepScene = false; };
struct FGallery
{
    TWeakObjectPtr<ACireGameMode> Mode;
    TWeakObjectPtr<ACireController> Controller;
    TWeakObjectPtr<ACameraActor> Camera;
    TArray<TWeakObjectPtr<AActor>> Scene;
    TMap<FString, TWeakObjectPtr<ACireHero>> Heroes;
    TWeakObjectPtr<ACireHero> Focus;
    TArray<FStage> Stages;
    TArray<FString> Captures, Only;
    FString Directory;
    FVector Hold, Forward = FVector(1, 0, 0), Right = FVector(0, 1, 0);
    double Started = 0, StageStarted = 0;
    int32 Stage = -1;
    bool bCaptured = false, bDone = false, bPass = true, bBuilt = false;
};
FGallery G;

UWorld* World() { return G.Mode.IsValid() ? G.Mode->GetWorld() : nullptr; }
void Fail(const FString& Why) { G.bPass = false; UE_LOG(LogCireNewChampionsGallery, Error, TEXT("CIRE_NEW_CHAMPIONS_GALLERY_CHECK_FAIL %s"), *Why); }
void Finish()
{
    if (G.bDone) return;
    G.bDone = true;
    for (const FString& File : G.Captures) if (IFileManager::Get().FileSize(*File) < 1024) Fail(TEXT("capture not written: ") + File);
    UE_LOG(LogCireNewChampionsGallery, Display, TEXT("CIRE_NEW_CHAMPIONS_GALLERY_%s captures=%d directory=%s"), G.bPass ? TEXT("PASS") : TEXT("FAIL"), G.Captures.Num(), *G.Directory);
    FPlatformMisc::RequestExitWithStatus(false, G.bPass ? 0 : 1);
}
float FloorZ(const FVector& P)
{
    FHitResult Hit; FCollisionQueryParams Query(SCENE_QUERY_STAT(CireNewChampionsGalleryFloor), false);
    for (const auto& Actor : G.Scene) if (Actor.IsValid()) Query.AddIgnoredActor(Actor.Get());
    return World()->LineTraceSingleByObjectType(Hit, P + FVector(0, 0, 2000), P - FVector(0, 0, 4000), FCollisionObjectQueryParams(ECC_WorldStatic), Query) ? Hit.ImpactPoint.Z : P.Z;
}
FVector Ground(float Along, float Side) { const FVector P = G.Hold + G.Forward * Along + G.Right * Side; return FVector(P.X, P.Y, FloorZ(P)); }
void Look(const FVector& Eye, const FVector& Target, float Fov = 55.f)
{
    if (!G.Camera.IsValid()) return;
    G.Camera->SetActorLocation(Eye); G.Camera->SetActorRotation((Target - Eye).Rotation());
    G.Camera->GetCameraComponent()->SetFieldOfView(Fov);
}
void Label(const FVector& At, const FString& Text, const FColor& Color, float Size)
{
    if (!G.Camera.IsValid()) return;
    if (auto* L = World()->SpawnActor<ATextRenderActor>(At, (G.Camera->GetActorLocation() - At).GetSafeNormal2D().Rotation()))
    {
        L->GetTextRender()->SetText(FText::FromString(Text)); L->GetTextRender()->SetWorldSize(Size);
        L->GetTextRender()->SetTextRenderColor(Color); L->GetTextRender()->SetHorizontalAlignment(EHTA_Center);
        G.Scene.Add(L);
    }
}
void ClearScene()
{
    for (TActorIterator<ACireConstruct> It(World()); It; ++It) if (It->HasAuthority()) It->Destroy();
    for (auto& Actor : G.Scene) if (Actor.IsValid()) Actor->Destroy();
    G.Scene.Reset(); G.Heroes.Reset(); G.Focus.Reset();
    if (G.Mode.IsValid())
    {
        G.Mode->Monsters.RemoveAll([](ACireMonster* M) { return !IsValid(M) || M->IsActorBeingDestroyed(); });
        G.Mode->Heroes.RemoveAll([](ACireHero* H) { return !IsValid(H) || H->IsActorBeingDestroyed(); });
    }
}
ACireHero* Hero(const TCHAR* Profile, float Along, float Side, float Yaw)
{
    FActorSpawnParameters Params; Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    const FVector At = Ground(Along, Side);
    auto* H = World()->SpawnActor<ACireHero>(At + FVector(0, 0, 200), FRotator(0, Yaw, 0), Params);
    if (!H) { Fail(FString(TEXT("spawn ")) + Profile); return nullptr; }
    G.Scene.Add(H); H->TeamId = 0;
    if (!H->DraftProfile(Profile)) Fail(FString(TEXT("draft ")) + Profile);
    H->Health = H->MaxHealth = 1.e6f; H->Mana = H->MaxMana = 1.e5f; H->Energy = 100; H->Offers.Reset();
    H->SetActorLocation(FVector(At.X, At.Y, At.Z + H->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 2));
    H->SetActorRotation(FRotator(0, Yaw, 0));
    H->GetMesh()->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
    G.Mode->Heroes.Add(H); G.Heroes.Add(Profile, H);
    return H;
}
ACireMonster* Monster(FName Id, float Along, float Side, bool bThink = true)
{
    FActorSpawnParameters Params; Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
    const FVector At = Ground(Along, Side);
    auto* M = World()->SpawnActor<ACireMonster>(At + FVector(0, 0, 140), (-G.Forward).Rotation(), Params);
    if (!M) return nullptr;
    G.Scene.Add(M); M->Lane = 0; G.Mode->Monsters.Add(M);
    CireNPCCombat::ConfigureArchetype(M, Id, 4, 0, 1);
    M->Health = M->MaxHealth = FMath::Max(M->MaxHealth, 2500.f);
    M->SetActorLocation(FVector(At.X, At.Y, At.Z + M->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 2));
    M->GetMesh()->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
    if (!bThink) { M->SetActorTickEnabled(false); M->GetCharacterMovement()->DisableMovement(); }
    return M;
}
bool CastAt(ACireHero* H, const TCHAR* Id, const FVector& Aim, AActor* Target = nullptr)
{
    if (!H) return false;
    H->Skills = {Id}; H->Cooldowns = {0}; H->GlobalCooldown = 0; H->Mana = H->MaxMana; H->Energy = 100;
    H->Target = Target; H->bHasCastAim = true; H->CastAimPoint = Aim;
    const bool bCast = CireSignatureSkills::Cast(H, 0, Id);
    H->bHasCastAim = false;
    UE_LOG(LogCireNewChampionsGallery, Display, TEXT("CIRE_NEW_CHAMPIONS_GALLERY_CAST %s %s %s"), *H->ChampionProfileId, Id, bCast ? TEXT("ok") : *H->Notice);
    if (!bCast) Fail(FString::Printf(TEXT("%s could not cast %s: %s"), *H->ChampionProfileId, Id, *H->Notice));
    return bCast;
}
// pets: the owner's companion, placed for the shot (its own AI takes over after).
ACirePet* Companion(ACireHero* H, float Along, float Side)
{
    ACirePet* Pet = H ? CirePets::Summon(H) : nullptr;
    if (!Pet) { Fail(TEXT("companion did not spawn")); return nullptr; }
    const FVector At = Ground(Along, Side);
    Pet->SetActorLocation(FVector(At.X, At.Y, At.Z + Pet->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 2));
    Pet->SetActorRotation((-G.Forward).Rotation());
    Pet->GetMesh()->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
    G.Scene.Add(Pet);
    return Pet;
}
void Fight(ACireHero* H, AActor* Target) { if (H && Target) { H->Target = Target; H->bAutoAttack = true; } }
void GameplayCamera(ACireHero* H, float Boom = 950.f, float Pitch = -30.f, float YawOffset = 0.f)
{
    if (!H) return;
    const FVector Pivot = H->GetActorLocation() + FVector(0, 0, 70);
    // Behind the champion, looking down the lane at the enemy side (monsters come from -Forward).
    const FRotator View(Pitch, (-G.Forward).Rotation().Yaw + YawOffset, 0);
    Look(Pivot - View.Vector() * Boom, Pivot - G.Forward * 250, 80.f);
}

// ---- stages ------------------------------------------------------------------------------------------
void EnterStage(const FStage& S)
{
    const FString& N = S.Name;
    if (!S.bKeepScene) ClearScene();
    const float Face = (-G.Forward).Rotation().Yaw; // toward the monsters' side
    if (N == TEXT("lineup"))
    {
        const TCHAR* Ids[] = {TEXT("gunblade"), TEXT("witch_slayer"), TEXT("huntress"), TEXT("aetheri_artificer"), TEXT("aetheri_warden")};
        const float CamYaw = (G.Forward).Rotation().Yaw;
        for (int32 I = 0; I < 5; ++I)
            if (auto* H = Hero(Ids[I], 0, (I - 2) * 300.f, CamYaw + 180.f))
                if (const auto* P = H->ChampionProfile()) Label(H->GetActorLocation() + FVector(0, 0, 190), P->DisplayName, FColor(246, 219, 155), 22);
        const FVector C = Ground(0, 0) + FVector(0, 0, 110);
        Look(C + G.Forward * -1250 + FVector(0, 0, 180), C, 60);
        // Labels face the camera.
        for (auto& A : G.Scene) if (auto* T = Cast<ATextRenderActor>(A.Get())) T->SetActorRotation((G.Camera->GetActorLocation() - T->GetActorLocation()).GetSafeNormal2D().Rotation());
    }
    else if (N == TEXT("huntress_close"))
    {
        // pets: the Huntress on foot with her sabercat companion Ashfang beside her.
        auto* H = Hero(TEXT("huntress"), 0, 0, Face);
        if (H) { Companion(H, -40, 190); const FVector C = H->GetActorLocation() + G.Right * 90; Look(C + G.Right * 360 + G.Forward * -520 + FVector(0, 0, 110), C + FVector(0, 0, 5), 50); }
    }
    else if (N == TEXT("gunblade_close"))
    {
        auto* H = Hero(TEXT("gunblade"), 0, 0, Face);
        if (H) { const FVector C = H->GetActorLocation(); Look(C + (-G.Forward) * 330 + G.Right * 140 + FVector(0, 0, 70), C + FVector(0, 0, 45), 50); }
    }
    else if (N == TEXT("aetheri_close"))
    {
        auto* A = Hero(TEXT("aetheri_artificer"), 0, -130, Face);
        auto* W = Hero(TEXT("aetheri_warden"), 0, 130, Face);
        if (A && W) { const FVector C = (A->GetActorLocation() + W->GetActorLocation()) * .5f; Look(C + (-G.Forward) * 520 + FVector(0, 0, 90), C + FVector(0, 0, 40), 50); }
    }
    else if (N == TEXT("witch_close"))
    {
        auto* H = Hero(TEXT("witch_slayer"), 0, 0, Face);
        if (H) { const FVector C = H->GetActorLocation(); Look(C + (-G.Forward) * 330 + G.Right * -120 + FVector(0, 0, 70), C + FVector(0, 0, 45), 50); }
    }
    else if (N == TEXT("combat_gunblade"))
    {
        auto* H = Hero(TEXT("gunblade"), 0, 0, Face);
        ACireMonster* First = Monster(TEXT("hollow_infantry"), -700, -80);
        Monster(TEXT("hollow_infantry"), -780, 120); Monster(TEXT("blight_caster"), -900, 0);
        if (H && First) { CastAt(H, TEXT("hex_mark"), First->GetActorLocation(), First); CastAt(H, TEXT("powder_flask"), Ground(-760, 0), First); Fight(H, First); }
        G.Focus = H; GameplayCamera(H);
    }
    else if (N == TEXT("combat_gunblade_melee"))
    {
        auto* H = Hero(TEXT("gunblade"), 0, 0, Face);
        ACireMonster* Close = Monster(TEXT("hollow_infantry"), -190, 0, false);
        Monster(TEXT("hollow_infantry"), -240, 170, false);
        if (H && Close) { CastAt(H, TEXT("blade_flurry"), H->GetActorLocation(), Close); Fight(H, Close); }
        G.Focus = H; if (H) { const FVector C = H->GetActorLocation(); Look(C + G.Right * 480 + (-G.Forward) * 180 + FVector(0, 0, 140), C + (-G.Forward) * 100, 60); }
    }
    else if (N == TEXT("combat_witch_slayer"))
    {
        auto* H = Hero(TEXT("witch_slayer"), 0, 0, Face);
        ACireMonster* Caster = Monster(TEXT("blight_caster"), -650, 0);
        Monster(TEXT("hollow_infantry"), -560, -160); Monster(TEXT("hollow_infantry"), -600, 170);
        if (H && Caster)
        {
            CastAt(H, TEXT("witchfinders_mark"), Caster->GetActorLocation(), Caster);
            CastAt(H, TEXT("spirit_lantern"), Ground(-300, 150));
            CastAt(H, TEXT("hexbane_judgment"), Ground(-600, 0), Caster);
            Fight(H, Caster);
        }
        G.Focus = H; GameplayCamera(H);
    }
    else if (N == TEXT("combat_witch_blunderbuss"))
    {
        auto* H = Hero(TEXT("witch_slayer"), 0, 0, Face);
        ACireMonster* Near = Monster(TEXT("hollow_infantry"), -380, 0, false);
        Monster(TEXT("hollow_infantry"), -420, -150, false); Monster(TEXT("blight_caster"), -450, 160, false);
        if (H && Near) CastAt(H, TEXT("arcane_blunderbuss"), Near->GetActorLocation(), Near);
        G.Focus = H; GameplayCamera(H, 800, -34);
    }
    else if (N == TEXT("combat_huntress"))
    {
        auto* H = Hero(TEXT("huntress"), 0, 0, Face);
        ACireMonster* A = Monster(TEXT("hollow_infantry"), -700, -200, false);
        ACireMonster* B = Monster(TEXT("hollow_infantry"), -850, 60, false);
        Monster(TEXT("hollow_infantry"), -760, 280, false); Monster(TEXT("ironbound_bruiser"), -950, -60, false);
        if (H && A && B)
        {
            Companion(H, -120, 160);
            CastAt(H, TEXT("owl_scout"), Ground(-800, 0)); CastAt(H, TEXT("bouncing_glaive"), A->GetActorLocation(), A);
            CastAt(H, TEXT("sabercat_pounce"), A->GetActorLocation(), A); Fight(H, B);
        }
        G.Focus = H; GameplayCamera(H);
    }
    else if (N == TEXT("combat_huntress_pet"))
    {
        // pets: Ashfang mauls while the Huntress throws glaives (the maul is her command).
        auto* H = Hero(TEXT("huntress"), 0, 0, Face);
        ACireMonster* A = Monster(TEXT("hollow_infantry"), -520, 60, false);
        Monster(TEXT("hollow_infantry"), -640, -150, false); Monster(TEXT("ironbound_bruiser"), -720, 200, false);
        if (H && A) { if (ACirePet* Pet = Companion(H, -380, 60)) CirePets::Command(H, ECirePetCommand::Attack, A); CastAt(H, TEXT("sabercat_maul"), A->GetActorLocation(), A); Fight(H, A); }
        G.Focus = H; if (H) { const FVector C = Ground(-330, 40); Look(C + G.Right * 620 + G.Forward * 260 + FVector(0, 0, 260), C + FVector(0, 0, 30), 55); }
    }
    else if (N == TEXT("pet_roar"))
    {
        // pets: the special command, Dread Roar, slows and taunts the pack onto the cat.
        auto* H = Hero(TEXT("huntress"), 0, 0, Face);
        for (int32 I = 0; I < 3; ++I) Monster(TEXT("hollow_infantry"), -420 - I * 40.f, (I - 1) * 170.f, false);
        if (H) { if (Companion(H, -300, 0)) { CirePets::Command(H, ECirePetCommand::Special, nullptr); UE_LOG(LogCireNewChampionsGallery, Display, TEXT("CIRE_NEW_CHAMPIONS_GALLERY_CAST pet special %s"), *H->Notice); } }
        G.Focus = H; if (H) { const FVector C = Ground(-330, 0); Look(C + G.Right * 560 + G.Forward * 420 + FVector(0, 0, 300), C + FVector(0, 0, 30), 60); }
    }
    else if (N.StartsWith(TEXT("hud_pet")))
    {
        // pets: the companion frame in the real HUD while the player's Huntress fights (possessed so the HUD draws her).
        ACireHero* H = G.Heroes.FindRef(TEXT("huntress")).Get();
        if (!H || N == TEXT("hud_pet_frame"))
        {
            ClearScene();
            H = Hero(TEXT("huntress"), 0, 0, Face);
            ACireMonster* A = Monster(TEXT("hollow_infantry"), -520, 40, false);
            Monster(TEXT("hollow_infantry"), -600, -160, false);
            if (H && A && G.Controller.IsValid())
            {
                if (APawn* Old = G.Controller->GetPawn(); Old && Old != H) G.Controller->UnPossess();
                G.Controller->Possess(H); G.Controller->SetViewTarget(G.Camera.Get());
                if (G.Controller->GetHUD()) G.Controller->GetHUD()->bShowHUD = true;
                H->Health = H->MaxHealth = 2400; H->Level = 8; H->Agility = 44;
                if (Companion(H, -380, 40)) { CirePets::Command(H, ECirePetCommand::StanceAggressive, nullptr); CirePets::Command(H, ECirePetCommand::Attack, A); }
                Fight(H, A);
            }
        }
        else if (N == TEXT("hud_pet_command"))
        {
            CirePets::Command(H, ECirePetCommand::Special, nullptr);
            CirePets::Command(H, ECirePetCommand::Stay, nullptr);
            UE_LOG(LogCireNewChampionsGallery, Display, TEXT("CIRE_NEW_CHAMPIONS_GALLERY_CAST pet commands: %s"), *H->Notice);
        }
        else if (N == TEXT("hud_pet_fallen"))
        {
            if (ACirePet* Pet = CirePets::PetOf(H)) Pet->TakeDamage(Pet->MaxHealth * 10.f, FDamageEvent(), nullptr, G.Mode->Monsters.Num() ? G.Mode->Monsters[0] : nullptr);
        }
        G.Focus = H; if (H) GameplayCamera(H, 900, -30);
    }
    else if (N == TEXT("combat_huntress_storm"))
    {
        auto* H = Hero(TEXT("huntress"), 0, 0, Face);
        for (int32 I = 0; I < 4; ++I) Monster(TEXT("hollow_infantry"), -250 - I * 60.f, (I - 1.5f) * 140.f, false);
        if (H) CastAt(H, TEXT("glaive_storm"), H->GetActorLocation());
        G.Focus = H; GameplayCamera(H, 1000, -40);
    }
    else if (N == TEXT("constructs_place"))
    {
        auto* A = Hero(TEXT("aetheri_artificer"), 0, -150, Face);
        auto* W = Hero(TEXT("aetheri_warden"), 0, 150, Face);
        for (int32 I = 0; I < 4; ++I) Monster(I == 3 ? TEXT("ironbound_bruiser") : TEXT("hollow_infantry"), -1300 - I * 90.f, (I - 1.5f) * 170.f);
        if (A && W)
        {
            CastAt(A, TEXT("photon_turret"), Ground(-420, -260));
            CastAt(A, TEXT("disruption_pylon"), Ground(-700, 0));
            CastAt(A, TEXT("arc_mine"), Ground(-560, -120));
            CastAt(W, TEXT("aegis_pylon"), Ground(-120, 0));
            CastAt(W, TEXT("haste_pylon"), Ground(80, 320));
            CastAt(W, TEXT("gravity_pylon"), Ground(-740, 250));
            CastAt(W, TEXT("stasis_snare"), Ground(-560, 180));
            CastAt(A, TEXT("skitter_swarm"), Ground(-300, 60));
        }
        G.Focus = A; if (A) { const FVector C = Ground(-450, 0); Look(C + (-G.Forward) * -1100 + G.Right * 520 + FVector(0, 0, 820), C, 70); }
    }
    else if (N == TEXT("constructs_close"))
    {
        const FVector C = Ground(-420, -130);
        Look(C + G.Forward * 420 + G.Right * -380 + FVector(0, 0, 260), C + FVector(0, 0, 60), 55);
    }
    else if (N == TEXT("constructs_fight"))
    {
        if (auto* A = G.Heroes.FindRef(TEXT("aetheri_artificer")).Get()) GameplayCamera(A, 1250, -38);
    }
    else if (N == TEXT("aetheri_wave"))
    {
        auto* Gb = Hero(TEXT("gunblade"), 0, -120, Face);
        auto* Wd = Hero(TEXT("aetheri_warden"), 30, 150, Face);
        const TCHAR* Units[] = {TEXT("aetheri_phaseblade"), TEXT("aetheri_phaseblade"), TEXT("aetheri_warframe"), TEXT("aetheri_bulwark"),
            TEXT("aetheri_engineer"), TEXT("aetheri_lancer"), TEXT("skitter_drone"), TEXT("aetheri_hierarch")};
        ACireMonster* Engineer = nullptr; ACireMonster* Hierarch = nullptr;
        for (int32 I = 0; I < UE_ARRAY_COUNT(Units); ++I)
        {
            ACireMonster* M = Monster(Units[I], -900 - (I / 3) * 220.f, ((I % 3) - 1) * 220.f);
            if (M && FString(Units[I]) == TEXT("aetheri_engineer")) Engineer = M;
            if (M && FString(Units[I]) == TEXT("aetheri_hierarch")) Hierarch = M;
        }
        const auto Release = [](ACireMonster* M, const TCHAR* Ability, const FVector& Aim)
        {
            const FCireNPCArchetype* A = M && M->NPCState ? CireNPCArchetypes::Find(M->NPCState->ArchetypeId) : nullptr;
            const FCireNPCAbility* Ab = A ? A->FindAbility(Ability) : nullptr;
            const int32 Placed = Ab ? CireRaces::OnAbilityReleased(M, *Ab, Aim) : 0;
            UE_LOG(LogCireNewChampionsGallery, Display, TEXT("CIRE_NEW_CHAMPIONS_GALLERY_DEPLOY %s placed=%d"), Ability, Placed);
            if (Placed <= 0) Fail(FString(TEXT("Aetheri deploy failed: ")) + Ability);
        };
        if (Engineer) { Release(Engineer, TEXT("aether_deploy_turret"), Ground(-640, -250)); Release(Engineer, TEXT("aether_deploy_skitters"), Ground(-600, 60)); }
        if (Hierarch) { Release(Hierarch, TEXT("aether_warp_pylons"), Ground(-1050, 200)); Release(Hierarch, TEXT("aether_gravity_field"), Ground(-40, 0)); }
        if (Gb) Fight(Gb, Engineer);
        G.Focus = Gb; if (Gb) GameplayCamera(Gb, 1300, -36);
    }
    else if (N == TEXT("aetheri_wave_fight"))
    {
        if (auto* Gb = G.Heroes.FindRef(TEXT("gunblade")).Get()) GameplayCamera(Gb, 1200, -34, 18);
    }
}

void Diagnose()
{
    for (const auto& Pair : G.Heroes)
    {
        ACireHero* H = Pair.Value.Get(); if (!H) continue;
        USkeletalMeshComponent* Mesh = H->GetMesh();
        FString Line = FString::Printf(TEXT("CIRE_NEW_CHAMPIONS_GALLERY_POSE %s mesh=%s anim=%s"), *Pair.Key,
            Mesh->GetSkeletalMeshAsset() ? *Mesh->GetSkeletalMeshAsset()->GetName() : TEXT("none"), Mesh->GetAnimInstance() ? *Mesh->GetAnimInstance()->GetClass()->GetName() : TEXT("none"));
        const float Feet = static_cast<float>(H->GetActorLocation().Z - H->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
        if (Mesh->GetBoneIndex(TEXT("hand_r")) != INDEX_NONE) Line += FString::Printf(TEXT(" hand_r_above_feet=%.0f hand_l_above_feet=%.0f"), Mesh->GetBoneLocation(TEXT("hand_r")).Z - Feet, Mesh->GetBoneLocation(TEXT("hand_l")).Z - Feet);
        if (auto* C = Cast<UCireCombatAnimInstance>(Mesh->GetAnimInstance()))
            Line += FString::Printf(TEXT(" attackW=%.2f seq=%s handsW=%.2f/%.2f two=%d carry=%d twoW=%.2f air=%.2f roll=%.2f seat=%.2f twist=%.1f"), C->AttackWeight, C->AttackSequence ? *C->AttackSequence->GetName() : TEXT("none"),
                C->Hands.Weight[0], C->Hands.Weight[1], C->Hands.bTwoHand ? 1 : 0, C->Hands.bCarry ? 1 : 0, C->Hands.TwoHandWeight, C->AirWeight, C->RollProgress, C->SeatWeight, C->SpineTwist);
        if (auto* Single = Mesh->GetSingleNodeInstance())
            if (auto* Blend = Cast<UBlendSpace>(Single->GetAnimationAsset()))
                for (const FBlendSample& Sample : Blend->GetBlendSamples())
                    if (auto* Seq = Cast<UAnimSequence>(Sample.Animation.Get())) Line += FString::Printf(TEXT(" %s:%d"), *Seq->GetName().Right(12), Seq->IsCompressedDataValid() ? 1 : 0);
        UE_LOG(LogCireNewChampionsGallery, Display, TEXT("%s"), *Line);
    }
    for (TActorIterator<ACirePet> It(World()); It; ++It)
    {
        const UCireCreatureArt* Body = It->ChampionArt ? It->ChampionArt->GetCreature() : nullptr;
        UE_LOG(LogCireNewChampionsGallery, Display, TEXT("CIRE_NEW_CHAMPIONS_GALLERY_PET %s kind=%s mesh=%s hp=%.0f/%.0f order=%d stance=%d dead=%d leaping=%d"), *It->HeroName,
            Body ? *Body->GetKind() : TEXT("none"), It->GetMesh()->GetSkeletalMeshAsset() ? *It->GetMesh()->GetSkeletalMeshAsset()->GetName() : TEXT("none"),
            It->Health, It->MaxHealth, static_cast<int32>(It->Order), static_cast<int32>(It->Stance), It->bDead ? 1 : 0, It->IsLeaping() ? 1 : 0);
    }
}
void Capture(const FStage& S)
{
    Diagnose();
    const FString File = FPaths::Combine(G.Directory, FString::Printf(TEXT("%02d_%s.png"), G.Stage + 1, *S.Name));
    FScreenshotRequest::RequestScreenshot(File, false, false, false, FIntRect(), true);
    G.Captures.Add(File);
    int32 Constructs = 0; for (TActorIterator<ACireConstruct> It(World()); It; ++It) if (!It->IsActorBeingDestroyed() && It->IsTech()) ++Constructs;
    UE_LOG(LogCireNewChampionsGallery, Display, TEXT("CIRE_NEW_CHAMPIONS_GALLERY_CAPTURE stage=%s constructs=%d file=%s"), *S.Name, Constructs, *File);
}

bool Build(ACireGameMode& Mode, ACireController& Controller)
{
    G.Controller = &Controller;
    if (auto* Player = Cast<ACireHero>(Controller.GetPawn()))
    { Player->TeamId = 0; Player->Draft(0); Player->SetActorHiddenInGame(true); Player->SetActorEnableCollision(false); Player->SetActorLocation(FVector(0, 0, -50000)); }
    Controller.SetIgnoreMoveInput(true); Controller.SetIgnoreLookInput(true); Controller.bShowMouseCursor = false;
    if (Controller.GetHUD()) Controller.GetHUD()->bShowHUD = false;
    for (auto* M : Mode.Monsters) if (IsValid(M)) M->Destroy();
    Mode.Monsters.Reset();
    UWorld* W = Mode.GetWorld();
    G.Hold = CireLanePath::PointAlongRoute(W, 0, .58f);
    const FVector Ahead = CireLanePath::PointAlongRoute(W, 0, .60f);
    G.Forward = (Ahead - G.Hold).GetSafeNormal2D(); if (G.Forward.IsNearlyZero()) G.Forward = FVector(-1, 0, 0);
    G.Right = FVector::CrossProduct(FVector::UpVector, G.Forward);
    G.Camera = W->SpawnActor<ACameraActor>();
    if (!G.Camera.IsValid()) return false;
    auto* Camera = G.Camera->GetCameraComponent();
    Camera->SetAspectRatio(16.f / 9.f); Camera->bConstrainAspectRatio = true;
    auto& Post = Camera->PostProcessSettings;
    Post.bOverride_MotionBlurAmount = true; Post.MotionBlurAmount = 0;
    Controller.SetViewTarget(G.Camera.Get());
    const FStage All[] = {{TEXT("lineup"), 3.f}, {TEXT("gunblade_close"), 2.5f}, {TEXT("witch_close"), 2.5f}, {TEXT("huntress_close"), 2.5f}, {TEXT("aetheri_close"), 2.5f},
        {TEXT("combat_gunblade"), .45f}, {TEXT("combat_gunblade_melee"), 1.1f}, {TEXT("combat_witch_blunderbuss"), .1f}, {TEXT("combat_witch_slayer"), .55f},
        {TEXT("combat_huntress"), .35f}, {TEXT("combat_huntress_pet"), 1.4f}, {TEXT("pet_roar"), .35f}, {TEXT("combat_huntress_storm"), 1.2f},
        {TEXT("constructs_place"), 1.4f}, {TEXT("constructs_close"), .6f, true}, {TEXT("constructs_fight"), 3.2f, true},
        {TEXT("aetheri_wave"), 1.6f}, {TEXT("aetheri_wave_fight"), 3.5f, true},
        {TEXT("hud_pet_frame"), 2.5f}, {TEXT("hud_pet_command"), .6f, true}, {TEXT("hud_pet_fallen"), 1.5f, true}};
    for (const FStage& S : All)
        if (G.Only.IsEmpty() || G.Only.ContainsByPredicate([&S](const FString& Prefix) { return S.Name.StartsWith(Prefix); })) G.Stages.Add(S);
    G.bBuilt = true;
    if (auto* Cat = LoadObject<USkeletalMesh>(nullptr, TEXT("/Game/Tripo/Champions/HuntressSabercat/CTS_Mount_HuntressSabercat.CTS_Mount_HuntressSabercat"), nullptr, LOAD_Quiet | LOAD_NoWarn))
    {
        FString Bones; const auto& Ref = Cat->GetRefSkeleton();
        for (int32 I = 0; I < Ref.GetNum(); ++I) Bones += Ref.GetBoneName(I).ToString() + TEXT("<") + (Ref.GetParentIndex(I) >= 0 ? Ref.GetBoneName(Ref.GetParentIndex(I)).ToString() : FString(TEXT("-"))) + TEXT(" ");
        UE_LOG(LogCireNewChampionsGallery, Display, TEXT("CIRE_NEW_CHAMPIONS_GALLERY_SABERCAT bones=%d %s"), Ref.GetNum(), *Bones);
    }
    UE_LOG(LogCireNewChampionsGallery, Display, TEXT("CIRE_NEW_CHAMPIONS_GALLERY_READY stages=%d hold=%s"), G.Stages.Num(), *G.Hold.ToString());
    return G.Stages.Num() > 0;
}
}

bool CireNewChampionsGallery::Initialize(ACireGameMode* Mode)
{
    G = FGallery();
    if (!FParse::Param(FCommandLine::Get(), TEXT("CireNewChampionsGallery"))) return false;
    G.Mode = Mode; G.Started = FPlatformTime::Seconds();
    FString Only;
    if (FParse::Value(FCommandLine::Get(), TEXT("CireNewChampionsGalleryOnly="), Only, false)) Only.ParseIntoArray(G.Only, TEXT(","), true);
    G.Directory = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("NewChampionsGallery"), FDateTime::UtcNow().ToString(TEXT("%Y%m%d-%H%M%S"))));
    if (!Mode || Mode->GetNetMode() != NM_Standalone || !IFileManager::Get().MakeDirectory(*G.Directory, true)) { Fail(TEXT("standalone match and capture directory")); Finish(); return true; }
    Mode->bBotsFilled = true; Mode->BotFillTimer = MAX_flt; Mode->WaveTimer = MAX_flt;
    return true;
}

bool CireNewChampionsGallery::Tick(ACireGameMode* Mode)
{
    if (G.Mode.Get() != Mode) return false;
    if (G.bDone) return true;
    const double Now = FPlatformTime::Seconds();
    if (Now - G.Started > 600) { Fail(TEXT("gallery exceeded 600 seconds")); Finish(); return true; }
    if (!G.bBuilt)
    {
        auto* Controller = Cast<ACireController>(Mode->GetWorld()->GetFirstPlayerController());
        if (Controller && Controller->GetPawn() && Controller->GetHUD() && !Build(*Mode, *Controller)) { Fail(TEXT("build")); Finish(); }
        return true;
    }
    // Let shaders and streamed textures settle before the first stage.
    if (G.Stage < 0 && GShaderCompilingManager && GShaderCompilingManager->GetNumRemainingJobs() > 0 && Now - G.Started < 240) return true;
    if (G.Stage < 0 || (G.bCaptured && Now - G.StageStarted > G.Stages[G.Stage].Settle + 1.0))
    {
        if (G.Stage + 1 >= G.Stages.Num()) { ClearScene(); Finish(); return true; }
        ++G.Stage; G.bCaptured = false; G.StageStarted = Now;
        EnterStage(G.Stages[G.Stage]);
        return true;
    }
    if (!G.bCaptured && Now - G.StageStarted >= G.Stages[G.Stage].Settle) { Capture(G.Stages[G.Stage]); G.bCaptured = true; }
    return true;
}
