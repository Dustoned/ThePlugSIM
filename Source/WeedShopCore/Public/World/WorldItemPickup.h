// AWorldItemPickup — een gedropt item in de wereld dat (in co-op) door elke speler opgepakt kan worden.
// Server-authoritative: drop verwijdert uit de inventory + spawnt dit; oppakken voegt toe + vernietigt dit.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Interaction/Interactable.h"
#include "WorldItemPickup.generated.h"

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

	UPROPERTY() TObjectPtr<USceneComponent> Root;
	UPROPERTY() TObjectPtr<UStaticMeshComponent> Mesh;

	UFUNCTION() void OnRep_Item();
	void RefreshVisual();
};
