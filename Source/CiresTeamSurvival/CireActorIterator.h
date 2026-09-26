#pragma once
// town-perf: a drop-in for TActorIterator<T> (for (TCireActorIterator<T> It(World); It; ++It) ... *It / It->).
//
// Why: the game runs from the editor binaries (UnrealEditor -game, Play.cmd), where TActorIterator's WITH_EDITOR path
// copies EVERY actor of EVERY loaded level into a local array and set-intersects it with the class list on each
// construction. The Medieval Kingdom town streams ~113,000 actors (both realms), so each iterator cost ~0.4-0.6 ms and
// the monster AI, HUD and skills built hundreds per frame: ~70 ms of the ~130 ms game thread in a 10-hero, 48-monster
// fight (Insights, Docs/CastleTown.md "Performance"). This iterates the class's objects directly, like the cooked-game
// (non-editor) TActorIterator: cost proportional to the number of T, not to the size of the map.
//
// Same semantics as TActorIterator's defaults: only actors of this world, not pending kill, in a visible (active) level.
// Actors spawned while iterating are not visited (none of the call sites rely on that).
#include "CoreMinimal.h"
#include "Engine/Level.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "UObject/UObjectHash.h"

template <typename T>
class TCireActorIterator
{
public:
    explicit TCireActorIterator(const UWorld* InWorld, TSubclassOf<T> Class = T::StaticClass())
    {
        if (!InWorld || !*Class) { Index = 0; return; }
        TArray<UObject*> Objects;
        GetObjectsOfClass(*Class, Objects, true, RF_ClassDefaultObject, EInternalObjectFlags::Garbage);
        Actors.Reserve(Objects.Num());
        for (UObject* Object : Objects)
        {
            T* Actor = static_cast<T*>(Object);
            const ULevel* Level = Actor->GetLevel();
            if (!Level || Level->OwningWorld != InWorld || !(Level->bIsVisible || Level == InWorld->PersistentLevel)) continue;
            Actors.Add(Actor);
        }
        Advance();
    }
    void operator++() { Advance(); }
    explicit operator bool() const { return Actors.IsValidIndex(Index); }
    T* operator*() const { return Actors[Index]; }
    T* operator->() const { return Actors[Index]; }

private:
    void Advance()
    {
        // Skip actors destroyed by the loop body (TActorIterator's SkipPendingKill).
        do { ++Index; } while (Actors.IsValidIndex(Index) && !IsValid(Actors[Index]));
    }
    TArray<T*, TInlineAllocator<16>> Actors;
    int32 Index = -1;
};
