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
	void BuildUnpackSection(); // uitpak-rijen onderaan de body (onder de pack-knop)

	TWeakObjectPtr<UPhoneClientComponent> PhoneComp;

	UPROPERTY() TObjectPtr<UWidget> Card;
	UPROPERTY() TObjectPtr<UVerticalBox> Body;
	UPROPERTY() TObjectPtr<class USlider> GramSlider;
	UPROPERTY() TObjectPtr<class UTextBlock> GramLabel;
	UPROPERTY() TObjectPtr<class UTextBlock> PackBtnLabel;

	UPROPERTY() TObjectPtr<class UTextBlock> TabBtnLabel; // header-knop naast Close: "Unpack bags" / "Back to packing"
	bool bUnpackTab = false; // true = aparte unpack-tab (alleen de uitpak-lijst); false = de normale pack-flow

	FName SelStrain;       // gekozen gedroogde Bud_<strain>
	FName SelContainer;    // gekozen container (Cont_*)
	int32 SelBags = 1;     // gekozen aantal bags (1..MaxBags)
	int32 MaxBags = 1;     // max bags = containers die je hebt vs wiet die je hebt
	int32 PackCap = 1;     // capaciteit (gram) van de gekozen container
	int32 PackBudHave = 0; // beschikbare gram van de gekozen strain
	FString LastSig;       // herbouw alleen bij wijziging
};
