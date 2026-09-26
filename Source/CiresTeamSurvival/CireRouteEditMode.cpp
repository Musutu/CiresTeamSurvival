// dev-route-tools: the clean map layout edit mode (-CireRouteEdit). See CireRouteEditMode.h.
#include "CireRouteEditMode.h"
#include "CireGame.h"
#include "CireHUD.h"
#include "CireLanePath.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

DEFINE_LOG_CATEGORY_STATIC(LogCireRouteEdit, Log, All);

bool CireRouteEditMode::IsActive()
{
#if UE_BUILD_SHIPPING
    return false;
#else
    static const bool bActive = FParse::Param(FCommandLine::Get(), TEXT("CireRouteEdit")) || FParse::Param(FCommandLine::Get(), TEXT("CireLayoutGallery"));
    return bActive;
#endif
}

bool CireRouteEditMode::InitializeServer(ACireGameMode* Mode)
{
    if (!IsActive() || !Mode) return false;
    // Nothing of the match starts: no first wave, no bots, no draft timer, no packs.
    Mode->WaveTimer = 1.e9f; Mode->bBotsFilled = true; Mode->BotFillTimer = 1.e9f; Mode->DraftPickSeconds = 0.f;
    if (auto* State = Mode->GetGameState<ACireGameState>())
    {
        State->NextWaveSeconds = -1; State->SecondsLeft = -1;
        State->Announcement = TEXT("Map layout editor: the game is paused; walk the town and place markers.");
    }
    UE_LOG(LogCireRouteEdit, Display, TEXT("CIRE_ROUTE_EDIT_MODE active: waves, spawns, draft, shop, economy, prep and win/lose are dormant"));
    return true;
}

bool CireRouteEditMode::TickServer(ACireGameMode* Mode, float)
{
    if (!IsActive() || !Mode) return false;
    Mode->WaveTimer = 1.e9f; Mode->bBotsFilled = true;
    // Whatever slipped in stays out: no monsters, no bots.
    for (ACireMonster* M : Mode->Monsters) if (IsValid(M)) M->Destroy();
    Mode->Monsters.Reset();
    for (ACireHero* H : Mode->Heroes)
    {
        if (!IsValid(H)) continue;
        if (H->bBot) { H->Destroy(); continue; }
        if (!H->bDrafted) { H->Draft(0); H->HeroName = TEXT("Map Editor"); }
        H->Offers.Reset();
        H->SetCanBeDamaged(false);
        if (H->Health < H->MaxHealth) H->Health = H->MaxHealth;
        if (APlayerController* PC = Cast<APlayerController>(H->GetController()))
            if (ACireHUD* HUD = Cast<ACireHUD>(PC->GetHUD()))
            {
                HUD->SetSkillOfferOpen(false);
                if (!HUD->IsLayoutEditorOpen()) HUD->OpenLayoutEditor(true);
            }
    }
    Mode->Heroes.RemoveAll([](ACireHero* H) { return !IsValid(H) || H->IsActorBeingDestroyed(); });
#if !UE_BUILD_SHIPPING
    CireLayoutGallery::Tick(Mode);
#endif
    return true;
}

bool CireRouteEditMode::TeleportTo(ACireHero* Hero, int32 Realm, const FVector2D& Local, float Yaw)
{
    if (!IsValid(Hero) || !Hero->HasAuthority()) return false;
    Realm = FMath::Clamp(Realm, 0, 1);
    FVector Target = CireLanePath::ToWorld(Realm, Local, Hero->GetActorLocation().Z + 60.f);
    // Drop onto the ground under the spot (the pack town has hills; the procedural town is flat).
    FHitResult Hit; FCollisionQueryParams Params(TEXT("CireRouteEditTeleport"), false, Hero);
    if (Hero->GetWorld()->LineTraceSingleByChannel(Hit, Target + FVector(0, 0, 3000), Target - FVector(0, 0, 6000), ECC_Visibility, Params))
        Target.Z = Hit.ImpactPoint.Z + 100.f;
    Hero->TeamId = Realm; // the realm the champion stands in (realm visibility, minimap)
    Hero->SetActorLocationAndRotation(Target, FRotator(0, Yaw, 0), false, nullptr, ETeleportType::TeleportPhysics);
    if (Hero->GetCharacterMovement()) Hero->GetCharacterMovement()->StopMovementImmediately();
    Hero->ForceNetUpdate();
    return true;
}

bool CireRouteEditMode::SwitchRealm(ACireHero* Hero)
{
    if (!IsValid(Hero)) return false;
    const int32 From = FMath::Clamp(Hero->TeamId, 0, 1);
    return TeleportTo(Hero, 1 - From, CireLanePath::ToLocal(From, Hero->GetActorLocation()), Hero->GetActorRotation().Yaw);
}
