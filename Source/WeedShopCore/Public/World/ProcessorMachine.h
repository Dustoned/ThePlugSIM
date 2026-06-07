// AProcessorMachine — verwerkings-machine in de hasj-keten. Eén klasse, twee rollen op basis van de tier:
//   * MESH  (Mesh_*):  gedroogde wiet (Bud_<strain>)     -> THC-crystals (Crystal_<strain>)
//   * PRESS (Press_*): THC-crystals (Crystal_<strain>)   -> hasj (Hash_<strain>), nog hogere THC%
// Tiers (cheap/std/pro) verschillen in capaciteit, snelheid, opbrengst en THC-boost. Level-gated in de winkel.
// Interactie: houd het juiste invoer-item in je hand en druk E om te laden; druk E als een batch KLAAR is om te oogsten.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Interaction/Interactable.h"
#include "ProcessorMachine.generated.h"

class UStaticMeshComponent;

// Eén verwerkende/klare batch.
USTRUCT(BlueprintType)
struct FProcEntry
{
	GENERATED_BODY()
	UPROPERTY() FName OutItemId = NAME_None; // resultaat (Crystal_<strain> of Hash_<strain>)
	UPROPERTY() int32 Quantity = 0;          // gram resultaat
	UPROPERTY() float Thc = 0.f;             // 0..100 (geboost)
	UPROPERTY() float Quality = 0.f;         // 0..100
	UPROPERTY() float Elapsed = 0.f;         // sec verwerkt
	UPROPERTY() bool bDone = false;
};

UCLASS()
class WEEDSHOPCORE_API AProcessorMachine : public AActor, public IInteractable
{
	GENERATED_BODY()

public:
	AProcessorMachine();

	virtual void BeginPlay() override;
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void Tick(float DeltaSeconds) override;

	// Tier-id (gezet bij plaatsen). Bepaalt rol (mesh/press), capaciteit, tijd, opbrengst, THC-boost.
	UPROPERTY(ReplicatedUsing = OnRep_Tier, BlueprintReadOnly, Category = "WeedShop|Proc")
	FName MachineTier = TEXT("Mesh_Cheap");

	UFUNCTION()
	void OnRep_Tier();

	void SetupVisual();

	// Tier-definitie. bOutIsPress = false -> mesh (Bud->Crystal), true -> press (Crystal->Hash).
	static bool GetProcDef(FName Tier, int32& OutCapacity, float& OutSeconds, float& OutConv, float& OutThcMult, bool& bOutIsPress);
	static bool IsPressTier(FName Tier);
	// Verwacht invoer-prefix ("Bud_" voor mesh, "Crystal_" voor press) + resultaat-prefix.
	static FString InputPrefixFor(FName Tier);
	static FString OutputPrefixFor(FName Tier);

	bool IsEmpty() const { return Entries.Num() == 0; }
	const TArray<FProcEntry>& GetEntries() const { return Entries; }
	void RestoreEntries(const TArray<FProcEntry>& In) { if (HasAuthority()) { Entries = In; UpdateRep(); } }

	int32 GetCapacityPublic() const { return Capacity(); }
	float GetProcTotalSeconds() const { return ProcSeconds(); }
	int32 NumReady() const { int32 R = 0; for (const FProcEntry& E : Entries) { if (E.bDone) { ++R; } } return R; }

	// Server: laad een stapel invoer (mesh: Bud_, press: Crystal_). Geeft aantal verwerkte gram terug (0 = vol/fout).
	int32 ServerLoad(FName InId, int32 Qty, float Thc, float QualPct);
	// Server: oogst één klare batch op index. Vult Out* en geeft true bij succes.
	bool ServerCollectIndex(int32 Index, FName& OutId, int32& OutQty, float& OutThc, float& OutQual);

	// IInteractable: E laadt het hand-item of oogst een klare batch.
	virtual void Interact_Implementation(APawn* InstigatorPawn) override;
	virtual FText GetInteractionPrompt_Implementation() const override;

protected:
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "WeedShop|Proc")
	TObjectPtr<UStaticMeshComponent> Mesh;
	UPROPERTY() TObjectPtr<USceneComponent> Deco;
	UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> Parts;

	UPROPERTY(Replicated)
	TArray<FProcEntry> Entries;

	UPROPERTY(Replicated) int32 RepBusy = 0;
	UPROPERTY(Replicated) int32 RepReady = 0;
	UPROPERTY(Replicated) int32 RepCapacity = 0;

	int32 Capacity() const;
	float ProcSeconds() const;
	void UpdateRep();
};
