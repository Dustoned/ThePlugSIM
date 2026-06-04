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

	// Marktprijs per eenheid van het gewenste product (cents). 0 als onbekend.
	UFUNCTION(BlueprintPure, Category = "WeedShop|Customer")
	int32 GetMarketPriceCents() const;

	// Marktprijs per eenheid van een willekeurig product (cents). Voor het aanbieden van een andere strain.
	UFUNCTION(BlueprintPure, Category = "WeedShop|Customer")
	int32 GetMarketPriceForProduct(FName ProductId) const;

	// Live acceptatie-% bij een bod (per eenheid) — voor de prijs-slider-UI. Quality01 (0..1) is de
	// kwaliteit van de wiet die je wil verkopen; negatief = neutraal/onbekend.
	UFUNCTION(BlueprintPure, Category = "WeedShop|Customer")
	float GetAcceptanceChance(int32 AskPriceCentsPerUnit, float Quality01 = -1.f) const;

	// Acceptatie-% als je een ANDERE strain aanbiedt dan gevraagd (substituut). ~50% basis, geschaald
	// met loyaliteit/verslaving (een trouwe/verslaafde klant neemt eerder iets anders).
	UFUNCTION(BlueprintPure, Category = "WeedShop|Customer")
	float GetSubstituteAcceptance(FName AltProductId, int32 AskPriceCentsPerUnit, float Quality01 = -1.f) const;

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
	// Laat de AI naar een wereldlocatie lopen (via de navmesh).
	bool WalkTo(const FVector& Dest);

	// Plek waar de klant gaat staan (door de spawner gezet).
	void SetSpot(const FVector& InSpot) { SpotLocation = InSpot; bHasSpot = true; }

	// "Thuis"/uitgang waar de klant heen loopt als hij vertrekt.
	void SetHome(const FVector& InHome) { HomeLocation = InHome; bHasHome = true; }

	// --- Bewoner: woont in een appartement. Roamt overdag (winkel/park), gaat 's nachts naar huis. ---
	// FrontSpot = plek vóór de voordeur (waar 'ie verschijnt/verdwijnt); InteriorPos = referentie binnen.
	void SetupResident(const FVector& FrontSpot, const FVector& InteriorPos, const FString& HouseNumber, const FVector& HallPos = FVector::ZeroVector);
	bool IsResident() const { return bResident; }

	// Huisnummer/adres van deze bewoner (voor afspraak-berichten "kom langs op nr X").
	const FString& GetHomeNumber() const { return HomeNumber; }
	const FVector& GetHomeInteriorPos() const { return HomeInteriorPos; }

	// Start een afspraak voor deze bewoner. bComeToPlayer = de NPC loopt naar de speler (TheyComeToYou);
	// anders verschijnt 'ie in z'n eigen unit en wacht daar tot de speler langskomt (YouGoToThem).
	void BeginAppointment(bool bComeToPlayer);
	void EndAppointment();
	bool HasActiveAppointment() const { return bApptActive; }

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
	FVector ParkCenter = FVector::ZeroVector; // gedeelde hub (stadscentrum/park)
	bool bHasPark = false;
	TWeakObjectPtr<ACityGenerator> CachedCity;
	int32 RoamRouteSeed = 0;
	int32 RoamLegIndex = 0;
	int32 ParkLegCountdown = 0;
	int32 HallLegCountdown = 0;
	bool bLeavingHomeRoute = false;
	ACityGenerator* GetResidentCity(UWorld* W);
	bool PickResidentRoamGoal(FVector& OutGoal, float& OutSearchXY, float& OutSearchZ);
	bool SetResidentRoamGoal(const FVector& DesiredGoal, float SearchXY, float SearchZ);
	float ComputeResidentRoamTimeout(const FVector& Goal) const;
	int32 CountResidentParkVisitors(float Radius) const;

	// Afspraak-staat (overschrijft tijdelijk het roam/nacht-schema).
	bool bApptActive = false;       // er loopt een afspraak voor deze bewoner
	bool bApptComeToPlayer = false; // true = NPC loopt naar de speler; false = NPC wacht in eigen unit
	bool bApptArrived = false;      // (YouGoToThem) al naar de unit verplaatst
	float ApptTimeout = 0.f;        // veiligheids-timer: na X sec geeft de NPC de afspraak op

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
};
