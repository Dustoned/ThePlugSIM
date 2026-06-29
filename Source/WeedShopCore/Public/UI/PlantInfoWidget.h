// UPlantInfoWidget — nette UMG-kaart die verschijnt als je een kweekpot aankijkt: groei-, water-
// en health-balken + verwachte oogst + de actie-hint. Vervangt de oude canvas-kaart (geen
// overlappende tekst meer). Leest de aangekeken pot via de InteractionComponent op de pawn.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "PlantInfoWidget.generated.h"

class UBorder;
class UVerticalBox;
class UTextBlock;
class UProgressBar;
class UCanvasPanel;
class UImage;
class UMaterialInterface;

UCLASS()
class WEEDSHOPCORE_API UPlantInfoWidget : public UUserWidget
{
	GENERATED_BODY()

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float DeltaTime) override;

	void BuildShell(UCanvasPanel* Root);

	UPROPERTY() TObjectPtr<UBorder> Card;
	UPROPERTY() TObjectPtr<UTextBlock> TitleText;
	UPROPERTY() TObjectPtr<UWidget> GrowthHeader;
	UPROPERTY() TObjectPtr<UTextBlock> GrowthLabel;
	UPROPERTY() TObjectPtr<UTextBlock> GrowthTimeText;
	UPROPERTY() TObjectPtr<UVerticalBox> GrowthBox;
	UPROPERTY() TArray<TObjectPtr<UProgressBar>> GrowthBars;
	UPROPERTY() TObjectPtr<UWidget> WaterRow;
	UPROPERTY() TObjectPtr<UProgressBar> WaterBar;
	UPROPERTY() TObjectPtr<UTextBlock> WaterText;
	UPROPERTY() TObjectPtr<UWidget> HealthRow;
	UPROPERTY() TObjectPtr<UProgressBar> HealthBar;
	UPROPERTY() TObjectPtr<UTextBlock> HealthText;
	UPROPERTY() TObjectPtr<UTextBlock> YieldText;
	UPROPERTY() TObjectPtr<UTextBlock> ThcText;
	UPROPERTY() TObjectPtr<UTextBlock> SoilText;
	UPROPERTY() TObjectPtr<UTextBlock> UpgradesText; // actieve gear-upgrades op deze pot (altijd zichtbaar)
	UPROPERTY() TObjectPtr<UTextBlock> HintText;

	// Radiale ring-gauges (water/health/groei) — UImage met M_RadialProgress (Percent + Color params).
	UPROPERTY() TObjectPtr<UMaterialInterface> RadialMat;
	UPROPERTY() TObjectPtr<UImage> WaterRing;
	UPROPERTY() TObjectPtr<UImage> HealthRing;
	UPROPERTY() TObjectPtr<UImage> GrowthRing;
	UPROPERTY() TObjectPtr<UWidget> RingRow; // container voor show/hide
};
