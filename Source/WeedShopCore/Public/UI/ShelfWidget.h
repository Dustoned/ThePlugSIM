// UShelfWidget — opslag-schap-menu. Twee kolommen: links de inhoud van het schap (Take), rechts je
// eigen voorraad (Store). Stacks behouden hun THC/kwaliteit. Server-authoritative via de telefoon.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "ShelfWidget.generated.h"

class UPhoneClientComponent;
class UCanvasPanel;
class UWidget;
class UScrollBox;

UCLASS()
class WEEDSHOPCORE_API UShelfWidget : public UUserWidget
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
	UPROPERTY() TObjectPtr<UScrollBox> ShelfList; // links: schap-inhoud
	UPROPERTY() TObjectPtr<UScrollBox> InvList;    // rechts: je eigen voorraad
	UPROPERTY() TObjectPtr<class UTextBlock> TitleText;

	FString LastSig; // herbouw alleen bij wijziging
};
