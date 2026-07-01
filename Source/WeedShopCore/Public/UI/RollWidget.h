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
class UWeedActionButton;

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
	// Bouwt de vaste inhoud (kop, twee panes) EENMALIG op. Daarna nooit meer ClearChildren.
	void BuildContentOnce();
	// Werkt de bestaande widgets IN-PLACE bij (SetText/SetBrush/SetPercent/SetVisibility) — geen teardown.
	void UpdateContent();

	TWeakObjectPtr<UPhoneClientComponent> PhoneComp;

	UPROPERTY() TObjectPtr<UWidget> Card;
	UPROPERTY() TObjectPtr<UVerticalBox> Body; // vaste inhoud (eenmalig gebouwd)

	// --- Persistente panes: getoggeld via Visibility i.p.v. Body->ClearChildren() ---
	UPROPERTY() TObjectPtr<UVerticalBox> NoPapersPane; // "No papers!" + Close
	UPROPERTY() TObjectPtr<UVerticalBox> FullPane;     // grams-keuze + sterkte + acties

	// --- Persistente widgets in FullPane die in-place worden bijgewerkt ---
	UPROPERTY() TObjectPtr<UTextBlock>   GramsLabel;    // "Grams per joint: N (up to Mg)"
	UPROPERTY() TObjectPtr<UHorizontalBox> GramsRow;    // container van de gram-knoppen-pool
	UPROPERTY() TObjectPtr<UTextBlock>   StrengthLabel; // "Joint strength: ..." / "No weed ..."
	UPROPERTY() TObjectPtr<UTextBlock>   HintLabel;     // "More grams = stronger joint."
	UPROPERTY() TObjectPtr<UProgressBar> StrengthBar;
	UPROPERTY() TObjectPtr<UWeedActionButton> LoadBtn;  // "Load (Ng) ..." (alleen zichtbaar met wiet)
	UPROPERTY() TObjectPtr<UTextBlock>   LoadBtnText;

	// Gram-knoppen-pool: één knop per mogelijk gram (index 0 = 1g). Groeit mee als betere papers
	// meer gram toestaan; knop g wordt verborgen als g > MaxG en opgekleurd als hij geselecteerd is.
	UPROPERTY() TArray<TObjectPtr<UWeedActionButton>> GramButtons;
	UPROPERTY() TArray<TObjectPtr<UTextBlock>>        GramButtonTexts;

	// Voor verandering-detectie (alleen bijwerken als er iets wijzigt -> geen per-frame werk).
	int32 LastGrams = -1;
	int32 LastMaxG = -2;
};
