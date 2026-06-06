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
	UPROPERTY() TObjectPtr<class UWeedActionButton> PayCashBtn;
	UPROPERTY() TObjectPtr<class UWeedActionButton> PayBankBtn;

	FString LastSig; // alleen herbouwen bij wijziging (winkel, geld, betaalmodus)
};
