// UDryingRackWidget — droogrek-scherm. Links de batches die hangen te drogen (met progress-bar + tijd
// resterend) en de klare batches (Collect). Rechts je natte wiet uit je inventory (Hang). Server-
// authoritative via de telefoon. De progress-bars updaten elke tick; rijen worden alleen herbouwd als
// de set batches/voorraad wijzigt.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "DryingRackWidget.generated.h"

class UPhoneClientComponent;
class UCanvasPanel;
class UWidget;
class UScrollBox;
class UTextBlock;
class UProgressBar;

UCLASS()
class WEEDSHOPCORE_API UDryingRackWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	void SetPhone(UPhoneClientComponent* InPhone);

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float DeltaTime) override;

	void BuildShell(UCanvasPanel* Root);
	void FillBody();
	void UpdateProgress(); // elke tick: bars + resterende-tijd labels bijwerken

	TWeakObjectPtr<UPhoneClientComponent> PhoneComp;

	UPROPERTY() TObjectPtr<UWidget> Card;
	UPROPERTY() TObjectPtr<UScrollBox> DryList; // links: drogende + klare batches
	UPROPERTY() TObjectPtr<UScrollBox> WetList; // rechts: natte wiet uit inventory
	UPROPERTY() TObjectPtr<UTextBlock> TitleText;

	// Per drogende-batch-rij: progress-bar + statuslabel + de batch-index in de rack-Entries.
	UPROPERTY() TArray<TObjectPtr<UProgressBar>> RowBars;
	UPROPERTY() TArray<TObjectPtr<UTextBlock>> RowStatus;
	TArray<int32> RowEntryIndex;

	FString LastSig; // herbouw alleen bij wijziging
};
