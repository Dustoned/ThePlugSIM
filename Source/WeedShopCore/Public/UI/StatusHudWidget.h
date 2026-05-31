// UStatusHudWidget — altijd zichtbaar status-paneel linksboven (UMG), zelfde clean stijl als de
// telefoon: afgeronde kaart, flat-iconen en echte progress bars voor heat, level en stoned.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "StatusHudWidget.generated.h"

class UTextBlock;
class UProgressBar;
class UWidget;
class UCanvasPanel;
class UVerticalBox;

UCLASS()
class WEEDSHOPCORE_API UStatusHudWidget : public UUserWidget
{
	GENERATED_BODY()

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float DeltaTime) override;

	void BuildShell(UCanvasPanel* Root);
	// Bouwt één rij (icoon + inhoud) en levert de inhoud-container terug om in te vullen.
	class UHorizontalBox* MakeRow(UVerticalBox* Parent, int32 IconType, const FLinearColor& IconCol);

	UPROPERTY() TObjectPtr<UTextBlock> CashText;
	UPROPERTY() TObjectPtr<UTextBlock> TimeText;
	UPROPERTY() TObjectPtr<UProgressBar> HeatBar;
	UPROPERTY() TObjectPtr<UTextBlock> HeatText;
	UPROPERTY() TObjectPtr<UProgressBar> LevelBar;
	UPROPERTY() TObjectPtr<UTextBlock> LevelText;
	UPROPERTY() TObjectPtr<UWidget> StonedRow;
	UPROPERTY() TObjectPtr<UProgressBar> StonedBar;
	UPROPERTY() TObjectPtr<UTextBlock> StonedText;
};
