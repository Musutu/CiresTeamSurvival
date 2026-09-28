// kit-editor: Hero Creator data (kit profiles, loadout presets, effect placements), rules and game integration.
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
#include "CireShopUI.h"
#include "CireWaves.h"
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
FCireKitData GKitData;
bool GKitDataLoaded = false;
const FCireKitData* GKitDataOverride = nullptr;
FString GKitForcedProfile;

void KitDataLoadOnce() { if (!GKitDataLoaded) CireKitEditor::Reload(); }
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
bool KitValidName(const FString& Name) { return !Name.TrimStartAndEnd().IsEmpty() && Name.Len() <= 40; }

bool KitParsePlacement(const TSharedPtr<FJsonObject>& Obj, FCireKitEffectPlacement& P)
{
    Obj->TryGetStringField(TEXT("attach"), P.Attach);
    float V3[3];
    if (KitVector(Obj, TEXT("offset"), 3, V3)) P.Offset = FVector(FMath::Clamp(V3[0], -400.f, 400.f), FMath::Clamp(V3[1], -400.f, 400.f), FMath::Clamp(V3[2], -400.f, 400.f));
    P.Scale = FMath::Clamp(KitNumber(Obj, TEXT("scale"), 1.f), .1f, 5.f);
    float V4[4];
    if (KitVector(Obj, TEXT("tint"), 4, V4)) P.Tint = FLinearColor(FMath::Clamp(V4[0], 0.f, 4.f), FMath::Clamp(V4[1], 0.f, 4.f), FMath::Clamp(V4[2], 0.f, 4.f), FMath::Clamp(V4[3], 0.f, 1.f));
    P.TintStrength = FMath::Clamp(KitNumber(Obj, TEXT("tintStrength"), 1.f), 0.f, 1.f);
    return !P.IsDefault();
}
TSharedRef<FJsonObject> KitPlacementJson(const FCireKitEffectPlacement& P)
{
    TSharedRef<FJsonObject> E = MakeShared<FJsonObject>();
    if (!P.Attach.IsEmpty()) E->SetStringField(TEXT("attach"), P.Attach);
    if (!P.Offset.IsNearlyZero(.01f)) E->SetArrayField(TEXT("offset"), KitNumbers({float(P.Offset.X), float(P.Offset.Y), float(P.Offset.Z)}));
    if (!FMath::IsNearlyEqual(P.Scale, 1.f, .001f)) E->SetNumberField(TEXT("scale"), FMath::RoundToFloat(P.Scale * 1000.f) / 1000.f);
    if (P.Tint.A > 0.f)
    {
        E->SetArrayField(TEXT("tint"), KitNumbers({P.Tint.R, P.Tint.G, P.Tint.B, P.Tint.A}));
        E->SetNumberField(TEXT("tintStrength"), FMath::RoundToFloat(P.TintStrength * 1000.f) / 1000.f);
    }
    return E;
}
// Slot keys in the file: "1".."6", "R", "P".
FString KitSlotKey(int32 Slot) { return Slot == FCireKitLoadout::UltimateSlot ? TEXT("R") : Slot == FCireKitLoadout::PassiveSlot ? TEXT("P") : FString::FromInt(Slot + 1); }
int32 KitSlotFromKey(const FString& Key)
{
    if (Key.Equals(TEXT("R"), ESearchCase::IgnoreCase)) return FCireKitLoadout::UltimateSlot;
    if (Key.Equals(TEXT("P"), ESearchCase::IgnoreCase)) return FCireKitLoadout::PassiveSlot;
    const int32 N = FCString::Atoi(*Key);
    return N >= 1 && N <= FCireKitLoadout::ActiveSlots && Key.IsNumeric() ? N - 1 : INDEX_NONE;
}

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

}

bool FCireKitEffectPlacement::IsDefault() const
{
    return Attach.IsEmpty() && Offset.IsNearlyZero(.01f) && FMath::IsNearlyEqual(Scale, 1.f, .001f) && Tint.A <= 0.f;
}
bool FCireKitEffectPlacement::operator==(const FCireKitEffectPlacement& O) const
{
    return Attach == O.Attach && Offset.Equals(O.Offset, .01f) && FMath::IsNearlyEqual(Scale, O.Scale, .001f) &&
        Tint.Equals(O.Tint, .002f) && FMath::IsNearlyEqual(TintStrength, O.TintStrength, .002f);
}
// ------------------------------------------------------------------ value types
TArray<FString> FCireKitLoadout::Skills() const
{
    TArray<FString> Out;
    for (const FString& S : Slots) if (!S.IsEmpty()) Out.AddUnique(S);
    return Out;
}
int32 FCireKitLoadout::Count() const { int32 N = 0; for (const FString& S : Slots) N += !S.IsEmpty(); return N; }
const FCireKitLoadout* FCireKitChampion::Default() const
{
    if (const FCireKitLoadout* L = Find(DefaultLoadout)) return L;
    return Loadouts.Num() ? &Loadouts[0] : nullptr;
}
const FCireKitLoadout* FCireKitChampion::Find(const FString& Name) const { return Loadouts.FindByPredicate([&](const FCireKitLoadout& L) { return L.Name == Name; }); }
FCireKitLoadout* FCireKitChampion::Find(const FString& Name) { return Loadouts.FindByPredicate([&](const FCireKitLoadout& L) { return L.Name == Name; }); }
const FCireKitProfile* FCireKitData::FindProfile(const FString& Name) const { return Profiles.FindByPredicate([&](const FCireKitProfile& P) { return P.Name.Equals(Name, ESearchCase::IgnoreCase); }); }
FCireKitProfile* FCireKitData::FindProfile(const FString& Name) { return Profiles.FindByPredicate([&](const FCireKitProfile& P) { return P.Name.Equals(Name, ESearchCase::IgnoreCase); }); }

// ------------------------------------------------------------------ data
FString CireKitEditor::DataPath() { return FPaths::Combine(FPaths::ProjectContentDir(), TEXT("Data/ChampionKitTemplates.json")); }
const FCireKitData& CireKitEditor::Data()
{
    if (GKitDataOverride) return *GKitDataOverride;
    KitDataLoadOnce();
    return GKitData;
}
void CireKitEditor::DebugOverride(const FCireKitData* InData) { GKitDataOverride = InData; }

namespace
{
bool KitParseChampion(const FString& Id, const TSharedPtr<FJsonObject>& Row, FCireKitChampion& C, FString& Error)
{
    Row->TryGetBoolField(TEXT("grantOnDraft"), C.bGrantOnDraft);
    Row->TryGetStringField(TEXT("default"), C.DefaultLoadout);
    Row->TryGetStringField(TEXT("updated"), C.Updated);
    const TArray<TSharedPtr<FJsonValue>>* Presets = nullptr;
    if (Row->TryGetArrayField(TEXT("presets"), Presets))
        for (const auto& V : *Presets)
        {
            const TSharedPtr<FJsonObject>* P = nullptr;
            if (!V->TryGetObject(P) || !P) { Error = FString::Printf(TEXT("%s: a preset is not an object"), *Id); return false; }
            FCireKitLoadout L;
            (*P)->TryGetStringField(TEXT("name"), L.Name);
            L.Name = L.Name.TrimStartAndEnd().Left(40);
            if (L.Name.IsEmpty() || C.Find(L.Name)) continue;
            const TSharedPtr<FJsonObject>* Slots = nullptr;
            if ((*P)->TryGetObjectField(TEXT("slots"), Slots) && Slots)
                for (const auto& S : (*Slots)->Values)
                {
                    FString Ability;
                    const int32 Slot = KitSlotFromKey(FString(S.Key));
                    if (Slot != INDEX_NONE && S.Value->TryGetString(Ability) && Ability.Len() <= 64) L.Slots[Slot] = Ability;
                }
            C.Loadouts.Add(L);
        }
    // schemaVersion 1 (one "baseKit" list): one preset named "Default".
    const TArray<TSharedPtr<FJsonValue>>* Kit = nullptr;
    if (C.Loadouts.IsEmpty() && Row->TryGetArrayField(TEXT("baseKit"), Kit))
    {
        FCireKitLoadout L; L.Name = TEXT("Default");
        int32 Next = 0;
        for (const auto& V : *Kit)
        {
            FString A;
            if (!V->TryGetString(A) || A.IsEmpty()) continue;
            const CireKitEditor::EKind K = CireKitEditor::KindOf(A);
            const int32 Slot = K == CireKitEditor::EKind::Ultimate ? FCireKitLoadout::UltimateSlot : K == CireKitEditor::EKind::Passive ? FCireKitLoadout::PassiveSlot
                : Next < FCireKitLoadout::ActiveSlots ? Next++ : INDEX_NONE;
            if (Slot != INDEX_NONE && L.Slots[Slot].IsEmpty()) L.Slots[Slot] = A;
        }
        C.Loadouts.Add(L);
    }
    if (!C.Find(C.DefaultLoadout)) C.DefaultLoadout = C.Loadouts.Num() ? C.Loadouts[0].Name : FString();
    return true;
}
void KitParseEffects(const FString& Champion, const TSharedPtr<FJsonObject>& Obj, FCireKitData& Out)
{
    for (const auto& E : Obj->Values)
    {
        const TSharedPtr<FJsonObject>* O = nullptr;
        FCireKitEffectPlacement P;
        if (E.Value->TryGetObject(O) && O && KitParsePlacement(*O, P)) Out.Effects.FindOrAdd(Champion).Add(FString(E.Key), P);
    }
}
}

bool CireKitEditor::ParseJson(const FString& Json, FCireKitData& Out, FString& Error)
{
    TSharedPtr<FJsonObject> Root;
    if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Root) || !Root) { Error = TEXT("not a JSON object"); return false; }
    FCireKitData Parsed;
    const TSharedPtr<FJsonObject>* Profiles = nullptr;
    if (Root->TryGetObjectField(TEXT("profiles"), Profiles) && Profiles)
        for (const auto& PP : (*Profiles)->Values)
        {
            const TSharedPtr<FJsonObject>* PObj = nullptr;
            const FString PName = FString(PP.Key).TrimStartAndEnd();
            if (!KitValidName(PName) || !PP.Value->TryGetObject(PObj) || !PObj) { Error = FString::Printf(TEXT("profile '%s' is not an object"), *PName); return false; }
            if (Parsed.FindProfile(PName)) continue;
            FCireKitProfile Profile; Profile.Name = PName;
            const TSharedPtr<FJsonObject>* Champs = nullptr;
            if ((*PObj)->TryGetObjectField(TEXT("champions"), Champs) && Champs)
                for (const auto& CP : (*Champs)->Values)
                {
                    const TSharedPtr<FJsonObject>* CObj = nullptr;
                    const FString Id(CP.Key);
                    if (Id.IsEmpty() || Id.Len() > 64 || !CP.Value->TryGetObject(CObj) || !CObj) { Error = FString::Printf(TEXT("champion '%s' is not an object"), *Id); return false; }
                    FCireKitChampion C;
                    if (!KitParseChampion(Id, *CObj, C, Error)) return false;
                    if (C.Loadouts.Num()) Profile.Champions.Add(Id, C);
                }
            Parsed.Profiles.Add(MoveTemp(Profile));
        }
    // schemaVersion 1: "champions" at the root = the Standard profile, with effects inside each champion.
    const TSharedPtr<FJsonObject>* Legacy = nullptr;
    if (Root->TryGetObjectField(TEXT("champions"), Legacy) && Legacy && !Parsed.FindProfile(StandardProfile))
    {
        FCireKitProfile Profile; Profile.Name = StandardProfile;
        for (const auto& CP : (*Legacy)->Values)
        {
            const TSharedPtr<FJsonObject>* CObj = nullptr;
            const FString Id(CP.Key);
            if (Id.IsEmpty() || !CP.Value->TryGetObject(CObj) || !CObj) { Error = FString::Printf(TEXT("champion '%s' is not an object"), *Id); return false; }
            FCireKitChampion C;
            if (!KitParseChampion(Id, *CObj, C, Error)) return false;
            if (C.Loadouts.Num() && C.Loadouts[0].Count()) Profile.Champions.Add(Id, C);
            const TSharedPtr<FJsonObject>* Eff = nullptr;
            if ((*CObj)->TryGetObjectField(TEXT("effects"), Eff) && Eff) KitParseEffects(Id, *Eff, Parsed);
        }
        Parsed.Profiles.Add(MoveTemp(Profile));
    }
    const TSharedPtr<FJsonObject>* Effects = nullptr;
    if (Root->TryGetObjectField(TEXT("effects"), Effects) && Effects)
        for (const auto& CE : (*Effects)->Values)
        {
            const TSharedPtr<FJsonObject>* CObj = nullptr;
            if (CE.Value->TryGetObject(CObj) && CObj) KitParseEffects(FString(CE.Key), *CObj, Parsed);
        }
    // Standard always exists and comes first.
    const int32 Std = Parsed.Profiles.IndexOfByPredicate([](const FCireKitProfile& P) { return P.Name.Equals(StandardProfile, ESearchCase::IgnoreCase); });
    if (Std == INDEX_NONE) { FCireKitProfile S; S.Name = StandardProfile; Parsed.Profiles.Insert(S, 0); }
    else if (Std > 0) { FCireKitProfile S = Parsed.Profiles[Std]; Parsed.Profiles.RemoveAt(Std); Parsed.Profiles.Insert(S, 0); }
    Parsed.Profiles[0].Name = StandardProfile;
    Out = MoveTemp(Parsed);
    Error.Reset();
    return true;
}

FString CireKitEditor::ToJson(const FCireKitData& In)
{
    TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
    Root->SetNumberField(TEXT("schemaVersion"), 2);
    Root->SetStringField(TEXT("about"), TEXT("Hero Creator (Champion Select > HERO CREATOR): kit profiles -> champions -> named loadout presets (buttons 1-6, R, P; 'default' = the one the game uses), plus per-champion effect placements. Docs/KitEditor.md"));
    TSharedRef<FJsonObject> Profiles = MakeShared<FJsonObject>();
    for (const FCireKitProfile& Profile : In.Profiles)
    {
        TSharedRef<FJsonObject> PObj = MakeShared<FJsonObject>();
        TSharedRef<FJsonObject> Champs = MakeShared<FJsonObject>();
        TArray<FString> Ids; Profile.Champions.GetKeys(Ids); Ids.Sort();
        for (const FString& Id : Ids)
        {
            const FCireKitChampion& C = Profile.Champions[Id];
            if (C.Loadouts.IsEmpty()) continue;
            TSharedRef<FJsonObject> CObj = MakeShared<FJsonObject>();
            CObj->SetStringField(TEXT("default"), C.Default() ? C.Default()->Name : FString());
            CObj->SetBoolField(TEXT("grantOnDraft"), C.bGrantOnDraft);
            if (!C.Updated.IsEmpty()) CObj->SetStringField(TEXT("updated"), C.Updated);
            TArray<TSharedPtr<FJsonValue>> Presets;
            for (const FCireKitLoadout& L : C.Loadouts)
            {
                TSharedRef<FJsonObject> LObj = MakeShared<FJsonObject>();
                LObj->SetStringField(TEXT("name"), L.Name);
                TSharedRef<FJsonObject> Slots = MakeShared<FJsonObject>();
                for (int32 S = 0; S < L.Slots.Num() && S < FCireKitLoadout::SlotCount; ++S) if (!L.Slots[S].IsEmpty()) Slots->SetStringField(KitSlotKey(S), L.Slots[S]);
                LObj->SetObjectField(TEXT("slots"), Slots);
                Presets.Add(MakeShared<FJsonValueObject>(LObj));
            }
            CObj->SetArrayField(TEXT("presets"), Presets);
            Champs->SetObjectField(Id, CObj);
        }
        PObj->SetObjectField(TEXT("champions"), Champs);
        Profiles->SetObjectField(Profile.Name, PObj);
    }
    Root->SetObjectField(TEXT("profiles"), Profiles);
    TSharedRef<FJsonObject> Effects = MakeShared<FJsonObject>();
    TArray<FString> Champions; In.Effects.GetKeys(Champions); Champions.Sort();
    for (const FString& Id : Champions)
    {
        TSharedRef<FJsonObject> CObj = MakeShared<FJsonObject>();
        TArray<FString> Keys; In.Effects[Id].GetKeys(Keys); Keys.Sort();
        for (const FString& K : Keys) if (!In.Effects[Id][K].IsDefault()) CObj->SetObjectField(K, KitPlacementJson(In.Effects[Id][K]));
        if (CObj->Values.Num()) Effects->SetObjectField(Id, CObj);
    }
    Root->SetObjectField(TEXT("effects"), Effects);
    FString Out;
    const auto Writer = TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create(&Out);
    FJsonSerializer::Serialize(Root, Writer);
    return Out + TEXT("\n");
}

bool CireKitEditor::Reload()
{
    GKitDataLoaded = true;
    FString Json, Error;
    FCireKitData Parsed;
    if (!FPaths::FileExists(DataPath())) { ParseJson(TEXT("{}"), GKitData, Error); return true; }
    if (!FFileHelper::LoadFileToString(Json, *DataPath()) || !ParseJson(Json, Parsed, Error))
    {
        UE_LOG(LogCireKitEditor, Error, TEXT("CIRE_KIT_TEMPLATES_REJECTED %s (keeping the previous data)"), *Error);
        if (GKitData.Profiles.IsEmpty()) ParseJson(TEXT("{}"), GKitData, Error);
        return false;
    }
    GKitData = MoveTemp(Parsed);
    int32 Loadouts = 0;
    for (const FCireKitProfile& P : GKitData.Profiles) for (const auto& C : P.Champions) Loadouts += C.Value.Loadouts.Num();
    UE_LOG(LogCireKitEditor, Display, TEXT("CIRE_KIT_TEMPLATES_LOADED profiles=%d loadouts=%d placed=%d"), GKitData.Profiles.Num(), Loadouts, GKitData.Effects.Num());
    return true;
}

bool CireKitEditor::Save(const FCireKitData& In, FString* Error)
{
    FCireKitData Clean = In;
    for (FCireKitProfile& P : Clean.Profiles)
        for (auto It = P.Champions.CreateIterator(); It; ++It)
        {
            for (FCireKitLoadout& L : It.Value().Loadouts) Compact(L);
            if (It.Value().Loadouts.IsEmpty()) It.RemoveCurrent();
            else if (!It.Value().Find(It.Value().DefaultLoadout)) It.Value().DefaultLoadout = It.Value().Loadouts[0].Name;
        }
    for (auto It = Clean.Effects.CreateIterator(); It; ++It)
    {
        for (auto E = It.Value().CreateIterator(); E; ++E) if (E.Value().IsDefault()) E.RemoveCurrent();
        if (It.Value().IsEmpty()) It.RemoveCurrent();
    }
    // Round-trip through the parser: the saved file is exactly what the game reads.
    FCireKitData Check;
    FString Why;
    const FString Json = ToJson(Clean);
    if (!ParseJson(Json, Check, Why)) { if (Error) *Error = TEXT("Internal: ") + Why; return false; }
    if (!FFileHelper::SaveStringToFile(Json, *DataPath(), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
    {
        if (Error) *Error = TEXT("Cannot write Content/Data/ChampionKitTemplates.json (read-only?)");
        return false;
    }
    GKitData = MoveTemp(Check);
    GKitDataLoaded = true;
    CireAbilityDB::Reload(); // purchasable lists pick the loadouts up at once
    UE_LOG(LogCireKitEditor, Display, TEXT("CIRE_KIT_TEMPLATES_SAVED profiles=%d"), GKitData.Profiles.Num());
    return true;
}

// ------------------------------------------------------------------ profiles and game modes
FString CireKitEditor::ProfileForMode(const FString& ModeKitProfile)
{
    const FString Want = ModeKitProfile.TrimStartAndEnd();
    if (const FCireKitProfile* P = Want.IsEmpty() ? nullptr : Data().FindProfile(Want)) return P->Name;
    return StandardProfile;
}
FString CireKitEditor::ModeKitProfile(const UWorld* World)
{
    // waves-modes: the host's game type (replicated ACireGameState::WavePreset) -> WavePresets.json "kitProfile".
    const ACireGameState* GS = World ? World->GetGameState<ACireGameState>() : nullptr;
    const FCireWavePreset* Preset = GS && !GS->WavePreset.IsNone() ? CireWaveDirector::FindPreset(GS->WavePreset) : nullptr;
    return Preset ? Preset->KitProfile : FString();
}
FString CireKitEditor::ActiveProfile(const UWorld* World)
{
    if (!GKitForcedProfile.IsEmpty()) return ProfileForMode(GKitForcedProfile);
    FString Cmd;
    if (FParse::Value(FCommandLine::Get(), TEXT("CireKitProfile="), Cmd)) return ProfileForMode(Cmd);
    return ProfileForMode(ModeKitProfile(World));
}
void CireKitEditor::DebugForceProfile(const FString& Profile) { GKitForcedProfile = Profile; }
const FCireKitChampion* CireKitEditor::ResolveChampion(const FString& Profile, const FString& ChampionId, FString* OutProfile)
{
    const FString Names[] = {Profile, FString(StandardProfile)};
    for (const FString& Name : Names)
        if (const FCireKitProfile* P = Data().FindProfile(Name))
            if (const FCireKitChampion* C = P->Champions.Find(ChampionId); C && C->Default() && C->Default()->Count() > 0)
            {
                if (OutProfile) *OutProfile = P->Name;
                return C;
            }
    return nullptr;
}
const FCireKitLoadout* CireKitEditor::ResolveLoadout(const FString& Profile, const FString& ChampionId, FString* OutProfile)
{
    const FCireKitChampion* C = ResolveChampion(Profile, ChampionId, OutProfile);
    return C ? C->Default() : nullptr;
}

// ------------------------------------------------------------------ skill buttons
CireKitEditor::EKind CireKitEditor::KindOf(const FString& AbilityId)
{
    if (const FCireAbilityDef* Def = CireAbilityDB::Find(AbilityId))
        return Def->IsUltimate() ? EKind::Ultimate : Def->IsPassive() ? EKind::Passive : EKind::Active;
    const auto Kind = CireSkillShop::KindOf(AbilityId);
    return Kind == Cires::Items::ShopSkillKind::Ultimate ? EKind::Ultimate : Kind == Cires::Items::ShopSkillKind::Passive ? EKind::Passive : EKind::Active;
}
bool CireKitEditor::SlotAccepts(int32 Slot, const FString& AbilityId)
{
    if (Slot < 0 || Slot >= FCireKitLoadout::SlotCount || !CireAbilityDB::Find(AbilityId)) return false;
    if (Slot >= FCireKitLoadout::ActiveSlots) return true; // R / passive button: anything, with a warning
    return KindOf(AbilityId) == EKind::Active;
}
FString CireKitEditor::SlotWarning(int32 Slot, const FString& AbilityId)
{
    if (AbilityId.IsEmpty() || !CireAbilityDB::Find(AbilityId)) return FString();
    const EKind K = KindOf(AbilityId);
    if (Slot == FCireKitLoadout::UltimateSlot && K != EKind::Ultimate)
        return K == EKind::Passive ? TEXT("A passive on R: it works as a passive (nothing to press).")
                                   : TEXT("Not an ultimate: in a match it is bound with the actives, after key 6.");
    if (Slot == FCireKitLoadout::PassiveSlot && K != EKind::Passive)
        return K == EKind::Ultimate ? TEXT("An ultimate on the passive button: in a match it is bound to R.")
                                    : TEXT("An active on the passive button: in a match it is bound with the actives.");
    return FString();
}
int32 CireKitEditor::TargetSlot(const FCireKitLoadout& L, const FString& AbilityId, int32 Preferred)
{
    if (Preferred != INDEX_NONE && SlotAccepts(Preferred, AbilityId)) return Preferred;
    const EKind K = KindOf(AbilityId);
    if (K == EKind::Ultimate) return FCireKitLoadout::UltimateSlot;
    if (K == EKind::Passive) return FCireKitLoadout::PassiveSlot;
    for (int32 S = 0; S < FCireKitLoadout::ActiveSlots; ++S) if (L.Slots.IsValidIndex(S) && (L.Slots[S].IsEmpty() || L.Slots[S] == AbilityId)) return S;
    return INDEX_NONE;
}
bool CireKitEditor::Assign(FCireKitLoadout& L, int32 Slot, const FString& AbilityId, FString* Why)
{
    L.Slots.SetNum(FCireKitLoadout::SlotCount);
    if (!SlotAccepts(Slot, AbilityId))
    {
        if (Why) *Why = !CireAbilityDB::Find(AbilityId) ? TEXT("Unknown ability.") : KindOf(AbilityId) == EKind::Ultimate ? TEXT("Ultimates go on R.") : TEXT("Passives go on the passive button.");
        return false;
    }
    for (FString& S : L.Slots) if (S == AbilityId) S.Reset();
    L.Slots[Slot] = AbilityId;
    Compact(L);
    if (Why) *Why = SlotWarning(L.Slots.IndexOfByKey(AbilityId), AbilityId);
    return true;
}
void CireKitEditor::Compact(FCireKitLoadout& L)
{
    L.Slots.SetNum(FCireKitLoadout::SlotCount);
    TSet<FString> Seen;
    for (FString& S : L.Slots)
    {
        if (S.IsEmpty()) continue;
        if (!CireAbilityDB::Find(S) || Seen.Contains(S)) { S.Reset(); continue; }
        Seen.Add(S);
    }
    TArray<FString> Actives;
    for (int32 S = 0; S < FCireKitLoadout::ActiveSlots; ++S) if (!L.Slots[S].IsEmpty() && KindOf(L.Slots[S]) == EKind::Active) Actives.Add(L.Slots[S]);
    for (int32 S = 0; S < FCireKitLoadout::ActiveSlots; ++S) L.Slots[S] = Actives.IsValidIndex(S) ? Actives[S] : FString();
}

// ------------------------------------------------------------------ game integration
void CireKitEditor::MergeIntoKits(TMap<FString, FCireChampionKit>& Kits)
{
    for (const FCireKitProfile& Profile : Data().Profiles)
        for (const auto& Pair : Profile.Champions)
            for (const FCireKitLoadout& L : Pair.Value.Loadouts)
            {
                const TArray<FString> Skills = L.Skills();
                if (Skills.IsEmpty()) continue;
                FCireChampionKit* K = Kits.Find(Pair.Key);
                if (!K)
                {
                    // A champion without an Ability DB kit (a new roster row): its loadouts are its purchasable list.
                    const FCireChampionProfile* P = CireChampionRoster::Find(Pair.Key);
                    FCireChampionKit New;
                    New.Name = P ? P->DisplayName : Pair.Key;
                    if (P) for (const FString& R : P->Roles) New.Roles.Add(R.ToUpper() == TEXT("HEALER") || R.ToUpper() == TEXT("SUPPORT") ? TEXT("HEAL") : R.ToUpper() == TEXT("DAMAGE") ? TEXT("DPS") : R.ToUpper());
                    New.PrimaryRole = New.Roles.Num() ? New.Roles[0] : TEXT("DPS");
                    K = &Kits.Add(Pair.Key, New);
                }
                for (const FString& Id : Skills)
                {
                    const FCireAbilityDef* Def = CireAbilityDB::Find(Id);
                    if (!Def) continue;
                    K->Purchasable.AddUnique(Id);
                    if (Def->IsImplemented()) K->PurchasableImplemented.AddUnique(Id);
                }
            }
}

int32 CireKitEditor::GrantOnDraft(ACireHero* Hero)
{
    if (!Hero || !Hero->HasAuthority() || Hero->ChampionProfileId.IsEmpty()) return 0;
    FString From;
    const FCireKitChampion* C = ResolveChampion(ActiveProfile(Hero->GetWorld()), Hero->ChampionProfileId, &From);
    if (!C || !C->bGrantOnDraft || !C->Default()) return 0;
    FCireKitLoadout L = *C->Default();
    Compact(L);
    int32 Granted = 0;
    TArray<FString> Skipped;
    for (const FString& Id : L.Skills())
    {
        const FCireAbilityDef* Def = CireAbilityDB::Find(Id);
        if (!Def || !Def->IsImplemented() || Hero->Skills.Contains(Id) || Hero->Skills.Num() >= Cires::MaxSkills) { Skipped.Add(Id); continue; }
        const EKind Kind = KindOf(Id);
        const Cires::SkillDefinition Skill{KitUtf8(Id), KitUtf8(Def->Name),
            Kind == EKind::Ultimate ? Cires::SkillKind::Ultimate : Kind == EKind::Passive ? Cires::SkillKind::Passive : Cires::SkillKind::Active};
        // Kind capacity (6 actives / 1 ultimate / 1 passive) still holds: an extra active placed on R does not fit a 7th.
        if (Hero->Progression.Schedule == Cires::SkillSchedule::Shop) { if (!Cires::AddPurchasedSkill(Hero->Progression, Skill)) { Skipped.Add(Id); continue; } }
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
        Hero->Notice = FString::Printf(TEXT("Champion bound with its %s loadout (%d skills)."), *C->Default()->Name, Granted);
    }
    UE_LOG(LogCireKitEditor, Display, TEXT("CIRE_KIT_GRANT champion=%s profile=%s loadout=%s granted=%d skipped=%s"), *Hero->ChampionProfileId, *From,
        *C->Default()->Name, Granted, *FString::Join(Skipped, TEXT(",")));
    return Granted;
}

const FCireKitEffectPlacement* CireKitEditor::Placement(const FString& ChampionId, const FString& AbilityId)
{
    const TMap<FString, FCireKitEffectPlacement>* Map = Data().Effects.Find(ChampionId);
    return Map ? Map->Find(AbilityId) : nullptr;
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
    if (!World || !System || Skill.IsNone() || Data().Effects.IsEmpty() || !CireFabVFX::Enabled()) return nullptr;
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

// ------------------------------------------------------------------ pool (every ability, Skill Shop sections)
bool CireKitEditor::MatchesSearch(const FCireAbilityDef& Def, const FString& Search)
{
    const FString Needle = Search.TrimStartAndEnd();
    if (Needle.IsEmpty()) return true;
    TArray<FString> Words;
    Needle.ParseIntoArrayWS(Words);
    for (const FString& W : Words)
    {
        bool bHit = Def.Name.Contains(W) || Def.Id.Contains(W) || Def.School.Contains(W) || Def.Section.Contains(W) || Def.Kind.Contains(W);
        for (const FString& T : Def.Types) bHit |= T.Contains(W);
        for (const FString& T : Def.EffectTags) bHit |= T.Contains(W);
        for (const FString& C : Def.Categories) bHit |= C.Contains(W);
        if (!bHit) return false; // every word must match somewhere
    }
    return true;
}
TArray<TPair<int32, TArray<const FCireAbilityDef*>>> CireKitEditor::Pool(const FPoolFilter& Filter)
{
    const int32 Sections = CireShopUI::SkillSectionCount();
    TArray<TArray<const FCireAbilityDef*>> By;
    By.SetNum(Sections);
    const FCireChampionKit* Kit = Filter.OnlyChampion.IsEmpty() ? nullptr : CireAbilityDB::Kit(Filter.OnlyChampion);
    for (const FCireAbilityDef& Def : CireAbilityDB::All())
    {
        const int32 S = FMath::Clamp(CireShopUI::SkillSectionOf(Def.Id), 0, Sections - 1);
        if ((Filter.HiddenSections & (1u << S)) || !MatchesSearch(Def, Filter.Search)) continue;
        if (Filter.Kind >= 0 && static_cast<int32>(KindOf(Def.Id)) != Filter.Kind) continue;
        if (!Filter.Role.IsEmpty() && !Def.Types.Contains(Filter.Role)) continue;
        if (!Filter.OnlyChampion.IsEmpty() && !(Kit && Kit->Purchasable.Contains(Def.Id))) continue;
        By[S].Add(&Def);
    }
    TArray<TPair<int32, TArray<const FCireAbilityDef*>>> Out;
    for (int32 S = 0; S < Sections; ++S)
    {
        if (By[S].IsEmpty()) continue;
        By[S].StableSort([](const FCireAbilityDef& A, const FCireAbilityDef& B) { return A.Name < B.Name; });
        Out.Add({S, MoveTemp(By[S])});
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
