// UBootCoverWidget - vol-scherm cover NA het laden. Blijft over beeld (THE PLUG + progress + wisselende
// teksten) tot de kamer echt klaar is (vloer ingestreamd, zie WeedShop_IsRoomReady), zodat je de wereld
// niet om je heen ziet inladen. Draait op gewone game-ticks -> geen movie-player hang-risico.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "BootCoverWidget.generated.h"

class UTextBlock;
class UProgressBar;
class UBorder;
class UCanvasPanel;

UCLASS()
class WEEDSHOPCORE_API UBootCoverWidget : public UUserWidget
{
	GENERATED_BODY()

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float DeltaTime) override;

private:
	UPROPERTY() TObjectPtr<UBorder> Cover;
	UPROPERTY() TObjectPtr<class UWidget> Content;     // gecentreerde inhoud (titel/tekst/bar)
	UPROPERTY() TObjectPtr<UTextBlock> Title;
	UPROPERTY() TObjectPtr<UTextBlock> Sub;
	UPROPERTY() TObjectPtr<UTextBlock> StatusText;
	UPROPERTY() TObjectPtr<class USizeBox> BarBox;
	UPROPERTY() TObjectPtr<UProgressBar> Bar;

	float Elapsed = 0.f;
	float ReadyAt = -1.f;   // moment waarop de kamer klaar werd (voor een korte na-buffer)
	float SettleAt = -1.f;  // kamer EN shaders klaar -> daarna nog wat tijd voor de lighting/exposure-settle (geen spawn-flashes)
	float LastDPI = -1.f;   // laatst toegepaste DPI-compensatie (alleen herrekenen als 'ie wijzigt)
	int32 LastStep = -1;
	int32 ShaderPeak = 0;   // hoogste aantal shader-compile-jobs gezien (voor echte bar-voortgang)
	bool bFading = false;
	float Fade = 1.f;
};
