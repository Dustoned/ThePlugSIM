// UWaterCanComponent — zit op de speler-pawn. Houdt bij hoeveel water er in je fles zit.
// Je hebt een fles (gekocht item) nodig om te kunnen water geven; vullen doe je bij de gootsteen.
//
// CO-OP: server-authoritative (charges muteren op de server), gerepliceerd voor de HUD.

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

	// Capaciteit op basis van de beste fles die je hebt (0 = geen fles).
	UFUNCTION(BlueprintPure, Category = "WeedShop|Water")
	int32 GetMaxCharges() const;

	UFUNCTION(BlueprintPure, Category = "WeedShop|Water")
	int32 GetCharges() const { return WaterCharges; }

	// Save/load: zet het waterniveau terug.
	void RestoreCharges(int32 C) { WaterCharges = FMath::Max(0, C); }

	UFUNCTION(BlueprintPure, Category = "WeedShop|Water")
	bool HasBottle() const { return GetMaxCharges() > 0; }

	// Server: vul de fles tot het maximum (gootsteen).
	void Fill();

	// Server: gebruik 1 slok water. False als leeg of geen fles.
	bool TryUseCharge();

protected:
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	UInventoryComponent* GetInv() const;

	// Hoeveel water er nu in de fles zit.
	UPROPERTY(Replicated)
	int32 WaterCharges = 0;

	// Hoeveel slokken er per klik (per gootsteen-interact) bijkomen. Grotere fles -> vaker klikken.
	UPROPERTY(EditAnywhere, Category = "WeedShop|Water")
	int32 FillPerClick = 3;
};
