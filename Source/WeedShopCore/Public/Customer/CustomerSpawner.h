// ACustomerSpawner — spawnt klanten op één punt en stuurt ze naar een plek in de buurt (via de
// navmesh). Server-authoritative. Plaats er één in de map (bij de deur/ingang).

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CustomerSpawner.generated.h"

class ACustomerBase;

UCLASS()
class WEEDSHOPCORE_API ACustomerSpawner : public AActor
{
	GENERATED_BODY()

public:
	ACustomerSpawner();

	// Welke klant-klasse gespawnd wordt (leeg = ACustomerBase).
	UPROPERTY(EditAnywhere, Category = "Spawn")
	TSubclassOf<ACustomerBase> CustomerClass;

	UPROPERTY(EditAnywhere, Category = "Spawn")
	int32 MaxCustomers = 3;

	// TEST: spawn slechts ÉÉN klant, met hoge stat (koopklaar), netjes in het centrale park (PlayerStart).
	UPROPERTY(EditAnywhere, Category = "Spawn")
	bool bTestSingleHighStat = false;

	// Bewoners: ken NPC's toe aan appartementen. Ze roamen overdag, gaan 's nachts naar huis, en hun
	// appartementdeur gaat op slot met hun naam. (Heeft voorrang op bovenstaande test-modus.)
	UPROPERTY(EditAnywhere, Category = "Spawn")
	bool bSpawnResidents = true;

	// Hoeveel bewoners ECHT rondlopen (de rest "woont er" via een op-slot-deur met naam). 0 = allemaal.
	UPROPERTY(EditAnywhere, Category = "Spawn")
	int32 MaxResidents = 65;

	// Dagelijkse rotatie: hoeveel fysieke roamers per nieuwe dag wisselen met de virtuele pool, zodat
	// je over een paar dagen iedereen ziet (en het straatbeeld verandert ook als je stilstaat).
	UPROPERTY(EditAnywhere, Category = "Spawn")
	int32 RotatePerDay = 25;

	// 's Nachts loopt er een KLEINERE, (bijna) volledig verslaafde crowd buiten: veel deals mogelijk,
	// maar je heat loopt 's nachts op. Overdag een grotere crowd met minder verslaafden.
	UPROPERTY(EditAnywhere, Category = "Spawn")
	int32 NightRoamers = 25;
	// Addiction-drempel om 's nachts als "verslaafd / op het randje" mee te tellen (= koop-drempel).
	UPROPERTY(EditAnywhere, Category = "Spawn")
	float NightAddictThreshold = 30.f;

	UPROPERTY(EditAnywhere, Category = "Spawn")
	float SpawnInterval = 10.f;

	// Alleen spawnen als er een speler binnen deze afstand is (0 = altijd). Voor streaming-maps:
	// zonder speler in de buurt is de grond daar niet ingeladen en vallen NPC's door de wereld.
	UPROPERTY(EditAnywhere, Category = "Spawn")
	float ActivationRange = 0.f;

	// Hoe ver van het spawn-punt de klanten gaan staan.
	UPROPERTY(EditAnywhere, Category = "Spawn")
	float SpotRadius = 350.f;

protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	void TrySpawn();
	void StartResidentMovementMonitor();
	void TickResidentMovementMonitor(float Now);
	// Maakt van C de koopklare test-klant op de park-plek (idempotent).
	void SetupTestCustomer(ACustomerBase* C, const FVector& Park);
	// Kent NPC-bewoners toe aan appartementen (één keer, zodra de stad gebouwd is).
	void SpawnResidents();

	// Plaatst wereld-fixtures bij een VERSE game: meubels (tafel/koelkast/matras) in elke koopbare/
	// starter-woning, en een ATM in elke winkel. Server-side (repliceert), zodat ze oppakbaar/verplaatsbaar
	// zijn en in de save belanden.
	void SpawnHomeAndShopFixtures(class ACityGenerator* City);

	// Woning-indexen die een fysieke bewoner kregen (voor het meubileren van NPC-woningen).
	TSet<int32> ResidentHomeIndices;

	// --- Dagelijkse rotatie van fysieke bewoners ---
	// Spawn/despawn één bewoner op een woning-index (herbruikbaar voor de initiële spawn én rotatie).
	ACustomerBase* SpawnOneResident(class ACityGenerator* City, int32 HomeIndex, bool bGuaranteedBuyer);
	void DespawnResidentByHome(int32 HomeIndex);
	ACustomerBase* FindResidentByHome(int32 HomeIndex) const;
	// Wissel RotatePerDay fysieke bewoners voor virtuele (voorrang aan nog-niet-getoonde woningen).
	void RotateResidents();
	// Periodieke check (timer): nieuwe dag -> roteren; dag<->nacht-overgang -> populatie aanpassen.
	void CheckResidentRotation();
	// Past de straat-populatie aan op dag/nacht: nacht = ~NightRoamers (bijna) verslaafden buiten,
	// dag = aanvullen tot MaxResidents met de normale mix.
	void ApplyDayNightPopulation(bool bNight);
	int8 LastNightState = -1; // -1 onbekend, 0 dag, 1 nacht
	float NextDayRefillTime = 0.f; // real-time gate: dag-bijvulling gespreid (niet alle bewoners in één frame -> geen burst/tollen)
	float NextCenterDiagTime = 0.f; // (vrij voor hergebruik)

	// --- Park-wachtrij: iedereen komt 1-2x per dag even bij het park, maar netjes OM DE BEURT. ---
	// Max een paar bewoners tegelijk "op park-trip" (lopen + even blijven); de rest staat in de FIFO-rij en
	// roamt gewoon door tot het hun beurt is. Voorkomt ophoping in het centrum.
	struct FParkTicket { TWeakObjectPtr<ACustomerBase> Holder; float GrantTime = 0.f; };
	TArray<FParkTicket> ParkTickets;                 // actieve trips
	TArray<TWeakObjectPtr<ACustomerBase>> ParkQueue; // wie wacht op z'n beurt (FIFO)

public:
	// True = jouw beurt (ga maar); false = in de rij gezet, roam door en vraag later opnieuw.
	bool RequestParkVisit(ACustomerBase* C);
	// Trip klaar/afgebroken -> beurt vrijgeven voor de volgende.
	void FinishParkVisit(ACustomerBase* C);

protected:

	TArray<int32> EligibleHomes;   // alle niet-koopbare ingang-woningen (de volledige pool)
	TSet<int32> PhysicalHomes;     // woning-indexen die NU een fysieke bewoner hebben
	TSet<int32> ActivatedEverHomes;// ooit fysiek geweest (voor "nieuwe gezichten eerst")
	int32 LastRotationDay = -1;
	FTimerHandle RotationTimer;

	bool bResidentsSpawned = false;
	FTimerHandle SpawnTimer;
	float NextResidentSpawnTryRealTime = 0.f;
	bool bResidentMonitorActive = false;
	bool bResidentMonitorDone = false;
	float NextResidentMonitorRealTime = 0.f;
	int32 ResidentMonitorSamplesRemaining = 0;

	UPROPERTY(Transient)
	TArray<TObjectPtr<ACustomerBase>> Spawned;
};
