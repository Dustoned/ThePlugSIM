// USpotInfoWidget - dev-overlay (F9-toggle): toont live je positie/yaw, de map, en de mesh waar je
// naar kijkt (naam + positie). Zo kan de speler exact doorgeven WAAR iets is en WELKE mesh het is,
// zonder console. Bij het aanzetten wordt de plek ook naar Saved/MarkedSpots.txt geschreven.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "SpotInfoWidget.generated.h"

class UTextBlock;
class UWidget;

UCLASS()
class WEEDSHOPCORE_API USpotInfoWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	void ToggleInfo();
	void SetInfoVisibleSilent(bool bVisible); // zonder spot-log (voor default-aan)
	bool IsInfoVisible() const { return bInfoVisible; }

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float DeltaTime) override;

	UPROPERTY() TObjectPtr<UWidget> Card;
	UPROPERTY() TObjectPtr<UTextBlock> InfoText;
	bool bInfoVisible = false;
	float UpdateAccum = 0.f;
};
