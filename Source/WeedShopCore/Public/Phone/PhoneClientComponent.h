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

	// Maakt de UMG-widgets (status/telefoon/deal) aan op de lokale client (lui, idempotent).
	void EnsureWidget();

	UFUNCTION(BlueprintCallable, Category = "WeedShop|Phone")
	void SetTab(int32 NewTab);

	UFUNCTION(BlueprintCallable, Category = "WeedShop|Phone")
	void CycleTab();

	// --- iPhone-achtige home/apps ---
	static constexpr int32 AppCount = 10; // 0 Upgrades 1 Grow 2 Contacts 3 Messages 4 Settings 5 Map 6 Sell 7 Supplies 8 Packages 9 Bank

	// --- Pauze-menu (ESC): overlay met Resume / Settings / Save / Load / Main Menu / Quit ---
	// (Geen echte wereld-pauze in co-op; in standalone pauzeert de wereld wel.)
	UFUNCTION(BlueprintCallable, Category = "WeedShop|Pause")
	void TogglePause();
	void OpenPause();
	UFUNCTION(BlueprintCallable, Category = "WeedShop|Pause")
	void ClosePause();
	UFUNCTION(BlueprintPure, Category = "WeedShop|Pause")
	bool IsPauseOpen() const { return bPauseOpen; }

	// Sluit ALLE in-game UI in één keer (pauze, settings, inventory, telefoon, panelen) - niet het
	// titelscherm. Voor ESC: als er iets open is, gaat alles dicht.
	void CloseAllUI();
	bool IsAnyGameUIOpen() const;

	// Open de telefoon direct op een bepaalde app (gebruikt door het pauze-menu voor Settings).
	void OpenToApp(int32 AppIndex);

	// --- Titelscherm (THE PLUG SIMULATOR): bij opstarten + via pauze "Main menu" ---
	UFUNCTION(BlueprintCallable, Category = "WeedShop|Menu")
	void ShowMainMenu();
	UFUNCTION(BlueprintCallable, Category = "WeedShop|Menu")
	void HideMainMenu();
	UFUNCTION(BlueprintPure, Category = "WeedShop|Menu")
	bool IsMainMenuOpen() const { return bMainMenuOpen; }

	// Toon het titelscherm en open meteen de Load-slot-picker (vanuit het pauze-menu).
	void OpenMainMenuLoad();

	// --- Settings-scherm (graphics + game). ---
	UFUNCTION(BlueprintCallable, Category = "WeedShop|Settings")
	void OpenSettings();
	UFUNCTION(BlueprintCallable, Category = "WeedShop|Settings")
	void CloseSettings();
	UFUNCTION(BlueprintPure, Category = "WeedShop|Settings")
	bool IsSettingsOpen() const { return bSettingsOpen; }

	// Game-instellingen (lokaal toegepast + bewaard in config).
	void ApplyFov(float NewFov);
	void SetLookSensitivity(float S);
	UFUNCTION(BlueprintPure, Category = "WeedShop|Settings")
	float GetFov() const { return FovValue; }
	UFUNCTION(BlueprintPure, Category = "WeedShop|Settings")
	float GetLookSensitivity() const { return LookSensitivity; }
	void LoadGameSettings(); // leest FOV/sensitivity uit config en past FOV toe

	// --- ATM (in de wereld): open/sluit het ATM-scherm (bankieren + storten + overboeken) ---
	UFUNCTION(BlueprintCallable, Category = "WeedShop|ATM")
	void OpenAtm();
	UFUNCTION(BlueprintCallable, Category = "WeedShop|ATM")
	void CloseAtm();
	UFUNCTION(BlueprintPure, Category = "WeedShop|ATM")
	bool IsAtmOpen() const { return bAtmOpen; }

	// --- Verpak-bench (in de wereld): verdeel gedroogde wiet in bakjes/jars (verkoopbare voorraad) ---
	// Batch = hoeveel zakjes deze tafel per keer verwerkt (tier).
	UFUNCTION(BlueprintCallable, Category = "WeedShop|Pack")
	void OpenPack(int32 Batch = 1);
	int32 GetPackBatch() const { return PackBatchUI; }
	UFUNCTION(BlueprintCallable, Category = "WeedShop|Pack")
	void ClosePack();
	UFUNCTION(BlueprintPure, Category = "WeedShop|Pack")
	bool IsPackOpen() const { return bPackOpen; }

	// Capaciteit (gram per bakje) van een container-item. 0 = geen container.
	static int32 ContainerCapacity(FName ContainerId);

	// Verdeel: stop tot capaciteit gram van BudId (gedroogd) in 1 container -> verkoopbare Bag_<strain>.
	UFUNCTION(BlueprintCallable, Category = "WeedShop|Pack")
	void RequestPack(FName BudId, FName ContainerId);
	UFUNCTION(Server, Reliable)
	void ServerPack(FName BudId, FName ContainerId, int32 Batch);

	// Nieuw: verpak een EXACT aantal gram (1..capaciteit) van BudId in 1 container (gram-slider-UI).
	UFUNCTION(BlueprintCallable, Category = "WeedShop|Pack")
	void RequestPackGrams(FName BudId, FName ContainerId, int32 Grams);
	UFUNCTION(Server, Reliable)
	void ServerPackGrams(FName BudId, FName ContainerId, int32 Grams);

	// --- Opslag-schap (in de wereld): stacks tussen je inventory en het schap verplaatsen ---
	void OpenShelf(class AStorageShelf* Shelf);
	UFUNCTION(BlueprintCallable, Category = "WeedShop|Shelf")
	void CloseShelf();
	UFUNCTION(BlueprintPure, Category = "WeedShop|Shelf")
	bool IsShelfOpen() const { return bShelfOpen; }
	class AStorageShelf* GetShelf() const;

	void RequestShelfStore(FName ItemId, int32 Count);
	void RequestShelfTake(int32 SlotIndex, int32 Count);
	UFUNCTION(Server, Reliable)
	void ServerShelfStore(class AStorageShelf* Shelf, FName ItemId, int32 Count);
	UFUNCTION(Server, Reliable)
	void ServerShelfTake(class AStorageShelf* Shelf, int32 SlotIndex, int32 Count);

	// --- Droogrek (in de wereld): natte wiet ophangen + gedroogde batches oogsten ---
	void OpenDryRack(class ADryingRack* Rack);
	UFUNCTION(BlueprintCallable, Category = "WeedShop|Dry")
	void CloseDryRack();
	UFUNCTION(BlueprintPure, Category = "WeedShop|Dry")
	bool IsDryRackOpen() const { return bDryRackOpen; }
	class ADryingRack* GetDryRack() const;

	void RequestDryHang(FName WetId);
	void RequestDryCollect(int32 Index);
	void RequestDryCollectAll();
	UFUNCTION(Server, Reliable)
	void ServerDryHang(class ADryingRack* Rack, FName WetId);
	UFUNCTION(Server, Reliable)
	void ServerDryCollect(class ADryingRack* Rack, int32 Index);
	UFUNCTION(Server, Reliable)
	void ServerDryCollectAll(class ADryingRack* Rack);

	// Open een app (zet 'm als actief scherm; verlaat het home-scherm).
	UFUNCTION(BlueprintCallable, Category = "WeedShop|Phone")
	void OpenApp(int32 AppIndex);

	// Terug naar het home-scherm met de app-iconen.
	UFUNCTION(BlueprintCallable, Category = "WeedShop|Phone")
	void GoHome();

	UFUNCTION(BlueprintPure, Category = "WeedShop|Phone")
	bool IsHomeScreen() const { return bHomeScreen; }

	// Forceer de juiste input-modus (bv. na een verse start/level-reload waar geen menu werd gesloten).
	UFUNCTION(BlueprintCallable, Category = "WeedShop|Phone")
	void RefreshInputMode();

	// Supplier-subcategorie (0=Seeds,1=Papers,2=Pots,3=Soil,4=Water).
	UFUNCTION(BlueprintCallable, Category = "WeedShop|Phone")
	void SetSupplierCat(int32 Cat);

	UFUNCTION(BlueprintPure, Category = "WeedShop|Phone")
	int32 GetSupplierCat() const { return SupplierCat; }

	// Voert de actie voor catalogus-index Index uit in de actieve tab (kopen / afspraak).
	UFUNCTION(BlueprintCallable, Category = "WeedShop|Phone")
	void DoAction(int32 Index);

	// Beantwoord de afspraak van een specifiek contact vanuit hun chat-thread.
	UFUNCTION(BlueprintCallable, Category = "WeedShop|Phone")
	void RespondChat(FName ContactId, bool bAccept) { ServerRespondContact(ContactId, bAccept); }

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

	// Idem maar voor de verkoop-pagina (aparte teller per item).
	int32 GetPendingSellQty(FName ItemId) const;
	void AdjustPendingSellQty(FName ItemId, int32 Delta);

	// Voeg het gekozen aantal van dit item toe aan de winkelwagen (koop- of verkoop-regel).
	void AddToCart(FName ItemId);
	void AddSellToCart(FName ItemId);

	// Winkelwagen-regels.
	int32 GetCartNumLines() const { return Cart.Num(); }
	bool GetCartLine(int32 Index, FName& OutItemId, int32& OutQty, bool& bOutSell) const;
	void AdjustCartLine(int32 Index, int32 Delta);   // Delta op het aantal; <=0 -> regel weg
	void ClearCart();
	int32 GetCartTotalCents() const;     // alleen koop-subtotaal (achterwaarts compatibel)
	int32 GetCartBuyCents() const;       // som van de koop-regels
	int32 GetCartSellCents() const;      // som van de verkoop-regels (opbrengst)
	// Netto te betalen incl. bezorgkosten op het koopdeel. Negatief = je ontvangt geld.
	int32 GetCartNetCents(int32 DeliveryOption) const;

	// Reken de hele winkelwagen af met een bezorgoptie (0=Standard, 1=Express, 2=Instant).
	UFUNCTION(BlueprintCallable, Category = "WeedShop|Phone")
	void Checkout(int32 DeliveryOption);

	// Stort cash -> bank (witwassen). CashAmount<=0 = maximaal (tot de dag-limiet). Client -> server.
	UFUNCTION(BlueprintCallable, Category = "WeedShop|Phone")
	void RequestDeposit(int64 CashAmount);

	// Boek bankgeld over naar een co-op vriend (fee + dag-limiet). Client -> server.
	UFUNCTION(BlueprintCallable, Category = "WeedShop|Phone")
	void RequestTransfer(int64 AmountCents);

	// === Telefoon-upgrade: ontgrendelt de Bank-app op de mobiel (i.p.v. alleen de ATM) ===
	// Kosten in cents (bankgeld, eenmalig).
	static constexpr int64 PhoneUpgradeCostCents = 250000; // EUR 2.500

	UFUNCTION(BlueprintPure, Category = "WeedShop|Phone")
	bool IsBankAppUnlocked() const { return bBankAppUnlocked; }

	// Server-only: zet de bank-app-unlock direct (voor save/load-herstel).
	void SetBankAppUnlocked(bool bUnlocked) { if (GetOwnerRole() == ROLE_Authority) { bBankAppUnlocked = bUnlocked; } }

	// Koop de telefoon-upgrade (bankgeld). Client -> server.
	UFUNCTION(BlueprintCallable, Category = "WeedShop|Phone")
	void RequestBuyPhoneUpgrade();

	// Vraag de host om een volledige save (neemt alle spelers mee). Werkt voor host én co-op client.
	UFUNCTION(BlueprintCallable, Category = "WeedShop|Phone")
	void RequestSaveGame();

	// Bezorgopties (fee = % van het besteed bedrag; tijd in seconden). Gedeeld door UI + server.
	static float DeliveryFeePct(int32 Opt);       // 0.01 / 0.08 / 0.25
	static float DeliveryDelaySeconds(int32 Opt); // 120 / 40 / 0
	static FString DeliveryName(int32 Opt);
	static FString DeliveryTimeText(int32 Opt);

	// --- Onderweg zijnde bestellingen (Packages-tab in de winkel) ---
	struct FPendingDelivery
	{
		int32 OrderId = 0;
		int32 DeliveryOpt = 0;
		int64 FeeCents = 0;
		int64 PaidCents = 0;      // betaald voor het koopdeel (itemprijs + fee) -> terug bij annuleren
		int32 ItemCount = 0;      // totaal aantal stuks
		FString Summary;          // korte omschrijving (bv. "3x Rolling papers, 2x ...")
		float PlacedTime = 0.f;   // wereldtijd bij plaatsen
		float ArriveTime = 0.f;   // wereldtijd van aankomst
		bool bArrived = false;    // drone heeft het pakket bij de deur laten vallen (wacht op oppakken)
		TWeakObjectPtr<class ADeliveryDrone> Drone;
		TArray<FName> Ids;
		TArray<int32> Qtys;
	};
	const TArray<FPendingDelivery>& GetPendingDeliveries() const { return PendingDeliveries; }
	int32 GetPendingCount() const { return PendingDeliveries.Num(); }
	// 0..1 voortgang van een bestelling (op aankomsttijd).
	float GetDeliveryProgress(const FPendingDelivery& D) const;
	// Resterende seconden tot aankomst.
	float GetDeliverySecondsLeft(const FPendingDelivery& D) const;
	// Annuleer een bestelling (server): drone weg, fee terug, regel weg. Kan alleen zolang 'ie nog vliegt.
	UFUNCTION(BlueprintCallable, Category = "WeedShop|Phone")
	void CancelDelivery(int32 OrderId);

	// Door de drone aangeroepen: het pakket is bij de deur afgeleverd (markeer als 'aangekomen').
	void NotifyDroneArrived(int32 OrderId);
	// Door het pakket aangeroepen wanneer het volledig is opgepakt: ruim de pending-regel op.
	void OnPackagePickedUp(int32 OrderId);
	// True als de bestelling al bij de deur ligt (drone gedropt, wacht op oppakken).
	bool IsDeliveryArrived(const FPendingDelivery& D) const { return D.bArrived; }
	// Droppunt voor leveringen (voordeur): getagde actor "DeliveryPoint", anders een CustomerSpawner.
	FVector FindDeliveryPoint() const;

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

	// Koop een pot-upgrade voor een specifieke pot (vanuit de Grow shop "Pot Upgrades"-tab).
	UFUNCTION(BlueprintCallable, Category = "WeedShop|Pot")
	void RequestPotUpgradeFor(AGrowPlant* Pot, int32 UpgIndex) { if (Pot) { ServerBuyPotUpgrade(Pot, UpgIndex); } }

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

	// "Load"-knop in het rol-menu: onthoud het gekozen aantal gram als geladen joint en sluit het menu.
	// Daarna rol je door rechtermuis in te houden (afgehandeld in de character).
	UFUNCTION(BlueprintCallable, Category = "WeedShop|Roll")
	void LoadRoll();

	UFUNCTION(BlueprintPure, Category = "WeedShop|Roll")
	bool IsRollOpen() const { return bRollOpen; }

	UFUNCTION(BlueprintPure, Category = "WeedShop|Roll")
	int32 GetRollGrams() const { return RollGrams; }

	// Maximaal gram per joint dat je huidige papers toelaten (basis 2; betere vloei verhoogt).
	UFUNCTION(BlueprintPure, Category = "WeedShop|Roll")
	int32 GetMaxJointGrams() const;

	// 0..1 verwachte "high" van een joint: schaalt met gram + THC% + kwaliteit% (zelfde formule als
	// het roken zelf). Gebruikt door de roll-UI als zinvolle sterkte-balk (beweegt mee met gram).
	static float JointIntensity(int32 Grams, float ThcPercent, float QualityPct);

	// THC% + kwaliteit% van de wiet-stapel die voor een joint van Grams gebruikt zou worden (de eerste
	// Bud_-stapel met genoeg voorraad). Geeft false als er geen bruikbare wiet is.
	bool GetRollWeedInfo(int32 Grams, float& OutThcPercent, float& OutQualityPct) const;
	// Idem maar ook met het item-id van de wiet (voor de naam).
	bool GetRollWeed(int32 Grams, FName& OutItemId, float& OutThcPercent, float& OutQualityPct) const;

	// Beschrijving van de geladen joint (lege string = niets geladen), bv. "2g Silver Haze - 70% Q".
	UFUNCTION(BlueprintPure, Category = "WeedShop|Roll")
	FString GetRollLoadDesc() const { return RollLoadDesc; }

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

	// Voortgang van het "joint overhandigen" (links-muis inhouden bij een klant met joint in de hand).
	void SetGiveHoldFrac(float Frac) { GiveHoldFrac = FMath::Clamp(Frac, 0.f, 1.f); }
	UFUNCTION(BlueprintPure, Category = "WeedShop|Deal")
	float GetGiveHoldFrac() const { return GiveHoldFrac; }

	// Voortgang van het rollen (rechtermuis inhouden met geladen vloei).
	void SetRollHoldFrac(float Frac) { RollHoldFrac = FMath::Clamp(Frac, 0.f, 1.f); }
	UFUNCTION(BlueprintPure, Category = "WeedShop|Roll")
	float GetRollHoldFrac() const { return RollHoldFrac; }

	// Of de vloei in de hand "geladen" is (door de character gezet) — voor de hotkey-hints.
	void SetRollLoadedUI(bool bLoaded, int32 Grams) { bRollLoadedUI = bLoaded; RollLoadGramsUI = Grams; if (!bLoaded) { RollLoadDesc.Reset(); } }
	UFUNCTION(BlueprintPure, Category = "WeedShop|Roll")
	bool IsRollLoadedUI() const { return bRollLoadedUI; }
	int32 GetRollLoadGramsUI() const { return RollLoadGramsUI; }

	// Stoned-info voor de HUD (door de character bijgewerkt): fractie resterend (0..1), resterende
	// seconden en hoe high je bent (0..1).
	void SetStonedHud(float Frac, float Seconds, float Intensity, float XpFrac) { StonedHudFrac = Frac; StonedHudSecs = Seconds; StonedHudIntensity = Intensity; StonedHudXpFrac = XpFrac; }
	float GetStonedHudFrac() const { return StonedHudFrac; }
	float GetStonedHudSecs() const { return StonedHudSecs; }
	float GetStonedHudIntensity() const { return StonedHudIntensity; }
	float GetStonedHudXpFrac() const { return StonedHudXpFrac; } // XP-bonus fractie (THC-gebaseerd)

	// --- Wiet-batches mergen (bevestig-popup) ---
	void OpenMerge(FName ItemId);

	UFUNCTION(BlueprintCallable, Category = "WeedShop|Inventory")
	void CloseMerge();

	UFUNCTION(BlueprintCallable, Category = "WeedShop|Inventory")
	void ConfirmMerge();

	// Merge alle stapels van dit item direct (voor de UMG-inventory; geen popup).
	UFUNCTION(BlueprintCallable, Category = "WeedShop|Inventory")
	void MergeNow(FName ItemId) { ServerMergeItem(ItemId); }

	UFUNCTION(BlueprintPure, Category = "WeedShop|Inventory")
	bool IsMergeOpen() const { return bMergeOpen; }

	UFUNCTION(BlueprintPure, Category = "WeedShop|Inventory")
	FName GetMergeItemId() const { return MergeItemId; }

	// Prijs-band: van 40% tot 200% van de marktprijs (in stappen van 10% voor de slider).
	static constexpr int32 DealStepCount = 17; // 40,50,...,200 %

protected:
	virtual void BeginPlay() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UFUNCTION(Server, Reliable)
	void ServerBuyUpgrade(FName UpgradeId);

	UFUNCTION(Server, Reliable)
	void ServerBuySeed(FName StrainId);

	UFUNCTION(Server, Reliable)
	void ServerBuySupply(FName SupplyId);

	UFUNCTION(Server, Reliable)
	void ServerRespond(bool bAccept);

	UFUNCTION(Server, Reliable)
	void ServerRespondContact(FName ContactId, bool bAccept);

	UFUNCTION(Server, Reliable)
	void ServerBuyPhoneUpgrade();

	UFUNCTION(Server, Reliable)
	void ServerRequestSave();

	// Ontgrendelt de Bank-app op de mobiel (per speler, na de telefoon-upgrade).
	UPROPERTY(Replicated)
	bool bBankAppUnlocked = false;

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

	// Server: reken de winkelwagen af — koop-regels (drone-levering) + verkoop-regels (meteen), met
	// een netto-afrekening (verkoop trekt van de kosten af) en bezorgkosten op het koopdeel.
	UFUNCTION(Server, Reliable)
	void ServerBuyCart(const TArray<FName>& BuyIds, const TArray<int32>& BuyQtys,
		const TArray<FName>& SellIds, const TArray<int32>& SellQtys, int32 DeliveryOption);

	// Server: levert de bestelling (voegt items toe / schrijft itemprijs af). Direct of na de levertijd.
	// OrderId>0 = ruim de bijbehorende pending-regel op na levering (0 = directe levering, geen regel).
	void DeliverCart(int32 OrderId, const TArray<FName>& ItemIds, const TArray<int32>& Quantities);

	// Server: annuleer-RPC.
	UFUNCTION(Server, Reliable)
	void ServerCancelDelivery(int32 OrderId);

	UFUNCTION(Server, Reliable)
	void ServerDeposit(int64 CashAmount);

	UFUNCTION(Server, Reliable)
	void ServerTransfer(int64 AmountCents);

	// Server: voeg alle stapels van dit item-id samen (gewogen gemiddelde THC%/Kwaliteit%).
	UFUNCTION(Server, Reliable)
	void ServerMergeItem(FName ItemId);

	// Server: koop pot-upgrade UpgIndex voor de gegeven pot (kosten van de kas).
	UFUNCTION(Server, Reliable)
	void ServerBuyPotUpgrade(AGrowPlant* Pot, int32 UpgIndex);

	AWeedShopGameState* GetGS() const;
	APlayerController* GetPC() const;
	UInventoryComponent* GetOwnerInventory() const;
	class UEconomyComponent* GetOwnerEconomy() const;

	// Zet muis-cursor/input-mode op basis van of er een UI open is.
	void UpdateCursor();

	UPROPERTY(Transient)
	TObjectPtr<class UPhoneWidget> PhoneWidget;

	UPROPERTY(Transient)
	TObjectPtr<class UStatusHudWidget> StatusWidget;

	UPROPERTY(Transient)
	TObjectPtr<class UDealWidget> DealWidget;

	UPROPERTY(Transient)
	TObjectPtr<class UPlantInfoWidget> PlantWidget;

	UPROPERTY(Transient)
	TObjectPtr<class UHotbarWidget> HotbarWidget;

	UPROPERTY(Transient)
	TObjectPtr<class UInventoryWidget> InventoryWidget;

	UPROPERTY(Transient)
	TObjectPtr<class URollWidget> RollWidget;

	UPROPERTY(Transient)
	TObjectPtr<class UCompassWidget> CompassWidget;

	UPROPERTY(Transient)
	TObjectPtr<class UHotkeyHintWidget> HotkeyWidget;

	UPROPERTY(Transient)
	TObjectPtr<class UAtmWidget> AtmWidget;

	UPROPERTY(Transient)
	TObjectPtr<class UPackWidget> PackWidget;

	UPROPERTY(Transient)
	TObjectPtr<class UShelfWidget> ShelfWidget;

	UPROPERTY(Transient)
	TObjectPtr<class UDryingRackWidget> DryRackWidget;

	UPROPERTY(Transient)
	TObjectPtr<class UHandInfoWidget> HandInfoWidget;

	UPROPERTY(Transient)
	TObjectPtr<class ULevelUpWidget> LevelUpWidget;

	UPROPERTY(Transient)
	TObjectPtr<class UWeedToast> ToastWidget;

	UPROPERTY(Transient)
	TObjectPtr<class UPauseMenuWidget> PauseWidget;

	UPROPERTY(Transient)
	TObjectPtr<class UMainMenuWidget> MainMenuWidget;

	UPROPERTY(Transient)
	TObjectPtr<class USaveIndicatorWidget> SaveIndicatorWidget;

	UPROPERTY(Transient)
	TObjectPtr<class USettingsWidget> SettingsWidget;

	bool bOpen = false;
	int32 Tab = 0;
	bool bHomeScreen = true; // toon het app-rooster i.p.v. een geopende app
	int32 SupplierCat = 0;

	bool bRollOpen = false;
	int32 RollGrams = 2;

	float SmokeHoldFrac = 0.f; // 0..1 voortgang van rook-inhouden (lokale UI-staat)
	float GiveHoldFrac = 0.f;  // 0..1 voortgang van joint-overhandigen
	float RollHoldFrac = 0.f;  // 0..1 voortgang van joint-rollen
	bool bRollLoadedUI = false;
	int32 RollLoadGramsUI = 0;
	FString RollLoadDesc; // omschrijving van de geladen wiet (voor de hint)
	float StonedHudFrac = 0.f;      // 0..1 resterende high voor de HUD
	float StonedHudSecs = 0.f;      // resterende high-seconden
	float StonedHudIntensity = 0.f; // hoe high (0..1)
	float StonedHudXpFrac = 0.f;    // XP-bonus fractie (op THC% gebaseerd)

	bool bDealOpen = false;
	TWeakObjectPtr<ACustomerBase> DealCustomer;
	int32 DealAskCents = 0;
	FName DealAltProduct = NAME_None; // andere strain die je aanbiedt (None = het gevraagde product)

	// Tijdstip (wereldseconden) waarop de UI voor het laatst een klik verwerkte.
	double LastUiClickTime = -100.0;

	bool bInventoryOpen = false;
	bool bAtmOpen = false;
	bool bPackOpen = false;
	int32 PackBatchUI = 1; // zakjes per verpak-actie (van de bench-tier)

	bool bShelfOpen = false;
	TWeakObjectPtr<class AStorageShelf> ShelfActor; // het schap dat nu open is
	bool bDryRackOpen = false;
	TWeakObjectPtr<class ADryingRack> DryRackActor; // het droogrek dat nu open is

	bool bPauseOpen = false;
	bool bMainMenuOpen = false;
	bool bSettingsOpen = false;
	float FovValue = 90.f;
	float LookSensitivity = 1.f;

	bool bMergeOpen = false;
	FName MergeItemId = NAME_None;

	// Winkel-state (client-side): gekozen aantal per item + winkelwagen. bSell = verkoop-regel.
	struct FCartLine { FName ItemId = NAME_None; int32 Qty = 0; bool bSell = false; };
	TArray<FCartLine> Cart;

	// Onderweg zijnde bestellingen + hun timers (server-side).
	TArray<FPendingDelivery> PendingDeliveries;
	TMap<int32, FTimerHandle> DeliveryTimers;
	int32 NextOrderId = 1;
	TMap<FName, int32> PendingQty;
	TMap<FName, int32> PendingSellQty;

	bool bPotUpgradeOpen = false;
	TWeakObjectPtr<AGrowPlant> UpgPot;
};
