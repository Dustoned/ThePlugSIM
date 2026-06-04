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
class ACityDoor;

// Eén appartement-woning: de deur, een binnen-plek, een plek net buiten de deur, en het huisnummer.
USTRUCT()
struct FApartmentHome
{
	GENERATED_BODY()
	UPROPERTY() TWeakObjectPtr<ACityDoor> Door;
	UPROPERTY() FVector InteriorPos = FVector::ZeroVector;
	UPROPERTY() FVector DoorPos = FVector::ZeroVector;
	UPROPERTY() FString Number;
};

// Eén blok op de kaart: wereld-XY-midden + kleur + label (winkelnaam of huisnummer-reeks).
USTRUCT()
struct FCityMapBlock
{
	GENERATED_BODY()
	UPROPERTY() FVector2D Center = FVector2D::ZeroVector;
	UPROPERTY() FLinearColor Color = FLinearColor(0.5f, 0.5f, 0.5f);
	UPROPERTY() FString Label;
	UPROPERTY() bool bShop = false;
};

UCLASS()
class WEEDSHOPCORE_API ACityGenerator : public AActor
{
	GENERATED_BODY()

public:
	ACityGenerator();

	// --- Kaart-API: dezelfde deterministische layout, maar als data (geen geometrie) ---
	FVector GetCityCenter() const { return CityCenter; }
	int32 GetGridRadiusClamped() const { return FMath::Clamp(GridRadius, 1, 8); }
	float GetPitch() const { return BlockSize + RoadWidth; }
	float GetMapBlockSize() const { return BlockSize; }
	void GetMapBlocks(TArray<FCityMapBlock>& Out) const;

	// Alle geregistreerde appartement-woningen (deur + plekken + huisnummer) voor het bewoner-systeem.
	const TArray<FApartmentHome>& GetApartmentHomes() const { return ApartmentHomes; }

	// --- Echte top-down kaart-render (orthografische SceneCapture van de stad) ---
	class UTextureRenderTarget2D* GetMapRenderTarget() const { return MapRT; }
	float GetMapOrthoWidth() const { return MapOrthoWidth; } // wereld-breedte die het beeld dekt
	void CaptureMapNow(); // (her)render de kaart

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

	// Een 3D-tekstbord (winkelnaam) op wereldpositie, gericht langs (DirX,DirY). bGlow = zelf-oplichtend
	// (emissive materiaal) zodat het 's nachts leesbaar is/licht geeft.
	void AddSignText(const FVector& WorldLoc, int32 DirX, int32 DirY, const FString& Text, const FLinearColor& Color, float Size, bool bGlow = false);

	// Een huisnummer-bordje (klein donker plaatje + oplichtend nummer) op wereldpositie, gericht langs
	// (DirX,DirY). Zoals in NL: rechts naast de deur op ~ooghoogte.
	void AddDoorNumber(const FVector& PlateCenter, int32 DirX, int32 DirY, const FString& Text, float Size);

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
	ACityDoor* BuildHouseUnitInterior(float UX, float UY, float D, float L, float WallH, bool bAlongX, int32 Ndir, float TopZ, const FLinearColor& Body);

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
	FVector CityCenter = FVector::ZeroVector; // referentie-midden (PlayerStart) voor de kaart
	UPROPERTY() TArray<FApartmentHome> ApartmentHomes; // geregistreerde woningen (bewoner-systeem)

	// Top-down kaart-capture.
	UPROPERTY() TObjectPtr<class USceneCaptureComponent2D> MapCapture;
	UPROPERTY() TObjectPtr<class UTextureRenderTarget2D> MapRT;
	float MapOrthoWidth = 0.f;
	FTimerHandle MapCaptureTimer;

	// Straatlampen: spots (naar onder) + zachte gloed-puntlichten + gloeiende koppen, getoggeld op kloktijd.
	UPROPERTY() TArray<TObjectPtr<class ULightComponent>> LampLights;
	UPROPERTY() TArray<TObjectPtr<class ULightComponent>> LampGlows;
	UPROPERTY() TArray<TObjectPtr<class UMaterialInstanceDynamic>> LampHeadMats;
	int32 bLampsOn = -1;
	float LampTickAccum = 0.f;
	float LastLampApplied = -1.f; // her-toepassen als de slider de intensiteit wijzigt
};
