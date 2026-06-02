// UPackWidget — verpak-menu van de bench. Kies een GEDROOGDE strain + een bakje/jar dat je hebt, klik
// Pack: er gaat tot de container-capaciteit gram in een zakje en dat wordt verkoopbare Bag_<strain>-
// voorraad. Losse/natte wiet kun je niet aan klanten verkopen — alleen verpakt.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "PackWidget.generated.h"

class UPhoneClientComponent;
class UCanvasPanel;
class UWidget;
class UVerticalBox;

UCLASS()
class WEEDSHOPCORE_API UPackWidget : public UUserWidget
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
	UPROPERTY() TObjectPtr<class USlider> GramSlider;
	UPROPERTY() TObjectPtr<class UTextBlock> GramLabel;
	UPROPERTY() TObjectPtr<class UTextBlock> PackBtnLabel;

	FName SelStrain;       // gekozen gedroogde Bud_<strain>
	FName SelContainer;    // gekozen container (Cont_*)
	int32 SelGrams = 1;    // gekozen aantal gram (1..capaciteit)
	int32 CurCap = 1;      // huidige max (container-cap geklemd op beschikbare wiet)
	FString LastSig;       // herbouw alleen bij wijziging
};
