// UAtmWidget — apart ATM-scherm (UMG) met een echte geldautomaat-look. Twee tabs: DEPOSIT (cash ->
// bank witwassen, belast, dag-limiet, heat) en SEND (geld naar een co-op vriend tegen een fee, met een
// dagelijkse transactielimiet). Wordt geopend door de ATM in de wereld (AAtm).

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "AtmWidget.generated.h"

class UPhoneClientComponent;
class UCanvasPanel;
class UWidget;
class UVerticalBox;
class UWeedActionButton;
class UTextBlock;

UCLASS()
class WEEDSHOPCORE_API UAtmWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	void SetPhone(UPhoneClientComponent* InPhone);

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float DeltaTime) override;

	void BuildShell(UCanvasPanel* Root);

	// Zet de actieve-tab-kleur op de 2 tab-knoppen + toont de bijbehorende pane (geen teardown).
	void ApplyTab();
	// Ververst alleen de tekst-waardes (saldo's, dag-limiet, transfers-left) op persistente TextBlocks.
	void RefreshValues();

	TWeakObjectPtr<UPhoneClientComponent> PhoneComp;

	UPROPERTY() TObjectPtr<UWidget> Card;
	UPROPERTY() TObjectPtr<UVerticalBox> Body;

	// Persistente widgets — één keer gebouwd in BuildShell, daarna alleen in-place bijgewerkt.
	UPROPERTY() TObjectPtr<UTextBlock> CashText;
	UPROPERTY() TObjectPtr<UTextBlock> BankText;
	UPROPERTY() TArray<TObjectPtr<UWeedActionButton>> TabButtons;   // [0]=Deposit, [1]=Send
	UPROPERTY() TObjectPtr<UVerticalBox> DepositPane;
	UPROPERTY() TObjectPtr<UVerticalBox> TransferPane;
	UPROPERTY() TObjectPtr<UTextBlock> DailyRoomText;               // "Daily room left: ..."
	UPROPERTY() TObjectPtr<UTextBlock> TransfersLeftText;           // "Transfers left today: ..."
	UPROPERTY() TObjectPtr<UTextBlock> OutOfServiceText;            // getoond als er geen Economy is

	int32 AtmTab = 0;     // 0 = Deposit, 1 = Send
	FString LastSig;      // ververs alleen bij wijziging
};
