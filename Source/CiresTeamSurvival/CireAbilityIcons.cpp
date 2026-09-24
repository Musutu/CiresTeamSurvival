#include "CireAbilityIcons.h"
#include "CireUIStyle.h"
#include "Dom/JsonObject.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
struct FIconMeta { FString School; FLinearColor Accent = CireUIColors::Gold; };
const TMap<FString, FIconMeta>& Meta()
{
    static TMap<FString, FIconMeta> Table;
    static bool bLoaded = false;
    if (bLoaded) return Table;
    bLoaded = true;
    FString Json;
    TSharedPtr<FJsonObject> Root;
    const FString Path = FPaths::Combine(FPaths::ProjectContentDir(), TEXT("Data/AbilityIcons.json"));
    if (!FFileHelper::LoadFileToString(Json, *Path) || Json.Len() > 512 * 1024 ||
        !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Root) || !Root.IsValid()) return Table;
    const TSharedPtr<FJsonObject>* Icons = nullptr;
    if (!Root->TryGetObjectField(TEXT("icons"), Icons) || !Icons) return Table;
    for (const auto& Pair : (*Icons)->Values)
    {
        const TSharedPtr<FJsonObject>* Row = nullptr;
        if (!Pair.Value->TryGetObject(Row) || !Row) continue;
        FIconMeta M;
        FString Hex;
        (*Row)->TryGetStringField(TEXT("school"), M.School);
        if ((*Row)->TryGetStringField(TEXT("accent"), Hex) && Hex.Len() == 7 && Hex[0] == '#')
            M.Accent = FLinearColor::FromSRGBColor(FColor::FromHex(Hex));
        Table.Add(FString(Pair.Key), M);
    }
    return Table;
}
}

UTexture2D* CireAbilityIcons::Texture(const FString& Id) { return CireUIStyle::FindAbilityIcon(Id); }
FString CireAbilityIcons::School(const FString& Id) { const auto* M = Meta().Find(Id); return M ? M->School : FString(); }
FLinearColor CireAbilityIcons::Accent(const FString& Id) { const auto* M = Meta().Find(Id); return M ? M->Accent : CireUIColors::Gold; }
int32 CireAbilityIcons::KnownCount() { return Meta().Num(); }

void CireAbilityIcons::Draw(const FCireUIPainter& P, const FString& Id, float X, float Y, float Size, float Alpha)
{
    if (UTexture2D* Tex = Texture(Id)) { P.Tex(Tex, X, Y, Size, Size, FLinearColor(1, 1, 1, Alpha)); return; }
    const FLinearColor A = Accent(Id);
    P.Rect(X, Y, Size, Size, FLinearColor(A.R * .12f, A.G * .12f, A.B * .12f, Alpha));
    CireUIStyle::Sigil(P, Id, X + Size * .13f, Y + Size * .13f, Size * .74f, FLinearColor(A.R, A.G, A.B, Alpha));
}
