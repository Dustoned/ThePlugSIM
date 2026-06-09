// AAtm — geldautomaat in de wereld. Aankijken + interacten opent de Bank-app op je telefoon, waar je
// cash kunt storten (witwassen): belasting bij binnenkomst, daglimiet en heat-risico. Het openen van de
// UI gebeurt lokaal (de character vangt dit af); de storting zelf loopt server-authoritative.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Interaction/Interactable.h"
#include "Atm.generated.h"

class UStaticMeshComponent;
class USceneComponent;

UCLASS()
class WEEDSHOPCORE_API AAtm : public AActor, public IInteractable
{
	GENERATED_BODY()

public:
	AAtm();

	// IInteractable — de prompt; de echte actie (Bank-app openen) doet de character lokaal.
	virtual void Interact_Implementation(APawn* InstigatorPawn) override;
	virtual FText GetInteractionPrompt_Implementation() const override;

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	// Safe-modus: dit is een KLUIS, geen ATM. Opent dezelfde UI maar op de Safe-tab, en bepaalt de kluis-
	// capaciteit (progressie: grotere kluis = meer cash veilig). Gezet bij plaatsen.
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "WeedShop|Safe")
	bool bSafeMode = false;
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "WeedShop|Safe")
	int64 SafeCapacityCents = 0;

	bool IsSafe() const { return bSafeMode; }
	int64 GetSafeCapacityCents() const { return SafeCapacityCents; }
	void InitAsSafe(int64 CapacityCents) { bSafeMode = true; SafeCapacityCents = CapacityCents; }

	// Capaciteit (cents) per kluis-tier-item (Safe_Small/Medium/Large/Vault). 0 = geen safe.
	static int64 SafeCapacityForItem(FName ItemId);

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "WeedShop|ATM")
	TObjectPtr<USceneComponent> Root;

	// Verborgen collision-kast (draagt de footprint).
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "WeedShop|ATM")
	TObjectPtr<UStaticMeshComponent> Mesh;

	// Deco-wortel (absolute schaal) waar de samengestelde onderdelen onder hangen.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "WeedShop|ATM")
	TObjectPtr<USceneComponent> Deco;
};
