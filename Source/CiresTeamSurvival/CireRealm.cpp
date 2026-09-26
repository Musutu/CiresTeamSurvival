#include "CireRealm.h"
#include "CireGame.h"
#include "CireLanePath.h"
#include "CireTownMap.h"
#include "CireConstruct.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"

namespace CireRealm {
bool CanObserve(const AActor* Observer, const AActor* Subject) {
    // A local replay has already disconnected from live play and may observe
    // both recorded realms. This never changes live network relevancy.
    if (Subject && Subject->GetWorld() && Subject->GetWorld()->IsPlayingReplay()) return true;
    const ACireHero* Viewer = Cast<ACireHero>(Observer);
    if (const auto* Controller = Cast<AController>(Observer)) Viewer = Cast<ACireHero>(Controller->GetPawn());
    if (!Viewer || !Subject || Viewer->TeamId < 0) return false;
    if (Viewer == Subject) return true;
    if (const auto* Construct=Cast<ACireConstruct>(Subject))return Construct->CanObserve(Viewer);
    const auto* State = Subject->GetWorld()->GetGameState<ACireGameState>();
    const auto* Mode = Subject->GetWorld()->GetAuthGameMode<ACireGameMode>();
    const bool bArena = Mode ? Mode->Clock.Phase() == Cires::MatchPhase::Arena : State && State->Phase == 2;
    if (const auto* Hero = Cast<ACireHero>(Subject)) return Hero->TeamId >= 0 && (bArena || Hero->TeamId == Viewer->TeamId);
    if (const auto* Monster = Cast<ACireMonster>(Subject)) return !bArena && Monster->Lane == Viewer->TeamId;
    return false;
}
void UpdateVisibility(AActor* Subject) {
    if (!Subject || !Subject->GetWorld()) return;
    CireTownMap::ApplyActorRealm(Subject); // medieval-kingdom: lit by its own realm's sun
    // The server maintains physical realm boundaries as well as damage restrictions.
    if (auto* Hero = Cast<ACireHero>(Subject); Hero && Hero->HasAuthority() && Hero->TeamId >= 0) {
        auto* Mode = Hero->GetWorld()->GetAuthGameMode<ACireGameMode>();
        if (Mode && Mode->Clock.Phase() != Cires::MatchPhase::Arena) {
            const FVector Location = Hero->GetActorLocation();
            if (!CireLanePath::Contains(Hero->GetWorld(),Hero->TeamId,Location)) {
                FVector Safe=CireLanePath::ClampToLane(Hero->GetWorld(),Hero->TeamId,Location,80);
                Safe.Z=FMath::Max(CireTownMap::Ground(Hero->GetWorld(),FVector2D(Safe))+100.,Location.Z); // medieval-kingdom: pack town ground
                Hero->GetCharacterMovement()->StopMovementImmediately();
                Hero->SetActorLocation(Safe, false, nullptr, ETeleportType::TeleportPhysics);
            }
        }
    }
    if (Subject->GetNetMode() == NM_DedicatedServer) return;
    const auto* Local = Subject->GetWorld()->GetFirstPlayerController();
    if (!Local || !Local->IsLocalController()) return;
    const bool bVisible = CanObserve(Local, Subject);
    if (auto* Character = Cast<ACharacter>(Subject)) Character->GetMesh()->SetVisibility(bVisible, true);
}
}

bool ACireHero::IsNetRelevantFor(const AActor* RealViewer, const AActor* ViewTarget, const FVector& SrcLocation) const {
    return RealViewer == GetController() || CireRealm::CanObserve(RealViewer ? RealViewer : ViewTarget, this);
}
bool ACireMonster::IsNetRelevantFor(const AActor* RealViewer, const AActor* ViewTarget, const FVector& SrcLocation) const {
    return CireRealm::CanObserve(RealViewer ? RealViewer : ViewTarget, this);
}
