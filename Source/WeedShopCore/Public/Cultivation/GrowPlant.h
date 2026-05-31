// AGrowPlant — een plant die in real-time groeit (los van de in-game dag/nacht-klok), 5 fases
// doorloopt, verzorging nodig heeft (water -> care-multiplier) en bij oogst gram aan de
// inventory van de oogstende speler toevoegt.
//
// CO-OP: server-authoritative. Groei/fase/care draaien op de server en repliceren; interactie
// loopt via UInteractionComponent (server-RPC). Aankijken (E) = water geven, of oogsten als 'ie klaar is.
//
// Editor-koppeling: maak een BP_GrowPlant (parent = AGrowPlant), wijs DT_Strains toe aan
// StrainTable, zet StrainId, en vul PhaseMeshes met 5 meshes voor de visuele fases (optioneel).

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Interaction/Interactable.h"
#include "GrowPlant.generated.h"

class UStaticMeshComponent;
class UStaticMesh;
class UDataTable;
struct FWeedStrainRow;

UENUM(BlueprintType)
enum class EGrowthPhase : uint8
{
	Seedling	UMETA(DisplayName = "Zaailing"),
	Vegetative	UMETA(DisplayName = "Vegetatief"),
	PreFlower	UMETA(DisplayName = "Pre-bloei"),
	Flower		UMETA(DisplayName = "Bloei"),
	Harvestable	UMETA(DisplayName = "Oogstklaar")
};

UCLASS()
class WEEDSHOPCORE_API AGrowPlant : public AActor, public IInteractable
{
	GENERATED_BODY()

public:
	AGrowPlant();

	virtual void Tick(float DeltaSeconds) override;

	// DataTable met strain-rijen (FWeedStrainRow). Wijs DT_Strains toe in de BP.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WeedShop|Plant")
	TObjectPtr<UDataTable> StrainTable;

	// Welke strain hier groeit (rij-naam in StrainTable).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, ReplicatedUsing = OnRep_Visual, Category = "WeedShop|Plant")
	FName StrainId = NAME_None;

	// Optionele meshes per fase (index = EGrowthPhase). Leeg = geen visuele wissel.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WeedShop|Plant")
	TArray<TObjectPtr<UStaticMesh>> PhaseMeshes;

	// Versnelt de groei (1 = realistisch; hoger = sneller, handig om te testen/demoën).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WeedShop|Plant")
	float GrowthSpeedMultiplier = 1.f;

	// IInteractable
	virtual void Interact_Implementation(APawn* InstigatorPawn) override;
	virtual FText GetInteractionPrompt_Implementation() const override;

	UFUNCTION(BlueprintPure, Category = "WeedShop|Plant")
	EGrowthPhase GetPhase() const { return Phase; }

	UFUNCTION(BlueprintPure, Category = "WeedShop|Plant")
	float GetGrowthFraction() const { return MaxGrowthSeconds > 0.f ? GrowthSeconds / MaxGrowthSeconds : 0.f; }

	// Info voor de plant-UI.
	UFUNCTION(BlueprintPure, Category = "WeedShop|Plant")
	bool IsPlanted() const { return bPlanted; }

	// Soil: er moet soil in de pot voordat je kunt planten. Soil gaat meerdere oogsten mee.
	UFUNCTION(BlueprintPure, Category = "WeedShop|Plant")
	bool HasSoil() const { return !SoilId.IsNone() && SoilUsesLeft > 0; }

	UFUNCTION(BlueprintPure, Category = "WeedShop|Plant")
	FName GetSoilId() const { return SoilId; }

	UFUNCTION(BlueprintPure, Category = "WeedShop|Plant")
	int32 GetSoilUsesLeft() const { return SoilUsesLeft; }

	UFUNCTION(BlueprintPure, Category = "WeedShop|Plant")
	float GetCareMultiplier() const { return CareMultiplier; }

	// Tijd-gewogen gemiddelde verzorging = de kwaliteit die je bij de oogst krijgt (0..1).
	UFUNCTION(BlueprintPure, Category = "WeedShop|Plant")
	float GetCareAvg() const { return CareAvg; }

	// Geschatte real-time seconden tot oogstklaar (0 als al klaar / leeg).
	UFUNCTION(BlueprintPure, Category = "WeedShop|Plant")
	float GetSecondsRemaining() const;

	// Geschatte opbrengst (gram) en kwaliteit (THC%) bij de huidige verzorging.
	UFUNCTION(BlueprintPure, Category = "WeedShop|Plant")
	float GetEstimatedYieldGrams() const;

	UFUNCTION(BlueprintPure, Category = "WeedShop|Plant")
	float GetEstimatedThcPercent() const;

protected:
	virtual void BeginPlay() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	// Scene-root op de actor-origin; de mesh hangt eronder met hoogte-offset (basis op de vloer).
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "WeedShop|Plant")
	TObjectPtr<USceneComponent> Root;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "WeedShop|Plant")
	TObjectPtr<UStaticMeshComponent> Mesh;

	// Bruin schijfje boven in de pot dat zichtbaar is zodra er soil in zit (visuele indicatie).
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "WeedShop|Plant")
	TObjectPtr<UStaticMeshComponent> SoilMesh;

	// Groen plantje (kegel) bovenop de pot dat per fase groter wordt; rijpe kleur bij oogstklaar.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "WeedShop|Plant")
	TObjectPtr<UStaticMeshComponent> PlantMesh;

	UPROPERTY(Transient)
	TObjectPtr<class UMaterialInterface> PlantMat;

	UPROPERTY(Transient)
	TObjectPtr<class UMaterialInterface> PlantReadyMat;

	// Verstreken groeitijd (server-side klok), gerepliceerd voor fractie-weergave.
	UPROPERTY(Replicated)
	float GrowthSeconds = 0.f;

	// Totale groeiduur in seconden (uit strain.GrowMinutes), server-bepaald.
	float MaxGrowthSeconds = 240.f;

	UPROPERTY(ReplicatedUsing = OnRep_Visual)
	EGrowthPhase Phase = EGrowthPhase::Seedling;

	// 0.3..MaxCare — huidige verzorging ("hoe nat"). Daalt langzaam, stijgt door water geven.
	UPROPERTY(Replicated)
	float CareMultiplier = 0.6f;

	// Tijd-gewogen gemiddelde verzorging = de uiteindelijke kwaliteit (gerepliceerd voor UI).
	UPROPERTY(Replicated)
	float CareAvg = 0.6f;

	// Accumulatoren voor het gemiddelde (server-only).
	float CareSum = 0.f;
	float CareTime = 0.f;

	// Maximaal haalbare verzorging op basis van gear (basis lekt -> cap < 1.0).
	float GetMaxCare() const;

	// Of er een plant in de pot staat (false = lege pot, wacht op een zaadje).
	UPROPERTY(Replicated)
	bool bPlanted = false;

	// Welke soil in de pot zit (NAME_None = leeg). Bepaalt yield/kwaliteit-bonus.
	UPROPERTY(ReplicatedUsing = OnRep_Soil)
	FName SoilId = NAME_None;

	// Resterende oogsten voordat de soil op is (0 = leeg).
	UPROPERTY(Replicated)
	int32 SoilUsesLeft = 0;

	// Server: doe de soil die je in de hand hebt (SoilItem) in de pot. Geeft succes.
	bool TryAddSoil(APawn* InstigatorPawn, FName SoilItem);

	// Server: plant het zaadje dat je in de hand hebt (SeedItem). Geeft succes.
	bool TryPlantFromInventory(APawn* InstigatorPawn, FName SeedItem);

	// Server: water geven (kost 1 slok uit de waterfles van de speler) verhoogt de care-multiplier.
	void Water(APawn* InstigatorPawn);

	// Server: oogsten -> gram in de inventory van de oogster, daarna plant verwijderen.
	void Harvest(APawn* InstigatorPawn);

	UFUNCTION()
	void OnRep_Visual();

	UFUNCTION()
	void OnRep_Soil();

	// Toon/verberg het soil-schijfje op basis van HasSoil().
	void UpdateSoilVisual();

	// Toon/schaal het plantje op basis van de groeifase (zaailing -> volgroeid -> oogstklaar).
	void UpdatePlantVisual();

	// Werkt de fase bij op basis van de groeifractie (server).
	void UpdatePhaseFromGrowth();

	// Zet de juiste mesh voor de huidige fase (indien PhaseMeshes gevuld).
	void RefreshMesh();

	const FWeedStrainRow* GetStrain() const;
};
