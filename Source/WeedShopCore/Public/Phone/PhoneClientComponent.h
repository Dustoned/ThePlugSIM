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
class AGrowPlant;

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

	// --- iPhone-achtige home/apps ---
	static constexpr int32 AppCount = 6; // 0=Upgrades 1=Suppliers 2=Contacts 3=Messages 4=Settings 5=Map

	// Open een app (zet 'm als actief scherm; verlaat het home-scherm).
	UFUNCTION(BlueprintCallable, Category = "WeedShop|Phone")
	void OpenApp(int32 AppIndex);

	// Terug naar het home-scherm met de app-iconen.
	UFUNCTION(BlueprintCallable, Category = "WeedShop|Phone")
	void GoHome();

	UFUNCTION(BlueprintPure, Category = "WeedShop|Phone")
	bool IsHomeScreen() const { return bHomeScreen; }

	// Supplier-subcategorie (0=Seeds,1=Papers,2=Pots,3=Soil,4=Water).
	UFUNCTION(BlueprintCallable, Category = "WeedShop|Phone")
	void SetSupplierCat(int32 Cat);

	UFUNCTION(BlueprintPure, Category = "WeedShop|Phone")
	int32 GetSupplierCat() const { return SupplierCat; }

	// Voert de actie voor catalogus-index Index uit in de actieve tab (kopen / afspraak).
	UFUNCTION(BlueprintCallable, Category = "WeedShop|Phone")
	void DoAction(int32 Index);

	// Verkoop het item op voorraad-stapel StackIndex aan de supplier (70% terug).
	UFUNCTION(BlueprintCallable, Category = "WeedShop|Phone")
	void SellInventoryIndex(int32 StackIndex);

	// Verkoop ALLE stuks van het item op voorraad-stapel StackIndex (bv. 10x in één klik).
	UFUNCTION(BlueprintCallable, Category = "WeedShop|Phone")
	void SellInventoryIndexAll(int32 StackIndex);

	// --- Winkel: aantal-keuze per item + winkelwagen (client-side UI) ---
	// Gekozen aantal voor een catalogus-item (default 1).
	int32 GetPendingQty(FName ItemId) const;
	void AdjustPendingQty(FName ItemId, int32 Delta);

	// Voeg het gekozen aantal van dit item toe aan de winkelwagen.
	void AddToCart(FName ItemId);

	// Winkelwagen-regels.
	int32 GetCartNumLines() const { return Cart.Num(); }
	bool GetCartLine(int32 Index, FName& OutItemId, int32& OutQty) const;
	void AdjustCartLine(int32 Index, int32 Delta);   // Delta op het aantal; <=0 -> regel weg
	void ClearCart();
	int32 GetCartTotalCents() const;

	// Reken de hele winkelwagen af (server koopt alles, daarna leeg).
	UFUNCTION(BlueprintCallable, Category = "WeedShop|Phone")
	void Checkout();

	// Cijfertoets-handler (1-6) als reserve naast klikken.
	void HandleNumberKey(FKey Key);

	UFUNCTION(BlueprintPure, Category = "WeedShop|Phone")
	bool IsOpen() const { return bOpen; }

	// --- Inventory-scherm (drag-n-drop naar hotbar) ---
	UFUNCTION(BlueprintCallable, Category = "WeedShop|Inventory")
	void ToggleInventory();

	UFUNCTION(BlueprintPure, Category = "WeedShop|Inventory")
	bool IsInventoryOpen() const { return bInventoryOpen; }

	// --- Per-pot upgrades ---
	// Open het upgrade-paneel voor de aangekeken pot (door de interactie/U-toets aangeroepen).
	void OpenPotUpgrade(AGrowPlant* Pot);

	UFUNCTION(BlueprintCallable, Category = "WeedShop|Pot")
	void ClosePotUpgrade();

	UFUNCTION(BlueprintCallable, Category = "WeedShop|Pot")
	void BuyPotUpgrade(int32 UpgIndex);

	UFUNCTION(BlueprintPure, Category = "WeedShop|Pot")
	bool IsPotUpgradeOpen() const { return bPotUpgradeOpen; }

	UFUNCTION(BlueprintPure, Category = "WeedShop|Pot")
	AGrowPlant* GetUpgradePot() const { return UpgPot.Get(); }

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
	static constexpr int32 GramsHardMax = 10;
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

	// Welk product je nu aanbiedt (gevraagd product, of een andere strain = substituut).
	UFUNCTION(BlueprintPure, Category = "WeedShop|Deal")
	FName GetOfferedProduct() const;

	// Kies welk product je aanbiedt (NAME_None = terug naar het gevraagde product).
	UFUNCTION(BlueprintCallable, Category = "WeedShop|Deal")
	void SetOfferedProduct(FName ProductId);

	// Of het huidige aanbod een andere strain is dan gevraagd.
	UFUNCTION(BlueprintPure, Category = "WeedShop|Deal")
	bool IsOfferingSubstitute() const;

	// Marktprijs (cents) van het nu aangeboden product.
	UFUNCTION(BlueprintPure, Category = "WeedShop|Deal")
	int32 GetOfferMarketCents() const;

	// De HUD meldt dat een knop/hit-box deze klik heeft verwerkt; zo voorkomen we dat dezelfde
	// muisklik (na het sluiten van een paneel) ook nog een wereld-interactie/heropening triggert.
	void MarkUiClickConsumed();
	bool DidUiConsumeClickRecently() const;

	// Voortgang van het "rechtermuisknop inhouden om te roken" (0..1; 0 = niet bezig). De character
	// vult dit; de HUD tekent er een duidelijke balk mee.
	void SetSmokeHoldFrac(float Frac) { SmokeHoldFrac = FMath::Clamp(Frac, 0.f, 1.f); }

	UFUNCTION(BlueprintPure, Category = "WeedShop|Roll")
	float GetSmokeHoldFrac() const { return SmokeHoldFrac; }

	// Stoned-info voor de HUD (door de character bijgewerkt): fractie resterend (0..1), resterende
	// seconden en de XP-bonus van de huidige high.
	void SetStonedHud(float Frac, float Seconds, int32 XpBonus) { StonedHudFrac = Frac; StonedHudSecs = Seconds; StonedHudXp = XpBonus; }
	float GetStonedHudFrac() const { return StonedHudFrac; }
	float GetStonedHudSecs() const { return StonedHudSecs; }
	int32 GetStonedHudXp() const { return StonedHudXp; }

	// --- Wiet-batches mergen (bevestig-popup) ---
	void OpenMerge(FName ItemId);

	UFUNCTION(BlueprintCallable, Category = "WeedShop|Inventory")
	void CloseMerge();

	UFUNCTION(BlueprintCallable, Category = "WeedShop|Inventory")
	void ConfirmMerge();

	UFUNCTION(BlueprintPure, Category = "WeedShop|Inventory")
	bool IsMergeOpen() const { return bMergeOpen; }

	UFUNCTION(BlueprintPure, Category = "WeedShop|Inventory")
	FName GetMergeItemId() const { return MergeItemId; }

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

	// Server: dien het bod in bij de klant op een specifiek product (betaalt naar de kas).
	UFUNCTION(Server, Reliable)
	void ServerSubmitOffer(ACustomerBase* Customer, FName ProductId, int32 AskCents);

	// Server: verkoop 1 van dit item uit de eigen inventory aan de supplier.
	UFUNCTION(Server, Reliable)
	void ServerSell(FName ItemId);

	// Server: verkoop alle stuks van dit item uit de eigen inventory aan de supplier.
	UFUNCTION(Server, Reliable)
	void ServerSellAll(FName ItemId);

	// Server: koop de hele winkelwagen (parallelle arrays item-id + aantal).
	UFUNCTION(Server, Reliable)
	void ServerBuyCart(const TArray<FName>& ItemIds, const TArray<int32>& Quantities);

	// Server: voeg alle stapels van dit item-id samen (gewogen gemiddelde THC%/Kwaliteit%).
	UFUNCTION(Server, Reliable)
	void ServerMergeItem(FName ItemId);

	// Server: koop pot-upgrade UpgIndex voor de gegeven pot (kosten van de kas).
	UFUNCTION(Server, Reliable)
	void ServerBuyPotUpgrade(AGrowPlant* Pot, int32 UpgIndex);

	AWeedShopGameState* GetGS() const;
	APlayerController* GetPC() const;
	UInventoryComponent* GetOwnerInventory() const;

	// Zet muis-cursor/input-mode op basis van of er een UI open is.
	void UpdateCursor();

	// Maakt de UMG-telefoon-widget aan (lokale client, lui bij eerste opening).
	void EnsureWidget();

	UPROPERTY(Transient)
	TObjectPtr<class UPhoneWidget> PhoneWidget;

	bool bOpen = false;
	int32 Tab = 0;
	bool bHomeScreen = true; // toon het app-rooster i.p.v. een geopende app
	int32 SupplierCat = 0;

	bool bRollOpen = false;
	int32 RollGrams = 2;

	float SmokeHoldFrac = 0.f; // 0..1 voortgang van rook-inhouden (lokale UI-staat)
	float StonedHudFrac = 0.f; // 0..1 resterende high voor de HUD
	float StonedHudSecs = 0.f; // resterende high-seconden
	int32 StonedHudXp = 0;     // XP-bonus van de huidige high

	bool bDealOpen = false;
	TWeakObjectPtr<ACustomerBase> DealCustomer;
	int32 DealAskCents = 0;
	FName DealAltProduct = NAME_None; // andere strain die je aanbiedt (None = het gevraagde product)

	// Tijdstip (wereldseconden) waarop de UI voor het laatst een klik verwerkte.
	double LastUiClickTime = -100.0;

	bool bInventoryOpen = false;

	bool bMergeOpen = false;
	FName MergeItemId = NAME_None;

	// Winkel-state (client-side): gekozen aantal per item + winkelwagen.
	struct FCartLine { FName ItemId = NAME_None; int32 Qty = 0; };
	TArray<FCartLine> Cart;
	TMap<FName, int32> PendingQty;

	bool bPotUpgradeOpen = false;
	TWeakObjectPtr<AGrowPlant> UpgPot;
};
