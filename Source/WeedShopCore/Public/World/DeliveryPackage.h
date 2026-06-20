// ADeliveryPackage — het doosje dat de bezorgdrone bij de voordeur laat vallen. Aankijken + interacten
// (E) pakt het uit: de bestelde items gaan naar de inventory/hotbar van de speler die 'm oppakt, mits
// er plek (en geld voor de itemprijs) is. Past niet alles? Dan blijft de rest in de doos staan.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Interaction/Interactable.h"
#include "DeliveryPackage.generated.h"

class USceneComponent;
class UStaticMeshComponent;
class UPhoneClientComponent;

UCLASS()
class WEEDSHOPCORE_API ADeliveryPackage : public AActor, public IInteractable
{
	GENERATED_BODY()

public:
	ADeliveryPackage();

	// Server: vul de doos met de bestelling + koppel de telefoon (voor het opruimen van de pending-regel).
	void SetupOrder(int32 InOrderId, const TArray<FName>& InIds, const TArray<int32>& InQtys, UPhoneClientComponent* InPhone);

	// IInteractable
	virtual void Interact_Implementation(APawn* InstigatorPawn) override;
	virtual FText GetInteractionPrompt_Implementation() const override;

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "WeedShop|Delivery")
	TObjectPtr<USceneComponent> Root;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "WeedShop|Delivery")
	TObjectPtr<UStaticMeshComponent> Mesh; // de kartonnen doos
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "WeedShop|Delivery")
	TObjectPtr<UStaticMeshComponent> TapeX; // pakkettape-strip (kruis bovenop)
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "WeedShop|Delivery")
	TObjectPtr<UStaticMeshComponent> TapeY;

	int32 OrderId = 0;
	TArray<FName> Ids;
	TArray<int32> Qtys;
	TWeakObjectPtr<UPhoneClientComponent> Phone;

	int32 TotalItems() const;
};
