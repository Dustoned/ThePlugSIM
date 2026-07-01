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

// Volledige pot/plant-staat voor save/load.
struct FGrowPlantState
{
	int32 PotUpgradeMask = 0;
	FName SoilId = NAME_None;
	int32 SoilUsesLeft = 0;
	float CareMultiplier = 0.6f;
	float CareAvg = 0.6f;
	float WaterLevel = 0.6f;
	TArray<FName> SlotStrain;
	TArray<float> SlotGrowth;
	TArray<uint8> SlotPhase;
	TArray<uint8> SlotAfflict;     // 0 gezond, 1 mold, 2 pest
	TArray<float> SlotAfflictTime; // sec sinds besmetting
	float FertYieldMult = 1.f;
};

UCLASS()
class WEEDSHOPCORE_API AGrowPlant : public AActor, public IInteractable
{
	GENERATED_BODY()

public:
	AGrowPlant();

	// Save/load: lees of zet de volledige pot/plant-staat (server-only voor Restore).
	void CaptureState(FGrowPlantState& Out) const;
	void RestoreState(const FGrowPlantState& In);

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

	// Per-pot upgrades als bit-masker (bit i = upgrade i). Wordt nu AFGELEID van fysieke gear-accessoires
	// die vlakbij de pot staan (zie RecomputeGearUpgradeMask), niet meer apart gekocht.
	UPROPERTY(Replicated)
	int32 PotUpgradeMask = 0;

	UFUNCTION(BlueprintPure, Category = "WeedShop|Plant")
	bool HasPotUpgrade(int32 UpgIndex) const { return (PotUpgradeMask & (1 << UpgIndex)) != 0; }

	void ApplyPotUpgrade(int32 UpgIndex) { PotUpgradeMask |= (1 << UpgIndex); }

	// Komma-gescheiden namen van de actieve gear-upgrades (uit het masker). Leeg = geen upgrades.
	FString GetActiveUpgradesLabel() const;

	// Server: herbereken het upgrade-masker uit gear-accessoires (Gear_*) die binnen bereik staan.
	void RecomputeGearUpgradeMask(float DeltaSeconds);
	float GearScanTimer = 0.f; // throttle voor de gear-scan

	// Versnelt de groei (1 = realistisch; hoger = sneller voor demo/testen). REALISTISCH op 1: de groeitijden
	// in DT_Strains (bv. Streetweed 2,5 min) gelden dan écht. NIET op een testwaarde laten staan voor release.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WeedShop|Plant")
	float GrowthSpeedMultiplier = 1.f;

	// Over-rijp verval: zodra een plek oogstklaar is en je niet oogst, zakt de gezondheid (health),
	// en dus de kwaliteit. Het "bulk"-deel (health -> 10%) duurt RotBulkFactor x de groeitijd; de
	// laatste 10% (10% -> 0%, dan sterft de plant en is het zaadje weg) duurt RotSlowFactor x langer.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WeedShop|Plant")
	float RotBulkFactor = 2.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WeedShop|Plant")
	float RotSlowFactor = 3.0f;

	// IInteractable
	virtual void Interact_Implementation(APawn* InstigatorPawn) override;
	virtual FText GetInteractionPrompt_Implementation() const override;

	void RobClear(); // overval (server): alle groeiende planten weg (slots leeg)

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

	// Weergavenaam van de eerst-geplante strain (leeg als de pot leeg is).
	UFUNCTION(BlueprintPure, Category = "WeedShop|Plant")
	FText GetPrimaryStrainName() const;

	// Rauwe strain-FName van de eerst-geplante plek (voor bv. de per-strain tag-kleur); NAME_None als leeg.
	UFUNCTION(BlueprintPure, Category = "WeedShop|Plant")
	FName GetPrimaryStrainId() const { for (const FName& S : SlotStrain) { if (!S.IsNone()) { return S; } } return NAME_None; }

	// Basis-THC% van de eerst-geplante strain (0 als leeg).
	UFUNCTION(BlueprintPure, Category = "WeedShop|Plant")
	float GetPrimaryBaseThc() const;

	// Groeifractie van een plek (0..1) voor de progressiebalk.
	UFUNCTION(BlueprintPure, Category = "WeedShop|Plant")
	float GetSlotFraction(int32 Slot) const;

	// Of een plek oogstklaar is (voor de balk-kleur).
	UFUNCTION(BlueprintPure, Category = "WeedShop|Plant")
	bool IsSlotReady(int32 Slot) const { return SlotPhase.IsValidIndex(Slot) && SlotPhase[Slot] == EGrowthPhase::Harvestable; }

	// --- Mold/pest ---
	// Besmetting van een plek: 0 gezond, 1 mold (schimmel), 2 pest (ongedierte).
	UFUNCTION(BlueprintPure, Category = "WeedShop|Plant")
	uint8 GetSlotAfflict(int32 Slot) const { return SlotAfflict.IsValidIndex(Slot) ? SlotAfflict[Slot] : 0; }

	// Aantal besmette plekken.
	UFUNCTION(BlueprintPure, Category = "WeedShop|Plant")
	int32 GetAfflictedCount() const;

	// Minste resterende seconden voor de besmette plant die het dichtst bij doodgaan is (0 = geen).
	// = resterende gratie + de tijd die de snelle kwaliteit-drain daarna nodig heeft tot de bodem.
	UFUNCTION(BlueprintPure, Category = "WeedShop|Plant")
	float GetWorstAfflictSecondsLeft() const;

	// Huidige-cyclus mest-bonus op de opbrengst (1 = geen).
	UFUNCTION(BlueprintPure, Category = "WeedShop|Plant")
	float GetFertYieldMult() const { return FertYieldMult; }


protected:
	virtual void BeginPlay() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	// --- Componenten ---
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "WeedShop|Plant")
	TObjectPtr<USceneComponent> Root;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "WeedShop|Plant")
	TObjectPtr<UStaticMeshComponent> Mesh; // pot

	// Bredere rand-lip aan de bovenkant + voetje onderaan -> echte bloempot-vorm i.p.v. kale cilinder.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "WeedShop|Plant")
	TObjectPtr<UStaticMeshComponent> PotRim;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "WeedShop|Plant")
	TObjectPtr<UStaticMeshComponent> PotFoot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "WeedShop|Plant")
	TObjectPtr<UStaticMeshComponent> SoilMesh;

	// Donkere "binnenkant" van de pot (altijd zichtbaar) -> de pot oogt leeg/hol tot je soil toevoegt.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "WeedShop|Plant")
	TObjectPtr<UStaticMeshComponent> PotInner;
	UPROPERTY() TObjectPtr<class UMaterialInstanceDynamic> InnerMID;

	// Samengestelde plant per plek: een steel + bossige blad-clusters + toppen (buds als 'ie klaar is).
	UPROPERTY() TArray<TObjectPtr<USceneComponent>> PlantRoots;        // 1 per plek
	UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> PlantStems;   // 1 per plek
	UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> PlantLeaves;  // FoliagePerPlant per plek
	UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> PlantBuds;    // BudsPerPlant per plek

	// Klein zwevend "ziek"-bolletje per plek (wit = mold, oranje = pest); zichtbaar bij besmetting.
	UPROPERTY()
	TArray<TObjectPtr<UStaticMeshComponent>> SickMarkers;

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

	// --- Mold/pest per plek ---
	// Besmet = harde lockout: de hele pot bevriest (groei/water/care) en water geven is geblokkeerd
	// tot er gesprayed is. Binnen de gratie sprayen = geen straf; daarna zakt de kwaliteit snel en
	// sterft de plant pas als die op de bodem staat (zie de afflict-constanten in GrowPlant.cpp).
	UPROPERTY(ReplicatedUsing = OnRep_Slots)
	TArray<uint8> SlotAfflict;        // 0 gezond, 1 mold, 2 pest

	UPROPERTY(Replicated)
	TArray<float> SlotAfflictTime;    // sec sinds besmetting (gratie -> daarna kwaliteit-drain)

	// Huidige-cyclus mest-bonus op de opbrengst (1 = geen). Reset na de oogst.
	UPROPERTY(Replicated)
	float FertYieldMult = 1.f;

	// --- Server-acties ---
	bool TryAddSoil(APawn* InstigatorPawn, FName SoilItem);
	bool TryPlantNextSlot(APawn* InstigatorPawn, FName SeedItem);
	void WaterAll(APawn* InstigatorPawn);
	void HarvestReady(APawn* InstigatorPawn);
	bool TryApplySpray(APawn* InstigatorPawn, FName SprayItem);
	bool TryApplyFertilizer(APawn* InstigatorPawn, FName FertItem);

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
