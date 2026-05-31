// UInventoryComponent — voorraad als stapels (item-id + aantal). Server-authoritative en
// replicated, zodat co-op-spelers dezelfde voorraad zien. Item-id's verwijzen naar rij-namen
// in DT_Products (of later DT_Strains-opbrengsten).
//
// Bedoeld om op de speler-pawn/PlayerState te zitten (persoonlijke voorraad) of op een
// opslag-actor (gedeelde voorraad). Mutaties (AddItem/RemoveItem) draaien alleen op de server.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "InventoryComponent.generated.h"

// Eén voorraad-stapel: een item-id en hoeveel ervan.
USTRUCT(BlueprintType)
struct FInventoryStack
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory")
	FName ItemId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory")
	int32 Quantity = 0;

	// Kwaliteit/THC% van deze stapel (alleen zinvol voor wiet/joints; 0 = n.v.t.).
	// Bij samenvoegen van dezelfde soort wordt dit gewogen gemiddeld.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory")
	float Quality = 0.f;
};

// Vuurt na elke voorraad-wijziging (server én clients), voor UI-refresh.
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnInventoryChanged);

UCLASS(ClassGroup = (WeedShop), meta = (BlueprintSpawnableComponent))
class WEEDSHOPCORE_API UInventoryComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UInventoryComponent();

	// Max aantal verschillende stapels/slots (0 = ongelimiteerd).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WeedShop|Inventory")
	int32 MaxStacks = 24;

	// Max draaggewicht (abstracte kg). 0 = ongelimiteerd.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WeedShop|Inventory")
	float MaxWeight = 60.f;

	// Gewicht per stuk van een item (kg). Som hiervan = totaalgewicht.
	UFUNCTION(BlueprintPure, Category = "WeedShop|Inventory")
	float GetUnitWeight(FName ItemId) const;

	UFUNCTION(BlueprintPure, Category = "WeedShop|Inventory")
	float GetTotalWeight() const;

	// Gebruikte slots: elke waterfles telt apart (zodat meerdere flessen ook meerdere slots kosten).
	UFUNCTION(BlueprintPure, Category = "WeedShop|Inventory")
	int32 GetUsedSlots() const;

	// Of dit item-id momenteel aan een hotbar-slot is toegewezen.
	UFUNCTION(BlueprintPure, Category = "WeedShop|Inventory")
	bool IsOnHotbar(FName ItemId) const;

	// Haal een item van de hotbar af (alle slots die het bevatten leegmaken).
	UFUNCTION(BlueprintCallable, Category = "WeedShop|Inventory")
	void UnassignHotbar(FName ItemId);

	UPROPERTY(BlueprintAssignable, Category = "WeedShop|Inventory")
	FOnInventoryChanged OnInventoryChanged;

	// Server-authoritative. Voegt Count toe aan de stapel van ItemId (maakt stapel aan indien nodig).
	// Quality < 0 = geen kwaliteit-info (papers/zaden/etc). Bij samenvoegen van een stapel met
	// kwaliteit wordt het gewogen gemiddelde genomen. Geeft false als er geen ruimte is (MaxStacks).
	UFUNCTION(BlueprintCallable, Category = "WeedShop|Inventory")
	bool AddItem(FName ItemId, int32 Count, float Quality = -1.f);

	// Kwaliteit/THC% van de stapel met dit item-id (0 als geen/n.v.t.).
	UFUNCTION(BlueprintPure, Category = "WeedShop|Inventory")
	float GetItemQuality(FName ItemId) const;

	// Server-authoritative. Haalt Count weg; false bij onvoldoende voorraad.
	UFUNCTION(BlueprintCallable, Category = "WeedShop|Inventory")
	bool RemoveItem(FName ItemId, int32 Count);

	UFUNCTION(BlueprintPure, Category = "WeedShop|Inventory")
	int32 GetQuantity(FName ItemId) const;

	UFUNCTION(BlueprintPure, Category = "WeedShop|Inventory")
	bool HasItem(FName ItemId, int32 Count = 1) const { return GetQuantity(ItemId) >= Count; }

	UFUNCTION(BlueprintPure, Category = "WeedShop|Inventory")
	const TArray<FInventoryStack>& GetStacks() const { return Stacks; }

	// --- Hotbar-selectie (puur lokale UI-staat: welk slot heb je "in de hand") ---
	// Aantal hotbar-slots (de eerste N voorraad-stapels).
	static constexpr int32 HotbarSize = 8;

	UFUNCTION(BlueprintCallable, Category = "WeedShop|Inventory")
	void SetActiveSlot(int32 Slot);

	// Schuif de selectie door (dir = +1 / -1), wrapt rond binnen de hotbar.
	UFUNCTION(BlueprintCallable, Category = "WeedShop|Inventory")
	void CycleActiveSlot(int32 Dir);

	UFUNCTION(BlueprintPure, Category = "WeedShop|Inventory")
	int32 GetActiveSlot() const { return ActiveSlot; }

	// Item-id van het geselecteerde slot (NAME_None als dat slot leeg is / niet meer op voorraad).
	UFUNCTION(BlueprintPure, Category = "WeedShop|Inventory")
	FName GetActiveItemId() const;

	// Het item dat aan hotbar-slot Slot is toegewezen (NAME_None = leeg).
	UFUNCTION(BlueprintPure, Category = "WeedShop|Inventory")
	FName GetHotbarItem(int32 Slot) const;

	// Wijs een item toe aan een hotbar-slot (drag-n-drop). Stond het item al in een ander slot,
	// dan wisselen die twee slots (verplaatsen i.p.v. dupliceren).
	UFUNCTION(BlueprintCallable, Category = "WeedShop|Inventory")
	void AssignHotbar(int32 Slot, FName ItemId);

protected:
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UPROPERTY(ReplicatedUsing = OnRep_Stacks)
	TArray<FInventoryStack> Stacks;

	UFUNCTION()
	void OnRep_Stacks();

	// Vindt de index van een stapel met dit item-id, of INDEX_NONE.
	int32 FindStackIndex(FName ItemId) const;

	// Houdt de hotbar-toewijzing netjes: verwijder items die op zijn, vul lege slots met
	// nieuwe voorraad. Behoudt handmatige (drag-n-drop) toewijzingen. Lokaal.
	void RefreshHotbarAuto();

	// Geselecteerd hotbar-slot (lokaal, niet gerepliceerd — puur UI/“in de hand”).
	int32 ActiveSlot = 0;

	// Welk item in elk hotbar-slot zit (lokale UI-staat, niet gerepliceerd). Grootte = HotbarSize.
	UPROPERTY(Transient)
	TArray<FName> HotbarSlots;
};
