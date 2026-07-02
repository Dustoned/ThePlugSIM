// UWaterCanComponent — zit op de speler-pawn. Logica rond het water in de fles die je VASTHOUDT.
// Het waterniveau zit in het Quality-veld van die fles-stack zelf (per fles, gaat mee in de save), dus
// twee aparte flessen hebben echt apart water. Je vult bij de gootsteen, gebruikt bij het water geven.
//
// CO-OP: server-authoritative (muteert de stack via de inventory), die repliceert naar de clients.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "WaterCanComponent.generated.h"

class UInventoryComponent;

UCLASS(ClassGroup = (WeedShop), meta = (BlueprintSpawnableComponent))
class WEEDSHOPCORE_API UWaterCanComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UWaterCanComponent();

	// Capaciteit van de fles die je NU vasthoudt (0 = geen fles in de hand).
	UFUNCTION(BlueprintPure, Category = "WeedShop|Water")
	int32 GetMaxCharges() const;

	// Water in de fles die je NU vasthoudt (zit in het Quality-veld van die stack).
	UFUNCTION(BlueprintPure, Category = "WeedShop|Water")
	int32 GetCharges() const;

	UFUNCTION(BlueprintPure, Category = "WeedShop|Water")
	bool HasBottle() const { return GetMaxCharges() > 0; }

	// Hoeveel WaterLevel (0..1) een plant per keer water geven erbij krijgt met de fles die je NU vasthoudt.
	// Grotere fles = grotere scheut (plastic 0.25, steel 0.35, jerry 0.50, tank 0.70). Fallback 0.25 zonder fles.
	UFUNCTION(BlueprintPure, Category = "WeedShop|Water")
	float GetWaterPerClick() const;

	// Server: doe 1 slok (FillPerClick) in de vastgehouden fles (gootsteen).
	void Fill();

	// Server: gebruik 1 slok water uit de vastgehouden fles. False als leeg of geen fles.
	bool TryUseCharge();

protected:
	UInventoryComponent* GetInv() const;

	// Item-id (WaterBottle_*) en StackId van de fles die je NU vasthoudt; None/0 als je er geen vasthoudt.
	FName ActiveBottleId() const;
	int32 ActiveBottleStackId() const;

	// Hoeveel slokken er per klik (per gootsteen-interact) bijkomen. 1 = elke fles vult per klik 1 slok,
	// dus grotere fles = altijd meer klikken (plastic 3, steel 6, jerry 12, tank 25 klikken).
	UPROPERTY(EditAnywhere, Category = "WeedShop|Water")
	int32 FillPerClick = 1;
};
