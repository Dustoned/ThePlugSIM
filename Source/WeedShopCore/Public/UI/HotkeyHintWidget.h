// UHotkeyHintWidget — vast hulplijstje rechtsonder met de toetsen die je NU kunt gebruiken (zoals
// het Fortnite-bouwmenu, maar rechtsonder). De lijst past zich aan de context aan: lopen, een item
// in de hand, mikken op een pot/klant, plaats-modus, of een open UI. Alleen-lezen (niet klikbaar).

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "HotkeyHintWidget.generated.h"

class UCanvasPanel;
class UVerticalBox;
class UBorder;

UCLASS()
class WEEDSHOPCORE_API UHotkeyHintWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	// Aan/uit-instelling (GConfig); standaard aan. Bestuurt of de controls-overlay zichtbaar is.
	static bool AreHintsEnabled();
	static void SetHintsEnabled(bool bEnabled);

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float DeltaTime) override;

	void BuildShell(UCanvasPanel* Root);

	// Bouwt 1x een POOL van hint-rijen (toets-chip + omschrijving); de tick vult ze in-place (SetText/SetVisibility)
	// i.p.v. ClearChildren+rebuild -> geen teardown-flits meer.
	void BuildRowPool(int32 Count);

	UPROPERTY() TObjectPtr<UBorder> Card;
	UPROPERTY() TObjectPtr<UVerticalBox> List;
	UPROPERTY() TArray<TObjectPtr<class UWidget>> RowPool;        // de rij-containers (HBox), Collapsed als ongebruikt
	UPROPERTY() TArray<TObjectPtr<class UTextBlock>> RowLabels;   // omschrijving-tekst per rij (in-place SetText)
	UPROPERTY() TArray<TObjectPtr<class UTextBlock>> RowKeys;     // toets-chip-tekst per rij (in-place SetText)

	// Gecentreerde interactie-popup (onder het crosshair) i.p.v. de lange prompt in de hoek-kaart.
	UPROPERTY() TObjectPtr<UBorder> CenterPromptCard;
	UPROPERTY() TObjectPtr<class UTextBlock> CenterPromptText;

	// Laatst getoonde set (om alleen te herbouwen als de context wijzigt -> geen flicker).
	FString LastSig;

	// Perf: component-cache (weak + pawn-check) -> geen 4x FindComponentByClass per tick.
	TWeakObjectPtr<APawn> CachedCompPawn;
	TWeakObjectPtr<class UPhoneClientComponent> CachedPhone;
	TWeakObjectPtr<class UBuildComponent> CachedBuild;
	TWeakObjectPtr<class UInteractionComponent> CachedInteract;
	TWeakObjectPtr<class UInventoryComponent> CachedInv;
	// FocusPrompt changed-check: SetText/visibility alleen bij wijziging.
	FString LastFocusPrompt;

	// Focus-HOLD: de raw focus-trace klapt actor<->null als een crowd-body langs de crosshair drijft; die 1-frame
	// misses gooiden de hint-set (en dus de sig) om -> constante ClearChildren-rebuild = de flits. We houden de
	// laatste geldige focus kort vast zodat een korte trace-miss de rijen niet omgooit.
	TWeakObjectPtr<class AActor> HeldFocus;
	float FocusHoldTimer = 0.f;
};
