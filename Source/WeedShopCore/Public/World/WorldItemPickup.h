// AWorldItemPickup — een gedropt item in de wereld dat (in co-op) door elke speler opgepakt kan worden.
// Server-authoritative: drop verwijdert uit de inventory + spawnt dit; oppakken voegt toe + vernietigt dit.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Interaction/Interactable.h"
#include "WorldItemPickup.generated.h"

class UBoxComponent;

UCLASS()
class WEEDSHOPCORE_API AWorldItemPickup : public AActor, public IInteractable
{
	GENERATED_BODY()

public:
	AWorldItemPickup();
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	// Server: vul het gedropte item (id + aantal + THC%/kwaliteit).
	void Setup(FName InItemId, int32 InQty, float InThc, float InQual);

	// Server. Spawnt een pickup op de grond op Loc (zelfde patroon als UInventoryComponent::ServerDropStack:
	// AlwaysSpawn + Setup). Retourneert nullptr op clients of bij een ongeldig item/aantal.
	static AWorldItemPickup* SpawnDrop(UWorld* W, const FVector& Loc, FName ItemId, int32 Qty, float Thc, float Quality);

	// Server. Probeert AddItem; past het niet -> SpawnDrop bij de voeten van de pawn + toast, zodat items
	// NOOIT stil verdwijnen bij een volle inventory. Cash wordt nooit gedropt (cash-stapel = spiegel van
	// het economy-saldo): daarvoor gewoon het AddItem-resultaat. True = toegevoegd OF netjes gedropt.
	static bool GiveOrDrop(class UInventoryComponent* Inv, APawn* Pawn, FName ItemId, int32 Qty, float Thc, float Quality);

	// IInteractable: oppakken.
	virtual void Interact_Implementation(APawn* InstigatorPawn) override;
	virtual FText GetInteractionPrompt_Implementation() const override;

protected:
	UPROPERTY(ReplicatedUsing = OnRep_Item) FName ItemId = NAME_None;
	UPROPERTY(Replicated) int32 Qty = 0;
	UPROPERTY(Replicated) float Thc = 0.f;
	UPROPERTY(Replicated) float Qual = 0.f;

	UPROPERTY() TObjectPtr<UBoxComponent> Body;        // physics-doos + root: valt + draagt collision en line-trace
	UPROPERTY() TObjectPtr<UStaticMeshComponent> Mesh; // anker; het echte model wordt er klein onder gebouwd

	// Schaal van het gedropte model (mul op BuildItemModel). Klein, maar herkenbaar (was 0.6 = veel te klein).
	UPROPERTY(EditAnywhere, Category = "Pickup") float ItemScale = 1.5f;

	FTimerHandle FreezeTimer;

	UFUNCTION() void OnRep_Item();
	void RefreshVisual();
	void AutoFitBody();   // physics-doos passend om het echte model schalen (i.p.v. een vaste 14cm)
	void FreezePhysics(); // na settelen physics uit (geen eindeloze sim/perf)
};
