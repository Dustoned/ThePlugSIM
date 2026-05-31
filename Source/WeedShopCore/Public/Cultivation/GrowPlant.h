// AGrowPlant — een kweekpot met 1..N plant-plekken (bepaald door de pot-tier). Elke plek heeft
// een eigen strain/groei/fase; verzorging (water) en soil gelden voor de hele pot. Interactie is
// hele-pot: planten vult de volgende lege plek, water geeft alle planten water, oogst haalt alle
// oogstklare planten tegelijk.
//
// CO-OP: server-authoritative; per-plek + pot-staat repliceren; interactie via UInteractionComponent.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Interaction/Interactable.h"
#include "GrowPlant.generated.h"

class UStaticMeshComponent;
class UStaticMesh;
class UMaterialInterface;
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

	static constexpr int32 MaxSlots = 6;

	// DataTable met strain-rijen (FWeedStrainRow).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WeedShop|Plant")
	TObjectPtr<UDataTable> StrainTable;

	// Pot-tier (Pot_Broken/Clay/Plastic/Fabric). Bepaalt waterretentie/yield/plekken/uiterlijk.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, ReplicatedUsing = OnRep_Pot, Category = "WeedShop|Plant")
	FName PotTier = NAME_None;

	UFUNCTION(BlueprintPure, Category = "WeedShop|Plant")
	FName GetPotTier() const { return PotTier; }

	// Per-pot upgrades als bit-masker (bit i = upgrade i). Verdwijnt met de pot.
	UPROPERTY(Replicated)
	int32 PotUpgradeMask = 0;

	UFUNCTION(BlueprintPure, Category = "WeedShop|Plant")
	bool HasPotUpgrade(int32 UpgIndex) const { return (PotUpgradeMask & (1 << UpgIndex)) != 0; }

	void ApplyPotUpgrade(int32 UpgIndex) { PotUpgradeMask |= (1 << UpgIndex); }

	// Versnelt de groei (1 = realistisch; hoger = sneller voor demo/testen).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WeedShop|Plant")
	float GrowthSpeedMultiplier = 20.f;

	// IInteractable
	virtual void Interact_Implementation(APawn* InstigatorPawn) override;
	virtual FText GetInteractionPrompt_Implementation() const override;

	// --- Pot-/plek-info voor de UI ---
	UFUNCTION(BlueprintPure, Category = "WeedShop|Plant")
	int32 GetNumSlots() const { return SlotStrain.Num(); }

	UFUNCTION(BlueprintPure, Category = "WeedShop|Plant")
	int32 GetPlantedCount() const;

	UFUNCTION(BlueprintPure, Category = "WeedShop|Plant")
	int32 GetReadyCount() const;

	UFUNCTION(BlueprintPure, Category = "WeedShop|Plant")
	bool IsPlanted() const { return GetPlantedCount() > 0; }

	UFUNCTION(BlueprintPure, Category = "WeedShop|Plant")
	bool HasSoil() const { return !SoilId.IsNone() && SoilUsesLeft > 0; }

	UFUNCTION(BlueprintPure, Category = "WeedShop|Plant")
	FName GetSoilId() const { return SoilId; }

	UFUNCTION(BlueprintPure, Category = "WeedShop|Plant")
	int32 GetSoilUsesLeft() const { return SoilUsesLeft; }

	// Gezondheid/verzorging nu (0..1) — volgt uit het waterpeil; bepaalt samen met tijd de kwaliteit.
	UFUNCTION(BlueprintPure, Category = "WeedShop|Plant")
	float GetCareMultiplier() const { return CareMultiplier; }

	// Waterpeil in de pot (0..1) — loopt leeg, vul je bij met de fles. Aparte stat van care.
	UFUNCTION(BlueprintPure, Category = "WeedShop|Plant")
	float GetWaterLevel() const { return WaterLevel; }

	// Tijd-gewogen gemiddelde gezondheid = de kwaliteit die je bij de oogst krijgt (0..1).
	UFUNCTION(BlueprintPure, Category = "WeedShop|Plant")
	float GetCareAvg() const { return CareAvg; }

	// Min. resterende real-time seconden onder de groeiende planten (0 als geen / al klaar).
	UFUNCTION(BlueprintPure, Category = "WeedShop|Plant")
	float GetSecondsRemaining() const;

	// Totale geschatte opbrengst (gram) over alle geplante plekken.
	UFUNCTION(BlueprintPure, Category = "WeedShop|Plant")
	float GetEstimatedTotalYield() const;

	// Gemiddelde geschatte THC% over de geplante plekken.
	UFUNCTION(BlueprintPure, Category = "WeedShop|Plant")
	float GetEstimatedThcPercent() const;

	// Per-plek (voor UI / debug).
	UFUNCTION(BlueprintPure, Category = "WeedShop|Plant")
	bool IsSlotPlanted(int32 Slot) const { return SlotStrain.IsValidIndex(Slot) && !SlotStrain[Slot].IsNone(); }

protected:
	virtual void BeginPlay() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	// --- Componenten ---
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "WeedShop|Plant")
	TObjectPtr<USceneComponent> Root;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "WeedShop|Plant")
	TObjectPtr<UStaticMeshComponent> Mesh; // pot

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "WeedShop|Plant")
	TObjectPtr<UStaticMeshComponent> SoilMesh;

	// Eén plant-mesh per mogelijke plek.
	UPROPERTY()
	TArray<TObjectPtr<UStaticMeshComponent>> PlantMeshes;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> PlantMat;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> PlantReadyMat;

	// --- Per-plek staat (parallelle arrays, gerepliceerd) ---
	UPROPERTY(ReplicatedUsing = OnRep_Slots)
	TArray<FName> SlotStrain;        // NAME_None = lege plek

	UPROPERTY(Replicated)
	TArray<float> SlotGrowth;        // verstreken groeitijd (sec)

	UPROPERTY(ReplicatedUsing = OnRep_Slots)
	TArray<EGrowthPhase> SlotPhase;

	// --- Pot-brede staat ---
	UPROPERTY(Replicated)
	float CareMultiplier = 0.6f;     // huidige verzorging ("hoe nat")

	UPROPERTY(Replicated)
	float CareAvg = 0.6f;            // tijd-gewogen gemiddelde = kwaliteit

	UPROPERTY(Replicated)
	float WaterLevel = 0.6f;         // vocht in de pot (0..1), apart van care

	float CareSum = 0.f;
	float CareTime = 0.f;

	UPROPERTY(ReplicatedUsing = OnRep_Soil)
	FName SoilId = NAME_None;

	UPROPERTY(Replicated)
	int32 SoilUsesLeft = 0;

	// --- Server-acties ---
	bool TryAddSoil(APawn* InstigatorPawn, FName SoilItem);
	bool TryPlantNextSlot(APawn* InstigatorPawn, FName SeedItem);
	void WaterAll(APawn* InstigatorPawn);
	void HarvestReady(APawn* InstigatorPawn);

	float GetMaxCare() const;

	// Maakt de per-plek arrays op maat (NumSlots uit de pot-tier).
	void EnsureSlots();
	int32 SlotCapacityForTier() const;
	float SlotMaxSeconds(int32 Slot) const;
	FVector SlotLocalOffset(int32 Slot) const;
	const FWeedStrainRow* GetStrainRow(FName StrainId) const;

	UFUNCTION()
	void OnRep_Slots();
	UFUNCTION()
	void OnRep_Soil();
	UFUNCTION()
	void OnRep_Pot();

	void UpdatePotVisual();
	void UpdateSoilVisual();
	void UpdatePlantVisual();
	void UpdatePhases();
};
