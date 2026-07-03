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
class ADeliveryDrone;

// --- Onderweg zijnde bestelling (Packages-app) ---
// USTRUCT met UPROPERTY-velden zodat de lijst PER SPELER repliceert (COND_OwnerOnly) -> ook de JOINER
// ziet z'n eigen ETA/annuleren/historie (was voorheen server-only -> joiner-app was leeg). De Drone-
// pointer blijft SERVER-ONLY (geen UPROPERTY -> repliceert niet); de UI gebruikt bArrived voor aankomst.
// Historische veldnamen behouden -> PhoneWidget/SaveGameSubsystem (andere agents) blijven compileren.
USTRUCT()
struct FPhonePendingDelivery
{
	GENERATED_BODY()
	UPROPERTY() int32 OrderId = 0;
	UPROPERTY() int32 DeliveryOpt = 0;
	UPROPERTY() int64 FeeCents = 0;
	UPROPERTY() int64 PaidCents = 0;      // betaald voor het koopdeel (itemprijs + fee) -> terug bij annuleren
	UPROPERTY() int32 ItemCount = 0;      // totaal aantal stuks
	UPROPERTY() FString Summary;          // korte omschrijving (bv. "3x Rolling papers, 2x ...")
	UPROPERTY() float PlacedTime = 0.f;   // wereldtijd bij plaatsen
	UPROPERTY() float ArriveTime = 0.f;   // wereldtijd van aankomst
	UPROPERTY() bool bArrived = false;    // drone heeft het pakket bij de deur laten vallen (wacht op oppakken)
	UPROPERTY() TArray<FName> Ids;
	UPROPERTY() TArray<int32> Qtys;
	// SERVER-ONLY (geen UPROPERTY): de levende drone-actor. Niet repliceren (client stuurt 'm nooit aan).
	TWeakObjectPtr<class ADeliveryDrone> Drone;
};

// --- Geleverd-historie (opgehaalde bestellingen; backing voor de Packages-historie-UI) ---
USTRUCT()
struct FPhoneDeliveredRecord
{
	GENERATED_BODY()
	UPROPERTY() int32 OrderId = 0;
	UPROPERTY() TArray<FName> Ids;
	UPROPERTY() TArray<int32> Qtys;
	UPROPERTY() int64 PaidCents = 0;
	UPROPERTY() int64 FeeCents = 0;
};

UCLASS(ClassGroup = (WeedShop), meta = (BlueprintSpawnableComponent))
class WEEDSHOPCORE_API UPhoneClientComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UPhoneClientComponent();

	// Open/sluit de telefoon (zet ook muis-cursor + input-mode op de lokale controller).
	UFUNCTION(BlueprintCallable, Category = "WeedShop|Phone")
	void Toggle();

	// Open/sluit het dev-menu (F10, alleen met dev-tools AAN) - één sidebar met alle dev-tools.
	// Ctrl+Shift+F10 (chord) zet de dev-tools sessie-breed aan/uit (werkt ook in Shipping, waar de
	// console/Exec niet bestaat). Dev-tools staan LOS van free-build (dat is een gameplay-vlag).
	UFUNCTION(BlueprintCallable, Category = "WeedShop|Phone")
	void ToggleDevMenu();
	bool IsDevMenuOpen() const { return bDevMenuOpen; }

	// Dev-tools sessie-vlag aan/uit op de server (gerepliceerd via de GameState). Aangeroepen door de
	// Ctrl+Shift+F10-chord en het `WeedDev`-Exec-commando op de character.
	UFUNCTION(Server, Reliable)
	void ServerSetDevTools(bool bOn);

	// --- Cheats (F10-dev-menu "Cheats"-categorie; server-authoritative, alleen met dev-tools AAN) ---
	// Geld: cash erbij op de eigen pawn-economy (Cents > 0).
	UFUNCTION(Server, Reliable) void ServerDevGiveCash(int64 Cents);
	// Sandbox-geld: zet cash EN bank op EUR 1.000.000 (zelfde bedragen als de Sandbox-startmode).
	UFUNCTION(Server, Reliable) void ServerDevSandboxMoney();
	// Level direct zetten; >= 50 zet OOK de shop-licentie (GrantLevel zelf doet dat niet - oude quirk).
	UFUNCTION(Server, Reliable) void ServerDevSetLevel(int32 NewLevel);
	// Alle NPC's warm (goede stats + ontgrendeld) + eerste 10 als telefoon-contact.
	UFUNCTION(Server, Reliable) void ServerDevWarmNpcs();
	// Free-build (gameplay-vlag: overal bouwen) los toggelen vanuit het dev-menu.
	UFUNCTION(Server, Reliable) void ServerDevSetFreeBuild(bool bOn);
	// Starter-kits (item-lijsten van de Testing/Sandbox-startmodes). LET OP: sandbox VERVANGT je inventory.
	UFUNCTION(Server, Reliable) void ServerDevGiveStarterKit(bool bSandbox);

	// Maakt de UMG-widgets (status/telefoon/deal) aan op de lokale client (lui, idempotent).
	void EnsureWidget();

	// Toon een toast-melding bij DEZE speler (de eigenaar). Werkt vanuit server-code: routeert via een
	// Client-RPC naar de juiste client, zodat co-op-spelers hun eigen meldingen zien (niet alleen de host).
	void Toast(const FString& Msg, FColor Color = FColor::White, float Time = 2.5f);
	UFUNCTION(Client, Reliable) void ClientToast(const FString& Msg, FColor Color, float Time);

	// Fullscreen stadskaart aan/uit (M-toets of de Fullscreen-knop in de Map-app).
	void ToggleMapOverlay();
	bool IsMapOpen() const { return MapOverlay != nullptr; }
	void CloseMapOverlay();

	// --- Waypoint (gezet op de kaart; getoond op kaart + kompas) ---
	void SetWaypoint(const FVector& World);
	void ClearWaypoint();
	bool HasWaypoint() const { return bHasWaypoint; }
	FVector GetWaypoint() const { return WaypointWorld; }

	UFUNCTION(BlueprintCallable, Category = "WeedShop|Phone")
	void SetTab(int32 NewTab);

	UFUNCTION(BlueprintCallable, Category = "WeedShop|Phone")
	void CycleTab();

	// --- iPhone-achtige home/apps ---
	static constexpr int32 AppCount = 13; // 0 Upgrades 1 Grow 2 Contacts 3 Messages 4 Settings 5 Map(dood) 6 Sell 7 Supplies 8 Packages 9 Bank 10 Lab(dood) 11 Goals 12 Leaderboard — moet gelijk zijn aan GNumApps in PhoneWidget (12 was te laag: OpenApp(12) clampte naar 11 -> Leaderboard-tegel opende Goals)

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

	// Hoofdmenu LIVE-achtergrond: zet de view op een opgeslagen camera-plek (Saved/MenuCam.txt) voor deze map,
	// zonder te pauzeren -> bewegende bomen/zonsondergang achter het menu. Geeft true als er een cam voor deze
	// map is. ClearMenuCam herstelt de view naar de speler.
	bool ApplyMenuCam();
	void ClearMenuCam();

	// Toon het titelscherm en open meteen de Load-slot-picker (vanuit het pauze-menu).
	void OpenMainMenuLoad();

	// --- Settings-scherm (graphics + game). ---
	UFUNCTION(BlueprintCallable, Category = "WeedShop|Settings")
	void OpenSettings();
	UFUNCTION(BlueprintCallable, Category = "WeedShop|Settings")
	void CloseSettings();
	UFUNCTION(BlueprintPure, Category = "WeedShop|Settings")
	bool IsSettingsOpen() const { return bSettingsOpen; }

	// Telefoon-open status (gerepliceerd): andere spelers spelen hierop een 'texting'-animatie af.
	bool IsPhoneOpenReplicated() const { return bPhoneOpenRep; }
	UFUNCTION(Server, Reliable)
	void ServerSetPhoneOpen(bool bInOpen);

	// --- Lichtschakelaar-dimmer (popup met verticale slider; zie APackLightSwitch). ---
	void OpenLightDimmer(class APackLightSwitch* Sw);
	void CloseLightDimmer();
	bool IsLightDimmerOpen() const { return bLightDimmerOpen; }
	class APackLightSwitch* GetDimmerSwitch() const;

	// --- Lamp-link-modus: klik lampen om ze aan de gekozen schakelaar te koppelen (groen/wit markers). ---
	void EnterLinkMode(); // start vanuit de dimmer-popup; sluit de dimmer + spawnt de klikbare lamp-markers
	void ExitLinkMode();  // ruimt de markers op (Esc / weglopen); links zijn al per-klik opgeslagen
	bool IsLinkModeActive() const { return bLinkModeActive; }
	void NotifyLinkActivity(); // reset de 1-min inactiviteits-timer (aangeroepen door een marker-klik)

	// Game-instellingen (lokaal toegepast + bewaard in config).
	void ApplyFov(float NewFov);
	void SetLookSensitivity(float S);
	void SetHeadBob(bool bOn);
	UFUNCTION(BlueprintPure, Category = "WeedShop|Settings")
	float GetFov() const { return FovValue; }
	UFUNCTION(BlueprintPure, Category = "WeedShop|Settings")
	float GetLookSensitivity() const { return LookSensitivity; }
	UFUNCTION(BlueprintPure, Category = "WeedShop|Settings")
	bool GetHeadBob() const { return bHeadBob; }
	void LoadGameSettings(); // leest FOV/sensitivity uit config en past FOV toe

	// --- ATM (in de wereld): open/sluit het ATM-scherm (bankieren + storten + overboeken) ---
	UFUNCTION(BlueprintCallable, Category = "WeedShop|ATM")
	void OpenAtm();
	UFUNCTION(BlueprintCallable, Category = "WeedShop|ATM")
	void CloseAtm();
	UFUNCTION(BlueprintPure, Category = "WeedShop|ATM")
	bool IsAtmOpen() const { return bAtmOpen; }
	bool IsWardrobeOpen() const { return bWardrobeOpen; }
	void ToggleSpotInfo();
	bool IsSpotInfoVisible() const; // F9 dev-overlay: positie + waar je naar kijkt (mesh-id)
	void OpenWardrobe();
	void CloseWardrobe();

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

	// UITPAKKEN: haal de wiet weer LOS uit een Bag_<strain> (terug naar Bud_<strain>), zodat je 'm kan
	// herverpakken of rollen. Het zakje/de container raak je kwijt (verbruikt).
	UFUNCTION(BlueprintCallable, Category = "WeedShop|Pack")
	void RequestUnpack(FName BagId, int32 Count);
	UFUNCTION(Server, Reliable)
	void ServerUnpack(FName BagId, int32 Count);

	// --- Opslag-schap (in de wereld): stacks tussen je inventory en het schap verplaatsen ---
	void OpenShelf(class AStorageShelf* Shelf);
	UFUNCTION(BlueprintCallable, Category = "WeedShop|Shelf")
	void CloseShelf();
	UFUNCTION(BlueprintPure, Category = "WeedShop|Shelf")
	bool IsShelfOpen() const { return bShelfOpen; }
	class AStorageShelf* GetShelf() const;

	void RequestShelfStore(FName ItemId, int32 Count);
	void RequestShelfTake(int32 SlotIndex, int32 Count);
	void RequestShelfCook(int32 SlotIndex); // koelkast: zet de ButterMix op schap-slot SlotIndex om in edibles
	UFUNCTION(Server, Reliable)
	void ServerShelfStore(class AStorageShelf* Shelf, FName ItemId, int32 Count);
	UFUNCTION(Server, Reliable)
	void ServerShelfTake(class AStorageShelf* Shelf, int32 SlotIndex, int32 Count, FName ExpectedId);
	UFUNCTION(Server, Reliable)
	void ServerShelfCook(class AStorageShelf* Shelf, int32 SlotIndex, FName ExpectedId);

	// --- Goals/milestones: een behaald doel claimen (reward naar deze speler) ---
	void ClaimGoal(int32 Idx);
	UFUNCTION(Server, Reliable)
	void ServerClaimGoal(int32 Idx);

	// --- Fysieke winkel (balie): fullscreen winkel-menu, betaal cash/bank, instant, geen shipping ---
	void OpenStore(class AStoreCounter* Counter);
	UFUNCTION(BlueprintCallable, Category = "WeedShop|Store")
	void CloseStore();
	UFUNCTION(BlueprintPure, Category = "WeedShop|Store")
	bool IsStoreOpen() const { return bStoreOpen; }
	class AStoreCounter* GetStoreCounter() const { return StoreCounterRef.Get(); }
	void SetStorePayBank(bool b) { bStorePayBank = b; }
	bool IsStorePayBank() const { return bStorePayBank; }
	void StoreBuy(FName ItemId) { ServerStoreBuy(ItemId, bStorePayBank); }
	UFUNCTION(Server, Reliable)
	void ServerStoreBuy(FName ItemId, bool bBank);
	// Checkout van een hele winkelmand in één keer (cash/bank, instant, geen bezorgkosten).
	void StoreCheckout(const TArray<FName>& Ids, const TArray<int32>& Qtys) { ServerStoreCheckout(Ids, Qtys, bStorePayBank); }
	UFUNCTION(Server, Reliable)
	void ServerStoreCheckout(const TArray<FName>& Ids, const TArray<int32>& Qtys, bool bBank);

	// --- Droogrek (in de wereld): natte wiet ophangen + gedroogde batches oogsten ---
	void OpenDryRack(class ADryingRack* Rack);
	UFUNCTION(BlueprintCallable, Category = "WeedShop|Dry")
	void CloseDryRack();
	UFUNCTION(BlueprintPure, Category = "WeedShop|Dry")
	bool IsDryRackOpen() const { return bDryRackOpen; }
	class ADryingRack* GetDryRack() const;

	void RequestDryHang(int32 StackId); // hang precies DEZE stapel op (eigen THC%/kwaliteit)
	void RequestDryCollect(int32 Index);
	void RequestDryCollectAll();
	UFUNCTION(Server, Reliable)
	void ServerDryHang(class ADryingRack* Rack, int32 StackId);
	// CO-OP anti-race: ExpectedId = de batch-id die de client op deze index zag (zelfde patroon als
	// ServerShelfTake), zodat de server een door RemoveAt verschoven index weigert.
	UFUNCTION(Server, Reliable)
	void ServerDryCollect(class ADryingRack* Rack, int32 Index, FName ExpectedId);
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

	// Stel je eigen kloktijd voor (minuten van de dag, 0..1439). Het contact gaat altijd akkoord.
	UFUNCTION(BlueprintCallable, Category = "WeedShop|Phone")
	void ProposeChatTime(FName ContactId, int32 MinutesOfDay) { ServerProposeContactTime(ContactId, MinutesOfDay); }
	UFUNCTION(Server, Reliable)
	void ServerProposeContactTime(FName ContactId, int32 MinutesOfDay);

	// Bied een ANDERE strain aan dan gevraagd (substituut) via de chat. Kans op akkoord (loyaliteit/THC).
	UFUNCTION(BlueprintCallable, Category = "WeedShop|Phone")
	void ProposeChatStrain(FName ContactId, FName Strain) { ServerProposeContactStrain(ContactId, Strain); }
	UFUNCTION(Server, Reliable)
	void ServerProposeContactStrain(FName ContactId, FName Strain);

	// Berichten-notificatie. De bubble op het telefoon-icoon telt ongelezen inkomende berichten en gaat pas
	// weg als je de CHAT van die persoon echt opent (MarkChatSeen -> server zet bSeen, gerepliceerd naar BEIDE
	// co-op-spelers zodat de badge bij allebei tegelijk verdwijnt).
	void MarkChatSeen(FName ContactId);
	UFUNCTION(Server, Reliable) void ServerMarkThreadSeen(FName ContactId);
	int32 GetUnreadMessageCount() const;
	int32 GetUnreadCountFrom(FName ContactId) const; // aantal ongelezen berichten van dit contact (teller-badge)
	bool HasUnreadFrom(FName ContactId) const; // ongelezen inkomende berichten van dit contact?

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

	// Kluis: verplaats cash <-> kluis. bToSafe=true: cash -> kluis; false: kluis -> cash. Cents<=0 = maximaal.
	UFUNCTION(BlueprintCallable, Category = "WeedShop|Phone")
	void RequestSafeMove(int64 Cents, bool bToSafe);

	// Dev/test: forceer direct een overval (bBust=false) of bust (bBust=true). Client -> server.
	void RequestDevHeatEvent(bool bBust);

	// Test-tool: zet de tijd op midden-dag (false) of midden-nacht (true). Client -> server.
	void RequestSetDayNight(bool bNight);

	// Test-tool: geef de complete building-kit (alle Struct_-items, oneindig bruikbaar). Client -> server.
	void RequestGiveBuildKit();
	// Meubel-kit: alle woon-meubels in je inventory zodat je de kamer kunt inrichten. Client -> server.
	void RequestGiveFurnitureKit();
	// Huidige meubels rond je heen opslaan als starter-layout (StarterFurniture.txt) + clear.
	void SaveStarterFurniture();
	void ClearStarterFurniture();

	// Test-tool: sla de huidige 3 markers op als permanente kamer-job (RoomJobs.txt) en maak de
	// markers vrij voor het volgende gebouw. Puur lokaal bestand-werk.
	void SaveRoomJob();

	// Room-stamper: kamer binnen 2 markers opslaan als template / stempel-modus starten.
	void SaveRoomTemplateNow();
	void StartRoomStamp(const FString& TemplateName);
	// Map-grens: huidige markers (in volgorde) worden een blokkerende glazen wand (Saved/MapBorder.txt).
	void SaveMapBorder();
	// NPC-looproute: F9-markers (op volgorde over de stoep) -> spawn-/wandel-punten voor NPC's.
	void SaveNpcRoute();
	void ClearNpcRoute();
	// No-build-zones: zet de huidige F9-markers (2 hoeken = box) vast in EIGEN bestand Saved/NoBuildZones.txt
	// (dat geen ander dev-tool wist) + leeg MarkedSpots. Daar mag je dan niks plaatsen (incl. wall-mounts).
	void SaveNoBuildZone();
	void ClearNoBuildZone();
	// Binnen-looppad (trappenhuis): F9-markers op volgorde -> smart-link ketting voor NPC's.
	void SaveStairsPath();
	void ClearStairsPath();
	// Alle gezette paden visualiseren: loop-ringen (groen) + gebouw-kettingen (oranje).
	void ShowAllPaths();
	void HideAllPaths();
	// Kijk naar een pad-bolletje en verwijder dat hele pad (route of gebouw-ketting).
	void DeletePathInCrosshair();
	// Thuis-spawn: zet 1 F9-marker op de plek in je kamer waar je elke sessie wil beginnen.
	void SaveHomeSpawn();
	// Chill-plekken: F9-markers -> plekken waar NPC's heen lopen en blijven hangen tot de dag om is.
	void SaveChillSpots();
	void ClearChillSpots();
	// Winkel-plekken: F9-markers -> op elk een werkende winkel (toonbank + ATM + verkoper).
	void SaveShopSpots();
	void ClearShopSpots();
	// Welke soort de eerstvolgende opgeslagen winkel(s) worden (0=Grow 1=Supplies 2=Furniture).
	int32 SelectedShopKind = 0;
	int32 GetSelectedShopKind() const { return SelectedShopKind; }
	void CycleSelectedShopKind() { SelectedShopKind = (SelectedShopKind + 1) % 3; }
	// Kijk naar een winkel-toonbank en wissel de soort ter plekke (live + opgeslagen).
	void SetShopTypeInCrosshair();
	void ClearMapBorder();
	UFUNCTION(Server, Reliable)
	void ServerGiveBuildKit();
	UFUNCTION(Server, Reliable)
	void ServerGiveFurnitureKit();

	// Boek bankgeld over naar een co-op vriend (fee + dag-limiet). Client -> server.
	UFUNCTION(BlueprintCallable, Category = "WeedShop|Phone")
	void RequestTransfer(int64 AmountCents);

	// === Telefoon-upgrade: ontgrendelt de Bank-app op de mobiel (i.p.v. alleen de ATM) ===
	// Kosten in cents (bankgeld, eenmalig).
	static constexpr int64 PhoneUpgradeCostCents = 250000; // EUR 2.500

	UFUNCTION(BlueprintPure, Category = "WeedShop|Phone")
	bool IsBankAppUnlocked() const { return bBankAppUnlocked; }

	// True wanneer de Bank-app via een fysieke ATM open staat (dan geen telefoon-upgrade nodig).
	bool IsBankViaAtm() const { return bBankViaAtm; }

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
	// De structs staan nu op file-scope (USTRUCT, zodat de lijsten per-eigenaar repliceren). De aliassen
	// houden de bestaande namen UPhoneClientComponent::FPendingDelivery/FDeliveredRecord intact zodat de
	// consumenten (PhoneWidget, SaveGameSubsystem) ongewijzigd blijven compileren.
	using FPendingDelivery = FPhonePendingDelivery;
	using FDeliveredRecord = FPhoneDeliveredRecord;
	const TArray<FPendingDelivery>& GetPendingDeliveries() const { return PendingDeliveries; }
	const TArray<FDeliveredRecord>& GetDeliveredHistory() const { return DeliveredHistory; }
	// Save/load: een opgeslagen (nog onderweg) bestelling meteen leveren (speler had al betaald).
	void RestoreDeliverInstant(const TArray<FName>& Ids, const TArray<int32>& Qtys) { DeliverCart(0, Ids, Qtys); }
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

	// --- Woningen (3 koopbare panden in de Upgrades-app) ---
	// De DoorRetrofitter host de woning-registry op de beach-map (ROADMAP 4.1).
	class ADoorRetrofitter* FindRetro() const;
private:
	// Cache voor FindRetro: TActorIterator-scan alleen als de cache invalid is (patroon UMapWidget).
	// Weak -> level-wissel/destroy maakt 'm vanzelf ongeldig; mutable want FindRetro is const.
	mutable TWeakObjectPtr<class ADoorRetrofitter> RetroCache;
public:
	// Woningen/aanbiedingen uit de beach-registry (DoorRetrofitter) - call-sites hoeven niet
	// te weten welke bron actief is.
	void GetHomesUnified(TArray<struct FApartmentHome>& Out) const;
	void GetOffersUnified(TArray<struct FCityPropertyOffer>& Out) const;
	void GetPropertyOffers(TArray<struct FCityPropertyOffer>& Out) const;
	bool IsPropertyOwned(int32 HomeIndex) const { return OwnedHomes.Contains(HomeIndex); }
	bool IsActiveHome(int32 HomeIndex) const { return ActiveHome == HomeIndex; }
	void BuyProperty(int32 HomeIndex) { ServerBuyProperty(HomeIndex); }
	void SetActiveHome(int32 HomeIndex) { ServerSetActiveHome(HomeIndex); }
	// Zet je woon-plek naar de GEKOCHTE woning waarin deze locatie (bv. een bed) valt — zónder teleport.
	// Server-side; aangeroepen als je in een bed slaapt zodat "ik woon hier" automatisch meeschuift.
	void SetActiveHomeFromLocation(const FVector& WorldLoc);
	// Verkoop een gekocht pand terug (~65% van de koopprijs). Starter-woning is niet verkoopbaar.
	void SellProperty(int32 HomeIndex) { ServerSellProperty(HomeIndex); }
	int32 GetHomeSellValueCents(int32 HomeIndex) const; // 0 = niet verkoopbaar (starter/onbekend)
	// Save/load van eigendom.
	const TArray<int32>& GetOwnedHomes() const { return OwnedHomes; }
	int32 GetActiveHome() const { return ActiveHome; }
	// Wereldlocatie van je huidige woning (voordeur) — voor de compass-home-marker. False = geen woning.
	bool GetActiveHomeLocation(FVector& OutWorld) const;

	// Max kluis-capaciteit (cents) over alle geplaatste safes — bepaalt hoeveel cash je veilig kunt stashen.
	int64 GetSafeCapCents() const;
	void RestoreProperty(const TArray<int32>& InOwned, int32 InActive);

	// --- Bezorg-doel: welk eigen huis krijgt de levering ---
	// Welke eigen woning bevat de speler NU (binnen de kamer-bounds)? -1 = geen.
	int32 GetHomePlayerIsInside() const;
	// Het te gebruiken bezorg-huis: handmatige keuze > huis waar je nu binnen bent > actieve woning.
	int32 ResolveDeliveryHome() const;
	// UI: kies expliciet een bezorg-huis (-1 = automatisch).
	void SetDeliveryHome(int32 HomeIndex) { SelectedDeliveryHome = HomeIndex; }
	int32 GetSelectedDeliveryHome() const { return SelectedDeliveryHome; }
	// Korte naam van een woning (huisnummer) voor de UI.
	FString GetHomeLabel(int32 HomeIndex) const;
	// Info-regel: type (flat/rijtjeshuis), huisnummer en verdieping. Voor het woning-scherm.
	FString GetHomeInfoLine(int32 HomeIndex) const;
	// Wordt periodiek aangeroepen (door de pawn-tick): starter toekennen + eigen deuren ontgrendelen.
	void PropertyTick();

	// --- Huur (per 30 dagen, van de bank; bank mag in de min = schuld -> heat) ---
	// Totale huur per termijn = som over je appartementen (basis + waarde-schaal).
	UFUNCTION(BlueprintPure, Category = "WeedShop|Rent")
	int32 GetRentDueCents() const;
	UFUNCTION(BlueprintPure, Category = "WeedShop|Rent")
	int32 GetRentDueDay() const { return RentDueDay; }
	// Save/load van de huur-staat.
	bool WasRentIntroShown() const { return bShownRentIntro; }
	void RestoreRent(int32 DueDay, bool bIntroShown) { RentDueDay = FMath::Max(1, DueDay); bShownRentIntro = bIntroShown; }
	// Server: door de dag-cyclus aangeroepen bij een nieuwe dag -> int de huur + intro-melding.
	void ProcessRentForDay(int32 Day);

	// --- Inventory-scherm (drag-n-drop naar hotbar) ---
	UFUNCTION(BlueprintCallable, Category = "WeedShop|Inventory")
	void ToggleInventory();

	UFUNCTION(BlueprintPure, Category = "WeedShop|Inventory")
	bool IsInventoryOpen() const { return bInventoryOpen; }
	// Voor de hotbar: zo kan een hotbar-slot bij hover het inventory-details-paneel vullen (zelfde als grid-cellen).
	class UInventoryWidget* GetInventoryWidget() const { return InventoryWidget; }

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

	// Capaciteit (gram per joint) van een SPECIFIEK papers-item; 0 als het geen papers is.
	// Zelfde tabel als GetMaxJointGrams - voor de item-quick-view ("Rolls joints up to 5g").
	static int32 PaperCapacity(FName PaperId);

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

	// Gekozen strain om te rollen (client-side keuze in de roll-UI). NAME_None = pak de eerste bruikbare
	// Bud_-stapel (oud gedrag). GetRollWeed/ServerRollJoint proberen deze strain eerst en vallen anders
	// rock-solid terug op de eerste match zodat rollen nooit breekt.
	void SetRollStrain(FName S) { RollStrain = S; }
	FName GetRollStrain() const { return RollStrain; }

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

	// Geef de huidige praat-klant een joint (knop in het praat-venster). Routeert naar de speler-pawn.
	void RequestGiveJoint(ACustomerBase* Customer);

	// Geef de praat-klant een SPECIFIEKE joint (gekozen in de deal-kiezer). Routeert naar de speler-pawn.
	void RequestGiveJointId(ACustomerBase* Customer, FName JointId);

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
	void SetDropHoldFrac(float Frac) { DropHoldFrac = FMath::Clamp(Frac, 0.f, 1.f); }
	float GetDropHoldFrac() const { return DropHoldFrac; }

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
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override; // link-modus markers opruimen
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

	// --- Woning-eigendom (per speler, gerepliceerd) ---
	UPROPERTY(ReplicatedUsing = OnRep_Property)
	TArray<int32> OwnedHomes;            // indices in de woning-registry (DoorRetrofitter beach-homes)
	UPROPERTY(ReplicatedUsing = OnRep_Property)
	int32 ActiveHome = -1;               // huidige woon-/spawn-plek
	int32 SelectedDeliveryHome = -1;     // UI-keuze bezorg-huis (-1 = automatisch: huidige/binnen-huis)
	bool bPropertyInit = false;          // server: starter al toegekend?

	UPROPERTY(Replicated)
	int32 RentDueDay = 31;               // dag waarop de volgende huur van de bank gaat (eerste = dag 31)
	bool bShownRentIntro = false;        // server: de "betaal je eerste huur"-melding al getoond?
	FTimerHandle PropertyTimer;

	UFUNCTION() void OnRep_Property();
	void ApplyLocalDoors();              // ontgrendel lokaal mijn eigen deuren ("Your home")
	void SpawnLightSwitches();           // plaats lichtschakelaars in je eigen woning (1x per woning)
	UFUNCTION(Server, Reliable) void ServerBuyProperty(int32 HomeIndex);
	UFUNCTION(Server, Reliable) void ServerSetActiveHome(int32 HomeIndex);
	UFUNCTION(Server, Reliable) void ServerSellProperty(int32 HomeIndex);
	void MoveOwnerToHome(int32 HomeIndex); // server: zet de speler in de woning
	// Client-RPC: laat de eigenaar-client zichzelf METEEN op de woning-plek zetten (anders komt de
	// server-teleport pas aan na een beweging-update -> "moet springen om in de woning te komen").
	UFUNCTION(Client, Reliable) void ClientLandAtHome(FVector To);

	// Server: maak 1 joint van Grams gram bud (item-id Joint_<Strain>_<G>g; meer gram = betere kwaliteit).
	// Strain = gekozen bud-stapel; None of niet-in-voorraad valt terug op de eerste bruikbare Bud_-stapel.
	UFUNCTION(Server, Reliable)
	void ServerRollJoint(int32 Grams, FName Strain);

	// Server: dien het bod in bij de klant op een specifiek product (betaalt naar de kas).
	UFUNCTION(Server, Reliable)
	void ServerSubmitOffer(ACustomerBase* Customer, FName ProductId, int32 AskCents);

	// Server: zet de "in gesprek"-vlag op een klant (stopt z'n lopen zolang je 'm aanspreekt).
	UFUNCTION(Server, Reliable)
	void ServerSetCustomerTalking(ACustomerBase* Customer, bool bTalking);

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
		const TArray<FName>& SellIds, const TArray<int32>& SellQtys, int32 DeliveryOption, int32 DeliveryHome);

	// Server: levert de bestelling (voegt items toe / schrijft itemprijs af). Direct of na de levertijd.
	// OrderId>0 = ruim de bijbehorende pending-regel op na levering (0 = directe levering, geen regel).
	void DeliverCart(int32 OrderId, const TArray<FName>& ItemIds, const TArray<int32>& Quantities);

	// Server: annuleer-RPC.
	UFUNCTION(Server, Reliable)
	void ServerCancelDelivery(int32 OrderId);

	UFUNCTION(Server, Reliable)
	void ServerDeposit(int64 CashAmount);

	UFUNCTION(Server, Reliable)
	void ServerSafeMove(int64 Cents, bool bToSafe);

	UFUNCTION(Server, Reliable)
	void ServerDevHeatEvent(bool bBust);

	UFUNCTION(Server, Reliable)
	void ServerSetDayNight(bool bNight);

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
	TObjectPtr<class UDevMenuWidget> DevMenuWidget;

	UPROPERTY(Transient)
	TObjectPtr<class UMapWidget> MapOverlay;

	// Waypoint (wereld-positie) die de speler op de kaart zette.
	FVector WaypointWorld = FVector::ZeroVector;
	bool bHasWaypoint = false;

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
	UPROPERTY()
	TObjectPtr<class UWardrobeWidget> WardrobeWidget;
	UPROPERTY()
	TObjectPtr<class USpotInfoWidget> SpotInfoWidget;

	UPROPERTY(Transient)
	TObjectPtr<class UPackWidget> PackWidget;

	UPROPERTY(Transient)
	TObjectPtr<class UShelfWidget> ShelfWidget;
	UPROPERTY()
	TObjectPtr<class UStoreWidget> StoreWidget;

	UPROPERTY(Transient)
	TObjectPtr<class UDryingRackWidget> DryRackWidget;

	UPROPERTY(Transient)
	TObjectPtr<class UHandInfoWidget> HandInfoWidget;

	UPROPERTY(Transient)
	TObjectPtr<class ULevelUpWidget> LevelUpWidget;

	UPROPERTY(Transient)
	TObjectPtr<class UCrosshairWidget> CrosshairWidget;

	UPROPERTY(Transient)
	TObjectPtr<class UWeedToast> ToastWidget;

	UPROPERTY(Transient)
	TObjectPtr<class UPauseMenuWidget> PauseWidget;

	UPROPERTY(Transient)
	TObjectPtr<class UMainMenuWidget> MainMenuWidget;

	// Camera-actor voor de live hoofdmenu-achtergrond (gespawnd op de opgeslagen plek; opgeruimd bij sluiten).
	TWeakObjectPtr<AActor> MenuCamActor;

	UPROPERTY(Transient)
	TObjectPtr<class USaveIndicatorWidget> SaveIndicatorWidget;

	UPROPERTY(Transient)
	TObjectPtr<class USettingsWidget> SettingsWidget;

	UPROPERTY(Transient)
	TObjectPtr<class ULightDimmerWidget> LightDimmerWidget;
	bool bLightDimmerOpen = false;
	TWeakObjectPtr<class APackLightSwitch> DimmerSwitch;
	// Lamp-link-modus:
	bool bLinkModeActive = false;
	TWeakObjectPtr<class APackLightSwitch> LinkSwitch; // schakelaar waarvoor we nu lampen linken
	UPROPERTY(Transient) TArray<TObjectPtr<class ALampLinkMarker>> LinkMarkers;
	FTimerHandle LinkIdleTimer; // link-modus sluit vanzelf na 60s zonder marker-klik
	// Lichtschakelaars per gekochte woning (1x plaatsen zodra de plafondlampen geladen zijn).
	TSet<int32> LightSwitchHomesDone;

	bool bOpen = false;
	bool bDevMenuOpen = false;
	UPROPERTY(Replicated)
	bool bPhoneOpenRep = false;   // server-side spiegel van bOpen -> proxies tonen de texting-anim
	int32 Tab = 0;
	bool bHomeScreen = true; // toon het app-rooster i.p.v. een geopende app

	// Per-contact aantal inkomende berichten dat je al GELEZEN hebt (chat geopend). Client-lokaal.
	TMap<FName, int32> MsgSeen;
	int32 SupplierCat = 0;

	bool bRollOpen = false;
	int32 RollGrams = 2;

	float SmokeHoldFrac = 0.f; // 0..1 voortgang van rook-inhouden (lokale UI-staat)
	float GiveHoldFrac = 0.f;  // 0..1 voortgang van joint-overhandigen
	float RollHoldFrac = 0.f;  // 0..1 voortgang van joint-rollen
	float DropHoldFrac = 0.f;  // 0..1 voortgang van item-droppen (hold Q)
	bool bRollLoadedUI = false;
	int32 RollLoadGramsUI = 0;
	FString RollLoadDesc; // omschrijving van de geladen wiet (voor de hint)
	FName RollStrain = NAME_None; // gekozen strain om te rollen (client-side; None = eerste bruikbare bud)
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
	bool bWardrobeOpen = false; // kledingkast: outfit-menu open
	bool bBankViaAtm = false; // Bank-app geopend via fysieke ATM (geen upgrade vereist)
	bool bPackOpen = false;
	int32 PackBatchUI = 1; // zakjes per verpak-actie (van de bench-tier)

	bool bShelfOpen = false;
	TWeakObjectPtr<class AStorageShelf> ShelfActor; // het schap dat nu open is

	bool bStoreOpen = false;
	bool bStorePayBank = false;                      // false = cash, true = bank
	TWeakObjectPtr<class AStoreCounter> StoreCounterRef; // de balie/winkel die nu open is
	bool bDryRackOpen = false;
	TWeakObjectPtr<class ADryingRack> DryRackActor; // het droogrek dat nu open is

	bool bPauseOpen = false;
	bool bMainMenuOpen = false;
	bool bSettingsOpen = false;
	float FovValue = 90.f;
	float LookSensitivity = 1.f;
	bool bHeadBob = true;

	bool bMergeOpen = false;
	FName MergeItemId = NAME_None;

	// Winkel-state (client-side): gekozen aantal per item + winkelwagen. bSell = verkoop-regel.
	struct FCartLine { FName ItemId = NAME_None; int32 Qty = 0; bool bSell = false; };
	TArray<FCartLine> Cart;

	// Onderweg zijnde bestellingen + hun timers. Gerepliceerd met COND_OwnerOnly (privacy in co-op/
	// competitive: alleen de eigenaar-client krijgt z'n eigen pakketten) -> ook de JOINER heeft nu een
	// gevulde Packages-app (ETA/annuleren/historie). OnRep_Deliveries refresht de app bij de client.
	UPROPERTY(ReplicatedUsing = OnRep_Deliveries)
	TArray<FPhonePendingDelivery> PendingDeliveries;
	// Opgehaalde bestellingen (nieuwste voorop, gecapt op 20) - historie voor de Packages-app.
	UPROPERTY(ReplicatedUsing = OnRep_Deliveries)
	TArray<FPhoneDeliveredRecord> DeliveredHistory;
	TMap<int32, FTimerHandle> DeliveryTimers;
	int32 NextOrderId = 1; // per-component fallback-teller (server-uniek id komt uit GameState::AllocDeliveryId)
	TMap<FName, int32> PendingQty;
	TMap<FName, int32> PendingSellQty;

	// Client: bezorg-lijsten zijn zojuist gerepliceerd -> refresh de Packages-app (via de PhoneWidget).
	UFUNCTION()
	void OnRep_Deliveries();

	bool bPotUpgradeOpen = false;
	TWeakObjectPtr<AGrowPlant> UpgPot;
};
