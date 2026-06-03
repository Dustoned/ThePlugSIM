// ACityGenerator — bouwt procedureel een gestileerde stad rond het speelgebied: een asfalt-wegraster
// met verhoogde stoepen en gebouwen, met een open centraal plein waar de bestaande shop/straat staat.
//
// Volledig DETERMINISTISCH (vaste layout + seeded gebouwhoogtes): host en alle clients bouwen lokaal
// exact dezelfde stad, dus geen replicatie van honderden actors nodig. Net als de DayNightController
// wordt deze lokaal per speler gespawnd (cosmetisch + collision).

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "World/StoreCounter.h" // EShopKind
#include "CityGenerator.generated.h"

class UStaticMesh;
class UStaticMeshComponent;
class USceneComponent;

UCLASS()
class WEEDSHOPCORE_API ACityGenerator : public AActor
{
	GENERATED_BODY()

public:
	ACityGenerator();

protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	// Bouwt de hele stad (idempotent: bouwt maar 1x).
	void BuildCity();

	// Straatlantaarn (paal + kop + warm puntlicht) die 's avonds aan gaat.
	void AddCityLamp(const FVector& BaseWorld);

	// Centraal parkje: gras, paden, boompjes, bankjes en een laag hekje.
	void BuildPark(float CX, float CY, float Size, float GroundTopZ);

	// Een buitenmuur met ECHTE ramen (glasstrook per verdieping + borstwering/latei) en optioneel een
	// deurgat op de begane grond. Muur loopt langs X (bAlongX) of langs Y, gecentreerd op (CenterX,CenterY).
	void BuildWallWindows(float CenterX, float CenterY, bool bAlongX, float Length, float BaseZ,
		int32 Floors, float FloorH, float T, const FLinearColor& Wall, float DoorCenter, float DoorW, float DoorTop);

	// Eén blok-mesh toevoegen onder de root. SizeCm = volledige afmeting; CenterWorld = wereld-midden.
	UStaticMeshComponent* AddBox(UStaticMesh* MeshAsset, const FVector& CenterWorld, const FVector& SizeCm,
		const FLinearColor& Color, bool bCollides, const FRotator& Rot = FRotator::ZeroRotator);

	// Een schuin zadeldak (twee tegen elkaar leunende vlakken) bovenop een gebouw.
	void AddGableRoof(const FVector& TopCenter, float Width, float Depth, float RidgeH, bool bRidgeAlongX, const FLinearColor& Color);

	// Een 3D-tekstbord (winkelnaam) op wereldpositie, gericht langs (DirX,DirY).
	void AddSignText(const FVector& WorldLoc, int32 DirX, int32 DirY, const FString& Text, const FLinearColor& Color, float Size);

	// Een inloopbaar gebouw: holle ruimte met deur-opening in de wand richting (DoorDirX,DoorDirY),
	// vloer, plafond, gekleurde gevel + bord, en (voor winkels) een AStoreCounter binnenin.
	void BuildEnterableBuilding(const FVector& CenterXY, float BaseZ, float Foot, float Height,
		int32 DoorDirX, int32 DoorDirY, EShopKind Kind, const FLinearColor& Body, const FLinearColor& Sign);

	// Warm binnenlicht (anders is een afgesloten ruimte pikdonker).
	void AddInteriorLight(const FVector& WorldLoc);

	// Een rij van 3-4 verschillende huisjes (rijtjeshuizen) die het hele lot vult, met 1 doorlopend dak.
	void BuildRowHouses(float CX, float CY, float TopZ, int32 Ddx, int32 Ddy, uint32 Seed);

	// Een echte flat: meerdere verdiepingen met vloeren, een switchback-trap en een werkende lift.
	void BuildApartmentBlock(float CX, float CY, float TopZ, int32 Ddx, int32 Ddy, int32 Floors, const FLinearColor& Body, const FLinearColor& Sign, bool bSign = true);

	// Een rijtjeshuis-unit van binnen: holle 2-verdiepingen-woning met vloer, rechte trap en werkende deur.
	void BuildHouseUnitInterior(float UX, float UY, float D, float L, float WallH, bool bAlongX, int32 Ndir, float TopZ, const FLinearColor& Body);

	UPROPERTY(VisibleAnywhere) TObjectPtr<USceneComponent> Root;

	// --- Tunables (cm) ---
	UPROPERTY(EditAnywhere, Category = "City") int32 GridRadius = 3;       // blokken vanaf het midden (7x7 bij 3)
	UPROPERTY(EditAnywhere, Category = "City") float BlockSize = 2200.f;   // breedte/diepte van een bouwblok
	UPROPERTY(EditAnywhere, Category = "City") float RoadWidth = 850.f;    // straatbreedte tussen blokken
	UPROPERTY(EditAnywhere, Category = "City") float SidewalkWidth = 250.f;// stoepbreedte rond een blok
	UPROPERTY(EditAnywhere, Category = "City") float CurbHeight = 16.f;    // hoogte van de stoeprand
	UPROPERTY(EditAnywhere, Category = "City") int32 OpenPlazaRadius = 0;  // centrale blokken open laten (0 = alleen het middenblok)

private:
	bool bBuilt = false;
	float GroundZ = 0.f;

	// Straatlampen: spots (naar onder) + zachte gloed-puntlichten + gloeiende koppen, getoggeld op kloktijd.
	UPROPERTY() TArray<TObjectPtr<class ULightComponent>> LampLights;
	UPROPERTY() TArray<TObjectPtr<class ULightComponent>> LampGlows;
	UPROPERTY() TArray<TObjectPtr<class UMaterialInstanceDynamic>> LampHeadMats;
	int32 bLampsOn = -1;
	float LampTickAccum = 0.f;
	float LastLampApplied = -1.f; // her-toepassen als de slider de intensiteit wijzigt
};
