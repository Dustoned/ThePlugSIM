// UHandInfoWidget — zachte "wat heb ik in m'n hand"-popup, rechts-midden in beeld. Toont de naam,
// een duidelijke type-tag (Seed / Dried bud / Baggie / Joint / Soil / Container / Spray / ...) en de
// relevante stats (gram, THC%, kwaliteit%, capaciteit). Faded zachtjes in/uit. Leest het actieve
// item van de lokale speler-inventory.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "HandInfoWidget.generated.h"

class UWidget;
class UTextBlock;
class UBorder;

UCLASS()
class WEEDSHOPCORE_API UHandInfoWidget : public UUserWidget
{
	GENERATED_BODY()

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float DeltaTime) override;

	void BuildShell(class UCanvasPanel* Root);

	UPROPERTY() TObjectPtr<UWidget> Card;        // het hele kaartje (fade)
	UPROPERTY() TObjectPtr<UBorder> AccentBar;   // gekleurde balk links (type-kleur)
	UPROPERTY() TObjectPtr<UTextBlock> TypeText; // type-tag (SEED / BAGGIE / ...)
	UPROPERTY() TObjectPtr<UTextBlock> NameText; // item-naam
	UPROPERTY() TObjectPtr<UTextBlock> QtyText;  // aantal/gram, groot bij de titel
	UPROPERTY() TObjectPtr<UBorder> Divider;     // dun lijntje onder de naam
	UPROPERTY() TObjectPtr<class UVerticalBox> StatBox; // nette label/waarde-rijen
	UPROPERTY() TObjectPtr<UTextBlock> HintText; // korte hint onderaan (wat je ermee doet)

	float Shown = 0.f;       // huidige fade (0..1)
	FString LastKey;         // herbouw tekst alleen bij wijziging
};
