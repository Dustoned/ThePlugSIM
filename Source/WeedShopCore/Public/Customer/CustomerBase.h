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
class ACityGenerator;

struct FResidentMovementSnapshot
{
	bool bValid = false;
	FString ResidentLabel;
	bool bVisibleOnMap = false;
	bool bAtHomeInside = false;
	bool bEmergingFromHome = false;
	bool bEnteringHome = false;
	bool bHasGoal = false;
	bool bGoalIsPark = false;
	bool bParkPause = false;
	bool bParkUrgentToday = false;
	bool bLikelyStreetCrossing = false;
	bool bOnSidewalkOrPark = false;
	bool bNeedsParkVisitToday = false;
	bool bStuckSuspect = false;
	bool bNearMapEdge = false;
	float Speed2D = 0.f;
	float NoGoalSeconds = 0.f;
	float StuckSeconds = 0.f;
	float DistanceFromCenter = 0.f;
	float DistanceToGoal = 0.f;
	FVector Location = FVector::ZeroVector;
	FVector Goal = FVector::ZeroVector;
};

UENUM(BlueprintType)
enum class ECustomerState : uint8
{
	WantsToOrder	UMETA(DisplayName = "Wil bestellen"),
	Negotiating		UMETA(DisplayName = "Onderhandelt"),
	Served			UMETA(DisplayName = "Geholpen"),
	Leaving			UMETA(DisplayName = "Vertrekt"),
	// Prospect: nog niet klaar om te kopen (te lage verslaving/respect). Eerst gratis samples geven;
	// zodra de verslaving hoog genoeg is wordt het een echte klant die wil kopen.
	Prospect		UMETA(DisplayName = "Prospect")
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

	// Welke persoon dit is (rij in DT_NPCs). Leeg = krijgt er één toegewezen bij spawn.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Replicated, Category = "WeedShop|Customer")
	FName NpcId = NAME_None;

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
	float OrderCooldownSeconds = 240.f;

	// Verslaving die nodig is voordat een prospect echt wil kopen (samples brengen dit omhoog).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WeedShop|Customer")
	float AddictionToBuy = 30.f;

	// Server: kijk of een prospect genoeg "warm" is (verslaving >= drempel) en zo ja maak er een
	// kopende klant van. Geeft true als hij deze keer overstapte (om een melding te tonen).
	bool RefreshProspect();

	// Server: maak er direct een kopende klant van (bv. na een goede gratis joint).
	void BecomeBuyerNow();

	// Appartement-/straat-klanten blijven (cooldown -> opnieuw bestellen). Afspraak-klanten despawnen.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WeedShop|Customer")
	bool bDespawnAfterServed = false;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "WeedShop|Customer")
	ECustomerState State = ECustomerState::WantsToOrder;

	// Laatste regel die deze NPC "zei" (reactie op een joint/deal), getoond in het praat-venster.
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "WeedShop|Customer")
	FString SpeechLine;

	// Server: zet de gesproken regel (verschijnt in het praat-venster bij alle spelers).
	void Say(const FString& Line) { if (HasAuthority()) { SpeechLine = Line; } }

	// True = de speler moet bij DEZE klant zijn nu (afspraak / staat buiten te wachten). Alleen deze
	// krijgen een poppetje-icoon op de kompas; gewone roamers niet.
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "WeedShop|Customer")
	bool bNeedsPlayer = false;
	void SetNeedsPlayer(bool b) { if (HasAuthority()) { bNeedsPlayer = b; } }

	// In gesprek met een speler -> de NPC stopt met lopen tot het gesprek sluit (server-authoritative).
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "WeedShop|Customer")
	bool bTalkingToPlayer = false;
	void SetTalkingToPlayer(bool b);

	// Verkoper achter de winkel-balie: staat altijd stil en is GEEN deal-klant (de balie opent de winkel).
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "WeedShop|Customer")
	bool bShopkeeper = false;
	bool IsShopkeeper() const { return bShopkeeper; }

	// Marktprijs per eenheid van het gewenste product (cents). 0 als onbekend.
	UFUNCTION(BlueprintPure, Category = "WeedShop|Customer")
	int32 GetMarketPriceCents() const;

	// Marktprijs per eenheid van een willekeurig product (cents). Voor het aanbieden van een andere strain.
	UFUNCTION(BlueprintPure, Category = "WeedShop|Customer")
	int32 GetMarketPriceForProduct(FName ProductId) const;

	// Live acceptatie-% bij een bod (per eenheid) — voor de prijs-slider-UI. Quality01 (0..1) is de
	// kwaliteit van de wiet die je wil verkopen; negatief = neutraal/onbekend.
	UFUNCTION(BlueprintPure, Category = "WeedShop|Customer")
	float GetAcceptanceChance(int32 AskPriceCentsPerUnit, float Quality01 = -1.f, float ThcPercent = -1.f) const;

	// Extra acceptatie-% als je STERKERE wiet aanbiedt dan de klant verwacht: per % boven de verwachte THC
	// ~+2.5%, tot +45%. OfferedThc < 0 = onbekend (geen bonus). ExpectedThc <= 0 -> val terug op ~15%.
	static float ThcWillingnessBonus(float OfferedThc, float ExpectedThc);
	// Verwachte THC% = basis-THC van de strain die de klant vroeg (DesiredProductId). 15 als onbekend.
	float GetExpectedThc() const;

	// Prijs-tolerantie per klant-tier (0 Casual .. 0.30 Whale): hogere tiers ervaren de vraagprijs als lager,
	// dus je kunt meer per gram vragen. Raakt ALLEEN de prijs - kwaliteit/THC blijven gewoon meetellen.
	static float TierPriceTolerance(int32 Tier);
	int32 GetMyCustomerTier() const; // tier van DEZE klant via de NPC-registry (1 als onbekend)

	// Acceptatie-% als je een ANDERE strain aanbiedt dan gevraagd (substituut). ~50% basis, geschaald
	// met loyaliteit/verslaving (een trouwe/verslaafde klant neemt eerder iets anders).
	UFUNCTION(BlueprintPure, Category = "WeedShop|Customer")
	float GetSubstituteAcceptance(FName AltProductId, int32 AskPriceCentsPerUnit, float Quality01 = -1.f, float ThcPercent = -1.f) const;

	// Server-authoritative bod op het gewenste product. Betaalt naar PayTo, haalt voorraad uit StockFrom.
	UFUNCTION(BlueprintCallable, Category = "WeedShop|Customer")
	EDealResult SubmitOffer(int32 AskPriceCentsPerUnit, UEconomyComponent* PayTo, UInventoryComponent* StockFrom);

	// Server-authoritative bod op een specifiek product (kan een andere strain zijn = substituut).
	UFUNCTION(BlueprintCallable, Category = "WeedShop|Customer")
	EDealResult SubmitOfferProduct(FName ProductId, int32 AskPriceCentsPerUnit, UEconomyComponent* PayTo, UInventoryComponent* StockFrom);

	// Voorspelt de nieuwe Respect/Loyalty/Addiction ALS deze deal (bij deze prijs + kwaliteit) lukt.
	// Voor de UI-preview; muteert niets. bSubstitute = je biedt een andere strain aan (minder binding).
	UFUNCTION(BlueprintPure, Category = "WeedShop|Customer")
	void PreviewDealOutcome(int32 AskPriceCentsPerUnit, float Quality01, float ThcPercent,
		float& OutRespect, float& OutLoyalty, float& OutAddiction, bool bSubstitute = false) const;

	// IInteractable
	virtual void Interact_Implementation(APawn* InstigatorPawn) override;
	virtual FText GetInteractionPrompt_Implementation() const override;

	// --- Navigatie (spawnt op één punt, loopt naar z'n plek; loopt naar huis bij vertrek) ---
	// Laat de AI naar een wereldlocatie lopen (via de navmesh). Re-requests worden gedempt zodat
	// path-following niet elke tick opnieuw begint, maar stuck-recovery kan geforceerd herpathen.
	bool WalkTo(const FVector& Dest, float AcceptanceRadius = 80.f, bool bAllowPartialPath = false, bool bForceRepath = false);

	// Plek waar de klant gaat staan (door de spawner gezet).
	void SetSpot(const FVector& InSpot) { SpotLocation = InSpot; bHasSpot = true; }

	// "Thuis"/uitgang waar de klant heen loopt als hij vertrekt.
	void SetHome(const FVector& InHome) { HomeLocation = InHome; bHasHome = true; }

	// --- Bewoner: woont in een appartement. Roamt overdag (winkel/park), gaat 's nachts naar huis. ---
	// FrontSpot = plek vóór de voordeur (waar 'ie verschijnt/verdwijnt); InteriorPos = referentie binnen.
	void SetupResident(const FVector& FrontSpot, const FVector& InteriorPos, const FString& HouseNumber, const FVector& HallPos = FVector::ZeroVector);
	bool IsResident() const { return bResident; }
	bool ShouldShowOnCityMap() const;

	// Huisnummer/adres van deze bewoner (voor afspraak-berichten "kom langs op nr X").
	const FString& GetHomeNumber() const { return HomeNumber; }
	const FVector& GetHomeInteriorPos() const { return HomeInteriorPos; }
	bool GetResidentMovementSnapshot(FResidentMovementSnapshot& OutSnapshot);

	// Start een afspraak voor deze bewoner. bComeToPlayer = de NPC loopt naar de speler (TheyComeToYou);
	// anders verschijnt 'ie in z'n eigen unit en wacht daar tot de speler langskomt (YouGoToThem).
	void BeginAppointment(bool bComeToPlayer);
	void EndAppointment();
	bool HasActiveAppointment() const { return bApptActive; }

	// Door de afspraak vooraf bepaald wat de klant wil (matcht met het telefoonbericht). NAME_None = vrij kiezen.
	// Product = volledig item-id (Bag_/Hash_/Edible_<strain>); leeg => bouw uit Strain (Bag_).
	void SetApptWant(FName Strain, int32 Qty, FName Product = NAME_None) { ApptWantStrain = Strain; ApptWantQty = Qty; ApptWantProduct = Product; }

	// Gedeelde keuze-logica: WAT wil een klant van deze tier (volledig product-id Bag_/Hash_/Edible_<strain>) +
	// HOEVEEL (OutQty). Gebruikt door zowel walk-ins als telefoon-afspraken zodat ze identiek schalen.
	static FName PickDesiredProduct(class AWeedShopGameState* GS, class UDataTable* ProductTable, FName NpcId, int32& OutQty);

	// Voor de chat-progressbar (client leest gerepliceerde ApptTimeout). Loopt van 1 -> 0 tot de NPC opgeeft.
	static constexpr float ApptTimeoutMax = 360.f; // moet matchen met de timer in BeginAppointment
	float GetApptTimeLeft() const { return FMath::Max(0.f, ApptTimeout); }
	float GetApptFraction() const { return FMath::Clamp(ApptTimeout / ApptTimeoutMax, 0.f, 1.f); }

	// Park-wachtrij (spawner): is deze bewoner bezig met z'n park-trip (er naartoe lopen of er even staan)?
	bool IsParkTripActive() const { return bRoamGoalIsPark || bPendingRoamGoalIsPark || ParkPauseTimer > 0.f; }

	// Loop eerst rustig naar je eigen huis (normale entry-routing) en despawn pas zodra je binnen bent.
	// Gebruikt door de nacht-populatie/dagelijkse rotatie i.p.v. een directe Destroy (geen plop op straat).
	void SendHomeAndDespawn();

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

	// Loop/idle zelf afspelen (single-node): de meegeleverde ABP_Unarmed animeert de NPC's niet. We
	// bepalen 'beweegt' uit de positie (werkt op host én client-proxy) en spelen walk/idle af.
	UPROPERTY() TObjectPtr<class UAnimSequence> NpcIdle;
	UPROPERTY() TObjectPtr<class UAnimSequence> NpcWalk;
	bool bNpcAnimStarted = false;
	int32 NpcAnimState = -1;        // -1 nog niet, 0 idle, 1 walk
	FVector NpcPrevLoc = FVector::ZeroVector;
	bool bHasNpcPrev = false;
	float NpcMoveHold = 0.f;
	void UpdateNpcAnim(float DeltaSeconds);

	// Bewoner-schema (dag-roamen / 's nachts thuis).
	void TickResident(float DeltaSeconds);
	bool bResident = false;
	FVector HomeFrontSpot = FVector::ZeroVector;
	FVector HomeExitSidewalkSpot = FVector::ZeroVector;
	FVector HomeInteriorPos = FVector::ZeroVector;
	FVector HomeHallPos = FVector::ZeroVector;
	bool bHasHomeHall = false;
	FString HomeNumber;
	float RoamTimer = 0.f;
	bool bAtHomeInside = false;
	FVector RoamGoal = FVector::ZeroVector; // huidig loopdoel
	bool bHasRoamGoal = false;
	bool bRoamGoalIsPark = false;
	bool bPendingRoamGoalIsPark = false;
	float ParkPauseTimer = 0.f;
	FVector ResidentPrevMoveLoc = FVector::ZeroVector;
	bool bHasResidentPrevMoveLoc = false;
	float ResidentStuckTimer = 0.f;
	float ResidentRecoveryCooldown = 0.f;
	float ResidentOffSidewalkTimer = 0.f;
	float ResidentBestDistToGoal = 0.f;
	bool bHasResidentBestDistToGoal = false;
	int32 ResidentRecoveryAttempts = 0;
	float ResidentNoGoalTimer = 0.f;
	int32 ResidentGoalFailCount = 0;
	FVector ParkCenter = FVector::ZeroVector; // gedeelde hub (stadscentrum/park)
	bool bHasPark = false;
	TWeakObjectPtr<ACityGenerator> CachedCity;
	int32 RoamRouteSeed = 0;
	int32 RoamLegIndex = 0;
	int32 ParkLegCountdown = 0;
	int32 HallLegCountdown = 0;
	int32 ResidentRouteDay = -1;
	int32 ResidentStreetLegsToday = 0;
	int32 LastParkVisitDay = -1;
	int32 LastMorningParkVisitDay = -1;
	int32 LastLaterParkVisitDay = -1;
	int32 PendingParkVisitSlot = 0;
	int32 ActiveParkVisitSlot = 0;
	float ResidentWakeDelay = -1.f;
	bool bLeavingHomeRoute = false;
	bool bDespawnWhenInside = false;  // naar huis lopen en pas binnen despawnen (nacht/rotatie)
	float DespawnSafetyTimer = 0.f;   // vangnet: na 90s alsnog (verborgen, binnen) despawnen
	ACityGenerator* GetResidentCity(UWorld* W);
	FVector SnapResidentPointToSidewalk(ACityGenerator* City, const FVector& Desired, bool bAllowPark) const;
	bool IsResidentOutdoorSidewalkPoint(ACityGenerator* City, const FVector& Point, bool bAllowPark) const;
	bool IsResidentParkPoint(ACityGenerator* City, const FVector& Point) const;
	void BuildResidentStreetStops(ACityGenerator* City, TArray<FVector>& OutStops) const;
	bool PickResidentStreetRoamGoal(ACityGenerator* City, int32 RouteLeg, FVector& OutGoal, float& OutSearchXY, float& OutSearchZ) const;
	bool PickResidentRoamGoal(FVector& OutGoal, float& OutSearchXY, float& OutSearchZ);
	bool SetResidentRoamGoal(const FVector& DesiredGoal, float SearchXY, float SearchZ);
	int32 GetResidentParkVisitsToday(int32 Today) const;
	float ComputeResidentParkVisitHour(int32 Today, int32 VisitSlot) const;
	float ComputeResidentParkUrgencyHour(ACityGenerator* City, const class UDayCycleComponent* DayCycle, int32 Today, int32 VisitSlot) const;
	int32 PickResidentParkVisitSlot(ACityGenerator* City, const class UDayCycleComponent* DayCycle, int32 Today, float Hour) const;
	FVector ProjectResidentPointToNav(const FVector& Desired, const FVector& Extent) const;
	FVector ResolveResidentHomeFrontSpot(const FVector& FrontSpot);
	FVector ResolveResidentHomeExitSidewalkSpot(ACityGenerator* City, const FVector& SafeFrontSpot) const;
	FVector GetResidentHomeEntrySpot() const;
	void StartResidentHomeExit(bool bFromInterior);
	bool TickResidentHomeExit(float DeltaSeconds);
	void StartResidentHomeEntry();
	bool TickResidentHomeEntry(float DeltaSeconds);
	float ComputeResidentRoamTimeout(const FVector& Goal) const;
	int32 CountResidentParkVisitors(float Radius) const;
	int32 CountResidentCrowdNear(const FVector& Point, float Radius) const;
	bool RecoverResidentSidewalkDrift(float DeltaSeconds);
	void RecoverResidentIfStuck(float DeltaSeconds);
	bool TrySetResidentDetourGoal(const FVector& FinalGoal);
	bool ForceResidentOutdoorRoamGoal(bool bAllowSnapToStreet);
	bool HasResidentPath(const FVector& From, const FVector& To, float MinDistance2D = 0.f) const;
	bool HasResidentObstacleAhead(const FVector& Goal) const;
	FVector MakeResidentStandingLocation(const FVector& FloorLocation) const;
	float ComputeResidentGoalThinkDelay(float MinDelay, float MaxDelay) const;
	bool bEmergingFromHome = false;
	int32 HomeExitStage = 0;
	float HomeExitStuckTimer = 0.f;
	bool bEnteringHome = false;
	int32 HomeEntryStage = 0;
	float HomeEntryStuckTimer = 0.f;

	// Afspraak-staat (overschrijft tijdelijk het roam/nacht-schema).
	UPROPERTY(Replicated)
	bool bApptActive = false;       // er loopt een afspraak voor deze bewoner
	bool bApptComeToPlayer = false; // true = NPC loopt naar de speler; false = NPC wacht in eigen unit
	bool bApptArrived = false;      // (YouGoToThem) al naar de unit verplaatst
	UPROPERTY(Replicated)
	float ApptTimeout = 0.f;        // veiligheids-timer: na X sec geeft de NPC de afspraak op (gerepliceerd voor de chat-balk)

	// Afspraak-statusberichten: elk hoogstens 1x sturen.
	bool bApptSaidOnWay = false;
	bool bApptSaidHere = false;
	bool bApptSaidWaiting = false;
	FName ApptWantStrain = NAME_None; // vooraf bepaalde wens (uit het afspraak-bericht)
	int32 ApptWantQty = 0;
	FName ApptWantProduct = NAME_None; // volledig product-id uit de afspraak (Bag_/Hash_/Edible_<strain>)
	// COMPETITIVE: actieve relatie-sleutel ("NpcId#spelerId") van de speler die nu met deze klant dealt.
	// NAME_None in co-op (dan geldt de gedeelde NpcId-relatie).
	FName ActiveRelKey = NAME_None;
	void PushApptMessage(const FString& InBody); // stuurt een chat-bericht namens deze NPC

	// Schrijf de huidige attributen terug naar het NPC-register (persistent per persoon).
	void WriteStatsToRegistry();

	// Seconden sinds de klant klaar is (geholpen/vertrekt) — voor auto-despawn of cooldown.
	float LeaveTimer = 0.f;

	// Begin-geduld (om te herstellen na een cooldown).
	float BasePatienceSeconds = 30.f;

	// Navigatie-doelen.
	FVector SpotLocation = FVector::ZeroVector;
	FVector HomeLocation = FVector::ZeroVector;
	bool bHasSpot = false;
	bool bHasHome = false;
	bool bWalkingHome = false;
	FVector LastMoveRequestGoal = FVector::ZeroVector;
	bool bHasLastMoveRequestGoal = false;
	float LastMoveRequestTime = -1000.f;
};
