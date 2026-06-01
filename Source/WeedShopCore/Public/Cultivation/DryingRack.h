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
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void Tick(float DeltaSeconds) override;

	// Tier-id (gezet bij plaatsen, net als de pot). Bepaalt capaciteit + droogtijd + grootte.
	UPROPERTY(ReplicatedUsing = OnRep_Tier, BlueprintReadOnly, Category = "WeedShop|Dry")
	FName RackTier = TEXT("DryRack_Cheap");

	UFUNCTION()
	void OnRep_Tier();

	// Zet mesh-schaal volgens de tier-definitie (zodat de plaatsing op de vloer klopt).
	void SetupVisual();

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

	// Server-acties vanuit het droogrek-scherm (afstand-check gebeurt in de PhoneClientComponent).
	// Hangt een natte stapel op om te drogen. Geeft het aantal opgehangen gram terug (0 = vol/niet nat).
	int32 ServerHangWet(FName WetId, int32 Qty, float Thc, float QualPct);
	// Oogst één klare batch op index. Vult Out* en geeft true bij succes (incl. kwaliteitsverlies).
	bool ServerCollectIndex(int32 Index, FName& OutId, int32& OutQty, float& OutThc, float& OutQual);

	// IInteractable
	virtual void Interact_Implementation(APawn* InstigatorPawn) override;
	virtual FText GetInteractionPrompt_Implementation() const override;

protected:
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "WeedShop|Dry")
	TObjectPtr<UStaticMeshComponent> Mesh;

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
};
