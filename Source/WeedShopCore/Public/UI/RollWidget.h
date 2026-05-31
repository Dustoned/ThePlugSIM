// URollWidget — joint-rol-scherm (UMG, clean stijl). Kies grams per joint (1..max van je papers) en
// zie meteen hoe STERK de joint wordt (verwachte high) — die balk schaalt met gram + THC% + kwaliteit,
// dus hij beweegt nu wél mee als je het gram-aantal verandert. Leest de PhoneClientComponent op de pawn.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "RollWidget.generated.h"

class UPhoneClientComponent;
class UCanvasPanel;
class UWidget;
class UVerticalBox;
class UHorizontalBox;
class UTextBlock;
class UProgressBar;

UCLASS()
class WEEDSHOPCORE_API URollWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	void SetPhone(UPhoneClientComponent* InPhone);

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float DeltaTime) override;

	void BuildShell(UCanvasPanel* Root);
	void RebuildContent();

	TWeakObjectPtr<UPhoneClientComponent> PhoneComp;

	UPROPERTY() TObjectPtr<UWidget> Card;
	UPROPERTY() TObjectPtr<UVerticalBox> Body; // herbouwde inhoud

	// Voor verandering-detectie (alleen herbouwen als er iets wijzigt -> geen flash).
	int32 LastGrams = -1;
	int32 LastMaxG = -2;
};
