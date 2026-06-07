// UWaterCanComponent — zit op de speler-pawn. Houdt bij hoeveel water er in je fles zit.
// Je hebt een fles (gekocht item) nodig om te kunnen water geven; vullen doe je bij de gootsteen.
//
// CO-OP: server-authoritative (charges muteren op de server), gerepliceerd voor de HUD.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "WaterCanComponent.generated.h"

class UInventoryComponent;

// Waterniveau PER fles-type (zodat een steel-fles los staat van een plastic-fles e.d.).
USTRUCT()
struct FBottleWater
{
	GENERATED_BODY()
	UPROPERTY() FName BottleId = NAME_None;
	UPROPERTY() int32 Charges = 0;
};

UCLASS(ClassGroup = (WeedShop), meta = (BlueprintSpawnableComponent))
class WEEDSHOPCORE_API UWaterCanComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UWaterCanComponent();

	// Capaciteit van de fles die je NU vasthoudt (0 = geen fles in de hand).
	UFUNCTION(BlueprintPure, Category = "WeedShop|Water")
	int32 GetMaxCharges() const;

	// Water in de fles die je NU vasthoudt.
	UFUNCTION(BlueprintPure, Category = "WeedShop|Water")
	int32 GetCharges() const;

	UFUNCTION(BlueprintPure, Category = "WeedShop|Water")
	bool HasBottle() const { return GetMaxCharges() > 0; }

	// Server: doe 1 slok (FillPerClick) in de vastgehouden fles (gootsteen).
	void Fill();

	// Server: gebruik 1 slok water uit de vastgehouden fles. False als leeg of geen fles.
	bool TryUseCharge();

	// Save/load van het waterniveau per fles-type.
	const TArray<FBottleWater>& GetWatersForSave() const { return Waters; }
	void RestoreWaters(const TArray<FBottleWater>& In) { Waters = In; }

protected:
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	UInventoryComponent* GetInv() const;

	// Item-id van de fles die je NU vasthoudt (WaterBottle_*), of NAME_None.
	FName ActiveBottleId() const;
	// Verwijzing naar (of nieuw) waterniveau van een fles-type.
	int32& WaterRef(FName BottleId);

	// Waterniveau per fles-type.
	UPROPERTY(Replicated)
	TArray<FBottleWater> Waters;

	// Hoeveel slokken er per klik (per gootsteen-interact) bijkomen. 1 = elke fles vult per klik 1 slok,
	// dus grotere fles = altijd meer klikken (plastic 3, steel 6, jerry 12, tank 25 klikken).
	UPROPERTY(EditAnywhere, Category = "WeedShop|Water")
	int32 FillPerClick = 1;
};
