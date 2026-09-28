// kit-editor: champion kit templates (data, rules, game integration, effect placement, pool browser).
// The editor screen is CireKitEditorUI.cpp; the native suite is CireKitEditorTests.cpp. Docs/KitEditor.md.
#include "CireKitEditor.h"
#include "CireAbilityDB.h"
#include "CireAbilityShapes.h"
#include "CireAbilityVFX.h"
#include "CireActorIterator.h"
#include "CireChampionRoster.h"
#include "CireFabVFX.h"
#include "CireGame.h"
#include "CireItems.h"
#include "CireScalingKits.h"
#include "CireSkillShop.h"
#include "Rules/CiresRules.h"
#include "Components/SkeletalMeshComponent.h"
#include "Dom/JsonObject.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "Kismet/GameplayStatics.h"
#include "Particles/ParticleSystem.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Particles/ParticleSystemComponent.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Policies/PrettyJsonPrintPolicy.h"

DEFINE_LOG_CATEGORY_STATIC(LogCireKitEditor, Log, All);

namespace
{
TMap<FString, FCireKitTemplate> GKitTemplates;
bool GKitTemplatesLoaded = false;
const TMap<FString, FCireKitTemplate>* GKitTemplatesOverride = nullptr;

void KitTemplatesLoadOnce() { if (!GKitTemplatesLoaded) CireKitEditor::Reload(); }

std::string KitUtf8(const FString& Text) { return std::string(TCHAR_TO_UTF8(*Text)); }

float KitNumber(const TSharedPtr<FJsonObject>& J, const TCHAR* Key, float Default)
{
    double V = Default;
    return J && J->TryGetNumberField(Key, V) && FMath::IsFinite(V) ? static_cast<float>(V) : Default;
}

bool KitVector(const TSharedPtr<FJsonObject>& J, const TCHAR* Key, int32 Count, float* Out)
{
    const TArray<TSharedPtr<FJsonValue>>* A = nullptr;
    if (!J || !J->TryGetArrayField(Key, A) || A->Num() != Count) return false;
    for (int32 I = 0; I < Count; ++I)
    {
        double V = 0;
        if (!(*A)[I]->TryGetNumber(V) || !FMath::IsFinite(V)) return false;
        Out[I] = static_cast<float>(V);
    }
    return true;
}

TArray<TSharedPtr<FJsonValue>> KitNumbers(std::initializer_list<float> Values)
{
    TArray<TSharedPtr<FJsonValue>> Out;
    for (float V : Values) Out.Add(MakeShared<FJsonValueNumber>(FMath::RoundToFloat(V * 1000.f) / 1000.f));
    return Out;
}

// Anchor keys and the bone / socket names they match on the bodies in the game (UE5 mannequin + Paragon, Mixamo-style
// Tripo rigs, Biped). The first existing name wins; then a case-insensitive exact match; then a contains match.
struct FKitAnchor { const TCHAR* Key; TArray<const TCHAR*> Names; };
const TArray<FKitAnchor>& KitAnchors()
{
    static const TArray<FKitAnchor> Anchors = {
        {TEXT("root"), {TEXT("root"), TEXT("Root")}},
        {TEXT("pelvis"), {TEXT("pelvis"), TEXT("Hips"), TEXT("hips"), TEXT("mixamorig:Hips"), TEXT("Bip001-Pelvis"), TEXT("Pelvis")}},
        {TEXT("chest"), {TEXT("spine_03"), TEXT("spine_05"), TEXT("Spine2"), TEXT("mixamorig:Spine2"), TEXT("chest"), TEXT("Chest"), TEXT("spine_02"), TEXT("Spine1"), TEXT("Bip001-Spine2")}},
        {TEXT("head"), {TEXT("head"), TEXT("Head"), TEXT("mixamorig:Head"), TEXT("Bip001-Head")}},
        {TEXT("hand_r"), {TEXT("hand_r"), TEXT("RightHand"), TEXT("mixamorig:RightHand"), TEXT("Hand_R"), TEXT("r_hand"), TEXT("Bip001-R-Hand"), TEXT("hand_right")}},
        {TEXT("hand_l"), {TEXT("hand_l"), TEXT("LeftHand"), TEXT("mixamorig:LeftHand"), TEXT("Hand_L"), TEXT("l_hand"), TEXT("Bip001-L-Hand"), TEXT("hand_left")}},
        {TEXT("foot_r"), {TEXT("foot_r"), TEXT("RightFoot"), TEXT("mixamorig:RightFoot"), TEXT("Foot_R"), TEXT("Bip001-R-Foot")}},
        {TEXT("foot_l"), {TEXT("foot_l"), TEXT("LeftFoot"), TEXT("mixamorig:LeftFoot"), TEXT("Foot_L"), TEXT("Bip001-L-Foot")}},
    };
    return Anchors;
}

struct FKitSectionDef { const TCHAR* Id; const TCHAR* Label; FLinearColor Color; };
// Same order, captions and colours as the Skill Shop periodic table (CireShopUI.cpp SkillSections).
const FKitSectionDef KitSectionDefs[] = {
    {TEXT("spell"), TEXT("OFFENSIVE  ·  SPELL DAMAGE"), FLinearColor(.42f, .55f, 1.f, 1)},
    {TEXT("attack"), TEXT("OFFENSIVE  ·  ATTACK DAMAGE"), FLinearColor(1.f, .42f, .28f, 1)},
    {TEXT("defensive"), TEXT("DEFENSIVE"), FLinearColor(.36f, .88f, .46f, 1)},
    {TEXT("control"), TEXT("CROWD CONTROL"), FLinearColor(.93f, .42f, .86f, 1)},
    {TEXT("summon"), TEXT("SUMMONS"), FLinearColor(.25f, .85f, .82f, 1)},
    {TEXT("construct"), TEXT("CONSTRUCTS"), FLinearColor(.95f, .62f, .22f, 1)},
    {TEXT("passive"), TEXT("PASSIVES"), FLinearColor(.84f, .80f, .68f, 1)},
    {TEXT("ultimate"), TEXT("ULTIMATES"), FLinearColor(.78f, .56f, 1.f, 1)},
};
}

// ------------------------------------------------------------------ value types
bool FCireKitEffectPlacement::IsDefault() const
{
    return Attach.IsEmpty() && Offset.IsNearlyZero(.01f) && FMath::IsNearlyEqual(Scale, 1.f, .001f) && Tint.A <= 0.f;
}
bool FCireKitEffectPlacement::operator==(const FCireKitEffectPlacement& O) const
{
    return Attach == O.Attach && Offset.Equals(O.Offset, .01f) && FMath::IsNearlyEqual(Scale, O.Scale, .001f) &&
        Tint.Equals(O.Tint, .002f) && FMath::IsNearlyEqual(TintStrength, O.TintStrength, .002f);
}
bool FCireKitTemplate::operator==(const FCireKitTemplate& O) const
{
    if (ChampionId != O.ChampionId || BaseKit != O.BaseKit || bGrantOnDraft != O.bGrantOnDraft || Effects.Num() != O.Effects.Num()) return false;
    for (const auto& Pair : Effects)
    {
        const FCireKitEffectPlacement* Other = O.Effects.Find(Pair.Key);
        if (!Other || !(*Other == Pair.Value)) return false;
    }
    return true;
}

// ------------------------------------------------------------------ data
FString CireKitEditor::DataPath() { return FPaths::Combine(FPaths::ProjectContentDir(), TEXT("Data/ChampionKitTemplates.json")); }

const TMap<FString, FCireKitTemplate>& CireKitEditor::Templates()
{
    if (GKitTemplatesOverride) return *GKitTemplatesOverride;
    KitTemplatesLoadOnce();
    return GKitTemplates;
}
const FCireKitTemplate* CireKitEditor::Find(const FString& ChampionId) { return ChampionId.IsEmpty() ? nullptr : Templates().Find(ChampionId); }
void CireKitEditor::DebugOverride(const TMap<FString, FCireKitTemplate>* InTemplates) { GKitTemplatesOverride = InTemplates; }

bool CireKitEditor::ParseJson(const FString& Json, TMap<FString, FCireKitTemplate>& Out, FString& Error)
{
    TSharedPtr<FJsonObject> Root;
    if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Root) || !Root) { Error = TEXT("not a JSON object"); return false; }
    TMap<FString, FCireKitTemplate> Parsed;
    const TSharedPtr<FJsonObject>* Champions = nullptr;
    if (Root->TryGetObjectField(TEXT("champions"), Champions) && Champions)
    {
        for (const auto& Pair : (*Champions)->Values)
        {
            const TSharedPtr<FJsonObject>* Row = nullptr;
            if (Pair.Key.IsEmpty() || Pair.Key.Len() > 64 || !Pair.Value->TryGetObject(Row) || !Row)
            {
                Error = FString::Printf(TEXT("champion '%s' is not an object"), *Pair.Key);
                return false;
            }
            FCireKitTemplate T;
            T.ChampionId = Pair.Key;
            (*Row)->TryGetBoolField(TEXT("grantOnDraft"), T.bGrantOnDraft);
            (*Row)->TryGetStringField(TEXT("updated"), T.Updated);
            const TArray<TSharedPtr<FJsonValue>>* Kit = nullptr;
            if ((*Row)->TryGetArrayField(TEXT("baseKit"), Kit))
                for (const auto& V : *Kit)
                {
                    FString Id;
                    if (V->TryGetString(Id) && !Id.IsEmpty() && Id.Len() <= 64) T.BaseKit.AddUnique(Id);
                }
            const TSharedPtr<FJsonObject>* Effects = nullptr;
            if ((*Row)->TryGetObjectField(TEXT("effects"), Effects) && Effects)
                for (const auto& E : (*Effects)->Values)
                {
                    const TSharedPtr<FJsonObject>* Obj = nullptr;
                    if (E.Key.IsEmpty() || !E.Value->TryGetObject(Obj) || !Obj) continue;
                    FCireKitEffectPlacement P;
                    (*Obj)->TryGetStringField(TEXT("attach"), P.Attach);
                    float V3[3];
                    if (KitVector(*Obj, TEXT("offset"), 3, V3)) P.Offset = FVector(FMath::Clamp(V3[0], -400.f, 400.f), FMath::Clamp(V3[1], -400.f, 400.f), FMath::Clamp(V3[2], -400.f, 400.f));
                    P.Scale = FMath::Clamp(KitNumber(*Obj, TEXT("scale"), 1.f), .1f, 5.f);
                    float V4[4];
                    if (KitVector(*Obj, TEXT("tint"), 4, V4)) P.Tint = FLinearColor(FMath::Clamp(V4[0], 0.f, 4.f), FMath::Clamp(V4[1], 0.f, 4.f), FMath::Clamp(V4[2], 0.f, 4.f), FMath::Clamp(V4[3], 0.f, 1.f));
                    P.TintStrength = FMath::Clamp(KitNumber(*Obj, TEXT("tintStrength"), 1.f), 0.f, 1.f);
                    if (!P.IsDefault()) T.Effects.Add(E.Key, P);
                }
            Parsed.Add(T.ChampionId, MoveTemp(T));
        }
    }
    Out = MoveTemp(Parsed);
    Error.Reset();
    return true;
}

FString CireKitEditor::ToJson(const TMap<FString, FCireKitTemplate>& InTemplates)
{
    TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
    Root->SetNumberField(TEXT("schemaVersion"), 1);
    Root->SetStringField(TEXT("about"), TEXT("Per-champion base kits and effect placements, written by the Skill Assignment editor (Champion Select > KIT EDITOR). Docs/KitEditor.md"));
    TSharedRef<FJsonObject> Champions = MakeShared<FJsonObject>();
    TArray<FString> Ids;
    InTemplates.GetKeys(Ids);
    Ids.Sort();
    for (const FString& Id : Ids)
    {
        const FCireKitTemplate& T = InTemplates[Id];
        TSharedRef<FJsonObject> Row = MakeShared<FJsonObject>();
        TArray<TSharedPtr<FJsonValue>> Kit;
        for (const FString& S : T.BaseKit) Kit.Add(MakeShared<FJsonValueString>(S));
        Row->SetArrayField(TEXT("baseKit"), Kit);
        Row->SetBoolField(TEXT("grantOnDraft"), T.bGrantOnDraft);
        if (!T.Updated.IsEmpty()) Row->SetStringField(TEXT("updated"), T.Updated);
        TSharedRef<FJsonObject> Effects = MakeShared<FJsonObject>();
        TArray<FString> Keys;
        T.Effects.GetKeys(Keys);
        Keys.Sort();
        for (const FString& Key : Keys)
        {
            const FCireKitEffectPlacement& P = T.Effects[Key];
            if (P.IsDefault()) continue;
            TSharedRef<FJsonObject> E = MakeShared<FJsonObject>();
            if (!P.Attach.IsEmpty()) E->SetStringField(TEXT("attach"), P.Attach);
            if (!P.Offset.IsNearlyZero(.01f)) E->SetArrayField(TEXT("offset"), KitNumbers({float(P.Offset.X), float(P.Offset.Y), float(P.Offset.Z)}));
            if (!FMath::IsNearlyEqual(P.Scale, 1.f, .001f)) E->SetNumberField(TEXT("scale"), FMath::RoundToFloat(P.Scale * 1000.f) / 1000.f);
            if (P.Tint.A > 0.f)
            {
                E->SetArrayField(TEXT("tint"), KitNumbers({P.Tint.R, P.Tint.G, P.Tint.B, P.Tint.A}));
                E->SetNumberField(TEXT("tintStrength"), FMath::RoundToFloat(P.TintStrength * 1000.f) / 1000.f);
            }
            Effects->SetObjectField(Key, E);
        }
        Row->SetObjectField(TEXT("effects"), Effects);
        Champions->SetObjectField(Id, Row);
    }
    Root->SetObjectField(TEXT("champions"), Champions);
    FString Out;
    const auto Writer = TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create(&Out);
    FJsonSerializer::Serialize(Root, Writer);
    return Out + TEXT("\n");
}

bool CireKitEditor::Reload()
{
    GKitTemplatesLoaded = true;
    FString Json, Error;
    TMap<FString, FCireKitTemplate> Parsed;
    if (!FPaths::FileExists(DataPath())) { GKitTemplates.Reset(); return true; } // no templates yet: every champion keeps its default kit
    if (!FFileHelper::LoadFileToString(Json, *DataPath()) || !ParseJson(Json, Parsed, Error))
    {
        UE_LOG(LogCireKitEditor, Error, TEXT("CIRE_KIT_TEMPLATES_REJECTED %s (keeping the previous templates)"), *Error);
        return false;
    }
    GKitTemplates = MoveTemp(Parsed);
    UE_LOG(LogCireKitEditor, Display, TEXT("CIRE_KIT_TEMPLATES_LOADED champions=%d"), GKitTemplates.Num());
    return true;
}

bool CireKitEditor::SaveTemplate(const FCireKitTemplate& Template, FString* Error)
{
    if (Template.ChampionId.IsEmpty()) { if (Error) *Error = TEXT("No champion selected."); return false; }
    KitTemplatesLoadOnce();
    TMap<FString, FCireKitTemplate> Next = GKitTemplates;
    FCireKitTemplate Clean = Template;
    Clean.BaseKit = Normalize(Template.BaseKit);
    for (auto It = Clean.Effects.CreateIterator(); It; ++It) if (It.Value().IsDefault()) It.RemoveCurrent();
    Clean.Updated = FDateTime::UtcNow().ToIso8601();
    if (Clean.BaseKit.IsEmpty() && Clean.Effects.IsEmpty()) Next.Remove(Clean.ChampionId);
    else Next.Add(Clean.ChampionId, Clean);
    if (!FFileHelper::SaveStringToFile(ToJson(Next), *DataPath(), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
    {
        if (Error) *Error = TEXT("Cannot write Content/Data/ChampionKitTemplates.json (read-only?)");
        return false;
    }
    GKitTemplates = MoveTemp(Next);
    GKitTemplatesLoaded = true;
    CireAbilityDB::Reload(); // purchasable lists pick the kit up at once
    UE_LOG(LogCireKitEditor, Display, TEXT("CIRE_KIT_TEMPLATE_SAVED champion=%s kit=%d effects=%d grant=%d"), *Clean.ChampionId, Clean.BaseKit.Num(), Clean.Effects.Num(), Clean.bGrantOnDraft ? 1 : 0);
    return true;
}

// ------------------------------------------------------------------ kit rules
CireKitEditor::EKind CireKitEditor::KindOf(const FString& AbilityId)
{
    if (const FCireAbilityDef* Def = CireAbilityDB::Find(AbilityId))
        return Def->IsUltimate() ? EKind::Ultimate : Def->IsPassive() ? EKind::Passive : EKind::Active;
    const auto Kind = CireSkillShop::KindOf(AbilityId);
    return Kind == Cires::Items::ShopSkillKind::Ultimate ? EKind::Ultimate : Kind == Cires::Items::ShopSkillKind::Passive ? EKind::Passive : EKind::Active;
}
int32 CireKitEditor::Capacity(EKind Kind)
{
    return Kind == EKind::Ultimate ? Cires::MaxUltimates : Kind == EKind::Passive ? Cires::MaxPassives : Cires::MaxActiveSkills;
}
FString CireKitEditor::AddBlocker(const TArray<FString>& Kit, const FString& Id)
{
    if (!CireAbilityDB::Find(Id)) return TEXT("Unknown ability.");
    if (Kit.Contains(Id)) return TEXT("Already in the kit.");
    const EKind Kind = KindOf(Id);
    int32 Have = 0;
    for (const FString& Other : Kit) Have += KindOf(Other) == Kind;
    if (Have >= Capacity(Kind))
        return Kind == EKind::Ultimate ? TEXT("The kit already has its ultimate: remove it first.")
            : Kind == EKind::Passive ? TEXT("The kit already has its passive: remove it first.")
            : FString::Printf(TEXT("All %d active slots are filled: remove one first."), Capacity(Kind));
    return FString();
}
TArray<FString> CireKitEditor::Normalize(const TArray<FString>& Kit, TArray<FString>* Dropped)
{
    TArray<FString> ByKind[3];
    int32 Count[3] = {0, 0, 0};
    TSet<FString> Seen;
    for (const FString& Id : Kit)
    {
        const bool bKnown = CireAbilityDB::Find(Id) != nullptr;
        const int32 K = bKnown ? static_cast<int32>(KindOf(Id)) : 0;
        if (!bKnown || Seen.Contains(Id) || Count[K] >= Capacity(static_cast<EKind>(K))) { if (Dropped) Dropped->Add(Id); continue; }
        Seen.Add(Id); ++Count[K]; ByKind[K].Add(Id);
    }
    TArray<FString> Out = ByKind[0];
    Out.Append(ByKind[static_cast<int32>(EKind::Ultimate)]);
    Out.Append(ByKind[static_cast<int32>(EKind::Passive)]);
    return Out;
}

// ------------------------------------------------------------------ game integration
void CireKitEditor::MergeIntoKits(TMap<FString, FCireChampionKit>& Kits)
{
    for (const auto& Pair : Templates())
    {
        const TArray<FString> Kit = Normalize(Pair.Value.BaseKit);
        if (Kit.IsEmpty()) continue;
        FCireChampionKit* K = Kits.Find(Pair.Key);
        if (!K)
        {
            // A champion without an Ability DB kit (a new roster row): the template is its whole purchasable list.
            const FCireChampionProfile* Profile = CireChampionRoster::Find(Pair.Key);
            FCireChampionKit New;
            New.Name = Profile ? Profile->DisplayName : Pair.Key;
            if (Profile) for (const FString& R : Profile->Roles) New.Roles.Add(R.ToUpper() == TEXT("HEALER") || R.ToUpper() == TEXT("SUPPORT") ? TEXT("HEAL") : R.ToUpper() == TEXT("DAMAGE") ? TEXT("DPS") : R.ToUpper());
            New.PrimaryRole = New.Roles.Num() ? New.Roles[0] : TEXT("DPS");
            K = &Kits.Add(Pair.Key, New);
        }
        for (const FString& Id : Kit)
        {
            K->Purchasable.AddUnique(Id);
            const FCireAbilityDef* Def = CireAbilityDB::Find(Id);
            if (Def && Def->IsImplemented()) K->PurchasableImplemented.AddUnique(Id);
        }
    }
}

int32 CireKitEditor::GrantOnDraft(ACireHero* Hero)
{
    if (!Hero || !Hero->HasAuthority() || Hero->ChampionProfileId.IsEmpty()) return 0;
    const FCireKitTemplate* T = Find(Hero->ChampionProfileId);
    if (!T || !T->bGrantOnDraft) return 0;
    int32 Granted = 0;
    TArray<FString> Skipped;
    for (const FString& Id : Normalize(T->BaseKit))
    {
        const FCireAbilityDef* Def = CireAbilityDB::Find(Id);
        if (!Def || !Def->IsImplemented() || Hero->Skills.Contains(Id) || Hero->Skills.Num() >= Cires::MaxSkills) continue;
        // A shield / ranged-only skill would do nothing on a body without one: skipped (the editor warns about it).
        if (!CireKits::MeetsRequirement(Hero, Id, nullptr)) { Skipped.Add(Id); continue; }
        const EKind Kind = KindOf(Id);
        const Cires::SkillDefinition Skill{KitUtf8(Id), KitUtf8(Def->Name),
            Kind == EKind::Ultimate ? Cires::SkillKind::Ultimate : Kind == EKind::Passive ? Cires::SkillKind::Passive : Cires::SkillKind::Active};
        if (Hero->Progression.Schedule == Cires::SkillSchedule::Shop) { if (!Cires::AddPurchasedSkill(Hero->Progression, Skill)) continue; }
        else Hero->Progression.LearnedSkills.push_back(Skill);
        Hero->Skills.Add(Id);
        Hero->Cooldowns.Add(0.f);
        if (Hero->Inventory)
        {
            Hero->Inventory->SkillRanks.RemoveAll([&](const FCireSkillRank& R) { return R.Id == Id; });
            FCireSkillRank Rank; Rank.Id = Id; Rank.Level = 1;
            Hero->Inventory->SkillRanks.Add(Rank);
        }
        ++Granted;
    }
    if (Granted > 0)
    {
        if (Hero->Progression.Schedule == Cires::SkillSchedule::Draft)
            Hero->Progression.NextAugmentLevel = Cires::BreakpointForSkill(static_cast<int>(Hero->Progression.LearnedSkills.size()));
        Hero->Offers.Reset();
        Hero->CurrentOffer = {};
        Hero->Notice = FString::Printf(TEXT("Champion bound with its base kit (%d skills)."), Granted);
    }
    if (Skipped.Num()) UE_LOG(LogCireKitEditor, Warning, TEXT("CIRE_KIT_GRANT_SKIPPED champion=%s skills=%s (requirement not met)"), *Hero->ChampionProfileId, *FString::Join(Skipped, TEXT(",")));
    return Granted;
}

const FCireKitEffectPlacement* CireKitEditor::Placement(const FString& ChampionId, const FString& AbilityId)
{
    const FCireKitTemplate* T = Find(ChampionId);
    return T ? T->Effects.Find(AbilityId) : nullptr;
}

FName CireKitEditor::ResolveAttach(const USkeletalMeshComponent* Mesh, const FString& Attach)
{
    if (!Mesh || Attach.IsEmpty()) return NAME_None;
    const FName Literal(*Attach);
    if (Mesh->DoesSocketExist(Literal)) return Literal;
    const FKitAnchor* Anchor = KitAnchors().FindByPredicate([&](const FKitAnchor& A) { return Attach.Equals(A.Key, ESearchCase::IgnoreCase); });
    if (!Anchor) return NAME_None;
    for (const TCHAR* Name : Anchor->Names) if (Mesh->DoesSocketExist(FName(Name))) return FName(Name);
    // Case-insensitive, then "contains" (e.g. "Bip01_R_Hand", "DEF-hand.R" style rigs).
    const TArray<FName> All = Mesh->GetAllSocketNames();
    for (const FName& N : All) for (const TCHAR* Name : Anchor->Names) if (N.ToString().Equals(Name, ESearchCase::IgnoreCase)) return N;
    const FString Key = Anchor->Key;
    const bool bRight = Key.EndsWith(TEXT("_r")), bLeft = Key.EndsWith(TEXT("_l"));
    const FString Part = bRight || bLeft ? Key.LeftChop(2) : Key;
    for (const FName& N : All)
    {
        const FString S = N.ToString().ToLower();
        if (!S.Contains(Part) || S.Contains(TEXT("twist")) || S.Contains(TEXT("ik_")) || S.Contains(TEXT("finger")) || S.Contains(TEXT("thumb")) || S.Contains(TEXT("index"))) continue;
        const bool bR = S.EndsWith(TEXT("_r")) || S.EndsWith(TEXT(".r")) || S.Contains(TEXT("right")) || S.Contains(TEXT("_r_")) || S.StartsWith(TEXT("r_"));
        const bool bL = S.EndsWith(TEXT("_l")) || S.EndsWith(TEXT(".l")) || S.Contains(TEXT("left")) || S.Contains(TEXT("_l_")) || S.StartsWith(TEXT("l_"));
        if ((bRight && bR) || (bLeft && bL) || (!bRight && !bLeft)) return N;
    }
    return NAME_None;
}

void CireKitEditor::ApplyPlacement(UFXSystemComponent* Component, ACireHero* Hero, const FCireKitEffectPlacement& P, float BaseScale, bool bTint)
{
    if (!Component || !Hero) return;
    // Offset in champion space (+X forward, +Y right, +Z up), converted into the attach socket's frame.
    const FVector WorldOffset = Hero->GetActorRotation().RotateVector(P.Offset * Hero->GetActorScale3D().Z);
    const FTransform Socket = Component->GetAttachParent() ? Component->GetAttachParent()->GetSocketTransform(Component->GetAttachSocketName()) : FTransform::Identity;
    Component->SetRelativeLocation(Socket.InverseTransformVectorNoScale(WorldOffset));
    Component->SetUsingAbsoluteScale(true);
    Component->SetWorldScale3D(FVector(FMath::Max(.01f, BaseScale * P.Scale)));
    if (bTint && P.Tint.A > 0.f) CireFabVFX::Recolor(Component, P.Tint, P.TintStrength);
}

UFXSystemComponent* CireKitEditor::SpawnPlaced(ACireHero* Hero, UFXSystemAsset* System, const FCireKitEffectPlacement& P, float BaseScale, bool bAutoDestroy,
    const CireFabVFX::FEntry* Entry)
{
    if (!Hero || !System || !Hero->GetMesh()) return nullptr;
    USkeletalMeshComponent* Mesh = Hero->GetMesh();
    const FName Socket = ResolveAttach(Mesh, P.Attach);
    UFXSystemComponent* C = nullptr;
    if (UNiagaraSystem* Niagara = Cast<UNiagaraSystem>(System))
        C = UNiagaraFunctionLibrary::SpawnSystemAttached(Niagara, Mesh, Socket, FVector::ZeroVector, FRotator::ZeroRotator, FVector(1.f),
            EAttachLocation::SnapToTarget, bAutoDestroy, ENCPoolMethod::None, true, true);
    else if (UParticleSystem* Cascade = Cast<UParticleSystem>(System))
        C = UGameplayStatics::SpawnEmitterAttached(Cascade, Mesh, Socket, FVector::ZeroVector, FRotator::ZeroRotator, FVector(1.f),
            EAttachLocation::SnapToTarget, bAutoDestroy, EPSCPoolMethod::None, true);
    if (!C) return nullptr;
    if (Entry) CireFabVFX::ApplyEntryTint(C, *Entry); // the data recolour first, the champion's own tint on top
    ApplyPlacement(C, Hero, P, BaseScale, true);
    return C;
}

UFXSystemComponent* CireKitEditor::SpawnPlacedCast(UWorld* World, FName Skill, FVector CasterAt, UFXSystemAsset* System, float Scale,
    const CireFabVFX::FEntry* Entry)
{
    if (!World || !System || Skill.IsNone() || Templates().IsEmpty() || !CireFabVFX::Enabled()) return nullptr;
    const FString Id = Skill.ToString();
    ACireHero* Best = nullptr;
    const FCireKitEffectPlacement* BestPlacement = nullptr;
    double BestDistance = FMath::Square(450.0);
    for (TCireActorIterator<ACireHero> It(World); It; ++It)
    {
        ACireHero* Hero = *It;
        if (!IsValid(Hero) || Hero->ChampionProfileId.IsEmpty() || !Hero->Skills.Contains(Id)) continue;
        const FCireKitEffectPlacement* P = Placement(Hero->ChampionProfileId, Id);
        if (!P || P->IsDefault()) continue;
        const double D = FVector::DistSquared2D(Hero->GetActorLocation(), CasterAt);
        if (D < BestDistance) { BestDistance = D; Best = Hero; BestPlacement = P; }
    }
    return Best ? SpawnPlaced(Best, System, *BestPlacement, Scale, true, Entry) : nullptr;
}

UFXSystemAsset* CireKitEditor::CastSystem(const FString& AbilityId, float* OutScale, const CireFabVFX::FEntry** OutEntry)
{
    const FName Key(*AbilityId);
    const FCireHitShape Shape = CireAbilityShapes::Describe(Key);
    const ECireSchool School = Shape.bHeal ? ECireSchool::Life : CireAbilityShapes::SchoolFor(Key);
    const CireFabVFX::FEntry* Entry = CireFabVFX::FindFor(Key, School, CireFabVFX::ERole::Cast);
    UFXSystemAsset* System = CireFabVFX::Resolve(Entry);
    if (OutScale) *OutScale = Entry ? Entry->Scale : 1.f;
    if (OutEntry) *OutEntry = System ? Entry : nullptr;
    return System;
}

// ------------------------------------------------------------------ pool browser
TArray<CireKitEditor::FPoolSection> CireKitEditor::Sections()
{
    TArray<FPoolSection> Out;
    for (const FKitSectionDef& D : KitSectionDefs) Out.Add({D.Id, D.Label, D.Color});
    // Any section a future ability brings (Abilities.json "section") gets its own block after the known ones.
    for (const FCireAbilityDef& Def : CireAbilityDB::All())
    {
        const FString S = SectionOf(Def);
        if (!Out.ContainsByPredicate([&](const FPoolSection& X) { return X.Id == S; }))
            Out.Add({S, S.ToUpper(), FLinearColor(.70f, .72f, .78f, 1)});
    }
    return Out;
}
FString CireKitEditor::SectionOf(const FCireAbilityDef& Def)
{
    if (!Def.Section.IsEmpty()) return Def.Section;
    return Def.IsUltimate() ? TEXT("ultimate") : Def.IsPassive() ? TEXT("passive") : Def.IsConstruct() ? TEXT("construct") : TEXT("spell");
}
bool CireKitEditor::MatchesSearch(const FCireAbilityDef& Def, const FString& Search)
{
    const FString Needle = Search.TrimStartAndEnd();
    if (Needle.IsEmpty()) return true;
    TArray<FString> Words;
    Needle.ParseIntoArrayWS(Words);
    for (const FString& W : Words)
    {
        bool bHit = Def.Name.Contains(W) || Def.Id.Contains(W) || Def.School.Contains(W) || SectionOf(Def).Contains(W) || Def.Kind.Contains(W);
        for (const FString& T : Def.Types) bHit |= T.Contains(W);
        for (const FString& T : Def.EffectTags) bHit |= T.Contains(W);
        for (const FString& C : Def.Champions) bHit |= C.Contains(W);
        if (!bHit) return false; // every word must match somewhere
    }
    return true;
}
TArray<TPair<CireKitEditor::FPoolSection, TArray<const FCireAbilityDef*>>> CireKitEditor::Pool(const FPoolFilter& Filter)
{
    TArray<TPair<FPoolSection, TArray<const FCireAbilityDef*>>> Out;
    const FCireChampionKit* Kit = Filter.OnlyChampion.IsEmpty() ? nullptr : CireAbilityDB::Kit(Filter.OnlyChampion);
    for (const FPoolSection& Section : Sections())
    {
        if (Filter.HiddenSections.Contains(Section.Id)) continue;
        TArray<const FCireAbilityDef*> List;
        for (const FCireAbilityDef& Def : CireAbilityDB::All())
        {
            if (SectionOf(Def) != Section.Id || !MatchesSearch(Def, Filter.Search)) continue;
            if (Filter.Kind >= 0 && static_cast<int32>(KindOf(Def.Id)) != Filter.Kind) continue;
            if (!Filter.Role.IsEmpty() && !Def.Types.Contains(Filter.Role)) continue;
            if (!Filter.OnlyChampion.IsEmpty() && !(Kit && Kit->Purchasable.Contains(Def.Id))) continue;
            List.Add(&Def);
        }
        if (List.IsEmpty()) continue;
        List.StableSort([](const FCireAbilityDef& A, const FCireAbilityDef& B) { return A.Name < B.Name; });
        Out.Add({Section, MoveTemp(List)});
    }
    return Out;
}

bool CireKitEditor::IsAvailable()
{
#if UE_BUILD_SHIPPING
    static const bool bFlag = FParse::Param(FCommandLine::Get(), TEXT("CireKitEditor"));
    return bFlag;
#else
    return true;
#endif
}
