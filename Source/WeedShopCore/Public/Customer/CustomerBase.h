// ACustomerBase — klant/prospect met een C++ state-machine (geen Behaviour Tree, zodat alle
// logica in code blijft). Heeft per-klant attributen respect/loyaliteit/verslaving, een gewenst
// product + hoeveelheid, en een geduld-timer. Server-authoritative; attributen + staat repliceren.
//
// Deal: de UI (prijs-slider) roept SubmitOffer aan; voor snel testen verkoopt Interact (E) meteen
// tegen marktprijs uit de voorraad van de speler naar de gedeelde kas.
//
// Editor: maak BP_Customer (parent = ACustomerBase), wijs DT_Products toe aan ProductTable.
// NavMesh + spawn/wachtrij komen later (editor); voor nu plaats je 'm handmatig om te testen.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "Interaction/Interactable.h"
#include "Deal/WeedDealLibrary.h"
#include "CustomerBase.generated.h"

class UDataTable;
class UEconomyComponent;
class UInventoryComponent;
class UStaticMeshComponent;

UENUM(BlueprintType)
enum class ECustomerState : uint8
{
	WantsToOrder	UMETA(DisplayName = "Wil bestellen"),
	Negotiating		UMETA(DisplayName = "Onderhandelt"),
	Served			UMETA(DisplayName = "Geholpen"),
	Leaving			UMETA(DisplayName = "Vertrekt")
};

UCLASS()
class WEEDSHOPCORE_API ACustomerBase : public ACharacter, public IInteractable
{
	GENERATED_BODY()

public:
	ACustomerBase();

	virtual void Tick(float DeltaSeconds) override;

	// DataTable met producten (FWeedShopProductRow) voor marktprijs-opzoek.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WeedShop|Customer")
	TObjectPtr<UDataTable> ProductTable;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, ReplicatedUsing = OnRep_Order, Category = "WeedShop|Customer")
	FName DesiredProductId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Replicated, Category = "WeedShop|Customer")
	int32 DesiredQuantity = 1;

	// Runtime-attributen (0..100), gerepliceerd voor UI.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Replicated, Category = "WeedShop|Customer")
	float Respect = 20.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Replicated, Category = "WeedShop|Customer")
	float Loyalty = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Replicated, Category = "WeedShop|Customer")
	float Addiction = 10.f;

	// Maximaal bedrag per eenheid dat hij wil betalen (cents).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WeedShop|Customer")
	int32 BudgetCentsPerUnit = 2000;

	// Geduld in seconden; loopt af terwijl hij wacht.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WeedShop|Customer")
	float PatienceSeconds = 30.f;

	// Na een aankoop moet de klant z'n spul eerst oproken: cooldown (sec) voordat hij weer wil.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WeedShop|Customer")
	float OrderCooldownSeconds = 60.f;

	// Appartement-/straat-klanten blijven (cooldown -> opnieuw bestellen). Afspraak-klanten despawnen.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WeedShop|Customer")
	bool bDespawnAfterServed = false;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "WeedShop|Customer")
	ECustomerState State = ECustomerState::WantsToOrder;

	// Marktprijs per eenheid van het gewenste product (cents). 0 als onbekend.
	UFUNCTION(BlueprintPure, Category = "WeedShop|Customer")
	int32 GetMarketPriceCents() const;

	// Live acceptatie-% bij een bod (per eenheid) — voor de prijs-slider-UI.
	UFUNCTION(BlueprintPure, Category = "WeedShop|Customer")
	float GetAcceptanceChance(int32 AskPriceCentsPerUnit) const;

	// Server-authoritative bod. Betaalt naar PayTo, haalt voorraad uit StockFrom. Geeft de uitkomst.
	UFUNCTION(BlueprintCallable, Category = "WeedShop|Customer")
	EDealResult SubmitOffer(int32 AskPriceCentsPerUnit, UEconomyComponent* PayTo, UInventoryComponent* StockFrom);

	// IInteractable
	virtual void Interact_Implementation(APawn* InstigatorPawn) override;
	virtual FText GetInteractionPrompt_Implementation() const override;

protected:
	virtual void BeginPlay() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	// Zichtbaar placeholder-lichaam (tot er een echte character-mesh is).
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "WeedShop|Customer")
	TObjectPtr<UStaticMeshComponent> Body;

	UFUNCTION()
	void OnRep_Order();

	void LeaveAngry();
	static float ClampAttr(float V) { return FMath::Clamp(V, 0.f, 100.f); }

	// Seconden sinds de klant klaar is (geholpen/vertrekt) — voor auto-despawn of cooldown.
	float LeaveTimer = 0.f;

	// Begin-geduld (om te herstellen na een cooldown).
	float BasePatienceSeconds = 30.f;
};
