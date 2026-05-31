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

	// Kwaliteit/THC% van deze stapel (alleen zinvol voor wiet/joints; 0 = n.v.t.).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory")
	float Quality = 0.f;

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

	// Server. Voegt Count toe. Stapelbare items mergen; niet-stapelbare (flessen) worden losse
	// stapels van 1 (elk een eigen slot). Quality < 0 = geen kwaliteit-info.
	UFUNCTION(BlueprintCallable, Category = "WeedShop|Inventory")
	bool AddItem(FName ItemId, int32 Count, float Quality = -1.f);

	// Server. Haalt Count weg (over meerdere stapels indien nodig). False bij te weinig.
	UFUNCTION(BlueprintCallable, Category = "WeedShop|Inventory")
	bool RemoveItem(FName ItemId, int32 Count);

	// Totaal aantal van dit item over alle stapels.
	UFUNCTION(BlueprintPure, Category = "WeedShop|Inventory")
	int32 GetQuantity(FName ItemId) const;

	UFUNCTION(BlueprintPure, Category = "WeedShop|Inventory")
	bool HasItem(FName ItemId, int32 Count = 1) const { return GetQuantity(ItemId) >= Count; }

	// Kwaliteit/THC% van de eerste stapel met dit item-id (0 als geen).
	UFUNCTION(BlueprintPure, Category = "WeedShop|Inventory")
	float GetItemQuality(FName ItemId) const;

	UFUNCTION(BlueprintPure, Category = "WeedShop|Inventory")
	const TArray<FInventoryStack>& GetStacks() const { return Stacks; }

	// Index van de stapel met deze StackId (INDEX_NONE als weg).
	int32 FindStackById(int32 StackId) const;

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

protected:
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UPROPERTY(ReplicatedUsing = OnRep_Stacks)
	TArray<FInventoryStack> Stacks;

	UFUNCTION()
	void OnRep_Stacks();

	static bool IsStackable(FName ItemId);

	// Eerste stapel-index met dit item-id, of INDEX_NONE.
	int32 FindStackIndex(FName ItemId) const;

	// Hotbar netjes houden: verwijder verdwenen StackId's, vul lege slots automatisch.
	void RefreshHotbarAuto();

	int32 ActiveSlot = 0;

	// StackId per hotbar-slot (0 = leeg). Lokaal, niet gerepliceerd.
	UPROPERTY(Transient)
	TArray<int32> HotbarStacks;

	// Server-teller voor unieke StackId's.
	int32 NextStackId = 1;
};
