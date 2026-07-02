// AStoreCounter — een balie in een fysieke winkel in de stad. Aankijken + interact (F) opent de
// bestaande telefoon-shop op de juiste categorie (één bron van waarheid). De character handelt het
// openen van de UI lokaal af (net als ATM/verpaktafel); deze actor levert alleen het type + de prompt.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Interaction/Interactable.h"
#include "StoreCounter.generated.h"

class UStaticMeshComponent;

UENUM(BlueprintType)
enum class EShopKind : uint8
{
	Grow        UMETA(DisplayName = "Grow shop"),
	Supplies    UMETA(DisplayName = "Supplies"),
	Furniture   UMETA(DisplayName = "Furniture"),
	GasStation  UMETA(DisplayName = "Gas station"),
	Apartment   UMETA(DisplayName = "Apartment")
};

UCLASS()
class WEEDSHOPCORE_API AStoreCounter : public AActor, public IInteractable
{
	GENERATED_BODY()

public:
	AStoreCounter();

	// Registry van alle levende balies (Add in BeginPlay, Remove in EndPlay): hot paths lopen
	// O(instanties) i.p.v. TActorIterator over ALLE actors. Weak-ptrs -> IsValid() checken.
	static const TArray<TWeakObjectPtr<AStoreCounter>>& GetAll();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WeedShop|Store")
	EShopKind Kind = EShopKind::Supplies;

	// Telefoon-app + subcategorie die bij dit winkeltype hoort (zie PhoneClientComponent app-indices).
	int32 GetShopApp() const;
	int32 GetShopCat() const;
	bool HasShop() const; // appartement-balie heeft (nog) geen winkel

	virtual void Interact_Implementation(APawn* InstigatorPawn) override;
	virtual FText GetInteractionPrompt_Implementation() const override;

	void SetupVisual(const FLinearColor& Accent);

protected:
	virtual void BeginPlay() override;                                       // registry-add
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override; // registry-remove

	UPROPERTY(VisibleAnywhere) TObjectPtr<USceneComponent> Root;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> Desk;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> Panel;
};
