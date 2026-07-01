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

	// Persistente rij-pool -> per FillBody alleen gewijzigde rijen vervangen (geen ClearChildren -> geen flash/scroll-sprong bij cart +/-).
	UPROPERTY() TArray<TObjectPtr<UBorder>> StoreRowBoxes;
	TArray<FString> StoreRowSigs;
	FString LastTabSig; // tabs alleen herbouwen als de tab-set/actieve tab wijzigt

	int32 CartTotalCents() const;
	void CartAdd(FName Id, int32 Delta);

	FString LastSig; // alleen herbouwen bij wijziging (winkel, tab, mand, betaalmodus)
};
