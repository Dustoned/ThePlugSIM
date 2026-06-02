// UHotbarWidget — altijd-zichtbare hotbar (UMG) onderaan: 8 slots met item + aantal + actieve markering.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "HotbarWidget.generated.h"

class UBorder;
class UTextBlock;
class UCanvasPanel;
class USizeBox;
class UInvCell;

UCLASS()
class WEEDSHOPCORE_API UHotbarWidget : public UUserWidget
{
	GENERATED_BODY()

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float DeltaTime) override;

	void BuildShell(UCanvasPanel* Root);

	UPROPERTY() TArray<TObjectPtr<UBorder>> SlotBoxes;
	UPROPERTY() TArray<TObjectPtr<USizeBox>> SlotIconBoxes; // icoon-container per slot (inhoud wisselt)
	UPROPERTY() TArray<TObjectPtr<UTextBlock>> SlotNames;
	UPROPERTY() TArray<TObjectPtr<UTextBlock>> SlotBadges;  // aantal/gram-tekst
	UPROPERTY() TArray<TObjectPtr<UBorder>> SlotBadgePills; // pill-achtergrond (verbergen als leeg)
	UPROPERTY() TArray<TObjectPtr<UInvCell>> DropCells;     // transparante sleep/drop-cel per slot
	TArray<FName> SlotLastIcon;                             // laatst getoonde item-id (om icoon niet elke tick te herbouwen)
};
