// ADryingRack — droogrek waar je VERS geoogste (natte) wiet ophangt om te drogen. Pas na drogen wordt
// het verkoopbare/rookbare "Bud_<strain>". Tiers (goedkoop/standaard/pro) verschillen in hoeveel batches
// tegelijk kunnen drogen en hoe snel. Laat je een batch te lang hangen nadat 'ie klaar is (>1 min), dan
// zakt de kwaliteit langzaam, tot maximaal 10% verlies.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Interaction/Interactable.h"
#include "DryingRack.generated.h"

class UStaticMeshComponent;

// Eén drogende batch.
USTRUCT(BlueprintType)
struct FDryEntry
{
	GENERATED_BODY()
	UPROPERTY() FName DryItemId = NAME_None; // resultaat (Bud_<strain>)
	UPROPERTY() int32 Quantity = 0;
	UPROPERTY() float Thc = 0.f;
	UPROPERTY() float Quality = 0.f;         // 0..100
	UPROPERTY() float Elapsed = 0.f;         // sec gedroogd
	UPROPERTY() bool bDone = false;
	UPROPERTY() float OverTime = 0.f;        // sec sinds klaar (kwaliteitsverlies na 60s)
};

UCLASS()
class WEEDSHOPCORE_API ADryingRack : public AActor, public IInteractable
{
	GENERATED_BODY()

public:
	ADryingRack();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override; // registry-remove
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void Tick(float DeltaSeconds) override;

	// Registry van alle levende droogrekken (Add in BeginPlay, Remove in EndPlay): de gear-scans
	// lopen O(instanties) i.p.v. TActorIterator over ALLE actors. Weak-ptrs -> IsValid() checken.
	static const TArray<TWeakObjectPtr<ADryingRack>>& GetAll();

	// Tier-id (gezet bij plaatsen, net als de pot). Bepaalt capaciteit + droogtijd + grootte.
	UPROPERTY(ReplicatedUsing = OnRep_Tier, BlueprintReadOnly, Category = "WeedShop|Dry")
	FName RackTier = TEXT("DryRack_Cheap");

	UFUNCTION()
	void OnRep_Tier();

	// Zet mesh-schaal volgens de tier-definitie (zodat de plaatsing op de vloer klopt).
	void SetupVisual();
	// Toon/verberg de wiet-stapels op de roosters o.b.v. RepDrying/RepReady (zichtbare droog-indicatie).
	void UpdateDryVisual();

	// Tier-definitie: capaciteit (aantal batches) + droogtijd (sec).
	static bool GetRackDef(FName Tier, int32& OutCapacity, float& OutDrySeconds);

	// Server: zit er nog (drogende of klare) wiet in? (voor het oppakken van het rek).
	bool IsEmpty() const { return Entries.Num() == 0; }

	// Save/load + UI (Entries repliceert zodat ook clients de progress-bars zien).
	const TArray<FDryEntry>& GetEntries() const { return Entries; }
	void RestoreEntries(const TArray<FDryEntry>& In) { if (HasAuthority()) { Entries = In; UpdateRep(); } }

	// Publieke info voor de UI.
	UFUNCTION(BlueprintPure, Category = "WeedShop|Dry") int32 GetCapacityPublic() const { return Capacity(); }
	UFUNCTION(BlueprintPure, Category = "WeedShop|Dry") float GetDryTotalSeconds() const { return DrySeconds(); }
	int32 NumReady() const { int32 R = 0; for (const FDryEntry& E : Entries) { if (E.bDone) { ++R; } } return R; }

	// Live over-dry-info voor de UI (gespiegeld aan de collect-formule).
	float OverdryLossFrac(float OverTime) const;     // 0..max kwaliteitsverlies-fractie (0 als verzegeld)
	float SecondsUntilDecay(float OverTime) const;   // sec tot de kwaliteit begint te zakken (0 = al bezig)
	bool IsSealed() const { return bUpSeal; }

	// Leesbare namen van de actieve gear-upgrades op dit rek (Fan/Small fan/Sealer); leeg = geen.
	// Spiegelt AGrowPlant::GetActiveUpgradesLabel zodat het rek z'n gear net als de pot toont bij look-at.
	FString GetActiveUpgradesLabel() const;

	// Server-acties vanuit het droogrek-scherm (afstand-check gebeurt in de PhoneClientComponent).
	// Hangt een natte stapel op om te drogen. Geeft het aantal opgehangen gram terug (0 = vol/niet nat).
	int32 ServerHangWet(FName WetId, int32 Qty, float Thc, float QualPct);
	// Oogst één klare batch op index. Vult Out* en geeft true bij succes (incl. kwaliteitsverlies).
	// CO-OP anti-race (zelfde patroon als AStorageShelf::ServerTake): ExpectedId = de batch-id die de
	// client op deze index zag; mismatch (indices verschoven door een gelijktijdige collect) -> weigeren.
	// NAME_None = geen check (alleen voor server-interne loops zoals collect-all).
	bool ServerCollectIndex(int32 Index, FName ExpectedId, FName& OutId, int32& OutQty, float& OutThc, float& OutQual);

	// IInteractable
	virtual void Interact_Implementation(APawn* InstigatorPawn) override;
	virtual FText GetInteractionPrompt_Implementation() const override;

protected:
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "WeedShop|Dry")
	TObjectPtr<UStaticMeshComponent> Mesh;

	// Samengestelde look (frame + dwarsbalken), los van de root-schaal.
	UPROPERTY() TObjectPtr<USceneComponent> Deco;
	UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> Parts;

	// Batches (repliceert zodat clients ook progress kunnen tonen).
	UPROPERTY(Replicated)
	TArray<FDryEntry> Entries;

	// Gerepliceerde samenvatting voor de prompt (clients zien de aantallen).
	UPROPERTY(Replicated) int32 RepDrying = 0;
	UPROPERTY(Replicated) int32 RepReady = 0;
	UPROPERTY(Replicated) int32 RepCapacity = 0;

	int32 Capacity() const;
	float DrySeconds() const;
	void UpdateRep();

	// Losse upgrade-gear (DryUp_*) vlakbij: sneller drogen (fan) + kwaliteit beschermen (sealer).
	// Gerepliceerd zodat clients de upgrades in de look-at-prompt zien (server rekent in RecomputeUpgrades).
	UPROPERTY(Replicated) float UpSpeedMult = 1.f;
	UPROPERTY(Replicated) bool bUpSeal = false;
	float UpScanTimer = 0.f;
	void RecomputeUpgrades(float DeltaSeconds);
};
