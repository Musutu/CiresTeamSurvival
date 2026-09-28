// ability-tuner (Playtest 6 section M): override layer, profiles, authority and replication. See CireAbilityTuner.h
// and Docs/AbilityTuner.md. Ships in every build configuration.
#include "CireAbilityTuner.h"
#include "CireAbilityTunerState.h"
#include "CireAbilityDB.h"
#include "CireAbilityExpansion.h"
#include "CireAbilityShapes.h"
#include "CireSignatureSkills.h"
#include "CireGame.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HAL/FileManager.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Net/UnrealNetwork.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

DEFINE_LOG_CATEGORY_STATIC(LogCireTuner, Log, All);

namespace
{
// ---------------------------------------------------------------------------------------------- file rows (cache)
struct FTunerFiles
{
    bool bLoaded = false;
    TMap<FString, TSharedPtr<FJsonObject>> Rows;
    TArray<TSharedPtr<FJsonValue>> Schools;
};
FTunerFiles GFiles;
FCireTuningSet GActive, GStartup;
uint32 GVersion = 1;
bool GStartupLoaded = false;
TWeakObjectPtr<ACireAbilityTunerState> GServerState;

FSimpleMulticastDelegate& ChangedDelegate() { static FSimpleMulticastDelegate D; return D; }

TSharedPtr<FJsonObject> ReadJsonFile(const FString& Path)
{
    FString Text; TSharedPtr<FJsonObject> Root;
    if (!FFileHelper::LoadFileToString(Text, *Path) || Text.Len() > 8 * 1024 * 1024) return nullptr;
    if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) || !Root.IsValid()) return nullptr;
    return Root;
}
FString WriteJson(const TSharedPtr<FJsonObject>& Root, bool bPretty)
{
    FString Out;
    if (bPretty) FJsonSerializer::Serialize(Root.ToSharedRef(), TJsonWriterFactory<>::Create(&Out));
    else FJsonSerializer::Serialize(Root.ToSharedRef(), TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Out));
    return Out;
}

void LoadFiles()
{
    if (GFiles.bLoaded) return;
    GFiles = FTunerFiles(); GFiles.bLoaded = true;
    TSet<FString> SchoolNames;
    for (const TCHAR* File : {TEXT("Data/Abilities.json"), TEXT("Data/AbilitiesExpansion.json")})
    {
        const TSharedPtr<FJsonObject> Root = ReadJsonFile(FPaths::Combine(FPaths::ProjectContentDir(), File));
        if (!Root) continue;
        const TArray<TSharedPtr<FJsonValue>>* Schools = nullptr;
        if (Root->TryGetArrayField(TEXT("schools"), Schools))
            for (const auto& V : *Schools) { FString S; if (V->TryGetString(S) && !SchoolNames.Contains(S)) { SchoolNames.Add(S); GFiles.Schools.Add(V); } }
        const TSharedPtr<FJsonObject>* Abilities = nullptr;
        if (!Root->TryGetObjectField(TEXT("abilities"), Abilities)) continue;
        for (const auto& Pair : (*Abilities)->Values)
        {
            const TSharedPtr<FJsonObject>* Row = nullptr;
            if (Pair.Value->TryGetObject(Row) && !GFiles.Rows.Contains(FString(Pair.Key))) GFiles.Rows.Add(FString(Pair.Key), *Row); // base rows win (as in CireAbilityDB)
        }
    }
}

TSharedPtr<FJsonObject> CloneObject(const TSharedPtr<FJsonObject>& O);
TSharedPtr<FJsonValue> CloneValue(const TSharedPtr<FJsonValue>& V)
{
    if (!V.IsValid()) return V;
    if (V->Type == EJson::Object) return MakeShared<FJsonValueObject>(CloneObject(V->AsObject()));
    if (V->Type == EJson::Array) { TArray<TSharedPtr<FJsonValue>> A; for (const auto& X : V->AsArray()) A.Add(CloneValue(X)); return MakeShared<FJsonValueArray>(A); }
    return V; // scalars are replaced, never mutated
}
TSharedPtr<FJsonObject> CloneObject(const TSharedPtr<FJsonObject>& O)
{
    auto R = MakeShared<FJsonObject>();
    if (O) for (const auto& P : O->Values) R->Values.Add(P.Key, CloneValue(P.Value));
    return R;
}

// Synthetic fields: numbers the row may omit but the parser reads (default 1).
bool IsSyntheticPath(const FString& Path) { return Path == TEXT("level15.scale") || Path == TEXT("level15.durationScale"); }
// Row numbers the runtime ignores (hidden from the editor): base.castTime (the parser reads the top-level castTime) and
// construct tech recipes (parsed once into the Constructs tables).
bool IsHiddenPath(const FString& Path) { return Path == TEXT("base.castTime") || Path.StartsWith(TEXT("recipe.construct.")); }

TSharedPtr<FJsonValue> Child(const TSharedPtr<FJsonValue>& Cur, const FString& Part)
{
    if (!Cur.IsValid()) return nullptr;
    if (Cur->Type == EJson::Object) return Cur->AsObject()->TryGetField(Part);
    if (Cur->Type == EJson::Array && Part.IsNumeric()) { const auto& A = Cur->AsArray(); const int32 I = FCString::Atoi(*Part); return A.IsValidIndex(I) ? A[I] : nullptr; }
    return nullptr;
}
/** Sets an existing numeric leaf (or a synthetic one whose parent object exists). */
bool SetNumberAtPath(const TSharedPtr<FJsonObject>& Root, const FString& Path, double Value)
{
    TArray<FString> Parts; Path.ParseIntoArray(Parts, TEXT("."));
    if (Parts.IsEmpty() || !FMath::IsFinite(Value)) return false;
    TSharedPtr<FJsonValue> Cur = MakeShared<FJsonValueObject>(Root);
    for (int32 I = 0; I + 1 < Parts.Num(); ++I) Cur = Child(Cur, Parts[I]);
    if (!Cur.IsValid()) return false;
    const FString& Leaf = Parts.Last();
    if (Cur->Type == EJson::Object)
    {
        const TSharedPtr<FJsonObject> O = Cur->AsObject();
        const TSharedPtr<FJsonValue> Old = O->TryGetField(Leaf);
        if (!(Old.IsValid() && Old->Type == EJson::Number) && !IsSyntheticPath(Path)) return false;
        O->SetNumberField(Leaf, Value); return true;
    }
    if (Cur->Type == EJson::Array && Leaf.IsNumeric())
    {
        TArray<TSharedPtr<FJsonValue>> A = Cur->AsArray(); const int32 I = FCString::Atoi(*Leaf);
        if (!A.IsValidIndex(I) || A[I]->Type != EJson::Number) return false;
        // Arrays of numbers are values: rebuild through the parent is not needed for rows (no numeric arrays today).
        return false;
    }
    return false;
}
void CollectNumbers(const TSharedPtr<FJsonValue>& V, const FString& Path, TArray<TPair<FString, double>>& Out)
{
    if (!V.IsValid()) return;
    if (V->Type == EJson::Number) { if (!IsHiddenPath(Path)) Out.Emplace(Path, V->AsNumber()); return; }
    if (V->Type == EJson::Object) for (const auto& P : V->AsObject()->Values) CollectNumbers(P.Value, Path.IsEmpty() ? FString(P.Key) : Path + TEXT(".") + FString(P.Key), Out);
    if (V->Type == EJson::Array) { const auto& A = V->AsArray(); for (int32 I = 0; I < A.Num(); ++I) CollectNumbers(A[I], Path + TEXT(".") + FString::FromInt(I), Out); }
}

FString Humanize(const FString& Key)
{
    FString Out;
    for (int32 I = 0; I < Key.Len(); ++I)
    {
        const TCHAR C = Key[I];
        if (I > 0 && FChar::IsUpper(C) && !FChar::IsUpper(Key[I - 1])) Out += TEXT(' ');
        Out += I == 0 ? FChar::ToUpper(C) : FChar::ToLower(C);
    }
    return Out;
}

void Bump() { ++GVersion; ChangedDelegate().Broadcast(); }

bool BuildRowJson(const FString& Id, const FCireAbilityOverride& O, FCireAbilityDef& OutRow, TSharedPtr<FJsonObject>& OutJson, FString& Error)
{
    const TSharedPtr<FJsonObject> Raw = CireAbilityTuner::FileRow(Id);
    if (!Raw) { Error = TEXT("Unknown ability ") + Id; return false; }
    TSharedPtr<FJsonObject> Row = CloneObject(Raw);
    for (const auto& F : O.Fields)
        if (!SetNumberAtPath(Row, F.Key, F.Value)) { Error = FString::Printf(TEXT("%s has no number \"%s\""), *Id, *F.Key); return false; }
    if (O.Name.IsSet())
    {
        const FString Name = O.Name->TrimStartAndEnd();
        if (Name.IsEmpty() || Name.Len() > 48) { Error = TEXT("A name needs 1-48 characters."); return false; }
        Row->SetStringField(TEXT("name"), Name);
    }
    if (O.Description.IsSet())
    {
        if (O.Description->Len() > 600) { Error = TEXT("Descriptions stay under 600 characters."); return false; }
        Row->SetStringField(TEXT("description"), *O.Description);
    }
    if (O.VfxScale.IsSet() && !(*O.VfxScale >= .1f && *O.VfxScale <= 5.f)) { Error = TEXT("VFX scale must be 0.1-5."); return false; }
    auto Doc = MakeShared<FJsonObject>();
    Doc->SetNumberField(TEXT("schemaVersion"), 1);
    Doc->SetArrayField(TEXT("schools"), GFiles.Schools);
    auto Abilities = MakeShared<FJsonObject>(); Abilities->SetObjectField(Id, Row);
    Doc->SetObjectField(TEXT("abilities"), Abilities);
    Doc->SetObjectField(TEXT("champions"), MakeShared<FJsonObject>());
    TArray<FCireAbilityDef> Parsed; TMap<FString, FCireChampionKit> Kits; TMap<FName, TArray<FCireModifier>> Mods; FString ParseError;
    if (!CireAbilityDB::ParseJson(WriteJson(Doc, false), Parsed, Kits, Mods, ParseError) || Parsed.Num() != 1)
    { Error = ParseError.IsEmpty() ? TEXT("Row rejected") : ParseError; return false; }
    OutRow = MoveTemp(Parsed[0]);
    if (const double* Cast = O.Fields.Find(TEXT("castTime"))) // a cast-time override beats CastRules
    {
        OutRow.CastTime = OutRow.Base.CastTime = static_cast<float>(*Cast);
        if (!Row->HasField(TEXT("castWhileMoving"))) OutRow.bCastWhileMoving = OutRow.CastTime <= 0;
        OutRow.CastRule = FName(TEXT("tuned"));
    }
    OutJson = Row;
    return true;
}

bool ApplyOne(const FString& Id, const FCireAbilityOverride& O, FString& Error)
{
    FCireAbilityDef Row; TSharedPtr<FJsonObject> Json;
    if (!BuildRowJson(Id, O, Row, Json, Error)) return false;
    if (!CireAbilityDB::ReplaceRow(Row)) { Error = TEXT("Ability not in the database: ") + Id; return false; }
    const TSharedPtr<FJsonObject>* Recipe = nullptr;
    if (Json->TryGetObjectField(TEXT("recipe"), Recipe))
    {
        bool bRecipe = false; for (const auto& F : O.Fields) bRecipe |= F.Key.StartsWith(TEXT("recipe."));
        CireAbilityExpansion::SetRecipeOverride(Id, bRecipe ? *Recipe : nullptr);
    }
    if (O.IsEmpty()) GActive.Remove(Id); else GActive.Add(Id, O);
    return true;
}

FCireTunerField MakeField(const FString& Id, const FString& Path, double Default)
{
    FCireTunerField F; F.Path = Path; F.Default = F.Value = Default; F.Label = CireAbilityTuner::FieldLabel(Id, Path);
    const FString Leaf = Path.Contains(TEXT(".")) ? Path.RightChop(Path.Find(TEXT("."), ESearchCase::CaseSensitive, ESearchDir::FromEnd) + 1) : Path;
    const FString L = Leaf.ToLower();
    F.Group = Path.StartsWith(TEXT("curve.")) ? TEXT("Level curve") : Path.StartsWith(TEXT("scaling.")) ? TEXT("Scaling") :
        Path.StartsWith(TEXT("effects.")) ? TEXT("Effects (CC, buffs, debuffs)") : Path.StartsWith(TEXT("void.")) ? TEXT("Void zone") :
        Path.StartsWith(TEXT("level15.")) ? TEXT("Level 15 bonus") : Path.StartsWith(TEXT("ultimateUpgrade.")) ? TEXT("Apotheosis upgrade") :
        Path.StartsWith(TEXT("recipe.")) ? TEXT("Delivery (DoTs, buffs, summons)") : TEXT("Core");
    const double A = FMath::Abs(Default);
    auto Range = [&](double Min, double Max, double Step) { F.Min = Min; F.Max = FMath::Max(Max, A); F.Step = Step; };
    const bool bSeconds = L.Contains(TEXT("duration")) || L.Contains(TEXT("seconds")) || L == TEXT("delay") || L == TEXT("warning") || L == TEXT("interval");
    if (Path == TEXT("castTime")) { Range(0, 10, .1); F.Help = TEXT("Seconds. A tuned cast time replaces the CastRules formula."); }
    else if (IsSyntheticPath(Path)) { Range(0, 5, .05); F.Help = Leaf == TEXT("scale") ? TEXT("Multiplies the level-15 bonus magnitude (burn, healing cut).") : TEXT("Multiplies every level-15 bonus duration (stun, slow, burn...)."); }
    else if (L == TEXT("magnitude") || L == TEXT("outermagnitude") || L == TEXT("pull") || L == TEXT("lifesteal") || L == TEXT("chance") || L == TEXT("threshold") ||
             L == TEXT("threatscale") || L == TEXT("cooldownfloorfraction") || L == TEXT("selfhealmaxhealthfraction") || L == TEXT("executebelow"))
    { Range(0, 1, .01); F.Help = TEXT("Fraction 0-1 (0.35 = 35%)."); }
    else if (bSeconds) { Range(0, FMath::Max(60.0, A * 3), .1); F.Help = TEXT("Seconds."); }
    else if (L.Contains(TEXT("radius")) || L.Contains(TEXT("range")) || L == TEXT("knockback") || L == TEXT("speed") || L == TEXT("movespeed"))
    { Range(0, FMath::Max(3000.0, A * 3), 10); F.Help = TEXT("Centimetres (radius values are authored sizes; the AoE scale grows them)."); }
    else if (L == TEXT("cooldown") || L == TEXT("mincooldownseconds")) { Range(0, FMath::Max(120.0, A * 3), .5); F.Help = TEXT("Seconds at level 1."); }
    else if (L.Contains(TEXT("cost"))) { Range(0, FMath::Max(300.0, A * 3), 1); F.Help = TEXT("At level 1; grows with the level curve."); }
    else if (L == TEXT("primary") || L == TEXT("dotpersecond") || L == TEXT("potency") || L.Contains(TEXT("scaling")) || L.Contains(TEXT("growth")))
    { Range(0, FMath::Max(10.0, A * 3), .05); F.Help = TEXT("Coefficient."); }
    else if (L == TEXT("hits") || L == TEXT("count") || L == TEXT("waves") || L == TEXT("visual")) { Range(0, FMath::Max(12.0, A * 2), 1); F.bInteger = true; }
    else if (A > 0 && A < 1.0001 && !FMath::IsNearlyEqual(A, FMath::RoundToDouble(A))) { Range(0, FMath::Max(2.0, A * 4), .01); }
    else { Range(0, FMath::Max(A >= 10 ? 500.0 : 20.0, A * 4), A >= 10 ? 1.0 : .1); if (L == TEXT("effect") || L == TEXT("base") || L == TEXT("damage") || L == TEXT("amount")) F.Help = TEXT("Headline number at level 1 (damage / heal / shield / potency)."); }
    if (F.Help.IsEmpty()) F.Help = Path;
    return F;
}

FString ProfileFileHeader() { return TEXT("CireAbilityOverrides"); }
TSharedPtr<FJsonObject> ProfileToJson(const FCireTuningProfile& P)
{
    auto J = MakeShared<FJsonObject>();
    if (!P.Notes.IsEmpty()) J->SetStringField(TEXT("notes"), P.Notes);
    auto A = MakeShared<FJsonObject>();
    TArray<FString> Ids; P.Abilities.GetKeys(Ids); Ids.Sort();
    for (const FString& Id : Ids) A->SetObjectField(Id, CireAbilityTuner::ToJson(P.Abilities[Id]));
    J->SetObjectField(TEXT("abilities"), A);
    return J;
}
bool ProfileFromJson(const FString& Name, const TSharedPtr<FJsonObject>& J, FCireTuningProfile& Out)
{
    if (!J) return false;
    Out = FCireTuningProfile(); Out.Name = Name; J->TryGetStringField(TEXT("notes"), Out.Notes);
    const TSharedPtr<FJsonObject>* A = nullptr;
    if (J->TryGetObjectField(TEXT("abilities"), A))
        for (const auto& Pair : (*A)->Values)
        {
            const TSharedPtr<FJsonObject>* O = nullptr; FCireAbilityOverride X;
            if (Pair.Value->TryGetObject(O) && CireAbilityTuner::FromJson(*O, X) && !X.IsEmpty()) Out.Abilities.Add(FString(Pair.Key), MoveTemp(X));
        }
    return true;
}
bool ReadProfilesRoot(const FString& Path, TSharedPtr<FJsonObject>& Root, FString* Error, bool bMissingOk)
{
    if (!IFileManager::Get().FileExists(*Path))
    {
        if (!bMissingOk) { if (Error) *Error = TEXT("No profiles file at ") + Path; return false; }
        Root = MakeShared<FJsonObject>(); Root->SetNumberField(TEXT("schemaVersion"), 1); Root->SetStringField(TEXT("profile"), ProfileFileHeader());
        Root->SetStringField(TEXT("startupProfile"), TEXT("")); Root->SetObjectField(TEXT("profiles"), MakeShared<FJsonObject>());
        return true;
    }
    Root = ReadJsonFile(Path); FString Header; double Schema = 0;
    if (!Root || !Root->TryGetNumberField(TEXT("schemaVersion"), Schema) || Schema != 1 || !Root->TryGetStringField(TEXT("profile"), Header) || Header != ProfileFileHeader())
    { if (Error) *Error = Path + TEXT(" is not an ability overrides file (schemaVersion 1, profile CireAbilityOverrides)."); return false; }
    if (!Root->HasTypedField<EJson::Object>(TEXT("profiles"))) Root->SetObjectField(TEXT("profiles"), MakeShared<FJsonObject>());
    return true;
}
bool WriteProfilesRoot(const FString& Path, const TSharedPtr<FJsonObject>& Root, FString* Error)
{
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true);
    if (!FFileHelper::SaveStringToFile(WriteJson(Root, true) + TEXT("\n"), *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
    { if (Error) *Error = TEXT("Could not write ") + Path; return false; }
    return true;
}
bool ValidProfileName(const FString& Name) { return !Name.IsEmpty() && Name.Len() <= 40 && !Name.Contains(TEXT("/")) && !Name.Contains(TEXT("\\")) && !Name.Contains(TEXT("..")); }

ACireGameState* CireState(const UWorld* World) { return World ? World->GetGameState<ACireGameState>() : nullptr; }
} // namespace

// ================================================================================================= FCireAbilityOverride
bool FCireAbilityOverride::operator==(const FCireAbilityOverride& O) const
{
    if (Fields.Num() != O.Fields.Num() || Name != O.Name || Description != O.Description || bEnabled != O.bEnabled) return false;
    if (VfxScale.IsSet() != O.VfxScale.IsSet() || (VfxScale.IsSet() && !FMath::IsNearlyEqual(*VfxScale, *O.VfxScale, 1e-4f))) return false;
    if (VfxTint.IsSet() != O.VfxTint.IsSet() || (VfxTint.IsSet() && !VfxTint->Equals(*O.VfxTint, 1e-3f))) return false;
    for (const auto& F : Fields) { const double* V = O.Fields.Find(F.Key); if (!V || !FMath::IsNearlyEqual(*V, F.Value, 1e-6)) return false; }
    return true;
}

// ================================================================================================= active set
const FCireTuningSet& CireAbilityTuner::Active() { return GActive; }
const FCireAbilityOverride* CireAbilityTuner::Find(const FString& Id) { return GActive.Find(Id); }
uint32 CireAbilityTuner::Version() { return GVersion; }
FSimpleMulticastDelegate& CireAbilityTuner::OnChanged() { return ChangedDelegate(); }
bool CireAbilityTuner::IsDisabled(const FString& Id) { const FCireAbilityOverride* O = GActive.Find(Id); return O && O->bEnabled.IsSet() && !*O->bEnabled; }
bool CireAbilityTuner::VfxFor(const FString& IdOrName, float& OutScale, FLinearColor& OutTint)
{
    if (GActive.IsEmpty()) return false;
    const FCireAbilityOverride* O = GActive.Find(IdOrName);
    if (!O) if (const FCireAbilityDef* D = CireAbilityDB::FindByName(IdOrName)) O = GActive.Find(D->Id);
    if (!O || (!O->VfxScale.IsSet() && !O->VfxTint.IsSet())) return false;
    OutScale = O->VfxScale.Get(1.f); OutTint = O->VfxTint.Get(FLinearColor(0, 0, 0, 0));
    return true;
}

bool CireAbilityTuner::BuildRow(const FString& Id, const FCireAbilityOverride& Override, FCireAbilityDef& OutRow, FString* Error)
{
    FString E; TSharedPtr<FJsonObject> Json;
    const bool bOk = BuildRowJson(Id, Override, OutRow, Json, E);
    if (Error) *Error = E;
    return bOk;
}
bool CireAbilityTuner::ApplyLocal(const FString& Id, const FCireAbilityOverride& Override, FString* Error)
{
    CireAbilityDB::All(); // loads the database (and this layer) first
    const FCireAbilityOverride* Current = GActive.Find(Id);
    if (Current && *Current == Override) return true;
    if (!Current && Override.IsEmpty()) return true;
    FString E;
    if (!ApplyOne(Id, Override, E)) { if (Error) *Error = E; UE_LOG(LogCireTuner, Warning, TEXT("CIRE_TUNER_REJECTED %s: %s"), *Id, *E); return false; }
    Bump();
    return true;
}
bool CireAbilityTuner::ApplySetLocal(const FCireTuningSet& Set, FString* Error)
{
    CireAbilityDB::All();
    TArray<FString> Errors; bool bChanged = false;
    TArray<FString> Old; GActive.GetKeys(Old);
    for (const FString& Id : Old) if (!Set.Contains(Id)) { FString E; if (ApplyOne(Id, FCireAbilityOverride(), E)) bChanged = true; else Errors.Add(E); }
    for (const auto& Pair : Set)
    {
        const FCireAbilityOverride* Cur = GActive.Find(Pair.Key);
        if (Cur ? *Cur == Pair.Value : Pair.Value.IsEmpty()) continue;
        FString E; if (ApplyOne(Pair.Key, Pair.Value, E)) bChanged = true; else Errors.Add(E);
    }
    if (bChanged) Bump();
    if (Error) *Error = FString::Join(Errors, TEXT("; "));
    return Errors.IsEmpty();
}
void CireAbilityTuner::ResetAllLocal() { ApplySetLocal(FCireTuningSet()); }

void CireAbilityTuner::OnDatabaseReloaded()
{
    GFiles.bLoaded = false; LoadFiles();
    if (!GStartupLoaded)
    {
        GStartupLoaded = true;
        FString Startup; TArray<FCireTuningProfile> Profiles; FString Ignored;
        // The startup profile is a shipped/authored balance layer: Saved's choice wins over Content's.
        const FString ContentPath = ProfilesPath(false), SavedPath = ProfilesPath(true);
        FString ContentStartup, SavedStartup;
        LoadProfiles(ContentPath, Profiles, &ContentStartup, &Ignored);
        if (IFileManager::Get().FileExists(*SavedPath)) { TArray<FCireTuningProfile> P; LoadProfiles(SavedPath, P, &SavedStartup, &Ignored); }
        Startup = SavedStartup.IsEmpty() ? ContentStartup : SavedStartup;
        FCireTuningProfile P;
        if (!Startup.IsEmpty() && FindProfile(Startup, P)) { GStartup = P.Abilities; GActive = P.Abilities; UE_LOG(LogCireTuner, Display, TEXT("CIRE_TUNER_STARTUP_PROFILE %s abilities=%d"), *Startup, GStartup.Num()); }
    }
    // The rows are fresh file rows again: re-apply every override (a failing one is dropped and logged).
    const FCireTuningSet Set = GActive; GActive.Reset();
    for (const auto& Pair : Set) { FString E; if (!ApplyOne(Pair.Key, Pair.Value, E)) UE_LOG(LogCireTuner, Warning, TEXT("CIRE_TUNER_DROPPED %s: %s"), *Pair.Key, *E); }
    Bump();
}

// ================================================================================================= fields
TSharedPtr<FJsonObject> CireAbilityTuner::FileRow(const FString& Id) { LoadFiles(); return GFiles.Rows.FindRef(Id); }

FString CireAbilityTuner::FieldLabel(const FString& Id, const FString& Path)
{
    static const TMap<FString, FString> Known = {
        {TEXT("castTime"), TEXT("Cast time (s)")}, {TEXT("threatScale"), TEXT("Threat kept (0 = drop)")},
        {TEXT("base.effect"), TEXT("Effect (headline)")}, {TEXT("base.manaCost"), TEXT("Mana cost")}, {TEXT("base.energyCost"), TEXT("Energy cost")},
        {TEXT("base.cooldown"), TEXT("Cooldown (s)")}, {TEXT("base.range"), TEXT("Range (cm)")}, {TEXT("base.radius"), TEXT("Radius / shape size")},
        {TEXT("base.duration"), TEXT("Duration (s)")}, {TEXT("scaling.base"), TEXT("Base amount")}, {TEXT("scaling.primary"), TEXT("x Primary stat")},
        {TEXT("scaling.dotPerSecond"), TEXT("DoT per second x Primary")}, {TEXT("scaling.potency"), TEXT("Potency % per Primary")},
        {TEXT("scaling.potencyCap"), TEXT("Potency cap %")}, {TEXT("level15.scale"), TEXT("Bonus magnitude x")}, {TEXT("level15.durationScale"), TEXT("Bonus duration x")}};
    if (const FString* K = Known.Find(Path)) return *K;
    TArray<FString> Parts; Path.ParseIntoArray(Parts, TEXT("."));
    if (Parts.Num() == 3 && Parts[0] == TEXT("effects"))
    {
        FString Type;
        if (const TSharedPtr<FJsonObject> Row = FileRow(Id))
        {
            const TArray<TSharedPtr<FJsonValue>>* A = nullptr; const int32 I = FCString::Atoi(*Parts[1]);
            if (Row->TryGetArrayField(TEXT("effects"), A) && A->IsValidIndex(I) && (*A)[I]->Type == EJson::Object) (*A)[I]->AsObject()->TryGetStringField(TEXT("type"), Type);
        }
        return FString::Printf(TEXT("%s %s: %s"), Type.IsEmpty() ? TEXT("Effect") : *Humanize(Type), *FString::FromInt(FCString::Atoi(*Parts[1]) + 1), *Humanize(Parts[2]).ToLower());
    }
    if (Parts.Num() >= 4 && Parts[0] == TEXT("ultimateUpgrade") && Parts[1] == TEXT("effects"))
        return FString::Printf(TEXT("Upgrade %d: %s"), FCString::Atoi(*Parts[2]) + 1, *Humanize(Parts.Last()).ToLower());
    return Humanize(Parts.Last());
}

TArray<FCireTunerField> CireAbilityTuner::Fields(const FString& Id)
{
    TArray<FCireTunerField> Out;
    const TSharedPtr<FJsonObject> Row = FileRow(Id); if (!Row) return Out;
    TArray<TPair<FString, double>> Numbers;
    CollectNumbers(MakeShared<FJsonValueObject>(Row), FString(), Numbers);
    if (Row->HasTypedField<EJson::Object>(TEXT("level15")))
    {
        const TSharedPtr<FJsonObject> L = Row->GetObjectField(TEXT("level15"));
        for (const TCHAR* K : {TEXT("scale"), TEXT("durationScale")}) if (!L->HasField(K)) Numbers.Emplace(FString(TEXT("level15.")) + K, 1.0);
    }
    const FCireAbilityOverride* O = GActive.Find(Id);
    static const TArray<FString> GroupOrder = {TEXT("Core"), TEXT("Scaling"), TEXT("Effects (CC, buffs, debuffs)"), TEXT("Delivery (DoTs, buffs, summons)"),
        TEXT("Void zone"), TEXT("Level 15 bonus"), TEXT("Apotheosis upgrade"), TEXT("Level curve")};
    for (const auto& N : Numbers)
    {
        FCireTunerField F = MakeField(Id, N.Key, N.Value);
        if (const double* V = O ? O->Fields.Find(N.Key) : nullptr) { F.Value = *V; F.bOverridden = true; F.Max = FMath::Max(F.Max, *V); }
        Out.Add(MoveTemp(F));
    }
    Out.StableSort([](const FCireTunerField& A, const FCireTunerField& B) { return GroupOrder.IndexOfByKey(A.Group) < GroupOrder.IndexOfByKey(B.Group); });
    return Out;
}

// ================================================================================================= JSON
TSharedPtr<FJsonObject> CireAbilityTuner::ToJson(const FCireAbilityOverride& O)
{
    auto J = MakeShared<FJsonObject>();
    if (O.Name.IsSet()) J->SetStringField(TEXT("name"), *O.Name);
    if (O.Description.IsSet()) J->SetStringField(TEXT("description"), *O.Description);
    if (O.bEnabled.IsSet()) J->SetBoolField(TEXT("enabled"), *O.bEnabled);
    if (O.VfxScale.IsSet()) J->SetNumberField(TEXT("vfxScale"), *O.VfxScale);
    if (O.VfxTint.IsSet())
    {
        TArray<TSharedPtr<FJsonValue>> C;
        for (float V : {O.VfxTint->R, O.VfxTint->G, O.VfxTint->B}) C.Add(MakeShared<FJsonValueNumber>(FMath::RoundToDouble(V * 1000.0) / 1000.0));
        J->SetArrayField(TEXT("vfxTint"), C);
    }
    if (!O.Fields.IsEmpty())
    {
        auto F = MakeShared<FJsonObject>(); TArray<FString> Keys; O.Fields.GetKeys(Keys); Keys.Sort();
        for (const FString& K : Keys) F->SetNumberField(K, O.Fields[K]);
        J->SetObjectField(TEXT("fields"), F);
    }
    return J;
}
bool CireAbilityTuner::FromJson(const TSharedPtr<FJsonObject>& J, FCireAbilityOverride& Out)
{
    Out = FCireAbilityOverride(); if (!J) return false;
    FString S; bool B = false; double D = 0;
    if (J->TryGetStringField(TEXT("name"), S)) Out.Name = S;
    if (J->TryGetStringField(TEXT("description"), S)) Out.Description = S;
    if (J->TryGetBoolField(TEXT("enabled"), B)) Out.bEnabled = B;
    if (J->TryGetNumberField(TEXT("vfxScale"), D) && FMath::IsFinite(D)) Out.VfxScale = FMath::Clamp(static_cast<float>(D), .1f, 5.f);
    const TArray<TSharedPtr<FJsonValue>>* Tint = nullptr;
    if (J->TryGetArrayField(TEXT("vfxTint"), Tint) && Tint->Num() >= 3)
        Out.VfxTint = FLinearColor(FMath::Clamp((float)(*Tint)[0]->AsNumber(), 0.f, 4.f), FMath::Clamp((float)(*Tint)[1]->AsNumber(), 0.f, 4.f), FMath::Clamp((float)(*Tint)[2]->AsNumber(), 0.f, 4.f), 1.f);
    const TSharedPtr<FJsonObject>* F = nullptr;
    if (J->TryGetObjectField(TEXT("fields"), F))
        for (const auto& P : (*F)->Values) { double V = 0; if (P.Value->TryGetNumber(V) && FMath::IsFinite(V)) Out.Fields.Add(FString(P.Key), V); }
    return true;
}
FString CireAbilityTuner::SerializeSet(const FCireTuningSet& Set)
{
    auto Root = MakeShared<FJsonObject>(); auto A = MakeShared<FJsonObject>();
    TArray<FString> Ids; Set.GetKeys(Ids); Ids.Sort();
    for (const FString& Id : Ids) A->SetObjectField(Id, ToJson(Set[Id]));
    Root->SetObjectField(TEXT("abilities"), A);
    return WriteJson(Root, false);
}
bool CireAbilityTuner::DeserializeSet(const FString& Text, FCireTuningSet& Out, FString* Error)
{
    Out.Reset();
    if (Text.IsEmpty()) return true;
    TSharedPtr<FJsonObject> Root;
    if (Text.Len() > 2 * 1024 * 1024 || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) || !Root.IsValid()) { if (Error) *Error = TEXT("Invalid tuning JSON"); return false; }
    FCireTuningProfile P; ProfileFromJson(TEXT(""), Root, P); Out = MoveTemp(P.Abilities);
    return true;
}

// ================================================================================================= profiles
FString CireAbilityTuner::ProfilesPath(bool bPackaged)
{
    return bPackaged ? FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Tuning/AbilityOverrides.json"))
                     : FPaths::Combine(FPaths::ProjectContentDir(), TEXT("Data/AbilityOverrides.json"));
}
FString CireAbilityTuner::DefaultProfilesPath()
{
#if WITH_EDITOR
    return ProfilesPath(false); // the editor (and editor -game sessions) author the shipped file
#else
    return ProfilesPath(true);  // packaged builds never write into Content
#endif
}
FString CireAbilityTuner::ExportDirectory() { return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Tuning/Exports")); }
FString CireAbilityTuner::ImportDirectory() { return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Tuning/Imports")); }

bool CireAbilityTuner::LoadProfiles(const FString& Path, TArray<FCireTuningProfile>& Out, FString* StartupProfile, FString* Error)
{
    Out.Reset(); TSharedPtr<FJsonObject> Root;
    if (!ReadProfilesRoot(Path, Root, Error, false)) return false;
    if (StartupProfile) Root->TryGetStringField(TEXT("startupProfile"), *StartupProfile);
    for (const auto& Pair : Root->GetObjectField(TEXT("profiles"))->Values)
    {
        const TSharedPtr<FJsonObject>* O = nullptr; FCireTuningProfile P;
        if (Pair.Value->TryGetObject(O) && ProfileFromJson(FString(Pair.Key), *O, P)) Out.Add(MoveTemp(P));
    }
    Out.Sort([](const FCireTuningProfile& A, const FCireTuningProfile& B) { return A.Name < B.Name; });
    return true;
}
bool CireAbilityTuner::SaveProfile(const FString& Path, const FCireTuningProfile& Profile, FString* Error)
{
    if (!ValidProfileName(Profile.Name)) { if (Error) *Error = TEXT("Profile names need 1-40 characters and no slashes."); return false; }
    TSharedPtr<FJsonObject> Root; if (!ReadProfilesRoot(Path, Root, Error, true)) return false;
    Root->GetObjectField(TEXT("profiles"))->SetObjectField(Profile.Name, ProfileToJson(Profile));
    return WriteProfilesRoot(Path, Root, Error);
}
bool CireAbilityTuner::DeleteProfile(const FString& Path, const FString& Name, FString* Error)
{
    TSharedPtr<FJsonObject> Root; if (!ReadProfilesRoot(Path, Root, Error, false)) return false;
    const TSharedPtr<FJsonObject> Profiles = Root->GetObjectField(TEXT("profiles"));
    if (!Profiles->HasField(Name)) { if (Error) *Error = TEXT("No profile ") + Name; return false; }
    Profiles->RemoveField(Name);
    FString Startup; if (Root->TryGetStringField(TEXT("startupProfile"), Startup) && Startup == Name) Root->SetStringField(TEXT("startupProfile"), TEXT(""));
    return WriteProfilesRoot(Path, Root, Error);
}
bool CireAbilityTuner::SetStartupProfile(const FString& Path, const FString& Name, FString* Error)
{
    TSharedPtr<FJsonObject> Root; if (!ReadProfilesRoot(Path, Root, Error, true)) return false;
    if (!Name.IsEmpty() && !Root->GetObjectField(TEXT("profiles"))->HasField(Name)) { if (Error) *Error = TEXT("Save the profile before making it the startup profile."); return false; }
    Root->SetStringField(TEXT("startupProfile"), Name);
    return WriteProfilesRoot(Path, Root, Error);
}
TArray<FCireTuningProfile> CireAbilityTuner::AllProfiles()
{
    TArray<FCireTuningProfile> Out, User;
    LoadProfiles(ProfilesPath(false), Out);
    if (ProfilesPath(true) != ProfilesPath(false) && LoadProfiles(ProfilesPath(true), User))
        for (FCireTuningProfile& P : User)
        {
            const int32 I = Out.IndexOfByPredicate([&](const FCireTuningProfile& X) { return X.Name == P.Name; });
            if (I == INDEX_NONE) Out.Add(MoveTemp(P)); else Out[I] = MoveTemp(P);
        }
    return Out;
}
bool CireAbilityTuner::FindProfile(const FString& Name, FCireTuningProfile& Out)
{
    for (FCireTuningProfile& P : AllProfiles()) if (P.Name == Name) { Out = MoveTemp(P); return true; }
    return false;
}
bool CireAbilityTuner::ExportProfile(const FCireTuningProfile& Profile, const FString& FilePath, FString* Error)
{
    auto Root = ProfileToJson(Profile);
    Root->SetNumberField(TEXT("schemaVersion"), 1); Root->SetStringField(TEXT("profile"), TEXT("CireAbilityTuningProfile")); Root->SetStringField(TEXT("name"), Profile.Name);
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(FilePath), true);
    if (!FFileHelper::SaveStringToFile(WriteJson(Root, true) + TEXT("\n"), *FilePath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM)) { if (Error) *Error = TEXT("Could not write ") + FilePath; return false; }
    return true;
}
bool CireAbilityTuner::ImportProfile(const FString& FilePath, FCireTuningProfile& Out, FString* Error)
{
    const TSharedPtr<FJsonObject> Root = ReadJsonFile(FilePath); FString Header, Name; double Schema = 0;
    if (!Root || !Root->TryGetNumberField(TEXT("schemaVersion"), Schema) || Schema != 1 || !Root->TryGetStringField(TEXT("profile"), Header) || Header != TEXT("CireAbilityTuningProfile"))
    { if (Error) *Error = FPaths::GetCleanFilename(FilePath) + TEXT(" is not an exported tuning profile."); return false; }
    if (!Root->TryGetStringField(TEXT("name"), Name) || !ValidProfileName(Name)) Name = FPaths::GetBaseFilename(FilePath).Left(40);
    ProfileFromJson(Name, Root, Out);
    // Only abilities this build knows survive an import.
    for (auto It = Out.Abilities.CreateIterator(); It; ++It) if (!FileRow(It.Key())) It.RemoveCurrent();
    return true;
}
TArray<FString> CireAbilityTuner::ImportCandidates()
{
    TArray<FString> Out;
    for (const FString& Dir : {ImportDirectory(), ExportDirectory()})
    {
        TArray<FString> Files; IFileManager::Get().FindFiles(Files, *FPaths::Combine(Dir, TEXT("*.json")), true, false);
        Files.Sort(); for (const FString& F : Files) Out.Add(FPaths::Combine(Dir, F));
    }
    return Out;
}

// ================================================================================================= authority / access
bool CireAbilityTuner::AllowedByDefault(bool bShippingBuild, const TCHAR* CommandLine)
{
    if (CommandLine && FParse::Param(CommandLine, TEXT("CireNoTuning"))) return false;
    if (CommandLine && FParse::Param(CommandLine, TEXT("CireAllowTuning"))) return true;
    return !bShippingBuild; // dev / editor builds: on; shipping: off unless the host turns it on (custom games)
}
bool CireAbilityTuner::IsAllowed(const UWorld* World) { const auto* S = ACireAbilityTunerState::Get(World); return S && S->bAllowTuning; }
bool CireAbilityTuner::CanTune(const APlayerController* Controller, FString* Why)
{
    auto No = [&](const TCHAR* Reason) { if (Why) *Why = Reason; return false; };
    const UWorld* World = Controller ? Controller->GetWorld() : nullptr;
    if (!World || !Controller->IsLocalController()) return No(TEXT("Only the host can tune abilities."));
    const ENetMode Mode = World->GetNetMode();
    if (Mode == NM_Client || !Controller->HasAuthority()) return No(TEXT("Only the host can tune abilities."));
    if (!ACireAbilityTunerState::Get(World)) return No(TEXT("The match has not started."));
    if (!IsAllowed(World)) return No(TEXT("Ability tuning is off for this match (game option \"Allow ability tuning\")."));
    return true;
}
bool CireAbilityTuner::CanChangeAllowed(const APlayerController* Controller, FString* Why)
{
    auto No = [&](const TCHAR* Reason) { if (Why) *Why = Reason; return false; };
    const UWorld* World = Controller ? Controller->GetWorld() : nullptr;
    if (!World || !Controller->IsLocalController() || World->GetNetMode() == NM_Client || !Controller->HasAuthority()) return No(TEXT("Only the host sets game options."));
    const auto* S = ACireAbilityTunerState::Get(World);
    if (!S) return No(TEXT("The match has not started."));
    if (S->bAllowLocked) return No(TEXT("This game mode fixes \"Allow ability tuning\"."));
#if UE_BUILD_SHIPPING
    // Release: a custom-game option, decided before the first wave like the progression mode.
    if (const ACireGameState* G = CireState(World); G && (G->Wave > 0 || G->Phase != 0)) return No(TEXT("\"Allow ability tuning\" can only change before the first wave."));
#endif
    return true;
}
bool CireAbilityTuner::SetAllowed(const APlayerController* Controller, bool bAllow, FString* Why)
{
    if (!CanChangeAllowed(Controller, Why)) return false;
    auto* S = ACireAbilityTunerState::Get(Controller->GetWorld());
    S->bAllowTuning = bAllow; S->ForceNetUpdate();
    UE_LOG(LogCireTuner, Display, TEXT("CIRE_TUNER_ALLOW %d"), bAllow ? 1 : 0);
    return true;
}
bool CireAbilityTuner::CommitAuthority(UWorld* World, const FString& Id, const FCireAbilityOverride& Override, FString* Why)
{
    if (!World || World->GetNetMode() == NM_Client) { if (Why) *Why = TEXT("Only the server applies overrides."); return false; }
    if (!ApplyLocal(Id, Override, Why)) return false;
    if (auto* S = ACireAbilityTunerState::Get(World)) S->Publish(S->ProfileName);
    return true;
}
bool CireAbilityTuner::CommitSetAuthority(UWorld* World, const FCireTuningSet& Set, const FString& ProfileName, FString* Why)
{
    if (!World || World->GetNetMode() == NM_Client) { if (Why) *Why = TEXT("Only the server applies overrides."); return false; }
    const bool bOk = ApplySetLocal(Set, Why);
    if (auto* S = ACireAbilityTunerState::Get(World)) S->Publish(ProfileName);
    return bOk;
}
bool CireAbilityTuner::Request(const APlayerController* Controller, const FString& Id, const FCireAbilityOverride& Override, FString* Why)
{
    return CanTune(Controller, Why) && CommitAuthority(Controller->GetWorld(), Id, Override, Why);
}
bool CireAbilityTuner::RequestSet(const APlayerController* Controller, const FCireTuningSet& Set, const FString& ProfileName, FString* Why)
{
    return CanTune(Controller, Why) && CommitSetAuthority(Controller->GetWorld(), Set, ProfileName, Why);
}
FString CireAbilityTuner::ActiveProfileName(const UWorld* World) { const auto* S = ACireAbilityTunerState::Get(World); return S ? S->ProfileName : FString(); }
int32 CireAbilityTuner::RemoteViewers(const UWorld* World)
{
    int32 N = 0;
    if (World) for (auto It = World->GetPlayerControllerIterator(); It; ++It) if (It->IsValid() && !(*It)->IsLocalController()) ++N;
    return N;
}

void CireAbilityTuner::InitializeServer(ACireGameMode* Mode)
{
    UWorld* World = Mode ? Mode->GetWorld() : nullptr;
    if (!World || World->GetNetMode() == NM_Client) return;
    CireAbilityDB::All();
    FActorSpawnParameters Params; Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    auto* S = ACireAbilityTunerState::Get(World);
    if (!S) S = World->SpawnActor<ACireAbilityTunerState>(ACireAbilityTunerState::StaticClass(), FTransform::Identity, Params);
    if (!S) { UE_LOG(LogCireTuner, Error, TEXT("CIRE_TUNER_STATE_SPAWN_FAILED")); return; }
    S->bAllowTuning = AllowedByDefault(UE_BUILD_SHIPPING != 0, FCommandLine::Get());
    S->bAllowLocked = FParse::Param(FCommandLine::Get(), TEXT("CireNoTuning"));
    // A new match starts from the startup profile (hand edits of an earlier match in this process are dropped).
    ApplySetLocal(GStartup);
    FString Profile;
    if (FParse::Value(FCommandLine::Get(), TEXT("CireTuningProfile="), Profile) && !Profile.IsEmpty()) ApplyModePreset(World, Profile, -1);
    else S->Publish(FString());
    if (World->GetNetMode() == NM_DedicatedServer && FParse::Param(FCommandLine::Get(), TEXT("CireNetServerProbe"))) PushNetProbeOverride(World);
    UE_LOG(LogCireTuner, Display, TEXT("CIRE_TUNER_READY allow=%d overrides=%d profile=%s"), S->bAllowTuning ? 1 : 0, GActive.Num(), *S->ProfileName);
}
bool CireAbilityTuner::ApplyModePreset(UWorld* World, const FString& TuningProfile, int32 AllowTuning, FString* Why)
{
    auto* S = ACireAbilityTunerState::Get(World);
    if (!S || !S->HasAuthority()) { if (Why) *Why = TEXT("No authoritative tuner state."); return false; }
    if (AllowTuning >= 0) { S->bAllowTuning = AllowTuning > 0; S->bAllowLocked = AllowTuning == 0; S->ForceNetUpdate(); }
    if (TuningProfile.IsEmpty()) return CommitSetAuthority(World, GStartup, FString(), Why);
    FCireTuningProfile P;
    if (!FindProfile(TuningProfile, P)) { if (Why) *Why = TEXT("No tuning profile ") + TuningProfile; UE_LOG(LogCireTuner, Warning, TEXT("CIRE_TUNER_PROFILE_MISSING %s"), *TuningProfile); return false; }
    UE_LOG(LogCireTuner, Display, TEXT("CIRE_TUNER_PROFILE %s abilities=%d"), *P.Name, P.Abilities.Num());
    return CommitSetAuthority(World, P.Abilities, P.Name, Why);
}
bool CireAbilityTuner::ApplyModePresetJson(UWorld* World, const TSharedPtr<FJsonObject>& Preset, FString* Why)
{
    if (!Preset) return false;
    FString Profile; Preset->TryGetStringField(TEXT("tuningProfile"), Profile);
    bool bAllow = false; const int32 Allow = Preset->TryGetBoolField(TEXT("allowTuning"), bAllow) ? (bAllow ? 1 : 0) : -1;
    if (Profile.IsEmpty() && Allow < 0) return true;
    return ApplyModePreset(World, Profile, Allow, Why);
}

// ================================================================================================= helpers
bool CireAbilityTuner::SpawnTargetDummy(const APlayerController* Controller, FString* Why)
{
    if (!CanTune(Controller, Why)) return false;
    auto* Hero = Cast<ACireHero>(Controller->GetPawn());
    auto* Mode = Controller->GetWorld()->GetAuthGameMode<ACireGameMode>();
    if (!Hero || !Mode) { if (Why) *Why = TEXT("Pick a champion first."); return false; }
    FActorSpawnParameters Params; Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
    const FVector At = Hero->GetActorLocation() + Hero->GetActorForwardVector().GetSafeNormal2D() * 450.f;
    auto* Dummy = Controller->GetWorld()->SpawnActor<ACireMonster>(ACireMonster::StaticClass(), At, (-Hero->GetActorForwardVector()).Rotation(), Params);
    if (!Dummy) { if (Why) *Why = TEXT("No room for a target dummy there."); return false; }
    Dummy->Lane = Hero->TeamId; Dummy->Health = Dummy->MaxHealth = 1000000.f; Dummy->Damage = 0;
    Dummy->MonsterName = TEXT("Target Dummy");
    if (Dummy->GetCharacterMovement()) Dummy->GetCharacterMovement()->DisableMovement();
    Mode->Monsters.Add(Dummy);
    Hero->Target = Dummy;
    return true;
}
bool CireAbilityTuner::CastNow(const APlayerController* Controller, const FString& Id, FString* Why)
{
    if (!CanTune(Controller, Why)) return false;
    auto* Hero = Cast<ACireHero>(Controller->GetPawn());
    auto* Mode = Controller->GetWorld()->GetAuthGameMode<ACireGameMode>();
    const FCireAbilityDef* D = CireAbilityDB::Find(Id);
    auto No = [&](const FString& Reason) { if (Why) *Why = Reason; return false; };
    if (!Hero || !Mode || !Hero->bDrafted) return No(TEXT("Pick a champion first."));
    if (!D || D->IsPassive()) return No(TEXT("Passives have nothing to cast."));
    if (!CireSignatureSkills::Handles(Id)) return No(TEXT("This skill casts through its champion kit: learn it in the Skill Shop to test it."));
    if (!Mode->IsCombatPhase()) return No(TEXT("Casting needs a combat phase (waves or arena)."));
    int32 Slot = Hero->Skills.IndexOfByKey(Id);
    if (Slot == INDEX_NONE) { Slot = Hero->Skills.Add(Id); Hero->Cooldowns.SetNum(Hero->Skills.Num()); } // host test grant
    if (!Hero->Cooldowns.IsValidIndex(Slot)) Hero->Cooldowns.SetNum(Hero->Skills.Num());
    Hero->Cooldowns[Slot] = 0; Hero->GlobalCooldown = 0; Hero->Mana = Hero->MaxMana; Hero->Energy = 100;
    const bool bHostile = IsValid(Hero->Target) && Hero->Target != Hero;
    Hero->CastAimPoint = bHostile ? Hero->Target->GetActorLocation() : Hero->GetActorLocation() + Hero->GetActorForwardVector().GetSafeNormal2D() * FMath::Min(450.f, D->Range > 0 ? D->Range : 450.f);
    Hero->bHasCastAim = true;
    const bool bCast = CireSignatureSkills::Cast(Hero, Slot, Id);
    Hero->bHasCastAim = false;
    if (!bCast) return No(Hero->Notice.IsEmpty() ? FString(TEXT("The cast was refused.")) : Hero->Notice);
    return true;
}

// ================================================================================================= probes
namespace { const TCHAR* ProbeId = TEXT("ember_lance"); const TCHAR* ProbeName = TEXT("Tuned Ember Lance"); const double ProbeCooldown = 3.5; }
void CireAbilityTuner::PushNetProbeOverride(UWorld* World)
{
    FCireAbilityOverride O; O.Name = FString(ProbeName); O.Fields.Add(TEXT("base.cooldown"), ProbeCooldown); O.VfxScale = 1.5f;
    FString Why; const bool bOk = CommitAuthority(World, ProbeId, O, &Why);
    UE_LOG(LogCireTuner, Display, TEXT("CIRE_NET_SERVER_TUNER_PUSH ok=%d %s"), bOk ? 1 : 0, *Why);
}
bool CireAbilityTuner::VerifyNetProbeOnClient(const APlayerController* Controller, FString* Why)
{
    const UWorld* World = Controller ? Controller->GetWorld() : nullptr;
    const auto* S = ACireAbilityTunerState::Get(World);
    const FCireAbilityDef* D = CireAbilityDB::Find(ProbeId);
    FString Denied;
    const bool bOk = S && S->Revision > 0 && D && D->Name == ProbeName && FMath::IsNearlyEqual(D->Base.Cooldown, static_cast<float>(ProbeCooldown)) &&
        CireAbilityDB::FindByName(TEXT("Ember Lance")) == D && CireAbilityDB::FindByName(ProbeName) == D && !CanTune(Controller, &Denied) && Version() > 1;
    if (Why) *Why = FString::Printf(TEXT("state=%d revision=%d name=%s cooldown=%.2f client_denied=\"%s\""), S ? 1 : 0, S ? S->Revision : -1, D ? *D->Name : TEXT("-"), D ? D->Base.Cooldown : -1.f, *Denied);
    return bOk;
}

// ================================================================================================= replicated state
ACireAbilityTunerState::ACireAbilityTunerState()
{
    bReplicates = true; bAlwaysRelevant = true; SetNetUpdateFrequency(4.f);
    PrimaryActorTick.bCanEverTick = false;
}
void ACireAbilityTunerState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(ACireAbilityTunerState, TuningJson); DOREPLIFETIME(ACireAbilityTunerState, Revision);
    DOREPLIFETIME(ACireAbilityTunerState, bAllowTuning); DOREPLIFETIME(ACireAbilityTunerState, bAllowLocked); DOREPLIFETIME(ACireAbilityTunerState, ProfileName);
}
void ACireAbilityTunerState::BeginPlay() { Super::BeginPlay(); if (HasAuthority()) GServerState = this; }
void ACireAbilityTunerState::EndPlay(const EEndPlayReason::Type Reason)
{
    // The set lives with the match: back to the startup profile when it ends (a client only when no server state
    // shares this process, e.g. PIE listen server).
    const bool bServer = HasAuthority();
    if (bServer && GServerState.Get() == this) GServerState.Reset();
    if (bServer || !GServerState.IsValid()) CireAbilityTuner::ApplySetLocal(GStartup);
    Super::EndPlay(Reason);
}
void ACireAbilityTunerState::OnRep_Tuning()
{
    if (HasAuthority()) return;
    FCireTuningSet Set; FString Error;
    if (!CireAbilityTuner::DeserializeSet(TuningJson, Set, &Error)) { UE_LOG(LogCireTuner, Error, TEXT("CIRE_TUNER_REPLICATION_REJECTED %s"), *Error); return; }
    CireAbilityTuner::ApplySetLocal(Set, &Error);
    UE_LOG(LogCireTuner, Display, TEXT("CIRE_TUNER_REPLICATED revision=%d overrides=%d profile=%s %s"), Revision, Set.Num(), *ProfileName, *Error);
}
void ACireAbilityTunerState::Publish(const FString& InProfileName)
{
    if (!HasAuthority()) return;
    TuningJson = CireAbilityTuner::SerializeSet(GActive); ProfileName = InProfileName; ++Revision;
    ForceNetUpdate();
}
ACireAbilityTunerState* ACireAbilityTunerState::Get(const UWorld* World)
{
    if (!World) return nullptr;
    if (ACireAbilityTunerState* S = GServerState.Get(); S && S->GetWorld() == World && !S->IsActorBeingDestroyed()) return S;
    for (TActorIterator<ACireAbilityTunerState> It(const_cast<UWorld*>(World)); It; ++It) if (!It->IsActorBeingDestroyed()) return *It;
    return nullptr;
}
