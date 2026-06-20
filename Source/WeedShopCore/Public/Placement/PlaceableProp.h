// APlaceableProp — generiek plaatsbaar object (meubels e.d.) dat z'n uiterlijk (mesh/schaal)
// uit de placeable-registry haalt op basis van ItemId. Oppakbaar (hold G via UBuildComponent)
// en herplaatsbaar als item.
//
// CO-OP: server-authoritative gespawnd (repliceert). ItemId repliceert zodat clients dezelfde
// mesh tonen.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Interaction/Interactable.h"
#include "PlaceableProp.generated.h"

class UStaticMeshComponent;

UCLASS()
class WEEDSHOPCORE_API APlaceableProp : public AActor, public IInteractable
{
	GENERATED_BODY()

public:
	APlaceableProp();

	// Welk item dit is (rij in de placeable-registry). Bepaalt mesh/schaal.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, ReplicatedUsing = OnRep_ItemId, Category = "WeedShop|Placeable")
	FName ItemId = NAME_None;

	// IInteractable: prompt; interact = slapen (alleen voor bedden), anders niets (oppakken via hold G).
	virtual void Interact_Implementation(APawn* InstigatorPawn) override;
	virtual FText GetInteractionPrompt_Implementation() const override;

protected:
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UFUNCTION()
	void OnRep_ItemId();

	// Zet mesh/schaal/offset op basis van de registry-definitie voor ItemId.
	void SetupVisual();

	// Scene-root (blijft op de actor-origin); de mesh hangt eronder met een hoogte-offset, zodat
	// het instellen van de mesh-offset de actor NIET verplaatst.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "WeedShop|Placeable")
	TObjectPtr<USceneComponent> Root;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "WeedShop|Placeable")
	TObjectPtr<UStaticMeshComponent> Mesh;

	// Samengestelde look voor specifieke meubels (tafel/koelkast/matras). Voor de rest blijft de
	// enkele mesh zichtbaar. Onder Root (ongeschaald), dus onderdelen in echte cm vanaf de vloer (z=0).
	UPROPERTY() TObjectPtr<USceneComponent> Deco;
	UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> Parts;
	UPROPERTY() TObjectPtr<UStaticMeshComponent> MirrorMesh; // tweede zijde van enkelzijdige pack-muren
	UPROPERTY() TObjectPtr<class UPointLightComponent> StructLight; // echt licht voor Struct_CeilLamp
	UPROPERTY() TObjectPtr<USceneComponent> RuntimeModel; // container voor runtime-opgebouwde basis-vorm modellen (BuildItemModel)
	UPROPERTY(ReplicatedUsing = OnRep_DoorOpen) bool bDoorOpen = false; // Struct_Door open/dicht
	UFUNCTION() void OnRep_DoorOpen();
	void HideParts();
};
