// ULightDimmerWidget - kleine popup met een VERTICALE slider om de helderheid van een lichtschakelaar
// (APackLightSwitch) te kiezen. Verschijnt als je de interact-toets op een schakelaar vasthoudt.
// Default-stand van de slider = midden (0.5). Sluit via de knop of door weg te lopen.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "LightDimmerWidget.generated.h"

class UPhoneClientComponent;
class UCanvasPanel;
class UBorder;
class USlider;
class UTextBlock;

UCLASS()
class WEEDSHOPCORE_API ULightDimmerWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	void SetPhone(UPhoneClientComponent* InPhone);

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float DeltaTime) override;

	void BuildShell(UCanvasPanel* Root);

	UFUNCTION() void OnSlider(float Value);

	TWeakObjectPtr<UPhoneClientComponent> Ph;
	UPROPERTY() TObjectPtr<UBorder> Card;
	UPROPERTY() TObjectPtr<USlider> Slider;
	UPROPERTY() TObjectPtr<UTextBlock> ValueText;

	bool bSliderHeld = false;
	TWeakObjectPtr<class APackLightSwitch> LastSwitch;
};
