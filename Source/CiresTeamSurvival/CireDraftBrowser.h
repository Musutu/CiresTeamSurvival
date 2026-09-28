#pragma once
// champ-select: roster browser logic for the large champion roster (authored + Paragon heroes).
//
// Pure data logic, no drawing, so it is unit-tested (RunTests) and shared by the draft screen and its probes:
//   - filters: role (tank / DPS / support, hybrids included), primary stat, source (authored / Paragon), favourites
//   - search: case-insensitive, every word must match (name, class, race, or a role / stat / source keyword)
//   - sort: by role, name, difficulty or source; favourites are pinned first
//   - paging: a fitted grid of fixed-size portrait tiles, the page that shows a given hero
//   - skins: cycling Default + N reskins
//   - favourites: per-user, Saved/CireDraftFavourites.json (local, never replicated)
//   - FLru: a small most-recently-used key list (asset caches in CireDraftAssets)
#include "CoreMinimal.h"

struct FCireChampionProfile;
class ACireGameMode;

namespace CireDraftBrowser
{
    enum class ERole : uint8 { All, Tank, Damage, Support };
    enum class EStat : uint8 { Any, Strength, Agility, Intelligence };
    enum class ESource : uint8 { Any, Authored, Paragon };
    enum class ESort : uint8 { Role, Name, Difficulty, Source, Count };

    /** Role bits (tank 1, damage 2, support 4), primary role index (0 tank, 1 damage, 2 support). */
    struct FEntry
    {
        FString Id, Name, ClassType, Race, PrimaryStat;
        uint8 RoleMask = 0;
        int32 PrimaryRole = 1;
        int32 Difficulty = 2;
        bool bParagon = false;
        int32 Order = 0; // roster (data) order
    };
    struct FQuery
    {
        ERole Role = ERole::All;
        EStat Stat = EStat::Any;
        ESource Source = ESource::Any;
        ESort Sort = ESort::Role;
        FString Search;
        bool bFavouritesOnly = false;
        bool bPinFavourites = true;
    };

    CIRESTEAMSURVIVAL_API FEntry MakeEntry(const FCireChampionProfile& Profile, int32 Order);
    /** Entries for the whole roster (cached; rebuilt when the roster count or first/last id changes). */
    CIRESTEAMSURVIVAL_API const TArray<FEntry>& RosterEntries();

    CIRESTEAMSURVIVAL_API bool MatchesSearch(const FEntry& Entry, const FString& Search);
    CIRESTEAMSURVIVAL_API bool Matches(const FEntry& Entry, const FQuery& Query, const TSet<FString>& Favourites);
    /** Indices into Entries that pass the query, in display order. */
    CIRESTEAMSURVIVAL_API TArray<int32> Filter(const TArray<FEntry>& Entries, const FQuery& Query, const TSet<FString>& Favourites);
    CIRESTEAMSURVIVAL_API const TCHAR* SortLabel(ESort Sort);
    CIRESTEAMSURVIVAL_API const TCHAR* StatLabel(EStat Stat);
    CIRESTEAMSURVIVAL_API const TCHAR* SourceLabel(ESource Source);

    /** Grid of fixed-size tiles fitted into W x H: TileH = TileW * Aspect. */
    struct FGridFit { int32 Cols = 1, Rows = 1; float TileW = 0, TileH = 0; int32 PageSize() const { return Cols * Rows; } };
    CIRESTEAMSURVIVAL_API FGridFit FitGrid(float W, float H, float Gap, float MinTileW, float MaxTileW, float Aspect);
    CIRESTEAMSURVIVAL_API int32 PageCount(int32 Items, int32 PageSize);
    CIRESTEAMSURVIVAL_API int32 PageOf(int32 Index, int32 PageSize);
    CIRESTEAMSURVIVAL_API int32 ClampPage(int32 Page, int32 Items, int32 PageSize);

    /** Skin cycling over Default (0) + SkinCount reskins, wrapping. */
    CIRESTEAMSURVIVAL_API int32 CycleSkin(int32 Current, int32 Step, int32 SkinCount);

    /** Local favourites (loaded lazily). Toggle saves immediately. */
    CIRESTEAMSURVIVAL_API const TSet<FString>& Favourites();
    CIRESTEAMSURVIVAL_API bool IsFavourite(const FString& Id);
    CIRESTEAMSURVIVAL_API void ToggleFavourite(const FString& Id);
    /** Tests: redirect the favourites file (empty = default). */
    CIRESTEAMSURVIVAL_API void SetFavouritesFileForTests(const FString& Path);

    /** Most-recently-used key list with a fixed capacity. Touch returns true on a hit; evicted keys are reported. */
    class CIRESTEAMSURVIVAL_API FLru
    {
    public:
        explicit FLru(int32 InCapacity = 8) : Capacity(FMath::Max(1, InCapacity)) {}
        bool Touch(const FString& Key, TArray<FString>* OutEvicted = nullptr);
        bool Contains(const FString& Key) const { return Keys.Contains(Key); }
        bool Remove(const FString& Key) { return Keys.Remove(Key) > 0; }
        int32 Num() const { return Keys.Num(); }
        int32 GetCapacity() const { return Capacity; }
        void SetCapacity(int32 InCapacity, TArray<FString>* OutEvicted = nullptr);
        /** Oldest first. */
        const TArray<FString>& GetKeys() const { return Keys; }
    private:
        int32 Capacity;
        TArray<FString> Keys;
    };

    /** Hover debounce: the heavy load for a hovered id starts only after it stayed hovered for DelaySeconds. */
    struct FDebounce
    {
        FString Pending, Settled;
        double Since = 0;
        /** Feed the currently hovered id (empty = none). Returns true on the frame the id settles. */
        bool Update(const FString& Hovered, double Now, double DelaySeconds);
    };

#if !UE_BUILD_SHIPPING
    CIRESTEAMSURVIVAL_API bool RunTests(ACireGameMode* Mode);
#endif
}
