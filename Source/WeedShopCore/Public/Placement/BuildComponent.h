// UBuildComponent — plaats-modus voor placeables (nu: de kweekpot). Zit op de speler-pawn.
//
// Flow: speler koopt een "Pot" (supplier) -> item in de inventory. Met de plaats-toets gaat hij
// in plaats-modus: een doorzichtige spook-pot volgt waar je naar de vloer kijkt. Links-klik
// plaatst (server-authoritative: spawnt een AGrowPlant en haalt 1 Pot uit de inventory).
//
// CO-OP: het richten/preview is puur lokaal (alleen voor de bestuurde speler). De daadwerkelijke
// spawn loopt via een Server-RPC zodat alle spelers de pot zien. De pot (AGrowPlant) repliceert.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "BuildComponent.generated.h"

class UStaticMeshComponent;
class UInventoryComponent;

UCLASS(ClassGroup = (WeedShop), meta = (BlueprintSpawnableComponent))
class WEEDSHOPCORE_API UBuildComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UBuildComponent();

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	// Hoe ver je kunt plaatsen (cm) vanaf het camerastandpunt.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WeedShop|Build")
	float PlaceDistance = 400.f;

	// Start/stop de plaats-modus voor een pot (toggle). Doet niets als je geen pot hebt.
	UFUNCTION(BlueprintCallable, Category = "WeedShop|Build")
	void TogglePotPlacement();

	// Stop de plaats-modus (zonder te plaatsen).
	UFUNCTION(BlueprintCallable, Category = "WeedShop|Build")
	void CancelPlacing();

	// Plaats op de huidige (geldige) preview-positie.
	UFUNCTION(BlueprintCallable, Category = "WeedShop|Build")
	void ConfirmPlacement();

	UFUNCTION(BlueprintPure, Category = "WeedShop|Build")
	bool IsPlacing() const { return bPlacing; }

	// Of de huidige preview-positie geldig is (kijkt naar een vlak/de vloer).
	UFUNCTION(BlueprintPure, Category = "WeedShop|Build")
	bool IsPlacementValid() const { return bValidSpot; }

protected:
	virtual void BeginPlay() override;

	// Start plaats-modus met een specifiek item-id (moet in de inventory zitten).
	void StartPlacing(FName ItemId);

	// Server: plaats het placeable (haalt 1 uit de inventory, spawnt de actor).
	UFUNCTION(Server, Reliable)
	void ServerPlace(FName ItemId, FVector Location, FRotator Rotation);

	// Camerastandpunt van de bestuurde pawn (voor de plaats-trace).
	bool GetViewPoint(FVector& OutLocation, FRotator& OutRotation) const;

	UInventoryComponent* GetOwnerInventory() const;

	// Doorzichtige preview-mesh (lokaal; alleen voor de bestuurde speler).
	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> Ghost;

	bool bPlacing = false;
	bool bValidSpot = false;
	FName PlacingItemId = NAME_None;
	FVector PreviewLocation = FVector::ZeroVector;
	FRotator PreviewRotation = FRotator::ZeroRotator;
};
