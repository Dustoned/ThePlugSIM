// UInventoryComponent — voorraad als stapels. Elke stapel heeft een unieke StackId zodat
// identieke (niet-stapelbare) items elk een eigen slot innemen (bv. losse waterflessen). De
// hotbar verwijst naar StackId's, niet naar item-id's. Server-authoritative + replicated.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "InventoryComponent.generated.h"

USTRUCT(BlueprintType)
struct FInventoryStack
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory")
	FName ItemId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory")
	int32 Quantity = 0;

	// THC% van deze stapel (alleen zinvol voor wiet/joints; 0 = n.v.t.). Afgeleid van strain + verzorging.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory")
	float Quality = 0.f;

	// Kwaliteit% (hoe goed gekweekt, 0..100). Los van THC%: bepaalt mede hoe graag klanten 'm willen.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory")
	float QualityPct = 0.f;

	// Unieke id van deze stapel (server toegekend, gerepliceerd). 0 = ongeldig.
	UPROPERTY(BlueprintReadOnly, Category = "Inventory")
	int32 StackId = 0;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnInventoryChanged);

UCLASS(ClassGroup = (WeedShop), meta = (BlueprintSpawnableComponent))
class WEEDSHOPCORE_API UInventoryComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UInventoryComponent();

	// Max aantal slots (= aantal stapels; 0 = ongelimiteerd).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WeedShop|Inventory")
	int32 MaxStacks = 24;

	// Max draaggewicht (abstracte kg). 0 = ongelimiteerd.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WeedShop|Inventory")
	float MaxWeight = 60.f;

	UFUNCTION(BlueprintPure, Category = "WeedShop|Inventory")
	float GetUnitWeight(FName ItemId) const;

	UFUNCTION(BlueprintPure, Category = "WeedShop|Inventory")
	float GetTotalWeight() const;

	// Gebruikte slots = aantal stapels (niet-stapelbare items zijn aparte stapels).
	UFUNCTION(BlueprintPure, Category = "WeedShop|Inventory")
	int32 GetUsedSlots() const { return Stacks.Num(); }

	UPROPERTY(BlueprintAssignable, Category = "WeedShop|Inventory")
	FOnInventoryChanged OnInventoryChanged;

	// Server. Voegt Count toe. Stapelbare items mergen (THC% + Kwaliteit% middelen gewogen op aantal);
	// niet-stapelbare (flessen) worden losse stapels van 1. ThcPercent/QualityPct < 0 = geen info.
	UFUNCTION(BlueprintCallable, Category = "WeedShop|Inventory")
	bool AddItem(FName ItemId, int32 Count, float ThcPercent = -1.f, float QualityPct = -1.f);

	// Server. Haalt Count weg (over meerdere stapels indien nodig). False bij te weinig.
	UFUNCTION(BlueprintCallable, Category = "WeedShop|Inventory")
	bool RemoveItem(FName ItemId, int32 Count);

	// Server. Maakt de hele inventory leeg (voor save/load-herstel).
	UFUNCTION(BlueprintCallable, Category = "WeedShop|Inventory")
	void ClearAll();

	// Splits Amount van een stapel af naar een NIEUWE stapel (shift+slepen). ToCell = grid-cel waar de
	// nieuwe helft heen moet (-1 = eerste vrije). Client-helper: onthoudt de doel-cel + roept de server.
	void RequestSplit(int32 StackId, int32 Amount, int32 ToCell);
	UFUNCTION(Server, Reliable) void ServerSplitStack(int32 StackId, int32 Amount);

	// Voeg PRECIES twee stapels samen (sleep From op Into). Alleen zelfde, stapelbaar item.
	void RequestMergeTwo(int32 IntoStackId, int32 FromStackId) { ServerMergeTwo(IntoStackId, FromStackId); }
	UFUNCTION(Server, Reliable) void ServerMergeTwo(int32 IntoStackId, int32 FromStackId);

	// Of dit item stapelbaar is (bv. flessen niet). Bepaalt of merge/split mag.
	static bool IsStackable(FName ItemId);

	// Server. Zet de inventory EXACT terug zoals opgeslagen: elke stack op z'n opgeslagen grid-cel,
	// geen merge/sortering. InCells[i] = grid-cel van InStacks[i] (-1 = eerste vrije). De cash-stack
	// (afgeleid van economy) blijft op cel 0. Voor save/load zodat slots niet meer wisselen.
	void RestoreStacksAndGrid(const TArray<FInventoryStack>& InStacks, const TArray<int32>& InCells);

	// Totaal aantal van dit item over alle stapels.
	UFUNCTION(BlueprintPure, Category = "WeedShop|Inventory")
	int32 GetQuantity(FName ItemId) const;

	// Server: spiegel het cash-saldo als fysiek "Cash"-briefgeld in de inventory (waarde in hele euro's).
	// Eén stapel; 0 = geen briefgeld. Gewichtloos en niet bruikbaar/op de hotbar.
	void SetCashDisplayEuros(int64 Euros);

	UFUNCTION(BlueprintPure, Category = "WeedShop|Inventory")
	bool HasItem(FName ItemId, int32 Count = 1) const { return GetQuantity(ItemId) >= Count; }

	// THC% van de eerste stapel met dit item-id (0 als geen).
	UFUNCTION(BlueprintPure, Category = "WeedShop|Inventory")
	float GetItemQuality(FName ItemId) const;

	// Kwaliteit% van de eerste stapel met dit item-id (0 als geen).
	UFUNCTION(BlueprintPure, Category = "WeedShop|Inventory")
	float GetItemQualityPct(FName ItemId) const;

	UFUNCTION(BlueprintPure, Category = "WeedShop|Inventory")
	const TArray<FInventoryStack>& GetStacks() const { return Stacks; }

	// Index van de stapel met deze StackId (INDEX_NONE als weg).
	int32 FindStackById(int32 StackId) const;

	// --- Wiet-batches mergen (verschillende oogsten met afwijkende THC%/Kwaliteit% blijven aparte
	// stapels; de speler kan ze bewust samenvoegen tot het gewogen gemiddelde) ---

	// Aantal aparte stapels van dit item-id (>1 = mergebaar).
	UFUNCTION(BlueprintPure, Category = "WeedShop|Inventory")
	int32 CountStacksOf(FName ItemId) const;

	// Voorbeeld van het merge-resultaat (gewogen gemiddelde) zonder iets te wijzigen.
	void GetMergePreview(FName ItemId, int32& OutQty, float& OutThcPercent, float& OutQualityPct, int32& OutBatches) const;

	// Server. Voegt alle stapels van dit item-id samen tot één (gewogen gemiddelde THC%/Kwaliteit%).
	UFUNCTION(BlueprintCallable, Category = "WeedShop|Inventory")
	bool MergeItem(FName ItemId);

	// --- Hotbar (verwijst naar StackId's; lokale UI-staat) ---
	static constexpr int32 HotbarSize = 8;

	UFUNCTION(BlueprintCallable, Category = "WeedShop|Inventory")
	void SetActiveSlot(int32 Slot);

	UFUNCTION(BlueprintCallable, Category = "WeedShop|Inventory")
	void CycleActiveSlot(int32 Dir);

	UFUNCTION(BlueprintPure, Category = "WeedShop|Inventory")
	int32 GetActiveSlot() const { return ActiveSlot; }

	// Item-id van het geselecteerde hotbar-slot (NAME_None als leeg/weg).
	UFUNCTION(BlueprintPure, Category = "WeedShop|Inventory")
	FName GetActiveItemId() const;

	// StackId in hotbar-slot Slot (0 = leeg).
	UFUNCTION(BlueprintPure, Category = "WeedShop|Inventory")
	int32 GetHotbarStackId(int32 Slot) const;

	// Of een stapel op de hotbar staat.
	bool IsStackOnHotbar(int32 StackId) const;

	// Zet een stapel in een hotbar-slot (drag-n-drop); stond 'ie al ergens, dan wisselen.
	UFUNCTION(BlueprintCallable, Category = "WeedShop|Inventory")
	void AssignHotbarStack(int32 Slot, int32 StackId);

	// Haal een stapel van de hotbar af.
	UFUNCTION(BlueprintCallable, Category = "WeedShop|Inventory")
	void UnassignHotbarStack(int32 StackId);

	// --- Rooster-indeling (items blijven staan waar je ze neerzet; nieuwe items vullen lege gaten) ---
	// StackId per rooster-cel (0 = leeg). Vaste posities, niet automatisch herschikt.
	const TArray<int32>& GetGridOrder() const { return GridOrder; }

	// Rooster-cel van een stapel (INDEX_NONE als niet geplaatst).
	int32 GetStackCell(int32 StackId) const { return GridOrder.IndexOfByKey(StackId); }

	// Verplaats een stapel naar een rooster-cel (wisselt met wat daar stond). Voor drag-and-drop.
	UFUNCTION(BlueprintCallable, Category = "WeedShop|Inventory")
	void MoveStackToCell(int32 StackId, int32 Cell);

	// Sorteer het rooster (0=naam, 1=aantal, 2=categorie) en pak van voren aan.
	UFUNCTION(BlueprintCallable, Category = "WeedShop|Inventory")
	void SortGrid(int32 Mode);

protected:
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UPROPERTY(ReplicatedUsing = OnRep_Stacks)
	TArray<FInventoryStack> Stacks;

	UFUNCTION()
	void OnRep_Stacks();

	// Eerste stapel-index met dit item-id, of INDEX_NONE.
	int32 FindStackIndex(FName ItemId) const;

	// Stapel om in te mergen: zelfde item-id én (voor wiet) bijna gelijke THC%/Kwaliteit%. Wiet met
	// afwijkende kwaliteit krijgt zo een eigen stapel; -1 voor thc/quality = geen kwaliteit-eis.
	int32 FindMergeStackIndex(FName ItemId, float ThcPercent, float QualityPct) const;

	// Hotbar netjes houden: verwijder verdwenen StackId's, vul lege slots automatisch.
	void RefreshHotbarAuto();

	// Rooster-indeling bijwerken: verwijder verdwenen StackId's (laat het gat staan), zet nieuwe
	// stapels in de eerste vrije cel. Bestaande posities blijven staan.
	void RefreshGridAuto();

	// Categorie-rang voor sorteren (lager = eerder): Bud, Joint, Seed, Papers, Pot, Soil, Water, rest.
	static int32 CategoryRank(FName ItemId);

	int32 ActiveSlot = 0;

	// Co-op: de hotbar (HotbarStacks) is lokaal, dus de SERVER weet niet wat een client vasthoudt.
	// De eigenaar-client pusht daarom de actieve StackId naar de server, zodat server-interacties
	// (soil/seed/spray/water gebruiken) het juiste hand-item zien. GetActiveItemId leest deze waarde.
	int32 ActiveStackId = 0;
	void RefreshActiveStack();
	UFUNCTION(Server, Reliable) void ServerReportActiveStack(int32 StackId);

	// StackId per hotbar-slot (0 = leeg). Lokaal, niet gerepliceerd.
	UPROPERTY(Transient)
	TArray<int32> HotbarStacks;

	// Stapels die we al "gezien" hebben: alleen gloednieuwe stapels worden automatisch op de hotbar
	// gezet. Zo blijft een handmatige unassign staan i.p.v. dat de auto-fill 'm meteen terugzet.
	TSet<int32> KnownStacks;

	// Vaste rooster-indeling: StackId per cel (0 = leeg). Lokale UI-staat, niet gerepliceerd.
	UPROPERTY(Transient)
	TArray<int32> GridOrder;

	// Split in behandeling: de eerstvolgende nieuwe stapel hoort in het ROOSTER (niet auto op de hotbar).
	// PendingSplitCell = gewenste cel (-1 = eerste vrije rooster-cel). Lokale UI-staat.
	bool bPendingSplit = false;
	int32 PendingSplitCell = -1;

	// Server-teller voor unieke StackId's.
	int32 NextStackId = 1;
};
