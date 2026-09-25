#include "CireItems.h"
#include "CireScalingKits.h" // scaling-kits
#include "CireSkillShop.h" // progression-shop: Skill Shop
#include "CireCrowdControl.h" // champion-draft: crowd control, timed casts, execute skills
#include "CireBuffs.h" // aura-vfx
// progression-shop: see CireItems.h, Docs/Items.md.
#include "CireGame.h"
#include "CireCombatEvents.h"
#include "CireThreat.h"
#include "CireSummon.h"
#include "CireChampionProfiles.h"
#include "CireNPCState.h"
#include "CireKeybindings.h"
#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Dom/JsonObject.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Net/UnrealNetwork.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/ConstructorHelpers.h"

DEFINE_LOG_CATEGORY_STATIC(LogCireItems, Log, All);

using namespace Cires::Items;

namespace
{
FCireItemData ItemData;
bool bDataLoaded = false;

std::string Utf8(const FString& Text) { return std::string(TCHAR_TO_UTF8(*Text)); }
FName ToName(const std::string& Text) { return FName(UTF8_TO_TCHAR(Text.c_str())); }

double Number(const TSharedPtr<FJsonObject>& Object, const TCHAR* Key, double Default = 0)
{
    double Value = Default;
    return Object.IsValid() && Object->TryGetNumberField(Key, Value) && FMath::IsFinite(Value) ? Value : Default;
}
FString Text(const TSharedPtr<FJsonObject>& Object, const TCHAR* Key)
{
    FString Value;
    if (Object.IsValid()) Object->TryGetStringField(Key, Value);
    return Value;
}
bool Flag(const TSharedPtr<FJsonObject>& Object, const TCHAR* Key, bool Default = false)
{
    bool Value = Default;
    if (Object.IsValid()) Object->TryGetBoolField(Key, Value);
    return Value;
}

bool ParseStats(const TSharedPtr<FJsonObject>& Object, StatBlock& Out, FString& Error, const FString& Owner)
{
    if (!Object.IsValid()) return true;
    for (const auto& Pair : Object->Values)
    {
        ItemStat Stat = ItemStat::Count;
        double Value = 0;
        if (!ParseStatKey(Utf8(FString(*Pair.Key)), Stat) || !Pair.Value.IsValid() || !Pair.Value->TryGetNumber(Value) || !FMath::IsFinite(Value))
        { Error = FString::Printf(TEXT("%s has unknown or invalid stat '%s'"), *Owner, *FString(*Pair.Key)); return false; }
        Out[Stat] = Value;
    }
    return true;
}

ACireGameMode* ModeOf(const AActor* Actor)
{
    return Actor && Actor->GetWorld() ? Actor->GetWorld()->GetAuthGameMode<ACireGameMode>() : nullptr;
}

double ServerNow(const UWorld* World)
{
    if (!World) return 0;
    if (const auto* State = World->GetGameState<ACireGameState>(); State && World->GetNetMode() == NM_Client)
        return State->GetServerWorldTimeSeconds();
    return World->GetTimeSeconds();
}

bool IsSummon(const AActor* Actor) { return Actor && Actor->IsA<ACireSummon>(); }

void HealDirect(ACireHero* Hero, float Amount)
{
    if (!IsValid(Hero) || Hero->bDead || !FMath::IsFinite(Amount) || Amount <= 0) return;
    Hero->Health = FMath::Min(Hero->MaxHealth, Hero->Health + Amount);
}

const TCHAR* ActionVerb(ECireShopAction Action)
{
    switch (Action)
    {
    case ECireShopAction::Buy: return TEXT("buy");
    case ECireShopAction::Sell: return TEXT("sell");
    case ECireShopAction::Undo: return TEXT("undo");
    case ECireShopAction::Use: return TEXT("use");
    case ECireShopAction::Loot: return TEXT("loot");
    case ECireShopAction::Teleport: return TEXT("teleport");
    default: return TEXT("swap");
    }
}
} // namespace

// ------------------------------------------------------------------ data
bool CireItems::ParseJson(const FString& Json, FCireItemData& Out, FString& Error)
{
    Out = FCireItemData();
    TSharedPtr<FJsonObject> Root;
    if (Json.Len() > 1024 * 1024 || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Root) || !Root.IsValid())
    { Error = TEXT("Items.json is not valid JSON"); return false; }
    if (Number(Root, TEXT("schemaVersion")) != 1) { Error = TEXT("Items.json schemaVersion must be 1"); return false; }
    const TSharedPtr<FJsonObject>* Shop = nullptr;
    if (Root->TryGetObjectField(TEXT("shopRules"), Shop))
    {
        Out.Shop.SellRatio = FMath::Clamp(Number(*Shop, TEXT("sellRatio"), .6), 0., 1.);
        Out.Shop.BuyAnywhereInPrep = Flag(*Shop, TEXT("buyAnywhereInPrep"), true);
        Out.Shop.TownShoppingDuringWaves = Flag(*Shop, TEXT("townShoppingDuringWaves"), false);
        Out.Shop.TownShoppingDuringRecovery = Flag(*Shop, TEXT("townShoppingDuringRecovery"), true);
        Out.Shop.TownRadius = FMath::Clamp(Number(*Shop, TEXT("townRadius"), 900), 100., 5000.);
    }
    const TSharedPtr<FJsonObject>* Teleport = nullptr;
    if (Root->TryGetObjectField(TEXT("teleport"), Teleport))
    {
        Out.Teleport.ChannelSeconds = FMath::Clamp(Number(*Teleport, TEXT("channelSeconds"), 6), 0., 30.);
        Out.Teleport.CooldownSeconds = FMath::Clamp(Number(*Teleport, TEXT("cooldownSeconds"), 120), 0., 900.);
        Out.Teleport.MoveTolerance = FMath::Clamp(Number(*Teleport, TEXT("moveTolerance"), 40), 5., 500.);
    }
    const TArray<TSharedPtr<FJsonValue>>* Items = nullptr;
    if (!Root->TryGetArrayField(TEXT("items"), Items) || Items->Num() == 0 || Items->Num() > 200)
    { Error = TEXT("Items.json needs 1-200 items"); return false; }
    for (const auto& Value : *Items)
    {
        const TSharedPtr<FJsonObject> Object = Value.IsValid() ? Value->AsObject() : nullptr;
        if (!Object.IsValid()) { Error = TEXT("Item entry is not an object"); return false; }
        ItemDef Item;
        Item.Id = Utf8(Text(Object, TEXT("id")));
        Item.Name = Utf8(Text(Object, TEXT("name")));
        Item.Lore = Utf8(Text(Object, TEXT("lore")));
        Item.Icon = Utf8(Text(Object, TEXT("icon")));
        const FString Owner = Text(Object, TEXT("id"));
        const FString Tier = Text(Object, TEXT("tier"));
        if (Tier == TEXT("consumable")) Item.Tier = ItemTier::Consumable;
        else if (Tier == TEXT("basic")) Item.Tier = ItemTier::Basic;
        else if (Tier == TEXT("epic")) Item.Tier = ItemTier::Epic;
        else if (Tier == TEXT("legendary")) Item.Tier = ItemTier::Legendary;
        else { Error = Owner + TEXT(" has an unknown tier"); return false; }
        const double Cost = Number(Object, TEXT("cost"), -1);
        if (Cost < 0 || Cost > 20000) { Error = Owner + TEXT(" has an invalid cost"); return false; }
        Item.RecipeCost = static_cast<int>(Cost);
        Item.Unique = Flag(Object, TEXT("unique"));
        Item.UniqueGroup = Utf8(Text(Object, TEXT("uniqueGroup")));
        Item.Purchasable = Flag(Object, TEXT("purchasable"), true);
        Item.Instant = Flag(Object, TEXT("instant"));
        Item.Belt = Flag(Object, TEXT("belt"));
        Item.MaxStack = static_cast<int>(Number(Object, TEXT("maxStack"), 1));
        const TArray<TSharedPtr<FJsonValue>>* List = nullptr;
        if (Object->TryGetArrayField(TEXT("components"), List))
            for (const auto& Entry : *List) Item.Components.push_back(Utf8(Entry->AsString()));
        if (Object->TryGetArrayField(TEXT("tags"), List))
            for (const auto& Entry : *List) Item.Tags.push_back(Utf8(Entry->AsString()));
        const TSharedPtr<FJsonObject>* Stats = nullptr;
        if (Object->TryGetObjectField(TEXT("stats"), Stats) && !ParseStats(*Stats, Item.Stats, Error, Owner)) return false;
        const TSharedPtr<FJsonObject>* Use = nullptr;
        if (Object->TryGetObjectField(TEXT("use"), Use))
        {
            if (!ParseEffectKind(Utf8(Text(*Use, TEXT("kind"))), Item.Use.Kind)) { Error = Owner + TEXT(" has an unknown use kind"); return false; }
            Item.Use.Name = Utf8(Text(*Use, TEXT("name")));
            Item.Use.Amount = Number(*Use, TEXT("amount"));
            Item.Use.Scaling = Number(*Use, TEXT("scaling"));
            Item.Use.Radius = Number(*Use, TEXT("radius"));
            Item.Use.Duration = Number(*Use, TEXT("duration"));
            Item.Use.Cooldown = Number(*Use, TEXT("cooldown"));
            const TSharedPtr<FJsonObject>* Buff = nullptr;
            if ((*Use)->TryGetObjectField(TEXT("buff"), Buff) && !ParseStats(*Buff, Item.Use.Buff, Error, Owner)) return false;
            Out.UseText.Add(FName(*Owner), Text(*Use, TEXT("text")));
        }
        const TArray<TSharedPtr<FJsonValue>>* Passives = nullptr;
        if (Object->TryGetArrayField(TEXT("passives"), Passives))
        {
            TArray<FString>& Texts = Out.PassiveText.FindOrAdd(FName(*Owner));
            for (const auto& Entry : *Passives)
            {
                const TSharedPtr<FJsonObject> P = Entry.IsValid() ? Entry->AsObject() : nullptr;
                Passive Entry2;
                if (!P.IsValid() || !ParsePassiveKind(Utf8(Text(P, TEXT("kind"))), Entry2.Kind)) { Error = Owner + TEXT(" has an unknown passive"); return false; }
                Entry2.Name = Utf8(Text(P, TEXT("name")));
                Entry2.Amount = Number(P, TEXT("amount"));
                Entry2.Threshold = Number(P, TEXT("threshold"));
                Entry2.Radius = Number(P, TEXT("radius"));
                Entry2.Duration = Number(P, TEXT("duration"));
                Entry2.Cooldown = Number(P, TEXT("cooldown"));
                Entry2.Count = static_cast<int>(Number(P, TEXT("count")));
                Item.Passives.push_back(Entry2);
                Texts.Add(Text(P, TEXT("name")) + TEXT(": ") + Text(P, TEXT("text")));
            }
        }
        Out.Order.Add(FName(*Owner));
        Out.Catalog.Items.push_back(Item);
    }
    const std::string CatalogError = Out.Catalog.Finalize();
    if (!CatalogError.empty()) { Error = UTF8_TO_TCHAR(CatalogError.c_str()); return false; }
    const TSharedPtr<FJsonObject>* Recommended = nullptr;
    if (Root->TryGetObjectField(TEXT("recommended"), Recommended))
        for (const auto& Role : (*Recommended)->Values)
        {
            const TSharedPtr<FJsonObject> Groups = Role.Value.IsValid() ? Role.Value->AsObject() : nullptr;
            TArray<TArray<FName>> Lists;
            for (const TCHAR* Group : {TEXT("starting"), TEXT("core"), TEXT("situational")})
            {
                TArray<FName> Ids;
                const TArray<TSharedPtr<FJsonValue>>* List = nullptr;
                if (Groups.IsValid() && Groups->TryGetArrayField(Group, List))
                    for (const auto& Entry : *List)
                    {
                        const FString Id = Entry->AsString();
                        if (!Out.Catalog.Find(Utf8(Id))) { Error = TEXT("Recommended list uses unknown item ") + Id; return false; }
                        Ids.Add(FName(*Id));
                    }
                Lists.Add(Ids);
            }
            Out.Recommended.Add(FString(*Role.Key), Lists);
        }
    Out.bValid = true;
    return true;
}

bool CireItems::Reload()
{
    FString Json, Error;
    const FString Path = FPaths::Combine(FPaths::ProjectContentDir(), TEXT("Data/Items.json"));
    FCireItemData Parsed;
    if (!FFileHelper::LoadFileToString(Json, *Path)) Error = TEXT("Cannot read Content/Data/Items.json");
    else ParseJson(Json, Parsed, Error);
    bDataLoaded = true;
    if (!Parsed.bValid)
    {
        UE_LOG(LogCireItems, Error, TEXT("CIRE_ITEMS_DATA_ERROR %s"), *Error);
        if (!ItemData.bValid) ItemData.Error = Error;
        return false;
    }
    ItemData = MoveTemp(Parsed);
    UE_LOG(LogCireItems, Display, TEXT("CIRE_ITEMS_LOADED items=%d"), ItemData.Order.Num());
    return true;
}

const FCireItemData& CireItems::Get()
{
    if (!bDataLoaded) Reload();
    return ItemData;
}

const ItemDef* CireItems::Find(FName Id)
{
    return Id.IsNone() ? nullptr : Get().Catalog.Find(Utf8(Id.ToString()));
}

FString CireItems::DisplayName(FName Id)
{
    const ItemDef* Item = Find(Id);
    return Item ? FString(UTF8_TO_TCHAR(Item->Name.c_str())) : Id.ToString();
}

FString CireItems::RoleKey(const ACireHero* Hero)
{
    if (!Hero) return TEXT("physical");
    if (Hero->HasChampionRole(TEXT("tank"))) return TEXT("tank");
    if (Hero->HasChampionRole(TEXT("healer")) || Hero->HasChampionRole(TEXT("support"))) return TEXT("support");
    return Hero->PrimaryStat() == Cires::PrimaryStat::Intelligence ? TEXT("caster") : TEXT("physical");
}

FName CireItems::LegacyItem(int32 Index)
{
    static const FName Legacy[] = {TEXT("tome_of_insight"), TEXT("tome_of_ascendance"), TEXT("rusted_longsword"), TEXT("sandglass_charm")};
    return Index >= 0 && Index < 4 ? Legacy[Index] : NAME_None;
}

UCireInventory* CireItems::InventoryOf(const AActor* Actor)
{
    const auto* Hero = Cast<ACireHero>(Actor);
    return Hero && !IsSummon(Hero) ? Hero->Inventory.Get() : nullptr;
}

Totals CireItems::TotalsOf(const ACireHero* Hero)
{
    const UCireInventory* Inventory = InventoryOf(Hero);
    return Inventory ? Inventory->Totals() : Totals();
}

bool CireItems::IsBasicAttack(const AActor* Source, const FString& Name)
{
    if (Name == TEXT("Basic attack") || Name == TEXT("Monster attack")) return true;
    if (Cast<ACireHero>(Source))
        return Name.EndsWith(TEXT(" strike")) || Name == TEXT("Thrown lance") || Name == TEXT("Bow shot") ||
            Name == TEXT("Thrown axe") || Name == TEXT("Arcane bolt");
    if (const auto* Monster = Cast<ACireMonster>(Source))
        if (const auto* State = Monster->NPCState.Get())
            if (const auto* Arch = State->Archetype())
                if (const auto* Basic = Arch->BasicAttack()) return Basic->Name == Name;
    return false;
}

// ------------------------------------------------------------------ stat pipeline
void CireItems::AddAttributes(const ACireHero* Hero, Cires::StatBlock& Attributes)
{
    const Totals T = TotalsOf(Hero);
    Attributes.Strength += FMath::RoundToInt(T.Stats.Get(ItemStat::Strength));
    Attributes.Agility += FMath::RoundToInt(T.Stats.Get(ItemStat::Agility));
    Attributes.Intelligence += FMath::RoundToInt(T.Stats.Get(ItemStat::Intelligence));
}

double CireItems::CooldownReductionFor(ACireHero* Hero, float CurrentCDR)
{
    UCireInventory* Inventory = InventoryOf(Hero);
    if (!Inventory) return FMath::Clamp(CurrentCDR, 0.f, static_cast<float>(MaxCooldownReductionTotal));
    // CDR also holds non-item pure CDR (developer fixtures, legacy relics); keep it as the base.
    const double Base = FMath::Clamp(static_cast<double>(CurrentCDR) - Inventory->ItemCDRApplied, 0., MaxCooldownReductionTotal);
    const double Total = FMath::Min(MaxCooldownReductionTotal, Base + Inventory->Totals().Stats.Get(ItemStat::CooldownReduction) / 100.);
    Inventory->ItemCDRApplied = static_cast<float>(Total - Base);
    return Total;
}

void CireItems::ApplyDerived(ACireHero* Hero)
{
    if (!InventoryOf(Hero)) return;
    const Totals T = TotalsOf(Hero);
    Hero->MaxHealth += static_cast<float>(T.Stats.Get(ItemStat::Health));
    Hero->MaxMana += static_cast<float>(T.Stats.Get(ItemStat::Mana));
    Hero->CriticalChance = FMath::Clamp(Hero->CriticalChance + static_cast<float>(T.Stats.Get(ItemStat::CritChance) / 100.), 0.f, 1.f);
    Hero->CriticalMultiplier += static_cast<float>(T.CritMultiplierBonus);
}

float CireItems::AttackDamageBonus(const ACireHero* Hero)
{
    return static_cast<float>(TotalsOf(Hero).Stats.Get(ItemStat::AttackDamage));
}

float CireItems::AttackSpeedBonus(const ACireHero* Hero)
{
    return static_cast<float>(TotalsOf(Hero).Stats.Get(ItemStat::AttackSpeed) / 100.);
}

float CireItems::MoveSpeedMultiplier(const ACireHero* Hero)
{
    const UCireInventory* Inventory = InventoryOf(Hero);
    if (!Inventory) return 1.f;
    float Bonus = static_cast<float>(Inventory->Totals().Stats.Get(ItemStat::MoveSpeed) / 100.);
    const double Now = Inventory->Now();
    for (const auto& Buff : Inventory->Buffs)
        if (Buff.EndsAt > Now)
            if (const ItemDef* Item = Find(Buff.Id); Item && Item->Use.Kind == EffectKind::Haste)
                Bonus += static_cast<float>(Item->Use.Amount / 100.);
    return FMath::Clamp(1.f + Bonus, .5f, 2.f);
}

void CireItems::ApplyRegen(ACireHero* Hero, float DeltaSeconds)
{
    if (!IsValid(Hero) || Hero->bDead || !InventoryOf(Hero) || !FMath::IsFinite(DeltaSeconds) || DeltaSeconds <= 0) return;
    const Totals T = TotalsOf(Hero);
    Hero->Health = FMath::Min(Hero->MaxHealth, Hero->Health + DeltaSeconds * static_cast<float>(T.Stats.Get(ItemStat::HealthRegen)));
    Hero->Mana = FMath::Min(Hero->MaxMana, Hero->Mana + DeltaSeconds * static_cast<float>(T.Stats.Get(ItemStat::ManaRegen)));
    Hero->Energy = FMath::Min(100.f, Hero->Energy + DeltaSeconds * static_cast<float>(T.Stats.Get(ItemStat::EnergyRegen)));
}

// ------------------------------------------------------------------ combat hooks
float CireItems::ModifyOutgoingDamage(AActor* Source, AActor* Target, float Amount, const FString& AbilityName)
{
    if (!FMath::IsFinite(Amount) || Amount <= 0) return Amount;
    auto* Hero = Cast<ACireHero>(Source);
    UCireInventory* Inventory = InventoryOf(Hero);
    if (Inventory)
    {
        const Totals& T = Inventory->Totals();
        if (IsBasicAttack(Source, AbilityName))
        {
            if (T.EveryNthCount > 0 && ++Inventory->BasicHitCounter % T.EveryNthCount == 0) Amount += static_cast<float>(T.EveryNthDamage);
        }
        else Amount *= (1.f + static_cast<float>(T.Stats.Get(ItemStat::SpellPower) / 100.)) * CireSkillShop::EffectScale(Hero, AbilityName);
        if (T.ExecuteBonus > 0)
        {
            float Health = 0, MaxHealth = 0;
            if (const auto* Victim = Cast<ACireHero>(Target)) { Health = Victim->Health; MaxHealth = Victim->MaxHealth; }
            else if (const auto* Monster = Cast<ACireMonster>(Target)) { Health = Monster->Health; MaxHealth = Monster->MaxHealth; }
            if (MaxHealth > 0 && Health / MaxHealth * 100.f < T.ExecuteThreshold) Amount *= 1.f + static_cast<float>(T.ExecuteBonus / 100.);
        }
    }
    const int32 Team = CireCombat::TeamOf(Source);
    if (Team >= 0) Amount *= 1.f + ACireLanternWard::MarkBonus(Target, Team);
    return Amount;
}

void CireItems::OnDamageDealt(AActor* Source, AActor* Target, float Applied, const FString& AbilityName)
{
    if (Applied > 0) CireLoot::NoteContribution(Source, Target); // personal-loot eligibility
    auto* Hero = Cast<ACireHero>(Source);
    UCireInventory* Inventory = InventoryOf(Hero);
    if (!Inventory || Applied <= 0 || Hero->bDead) return;
    const Totals& T = Inventory->Totals();
    const double Percent = IsBasicAttack(Source, AbilityName) ? T.Stats.Get(ItemStat::Lifesteal) : T.AbilityLifesteal;
    if (Percent > 0) HealDirect(Hero, Applied * static_cast<float>(Percent / 100.));
}

float CireItems::ModifyIncomingDamage(ACireHero* Hero, AActor* Causer, const FString& AbilityName, float Amount)
{
    UCireInventory* Inventory = InventoryOf(Hero);
    if (!Inventory || !FMath::IsFinite(Amount) || Amount <= 0) return Amount;
    const Totals& T = Inventory->Totals();
    const bool bPhysical = IsBasicAttack(Causer, AbilityName);
    // champion-draft: armor break; scaling-kits: party armour / MR aura, shield-tank -10% and Vulnerability.
    Amount *= 1.f - static_cast<float>(Mitigation((T.Stats.Get(bPhysical ? ItemStat::Armor : ItemStat::Ward) + CireKits::FlatDefense(Hero, bPhysical)) * (bPhysical ? CireCrowdControl::ArmorMultiplier(Hero) : 1.f) * CireKits::DefenseMultiplier(Hero)));
    const double Now = Inventory->Now();
    for (const auto& Buff : Inventory->Buffs)
        if (Buff.EndsAt > Now)
            if (const ItemDef* Item = Find(Buff.Id); Item && Item->Use.Kind == EffectKind::SelfBarrier)
                Amount *= 1.f - FMath::Clamp(static_cast<float>(Item->Use.Amount / 100.), 0.f, .9f);
    return Amount;
}

void CireItems::OnHeroDamaged(ACireHero* Hero, AActor* Causer, const FString& AbilityName, float Taken)
{
    UCireInventory* Inventory = InventoryOf(Hero);
    if (!Inventory || Taken <= 0) return;
    Inventory->InterruptTeleport(TEXT("Teleport interrupted by damage."));
    const Totals& T = Inventory->Totals();
    if (T.Thorns > 0 && IsBasicAttack(Causer, AbilityName) && Hero->IsHostile(Causer))
        CireCombat::ApplyDamage(Hero, Causer, Taken * static_cast<float>(T.Thorns / 100.), TEXT("Iron Retribution"));
    if (T.LowHealthThreshold > 0 && !Hero->bDead && Hero->MaxHealth > 0)
    {
        const float Now = static_cast<float>(Inventory->Now());
        const float Fraction = Hero->Health / Hero->MaxHealth * 100.f;
        const float Before = (Hero->Health + Taken) / Hero->MaxHealth * 100.f;
        if (Fraction < T.LowHealthThreshold && Before >= T.LowHealthThreshold && Now >= Inventory->LowHealthReadyAt)
        {
            Hero->ShieldUntil = FMath::Max(Hero->ShieldUntil, Now + static_cast<float>(T.LowHealthDuration));
            Inventory->LowHealthReadyAt = Now + static_cast<float>(T.LowHealthCooldown);
            Hero->Notice = TEXT("Unbroken: stone skin hardens.");
        }
    }
}

float CireItems::HealingMultiplier(const ACireHero* Source, const FString& AbilityName)
{
    const UCireInventory* Inventory = InventoryOf(Source);
    if (!Inventory) return 1.f;
    const Totals& T = Inventory->Totals();
    const float SkillLevel = AbilityName.IsEmpty() ? 1.f : CireSkillShop::EffectScale(Source, AbilityName); // Skill Shop level
    return SkillLevel * (1.f + static_cast<float>(T.Stats.Get(ItemStat::SpellPower) / 100.)) * (1.f + static_cast<float>(T.HealAmp / 100.));
}

// ------------------------------------------------------------------ shop access / teleport / bots
ShopAccess CireItems::ShopAccessFor(const ACireHero* Hero)
{
    const auto* Mode = ModeOf(Hero);
    if (!Hero || !Mode || !Hero->bDrafted) return ShopAccess::WrongPhase;
    const double Distance = FVector::Dist2D(Hero->GetActorLocation(), Mode->BasePosition(Hero->TeamId));
    return CheckShopAccess(Get().Shop, static_cast<int>(Mode->Clock.Phase()), Distance, Hero->bDead);
}

void CireItems::RequestTeleport(ACireHero* Hero)
{
    if (UCireInventory* Inventory = InventoryOf(Hero)) Inventory->Teleport();
}

void CireItems::BotShop(ACireHero* Hero)
{
    UCireInventory* Inventory = InventoryOf(Hero);
    if (!Inventory || ShopAccessFor(Hero) != ShopAccess::Allowed) return;
    const auto& D = Get();
    const auto* Lists = D.Recommended.Find(RoleKey(Hero));
    if (!Lists || Lists->Num() < 2) return;
    FString Message;
    // Keep two health potions, then work down the core build one component at a time.
    if (Inventory->ToRules().CountOf("vial_of_crimson") == 0 && Hero->Gold >= 200) Inventory->Buy(TEXT("vial_of_crimson"), Message);
    for (const FName Target : (*Lists)[1])
    {
        const auto Rules = Inventory->ToRules();
        if (Rules.CountOf(Utf8(Target.ToString())) > 0) continue;
        if (Inventory->Buy(Target, Message)) return;
        // Not affordable: buy the most expensive affordable missing part of its tree.
        const ItemDef* Item = Find(Target);
        const ItemDef* Best = nullptr;
        TArray<const ItemDef*> Open;
        if (Item) Open.Add(Item);
        while (Open.Num() > 0)
        {
            const ItemDef* Node = Open.Pop();
            for (const auto& Part : Node->Components)
            {
                const ItemDef* Component = D.Catalog.Find(Part);
                if (!Component || Rules.CountOf(Part) > 0) continue;
                const auto Plan = PlanPurchase(D.Catalog, Rules, Part, Hero->Gold);
                if (Plan.Ok && (!Best || Component->TotalCost > Best->TotalCost)) Best = Component;
                Open.Add(Component);
            }
        }
        if (Best) Inventory->Buy(ToName(Best->Id), Message);
        return;
    }
    if (Hero->Gold >= 300) Inventory->Buy(TEXT("tome_of_ascendance"), Message);
}

FName CireItems::BeltAction(int32 Index) { return FName(*FString::Printf(TEXT("UseBelt%d"), Index + 1)); }
FName CireItems::ItemAction(int32 Index) { return FName(*FString::Printf(TEXT("UseItem%d"), Index + 1)); }
FString CireItems::ItemSlotId(FName ItemId) { return TEXT("item:") + ItemId.ToString(); }
bool CireItems::ParseItemSlotId(const FString& SlotId, FName& OutItem)
{
    if (!SlotId.StartsWith(TEXT("item:"))) return false;
    OutItem = FName(*SlotId.Mid(5));
    return !OutItem.IsNone();
}

FString CireItems::AutoActionBarItem(const ACireHero& Hero, int32 Ordinal)
{
    const UCireInventory* Inventory = Hero.Inventory.Get();
    if (!Inventory || Ordinal < 0) return FString();
    int32 Seen = 0;
    for (const auto& Cell : Inventory->Equipment)
        if (const ItemDef* Item = Find(Cell.Id); Item && Item->HasActive() && Seen++ == Ordinal) return ItemSlotId(Cell.Id);
    return FString();
}

int32 CireItems::ResolveItemSlot(const FCireKeybindings& Bindings, const ACireHero& Hero, FName Slot)
{
    FName ItemId;
    if (!Hero.Inventory || !ParseItemSlotId(CireKeybindings::SlotAbilityId(Bindings, Hero, Slot), ItemId)) return INDEX_NONE;
    for (int32 Index = 0; Index < Hero.Inventory->Equipment.Num(); ++Index)
        if (Hero.Inventory->Equipment[Index].Id == ItemId) return Index;
    return INDEX_NONE;
}

void CireItems::OnPhaseChanged(ACireGameMode* Mode, int32 NewPhase)
{
    if (!Mode) return;
    for (ACireHero* Hero : Mode->Heroes)
        if (UCireInventory* Inventory = InventoryOf(Hero))
        {
            Inventory->InterruptTeleport(nullptr);
            Inventory->EndShopVisit();
        }
    if (NewPhase == 2 || NewPhase == 3)
        for (TActorIterator<ACireLanternWard> It(Mode->GetWorld()); It; ++It) It->Destroy();
}

// ------------------------------------------------------------------ inventory component
UCireInventory::UCireInventory()
{
    SetIsReplicatedByDefault(true);
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.TickInterval = 0.f;
    Equipment.SetNum(EquipmentSlots);
    Belt.SetNum(BeltSlots);
}

void UCireInventory::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(UCireInventory, Equipment);
    DOREPLIFETIME(UCireInventory, Belt);
    DOREPLIFETIME(UCireInventory, Buffs);
    DOREPLIFETIME(UCireInventory, TeleportChannelStart);
    DOREPLIFETIME(UCireInventory, TeleportChannelEnd);
    DOREPLIFETIME(UCireInventory, TeleportReadyAt);
    DOREPLIFETIME(UCireInventory, PrimaryTomePoints);
    DOREPLIFETIME(UCireInventory, LootScore);
    DOREPLIFETIME_CONDITION(UCireInventory, UndoDepth, COND_OwnerOnly);
    DOREPLIFETIME_CONDITION(UCireInventory, bShopVisit, COND_OwnerOnly);
    DOREPLIFETIME(UCireInventory, SkillRanks);
}

ACireHero* UCireInventory::Hero() const { return Cast<ACireHero>(GetOwner()); }
double UCireInventory::Now() const { return ServerNow(GetWorld()); }
void UCireInventory::OnRep_Items() { bTotalsDirty = true; }

Inventory UCireInventory::ToRules() const
{
    Inventory Rules;
    for (int32 Index = 0; Index < EquipmentSlots && Index < Equipment.Num(); ++Index)
    {
        const auto& Cell = Equipment[Index];
        Rules.Equipment[Index] = {Cell.Id.IsNone() ? std::string() : Utf8(Cell.Id.ToString()), Cell.Charges, Cell.ReadyAt};
    }
    for (int32 Index = 0; Index < BeltSlots && Index < Belt.Num(); ++Index)
    {
        const auto& Cell = Belt[Index];
        Rules.Belt[Index] = {Cell.Id.IsNone() ? std::string() : Utf8(Cell.Id.ToString()), Cell.Charges, Cell.ReadyAt};
    }
    return Rules;
}

void UCireInventory::FromRules(const Inventory& Rules)
{
    auto Copy = [this](TArray<FCireItemSlot>& Target, const auto& Source)
    {
        Target.SetNum(static_cast<int32>(Source.size()));
        for (int32 Index = 0; Index < Target.Num(); ++Index)
        {
            const Slot& From = Source[Index];
            FCireItemSlot& To = Target[Index];
            const FName Id = From.Empty() ? NAME_None : ToName(From.Id);
            if (To.Id != Id) To.Cooldown = 0;
            if (From.ReadyAt > To.ReadyAt + .01f)
                if (const ItemDef* Item = CireItems::Find(Id)) To.Cooldown = static_cast<float>(Item->Use.Cooldown);
            To.Id = Id;
            To.Charges = From.Charges;
            To.ReadyAt = static_cast<float>(From.ReadyAt);
        }
    };
    Copy(Equipment, Rules.Equipment);
    Copy(Belt, Rules.Belt);
    bTotalsDirty = true;
}

const Totals& UCireInventory::Totals() const
{
    if (bTotalsDirty)
    {
        std::vector<StatBlock> Active;
        const double NowTime = Now();
        for (const auto& Buff : Buffs)
            if (Buff.EndsAt > NowTime)
                if (const ItemDef* Item = CireItems::Find(Buff.Id); Item && Item->Use.Kind == EffectKind::Elixir) Active.push_back(Item->Use.Buff);
        CachedTotals = ComputeTotals(CireItems::Get().Catalog, ToRules(), Active);
        bTotalsDirty = false;
    }
    return CachedTotals;
}

void UCireInventory::SendFeedback(ECireShopAction Action, bool bOk, FName ItemId, int32 Slot, bool bBeltSlot, int32 GoldDelta, const FString& Message)
{
    FCireShopFeedback Feedback;
    Feedback.Action = Action; Feedback.bOk = bOk; Feedback.ItemId = ItemId; Feedback.Slot = Slot;
    Feedback.bBelt = bBeltSlot; Feedback.GoldDelta = GoldDelta; Feedback.Message = Message;
    UE_LOG(LogCireItems, Verbose, TEXT("CIRE_SHOP %s %s ok=%d slot=%d gold=%d %s"), ActionVerb(Action), *ItemId.ToString(), bOk ? 1 : 0, Slot, GoldDelta, *Message);
    ACireHero* Owner = Hero();
    if (Owner && !Owner->bBot && Owner->IsPlayerControlled()) ClientFeedback(Feedback);
}

void UCireInventory::ServerBuySkill_Implementation(const FString& SkillId) { FString Message; CireSkillShop::Buy(Hero(), SkillId, Message); }
void UCireInventory::ServerLevelSkill_Implementation(const FString& SkillId) { FString Message; CireSkillShop::LevelUp(Hero(), SkillId, Message); }
void UCireInventory::ServerSetProgressionMode_Implementation(uint8 NewMode)
{
    ACireHero* Owner = Hero();
    auto* Mode = ModeOf(Owner);
    // Only the host (listen server's local player, or standalone) may pick the match mode.
    const APlayerController* PC = Owner ? Cast<APlayerController>(Owner->GetController()) : nullptr;
    if (!Mode || !PC || !PC->IsLocalController()) return;
    FString Why;
    if (!CireSkillShop::SetMode(Mode, NewMode != 0, &Why) && Owner) Owner->Notice = Why;
}
void UCireInventory::ClientGoldGain_Implementation(int32 Amount, FVector_NetQuantize Where, uint8 Kind)
{
    FGoldGain Gain; Gain.Amount = Amount; Gain.Where = Where; Gain.Kind = Kind;
    PendingGold.Add(Gain);
    if (PendingGold.Num() > 24) PendingGold.RemoveAt(0);
}

void UCireInventory::ClientLootReport_Implementation(const FCireLootReport& Report)
{
    PendingLoot.Add(Report);
    if (PendingLoot.Num() > 8) PendingLoot.RemoveAt(0);
}

void UCireInventory::ClientFeedback_Implementation(const FCireShopFeedback& Feedback)
{
    PendingFeedback.Add(Feedback);
    if (PendingFeedback.Num() > 16) PendingFeedback.RemoveAt(0);
}

void UCireInventory::AfterChange()
{
    bTotalsDirty = true;
    ACireHero* Owner = Hero();
    if (!Owner) return;
    int32 Legendary = 0;
    for (const auto& Cell : Equipment)
        if (const ItemDef* Item = CireItems::Find(Cell.Id); Item && Item->Tier == ItemTier::Legendary) ++Legendary;
    Owner->GearRank = Legendary;
    UndoDepth = static_cast<int32>(Session.History.size());
    if (Owner->HasAuthority() && Owner->bDrafted) Owner->Recalculate(false);
}

void UCireInventory::BeginShopVisit(bool bOpen)
{
    if (!bShopVisit || !Session.Open) Session.Clear();
    Session.Open = true;
    bShopVisit = true;
    UndoDepth = static_cast<int32>(Session.History.size());
    (void)bOpen;
}

void UCireInventory::EndShopVisit()
{
    Session.Clear();
    Session.Open = false;
    bShopVisit = false;
    UndoDepth = 0;
}

bool UCireInventory::Buy(FName ItemId, FString& Message)
{
    Message.Reset();
    ACireHero* Owner = Hero();
    if (!Owner || !Owner->HasAuthority() || !Owner->bDrafted) return false;
    const auto& D = CireItems::Get();
    const ItemDef* Item = CireItems::Find(ItemId);
    const ShopAccess Access = CireItems::ShopAccessFor(Owner);
    if (Access != ShopAccess::Allowed) Message = UTF8_TO_TCHAR(ShopAccessMessage(Access).c_str());
    else if (!Item) Message = TEXT("Unknown item.");
    if (!Message.IsEmpty())
    {
        Owner->Notice = Message;
        SendFeedback(ECireShopAction::Buy, false, ItemId, -1, false, 0, Message);
        return false;
    }
    Inventory Rules = ToRules();
    int32 Gold = Owner->Gold;
    const PurchasePlan Plan = PlanPurchase(D.Catalog, Rules, Utf8(ItemId.ToString()), Gold);
    if (!Plan.Ok)
    {
        Message = UTF8_TO_TCHAR(Plan.Error.c_str());
        Owner->Notice = Message;
        SendFeedback(ECireShopAction::Buy, false, ItemId, -1, false, 0, Message);
        return false;
    }
    if (!Session.Open) BeginShopVisit(false);
    if (Plan.Instant) Session.Clear(); // tomes are read on purchase and cannot be undone
    else Session.Record(Rules, Gold, "Buy " + Item->Name);
    int Remaining = Gold;
    if (!ApplyPurchase(D.Catalog, Rules, Remaining, Utf8(ItemId.ToString()), Plan)) { Message = TEXT("Purchase rejected."); return false; }
    Owner->Gold = Remaining;
    if (Plan.Instant)
    {
        FString EffectMessage;
        ApplyEffect(Item->Use, ItemId, EffectMessage);
    }
    else FromRules(Rules);
    AfterChange();
    Message = FString::Printf(TEXT("Purchased %s  -%dg"), UTF8_TO_TCHAR(Item->Name.c_str()), Plan.Cost);
    Owner->Notice = Message;
    SendFeedback(ECireShopAction::Buy, true, ItemId, Plan.TargetSlot, Plan.ToBelt, -Plan.Cost, Message);
    return true;
}

bool UCireInventory::SellSlot(int32 Index, bool bBeltSlot, FString& Message)
{
    Message.Reset();
    ACireHero* Owner = Hero();
    if (!Owner || !Owner->HasAuthority()) return false;
    const auto& D = CireItems::Get();
    const ShopAccess Access = CireItems::ShopAccessFor(Owner);
    const TArray<FCireItemSlot>& Slots = bBeltSlot ? Belt : Equipment;
    const FName ItemId = Slots.IsValidIndex(Index) ? Slots[Index].Id : NAME_None;
    if (Access != ShopAccess::Allowed) Message = UTF8_TO_TCHAR(ShopAccessMessage(Access).c_str());
    else if (ItemId.IsNone()) Message = TEXT("That slot is empty.");
    if (!Message.IsEmpty())
    {
        Owner->Notice = Message;
        SendFeedback(ECireShopAction::Sell, false, ItemId, Index, bBeltSlot, 0, Message);
        return false;
    }
    Inventory Rules = ToRules();
    int Gold = Owner->Gold;
    if (!Session.Open) BeginShopVisit(false);
    Session.Record(Rules, Gold, "Sell " + Utf8(CireItems::DisplayName(ItemId)));
    const int Value = Sell(D.Catalog, Rules, Gold, Index, bBeltSlot, D.Shop);
    if (Value < 0) { Session.History.pop_back(); Message = TEXT("Nothing to sell."); return false; }
    Owner->Gold = Gold;
    FromRules(Rules);
    AfterChange();
    Message = FString::Printf(TEXT("Sold %s  +%dg"), *CireItems::DisplayName(ItemId), Value);
    Owner->Notice = Message;
    SendFeedback(ECireShopAction::Sell, true, ItemId, Index, bBeltSlot, Value, Message);
    return true;
}

bool UCireInventory::UndoLast(FString& Message)
{
    Message.Reset();
    ACireHero* Owner = Hero();
    if (!Owner || !Owner->HasAuthority()) return false;
    Inventory Rules = ToRules();
    int Gold = Owner->Gold;
    std::string Label;
    if (CireItems::ShopAccessFor(Owner) != ShopAccess::Allowed || !Session.Open || !Session.Undo(Rules, Gold, Label))
    {
        Message = TEXT("Nothing to undo in this shop visit.");
        SendFeedback(ECireShopAction::Undo, false, NAME_None, -1, false, 0, Message);
        return false;
    }
    const int32 Delta = Gold - Owner->Gold;
    Owner->Gold = Gold;
    FromRules(Rules);
    AfterChange();
    Message = FString::Printf(TEXT("Undone: %s  %+dg"), UTF8_TO_TCHAR(Label.c_str()), Delta);
    Owner->Notice = Message;
    SendFeedback(ECireShopAction::Undo, true, NAME_None, -1, false, Delta, Message);
    return true;
}

bool UCireInventory::SwapSlots(int32 From, int32 To)
{
    if (!Equipment.IsValidIndex(From) || !Equipment.IsValidIndex(To) || From == To) return false;
    Equipment.Swap(From, To);
    bTotalsDirty = true;
    return true;
}

bool UCireInventory::HasRoomFor(FName ItemId) const
{
    const ItemDef* Item = CireItems::Find(ItemId);
    if (!Item) return false;
    if (Item->Instant) return true;
    if (Item->Belt)
    {
        for (const auto& Cell : Belt) if (Cell.Id.IsNone() || (Cell.Id == ItemId && Cell.Charges < Item->MaxStack)) return true;
        return false;
    }
    if (Item->Unique && ToRules().CountOf(Item->Id) > 0) return false;
    for (const auto& Cell : Equipment) if (Cell.Id.IsNone()) return true;
    return false;
}

bool UCireInventory::GrantItem(FName ItemId, int32& ConvertedGold, int32* OutSlot, bool* OutBelt)
{
    ConvertedGold = 0;
    if (OutSlot) *OutSlot = -1;
    ACireHero* Owner = Hero();
    const ItemDef* Item = CireItems::Find(ItemId);
    if (!Owner || !Item) return false;
    if (!HasRoomFor(ItemId))
    {
        ConvertedGold = Item->TotalCost;
        Owner->Gold += ConvertedGold;
        return false;
    }
    if (Item->Instant) { FString Ignored; ApplyEffect(Item->Use, ItemId, Ignored); AfterChange(); return true; }
    TArray<FCireItemSlot>& Slots = Item->Belt ? Belt : Equipment;
    int32 Target = INDEX_NONE;
    if (Item->Belt)
        for (int32 Index = 0; Index < Slots.Num() && Target == INDEX_NONE; ++Index)
            if (Slots[Index].Id == ItemId && Slots[Index].Charges < Item->MaxStack) Target = Index;
    for (int32 Index = 0; Index < Slots.Num() && Target == INDEX_NONE; ++Index) if (Slots[Index].Id.IsNone()) Target = Index;
    if (Target == INDEX_NONE) return false;
    FCireItemSlot& Cell = Slots[Target];
    if (Cell.Id == ItemId) ++Cell.Charges;
    else { Cell = FCireItemSlot(); Cell.Id = ItemId; Cell.Charges = 1; }
    // Loot is not part of any shop transaction; undoing an earlier buy must not delete it.
    Session.Clear();
    AfterChange();
    if (OutSlot) *OutSlot = Target;
    if (OutBelt) *OutBelt = Item->Belt;
    return true;
}

void UCireInventory::ApplyPrimaryTome(int32 Points)
{
    ACireHero* Owner = Hero();
    if (!Owner || Points <= 0) return;
    switch (Owner->PrimaryStat())
    {
    case Cires::PrimaryStat::Strength: Owner->Progression.Stats.Strength += Points; break;
    case Cires::PrimaryStat::Agility: Owner->Progression.Stats.Agility += Points; break;
    default: Owner->Progression.Stats.Intelligence += Points; break;
    }
    PrimaryTomePoints += Points;
    Owner->Recalculate(false);
}

bool UCireInventory::ApplyEffect(const Effect& Use, FName ItemId, FString& Message)
{
    ACireHero* Owner = Hero();
    auto* Mode = ModeOf(Owner);
    if (!Owner || !Mode) return false;
    const float NowTime = static_cast<float>(Now());
    const float Power = Mode->Power(Owner->TeamId);
    const FString EffectName = Use.Name.empty() ? CireItems::DisplayName(ItemId) : FString(UTF8_TO_TCHAR(Use.Name.c_str()));
    auto AddBuff = [&](float Duration)
    {
        Buffs.RemoveAll([&](const FCireTimedBuff& Buff) { return Buff.Id == ItemId || Buff.EndsAt <= NowTime; });
        FCireTimedBuff Buff; Buff.Id = ItemId; Buff.EndsAt = NowTime + Duration; Buff.Duration = Duration;
        Buffs.Add(Buff);
        bTotalsDirty = true;
    };
    switch (Use.Kind)
    {
    case EffectKind::HealOverTime:
    case EffectKind::ManaOverTime:
    {
        FCireRestore Restore;
        Restore.bMana = Use.Kind == EffectKind::ManaOverTime;
        Restore.Remaining = FMath::Max(.1f, static_cast<float>(Use.Duration));
        Restore.PerSecond = static_cast<float>(Use.Amount) / Restore.Remaining;
        Restore.EnergyPerSecond = Restore.bMana ? Restore.PerSecond / 3.f : 0.f;
        Restores.Add(Restore);
        AddBuff(Restore.Remaining);
        Message = EffectName;
        return true;
    }
    case EffectKind::Elixir:
        AddBuff(static_cast<float>(Use.Duration));
        Owner->Recalculate(false);
        Message = EffectName + TEXT(" empowers you.");
        return true;
    case EffectKind::Ward:
    {
        FActorSpawnParameters Params;
        Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        Params.Owner = Owner;
        const FVector Feet = Owner->GetActorLocation() - FVector(0, 0, 90);
        if (auto* Ward = Owner->GetWorld()->SpawnActor<ACireLanternWard>(Feet, FRotator::ZeroRotator, Params))
        {
            Ward->TeamId = Owner->TeamId;
            Ward->Radius = static_cast<float>(Use.Radius);
            Ward->ExpiresAt = NowTime + static_cast<float>(Use.Duration);
            Ward->SlowPercent = static_cast<float>(Use.Amount);
            Ward->MarkPercent = static_cast<float>(Use.Scaling);
        }
        Message = TEXT("Lantern ward planted.");
        return true;
    }
    case EffectKind::Experience:
        Owner->GrantExperience(FMath::RoundToInt(Use.Amount));
        Message = FString::Printf(TEXT("+%d experience"), FMath::RoundToInt(Use.Amount));
        return true;
    case EffectKind::PrimaryStat:
        ApplyPrimaryTome(FMath::RoundToInt(Use.Amount));
        Message = FString::Printf(TEXT("+%d primary attribute"), FMath::RoundToInt(Use.Amount));
        return true;
    case EffectKind::DamageArea:
    {
        AActor* Target = Owner->Target;
        if (!Owner->IsHostile(Target) || !Owner->InRange(Target, 1200.f)) { Message = TEXT("Select a hostile target within 12 m."); return false; }
        const FVector Center = Target->GetActorLocation();
        const float Amount = static_cast<float>(Use.Amount + Use.Scaling * Owner->Intelligence) * Power;
        TArray<AActor*> Victims{Target};
        for (ACireMonster* Monster : Mode->Monsters)
            if (Monster != Target && Owner->IsHostile(Monster) && FVector::DistSquared2D(Center, Monster->GetActorLocation()) <= FMath::Square(Use.Radius)) Victims.Add(Monster);
        for (ACireHero* Enemy : Mode->Heroes)
            if (Enemy != Target && Owner->IsHostile(Enemy) && FVector::DistSquared2D(Center, Enemy->GetActorLocation()) <= FMath::Square(Use.Radius)) Victims.Add(Enemy);
        CireCombat::PlayCue(Owner, Target, TEXT("cataclysm"), Owner->GetActorLocation(), Center, ECireSpellCue::Impact, FMath::Clamp(static_cast<float>(Use.Radius) / 550.f, .4f, 2.f));
        for (AActor* Victim : Victims) CireCombat::ApplyDamage(Owner, Victim, Amount, EffectName);
        Message = EffectName;
        return true;
    }
    case EffectKind::SelfBarrier:
        AddBuff(static_cast<float>(Use.Duration));
        {
            const float Before = Owner->Health;
            HealDirect(Owner, Owner->MaxHealth * static_cast<float>(Use.Scaling / 100.));
            if (Owner->Health > Before) CireCombat::BroadcastHealing(Owner, Owner, Owner->Health - Before, EffectName);
        }
        Message = EffectName;
        return true;
    case EffectKind::ShieldAllies:
        for (ACireHero* Ally : Mode->Heroes)
            if (IsValid(Ally) && !Ally->bDead && Ally->bDrafted && Ally->TeamId == Owner->TeamId && Owner->InRange(Ally, static_cast<float>(Use.Radius)))
                { Ally->ShieldUntil = FMath::Max(Ally->ShieldUntil, NowTime + static_cast<float>(Use.Duration)); CireBuffs::Apply(Ally, TEXT("oathshield"), static_cast<float>(Use.Duration), Owner); } // aura-vfx
        CireCombat::PlayCue(Owner, Owner, TEXT("bastion_of_dawn"), Owner->GetActorLocation(), Owner->GetActorLocation(), ECireSpellCue::Impact);
        Message = EffectName;
        return true;
    case EffectKind::TauntArea:
    {
        int32 Taunted = 0;
        for (ACireMonster* Monster : Mode->Monsters)
            if (Owner->IsHostile(Monster) && Owner->InRange(Monster, static_cast<float>(Use.Radius)))
            { CireThreat::Taunt(Monster, Owner, static_cast<float>(Use.Duration)); ++Taunted; }
        Owner->TauntUntil = FMath::Max(Owner->TauntUntil, NowTime + static_cast<float>(Use.Duration));
        CireBuffs::Apply(Owner, TEXT("toll_of_the_grave"), static_cast<float>(Use.Duration), Owner); // aura-vfx
        CireCombat::PlayCue(Owner, Owner, TEXT("war_cry"), Owner->GetActorLocation(), Owner->GetActorLocation(), ECireSpellCue::Impact);
        Message = FString::Printf(TEXT("%s: %d taunted"), *EffectName, Taunted);
        return true;
    }
    case EffectKind::HealAllies:
    {
        const float Amount = static_cast<float>(Use.Amount + Use.Scaling * Owner->Intelligence) * Power;
        for (ACireHero* Ally : Mode->Heroes)
        {
            if (!IsValid(Ally) || Ally->bDead || !Ally->bDrafted || Ally->TeamId != Owner->TeamId || !Owner->InRange(Ally, static_cast<float>(Use.Radius))) continue;
            if (Mode->IsCombatPhase()) CireCombat::ApplyHealing(Owner, Ally, Amount, EffectName);
            else HealDirect(Ally, Amount * CireItems::HealingMultiplier(Owner));
        }
        Message = EffectName;
        return true;
    }
    case EffectKind::Haste:
        Owner->SlowUntil = 0;
        AddBuff(static_cast<float>(Use.Duration));
        Message = EffectName;
        return true;
    default:
        Message = TEXT("This item has no use effect.");
        return false;
    }
}

bool UCireInventory::UseSlot(int32 Index, bool bBeltSlot, FString& Message)
{
    Message.Reset();
    ACireHero* Owner = Hero();
    if (!Owner || !Owner->HasAuthority()) return false;
    const TArray<FCireItemSlot>& Slots = bBeltSlot ? Belt : Equipment;
    const FName ItemId = Slots.IsValidIndex(Index) ? Slots[Index].Id : NAME_None;
    const ItemDef* Item = CireItems::Find(ItemId);
    auto Fail = [&](const FString& Reason)
    {
        Message = Reason;
        Owner->Notice = Reason;
        SendFeedback(ECireShopAction::Use, false, ItemId, Index, bBeltSlot, 0, Reason);
        return false;
    };
    if (!Owner->bDrafted || Owner->bDead) return Fail(TEXT("You cannot use items right now."));
    if (!Item) return Fail(TEXT("That slot is empty."));
    if (!Item->Use.IsSet() || Item->Instant) return Fail(CireItems::DisplayName(ItemId) + TEXT(" has no active use."));
    if (Slots[Index].ReadyAt > Now()) return Fail(FString::Printf(TEXT("%s is recharging (%.0fs)."), *CireItems::DisplayName(ItemId), Slots[Index].ReadyAt - Now()));
    // Preconditions first so a missing target never spends a charge or cooldown.
    if (Item->Use.Kind == EffectKind::DamageArea && (!Owner->IsHostile(Owner->Target) || !Owner->InRange(Owner->Target, 1200.f)))
        return Fail(TEXT("Select a hostile target within 12 m."));
    Inventory Rules = ToRules();
    Effect Use;
    if (Cires::Items::UseSlot(CireItems::Get().Catalog, Rules, Index, bBeltSlot, Now(), Use) != UseResult::Used)
        return Fail(TEXT("Nothing happens."));
    FromRules(Rules);
    FString EffectMessage;
    ApplyEffect(Use, ItemId, EffectMessage);
    EndShopVisit(); // LoL rule: using an item ends the undo history
    AfterChange();
    Message = EffectMessage;
    Owner->Notice = EffectMessage;
    SendFeedback(ECireShopAction::Use, true, ItemId, Index, bBeltSlot, 0, EffectMessage);
    return true;
}

float UCireInventory::TeleportCooldownRemaining() const
{
    return FMath::Max(0.f, TeleportReadyAt - static_cast<float>(Now()));
}

void UCireInventory::Teleport()
{
    ACireHero* Owner = Hero();
    auto* Mode = ModeOf(Owner);
    if (!Owner || !Mode || !Owner->HasAuthority()) return;
    const auto Phase = Mode->Clock.Phase();
    const bool bAllowed = Owner->bDrafted && !Owner->bDead && (Phase == Cires::MatchPhase::Survival ||
        Phase == Cires::MatchPhase::Intermission || Phase == Cires::MatchPhase::Recovery);
    const bool bInstant = Phase == Cires::MatchPhase::Intermission || Phase == Cires::MatchPhase::Recovery;
    TeleportState State{TeleportChannelStart, TeleportChannelEnd, TeleportReadyAt};
    const TeleportRules& Rules = CireItems::Get().Teleport;
    const TeleportStart Result = BeginTeleport(State, Rules, Now(), bAllowed, bInstant);
    FString Message;
    switch (Result)
    {
    case TeleportStart::Instant:
        Owner->ReviveAt(Mode->BasePosition(Owner->TeamId));
        Message = TEXT("Recalled to town.");
        break;
    case TeleportStart::Channeling:
        TeleportChannelStart = static_cast<float>(State.ChannelStart);
        TeleportChannelEnd = static_cast<float>(State.ChannelEnd);
        ChannelOrigin = Owner->GetActorLocation();
        Message = FString::Printf(TEXT("Teleporting to base... %.0fs (damage or moving cancels)"), Rules.ChannelSeconds);
        break;
    case TeleportStart::OnCooldown:
        Message = FString::Printf(TEXT("Teleport to Base recharging: %.0fs."), TeleportCooldownRemaining());
        break;
    case TeleportStart::AlreadyChanneling:
        InterruptTeleport(TEXT("Teleport cancelled."));
        return;
    default:
        Message = Owner->bDead ? TEXT("The fallen cannot teleport.") : TEXT("Teleport to Base is sealed during the arena.");
        break;
    }
    Owner->Notice = Message;
    const bool bOk = Result == TeleportStart::Instant || Result == TeleportStart::Channeling;
    SendFeedback(ECireShopAction::Teleport, bOk, NAME_None, bInstant ? 1 : 0, false, 0, Message);
}

void UCireInventory::InterruptTeleport(const TCHAR* Reason)
{
    TeleportState State{TeleportChannelStart, TeleportChannelEnd, TeleportReadyAt};
    if (!Cires::Items::InterruptTeleport(State)) return;
    TeleportChannelStart = TeleportChannelEnd = -1;
    if (Reason)
    {
        if (ACireHero* Owner = Hero()) Owner->Notice = Reason;
        SendFeedback(ECireShopAction::Teleport, false, NAME_None, -1, false, 0, Reason);
    }
}

void UCireInventory::CompleteTeleportNow()
{
    if (IsChanneling()) TeleportChannelEnd = static_cast<float>(Now());
    TickComponent(0.f, LEVELTICK_All, nullptr);
}

void UCireInventory::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
    ACireHero* Owner = Hero();
    if (!Owner || !Owner->HasAuthority() || IsSummon(Owner)) return;
    auto* Mode = ModeOf(Owner);
    if (!Mode) return;
    const double NowTime = Now();
    // Expired buffs change stats.
    const int32 Before = Buffs.Num();
    Buffs.RemoveAll([&](const FCireTimedBuff& Buff) { return Buff.EndsAt <= NowTime; });
    if (Buffs.Num() != Before) { bTotalsDirty = true; if (Owner->bDrafted) Owner->Recalculate(false); }
    // Potions.
    for (int32 Index = Restores.Num() - 1; Index >= 0; --Index)
    {
        FCireRestore& Restore = Restores[Index];
        const float Step = FMath::Min(DeltaTime, Restore.Remaining);
        Restore.Remaining -= Step;
        if (!Owner->bDead)
        {
            if (Restore.bMana)
            {
                Owner->Mana = FMath::Min(Owner->MaxMana, Owner->Mana + Restore.PerSecond * Step);
                Owner->Energy = FMath::Min(100.f, Owner->Energy + Restore.EnergyPerSecond * Step);
            }
            else HealDirect(Owner, Restore.PerSecond * Step);
        }
        if (Restore.Remaining <= 0 || Owner->bDead) Restores.RemoveAt(Index);
    }
    // Vigil aura: allies regenerate health once per second.
    AuraTimer -= DeltaTime;
    if (AuraTimer <= 0)
    {
        AuraTimer = 1.f;
        const Cires::Items::Totals& T = Totals();
        if (T.AuraRegen > 0 && !Owner->bDead)
            for (ACireHero* Ally : Mode->Heroes)
                if (IsValid(Ally) && Ally->TeamId == Owner->TeamId && Owner->InRange(Ally, static_cast<float>(T.AuraRadius)))
                    HealDirect(Ally, static_cast<float>(T.AuraRegen));
    }
    // Teleport channel: moving or leaving the survival phase cancels; completion starts the cooldown.
    if (IsChanneling())
    {
        const auto& Rules = CireItems::Get().Teleport;
        if (Owner->bDead || Mode->Clock.Phase() != Cires::MatchPhase::Survival) InterruptTeleport(TEXT("Teleport cancelled."));
        else if (FVector::Dist2D(Owner->GetActorLocation(), ChannelOrigin) > Rules.MoveTolerance) InterruptTeleport(TEXT("Teleport interrupted: you moved."));
        else
        {
            TeleportState State{TeleportChannelStart, TeleportChannelEnd, TeleportReadyAt};
            if (CompleteTeleport(State, Rules, NowTime))
            {
                TeleportChannelStart = TeleportChannelEnd = -1;
                TeleportReadyAt = static_cast<float>(State.ReadyAt);
                CireThreat::Remove(Owner);
                Owner->Target = nullptr;
                Owner->GetCharacterMovement()->StopMovementImmediately();
                const FVector Spot = Mode->BasePosition(Owner->TeamId) + FVector(120, (static_cast<int32>(Owner->GetUniqueID() % 5) - 2) * 110, 0);
                Owner->SetActorLocation(Spot, false, nullptr, ETeleportType::TeleportPhysics);
                Owner->Notice = TEXT("Arrived at town.");
                SendFeedback(ECireShopAction::Teleport, true, NAME_None, 2, false, 0, TEXT("Arrived at town."));
            }
        }
    }
    // A shop visit ends when trading is no longer allowed (left town, phase change, death).
    if (bShopVisit && CireItems::ShopAccessFor(Owner) != ShopAccess::Allowed) EndShopVisit();
    // Skill Shop: bots shop between waves (per-level cast scaling runs in the cast path).
    BotSkillTimer -= DeltaTime;
    if (Owner->bBot && BotSkillTimer <= 0) { BotSkillTimer = 2.f; CireSkillShop::BotShop(Owner); }
}

void UCireInventory::ServerBuy_Implementation(FName ItemId) { FString Message; Buy(ItemId, Message); }
void UCireInventory::ServerSell_Implementation(int32 Index, bool bBeltSlot) { FString Message; SellSlot(Index, bBeltSlot, Message); }
void UCireInventory::ServerUndo_Implementation() { FString Message; UndoLast(Message); }
void UCireInventory::ServerUse_Implementation(int32 Index, bool bBeltSlot) { FString Message; UseSlot(Index, bBeltSlot, Message); }
void UCireInventory::ServerSwap_Implementation(int32 From, int32 To) { SwapSlots(From, To); }
void UCireInventory::ServerShopOpen_Implementation(bool bOpen)
{
    ACireHero* Owner = Hero();
    if (!Owner) return;
    if (bOpen && CireItems::ShopAccessFor(Owner) == ShopAccess::Allowed) BeginShopVisit(true);
    else if (!bOpen) EndShopVisit();
}

// ------------------------------------------------------------------ lantern ward
ACireLanternWard::ACireLanternWard()
{
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.TickInterval = .25f;
    bReplicates = true;
    SetReplicateMovement(false);
    Post = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Post"));
    SetRootComponent(Post);
    Flame = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Flame"));
    Flame->SetupAttachment(Post);
    Light = CreateDefaultSubobject<UPointLightComponent>(TEXT("Light"));
    Light->SetupAttachment(Post);
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Cylinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Sphere(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
    if (Cylinder.Succeeded()) Post->SetStaticMesh(Cylinder.Object);
    if (Sphere.Succeeded()) Flame->SetStaticMesh(Sphere.Object);
    Post->SetRelativeScale3D(FVector(.12f, .12f, 1.3f));
    Post->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Flame->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Flame->SetRelativeLocation(FVector(0, 0, 70));
    Flame->SetRelativeScale3D(FVector(2.2f, 2.2f, .28f));
    Light->SetRelativeLocation(FVector(0, 0, 150));
    Light->SetLightColor(FLinearColor(.35f, .6f, 1.f));
    Light->SetIntensity(9000.f);
    Light->SetAttenuationRadius(700.f);
    Light->SetCastShadows(false);
}

void ACireLanternWard::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(ACireLanternWard, TeamId);
    DOREPLIFETIME(ACireLanternWard, Radius);
    DOREPLIFETIME(ACireLanternWard, ExpiresAt);
}

bool ACireLanternWard::IsNetRelevantFor(const AActor* RealViewer, const AActor* ViewTarget, const FVector& SrcLocation) const
{
    const ACireHero* Viewer = Cast<ACireHero>(ViewTarget);
    if (const auto* Controller = Cast<AController>(RealViewer)) Viewer = Cast<ACireHero>(Controller->GetPawn());
    return Viewer && Viewer->TeamId == TeamId;
}

void ACireLanternWard::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    const float NowTime = GetWorld()->GetTimeSeconds();
    if (Flame) Flame->SetRelativeScale3D(FVector(2.2f + .25f * FMath::Sin(NowTime * 5.f), 2.2f, .28f));
    if (!HasAuthority()) return;
    if (ExpiresAt > 0 && NowTime >= ExpiresAt) { Destroy(); return; }
    for (TActorIterator<ACireMonster> It(GetWorld()); It; ++It)
        if (It->Lane == TeamId && It->Health > 0 && !It->bArmoredEscort &&
            FVector::DistSquared2D(It->GetActorLocation(), GetActorLocation()) <= FMath::Square(Radius))
            It->SlowUntil = FMath::Max(It->SlowUntil, NowTime + .5f);
}

float ACireLanternWard::MarkBonus(const AActor* Target, int32 AttackerTeam)
{
    const auto* Monster = Cast<ACireMonster>(Target);
    if (!Monster || !Monster->GetWorld()) return 0.f;
    float Best = 0.f;
    for (TActorIterator<ACireLanternWard> It(Monster->GetWorld()); It; ++It)
        if (It->TeamId == AttackerTeam && FVector::DistSquared2D(It->GetActorLocation(), Monster->GetActorLocation()) <= FMath::Square(It->Radius))
            Best = FMath::Max(Best, It->MarkPercent / 100.f);
    return Best;
}
