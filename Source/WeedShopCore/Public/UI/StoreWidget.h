// UStoreWidget — fullscreen fysieke winkel (geen telefoon). Opent als je een winkel-balie aanspreekt.
// Toont de artikelen + prijzen van die winkel; betaal met CASH of BANK (knop), geen shipping, je krijgt
// het artikel direct. Server-authoritative koop via de telefoon-component.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "StoreWidget.generated.h"

class UPhoneClientComponent;
class UCanvasPanel;
class UWidget;
class UScrollBox;
class UTextBlock;
class UBorder;

UCLASS()
class WEEDSHOPCORE_API UStoreWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	void SetPhone(UPhoneClientComponent* InPhone);

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float DeltaTime) override;

	void BuildShell(UCanvasPanel* Root);
	void FillBody();

	TWeakObjectPtr<UPhoneClientComponent> PhoneComp;

	UPROPERTY() TObjectPtr<UWidget> Card;
	UPROPERTY() TObjectPtr<UTextBlock> TitleText;
	UPROPERTY() TObjectPtr<UScrollBox> ItemList;
	UPROPERTY() TObjectPtr<class UHorizontalBox> TabRow;
	UPROPERTY() TObjectPtr<UTextBlock> CartText;
	UPROPERTY() TObjectPtr<UTextBlock> BalanceText;
	UPROPERTY() TObjectPtr<class UWeedActionButton> PayCashBtn;
	UPROPERTY() TObjectPtr<class UWeedActionButton> PayBankBtn;
	UPROPERTY() TObjectPtr<class UWeedActionButton> CheckoutBtn;

	int32 ActiveCat = -1;       // -1 = nog niet gekozen (eerste tab)
	bool bConfirmPending = false; // checkout: tweede klik bevestigt
	TMap<FName, int32> Cart;    // winkelmand: catalog-id -> aantal

	// Persistente rij-pool -> per FillBody alleen gewijzigde rijen bijwerken (geen ClearChildren -> geen flash/scroll-sprong bij cart +/-).
	UPROPERTY() TArray<TObjectPtr<UBorder>> StoreRowBoxes;
	TArray<FString> StoreRowSigs;
	// Per-rij sub-refs: cart/prijs-wijzigingen gaan in-place (SetText/SetVisibility, hover-state blijft staan);
	// SetContent alleen bij een STRUCTURELE wissel (ander item op die rij-positie, zie StoreRowIds).
	TArray<FName> StoreRowIds;
	UPROPERTY() TArray<TObjectPtr<UTextBlock>> RowNameTexts;
	UPROPERTY() TArray<TObjectPtr<UTextBlock>> RowDescTexts;
	UPROPERTY() TArray<TObjectPtr<UTextBlock>> RowPriceTexts;
	UPROPERTY() TArray<TObjectPtr<UTextBlock>> RowLockTexts;
	UPROPERTY() TArray<TObjectPtr<UTextBlock>> RowQtyTexts;
	UPROPERTY() TArray<TObjectPtr<class UWeedActionButton>> RowMinusBtns;
	UPROPERTY() TArray<TObjectPtr<class UWeedActionButton>> RowPlusBtns;

	// Tab-knoppen-pool: 1x gebouwd per tab-set (shop-kind); tab-klik herkleurt alleen in-place (idem AtmWidget::ApplyTab).
	UPROPERTY() TArray<TObjectPtr<class UWeedActionButton>> TabButtons;
	TArray<int32> TabButtonCats; // categorie per tab-knop
	FString LastTabSig; // tab-rij alleen herbouwen als de tab-SET wijzigt (andere winkel-soort), niet bij tab-klik

	// Persistent checkout-label: tekst wisselt via SetText (geen SetContent per refresh).
	UPROPERTY() TObjectPtr<UTextBlock> CheckoutLabel;

	int32 CartTotalCents() const;
	void CartAdd(FName Id, int32 Delta);

	FString LastSig; // alleen herbouwen bij wijziging (winkel, tab, mand, betaalmodus)
};
