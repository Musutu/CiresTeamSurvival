#include "CireChat.h"
#include "CireGame.h"
#include "Engine/World.h"
#include "CireInterfaceProbe.h"

bool UCireViewportClient::InputChar(FViewport* InViewport, int32 ControllerId, TCHAR InputCharacter) {
    auto* Controller = GetWorld() ? Cast<ACireController>(GetWorld()->GetFirstPlayerController()) : nullptr;
    if (Controller && Controller->bChatInput) {
        Controller->AppendChatCharacter(InputCharacter);
        return true;
    }
    if (Controller && Controller->bDraftSearch) { // champion-select search box
        Controller->AppendDraftSearchCharacter(InputCharacter);
        return true;
    }
    return Super::InputChar(InViewport, ControllerId, InputCharacter);
}

void ACireController::BeginChat() { bChatInput = true; }
void ACireController::AppendDraftSearchCharacter(TCHAR InputCharacter) {
    if (!bDraftSearch || InputCharacter < 32 || InputCharacter == 127 || DraftSearch.Len() >= 24) return;
    DraftSearch.AppendChar(InputCharacter);
}
void ACireController::CancelChat() { bChatInput = false; ChatDraft.Empty(); }
void ACireController::AppendChatCharacter(TCHAR InputCharacter) {
    if (!bChatInput || InputCharacter < 32 || InputCharacter == 127 || ChatDraft.Len() >= 180) return;
    ChatDraft.AppendChar(InputCharacter);
}
void ACireController::SubmitChat() {
    const FString Message = ChatDraft.TrimStartAndEnd();
    if (!Message.IsEmpty()) ServerSendChat(Message, bChatTeamOnly);
    CancelChat();
}
void ACireController::ServerSendChat_Implementation(const FString& Message, bool bTeamOnly) {
    const auto* Hero = Cast<ACireHero>(GetPawn());
    if (!Hero || Message.Len() > 180 || !GetWorld()) return;
    const double Now = GetWorld()->GetTimeSeconds();
    if (Now - LastChatTime < 0.75) return;
    FString Clean;
    for (TCHAR InputCharacter : Message) if (InputCharacter >= 32 && InputCharacter != 127) Clean.AppendChar(InputCharacter);
    Clean.TrimStartAndEndInline();
    if (Clean.IsEmpty()) return;
    LastChatTime = Now;
#if !UE_BUILD_SHIPPING
    CireInterfaceProbe::ObserveChat(this, Clean);
#endif
    FCireChatMessage Entry;
    Entry.Sender = Hero->HeroName;
    Entry.Text = Clean;
    Entry.bTeamOnly = bTeamOnly;
    Entry.SenderTeam = Hero->TeamId;
    for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It) {
        auto* Recipient = Cast<ACireController>(It->Get());
        const auto* Other = Recipient ? Cast<ACireHero>(Recipient->GetPawn()) : nullptr;
        if (Other && (!bTeamOnly || Other->TeamId == Hero->TeamId)) Recipient->ClientChatMessage(Entry);
    }
}
void ACireController::ClientChatMessage_Implementation(const FCireChatMessage& Message) {
    FCireChatMessage Entry = Message;
    Entry.TimeSeconds = GetWorld()->GetTimeSeconds();
    ChatMessages.Add(MoveTemp(Entry));
    if (ChatMessages.Num() > 80) ChatMessages.RemoveAt(0, ChatMessages.Num() - 80);
}
void ACireController::ClientCombatEvent_Implementation(const FCireCombatEvent& Event) {
    if (GetWorld()) CireCombat::AppendReceivedEvent(CombatEvents, CombatEventSequence, Event, GetWorld()->GetTimeSeconds());
}
void ACireController::SetFocusTarget(AActor* Actor) {
    const auto* Hero = Cast<ACireHero>(GetPawn());
    const auto* Other = Cast<ACireHero>(Actor);
    if (!Actor || Actor == FocusTarget) { FocusTarget = nullptr; return; }
    if (Hero && IsValid(Other) && (Other->TeamId == Hero->TeamId || Hero->IsHostile(Actor))) FocusTarget = Actor;
}
