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
	void FillBody();

	TWeakObjectPtr<UPhoneClientComponent> PhoneComp;

	UPROPERTY() TObjectPtr<UWidget> Card;
	UPROPERTY() TObjectPtr<UVerticalBox> Body;

	int32 AtmTab = 0;     // 0 = Deposit, 1 = Send
	FString LastSig;      // herbouw alleen bij wijziging
};
