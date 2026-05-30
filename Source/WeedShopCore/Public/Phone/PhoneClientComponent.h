// UPhoneClientComponent — zit op de speler-pawn en regelt de telefoon-acties (openen, tab,
// kopen, afspraak beantwoorden). De HUD roept dit aan bij klikken; de cijfertoetsen ook.
// Aankopen lopen via Server-RPC's hier (server-authoritative). Decoupled van de template-
// character zodat de HUD (WeedShopCore) het kan aanroepen zonder module-cykel.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "PhoneClientComponent.generated.h"

class AWeedShopGameState;
class APlayerController;
class UInventoryComponent;
class ACustomerBase;

UCLASS(ClassGroup = (WeedShop), meta = (BlueprintSpawnableComponent))
class WEEDSHOPCORE_API UPhoneClientComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UPhoneClientComponent();

	// Open/sluit de telefoon (zet ook muis-cursor + input-mode op de lokale controller).
	UFUNCTION(BlueprintCallable, Category = "WeedShop|Phone")
	void Toggle();

	UFUNCTION(BlueprintCallable, Category = "WeedShop|Phone")
	void SetTab(int32 NewTab);

	UFUNCTION(BlueprintCallable, Category = "WeedShop|Phone")
	void CycleTab();

	// Voert de actie voor catalogus-index Index uit in de actieve tab (kopen / afspraak).
	UFUNCTION(BlueprintCallable, Category = "WeedShop|Phone")
	void DoAction(int32 Index);

	// Cijfertoets-handler (1-6) als reserve naast klikken.
	void HandleNumberKey(FKey Key);

	UFUNCTION(BlueprintPure, Category = "WeedShop|Phone")
	bool IsOpen() const { return bOpen; }

	UFUNCTION(BlueprintPure, Category = "WeedShop|Phone")
	int32 GetTab() const { return Tab; }

	// --- Joint-rollen (roll-UI met grams-keuze) ---
	UFUNCTION(BlueprintCallable, Category = "WeedShop|Roll")
	void ToggleRollUI();

	UFUNCTION(BlueprintCallable, Category = "WeedShop|Roll")
	void SetRollGrams(int32 Grams);

	UFUNCTION(BlueprintCallable, Category = "WeedShop|Roll")
	void ConfirmRoll();

	UFUNCTION(BlueprintPure, Category = "WeedShop|Roll")
	bool IsRollOpen() const { return bRollOpen; }

	UFUNCTION(BlueprintPure, Category = "WeedShop|Roll")
	int32 GetRollGrams() const { return RollGrams; }

	// Maximaal gram per joint dat je huidige papers toelaten (basis 2; betere vloei verhoogt).
	UFUNCTION(BlueprintPure, Category = "WeedShop|Roll")
	int32 GetMaxJointGrams() const;

	// Absolute grenzen (papers tussen MinGrams en GramsHardMax).
	static constexpr int32 MinGrams = 1;
	static constexpr int32 GramsHardMax = 5;
	static constexpr int32 BaseMaxGrams = 2;

	// --- Deal (verkoop aan een klant met prijs-slider) ---
	// Opent het deal-paneel voor de aangekeken klant (lokaal; door de interactie aangeroepen).
	void OpenDeal(ACustomerBase* Customer);

	// Stel de vraagprijs per eenheid in (cents); wordt geklemd op een redelijke band rond de markt.
	UFUNCTION(BlueprintCallable, Category = "WeedShop|Deal")
	void SetDealAskCents(int32 Cents);

	// Stuur het bod naar de server (klant beslist op basis van prijs + respect/loyaliteit/verslaving).
	UFUNCTION(BlueprintCallable, Category = "WeedShop|Deal")
	void ConfirmDeal();

	UFUNCTION(BlueprintCallable, Category = "WeedShop|Deal")
	void CloseDeal();

	UFUNCTION(BlueprintPure, Category = "WeedShop|Deal")
	bool IsDealOpen() const { return bDealOpen; }

	UFUNCTION(BlueprintPure, Category = "WeedShop|Deal")
	ACustomerBase* GetDealCustomer() const { return DealCustomer.Get(); }

	UFUNCTION(BlueprintPure, Category = "WeedShop|Deal")
	int32 GetDealAskCents() const { return DealAskCents; }

	// Prijs-band: van 40% tot 200% van de marktprijs (in stappen van 10% voor de slider).
	static constexpr int32 DealStepCount = 17; // 40,50,...,200 %

protected:
	UFUNCTION(Server, Reliable)
	void ServerBuyUpgrade(FName UpgradeId);

	UFUNCTION(Server, Reliable)
	void ServerBuySeed(FName StrainId);

	UFUNCTION(Server, Reliable)
	void ServerBuySupply(FName SupplyId);

	UFUNCTION(Server, Reliable)
	void ServerRespond(bool bAccept);

	// Server: maak 1 joint van Grams gram bud (item-id Joint_<G>g; meer gram = betere kwaliteit).
	UFUNCTION(Server, Reliable)
	void ServerRollJoint(int32 Grams);

	// Server: dien het bod in bij de klant (betaalt naar de kas, voorraad uit speler-inventory).
	UFUNCTION(Server, Reliable)
	void ServerSubmitOffer(ACustomerBase* Customer, int32 AskCents);

	AWeedShopGameState* GetGS() const;
	APlayerController* GetPC() const;
	UInventoryComponent* GetOwnerInventory() const;

	// Zet muis-cursor/input-mode op basis van of er een UI open is.
	void UpdateCursor();

	bool bOpen = false;
	int32 Tab = 0;

	bool bRollOpen = false;
	int32 RollGrams = 2;

	bool bDealOpen = false;
	TWeakObjectPtr<ACustomerBase> DealCustomer;
	int32 DealAskCents = 0;
};
