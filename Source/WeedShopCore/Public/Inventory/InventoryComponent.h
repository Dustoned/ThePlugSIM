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
};

// Vuurt na elke voorraad-wijziging (server én clients), voor UI-refresh.
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnInventoryChanged);

UCLASS(ClassGroup = (WeedShop), meta = (BlueprintSpawnableComponent))
class WEEDSHOPCORE_API UInventoryComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UInventoryComponent();

	// Max aantal verschillende stapels (0 = ongelimiteerd).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WeedShop|Inventory")
	int32 MaxStacks = 0;

	UPROPERTY(BlueprintAssignable, Category = "WeedShop|Inventory")
	FOnInventoryChanged OnInventoryChanged;

	// Server-authoritative. Voegt Count toe aan de stapel van ItemId (maakt stapel aan indien nodig).
	// Geeft false als er geen ruimte is voor een nieuwe stapel (MaxStacks bereikt).
	UFUNCTION(BlueprintCallable, Category = "WeedShop|Inventory")
	bool AddItem(FName ItemId, int32 Count);

	// Server-authoritative. Haalt Count weg; false bij onvoldoende voorraad.
	UFUNCTION(BlueprintCallable, Category = "WeedShop|Inventory")
	bool RemoveItem(FName ItemId, int32 Count);

	UFUNCTION(BlueprintPure, Category = "WeedShop|Inventory")
	int32 GetQuantity(FName ItemId) const;

	UFUNCTION(BlueprintPure, Category = "WeedShop|Inventory")
	bool HasItem(FName ItemId, int32 Count = 1) const { return GetQuantity(ItemId) >= Count; }

	UFUNCTION(BlueprintPure, Category = "WeedShop|Inventory")
	const TArray<FInventoryStack>& GetStacks() const { return Stacks; }

protected:
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UPROPERTY(ReplicatedUsing = OnRep_Stacks)
	TArray<FInventoryStack> Stacks;

	UFUNCTION()
	void OnRep_Stacks();

	// Vindt de index van een stapel met dit item-id, of INDEX_NONE.
	int32 FindStackIndex(FName ItemId) const;
};
