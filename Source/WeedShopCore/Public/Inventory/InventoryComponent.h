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

	// Max aantal slots (= aantal stapels; 0 = ongelimiteerd). Start bewust laag (je hebt ook 8 hotbar-slots);
	// grid + UI volgen deze waarde automatisch. Cel 0 = cash-stapel, dus effectief ~9 vrije slots aan 't begin.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WeedShop|Inventory")
	int32 MaxStacks = 10;

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

	// Plaats de eerstvolgende NIEUWE stapel (bv. een item uit een storage-transfer dat je op een rooster-cel
	// dropt) in die specifieke rooster-cel i.p.v. automatisch op de hotbar. Hergebruikt het split-placement-
	// mechanisme (bPendingSplit/PendingSplitCell): RefreshHotbarAuto slaat de hotbar over, RefreshGridAuto plaatst 'm.
	void SetPendingGridCell(int32 Cell) { bPendingSplit = true; PendingSplitCell = Cell; }

	// Voeg PRECIES twee stapels samen (sleep From op Into). Alleen zelfde, stapelbaar item.
	void RequestMergeTwo(int32 IntoStackId, int32 FromStackId) { ServerMergeTwo(IntoStackId, FromStackId); }
	UFUNCTION(Server, Reliable) void ServerMergeTwo(int32 IntoStackId, int32 FromStackId);

	// Drop een hele stapel op de grond (sleep 'm de inventory UIT, los op niks) -> wereld-pickup bij de voeten.
	void RequestDropStack(int32 StackId) { ServerDropStack(StackId); }
	UFUNCTION(Server, Reliable) void ServerDropStack(int32 StackId);

	// Of dit item stapelbaar is (bv. flessen niet). Bepaalt of merge/split mag.
	static bool IsStackable(FName ItemId);

	// --- Zakjes (bags): discrete eenheden Bag_<strain>_<gram>, max BagStackMax per slot ---
	static constexpr int32 BagStackMax = 10;          // max aantal zakjes per slot
	static bool IsBag(FName ItemId);
	static int32 BagGrams(FName ItemId);              // gram per zakje (0 = oude/maatloze bag)
	static FName BagStrain(FName ItemId);             // strain-deel, bv. "SilverHaze"
	static FName MakeBagId(FName Strain, FName ContainerId, int32 Grams);
	static FName BagContainer(FName ItemId);   // -> "Cont_Bag2" etc, of NAME_None voor oude 2-token bags

	// --- Joints: id onthoudt nu ook de STRAIN. Joint_<Strain>_<G>g (nieuw) / Joint_<G>g (oud, back-compat) ---
	static FName MakeJointId(FName Strain, int32 Grams);   // Joint_<Strain>_<G>g  (Joint_<G>g als Strain==None)
	static FName JointStrain(FName ItemId);                // strain, of NAME_None voor oude joints
	static int32 JointGrams(FName ItemId);                 // gram uit de laatste token ("3g"->3)
	// Totaal aantal grammen in alle zakjes van een strain (som van aantal * grootte).
	int32 BagGramsAvailable(FName Strain) const;
	// Totaal aantal GRAMMEN in zakjes van deze strain + de (op gram gewogen) THC%/kwaliteit. Voor de deal-stock.
	int32 BagStockGrams(FName Strain, float& OutThc, float& OutQualPct) const;
	// Verwijder HELE zakjes van een strain tot ~DesiredGrams (>= indien mogelijk, minimale overschot);
	// geeft de werkelijk verkochte grammen + gewogen THC/kwaliteit terug. Server-only.
	int32 RemoveBagsForGrams(FName Strain, int32 DesiredGrams, float& OutThc, float& OutQualPct);

	// Server. Zet de inventory EXACT terug zoals opgeslagen: elke stack op z'n opgeslagen grid-cel,
	// geen merge/sortering. InCells[i] = grid-cel van InStacks[i] (-1 = eerste vrije). De cash-stack
	// (afgeleid van economy) blijft op cel 0. Voor save/load zodat slots niet meer wisselen.
	// InHotbarSlots[i] = hotbar-slot van stapel i (-1 = niet op de hotbar). Hotbar-stapels gaan NIET in het backpack-rooster.
	void RestoreStacksAndGrid(const TArray<FInventoryStack>& InStacks, const TArray<int32>& InCells, const TArray<int32>& InHotbarSlots = TArray<int32>());

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

	// StackId van het geselecteerde hotbar-slot (0 = leeg). Voor per-fles water e.d.
	UFUNCTION(BlueprintPure, Category = "WeedShop|Inventory")
	int32 GetActiveStackId() const { return ActiveStackId; }

	// Quality-veld van een specifieke stack lezen/zetten (bv. water in een fles). SetQuality = server-only.
	UFUNCTION(BlueprintPure, Category = "WeedShop|Inventory")
	float GetStackQualityById(int32 StackId) const;
	void SetStackQualityById(int32 StackId, float Q);

	// Verwijder Count stuks uit één SPECIFIEKE stack (server). Voor per-stack acties zoals het droogrek,
	// zodat alleen die exacte stapel (THC%/kwaliteit) wordt gepakt en niet alle stapels van die strain.
	void RemoveFromStackById(int32 StackId, int32 Count);

	// StackId in hotbar-slot Slot (0 = leeg).
	UFUNCTION(BlueprintPure, Category = "WeedShop|Inventory")
	int32 GetHotbarStackId(int32 Slot) const;
	// Op welk hotbar-slot zit deze stapel (-1 = geen). Voor de save (bewaart de hotbar-toewijzing per stapel).
	int32 GetHotbarSlotOf(int32 StackId) const { return HotbarStacks.IndexOfByKey(StackId); }

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
	// Stapel-id op een rooster-cel (0 = leeg). Voor hotbar-herstel na load (cel -> nieuwe stapel-id).
	int32 GetStackIdAtCell(int32 Cell) const { return GridOrder.IsValidIndex(Cell) ? GridOrder[Cell] : 0; }

	// Verplaats een stapel naar een rooster-cel (wisselt met wat daar stond). Voor drag-and-drop.
	UFUNCTION(BlueprintCallable, Category = "WeedShop|Inventory")
	void MoveStackToCell(int32 StackId, int32 Cell);

	// Sorteer het rooster (0=naam, 1=aantal, 2=categorie) en pak van voren aan.
	UFUNCTION(BlueprintCallable, Category = "WeedShop|Inventory")
	void SortGrid(int32 Mode);

protected:
	virtual void BeginPlay() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	// Versheid: boter/edibles verliezen langzaam QualityPct buiten een Fridge (server-timer).
	void DegradePerishables();
	FTimerHandle PerishTimer;

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
