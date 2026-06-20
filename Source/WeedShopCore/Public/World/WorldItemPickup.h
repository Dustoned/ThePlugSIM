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
