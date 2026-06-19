// AStorageShelf — fysiek opslag-schap in de shop. Sla verpakte voorraad (en andere items) op,
// zodat je niet alles in je eigen inventory hoeft te dragen. Interacteren opent het schap-menu
// (UShelfWidget) waar je stacks tussen je inventory en het schap verplaatst. Server-authoritative.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Interaction/Interactable.h"
#include "World/ProcessorMachine.h" // FProcEntry + statisch Fridge_Std-recept (koelkast zet ButterMix -> edibles)
#include "StorageShelf.generated.h"

class UStaticMeshComponent;
class UInventoryComponent;

// Eén opgeslagen stapel op het schap (met kwaliteit/THC behouden).
USTRUCT(BlueprintType)
struct FShelfStack
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Shelf") FName ItemId = NAME_None;
	UPROPERTY(BlueprintReadOnly, Category = "Shelf") int32 Quantity = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Shelf") float Thc = 0.f;        // THC% (Quality-veld in de inventory)
	UPROPERTY(BlueprintReadOnly, Category = "Shelf") float QualityPct = 0.f; // kwaliteit%
};

UCLASS()
class WEEDSHOPCORE_API AStorageShelf : public AActor, public IInteractable
{
	GENERATED_BODY()

public:
	AStorageShelf();

	virtual void BeginPlay() override;
	virtual void OnConstruction(const FTransform& Transform) override;

	// Tier-id (= ItemId waarmee 'm geplaatst is). Bepaalt grootte (en later capaciteit/uiterlijk).
	UPROPERTY(ReplicatedUsing = OnRep_Tier, BlueprintReadOnly, Category = "WeedShop|Shelf")
	FName ShelfTier = TEXT("Shelf");

	UFUNCTION()
	void OnRep_Tier();

	void SetupVisual();

	// Versheid: laat boter/edibles in dit (niet-Fridge) meubel langzaam in kwaliteit zakken.
	void DegradeShelfPerishables();
	FTimerHandle PerishTimer;

	// Koelkast: laat edible-batches lopen (server-timer) en stort klaar resultaat in de koelkast-voorraad.
	void TickCooking();
	FTimerHandle CookTimer;

	// Aantal verschillende stapels dat dit opslag-meubel kan bevatten (per type).
	int32 GetCapacity() const;

	// Weergavenaam voor de UI ("Storage shelf" / "Storage chest"), uit de placeable-def.
	FString GetTitle() const;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "WeedShop|Shelf")
	TArray<FShelfStack> Contents;

	// Koelkast-verwerking: lopende edible-batches (ButterMix -> Edible/Cookie/Gummy). Klaar -> vanzelf in Contents.
	// Een fridge is gewoon een fridge: hierin maak je boter/edibles, geen aparte "conversion kit" nodig.
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "WeedShop|Shelf")
	TArray<FProcEntry> Cooking;

	bool IsFridge() const { return ShelfTier.ToString().StartsWith(TEXT("Fridge")); }
	int32 FridgeCookCap() const { return 4; } // max gelijktijdige edible-batches in een koelkast

	// Server: start een edible-batch (Strain = strain-naam, OutPrefix = Edible_/Cookie_/Gummy_). False = vol/fout.
	bool ServerStartEdible(const FString& Strain, int32 Qty, float Thc, float Qual, const FString& OutPrefix);

	// Server: leg Count stuks van ItemId (met THC/kwaliteit) op het schap. Geeft het werkelijk
	// opgeslagen aantal terug (0 als vol).
	int32 ServerStore(FName ItemId, int32 Count, float Thc, float QualityPct);

	// Server: haal Count stuks uit schap-slot SlotIndex. Vult de item-info in. Geeft aantal terug.
	int32 ServerTake(int32 SlotIndex, int32 Count, FName& OutId, float& OutThc, float& OutQualityPct);

	// IInteractable (openen van de UI gebeurt lokaal in de character).
	virtual void Interact_Implementation(APawn* InstigatorPawn) override;
	virtual FText GetInteractionPrompt_Implementation() const override;

protected:
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "WeedShop|Shelf")
	TObjectPtr<UStaticMeshComponent> Mesh;

	UPROPERTY()
	TObjectPtr<class UMaterialInstanceDynamic> DynMat; // voor per-type kleur in SetupVisual

	// Samengestelde look (zijpanelen + planken, of kist + deksel), los van de root-schaal.
	UPROPERTY() TObjectPtr<USceneComponent> Deco;
	UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> Parts;
};
