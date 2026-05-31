// UHotbarWidget — altijd-zichtbare hotbar (UMG) onderaan: 8 slots met item + aantal + actieve markering.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "HotbarWidget.generated.h"

class UBorder;
class UTextBlock;
class UCanvasPanel;

UCLASS()
class WEEDSHOPCORE_API UHotbarWidget : public UUserWidget
{
	GENERATED_BODY()

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float DeltaTime) override;

	void BuildShell(UCanvasPanel* Root);

	UPROPERTY() TArray<TObjectPtr<UBorder>> SlotBoxes;
	UPROPERTY() TArray<TObjectPtr<UTextBlock>> SlotNames;
	UPROPERTY() TArray<TObjectPtr<UTextBlock>> SlotInfos;
};
