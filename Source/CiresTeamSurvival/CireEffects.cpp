#include "CireEffects.h"
#include "CireAuraVisuals.h"
#include "CireBuffs.h"
#include "CireGame.h"
#include "CireItems.h"
#include "CireNPCState.h"
#include "CireNPCArchetypes.h"
#include "Dom/JsonObject.h"
#include "Engine/World.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
TMap<FName, FCireEffectInfo> GRows;
TMap<FName, FCireEffectInfo> GMerged;
TSet<FName> GKindSet; // rows that set "kind" explicitly
bool GLoaded = false;

ECireControl ParseControl(const FString& S)
{
    const FString L = S.ToLower();
    if (L == TEXT("stun")) return ECireControl::Stun;
    if (L == TEXT("silence")) return ECireControl::Silence;
    if (L == TEXT("root")) return ECireControl::Root;
    if (L == TEXT("healcut") || L == TEXT("heal_cut") || L == TEXT("healingcut")) return ECireControl::HealCut;
    if (L == TEXT("taunt")) return ECireControl::Taunt;
    if (L == TEXT("fear")) return ECireControl::Fear;
    if (L == TEXT("disarm")) return ECireControl::Disarm;
    if (L == TEXT("slow")) return ECireControl::Slow;
    return ECireControl::None;
}
ECireDispel ParseDispel(const FString& S)
{
    const FString L = S.ToLower();
    if (L == TEXT("magic")) return ECireDispel::Magic;
    if (L == TEXT("poison")) return ECireDispel::Poison;
    if (L == TEXT("curse")) return ECireDispel::Curse;
    if (L == TEXT("disease")) return ECireDispel::Disease;
    if (L == TEXT("physical")) return ECireDispel::Physical;
    return ECireDispel::None;
}
ECireEffectKind ParseKind(const FString& S)
{
    const FString L = S.ToLower();
    if (L == TEXT("debuff")) return ECireEffectKind::Debuff;
    if (L == TEXT("stance")) return ECireEffectKind::Stance;
    if (L == TEXT("aura")) return ECireEffectKind::Aura;
    if (L == TEXT("passive")) return ECireEffectKind::Passive;
    return ECireEffectKind::Buff;
}
bool ParseRows(const TSharedPtr<FJsonObject>& Rows, TMap<FName, FCireEffectInfo>& Out, FString& Error)
{
    for (const auto& Pair : Rows->Values)
    {
        const TSharedPtr<FJsonObject>* Row = nullptr;
        if (!Pair.Value.IsValid() || !Pair.Value->TryGetObject(Row) || !Row) { Error = FString(TEXT("Row is not an object: ")) + Pair.Key; return false; }
        if (Pair.Key.IsEmpty() || Pair.Key.Len() > 64) { Error = TEXT("Bad effect id"); return false; }
        FCireEffectInfo I; I.Id = FName(*Pair.Key); I.bFromRegistry = true;
        FString Text;
        if ((*Row)->TryGetStringField(TEXT("type"), Text)) I.Dispel = ParseDispel(Text);
        if ((*Row)->TryGetStringField(TEXT("control"), Text)) I.Control = ParseControl(Text);
        if ((*Row)->TryGetStringField(TEXT("kind"), Text)) { I.Kind = ParseKind(Text); GKindSet.Add(I.Id); }
        (*Row)->TryGetStringField(TEXT("name"), I.Name);
        (*Row)->TryGetStringField(TEXT("line"), I.Line);
        bool bCallout = true; if ((*Row)->TryGetBoolField(TEXT("callout"), bCallout)) I.bCallout = bCallout;
        const TArray<TSharedPtr<FJsonValue>>* Mods = nullptr;
        if ((*Row)->TryGetArrayField(TEXT("mods"), Mods))
            for (const auto& V : *Mods)
            {
                const TSharedPtr<FJsonObject>* M = nullptr;
                if (!V->TryGetObject(M) || !M) { Error = FString(TEXT("Bad mod in ")) + Pair.Key; return false; }
                FCireStatMod Mod; double Number = 0;
                if (!(*M)->TryGetStringField(TEXT("stat"), Mod.Stat) || Mod.Stat.IsEmpty()) { Error = FString(TEXT("Mod without stat in ")) + Pair.Key; return false; }
                if ((*M)->TryGetNumberField(TEXT("value"), Number)) Mod.Value = static_cast<float>(Number);
                (*M)->TryGetStringField(TEXT("unit"), Mod.Unit);
                if ((*M)->TryGetNumberField(TEXT("duration"), Number)) Mod.Duration = static_cast<float>(Number);
                if (!FMath::IsFinite(Mod.Value) || !FMath::IsFinite(Mod.Duration)) { Error = FString(TEXT("Non-finite mod in ")) + Pair.Key; return false; }
                I.Mods.Add(Mod);
            }
        Out.Add(I.Id, I);
    }
    return true;
}
void EnsureLoaded() { if (!GLoaded) { FString Error; CireEffects::Reload(Error); GLoaded = true; } }
FCireEffectInfo Merge(FName Id)
{
    FCireEffectInfo Info;
    if (const FCireEffectInfo* Row = GRows.Find(Id)) Info = *Row;
    Info.Id = Id;
    if (const FCireAuraDef* Def = CireAuraData::Find(Id))
    {
        if (Info.Name.IsEmpty()) Info.Name = Def->Name;
        Info.School = Def->School;
        // BuffVisuals owns buff/debuff/stance/aura/passive; the registry may override.
        if (!GKindSet.Contains(Id)) Info.Kind = ParseKind(Def->Kind);
        if (Info.Line.IsEmpty()) Info.Line = Def->Source;
    }
    if (Info.Name.IsEmpty()) Info.Name = FName::NameToDisplayString(Id.ToString(), false);
    if (Info.Control != ECireControl::None && Info.Control != ECireControl::Taunt) Info.Kind = Info.Kind == ECireEffectKind::Buff ? ECireEffectKind::Debuff : Info.Kind;
    if (Info.Kind == ECireEffectKind::Passive || Info.Kind == ECireEffectKind::Aura) Info.bCallout = false;
    return Info;
}
const TCHAR* Minus = TEXT("−");
}

bool CireEffects::Parse(const FString& Json, TMap<FName, FCireEffectInfo>& Out, FString& Error)
{
    TSharedPtr<FJsonObject> Root;
    if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Root) || !Root.IsValid()) { Error = TEXT("Invalid JSON"); return false; }
    const TSharedPtr<FJsonObject>* Rows = nullptr;
    if (Root->TryGetObjectField(TEXT("effects"), Rows) && Rows && !ParseRows(*Rows, Out, Error)) return false;
    if (Root->TryGetObjectField(TEXT("buffModifiers"), Rows) && Rows && !ParseRows(*Rows, Out, Error)) return false;
    return true;
}
bool CireEffects::Reload(FString& Error)
{
    TMap<FName, FCireEffectInfo> Rows; GKindSet.Reset();
    for (const TCHAR* File : {TEXT("Data/BuffModifiers.json"), TEXT("Data/Abilities.json")})
    {
        FString Text; const FString Path = FPaths::Combine(FPaths::ProjectContentDir(), File);
        if (!FFileHelper::LoadFileToString(Text, *Path)) { if (FCString::Strstr(File, TEXT("BuffModifiers"))) { Error = TEXT("Missing BuffModifiers.json"); } continue; }
        if (Text.Len() > 2000000 || !Parse(Text, Rows, Error)) return false;
    }
    GRows = MoveTemp(Rows); GMerged.Reset(); GLoaded = true;
    return Error.IsEmpty();
}
bool CireEffects::HasRegistryRow(FName Id) { EnsureLoaded(); return GRows.Contains(Id); }
const FCireEffectInfo* CireEffects::Find(FName Id)
{
    EnsureLoaded();
    if (Id.IsNone()) return nullptr;
    if (const FCireEffectInfo* Hit = GMerged.Find(Id)) return Hit;
    if (!GRows.Contains(Id) && !CireAuraData::Find(Id)) return nullptr;
    return &GMerged.Add(Id, Merge(Id));
}
FString CireEffects::DurationText(float S)
{
    if (!FMath::IsFinite(S) || S < 0.f) return FString();
    if (S >= 60.f) return FString::Printf(TEXT("%dm"), FMath::CeilToInt(S / 60.f));
    if (S < 10.f && FMath::Abs(S - FMath::RoundToFloat(S)) > .05f) return FString::Printf(TEXT("%.1fs"), S);
    return FString::Printf(TEXT("%ds"), FMath::Max(1, FMath::RoundToInt(S)));
}
FString CireEffects::FormatMod(const FCireStatMod& M)
{
    FString Out = M.Stat;
    if (M.Value != 0.f)
    {
        const float A = FMath::Abs(M.Value);
        const FString Number = FMath::IsNearlyEqual(A, FMath::RoundToFloat(A)) ? FString::FromInt(FMath::RoundToInt(A)) : FString::Printf(TEXT("%.1f"), A);
        Out += FString(TEXT(" ")) + FString(M.Value > 0 ? TEXT("+") : Minus) + Number + M.Unit;
    }
    if (M.Duration > 0.f) Out += FString(TEXT(" (")) + DurationText(M.Duration) + TEXT(")");
    return Out;
}
FString CireEffects::ControlWord(ECireControl C)
{
    switch (C)
    {
    case ECireControl::Stun: return TEXT("STUNNED");
    case ECireControl::Silence: return TEXT("SILENCED");
    case ECireControl::Root: return TEXT("ROOTED");
    case ECireControl::HealCut: return TEXT("HEALING CUT");
    case ECireControl::Fear: return TEXT("FEARED");
    case ECireControl::Disarm: return TEXT("DISARMED");
    case ECireControl::Taunt: return TEXT("TAUNTED");
    case ECireControl::Slow: return TEXT("SLOWED");
    default: return FString();
    }
}
FString CireEffects::ControlBadge(ECireControl C)
{
    switch (C)
    {
    case ECireControl::Stun: return TEXT("STUN");
    case ECireControl::Silence: return TEXT("SILENCE");
    case ECireControl::Root: return TEXT("ROOT");
    case ECireControl::HealCut: return TEXT("HEAL-CUT");
    case ECireControl::Fear: return TEXT("FEAR");
    case ECireControl::Disarm: return TEXT("DISARM");
    case ECireControl::Taunt: return TEXT("TAUNT");
    case ECireControl::Slow: return TEXT("SLOW");
    default: return FString();
    }
}
FString CireEffects::FormatControl(ECireControl C, float Remaining)
{
    const FString Word = ControlWord(C);
    if (Word.IsEmpty()) return Word;
    const FString Pretty = Word.Left(1) + Word.Mid(1).ToLower();
    return Remaining > 0.f ? Pretty + FString(TEXT(" ")) + DurationText(Remaining) : Pretty;
}
FString CireEffects::Symbols(const FCireEffectInfo& Info, float Remaining)
{
    TArray<FString> Parts;
    if (Info.Control != ECireControl::None && Info.Control != ECireControl::Slow) Parts.Add(FormatControl(Info.Control, Remaining));
    for (const FCireStatMod& M : Info.Mods) Parts.Add(FormatMod(M));
    return FString::Join(Parts, TEXT("  ·  "));
}
FLinearColor CireEffects::BorderColor(const FCireEffectInfo& Info)
{
    if (!Info.IsHarmful()) return Info.Kind == ECireEffectKind::Stance ? FLinearColor(1.f, .6f, .2f, 1) : FLinearColor(.95f, .78f, .25f, 1);
    switch (Info.Dispel)
    {
    case ECireDispel::Magic: return FLinearColor(.25f, .55f, 1.f, 1);
    case ECireDispel::Poison: return FLinearColor(.35f, .9f, .25f, 1);
    case ECireDispel::Curse: return FLinearColor(.72f, .35f, 1.f, 1);
    case ECireDispel::Disease: return FLinearColor(.75f, .6f, .25f, 1);
    default: return FLinearColor(.85f, .12f, .1f, 1);
    }
}

namespace
{
int32 SortKey(const FCireActiveEffect& E)
{
    const FCireEffectInfo* I = CireEffects::Find(E.Id);
    if (!I) return 5;
    if (I->Control != ECireControl::None && I->Control != ECireControl::Slow && I->Control != ECireControl::Taunt) return 0;
    if (I->IsHarmful()) return E.bFromLocalPlayer ? 1 : 2;
    if (I->Kind == ECireEffectKind::Passive) return 4;
    return 3;
}
FName MonsterAbilityId(const ACireMonster* M, ECireNPCAbilityKind Kind, const TCHAR* Fallback)
{
    if (const auto* A = M && M->NPCState ? M->NPCState->Archetype() : nullptr)
        for (const auto& Ab : A->Abilities) if (Ab.Kind == Kind && CireAuraData::Find(Ab.Id)) return Ab.Id;
    return Fallback;
}
}

void CireEffects::Gather(const AActor* Unit, float Now, TArray<FCireActiveEffect>& Out, const AActor* LocalHero)
{
    Out.Reset();
    if (!IsValid(Unit)) return;
    const ACireHero* Hero = Cast<ACireHero>(Unit); const ACireMonster* Monster = Cast<ACireMonster>(Unit);
    if ((Hero && Hero->bDead) || (Monster && Monster->Health <= 0)) return;
    auto Want = [&](FName Id, float Start, float End, int32 Stacks, AActor* Source)
    {
        if (Out.ContainsByPredicate([Id](const FCireActiveEffect& E) { return E.Id == Id; })) return;
        FCireActiveEffect E; E.Id = Id; E.Start = Start; E.End = End; E.Stacks = FMath::Max(1, Stacks); E.Source = Source;
        E.bFromLocalPlayer = LocalHero && Source == LocalHero; Out.Add(E);
    };
    // Mirrors UCireAuraComponent::Synchronize so the icons match the visuals.
    const float Shield = Hero ? Hero->ShieldUntil : 0, Taunt = Hero ? Hero->TauntUntil : 0, Slow = Hero ? Hero->SlowUntil : Monster ? Monster->SlowUntil : 0;
    const int32 Poison = Hero ? Hero->PoisonAreaCount : Monster ? Monster->PoisonAreaCount : 0;
    static const TSet<FName> GuardIds = {TEXT("iron_guard"), TEXT("challenge_of_iron"), TEXT("sanctuary"), TEXT("bastion_of_dawn"), TEXT("mass_aegis"), TEXT("wellspring"), TEXT("oathshield")};
    static const TSet<FName> TauntIds = {TEXT("war_cry"), TEXT("challenge_of_iron"), TEXT("toll_of_the_grave")};
    static const TSet<FName> SlowIds = {TEXT("frost_bind"), TEXT("shield_slam")};
    bool bGuardNamed = false, bTauntNamed = false, bSlowNamed = false;
    if (const auto* State = CireBuffs::Get(Unit))
        for (const auto& E : State->Buffs)
        {
            const bool bOpen = E.EndTime <= E.StartTime; if (!bOpen && E.EndTime <= Now) continue;
            if (Hero && GuardIds.Contains(E.Id) && Shield <= Now) continue;
            if (Hero && TauntIds.Contains(E.Id) && Taunt <= Now && Shield <= Now) continue;
            if (SlowIds.Contains(E.Id) && Slow <= Now) continue;
            bGuardNamed |= GuardIds.Contains(E.Id) || E.Id == TEXT("war_cry"); bTauntNamed |= TauntIds.Contains(E.Id); bSlowNamed |= SlowIds.Contains(E.Id);
            Want(E.Id, E.StartTime, bOpen ? 0.f : E.EndTime, E.Stacks, E.Source.Get());
        }
    if (Shield > Now && !bGuardNamed) Want(TEXT("guarded"), Now, Shield, 1, nullptr);
    if (Taunt > Now && !bTauntNamed) Want(TEXT("taunting"), Now, Taunt, 1, nullptr);
    if (Slow > Now && !bSlowNamed) Want(TEXT("slowed"), Now, Slow, 1, nullptr);
    if (Poison > 0)
    {
        const float End = Hero ? Hero->PoisonEndsAt : Monster ? Monster->PoisonEndsAt : 0;
        Want(TEXT("poisoned"), Now, End > Now ? End : 0.f, Poison, nullptr);
    }
    if (Hero)
    {
        if (Hero->HasSkill(TEXT("battle_rhythm"))) Want(TEXT("battle_rhythm"), 0, 0, 1, nullptr);
        if (Hero->HasSkill(TEXT("soul_conduit"))) Want(TEXT("soul_conduit"), 0, 0, 1, nullptr);
        if (const auto* Bag = Hero->Inventory.Get())
            for (const auto& Timed : Bag->Buffs)
                if (Timed.EndsAt > Now) if (const FName* Visual = CireAuraData::ItemBuffs().Find(Timed.Id)) Want(*Visual, Timed.EndsAt - Timed.Duration, Timed.EndsAt, 1, nullptr);
    }
    if (Monster && Monster->NPCState)
    {
        const auto* S = Monster->NPCState.Get();
        if (S->HasStatus(CireNPCStatus::Enraged)) Want(MonsterAbilityId(Monster, ECireNPCAbilityKind::Enrage, TEXT("enraged")), Now, 0, 1, nullptr);
        if (S->HasStatus(CireNPCStatus::Rallied)) Want(TEXT("rallied"), Now, 0, 1, nullptr);
        if (S->HasStatus(CireNPCStatus::ShieldWall)) Want(TEXT("npc_tank_wall"), Now, 0, 1, nullptr);
        if (S->HasStatus(CireNPCStatus::Guarded)) Want(TEXT("npc_tank_guard"), Now, 0, 1, nullptr);
        if (S->HasStatus(CireNPCStatus::Provoking)) Want(MonsterAbilityId(Monster, ECireNPCAbilityKind::Provoke, TEXT("npc_tank_provoke")), Now, 0, 1, nullptr);
    }
    Out.StableSort([](const FCireActiveEffect& A, const FCireActiveEffect& B) { return SortKey(A) < SortKey(B); });
}
ECireControl CireEffects::HardControl(const AActor* Unit, float Now, float* OutRemaining)
{
    TArray<FCireActiveEffect> Effects; Gather(Unit, Now, Effects);
    ECireControl Best = ECireControl::None; float Remaining = 0.f;
    for (const auto& E : Effects)
        if (const FCireEffectInfo* I = Find(E.Id))
            if (I->Control > Best && I->Control != ECireControl::Slow && I->Control != ECireControl::Taunt)
            { Best = I->Control; Remaining = E.End > Now ? E.End - Now : 0.f; }
    if (OutRemaining) *OutRemaining = Remaining;
    return Best;
}

// ---------------------------------------------------------------------------
// Casts
// ---------------------------------------------------------------------------
void FCireCastTracker::Sample(bool bCasting, FName Ability, const FString& Name, float End, float Now, bool bSilenced)
{
    if (bCasting) { bWasCasting = true; LastEnd = End; LastAbility = Ability; LastName = Name; return; }
    if (!bWasCasting) return;
    bWasCasting = false;
    // Ended more than 0.15s before its end time: it was stopped.
    Result = Now < LastEnd - .15f ? (bSilenced ? ECireCastResult::Silenced : ECireCastResult::Interrupted) : ECireCastResult::Completed;
    ResultTime = Now;
}
namespace
{
CireCasts::FHeroProvider GHeroProvider;
struct FTrack { FCireCastTracker Tracker; uint64 Frame = 0; FCireCastView Last; };
TMap<TWeakObjectPtr<const AActor>, FTrack> GTracks;
#if !UE_BUILD_SHIPPING
TMap<TWeakObjectPtr<const AActor>, FCireCastView> GDebug;
#endif
}
void CireCasts::RegisterHeroProvider(FHeroProvider Provider) { GHeroProvider = MoveTemp(Provider); }
FCireCastView CireCasts::Get(const AActor* Unit, float Now)
{
    FCireCastView V;
    if (!IsValid(Unit)) return V;
#if !UE_BUILD_SHIPPING
    if (const FCireCastView* D = GDebug.Find(Unit)) return *D;
#endif
    FTrack& T = GTracks.FindOrAdd(Unit);
    if (T.Frame == GFrameCounter) return T.Last; // once per frame per unit
    T.Frame = GFrameCounter;
    if (const auto* M = Cast<ACireMonster>(Unit))
    {
        if (M->NPCState)
        {
            const FCireNPCCastInfo C = M->NPCState->CastInfo();
            if (C.bCasting && C.Remaining > 0)
            {
                V.bCasting = true; V.AbilityId = C.AbilityId; V.Name = C.Name; V.Progress = C.Progress; V.Remaining = C.Remaining;
                V.Duration = M->CastEndsAt - M->CastStartedAt; V.bInterruptible = C.bInterruptible;
                const FString Lower = C.Name.ToLower(); V.bHeal = Lower.Contains(TEXT("mend")) || Lower.Contains(TEXT("heal"));
            }
        }
    }
    else if (const auto* H = Cast<ACireHero>(Unit))
    {
        if (GHeroProvider) GHeroProvider(*H, Now, V);
    }
    float Ignored = 0.f;
    const bool bSilenced = CireEffects::HardControl(Unit, Now, &Ignored) == ECireControl::Silence;
    T.Tracker.Sample(V.bCasting, V.AbilityId, V.Name, Now + V.Remaining, Now, bSilenced);
    if ((T.Tracker.Result == ECireCastResult::Interrupted || T.Tracker.Result == ECireCastResult::Silenced) && !V.bCasting)
    { V.Result = T.Tracker.Result; V.ResultAge = Now - T.Tracker.ResultTime; V.ResultName = T.Tracker.LastName; }
    T.Last = V;
    if (GTracks.Num() > 256) for (auto It = GTracks.CreateIterator(); It; ++It) if (!It.Key().IsValid()) It.RemoveCurrent();
    return V;
}
#if !UE_BUILD_SHIPPING
void CireCasts::DebugSet(const AActor* Unit, const FCireCastView& View) { GDebug.Add(Unit, View); }
void CireCasts::DebugClear() { GDebug.Reset(); }
#endif

// ---------------------------------------------------------------------------
// Callout queue
// ---------------------------------------------------------------------------
bool FCireCalloutQueue::Push(FName Id, float Duration, bool bControl, double Now)
{
    if (const double* Last = LastShown.Find(Id); Last && Now - *Last < SameIdCooldown) return false;
    if (Active.IsSet() && Active->Id == Id) return false;
    if (Queue.ContainsByPredicate([Id](const FCireCallout& C) { return C.Id == Id; })) return false;
    FCireCallout C; C.Id = Id; C.Duration = Duration; C.bControl = bControl;
    if (bControl) Queue.Insert(C, 0);
    else { if (Queue.Num() >= MaxQueued) return false; Queue.Add(C); }
    if (Queue.Num() > MaxQueued) Queue.SetNum(MaxQueued);
    return true;
}
const FCireCallout* FCireCalloutQueue::Tick(double Now)
{
    if (Active.IsSet() && Now - Active->Shown > Life) Active.Reset();
    // A hard-CC callout pre-empts a normal one that is already showing.
    if (Active.IsSet() && !Active->bControl && Queue.Num() && Queue[0].bControl) Active.Reset();
    if (!Active.IsSet() && Queue.Num() && Now - LastStart >= MinSpacing)
    {
        FCireCallout C = Queue[0]; Queue.RemoveAt(0); C.Shown = Now; LastStart = Now; LastShown.Add(C.Id, Now); Active = C;
    }
    return Active.IsSet() ? &Active.GetValue() : nullptr;
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------
#if !UE_BUILD_SHIPPING
bool CireEffects::RunSmoke()
{
    bool bPass = true; int32 Checks = 0;
    auto Check = [&](bool b, const FString& Why) { ++Checks; if (!b) { bPass = false; UE_LOG(LogTemp, Error, TEXT("CIRE_EFFECTS_CHECK_FAIL %s"), *Why); } };
    FString Error; Check(Reload(Error), FString(TEXT("BuffModifiers.json loads: ")) + Error);
    // Registry coverage: every id the game can produce and every visual row has a modifier row.
    for (const FName Id : CireBuffs::KnownIds()) Check(HasRegistryRow(Id), FString(TEXT("registry row for known id ")) + Id.ToString());
    for (const auto& Pair : CireAuraData::All()) Check(HasRegistryRow(Pair.Key), FString(TEXT("registry row for BuffVisuals id ")) + Pair.Key.ToString());
    for (const auto& Pair : GRows) Check(!Pair.Value.Line.IsEmpty(), FString(TEXT("one-line effect for ")) + Pair.Key.ToString());
    // Symbol formatting.
    Check(FormatMod({TEXT("DEF"), 20, TEXT("%"), 0}) == TEXT("DEF +20%"), TEXT("DEF +20%"));
    Check(FormatMod({TEXT("ATK"), 20, TEXT("%"), 0}) == TEXT("ATK +20%"), TEXT("ATK +20%"));
    Check(FormatMod({TEXT("Healing"), -50, TEXT("%"), 0}) == FString(TEXT("Healing −50%")), TEXT("Healing -50% uses a true minus"));
    Check(FormatMod({TEXT("Move"), -30, TEXT("%"), 0}) == FString(TEXT("Move −30%")), TEXT("Move -30%"));
    Check(FormatMod({TEXT("Armor"), -50, TEXT("%"), 10}) == FString(TEXT("Armor −50% (10s)")), TEXT("Armor -50% (10s)"));
    Check(FormatMod({TEXT("HP"), -12, TEXT("/s"), 0}) == FString(TEXT("HP −12/s")), TEXT("per-second unit"));
    Check(FormatMod({TEXT("Mana Regen"), 0, TEXT(""), 0}) == TEXT("Mana Regen"), TEXT("label-only mod"));
    Check(FormatControl(ECireControl::Stun, 1.5f) == TEXT("Stunned 1.5s"), TEXT("Stunned 1.5s"));
    Check(FormatControl(ECireControl::Silence, 3.f) == TEXT("Silenced 3s"), TEXT("Silenced 3s"));
    Check(DurationText(125.f) == TEXT("3m") && DurationText(8.f) == TEXT("8s"), TEXT("duration text"));
    const FCireEffectInfo* Guard = Find(TEXT("iron_guard"));
    Check(Guard && Symbols(*Guard) == TEXT("DEF +40%") && !Guard->IsHarmful(), TEXT("iron_guard symbols"));
    const FCireEffectInfo* Stun = Find(TEXT("stunned"));
    Check(Stun && Stun->Control == ECireControl::Stun && Stun->IsHarmful() && Symbols(*Stun, 1.5f) == TEXT("Stunned 1.5s"), TEXT("stunned symbols"));
    const FCireEffectInfo* Cut = Find(TEXT("healing_cut"));
    Check(Cut && Cut->Control == ECireControl::HealCut && Symbols(*Cut).Contains(TEXT("Healing")), TEXT("healing cut row"));
    const FCireEffectInfo* Passive = Find(TEXT("battle_rhythm"));
    Check(Passive && !Passive->bCallout, TEXT("passives never call out"));
    TMap<FName, FCireEffectInfo> Bad;
    Check(!Parse(TEXT("{\"effects\":{\"x\":{\"mods\":[{\"value\":3}]}}}"), Bad, Error), TEXT("mod without stat rejected"));
    // Callout queue throttling.
    FCireCalloutQueue Q; double T = 0;
    int32 Accepted = 0; for (int32 I = 0; I < 10; ++I) Accepted += Q.Push(FName(*FString::Printf(TEXT("fx%d"), I)), 5, false, T) ? 1 : 0;
    Check(Accepted == Q.MaxQueued, TEXT("burst is bounded by MaxQueued"));
    const FCireCallout* First = Q.Tick(T); Check(First && First->Id == TEXT("fx0"), TEXT("first callout shows"));
    Check(Q.Tick(T + .5) && Q.Tick(T + .5)->Id == TEXT("fx0"), TEXT("no switch before Life"));
    Check(Q.Push(TEXT("stun_now"), 2, true, T + .6), TEXT("CC accepted even when full"));
    const FCireCallout* Cc = Q.Tick(T + 1.0); Check(Cc && Cc->Id == TEXT("stun_now") && Cc->bControl, TEXT("CC pre-empts after min spacing"));
    Check(!Q.Push(TEXT("stun_now"), 2, true, T + 3.0), TEXT("same id within cooldown dropped"));
    int32 Shown = 0; double Last = -10, MinGap = 99; FName Prev;
    for (double Time = 1.0; Time < 20.0; Time += .05) if (const FCireCallout* C = Q.Tick(Time)) if (C->Id != Prev) { if (Last > 0) MinGap = FMath::Min(MinGap, Time - Last); Last = Time; Prev = C->Id; ++Shown; }
    Check(Shown >= 2 && MinGap >= Q.MinSpacing - .06, TEXT("callouts respect min spacing"));
    // Cast tracker: interrupt, silence, natural completion.
    FCireCastTracker K;
    K.Sample(true, TEXT("bolt"), TEXT("Shadow Bolt"), 10.f, 9.f, false); K.Sample(false, NAME_None, FString(), 0, 9.2f, false);
    Check(K.Result == ECireCastResult::Interrupted, TEXT("cast stopped early -> INTERRUPTED"));
    K.Sample(true, TEXT("mend"), TEXT("Dark Mending"), 20.f, 18.f, false); K.Sample(false, NAME_None, FString(), 0, 18.5f, true);
    Check(K.Result == ECireCastResult::Silenced, TEXT("cast stopped by silence -> SILENCED"));
    K.Sample(true, TEXT("bolt"), TEXT("Shadow Bolt"), 30.f, 29.f, false); K.Sample(false, NAME_None, FString(), 0, 30.02f, false);
    Check(K.Result == ECireCastResult::Completed, TEXT("cast reaching its end -> completed"));
    UE_LOG(LogTemp, Display, TEXT("CIRE_EFFECTS_%s checks=%d rows=%d"), bPass ? TEXT("PASS") : TEXT("FAIL"), Checks, GRows.Num());
    return bPass;
}
#endif
