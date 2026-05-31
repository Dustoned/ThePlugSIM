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

	// Verstreken groeitijd (server-side klok), gerepliceerd voor fractie-weergave.
	UPROPERTY(Replicated)
	float GrowthSeconds = 0.f;

	// Totale groeiduur in seconden (uit strain.GrowMinutes), server-bepaald.
	float MaxGrowthSeconds = 240.f;

	UPROPERTY(ReplicatedUsing = OnRep_Visual)
	EGrowthPhase Phase = EGrowthPhase::Seedling;

	// 0.3..1.0 — verzorging; bepaalt yield + kwaliteit. Daalt langzaam, stijgt door water.
	UPROPERTY(Replicated)
	float CareMultiplier = 0.6f;

	// Of er een plant in de pot staat (false = lege pot, wacht op een zaadje).
	UPROPERTY(Replicated)
	bool bPlanted = false;

	// Welke soil in de pot zit (NAME_None = leeg). Bepaalt yield/kwaliteit-bonus.
	UPROPERTY(Replicated)
	FName SoilId = NAME_None;

	// Resterende oogsten voordat de soil op is (0 = leeg).
	UPROPERTY(Replicated)
	int32 SoilUsesLeft = 0;

	// Server: probeer soil uit de inventory in de pot te doen (beste die je hebt). Geeft succes.
	bool TryAddSoil(APawn* InstigatorPawn);

	// Server: probeer een zaadje uit de inventory van de speler te planten. Geeft succes.
	bool TryPlantFromInventory(APawn* InstigatorPawn);

	// Server: water geven verhoogt de care-multiplier.
	void Water();

	// Server: oogsten -> gram in de inventory van de oogster, daarna plant verwijderen.
	void Harvest(APawn* InstigatorPawn);

	UFUNCTION()
	void OnRep_Visual();

	// Werkt de fase bij op basis van de groeifractie (server).
	void UpdatePhaseFromGrowth();

	// Zet de juiste mesh voor de huidige fase (indien PhaseMeshes gevuld).
	void RefreshMesh();

	const FWeedStrainRow* GetStrain() const;
};
